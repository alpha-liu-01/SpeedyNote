#pragma once

// ============================================================================
// Page - A single page in a document
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.6)
// 
// Page is the coordinator that brings together:
// - Vector layers (containing strokes)
// - Inserted objects (images, text boxes, etc.)
// - Background (PDF, custom image, grid, lines, or none)
//
// Page is a pure data class - no caching or input handling.
// The DocumentViewport handles rendering optimizations and user input.
// ============================================================================

#include "../layers/VectorLayer.h"
#include "../objects/InsertedObject.h"
#include "../objects/ImageObject.h"

#include <QSizeF>
#include <QColor>
#include <QPixmap>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <memory>

/**
 * @brief A single page in a document.
 * 
 * Coordinates layers and objects on a page. This is a data container class
 * that does not handle rendering caching or user input - those are handled
 * by DocumentViewport.
 * 
 * Supports multiple background types and multiple vector layers.
 */
class Page {
public:
    // ===== Identity =====
    int pageIndex = 0;          ///< Index of this page in the document (0-based)
    QSizeF size;                ///< Page dimensions in logical pixels
    bool modified = false;      ///< True if page has unsaved changes
    
    // ===== Background =====
    
    /**
     * @brief Types of page backgrounds.
     */
    enum class BackgroundType {
        None,       ///< Solid color only
        PDF,        ///< PDF page as background
        Custom,     ///< Custom image as background
        Grid,       ///< Grid pattern
        Lines       ///< Horizontal lines (ruled paper)
    };
    
    BackgroundType backgroundType = BackgroundType::None;
    int pdfPageNumber = -1;             ///< PDF page index if BackgroundType::PDF
    QPixmap customBackground;           ///< Custom background image if BackgroundType::Custom
    QColor backgroundColor = Qt::white; ///< Background color (used by all types)
    QColor gridColor = QColor(200, 200, 200); ///< Grid/line color
    int gridSpacing = 20;               ///< Grid spacing in pixels
    int lineSpacing = 24;               ///< Line spacing for ruled paper
    
    // ===== Bookmarks (Task 1.2.6) =====
    bool isBookmarked = false;          ///< True if this page has a bookmark
    QString bookmarkLabel;              ///< User-visible bookmark label/title
    
    // ===== Layers =====
    // Note: Using std::vector because QVector requires copyable types,
    // but std::unique_ptr is move-only
    std::vector<std::unique_ptr<VectorLayer>> vectorLayers;  ///< Layers (index 0 = bottom)
    int activeLayerIndex = 0;                                 ///< Currently active layer
    
    // ===== Inserted Objects =====
    std::vector<std::unique_ptr<InsertedObject>> objects;    ///< All inserted objects
    
    // ===== Constructors & Rule of Five =====
    
    /**
     * @brief Default constructor.
     * Creates an empty page with default size and one layer.
     */
    Page();
    
    /**
     * @brief Constructor with size.
     * @param pageSize The page dimensions.
     */
    explicit Page(const QSizeF& pageSize);
    
    /**
     * @brief Destructor.
     */
    ~Page() = default;
    
    // Page is non-copyable due to unique_ptr members
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    
    // Page is movable
    Page(Page&&) = default;
    Page& operator=(Page&&) = default;
    
    // ===== Layer Management =====
    
    /**
     * @brief Get the currently active layer.
     * @return Pointer to active layer, or nullptr if no layers exist.
     */
    VectorLayer* activeLayer();
    
    /**
     * @brief Get the currently active layer (const version).
     */
    const VectorLayer* activeLayer() const;
    
    /**
     * @brief Add a new layer at the top.
     * @param name Name for the new layer.
     * @return Pointer to the new layer.
     */
    VectorLayer* addLayer(const QString& name = "New Layer");
    
    /**
     * @brief Remove a layer by index.
     * @param index The layer index to remove.
     * @return True if removed, false if index out of range or only one layer.
     * 
     * Will not remove the last layer (always keeps at least one).
     */
    bool removeLayer(int index);
    
    /**
     * @brief Move a layer from one position to another.
     * @param from Source index.
     * @param to Destination index.
     * @return True if moved, false if indices out of range.
     */
    bool moveLayer(int from, int to);
    
    /**
     * @brief Get the number of layers.
     */
    int layerCount() const { return static_cast<int>(vectorLayers.size()); }
    
