#include "Toolbar.h"
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>
#include <QStyleOption>

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    updateTheme(false);  // Default to light mode
}

void Toolbar::setupUi()
{
    // Fixed height for toolbar
    setFixedHeight(44);
    
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);
    
    // Center the content
    mainLayout->addStretch(1);
    
    // === Tool button group (exclusive selection) ===
    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    
    // Pen (Photoshop-style: B for Brush)
    m_penButton = new ToolButton(this);
    m_penButton->setThemedIcon("pen");
    m_penButton->setToolTip(tr("Pen Tool (B)"));
    m_penButton->setChecked(true);  // Default tool
    m_toolGroup->addButton(m_penButton);
    mainLayout->addWidget(m_penButton);
    
    // Marker
    m_markerButton = new ToolButton(this);
    m_markerButton->setThemedIcon("marker");
    m_markerButton->setToolTip(tr("Marker Tool (M)"));
    m_toolGroup->addButton(m_markerButton);
    mainLayout->addWidget(m_markerButton);
    
    // Eraser
    m_eraserButton = new ToolButton(this);
    m_eraserButton->setThemedIcon("eraser");
    m_eraserButton->setToolTip(tr("Eraser Tool (E)"));
    m_toolGroup->addButton(m_eraserButton);
    mainLayout->addWidget(m_eraserButton);
    
    // Straight Line Toggle (NOT in exclusive tool group - can be combined with any tool)
    m_straightLineButton = new ToggleButton(this);
    m_straightLineButton->setThemedIcon("straightLine");
    m_straightLineButton->setToolTip(tr("Straight Line Mode (/)"));
    // Note: NOT added to m_toolGroup - this is a modifier, not a tool
    mainLayout->addWidget(m_straightLineButton);
    
    // Lasso
    m_lassoButton = new ToolButton(this);
    m_lassoButton->setThemedIcon("rope");
    m_lassoButton->setToolTip(tr("Lasso Selection Tool (L)"));
    m_toolGroup->addButton(m_lassoButton);
    mainLayout->addWidget(m_lassoButton);
    
    // Object Insert (Photoshop-style: V for Move/Select)
    m_objectInsertButton = new ToolButton(this);
    m_objectInsertButton->setThemedIcon("objectinsert");
    m_objectInsertButton->setToolTip(tr("Object Select Tool (V)"));
    m_toolGroup->addButton(m_objectInsertButton);
    mainLayout->addWidget(m_objectInsertButton);
    
    // Text Highlighter (PDF text selection)
    m_textButton = new ToolButton(this);
    m_textButton->setThemedIcon("text");
    m_textButton->setToolTip(tr("Text Highlighter Tool (T)"));
    m_toolGroup->addButton(m_textButton);
    mainLayout->addWidget(m_textButton);
    
    // === Gap before action buttons ===
    mainLayout->addSpacing(16);
    
    // === Action buttons ===
    m_undoButton = new ActionButton(this);
    m_undoButton->setThemedIcon("undo");
    m_undoButton->setToolTip(tr("Undo (Ctrl+Z)"));
    mainLayout->addWidget(m_undoButton);
    
    m_redoButton = new ActionButton(this);
    m_redoButton->setThemedIcon("redo");
    m_redoButton->setToolTip(tr("Redo (Ctrl+Shift+Z / Ctrl+Y)"));
    mainLayout->addWidget(m_redoButton);
    
    // === Gap before touch gesture ===
    mainLayout->addSpacing(8);
    
    // === Touch gesture mode ===
    m_touchGestureButton = new ThreeStateButton(this);
    m_touchGestureButton->setThemedIcon("hand");
    m_touchGestureButton->setToolTip(tr("Touch Gesture Mode\n0: Off\n1: Y-axis scroll only\n2: Full gestures"));
    mainLayout->addWidget(m_touchGestureButton);
    
    // Center the content
    mainLayout->addStretch(1);
}

