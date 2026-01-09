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
#include <QSet>        // Phase O4.1: For excludeIds parameter
#include <vector>
#include <memory>
#include <map>

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
    QString uuid;               ///< Unique identifier for LinkObject position links (Phase C.0.1)
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
    int gridSpacing = 32;               ///< Grid spacing in pixels
    int lineSpacing = 32;               ///< Line spacing for ruled paper
    
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
    
    /**
     * @brief Objects grouped by layer affinity for efficient rendering.
     * 
     * Non-owning pointers into the `objects` vector, grouped by affinity value.
     * Key: layerAffinity value (-1, 0, 1, 2, ...)
     * Value: vector of object pointers with that affinity
     * 
     * This map is maintained by addObject(), removeObject(), and updateObjectAffinity().
     * Call rebuildAffinityMap() after bulk operations or loading from JSON.
     */
    std::map<int, std::vector<InsertedObject*>> objectsByAffinity;
    
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
     * @brief Phase O3.5.7: Handle object affinities when a layer is deleted.
     * @param deletedLayerIndex The index of the deleted layer (0-based).
     * 
     * Uses Option C (Move to layer below):
     * - Objects tied to deleted layer (affinity = deletedLayerIndex - 1) move down
     *   to affinity = deletedLayerIndex - 2 (or -1 if at bottom)
     * - Objects with higher affinity shift down by 1
     * 
     * Called automatically by removeLayer().
     */
    void handleLayerDeleted(int deletedLayerIndex);
    
    /**
     * @brief Move a layer from one position to another.
     * @param from Source index.
     * @param to Destination index.
     * @return True if moved, false if indices out of range.
     */
    bool moveLayer(int from, int to);
    
    /**
     * @brief Phase O3.5.6: Adjust object affinities after a layer move.
     * @param from Original layer index (before move).
     * @param to New layer index (after move).
     * 
     * When a layer moves from index `from` to index `to`:
     * - Objects tied to the moved layer (affinity = from - 1) get affinity = to - 1
     * - Objects tied to layers that shifted have their affinity adjusted
     * 
     * Called automatically by moveLayer().
     */
    void adjustObjectAffinitiesAfterLayerMove(int from, int to);
    
    /**
     * @brief Phase 5.4: Merge multiple layers into one.
     * @param targetIndex The layer that will receive all strokes.
     * @param sourceIndices The layers to merge into target (will be removed).
     * @return True if merge succeeded.
     */
    bool mergeLayers(int targetIndex, const QVector<int>& sourceIndices);
    
    /**
     * @brief Phase 5.5: Duplicate a layer with all its strokes.
     * @param index The layer to duplicate.
     * @return Index of the new layer, or -1 on failure.
     * 
     * Creates a copy with name "OriginalName Copy", inserted above original.
     * All strokes are deep-copied with new UUIDs.
     */
    int duplicateLayer(int index);
    
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
    
    /**
     * @brief Release all layer stroke caches to free memory.
     * Call this for pages that are far from the visible area.
     * Caches will be rebuilt lazily when the page becomes visible again.
     */
    void releaseLayerCaches();
    
    /**
     * @brief Check if any layer has a stroke cache allocated.
     * @return True if at least one layer has a cache using memory.
     */
    bool hasLayerCachesAllocated() const;
    
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
     * @brief Extract an object by ID (removes from page but returns ownership).
     * @param id The object ID to extract.
     * @return The extracted object, or nullptr if not found.
     * 
     * Phase O2.3.4: Used for moving objects between tiles in edgeless mode.
     * Unlike removeObject(), this returns the object instead of destroying it.
     */
    std::unique_ptr<InsertedObject> extractObject(const QString& id);
    
    /**
     * @brief Find an object at a given point.
     * @param pt Point in page coordinates.
     * @param affinityFilter Optional affinity filter. If provided (not INT_MIN),
     *        only objects with this exact affinity are considered.
     *        Phase O3.5.5: Strict filtering - only select objects tied to current layer.
     * @return Pointer to topmost object containing the point, or nullptr.
     * 
     * Objects are checked in reverse z-order (topmost first).
     */
    InsertedObject* objectAtPoint(const QPointF& pt, int affinityFilter = INT_MIN);
    
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
    
    /**
     * @brief Rebuild the objectsByAffinity map from the objects vector.
     * 
     * Call this after:
     * - Loading objects from JSON (Page::fromJson)
     * - Bulk operations that bypass addObject/removeObject
     * 
     * Individual addObject/removeObject/updateObjectAffinity calls maintain
     * the map incrementally, so this is not needed for single operations.
     */
    void rebuildAffinityMap();
    
    /**
     * @brief Update an object's layer affinity and re-group it.
     * @param id The object ID.
     * @param newAffinity The new affinity value.
     * @return True if object found and updated, false if not found.
     * 
     * This properly removes the object from its old affinity group
     * and adds it to the new one.
     */
    bool updateObjectAffinity(const QString& id, int newAffinity);
    
    // ===== Rendering =====
    
    /**
     * @brief Render the page to a painter.
     * @param painter The QPainter to render to.
     * @param pdfBackground Optional pre-rendered PDF background pixmap.
     * @param zoom Zoom level (1.0 = 100%).
     * 
     * @deprecated This method renders objects AFTER all layers, bypassing the affinity system.
     * For live rendering, DocumentViewport::renderPage() calls renderObjectsWithAffinity() 
     * for proper interleaved rendering based on layer affinity.
     * 
     * This method may be used for simple export/preview where affinity doesn't matter.
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
     * @brief Static helper to render a background pattern (Grid/Lines/None).
     * 
     * Used by both:
     * - Page::renderBackground() for existing pages
     * - DocumentViewport::renderEdgelessMode() for empty tile coordinates
     * 
     * This avoids duplicating grid/lines rendering logic.
     * 
     * @param painter The QPainter to render to (should be positioned at page/tile origin).
     * @param rect The rectangle to fill (in painter's coordinate system).
     * @param bgColor Background fill color.
     * @param bgType Background type (None, Grid, or Lines - PDF/Custom handled separately).
     * @param gridColor Color for grid/lines.
     * @param gridSpacing Spacing between grid lines (ignored for Lines type).
     * @param lineSpacing Spacing between horizontal lines (ignored for Grid type).
     * @param penWidth Pen width for grid/lines (typically 1.0).
     */
    static void renderBackgroundPattern(
        QPainter& painter,
        const QRectF& rect,
        const QColor& bgColor,
        BackgroundType bgType,
        const QColor& gridColor,
        qreal gridSpacing,
        qreal lineSpacing,
        qreal penWidth = 1.0
    );
    
    /**
     * @brief Render just the inserted objects (Task 1.3.7).
     * @param painter The QPainter to render to.
     * @param zoom Zoom level.
     * 
     * This is separated from render() to allow DocumentViewport to use
     * cached layer rendering while still rendering objects.
     * 
     * @deprecated Use renderObjectsWithAffinity() for proper layer interleaving.
     */
    void renderObjects(QPainter& painter, qreal zoom = 1.0) const;
    
    /**
     * @brief Render objects with a specific layer affinity.
     * @param painter The QPainter to render to.
     * @param zoom Zoom level.
     * @param affinity The affinity value to render (-1, 0, 1, 2, ...).
     * @param layerVisible Phase O3.5.8: If false, skip rendering (layer is hidden).
     *        Objects with affinity = K are tied to Layer K+1. When that layer is hidden,
     *        pass layerVisible=false to hide objects too.
     * 
     * This enables layer-interleaved rendering:
     * - renderObjectsWithAffinity(painter, zoom, -1) → objects below all strokes
     * - renderObjectsWithAffinity(painter, zoom, 0)  → objects above Layer 0
     * - renderObjectsWithAffinity(painter, zoom, 1)  → objects above Layer 1
     * - etc.
     * 
     * Objects within the same affinity group are sorted by zOrder.
     * 
     * @param excludeIds Phase O4.1: Optional set of object IDs to skip rendering.
     *        Used during background snapshot capture to exclude selected objects.
     */
    void renderObjectsWithAffinity(QPainter& painter, qreal zoom, int affinity, 
                                    bool layerVisible = true,
                                    const QSet<QString>* excludeIds = nullptr) const;
    
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
     * @param basePath Bundle path (e.g., "/path/to/notebook.snb").
     * @return Number of images successfully loaded.
     * 
     * Phase O1.6: Images are stored in assets/images/ subdirectory.
     * Each ImageObject's fullPath() resolves against basePath/assets/images/.
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
