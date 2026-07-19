// ============================================================================
// SplitViewManager Implementation
// ============================================================================

#include "SplitViewManager.h"
#include "TabBar.h"
#include "TabManager.h"
#include "widgets/ViewportScrollBar.h"
#include "widgets/PageWheelPicker.h"  // SP3: floating page-wheel next to the handle
#include "../core/DocumentViewport.h"
#include "../core/Document.h"
#include "../core/DarkModeUtils.h"
#include "../pdf/PdfSearchEngine.h"  // SBS3: PdfSearchMatch for search-tick placement
#include "../compat/qt_compat.h"     // Qt5/Qt6 input-device + position shims

#include <QStackedWidget>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QHash>

// ============================================================================
// Constructor / Destructor
// ============================================================================

SplitViewManager::SplitViewManager(QWidget* parent)
    : QWidget(parent)
{
    // --- Tab bar container (horizontal row of tab bars) ---
    m_tabBarContainer = new QWidget(this);
    m_tabBarLayout = new QHBoxLayout(m_tabBarContainer);
    m_tabBarLayout->setContentsMargins(0, 0, 0, 0);
    m_tabBarLayout->setSpacing(0);

    // --- Viewport splitter ---
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(3);

    // Sync tab bar widths with splitter proportions
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        if (!isSplit() || !m_rightTabBar) return;
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() >= 2 && sizes[0] + sizes[1] > 0) {
            m_tabBarLayout->setStretch(0, sizes[0]);
            m_tabBarLayout->setStretch(1, sizes[1]);
        }
    });

    // --- Create left pane (always exists) ---
    m_leftTabBar = new TabBar(m_tabBarContainer);
    m_leftViewportStack = new QStackedWidget(m_splitter);
    m_leftViewportStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftViewportStack->setMinimumWidth(200);
    m_leftTabManager = new TabManager(m_leftTabBar, m_leftViewportStack, this);

    m_tabBarLayout->addWidget(m_leftTabBar, 1);
    m_splitter->addWidget(m_leftViewportStack);

    // Connect left pane signals
    connect(m_leftTabManager, &TabManager::currentViewportChanged,
            this, &SplitViewManager::onLeftViewportChanged);
    connect(m_leftTabManager, &TabManager::tabCloseAttempted,
            this, &SplitViewManager::onLeftTabCloseAttempted);
    connect(m_leftTabManager, &TabManager::tabCloseRequested,
            this, &SplitViewManager::onLeftTabCloseRequested);

    // Tab context menu: split/merge
    connect(m_leftTabBar, &TabBar::splitRequested, this, [this](int index) {
        splitTab(index, Left);
    });
    connect(m_leftTabBar, &TabBar::mergeAllRequested, this, [this]() {
        mergePanes();
    });

    // Forward left-pane tab count changes to the unified totalTabCountChanged signal.
    connect(m_leftTabBar, &TabBar::tabCountChanged, this, [this](int) {
        emit totalTabCountChanged(totalTabCount());
    });

    // Application-level event filter catches mouse/tablet/touch on ANY
    // descendant widget (viewports, tab bars, etc.) for pane activation
    // and scroll-bar proximity/reposition (SB1).
    QApplication::instance()->installEventFilter(this);

    // No right pane initially
    m_activePane = Left;

    // Enhanced scroll bars (SB1): read the persisted pin state, then create
    // the always-present left pane's overlay bars.
    {
        QSettings settings;
        m_scrollBarsPinned = settings.value(QStringLiteral("scrollbar/pinned"),
                                             defaultScrollBarsPinned()).toBool();
        // SB4: docked edges (page-axis Left/Right, cross-axis Top/Bottom).
        m_vEdge = settings.value(QStringLiteral("scrollbar/verticalEdge"),
                                 QStringLiteral("Left")).toString() == QLatin1String("Right")
                      ? ViewportScrollBar::DockEdge::Right
                      : ViewportScrollBar::DockEdge::Left;
        m_hEdge = settings.value(QStringLiteral("scrollbar/horizontalEdge"),
                                 QStringLiteral("Top")).toString() == QLatin1String("Bottom")
                      ? ViewportScrollBar::DockEdge::Bottom
                      : ViewportScrollBar::DockEdge::Top;
    }
    createScrollBars(Left);
}

SplitViewManager::~SplitViewManager()
{
    // TabManagers are children of this, so Qt handles deletion
}

// ============================================================================
// Active Pane
// ============================================================================

SplitViewManager::Pane SplitViewManager::activePane() const
{
    return m_activePane;
}

void SplitViewManager::setActivePane(Pane pane)
{
    if (pane == Right && !m_rightTabManager)
        return;

    if (m_activePane != pane) {
        m_activePane = pane;
        updateActivePaneIndicator();
        emit activePaneChanged(pane);
        DocumentViewport* vp = activeViewport();
        emit activeViewportChanged(vp);
    }
}

DocumentViewport* SplitViewManager::activeViewport() const
{
    TabManager* tm = activeTabManager();
    return tm ? tm->currentViewport() : nullptr;
}

DocumentViewport* SplitViewManager::inactiveViewport() const
{
    if (!isSplit()) return nullptr;
    TabManager* tm = (m_activePane == Left) ? m_rightTabManager : m_leftTabManager;
    return tm ? tm->currentViewport() : nullptr;
}

// ============================================================================
// Split Control
// ============================================================================

bool SplitViewManager::isSplit() const
{
    return m_rightTabManager != nullptr;
}

