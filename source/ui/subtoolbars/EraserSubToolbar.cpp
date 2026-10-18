#include "EraserSubToolbar.h"
#include "../widgets/ThicknessPresetButton.h"
#include "../widgets/IconPresetButton.h"

#include <QFrame>
#include <QSettings>

// Static member definitions
const QColor EraserSubToolbar::PREVIEW_COLOR = QColor(0x80, 0x80, 0x80);  // Gray

const QString EraserSubToolbar::SETTINGS_GROUP = "eraser";
const QString EraserSubToolbar::KEY_ERASER_MODE = "eraserMode";
const QString EraserSubToolbar::KEY_MODE_VERSION = "eraserModeVersion";
const QString EraserSubToolbar::KEY_SLOT_WHOLE = "whole";
const QString EraserSubToolbar::KEY_SLOT_PARTIAL = "partial";
const QString EraserSubToolbar::KEY_SIZE_PREFIX = "size";
const QString EraserSubToolbar::KEY_SELECTED_SIZE = "selectedSize";

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

    // Mode buttons first: three side-by-side choices rather than one button
    // that cycles. A cycle hides what the alternatives are and where you are in
    // the list, and getting back to a mode meant clicking through the others.
    // This is the same "row of presets" the Pen subtoolbar uses.
    static const char* const kModeIcons[NUM_MODES] = {
        "eraser", "eraser_partial", "rope", "rope_partial"
    };
    const QString modeTips[NUM_MODES] = {
        tr("Whole-stroke eraser - touching a stroke removes all of it"),
        tr("Partial eraser - removes only the part you touch"),
        tr("Lasso eraser - draw a region to remove every stroke inside it"),
        tr("Partial lasso eraser - draw a region to cut strokes at its edge")
    };

    for (int i = 0; i < NUM_MODES; ++i) {
        m_modeButtons[i] = new IconPresetButton(this);
        m_modeButtons[i]->setDarkMode(dark);
        m_modeButtons[i]->setIconName(QString::fromLatin1(kModeIcons[i]));
        m_modeButtons[i]->setToolTip(modeTips[i]);
        addWidget(m_modeButtons[i]);
    }

    m_sizeSeparator = addSeparator();

    // Create size preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i] = new ThicknessPresetButton(this);
        m_sizeButtons[i]->setThickness(DEFAULT_SIZES[0][i]);
        m_sizeButtons[i]->setLineColor(PREVIEW_COLOR);
        m_sizeButtons[i]->setToolTip(tr("Eraser size preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_sizeButtons[i]);
    }
}

void EraserSubToolbar::setupConnections()
{
    // Mode buttons: picking one selects it and deselects the rest.
    for (int i = 0; i < NUM_MODES; ++i) {
        connect(m_modeButtons[i], &IconPresetButton::clicked, this, [this, i]() {
            if (m_eraserModeIndex == i) {
                return;
            }
            // Keep any edits made to the outgoing mode's presets before the
            // buttons are repurposed for the incoming mode.
            flushButtonsToActiveSlot();
            m_eraserModeIndex = i;

            const int slot = activeSizeSlot();
            if (slot >= 0) {
                loadSizeSet(slot);
            }
            applyModePresentation();
            saveToSettings();

            emit eraserModeChanged(i);
            // The disc needs a radius again as soon as a disc-based mode is
            // back; the viewport may have been left on a lasso cycle.
            if (slot >= 0) {
                emit eraserSizeChanged(currentSize());
            }
        });
    }

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

int EraserSubToolbar::sizeSlotForMode(int mode)
{
    switch (static_cast<DocumentViewport::EraserMode>(mode)) {
    case DocumentViewport::EraserMode::Normal:       return 0;
    case DocumentViewport::EraserMode::Partial:      return 1;
    case DocumentViewport::EraserMode::Lasso:        return -1;
    case DocumentViewport::EraserMode::LassoPartial: return -1;
    }
    return 0;
}

void EraserSubToolbar::flushButtonsToActiveSlot()
{
    const int slot = activeSizeSlot();
    if (slot < 0) {
        return;   // Lasso: the buttons hold the last disc-based values already.
    }
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeSets[slot].sizes[i] = m_sizeButtons[i]->thickness();
    }
    m_sizeSets[slot].selected = m_selectedSizeIndex;
}

