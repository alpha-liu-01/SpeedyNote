#pragma once

// ============================================================================
// SplitViewManager - Manages dual-pane split view with independent tab bars
// ============================================================================
// Coordinates two independent panes (left/right), each with its own TabBar,
// QStackedWidget, and TabManager. Only one pane is "active" at a time; all
// MainWindow UI actions route through the active pane's viewport.
//
// When not split, the left pane occupies the full width and the right pane
// does not exist. Splitting creates the right pane on demand.
// ============================================================================

#include <QWidget>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVector>
#include <QHash>
#include <QPointer>

#include "widgets/ViewportScrollBar.h"  // ViewportScrollBar::DockEdge used in the API

class TabBar;
class TabManager;
class Document;
class DocumentViewport;
class QStackedWidget;
class QTimer;
class PageWheelPicker;   // SP3: floating page-wheel next to the page-axis handle
class ActionBarButton;   // floating search button stacked under the page-wheel
class MissingPdfBanner;  // per-pane top strip warning about unavailable PDFs
struct PdfSearchMatch;  // SBS3: search-hit results feed the scroll-bar markers

class SplitViewManager : public QWidget {
    Q_OBJECT

public:
    enum Pane { Left = 0, Right = 1 };

    explicit SplitViewManager(QWidget* parent = nullptr);
    ~SplitViewManager() override;

    // =========================================================================
    // Active pane
    // =========================================================================

    Pane activePane() const;
    void setActivePane(Pane pane);
    DocumentViewport* activeViewport() const;
    DocumentViewport* inactiveViewport() const;

    /**
     * @brief The widget occupying the active pane's share of the splitter.
     *
     * Lets overlays that live above the whole canvas (the search bar) anchor to
     * the active pane's corner rather than the window's, which are the same
     * thing only when not split. Never null.
     */
    QWidget* activePaneContainer() const;

    // =========================================================================
    // Split control
    // =========================================================================

    bool isSplit() const;

    /**
     * @brief Move a tab from sourcePane to the opposite pane.
     *
     * If not yet split, the right pane is created first.
     * If moving the last tab out of the right pane, the pane is auto-closed.
     */
    void splitTab(int tabIndex, Pane sourcePane);

    /**
     * @brief Move all right-pane tabs to the left pane, then destroy the right pane.
     */
    void mergePanes();

    // =========================================================================
    // Delegated TabManager API (routes to active pane by default)
    // =========================================================================

    int createTab(Document* doc, const QString& title);
    int createTabInPane(Document* doc, const QString& title, Pane pane);

    int totalTabCount() const;
    int activeTabCount() const;

    TabManager* activeTabManager() const;
    TabManager* leftTabManager() const;
    TabManager* rightTabManager() const;

    TabBar* leftTabBar() const;
    TabBar* rightTabBar() const;

    QStackedWidget* leftViewportStack() const;
    QStackedWidget* rightViewportStack() const;

    // =========================================================================
    // Iteration helpers (for session save, close-all, etc.)
    // =========================================================================

    struct TabRef {
        Pane pane;
        int index;
    };
    QVector<TabRef> allTabs() const;

    /**
     * @brief Apply a function to every TabManager (left, and right if split).
     */
    template<typename Func>
    void forEachTabManager(Func fn) {
        if (m_leftTabManager) fn(m_leftTabManager, Left);
        if (m_rightTabManager) fn(m_rightTabManager, Right);
    }

    /**
     * @brief Get the widget row that contains the tab bars (for layout insertion).
     */
    QWidget* tabBarContainer() const;

    /**
     * @brief Get the QSplitter that holds the viewport stacks (for layout insertion).
     */
    QSplitter* viewportSplitter() const;

    /**
     * @brief Apply theme to all tab bars (current and future).
     *
     * Stores the theme settings so that newly created right panes
     * automatically receive the correct theme.
     */
    void updateTheme(bool darkMode, const QColor& accentColor);

    // =========================================================================
    // Enhanced scroll bars (Plan SB1)
    // =========================================================================

    /**
     * @brief Pin the overlay scroll bars visible (disable proximity auto-hide).
     *
     * Persisted to QSettings; SB4's settings UI can flip this live.
     */
    void setScrollBarsPinned(bool pinned);
    bool scrollBarsPinned() const { return m_scrollBarsPinned; }

    /**
     * @brief Choose which edge each overlay bar docks against (Plan SB4).
     *
     * The page-axis (vertical) bar accepts Left/Right; the cross-axis
     * (horizontal) bar accepts Top/Bottom. Persisted to QSettings and applied
     * live to both panes.
     */
    void setScrollBarVerticalEdge(ViewportScrollBar::DockEdge edge);
    void setScrollBarHorizontalEdge(ViewportScrollBar::DockEdge edge);
    ViewportScrollBar::DockEdge scrollBarVerticalEdge() const { return m_vEdge; }
    ViewportScrollBar::DockEdge scrollBarHorizontalEdge() const { return m_hEdge; }

