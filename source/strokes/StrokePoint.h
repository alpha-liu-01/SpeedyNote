#pragma once

// ============================================================================
// StrokePoint - A single point in a vector stroke with pressure
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
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
    QPointF pos;          ///< Position in canvas coordinates
    qreal pressure;       ///< Pen pressure, 0.0 to 1.0
    qint64 timestamp = 0; ///< Milliseconds since epoch; 0 = not recorded (legacy)
    
    /**
     * @brief Serialize to JSON.
     * @return JSON object with x, y, p (pressure), and optional t (timestamp) fields.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["x"] = pos.x();
        obj["y"] = pos.y();
        obj["p"] = pressure;
        if (timestamp != 0)
            obj["t"] = static_cast<double>(timestamp);
        return obj;
    }
    
    /**
     * @brief Deserialize from JSON.
     * @param obj JSON object with x, y, and optional p / t fields.
     * @return StrokePoint with values from JSON.
     */
    static StrokePoint fromJson(const QJsonObject& obj) {
        StrokePoint pt;
        pt.pos = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
        pt.pressure = obj["p"].toDouble(1.0);
        pt.timestamp = static_cast<qint64>(obj["t"].toDouble(0));
        return pt;
    }
};
