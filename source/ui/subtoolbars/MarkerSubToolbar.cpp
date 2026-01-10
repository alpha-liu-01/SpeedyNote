#include "MarkerSubToolbar.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QSettings>
#include <QColorDialog>

// Static member definitions
// Default marker colors (semi-transparent highlight colors)
const QColor MarkerSubToolbar::DEFAULT_COLORS[NUM_PRESETS] = {
    QColor(0xFF, 0xAA, 0xAA),  // Light red/pink
    QColor(0xFF, 0xFF, 0x00),  // Yellow
    QColor(0xAA, 0xAA, 0xFF)   // Light blue
};

// Marker-specific settings group
const QString MarkerSubToolbar::SETTINGS_GROUP_MARKER = "marker";
// Shared color settings (used by both Marker and Highlighter)
const QString MarkerSubToolbar::SETTINGS_GROUP_SHARED_COLORS = "marker";  // Colors stored under "marker" group
const QString MarkerSubToolbar::KEY_COLOR_PREFIX = "color";
const QString MarkerSubToolbar::KEY_THICKNESS_PREFIX = "thickness";
const QString MarkerSubToolbar::KEY_SELECTED_COLOR = "selectedColor";
const QString MarkerSubToolbar::KEY_SELECTED_THICKNESS = "selectedThickness";

MarkerSubToolbar::MarkerSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void MarkerSubToolbar::createWidgets()
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

void MarkerSubToolbar::setupConnections()
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

void MarkerSubToolbar::loadFromSettings()
{
    QSettings settings;
    
    // Load SHARED colors (from marker group, shared with Highlighter)
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    settings.endGroup();
    
    // Load marker-specific thicknesses and selections
    settings.beginGroup(SETTINGS_GROUP_MARKER);
    
    // Load thicknesses
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_THICKNESS_PREFIX + QString::number(i + 1);
        qreal thickness = settings.value(key, DEFAULT_THICKNESSES[i]).toReal();
        m_thicknessButtons[i]->setThickness(thickness);
    }
    
    // Load selections
    m_selectedColorIndex = settings.value(KEY_SELECTED_COLOR, 0).toInt();
    m_selectedThicknessIndex = settings.value(KEY_SELECTED_THICKNESS, 0).toInt();
    
    settings.endGroup();
    
    // Apply selections
    selectColorPreset(m_selectedColorIndex);
    selectThicknessPreset(m_selectedThicknessIndex);
}

void MarkerSubToolbar::saveColorsToSettings()
{
    // Save SHARED colors (affects Highlighter too)
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_colorButtons[i]->color());
    }
    
    settings.endGroup();
}

void MarkerSubToolbar::saveThicknessesToSettings()
{
    // Save marker-specific thicknesses and selections
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_MARKER);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_THICKNESS_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_thicknessButtons[i]->thickness());
    }
    
    settings.setValue(KEY_SELECTED_COLOR, m_selectedColorIndex);
    settings.setValue(KEY_SELECTED_THICKNESS, m_selectedThicknessIndex);
    
    settings.endGroup();
}

void MarkerSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void MarkerSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset values to sync with viewport
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS) {
        QColor colorWithOpacity = m_colorButtons[m_selectedColorIndex]->color();
        colorWithOpacity.setAlpha(MARKER_OPACITY);
        emit markerColorChanged(colorWithOpacity);
    }
    if (m_selectedThicknessIndex >= 0 && m_selectedThicknessIndex < NUM_PRESETS) {
        emit markerThicknessChanged(m_thicknessButtons[m_selectedThicknessIndex]->thickness());
    }
}

QColor MarkerSubToolbar::currentColor() const
{
    QColor color;
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS && m_colorButtons[m_selectedColorIndex]) {
        color = m_colorButtons[m_selectedColorIndex]->color();
    } else {
        color = DEFAULT_COLORS[0];  // Fallback to first default
    }
    color.setAlpha(MARKER_OPACITY);
    return color;
}

qreal MarkerSubToolbar::currentThickness() const
{
    if (m_selectedThicknessIndex >= 0 && m_selectedThicknessIndex < NUM_PRESETS && m_thicknessButtons[m_selectedThicknessIndex]) {
        return m_thicknessButtons[m_selectedThicknessIndex]->thickness();
    }
    return DEFAULT_THICKNESSES[0];  // Fallback to first default
}

void MarkerSubToolbar::restoreTabState(int tabIndex)
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

void MarkerSubToolbar::saveTabState(int tabIndex)
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

void MarkerSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void MarkerSubToolbar::onColorPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the color when clicked - the preset might show as "selected"
    // but the actual current color could be different (changed via other means)
    selectColorPreset(index);
    
    // Emit color change with marker opacity applied
    QColor colorWithOpacity = m_colorButtons[index]->color();
    colorWithOpacity.setAlpha(MARKER_OPACITY);
    emit markerColorChanged(colorWithOpacity);
}

void MarkerSubToolbar::onColorEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open color dialog
    QColor currentColor = m_colorButtons[index]->color();
    QColor newColor = QColorDialog::getColor(currentColor, this, tr("Select Marker Color"));
    
    if (newColor.isValid() && newColor != currentColor) {
        m_colorButtons[index]->setColor(newColor);
        saveColorsToSettings();  // Persist change (SHARED with Highlighter)
        
        // If this is the selected preset, emit change with marker opacity
        if (m_selectedColorIndex == index) {
            QColor colorWithOpacity = newColor;
            colorWithOpacity.setAlpha(MARKER_OPACITY);
            emit markerColorChanged(colorWithOpacity);
        }
        
        // Update thickness preview colors
        updateThicknessPreviewColors();
    }
}

void MarkerSubToolbar::onThicknessPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the thickness when clicked
    selectThicknessPreset(index);
    
    // Emit thickness change
    emit markerThicknessChanged(m_thicknessButtons[index]->thickness());
}

void MarkerSubToolbar::onThicknessEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open thickness edit dialog
    ThicknessEditDialog dialog(m_thicknessButtons[index]->thickness(), 0.5, 50.0, this);
    dialog.setWindowTitle(tr("Edit Marker Thickness"));
    
    if (dialog.exec() == QDialog::Accepted) {
        qreal newThickness = dialog.thickness();
        m_thicknessButtons[index]->setThickness(newThickness);
        saveThicknessesToSettings();  // Persist change (marker-specific)
        
        // If this is the selected preset, emit change
        if (m_selectedThicknessIndex == index) {
            emit markerThicknessChanged(newThickness);
        }
    }
}

void MarkerSubToolbar::selectColorPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setSelected(i == index);
    }
    
    m_selectedColorIndex = index;
    
    // Update thickness preview colors to match new color
    updateThicknessPreviewColors();
}

void MarkerSubToolbar::selectThicknessPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setSelected(i == index);
    }
    
    m_selectedThicknessIndex = index;
}

void MarkerSubToolbar::updateThicknessPreviewColors()
{
    // Set thickness preview line color to match selected marker color
    // Apply MARKER_OPACITY so the preview shows actual marker appearance
    // If no color is selected (index = -1), use the first preset's color as fallback
    int colorIndex = (m_selectedColorIndex >= 0) ? m_selectedColorIndex : 0;
    QColor previewColor = m_colorButtons[colorIndex]->color();
    previewColor.setAlpha(MARKER_OPACITY);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setLineColor(previewColor);
    }
}

