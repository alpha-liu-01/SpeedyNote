/**
 * @file DialTypes.h
 * @brief Shared type definitions for dial controller system
 * 
 * This header defines enums and types used by DialController and MainWindow.
 * Extracted to prevent circular dependencies.
 * 
 * MW2.1: Created as part of dial controller extraction.
 */

#ifndef DIALTYPES_H
#define DIALTYPES_H

/**
 * @brief Dial operation modes
 * 
 * Note: This enum matches the existing DialMode in MainWindow.h
 * to ensure compatibility during migration.
 */
enum DialMode {
    None,             // No dial mode active
    PageSwitching,    // Page navigation
    ZoomControl,      // Zoom control
    ThicknessControl, // Pen thickness
    ToolSwitching,    // Tool selection
    PresetSelection,  // Preset selection
    PanAndPageScroll  // Pan/scroll with page flip
};

#endif // DIALTYPES_H