    /**
     * @brief Tell the manager whether the page-panel action bar (which hosts its
     *        own page-wheel, docked on the LEFT of the viewport) is currently
     *        shown (SP3).
     *
     * When it is shown AND the vertical scroll bar is docked Left, the floating
     * page-wheel would duplicate/overlap it, so it is suppressed. MainWindow
     * calls this whenever the action bar's visibility changes.
     */
    void setPagePanelActionBarShown(bool shown);

    /**
     * @brief Height of the strip a top-docked cross-axis bar occupies, in pane
     *        coordinates, or 0 when that bar is docked at the bottom.
     *
     * Lets an overlay anchored to the top of the viewport clear the bar rather
     * than sit on top of it. Reports the reserved band whether or not the bar is
     * currently faded in, so an overlay positioned against it does not jump
     * every time the bar floats in and out.
     */
    int viewportTopReserve() const;

    /**
     * @brief Recompute the SB2 document map (per-source accents + link markers)
     *        for whichever pane is bound to @p vp and push it to that pane's
     *        vertical bar.
     *
     * Cheap and idempotent; safe to call on any structure/link/theme change.
     * A null @p vp or a vp not currently bound to a pane is a no-op.
     */
    void updateScrollBarDocumentMap(DocumentViewport* vp);

    /**
     * @brief Push SBS2 whole-document search results as amber ticks on the
     *        page-axis bar bound to @p vp (Plan SBS3).
     * @param resultsByPage   Per-page match lists from the streaming scan.
     * @param currentPage     Notebook page of the active Next/Prev match (-1 none).
     * @param currentMatchIndex Index within that page's matches of the active match.
     *
     * No-op for a null/unbound @p vp or an edgeless document. Coexists with the
     * SB2 link markers (separate channel). Positions come from the viewport's
     * page layout so they are stable across zoom.
     */
    void updateScrollBarSearchMarkers(DocumentViewport* vp,
                                      const QHash<int, QVector<PdfSearchMatch>>& resultsByPage,
                                      int currentPage,
                                      int currentMatchIndex);

    /**
     * @brief Remove all search-hit ticks from the bar bound to @p vp (Plan SBS3).
     */
    void clearScrollBarSearchMarkers(DocumentViewport* vp);

signals:
    void activeViewportChanged(DocumentViewport* viewport);
    void activePaneChanged(Pane pane);
    void tabCloseRequested(int tabId, DocumentViewport* viewport, Pane pane);
    void tabCloseAttempted(int tabId, DocumentViewport* viewport, Pane pane);
    void splitStateChanged(bool isSplit);

    /**
     * @brief Emitted whenever the total number of tabs across both panes changes.
     *
     * Used by MainWindow to auto-hide the tab bar container when only one
     * notebook is open.
     */
    void totalTabCountChanged(int total);

    /**
     * @brief Forwarded from a bar's search-tick click (Plan SBS3), tagged with
     *        the bound viewport so MainWindow can reveal + select the match.
     */
    void searchMarkerActivated(DocumentViewport* vp, int pageIndex, qreal normY, int matchIndex);

    /**
     * @brief Emitted when a floating page wheel requests direct page entry.
     *
     * The originating pane is made active before this signal is emitted.
     */
    void jumpToPageRequested();

    /**
     * @brief Emitted when a floating search button is pressed.
     *
     * The originating pane is made active first, the same way the page wheel's
     * jump request does, because the search bar acts on MainWindow's active
     * viewport rather than on a pane it is told about.
     */
    void searchRequested();

    /**
     * @brief Emitted when Review Sources is pressed on a pane's missing-PDF
     *        banner.
     *
     * Carries the viewport the banner was rendering, so the dialog repairs that
     * document rather than whichever happens to be active. The originating pane
     * is made active first, so in practice the two agree.
     */
    void reviewPdfSourcesRequested(DocumentViewport* viewport);

private slots:
    void onLeftViewportChanged(DocumentViewport* vp);
    void onRightViewportChanged(DocumentViewport* vp);
    void onLeftTabCloseAttempted(int index, DocumentViewport* vp);
    void onRightTabCloseAttempted(int index, DocumentViewport* vp);
    void onLeftTabCloseRequested(int index, DocumentViewport* vp);
    void onRightTabCloseRequested(int index, DocumentViewport* vp);

private:
    void createRightPane();
    void destroyRightPane();
    void updateTabBarContainerLayout();
    void updateActivePaneIndicator();
    void recenterAllViewports();
    bool eventFilter(QObject* watched, QEvent* event) override;

    // --- Enhanced scroll bars (SB1) ---
    // The two bars float in / fade out independently, so each axis has its own
    // trigger (proximity / wheel) and its own fade timer.
    enum class BarAxis { Vertical, Horizontal };

