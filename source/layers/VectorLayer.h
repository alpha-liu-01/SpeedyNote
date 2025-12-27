#pragma once

// ============================================================================
// VectorLayer - A single layer containing vector strokes
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.3)
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
     */
    void addStroke(const VectorStroke& stroke) {
        m_strokes.append(stroke);
        invalidateStrokeCache();  // Cache needs rebuild
    }
    
    /**
     * @brief Add a stroke by moving it (more efficient for large strokes).
     * @param stroke The stroke to move into this layer.
     */
    void addStroke(VectorStroke&& stroke) {
        m_strokes.append(std::move(stroke));
        invalidateStrokeCache();  // Cache needs rebuild
    }
    
    /**
     * @brief Remove a stroke by its ID.
     * @param strokeId The UUID of the stroke to remove.
     * @return True if stroke was found and removed.
     */
    bool removeStroke(const QString& strokeId) {
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            if (m_strokes[i].id == strokeId) {
                m_strokes.removeAt(i);
                invalidateStrokeCache();  // Cache needs rebuild
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
    int strokeCount() const { return m_strokes.size(); }
    
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
     */
    static void renderStroke(QPainter& painter, const VectorStroke& stroke) {
        if (stroke.points.size() < 2) {
            // Single point - draw a dot
            if (stroke.points.size() == 1) {
                qreal radius = stroke.baseThickness * stroke.points[0].pressure / 2.0;
                painter.setPen(Qt::NoPen);
                painter.setBrush(stroke.color);
                painter.drawEllipse(stroke.points[0].pos, radius, radius);
            }
            return;
        }
        
        // ========== OPTIMIZATION: Render as filled polygon ==========
        // Instead of N drawLine() calls with varying widths, render the stroke
        // as a single filled polygon representing the variable-width outline.
        // This is much faster: 1 draw call instead of N, and GPU-friendly.
        
        const int n = stroke.points.size();
        
        // Pre-calculate half-widths for each point
        QVector<qreal> halfWidths(n);
        for (int i = 0; i < n; ++i) {
            qreal width = stroke.baseThickness * stroke.points[i].pressure;
            halfWidths[i] = qMax(width, 1.0) / 2.0;
        }
        
        // Build the stroke outline polygon
        // Left edge goes forward, right edge goes backward
        QVector<QPointF> leftEdge(n);
        QVector<QPointF> rightEdge(n);
        
        for (int i = 0; i < n; ++i) {
            const QPointF& pos = stroke.points[i].pos;
            qreal hw = halfWidths[i];
            
            // Calculate perpendicular direction
            QPointF tangent;
            if (i == 0) {
                // First point: use direction to next point
                tangent = stroke.points[1].pos - pos;
            } else if (i == n - 1) {
                // Last point: use direction from previous point
                tangent = pos - stroke.points[n - 2].pos;
            } else {
                // Middle points: average of incoming and outgoing directions
                tangent = stroke.points[i + 1].pos - stroke.points[i - 1].pos;
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
        QPolygonF polygon;
        polygon.reserve(n * 2 + 2);
        
        for (int i = 0; i < n; ++i) {
            polygon << leftEdge[i];
        }
        for (int i = n - 1; i >= 0; --i) {
            polygon << rightEdge[i];
        }
        
        // Draw filled polygon with WindingFill to handle self-intersections
        // OddEvenFill (default) leaves holes where stroke crosses itself
        // WindingFill fills all enclosed areas regardless of winding count
        painter.setPen(Qt::NoPen);
        painter.setBrush(stroke.color);
        painter.drawPolygon(polygon, Qt::WindingFill);
        
        // Draw round end caps for a smooth look
        qreal startRadius = halfWidths[0];
        qreal endRadius = halfWidths[n - 1];
        painter.drawEllipse(stroke.points[0].pos, startRadius, startRadius);
        painter.drawEllipse(stroke.points[n - 1].pos, endRadius, endRadius);
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
     * Cache is built at size * zoom * dpr for sharp rendering at current zoom.
     * If cache is invalid, wrong size, or wrong zoom, rebuilds it.
     */
    void ensureStrokeCacheValid(const QSizeF& size, qreal zoom, qreal dpr) {
        // Calculate physical size at this zoom level
        QSize physicalSize(static_cast<int>(size.width() * zoom * dpr), 
                           static_cast<int>(size.height() * zoom * dpr));
        
        // Check if cache is valid for current parameters
        if (!m_strokeCacheDirty && 
            m_strokeCache.size() == physicalSize &&
            qFuzzyCompare(m_cacheZoom, zoom) &&
            qFuzzyCompare(m_cacheDpr, dpr)) {
            return;  // Cache is valid
        }
        
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
     * @brief Invalidate stroke cache (call when strokes change).
     * Note: This only marks the cache dirty, it does NOT free memory.
     */
    void invalidateStrokeCache() { m_strokeCacheDirty = true; }
    
    /**
     * @brief Release stroke cache memory completely.
     * Call this for pages that are far from the visible area to save memory.
     * The cache will be rebuilt lazily when the page becomes visible again.
     */
    void releaseStrokeCache() {
        m_strokeCache = QPixmap();  // Actually free the pixmap memory
        m_strokeCacheDirty = true;
        m_cacheZoom = 0;
        m_cacheDpr = 0;
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
    
private:
    QVector<VectorStroke> m_strokes;  ///< All strokes in this layer
    
    // Stroke cache for performance (Task 1.3.7 + Zoom-Aware)
    mutable QPixmap m_strokeCache;          ///< Cached rendered strokes at current zoom
    mutable bool m_strokeCacheDirty = true; ///< Whether cache needs rebuild
    mutable qreal m_cacheZoom = 1.0;        ///< Zoom level cache was built at
    mutable qreal m_cacheDpr = 1.0;         ///< DPI ratio cache was built at
    
    /**
     * @brief Rebuild stroke cache at given size and zoom.
     * @param size Target size in logical pixels (page size).
     * @param zoom Zoom level to render at.
     * @param dpr Device pixel ratio.
     * 
     * Creates a cache pixmap at size * zoom * dpr physical pixels.
     * Strokes are rendered pre-scaled by zoom for sharp display.
     */
    void rebuildStrokeCache(const QSizeF& size, qreal zoom, qreal dpr) const {
        // Physical size includes both zoom and DPI scaling
        QSize physicalSize(static_cast<int>(size.width() * zoom * dpr), 
                           static_cast<int>(size.height() * zoom * dpr));
        
        m_strokeCache = QPixmap(physicalSize);
        // Set device pixel ratio to zoom * dpr so Qt draws at logical page size
        m_strokeCache.setDevicePixelRatio(zoom * dpr);
        m_strokeCache.fill(Qt::transparent);
        
        if (m_strokes.isEmpty()) {
            m_strokeCacheDirty = false;
            m_cacheZoom = zoom;
            m_cacheDpr = dpr;
            return;
        }
        
        QPainter cachePainter(&m_strokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Strokes are in page coordinates - with devicePixelRatio set to zoom * dpr,
        // Qt automatically scales the drawing to fill the physical pixmap.
        // This gives us zoom-level resolution without manual coordinate scaling.
        for (const auto& stroke : m_strokes) {
            renderStroke(cachePainter, stroke);
        }
        
        m_strokeCacheDirty = false;
        m_cacheZoom = zoom;
        m_cacheDpr = dpr;
    }
};
