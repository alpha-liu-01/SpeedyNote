#ifndef MARKERSUBTOOLBAR_H
#define MARKERSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>
#include <QHash>

class ColorPresetButton;
class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Marker tool.
 * 
 * Layout:
 * - 3 color preset buttons (shared with Highlighter)
 * - Separator
 * - 3 thickness preset buttons (marker-specific)
 * 
 * Key difference from PenSubToolbar:
 * - Colors are SHARED with HighlighterSubToolbar via same QSettings keys
 * - Thickness presets are marker-specific (8.0, 16.0, 32.0 defaults)
 * 
 * Features:
 * - Click unselected preset → select and apply
 * - Click selected preset → open editor dialog
 * - Per-tab state for preset values and selection
 * - Global persistence via QSettings
 */
class MarkerSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit MarkerSubToolbar(QWidget* parent = nullptr);
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;

signals:
    /**
     * @brief Emitted when the marker color changes.
     * @param color The new marker color.
     */
    void markerColorChanged(const QColor& color);
    
    /**
     * @brief Emitted when the marker thickness changes.
     * @param thickness The new marker thickness.
     */
    void markerThicknessChanged(qreal thickness);

private slots:
    void onColorPresetClicked(int index);
    void onColorEditRequested(int index);
    void onThicknessPresetClicked(int index);
    void onThicknessEditRequested(int index);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveColorsToSettings();
    void saveThicknessesToSettings();
    void selectColorPreset(int index);
    void selectThicknessPreset(int index);
    void updateThicknessPreviewColors();

    // Widgets
    ColorPresetButton* m_colorButtons[3] = {nullptr, nullptr, nullptr};
    ThicknessPresetButton* m_thicknessButtons[3] = {nullptr, nullptr, nullptr};
    
    // Current state
    int m_selectedColorIndex = 0;      // Default: first color
    int m_selectedThicknessIndex = 0;  // Default: thin
    
    // Per-tab state storage
    struct TabState {
        QColor colors[3];
        qreal thicknesses[3];
        int selectedColorIndex;
        int selectedThicknessIndex;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // Default values
    static constexpr int NUM_PRESETS = 3;
    static const QColor DEFAULT_COLORS[NUM_PRESETS];
    static constexpr qreal DEFAULT_THICKNESSES[NUM_PRESETS] = {8.0, 16.0, 32.0};
    
    // Marker opacity (50% = 128/255)
    // This is applied when emitting markerColorChanged to maintain consistency
    static constexpr int MARKER_OPACITY = 128;
    
    // QSettings keys
    // NOTE: Color keys are SHARED with Highlighter
    static const QString SETTINGS_GROUP_MARKER;
    static const QString SETTINGS_GROUP_SHARED_COLORS;
    static const QString KEY_COLOR_PREFIX;
    static const QString KEY_THICKNESS_PREFIX;
    static const QString KEY_SELECTED_COLOR;
    static const QString KEY_SELECTED_THICKNESS;
};

#endif // MARKERSUBTOOLBAR_H

