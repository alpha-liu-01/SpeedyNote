#ifndef ACTIONBARBUTTON_H
#define ACTIONBARBUTTON_H

#include <QWidget>
#include <QIcon>

/**
 * @brief A round button widget for action bars.
 * 
 * Similar to SubToolbarToggle but simpler - a click button (not toggle)
 * with enabled/disabled state support.
 * 
 * Visual states:
 * - Normal: Neutral gray background
 * - Hovered: Slightly lighter
 * - Pressed: Darker
 * - Disabled: Grayed out, no hover effects
 * 
 * Size: 36×36 logical pixels, fully round
 * 
 * Supports dark/light mode icon switching via setDarkMode().
 */
class ActionBarButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled)

public:
    explicit ActionBarButton(QWidget* parent = nullptr);
    
    /**
     * @brief Set the button icon directly (no dark mode switching).
     * @param icon The icon to display.
     */
    void setIcon(const QIcon& icon);
    
    /**
     * @brief Get the button icon.
     */
    QIcon icon() const;
    
    /**
     * @brief Set the icon by base name (enables dark mode switching).
     * @param baseName The icon base name (e.g., "copy" loads copy.png or copy_reversed.png)
     */
    void setIconName(const QString& baseName);
    
    /**
     * @brief Set dark mode and update icon accordingly.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
    /**
     * @brief Check if the button is currently enabled.
     */
    bool isEnabled() const;
    
    /**
     * @brief Set the enabled state.
     * @param enabled True for enabled, false for disabled.
     */
    void setEnabled(bool enabled);
    
    /**
     * @brief Get the recommended size for this widget.
     */
    QSize sizeHint() const override;
    
    /**
     * @brief Get the minimum size for this widget.
     */
    QSize minimumSizeHint() const override;

signals:
    /**
     * @brief Emitted when the button is clicked (only when enabled).
     */
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /**
     * @brief Check if the application is in dark mode based on palette.
     */
    bool isDarkMode() const;
    
    /**
     * @brief Get the background color based on state.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Update the icon based on current dark mode setting.
     */
    void updateIcon();

    QIcon m_icon;
    QString m_iconBaseName;
    bool m_darkMode = false;
    bool m_enabled = true;
    bool m_pressed = false;
    bool m_hovered = false;
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int ICON_SIZE = 20;
};

#endif // ACTIONBARBUTTON_H

