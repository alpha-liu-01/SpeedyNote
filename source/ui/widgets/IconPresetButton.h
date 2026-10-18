#ifndef ICONPRESETBUTTON_H
#define ICONPRESETBUTTON_H

#include <QIcon>
#include <QWidget>

/**
 * @brief A 24×24 icon button with a selected state, for subtoolbars.
 *
 * Visual language matches ThicknessPresetButton / ColorPresetButton: a round
 * background that blends into the subtoolbar, and an accent ring when selected.
 * That makes a row of these read as "pick one of these", which is what the
 * pen's presets already do - and what a cycling toggle cannot express, since a
 * toggle shows no list and no position within it.
 *
 * Icon swapping for dark mode follows the `_reversed` convention used by the
 * rest of the icon set (see setIconName()).
 */
class IconPresetButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon)

public:
    explicit IconPresetButton(QWidget* parent = nullptr);

    /**
     * @brief Set the icon directly (no dark-mode switching).
     */
    void setIcon(const QIcon& icon);
    QIcon icon() const { return m_icon; }

    /**
     * @brief Set the icon by base name, e.g. "eraser".
     *
     * Loads ":/resources/icons/<name>.png", or "<name>_reversed.png" in dark
     * mode. Call setDarkMode() first if the theme is already known.
     */
    void setIconName(const QString& baseName);

    /**
     * @brief Switch between the normal and _reversed icon variants.
     */
    void setDarkMode(bool darkMode);
    bool isDarkMode() const { return m_darkMode; }

    /**
     * @brief Show the accent ring that marks the active choice.
     */
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();

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
    void updateIcon();

    QIcon m_icon;
    QString m_iconBaseName;
    bool m_darkMode = false;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_hovered = false;

    static constexpr int BUTTON_SIZE = 24;
    static constexpr int ICON_SIZE = 16;
    static constexpr int BORDER_RADIUS = BUTTON_SIZE / 2;
    static constexpr int BORDER_WIDTH_SELECTED = 2;
};

#endif // ICONPRESETBUTTON_H
