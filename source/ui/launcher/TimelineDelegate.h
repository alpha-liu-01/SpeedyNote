#ifndef TIMELINEDELEGATE_H
#define TIMELINEDELEGATE_H

#include <QStyledItemDelegate>

/**
 * @brief Custom delegate for rendering Timeline section headers.
 * 
 * This delegate only handles section headers (Today, Yesterday, etc.)
 * with bold text and an underline. Notebook cards are rendered by
 * NotebookCardDelegate via the CompositeTimelineDelegate.
 * 
 * Section headers:
 * - Bold text with underline
 * - Full width (spans entire viewport in IconMode)
 * - Smaller height than cards
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
    
    bool m_darkMode = false;
    
    // Layout constants
    static constexpr int HEADER_HEIGHT = 32;
    static constexpr int HEADER_PADDING = 8;
};

#endif // TIMELINEDELEGATE_H
