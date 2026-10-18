#pragma once

// ============================================================================
// VectorStroke - A complete stroke (pen down → pen up)
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
// Extracted from VectorCanvas.h for modularity
// ============================================================================

#include "StrokePoint.h"

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QColor>
#include <QRectF>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineF>
#include <QUuid>

/**
 * @brief A complete vector stroke consisting of multiple points.
 * 
 * Represents a single pen stroke from pen-down to pen-up.
 * Stores all points with pressure, color, and base thickness.
 * Provides hit testing for eraser functionality and serialization.
 */
struct VectorStroke {
    QString id;                     ///< UUID for tracking (used in undo/redo)
    QVector<StrokePoint> points;    ///< All points in the stroke
    QColor color;                   ///< Stroke color
    qreal baseThickness;            ///< Base thickness before pressure scaling
    QRectF boundingBox;             ///< Cached bounding box for fast culling/hit testing
    
    /**
     * @brief Default constructor.
     * Initializes baseThickness to 5.0 pixels.
     */
    VectorStroke() : baseThickness(5.0) {}
    
    /**
     * @brief Recalculate the bounding box from current points.
     * 
     * Should be called after all points are added (when stroke is finalized).
     * Adds padding based on maximum possible stroke width.
     */
    void updateBoundingBox() {
        if (points.isEmpty()) {
            boundingBox = QRectF();
            return;
        }
        qreal maxWidth = baseThickness * 2;
        qreal minX = points[0].pos.x(), maxX = minX;
        qreal minY = points[0].pos.y(), maxY = minY;
        for (const auto& pt : points) {
            minX = qMin(minX, pt.pos.x());
            maxX = qMax(maxX, pt.pos.x());
            minY = qMin(minY, pt.pos.y());
            maxY = qMax(maxY, pt.pos.y());
        }
        boundingBox = QRectF(minX - maxWidth, minY - maxWidth,
                             maxX - minX + maxWidth * 2,
                             maxY - minY + maxWidth * 2);
    }
    