void SplitViewManager::splitTab(int tabIndex, Pane sourcePane)
{
    TabManager* source = (sourcePane == Left) ? m_leftTabManager : m_rightTabManager;
    if (!source || tabIndex < 0 || tabIndex >= source->tabCount())
        return;

    // Don't split if it's the only tab in the only pane
    if (source->tabCount() <= 1 && !isSplit())
        return;

    // Don't move if source has only 1 tab and it's the left pane while split
    // (moving it right would empty the left; the auto-merge handles right→empty)
    if (source->tabCount() <= 1 && sourcePane == Left && isSplit())
        return;

    // Create right pane if needed
    if (!isSplit()) {
        createRightPane();
    }

    TabManager* target = (sourcePane == Left) ? m_rightTabManager : m_leftTabManager;

    // Detach viewport from source, attach to target (preserving state)
    TabManager::DetachedTab tab = source->detachTab(tabIndex);
    if (!tab.viewport)
        return;

    target->attachTab(tab.viewport, tab.title, tab.modified, tab.tabId);

    // If source pane (right) is now empty, auto-merge
    if (m_rightTabManager && m_rightTabManager->tabCount() == 0) {
        destroyRightPane();
    }

    // Activate the target pane
    Pane targetPane = (sourcePane == Left) ? Right : Left;
    setActivePane(targetPane);

    // Recenter all visible viewports after geometry settles
    recenterAllViewports();
}

void SplitViewManager::mergePanes()
{
    if (!isSplit())
        return;

    // Move all tabs from right to left (preserving state)
    while (m_rightTabManager->tabCount() > 0) {
        TabManager::DetachedTab tab = m_rightTabManager->detachTab(0);
        if (!tab.viewport) break;

        m_leftTabManager->attachTab(tab.viewport, tab.title, tab.modified, tab.tabId);
    }

    destroyRightPane();
    setActivePane(Left);

    // Recenter after merging back to single pane
    recenterAllViewports();
}

// ============================================================================
// Delegated TabManager API
// ============================================================================

int SplitViewManager::createTab(Document* doc, const QString& title)
{
    return createTabInPane(doc, title, m_activePane);
}

int SplitViewManager::createTabInPane(Document* doc, const QString& title, Pane pane)
{
    TabManager* tm = (pane == Left) ? m_leftTabManager : m_rightTabManager;
    if (!tm) tm = m_leftTabManager;
    return tm->createTab(doc, title);
}

int SplitViewManager::totalTabCount() const
{
    int count = m_leftTabManager ? m_leftTabManager->tabCount() : 0;
    if (m_rightTabManager) count += m_rightTabManager->tabCount();
    return count;
}

int SplitViewManager::activeTabCount() const
{
    TabManager* tm = activeTabManager();
    return tm ? tm->tabCount() : 0;
}

TabManager* SplitViewManager::activeTabManager() const
{
    if (m_activePane == Right && m_rightTabManager)
        return m_rightTabManager;
    return m_leftTabManager;
}

TabManager* SplitViewManager::leftTabManager() const { return m_leftTabManager; }
TabManager* SplitViewManager::rightTabManager() const { return m_rightTabManager; }
TabBar* SplitViewManager::leftTabBar() const { return m_leftTabBar; }
TabBar* SplitViewManager::rightTabBar() const { return m_rightTabBar; }
QStackedWidget* SplitViewManager::leftViewportStack() const { return m_leftViewportStack; }
QStackedWidget* SplitViewManager::rightViewportStack() const { return m_rightViewportStack; }

// ============================================================================
// Iteration
// ============================================================================

QVector<SplitViewManager::TabRef> SplitViewManager::allTabs() const
{
    QVector<TabRef> refs;
    if (m_leftTabManager) {
        for (int i = 0; i < m_leftTabManager->tabCount(); ++i)
            refs.append({Left, i});
    }
    if (m_rightTabManager) {
        for (int i = 0; i < m_rightTabManager->tabCount(); ++i)
            refs.append({Right, i});
    }
    return refs;
}

void SplitViewManager::updateTheme(bool darkMode, const QColor& accentColor)
{
    m_darkMode = darkMode;
    m_accentColor = accentColor;
    if (m_leftTabBar) m_leftTabBar->updateTheme(darkMode, accentColor);
    if (m_rightTabBar) m_rightTabBar->updateTheme(darkMode, accentColor);
    applyScrollBarDarkMode();
    updateActivePaneIndicator();
}

QWidget* SplitViewManager::tabBarContainer() const { return m_tabBarContainer; }
QSplitter* SplitViewManager::viewportSplitter() const { return m_splitter; }

// ============================================================================
// Signal Handlers
// ============================================================================

void SplitViewManager::onLeftViewportChanged(DocumentViewport* vp)
{
    bindScrollBars(Left, vp);
    if (m_activePane == Left) {
        emit activeViewportChanged(vp);
    }
}

void SplitViewManager::onRightViewportChanged(DocumentViewport* vp)
{
    bindScrollBars(Right, vp);
    if (m_activePane == Right) {
        emit activeViewportChanged(vp);
    }
}

void SplitViewManager::onLeftTabCloseAttempted(int index, DocumentViewport* vp)
{
    int tabId = m_leftTabManager->tabIdAt(index);
    emit tabCloseAttempted(tabId, vp, Left);
}

void SplitViewManager::onRightTabCloseAttempted(int index, DocumentViewport* vp)
{
    if (!m_rightTabManager) return;
    int tabId = m_rightTabManager->tabIdAt(index);
    emit tabCloseAttempted(tabId, vp, Right);
}

void SplitViewManager::onLeftTabCloseRequested(int index, DocumentViewport* vp)
{
    int tabId = m_leftTabManager->tabIdAt(index);
    emit tabCloseRequested(tabId, vp, Left);
}

void SplitViewManager::onRightTabCloseRequested(int index, DocumentViewport* vp)
{
    if (!m_rightTabManager) return;
    int tabId = m_rightTabManager->tabIdAt(index);
    emit tabCloseRequested(tabId, vp, Right);
}

// ============================================================================
// Private: Pane Management
// ============================================================================

