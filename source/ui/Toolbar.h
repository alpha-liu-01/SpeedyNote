#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QWidget>
#include <QButtonGroup>
#include <QColor>
#include "ToolbarButtons.h"
#include "../core/ToolType.h"

class QPaintEvent;
class ExpandableToolButton;
class PenSubToolbar;
class MarkerSubToolbar;
class EraserSubToolbar;
class HighlighterSubToolbar;
class ObjectSelectSubToolbar;

/**
 * Toolbar - Tab-specific tool selection and actions with inline subtoolbars.
 *
 * Layout (center-aligned):
 * [Pen(+presets)][Marker(+presets)][Eraser(+presets)][Shape][Lasso][ObjInsert(+presets)][Text(+presets)]  gap  [Undo][Redo] [Touch]
 *
 * When a tool is selected its ExpandableToolButton expands to reveal
 * inline preset buttons (colors, thicknesses, toggles).
 */
class Toolbar : public QWidget {
    Q_OBJECT

public:
    explicit Toolbar(QWidget *parent = nullptr);

    void setCurrentTool(ToolType tool);
    void setTouchGestureMode(int mode);
    void updateTheme(bool darkMode);
    void setUndoEnabled(bool enabled);
    void setRedoEnabled(bool enabled);
    void setStraightLineMode(bool enabled);

    // Per-tab state management (replaces SubToolbarContainer duties)
    void onTabChanged(int newTabIndex, int oldTabIndex);
    void clearTabState(int tabIndex);

    // Subtoolbar accessors for MainWindow signal wiring
    PenSubToolbar* penSubToolbar() const { return m_penSubToolbar; }
    MarkerSubToolbar* markerSubToolbar() const { return m_markerSubToolbar; }
    EraserSubToolbar* eraserSubToolbar() const { return m_eraserSubToolbar; }
    HighlighterSubToolbar* highlighterSubToolbar() const { return m_highlighterSubToolbar; }
    ObjectSelectSubToolbar* objectSelectSubToolbar() const { return m_objectSelectSubToolbar; }

signals:
    void toolSelected(ToolType tool);
    void straightLineToggled(bool enabled);
    void objectInsertClicked();
    void textClicked();
    void undoClicked();
    void redoClicked();
    void touchGestureModeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUi();
    void connectSignals();
    void expandToolButton(ToolType tool);
    void collapseAllToolButtons();

    ExpandableToolButton* expandableForTool(ToolType tool) const;

    // Expandable tool buttons (own subtoolbar content)
    ExpandableToolButton *m_penExpandable;
    ExpandableToolButton *m_markerExpandable;
    ExpandableToolButton *m_eraserExpandable;
    ExpandableToolButton *m_objectInsertExpandable;
    ExpandableToolButton *m_textExpandable;

    // Plain tool button (no subtoolbar)
    ToolButton *m_lassoButton;

    // Non-exclusive toggle
    ToggleButton *m_straightLineButton;

    // Action buttons
    ActionButton *m_undoButton;
    ActionButton *m_redoButton;

    // Tab-specific mode
    ThreeStateButton *m_touchGestureButton;

    // Tool group for exclusive selection
    QButtonGroup *m_toolGroup;

    // Subtoolbar instances (owned by this Toolbar, embedded in ExpandableToolButtons)
    PenSubToolbar *m_penSubToolbar;
    MarkerSubToolbar *m_markerSubToolbar;
    EraserSubToolbar *m_eraserSubToolbar;
    HighlighterSubToolbar *m_highlighterSubToolbar;
    ObjectSelectSubToolbar *m_objectSelectSubToolbar;

    // State
    bool m_darkMode = false;
    QColor m_borderColor;
    ToolType m_currentTool = ToolType::Pen;
};

#endif // TOOLBAR_H
