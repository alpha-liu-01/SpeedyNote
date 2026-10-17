#include "EraserSubToolbar.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QSettings>

// Static member definitions
const QColor EraserSubToolbar::PREVIEW_COLOR = QColor(0x80, 0x80, 0x80);  // Gray

const QString EraserSubToolbar::SETTINGS_GROUP = "eraser";
const QString EraserSubToolbar::KEY_SIZE_PREFIX = "size";
const QString EraserSubToolbar::KEY_SELECTED_SIZE = "selectedSize";
const QString EraserSubToolbar::KEY_ERASER_MODE = "eraserMode";
const QString EraserSubToolbar::KEY_MODE_VERSION = "eraserModeVersion";

EraserSubToolbar::EraserSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void EraserSubToolbar::createWidgets()
{
    bool dark = isDarkMode();

    // Mode toggle (Whole / Partial / Lasso) as the first widget.
    // The display order matches DocumentViewport::EraserMode, so the toggle's
    // mode index is used directly as the enum value.
    m_modeToggle = new ModeToggleButton(this);
    m_modeToggle->setModeIconNames(QStringList{"eraser", "eraser_partial", "rope"});
    m_modeToggle->setDarkMode(dark);
    m_modeToggle->setModeToolTips(QStringList{
        tr("Whole-stroke eraser (click for partial erase)"),
        tr("Partial eraser - erase only what you touch (click for lasso)"),
        tr("Lasso eraser (click to switch to whole-stroke)")
    });
    addWidget(m_modeToggle);

    addSeparator();

    // Create size preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i] = new ThicknessPresetButton(this);
        m_sizeButtons[i]->setThickness(DEFAULT_SIZES[i]);
        m_sizeButtons[i]->setLineColor(PREVIEW_COLOR);
        m_sizeButtons[i]->setToolTip(tr("Eraser size preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_sizeButtons[i]);
    }
}

void EraserSubToolbar::setupConnections()
{
    // Mode toggle connection
    connect(m_modeToggle, &ModeToggleButton::modeChanged, this, [this](int mode) {
        m_eraserModeIndex = mode;
        saveToSettings();
        emit eraserModeChanged(mode);
    });

    // Size button connections
    for (int i = 0; i < NUM_PRESETS; ++i) {
        connect(m_sizeButtons[i], &ThicknessPresetButton::clicked, this, [this, i]() {
            onSizePresetClicked(i);
        });
        connect(m_sizeButtons[i], &ThicknessPresetButton::editRequested, this, [this, i]() {
            onSizeEditRequested(i);
        });
    }
}

void EraserSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    // Load sizes
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
        qreal size = settings.value(key, DEFAULT_SIZES[i]).toReal();
        // Clamp to valid range to handle corrupted settings
        size = qBound(MIN_SIZE, size, MAX_SIZE);
        m_sizeButtons[i]->setThickness(size);
    }
    
    // Load selection with bounds checking
    int loadedIndex = settings.value(KEY_SELECTED_SIZE, 1).toInt();  // Default: medium (index 1)
    // Clamp to valid range [0, NUM_PRESETS-1] to handle corrupted settings
    m_selectedSizeIndex = qBound(0, loadedIndex, NUM_PRESETS - 1);

    // Load eraser mode. Display order is now Whole(0) / Partial(1) / Lasso(2),
    // while the pre-partial builds stored Lasso as 1. KEY_MODE_VERSION records
    // which scheme a value came from so an old "1" is not silently reinterpreted
    // as Partial; without the marker the saved mode is reset to Whole.
    const int storedMode = settings.value(KEY_ERASER_MODE, 0).toInt();
    const int modeVersion = settings.value(KEY_MODE_VERSION, 1).toInt();
    if (modeVersion < 2) {
        m_eraserModeIndex = (storedMode == 1) ? static_cast<int>(DocumentViewport::EraserMode::Lasso)
                                             : static_cast<int>(DocumentViewport::EraserMode::Normal);
    } else {
        m_eraserModeIndex = qBound(0, storedMode, NUM_MODES - 1);
    }
    
    settings.endGroup();
    
    // Apply selection
    selectSizePreset(m_selectedSizeIndex);

    // Apply mode toggle state
    m_modeToggle->blockSignals(true);
    m_modeToggle->setCurrentMode(m_eraserModeIndex);
    m_modeToggle->blockSignals(false);
}

