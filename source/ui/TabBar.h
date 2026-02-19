#ifndef TABBAR_H
#define TABBAR_H

#include <QTabBar>
#include <QColor>

/**
 * @brief Custom TabBar for SpeedyNote's document tabs
 * 
 * Phase C.2: Extracted from MainWindow for better maintainability.
 * Handles:
 * - Tab bar configuration (expanding, movable, closable, scroll buttons)
 * - Theme-aware styling via QSS
 * - Close button on each tab (right side)
 * 
 * On macOS, Fusion style is applied to the tab bar so that QSS properties
 * (image, size, colors) work for the close button.  The native QMacStyle
 * ignores QSS for QTabBar::close-button.
 * 
 * On Android, the QTabBar::close-button QSS pseudo-element is not applied
 * to the internal close button widget. Custom QToolButtons are created
 * programmatically and set via setTabButton() to replace the defaults.
 * 
 * Usage:
 *   TabBar *tabBar = new TabBar(parent);
 *   tabBar->updateTheme(isDarkMode, accentColor);
 */
class TabBar : public QTabBar
{
    Q_OBJECT

public:
    /**
     * @brief Construct a new TabBar
     * @param parent Parent widget
     * 
     * Configures the tab bar with:
     * - Non-expanding tabs (fit content width)
     * - Close buttons on each tab
     * - Scroll buttons for overflow
     * - Text elision for long titles
     */
    explicit TabBar(QWidget *parent = nullptr);
    
    /**
     * @brief Update tab bar styling for current theme
     * @param darkMode Whether dark mode is active
     * @param accentColor The accent color (from QSettings or system)
     * 
     * Applies complete QSS styling including:
     * - Tab bar background (accent color)
     * - Inactive tab background (washed/desaturated accent)
     * - Selected tab background (system window color)
     * - Hover effects
     * - Theme-appropriate icons (close, scroll arrows)
     */
    void updateTheme(bool darkMode, const QColor &accentColor);

protected:
    /**
     * @brief Adjust close button positions after Qt's tab layout pass.
     * 
     * On macOS, Fusion + QStyleSheetStyle places close buttons flush at
     * the tab edge.  This override nudges them inward for proper spacing.
     */
    void tabLayoutChange() override;
    
    /**
     * @brief Replace close button when a new tab is inserted (Android).
     */
    void tabInserted(int index) override;

private:
#ifdef Q_OS_ANDROID
    void installCloseButton(int index);
    void updateCloseButtonIcons();
    bool m_darkMode = false;
#endif
};

#endif // TABBAR_H
