#ifndef EXPANDABLETOOLBUTTON_H
#define EXPANDABLETOOLBUTTON_H

#include <QWidget>
#include "../ToolbarButtons.h"

class QHBoxLayout;

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

private:
    void updateContentVisibility();

    ToolButton* m_toolButton = nullptr;
    QWidget* m_contentWidget = nullptr;
    QHBoxLayout* m_mainLayout = nullptr;
    bool m_expanded = false;
    bool m_darkMode = false;

    static constexpr int BORDER_RADIUS = 6;
    static constexpr int CONTENT_SPACING = 2;
};

#endif // EXPANDABLETOOLBUTTON_H
