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
 * - Star indicator (top-right, if starred)
 * - 3-dot menu button (bottom-right) for single-item actions
 * - Hover and selected states
 * - Shadow in light mode
 * - Dark mode support
 * 
 * This delegate is shared between StarredView and SearchView.
 * 
 * The 3-dot menu button area can be queried via menuButtonRect() to allow
 * list views to detect clicks on it and show a context menu.
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
    
    /**
     * @brief Get the rectangle for the 3-dot menu button within a card.
     * @param cardRect The bounding rectangle of the card.
     * @return The rectangle where the menu button is painted.
     * 
     * List views should use this to detect clicks on the menu button area
     * and show the single-item context menu instead of entering select mode.
     */
    static QRect menuButtonRect(const QRect& cardRect);

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
    // These should match the roles defined in StarredModel, SearchModel, and TimelineModel
    enum DataRoles {
        // Notebook data roles (Qt::UserRole + 100 range)
        NotebookInfoRole = Qt::UserRole + 100,  // QVariant containing NotebookInfo
        BundlePathRole,                          // QString: path to notebook bundle
        DisplayNameRole,                         // QString: notebook display name
        ThumbnailPathRole,                       // QString: path to thumbnail file
        IsStarredRole,                           // bool: whether notebook is starred
        IsPdfBasedRole,                          // bool: whether notebook is PDF-based
        IsEdgelessRole,                          // bool: whether notebook is edgeless
        LastModifiedRole,                        // QDateTime: last modification time
        
        // Batch select mode roles (Qt::UserRole + 200 range)
        IsInSelectModeRole = Qt::UserRole + 200, // bool: whether view is in select mode
        IsSelectedInBatchRole,                    // bool: whether this item is selected in batch
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
     * @brief Draw the 3-dot menu button.
     * Only drawn when NOT in select mode.
     */
    void drawMenuButton(QPainter* painter, const QRect& buttonRect, bool hovered) const;
    
    /**
     * @brief Draw selection indicator (checkmark overlay) in top-left.
     * @param painter The painter.
     * @param cardRect The card bounding rectangle.
     * @param isSelected Whether this item is selected in batch mode.
     * 
     * Draws a circular checkmark indicator in the top-left corner.
     * Shows empty circle when not selected, filled circle with checkmark when selected.
     */
    void drawSelectionIndicator(QPainter* painter, const QRect& cardRect, bool isSelected) const;
    
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
    
    /**
     * @brief Format date/time for display on card.
     * @param dateTime The date/time to format.
     * @return Formatted string like "Today 2:30 PM" or "Jan 15, 2:30 PM".
     */
    QString formatDateTime(const QDateTime& dateTime) const;
    
    // Cached pixmaps for performance
    mutable QHash<QString, QPixmap> m_thumbnailCache;
    
    bool m_darkMode = false;
    
    // Card dimensions (wider to fit date/time, taller for extra line)
    static constexpr int CARD_WIDTH = 140;
    static constexpr int CARD_HEIGHT = 180;
    static constexpr int THUMBNAIL_HEIGHT = 100;
    static constexpr int PADDING = 8;
    static constexpr int CORNER_RADIUS = 12;
    static constexpr int THUMBNAIL_CORNER_RADIUS = 8;
    
    // Menu button dimensions
    static constexpr int MENU_BUTTON_SIZE = 24;
    static constexpr int MENU_BUTTON_MARGIN = 4;
    
    // Selection indicator dimensions (for batch select mode)
    static constexpr int SELECTION_INDICATOR_SIZE = 22;
    static constexpr int SELECTION_INDICATOR_MARGIN = 6;
};

#endif // NOTEBOOKCARDDELEGATE_H
