#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QWidget>
#include <QButtonGroup>
#include "ToolbarButtons.h"
#include "../core/ToolType.h"

/**
 * Toolbar - Tab-specific tool selection and actions.
 * 
 * Layout (center-aligned):
 * ┌────────────────────────────────────────────────────────────────────────────────┐
 * │      [Pen][Marker][Eraser][Shape][Lasso][ObjInsert][Text]  gap  [Undo][Redo] [Touch] │
 * └────────────────────────────────────────────────────────────────────────────────┘
 *        [------------ Tool Buttons (exclusive) ------------]      [Actions] [Mode]
 * 
 * Tool buttons are exclusive (only one active at a time).
 * Undo/Redo are instant actions.
 * Touch Gesture is a 3-state toggle.
 */
class Toolbar : public QWidget {
    Q_OBJECT

public:
    explicit Toolbar(QWidget *parent = nullptr);
    
    /**
     * Set the currently active tool (for external sync).
     * @param tool The tool type to select
     */
    void setCurrentTool(ToolType tool);
    
    /**
     * Set touch gesture mode (for external sync).
     * @param mode 0 = off, 1 = y-axis only, 2 = full
     */
    void setTouchGestureMode(int mode);
    
    /**
     * Update theme colors.
     * @param darkMode True for dark theme icons
     */
    void updateTheme(bool darkMode);
    
    /**
     * Enable/disable undo button.
     */
    void setUndoEnabled(bool enabled);
    
    /**
     * Enable/disable redo button.
     */
    void setRedoEnabled(bool enabled);
    
    /**
     * Set straight line mode (for external sync, e.g., from keyboard shortcut).
     * @param enabled True to enable straight line mode
     */
    void setStraightLineMode(bool enabled);

signals:
    // Tool selection
    void toolSelected(ToolType tool);
    void straightLineToggled(bool enabled);  // Straight line mode toggle
    void objectInsertClicked();  // Object insert tool
    void textClicked();          // Text tool
    
    // Actions
    void undoClicked();
    void redoClicked();
    
    // Mode
    void touchGestureModeChanged(int mode);

private:
    void setupUi();
    void connectSignals();
    
    // Tool buttons (exclusive selection via QButtonGroup)
    QButtonGroup *m_toolGroup;
    ToolButton *m_penButton;
    ToolButton *m_markerButton;
    ToolButton *m_eraserButton;
    ToolButton *m_lassoButton;
    ToolButton *m_objectInsertButton;
    ToolButton *m_textButton;
    
    // Non-exclusive toggle (not part of tool group)
    ToggleButton *m_straightLineButton;
    
    // Action buttons
    ActionButton *m_undoButton;
    ActionButton *m_redoButton;
    
    // Tab-specific mode
    ThreeStateButton *m_touchGestureButton;
    
    // State
    bool m_darkMode = false;
};

#endif // TOOLBAR_H

