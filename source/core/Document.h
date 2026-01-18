#pragma once

// ============================================================================
// Document - The central data structure for a notebook
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3)
//
// Document represents an open notebook and owns:
// - All Pages (paged or edgeless mode)
// - PDF reference (external, not embedded)
// - Metadata (name, author, dates, settings)
// - Bookmarks
//
// Document is a pure data class - rendering and input are handled by
// DocumentViewport (Phase 1.3).
// ============================================================================

#include "Page.h"
#include "../pdf/PdfProvider.h"

#include <QCoreApplication>  // For translate() in displayName()
#include <QString>
#include <QDateTime>
#include <QColor>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QSet>
#include <QHash>
#include <vector>
#include <map>
#include <set>
#include <memory>

// ============================================================================
// LayerDefinition - Layer metadata for edgeless mode manifest (Phase 5.6)
// ============================================================================

/**
 * @brief Layer metadata for edgeless mode manifest.
 * 
 * In edgeless mode, layer structure (name, visibility, opacity, locked) is
 * stored once in the document manifest. Individual tiles only store strokes
 * with layer IDs - they don't duplicate layer metadata.
 * 
 * This enables O(1) layer operations (add/remove/rename/reorder) without
 * touching any tile files.
 */
struct LayerDefinition {
    QString id;                     ///< UUID for tracking
    QString name;                   ///< User-visible layer name
    bool visible = true;            ///< Whether layer is rendered
    qreal opacity = 1.0;            ///< Layer opacity (0.0 to 1.0)
    bool locked = false;            ///< If true, layer cannot be edited
    
    /**
     * @brief Serialize to JSON.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["visible"] = visible;
        obj["opacity"] = opacity;
        obj["locked"] = locked;
        return obj;
    }
    
    /**
     * @brief Deserialize from JSON.
     */
    static LayerDefinition fromJson(const QJsonObject& obj) {
        LayerDefinition def;
        def.id = obj["id"].toString();
        def.name = obj["name"].toString("Layer");
        def.visible = obj["visible"].toBool(true);
        def.opacity = obj["opacity"].toDouble(1.0);
        def.locked = obj["locked"].toBool(false);
        
        // Generate UUID if missing (for backwards compatibility)
        if (def.id.isEmpty()) {
            def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        return def;
    }
};

// ============================================================================

/**
 * @brief The central data structure representing an open notebook.
 * 
 * Document is the in-memory representation of a .snb notebook bundle.
 * It owns all pages, references external PDFs, and manages metadata.
 * 
 * Supports two modes:
 * - Paged: Traditional page-based document (multiple pages)
 * - Edgeless: Single infinite canvas (one unbounded page)
 */
class Document {
public:
    // ===== Bundle Format Version =====
    
    /**
     * @brief Current bundle format version.
     * 
     * Increment this when making breaking changes to the bundle structure.
     * Used for forward compatibility checks - if a bundle was created with
     * a newer version of SpeedyNote, we warn the user.
     * 
     * Version history:
     * - 1: Initial .snb bundle format (2026-01)
     * - 2: Added pdf_relative_path for portable .snbx packages (2026-01)
     */
    static constexpr int BUNDLE_FORMAT_VERSION = 2;
    
    // ===== Document Mode =====
    
    /**
     * @brief The document layout mode.
     */
    enum class Mode {
        Paged,      ///< Traditional page-based document
        Edgeless    ///< Single infinite canvas
    };
    
    // ===== Identity & Metadata =====
    QString id;                         ///< UUID for tracking
    QString name;                       ///< Display name (notebook title)
    QString author;                     ///< Optional author field
    QDateTime created;                  ///< Creation timestamp
    QDateTime lastModified;             ///< Last modification timestamp
    // NOTE: formatVersion removed - use BUNDLE_FORMAT_VERSION constant instead
    
    // ===== Document Mode =====
    Mode mode = Mode::Paged;            ///< Layout mode
    
    // ===== Default Page Settings =====
    // These are applied to new pages created in this document
    Page::BackgroundType defaultBackgroundType = Page::BackgroundType::None;
    QColor defaultBackgroundColor = Qt::white;
    QColor defaultGridColor = QColor(200, 200, 200);
    int defaultGridSpacing = 32;
    int defaultLineSpacing = 32;
    QSizeF defaultPageSize = QSizeF(816, 1056);  ///< Default page size (US Letter at 96 DPI)
    
    // ===== State =====
    bool modified = false;              ///< True if document has unsaved changes
    int lastAccessedPage = 0;           ///< Last viewed page index (for restoring position)
    
    // ===== Constructors & Rule of Five =====
    
    /**
     * @brief Default constructor.
     * Creates a new document with a unique ID and current timestamp.
     */
    Document();
    
    /**
     * @brief Destructor.
     * Cleans up pages and PDF provider (via unique_ptr).
     * In debug builds, logs destruction for memory leak detection.
     */
    ~Document();
    
    // Document is non-copyable due to unique_ptr members
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    
    // Document is movable
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;
    
    // ===== Factory Methods =====
    
    /**
     * @brief Create a new empty document.
     * @param docName Display name for the document.
     * @param docMode Layout mode (Paged or Edgeless).
     * @return New document with one empty page.
     */
    static std::unique_ptr<Document> createNew(const QString& docName, 
                                                Mode docMode = Mode::Paged);
    
