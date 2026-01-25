#pragma once

// ============================================================================
// InsertedObject - Abstract base class for all insertable objects
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.5)
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
    int zOrder = 0;           ///< Stacking order within same affinity (higher = on top)
    bool locked = false;      ///< If true, object cannot be moved/resized/deleted
    bool visible = true;      ///< Whether object is rendered
    qreal rotation = 0.0;     ///< Rotation in degrees (for future use)
    
    /**
     * @brief Layer affinity - determines rendering order relative to stroke layers.
     * 
     * Objects are rendered at specific points in the layer stack:
     * - -1 (default): Rendered BELOW all stroke layers (e.g., background image)
     * -  0: Rendered AFTER Layer 0 strokes, BEFORE Layer 1 strokes
     * -  1: Rendered AFTER Layer 1 strokes, BEFORE Layer 2 strokes
     * -  N: Rendered AFTER Layer N strokes, BEFORE Layer N+1 strokes
     * 
     * Use case: Paste a test paper image (affinity=-1), write strokes on top.
     * The zOrder property only affects ordering among objects with the SAME affinity.
     */
    int layerAffinity = -1;
    
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
    
    // ===== Virtual Asset Management Methods (Phase O2.C) =====
    
    /**
     * @brief Load external assets (e.g., images) from the bundle.
     * @param bundlePath Path to the .snb bundle directory.
     * @return True if successful or no assets to load.
     * 
     * Default implementation does nothing (for objects without external assets).
     * ImageObject overrides to load the pixmap from assets/images/.
     * 
     * This abstraction allows DocumentViewport to work with InsertedObject*
     * without knowing the concrete type.
     */
    virtual bool loadAssets(const QString& bundlePath) { 
        Q_UNUSED(bundlePath);
        return true; 
    }
    
    /**
     * @brief Save external assets (e.g., images) to the bundle.
     * @param bundlePath Path to the .snb bundle directory.
     * @return True if successful or no assets to save.
     * 
     * Default implementation does nothing (for objects without external assets).
     * ImageObject overrides to save the pixmap to assets/images/.
     * 
     * This abstraction allows DocumentViewport to work with InsertedObject*
     * without knowing the concrete type.
     */
    virtual bool saveAssets(const QString& bundlePath) { 
        Q_UNUSED(bundlePath);
        return true; 
    }
    
    /**
     * @brief Check if this object's assets are loaded and ready to render.
     * @return True if ready (or no assets needed).
     * 
     * Default returns true (objects without external assets are always ready).
     * ImageObject returns !cachedPixmap.isNull().
     */
    virtual bool isAssetLoaded() const { return true; }
    
    // ===== Common Helpers =====
    
    /**
     * @brief Get the layer affinity.
     * @return Layer index this object renders after (-1 = below all layers).
     */
    int getLayerAffinity() const { return layerAffinity; }
    
    /**
     * @brief Set the layer affinity.
     * @param affinity Layer index this object should render after (-1 = below all layers).
     * 
     * Note: Changing affinity requires updating the containing Page's affinity map.
     * Use Page::updateObjectAffinity() to properly re-group the object.
     */
    void setLayerAffinity(int affinity) { layerAffinity = affinity; }
    
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
