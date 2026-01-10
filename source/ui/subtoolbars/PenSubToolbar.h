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
    static constexpr qreal DEFAULT_THICKNESSES[NUM_PRESETS] = {2.0, 5.0, 10.0};
    
    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_COLOR_PREFIX;
    static const QString KEY_THICKNESS_PREFIX;
    static const QString KEY_SELECTED_COLOR;
    static const QString KEY_SELECTED_THICKNESS;
};

#endif // PENSUBTOOLBAR_H

