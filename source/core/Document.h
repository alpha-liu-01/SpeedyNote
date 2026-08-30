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
#include "../ui/sidebars/LinkOutlineEntry.h"

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
#include <QFuture>
#include <QImage>
#include <QPixmap>
#include <QThreadPool>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QStringList>
#include <vector>
#include <map>
#include <set>
#include <memory>

class ImageObject;

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
// PdfSource - One external/bundled PDF a document draws page backgrounds from
// ============================================================================

/**
 * @brief A single PDF source registered with a document.
 *
 * A document owns an ordered list of sources. An explicit flag identifies the
 * optional primary source (the PDF a document was born from); list position is
 * not significant. The primary is mirrored to the legacy top-level
 * pdf_path/pdf_hash/pdf_size manifest keys for backward compatibility. A page
 * references its source by id via Page::pdfSourceId (empty id = primary).
 *
 * bundledFile / pageMap describe the compact in-bundle fallback generated for
 * pages imported from non-primary sources.
 */
struct PdfSource {
    QString id;                 ///< UUID identifying this source within the document
    QString path;               ///< Absolute path to the PDF file (may be stale -> relink)
    QString relativePath;       ///< Path relative to the bundle dir (portable .snbx)
    QString hash;               ///< SHA-256 of first 1MB ("sha256:...") of the original PDF
    qint64 size = 0;            ///< File size in bytes of the original PDF
    bool bundled = false;       ///< True if materialized into the bundle (mini-PDF)
    QString bundledFile;        ///< Relative path of the bundled mini-PDF (when bundled)
    QHash<int,int> pageMap;     ///< Original PDF page -> bundled-file page (when bundled)
    bool primary = false;       ///< True for the document's own base PDF (never bundled/minified,
                                ///< mirrored to legacy pdf_path). Imported sources are non-primary.
};

enum class PdfSourceHealthStatus {
    AvailableExternal,
    AvailableRelative,
    AvailableBundled,
    PartialBundled,
    Missing,
    Unreadable,
    IdentityMismatch
};

struct PdfSourceHealth {
    QString sourceId;           ///< Empty for the primary source, otherwise the registry UUID
    QString title;
    QString activePath;
    PdfSourceHealthStatus status = PdfSourceHealthStatus::Missing;
    int referencedPages = 0;
    int unavailablePages = 0;

    bool requiresRepair() const {
        return unavailablePages > 0
            && (status == PdfSourceHealthStatus::PartialBundled
            || status == PdfSourceHealthStatus::Missing
            || status == PdfSourceHealthStatus::Unreadable
            || status == PdfSourceHealthStatus::IdentityMismatch);
    }
};

// ============================================================================

/**
 * @brief Result of a cross-document page import (Plan B).
 *
 * Returned by Document::importPagesFrom. Carries the final, id-regenerated page
 * JSON for each inserted page (in insertion order, starting at destStartIndex)
 * plus old->new id maps that later plans (B-links) use to remap link targets.
 */
struct PageImportResult {
    QVector<QJsonObject> insertedPageJson;  ///< Final id-regenerated page JSON, in insertion order
    int destStartIndex = -1;                ///< Notebook index of the first inserted page
    QHash<QString, QString> pageUuidMap;    ///< Source page uuid -> new destination uuid
    QHash<QString, QString> objectIdMap;    ///< Source object id -> new object id
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
    static constexpr int BUNDLE_FORMAT_VERSION = 3;
    
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
    
    // ===== OCR Settings =====
    QString ocrLanguage;                ///< Per-document OCR recognizer name (empty = global fallback)
    bool ocrSnapToBackground = false;   ///< Snap OCR grouping to grid/line spacing

    // ===== PDF Display Overrides (PDF-backed documents only) =====
    // Tri-state: -1 = inherit global setting, 0 = off, 1 = on.
    int pdfInvertDarkOverride = -1;          ///< Invert PDF lightness in dark mode
    int pdfInvertIncludeImagesOverride = -1; ///< Invert entire page including images

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

    /**
     * @brief Get all tile coordinates that exist (union of in-memory and disk index).
     * @return Vector of tile coordinates from both m_tiles and m_tileIndex.
     */
    QVector<TileCoord> allKnownTileCoords() const;

    /**
     * @brief Monotonically-increasing counter bumped on every m_tiles mutation
     * (insert, erase, clear). Used by consumers that cache data derived from
     * the full loaded-tile set (e.g. the Highlighter's OCR-block cache) to
     * cheaply detect when the set has changed without hashing its contents.
     */
    quint64 tileLoadVersion() const { return m_tileLoadVersion; }

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
    void setBundlePath(const QString& path);
    
    /**
     * @brief Get the bundle path.
     * @return Path to the .snb directory, or empty if not set.
     */
    QString bundlePath() const { return m_bundlePath; }

