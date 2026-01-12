#ifndef PAGETHUMBNAILDELEGATE_H
#define PAGETHUMBNAILDELEGATE_H

#include <QStyledItemDelegate>
#include <QColor>

/**
 * @brief Custom delegate for rendering page thumbnails in QListView.
 * 
 * Renders each item as:
 * 1. Thumbnail image (or placeholder if loading)
 * 2. Border (thin neutral for normal, thick accent for current page)
 * 3. Page number below ("Page N")
 * 4. Slight corner rounding (4px radius)
 * 
 * Placeholder rendering:
 * - Page background color (or gray for PDF)
 * - Page number visible
 * - Optional loading indicator
 */
class PageThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit PageThumbnailDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
               
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    /**
     * @brief Set the thumbnail width.
     * @param width Thumbnail width in pixels.
     */
    void setThumbnailWidth(int width);
    
    /**
     * @brief Get the current thumbnail width.
     */
    int thumbnailWidth() const { return m_thumbnailWidth; }
    
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
     * @brief Set the default page aspect ratio.
     * @param ratio Height / Width ratio (default: US Letter ≈ 1.294)
     */
    void setPageAspectRatio(qreal ratio);
    
    /**
     * @brief Get the current page aspect ratio.
     */
    qreal pageAspectRatio() const { return m_pageAspectRatio; }

private:
    /**
     * @brief Draw the thumbnail placeholder when image is not available.
     */
    void drawPlaceholder(QPainter* painter, const QRect& thumbRect,
                         bool isPdfPage) const;
    
    /**
     * @brief Draw the border around the thumbnail.
     */
    void drawBorder(QPainter* painter, const QRect& thumbRect,
                    bool isCurrentPage, bool isSelected, bool isHovered) const;
    
    /**
     * @brief Get the accent color for current page border.
     */
    QColor accentColor() const;
    
    /**
     * @brief Get neutral border color.
     */
    QColor neutralBorderColor() const;
    
    /**
     * @brief Get placeholder background color.
     */
    QColor placeholderColor(bool isPdfPage) const;
    
    /**
     * @brief Get text color for page number.
     */
    QColor textColor() const;
    
    /**
     * @brief Get background color for hover/selection.
     */
    QColor backgroundColor(bool isSelected, bool isHovered) const;

    int m_thumbnailWidth = 150;
    bool m_darkMode = false;
    qreal m_pageAspectRatio = 1.294;  // Default: US Letter (1056/816)
    
    // Visual constants
    static constexpr int VERTICAL_PADDING = 8;
    static constexpr int HORIZONTAL_PADDING = 8;
    static constexpr int BORDER_RADIUS = 4;
    static constexpr int BORDER_WIDTH_NORMAL = 1;
    static constexpr int BORDER_WIDTH_CURRENT = 3;
    static constexpr int PAGE_NUMBER_HEIGHT = 24;
    static constexpr int ITEM_SPACING = 8;
};

#endif // PAGETHUMBNAILDELEGATE_H

