#pragma once

// ============================================================================
// VectorLayer - A single layer containing vector strokes
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
// A layer is a data container for strokes with visibility/opacity control.
// No widget functionality - rendering is done by the Viewport.
// ============================================================================

#include "../strokes/VectorStroke.h"

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QPainter>
#include <QPolygonF>
#include <QPixmap>
#include <QtMath>

/**
 * @brief A single vector layer containing strokes.
 * 
 * Layers allow organizing strokes into groups that can be independently
 * shown/hidden, locked, and have opacity applied. This is similar to
 * layer systems in applications like Photoshop or SAI.
 * 
 * VectorLayer is a pure data class - it does not handle input or caching.
 * The DocumentViewport handles rendering with caching optimizations.
 */
class VectorLayer {
public:
    // ===== Layer Properties =====
    QString id;                     ///< UUID for tracking
    QString name = "Layer 1";       ///< User-visible layer name
    bool visible = true;            ///< Whether layer is rendered
    qreal opacity = 1.0;            ///< Layer opacity (0.0 to 1.0)
    bool locked = false;            ///< If true, layer cannot be edited
    
    /**
     * @brief Default constructor.
     * Creates a layer with a unique ID.
     */
    VectorLayer() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    /**
     * @brief Constructor with name.
     * @param layerName The display name for this layer.
     */
    explicit VectorLayer(const QString& layerName) : name(layerName) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    // ===== Stroke Management =====
    
    /**
     * @brief Add a stroke to this layer.
     * @param stroke The stroke to add.
     * 
     * If the stroke cache is valid, the new stroke is marked for incremental
     * rendering (painted on top of the existing cache) instead of triggering
     * a full cache rebuild. This makes pen-up O(1) instead of O(n) at any zoom.
     */
    void addStroke(const VectorStroke& stroke) {
        m_strokes.append(stroke);
        markStrokePending();
    }
    
    /**
     * @brief Add a stroke by moving it (more efficient for large strokes).
     * @param stroke The stroke to move into this layer.
     */
    void addStroke(VectorStroke&& stroke) {
        m_strokes.append(std::move(stroke));
        markStrokePending();
    }
    