void EraserSubToolbar::loadSizeSet(int slot)
{
    if (slot < 0 || slot >= NUM_SIZE_SETS) {
        return;
    }
    const SizeSet& set = m_sizeSets[slot];
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i]->setThickness(set.sizes[i]);
    }
    selectSizePreset(qBound(0, set.selected, NUM_PRESETS - 1));
}

void EraserSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    // Start from the built-in defaults, then overlay whatever is on disk.
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        for (int i = 0; i < NUM_PRESETS; ++i) {
            m_sizeSets[slot].sizes[i] = DEFAULT_SIZES[slot][i];
        }
        m_sizeSets[slot].selected = 1;
    }

    // Which settings layout produced the stored values. Builds before the
    // per-mode split kept one shared set plus one mode number.
    const int version = settings.value(KEY_MODE_VERSION, 1).toInt();

    if (version < 3) {
        // Legacy: the shared presets belonged to the whole-stroke eraser. The
        // partial eraser is new enough that its defaults are a better seed
        // than the whole-stroke values, which were tuned for a different job.
        for (int i = 0; i < NUM_PRESETS; ++i) {
            const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
            const qreal size = settings.value(key, DEFAULT_SIZES[0][i]).toReal();
            m_sizeSets[0].sizes[i] = qBound(MIN_SIZE, size, MAX_SIZE);
        }
        m_sizeSets[0].selected =
            qBound(0, settings.value(KEY_SELECTED_SIZE, 1).toInt(), NUM_PRESETS - 1);
    } else {
        const QString groups[NUM_SIZE_SETS] = {KEY_SLOT_WHOLE, KEY_SLOT_PARTIAL};
        for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
            settings.beginGroup(groups[slot]);
            for (int i = 0; i < NUM_PRESETS; ++i) {
                const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
                const qreal size = settings.value(key, DEFAULT_SIZES[slot][i]).toReal();
                m_sizeSets[slot].sizes[i] = qBound(MIN_SIZE, size, MAX_SIZE);
            }
            m_sizeSets[slot].selected =
                qBound(0, settings.value(KEY_SELECTED_SIZE, 1).toInt(), NUM_PRESETS - 1);
            settings.endGroup();
        }
    }

    // Load eraser mode. Version 1 stored Lasso as 1, before Partial existed, so
    // an old "1" must not be reinterpreted as Partial.
    const int storedMode = settings.value(KEY_ERASER_MODE, 0).toInt();
    if (version < 2) {
        m_eraserModeIndex = (storedMode == 1)
            ? static_cast<int>(DocumentViewport::EraserMode::Lasso)
            : static_cast<int>(DocumentViewport::EraserMode::Normal);
    } else {
        m_eraserModeIndex = qBound(0, storedMode, NUM_MODES - 1);
    }

    settings.endGroup();

    // The buttons always carry a disc-based set, even when the saved mode is
    // lasso: currentSize() stays meaningful and switching back is instant.
    const int slot = activeSizeSlot();
    loadSizeSet(slot >= 0 ? slot : 0);
    applyModePresentation();
}

void EraserSubToolbar::saveToSettings()
{
    // The buttons are the source of truth for the visible mode, so fold them
    // into that mode's set before writing anything.
    flushButtonsToActiveSlot();

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    const QString groups[NUM_SIZE_SETS] = {KEY_SLOT_WHOLE, KEY_SLOT_PARTIAL};
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        settings.beginGroup(groups[slot]);
        for (int i = 0; i < NUM_PRESETS; ++i) {
            const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
            settings.setValue(key, m_sizeSets[slot].sizes[i]);
        }
        settings.setValue(KEY_SELECTED_SIZE, m_sizeSets[slot].selected);
        settings.endGroup();
    }

    settings.setValue(KEY_ERASER_MODE, m_eraserModeIndex);
    settings.setValue(KEY_MODE_VERSION, 3);

    settings.endGroup();
}