    /**
     * @brief Get this Document instance's per-session id.
     *
     * Non-persistent, generated on construction and unique across the app's
     * lifetime. Used to tag OCR requests so asynchronous results can be
     * routed back to exactly the Document that queued them (and dropped if
     * that Document has been closed in the meantime). Not stored on disk.
     */
    QString sessionId() const { return m_sessionId; }
    
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
     * @brief Enumerate every LinkObject with at least one markdown slot.
     *
     * Returns a flat snapshot of the persistent outline cache (see
     * `buildLinkOutlineCache`).  The result is independent of which
     * pages / tiles are currently resident in RAM: evicted tiles still
     * contribute their LinkObjects.  No file I/O is performed on the hot
     * path — the cache is populated on first use and maintained
     * incrementally by `refreshLinkOutlineFor` / `dropLinkOutlineFor`.
     *
     * Used by the right-sidebar `NotesTreePanel` (Phase M.8) to build the
     * 3-level tree without having to read any .md file until the user
     * expands a LinkObject row.
     *
     * @return Flat list of entries; sorting/grouping is done by the caller.
     */
    QVector<LinkOutlineEntry> enumerateLinkOutline() const;

    /**
     * @brief One scroll-bar tick per page that owns at least one LinkObject.
     *
     * SB2 document map: unlike enumerateLinkOutline() (markdown-only, tree
     * building), this reduces EVERY page that carries any LinkObject to a
     * single marker at its topmost link (smallest local Y), for painting on
     * the enhanced scroll bar. Backed by the all-links marker cache
     * (`m_pageMarkers`) which shares invalidation with the outline cache, so
     * it costs no extra I/O on the hot path and never force-loads a page
     * (unloaded pages are peeked from disk). Paged-mode only; edgeless returns
     * an empty list.
     */
    struct PageLinkMarker {
        int pageIndex = -1;   ///< 0-based notebook page index.
        qreal localY = 0.0;   ///< Y of the topmost link in page-local coords.
        QColor color;         ///< iconColor of the topmost link (raw; may be default-gray).
        QString description;  ///< Tooltip text of the topmost link.
    };
    QVector<PageLinkMarker> pageLinkMarkers() const;

    // ========================================================================
    // Link Outline Cache (Phase M.9)
    // ========================================================================
    // The outline is a document-level summary of every LinkObject that
    // references at least one markdown note.  It is expensive to recompute
    // from disk every refresh and would otherwise be gated by which tiles
    // are currently resident (edgeless) or force a full page-load of every
    // page (paged).  We cache it per-container and keep it in sync with
    // authoritative in-memory / on-disk state through the helpers below.
    // ========================================================================

    /**
     * @brief Build (or rebuild) the entire outline cache in one pass.
     *
     * Walks loaded pages / tiles for authoritative data; for unloaded
     * tiles in `m_tileIndex`, peeks the tile JSON on disk via
     * `peekTileLinkOutlineFromDisk` without constructing a full `Page`.
     * For paged mode, uses loaded pages directly and falls back to a
     * page-file peek for unloaded ones.
     *
     * Idempotent.  Cheap to call multiple times; clears the cache first.
     */
    void buildLinkOutlineCache() const;

    /**
     * @brief Re-extract the outline contribution for a single tile from
     *        whatever is most authoritative (in-memory if loaded, else
     *        disk peek).
     *
     * Call after any LinkObject add / remove / edit on the tile, after
     * `loadTileFromDisk` or `saveTile` has run, and on any external signal
     * that says "this tile's links may have changed."
     */
    void refreshLinkOutlineFor(TileCoord coord) const;

    /**
     * @brief Re-extract the outline contribution for a single page.
     *        Paged-mode counterpart of the TileCoord overload.
     */
    void refreshLinkOutlineFor(int pageIndex) const;

    /**
     * @brief Drop a container's contribution from the cache (page/tile
     *        deletion).  Eviction-from-memory does NOT call this — the
     *        cache must survive eviction; that is the whole point.
     */
    void dropLinkOutlineFor(TileCoord coord) const;
    void dropLinkOutlineFor(int pageIndex) const;

    /// Clear the outline cache (e.g. bundle close).  Next query will
    /// rebuild.
    void clearLinkOutlineCache() const;

    /**
     * @brief Save all unsaved ImageObjects to the assets folder.
     * @param bundlePath Path to the bundle directory.
     * @return Number of images saved, or -1 if any asset failed.
     * 
     * Phase O2: Called during saveBundle() to ensure all images are persisted.
     * ImageObjects with empty imagePath but valid cachedPixmap are saved.
     */
    int saveUnsavedImages(const QString& bundlePath);

    /**
     * @brief Persist a fresh image without blocking the GUI thread.
     *
     * File imports write their retained original bytes. Clipboard images pass
     * an immutable QImage which the worker encodes once as lossless PNG.
     */
    bool enqueueImageAssetWrite(ImageObject* imageObject, const QImage& sourceImage);

    /**
     * @brief Wait for all pending image writes and publish their results.
     *
     * Called at every page/tile/bundle persistence and cleanup boundary.
     */
    bool flushPendingImageWrites();
    
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
    
    // =========================================================================
    // OCR Sidecar File I/O (Phase 1A)
    // =========================================================================
    
    /**
     * @brief Save OCR data for a page to its .ocr.json sidecar file.
     * @param uuid Page UUID.
     * @param page Page with OCR data to save.
     * @return True if saved (or cleaned up empty file).
     */
    bool savePageOcr(const QString& uuid, const Page* page);
    
    /**
     * @brief Load OCR data for a page from its .ocr.json sidecar file.
     * @param page Page to populate with OCR data.
     * @param uuid Page UUID (used to construct the file path).
     * @return True if loaded, false if file doesn't exist or parse error.
     */
    bool loadPageOcr(Page* page, const QString& uuid) const;
    
