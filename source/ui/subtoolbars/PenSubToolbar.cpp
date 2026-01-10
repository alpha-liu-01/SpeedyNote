#include "PenSubToolbar.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QSettings>
#include <QColorDialog>

// Static member definitions
const QColor PenSubToolbar::DEFAULT_COLORS[NUM_PRESETS] = {
    QColor(0xFF, 0x00, 0x00),  // Red
    QColor(0x00, 0x00, 0xFF),  // Blue
    QColor(0x00, 0x00, 0x00)   // Black
};

const QString PenSubToolbar::SETTINGS_GROUP = "pen";
const QString PenSubToolbar::KEY_COLOR_PREFIX = "color";
const QString PenSubToolbar::KEY_THICKNESS_PREFIX = "thickness";
const QString PenSubToolbar::KEY_SELECTED_COLOR = "selectedColor";
const QString PenSubToolbar::KEY_SELECTED_THICKNESS = "selectedThickness";

PenSubToolbar::PenSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void PenSubToolbar::createWidgets()
{
    // Create color preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i] = new ColorPresetButton(this);
        m_colorButtons[i]->setColor(DEFAULT_COLORS[i]);
        m_colorButtons[i]->setToolTip(tr("Color preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_colorButtons[i]);
    }
    
    // Add separator between color and thickness
    addSeparator();
    
    // Create thickness preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i] = new ThicknessPresetButton(this);
        m_thicknessButtons[i]->setThickness(DEFAULT_THICKNESSES[i]);
        m_thicknessButtons[i]->setToolTip(tr("Thickness preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_thicknessButtons[i]);
    }
    
    // Update thickness preview colors to match selected color
    updateThicknessPreviewColors();
}

void PenSubToolbar::setupConnections()
{
    // Color button connections
    for (int i = 0; i < NUM_PRESETS; ++i) {
        connect(m_colorButtons[i], &ColorPresetButton::clicked, this, [this, i]() {
            onColorPresetClicked(i);
        });
        connect(m_colorButtons[i], &ColorPresetButton::editRequested, this, [this, i]() {
            onColorEditRequested(i);
        });
    }
    
    // Thickness button connections
    for (int i = 0; i < NUM_PRESETS; ++i) {
        connect(m_thicknessButtons[i], &ThicknessPresetButton::clicked, this, [this, i]() {
            onThicknessPresetClicked(i);
        });
        connect(m_thicknessButtons[i], &ThicknessPresetButton::editRequested, this, [this, i]() {
            onThicknessEditRequested(i);
        });
    }
}

void PenSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    // Load colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    
    // Load thicknesses
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_THICKNESS_PREFIX + QString::number(i + 1);
        qreal thickness = settings.value(key, DEFAULT_THICKNESSES[i]).toReal();
        m_thicknessButtons[i]->setThickness(thickness);
    }
    
    // Load selections
    m_selectedColorIndex = settings.value(KEY_SELECTED_COLOR, 2).toInt();  // Default: black
    m_selectedThicknessIndex = settings.value(KEY_SELECTED_THICKNESS, 0).toInt();  // Default: thin
    
    settings.endGroup();
    
    // Apply selections
    selectColorPreset(m_selectedColorIndex);
    selectThicknessPreset(m_selectedThicknessIndex);
}

void PenSubToolbar::saveToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    // Save colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_colorButtons[i]->color());
    }
    
    // Save thicknesses
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_THICKNESS_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_thicknessButtons[i]->thickness());
    }
    
    // Save selections
    settings.setValue(KEY_SELECTED_COLOR, m_selectedColorIndex);
    settings.setValue(KEY_SELECTED_THICKNESS, m_selectedThicknessIndex);
    
    settings.endGroup();
}

void PenSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void PenSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset values to sync with viewport
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS) {
        emit penColorChanged(m_colorButtons[m_selectedColorIndex]->color());
    }
    if (m_selectedThicknessIndex >= 0 && m_selectedThicknessIndex < NUM_PRESETS) {
        emit penThicknessChanged(m_thicknessButtons[m_selectedThicknessIndex]->thickness());
    }
}

