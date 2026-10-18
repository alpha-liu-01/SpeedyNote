#ifndef ERASERSUBTOOLBAR_H
#define ERASERSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../../core/DocumentViewport.h"
#include <QColor>
#include <QHash>

class QFrame;
class ThicknessPresetButton;
class IconPresetButton;

/**
 * @brief Subtoolbar for the Eraser tool.
 *
 * Layout (mirrors the Pen subtoolbar: a row of round preset buttons):
 * - 3 mode buttons: whole-stroke / partial / lasso (exactly one selected)
 * - separator + 3 size preset buttons, hidden in lasso mode
 *
 * Size presets are kept per mode: the whole-stroke eraser and the partial
 * eraser each own three values and their own selection, so switching modes
 * restores that mode's sizes instead of dragging one shared set around. The
 * two want different magnitudes anyway - a whole-stroke eraser only has to
 * touch ink, while the partial one should span the stroke it is cutting.
 *
 * Lasso mode has no size at all (it removes everything inside the drawn
 * region), so the size row is hidden there rather than left showing a control
 * that does nothing.
 *
 * Features:
 * - Click unselected preset → select and apply
 * - Click selected preset → open editor dialog
 * - Per-tab state for preset values and selection
 * - Global persistence via QSettings
 *
 * Size range: 2-100
 * Preview color: Gray (#808080)
 */
class EraserSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit EraserSubToolbar(QWidget* parent = nullptr);
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;
    
    /**
     * @brief Emit the currently selected preset value.
     * 
     * Call this when connecting to a new viewport to sync its
     * eraser size with the subtoolbar's current selection.
     */
    void emitCurrentValues();
    
    /**
     * @brief Get the currently selected eraser size.
     * @return The size from the selected preset button.
     */
    qreal currentSize() const;

    /**
     * @brief Advance to the next size preset (wraps), applying and persisting it.
     * Reuses the click-handler path so behaviour matches a manual preset click.
     * Does nothing in lasso mode, which has no size.
     */
    void cycleSize();

    /**
     * @brief Get the current eraser mode index.
     * @return The DocumentViewport::EraserMode value (0 = Whole, 1 = Partial, 2 = Lasso).
     */
    int currentModeIndex() const;

    /**
     * @brief Set the eraser mode from external source without emitting signal.
     * @param mode A DocumentViewport::EraserMode value.
     */
    void setModeState(int mode);

signals:
    /**
     * @brief Emitted when the eraser size changes.
     * @param size The new eraser size.
     */
    void eraserSizeChanged(qreal size);

    /**
     * @brief Emitted when the eraser mode changes.
     * @param mode A DocumentViewport::EraserMode value.
     */
    void eraserModeChanged(int mode);

private slots:
    void onSizePresetClicked(int index);
    void onSizeEditRequested(int index);

private:
    // Constants first: the member arrays below are sized from them.
    static constexpr int NUM_PRESETS = 3;
    /// Number of eraser modes: whole-stroke, partial, lasso.
    static constexpr int NUM_MODES = 3;
    /// Number of modes that own a size: whole-stroke and partial.
    static constexpr int NUM_SIZE_SETS = 2;
    static constexpr qreal DEFAULT_SIZES[NUM_SIZE_SETS][NUM_PRESETS] = {
        {5.0, 15.0, 40.0},   // whole-stroke: the values this bar always shipped with
        {8.0, 15.0, 25.0}    // partial: large enough to cut a stroke cleanly
    };
    /// Bounds come from DocumentViewport so the picker cannot offer a size the
    /// viewport would clamp (and therefore draw at a different radius).
    static constexpr qreal MIN_SIZE = DocumentViewport::ERASER_MIN_SIZE_DOC_UNITS;
    static constexpr qreal MAX_SIZE = DocumentViewport::ERASER_MAX_SIZE_DOC_UNITS;

    /// One mode's three presets and which of them is active.
    struct SizeSet {
        qreal sizes[NUM_PRESETS] = {0.0, 0.0, 0.0};
        int selected = 1;
    };

    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveToSettings();
    /// Write ONLY the selected-size index of the active mode.
    /// Used by the click handler so spam-clicking a preset doesn't
    /// re-write every size and the mode through the heavier
    /// saveToSettings() path.
    void saveSelectionToSettings();
    void selectSizePreset(int index);

    /**
     * @brief Size slot for a mode: 0 = whole-stroke, 1 = partial, -1 = lasso.
     */
    static int sizeSlotForMode(int mode);

    /// @brief Slot of the mode currently selected, or -1 (lasso).
    int activeSizeSlot() const { return sizeSlotForMode(m_eraserModeIndex); }

    /// @brief Copy the on-screen button values into the active mode's set.
    /// No-op in lasso mode, where the buttons keep the last disc-based sizes.
    void flushButtonsToActiveSlot();

    /// @brief Push one mode's set onto the buttons and apply its selection.
    void loadSizeSet(int slot);

    /// @brief Show/hide the size row for the current mode and sync the rings.
    void applyModePresentation();

    /// @brief Show the accent ring on the active mode button only.
    void updateModeButtonSelection();

    // Widgets
    IconPresetButton* m_modeButtons[NUM_MODES] = {nullptr, nullptr, nullptr};
    ThicknessPresetButton* m_sizeButtons[NUM_PRESETS] = {nullptr, nullptr, nullptr};
    QFrame* m_sizeSeparator = nullptr;
    
    // Current state
    int m_selectedSizeIndex = 1;  // Default: medium (index 1)
    int m_eraserModeIndex = 0;    // DocumentViewport::EraserMode value

    /// Live presets, one set per disc-based mode (see sizeSlotForMode).
    SizeSet m_sizeSets[NUM_SIZE_SETS];
    
    // Per-tab state storage
    struct TabState {
        SizeSet sets[NUM_SIZE_SETS];
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // Preview color (gray, visible in both light and dark themes)
    static const QColor PREVIEW_COLOR;
    
    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_ERASER_MODE;
    /// Marks which settings layout KEY_ERASER_MODE was written with.
    /// v1: Whole=0 / Lasso=1, one shared size set.
    /// v2: Partial inserted at 1 (Whole=0 / Partial=1 / Lasso=2), still shared.
    /// v3: per-mode size sets ({"whole", "partial"} groups).
    static const QString KEY_MODE_VERSION;
    static const QString KEY_SLOT_WHOLE;
    static const QString KEY_SLOT_PARTIAL;
    static const QString KEY_SIZE_PREFIX;
    static const QString KEY_SELECTED_SIZE;
};

#endif // ERASERSUBTOOLBAR_H
