#ifndef ERASERSUBTOOLBAR_H
#define ERASERSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../../core/DocumentViewport.h"
#include <QColor>
#include <QHash>

class ThicknessPresetButton;
class IconPresetButton;

/**
 * @brief Subtoolbar for the Eraser tool.
 * 
 * Layout (mirrors the Pen subtoolbar: a row of round preset buttons):
 * - 3 mode buttons: whole-stroke / partial / lasso (exactly one selected)
 * - separator
 * - 3 size preset buttons (5, 15, 40 defaults)
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
     * @param mode 0 = Normal, 1 = Lasso.
     */
    void eraserModeChanged(int mode);

private slots:
    void onSizePresetClicked(int index);
    void onSizeEditRequested(int index);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveToSettings();
    /// Write ONLY the selected-size index key under the eraser group.
    /// Used by the click handler so spam-clicking a preset doesn't
    /// re-write all sizes and the eraser mode through the heavier
    /// saveToSettings() path.
    void saveSelectionToSettings();
    void selectSizePreset(int index);

    // Widgets
    IconPresetButton* m_modeButtons[3] = {nullptr, nullptr, nullptr};
    ThicknessPresetButton* m_sizeButtons[3] = {nullptr, nullptr, nullptr};

    /// Show the accent ring on the active mode button only.
    void updateModeButtonSelection();
    
    // Current state
    int m_selectedSizeIndex = 1;  // Default: medium (index 1)
    int m_eraserModeIndex = 0;    // DocumentViewport::EraserMode value
    
    // Per-tab state storage
    struct TabState {
        qreal sizes[3];
        int selectedSizeIndex;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // Default values
    static constexpr int NUM_PRESETS = 3;
    /// Number of eraser modes: whole-stroke, partial, lasso.
    static constexpr int NUM_MODES = 3;
    static constexpr qreal DEFAULT_SIZES[NUM_PRESETS] = {5.0, 15.0, 40.0};
    /// Bounds come from DocumentViewport so the picker cannot offer a size the
    /// viewport would clamp (and therefore draw at a different radius).
    static constexpr qreal MIN_SIZE = DocumentViewport::ERASER_MIN_SIZE_DOC_UNITS;
    static constexpr qreal MAX_SIZE = DocumentViewport::ERASER_MAX_SIZE_DOC_UNITS;
    
    // Preview color (gray, visible in both light and dark themes)
    static const QColor PREVIEW_COLOR;
    
    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_SIZE_PREFIX;
    static const QString KEY_SELECTED_SIZE;
    static const QString KEY_ERASER_MODE;
    /// Marks which mode numbering KEY_ERASER_MODE was written with. Version 1
    /// had Whole=0 / Lasso=1; version 2 added Partial in the middle.
    static const QString KEY_MODE_VERSION;
};

#endif // ERASERSUBTOOLBAR_H