    /**
     * @brief Save OCR data for an edgeless tile to its .ocr.json sidecar file.
     * @param coord Tile coordinate.
     * @return True if saved.
     */
    bool saveTileOcr(TileCoord coord);
    
    /**
     * @brief Load OCR data for an edgeless tile from its .ocr.json sidecar file.
     * @param tile Tile Page to populate.
     * @param coord Tile coordinate.
     * @return True if loaded.
     */
    bool loadTileOcr(Page* tile, TileCoord coord) const;
    
    /**
     * @brief Create OcrTextObjects on a page from its ocrTextBlocks.
     * Called after loadPageOcr/loadTileOcr to materialize the derived cache.
     * Objects are created with visible = false (Show Text defaults to OFF).
     */
    void materializeOcrTextObjects(Page* page) const;
    
    /**
     * @brief Load OCR text blocks from any .ocr.json file path (for disk-based search).
     * @param ocrJsonPath Full path to the .ocr.json file.
     * @return Parsed text blocks, or empty vector on error.
     */
    static QVector<OcrTextBlock> loadOcrBlocksFromFile(const QString& ocrJsonPath);

    void setOcrTextVisible(bool visible) { m_ocrTextVisible = visible; }
    bool ocrTextVisible() const { return m_ocrTextVisible; }
    void setOcrDarkMode(bool dark) { m_ocrDarkMode = dark; }
    bool ocrDarkMode() const { return m_ocrDarkMode; }
    void setOcrShowConfidence(bool show) { m_ocrShowConfidence = show; }
    bool ocrShowConfidence() const { return m_ocrShowConfidence; }

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
     * @param finalize When true, materialize non-primary imported PDF sources into
     *        bundled mini-PDFs (Plan B2) before writing the manifest, so the bundle
     *        becomes self-contained. Only used on document close and .snbx export;
     *        ordinary saves/autosaves pass false to avoid grafting churn.
     * @return True if saved successfully.
     */
    bool saveBundle(const QString& path, bool finalize = false);
    
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
    
    // ===== Edgeless Position History (Phase 4) =====
    
    /**
     * @brief Get the last viewport position for edgeless mode.
     * @return Document coordinates of the last viewport center.
     */
    QPointF edgelessLastPosition() const { return m_edgelessLastPosition; }
    
    /**
     * @brief Set the last viewport position for edgeless mode.
     * @param pos Document coordinates of the viewport center.
     */
    void setEdgelessLastPosition(const QPointF& pos) { m_edgelessLastPosition = pos; }
    
    /**
     * @brief Get the position history stack for edgeless mode.
     * @return Vector of document coordinates (most recent last).
     */
    const QVector<QPointF>& edgelessPositionHistory() const { return m_edgelessPositionHistory; }
    
    /**
     * @brief Set the position history stack for edgeless mode.
     * @param history Vector of document coordinates.
     */
    void setEdgelessPositionHistory(const QVector<QPointF>& history) { m_edgelessPositionHistory = history; }
    
    // =========================================================================
    // PDF Reference Management (Task 1.2.4)
    // =========================================================================
    
    /**
     * @brief Check if this document has a PDF reference (path set).
     * @return True if the primary source has a path, even if not currently loaded.
     */
    bool hasPdfReference() const { const PdfSource* s = primarySource(); return s && !s->path.isEmpty(); }

    /**
     * @brief Whether the document references any PDF source at all (primary or
     *        imported). True for import-only documents that have no primary PDF.
     * @return True if at least one PDF source is registered.
     */
    bool hasAnyPdfSource() const { return !m_pdfSources.empty(); }
    
    /**
     * @brief Check if the primary PDF is currently loaded and valid.
     * @return True if the primary provider is valid and loaded.
     */
    bool isPdfLoaded() const { const PdfProvider* p = primaryProvider(); return p && p->isValid(); }
    
    /**
     * @brief Check if the PDF file exists at the referenced path.
     * @return True if the file exists on disk.
     */
    bool pdfFileExists() const;
    
    /**
     * @brief Get the path to the referenced PDF file.
     * @return The PDF path, or empty string if no PDF is referenced.
     */
    QString pdfPath() const { const PdfSource* s = primarySource(); return s ? s->path : QString(); }
    
    /**
     * @brief Get the relative path to the PDF file.
     * @return Relative path (from document.json location), or empty if not set.
     * 
     * Phase SHARE: Used for portable .snbx packages. When importing, if the
     * absolute path fails, the relative path is tried. Relative paths are
     * calculated from the document.json file location.
     */
    QString pdfRelativePath() const { const PdfSource* s = primarySource(); return s ? s->relativePath : QString(); }
    
    /**
     * @brief Set the relative path to the primary PDF file.
     * @param path Relative path from document.json location.
     */
    void setPdfRelativePath(const QString& path) { if (PdfSource* s = primarySource()) s->relativePath = path; }
    
    /**
     * @brief Get the primary PDF provider for advanced operations.
     * @return Pointer to the provider, or nullptr if not loaded.
     * 
     * Use this for accessing text boxes, links, outline, etc. (primary source).
     */
    const PdfProvider* pdfProvider() const { return primaryProvider(); }
    
    /**
     * @brief Load a PDF file.
     * @param path Path to the PDF file.
     * @return True if loaded successfully.
     * 
     * If a PDF is already loaded, it will be unloaded first.
     * Sets the primary source path even if loading fails so it can be repaired later.
     */
    bool loadPdf(const QString& path);
    
