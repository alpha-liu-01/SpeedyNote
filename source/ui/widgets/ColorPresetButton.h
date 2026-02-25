#ifndef COLORPRESETBUTTON_H
#define COLORPRESETBUTTON_H

#include <QWidget>
#include <QColor>

/**
 * @brief A round button displaying a filled color circle for preset selection.
 * 
 * Click behavior:
 * - Click unselected button → Select this preset (emits clicked())
 * - Click selected button → Open color editor (emits editRequested())
 * 
 * Visual states:
 * - Unselected: Color-filled circle with thin neutral border
 * - Selected: Color-filled circle with white border (dark mode) or black border (light mode)
 * - Pressed: Darken/lighten effect
 * 
 * Size: 36×36 logical pixels, fully round (18px border radius)
 */
class ColorPresetButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)

public:
    explicit ColorPresetButton(QWidget* parent = nullptr);
    
    /**
     * @brief Get the current color of this preset.
     */
    QColor color() const;
    
    /**
     * @brief Set the color of this preset.
     * @param color The new color.
     */
    void setColor(const QColor& color);
    
    /**
     * @brief Check if this button is currently selected.
     */
    bool isSelected() const;
    
    /**
     * @brief Set the selected state of this button.
     * @param selected True to select, false to deselect.
     */
    void setSelected(bool selected);
    
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
     * @brief Emitted when the button is clicked (on release).
     */
    void clicked();
    
    /**
     * @brief Emitted when the color is changed.
     * @param color The new color.
     */
    void colorChanged(QColor color);
    
    /**
     * @brief Emitted when the selected state changes.
     * @param selected The new selected state.
     */
    void selectedChanged(bool selected);
    
    /**
     * @brief Emitted when a selected button is clicked (request to open color editor).
     */
    void editRequested();

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
     * @brief Get the border color based on selection state and theme.
     */
    QColor borderColor() const;
    
    /**
     * @brief Get the fill color adjusted for pressed state.
     */
    QColor adjustedFillColor() const;

    QColor m_color = Qt::black;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_hovered = false;
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int BORDER_WIDTH_NORMAL = 2;
    static constexpr int BORDER_WIDTH_SELECTED = 3;
};

#endif // COLORPRESETBUTTON_H

