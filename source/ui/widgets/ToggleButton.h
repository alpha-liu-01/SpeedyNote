#ifndef SUBTOOLBARTOGGLE_H
#define SUBTOOLBARTOGGLE_H

#include <QWidget>
#include <QIcon>

/**
 * @brief A simple on/off toggle button with icon for subtoolbars.
 * 
 * Note: Named SubToolbarToggle to avoid conflict with ToggleButton in ToolbarButtons.h
 * 
 * Visual states:
 * - Unchecked: Icon with neutral background
 * - Checked: Icon with accent/highlighted background
 * - Pressed: Darken/lighten effect
 * 
 * Size: 36×36 logical pixels, fully round (18px border radius)
 * 
 * Supports dark/light mode icon switching via setDarkMode().
 */
class SubToolbarToggle : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon)

public:
    explicit SubToolbarToggle(QWidget* parent = nullptr);
    
    /**
     * @brief Check if the button is currently checked/on.
     */
    bool isChecked() const;
    
    /**
     * @brief Set the checked state.
     * @param checked True for on, false for off.
     */
    void setChecked(bool checked);
    
    /**
     * @brief Get the button icon.
     */
    QIcon icon() const;
    
    /**
     * @brief Set the button icon directly (no dark mode switching).
     * @param icon The icon to display.
     */
    void setIcon(const QIcon& icon);
    
    /**
     * @brief Set the icon by base name (enables dark mode switching).
     * @param baseName The icon base name (e.g., "marker" loads marker.png or marker_reversed.png)
     */
    void setIconName(const QString& baseName);
    
    /**
     * @brief Set dark mode and update icon accordingly.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
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
     * @brief Emitted when the checked state changes.
     * @param checked The new checked state.
     */
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private:
    /**
     * @brief Check if the application is in dark mode based on palette.
     */
    bool isDarkMode() const;
    
    /**
     * @brief Get the background color based on checked state.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Update the icon based on current dark mode setting.
     */
    void updateIcon();

    bool m_checked = false;
    bool m_pressed = false;
    bool m_hovered = false;
    bool m_darkMode = false;
    QIcon m_icon;
    QString m_iconBaseName;
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int ICON_SIZE = 20;
};

#endif // SUBTOOLBARTOGGLE_H