    /**
     * @brief Unload the PDF and clear the reference.
     * 
     * Releases PDF resources but keeps the source metadata for later recovery.
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
    QString pdfHash() const { const PdfSource* s = primarySource(); return s ? s->hash : QString(); }
    
    /**
     * @brief Get the stored PDF file size.
     * @return File size in bytes, or 0 if not set.
     */
    qint64 pdfSize() const { const PdfSource* s = primarySource(); return s ? s->size : 0; }
    
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
     * @brief Render a PDF page from a specific source to an image.
     * @param sourceId Source id (empty = primary source).
     * @param pageIndex 0-based page index within that source.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered image, or null image if the source is not available.
     */
    QImage renderPdfPageToImage(const QString& sourceId, int pageIndex, qreal dpi = 96.0) const;
    
    /**
     * @brief Render a PDF page to a pixmap.
     * @param pageIndex 0-based page index.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered pixmap, or null pixmap if not available.
     */
    QPixmap renderPdfPageToPixmap(int pageIndex, qreal dpi = 96.0) const;

    /**
     * @brief Get bounding rectangles of raster images on a PDF page.
     * @param pageIndex 0-based page index.
     * @param dpi Resolution (rects are in pixel coords at this DPI).
     * @return List of image bounding rects, empty if no PDF or no images.
     */
    QVector<QRect> pdfImageRegions(int pageIndex, qreal dpi = 96.0) const;

    /**
     * @brief Get bounding rectangles of raster images on a PDF page (source-aware).
     * @param sourceId Source id (empty = primary source).
     */
    QVector<QRect> pdfImageRegions(const QString& sourceId, int pageIndex, qreal dpi = 96.0) const;

    /**
     * @brief Shrink every open PDF provider's internal resource cache to free memory.
     */
    void trimPdfStore() const;

    /**
     * @brief Get the number of pages in the primary PDF.
     * @return Page count, or 0 if no PDF is loaded.
     */
    int pdfPageCount() const;

    /**
     * @brief Get the number of pages in a specific source's PDF.
     * @param sourceId Source id (empty = primary source).
     */
    int pdfPageCount(const QString& sourceId) const;
    
    /**
     * @brief Get the size of a primary PDF page.
     * @param pageIndex 0-based page index.
     * @return Page size in PDF points (72 dpi), or invalid size if not available.
     */
    QSizeF pdfPageSize(int pageIndex) const;

    /**
     * @brief Get the size of a PDF page from a specific source.
     * @param sourceId Source id (empty = primary source).
     */
    QSizeF pdfPageSize(const QString& sourceId, int pageIndex) const;

    // =========================================================================
    // Multi-Source Registry (cross-document page transfer foundation)
    // =========================================================================

    /**
     * @brief All PDF sources (index 0 is the primary). Read-only view.
     */
    const std::vector<PdfSource>& pdfSources() const { return m_pdfSources; }

    /**
     * @brief Number of registered PDF sources.
     */
    int pdfSourceCount() const { return static_cast<int>(m_pdfSources.size()); }

    /**
     * @brief Probe every PDF source and return its current runtime health.
     *
     * Candidate selection is shared with providerForSource(): a matching external
     * absolute path, then a matching bundle-relative full PDF, then the bundled
     * mini-PDF fallback. Providers are opened while probing so corrupt files are
     * distinguished from merely missing files.
     */
    QVector<PdfSourceHealth> pdfSourceHealthSnapshot() const;

    /**
     * @brief Number of notebook pages backed by a source.
     * @param sourceId Empty for the primary source.
     */
    int notebookPageCountForSource(const QString& sourceId) const;

    /**
     * @brief Locate a moved source without changing its stored identity.
     *
     * When a hash/size is stored the candidate must match. Legacy hashless sources
     * establish their identity from the first successfully located file.
     */
    bool locateSource(const QString& sourceId, const QString& newPath);

    /**
     * @brief Clear cached runtime resolution so the next health query retries a source.
     * @param sourceId Empty for the primary source.
     */
    void retryPdfSource(const QString& sourceId) const;

    /**
     * @brief Look up a source by id. Empty id resolves to the primary.
     * @return Pointer into m_pdfSources, or nullptr if not found.
     */
    const PdfSource* pdfSourceById(const QString& sourceId) const;
    PdfSource* pdfSourceById(const QString& sourceId);

    /**
     * @brief Resolve the (lazily opened) provider for a source.
     * @param sourceId Source id (empty = primary source).
     * @return Provider pointer, or nullptr if the source file is unavailable.
     *
     * The primary provider is opened eagerly on load; non-primary providers are
     * opened on first request and cached. A missing file marks the source for relink.
     */
    PdfProvider* providerForSource(const QString& sourceId) const;

    /**
     * @brief Absolute path used to open a source (bundled file path when bundled).
     * @param sourceId Source id (empty = primary source).
     */
    QString pdfPathForSource(const QString& sourceId) const;

    /**
     * @brief Register a PDF source, deduping by identity (hash + size).
     * @return The id of the existing (deduped) or newly created source.
     *
     * If a source with a matching non-empty hash and size already exists, its id is
     * returned and no new source is added. Otherwise a new source with a fresh UUID
     * is appended.
     */
    QString registerSource(const QString& path, const QString& hash, qint64 size, bool bundled = false);