    /**
     * @brief Create a new document for annotating a PDF.
     * @param docName Display name for the document.
     * @param pdfPath Path to the PDF file.
     * @return New document configured for PDF annotation, or nullptr on failure.
     * 
     * Creates one page per PDF page, each with BackgroundType::PDF.
     */
    static std::unique_ptr<Document> createForPdf(const QString& docName,
                                                   const QString& pdfPath);
    
    // ===== Utility =====
    
    /**
     * @brief Mark the document as modified.
     */
    void markModified() { modified = true; lastModified = QDateTime::currentDateTime(); }
    
    /**
     * @brief Clear the modified flag.
     * Call after saving.
     */
    void clearModified() { modified = false; }
    
    /**
     * @brief Get a display title for the document.
     * @return The name if set, otherwise "Untitled" (translated).
     */
    QString displayName() const { 
        return name.isEmpty() 
            ? QCoreApplication::translate("Document", "Untitled") 
            : name; 
    }
    
    /**
     * @brief Check if this is an edgeless (infinite canvas) document.
     */
    bool isEdgeless() const { return mode == Mode::Edgeless; }
    
    /**
     * @brief Check if this is a paged document.
     */
    bool isPaged() const { return mode == Mode::Paged; }
    
    // =========================================================================
    // Edgeless Tile Management (Phase E1)
    // =========================================================================
    
    /// Fixed tile size for edgeless mode (1024x1024 pixels)
    static constexpr int EDGELESS_TILE_SIZE = 1024;
    
    /// Type alias for tile coordinate
    using TileCoord = std::pair<int,int>;
    
    /**
     * @brief Get the tile coordinate for a document point.
     * @param docPt Point in document coordinates.
     * @return Tile coordinate (tx, ty).
     */
    TileCoord tileCoordForPoint(QPointF docPt) const;
    
    /**
     * @brief Get a tile by coordinate (does not create if missing).
     * @param tx Tile X coordinate.
     * @param ty Tile Y coordinate.
     * @return Pointer to tile, or nullptr if tile doesn't exist.
     */
    Page* getTile(int tx, int ty) const;
    
    /**
     * @brief Get or create a tile at the given coordinate.
     * @param tx Tile X coordinate.
     * @param ty Tile Y coordinate.
     * @return Pointer to the tile (never nullptr).
     */
    Page* getOrCreateTile(int tx, int ty);
    
    /**
     * @brief Get all tiles that intersect a document rectangle.
     * @param docRect Rectangle in document coordinates.
     * @return Vector of tile coordinates.
     */
    QVector<TileCoord> tilesInRect(QRectF docRect) const;
    
    /**
     * @brief Remove a tile if it has no content.
     * @param tx Tile X coordinate.
     * @param ty Tile Y coordinate.
     */
    void removeTileIfEmpty(int tx, int ty);
    
    /**
     * @brief Get the number of tiles in the edgeless canvas.
     * @return Tile count.
     */
    int tileCount() const { return static_cast<int>(m_tiles.size()); }
    
    /**
     * @brief Get count of tiles indexed on disk (for lazy loading).
     * @return Number of tiles in the disk index.
     */
    int tileIndexCount() const { return static_cast<int>(m_tileIndex.size()); }
    
    /**
     * @brief Get all tile coordinates.
     * @return Vector of tile coordinates.
     */
    QVector<TileCoord> allTileCoords() const;
    
    /**
     * @brief Get all tile coordinates currently loaded in memory.
     * @return Vector of tile coordinates that are in m_tiles.
     */
    QVector<TileCoord> allLoadedTileCoords() const;
    
    // =========================================================================
    // Object Extent Tracking (Phase O1.5)
    // =========================================================================
    
    /**
     * @brief Get the maximum object extent.
     * @return Largest dimension (width or height) of any object in the document.
     * 
     * Used by DocumentViewport to calculate extra tile loading margin.
     * If no objects exist, returns 0.
     */
    int maxObjectExtent() const { return m_maxObjectExtent; }
    
    /**
     * @brief Update the maximum object extent based on an object's size.
     * @param obj The object to consider.
     * 
     * Call this when adding an object or resizing an existing one.
     * Updates m_maxObjectExtent if the object's largest dimension is greater.
     */
    void updateMaxObjectExtent(const InsertedObject* obj);
    
    /**
     * @brief Recalculate the maximum object extent from all objects.
     * 
     * Call this after removing an object (the removed object might have been the largest).
     * Scans all tiles/pages to find the new maximum. May be slow with many tiles.
     */
    void recalculateMaxObjectExtent();
    
    // =========================================================================
    // Tile Persistence (Phase E5)
    // =========================================================================
    
    /**
     * @brief Set the bundle path for saving/loading tiles.
     * @param path Path to the .snb directory.
     */
    void setBundlePath(const QString& path) { m_bundlePath = path; }
    
    /**
     * @brief Get the bundle path.
     * @return Path to the .snb directory, or empty if not set.
     */
    QString bundlePath() const { return m_bundlePath; }
    
