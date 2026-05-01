#include "TabBar.h"
#include "StyleLoader.h"
#include <QGuiApplication>
#include <QPalette>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>

// macOS native style (QMacStyle) ignores QSS for QTabBar::close-button,
// rendering it with Cocoa drawing instead.  Applying Fusion to the tab bar
// ensures our QSS image / sizing / color properties take effect.
#ifdef Q_OS_MACOS
#include <QStyle>
#include <QStyleFactory>
#endif

// Android: QTabBar::close-button QSS pseudo-element is not applied to the
// internal close button widget. We replace it with a custom QToolButton.
#ifdef Q_OS_ANDROID
#include <QToolButton>
#include <QIcon>
#endif

TabBar::TabBar(QWidget *parent)
    : QTabBar(parent)
{
#ifdef Q_OS_MACOS
    auto *fusion = QStyleFactory::create("Fusion");
    fusion->setParent(this);
    setStyle(fusion);
#endif

    // Configure tab bar behavior
    setExpanding(false);           // Tabs fit content, don't expand to fill
    setMovable(false);             // Disabled: reordering tabs doesn't reorder viewports/documents
    setTabsClosable(true);         // Show close button on each tab (right side, default)
    setUsesScrollButtons(true);    // Show arrows when tabs overflow
    setElideMode(Qt::ElideRight);  // Truncate long titles with "..."

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(500);
    connect(m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (m_pressTabIndex >= 0) {
            showSplitMenu(mapToGlobal(m_pressPos), m_pressTabIndex);
            m_pressTabIndex = -1;
        }
    });
}

void TabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();

    // Sync pass: wins on platforms that already size the close button
    // synchronously (Windows native, Linux KDE/Plasma). repositionCloseButtons()
    // skips any button whose size is still 0, so a freshly-created button
    // on macOS is left alone here and handled by the deferred pass below.
    repositionCloseButtons();

    // Deferred pass: on macOS (Fusion + QStyleSheetStyle), the close button
    // is sized via polish() AFTER tabLayoutChange returns, and tab-switches
    // re-polish (which would reset geometry to flush-right). Posting via
    // QTimer::singleShot(0, ...) lets us run after the next event-loop tick,
    // by which time the button has its 20x20 QSS-driven size and any
    // post-tabLayoutChange Qt internals have settled. Last writer wins.
    scheduleCloseButtonReposition();
}

void TabBar::repositionCloseButtons()
{
    // Place each close button at the same inset from its tab's right edge,
    // vertically centered. Computing absolute coords from tabRect() makes us
    // immune to whichever placement Qt's active QStyle picks for
    // SE_TabBarTabRightButton (Fusion: flush right; Windows native: small
    // inset; Plasma Breeze: small inset; Flatpak: Fusion-like flush right).
    static constexpr int kCloseButtonRightGap = 6;
    for (int i = 0; i < count(); ++i) {
        QWidget *btn = tabButton(i, QTabBar::RightSide);
        // Skip unsized buttons. On macOS QStyleSheetStyle::polish has not
        // applied the QSS width/height yet on a fresh CloseButton, so
        // btn->size() is 0x0. Using width()==0 here would put the button's
        // top-left near the tab's right edge, and once polish resizes it to
        // 20x20 it would extend past the edge and look flush-right.
        if (!btn || btn->width() <= 0 || btn->height() <= 0) continue;
        const QRect tab = tabRect(i);
        const int x = tab.right() - btn->width() - kCloseButtonRightGap + 1;
        const int y = tab.top() + (tab.height() - btn->height()) / 2;
        btn->move(x, y);
    }
}

void TabBar::scheduleCloseButtonReposition()
{
    if (m_closeBtnRepositionPending) return;
    m_closeBtnRepositionPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_closeBtnRepositionPending = false;
        repositionCloseButtons();
    });
}

void TabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);
#ifdef Q_OS_ANDROID
    installCloseButton(index);
#endif
    // Rebalance equal-width tab sizing now that count() has changed.
    updateGeometry();
    // Defer the emit: TabManager::createTab() temporarily calls
    // blockSignals(true) around addTab(), which would silently swallow
    // a synchronous emit here. Posting via singleShot(0) lets us fire
    // after the surrounding code re-enables signals.
    QTimer::singleShot(0, this, [this]() {
        emit tabCountChanged(count());
    });
}

void TabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);
    // Rebalance equal-width tab sizing now that count() has changed.
    updateGeometry();
    // Same deferral as tabInserted - some callers may have signals blocked.
    QTimer::singleShot(0, this, [this]() {
        emit tabCountChanged(count());
    });
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    QTabBar::resizeEvent(event);
    // Bar width changed (window resize / splitter drag); re-query
    // every tabSizeHint so tabs reflow to barWidth / max(N, 2).
    updateGeometry();
}

QSize TabBar::tabSizeHint(int index) const
{
    QSize base = QTabBar::tabSizeHint(index);

    // Equal-width rule: 1 tab still uses half-width (other half empty),
    // 2 tabs each take half, 3 each take a third, etc.
    const int n = qMax(count(), 2);
    const int barWidth = width();
    if (barWidth <= 0)
        return base;

    const int target = barWidth / n;
    const int finalW = qMax(target, kMinTabWidth);
    return QSize(finalW, base.height());
}

