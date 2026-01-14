#ifndef LAUNCHERNAVBUTTON_H
#define LAUNCHERNAVBUTTON_H

#include <QWidget>
#include <QIcon>
#include <QString>
#include <QColor>

/**
 * @brief A pill-shaped navigation button for the Launcher.
 * 
 * LauncherNavButton provides a touch-friendly button with:
 * - Icon on the left, text on the right (expanded mode)
 * - Icon only in a 44x44 circle (compact mode for portrait)
 * - Checkable state for view selection (accent color when checked)
 * - Consistent styling with ActionBar and SubToolbar buttons
 * 
 * Phase P.3: Part of the new Launcher UI.
 */
class LauncherNavButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(bool compact READ isCompact WRITE setCompact)

public:
    explicit LauncherNavButton(QWidget* parent = nullptr);
    ~LauncherNavButton() override = default;
    
    // === Icon ===
    
    /**
     * @brief Set the icon using a base name.
     * @param baseName Icon base name (e.g., "timeline" loads timeline.png or timeline_reversed.png)
     */
    void setIconName(const QString& baseName);
    QString iconName() const { return m_iconBaseName; }
    
    /**
     * @brief Set a direct QIcon (overrides iconName).
     */
    void setIcon(const QIcon& icon);
    QIcon icon() const { return m_icon; }
    
    // === Text ===
    
    /**
     * @brief Set the button text (shown in expanded mode).
     */
    void setText(const QString& text);
    QString text() const { return m_text; }
    
    // === State ===
    
    /**
     * @brief Whether the button is checkable (for view selection).
     */
    void setCheckable(bool checkable);
    bool isCheckable() const { return m_checkable; }
    
    /**
     * @brief Whether the button is currently checked.
     */
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    
    // === Display Mode ===
    
    /**
     * @brief Set compact mode (44x44 circle, icon only).
     */
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }
    
    /**
     * @brief Set dark mode for icon theming.
     */
    void setDarkMode(bool dark);
    bool isDarkMode() const;
    
    // === Size ===
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
    // === Constants ===
    static constexpr int BUTTON_HEIGHT = 44;
    static constexpr int EXPANDED_WIDTH = 132;
    static constexpr int ICON_SIZE = 20;
    static constexpr int BORDER_RADIUS = 22; // Half of height for pill shape
    static constexpr int ICON_MARGIN = 12;
    static constexpr int TEXT_MARGIN = 8;

signals:
    void clicked();
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateIcon();
    QColor backgroundColor() const;
    QColor textColor() const;
    
    QString m_iconBaseName;
    QIcon m_icon;
    QString m_text;
    
    bool m_checkable = false;
    bool m_checked = false;
    bool m_compact = false;
    bool m_darkMode = false;
    
    bool m_hovered = false;
    bool m_pressed = false;
};

#endif // LAUNCHERNAVBUTTON_H

