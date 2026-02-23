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
                // Apply minimum width (1.0) consistent with multi-point strokes
                qreal width = stroke.baseThickness * stroke.points[0].pressure;
                result.startCapRadius = qMax(width, 1.0) / 2.0;
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
            qreal width = stroke.baseThickness * pts[i].pressure;
            halfWidths[i] = qMax(width, 1.0) / 2.0;
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
        
        for (const auto& stroke : m_strokes) {
            renderStroke(painter, stroke);
        }
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
            // Use polygon bounds if stroke.boundingBox is invalid (empty or not updated)
            QRectF bounds = stroke.boundingBox;
            if (bounds.isEmpty() || !bounds.isValid()) {
                bounds = poly.polygon.boundingRect();
            }
            // Expand for caps (which may extend beyond the point positions)
            qreal maxRadius = qMax(poly.startCapRadius, poly.endCapRadius);
            bounds.adjust(-maxRadius - 2, -maxRadius - 2, maxRadius + 2, maxRadius + 2);
            
            // Safety check: ensure bounds are valid and reasonable
            if (bounds.isEmpty() || bounds.width() > 10000 || bounds.height() > 10000) {
                // Fallback to direct rendering if bounds are invalid or too large
                painter.setPen(Qt::NoPen);
                painter.setBrush(stroke.color);
                painter.drawPolygon(poly.polygon, Qt::WindingFill);
                painter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
                painter.drawEllipse(poly.endCapCenter, poly.endCapRadius, poly.endCapRadius);
                return;
            }
            
            // Create temp buffer (use painter's device pixel ratio for high DPI)
            qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
            QSize bufferSize(static_cast<int>(bounds.width() * dpr) + 1,
                             static_cast<int>(bounds.height() * dpr) + 1);
            QPixmap tempBuffer(bufferSize);
            tempBuffer.setDevicePixelRatio(dpr);
            tempBuffer.fill(Qt::transparent);
            
            // Render to temp buffer at full opacity
            QPainter tempPainter(&tempBuffer);
            tempPainter.setRenderHint(QPainter::Antialiasing, true);
            tempPainter.translate(-bounds.topLeft());
            
            QColor opaqueColor = stroke.color;
            opaqueColor.setAlpha(255);
            tempPainter.setPen(Qt::NoPen);
            tempPainter.setBrush(opaqueColor);
            
            // Draw polygon and caps at full opacity
            tempPainter.drawPolygon(poly.polygon, Qt::WindingFill);
            tempPainter.drawEllipse(poly.startCapCenter, poly.startCapRadius, poly.startCapRadius);
            tempPainter.drawEllipse(poly.endCapCenter, poly.endCapRadius, poly.endCapRadius);
            tempPainter.end();
            
            // Blit temp buffer to output with stroke's alpha
            // Use save/restore to ensure opacity is properly restored even if something fails
            painter.save();
            painter.setOpacity(strokeAlpha / 255.0);
            painter.drawPixmap(bounds.topLeft(), tempBuffer);
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
            // Draw the pre-zoomed cache at (0,0) - it's already at the right size
            // The cache was built at size * zoom * dpr, with devicePixelRatio = zoom * dpr
            painter.drawPixmap(0, 0, m_strokeCache);
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
        
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const VectorStroke& stroke : m_strokes) {
            if (!excludeIds.contains(stroke.id)) {
                renderStroke(painter, stroke);
            }
        }
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
    
    static constexpr int MAX_STROKE_CACHE_DIM = 4096;

    // Stroke cache for performance (Task 1.3.7 + Zoom-Aware + Incremental)
    mutable QPixmap m_strokeCache;          ///< Cached rendered strokes at current zoom
    mutable bool m_strokeCacheDirty = true; ///< Whether cache needs full rebuild
    mutable qreal m_cacheZoom = 1.0;        ///< Zoom level cache was built at
    mutable qreal m_cacheDpr = 1.0;         ///< DPI ratio cache was built at
    mutable int m_cacheDivisor = 1;         ///< Integer divisor applied for resolution cap
    
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
        // Cannot patch if cache is not in a usable state
        if (m_strokeCacheDirty || m_strokeCache.isNull() ||
            removedBounds.isEmpty() || m_pendingStrokeStart >= 0) {
            invalidateStrokeCache();
            return;
        }
        
        QPainter cachePainter(&m_strokeCache);
        
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
    void rebuildStrokeCache(const QSizeF& size, qreal zoom, qreal dpr) const {
        int divisor = computeCacheDivisor(size, zoom, dpr);
        QSize physicalSize = cappedPhysicalSize(size, zoom, dpr, divisor);
        qreal effectiveZoomDpr = zoom * dpr / divisor;
        
        m_strokeCache = QPixmap(physicalSize);
        m_strokeCache.setDevicePixelRatio(effectiveZoomDpr);
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
