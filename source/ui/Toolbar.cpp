#include "Toolbar.h"
#include "widgets/ExpandableToolButton.h"
#include "subtoolbars/PenSubToolbar.h"
#include "subtoolbars/MarkerSubToolbar.h"
#include "subtoolbars/EraserSubToolbar.h"
#include "subtoolbars/HighlighterSubToolbar.h"
#include "subtoolbars/ObjectSelectSubToolbar.h"

#include <QHBoxLayout>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    updateTheme(false);
}

void Toolbar::setupUi()
{
    setFixedHeight(44);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    mainLayout->addStretch(1);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // --- Pen ---
    m_penSubToolbar = new PenSubToolbar();
    m_penExpandable = new ExpandableToolButton(this);
    m_penExpandable->setThemedIcon("pen");
    m_penExpandable->toolButton()->setToolTip(tr("Pen Tool (B)"));
    m_penExpandable->setContentWidget(m_penSubToolbar);
    m_penExpandable->toolButton()->setChecked(true);
    m_penExpandable->setExpanded(true);
    m_toolGroup->addButton(m_penExpandable->toolButton());
    mainLayout->addWidget(m_penExpandable);

    // --- Marker ---
    m_markerSubToolbar = new MarkerSubToolbar();
    m_markerExpandable = new ExpandableToolButton(this);
    m_markerExpandable->setThemedIcon("marker");
    m_markerExpandable->toolButton()->setToolTip(tr("Marker Tool (M)"));
    m_markerExpandable->setContentWidget(m_markerSubToolbar);
    m_toolGroup->addButton(m_markerExpandable->toolButton());
    mainLayout->addWidget(m_markerExpandable);

    // --- Eraser ---
    m_eraserSubToolbar = new EraserSubToolbar();
    m_eraserExpandable = new ExpandableToolButton(this);
    m_eraserExpandable->setThemedIcon("eraser");
    m_eraserExpandable->toolButton()->setToolTip(tr("Eraser Tool (E)"));
    m_eraserExpandable->setContentWidget(m_eraserSubToolbar);
    m_toolGroup->addButton(m_eraserExpandable->toolButton());
    mainLayout->addWidget(m_eraserExpandable);

    // --- Straight Line Toggle ---
    m_straightLineButton = new ToggleButton(this);
    m_straightLineButton->setThemedIcon("straightLine");
    m_straightLineButton->setToolTip(tr("Straight Line Mode (/)"));
    mainLayout->addWidget(m_straightLineButton);

    // --- Lasso (no subtoolbar) ---
    m_lassoButton = new ToolButton(this);
    m_lassoButton->setThemedIcon("rope");
    m_lassoButton->setToolTip(tr("Lasso Selection Tool (L)"));
    m_toolGroup->addButton(m_lassoButton);
    mainLayout->addWidget(m_lassoButton);

    // --- Object Select ---
    m_objectSelectSubToolbar = new ObjectSelectSubToolbar();
    m_objectInsertExpandable = new ExpandableToolButton(this);
    m_objectInsertExpandable->setThemedIcon("objectinsert");
    m_objectInsertExpandable->toolButton()->setToolTip(tr("Object Select Tool (V)"));
    m_objectInsertExpandable->setContentWidget(m_objectSelectSubToolbar);
    m_toolGroup->addButton(m_objectInsertExpandable->toolButton());
    mainLayout->addWidget(m_objectInsertExpandable);

    // --- Highlighter ---
    m_highlighterSubToolbar = new HighlighterSubToolbar();
    m_textExpandable = new ExpandableToolButton(this);
    m_textExpandable->setThemedIcon("text");
    m_textExpandable->toolButton()->setToolTip(tr("Text Highlighter Tool (T)"));
    m_textExpandable->setContentWidget(m_highlighterSubToolbar);
    m_toolGroup->addButton(m_textExpandable->toolButton());
    mainLayout->addWidget(m_textExpandable);

    mainLayout->addSpacing(16);

    // --- Undo / Redo ---
    m_undoButton = new ActionButton(this);
    m_undoButton->setThemedIcon("undo");
    m_undoButton->setToolTip(tr("Undo (Ctrl+Z)"));
    mainLayout->addWidget(m_undoButton);

    m_redoButton = new ActionButton(this);
    m_redoButton->setThemedIcon("redo");
    m_redoButton->setToolTip(tr("Redo (Ctrl+Shift+Z / Ctrl+Y)"));
    mainLayout->addWidget(m_redoButton);

    mainLayout->addSpacing(8);

    // --- Touch gesture mode ---
    m_touchGestureButton = new ThreeStateButton(this);
    m_touchGestureButton->setThemedIcon("hand");
    m_touchGestureButton->setToolTip(tr("Touch Gesture Mode\n0: Off\n1: Y-axis scroll only\n2: Full gestures"));
    mainLayout->addWidget(m_touchGestureButton);

    mainLayout->addStretch(1);

    // Wire contentSizeChanged from ObjectSelectSubToolbar to re-layout
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::contentSizeChanged, this, [this]() {
        m_objectInsertExpandable->updateGeometry();
        layout()->invalidate();
        layout()->activate();
    });
}