    /**
     * @brief Get a layer by index.
     * @param index The layer index.
     * @return Pointer to layer, or nullptr if index out of range.
     */
    VectorLayer* layer(int index);
    const VectorLayer* layer(int index) const;
    
    // ===== Object Management =====
    
    /**
     * @brief Add an object to the page.
     * @param obj The object to add (ownership transferred).
     */
    void addObject(std::unique_ptr<InsertedObject> obj);
    
    /**
     * @brief Remove an object by ID.
     * @param id The object ID to remove.
     * @return True if removed, false if not found.
     */
    bool removeObject(const QString& id);
    
    /**
     * @brief Find an object at a given point.
     * @param pt Point in page coordinates.
     * @return Pointer to topmost object containing the point, or nullptr.
     * 
     * Objects are checked in reverse z-order (topmost first).
     */
    InsertedObject* objectAtPoint(const QPointF& pt);
    
    /**
     * @brief Get an object by ID.
     * @param id The object ID.
     * @return Pointer to object, or nullptr if not found.
     */
    InsertedObject* objectById(const QString& id);
    
    /**
     * @brief Get the number of objects.
     */
    int objectCount() const { return static_cast<int>(objects.size()); }
    
    /**
     * @brief Sort objects by z-order.
     * 
     * Call after modifying z-order values to ensure correct rendering order.
     */
    void sortObjectsByZOrder();
    
    // ===== Rendering =====
    
    /**
     * @brief Render the page to a painter.
     * @param painter The QPainter to render to.
     * @param pdfBackground Optional pre-rendered PDF background pixmap.
     * @param zoom Zoom level (1.0 = 100%).
     * 
     * This is used for export/preview. Live rendering is handled by Viewport.
     * Renders in order: background → layers (bottom to top) → objects (by z-order).
     */
    void render(QPainter& painter, const QPixmap* pdfBackground = nullptr, qreal zoom = 1.0) const;
    
    /**
     * @brief Render just the background.
     * @param painter The QPainter to render to.
     * @param pdfBackground Optional pre-rendered PDF background pixmap.
     * @param zoom Zoom level.
     */
    void renderBackground(QPainter& painter, const QPixmap* pdfBackground = nullptr, qreal zoom = 1.0) const;
    
    /**
     * @brief Render just the inserted objects (Task 1.3.7).
     * @param painter The QPainter to render to.
     * @param zoom Zoom level.
     * 
     * This is separated from render() to allow DocumentViewport to use
     * cached layer rendering while still rendering objects.
     */
    void renderObjects(QPainter& painter, qreal zoom = 1.0) const;
    
    // ===== Serialization =====
    
    /**
     * @brief Serialize page to JSON.
     * @return JSON object containing all page data.
     */
    QJsonObject toJson() const;
    
    /**
     * @brief Deserialize page from JSON.
     * @param obj JSON object containing page data.
     * @return Page with data loaded from JSON.
     * 
     * Note: Images in objects are not loaded - call loadImages() separately.
     */
    static std::unique_ptr<Page> fromJson(const QJsonObject& obj);
    
    /**
     * @brief Load all images in objects from disk.
     * @param basePath Base directory for resolving relative paths.
     * @return Number of images successfully loaded.
     */
    int loadImages(const QString& basePath);
    
    // ===== Factory Methods =====
    
    /**
     * @brief Create a default empty page.
     * @param pageSize The page dimensions.
     * @return New page with one empty layer.
     */
    static std::unique_ptr<Page> createDefault(const QSizeF& pageSize);
    
    /**
     * @brief Create a page for a PDF background.
     * @param pageSize The page dimensions.
     * @param pdfPage The PDF page number.
     * @return New page configured for PDF background.
     */
    static std::unique_ptr<Page> createForPdf(const QSizeF& pageSize, int pdfPage);
    
    // ===== Utility =====
    
    /**
     * @brief Check if page has any content (strokes or objects).
     */
    bool hasContent() const;
    
    /**
     * @brief Clear all content (strokes and objects).
     */
    void clearContent();
    
    /**
     * @brief Get the bounding rect of all content.
     * @return Bounding rectangle, or empty rect if no content.
     * 
     * Useful for edgeless canvas mode.
     */
    QRectF contentBoundingRect() const;
};
