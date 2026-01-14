#ifndef TIMELINEDELEGATE_H
#define TIMELINEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPixmap>
#include <QHash>

/**
 * @brief Custom delegate for rendering Timeline items in the Launcher.
 * 
 * Renders two types of items:
 * 
 * 1. **Section Headers** - Time period labels (Today, Yesterday, etc.)
 *    - Bold text with underline
 *    - Full width
 *    - Smaller height than cards
 * 
 * 2. **Notebook Cards** - Clickable notebook entries
 *    - Thumbnail on left (with letterbox/crop as needed)
 *    - Name, date, and type indicators on right
 *    - Star indicator if starred
 *    - Rounded corners with subtle shadow
 *    - Hover effect
 * 
 * Thumbnail display (per Q&A):
 * - Pages taller than card: top-align crop (keep top, crop bottom)
 * - Pages shorter than card: letterbox (show full, add bars)
 * - Standard aspect ratios: no cropping, no bars
 * 
 * Phase P.3.3: Part of the new Launcher implementation.
 */
class TimelineDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TimelineDelegate(QObject* parent = nullptr);
    
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

private:
    /**
     * @brief Paint a section header item.
     */
    void paintSectionHeader(QPainter* painter, const QRect& rect,
                           const QString& title) const;
    
    /**
     * @brief Paint a notebook card item.
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
     * @brief Get a type indicator string (PDF, Edgeless, Paged).
     */
    QString typeIndicatorText(bool isPdf, bool isEdgeless) const;
    
    /**
     * @brief Format a datetime for display.
     */
    QString formatDateTime(const QDateTime& dt) const;
    
    // Cached pixmaps for performance
    mutable QHash<QString, QPixmap> m_thumbnailCache;
    
    bool m_darkMode = false;
    
    // Layout constants
    static constexpr int CARD_HEIGHT = 80;
    static constexpr int HEADER_HEIGHT = 32;
    static constexpr int THUMBNAIL_WIDTH = 60;
    static constexpr int CARD_MARGIN = 8;
    static constexpr int CARD_PADDING = 12;
    static constexpr int CARD_CORNER_RADIUS = 8;
    static constexpr int THUMBNAIL_CORNER_RADIUS = 4;
};

#endif // TIMELINEDELEGATE_H

