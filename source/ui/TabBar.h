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
     * @brief Defense-in-depth reposition trigger after Qt's tab layout.
     *
     * The primary mechanism for keeping close buttons inset is the
     * eventFilter() above, which reacts whenever Qt's style or
     * QStyleSheetStyle::polish() moves/resizes a button. tabLayoutChange()
     * just adds a synchronous + deferred reposition pass at known layout
     * boundaries, in case some platform suppresses the per-button
     * Move/Resize events (e.g. for a no-op setGeometry).
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
    /// from the right edge of its tab, vertically centered. Two safeties:
    ///  - Skips buttons whose size has not yet been resolved by
    ///    QStyleSheetStyle::polish (size() is 0x0 on a freshly-created
    ///    CloseButton on macOS, where polish runs AFTER tabLayoutChange);
    ///    the deferred pass picks them up next event-loop tick.
    ///  - Skips move() when the button is already at the target position.
    ///    Without this, the eventFilter() install on each button would turn
    ///    our own move() into an infinite Move-event ping-pong.
    void repositionCloseButtons();

    /// Post a deferred (next event-loop tick) call to repositionCloseButtons().
    /// Coalesced via m_closeBtnRepositionPending so a burst of triggers
    /// (eventFilter callbacks + tabLayoutChange + nested setGeometry)
    /// produces at most one extra reposition pass per tick.
    /// Needed because:
    ///  - macOS Fusion polishes the close button AFTER tabLayoutChange,
    ///    so a synchronous move() against width()==0 is wrong.
    ///  - macOS / Flatpak Fusion re-apply SE_TabBarTabRightButton on tab
    ///    switch (and possibly on :selected re-polish), overwriting our
    ///    inset; the deferred pass runs after that and corrects.
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
