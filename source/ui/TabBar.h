#ifndef TABBAR_H
#define TABBAR_H

#include <QTabBar>
#include <QColor>
#include <QPoint>

class QContextMenuEvent;
class QTimer;

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

    /**
     * @brief Enable or disable the "Split" context menu action.
     *
     * When the split view is already active and this is the right pane,
     * the label changes to "Split Left"; otherwise "Split Right".
     */
    void setSplitEnabled(bool enabled);
    void setMergeEnabled(bool enabled);

signals:
    void splitRequested(int tabIndex);
    void mergeAllRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
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
    void showSplitMenu(const QPoint& globalPos, int tabIndex);

    bool m_splitEnabled = true;
    bool m_mergeEnabled = false;
    QTimer* m_longPressTimer = nullptr;
    QPoint m_pressPos;
    int m_pressTabIndex = -1;

#ifdef Q_OS_ANDROID
    void installCloseButton(int index);
    void updateCloseButtonIcons();
    bool m_darkMode = false;
#endif
};

#endif // TABBAR_H