    /**
     * @brief Get the path to the assets directory.
     * @return Full path to assets/, or empty if bundle path not set.
     * 
     * Phase M.1: Base path for all document assets (images, notes, etc.).
     */
    QString assetsPath() const { 
        return m_bundlePath.isEmpty() ? QString() : m_bundlePath + "/assets"; 
    }
    
    /**
     * @brief Get the path to the assets/images directory.
     * @return Full path to assets/images, or empty if bundle path not set.
     * 
     * Phase O1.6: Used for storing image files with hash-based names.
     * ImageObjects store just the filename, and this path is used
     * to resolve the full path when loading/saving.
     */
    QString assetsImagePath() const { 
        return m_bundlePath.isEmpty() ? QString() : m_bundlePath + "/assets/images"; 
    }
    
    /**
     * @brief Get the path to the assets/notes directory.
     * Creates the directory if it doesn't exist.
     * @return Full path to assets/notes/, or empty if bundle path not set.
     * 
     * Phase M.1: Used for storing markdown note files.
     */
    QString notesPath() const;
    
    /**
     * @brief Delete a markdown note file from the notes directory.
     * @param noteId The note UUID (filename without .md extension).
     * @return true if deleted or file didn't exist, false on error.
     * 
     * Phase M.1: Used for cascade delete when clearing LinkSlots or deleting LinkObjects.
     */
    bool deleteNoteFile(const QString& noteId);
    
    /**
     * @brief Save all unsaved ImageObjects to the assets folder.
     * @param bundlePath Path to the bundle directory.
     * @return Number of images saved.
     * 
     * Phase O2: Called during saveBundle() to ensure all images are persisted.
     * ImageObjects with empty imagePath but valid cachedPixmap are saved.
     */
    int saveUnsavedImages(const QString& bundlePath);
    
    /**
     * @brief Clean up orphaned asset files from the assets folder.
     * 
     * Phase C.0.4: Scans the assets/images directory and deletes files
     * that are no longer referenced by any ImageObject in the document.
     * 
     * Should be called when closing a document to free disk space.
     * Safe to call on unsaved documents (no-op if bundlePath is empty).
     */
    void cleanupOrphanedAssets();
    
    /**
     * @brief Check if lazy loading from disk is enabled.
     */
    bool isLazyLoadEnabled() const { return m_lazyLoadEnabled; }
    
    /**
     * @brief Save a single tile to disk.
     * @param coord Tile coordinate to save.
     * @return True if saved successfully.
     */
    bool saveTile(TileCoord coord);
    
    /**
     * @brief Load a single tile from disk into memory.
     * @param coord Tile coordinate to load.
     * @return True if loaded successfully.
     */
    bool loadTileFromDisk(TileCoord coord) const;
    
    /**
     * @brief Mark a tile as dirty (modified since last save).
     * @param coord Tile coordinate.
     */
    void markTileDirty(TileCoord coord);
    
    /**
     * @brief Check if a tile is dirty.
     * @param coord Tile coordinate.
     * @return True if tile has unsaved changes.
     */
    bool isTileDirty(TileCoord coord) const { return m_dirtyTiles.count(coord) > 0; }
    
    /**
     * @brief Evict a tile from memory (save if dirty first).
     * @param coord Tile coordinate to evict.
     * 
     * The tile coord remains in m_tileIndex so it can be reloaded later.
     */
    void evictTile(TileCoord coord);
    
    /**
     * @brief Check if a tile is currently loaded in memory.
     * @param coord Tile coordinate.
     */
    bool isTileLoaded(TileCoord coord) const { return m_tiles.find(coord) != m_tiles.end(); }
    
    /**
     * @brief Check if a tile exists on disk.
     * @param coord Tile coordinate.
     */
    bool tileExistsOnDisk(TileCoord coord) const { return m_tileIndex.count(coord) > 0; }
    
    // Phase 5.6.5: syncTileLayerStructure() removed - layer structure now comes from manifest
    
    /**
     * @brief Save the entire document as a bundle.
     * @param path Path to the .snb directory.
     * @return True if saved successfully.
     */
    bool saveBundle(const QString& path);
    
    /**
     * @brief Load a document from a bundle (tiles lazy-loaded).
     * @param path Path to the .snb directory.
     * @return Loaded document, or nullptr on error.
     */
    static std::unique_ptr<Document> loadBundle(const QString& path);
    
    /**
     * @brief Peek at a bundle's document ID without fully loading it.
     * @param path Path to the .snb directory.
     * @return Document ID (UUID), or empty string on error.
     * 
     * This is a lightweight operation that only reads the manifest file
     * to extract the document ID. Used for duplicate detection before
     * loading a full document.
     */
    static QString peekBundleId(const QString& path);
    
    /**
     * @brief Check if there are any unsaved tile changes.
     */
    bool hasUnsavedTileChanges() const { return !m_dirtyTiles.empty(); }
    
    // =========================================================================
    // Edgeless Layer Manifest (Phase 5.6)
    // =========================================================================
    
    /**
     * @brief Get the number of layers in the edgeless manifest.
     * @return Layer count. Note: createNew() and loadBundle() ensure >= 1.
     */
    int edgelessLayerCount() const { return static_cast<int>(m_edgelessLayers.size()); }
    
