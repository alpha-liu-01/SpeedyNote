#ifndef PAGETHUMBNAILMODEL_H
#define PAGETHUMBNAILMODEL_H

#include <QAbstractListModel>
#include <QPixmap>
#include <QHash>
#include <QSet>

class Document;
class ThumbnailRenderer;

/**
 * @brief QAbstractListModel providing page data for QListView.
 * 
 * This model provides:
 * - Page index, thumbnail pixmap, current/PDF/draggable state
 * - Drag-and-drop support via MIME data (internal move only)
 * - Lazy thumbnail generation with cache
 * - Cache invalidation on content change
 * 
 * The model connects to a Document and reflects its page structure.
 * Thumbnails are generated on-demand and cached in memory.
 */
class PageThumbnailModel : public QAbstractListModel {
    Q_OBJECT

public:
    /**
     * @brief Custom roles for page data.
     */
    enum Roles {
        PageIndexRole = Qt::UserRole + 1,   ///< Page index (0-based)
        ThumbnailRole,                       ///< QPixmap thumbnail
        IsCurrentPageRole,                   ///< bool: is this the current page?
        IsPdfPageRole,                       ///< bool: is this a PDF background page?
        CanDragRole,                         ///< bool: can this page be dragged?
        PageAspectRatioRole                  ///< qreal: page height/width ratio
    };

    explicit PageThumbnailModel(QObject* parent = nullptr);
    ~PageThumbnailModel() override;

    // ===== QAbstractListModel Interface =====
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // ===== Drag-and-Drop Support =====
    
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;
    
    // ===== Document Binding =====
    
    /**
     * @brief Set the document to display pages from.
     * @param doc Document pointer (not owned).
     */
    void setDocument(Document* doc);
    
    /**
     * @brief Get the current document.
     */
    Document* document() const { return m_document; }
    
    /**
     * @brief Set the current page index (for highlighting).
     * @param index 0-based page index.
     */
    void setCurrentPageIndex(int index);
    
    /**
     * @brief Get the current page index.
     */
    int currentPageIndex() const { return m_currentPageIndex; }
    
    // ===== Thumbnail Management =====
    
    /**
     * @brief Set the thumbnail width for rendering.
     * @param width Thumbnail width in pixels.
     */
    void setThumbnailWidth(int width);
    
    /**
     * @brief Get the current thumbnail width.
     */
    int thumbnailWidth() const { return m_thumbnailWidth; }
    
    /**
     * @brief Get the cached thumbnail for a page.
     * @param pageIndex 0-based page index.
     * @return Cached thumbnail, or null pixmap if not cached.
     */
    QPixmap thumbnailForPage(int pageIndex) const;
    
    /**
     * @brief Invalidate the thumbnail cache for a specific page.
     * @param pageIndex 0-based page index.
     */
    void invalidateThumbnail(int pageIndex);
    
    /**
     * @brief Invalidate all thumbnail caches.
     */
    void invalidateAllThumbnails();
    
    /**
     * @brief Set the device pixel ratio for high DPI rendering.
     * @param dpr Device pixel ratio (e.g., 2.0 for Retina displays).
     */
    void setDevicePixelRatio(qreal dpr);
    
    /**
     * @brief Request thumbnail rendering for visible pages.
     * 
     * Call this when the visible range changes (e.g., on scroll).
     * Only requests thumbnails that aren't already cached or pending.
     * 
     * @param firstVisible First visible row index.
     * @param lastVisible Last visible row index.
     */
    void requestVisibleThumbnails(int firstVisible, int lastVisible);

signals:
    /**
     * @brief Emitted when a page was dropped to a new position.
     * @param fromIndex Original page index.
     * @param toIndex Target page index.
     */
    void pageDropped(int fromIndex, int toIndex);
    
    /**
     * @brief Emitted when a thumbnail has been rendered and is ready.
     * @param pageIndex Page index that was rendered.
     */
    void thumbnailReady(int pageIndex);

public slots:
    /**
     * @brief Handle document page count changes.
     * 
     * Call this when pages are added/removed from the document.
     */
    void onPageCountChanged();
    
    /**
     * @brief Handle page content changes.
     * @param pageIndex Page index that changed.
     * 
     * Invalidates the thumbnail for that page.
     */
    void onPageContentChanged(int pageIndex);

private slots:
    /**
     * @brief Handle thumbnail ready from async renderer.
     * @param pageIndex Page index that was rendered.
     * @param thumbnail The rendered thumbnail pixmap.
     */
    void onThumbnailRendered(int pageIndex, QPixmap thumbnail);

private:
    /**
     * @brief Check if a page is a PDF background page.
     * @param pageIndex 0-based page index.
     * @return True if the page has a PDF background.
     */
    bool isPdfPage(int pageIndex) const;
    
    /**
     * @brief Check if a page can be dragged (reordered).
     * @param pageIndex 0-based page index.
     * @return True if the page can be dragged.
     * 
     * PDF background pages cannot be dragged in documents with PDF references.
     */
    bool canDragPage(int pageIndex) const;

    /**
     * @brief Request a thumbnail for a single page.
     * @param pageIndex Page index to request.
     */
    void requestThumbnail(int pageIndex) const;

    // Document reference (not owned)
    Document* m_document = nullptr;
    
    // Current page for highlighting
    int m_currentPageIndex = 0;
    
    // Thumbnail cache with LRU eviction
    mutable QHash<int, QPixmap> m_thumbnailCache;
    mutable QList<int> m_cacheAccessOrder;  // LRU: front = oldest, back = newest
    mutable QSet<int> m_pendingThumbnails;
    
    void touchCache(int pageIndex) const;   // Mark page as recently used
    void evictOldestIfNeeded() const;       // Evict LRU entries if over limit
    
    // Thumbnail settings
    int m_thumbnailWidth = 150;
    qreal m_devicePixelRatio = 1.0;
    
    // Async thumbnail renderer (owned)
    ThumbnailRenderer* m_renderer = nullptr;
    
    // Cache size limit (~20MB at 2x DPI, ~5MB at 1x DPI)
    static constexpr int MAX_CACHED_THUMBNAILS = 50;
    
    // MIME type for drag-and-drop
    static constexpr const char* MIME_TYPE = "application/x-speedynote-page-index";
};

#endif // PAGETHUMBNAILMODEL_H