void SplitViewManager::createRightPane()
{
    if (m_rightTabManager)
        return;

    m_rightTabBar = new TabBar(m_tabBarContainer);
    m_rightViewportStack = new QStackedWidget(m_splitter);
    m_rightViewportStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightViewportStack->setMinimumWidth(200);
    m_rightTabManager = new TabManager(m_rightTabBar, m_rightViewportStack, this);

    m_splitter->addWidget(m_rightViewportStack);

    // Even split
    m_splitter->setSizes({1, 1});

    updateTabBarContainerLayout();

    // Connect right pane signals
    connect(m_rightTabManager, &TabManager::currentViewportChanged,
            this, &SplitViewManager::onRightViewportChanged);
    connect(m_rightTabManager, &TabManager::tabCloseAttempted,
            this, &SplitViewManager::onRightTabCloseAttempted);
    connect(m_rightTabManager, &TabManager::tabCloseRequested,
            this, &SplitViewManager::onRightTabCloseRequested);

    // Tab context menu: split/merge
    connect(m_rightTabBar, &TabBar::splitRequested, this, [this](int index) {
        splitTab(index, Right);
    });
    connect(m_rightTabBar, &TabBar::mergeAllRequested, this, [this]() {
        mergePanes();
    });

    // Forward right-pane tab count changes to the unified totalTabCountChanged signal.
    connect(m_rightTabBar, &TabBar::tabCountChanged, this, [this](int) {
        emit totalTabCountChanged(totalTabCount());
    });

    // Apply cached theme to new tab bar
    if (m_accentColor.isValid())
        m_rightTabBar->updateTheme(m_darkMode, m_accentColor);

    // Update context menu state
    m_leftTabBar->setMergeEnabled(true);
    m_rightTabBar->setMergeEnabled(true);

    // SB1: give the new pane its own overlay scroll bars, bound to its viewport.
    createScrollBars(Right);

    updateActivePaneIndicator();
    emit splitStateChanged(true);
}

void SplitViewManager::destroyRightPane()
{
    if (!m_rightTabManager)
        return;

    if (m_activePane == Right)
        m_activePane = Left;

    // SB1: tear down the right pane's overlay bars before the stack is deleted.
    destroyScrollBars(Right);

    // Disconnect all signals so no stale emissions occur
    disconnect(m_rightTabManager, nullptr, this, nullptr);
    disconnect(m_rightTabBar, nullptr, this, nullptr);

    // Hide immediately so they disappear from the UI
    m_rightTabBar->hide();
    m_rightViewportStack->hide();

    // Use deleteLater() instead of delete -- the context menu action
    // that triggered mergePanes() may still be on the call stack inside
    // TabBar::contextMenuEvent, so destroying the TabBar synchronously
    // would be a use-after-free.
    m_rightTabManager->deleteLater();
    m_rightTabBar->deleteLater();
    m_rightViewportStack->deleteLater();

    m_rightTabManager = nullptr;
    m_rightTabBar = nullptr;
    m_rightViewportStack = nullptr;

    updateTabBarContainerLayout();

    // Update context menu state
    m_leftTabBar->setMergeEnabled(false);

    updateActivePaneIndicator();
    emit splitStateChanged(false);

    // Title refresh: setActivePane() short-circuits when the new pane equals
    // the old (we set m_activePane=Left above), so subscribers like the
    // navigation bar would otherwise miss the post-merge viewport switch
    // (the last activeViewportChanged could be nullptr from the right pane
    // emptying while it was still the active pane).
    emit activeViewportChanged(activeViewport());

    // Tab-bar autohide refresh: when the right pane is destroyed with 0 tabs
    // (e.g., user closed the last right tab while left has only 1 tab), the
    // surviving left TabBar's count never changed so it emits no tabCountChanged,
    // and the right TabBar's deferred tabCountChanged is dropped because we
    // disconnected it above. Without this explicit emit, MainWindow's autohide
    // handler would never re-evaluate after the merge.
    emit totalTabCountChanged(totalTabCount());
}

void SplitViewManager::updateTabBarContainerLayout()
{
    // Remove all items from layout (without deleting the widgets themselves)
    while (m_tabBarLayout->count() > 0) {
        delete m_tabBarLayout->takeAt(0);
    }

    m_tabBarLayout->addWidget(m_leftTabBar, 1);
    m_leftTabBar->show();

    if (m_rightTabBar) {
        m_tabBarLayout->addWidget(m_rightTabBar, 1);
        m_rightTabBar->show();
    }
}

void SplitViewManager::updateActivePaneIndicator()
{
    if (!isSplit()) {
        m_leftViewportStack->setStyleSheet(QString());
        return;
    }

    QString color = m_accentColor.isValid() ? m_accentColor.name()
                                            : QStringLiteral("palette(highlight)");
    QString activeStyle = QStringLiteral(
        "QStackedWidget { border-top: 2px solid %1; }").arg(color);
    static const QString inactiveStyle = QStringLiteral(
        "QStackedWidget { border-top: 2px solid transparent; }");

    m_leftViewportStack->setStyleSheet(
        m_activePane == Left ? activeStyle : inactiveStyle);
    if (m_rightViewportStack) {
        m_rightViewportStack->setStyleSheet(
            m_activePane == Right ? activeStyle : inactiveStyle);
    }
}

// ============================================================================
// Recenter viewports after layout change (split/merge)
// ============================================================================

void SplitViewManager::recenterAllViewports()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_leftTabManager) {
            if (DocumentViewport* vp = m_leftTabManager->currentViewport())
                vp->zoomToWidth();
        }
        if (m_rightTabManager) {
            if (DocumentViewport* vp = m_rightTabManager->currentViewport())
                vp->zoomToWidth();
        }
    });
}

// ============================================================================
// Event Filter (pane activation on any interaction)
// ============================================================================