QColor PenSubToolbar::currentColor() const
{
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS && m_colorButtons[m_selectedColorIndex]) {
        return m_colorButtons[m_selectedColorIndex]->color();
    }
    return DEFAULT_COLORS[0];  // Fallback to first default
}

qreal PenSubToolbar::currentThickness() const
{
    if (m_selectedThicknessIndex >= 0 && m_selectedThicknessIndex < NUM_PRESETS && m_thicknessButtons[m_selectedThicknessIndex]) {
        return m_thicknessButtons[m_selectedThicknessIndex]->thickness();
    }
    return DEFAULT_THICKNESSES[0];  // Fallback to first default
}

void PenSubToolbar::restoreTabState(int tabIndex)
{
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Restore colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setColor(state.colors[i]);
    }
    
    // Restore thicknesses
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setThickness(state.thicknesses[i]);
    }
    
    // Restore selections
    selectColorPreset(state.selectedColorIndex);
    selectThicknessPreset(state.selectedThicknessIndex);
}

void PenSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    
    // Save colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        state.colors[i] = m_colorButtons[i]->color();
    }
    
    // Save thicknesses
    for (int i = 0; i < NUM_PRESETS; ++i) {
        state.thicknesses[i] = m_thicknessButtons[i]->thickness();
    }
    
    // Save selections
    state.selectedColorIndex = m_selectedColorIndex;
    state.selectedThicknessIndex = m_selectedThicknessIndex;
    state.initialized = true;
}

void PenSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void PenSubToolbar::onColorPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the color when clicked - the preset might show as "selected"
    // but the actual current color could be different (changed via other means)
    selectColorPreset(index);
    
    // Emit color change
    emit penColorChanged(m_colorButtons[index]->color());
}

void PenSubToolbar::onColorEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open color dialog
    QColor currentColor = m_colorButtons[index]->color();
    QColor newColor = QColorDialog::getColor(currentColor, this, tr("Select Pen Color"));
    
    if (newColor.isValid() && newColor != currentColor) {
        m_colorButtons[index]->setColor(newColor);
        saveToSettings();  // Persist change
        
        // If this is the selected preset, emit change
        if (m_selectedColorIndex == index) {
            emit penColorChanged(newColor);
        }
        
        // Update thickness preview colors
        updateThicknessPreviewColors();
    }
}

void PenSubToolbar::onThicknessPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the thickness when clicked
    selectThicknessPreset(index);
    
    // Emit thickness change
    emit penThicknessChanged(m_thicknessButtons[index]->thickness());
}

void PenSubToolbar::onThicknessEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open thickness edit dialog
    ThicknessEditDialog dialog(m_thicknessButtons[index]->thickness(), 0.5, 50.0, this);
    dialog.setWindowTitle(tr("Edit Pen Thickness"));
    
    if (dialog.exec() == QDialog::Accepted) {
        qreal newThickness = dialog.thickness();
        m_thicknessButtons[index]->setThickness(newThickness);
        saveToSettings();  // Persist change
        
        // If this is the selected preset, emit change
        if (m_selectedThicknessIndex == index) {
            emit penThicknessChanged(newThickness);
        }
    }
}

void PenSubToolbar::selectColorPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setSelected(i == index);
    }
    
    m_selectedColorIndex = index;
    
    // Update thickness preview colors to match selected color (if any)
    updateThicknessPreviewColors();
}

void PenSubToolbar::selectThicknessPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setSelected(i == index);
    }
    
    m_selectedThicknessIndex = index;
}

void PenSubToolbar::updateThicknessPreviewColors()
{
    // Set thickness preview line color to match selected pen color
    // If no color is selected (index = -1), use the first preset's color as fallback
    int colorIndex = (m_selectedColorIndex >= 0) ? m_selectedColorIndex : 0;
    QColor previewColor = m_colorButtons[colorIndex]->color();
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setLineColor(previewColor);
    }
}