    /**
     * @brief Get a layer definition by index.
     * @param index 0-based layer index.
     * @return Pointer to layer definition, or nullptr if out of range.
     */
    const LayerDefinition* edgelessLayerDef(int index) const;
    
    /**
     * @brief Get layer ID by index.
     * @param index 0-based layer index.
     * @return Layer ID, or empty string if out of range.
     */
    QString edgelessLayerId(int index) const;
    
    /**
     * @brief Add a new layer to the edgeless manifest.
     * @param name Layer name.
     * @return Index of the new layer.
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    int addEdgelessLayer(const QString& name);
    
    /**
     * @brief Remove a layer from the edgeless manifest.
     * @param index 0-based layer index.
     * @return True if removed, false if invalid index or only one layer remains.
     * 
     * This operation loads ALL evicted tiles from disk to ensure strokes
     * on the removed layer are properly deleted everywhere. This may be slow
     * if many tiles are evicted.
     */
    bool removeEdgelessLayer(int index);
    
    /**
     * @brief Move a layer in the edgeless manifest.
     * @param from Source index.
     * @param to Destination index.
     * @return True if moved, false if indices invalid.
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    bool moveEdgelessLayer(int from, int to);
    
    /**
     * @brief Phase 5.4: Merge multiple layers into one.
     * @param targetIndex The layer that will receive all strokes.
     * @param sourceIndices The layers to merge into target (will be removed).
     * @return True if merge succeeded.
     * 
     * For each loaded tile:
     * 1. Collects strokes from source layers
     * 2. Adds them to target layer
     * 3. Removes source layers from tile
     * Then removes source layers from manifest.
     */
    bool mergeEdgelessLayers(int targetIndex, const QVector<int>& sourceIndices);
    
    /**
     * @brief Phase 5.5: Duplicate a layer with all its strokes.
     * @param index The layer to duplicate.
     * @return Index of the new layer, or -1 on failure.
     * 
     * Creates a copy of the layer with name "OriginalName Copy".
     * All strokes are deep-copied with new UUIDs.
     * New layer is inserted above the original (at index + 1).
     */
    int duplicateEdgelessLayer(int index);
    
    /**
     * @brief Set layer visibility in the edgeless manifest.
     * @param index 0-based layer index.
     * @param visible New visibility state.
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    void setEdgelessLayerVisible(int index, bool visible);
    
    /**
     * @brief Set layer name in the edgeless manifest.
     * @param index 0-based layer index.
     * @param name New layer name.
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    void setEdgelessLayerName(int index, const QString& name);
    
    /**
     * @brief Set layer opacity in the edgeless manifest.
     * @param index 0-based layer index.
     * @param opacity New opacity (0.0 to 1.0).
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    void setEdgelessLayerOpacity(int index, qreal opacity);
    
    /**
     * @brief Set layer locked state in the edgeless manifest.
     * @param index 0-based layer index.
     * @param locked New locked state.
     * 
     * Marks manifest as dirty. Does NOT touch any tiles.
     */
    void setEdgelessLayerLocked(int index, bool locked);
    
    /**
     * @brief Get the active layer index for edgeless mode.
     * @return 0-based layer index.
     */
    int edgelessActiveLayerIndex() const { return m_edgelessActiveLayerIndex; }
    
    /**
     * @brief Set the active layer index for edgeless mode.
     * @param index 0-based layer index.
     * 
     * Marks manifest as dirty if changed.
     */
    void setEdgelessActiveLayerIndex(int index);
    
    /**
     * @brief Check if the edgeless manifest has unsaved changes.
     */
    bool isEdgelessManifestDirty() const { return m_edgelessManifestDirty; }
    
    /**
     * @brief Get all layer definitions (read-only).
     * @return Const reference to the layer definitions vector.
     */
    const std::vector<LayerDefinition>& edgelessLayers() const { return m_edgelessLayers; }
    
    // =========================================================================
    // PDF Reference Management (Task 1.2.4)
    // =========================================================================
    
    /**
     * @brief Check if this document has a PDF reference (path set).
     * @return True if pdfPath is set, even if PDF is not currently loaded.
     */
    bool hasPdfReference() const { return !m_pdfPath.isEmpty(); }
    
    /**
     * @brief Check if the PDF is currently loaded and valid.
     * @return True if pdfProvider is valid and the PDF is loaded.
     */
    bool isPdfLoaded() const { return m_pdfProvider && m_pdfProvider->isValid(); }
    
    /**
     * @brief Check if the PDF file exists at the referenced path.
     * @return True if the file exists on disk.
     */
    bool pdfFileExists() const;
    
    /**
     * @brief Get the path to the referenced PDF file.
     * @return The PDF path, or empty string if no PDF is referenced.
     */
    QString pdfPath() const { return m_pdfPath; }
    
    /**
     * @brief Get the relative path to the PDF file.
     * @return Relative path (from document.json location), or empty if not set.
     * 
     * Phase SHARE: Used for portable .snbx packages. When importing, if the
     * absolute path fails, the relative path is tried. Relative paths are
     * calculated from the document.json file location.
     */
    QString pdfRelativePath() const { return m_pdfRelativePath; }
    
    /**
     * @brief Set the relative path to the PDF file.
     * @param path Relative path from document.json location.
     */
    void setPdfRelativePath(const QString& path) { m_pdfRelativePath = path; }
    
