// ============================================================================
// InsertedObject - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.4)
// ============================================================================

#include "InsertedObject.h"
#include "ImageObject.h"
#include "LinkObject.h"

QJsonObject InsertedObject::toJson() const
{
    QJsonObject obj;
    
    // Type identifier (subclasses provide this)
    obj["type"] = type();
    
    // Common properties
    obj["id"] = id;
    obj["x"] = position.x();
    obj["y"] = position.y();
    obj["width"] = size.width();
    obj["height"] = size.height();
    obj["zOrder"] = zOrder;
    obj["locked"] = locked;
    obj["visible"] = visible;
    obj["rotation"] = rotation;
    obj["layerAffinity"] = layerAffinity;
    
    return obj;
}

void InsertedObject::loadFromJson(const QJsonObject& obj)
{
    // Load common properties
    id = obj["id"].toString();
    position = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
    size = QSizeF(obj["width"].toDouble(), obj["height"].toDouble());
    zOrder = obj["zOrder"].toInt(0);
    locked = obj["locked"].toBool(false);
    visible = obj["visible"].toBool(true);
    rotation = obj["rotation"].toDouble(0.0);
    layerAffinity = obj["layerAffinity"].toInt(-1);  // Default: below all strokes
    
    qDebug() << "InsertedObject::loadFromJson: loaded position =" << position
             << "size =" << size << "zOrder =" << zOrder
             << "layerAffinity =" << layerAffinity;
    
    // Generate ID if missing (for backwards compatibility)
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

bool InsertedObject::containsPoint(const QPointF& pt) const
{
    // Default: simple bounding rect check
    // Subclasses can override for more precise hit testing
    // (e.g., checking transparent pixels in images)
    return boundingRect().contains(pt);
}

std::unique_ptr<InsertedObject> InsertedObject::fromJson(const QJsonObject& obj)
{
    QString objectType = obj["type"].toString();
    
    std::unique_ptr<InsertedObject> result;
    
    // Factory: create appropriate subclass based on type
    // Add new object types here as they are implemented
    
    if (objectType == "image") {
        result = std::make_unique<ImageObject>();
    }
    else if (objectType == "link") {
        result = std::make_unique<LinkObject>();
    }
    // Future object types:
    // else if (objectType == "textbox") {
    //     result = std::make_unique<TextBoxObject>();
    // }
    // else if (objectType == "shape") {
    //     result = std::make_unique<ShapeObject>();
    // }
    else {
        // Unknown type - return nullptr
        // Caller should handle this gracefully (skip unknown objects)
        return nullptr;
    }
    
    // Load common and type-specific data
    if (result) {
        result->loadFromJson(obj);
    }
    
    return result;
}