bool SplitViewManager::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();

    // SB1: keep overlay bars laid out when a pane stack resizes or is shown.
    if (type == QEvent::Resize || type == QEvent::Show) {
        if (watched == m_leftViewportStack) {
            repositionScrollBars(Left);
        } else if (m_rightViewportStack && watched == m_rightViewportStack) {
            repositionScrollBars(Right);
        }
    }

    // SB1: pen/mouse proximity floats the bars in (palm-rejected inside).
    if (type == QEvent::MouseMove || type == QEvent::TabletMove) {
        proximityFloatCheck(event);
    }

    // Pane activation on any interaction (only meaningful when split).
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
    case QEvent::TouchBegin:
        if (isSplit()) {
            if (QWidget* target = qobject_cast<QWidget*>(watched)) {
                // Walk up the parent chain to find the owning pane.
                for (QWidget* w = target; w != nullptr; w = w->parentWidget()) {
                    if (w == m_leftViewportStack || w == m_leftTabBar) {
                        setActivePane(Left);
                        break;
                    }
                    if (w == m_rightViewportStack || w == m_rightTabBar) {
                        setActivePane(Right);
                        break;
                    }
                }
            }
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

// ============================================================================
// Enhanced scroll bars (Plan SB1)
// ============================================================================

bool SplitViewManager::defaultScrollBarsPinned()
{
    // Default to pinned (always visible) when a physical keyboard is present,
    // matching the pre-SB1 keyboard-keyed visibility behavior.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto devices = QInputDevice::devices();
    for (const QInputDevice* device : devices) {
        if (device && device->type() == QInputDevice::DeviceType::Keyboard) {
            return true;
        }
    }
    return false;
#else
    // Qt5 has no QInputDevice enumeration; default to pinned (desktop-friendly).
    return true;
#endif
}

QStackedWidget* SplitViewManager::stackForPane(Pane pane) const
{
    return (pane == Left) ? m_leftViewportStack : m_rightViewportStack;
}

DocumentViewport* SplitViewManager::viewportForPane(Pane pane) const
{
    TabManager* tm = (pane == Left) ? m_leftTabManager : m_rightTabManager;
    return tm ? tm->currentViewport() : nullptr;
}

void SplitViewManager::createScrollBars(Pane pane)
{
    QStackedWidget* stack = stackForPane(pane);
    if (!stack) return;

    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (b.vBar) return;  // already created

    b.vBar = new ViewportScrollBar(Qt::Vertical, m_vEdge, stack);
    b.hBar = new ViewportScrollBar(Qt::Horizontal, m_hEdge, stack);
    b.vBar->setDarkMode(m_darkMode);
    b.hBar->setDarkMode(m_darkMode);

    // SP3: page-wheel that floats beside the vertical handle. Visibility is driven
    // by the page-axis bar (see syncPageWheelVisibility); start hidden.
    b.wheel = new PageWheelPicker(stack);
    b.wheel->setDarkMode(m_darkMode);
    b.wheel->setVisible(false);

    // Independent fade timers so each axis hides on its own inactivity.
    b.vFadeTimer = new QTimer(this);
    b.vFadeTimer->setSingleShot(true);
    b.vFadeTimer->setInterval(2500);  // ~2.5s of inactivity before fade-out
    connect(b.vFadeTimer, &QTimer::timeout, this, [this, pane]() {
        hideScrollBar(pane, BarAxis::Vertical);
    });
    b.hFadeTimer = new QTimer(this);
    b.hFadeTimer->setSingleShot(true);
    b.hFadeTimer->setInterval(2500);
    connect(b.hFadeTimer, &QTimer::timeout, this, [this, pane]() {
        hideScrollBar(pane, BarAxis::Horizontal);
    });

    // Initial visibility follows the pin state.
    b.vBar->setVisible(m_scrollBarsPinned);
    b.hBar->setVisible(m_scrollBarsPinned);

    repositionScrollBars(pane);
    bindScrollBars(pane, viewportForPane(pane));
}

void SplitViewManager::destroyScrollBars(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    disconnect(b.cViewToV);
    disconnect(b.cViewToH);
    disconnect(b.cVToView);
    disconnect(b.cHToView);
    disconnect(b.cMarker);
    disconnect(b.cSearchMarker);
    disconnect(b.cViewToWheel);
    disconnect(b.cWheelToView);
    if (b.vFadeTimer) { b.vFadeTimer->stop(); delete b.vFadeTimer; }
    if (b.hFadeTimer) { b.hFadeTimer->stop(); delete b.hFadeTimer; }
    delete b.vBar;
    delete b.hBar;
    delete b.wheel;
    b = PaneBars{};
}

void SplitViewManager::repositionScrollBars(Pane pane)
{
    QStackedWidget* stack = stackForPane(pane);
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!stack || !b.vBar || !b.hBar) return;

    const int thickness = ViewportScrollBar::barThickness();
    const int margin = 3;
    const int corner = 15;  // gap where the two bars would meet
    const int w = stack->width();
    const int h = stack->height();

    const bool vRight  = (m_vEdge == ViewportScrollBar::DockEdge::Right);
    const bool hBottom = (m_hEdge == ViewportScrollBar::DockEdge::Bottom);

    // Vertical (page-axis) bar: docked left or right, spanning the height with
    // the corner reserved at the end the horizontal bar occupies.
    const int vX = vRight ? (w - thickness - margin) : margin;
    const int vY = hBottom ? margin : (corner + margin);
    const int vBottomReserve = hBottom ? m_bottomInset : 0;
    b.vBar->setGeometry(vX,
                        vY,
                        thickness,
                        qMax(0, h - corner - margin * 2 - vBottomReserve));

    // Horizontal (cross-axis) bar: docked top or bottom, spanning the width with
    // the corner reserved at the end the vertical bar occupies. When docked at
    // the bottom it is lifted by m_bottomInset to clear the search bar (SB4).
    const int hX = vRight ? margin : (corner + margin);
    const int hY = hBottom ? (h - thickness - margin - m_bottomInset) : margin;
    b.hBar->setGeometry(hX,
                        hY,
                        qMax(0, w - corner - margin * 2),
                        thickness);
    b.vBar->raise();
    b.hBar->raise();

    // SP3: keep the floating page-wheel aligned with the (possibly moved) handle.
    repositionPageWheel(pane);
}

void SplitViewManager::repositionPageWheel(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.wheel || !b.vBar) return;
    QStackedWidget* stack = stackForPane(pane);
    if (!stack) return;

    const int ww = b.wheel->width();
    const int wh = b.wheel->height();
    const int gap = 4;

    // Vertical: center the wheel on the handle midpoint, clamped inside the pane.
    const int handleCenterY = b.vBar->y() + qRound(b.vBar->handleCenterPx());
    int y = handleCenterY - wh / 2;
    y = qBound(0, y, qMax(0, stack->height() - wh));

    // Horizontal: sit on the content-facing side of the vertical bar so the wheel
    // never overlaps the bar itself.
    int x;
    if (m_vEdge == ViewportScrollBar::DockEdge::Right) {
        x = b.vBar->x() - ww - gap;
    } else {
        x = b.vBar->x() + b.vBar->width() + gap;
    }
    x = qBound(0, x, qMax(0, stack->width() - ww));

    b.wheel->move(x, y);
    if (b.wheel->isVisible()) b.wheel->raise();
}

