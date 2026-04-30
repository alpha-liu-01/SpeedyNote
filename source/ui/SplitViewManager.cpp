// ============================================================================
// SplitViewManager Implementation
// ============================================================================

#include "SplitViewManager.h"
#include "TabBar.h"
#include "TabManager.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"

#include <QStackedWidget>
#include <QMouseEvent>
#include <QApplication>
#include <QTimer>

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
    // descendant widget (viewports, tab bars, etc.) for pane activation.
    QApplication::instance()->installEventFilter(this);

    // No right pane initially
    m_activePane = Left;
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
    updateActivePaneIndicator();
}

QWidget* SplitViewManager::tabBarContainer() const { return m_tabBarContainer; }
QSplitter* SplitViewManager::viewportSplitter() const { return m_splitter; }

// ============================================================================
// Signal Handlers
// ============================================================================

void SplitViewManager::onLeftViewportChanged(DocumentViewport* vp)
{
    if (m_activePane == Left) {
        emit activeViewportChanged(vp);
    }
}

void SplitViewManager::onRightViewportChanged(DocumentViewport* vp)
{
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

    updateActivePaneIndicator();
    emit splitStateChanged(true);
}

void SplitViewManager::destroyRightPane()
{
    if (!m_rightTabManager)
        return;

    if (m_activePane == Right)
        m_activePane = Left;

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
    // Only process input-initiating events
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
    case QEvent::TouchBegin:
        break;
    default:
        return QWidget::eventFilter(watched, event);
    }

    if (!isSplit())
        return QWidget::eventFilter(watched, event);

    QWidget* target = qobject_cast<QWidget*>(watched);
    if (!target)
        return QWidget::eventFilter(watched, event);

    // Walk up the parent chain to determine which pane the widget belongs to
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

    return QWidget::eventFilter(watched, event);
}
