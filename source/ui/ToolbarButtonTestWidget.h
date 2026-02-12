#ifndef TOOLBARBUTTONTESTWIDGET_H
#define TOOLBARBUTTONTESTWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QButtonGroup>
#include <QCheckBox>
#include "ToolbarButtons.h"

/**
 * Visual test widget for toolbar buttons.
 * Displays all button types for manual verification of appearance and behavior.
 * 
 * Launch with: speedynote --test-buttons-visual
 */
class ToolbarButtonTestWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToolbarButtonTestWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle("Toolbar Button Test");
        setMinimumSize(400, 500);
        
        auto *mainLayout = new QVBoxLayout(this);
        
        // Dark mode toggle
        auto *darkModeCheck = new QCheckBox("Dark Mode", this);
        connect(darkModeCheck, &QCheckBox::toggled, this, &ToolbarButtonTestWidget::setDarkMode);
        mainLayout->addWidget(darkModeCheck);
        
        // Action Buttons
        auto *actionGroup = new QGroupBox("Action Buttons (instant action, no state)", this);
        auto *actionLayout = new QHBoxLayout(actionGroup);
        
        m_saveBtn = new ActionButton(this);
        m_saveBtn->setThemedIcon("save");
        m_saveBtn->setToolTip("Save");
        actionLayout->addWidget(m_saveBtn);
        
        m_undoBtn = new ActionButton(this);
        m_undoBtn->setThemedIcon("copy");  // Using copy as placeholder
        m_undoBtn->setToolTip("Undo");
        actionLayout->addWidget(m_undoBtn);
        
        m_menuBtn = new ActionButton(this);
        m_menuBtn->setThemedIcon("menu");
        m_menuBtn->setToolTip("Menu");
        actionLayout->addWidget(m_menuBtn);
        
        actionLayout->addStretch();
        mainLayout->addWidget(actionGroup);
        
        // Status label for action buttons
        m_actionStatus = new QLabel("Click an action button...", this);
        mainLayout->addWidget(m_actionStatus);
        
        connect(m_saveBtn, &QPushButton::clicked, [this]() {
            m_actionStatus->setText("Save clicked!");
        });
        connect(m_undoBtn, &QPushButton::clicked, [this]() {
            m_actionStatus->setText("Undo clicked!");
        });
        connect(m_menuBtn, &QPushButton::clicked, [this]() {
            m_actionStatus->setText("Menu clicked!");
        });
        
        // Toggle Buttons
        auto *toggleGroup = new QGroupBox("Toggle Buttons (on/off state)", this);
        auto *toggleLayout = new QHBoxLayout(toggleGroup);
        
        m_bookmarkBtn = new ToggleButton(this);
        m_bookmarkBtn->setThemedIcon("bookmark");
        m_bookmarkBtn->setToolTip("Bookmarks");
        toggleLayout->addWidget(m_bookmarkBtn);
        
        m_outlineBtn = new ToggleButton(this);
        m_outlineBtn->setThemedIcon("outline");
        m_outlineBtn->setToolTip("Outline");
        toggleLayout->addWidget(m_outlineBtn);
        
        m_layerBtn = new ToggleButton(this);
        m_layerBtn->setThemedIcon("layer");
        m_layerBtn->setToolTip("Layers");
        toggleLayout->addWidget(m_layerBtn);
        
        toggleLayout->addStretch();
        mainLayout->addWidget(toggleGroup);
        
        // Status label for toggle buttons
        m_toggleStatus = new QLabel("Toggle state: none checked", this);
        mainLayout->addWidget(m_toggleStatus);
        
        auto updateToggleStatus = [this]() {
            QStringList checked;
            if (m_bookmarkBtn->isChecked()) checked << "Bookmarks";
            if (m_outlineBtn->isChecked()) checked << "Outline";
            if (m_layerBtn->isChecked()) checked << "Layers";
            m_toggleStatus->setText("Toggle state: " + 
                (checked.isEmpty() ? "none checked" : checked.join(", ")));
        };
        connect(m_bookmarkBtn, &QPushButton::toggled, updateToggleStatus);
        connect(m_outlineBtn, &QPushButton::toggled, updateToggleStatus);
        connect(m_layerBtn, &QPushButton::toggled, updateToggleStatus);
        
        // Three-State Button
        auto *threeStateGroup = new QGroupBox("Three-State Button (cycles 0→1→2)", this);
        auto *threeStateLayout = new QHBoxLayout(threeStateGroup);
        
        m_touchGestureBtn = new ThreeStateButton(this);
        m_touchGestureBtn->setThemedIcon("hand");
        m_touchGestureBtn->setToolTip("Touch Gestures (Off/Y-Axis/Full)");
        threeStateLayout->addWidget(m_touchGestureBtn);
        
        threeStateLayout->addStretch();
        mainLayout->addWidget(threeStateGroup);
        
        m_threeStateStatus = new QLabel("State: 0 (Off)", this);
        mainLayout->addWidget(m_threeStateStatus);
        
        connect(m_touchGestureBtn, &ThreeStateButton::stateChanged, [this](int state) {
            QString stateName;
            switch (state) {
                case 0: stateName = "Off"; break;
                case 1: stateName = "Y-Axis Only (red)"; break;
                case 2: stateName = "Full"; break;
            }
            m_threeStateStatus->setText(QString("State: %1 (%2)").arg(state).arg(stateName));
        });
        
        // Tool Buttons (exclusive selection)
        auto *toolGroup = new QGroupBox("Tool Buttons (exclusive selection)", this);
        auto *toolLayout = new QHBoxLayout(toolGroup);
        
        m_toolGroup = new QButtonGroup(this);
        m_toolGroup->setExclusive(true);
        
        m_penBtn = new ToolButton(this);
        m_penBtn->setThemedIcon("pen");
        m_penBtn->setToolTip("Pen");
        m_toolGroup->addButton(m_penBtn, 0);
        toolLayout->addWidget(m_penBtn);
        
        m_markerBtn = new ToolButton(this);
        m_markerBtn->setThemedIcon("marker");
        m_markerBtn->setToolTip("Marker");
        m_toolGroup->addButton(m_markerBtn, 1);
        toolLayout->addWidget(m_markerBtn);
        
        m_eraserBtn = new ToolButton(this);
        m_eraserBtn->setThemedIcon("eraser");
        m_eraserBtn->setToolTip("Eraser");
        m_toolGroup->addButton(m_eraserBtn, 2);
        toolLayout->addWidget(m_eraserBtn);
        
        m_lassoBtn = new ToolButton(this);
        m_lassoBtn->setThemedIcon("rope");
        m_lassoBtn->setToolTip("Lasso");
        m_toolGroup->addButton(m_lassoBtn, 3);
        toolLayout->addWidget(m_lassoBtn);
        
        toolLayout->addStretch();
        mainLayout->addWidget(toolGroup);
        
        m_toolStatus = new QLabel("Selected tool: none", this);
        mainLayout->addWidget(m_toolStatus);
        
        connect(m_toolGroup, &QButtonGroup::idClicked, [this](int id) {
            QString toolName;
            switch (id) {
                case 0: toolName = "Pen"; break;
                case 1: toolName = "Marker"; break;
                case 2: toolName = "Eraser"; break;
                case 3: toolName = "Lasso"; break;
            }
            m_toolStatus->setText("Selected tool: " + toolName);
        });
        
        mainLayout->addStretch();
        
        // Apply initial light mode styles
        setDarkMode(false);
    }