    /**
     * @brief Check if PDF needs to be relinked.
     * @return True if neither absolute nor relative path could locate the PDF.
     * 
     * Phase SHARE: Set by loadBundle() when PDF path resolution fails.
     * DocumentManager checks this flag and shows PdfRelinkDialog if true.
     */
    bool needsPdfRelink() const { return m_needsPdfRelink; }
    
    /**
     * @brief Clear the PDF relink flag.
     * 
     * Call after successfully relinking or if user chooses to continue without PDF.
     */
    void clearNeedsPdfRelink() { m_needsPdfRelink = false; }
    
    /**
     * @brief Get the PDF provider for advanced operations.
     * @return Pointer to the provider, or nullptr if not loaded.
     * 
     * Use this for accessing text boxes, links, outline, etc.
     */
    const PdfProvider* pdfProvider() const { return m_pdfProvider.get(); }
    
    /**
     * @brief Load a PDF file.
     * @param path Path to the PDF file.
     * @return True if loaded successfully.
     * 
     * If a PDF is already loaded, it will be unloaded first.
     * Sets m_pdfPath even if loading fails (for relink functionality).
     */
    bool loadPdf(const QString& path);
    
    /**
     * @brief Relink to a different PDF file.
     * @param newPath Path to the new PDF file.
     * @return True if the new PDF was loaded successfully.
     * 
     * Use this when the user locates a moved/renamed PDF.
     * Marks the document as modified if successful.
     */
    bool relinkPdf(const QString& newPath);
    
    /**
     * @brief Unload the PDF and clear the reference.
     * 
     * Releases PDF resources but keeps the path for potential relink.
     */
    void unloadPdf();
    
    /**
     * @brief Clear the PDF reference entirely.
     * 
     * Unloads PDF and clears the path. Document becomes a blank notebook.
     */
    void clearPdfReference();
    
    /**
     * @brief Compute SHA-256 hash of first 1MB of a PDF file.
     * @param path Path to the PDF file.
     * @return Hash string in format "sha256:{hex}", or empty string on error.
     * 
     * Used for verifying that a relinked PDF is the same file.
     * Only hashes first 1MB for performance with large files.
     */
    static QString computePdfHash(const QString& path);
    
    /**
     * @brief Get the size of a PDF file.
     * @param path Path to the PDF file.
     * @return File size in bytes, or -1 on error.
     */
    static qint64 getPdfFileSize(const QString& path);
    
    /**
     * @brief Get the stored PDF hash.
     * @return Hash string, or empty if not set (legacy document).
     */
    QString pdfHash() const { return m_pdfHash; }
    
    /**
     * @brief Get the stored PDF file size.
     * @return File size in bytes, or 0 if not set.
     */
    qint64 pdfSize() const { return m_pdfSize; }
    
    /**
     * @brief Verify that a PDF file matches the stored hash.
     * @param path Path to the PDF file to verify.
     * @return True if hash matches or no hash stored (legacy), false if mismatch.
     * 
     * Used when relinking to check if user selected the correct PDF.
     */
    bool verifyPdfHash(const QString& path) const;
    
    /**
     * @brief Render a PDF page to an image.
     * @param pageIndex 0-based page index.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered image, or null image if not available.
     */
    QImage renderPdfPageToImage(int pageIndex, qreal dpi = 96.0) const;
    
    /**
     * @brief Render a PDF page to a pixmap.
     * @param pageIndex 0-based page index.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered pixmap, or null pixmap if not available.
     */
    QPixmap renderPdfPageToPixmap(int pageIndex, qreal dpi = 96.0) const;
    
    /**
     * @brief Get the number of pages in the PDF.
     * @return Page count, or 0 if no PDF is loaded.
     */
    int pdfPageCount() const;
    
    /**
     * @brief Get the size of a PDF page.
     * @param pageIndex 0-based page index.
     * @return Page size in PDF points (72 dpi), or invalid size if not available.
     */
    QSizeF pdfPageSize(int pageIndex) const;
    
    /**
     * @brief Get the PDF title metadata.
     * @return Title string, or empty if not available.
     */
    QString pdfTitle() const;
    
    /**
     * @brief Get the PDF author metadata.
     * @return Author string, or empty if not available.
     */
    QString pdfAuthor() const;
    
    /**
     * @brief Check if the PDF has an outline (table of contents).
     * @return True if outline is available.
     */
    bool pdfHasOutline() const;
    
    /**
     * @brief Get the PDF outline.
     * @return Vector of outline items, or empty if not available.
     */
    QVector<PdfOutlineItem> pdfOutline() const;
    
    // =========================================================================
    // Page Management (Task 1.2.5)
    // =========================================================================
    
    /**
     * @brief Get the number of pages in the document.
     * @return Page count (always >= 1 after ensureMinimumPages).
     * 
     * Phase O1.7: Returns m_pageOrder.size() in lazy loading mode.
     */
    int pageCount() const { 
        return m_pageOrder.isEmpty() 
            ? static_cast<int>(m_pages.size()) 
            : m_pageOrder.size(); 
    }
    
    /**
     * @brief Get a page by index.
     * @param index 0-based page index.
     * @return Pointer to the page, or nullptr if index is out of range.
     */
    Page* page(int index);
    