    /**
     * @brief Ids of sources not referenced by any page (candidates for cleanup).
     *
     * Used by later plans (page delete / save cleanup). Provided here so the
     * registry API is complete.
     */
    QStringList unreferencedSourceIds() const;

    /**
     * @brief Keep a source alive for this session while undo/redo snapshots refer to it.
     *
     * The retention is runtime-only. A later session, which has no matching undo
     * history, can prune the source normally.
     */
    void retainPdfSourceForUndo(const QString& sourceId);

    /**
     * @brief Drop PDF sources no page references anymore (Plan A2 / Q7.2).
     * @return Number of sources removed.
     *
     * Removes each source returned by unreferencedSourceIds() from the registry
     * and closes its cached provider. When the primary becomes unreferenced and
     * is dropped, the legacy top-level pdf_path/pdf_hash/pdf_size mirror clears
     * automatically on the next toJson() (it mirrors the surviving primary, or
     * writes an empty path when no source remains). Called during saveBundle().
     */
    int pruneUnreferencedSources();

    /**
     * @brief Translate a page's ORIGINAL PDF page number to the index its provider
     *        actually uses (Plan B2).
     * @param sourceId Source id (empty = primary).
     * @param originalPage 0-based page number in the ORIGINAL full PDF.
     * @return For a bundled source, the mini-PDF page index from pageMap (or -1 if
     *         the original page is not present in the mini-PDF). For a non-bundled
     *         source (including the always-external primary), originalPage unchanged.
     *
     * Bundled mini-PDFs contain only the referenced pages, remapped to a compact
     * index, while pages keep storing their original page number; every provider
     * access must route the index through this helper.
     */
    int resolveSourcePageIndex(const QString& sourceId, int originalPage) const;

    /**
     * @brief Whether any non-primary source has referenced pages not yet bundled.
     * @return True only when materialization is possible (MuPDF export available,
     *         bundle path set) and there is un-bundled imported PDF content.
     *
     * Used to gate the finalize-on-close / finalize-on-export hooks so ordinary
     * documents pay no cost.
     */
    bool needsMaterialization() const;

    /**
     * @brief Graft referenced pages of each non-primary source into a bundled
     *        mini-PDF inside this bundle, marking those sources bundled (Plan B2).
     * @param errorOut Optional: set to a message if a source failed to materialize.
     * @return Number of sources materialized/updated.
     *
     * Incremental: only pages missing from a source's pageMap are appended, and a
     * source with nothing new is skipped. The primary source is never materialized
     * (it stays external/full, Q12.4). Requires a non-empty bundle path; callers
     * invoke this from saveBundle(path, finalize=true).
     */
    int materializeSources(QString* errorOut = nullptr);
    
    /**
     * @brief Find the notebook page index for a given PDF page.
     * @param pdfPageIndex 0-based PDF page index.
     * @return Notebook page index (position in page order), or -1 if not found.
     * 
     * This is useful for PDF text search: the search engine operates in PDF page space,
     * but navigation requires notebook page indices. When pages are inserted between
     * PDF pages, the notebook page index differs from the PDF page index.
     * 
     * Example:
     * - PDF has pages 0, 1, 2, 3
     * - User inserts a blank page after page 1
     * - Notebook pages: [pdf 0], [pdf 1], [blank], [pdf 2], [pdf 3]
     * - notebookPageIndexForPdfPage(2) returns 3 (not 2)
     */
    int notebookPageIndexForPdfPage(int pdfPageIndex) const;
    
    /**
     * @brief Get the PDF page index for a given notebook page.
     * @param notebookPageIndex 0-based notebook page index (position in page order).
     * @return PDF page index, or -1 if the page is not a PDF page.
     * 
     * This is the reverse of notebookPageIndexForPdfPage(). Used for outline
     * highlighting: when the user scrolls to a notebook page, we need to find
     * which PDF page it corresponds to for highlighting the correct outline item.
     * 
     * Example:
     * - Notebook pages: [pdf 0], [pdf 1], [blank], [pdf 2], [pdf 3]
     * - pdfPageIndexForNotebookPage(3) returns 2
     * - pdfPageIndexForNotebookPage(2) returns -1 (blank page, not PDF)
     */
    int pdfPageIndexForNotebookPage(int notebookPageIndex) const;

    /**
     * @brief Resolve a notebook page to its PDF source id and PDF page number.
     * @param notebookPageIndex 0-based notebook page index.
     * @param outSourceId Set to the page's PDF source id (empty = primary source).
     * @param outPdfPage Set to the 0-based PDF page number within that source.
     * @return True if the page is PDF-backed, false otherwise.
     *
     * Unlike pdfPageIndexForNotebookPage(), this resolves against ANY source (not
     * just primary), reading the live Page fields so it stays correct for pages
     * imported at runtime. Used by multi-source export and search.
     */
    bool pdfBindingForNotebookPage(int notebookPageIndex, QString& outSourceId, int& outPdfPage) const;

    /**
     * @brief Find the notebook page displaying a given source's original PDF page.
     * @param sourceId PDF source id (empty = primary source).
     * @param originalPage 0-based ORIGINAL page number within that source's PDF.
     * @return Notebook page index, or -1 if no page in this document shows it.
     *
     * Source-aware reverse lookup used for internal PDF-link navigation. Matches by
     * (pdfSourceId, pdfPageNumber) on live pages, so it is correct regardless of page
     * order or when only a subset of a PDF's pages was imported.
     */
    int notebookPageIndexForSourcePage(const QString& sourceId, int originalPage) const;