void Toolbar::connectSignals()
{
    connect(m_penExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Pen);
        emit toolSelected(ToolType::Pen);
    });
    connect(m_markerExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Marker);
        emit toolSelected(ToolType::Marker);
    });
    connect(m_eraserExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Eraser);
        emit toolSelected(ToolType::Eraser);
    });
    connect(m_lassoButton, &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Lasso);
        emit toolSelected(ToolType::Lasso);
    });
    connect(m_objectInsertExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::ObjectSelect);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_textExpandable->toolButton(), &QPushButton::clicked, this, [this]() {
        expandToolButton(ToolType::Highlighter);
        emit toolSelected(ToolType::Highlighter);
    });

    connect(m_straightLineButton, &ToggleButton::toggled,
            this, &Toolbar::straightLineToggled);

    connect(m_undoButton, &QPushButton::clicked,
            this, &Toolbar::undoClicked);
    connect(m_redoButton, &QPushButton::clicked,
            this, &Toolbar::redoClicked);

    connect(m_touchGestureButton, &ThreeStateButton::stateChanged,
            this, &Toolbar::touchGestureModeChanged);
}

void Toolbar::expandToolButton(ToolType tool)
{
    if (m_currentTool == tool)
        return;

    // Sync shared state when switching between Marker/Highlighter
    SubToolbar* newSub = nullptr;
    ExpandableToolButton* newExp = expandableForTool(tool);

    switch (tool) {
        case ToolType::Pen:       newSub = m_penSubToolbar; break;
        case ToolType::Marker:    newSub = m_markerSubToolbar; break;
        case ToolType::Eraser:    newSub = m_eraserSubToolbar; break;
        case ToolType::Highlighter: newSub = m_highlighterSubToolbar; break;
        case ToolType::ObjectSelect: newSub = m_objectSelectSubToolbar; break;
        default: break;
    }

    collapseAllToolButtons();

    if (newSub) {
        newSub->syncSharedState();
    }
    if (newExp) {
        newExp->setExpanded(true);
    }

    m_currentTool = tool;
}

void Toolbar::collapseAllToolButtons()
{
    m_penExpandable->setExpanded(false);
    m_markerExpandable->setExpanded(false);
    m_eraserExpandable->setExpanded(false);
    m_objectInsertExpandable->setExpanded(false);
    m_textExpandable->setExpanded(false);
}

ExpandableToolButton* Toolbar::expandableForTool(ToolType tool) const
{
    switch (tool) {
        case ToolType::Pen:          return m_penExpandable;
        case ToolType::Marker:       return m_markerExpandable;
        case ToolType::Eraser:       return m_eraserExpandable;
        case ToolType::ObjectSelect: return m_objectInsertExpandable;
        case ToolType::Highlighter:  return m_textExpandable;
        default: return nullptr;
    }
}