void SplitViewManager::syncPageWheelVisibility(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.wheel) return;

    // The wheel lives and dies with the page-axis bar; edgeless panes never show
    // either (they have no meaningful page position). It is also suppressed when
    // the left-docked page-panel action bar (which hosts its own wheel) is shown
    // and the vertical bar is on the left, to avoid a duplicate/overlap (SP3).
    // That action bar only overlaps the left pane, so in split view the right
    // pane's wheel is exempt from the conflict.
    const bool rightPaneInSplit = isSplit() && (pane == Right);
    const bool actionBarConflict =
        m_pagePanelActionBarShown && (m_vEdge == ViewportScrollBar::DockEdge::Left)
        && !rightPaneInSplit;
    const bool visible = b.vBar && b.vBar->isVisible()
                      && !paneIsEdgeless(pane) && !actionBarConflict;
    if (visible) {
        repositionPageWheel(pane);
        b.wheel->setVisible(true);
        b.wheel->raise();
    } else {
        b.wheel->setVisible(false);
    }
}

void SplitViewManager::bindScrollBars(Pane pane, DocumentViewport* vp)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.vBar || !b.hBar) return;

    // Drop connections to the previous viewport.
    disconnect(b.cViewToV);
    disconnect(b.cViewToH);
    disconnect(b.cVToView);
    disconnect(b.cHToView);
    disconnect(b.cMarker);
    disconnect(b.cSearchMarker);
    disconnect(b.cViewToWheel);
    disconnect(b.cWheelToView);
    b.cViewToV = b.cViewToH = b.cVToView = b.cHToView = b.cMarker
              = b.cSearchMarker = b.cViewToWheel = b.cWheelToView
              = QMetaObject::Connection{};

    b.bound = vp;
    if (!vp) return;

    // Initialize handle sizes and positions from the viewport's current state.
    refreshHandleSizes(pane);
    {
        qreal zoom = vp->zoomLevel();
        if (zoom <= 0) zoom = 1.0;
        const QPointF panOffset = vp->panOffset();
        const QSizeF content = vp->totalContentSize();
        const qreal viewW = vp->width() / zoom;
        const qreal viewH = vp->height() / zoom;
        const qreal scrollW = content.width() - viewW;
        const qreal scrollH = content.height() - viewH;
        b.vBar->setFraction(scrollH > 0 ? qBound(0.0, panOffset.y() / scrollH, 1.0) : 0.0);
        b.hBar->setFraction(scrollW > 0 ? qBound(0.0, panOffset.x() / scrollW, 1.0) : 0.0);
    }

    // Viewport -> bar (programmatic; does not feed back).
    b.cViewToV = connect(vp, &DocumentViewport::verticalScrollChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (!pb.vBar) return;
        refreshHandleSizes(pane);
        // emitScrollFractions() always emits both axes, so a pure horizontal pan
        // re-emits the vertical fraction unchanged. Only float the vertical bar in
        // when the page axis actually moved.
        const bool changed = !qFuzzyCompare(f + 1.0, pb.vBar->fraction() + 1.0);
        pb.vBar->setFraction(f);
        if (changed) showScrollBar(pane, BarAxis::Vertical);
    });
    b.cViewToH = connect(vp, &DocumentViewport::horizontalScrollChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (!pb.hBar) return;
        refreshHandleSizes(pane);
        // Only float the horizontal bar in when the cross axis actually moved, so a
        // pure vertical pan (plain/backtick wheel) never summons it.
        const bool changed = !qFuzzyCompare(f + 1.0, pb.hBar->fraction() + 1.0);
        pb.hBar->setFraction(f);
        if (changed) showScrollBar(pane, BarAxis::Horizontal);
    });

    // Bar -> viewport (user interaction only).
    b.cVToView = connect(b.vBar, &ViewportScrollBar::fractionChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound) pb.bound->setVerticalScrollFraction(f);
        repositionPageWheel(pane);  // SP3: follow the handle during a user drag
    });
    b.cHToView = connect(b.hBar, &ViewportScrollBar::fractionChanged, this,
                         [this, pane](qreal f) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound) pb.bound->setHorizontalScrollFraction(f);
    });

    // SB2: clicking a link marker jumps the bound viewport to that page.
    // scrollToPage emits no scroll fractions, so re-align the handle afterwards
    // (otherwise the tick jumps the page but the handle stays put).
    b.cMarker = connect(b.vBar, &ViewportScrollBar::markerActivated, this,
                        [this, pane](int pageIndex) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound && pageIndex >= 0) {
            pb.bound->scrollToPage(pageIndex);
            realignVerticalBarToViewport(pane);
            repositionPageWheel(pane);
        }
    });

    // SBS3: forward a search-tick click up to MainWindow (tagged with the vp)
    // so it can reveal + select the exact match and keep Next/Prev in sync.
    b.cSearchMarker = connect(b.vBar, &ViewportScrollBar::searchMarkerActivated, this,
                              [this, pane](int pageIndex, qreal normY, int matchIndex) {
        PaneBars& pb = m_paneBars[static_cast<int>(pane)];
        if (pb.bound) emit searchMarkerActivated(pb.bound, pageIndex, normY, matchIndex);
    });

    // SP3: initialize the floating page-wheel from the viewport, then wire the
    // two-way sync. setCurrentPage is guarded with blockSignals so the init and
    // the viewport->wheel path never echo back into scrollToPage.
    if (b.wheel) {
        Document* doc = vp->document();
        b.wheel->setPageCount(doc ? qMax(1, doc->pageCount()) : 1);
        b.wheel->blockSignals(true);
        b.wheel->setCurrentPage(vp->currentPageIndex());
        b.wheel->blockSignals(false);

        // Viewport -> wheel (programmatic display update; does not feed back).
        b.cViewToWheel = connect(vp, &DocumentViewport::currentPageChanged, this,
                                 [this, pane](int p) {
            PaneBars& pb = m_paneBars[static_cast<int>(pane)];
            if (!pb.wheel) return;
            pb.wheel->blockSignals(true);
            pb.wheel->setCurrentPage(p);
            pb.wheel->blockSignals(false);
            repositionPageWheel(pane);
        });

        // Wheel -> viewport (user drag/wheel; emitted only on snap finish).
        // scrollToPage does not emit scroll fractions, so re-align the bar handle
        // from the new pan offset afterwards, then reposition the wheel.
        b.cWheelToView = connect(b.wheel, &PageWheelPicker::currentPageChanged, this,
                                 [this, pane](int p) {
            PaneBars& pb = m_paneBars[static_cast<int>(pane)];
            if (!pb.bound || !pb.vBar) return;
            pb.bound->scrollToPage(p);
            realignVerticalBarToViewport(pane);
            repositionPageWheel(pane);
        });
    }

    // SB2: compute the per-source accent + link-marker document map now.
    updateScrollBarDocumentMap(vp);

    // Force-hide in edgeless mode; otherwise restore the correct paged
    // visibility (pinned -> shown, unpinned -> hidden/float) for this pane.
    applyEdgelessVisibility(pane);

    // Keep the bars above the (possibly newly shown) viewport.
    b.vBar->raise();
    b.hBar->raise();

    // SP3: match the wheel's visibility to the (now-updated) bar visibility.
    syncPageWheelVisibility(pane);
}

