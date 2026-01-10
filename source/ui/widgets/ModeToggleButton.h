#ifndef MODETOGGLEBUTTON_H
#define MODETOGGLEBUTTON_H

#include <QWidget>
#include <QIcon>
#include <QString>

/**
 * @brief A two-state toggle button that shows different icons based on current mode.
 * 
 * Click toggles between mode 0 and mode 1.
 * 
 * Usage examples:
 * - Insert mode: Image (0) ↔ Link (1)
 * - Action mode: Select (0) ↔ Create (1)
 * 
 * Size: 36×36 logical pixels, fully round (18px border radius)
 * 
 * Supports dark/light mode icon switching via setDarkMode().
 */
class ModeToggleButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int currentMode READ currentMode WRITE setCurrentMode NOTIFY modeChanged)

public:
    explicit ModeToggleButton(QWidget* parent = nullptr);
    
    /**
     * @brief Set the icons for both modes directly (no dark mode switching).
     * @param mode0Icon Icon displayed when currentMode is 0.
     * @param mode1Icon Icon displayed when currentMode is 1.
     */
    void setModeIcons(const QIcon& mode0Icon, const QIcon& mode1Icon);
    
    /**
     * @brief Set the icons for both modes by base name (enables dark mode switching).
     * @param mode0BaseName Base name for mode 0 icon (e.g., "background" loads background.png or background_reversed.png)
     * @param mode1BaseName Base name for mode 1 icon.
     */
    void setModeIconNames(const QString& mode0BaseName, const QString& mode1BaseName);
    
    /**
     * @brief Set dark mode and update icons accordingly.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
    /**
     * @brief Set the tooltips for both modes.
     * @param mode0Tip Tooltip displayed when currentMode is 0.
     * @param mode1Tip Tooltip displayed when currentMode is 1.
     */
    void setModeToolTips(const QString& mode0Tip, const QString& mode1Tip);
    
    /**
     * @brief Get the current mode.
     * @return 0 or 1
     */
    int currentMode() const;
    
    /**
     * @brief Set the current mode.
     * @param mode The mode (0 or 1). Values outside this range are clamped.
     */
    void setCurrentMode(int mode);
    
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
     * @brief Emitted when the mode changes.
     * @param mode The new mode (0 or 1).
     */
    void modeChanged(int mode);

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
     * @brief Get the background color.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Update the tooltip based on current mode.
     */
    void updateToolTip();
    
    /**
     * @brief Update icons based on current dark mode setting.
     */
    void updateIcons();

    int m_currentMode = 0;
    bool m_pressed = false;
    bool m_hovered = false;
    bool m_darkMode = false;
    QIcon m_icons[2];
    QString m_iconBaseNames[2];
    QString m_toolTips[2];
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int ICON_SIZE = 20;
};

#endif // MODETOGGLEBUTTON_H

