#ifndef PENSUBTOOLBAR_H
#define PENSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>
#include <QHash>

class ColorPresetButton;
class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Pen tool.
 * 
 * Layout:
 * - 3 color preset buttons (red, blue, black defaults)
 * - Separator
 * - 3 thickness preset buttons (2.0, 5.0, 10.0 defaults)
 * 
 * Features:
 * - Click unselected preset → select and apply
 * - Click selected preset → open editor dialog
 * - Per-tab state for preset values and selection
 * - Global persistence via QSettings
 */
class PenSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit PenSubToolbar(QWidget* parent = nullptr);
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;
    
    /**
     * @brief Emit the currently selected preset values.
     * 
     * Call this when connecting to a new viewport to sync its
     * color/thickness with the subtoolbar's current selection.
     */
    void emitCurrentValues();
    
    /**
     * @brief Get the currently selected pen color.
     * @return The color from the selected preset button.
     */
    QColor currentColor() const;
    
    /**
     * @brief Get the currently selected pen thickness.
     * @return The thickness from the selected preset button.
     */
    qreal currentThickness() const;

    /**
     * @brief Get the currently selected preset's minimum stroke width.
     *
     * Unlike `currentThickness` / `currentColor`, this value lives only on
     * the PenSubToolbar (not on the button widget) because the preset button
     * has no visual representation of the min-width.
     * @return Minimum stroke width in pt for the selected preset.
     */
    qreal currentMinStrokeWidth() const;

signals:
    /**
     * @brief Emitted when the pen color changes.
     * @param color The new pen color.
     */
    void penColorChanged(const QColor& color);
    
    /**
     * @brief Emitted when the pen thickness changes.
     * @param thickness The new pen thickness.
     */
    void penThicknessChanged(qreal thickness);

    /**
     * @brief Emitted when the pen's per-preset minimum stroke width changes.
     *
     * Fires alongside `penThicknessChanged` when the user switches presets or
     * edits the selected preset's min-width row in the thickness dialog.
     * @param minWidth The new minimum stroke width in pt.
     */
    void penMinStrokeWidthChanged(qreal minWidth);

private slots:
    void onColorPresetClicked(int index);
    void onColorEditRequested(int index);
    void onThicknessPresetClicked(int index);
    void onThicknessEditRequested(int index);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveToSettings();
    void selectColorPreset(int index);
    void selectThicknessPreset(int index);
    void updateThicknessPreviewColors();

    // Widgets
    ColorPresetButton* m_colorButtons[3] = {nullptr, nullptr, nullptr};
    ThicknessPresetButton* m_thicknessButtons[3] = {nullptr, nullptr, nullptr};
    
    // Current state
    int m_selectedColorIndex = 2;      // Default: black (index 2)
    int m_selectedThicknessIndex = 0;  // Default: thin (index 0)
    
    // Per-preset minimum stroke width (pt).  Floored into the stored stroke
    // pressure at capture time by DocumentViewport; see the plan doc for why
    // we don't store this on the button widget.
    qreal m_minWidths[3] = {0.3, 0.3, 0.3};

    // Per-tab state storage
    struct TabState {
        QColor colors[3];
        qreal thicknesses[3];
        qreal minWidths[3];
        int selectedColorIndex;
        int selectedThicknessIndex;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;

    // Default values
    static constexpr int NUM_PRESETS = 3;
    static const QColor DEFAULT_COLORS[NUM_PRESETS];
    static const QColor DEFAULT_COLORS_DARK[NUM_PRESETS];
    static constexpr qreal DEFAULT_THICKNESSES[NUM_PRESETS] = {2.0, 5.0, 10.0};
    static constexpr qreal DEFAULT_MIN_WIDTH = 0.3;

    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_COLOR_PREFIX;
    static const QString KEY_THICKNESS_PREFIX;
    static const QString KEY_MIN_WIDTH_PREFIX;
    static const QString KEY_SELECTED_COLOR;
    static const QString KEY_SELECTED_THICKNESS;
    static const QString KEY_LEGACY_MIN_STROKE_WIDTH; // legacy tools/minStrokeWidth
};

#endif // PENSUBTOOLBAR_H