void SplitViewManager::updateScrollBarDocumentMap(DocumentViewport* vp)
{
    if (!vp) return;

    // Find the pane currently bound to this viewport.
    int paneIdx = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].bound == vp) { paneIdx = i; break; }
    }
    if (paneIdx < 0) return;

    PaneBars& b = m_paneBars[paneIdx];
    if (!b.vBar) return;

    Document* doc = vp->document();

    // SP3: keep the floating page-wheel's range current across add/insert/delete/
    // import (MainWindow calls this on page-structure changes).
    if (b.wheel) b.wheel->setPageCount(doc ? qMax(1, doc->pageCount()) : 1);

    if (!doc) {
        b.vBar->setAccentRegions({});
        b.vBar->setMarkers({});
        return;
    }

    // --- Per-source accent bands ---------------------------------------
    // Single-source (or plain) documents get no stripes: parity with SB1.
    QVector<ViewportScrollBar::AccentRegion> accents;
    const QStringList order = doc->sourceDisplayOrder();
    if (order.size() > 1) {
        // The palette slot IS the index in sourceDisplayOrder(); precompute a
        // lookup so the per-page loop stays O(pages) instead of calling
        // paletteSlotForSource() (which rebuilds the order list every call).
        QHash<QString, int> slotOf;
        slotOf.reserve(order.size());
        for (int i = 0; i < order.size(); ++i) slotOf.insert(order[i], i);

        const int pageCount = doc->pageCount();
        int runStart = -1;
        int runSlot = -2;  // -2 = no active run
        auto flushRun = [&](int runEnd) {
            if (runSlot < 0 || runStart < 0) return;  // plain-page run: no stripe
            const qreal start = vp->pageTrackFraction(runStart);
            const qreal end = vp->pageTrackFraction(runEnd + 1);  // bottom of last page
            if (start >= 0.0 && end > start) {
                const QColor c = DarkModeUtils::sourceAccentColor(runSlot, m_darkMode);
                if (c.isValid()) accents.push_back({ start, end, c });
            }
        };
        for (int i = 0; i < pageCount; ++i) {
            QString srcId;
            int pdfPage = -1;
            int slot = -1;  // plain page
            if (doc->pdfBindingForNotebookPage(i, srcId, pdfPage)) {
                slot = slotOf.value(srcId, -1);
            }
            if (slot != runSlot) {
                flushRun(i - 1);
                runSlot = slot;
                runStart = i;
            }
        }
        flushRun(pageCount - 1);
    }
    b.vBar->setAccentRegions(accents);

    // --- Link markers ---------------------------------------------------
    QVector<ViewportScrollBar::BarMarker> markers;
    const QVector<Document::PageLinkMarker> pageMarkers = doc->pageLinkMarkers();
    markers.reserve(pageMarkers.size());
    for (const Document::PageLinkMarker& pm : pageMarkers) {
        const qreal frac = vp->pageTrackFraction(pm.pageIndex);
        if (frac < 0.0) continue;
        ViewportScrollBar::BarMarker m;
        m.pos = frac;
        m.color = pm.color;
        m.pageIndex = pm.pageIndex;
        m.kind = ViewportScrollBar::MarkerKind::Link;
        m.tooltip = pm.description;
        markers.push_back(std::move(m));
    }
    b.vBar->setMarkers(markers);
}

