#ifndef THEMECOLORS_H
#define THEMECOLORS_H

#include <QColor>
#include <QMenu>

/**
 * @brief Unified color palette for consistent theming across the application.
 * 
 * These colors are used throughout the UI for backgrounds, borders, text,
 * and semantic indicators. All components should use these constants
 * instead of hardcoded color values.
 * 
 * The palette supports both light and dark modes with carefully chosen
 * colors that provide good contrast and visual hierarchy.
 */
namespace ThemeColors {

// ============================================================================
// Base Gray Palette
// ============================================================================

// Dark mode grays (cool-tinted)
inline QColor darkPrimary()     { return QColor(0x2a, 0x2e, 0x32); }  // #2a2e32 - backgrounds
inline QColor darkSecondary()   { return QColor(0x3a, 0x3e, 0x42); }  // #3a3e42 - hover states
inline QColor darkTertiary()    { return QColor(0x4d, 0x4d, 0x4d); }  // #4d4d4d - borders, separators

// Light mode grays
inline QColor lightPrimary()    { return QColor(0xF5, 0xF5, 0xF5); }  // #F5F5F5 - backgrounds
inline QColor lightSecondary()  { return QColor(0xE8, 0xE8, 0xE8); }  // #E8E8E8 - hover states
inline QColor lightTertiary()   { return QColor(0xD0, 0xD0, 0xD0); }  // #D0D0D0 - borders, separators

// ============================================================================
// Convenience Functions (select by dark mode)
// ============================================================================

inline QColor background(bool dark)     { return dark ? darkPrimary() : QColor(Qt::white); }
inline QColor backgroundAlt(bool dark)  { return dark ? darkPrimary() : lightPrimary(); }
inline QColor hover(bool dark)          { return dark ? darkSecondary() : lightSecondary(); }
inline QColor border(bool dark)         { return dark ? darkTertiary() : lightTertiary(); }
inline QColor separator(bool dark)      { return dark ? darkTertiary() : lightTertiary(); }

// ============================================================================
// Text Colors
// ============================================================================

inline QColor textPrimary(bool dark)    { return dark ? QColor(240, 240, 240) : QColor(30, 30, 30); }
inline QColor textSecondary(bool dark)  { return dark ? QColor(180, 180, 180) : QColor(100, 100, 100); }
inline QColor textMuted(bool dark)      { return dark ? QColor(150, 150, 150) : QColor(120, 120, 120); }
inline QColor textDisabled(bool dark)   { return dark ? QColor(100, 100, 100) : QColor(150, 150, 150); }

// ============================================================================
// Selection & Accent Colors
// ============================================================================

// Selection highlight (subtle blue tint)
inline QColor selection(bool dark)      { return dark ? QColor(50, 80, 120) : QColor(220, 235, 250); }
inline QColor selectionBorder(bool dark){ return dark ? QColor(138, 180, 248) : QColor(26, 115, 232); }

// Hover state for interactive items (cards, list items)
inline QColor itemHover(bool dark)      { return dark ? QColor(55, 55, 60) : QColor(248, 248, 252); }
inline QColor itemBackground(bool dark) { return dark ? QColor(45, 45, 50) : QColor(255, 255, 255); }

// Pressed/active state
inline QColor pressed(bool dark)        { return dark ? QColor(60, 60, 65) : QColor(235, 235, 240); }

// ============================================================================
// Semantic Colors
// ============================================================================

// Star indicator (gold/yellow)
inline QColor star(bool dark)           { return dark ? QColor(255, 200, 50) : QColor(230, 180, 30); }

// Notebook type indicators
inline QColor typePdf(bool dark)        { return dark ? QColor(200, 100, 100) : QColor(180, 60, 60); }
inline QColor typeEdgeless(bool dark)   { return dark ? QColor(100, 180, 100) : QColor(60, 140, 60); }
inline QColor typePaged(bool dark)      { return dark ? QColor(100, 140, 200) : QColor(60, 100, 180); }

// ============================================================================
// Card-Specific Colors
// ============================================================================

// Card shadow (light mode only, use with alpha)
inline QColor cardShadow()              { return QColor(0, 0, 0, 25); }

// Thumbnail placeholder background
inline QColor thumbnailBg(bool dark)    { return dark ? QColor(50, 50, 55) : QColor(235, 235, 240); }
inline QColor thumbnailPlaceholder(bool dark) { return dark ? QColor(100, 100, 105) : QColor(180, 180, 185); }

// Card border
inline QColor cardBorder(bool dark)     { return dark ? QColor(70, 70, 75) : QColor(220, 220, 225); }

// ============================================================================
// Folder Header Colors
// ============================================================================

inline QColor chevron(bool dark)        { return dark ? QColor(150, 150, 150) : QColor(100, 100, 100); }
inline QColor folderText(bool dark)     { return dark ? QColor(220, 220, 220) : QColor(50, 50, 50); }
inline QColor folderSeparator(bool dark){ return dark ? QColor(70, 70, 75) : QColor(220, 220, 225); }

// ============================================================================
// Menu Styling Helper
// ============================================================================

/**
 * @brief Style a QMenu with rounded corners and theme-appropriate colors.
 * 
 * This function sets up the menu with a frameless window and translucent
 * background to prevent the native window manager from drawing a rectangular
 * frame around the rounded corners.
 * 
 * Call this immediately after creating the menu, before adding actions.
 */
inline void styleMenu(QMenu* menu, bool dark) {
    if (!menu) return;
    
    // Required for true rounded corners on Linux/X11
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    
    QColor bgColor = background(dark);
    QColor textClr = textPrimary(dark);
    QColor hoverClr = itemHover(dark);
    QColor borderClr = border(dark);
    QColor disabledClr = textSecondary(dark);
    
    menu->setStyleSheet(QString(
        "QMenu {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  color: %3;"
        "  padding: 8px 16px;"
        "  border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: %4;"
        "}"
        "QMenu::item:disabled {"
        "  color: %5;"
        "}"
    ).arg(bgColor.name(), borderClr.name(), textClr.name(), 
          hoverClr.name(), disabledClr.name()));
}

} // namespace ThemeColors

#endif // THEMECOLORS_H
