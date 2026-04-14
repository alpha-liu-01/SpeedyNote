#ifndef EXPANDABLETOOLBUTTON_H
#define EXPANDABLETOOLBUTTON_H

#include <QWidget>
#include "../ToolbarButtons.h"

class QHBoxLayout;
class QTimer;

/**
 * @brief Composite toolbar widget that expands to reveal inline subtoolbar content.
 *
 * When collapsed (tool not selected): displays only the 36x36 tool icon.
 * When expanded (tool selected): displays the icon followed by a horizontal
 * strip of preset buttons (colors, thicknesses, toggles, etc.).
 *
 * The expanded state draws a unified background with shadow across the
 * icon and content area, styled per theme (white/black + shadow).
 */
class ExpandableToolButton : public QWidget {
    Q_OBJECT

public:
    explicit ExpandableToolButton(QWidget* parent = nullptr);

    /**
     * @brief Access the inner ToolButton for QButtonGroup integration.
     */
    ToolButton* toolButton() const { return m_toolButton; }

    /**
     * @brief Set the widget to display in the expandable content area.
     * Takes ownership. The widget's layout should be horizontal (QHBoxLayout).
     */
    void setContentWidget(QWidget* widget);

    /**
     * @brief Show or hide the content area.
     */
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

    /**
     * @brief Enable hover-to-expand mode.
     * When enabled, the content area is shown on mouse enter
     * and hidden after the mouse leaves (with a short delay).
     */
    void setHoverExpand(bool enabled);

    /**
     * @brief Forward themed icon to the inner ToolButton.
     */
    void setThemedIcon(const QString& baseName);

    /**
     * @brief Forward dark mode to the inner ToolButton.
     */
    void setDarkMode(bool darkMode);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private:
    void updateContentVisibility();

    ToolButton* m_toolButton = nullptr;
    QWidget* m_contentWidget = nullptr;
    QHBoxLayout* m_mainLayout = nullptr;
    QTimer* m_collapseTimer = nullptr;
    QTimer* m_expandTimer = nullptr;
    bool m_expanded = false;
    bool m_darkMode = false;
    bool m_hoverExpand = false;

    static constexpr int BORDER_RADIUS = 6;
    static constexpr int CONTENT_SPACING = 2;
    static constexpr int COLLAPSE_DELAY_MS = 350;
    static constexpr int EXPAND_DELAY_MS = 1000;
};

#endif // EXPANDABLETOOLBUTTON_H
