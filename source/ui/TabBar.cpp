#include "TabBar.h"
#include "StyleLoader.h"
#include <QGuiApplication>
#include <QPalette>

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
}

void TabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();

#ifdef Q_OS_MACOS
    // Fusion + QStyleSheetStyle positions close buttons flush at the tab edge.
    // Nudge each close button inward after Qt finishes its layout pass.
    static constexpr int kCloseButtonInset = 6;
    for (int i = 0; i < count(); ++i) {
        QWidget *btn = tabButton(i, QTabBar::RightSide);
        if (btn)
            btn->move(btn->x() - kCloseButtonInset, btn->y());
    }
#endif
}

void TabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);
#ifdef Q_OS_ANDROID
    installCloseButton(index);
#endif
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