    /**
     * @brief Check if an eraser disc centred on `point` touches this stroke.
     *
     * The test is solid-to-solid: the eraser disc reaches the stroke's drawn
     * outline, not merely its centre line. `radius` is the eraser's own radius
     * (the circle the user sees), and the stroke contributes its rendered
     * half-width at the tested sample.
     *
     * Why the half-width is added back: a stroke is a filled ribbon of width
     * `baseThickness * pressure`, so requiring the eraser centre to come within
     * `radius` of the centre line makes the eraser stop short of the visible
     * ink by half the stroke width - the cursor circle would have to overlap
     * the stroke noticeably before anything happened. The previous version had
     * this same half-width term but grew only the hit test and not the cursor,
     * which is what made the eraser look bigger than its circle. Now both sides
     * use this rule, so the circle and the behaviour agree.
     *
     * @param point The point to test (eraser centre).
     * @param radius Eraser radius in the same units as the stroke points.
     * @return True if the disc touches any part of the stroke.
     */
    bool containsPoint(const QPointF& point, qreal radius) const {
        if (points.isEmpty()) {
            return false;
        }
        
        // Quick rejection. Pad by the widest half-width this stroke can reach,
        // otherwise a thick stroke whose outline is touched but whose centre
        // line is farther than `radius` would be culled here.
        const qreal pad = radius + maxHalfWidth();
        if (!boundingBox.adjusted(-pad, -pad, pad, pad).contains(point)) {
            return false;
        }
        
        // Single-point stroke (dot): distance to the dot's disc.
        if (points.size() == 1) {
            const qreal dx = point.x() - points[0].pos.x();
            const qreal dy = point.y() - points[0].pos.y();
            const qreal reach = radius + halfWidthAt(0);
            return dx * dx + dy * dy < reach * reach;
        }
        
        // Multi-point stroke: compare against each segment's own half-width so
        // a pressure-tapered stroke is judged by its local thickness.
        for (int i = 1; i < points.size(); ++i) {
            const qreal reach = radius
                                + (halfWidthAt(i - 1) + halfWidthAt(i)) * 0.5;
            if (distanceToSegment(point, points[i-1].pos, points[i].pos) < reach) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Rendered half-width of the stroke at point index `i`.
     *
     * Matches the polygon builder in VectorLayer, which draws a ribbon of
     * width `baseThickness * pressure`.
     */
    qreal halfWidthAt(int i) const {
        if (i < 0 || i >= points.size()) {
            return baseThickness / 2.0;
        }
        return baseThickness * points[i].pressure / 2.0;
    }
    
    /**
     * @brief Largest half-width anywhere on this stroke.
     *
     * Used to size the broad-phase rejection padding.
     */
    qreal maxHalfWidth() const {
        qreal hw = baseThickness / 2.0;
        for (const StrokePoint& pt : points) {
            hw = qMax(hw, baseThickness * pt.pressure / 2.0);
        }
        return hw;
    }

    /**
     * @brief Check whether an eraser disc swept from `a` to `b` reaches this stroke.
     *
     * Pointer events arrive at a finite rate, so a fast eraser stroke skips
     * whole regions between two sampled positions. Treating the motion as a
     * capsule (a disc dragged along the segment a->b) closes those gaps; a
     * per-position disc test alone cannot.
     *
     * @param a Start of the eraser motion path.
     * @param b End of the eraser motion path.
     * @param radius Eraser radius in the same units as the stroke points.
     * @return True if the swept disc touches any part of this stroke.
     */
    bool intersectsEraserSweep(const QPointF& a, const QPointF& b, qreal radius) const {
        if (points.isEmpty()) {
            return false;
        }
        
        // Quick rejection against the swept capsule's bounding box, grown by the
        // stroke's own half-width so the broad phase covers outlines, not just
        // centre lines. QRectF::intersects() is false for a degenerate rect,
        // which an axis-aligned swipe produces, so the path is padded first.
        const qreal pad = radius + maxHalfWidth();
        const QRectF sweepRect = QRectF(a, b).normalized()
                                    .adjusted(-pad, -pad, pad, pad);
        if (!boundingBox.adjusted(-pad, -pad, pad, pad).intersects(sweepRect)) {
            return false;
        }
        
        if (points.size() == 1) {
            return distanceToSegment(points[0].pos, a, b) < radius + halfWidthAt(0);
        }
        
        // Two polylines touch when their closest approach is within the sum of
        // their half-widths; the sweep is a capsule of `radius` around a->b.
        for (int i = 1; i < points.size(); ++i) {
            const qreal reach = radius
                                + (halfWidthAt(i - 1) + halfWidthAt(i)) * 0.5;
            if (segmentsCloserThan(points[i-1].pos, points[i].pos, a, b, reach)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Serialize to JSON.
     * @return JSON object containing id, color, thickness, and points array.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["color"] = color.name(QColor::HexArgb);
        obj["thickness"] = baseThickness;
        QJsonArray pointsArray;
        for (const auto& pt : points) {
            pointsArray.append(pt.toJson());
        }
        obj["points"] = pointsArray;
        return obj;
    }
    
    /**
     * @brief Deserialize from JSON.
     * @param obj JSON object containing stroke data.
     * @return VectorStroke with values from JSON (bounding box auto-calculated).
     */
    static VectorStroke fromJson(const QJsonObject& obj) {
        VectorStroke stroke;
        stroke.id = obj["id"].toString();
        stroke.color = QColor(obj["color"].toString());
        stroke.baseThickness = obj["thickness"].toDouble(5.0);
        
        // Generate UUID if missing (for backwards compatibility)
        if (stroke.id.isEmpty()) {
            stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        
        QJsonArray pointsArray = obj["points"].toArray();
        for (const auto& val : pointsArray) {
            stroke.points.append(StrokePoint::fromJson(val.toObject()));
        }
        stroke.updateBoundingBox();
        return stroke;
    }
    
private:
    /**
     * @brief Calculate distance from a point to a line segment.
     * @param p The point to test.
     * @param a Start of segment.
     * @param b End of segment.
     * @return Distance from p to the nearest point on segment ab.
     */
    static qreal distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
        QPointF ab = b - a;
        QPointF ap = p - a;
        qreal lenSq = ab.x() * ab.x() + ab.y() * ab.y();
        if (lenSq < 0.0001) return QLineF(p, a).length();
        qreal t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / lenSq, 1.0);
        QPointF closest = a + t * ab;
        return QLineF(p, closest).length();
    }
    
    /**
     * @brief Segment-to-segment closest-approach test, capped at `limit`.
     *
     * Used by the swept-eraser test: an eraser dragged along c->d touches a
     * stroke segment a->b exactly when the two segments come within `limit`
     * of each other. Endpoint-to-segment distances alone are not sufficient
     * (two crossing segments can have all four endpoints far apart), so this
     * also tests for a proper crossing.
     */
    static bool segmentsCloserThan(const QPointF& a, const QPointF& b,
                                   const QPointF& c, const QPointF& d,
                                   qreal limit) {
        if (distanceToSegment(a, c, d) < limit) return true;
        if (distanceToSegment(b, c, d) < limit) return true;
        if (distanceToSegment(c, a, b) < limit) return true;
        if (distanceToSegment(d, a, b) < limit) return true;
        // Proper crossing: endpoints can all be farther than `limit`.
        return segmentsIntersect(a, b, c, d);
    }
    
    /**
     * @brief Orientation-based proper/improper segment intersection test.
     */
    static bool segmentsIntersect(const QPointF& a, const QPointF& b,
                                  const QPointF& c, const QPointF& d) {
        auto cross = [](const QPointF& o, const QPointF& p, const QPointF& q) {
            return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
        };
        const qreal d1 = cross(c, d, a);
        const qreal d2 = cross(c, d, b);
        const qreal d3 = cross(a, b, c);
        const qreal d4 = cross(a, b, d);
        return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
    }
};

Q_DECLARE_METATYPE(VectorStroke)
Q_DECLARE_METATYPE(QVector<VectorStroke>)
