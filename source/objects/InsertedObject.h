#pragma once

// ============================================================================
// InsertedObject - Abstract base class for all insertable objects
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.4)
// 
// InsertedObject is the base for any content that can be placed on a page:
// - Images (ImageObject)
// - Text boxes (future: TextBoxObject)
// - Shapes (future: ShapeObject)
// - Sticky notes (future: StickyNoteObject)
// - etc.
//
// This enables polymorphic handling of all inserted content through a
// unified interface for rendering, hit testing, and serialization.
// ============================================================================

#include <QString>
#include <QPointF>
#include <QSizeF>
#include <QRectF>
#include <QJsonObject>
#include <QUuid>
#include <QPainter>
#include <memory>

/**
 * @brief Abstract base class for objects that can be inserted onto a page.
 * 
 * Provides common properties and interface for all insertable objects.
 * Subclasses implement type-specific rendering and serialization.
 */
class InsertedObject {
public:
    // ===== Common Properties =====
    QString id;               ///< UUID for tracking
    QPointF position;         ///< Top-left position on page (in page coordinates)
    QSizeF size;              ///< Bounding size
    int zOrder = 0;           ///< Stacking order (higher = on top)
    bool locked = false;      ///< If true, object cannot be moved/resized/deleted
    bool visible = true;      ///< Whether object is rendered
    qreal rotation = 0.0;     ///< Rotation in degrees (for future use)
    
    /**
     * @brief Default constructor.
     * Creates an object with a unique ID.
     */
    InsertedObject() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    /**
     * @brief Virtual destructor for proper polymorphic deletion.
     */
    virtual ~InsertedObject() = default;
    
    // ===== Pure Virtual Methods (subclasses MUST implement) =====
    
    /**
     * @brief Render this object.
     * @param painter The QPainter to render to.
     * @param zoom Current zoom level (1.0 = 100%).
     * 
     * Subclasses implement their specific rendering logic.
     * The painter's coordinate system is in page coordinates.
     */
    virtual void render(QPainter& painter, qreal zoom) const = 0;
    
    /**
     * @brief Get the type identifier for this object.
     * @return Type string (e.g., "image", "textbox", "shape").
     * 
     * Used for serialization and type identification.
     */
    virtual QString type() const = 0;
    
    // ===== Virtual Methods (subclasses may override) =====
    
    /**
     * @brief Serialize to JSON.
     * @return JSON object containing object data.
     * 
     * Base implementation saves common properties.
     * Subclasses should call base and add their specific data.
     */
    virtual QJsonObject toJson() const;
    
    /**
     * @brief Deserialize type-specific data from JSON.
     * @param obj JSON object containing object data.
     * 
     * Called by fromJson() after creating the correct subclass.
     * Subclasses should call base and load their specific data.
     */
    virtual void loadFromJson(const QJsonObject& obj);
    
    /**
     * @brief Check if a point is inside this object (for selection/hit testing).
     * @param pt Point in page coordinates.
     * @return True if point is inside the object's bounds.
     * 
     * Default implementation checks bounding rect.
     * Subclasses may override for more precise hit testing (e.g., transparent areas).
     */
    virtual bool containsPoint(const QPointF& pt) const;
    
    // ===== Common Helpers =====
    
    /**
     * @brief Get the bounding rectangle of this object.
     * @return Rectangle from position with size.
     */
    QRectF boundingRect() const {
        return QRectF(position, size);
    }
    
    /**
     * @brief Set position and size from a bounding rectangle.
     * @param rect The new bounding rectangle.
     */
    void setBoundingRect(const QRectF& rect) {
        position = rect.topLeft();
        size = rect.size();
    }
    
    /**
     * @brief Get the center point of this object.
     * @return Center point in page coordinates.
     */
    QPointF center() const {
        return position + QPointF(size.width() / 2.0, size.height() / 2.0);
    }
    
    /**
     * @brief Move the object by a delta.
     * @param delta The offset to move by.
     */
    void moveBy(const QPointF& delta) {
        position += delta;
    }
    
    // ===== Factory Method =====
    
    /**
     * @brief Create an InsertedObject from JSON (factory method).
     * @param obj JSON object containing object data (must have "type" field).
     * @return Unique pointer to the created object, or nullptr if type unknown.
     * 
     * This factory reads the "type" field and creates the appropriate subclass.
     * New object types should be registered here.
     */
    static std::unique_ptr<InsertedObject> fromJson(const QJsonObject& obj);
};