    /**
     * @brief Get a page by index (const version).
     * @param index 0-based page index.
     * @return Const pointer to the page, or nullptr if index is out of range.
     */
    const Page* page(int index) const;
    
    // ===== Paged Mode Lazy Loading Accessors (Phase O1.7) =====
    
    /**
     * @brief Check if a page is currently loaded in memory.
     * @param index 0-based page index.
     * @return True if page is loaded, false if on disk or invalid index.
     */
    bool isPageLoaded(int index) const;
    
    /**
     * @brief Get indices of all currently loaded pages.
     * @return Vector of page indices that are currently in memory.
     * 
     * PERF: This allows iterating only over loaded pages instead of all pages,
     * avoiding O(n) iterations through potentially thousands of pages.
     */
    QVector<int> loadedPageIndices() const;
    
    /**
     * @brief Get the UUID of a page by index.
     * @param index 0-based page index.
     * @return Page UUID, or empty string if index is out of range.
     */
    QString pageUuidAt(int index) const;
    
    /**
     * @brief Get the size of a page without loading it.
     * @param index 0-based page index.
     * @return Page size from metadata, or invalid size if not available.
     * 
     * Used for layout calculations without loading full page content.
     */
    QSizeF pageSizeAt(int index) const;
    
    // ===== UUID→Index Lookup (Phase C.0.2) =====
    
    /**
     * @brief Get page index by UUID.
     * @param uuid Page UUID to look up.
     * @return 0-based page index, or -1 if not found.
     * 
     * Uses cached mapping for O(1) lookups. Cache is rebuilt O(n) only
     * when page order changes (insert/delete/move), not on every lookup.
     * 
     * Phase C.0.2: For LinkObject position links - enables stable cross-references.
     */
    int pageIndexByUuid(const QString& uuid) const;
    
    /**
     * @brief Invalidate the UUID→Index cache.
     * 
     * Call this when page order changes (insert/delete/move).
     * The cache will be rebuilt lazily on next pageIndexByUuid() call.
     */
    void invalidateUuidCache();
    
    /**
     * @brief Load a page from disk into memory.
     * @param index 0-based page index.
     * @return True if loaded successfully.
     * 
     * Only used in lazy loading mode (when m_pageOrder is populated).
     * Loads from pages/{uuid}.json file.
     */
    bool loadPageFromDisk(int index) const;
    
    /**
     * @brief Save a single page to disk.
     * @param index 0-based page index.
     * @return True if saved successfully.
     * 
     * Saves to pages/{uuid}.json file in the bundle.
     * Clears the page's dirty flag.
     */
    bool savePage(int index);
    
    /**
     * @brief Evict a page from memory (save if dirty first).
     * @param index 0-based page index.
     * 
     * The page UUID remains in m_pageOrder so it can be reloaded later.
     * Use for memory management when pages are no longer visible.
     */
    void evictPage(int index);
    
    /**
     * @brief Mark a page as dirty (modified since last save).
     * @param index 0-based page index.
     */
    void markPageDirty(int index);
    
    /**
     * @brief Check if a page is dirty.
     * @param index 0-based page index.
     * @return True if page has unsaved changes.
     */
    bool isPageDirty(int index) const;
    
    /**
     * @brief Add a new page at the end of the document.
     * @return Pointer to the newly created page.
     * 
     * The page inherits default settings from the document.
     * Marks the document as modified.
     */
    Page* addPage();
    
    /**
     * @brief Insert a new page at a specific position.
     * @param index Position to insert (0 = beginning).
     * @return Pointer to the newly created page, or nullptr if index invalid.
     * 
     * Existing pages at and after the index are shifted.
     * Marks the document as modified.
     */
    Page* insertPage(int index);
    
    /**
     * @brief Add a page configured for a specific PDF page.
     * @param pdfPageIndex 0-based PDF page index.
     * @return Pointer to the newly created page.
     * 
     * Sets the page's background to BackgroundType::PDF and stores the PDF page index.
     * Page size is set to match the PDF page size (scaled from 72 dpi to 96 dpi).
     * Marks the document as modified.
     */
    Page* addPageForPdf(int pdfPageIndex);
    
    /**
     * @brief Remove a page from the document.
     * @param index 0-based page index.
     * @return True if removed, false if index invalid or only one page remains.
     * 
     * Cannot remove the last page (use ensureMinimumPages constraint).
     * Marks the document as modified.
     */
    bool removePage(int index);
    
    /**
     * @brief Move a page from one position to another.
     * @param from Source index.
     * @param to Destination index.
     * @return True if moved, false if indices invalid.
     * 
     * Marks the document as modified.
     */
    bool movePage(int from, int to);
    
    /**
     * @brief Get the single page in edgeless mode.
     * @return Pointer to the edgeless page, or nullptr if not in edgeless mode.
     * 
     * In edgeless mode, there is exactly one unbounded page.
     */
    Page* edgelessPage();
    
    /**
     * @brief Get the single page in edgeless mode (const version).
     */
    const Page* edgelessPage() const;
    
    /**
     * @brief Ensure at least one page exists.
     * 
     * If the document has no pages, creates one with default settings.
     * Called automatically by factory methods.
     */
    void ensureMinimumPages();
    