    struct PaneBars {
        ViewportScrollBar* vBar = nullptr;   // page axis (vertical), docked left
        ViewportScrollBar* hBar = nullptr;   // cross axis (horizontal), docked top
        PageWheelPicker* wheel = nullptr;    // SP3: floats beside the vertical handle
        ActionBarButton* searchBtn = nullptr;  // stacked under the wheel, same visibility
        MissingPdfBanner* banner = nullptr;  // top strip, driven by bound->pdfWarning()
        QTimer* vFadeTimer = nullptr;        // fades the vertical (page-axis) bar
        QTimer* hFadeTimer = nullptr;        // fades the horizontal (cross-axis) bar
        QPointer<DocumentViewport> bound;
        QMetaObject::Connection cViewToV;
        QMetaObject::Connection cViewToH;
        QMetaObject::Connection cVToView;
        QMetaObject::Connection cHToView;
        QMetaObject::Connection cMarker;   // SB2: vBar markerActivated -> scrollToPage
        QMetaObject::Connection cSearchMarker;  // SBS3: vBar searchMarkerActivated -> forward
        QMetaObject::Connection cViewToWheel;   // SP3: viewport currentPageChanged -> wheel
        QMetaObject::Connection cWheelToView;   // SP3: wheel currentPageChanged -> scrollToPage
        QMetaObject::Connection cPdfWarning;    // viewport pdfWarningChanged -> syncPdfBanner
    };

    QStackedWidget* stackForPane(Pane pane) const;
    DocumentViewport* viewportForPane(Pane pane) const;
    void createScrollBars(Pane pane);
    void destroyScrollBars(Pane pane);
    void repositionScrollBars(Pane pane);
    void bindScrollBars(Pane pane, DocumentViewport* vp);

    /**
     * @brief Render the bound viewport's pdfWarning() into the pane's banner.
     *
     * @param animate Slide it, for a change the user just caused. A rebind onto
     *                a different document passes false, since a 200ms slide on
     *                every tab switch reads as a glitch rather than feedback.
     */
    void syncPdfBanner(Pane pane, bool animate);
    // SP3: float the page-wheel next to the vertical handle / track its
    // visibility. The search button rides along directly beneath it, sharing
    // both the anchor and the visibility rule.
    void repositionPageWheel(Pane pane);
    void syncPageWheelVisibility(Pane pane);
    void refreshHandleSizes(Pane pane);
    // Silently re-align the vertical handle to the bound viewport's current pan
    // offset. Needed after scrollToPage() (which emits no scroll fractions), e.g.
    // when a link marker or the page-wheel jumps to a page.
    void realignVerticalBarToViewport(Pane pane);
    // Per-axis float-in / fade-out: proximity to one edge or a wheel scroll along
    // one axis triggers only that bar. The page-wheel (SP3) tracks the vertical
    // axis only.
    void showScrollBar(Pane pane, BarAxis axis);
    void hideScrollBar(Pane pane, BarAxis axis);
    // Edgeless documents have no meaningful scroll bar; force-hide it regardless
    // of the pinned setting / proximity / scroll floats.
    bool paneIsEdgeless(Pane pane) const;
    void applyEdgelessVisibility(Pane pane);
    void applyScrollBarDarkMode();
    void proximityFloatCheck(QEvent* event);
    void checkPaneProximity(Pane pane, const QPointF& globalPos);
    static bool defaultScrollBarsPinned();

    // Left pane (always exists)
    TabBar* m_leftTabBar = nullptr;
    QStackedWidget* m_leftViewportStack = nullptr;
    TabManager* m_leftTabManager = nullptr;

    // Right pane (created on demand)
    TabBar* m_rightTabBar = nullptr;
    QStackedWidget* m_rightViewportStack = nullptr;
    TabManager* m_rightTabManager = nullptr;

    // Layout
    QWidget* m_tabBarContainer = nullptr;
    QHBoxLayout* m_tabBarLayout = nullptr;
    QSplitter* m_splitter = nullptr;

    Pane m_activePane = Left;

    // Cached theme for applying to newly created panes
    bool m_darkMode = false;
    QColor m_accentColor;

    // Enhanced scroll bars (SB1), indexed by Pane (Left=0, Right=1)
    PaneBars m_paneBars[2];
    bool m_scrollBarsPinned = true;

    // Docked edges (SB4): page-axis bar = Left/Right, cross-axis bar = Top/Bottom.
    ViewportScrollBar::DockEdge m_vEdge = ViewportScrollBar::DockEdge::Left;
    ViewportScrollBar::DockEdge m_hEdge = ViewportScrollBar::DockEdge::Top;
    // SP3: page-panel action bar (left-docked, has its own wheel) currently shown.
    // Suppresses the floating page-wheel when the vertical bar is also on the left.
    bool m_pagePanelActionBarShown = false;
};