void Toolbar::connectSignals()
{
    // Tool buttons - emit toolSelected() for known tools
    connect(m_penButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::Pen);
    });
    connect(m_markerButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::Marker);
    });
    connect(m_eraserButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::Eraser);
    });
    connect(m_lassoButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::Lasso);
    });
    
    // Text/Highlighter tool - now has ToolType
    connect(m_textButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::Highlighter);
    });
    
    // Straight Line toggle - emits straightLineToggled(bool)
    connect(m_straightLineButton, &ToggleButton::toggled, 
            this, &Toolbar::straightLineToggled);
    
    // Object Insert - selects ObjectSelect tool
    connect(m_objectInsertButton, &QPushButton::clicked, this, [this]() {
        emit toolSelected(ToolType::ObjectSelect);
    });
    
    // Action buttons
    connect(m_undoButton, &QPushButton::clicked,
            this, &Toolbar::undoClicked);
    connect(m_redoButton, &QPushButton::clicked,
            this, &Toolbar::redoClicked);
    
    // Touch gesture mode
    connect(m_touchGestureButton, &ThreeStateButton::stateChanged,
            this, &Toolbar::touchGestureModeChanged);
}

void Toolbar::setCurrentTool(ToolType tool)
{
    // Block signals to avoid triggering toolSelected during external sync
    m_toolGroup->blockSignals(true);
    
    switch (tool) {
        case ToolType::Pen:
            m_penButton->setChecked(true);
            break;
        case ToolType::Marker:
            m_markerButton->setChecked(true);
            break;
        case ToolType::Eraser:
            m_eraserButton->setChecked(true);
            break;
        case ToolType::Lasso:
            m_lassoButton->setChecked(true);
            break;
        case ToolType::ObjectSelect:
            // ObjectSelect maps to objectInsert button for now
            m_objectInsertButton->setChecked(true);
            break;
        case ToolType::Highlighter:
            m_textButton->setChecked(true);
            break;
    }
    
    m_toolGroup->blockSignals(false);
}

void Toolbar::setTouchGestureMode(int mode)
{
    m_touchGestureButton->setState(mode);
}

void Toolbar::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;
    
    // Toolbar uses system window color (follows KDE/system theme)
    QPalette sysPalette = QGuiApplication::palette();
    QString bgColor = sysPalette.color(QPalette::Window).name();
    
    // Use QPalette for reliable background (stylesheet can be unreliable for custom widgets)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, sysPalette.color(QPalette::Window));
    setPalette(pal);
    
    // Store border color for paintEvent (unified gray: dark #4d4d4d, light #D0D0D0)
    m_borderColor = darkMode ? QColor(0x4d, 0x4d, 0x4d) : QColor(0xD0, 0xD0, 0xD0);
    
    // Apply button styles
    ButtonStyles::applyToWidget(this, darkMode);
    
    // Update all button icons for theme
    m_penButton->setDarkMode(darkMode);
    m_markerButton->setDarkMode(darkMode);
    m_eraserButton->setDarkMode(darkMode);
    m_straightLineButton->setDarkMode(darkMode);
    m_lassoButton->setDarkMode(darkMode);
    m_objectInsertButton->setDarkMode(darkMode);
    m_textButton->setDarkMode(darkMode);
    m_undoButton->setDarkMode(darkMode);
    m_redoButton->setDarkMode(darkMode);
    m_touchGestureButton->setDarkMode(darkMode);
    
    // Trigger repaint for border
    update();
}

void Toolbar::paintEvent(QPaintEvent *event)
{
    // Let the base class handle the default painting (background via palette)
    QWidget::paintEvent(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    
    // Draw bottom border line
    painter.setPen(QPen(m_borderColor, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
    
    // Draw subtle shadow gradient below the border (2 pixel gradient)
    QColor shadowColor = m_darkMode ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 20);
    painter.setPen(QPen(shadowColor, 1));
    // The shadow will be drawn on the widgets below, but we can hint at depth
    // by drawing a slightly darker line above the border
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
    // Block signals to avoid feedback loop when called from external sync
    m_straightLineButton->blockSignals(true);
    m_straightLineButton->setChecked(enabled);
    m_straightLineButton->blockSignals(false);
}