void SplitViewManager::updateScrollBarSearchMarkers(
    DocumentViewport* vp,
    const QHash<int, QVector<PdfSearchMatch>>& resultsByPage,
    int currentPage,
    int currentMatchIndex)
{
    if (!vp) return;

    int paneIdx = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].bound == vp) { paneIdx = i; break; }
    }
    if (paneIdx < 0) return;

    PaneBars& b = m_paneBars[paneIdx];
    if (!b.vBar) return;

    Document* doc = vp->document();
    if (!doc || doc->isEdgeless() || resultsByPage.isEmpty()) {
        b.vBar->clearSearchMarkers();
        return;
    }

    // A global cap keeps huge result sets cheap to build/paint. Beyond it we
    // coarsen to one tick per page (page-top) that still counts all matches.
    static constexpr int kSearchMarkerCap = 2000;
    int totalMatches = 0;
    for (auto it = resultsByPage.constBegin(); it != resultsByPage.constEnd(); ++it) {
        totalMatches += it.value().size();
    }
    const bool coarsen = totalMatches > kSearchMarkerCap;

    QVector<ViewportScrollBar::BarMarker> raw;
    raw.reserve(coarsen ? resultsByPage.size() : totalMatches);

    for (auto it = resultsByPage.constBegin(); it != resultsByPage.constEnd(); ++it) {
        const int page = it.key();
        const QVector<PdfSearchMatch>& matches = it.value();
        if (matches.isEmpty()) continue;

        const qreal f0 = vp->pageTrackFraction(page);
        if (f0 < 0.0) continue;  // unmappable (e.g. layout not ready)
        const qreal f1 = vp->pageTrackFraction(page + 1);
        const qreal span = (f1 > f0) ? (f1 - f0) : 0.0;

        if (coarsen) {
            // One page-level tick summarizing the whole page.
            const PdfSearchMatch& first = matches.first();
            ViewportScrollBar::BarMarker m;
            m.pos = f0;
            m.kind = ViewportScrollBar::MarkerKind::SearchHit;
            m.pageIndex = page;
            m.matchIndex = first.matchIndex;
            m.normY = 0.0;
            m.matchCount = matches.size();
            m.current = (page == currentPage);
            raw.push_back(std::move(m));
            continue;
        }

        for (const PdfSearchMatch& match : matches) {
            const qreal normY = vp->searchMatchPageYFraction(match);
            if (normY < 0.0) continue;  // tile/edgeless source: no page-axis pos
            ViewportScrollBar::BarMarker m;
            m.pos = f0 + normY * span;
            m.kind = ViewportScrollBar::MarkerKind::SearchHit;
            m.pageIndex = page;
            m.matchIndex = match.matchIndex;
            m.normY = normY;
            m.matchCount = 1;
            m.current = (page == currentPage && match.matchIndex == currentMatchIndex);
            raw.push_back(std::move(m));
        }
    }

    b.vBar->setSearchMarkers(raw);
}

void SplitViewManager::clearScrollBarSearchMarkers(DocumentViewport* vp)
{
    if (!vp) return;
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].bound == vp && m_paneBars[i].vBar) {
            m_paneBars[i].vBar->clearSearchMarkers();
            return;
        }
    }
}

void SplitViewManager::refreshHandleSizes(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    DocumentViewport* vp = b.bound;
    if (!vp || !b.vBar || !b.hBar) return;

    qreal zoom = vp->zoomLevel();
    if (zoom <= 0) zoom = 1.0;
    const QSizeF content = vp->totalContentSize();
    if (content.width() <= 0 || content.height() <= 0) return;

    const qreal viewW = vp->width() / zoom;
    const qreal viewH = vp->height() / zoom;
    b.vBar->setHandleFraction(qBound(0.0, viewH / content.height(), 1.0));
    b.hBar->setHandleFraction(qBound(0.0, viewW / content.width(), 1.0));
}

void SplitViewManager::realignVerticalBarToViewport(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    DocumentViewport* vp = b.bound;
    if (!vp || !b.vBar) return;

    qreal zoom = vp->zoomLevel();
    if (zoom <= 0) zoom = 1.0;
    const QPointF panOffset = vp->panOffset();
    const QSizeF content = vp->totalContentSize();
    const qreal scrollH = content.height() - vp->height() / zoom;
    // setFraction is a silent display update (no fractionChanged), so it never
    // feeds back into setVerticalScrollFraction.
    b.vBar->setFraction(scrollH > 0 ? qBound(0.0, panOffset.y() / scrollH, 1.0) : 0.0);
}

bool SplitViewManager::paneIsEdgeless(Pane pane) const
{
    const DocumentViewport* vp = m_paneBars[static_cast<int>(pane)].bound;
    return vp && vp->document() && vp->document()->isEdgeless();
}

void SplitViewManager::applyEdgelessVisibility(Pane pane)
{
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!b.vBar || !b.hBar) return;

    if (paneIsEdgeless(pane)) {
        if (b.vFadeTimer) b.vFadeTimer->stop();
        if (b.hFadeTimer) b.hFadeTimer->stop();
        b.vBar->setVisible(false);
        b.hBar->setVisible(false);
    } else if (m_scrollBarsPinned) {
        if (b.vFadeTimer) b.vFadeTimer->stop();  // pinned bars never fade
        if (b.hFadeTimer) b.hFadeTimer->stop();
        b.vBar->setVisible(true);
        b.hBar->setVisible(true);
        b.vBar->raise();
        b.hBar->raise();
    } else {
        // Not pinned, not edgeless: leave hidden and float in on demand.
        b.vBar->setVisible(false);
        b.hBar->setVisible(false);
    }

    syncPageWheelVisibility(pane);  // SP3: wheel tracks the page-axis bar
}

void SplitViewManager::showScrollBar(Pane pane, BarAxis axis)
{
    // Edgeless documents never show the scroll bar (overrides pin/proximity/scroll).
    if (paneIsEdgeless(pane)) return;

    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    ViewportScrollBar* bar = (axis == BarAxis::Vertical) ? b.vBar : b.hBar;
    QTimer* timer = (axis == BarAxis::Vertical) ? b.vFadeTimer : b.hFadeTimer;
    if (!bar) return;

    if (!bar->isVisible()) { bar->setVisible(true); bar->raise(); }

    // SP3: the page-wheel floats with the vertical (page-axis) bar only.
    if (axis == BarAxis::Vertical) syncPageWheelVisibility(pane);

    // When pinned, the bar stays up; otherwise (re)start this axis's fade timer.
    if (m_scrollBarsPinned) {
        if (timer) timer->stop();
    } else if (timer) {
        timer->start();
    }
}

void SplitViewManager::hideScrollBar(Pane pane, BarAxis axis)
{
    if (m_scrollBarsPinned) return;
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    ViewportScrollBar* bar = (axis == BarAxis::Vertical) ? b.vBar : b.hBar;
    QTimer* timer = (axis == BarAxis::Vertical) ? b.vFadeTimer : b.hFadeTimer;

    // Never hide while the user is actively driving this axis's handle (or, for
    // the vertical axis, the page-wheel). Re-arm the fade timer instead of just
    // bailing: the wheel path navigates via scrollToPage, which emits no scroll
    // signal to re-arm it, so without this the bar + wheel could linger visible.
    const bool busy = (bar && bar->isDragging())
                   || (axis == BarAxis::Vertical && b.wheel && b.wheel->isInteracting());
    if (busy) {
        if (timer) timer->start();
        return;
    }
    if (bar) bar->setVisible(false);
    if (axis == BarAxis::Vertical) syncPageWheelVisibility(pane);  // SP3: hide wheel with bar
}