void EraserSubToolbar::saveToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    // Save sizes
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_sizeButtons[i]->thickness());
    }
    
    // Save selection
    settings.setValue(KEY_SELECTED_SIZE, m_selectedSizeIndex);

    // Save eraser mode, stamped with the scheme version that produced it.
    settings.setValue(KEY_ERASER_MODE, m_eraserModeIndex);
    settings.setValue(KEY_MODE_VERSION, 2);
    
    settings.endGroup();
}

void EraserSubToolbar::saveSelectionToSettings()
{
    // Write ONLY the selected-size index under the eraser group. Cheap
    // enough to call on every preset click; avoids the heavier
    // saveToSettings() path (which re-writes every size and the mode).
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(KEY_SELECTED_SIZE, m_selectedSizeIndex);
    settings.endGroup();
}

void EraserSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void EraserSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset value to sync with viewport
    if (m_selectedSizeIndex >= 0 && m_selectedSizeIndex < NUM_PRESETS) {
        emit eraserSizeChanged(m_sizeButtons[m_selectedSizeIndex]->thickness());
    }
    emit eraserModeChanged(m_eraserModeIndex);
}

int EraserSubToolbar::currentModeIndex() const
{
    return m_eraserModeIndex;
}

void EraserSubToolbar::setModeState(int mode)
{
    m_eraserModeIndex = qBound(0, mode, NUM_MODES - 1);
    m_modeToggle->blockSignals(true);
    m_modeToggle->setCurrentMode(m_eraserModeIndex);
    m_modeToggle->blockSignals(false);
}

qreal EraserSubToolbar::currentSize() const
{
    if (m_selectedSizeIndex >= 0 && m_selectedSizeIndex < NUM_PRESETS && m_sizeButtons[m_selectedSizeIndex]) {
        return m_sizeButtons[m_selectedSizeIndex]->thickness();
    }
    return DEFAULT_SIZES[1];  // Fallback to medium default
}

void EraserSubToolbar::restoreTabState(int tabIndex)
{
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Restore sizes
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i]->setThickness(state.sizes[i]);
    }
    
    // Restore selection
    selectSizePreset(state.selectedSizeIndex);
}

void EraserSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    
    // Save sizes
    for (int i = 0; i < NUM_PRESETS; ++i) {
        state.sizes[i] = m_sizeButtons[i]->thickness();
    }
    
    // Save selection
    state.selectedSizeIndex = m_selectedSizeIndex;
    state.initialized = true;
}

void EraserSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void EraserSubToolbar::cycleSize()
{
    int start = qMax(0, m_selectedSizeIndex);
    onSizePresetClicked((start + 1) % NUM_PRESETS);
}

void EraserSubToolbar::onSizePresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;

    // Always apply the size when clicked - the preset might show as "selected"
    // but the actual current size could be different (changed via other means)
    const bool indexChanged = (m_selectedSizeIndex != index);
    selectSizePreset(index);

    // Emit size change
    emit eraserSizeChanged(m_sizeButtons[index]->thickness());

    // Persist the new selection so it survives an app restart. Guarded on
    // indexChanged so spam-clicking the same preset does not write QSettings
    // every click; the re-emit above still fires unconditionally to handle
    // the case where the actual current size drifted via another path.
    if (indexChanged) saveSelectionToSettings();
}

void EraserSubToolbar::onSizeEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open size edit dialog with eraser range (5-200)
    ThicknessEditDialog dialog(m_sizeButtons[index]->thickness(), MIN_SIZE, MAX_SIZE,
                               /*currentMinWidth=*/-1.0, this);
    dialog.setWindowTitle(tr("Edit Eraser Size"));
    
    if (dialog.exec() == QDialog::Accepted) {
        qreal newSize = dialog.thickness();
        m_sizeButtons[index]->setThickness(newSize);
        saveToSettings();  // Persist change
        
        // If this is the selected preset, emit change
        if (m_selectedSizeIndex == index) {
            emit eraserSizeChanged(newSize);
        }
    }
}

void EraserSubToolbar::selectSizePreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i]->setSelected(i == index);
    }
    
    m_selectedSizeIndex = index;
}
