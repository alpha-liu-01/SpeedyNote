#ifndef LAYERPANELPILLBUTTON_H
#define LAYERPANELPILLBUTTON_H

#include <QWidget>

/**
 * @brief A pill-shaped button widget for the LayerPanel.
 * 
 * Similar to ActionBarButton but pill-shaped (96×36) with text instead of icon.
 * 
 * Visual states:
 * - Normal: Neutral gray background
 * - Hovered: Slightly lighter
 * - Pressed: Darker
 * - Disabled: Grayed out, no hover effects
 * 
 * Size: 96×36 logical pixels, pill-shaped (rounded ends)
 * 
 * Supports dark/light mode theming.
 * 
 * Part of Phase L.3 - LayerPanel Touch-Friendly Restyle
 */
class LayerPanelPillButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled)

public:
    explicit LayerPanelPillButton(const QString& text, QWidget* parent = nullptr);
    
    /**
     * @brief Set the button text.
     * @param text The text to display.
     */
    void setText(const QString& text);
    
    /**
     * @brief Get the button text.
     */
    QString text() const;
    
    /**
     * @brief Set dark mode for theming.
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
     * @brief Get the text color based on state.
     */
    QColor textColor() const;

    QString m_text;
    bool m_darkMode = false;
    bool m_enabled = true;
    bool m_pressed = false;
    bool m_hovered = false;
    
    static constexpr int BUTTON_WIDTH = 96;
    static constexpr int BUTTON_HEIGHT = 36;
    static constexpr int CORNER_RADIUS = 18;  // Half of height for pill shape
};

#endif // LAYERPANELPILLBUTTON_H
