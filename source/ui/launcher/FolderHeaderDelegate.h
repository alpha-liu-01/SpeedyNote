#ifndef FOLDERHEADERDELEGATE_H
#define FOLDERHEADERDELEGATE_H

#include <QStyledItemDelegate>

/**
 * @brief Custom delegate for rendering folder headers in StarredView.
 * 
 * This delegate paints folder section headers with an expand/collapse chevron
 * and folder name. Used by StarredListView to render folder headers within
 * the virtualized Model/View architecture.
 * 
 * Visual appearance matches the original FolderHeader widget:
 * - Fixed height (44px)
 * - Chevron indicator (▶ collapsed, ▼ expanded)
 * - Bold folder name
 * - Bottom separator line
 * - Hover and pressed states
 * - Dark mode support
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class FolderHeaderDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit FolderHeaderDelegate(QObject* parent = nullptr);
    
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

public:
    // Data roles used by this delegate
    // These MUST match the roles defined in StarredModel
    enum DataRoles {
        FolderNameRole = Qt::UserRole + 250,  // QString: folder display name
        IsCollapsedRole,                       // bool: whether folder is collapsed
    };

private:
    /**
     * @brief Paint a folder header.
     */
    void paintFolderHeader(QPainter* painter, const QRect& rect,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) const;
    
    /**
     * @brief Get background color based on state and dark mode.
     */
    QColor backgroundColor(bool pressed, bool hovered) const;
    
    bool m_darkMode = false;
    
    // Layout constants (match original FolderHeader widget)
    static constexpr int HEADER_HEIGHT = 44;
    static constexpr int CHEVRON_X = 8;
    static constexpr int CHEVRON_WIDTH = 20;
    static constexpr int NAME_X = 32;
    static constexpr int NAME_MARGIN_RIGHT = 8;
};

#endif // FOLDERHEADERDELEGATE_H
