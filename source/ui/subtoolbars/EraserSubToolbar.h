#ifndef ERASERSUBTOOLBAR_H
#define ERASERSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../widgets/ModeToggleButton.h"
#include <QColor>
#include <QHash>

class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Eraser tool.
 * 
 * Layout:
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
     * @brief Get the current eraser mode index (0 = Normal, 1 = Lasso).
     */
    int currentModeIndex() const;

    /**
     * @brief Set the eraser mode from external source without emitting signal.
     * @param mode 0 = Normal, 1 = Lasso.
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
    void selectSizePreset(int index);

    // Widgets
    ModeToggleButton* m_modeToggle = nullptr;
    ThicknessPresetButton* m_sizeButtons[3] = {nullptr, nullptr, nullptr};
    
    // Current state
    int m_selectedSizeIndex = 1;  // Default: medium (index 1)
    int m_eraserModeIndex = 0;    // 0 = Normal, 1 = Lasso
    
    // Per-tab state storage
    struct TabState {
        qreal sizes[3];
        int selectedSizeIndex;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // Default values
    static constexpr int NUM_PRESETS = 3;
    static constexpr qreal DEFAULT_SIZES[NUM_PRESETS] = {5.0, 15.0, 40.0};
    static constexpr qreal MIN_SIZE = 2.0;
    static constexpr qreal MAX_SIZE = 100.0;
    
    // Preview color (gray, visible in both light and dark themes)
    static const QColor PREVIEW_COLOR;
    
    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_SIZE_PREFIX;
    static const QString KEY_SELECTED_SIZE;
    static const QString KEY_ERASER_MODE;
};

#endif // ERASERSUBTOOLBAR_H