    /**
     * @brief Convert a provider-document page index back to the source's original page.
     * @param sourceId PDF source id (empty = primary source).
     * @param providerPage Page index in the currently-open provider document.
     * @return Original PDF page number, or -1 if it cannot be resolved.
     *
     * Inverse of resolveSourcePageIndex(). For external/full-file or non-bundled
     * sources the provider index equals the original page. For a bundled mini-PDF
     * (original file absent) it reverse-maps through PdfSource::pageMap. Used to turn
     * a MuPDF link targetPage (provider space) into an original page number.
     */
    int originalPageForProviderIndex(const QString& sourceId, int providerPage) const;

    /**
     * @brief Eagerly open (and cache) providers for every registered PDF source.
     *
     * Call on the main thread before dispatching background search workers so that
     * worker threads only ever read the provider cache (providerForSource() lazily
     * mutates it, which is not safe to race from multiple threads).
     */
    void ensureAllPdfProvidersLoaded() const;

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

    /**
     * @brief Merged outline across all contributing PDF sources (OUT1).
     * @return One tree with each contributing source's outline as a subtree.
     *
     * For every source in sourceDisplayOrder() that exposes an outline, its
     * native outline is deep-copied with each entry's targetPage converted from
     * provider space to the source's ORIGINAL page (via originalPageForProviderIndex)
     * and tagged with sourceId. Empty subtrees (reaching no page present in this
     * document) are pruned; a source contributing nothing is dropped entirely.
     * When more than one source contributes, each is wrapped under a synthetic,
     * non-navigable root titled by sourceDisplayTitle(); with a single contributor
     * the root is suppressed so single-PDF documents look exactly as before.
     */
    QVector<PdfOutlineItem> aggregatedOutline() const;

    /**
     * @brief Ordered list of PDF source ids for display/accent assignment (OUT1).
     * @return Primary source id first (if a primary exists), then remaining
     *         source ids by first appearance in page order.
     *
     * This is the reusable "document map" seed: the outline panel maps a source's
     * slot to a gray shade and the future scroll bar (SB1) maps the same slot to
     * a color accent, so a source is recognizably the same slot in both.
     */
    QStringList sourceDisplayOrder() const;

    /**
     * @brief Palette slot (0-based index in sourceDisplayOrder()) for a source.
     * @param sourceId Source id (empty = primary).
     * @return Slot index, or -1 if the source is unknown.
     */
    int paletteSlotForSource(const QString& sourceId) const;

    /**
     * @brief Human-readable title for a PDF source (OUT1 outline root label).
     * @param sourceId Source id (empty = primary).
     * @return Embedded PDF title, else the file base name, else "Source N".
     */
    QString sourceDisplayTitle(const QString& sourceId) const;
    
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
        return static_cast<int>(m_pageOrder.size()); 
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
    
    /**
     * @brief Update a page's size and sync the layout metadata.
     * @param index 0-based page index.
     * @param size New page size.
     *
     * Sets both Page::size and the internal metadata used by pageSizeAt()
     * and the viewport layout engine.  Must be called instead of writing
     * to page->size directly whenever the metadata cache must stay in sync
     * (e.g. applying user-configured defaults to the first page).
     */
    void setPageSize(int index, const QSizeF& size);
    
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
     * @brief Restore a previously-removed page from a JSON snapshot (Plan A2).
     * @param index Notebook index at which to reinsert the page.
     * @param pageJson A Page::toJson() snapshot captured before removal.
     * @return True if the page was restored.
     *
     * Rebuilds the page via Page::fromJson, reinserts its UUID into page_order
     * at @p index, restores metadata and PDF-source mappings from the page's
     * own fields, cancels any pending on-disk deletion, loads its images, and
     * marks the document modified. Used to undo removePage().
     */
    bool restorePageFromSnapshot(int index, const QJsonObject& pageJson);

    /**
     * @brief Deep-copy pages from another open document into this one (Plan B).
     * @param srcDoc The source document to copy pages from (must differ from this).
     * @param srcPageUuids UUIDs of the source pages to copy, in the desired order.
     * @param destIndex Notebook index at which to insert the copied pages.
     * @return A PageImportResult describing the inserted pages, or an empty
     *         result (insertedPageJson empty) on failure.
     *
     * Each source page is serialized (Page::toJson), its referenced image assets
     * are copied into this document's assets store (content-hash deduped), and its
     * ids are regenerated (new page uuid, layer ids, object ids; stroke ids kept)
     * before insertion via restorePageFromSnapshot. PDF-backed pages copy their
     * (pdfSourceId, pdfPageNumber) verbatim (they render blank until Plan B-pdf
     * registers the source); LinkObject/markdown targets are copied as-is until
     * Plan B-links remaps them.
     */
    PageImportResult importPagesFrom(Document* srcDoc, const QStringList& srcPageUuids, int destIndex);
    
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
    struct PendingImageWrite {
        QString objectId;
        QString fullPath;
        QFuture<bool> future;
    };

    bool collectFinishedImageWrites(bool waitForAll);
    void confirmPersistedImageAssets();
    ImageObject* findLoadedImageObject(const QString& objectId) const;

