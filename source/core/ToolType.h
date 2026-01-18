#pragma once

// ============================================================================
// ToolType - Available drawing/editing tools
// ============================================================================
// Part of the new SpeedyNote document architecture
// All tools are vector-based with full undo/redo support.
// ============================================================================

/**
 * @brief Available drawing and editing tools.
 * 
 * All tools work with vector strokes stored in VectorLayer.
 * The pixmap-based tools from the old InkCanvas are obsolete.
 */
enum class ToolType {
    Pen,        ///< Vector pen - pressure-sensitive drawing with undo support
    Marker,     ///< Vector marker - semi-transparent strokes
    Eraser,     ///< Vector eraser - stroke-based removal
    Highlighter,///< Vector highlighter - highlight blend mode (Phase 2B)
    Lasso,      ///< Selection tool - select and manipulate strokes (Phase 2B)
    ObjectSelect///< Object selection tool - select and manipulate inserted objects (Phase O2)
};