    /**
     * @brief Find the document page index for a given PDF page.
     * @param pdfPageIndex 0-based PDF page index.
     * @return Document page index, or -1 if not found.
     * 
     * Searches pages with BackgroundType::PDF for matching pdfPageNumber.
     */
    int findPageByPdfPage(int pdfPageIndex) const;
    
    /**
     * @brief Create pages for all PDF pages.
     * 
     * Creates one document page per PDF page, each configured with
     * BackgroundType::PDF and the appropriate page size.
     * Clears existing pages first.
     */
    void createPagesForPdf();
    
    // =========================================================================
    // Bookmarks (Task 1.2.6)
    // =========================================================================
    
    /**
     * @brief Bookmark info structure for quick access.
     */
    struct Bookmark {
        int pageIndex;      ///< 0-based page index
        QString label;      ///< Bookmark label/title
    };
    
    /**
     * @brief Get all bookmarks in the document.
     * @return Vector of bookmarks sorted by page index.
     */
    QVector<Bookmark> getBookmarks() const;
    
    /**
     * @brief Set a bookmark on a page.
     * @param pageIndex 0-based page index.
     * @param label Bookmark label (optional, defaults to "Bookmark N").
     * 
     * If the page already has a bookmark, updates the label.
     * Marks the document as modified.
     */
    void setBookmark(int pageIndex, const QString& label = QString());
    
    /**
     * @brief Remove a bookmark from a page.
     * @param pageIndex 0-based page index.
     * 
     * No-op if page doesn't have a bookmark.
     * Marks the document as modified if bookmark was removed.
     */
    void removeBookmark(int pageIndex);
    
    /**
     * @brief Check if a page has a bookmark.
     * @param pageIndex 0-based page index.
     * @return True if page has a bookmark.
     */
    bool hasBookmark(int pageIndex) const;
    
    /**
     * @brief Get the bookmark label for a page.
     * @param pageIndex 0-based page index.
     * @return Bookmark label, or empty string if no bookmark.
     */
    QString bookmarkLabel(int pageIndex) const;
    
    /**
     * @brief Find the next bookmarked page after a given page.
     * @param fromPage 0-based page index to search from (exclusive).
     * @return Page index of next bookmark, or -1 if none found.
     * 
     * Wraps around to the beginning if no bookmark found after fromPage.
     */
    int nextBookmark(int fromPage) const;
    
    /**
     * @brief Find the previous bookmarked page before a given page.
     * @param fromPage 0-based page index to search from (exclusive).
     * @return Page index of previous bookmark, or -1 if none found.
     * 
     * Wraps around to the end if no bookmark found before fromPage.
     */
    int prevBookmark(int fromPage) const;
    
    /**
     * @brief Toggle bookmark on a page.
     * @param pageIndex 0-based page index.
     * @param label Label to use if adding bookmark.
     * @return True if bookmark was added, false if removed.
     */
    bool toggleBookmark(int pageIndex, const QString& label = QString());
    
    /**
     * @brief Get the total number of bookmarks.
     */
    int bookmarkCount() const;
    
    // =========================================================================
    // Serialization (Task 1.2.7)
    // =========================================================================
    
    /**
     * @brief Serialize document metadata to JSON.
     * @return JSON object containing document metadata.
     * 
     * Does NOT include page content (strokes, objects).
     * Use toFullJson() for complete serialization.
     */
    QJsonObject toJson() const;
    
    /**
     * @brief Create a document from metadata JSON.
     * @param obj JSON object containing document metadata.
     * @return New document with metadata loaded, or nullptr on error.
     * 
     * Pages are created but content is NOT loaded - call loadPagesFromJson() 
     * or read page data separately.
     */
    static std::unique_ptr<Document> fromJson(const QJsonObject& obj);
    
    /**
     * @brief Serialize complete document to JSON.
     * @return JSON object containing document metadata AND all page content.
     * 
     * Warning: Can be very large for documents with many strokes.
     */
    QJsonObject toFullJson() const;
    
    /**
     * @brief Create a complete document from full JSON.
     * @param obj JSON object containing document metadata and pages.
     * @return New document with all data loaded, or nullptr on error.
     */
    static std::unique_ptr<Document> fromFullJson(const QJsonObject& obj);
    
    /**
     * @brief Load page content from a pages JSON array.
     * @param pagesArray JSON array of page objects.
     * @return Number of pages successfully loaded.
     * 
     * Clears existing pages and creates new ones from JSON.
     * Use after fromJson() to load page content.
     */
    int loadPagesFromJson(const QJsonArray& pagesArray);
    
    /**
     * @brief Get pages as JSON array.
     * @return JSON array of page objects.
     */
    QJsonArray pagesToJson() const;
    
    /**
     * @brief Get default background settings as JSON.
     * @return JSON object with background settings.
     */
    QJsonObject defaultBackgroundToJson() const;
    
    /**
     * @brief Load default background settings from JSON.
     * @param obj JSON object with background settings.
     */
    void loadDefaultBackgroundFromJson(const QJsonObject& obj);
    
    /**
     * @brief Convert BackgroundType enum to string.
     */
    static QString backgroundTypeToString(Page::BackgroundType type);
    
