#pragma once

// ============================================================================
// StrokePoint - A single point in a vector stroke with pressure
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.4)
// Extracted from VectorCanvas.h for modularity
// ============================================================================

#include <QPointF>
#include <QJsonObject>

/**
 * @brief A single point in a stroke with position and pressure.
 * 
 * Used by VectorStroke to store the path of a pen stroke.
 * Pressure is used to calculate variable-width rendering.
 */
struct StrokePoint {
    QPointF pos;      ///< Position in canvas coordinates
    qreal pressure;   ///< Pen pressure, 0.0 to 1.0
    
    /**
     * @brief Serialize to JSON.
     * @return JSON object with x, y, and p (pressure) fields.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["x"] = pos.x();
        obj["y"] = pos.y();
        obj["p"] = pressure;
        return obj;
    }
    
    /**
     * @brief Deserialize from JSON.
     * @param obj JSON object with x, y, and optional p fields.
     * @return StrokePoint with values from JSON.
     */
    static StrokePoint fromJson(const QJsonObject& obj) {
        StrokePoint pt;
        pt.pos = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
        pt.pressure = obj["p"].toDouble(1.0);  // Default pressure 1.0 if missing
        return pt;
    }
};
