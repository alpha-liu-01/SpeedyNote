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
    /// Minimum width below which a single tab is not allowed to shrink.
    /// When N * kMinTabWidth would exceed the bar width, Qt's scroll
    /// buttons take over (setUsesScrollButtons(true)).
    static constexpr int kMinTabWidth = 80;

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

    /**
     * @brief Emitted after a tab is inserted or removed.
     *
     * Lets observers (e.g. SplitViewManager) react to any count change
     * without having to manually emit at every mutation call site.
     */
    void tabCountChanged(int newCount);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    /**
     * @brief Catch Move/Resize events on close-button widgets.
     *
     * On macOS Fusion + QStyleSheetStyle, switching tabs causes Qt to
     * re-apply SE_TabBarTabRightButton (and possibly re-polish the button
     * via the :selected pseudo-state) AFTER our deferred reposition has
     * already run. tabLayoutChange() is not always the last writer in
     * that case. Watching the buttons directly lets us re-apply our inset
     * whenever any external code mutates their geometry.
     *
     * Idempotency in repositionCloseButtons() (it skips move() when the
     * button is already at the target position) breaks the otherwise
     * infinite Move-event ping-pong our own move() would create.
     */
    bool eventFilter(QObject* obj, QEvent* event) override;
    /**
     * @brief Adjust close button positions after Qt's tab layout pass.
     * 
     * On macOS, Fusion + QStyleSheetStyle places close buttons flush at
     * the tab edge.  This override nudges them inward for proper spacing.
     */
    void tabLayoutChange() override;
    
    /**
     * @brief Replace close button when a new tab is inserted (Android).
     *
     * Also requests a layout update so equal-width sizing rebalances.
     */
    void tabInserted(int index) override;

    /**
     * @brief Request a layout update when a tab is removed so widths rebalance.
     */
    void tabRemoved(int index) override;

    /**
     * @brief Re-query tab size hints when the bar is resized (split drag,
     *        window resize) so each tab tracks barWidth / max(N, 2).
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief Equal-width sizing: each tab takes barWidth / max(count(), 2),
     *        clamped to a minimum of kMinTabWidth.
     */
    QSize tabSizeHint(int index) const override;

private:
    void showSplitMenu(const QPoint& globalPos, int tabIndex);

    /// Reposition every close button to the same inset (kCloseButtonRightGap)
    /// from the right edge of its tab, vertically centered. Skips buttons
    /// whose width/height has not yet been resolved by QStyleSheetStyle::polish
    /// (size() is 0x0 on a freshly-created CloseButton on macOS, for example).
    void repositionCloseButtons();

    /// Post a deferred reposition via QTimer::singleShot(0, ...). Required on
    /// macOS Fusion + QStyleSheetStyle, where the close button is sized via
    /// polish() AFTER our tabLayoutChange() returns; without the deferral our
    /// synchronous move() runs against a still-zero-sized widget. Coalesced
    /// via m_closeBtnRepositionPending so back-to-back tabLayoutChange calls
    /// produce only one extra event-loop trip.
    void scheduleCloseButtonReposition();

    bool m_splitEnabled = true;
    bool m_mergeEnabled = false;
    QTimer* m_longPressTimer = nullptr;
    QPoint m_pressPos;
    int m_pressTabIndex = -1;
    bool m_closeBtnRepositionPending = false;

#ifdef Q_OS_ANDROID
    void installCloseButton(int index);
    void updateCloseButtonIcons();
    bool m_darkMode = false;
#endif
};

#endif // TABBAR_H
