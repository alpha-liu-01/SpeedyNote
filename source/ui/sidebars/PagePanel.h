#ifndef PAGEPANEL_H
#define PAGEPANEL_H

#include <QWidget>
#include <QHash>
#include <QSet>
#include <QPixmap>

class PagePanelListView;
class QTimer;
class Document;
class PageThumbnailModel;
class PageThumbnailDelegate;

/**
 * @brief Main page panel widget displaying page thumbnails.
 * 
 * Provides a thumbnail view of all pages in a paged document, allowing
 * users to navigate by clicking and reorder pages via drag-and-drop.
 * 
 * Features:
 * - QListView with custom model and delegate
 * - Touch-friendly scrolling (QScroller)
 * - Auto-scroll to current page when not visible
 * - Debounced thumbnail invalidation (500ms)
 * - Drag-and-drop reorder support
 * - Width-responsive thumbnail sizing
 * - Per-tab scroll position state
 * 
 * Usage:
 * 1. MainWindow creates PagePanel in left sidebar
 * 2. Call setDocument() when document changes
 * 3. Connect pageClicked to navigation
 * 4. Connect viewport's currentPageChanged to onCurrentPageChanged()
 */
class PagePanel : public QWidget {
    Q_OBJECT

public:
    explicit PagePanel(QWidget* parent = nullptr);
    ~PagePanel() override;

    // =========================================================================
    // Document Binding
    // =========================================================================

    /**
     * @brief Set the document to display pages from.
     * @param doc Document pointer (not owned).
     */
    void setDocument(Document* doc);
    
    /**
     * @brief Get the current document.
     */
    Document* document() const { return m_document; }

    // =========================================================================
    // Current Page
    // =========================================================================

    /**
     * @brief Set the current page index (for highlighting).
     * @param index 0-based page index.
     */
    void setCurrentPageIndex(int index);
    
    /**
     * @brief Get the current page index.
     */
    int currentPageIndex() const { return m_currentPageIndex; }

    // =========================================================================
    // Scroll Position State (per-tab)
    // =========================================================================

    /**
     * @brief Get the current scroll position.
     * @return Vertical scroll bar value.
     */
    int scrollPosition() const;
    
    /**
     * @brief Set the scroll position.
     * @param pos Vertical scroll bar value.
     */
    void setScrollPosition(int pos);
    
    /**
     * @brief Save the scroll position for a tab.
     * @param tabIndex Tab index.
     */
    void saveTabState(int tabIndex);
    
    /**
     * @brief Restore the scroll position for a tab.
     * @param tabIndex Tab index.
     */
    void restoreTabState(int tabIndex);
    
    /**
     * @brief Clear saved state for a closed tab.
     * @param tabIndex Tab index.
     */
    void clearTabState(int tabIndex);

    // =========================================================================
    // Theme
    // =========================================================================

    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode.
     */
    void setDarkMode(bool dark);

    // =========================================================================
    // Thumbnail Access
    // =========================================================================

    /**
     * @brief Get the cached thumbnail for a page.
     * @param pageIndex 0-based page index.
     * @return Cached thumbnail, or null pixmap if not cached.
     */
    QPixmap thumbnailForPage(int pageIndex) const;

    // =========================================================================
    // Thumbnail Invalidation
    // =========================================================================

    /**
     * @brief Invalidate the thumbnail for a specific page.
     * @param pageIndex 0-based page index.
     */
    void invalidateThumbnail(int pageIndex);
    
    /**
     * @brief Invalidate all thumbnails.
     */
    void invalidateAllThumbnails();

signals:
    /**
     * @brief Emitted when a page thumbnail is clicked.
     * @param pageIndex The clicked page index (0-based).
     */
    void pageClicked(int pageIndex);
    
    /**
     * @brief Emitted when a page is dropped to a new position.
     * @param fromIndex Original page index.
     * @param toIndex Target page index.
     */
    void pageDropped(int fromIndex, int toIndex);

public slots:
    /**
     * @brief Handle current page change from viewport.
     * @param pageIndex The new current page index.
     */
    void onCurrentPageChanged(int pageIndex);
    
    /**
     * @brief Scroll the view to show the current page.
     */
    void scrollToCurrentPage();
    
    /**
     * @brief Handle page count change in document.
     */
    void onPageCountChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onItemClicked(const QModelIndex& index);
    void onModelPageDropped(int fromIndex, int toIndex);
    void performPendingInvalidation();
    void onDragRequested(const QModelIndex& index);

private:
    void setupUI();
    void setupConnections();
    void configureListView();
    void updateThumbnailWidth();
    void applyTheme();

    // Widgets
    PagePanelListView* m_listView = nullptr;
    PageThumbnailModel* m_model = nullptr;
    PageThumbnailDelegate* m_delegate = nullptr;

    // State
    Document* m_document = nullptr;
    int m_currentPageIndex = 0;
    bool m_darkMode = false;
    
    // Debounced invalidation
    QTimer* m_invalidationTimer = nullptr;
    QSet<int> m_pendingInvalidations;
    bool m_needsFullRefresh = false;
    
    // Per-tab scroll positions
    QHash<int, int> m_tabScrollPositions;
    
    // Constants
    static constexpr int MIN_THUMBNAIL_WIDTH = 100;
    static constexpr int THUMBNAIL_PADDING = 16;  // Padding on each side
    static constexpr int INVALIDATION_DELAY_MS = 500;
};

#endif // PAGEPANEL_H