void EraserSubToolbar::saveSelectionToSettings()
{
    // Write ONLY the active mode's selected-size index: cheap enough to call on
    // every preset click, and it leaves the inactive mode's presets untouched.
    const int slot = activeSizeSlot();
    if (slot < 0) {
        return;
    }

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.beginGroup(slot == 0 ? KEY_SLOT_WHOLE : KEY_SLOT_PARTIAL);
    settings.setValue(KEY_SELECTED_SIZE, m_selectedSizeIndex);
    settings.endGroup();
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
    mode = qBound(0, mode, NUM_MODES - 1);
    if (mode != m_eraserModeIndex) {
        flushButtonsToActiveSlot();
        m_eraserModeIndex = mode;
        const int slot = activeSizeSlot();
        if (slot >= 0) {
            loadSizeSet(slot);
        }
    }
    applyModePresentation();
}

void EraserSubToolbar::updateModeButtonSelection()
{
    for (int i = 0; i < NUM_MODES; ++i) {
        if (m_modeButtons[i]) {
            m_modeButtons[i]->setSelected(i == m_eraserModeIndex);
        }
    }
}

void EraserSubToolbar::applyModePresentation()
{
    updateModeButtonSelection();

    // Lasso removes everything inside the drawn region, so it has no size;
    // showing a size row there would offer a control that changes nothing.
    const bool showSizes = activeSizeSlot() >= 0;
    for (int i = 0; i < NUM_PRESETS; ++i) {
        if (m_sizeButtons[i]) {
            m_sizeButtons[i]->setVisible(showSizes);
        }
    }
    if (m_sizeSeparator) {
        m_sizeSeparator->setVisible(showSizes);
    }
}

qreal EraserSubToolbar::currentSize() const
{
    if (m_selectedSizeIndex >= 0 && m_selectedSizeIndex < NUM_PRESETS && m_sizeButtons[m_selectedSizeIndex]) {
        return m_sizeButtons[m_selectedSizeIndex]->thickness();
    }
    return DEFAULT_SIZES[0][1];  // Fallback to medium default
}

void EraserSubToolbar::restoreTabState(int tabIndex)
{
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Both modes' presets travel with the tab, not just the visible one.
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        m_sizeSets[slot] = state.sets[slot];
    }

    const int slot = activeSizeSlot();
    loadSizeSet(slot >= 0 ? slot : 0);
    applyModePresentation();
}

void EraserSubToolbar::saveTabState(int tabIndex)
{
    flushButtonsToActiveSlot();

    TabState& state = m_tabStates[tabIndex];
    
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        state.sets[slot] = m_sizeSets[slot];
    }
    state.initialized = true;
}

void EraserSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void EraserSubToolbar::cycleSize()
{
    // Lasso has no size to cycle through.
    if (activeSizeSlot() < 0) {
        return;
    }
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

    // Keep the mode's own set in step, so switching modes and back does not
    // resurrect the old selection.
    const int slot = activeSizeSlot();
    if (slot >= 0) {
        m_sizeSets[slot].selected = index;
    }

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
    
    // Open size edit dialog with the eraser's own range
    ThicknessEditDialog dialog(m_sizeButtons[index]->thickness(), MIN_SIZE, MAX_SIZE,
                               /*currentMinWidth=*/-1.0, this);
    dialog.setWindowTitle(tr("Edit Eraser Size"));
    
    if (dialog.exec() == QDialog::Accepted) {
        qreal newSize = dialog.thickness();
        m_sizeButtons[index]->setThickness(newSize);
        saveToSettings();  // Persist change (flushes the buttons into the set)
        
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