void TabBar::setSplitEnabled(bool enabled) { m_splitEnabled = enabled; }
void TabBar::setMergeEnabled(bool enabled) { m_mergeEnabled = enabled; }

void TabBar::showSplitMenu(const QPoint& globalPos, int tabIndex)
{
    QMenu menu(this);

    if (m_splitEnabled) {
        QAction* splitAction = menu.addAction(tr("Split"));
        connect(splitAction, &QAction::triggered, this, [this, tabIndex]() {
            emit splitRequested(tabIndex);
        });
    }

    if (m_mergeEnabled) {
        QAction* mergeAction = menu.addAction(tr("Merge All to Left"));
        connect(mergeAction, &QAction::triggered, this, [this]() {
            emit mergeAllRequested();
        });
    }

    if (!menu.isEmpty())
        menu.exec(globalPos);
}

void TabBar::contextMenuEvent(QContextMenuEvent* event)
{
    // Stop the long-press timer to prevent a duplicate menu on platforms
    // where Qt synthesizes contextMenuEvent from a long touch press.
    m_longPressTimer->stop();
    m_pressTabIndex = -1;

    int tabIndex = tabAt(event->pos());
    if (tabIndex < 0)
        return;

    showSplitMenu(event->globalPos(), tabIndex);
}

void TabBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_pressTabIndex = tabAt(m_pressPos);
        if (m_pressTabIndex >= 0)
            m_longPressTimer->start();
    }
    QTabBar::mousePressEvent(event);
}

void TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_longPressTimer->stop();
    m_pressTabIndex = -1;
    QTabBar::mouseReleaseEvent(event);
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_longPressTimer->isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > 10)
            m_longPressTimer->stop();
    }
    QTabBar::mouseMoveEvent(event);
}

void TabBar::updateTheme(bool darkMode, const QColor &accentColor)
{
    // Use system window color for selected tab (follows KDE/system theme)
    QPalette sysPalette = QGuiApplication::palette();
    QColor selectedBg = sysPalette.color(QPalette::Window);
    QColor textColor = sysPalette.color(QPalette::WindowText);
    
    // Washed out accent: lighter and desaturated for inactive tabs
    QColor washedColor = accentColor;
    if (darkMode) {
        // Dark mode: darken and desaturate
        washedColor = washedColor.darker(120);
        washedColor.setHsl(washedColor.hslHue(), 
                          washedColor.hslSaturation() * 0.6, 
                          washedColor.lightness());
    } else {
        // Light mode: lighten significantly
        washedColor = washedColor.lighter(150);
        washedColor.setHsl(washedColor.hslHue(), 
                          washedColor.hslSaturation() * 0.5, 
                          qMin(washedColor.lightness() + 30, 255));
    }
    
    // Hover color: between washed and full accent
    QColor hoverColor = darkMode ? accentColor.darker(105) : accentColor.lighter(115);
    
    // Load stylesheet from QSS file with placeholder substitution
    QString tabStylesheet = StyleLoader::loadTabStylesheet(
        darkMode,
        accentColor,    // Tab bar background
        washedColor,    // Inactive tab background
        textColor,      // Text color
        selectedBg,     // Selected tab background
        hoverColor      // Hover background
    );
    setStyleSheet(tabStylesheet);

#ifdef Q_OS_ANDROID
    m_darkMode = darkMode;
    updateCloseButtonIcons();
#endif
}

#ifdef Q_OS_ANDROID
static QString closeButtonStyle(bool darkMode)
{
    QString hoverBg = darkMode
        ? QStringLiteral("rgba(255, 255, 255, 50)")
        : QStringLiteral("rgba(0, 0, 0, 30)");
    return QStringLiteral(
        "QToolButton { border: none; border-radius: 9px; padding: 0px;"
        "              background: transparent; }"
        "QToolButton:hover { background-color: %1; }").arg(hoverBg);
}

static QIcon closeButtonIcon(bool darkMode)
{
    return QIcon(darkMode ? QStringLiteral(":/resources/icons/cross_reversed.png")
                          : QStringLiteral(":/resources/icons/cross.png"));
}

void TabBar::installCloseButton(int index)
{
    auto* btn = new QToolButton(this);
    btn->setIcon(closeButtonIcon(m_darkMode));
    btn->setIconSize(QSize(18, 18));
    btn->setFixedSize(18, 18);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::ArrowCursor);
    btn->setStyleSheet(closeButtonStyle(m_darkMode));
    
    connect(btn, &QToolButton::clicked, this, [this, btn]() {
        for (int i = 0; i < count(); ++i) {
            if (tabButton(i, QTabBar::RightSide) == btn) {
                emit tabCloseRequested(i);
                return;
            }
        }
    });
    
    setTabButton(index, QTabBar::RightSide, btn);
}

void TabBar::updateCloseButtonIcons()
{
    QIcon icon = closeButtonIcon(m_darkMode);
    QString style = closeButtonStyle(m_darkMode);
    
    for (int i = 0; i < count(); ++i) {
        if (auto* btn = qobject_cast<QToolButton*>(tabButton(i, QTabBar::RightSide))) {
            btn->setIcon(icon);
            btn->setStyleSheet(style);
        }
    }
}
#endif
