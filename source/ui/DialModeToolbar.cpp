/**
 * @file DialModeToolbar.cpp
 * @brief Implementation of dial mode toolbar
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 * TODO MW2.2: Move dial toolbar UI from MainWindow to here
 */

#include "DialModeToolbar.h"
#include "../input/DialController.h"

#include <QDebug>

DialModeToolbar::DialModeToolbar(DialController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setupUi();
    
    // Connect to controller signals
    if (m_controller) {
        connect(m_controller, &DialController::modeChanged, 
                this, &DialModeToolbar::onModeChanged);
    }
    
    qDebug() << "DialModeToolbar: Initialized";
}

DialModeToolbar::~DialModeToolbar()
{
}

void DialModeToolbar::setupUi()
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(4);
    
    // Mode label
    m_modeLabel = new QLabel(tr("Dial Mode:"), this);
    m_layout->addWidget(m_modeLabel);
    
    // Mode combo box - using existing DialMode enum values
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Tool"), static_cast<int>(ToolSwitching));
    m_modeCombo->addItem(tr("Zoom"), static_cast<int>(ZoomControl));
    m_modeCombo->addItem(tr("Pan/Scroll"), static_cast<int>(PanAndPageScroll));
    m_modeCombo->addItem(tr("Thickness"), static_cast<int>(ThicknessControl));
    m_modeCombo->addItem(tr("Presets"), static_cast<int>(PresetSelection));
    m_modeCombo->addItem(tr("Page"), static_cast<int>(PageSwitching));
    m_layout->addWidget(m_modeCombo);
    
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        DialMode mode = static_cast<DialMode>(m_modeCombo->itemData(index).toInt());
        emit modeChangeRequested(mode);
    });
    
    // Dial state display
    m_dialDisplay = new QLabel(this);
    m_dialDisplay->setMinimumWidth(60);
    m_dialDisplay->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_dialDisplay);
    
    m_layout->addStretch();
    
    setLayout(m_layout);
}

void DialModeToolbar::createModeButtons()
{
    // TODO MW2.2: Create icon-based mode buttons as alternative to combo
}

void DialModeToolbar::setToolbarVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        setVisible(visible);
        emit visibilityChanged(visible);
    }
}

void DialModeToolbar::updateModeDisplay(DialMode mode)
{
    if (m_dialDisplay) {
        m_dialDisplay->setText(getModeName(mode));
    }
}

void DialModeToolbar::onModeChanged(DialMode mode)
{
    // Update combo box selection without triggering signal
    m_modeCombo->blockSignals(true);
    for (int i = 0; i < m_modeCombo->count(); ++i) {
        if (m_modeCombo->itemData(i).toInt() == static_cast<int>(mode)) {
            m_modeCombo->setCurrentIndex(i);
            break;
        }
    }
    m_modeCombo->blockSignals(false);
    
    updateModeDisplay(mode);
}

void DialModeToolbar::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;
    updateButtonStyles();
}

void DialModeToolbar::updateButtonStyles()
{
    // TODO MW2.2: Apply theme-aware styles
}

QString DialModeToolbar::getModeIcon(DialMode mode) const
{
    switch (mode) {
        case ToolSwitching: return "pen";
        case ZoomControl: return "zoom";
        case PanAndPageScroll: return "scroll";
        case ThicknessControl: return "thickness";
        case PresetSelection: return "preset";
        case PageSwitching: return "bookpage";
        default: return "dial";
    }
}

QString DialModeToolbar::getModeName(DialMode mode) const
{
    switch (mode) {
        case ToolSwitching: return tr("Tool");
        case ZoomControl: return tr("Zoom");
        case PanAndPageScroll: return tr("Pan");
        case ThicknessControl: return tr("Size");
        case PresetSelection: return tr("Preset");
        case PageSwitching: return tr("Page");
        default: return tr("Dial");
    }
}
