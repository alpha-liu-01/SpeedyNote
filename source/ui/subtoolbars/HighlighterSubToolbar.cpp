#include "HighlighterSubToolbar.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ToggleButton.h"  // Contains SubToolbarToggle

#include <QSettings>
#include <QColorDialog>

// Static member definitions
// Default colors (same as Marker - shared via QSettings)
const QColor HighlighterSubToolbar::DEFAULT_COLORS[NUM_PRESETS] = {
    QColor(0xFF, 0xAA, 0xAA),  // Light red/pink
    QColor(0xFF, 0xFF, 0x00),  // Yellow
    QColor(0xAA, 0xAA, 0xFF)   // Light blue
};

// Shared color settings (used by both Marker and Highlighter)
const QString HighlighterSubToolbar::SETTINGS_GROUP_SHARED_COLORS = "marker";  // Colors stored under "marker" group
const QString HighlighterSubToolbar::SETTINGS_GROUP_HIGHLIGHTER = "highlighter";
const QString HighlighterSubToolbar::KEY_COLOR_PREFIX = "color";
const QString HighlighterSubToolbar::KEY_SELECTED_COLOR = "selectedColor";
const QString HighlighterSubToolbar::KEY_AUTO_HIGHLIGHT = "autoHighlight";

HighlighterSubToolbar::HighlighterSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void HighlighterSubToolbar::createWidgets()
{
    // Create color preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i] = new ColorPresetButton(this);
        m_colorButtons[i]->setColor(DEFAULT_COLORS[i]);
        m_colorButtons[i]->setToolTip(tr("Color preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_colorButtons[i]);
    }
    
    // Add separator before toggle
    addSeparator();
    
    // Create auto-highlight toggle
    m_autoHighlightToggle = new SubToolbarToggle(this);
    m_autoHighlightToggle->setToolTip(tr("Auto-highlight mode (automatically highlight selected text)"));
    // Use icon base name for dark mode switching support
    m_autoHighlightToggle->setIconName("marker");
    m_autoHighlightToggle->setDarkMode(isDarkMode());
    addWidget(m_autoHighlightToggle);
}

void HighlighterSubToolbar::setupConnections()
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
    
    // Auto-highlight toggle connection
    connect(m_autoHighlightToggle, &SubToolbarToggle::toggled, 
            this, &HighlighterSubToolbar::onAutoHighlightToggled);
}

void HighlighterSubToolbar::loadFromSettings()
{
    QSettings settings;
    
    // Load SHARED colors (from marker group, shared with Marker)
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    settings.endGroup();
    
    // Load highlighter-specific settings
    settings.beginGroup(SETTINGS_GROUP_HIGHLIGHTER);
    
    // Load selection
    m_selectedColorIndex = settings.value(KEY_SELECTED_COLOR, 0).toInt();
    
    // Load auto-highlight state
    m_autoHighlightEnabled = settings.value(KEY_AUTO_HIGHLIGHT, false).toBool();
    
    settings.endGroup();
    
    // Apply settings
    selectColorPreset(m_selectedColorIndex);
    m_autoHighlightToggle->setChecked(m_autoHighlightEnabled);
}

void HighlighterSubToolbar::saveColorsToSettings()
{
    // Save SHARED colors (affects Marker too)
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_colorButtons[i]->color());
    }
    
    settings.endGroup();
}

void HighlighterSubToolbar::saveAutoHighlightToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_HIGHLIGHTER);
    settings.setValue(KEY_SELECTED_COLOR, m_selectedColorIndex);
    settings.setValue(KEY_AUTO_HIGHLIGHT, m_autoHighlightEnabled);
    settings.endGroup();
}

void HighlighterSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void HighlighterSubToolbar::syncSharedState()
{
    // Sync shared colors when switching to this subtoolbar
    syncSharedColorsFromSettings();
}

void HighlighterSubToolbar::syncSharedColorsFromSettings()
{
    // Reload ONLY shared colors from QSettings
    // This preserves per-tab selection state while syncing with Marker edits
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    
    settings.endGroup();
}

void HighlighterSubToolbar::restoreTabState(int tabIndex)
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
    
    // Restore selection
    selectColorPreset(state.selectedColorIndex);
    
    // NOTE: Auto-highlight state is NOT restored here.
    // DocumentViewport is the source of truth for auto-highlight state (per-viewport).
    // The toggle is synced from viewport via setAutoHighlightState() in MainWindow.
}

void HighlighterSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    
    // Save colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        state.colors[i] = m_colorButtons[i]->color();
    }
    
    // Save selection
    state.selectedColorIndex = m_selectedColorIndex;
    
    // NOTE: Auto-highlight state is NOT saved here.
    // DocumentViewport stores auto-highlight state per-viewport (per-tab).
    // We don't duplicate that state in the subtoolbar.
    state.initialized = true;
}

void HighlighterSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void HighlighterSubToolbar::onColorPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the color when clicked - the preset might show as "selected"
    // but the actual current color could be different (changed via other means)
    selectColorPreset(index);
    
    // Emit color change with marker opacity applied
    QColor colorWithOpacity = m_colorButtons[index]->color();
    colorWithOpacity.setAlpha(MARKER_OPACITY);
    emit highlighterColorChanged(colorWithOpacity);
}

void HighlighterSubToolbar::onColorEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open color dialog
    QColor currentColor = m_colorButtons[index]->color();
    QColor newColor = QColorDialog::getColor(currentColor, this, tr("Select Highlighter Color"));
    
    if (newColor.isValid() && newColor != currentColor) {
        m_colorButtons[index]->setColor(newColor);
        saveColorsToSettings();  // Persist change (SHARED with Marker)
        
        // If this is the selected preset, emit change with marker opacity
        if (m_selectedColorIndex == index) {
            QColor colorWithOpacity = newColor;
            colorWithOpacity.setAlpha(MARKER_OPACITY);
            emit highlighterColorChanged(colorWithOpacity);
        }
    }
}

void HighlighterSubToolbar::onAutoHighlightToggled(bool checked)
{
    m_autoHighlightEnabled = checked;
    saveAutoHighlightToSettings();
    emit autoHighlightChanged(checked);
}

void HighlighterSubToolbar::selectColorPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setSelected(i == index);
    }
    
    m_selectedColorIndex = index;
}

void HighlighterSubToolbar::setAutoHighlightState(bool enabled)
{
    // Update internal state
    m_autoHighlightEnabled = enabled;
    
    // Block signals to avoid feedback loop (external change shouldn't emit back)
    m_autoHighlightToggle->blockSignals(true);
    m_autoHighlightToggle->setChecked(enabled);
    m_autoHighlightToggle->blockSignals(false);
}

void HighlighterSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset values to sync with viewport
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS) {
        QColor colorWithOpacity = m_colorButtons[m_selectedColorIndex]->color();
        colorWithOpacity.setAlpha(MARKER_OPACITY);
        emit highlighterColorChanged(colorWithOpacity);
    }
    // Note: auto-highlight state is synced via setAutoHighlightState() separately
}

QColor HighlighterSubToolbar::currentColor() const
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

void HighlighterSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);

    if (m_autoHighlightToggle) {
        m_autoHighlightToggle->setDarkMode(darkMode);
    }
}

