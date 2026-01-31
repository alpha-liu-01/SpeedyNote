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
// Base Gray Palette (matches launcher stylesheets)
// ============================================================================

// Dark mode grays - aligned with launcher_dark.qss
inline QColor darkPrimary()     { return QColor(0x1e, 0x1e, 0x1e); }  // #1e1e1e - main background
inline QColor darkSecondary()   { return QColor(0x2d, 0x2d, 0x2d); }  // #2d2d2d - card/input backgrounds
inline QColor darkTertiary()    { return QColor(0x3a, 0x3a, 0x3a); }  // #3a3a3a - borders, separators

// Light mode grays - aligned with launcher.qss
inline QColor lightPrimary()    { return QColor(0xfa, 0xfa, 0xfa); }  // #fafafa - main background
inline QColor lightSecondary()  { return QColor(0xf0, 0xf0, 0xf0); }  // #f0f0f0 - hover states
inline QColor lightTertiary()   { return QColor(0xe0, 0xe0, 0xe0); }  // #e0e0e0 - borders, separators

// ============================================================================
// Convenience Functions (select by dark mode)
// ============================================================================

inline QColor background(bool dark)     { return dark ? darkPrimary() : QColor(0xfa, 0xfa, 0xfa); }
inline QColor backgroundAlt(bool dark)  { return dark ? darkSecondary() : QColor(Qt::white); }
inline QColor hover(bool dark)          { return dark ? QColor(0x4a, 0x4a, 0x4a) : lightSecondary(); }
inline QColor border(bool dark)         { return dark ? darkTertiary() : lightTertiary(); }
inline QColor separator(bool dark)      { return dark ? darkTertiary() : lightTertiary(); }

// ============================================================================
// Text Colors (matches launcher stylesheets)
// ============================================================================

inline QColor textPrimary(bool dark)    { return dark ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x33, 0x33, 0x33); }
inline QColor textSecondary(bool dark)  { return dark ? QColor(0x9e, 0x9e, 0x9e) : QColor(0x66, 0x66, 0x66); }
inline QColor textMuted(bool dark)      { return dark ? QColor(0x75, 0x75, 0x75) : QColor(0x88, 0x88, 0x88); }
inline QColor textDisabled(bool dark)   { return dark ? QColor(0x66, 0x66, 0x66) : QColor(0x99, 0x99, 0x99); }

// ============================================================================
// Selection & Accent Colors (matches launcher stylesheets)
// ============================================================================

// Selection highlight (subtle blue tint)
inline QColor selection(bool dark)      { return dark ? QColor(0x26, 0x4f, 0x78) : QColor(0xb3, 0xd4, 0xfc); }
inline QColor selectionBorder(bool dark){ return dark ? QColor(0x8a, 0xb4, 0xf8) : QColor(0x1a, 0x73, 0xe8); }

// Hover state for interactive items (cards, list items)
inline QColor itemHover(bool dark)      { return dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xe8, 0xe8, 0xe8); }
inline QColor itemBackground(bool dark) { return dark ? QColor(0x2d, 0x2d, 0x2d) : QColor(0xff, 0xff, 0xff); }

// Pressed/active state
inline QColor pressed(bool dark)        { return dark ? QColor(0x55, 0x55, 0x55) : QColor(0xd8, 0xd8, 0xd8); }

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
// Card-Specific Colors (aligned with launcher stylesheets)
// ============================================================================

// Card shadow (light mode only, use with alpha)
inline QColor cardShadow()              { return QColor(0, 0, 0, 25); }

// Thumbnail placeholder background
inline QColor thumbnailBg(bool dark)    { return dark ? QColor(0x25, 0x25, 0x25) : QColor(0xf0, 0xf0, 0xf0); }
inline QColor thumbnailPlaceholder(bool dark) { return dark ? QColor(0x4a, 0x4a, 0x4a) : QColor(0xc0, 0xc0, 0xc0); }

// Card border - matches launcher stylesheet borders
inline QColor cardBorder(bool dark)     { return dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xe0, 0xe0, 0xe0); }

// ============================================================================
// Folder Header Colors (aligned with launcher stylesheets)
// ============================================================================

inline QColor chevron(bool dark)        { return dark ? QColor(0xb0, 0xb0, 0xb0) : QColor(0x66, 0x66, 0x66); }
inline QColor folderText(bool dark)     { return dark ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x33, 0x33, 0x33); }
inline QColor folderSeparator(bool dark){ return dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xe0, 0xe0, 0xe0); }

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
