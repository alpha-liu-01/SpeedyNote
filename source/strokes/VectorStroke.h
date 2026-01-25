#pragma once

// ============================================================================
// VectorStroke - A complete stroke (pen down → pen up)
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.4)
// Extracted from VectorCanvas.h for modularity
// ============================================================================

#include "StrokePoint.h"

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
     * @brief Check if a point is near this stroke (for eraser hit testing).
     * @param point The point to test.
     * @param tolerance Additional tolerance radius (eraser size).
     * @return True if the point is within tolerance of any stroke segment or point.
     */
    bool containsPoint(const QPointF& point, qreal tolerance) const {
        if (points.isEmpty()) {
            return false;
        }
        
        // Quick rejection test using bounding box
        if (!boundingBox.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) {
            return false;
        }
        
        // Single-point stroke (dot): check distance to the single point
        // Use baseThickness/2 because that's the actual visual radius of the stroke
        if (points.size() == 1) {
            qreal dx = point.x() - points[0].pos.x();
            qreal dy = point.y() - points[0].pos.y();
            qreal distSq = dx * dx + dy * dy;
            qreal threshold = tolerance + baseThickness / 2.0;
            return distSq < threshold * threshold;
        }
        
        // Multi-point stroke: check each segment
        // Hit when eraser edge (tolerance) touches stroke edge (baseThickness/2)
        for (int i = 1; i < points.size(); ++i) {
            if (distanceToSegment(point, points[i-1].pos, points[i].pos) < tolerance + baseThickness / 2.0) {
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
};
