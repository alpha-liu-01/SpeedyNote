#ifndef THICKNESSPRESETBUTTON_H
#define THICKNESSPRESETBUTTON_H

#include <QWidget>
#include <QColor>
#include <QDialog>

class QSlider;
class QDoubleSpinBox;

/**
 * @brief Modal dialog for editing thickness value.
 * 
 * Contains a horizontal slider and connected spinbox.
 */
class ThicknessEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit ThicknessEditDialog(qreal currentThickness, 
                                  qreal minThickness = 0.5,
                                  qreal maxThickness = 50.0,
                                  QWidget* parent = nullptr);
    
    /**
     * @brief Get the selected thickness value.
     */
    qreal thickness() const;

protected:
    void done(int result) override;  // Android keyboard fix (BUG-A001)

private slots:
    void onSliderChanged(int value);
    void onSpinBoxChanged(double value);

private:
    QSlider* m_slider = nullptr;
    QDoubleSpinBox* m_spinBox = nullptr;
    qreal m_minThickness;
    qreal m_maxThickness;
};

/**
 * @brief A round button displaying a diagonal line preview for thickness preset selection.
 * 
 * Click behavior:
 * - Click unselected button → Select this preset (emits clicked())
 * - Click selected button → Open thickness editor (emits editRequested())
 * 
 * Visual states:
 * - Unselected: Diagonal line with thin neutral border
 * - Selected: Diagonal line with white border (dark mode) or black border (light mode)
 * - Pressed: Darken/lighten effect
 * 
 * Size: 36×36 logical pixels, fully round (18px border radius)
 */
class ThicknessPresetButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor)

public:
    explicit ThicknessPresetButton(QWidget* parent = nullptr);
    
    /**
     * @brief Get the current thickness value.
     */
    qreal thickness() const;
    
    /**
     * @brief Set the thickness value.
     * @param thickness The new thickness in points.
     */
    void setThickness(qreal thickness);
    
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
     * @brief Get the color used for the line preview.
     */
    QColor lineColor() const;
    
    /**
     * @brief Set the color used for the line preview.
     * @param color The line color.
     */
    void setLineColor(const QColor& color);
    
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
     * @brief Emitted when the thickness is changed.
     * @param thickness The new thickness.
     */
    void thicknessChanged(qreal thickness);
    
    /**
     * @brief Emitted when the selected state changes.
     * @param selected The new selected state.
     */
    void selectedChanged(bool selected);
    
    /**
     * @brief Emitted when a selected button is clicked (request to open editor).
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
     * @brief Get the line color adjusted for pressed/hover state.
     */
    QColor adjustedLineColor() const;
    
    /**
     * @brief Calculate the visual line width for display (scaled to fit button).
     */
    qreal displayLineWidth() const;

    qreal m_thickness = 2.0;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_hovered = false;
    QColor m_lineColor = Qt::black;
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int BORDER_WIDTH_NORMAL = 2;
    static constexpr int BORDER_WIDTH_SELECTED = 3;
    static constexpr qreal MIN_DISPLAY_WIDTH = 1.0;
    static constexpr qreal MAX_DISPLAY_WIDTH = 12.0;
};

#endif // THICKNESSPRESETBUTTON_H

