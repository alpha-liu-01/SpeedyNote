#include "TabBar.h"
#include "StyleLoader.h"
#include <QGuiApplication>
#include <QPalette>

TabBar::TabBar(QWidget *parent)
    : QTabBar(parent)
{
    // Configure tab bar behavior
    setExpanding(false);           // Tabs fit content, don't expand to fill
    setMovable(false);             // Disabled: reordering tabs doesn't reorder viewports/documents
    setTabsClosable(true);         // Show close button on each tab
    setUsesScrollButtons(true);    // Show arrows when tabs overflow
    setElideMode(Qt::ElideRight);  // Truncate long titles with "..."
    
    // Apply initial stylesheet to ensure close button position
    // BEFORE any tabs are created (fixes first-tab positioning bug)
    applyInitialStyle();
}

void TabBar::applyInitialStyle()
{
    // Minimal stylesheet to establish close button position
    // Full styling is applied later via updateTheme()
    setStyleSheet(R"(
        QTabBar::close-button {
            subcontrol-position: left;
        }
    )");
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
}

