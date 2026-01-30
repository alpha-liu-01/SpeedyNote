#ifndef NOTEBOOKCARDDELEGATE_H
#define NOTEBOOKCARDDELEGATE_H

#include <QStyledItemDelegate>
#include <QPixmap>
#include <QHash>

struct NotebookInfo;

/**
 * @brief Custom delegate for rendering notebook cards in grid layouts.
 * 
 * This delegate paints notebook cards for StarredView and SearchView,
 * replacing the widget-based NotebookCard with a virtualized approach.
 * Only visible items are rendered, providing significant performance
 * improvements for large collections (100+ folders, 500+ notebooks).
 * 
 * Visual appearance matches the original NotebookCard widget:
 * - Fixed size card with rounded corners
 * - Thumbnail with C+D hybrid display (top-crop for tall, letterbox for short)
 * - Name label (elided if too long)
 * - Type indicator (PDF/Edgeless/Paged)
 * - Star indicator (if starred)
 * - Hover and selected states
 * - Shadow in light mode
 * - Dark mode support
 * 
 * This delegate is shared between StarredView and SearchView.
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class NotebookCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit NotebookCardDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
               
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    
    /**
     * @brief Set dark mode for theming.
     * @param dark True for dark theme colors.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Check if dark mode is enabled.
     */
    bool isDarkMode() const { return m_darkMode; }

public slots:
    /**
     * @brief Invalidate cached thumbnail for a notebook.
     * @param bundlePath The notebook whose thumbnail was updated.
     * 
     * Called when NotebookLibrary::thumbnailUpdated is emitted
     * to ensure the delegate reloads the updated thumbnail.
     */
    void invalidateThumbnail(const QString& bundlePath);
    
    /**
     * @brief Clear the entire thumbnail cache.
     * 
     * Useful when the view becomes visible again after being hidden,
     * to ensure fresh thumbnails are loaded.
     */
    void clearThumbnailCache();

public:
    // Data roles used by this delegate
    // These should match the roles defined in StarredModel and SearchModel
    enum DataRoles {
        NotebookInfoRole = Qt::UserRole + 100,  // QVariant containing NotebookInfo
        BundlePathRole,                          // QString: path to notebook bundle
        DisplayNameRole,                         // QString: notebook display name
        ThumbnailPathRole,                       // QString: path to thumbnail file
        IsStarredRole,                           // bool: whether notebook is starred
        IsPdfBasedRole,                          // bool: whether notebook is PDF-based
        IsEdgelessRole,                          // bool: whether notebook is edgeless
    };

private:
    /**
     * @brief Paint a notebook card.
     */
    void paintNotebookCard(QPainter* painter, const QRect& rect,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) const;
    
    /**
     * @brief Draw thumbnail with proper cropping/letterboxing.
     */
    void drawThumbnail(QPainter* painter, const QRect& rect,
                       const QString& thumbnailPath) const;
    
    /**
     * @brief Get type indicator text (PDF, Edgeless, Paged).
     */
    QString typeIndicatorText(bool isPdf, bool isEdgeless) const;
    
    /**
     * @brief Get type indicator color based on type and dark mode.
     */
    QColor typeIndicatorColor(bool isPdf, bool isEdgeless) const;
    
    /**
     * @brief Get background color based on state and dark mode.
     */
    QColor backgroundColor(bool selected, bool hovered) const;
    
    // Cached pixmaps for performance
    mutable QHash<QString, QPixmap> m_thumbnailCache;
    
    bool m_darkMode = false;
    
    // Card dimensions (match original NotebookCard widget)
    static constexpr int CARD_WIDTH = 120;
    static constexpr int CARD_HEIGHT = 160;
    static constexpr int THUMBNAIL_HEIGHT = 100;
    static constexpr int PADDING = 8;
    static constexpr int CORNER_RADIUS = 12;
    static constexpr int THUMBNAIL_CORNER_RADIUS = 8;
};

#endif // NOTEBOOKCARDDELEGATE_H