void Toolbar::setCurrentTool(ToolType tool)
{
    m_toolGroup->blockSignals(true);

    ExpandableToolButton* exp = expandableForTool(tool);
    if (exp) {
        exp->toolButton()->setChecked(true);
    } else if (tool == ToolType::Lasso) {
        m_lassoButton->setChecked(true);
    }

    m_toolGroup->blockSignals(false);

    expandToolButton(tool);
}

void Toolbar::setTouchGestureMode(int mode)
{
    m_touchGestureButton->setState(mode);
}

void Toolbar::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;

    QPalette sysPalette = QGuiApplication::palette();
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, sysPalette.color(QPalette::Window));
    setPalette(pal);

    m_borderColor = darkMode ? QColor(0x4d, 0x4d, 0x4d) : QColor(0xD0, 0xD0, 0xD0);

    ButtonStyles::applyToWidget(this, darkMode);

    // Update expandable tool buttons
    m_penExpandable->setDarkMode(darkMode);
    m_markerExpandable->setDarkMode(darkMode);
    m_eraserExpandable->setDarkMode(darkMode);
    m_objectInsertExpandable->setDarkMode(darkMode);
    m_textExpandable->setDarkMode(darkMode);

    // Update subtoolbars
    m_penSubToolbar->setDarkMode(darkMode);
    m_markerSubToolbar->setDarkMode(darkMode);
    m_eraserSubToolbar->setDarkMode(darkMode);
    m_highlighterSubToolbar->setDarkMode(darkMode);
    m_objectSelectSubToolbar->setDarkMode(darkMode);

    // Update plain buttons
    m_straightLineButton->setDarkMode(darkMode);
    m_lassoButton->setDarkMode(darkMode);
    m_undoButton->setDarkMode(darkMode);
    m_redoButton->setDarkMode(darkMode);
    m_touchGestureButton->setDarkMode(darkMode);

    update();
}

void Toolbar::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(QPen(m_borderColor, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    QColor innerShadow = m_darkMode ? QColor(0, 0, 0, 30) : QColor(0, 0, 0, 15);
    painter.setPen(QPen(innerShadow, 1));
    painter.drawLine(0, height() - 2, width(), height() - 2);
}

void Toolbar::setUndoEnabled(bool enabled)
{
    m_undoButton->setEnabled(enabled);
}

void Toolbar::setRedoEnabled(bool enabled)
{
    m_redoButton->setEnabled(enabled);
}

void Toolbar::setStraightLineMode(bool enabled)
{
    m_straightLineButton->blockSignals(true);
    m_straightLineButton->setChecked(enabled);
    m_straightLineButton->blockSignals(false);
}

void Toolbar::onTabChanged(int newTabIndex, int oldTabIndex)
{
    // Save state for old tab across all subtoolbars
    if (oldTabIndex >= 0) {
        m_penSubToolbar->saveTabState(oldTabIndex);
        m_markerSubToolbar->saveTabState(oldTabIndex);
        m_highlighterSubToolbar->saveTabState(oldTabIndex);
        m_eraserSubToolbar->saveTabState(oldTabIndex);
        m_objectSelectSubToolbar->saveTabState(oldTabIndex);
    }

    // Restore state for new tab across all subtoolbars
    if (newTabIndex >= 0) {
        m_penSubToolbar->restoreTabState(newTabIndex);
        m_markerSubToolbar->restoreTabState(newTabIndex);
        m_highlighterSubToolbar->restoreTabState(newTabIndex);
        m_eraserSubToolbar->restoreTabState(newTabIndex);
        m_objectSelectSubToolbar->restoreTabState(newTabIndex);
    }
}

void Toolbar::clearTabState(int tabIndex)
{
    m_penSubToolbar->clearTabState(tabIndex);
    m_markerSubToolbar->clearTabState(tabIndex);
    m_highlighterSubToolbar->clearTabState(tabIndex);
    m_eraserSubToolbar->clearTabState(tabIndex);
    m_objectSelectSubToolbar->clearTabState(tabIndex);
}