    /**
     * @brief Remove a stroke by its ID.
     * @param strokeId The UUID of the stroke to remove.
     * @return True if stroke was found and removed.
     * 
     * If the stroke cache is valid, the removed stroke's region is patched
     * incrementally (clear + re-render overlapping strokes) instead of
     * rebuilding the entire cache. This makes eraser O(k) where k is the
     * number of strokes overlapping the erased one, instead of O(n) for all.
     */
    bool removeStroke(const QString& strokeId) {
        for (int i = static_cast<int>(m_strokes.size()) - 1; i >= 0; --i) {
            if (m_strokes[i].id == strokeId) {
                QRectF removedBounds = m_strokes[i].boundingBox;
                m_strokes.removeAt(i);
                patchCacheAfterRemoval(removedBounds);
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Get all strokes (const reference).
     * @return Vector of strokes in this layer.
     */
    const QVector<VectorStroke>& strokes() const { return m_strokes; }
    
    /**
     * @brief Get all strokes (mutable reference for modification).
     * @return Mutable vector of strokes.
     */
    QVector<VectorStroke>& strokes() { return m_strokes; }
    
    /**
     * @brief Get the number of strokes in this layer.
     */
    int strokeCount() const { return static_cast<int>(m_strokes.size()); }
    
    /**
     * @brief Check if layer has any strokes.
     */
    bool isEmpty() const { return m_strokes.isEmpty(); }
    
    /**
     * @brief Clear all strokes from this layer.
     */
    void clear() { 
        m_strokes.clear(); 
        invalidateStrokeCache();  // Cache needs rebuild
    }
    
    // ===== Hit Testing =====
    
    /**
     * @brief Find all strokes that contain a given point (for eraser).
     * @param pt The point to test.
     * @param tolerance Additional radius around the point.
     * @return List of stroke IDs that contain the point.
     */
    QVector<QString> strokesAtPoint(const QPointF& pt, qreal tolerance) const {
        QVector<QString> result;
        for (const auto& stroke : m_strokes) {
            if (stroke.containsPoint(pt, tolerance)) {
                result.append(stroke.id);
            }
        }
        return result;
    }
    
    /**
     * @brief Calculate bounding box of all strokes in this layer.
     * @return Bounding rectangle, or empty rect if layer is empty.
     */
    QRectF boundingBox() const {
        if (m_strokes.isEmpty()) {
            return QRectF();
        }
        QRectF bounds = m_strokes[0].boundingBox;
        for (int i = 1; i < m_strokes.size(); ++i) {
            bounds = bounds.united(m_strokes[i].boundingBox);
        }
        return bounds;
    }
    
    // ===== Rendering =====
    
    /**
     * @brief Result of building a stroke polygon.
     * 
     * Contains the filled polygon representing the stroke outline, plus
     * information about round end caps if needed. This is used by both
     * QPainter rendering and PDF export.
     */
    struct StrokePolygonResult {
        QPolygonF polygon;              ///< The filled polygon outline
        bool isSinglePoint = false;     ///< True if stroke is just a dot
        bool hasRoundCaps = false;      ///< True if round end caps should be drawn
        QPointF startCapCenter;         ///< Center of start cap ellipse
        qreal startCapRadius = 0;       ///< Radius of start cap
        QPointF endCapCenter;           ///< Center of end cap ellipse
        qreal endCapRadius = 0;         ///< Radius of end cap
    };
    
    /**
     * @brief Build the filled polygon for a stroke (reusable for rendering and export).
     * @param stroke The stroke to convert.
     * @return StrokePolygonResult containing polygon and cap information.
     * 
     * This extracts the polygon generation logic so it can be used by:
     * - QPainter rendering (VectorLayer::renderStroke)
     * - PDF export (MuPdfExporter - converts to MuPDF paths)
     * 
     * The stored stroke points are first smoothed with Catmull-Rom interpolation
     * (see catmullRomSubdivide) to produce a dense, smooth point sequence. This
     * eliminates the visible polyline edges that would otherwise appear at high zoom.
     * 
     * The polygon represents the variable-width stroke outline:
     * - Left edge goes forward along the stroke
     * - Right edge goes backward
     * - This creates a closed shape that can be filled
     * - Round caps are drawn separately as ellipses
     */
    static StrokePolygonResult buildStrokePolygon(const VectorStroke& stroke) {
        StrokePolygonResult result;
        
        if (stroke.points.size() < 2) {
            // Single point - just a dot
            if (stroke.points.size() == 1) {
                result.isSinglePoint = true;
                result.startCapCenter = stroke.points[0].pos;
                // Minimum stroke width is now enforced at capture time in
                // DocumentViewport (per pen preset), so the stored per-point
                // pressure already embeds the floor.  No qMax() needed here.
                qreal width = stroke.baseThickness * stroke.points[0].pressure;
                result.startCapRadius = width / 2.0;
            }
            return result;
        }
        
        // Smooth the stroke points with Catmull-Rom interpolation.
        // This inserts intermediate points along a smooth curve between each
        // pair of stored points, eliminating the visible polyline edges that
        // appear when zoomed in. For 2-point strokes (straight lines),
        // catmullRomSubdivide returns them unchanged.
        const QVector<StrokePoint>& pts = catmullRomSubdivide(stroke.points);
        const int n = static_cast<int>(pts.size());
        
        // Pre-calculate half-widths for each point
        QVector<qreal> halfWidths(n);
        for (int i = 0; i < n; ++i) {
            // See comment above: minimum-width floor is applied in
            // DocumentViewport at capture time, not here.
            qreal width = stroke.baseThickness * pts[i].pressure;
            halfWidths[i] = width / 2.0;
        }
        
        // Build the stroke outline polygon
        // Left edge goes forward, right edge goes backward
        QVector<QPointF> leftEdge(n);
        QVector<QPointF> rightEdge(n);
        
        for (int i = 0; i < n; ++i) {
            const QPointF& pos = pts[i].pos;
            qreal hw = halfWidths[i];
            
            // Calculate perpendicular direction
            QPointF tangent;
            if (i == 0) {
                // First point: use direction to next point
                tangent = pts[1].pos - pos;
            } else if (i == n - 1) {
                // Last point: use direction from previous point
                tangent = pos - pts[n - 2].pos;
            } else {
                // Middle points: average of incoming and outgoing directions
                tangent = pts[i + 1].pos - pts[i - 1].pos;
            }
            
            // Normalize tangent
            qreal len = qSqrt(tangent.x() * tangent.x() + tangent.y() * tangent.y());
            if (len < 0.0001) {
                // Degenerate case: use arbitrary perpendicular
                tangent = QPointF(1.0, 0.0);
                len = 1.0;
            }
            tangent /= len;
            
            // Perpendicular vector (rotate 90 degrees)
            QPointF perp(-tangent.y(), tangent.x());
            
            // Calculate left and right edge points
            leftEdge[i] = pos + perp * hw;
            rightEdge[i] = pos - perp * hw;
        }
        
        // Build polygon: left edge forward, then right edge backward
        result.polygon.reserve(n * 2);
        
        for (int i = 0; i < n; ++i) {
            result.polygon << leftEdge[i];
        }
        for (int i = n - 1; i >= 0; --i) {
            result.polygon << rightEdge[i];
        }
        
        // Set up round cap information
        // Use first/last smoothed points (which equal the original stroke endpoints,
        // since Catmull-Rom passes through its control points)
        result.hasRoundCaps = true;
        result.startCapCenter = pts.first().pos;
        result.startCapRadius = halfWidths[0];
        result.endCapCenter = pts.last().pos;
        result.endCapRadius = halfWidths[n - 1];
        
        return result;
    }
    
    /**
     * @brief Render all strokes in this layer.
     * @param painter The QPainter to render to (should have antialiasing enabled).
     * 
     * Note: This does not apply layer opacity - the caller (Viewport) should
     * handle opacity by rendering to an intermediate pixmap if opacity < 1.0.
     */
    void render(QPainter& painter) const {
        if (!visible || m_strokes.isEmpty()) {
            return;
        }

        // renderStroke mutates pen/brush on the outer painter and never
        // restores them. Wrap the loop so callers (Page::render, thumbnail
        // and export paths in MainWindow) see a clean painter state on return,
        // matching the contract of renderExcluding.
        painter.save();
        for (const auto& stroke : m_strokes) {
            renderStroke(painter, stroke);
        }
        painter.restore();
    }
    
    /**
     * @brief Render a single stroke (static helper for shared use).
     * @param painter The QPainter to render to.
     * @param stroke The stroke to render.
     * 
     * This uses the optimized filled-polygon rendering for variable-width strokes.
     * Can be used by VectorCanvas, VectorLayer, or any other component.
     * 
     * For semi-transparent strokes with round caps, renders to a temp buffer at
     * full opacity then blits with the stroke's alpha to avoid alpha compounding
     * where the caps overlap the stroke body.
     */
    static void renderStroke(QPainter& painter, const VectorStroke& stroke) {
        StrokePolygonResult poly = buildStrokePolygon(stroke);
        
        if (poly.isSinglePoint) {
            // Single point - draw a dot (no alpha compounding issue)
            painter.setPen(Qt::NoPen);
            painter.setBrush(stroke.color);
            painter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
            return;
        }
        
        if (poly.polygon.isEmpty()) {
            return;
        }
        
        // Check if we need special handling for semi-transparent strokes with round caps
        // The issue: polygon body + cap ellipses overlap, causing alpha compounding
        // The fix: render everything to temp buffer at full opacity, then blit with alpha
        int strokeAlpha = stroke.color.alpha();
        bool needsAlphaCompositing = (strokeAlpha < 255) && poly.hasRoundCaps;
        
        if (needsAlphaCompositing) {
            // Calculate bounding rect for the temp buffer
            QRectF bounds = stroke.boundingBox;
            if (bounds.isEmpty() || !bounds.isValid()) {
                bounds = poly.polygon.boundingRect();
            }
            qreal maxRadius = qMax(poly.startCapRadius, poly.endCapRadius);
            bounds.adjust(-maxRadius - 2, -maxRadius - 2, maxRadius + 2, maxRadius + 2);
            
            // Map bounds through the painter's transform to get actual output size.
            // Without this, the buffer uses input-space dimensions (e.g., full page
            // coords) even when the painter scales down to thumbnail size, wasting
            // up to 9x memory per semi-transparent stroke.
            QTransform xform = painter.transform();
            QRectF mappedBounds = xform.mapRect(bounds);
            
            qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
            int bufW = static_cast<int>(mappedBounds.width() * dpr) + 2;
            int bufH = static_cast<int>(mappedBounds.height() * dpr) + 2;
            
            if (bufW <= 0 || bufH <= 0 || bufW > 10000 || bufH > 10000) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(stroke.color);
                painter.drawPolygon(poly.polygon, Qt::WindingFill);
                painter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
                painter.drawEllipse(poly.endCapCenter, poly.endCapRadius, poly.endCapRadius);
                return;
            }
            
            QPixmap tempBuffer(bufW, bufH);
            tempBuffer.setDevicePixelRatio(dpr);
            tempBuffer.fill(Qt::transparent);
            
            // Transform chain: input coords → device logical coords (xform)
            //                  → buffer coords (translate by -mappedBounds.topLeft)
            QPainter tempPainter(&tempBuffer);
            tempPainter.setRenderHint(QPainter::Antialiasing, true);
            tempPainter.translate(-mappedBounds.topLeft());
            tempPainter.setTransform(xform, true);
            
            QColor opaqueColor = stroke.color;
            opaqueColor.setAlpha(255);
            tempPainter.setPen(Qt::NoPen);
            tempPainter.setBrush(opaqueColor);
            
            tempPainter.drawPolygon(poly.polygon, Qt::WindingFill);
            tempPainter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
            tempPainter.drawEllipse(poly.endCapCenter, poly.endCapRadius, poly.endCapRadius);
            tempPainter.end();
            
            // Blit in device logical coordinates (bypass the input→output transform)
            painter.save();
            painter.resetTransform();
            painter.setOpacity(strokeAlpha / 255.0);
            painter.drawPixmap(mappedBounds.topLeft(), tempBuffer);
            painter.restore();
        } else {
            // Standard rendering for opaque strokes (no alpha compounding issue)
            painter.setPen(Qt::NoPen);
            painter.setBrush(stroke.color);
            
            // Draw filled polygon with WindingFill to handle self-intersections
            painter.drawPolygon(poly.polygon, Qt::WindingFill);
            
            // Draw round end caps
            if (poly.hasRoundCaps) {
                painter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
                painter.drawEllipse(poly.endCapCenter, poly.endCapRadius, poly.endCapRadius);
            }
        }
    }
    
    // ===== Serialization =====
    
    /**
     * @brief Serialize layer to JSON.
     * @return JSON object containing layer data.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["visible"] = visible;
        obj["opacity"] = opacity;
        obj["locked"] = locked;
        
        QJsonArray strokesArray;
        for (const auto& stroke : m_strokes) {
            strokesArray.append(stroke.toJson());
        }
        obj["strokes"] = strokesArray;
        
        return obj;
    }
    
    /**
     * @brief Deserialize layer from JSON.
     * @param obj JSON object containing layer data.
     * @return VectorLayer with values from JSON.
     */
    static VectorLayer fromJson(const QJsonObject& obj) {
        VectorLayer layer;
        layer.id = obj["id"].toString();
        layer.name = obj["name"].toString("Layer");
        layer.visible = obj["visible"].toBool(true);
        layer.opacity = obj["opacity"].toDouble(1.0);
        layer.locked = obj["locked"].toBool(false);
        
        // Generate UUID if missing (for backwards compatibility)
        if (layer.id.isEmpty()) {
            layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        
        QJsonArray strokesArray = obj["strokes"].toArray();
        for (const auto& val : strokesArray) {
            layer.m_strokes.append(VectorStroke::fromJson(val.toObject()));
        }
        
        return layer;
    }
    
    // ===== Stroke Cache (Task 1.3.7 + Zoom-Aware Update) =====

    /// Long-side cap (physical pixels) for the per-page/per-tile stroke
    /// pixmap cache. When a page would exceed this at the current zoom,
    /// `computeCacheDivisor` returns >1 and the pixmap is downsampled - this
    /// is the source of the "blurry zoom-in" at extreme zooms. Exposed so
    /// `DocumentViewport::chooseRenderTier` can decide when to switch the
    /// focused page/tile to the viewport-clipped focus cache.
    static constexpr int MAX_STROKE_CACHE_DIM = 4096;

    /// Render-tier dispatch for `renderTiered` / `renderExcludingTiered`.
    /// Selected per-paint per-tile by `DocumentViewport::chooseRenderTier`.
    /// - Capped: legacy whole-page pixmap, capped via `computeCacheDivisor`.
    ///   Used on context (off-screen-ish) tiles, and on every tile when
    ///   effective scale doesn't exceed the cap.
    /// - Focus: viewport-clipped pixmap covering only `focusRect`. Used on
    ///   the focused tile at high zoom while pan/zoom is settled.
    /// - Direct: cache-free, painter clipped to `focusRect`, per-stroke
    ///   bbox cull. Used on the focused tile while pan/zoom is in flight
    ///   (avoids rebuilding the focus pixmap every frame).
    enum class RenderTier { Capped, Focus, Direct };

    /**
     * @brief Ensure stroke cache is valid for the given size, zoom, and DPI.
     * @param size The target size in logical pixels (page size).
     * @param zoom Current zoom level (1.0 = 100%).
     * @param dpr Device pixel ratio for high DPI support.
     * 
     * Cache is built at size * zoom * dpr for sharp rendering at current zoom,
     * capped to MAX_STROKE_CACHE_DIM per dimension using integer-divisor scaling
     * to prevent extreme memory usage at high zoom levels.
     * If the cache is valid but has pending strokes (from addStroke), those are
     * rendered incrementally without rebuilding the entire cache.
     * If cache is invalid, wrong size, or wrong zoom, rebuilds from scratch.
     */
    void ensureStrokeCacheValid(const QSizeF& size, qreal zoom, qreal dpr) {
        int divisor = computeCacheDivisor(size, zoom, dpr);
        QSize physicalSize = cappedPhysicalSize(size, zoom, dpr, divisor);
        
        // Fast path: cache is valid and has pending strokes to append
        if (m_pendingStrokeStart >= 0 && !m_strokeCacheDirty &&
            m_strokeCache.size() == physicalSize &&
            m_cacheDivisor == divisor &&
            qFuzzyCompare(m_cacheZoom, zoom) &&
            qFuzzyCompare(m_cacheDpr, dpr)) {
            appendPendingStrokes();
            return;
        }
        
        // Check if cache is fully valid (no pending, no dirty)
        if (!m_strokeCacheDirty && m_pendingStrokeStart < 0 &&
            m_strokeCache.size() == physicalSize &&
            m_cacheDivisor == divisor &&
            qFuzzyCompare(m_cacheZoom, zoom) &&
            qFuzzyCompare(m_cacheDpr, dpr)) {
            return;  // Cache is valid
        }
        
        // Full rebuild needed (dirty, size changed, or zoom changed)
        m_pendingStrokeStart = -1;
        rebuildStrokeCache(size, zoom, dpr);
    }
    
    // Backward-compatible overload (assumes zoom = 1.0)
    void ensureStrokeCacheValid(const QSizeF& size, qreal dpr) {
        ensureStrokeCacheValid(size, 1.0, dpr);
    }
    
    /**
     * @brief Check if stroke cache is valid.
     */
    bool isStrokeCacheValid() const { return !m_strokeCacheDirty && !m_strokeCache.isNull(); }
    
    /**
     * @brief Check if stroke cache matches the given zoom level.
     * @param zoom The zoom level to check against.
     * @return True if cache was built at this zoom level.
     */
    bool isCacheValidForZoom(qreal zoom) const { 
        return !m_strokeCacheDirty && !m_strokeCache.isNull() && qFuzzyCompare(m_cacheZoom, zoom);
    }
    
    /**
     * @brief Invalidate stroke cache (call when strokes change destructively).
     * Note: This only marks the cache dirty, it does NOT free memory.
     * Used by removeStroke() and clear(). addStroke() uses incremental updates instead.
     */
    void invalidateStrokeCache() {
        m_strokeCacheDirty = true;
        m_pendingStrokeStart = -1;  // Incremental update no longer possible
        // The focus cache is sourced from the same stroke list, so any
        // destructive change invalidates it too.
        invalidateFocusCache();
    }
    
    /**
     * @brief Repair the caches after a bulk removal confined to one region.
     * @param removedBounds Region covering every removed stroke (page/tile coords).
     *
     * Bulk removals (lasso erase) strip strokes from the vector directly instead
     * of going through removeStroke(), so they have to repair the caches
     * themselves. Patching the region the removals were confined to costs only
     * the strokes overlapping it, where invalidateStrokeCache() would discard
     * both caches and force a full rebuild on the next paint.
     */
    void patchCacheAfterRemovals(const QRectF& removedBounds) {
        patchCacheAfterRemoval(removedBounds);
    }
    
    /**
     * @brief Release stroke cache memory completely.
     * Call this for pages that are far from the visible area to save memory.
     * The cache will be rebuilt lazily when the page becomes visible again.
     */
    void releaseStrokeCache() {
        m_strokeCache = QPixmap();  // Actually free the pixmap memory
        m_strokeCacheDirty = true;
        m_pendingStrokeStart = -1;
        m_cacheZoom = 0;
        m_cacheDpr = 0;
        m_cacheDivisor = 1;
    }

    /**
     * @brief Check if stroke cache is currently allocated (using memory).
     * @return True if cache pixmap is allocated.
     */
    bool hasStrokeCacheAllocated() const { return !m_strokeCache.isNull(); }

    // ===== Focus Cache (viewport-clipped, high-zoom path) =====

    /**
     * @brief Check if focus cache is currently allocated (using memory).
     */
    bool hasFocusCacheAllocated() const { return !m_focusCache.isNull(); }

    /**
     * @brief Mark the focus cache dirty without freeing the pixmap.
     */
    void invalidateFocusCache() {
        m_focusCacheDirty = true;
        m_focusPendingStrokeStart = -1;
    }

    /**
     * @brief Release focus cache memory completely.
     * Called by DocumentViewport when the focused tile changes or when zoom
     * drops below the divisor-one threshold and the focus cache is no longer
     * needed.
     *
     * Marked const because all focus-cache state is `mutable` (the cache is
     * a paint-time optimisation, not part of the layer's logical state); this
     * lets `ensureFocusCacheValid` (also const) bail out cleanly on degenerate
     * inputs.
     */
    void releaseFocusCache() const {
        m_focusCache = QPixmap();
        m_focusCacheDirty = true;
        m_focusPendingStrokeStart = -1;
        m_focusZoom = 0;
        m_focusDpr  = 0;
        m_focusRect = QRectF();
    }

    /**
     * @brief Ensure the focus cache covers focusRect at (zoom, dpr).
     * @param size Owning page/tile size in logical units (only used for the
     *             outer-bounds clamp; physical size is computed from focusRect).
     * @param zoom Current zoom level.
     * @param dpr  Current device pixel ratio.
     * @param focusRect Page/tile-local rectangle to cover (logical units).
     *
     * Cache pixmap physical size = focusRect.size() * zoom * dpr (no divisor).
     * Bounded by the viewport clip in `focusRect`, so it can never exceed
     * screen-pixel scale. Validity key is `(focusRect, zoom, dpr)`; any change
     * forces a full rebuild. Incremental append is used when only new strokes
     * have been pushed and the rect/zoom/dpr have not changed.
     */
    void ensureFocusCacheValid(const QSizeF& size, qreal zoom, qreal dpr,
                               const QRectF& focusRect) const {
        Q_UNUSED(size);
        if (focusRect.isEmpty() || zoom <= 0.0 || dpr <= 0.0) {
            releaseFocusCache();
            return;
        }

        const QSize physicalSize(qMax(1, qCeil(focusRect.width()  * zoom * dpr)),
                                 qMax(1, qCeil(focusRect.height() * zoom * dpr)));
        const bool sameKey =
            qFuzzyCompare(m_focusZoom, zoom) &&
            qFuzzyCompare(m_focusDpr,  dpr)  &&
            m_focusRect == focusRect &&
            !m_focusCache.isNull() &&
            m_focusCache.size() == physicalSize;

        // Fast path: same rect/zoom/dpr, only new strokes appended since
        // last build.
        if (sameKey && !m_focusCacheDirty && m_focusPendingStrokeStart >= 0) {
            appendPendingFocusStrokes();
            return;
        }
        if (sameKey && !m_focusCacheDirty && m_focusPendingStrokeStart < 0) {
            return;  // Cache is fully valid.
        }

        // Full rebuild.
        m_focusPendingStrokeStart = -1;
        rebuildFocusCache(zoom, dpr, focusRect, physicalSize);
    }

    /**
     * @brief Patch the focus cache after removing a stroke.
     * @param removedBounds Bounding box of the removed stroke (page/tile coords).
     *
     * Mirror of `patchCacheAfterRemoval` for the focus cache; only runs when
     * the removed stroke actually intersects `m_focusRect` (otherwise the
     * stroke was outside the cached region and the cache is already correct).
     */
    void patchFocusCacheAfterRemoval(const QRectF& removedBounds) {
        if (m_focusCacheDirty || m_focusCache.isNull() ||
            removedBounds.isEmpty() || m_focusPendingStrokeStart >= 0 ||
            !m_focusRect.intersects(removedBounds)) {
            invalidateFocusCache();
            return;
        }

        // Clear-and-repaint the affected region. The pixmap has its DPR set
        // to (zoom * dpr) by rebuildFocusCache, so the focus painter operates
        // in page/tile-local logical coordinates after a single
        // translate(-m_focusRect.topLeft()).
        const QRectF clearRect = m_focusRect.intersected(removedBounds);
        QPainter p(&m_focusCache);
        beginFocusPainter(p);  // antialias + page-local coords
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(clearRect, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setClipRect(clearRect);
        for (const auto& s : m_strokes) {
            if (s.boundingBox.intersects(clearRect)) renderStroke(p, s);
        }
    }
    
    /**
     * @brief Render using zoom-aware stroke cache.
     * @param painter The QPainter to render to.
     * @param size The page size in logical pixels.
     * @param zoom Current zoom level.
     * @param dpr Device pixel ratio.
     * 
     * The cache is built at size * zoom * dpr physical pixels with devicePixelRatio
     * set to zoom * dpr. This means Qt sees the cache as having logical size = size.
     * If the painter is pre-scaled by zoom, the result is that each cache pixel maps
     * to exactly one physical screen pixel, giving sharp rendering at any zoom level.
     * New strokes are rendered incrementally to the existing cache (no full rebuild).
     */
    void renderWithZoomCache(QPainter& painter, const QSizeF& size, qreal zoom, qreal dpr) {
        if (!visible || m_strokes.isEmpty()) {
            return;
        }
        
        ensureStrokeCacheValid(size, zoom, dpr);
        
        if (!m_strokeCache.isNull()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            // Qt5: cache DPR is clamped to max(1.0, rawScale). When the
            // cache DPR was NOT clamped (rawScale >= 1.0), the pixmap's
            // logical size matches the page/tile size and drawPixmap(0,0)
            // works correctly. We MUST use this point-draw path rather than
            // the QRectF overload because the rect-to-rect mapping path in
            // Qt5's raster engine composites at fractional sub-pixel
            // positions differently, breaking the sub-pixel snap correction
            // that aligns the live stroke cache with the layer cache.
            // Only fall back to the QRectF overload when DPR was clamped
            // (rawScale < 1.0, i.e. zoomed out) where the logical size
            // mismatch requires explicit rect mapping.
            int divisor = computeCacheDivisor(size, zoom, dpr);
            if (zoom * dpr / divisor >= 1.0) {
                painter.drawPixmap(0, 0, m_strokeCache);
            } else {
                painter.drawPixmap(QRectF(0, 0, size.width(), size.height()),
                                   m_strokeCache,
                                   QRectF(0, 0, m_strokeCache.width(), m_strokeCache.height()));
            }
#else
            painter.drawPixmap(0, 0, m_strokeCache);
#endif
        } else {
            // Fallback to direct rendering (shouldn't happen)
            painter.save();
            painter.scale(zoom, zoom);
            render(painter);
            painter.restore();
        }
    }
    
    // Legacy method for backward compatibility (1:1 cache, no zoom)
    void renderWithCache(QPainter& painter, const QSizeF& size, qreal dpr) {
        renderWithZoomCache(painter, size, 1.0, dpr);
    }
    
    /**
     * @brief Render layer strokes excluding specific stroke IDs.
     * @param painter The QPainter to render to (assumed already scaled by zoom).
     * @param excludeIds Set of stroke IDs to skip during rendering.
     * 
     * CR-2B-7: Used during lasso selection to hide original strokes while
     * rendering the transformed copies separately. This bypasses the cache
     * to allow per-stroke exclusion.
     */
    void renderExcluding(QPainter& painter, const QSet<QString>& excludeIds) {
        if (!visible || m_strokes.isEmpty() || excludeIds.isEmpty()) {
            // No exclusions needed, but caller expects direct render (no cache)
            render(painter);
            return;
        }

        // renderStroke mutates pen/brush on the outer painter and never
        // restores them. Without this save/restore, the last drawn stroke's
        // brush leaks out and any later drawRect (e.g. the per-page border in
        // DocumentViewport::renderPage) fills its area with the leaked color.
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const VectorStroke& stroke : m_strokes) {
            if (!excludeIds.contains(stroke.id)) {
                renderStroke(painter, stroke);
            }
        }
        painter.restore();
    }

    // ===== Direct (cache-free) render with bbox cull =====

    /**
     * @brief Render only strokes whose boundingBox intersects clipRect.
     * @param painter Caller-supplied painter, already scaled by zoom.
     * @param clipRect Page/tile-local logical rect; only intersecting strokes
     *                 are drawn.
     *
     * Caller is responsible for `painter.setClipRect(clipRect)` if it wants
     * hard pixel clipping in addition to the bbox cull. Used by `renderTiered`
     * on the Direct tier.
     */
    void renderDirectClipped(QPainter& painter, const QRectF& clipRect) const {
        if (!visible || m_strokes.isEmpty()) return;
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const auto& s : m_strokes) {
            if (s.boundingBox.intersects(clipRect)) {
                renderStroke(painter, s);
            }
        }
    }

    /**
     * @brief Render only strokes whose boundingBox intersects clipRect, with
     *        IDs in excludeIds skipped.
     *
     * Replaces the unbounded `renderExcluding` path on the lasso source layer
     * at high zoom. Same complexity class as `renderExcluding` (one bbox test
     * per stroke), but stroke rasterisation only fires for strokes inside the
     * focus rect.
     */
    void renderDirectExcludingClipped(QPainter& painter,
                                      const QSet<QString>& excludeIds,
                                      const QRectF& clipRect) const {
        if (!visible || m_strokes.isEmpty()) return;
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const auto& s : m_strokes) {
            if (excludeIds.contains(s.id)) continue;
            if (s.boundingBox.intersects(clipRect)) {
                renderStroke(painter, s);
            }
        }
    }

    // ===== Tiered render dispatch =====

    /**
     * @brief Render this layer at one of three tiers chosen by the viewport.
     *
     * Capped: legacy whole-page pixmap (`renderWithZoomCache`).
     * Focus:  viewport-clipped pixmap covering only `focusRect`.
     * Direct: cache-free, painter clipped to `focusRect`, per-stroke bbox cull.
     *
     * Caller (DocumentViewport) is responsible for releasing the capped cache
     * (`releaseStrokeCache`) on tiles where `tier != Capped` to actually free
     * the memory; this dispatcher only chooses what to draw.
     */
    void renderTiered(QPainter& painter, const QSizeF& size,
                      qreal zoom, qreal dpr,
                      RenderTier tier,
                      const QRectF& focusRect = QRectF()) {
        if (!visible || m_strokes.isEmpty()) return;
        // Symmetric to DocumentViewport releasing the capped cache before
        // calling us with tier != Capped: when the dispatcher picks Capped,
        // any leftover focus pixmap (from when this tile was on-screen at
        // high zoom) is dead weight, so free it eagerly.
        if (tier == RenderTier::Capped && hasFocusCacheAllocated()) {
            releaseFocusCache();
        }
        switch (tier) {
        case RenderTier::Capped:
            // Delegate to the existing path; preserves the Qt5 rect-mapping
            // sub-pixel correction (see renderWithZoomCache).
            renderWithZoomCache(painter, size, zoom, dpr);
            break;
        case RenderTier::Focus: {
            ensureFocusCacheValid(size, zoom, dpr, focusRect);
            if (m_focusCache.isNull()) break;
            // The pixmap has DPR set to (zoom * dpr), so its logical size
            // matches m_focusRect.size() in page/tile-local units. The
            // point-draw overload routes through the same fast 1:1 blit
            // path used by the capped cache - no smooth-scale resampling.
            painter.drawPixmap(m_focusRect.topLeft(), m_focusCache);
            break;
        }
        case RenderTier::Direct:
            painter.save();
            // IntersectClip rather than replace: the outer painter may
            // already have a clip (e.g. paintEvent dirty-region), and we
            // only want to further constrain to the focus rect.
            painter.setClipRect(focusRect, Qt::IntersectClip);
            renderDirectClipped(painter, focusRect);
            painter.restore();
            break;
        }
    }

    /**
     * @brief Render this layer with excluded stroke IDs at the chosen tier.
     *
     * Lasso source-layer variant. Capped tier delegates to today's
     * `renderExcluding`. Focus and Direct tiers are identical to each other:
     * cache-free + bbox cull. (A per-tier excluded-IDs cache would have to be
     * invalidated on every selection change, which isn't worth the complexity
     * for the typical lasso interaction pattern.)
     */
    void renderExcludingTiered(QPainter& painter,
                               const QSet<QString>& excludeIds,
                               const QSizeF& size, qreal zoom, qreal dpr,
                               RenderTier tier, const QRectF& focusRect) {
        Q_UNUSED(size);
        Q_UNUSED(zoom);
        Q_UNUSED(dpr);
        if (!visible || m_strokes.isEmpty()) return;
        if (tier == RenderTier::Capped) {
            // Same symmetry as `renderTiered`: free the focus pixmap when
            // we're back on the capped path.
            if (hasFocusCacheAllocated()) releaseFocusCache();
            renderExcluding(painter, excludeIds);
            return;
        }
        painter.save();
        painter.setClipRect(focusRect, Qt::IntersectClip);
        renderDirectExcludingClipped(painter, excludeIds, focusRect);
        painter.restore();
    }

private:
    QVector<VectorStroke> m_strokes;  ///< All strokes in this layer
    
    // ===== Curve Smoothing =====
    
    /// Number of interpolated points to insert between each pair of stored points.
    /// Higher values produce smoother curves at high zoom, at the cost of more
    /// polygon vertices (which are cached, so the per-frame cost is zero).
    /// 4 subdivisions keeps segments under ~4 screen pixels at 10x zoom.
    static constexpr int CURVE_SUBDIVISIONS = 4;
    
    /**
     * @brief Subdivide stroke points using uniform Catmull-Rom interpolation.
     * @param points The original (decimated) stroke points.
     * @return A denser point sequence following a smooth curve through the originals.
     * 
     * Interpolates both position and pressure. Endpoint tangents are computed
     * by duplicating the first/last control point (zero-acceleration boundary).
     * Interpolated pressure is clamped to [0.1, 1.0] to prevent overshoot.
     */
    static QVector<StrokePoint> catmullRomSubdivide(const QVector<StrokePoint>& points) {
        const int n = static_cast<int>(points.size());
        if (n < 3) return points;  // Straight lines don't benefit from smoothing
        
        QVector<StrokePoint> result;
        result.reserve((n - 1) * CURVE_SUBDIVISIONS + 1);
        
        for (int i = 0; i < n - 1; ++i) {
            // Four control points: P0, P1, P2, P3
            // Clamp at boundaries (duplicate endpoint)
            const StrokePoint& p0 = points[qMax(0, i - 1)];
            const StrokePoint& p1 = points[i];
            const StrokePoint& p2 = points[i + 1];
            const StrokePoint& p3 = points[qMin(n - 1, i + 2)];
            
            // Include start point of the first segment
            if (i == 0) {
                result.append(p1);
            }
            
            // Interpolate CURVE_SUBDIVISIONS points between p1 and p2
            for (int s = 1; s <= CURVE_SUBDIVISIONS; ++s) {
                qreal t = static_cast<qreal>(s) / CURVE_SUBDIVISIONS;
                qreal t2 = t * t;
                qreal t3 = t2 * t;
                
                // Uniform Catmull-Rom: q(t) = 0.5 * [ (2·P1) + (-P0+P2)·t
                //   + (2·P0 - 5·P1 + 4·P2 - P3)·t² + (-P0 + 3·P1 - 3·P2 + P3)·t³ ]
                qreal x = 0.5 * (2.0 * p1.pos.x()
                    + (-p0.pos.x() + p2.pos.x()) * t
                    + (2.0 * p0.pos.x() - 5.0 * p1.pos.x() + 4.0 * p2.pos.x() - p3.pos.x()) * t2
                    + (-p0.pos.x() + 3.0 * p1.pos.x() - 3.0 * p2.pos.x() + p3.pos.x()) * t3);
                qreal y = 0.5 * (2.0 * p1.pos.y()
                    + (-p0.pos.y() + p2.pos.y()) * t
                    + (2.0 * p0.pos.y() - 5.0 * p1.pos.y() + 4.0 * p2.pos.y() - p3.pos.y()) * t2
                    + (-p0.pos.y() + 3.0 * p1.pos.y() - 3.0 * p2.pos.y() + p3.pos.y()) * t3);
                qreal pr = 0.5 * (2.0 * p1.pressure
                    + (-p0.pressure + p2.pressure) * t
                    + (2.0 * p0.pressure - 5.0 * p1.pressure + 4.0 * p2.pressure - p3.pressure) * t2
                    + (-p0.pressure + 3.0 * p1.pressure - 3.0 * p2.pressure + p3.pressure) * t3);
                
                StrokePoint pt;
                pt.pos = QPointF(x, y);
                pt.pressure = qBound(0.1, pr, 1.0);
                result.append(pt);
            }
        }
        
        return result;
    }
    
    // Stroke cache for performance (Task 1.3.7 + Zoom-Aware + Incremental)
    mutable QPixmap m_strokeCache;          ///< Cached rendered strokes at current zoom
    mutable bool m_strokeCacheDirty = true; ///< Whether cache needs full rebuild
    mutable qreal m_cacheZoom = 1.0;        ///< Zoom level cache was built at
    mutable qreal m_cacheDpr = 1.0;         ///< DPI ratio cache was built at
    mutable int m_cacheDivisor = 1;         ///< Integer divisor applied for resolution cap

    // Viewport-clipped focus cache (high-zoom path). When the capped cache
    // would have to apply a divisor > 1 (effective scale * pageMaxDim >
    // MAX_STROKE_CACHE_DIM), DocumentViewport drops the whole-page pixmap
    // and asks for a focus cache instead, sized to the viewport-tile
    // intersection only. Memory is bounded by viewport pixels, not page size.
    mutable QPixmap m_focusCache;           ///< Crisp pixmap for focusRect
    mutable QRectF  m_focusRect;            ///< Page/tile-local logical bounds covered
    mutable qreal   m_focusZoom = 0.0;      ///< Zoom focus cache was built at
    mutable qreal   m_focusDpr  = 0.0;      ///< DPR focus cache was built at
    mutable bool    m_focusCacheDirty = true;
    /// Index of first stroke pending incremental render into the focus cache,
    /// or -1 if none. Mirrors m_pendingStrokeStart for the focus cache.
    mutable int     m_focusPendingStrokeStart = -1;
    
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5: when zoom*dpr < 1.0 the cache DPR is clamped to 1.0, so the
    // painter's logical canvas is smaller than the page. Apply a manual
    // scale transform so strokes (in page coordinates) map correctly to
    // cache pixels and rasterize with proper anti-aliasing at the target
    // resolution. When rawScale >= 1.0 this is a no-op.
    static void applyCachePainterScale(QPainter& p, qreal rawScale) {
        if (rawScale < 1.0) {
            p.scale(rawScale, rawScale);
        }
    }
#else
    static void applyCachePainterScale(QPainter&, qreal) {}
#endif
    
    /// Index of first stroke pending incremental render, or -1 if none.
    /// When addStroke() is called and the cache is valid, the stroke index is
    /// recorded here. On next ensureStrokeCacheValid(), only these new strokes
    /// are painted to the existing cache — no allocation, no full re-render.
    mutable int m_pendingStrokeStart = -1;
    
    /**
     * @brief Mark the last added stroke for incremental cache rendering.
     * 
     * If the cache is currently valid, records the stroke index so it can be
     * painted incrementally. If the cache is already dirty (needs full rebuild),
     * stays dirty — the new stroke will be included in the next full rebuild.
     */
    void markStrokePending() {
        if (!m_strokeCacheDirty && !m_strokeCache.isNull()) {
            // Cache is valid — mark for incremental update
            if (m_pendingStrokeStart < 0) {
                m_pendingStrokeStart = static_cast<int>(m_strokes.size()) - 1;
            }
            // If m_pendingStrokeStart is already set (multiple adds between paints),
            // keep the earlier index so all new strokes get rendered.
        } else {
            // Cache is dirty anyway — full rebuild will include this stroke
            m_strokeCacheDirty = true;
        }
        // Mirror the same logic on the focus cache. It is independent: the
        // user might be at high zoom (focus cache live, capped released) or
        // moderate zoom (capped live, focus released), and either may need
        // an incremental append for the same new stroke.
        markFocusStrokePending();
    }

    /**
     * @brief Mark the latest stroke for incremental focus-cache rendering.
     */
    void markFocusStrokePending() {
        if (!m_focusCacheDirty && !m_focusCache.isNull()) {
            if (m_focusPendingStrokeStart < 0) {
                m_focusPendingStrokeStart = static_cast<int>(m_strokes.size()) - 1;
            }
        } else {
            m_focusCacheDirty = true;
        }
    }
    
    /**
     * @brief Render pending strokes incrementally to the existing cache.
     * 
     * Called by ensureStrokeCacheValid() when the cache is valid but has
     * new strokes to append. Renders only the new strokes (O(k) where k
     * is the number of new strokes, typically 1) instead of all n strokes.
     */
    void appendPendingStrokes() const {
        if (m_pendingStrokeStart < 0 || m_strokeCache.isNull()) return;
        
        QPainter cachePainter(&m_strokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        applyCachePainterScale(cachePainter, m_cacheZoom * m_cacheDpr / m_cacheDivisor);
        
        for (int i = m_pendingStrokeStart; i < m_strokes.size(); ++i) {
            renderStroke(cachePainter, m_strokes[i]);
        }
        
        m_pendingStrokeStart = -1;
    }
    
    /**
     * @brief Patch the stroke cache after removing a stroke.
     * @param removedBounds Bounding box of the removed stroke (in page/tile coords).
     * 
     * If the cache is valid, clears the removed stroke's bounding box region
     * and re-renders only the strokes that overlap that region. This is O(k)
     * where k is the number of overlapping strokes, not O(n) for all strokes.
     * Falls back to full invalidation if the cache is already dirty.
     */
    void patchCacheAfterRemoval(const QRectF& removedBounds) {
        // Patch (or invalidate) the focus cache regardless of capped-cache
        // state - the two caches are independent.
        patchFocusCacheAfterRemoval(removedBounds);

        // Cannot patch if cache is not in a usable state
        if (m_strokeCacheDirty || m_strokeCache.isNull() ||
            removedBounds.isEmpty() || m_pendingStrokeStart >= 0) {
            // invalidateStrokeCache() also invalidates the focus cache; the
            // patch above already handled that path, so call the direct
            // capped-only invalidation to avoid redundant work.
            m_strokeCacheDirty = true;
            m_pendingStrokeStart = -1;
            return;
        }
        
        QPainter cachePainter(&m_strokeCache);
        applyCachePainterScale(cachePainter, m_cacheZoom * m_cacheDpr / m_cacheDivisor);
        
        // Step 1: Clear the removed stroke's bounding box
        cachePainter.setCompositionMode(QPainter::CompositionMode_Clear);
        cachePainter.fillRect(removedBounds, Qt::transparent);
        
        // Step 2: Re-render overlapping strokes within the cleared region.
        // Clip to the cleared rect so we don't double-paint outside it.
        cachePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        cachePainter.setClipRect(removedBounds);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        for (const auto& stroke : m_strokes) {
            if (stroke.boundingBox.intersects(removedBounds)) {
                renderStroke(cachePainter, stroke);
            }
        }
    }
    
    /**
     * @brief Rebuild stroke cache at given size and zoom.
     * @param size Target size in logical pixels (page size).
     * @param zoom Zoom level to render at.
     * @param dpr Device pixel ratio.
     * 
     * Creates a cache pixmap capped to MAX_STROKE_CACHE_DIM per dimension.
     * When the full-resolution size exceeds the cap, an integer divisor N is
     * applied so each cache pixel maps to exactly NxN sub-pixels, avoiding
     * fractional-pixel aliasing.
     */
    /**
     * @brief Set up the focus-cache painter in page/tile-local logical coords.
     *
     * Pre-condition: `m_focusCache` is allocated with
     * `setDevicePixelRatio(m_focusZoom * m_focusDpr)`, so the painter starts
     * in logical-pixel space (1 unit = 1 page/tile point inside `m_focusRect`).
     * After this call, drawing a stroke at its page/tile-local position
     * lands in the right pixel. Caller is responsible for any clip.
     */
    void beginFocusPainter(QPainter& p) const {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-m_focusRect.topLeft());
    }

    /**
     * @brief Rebuild the focus cache from scratch.
     *
     * The pixmap is allocated at physical pixel size and tagged with
     * `setDevicePixelRatio(zoom * dpr)`, so its logical size matches
     * `focusRect.size()`. This lets `drawPixmap(focusRect.topLeft(), pixmap)`
     * blit at exactly 1 cache pixel = 1 screen pixel through Qt's fast path
     * (no QRectF rect-to-rect resample), matching the capped cache pattern.
     */
    void rebuildFocusCache(qreal zoom, qreal dpr,
                           const QRectF& focusRect,
                           const QSize& physicalSize) const {
        m_focusCache = QPixmap(physicalSize);
        m_focusCache.setDevicePixelRatio(zoom * dpr);
        m_focusCache.fill(Qt::transparent);

        m_focusZoom = zoom;
        m_focusDpr  = dpr;
        m_focusRect = focusRect;
        m_focusCacheDirty = false;

        if (m_strokes.isEmpty()) return;

        QPainter p(&m_focusCache);
        beginFocusPainter(p);
        // Cull to the visible rect so per-stroke renderStroke can early-exit.
        p.setClipRect(focusRect);
        for (const auto& stroke : m_strokes) {
            if (stroke.boundingBox.intersects(focusRect)) {
                renderStroke(p, stroke);
            }
        }
    }

    /**
     * @brief Append pending strokes incrementally to the focus cache.
     */
    void appendPendingFocusStrokes() const {
        if (m_focusPendingStrokeStart < 0 || m_focusCache.isNull()) return;
        QPainter p(&m_focusCache);
        beginFocusPainter(p);
        p.setClipRect(m_focusRect);
        for (int i = m_focusPendingStrokeStart; i < m_strokes.size(); ++i) {
            if (m_strokes[i].boundingBox.intersects(m_focusRect)) {
                renderStroke(p, m_strokes[i]);
            }
        }
        m_focusPendingStrokeStart = -1;
    }

    void rebuildStrokeCache(const QSizeF& size, qreal zoom, qreal dpr) const {
        int divisor = computeCacheDivisor(size, zoom, dpr);
        QSize physicalSize = cappedPhysicalSize(size, zoom, dpr, divisor);
        qreal rawScale = zoom * dpr / divisor;
        
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // Qt5: QPixmap::setDevicePixelRatio() breaks with values < 1.0.
        // Use DPR = max(1.0, rawScale) and compensate with a painter scale()
        // so strokes rasterize at the correct zoomed-out resolution with
        // proper anti-aliasing (instead of rendering at page resolution and
        // then downscaling the whole pixmap, which causes aliasing).
        qreal cacheDpr = qMax(1.0, rawScale);
#else
        qreal cacheDpr = rawScale;
#endif
        
        m_strokeCache = QPixmap(physicalSize);
        m_strokeCache.setDevicePixelRatio(cacheDpr);
        m_strokeCache.fill(Qt::transparent);
        
        if (m_strokes.isEmpty()) {
            m_strokeCacheDirty = false;
            m_cacheZoom = zoom;
            m_cacheDpr = dpr;
            m_cacheDivisor = divisor;
            return;
        }
        
        QPainter cachePainter(&m_strokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        applyCachePainterScale(cachePainter, rawScale);
        
        for (const auto& stroke : m_strokes) {
            renderStroke(cachePainter, stroke);
        }
        
        m_strokeCacheDirty = false;
        m_cacheZoom = zoom;
        m_cacheDpr = dpr;
        m_cacheDivisor = divisor;
    }
    
    static int computeCacheDivisor(const QSizeF& size, qreal zoom, qreal dpr) {
        int desiredMax = static_cast<int>(
            qMax(size.width(), size.height()) * zoom * dpr);
        if (desiredMax <= MAX_STROKE_CACHE_DIM) return 1;
        return (desiredMax + MAX_STROKE_CACHE_DIM - 1) / MAX_STROKE_CACHE_DIM;
    }
    
    static QSize cappedPhysicalSize(const QSizeF& size, qreal zoom, qreal dpr,
                                    int divisor) {
        return QSize(static_cast<int>(size.width()  * zoom * dpr / divisor),
                     static_cast<int>(size.height() * zoom * dpr / divisor));
    }
};