    // ===== PDF Sources (multi-source model) =====
    /// Ordered list of PDF sources. Index 0 is the "primary" (born-from single PDF),
    /// mirrored to the legacy top-level pdf_path/pdf_hash/pdf_size keys on save.
    /// Empty when the document references no PDF.
    std::vector<PdfSource> m_pdfSources;
    /// Lazily-opened providers keyed by source id. The primary is opened eagerly on
    /// load; other sources open on first render. Mutable so providerForSource() (used
    /// from const render paths) can populate the cache.
    mutable std::map<QString, std::unique_ptr<PdfProvider>> m_pdfProviders;
    /// Runtime path and page-number space used by each cached provider.
    mutable std::map<QString, QString> m_pdfProviderPaths;
    mutable std::map<QString, qint64> m_pdfProviderPathSizes;
    mutable std::map<QString, qint64> m_pdfProviderPathModifiedTimes;
    mutable QSet<QString> m_pdfProvidersUsingBundled;
    mutable QSet<QString> m_pdfProvidersUsingRelative;
    mutable std::map<QString, PdfSourceHealthStatus> m_pdfSourceFailures;
    QSet<QString> m_undoRetainedPdfSourceIds;

    // ===== Private PDF source helpers =====
    /// The primary source (the document's own base PDF, flagged primary), or nullptr
    /// if the document has no primary PDF. NOTE: this is tracked by an explicit flag,
    /// NOT by list position - imported sources are non-primary even at index 0.
    PdfSource* primarySource() {
        for (PdfSource& s : m_pdfSources) if (s.primary) return &s;
        return nullptr;
    }
    const PdfSource* primarySource() const {
        for (const PdfSource& s : m_pdfSources) if (s.primary) return &s;
        return nullptr;
    }
    /// Ensure a primary source exists, creating one (flagged primary) if needed.
    PdfSource& ensurePrimarySource();
    struct PdfSourceOpenResult {
        std::unique_ptr<PdfProvider> provider;
        QString path;
        PdfSourceHealthStatus failureStatus = PdfSourceHealthStatus::Missing;
        bool bundled = false;
        bool relative = false;
    };
    PdfSourceOpenResult openBestPdfSourceCandidate(const PdfSource& source) const;
    void cachePdfProviderPath(const QString& registryId, const QString& path) const;
    bool cachedPdfProviderPathIsCurrent(const QString& registryId) const;
    void clearCachedPdfProvider(const QString& registryId) const;
    QString normalizedPdfSourceId(const PdfSource& source) const {
        return source.primary ? QString() : source.id;
    }
    /// The primary provider without lazy creation, or nullptr if not open.
    PdfProvider* primaryProvider() const {
        const PdfSource* s = primarySource();
        if (!s) return nullptr;
        auto it = m_pdfProviders.find(s->id);
        return it != m_pdfProviders.end() ? it->second.get() : nullptr;
    }

    // ===== Private page-import helpers (Plan B) =====
    /// Return a copy of @p pageJson with regenerated ids: a new page uuid, new
    /// layer ids, and new object ids. Stroke ids, image asset references, and link
    /// targets are left untouched. Records src->new mappings into @p pageMap /
    /// @p objMap for later link remapping (Plan B-links).
    QJsonObject regeneratePageIds(const QJsonObject& pageJson,
                                  QHash<QString, QString>& pageMap,
                                  QHash<QString, QString>& objMap) const;
    /// Copy every image asset referenced by @p pageJson from @p srcDoc's assets
    /// store into this document's assets store (skips assets already present;
    /// content-hash filenames make this inherently deduped).
    void copyImageAssets(Document* srcDoc, const QJsonObject& pageJson) const;
    /// Resolve @p srcDoc's source (@p originSourceId, empty = origin primary) into
    /// a source id in THIS document's registry: dedup by identity hash+size (path
    /// fallback when hash is empty), else register a new external source pointing
    /// at the origin PDF's absolute path. Returns the destination source id, or an
    /// empty string when the match is this document's primary (index 0) or the
    /// origin source cannot be resolved (Plan B-pdf).
    QString ensureImportedPdfSourceId(Document* srcDoc, const QString& originSourceId);
    /// For a PDF-backed @p pageJson, rewrite its pdfSourceId to reference a source
    /// in this document (via ensureImportedPdfSourceId) so the copied page renders
    /// in the destination. pdfPageNumber is left unchanged (Plan B-pdf).
    void remapImportedPdfSource(QJsonObject& pageJson, Document* srcDoc);
    /// Copy every markdown note referenced by a LinkObject markdown slot in
    /// @p pageJson from @p srcDoc's notes store into this document's notes store
    /// (skips notes already present; note ids are stable UUIDs so no repoint is
    /// needed) (Plan B-links).
    void copyMarkdownNotes(Document* srcDoc, const QJsonObject& pageJson) const;
    /// Remap LinkObject position-slot targets in @p pageJson: targets inside the
    /// copied set (present in @p pageUuidMap) are repointed to the new page uuid;
    /// out-of-set or edgeless targets are made inert (slot emptied) while keeping
    /// the LinkObject itself. url/markdown/empty slots are untouched (Plan B-links).
    /// A highlight region's sourceRange page uuid is remapped the same way, but
    /// an unresolvable one is marked stale rather than dropped: the region rects
    /// are the rendering truth and the highlight must keep displaying.
    void remapImportedLinkTargets(QJsonObject& pageJson,
                                  const QHash<QString, QString>& pageUuidMap) const;
    
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