void SplitViewManager::applyScrollBarDarkMode()
{
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].vBar) m_paneBars[i].vBar->setDarkMode(m_darkMode);
        if (m_paneBars[i].hBar) m_paneBars[i].hBar->setDarkMode(m_darkMode);
        if (m_paneBars[i].wheel) m_paneBars[i].wheel->setDarkMode(m_darkMode);  // SP3
        // SB2 accent colors are theme-dependent, so recompute the document map.
        if (m_paneBars[i].bound) updateScrollBarDocumentMap(m_paneBars[i].bound);
    }
}

void SplitViewManager::setScrollBarsPinned(bool pinned)
{
    if (m_scrollBarsPinned == pinned) {
        return;
    }
    m_scrollBarsPinned = pinned;
    QSettings().setValue(QStringLiteral("scrollbar/pinned"), pinned);

    for (int i = 0; i < 2; ++i) {
        PaneBars& b = m_paneBars[i];
        if (!b.vBar) continue;
        if (pinned) {
            // Show for paged panes; edgeless panes stay hidden (single source
            // of truth for the pinned/edgeless decision).
            applyEdgelessVisibility(static_cast<Pane>(i));
        } else {
            // Begin fading the currently-shown bars (each axis independently).
            if (b.vFadeTimer) b.vFadeTimer->start();
            if (b.hFadeTimer) b.hFadeTimer->start();
        }
    }
}

void SplitViewManager::setScrollBarVerticalEdge(ViewportScrollBar::DockEdge edge)
{
    if (edge != ViewportScrollBar::DockEdge::Left &&
        edge != ViewportScrollBar::DockEdge::Right) {
        return;  // page-axis bar only lives on the left/right edges
    }
    if (m_vEdge == edge) return;
    m_vEdge = edge;
    QSettings().setValue(QStringLiteral("scrollbar/verticalEdge"),
                         edge == ViewportScrollBar::DockEdge::Right
                             ? QStringLiteral("Right") : QStringLiteral("Left"));
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].vBar) m_paneBars[i].vBar->setDockEdge(edge);
        repositionScrollBars(static_cast<Pane>(i));
        // SP3: moving to/from the left edge changes the action-bar conflict.
        syncPageWheelVisibility(static_cast<Pane>(i));
    }
}

void SplitViewManager::setPagePanelActionBarShown(bool shown)
{
    if (m_pagePanelActionBarShown == shown) return;
    m_pagePanelActionBarShown = shown;
    // Only affects the floating page-wheel; re-evaluate it for both panes.
    for (int i = 0; i < 2; ++i) {
        syncPageWheelVisibility(static_cast<Pane>(i));
    }
}

void SplitViewManager::setScrollBarHorizontalEdge(ViewportScrollBar::DockEdge edge)
{
    if (edge != ViewportScrollBar::DockEdge::Top &&
        edge != ViewportScrollBar::DockEdge::Bottom) {
        return;  // cross-axis bar only lives on the top/bottom edges
    }
    if (m_hEdge == edge) return;
    m_hEdge = edge;
    QSettings().setValue(QStringLiteral("scrollbar/horizontalEdge"),
                         edge == ViewportScrollBar::DockEdge::Bottom
                             ? QStringLiteral("Bottom") : QStringLiteral("Top"));
    for (int i = 0; i < 2; ++i) {
        if (m_paneBars[i].hBar) m_paneBars[i].hBar->setDockEdge(edge);
        repositionScrollBars(static_cast<Pane>(i));
    }
}

void SplitViewManager::setViewportBottomInset(int px)
{
    px = qMax(0, px);
    if (m_bottomInset == px) return;
    m_bottomInset = px;
    repositionScrollBars(Left);
    if (isSplit()) repositionScrollBars(Right);
}

void SplitViewManager::proximityFloatCheck(QEvent* event)
{
    // Palm rejection: only a real pen (tablet) or a non-finger mouse may arm
    // the float. Touch-synthesized mouse moves are ignored.
    QPointF globalPos;
    if (event->type() == QEvent::TabletMove) {
        globalPos = SN_TABLET_GLOBAL_POSF(static_cast<QTabletEvent*>(event));
    } else {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (SN_MOUSE_IS_FINGER(me)) {
            return;
        }
        globalPos = SN_MOUSE_GLOBAL_POSF(me);
    }

    checkPaneProximity(Left, globalPos);
    if (isSplit()) {
        checkPaneProximity(Right, globalPos);
    }
}

void SplitViewManager::checkPaneProximity(Pane pane, const QPointF& globalPos)
{
    QStackedWidget* stack = stackForPane(pane);
    PaneBars& b = m_paneBars[static_cast<int>(pane)];
    if (!stack || !b.vBar || !stack->isVisible()) return;

    const QPoint local = stack->mapFromGlobal(globalPos.toPoint());
    if (!stack->rect().contains(local)) return;

    // Arm when the pointer is near the docked edges the bars actually live on
    // (SB4: left/right for the vertical bar, top/bottom for the horizontal bar),
    // including the region the bars themselves occupy.
    const int threshold = 24;
    const int w = stack->width();
    const int h = stack->height();
    const bool nearVEdge = (m_vEdge == ViewportScrollBar::DockEdge::Right)
                               ? (local.x() >= w - threshold)
                               : (local.x() <= threshold);
    const bool nearHEdge = (m_hEdge == ViewportScrollBar::DockEdge::Bottom)
                               ? (local.y() >= h - threshold)
                               : (local.y() <= threshold);
    // Proximity to one edge floats in only that axis's bar, independently.
    if (nearVEdge) showScrollBar(pane, BarAxis::Vertical);
    if (nearHEdge) showScrollBar(pane, BarAxis::Horizontal);
}