public slots:
    void setDarkMode(bool dark) {
        m_darkMode = dark;
        
        // Update background color
        if (dark) {
            setStyleSheet("ToolbarButtonTestWidget { background-color: #2d2d2d; color: white; }");
        } else {
            setStyleSheet("ToolbarButtonTestWidget { background-color: #f0f0f0; color: black; }");
        }
        
        // Apply button styles
        ButtonStyles::applyToWidget(this, dark);
        
        // Update all button icons
        m_saveBtn->setDarkMode(dark);
        m_undoBtn->setDarkMode(dark);
        m_menuBtn->setDarkMode(dark);
        m_bookmarkBtn->setDarkMode(dark);
        m_outlineBtn->setDarkMode(dark);
        m_layerBtn->setDarkMode(dark);
        m_touchGestureBtn->setDarkMode(dark);
        m_penBtn->setDarkMode(dark);
        m_markerBtn->setDarkMode(dark);
        m_eraserBtn->setDarkMode(dark);
        m_lassoBtn->setDarkMode(dark);
    }

private:
    bool m_darkMode = false;
    
    // Action buttons
    ActionButton *m_saveBtn;
    ActionButton *m_undoBtn;
    ActionButton *m_menuBtn;
    QLabel *m_actionStatus;
    
    // Toggle buttons
    ToggleButton *m_bookmarkBtn;
    ToggleButton *m_outlineBtn;
    ToggleButton *m_layerBtn;
    QLabel *m_toggleStatus;
    
    // Three-state button
    ThreeStateButton *m_touchGestureBtn;
    QLabel *m_threeStateStatus;
    
    // Tool buttons
    QButtonGroup *m_toolGroup;
    ToolButton *m_penBtn;
    ToolButton *m_markerBtn;
    ToolButton *m_eraserBtn;
    ToolButton *m_lassoBtn;
    QLabel *m_toolStatus;
};

#endif // TOOLBARBUTTONTESTWIDGET_H