    /**
     * @brief Convert string to BackgroundType enum.
     */
    static Page::BackgroundType stringToBackgroundType(const QString& str);
    
    /**
     * @brief Convert Mode enum to string.
     */
    static QString modeToString(Mode m);
    
    /**
     * @brief Convert string to Mode enum.
     */
    static Mode stringToMode(const QString& str);
    
private:
    // ===== PDF Reference (Task 1.2.4) =====
    QString m_pdfPath;                              ///< Path to external PDF file
    QString m_pdfRelativePath;                      ///< Relative path from document.json (Phase SHARE)
    QString m_pdfHash;                              ///< SHA-256 hash of first 1MB (format: "sha256:...")
    qint64 m_pdfSize = 0;                           ///< File size in bytes (for quick verification)
    bool m_needsPdfRelink = false;                  ///< True if PDF not found at either path (Phase SHARE)
    std::unique_ptr<PdfProvider> m_pdfProvider;    ///< Loaded PDF (may be null)
    
    // ===== Pages (Task 1.2.5) =====
    std::vector<std::unique_ptr<Page>> m_pages;    ///< All pages in paged mode (legacy, before O1.7)
    
    // ===== Paged Mode Lazy Loading (Phase O1.7) =====
    /// Ordered list of page UUIDs. Defines page order in the document.
    /// Pages are loaded on-demand from pages/{uuid}.json files.
    QStringList m_pageOrder;
    
    /// Minimal metadata for layout calculations without loading full pages.
    /// Key: page UUID, Value: page size (width, height).
    std::map<QString, QSizeF> m_pageMetadata;
    
    /// PDF page index for each page (for pristine PDF page synthesis).
    /// Key: page UUID, Value: PDF page index (0-based).
    /// Only contains entries for pages with PDF backgrounds.
    /// Pages not in this map are non-PDF pages (blank, grid, lines, etc.).
    std::map<QString, int> m_pagePdfIndex;
    
    /// Currently loaded pages. Key: page UUID, Value: Page object.
    /// Mutable for lazy loading in const methods like page().
    mutable std::map<QString, std::unique_ptr<Page>> m_loadedPages;
    
    /// Pages that have been modified since last save.
    mutable std::set<QString> m_dirtyPages;
    
    /// Pages that have been deleted and need cleanup on next save.
    std::set<QString> m_deletedPages;
    
    // ===== Tiles (Phase E1 - Edgeless Mode) =====
    /// Sparse 2D map of tiles for edgeless mode. Key = (tx, ty) tile coordinate.
    /// Uses std::map instead of QMap because QMap requires copyable values,
    /// but unique_ptr is move-only.
    mutable std::map<std::pair<int,int>, std::unique_ptr<Page>> m_tiles;
    
    // ===== Tile Persistence (Phase E5) =====
    QString m_bundlePath;                           ///< Path to .snb bundle directory
    mutable std::set<TileCoord> m_tileIndex;        ///< All tile coords that exist on disk (mutable for lazy-load failure cleanup)
    mutable std::set<TileCoord> m_dirtyTiles;       ///< Tiles modified since last save
    std::set<TileCoord> m_deletedTiles;             ///< Tiles to delete from disk on next save
    bool m_lazyLoadEnabled = false;                 ///< True after loading from bundle
    
    // ===== Object Extent Tracking (Phase O1.5) =====
    /// Maximum extent (largest dimension) of any object in the document.
    /// Used to calculate extra tile loading margin in edgeless mode.
    /// Updated when objects are added or resized.
    /// Mutable because it can be updated during lazy tile loading (const method).
    mutable int m_maxObjectExtent = 0;
    
    // ===== Edgeless Layer Manifest (Phase 5.6) =====
    /// Layer definitions for edgeless mode. This is the single source of truth
    /// for layer structure (name, visibility, order). Tiles only store strokes.
    std::vector<LayerDefinition> m_edgelessLayers;
    
    /// Active layer index for edgeless mode (global across all tiles).
    int m_edgelessActiveLayerIndex = 0;
    
    /// True if layer manifest has unsaved changes (need to save document.json).
    bool m_edgelessManifestDirty = false;
    
    // ===== UUID→Index Cache (Phase C.0.2) =====
    /// Cached mapping from page UUID to index for O(1) lookups.
    /// Mutable for lazy rebuilding in const methods.
    mutable QHash<QString, int> m_uuidToIndexCache;
    
    /// True if cache needs rebuilding (page order changed).
    mutable bool m_uuidCacheDirty = true;
    
    /**
     * @brief Rebuild the UUID→Index cache from current page order.
     * Called lazily when cache is dirty and a lookup is requested.
     */
    void rebuildUuidCache() const;
    
    /**
     * @brief CR-L13: Load all evicted tiles from disk into memory.
     * 
     * This is needed before destructive layer operations (remove, merge)
     * to ensure strokes on affected layers are properly handled on ALL tiles,
     * not just the ones currently in memory.
     * 
     * May be slow with many evicted tiles, but ensures data consistency.
     */
    void loadAllEvictedTiles();
    
    /**
     * @brief Create a new page with document defaults applied.
     * @return Unique pointer to the new page.
     */
    std::unique_ptr<Page> createDefaultPage();
};