    /// PDF source id for each PDF page whose source is NOT the primary.
    /// Key: page UUID, Value: source id. Absence = primary source (empty id).
    /// Parallel to m_pagePdfIndex.
    std::map<QString, QString> m_pagePdfSource;
    
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

    /// Bumped on every m_tiles insert/erase/clear (see tileLoadVersion()).
    /// Mutable because const paths (getTile) trigger lazy-load inserts.
    mutable quint64 m_tileLoadVersion = 0;
    
    // ===== Tile Persistence (Phase E5) =====
    QString m_bundlePath;                           ///< Path to .snb bundle directory
    mutable std::set<TileCoord> m_tileIndex;        ///< All tile coords that exist on disk (mutable for lazy-load failure cleanup)
    mutable std::set<TileCoord> m_dirtyTiles;       ///< Tiles modified since last save
    std::set<TileCoord> m_deletedTiles;             ///< Tiles to delete from disk on next save
    bool m_lazyLoadEnabled = false;                 ///< True after loading from bundle
    std::unique_ptr<QThreadPool> m_imageWritePool;  ///< Bounded pool isolated from PDF/render jobs
    QVector<PendingImageWrite> m_pendingImageWrites;

    bool m_ocrTextVisible = false;
    bool m_ocrDarkMode = false;
    bool m_ocrShowConfidence = false;

    /// Per-instance session id used to tag OCR requests so asynchronous
    /// results can be routed back to the exact Document that queued them.
    /// Generated on construction, never persisted.
    QString m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

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
    
    // ===== Edgeless Position History (Phase 4) =====
    /// Last viewport center position in document coordinates.
    /// Used to restore position when reopening the document.
    QPointF m_edgelessLastPosition{0.0, 0.0};
    
    /// Navigation history for "go back" functionality.
    /// Stores document coordinates of previous viewport positions.
    QVector<QPointF> m_edgelessPositionHistory;
    
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

    // ========================================================================
    // Link Outline Cache — internal state & peek helpers (Phase M.9)
    // ========================================================================

    /**
     * @brief Extract a single Page*'s LinkObjects into outline entries.
     *        Factored out of `enumerateLinkOutline`'s previous lambda so
     *        both the rebuild path and the refresh path can share it.
     * @param requireContent Skip annotations with nothing worth opening: no
     *        filled slot and no description the user actually wrote. The
     *        scroll-bar markers use this so a highlight nobody has given
     *        content to yet does not tick the bar. Auto-derived descriptions do
     *        not count, which is why `descriptionUserEdited` exists.
     */
    static QVector<LinkOutlineEntry>
    extractLinkOutlineFromPage(const Page* page,
                                int pageIdx,
                                int tileX,
                                int tileY,
                                bool edgeless,
                                bool requireMarkdown = true,
                                bool requireContent = false);

    /**
     * @brief Read a tile's JSON on disk and extract just the LinkObject
     *        summary (id/description/iconColor/position/slots).
     *
     * Does NOT construct a `Page`, load strokes, or touch OCR.  Returns
     * an empty vector on any I/O or parse failure (and does not mutate
     * `m_tileIndex`).  Safe to call on const paths.
     */
    QVector<LinkOutlineEntry>
    peekTileLinkOutlineFromDisk(TileCoord coord, bool requireMarkdown = true,
                                bool requireContent = false) const;

    /// Paged-mode counterpart of `peekTileLinkOutlineFromDisk`.
    QVector<LinkOutlineEntry>
    peekPageLinkOutlineFromDisk(int pageIndex, bool requireMarkdown = true,
                                bool requireContent = false) const;

    /**
     * @brief Shared JSON → outline-entry walker used by both peek helpers.
     *        Parses the \c objects array of a container JSON file, filters
     *        to LinkObjects with at least one markdown slot, and fills
     *        caller-supplied coordinate fields (pageIndex/tileX/tileY/
     *        tileOrigin) on each emitted entry.
     */
    static QVector<LinkOutlineEntry>
    extractLinkOutlineFromJsonObjects(const QJsonArray& objects,
                                       int  pageIndex,
                                       int  tileX,
                                       int  tileY,
                                       const QPointF& tileOrigin,
                                       bool requireMarkdown = true,
                                       bool requireContent = false);

    /// Cache contents, keyed by container.  Empty vectors are allowed
    /// and mean "container exists but has no markdown-backed links."
    /// `mutable` because enumeration is a const operation that may need
    /// to lazily populate the cache on first use.
    mutable std::map<int, QVector<LinkOutlineEntry>>       m_pageOutline;
    mutable std::map<TileCoord, QVector<LinkOutlineEntry>> m_tileOutline;
    mutable bool m_linkOutlineCacheReady = false;

    // ------------------------------------------------------------------
    // SB2 all-links marker cache (paged mode only).
    // Parallel to m_pageOutline but built with requireMarkdown=false, so it
    // holds EVERY LinkObject (not just markdown-backed ones). Shares the
    // same invalidation call sites (refreshLinkOutlineFor / dropLinkOutlineFor
    // / clearLinkOutlineCache); each keeps its own ready flag so either cache
    // can be populated independently on first use.
    // ------------------------------------------------------------------
    mutable std::map<int, QVector<LinkOutlineEntry>> m_pageMarkers;
    mutable bool m_markerCacheReady = false;

    /// Build (or rebuild) m_pageMarkers in one pass (paged mode only).
    void buildMarkerCache() const;
};
