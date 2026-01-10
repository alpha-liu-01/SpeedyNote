#ifndef HIGHLIGHTERSUBTOOLBAR_H
#define HIGHLIGHTERSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>
#include <QHash>

class ColorPresetButton;
class SubToolbarToggle;

/**
 * @brief Subtoolbar for the Highlighter (text selection) tool.
 * 
 * Layout:
 * - 3 color preset buttons (SHARED with Marker)
 * - Separator
 * - 1 auto-highlight toggle button
 * 
 * Key features:
 * - Colors are SHARED with MarkerSubToolbar via same QSettings keys
 * - Auto-highlight toggle controls automatic highlighting of selected text
 * - No thickness controls (highlighter has fixed thickness)
 * 
 * Features:
 * - Click unselected color preset → select and apply
 * - Click selected color preset → open editor dialog
 * - Toggle auto-highlight mode on/off
 * - Per-tab state for preset values and selection
 * - Global persistence via QSettings
 */
class HighlighterSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit HighlighterSubToolbar(QWidget* parent = nullptr);
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void setDarkMode(bool darkMode) override;
    
    /**
     * @brief Set the auto-highlight toggle state from outside.
     * @param enabled The new state.
     * 
     * Used to sync the toggle when auto-highlight is changed via keyboard shortcut (Ctrl+H).
     * This updates the UI without emitting the autoHighlightChanged signal to avoid loops.
     */
    void setAutoHighlightState(bool enabled);

signals:
    /**
     * @brief Emitted when the highlighter color changes.
     * @param color The new highlighter color.
     */
    void highlighterColorChanged(const QColor& color);
    
    /**
     * @brief Emitted when auto-highlight mode changes.
     * @param enabled True if auto-highlight is enabled.
     */
    void autoHighlightChanged(bool enabled);

private slots:
    void onColorPresetClicked(int index);
    void onColorEditRequested(int index);
    void onAutoHighlightToggled(bool checked);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveColorsToSettings();
    void saveAutoHighlightToSettings();
    void selectColorPreset(int index);

    // Widgets
    ColorPresetButton* m_colorButtons[3] = {nullptr, nullptr, nullptr};
    SubToolbarToggle* m_autoHighlightToggle = nullptr;
    
    // Current state
    int m_selectedColorIndex = 0;  // Default: first color
    bool m_autoHighlightEnabled = false;
    
    // Per-tab state storage
    struct TabState {
        QColor colors[3];
        int selectedColorIndex;
        bool autoHighlightEnabled;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // Default values
    static constexpr int NUM_PRESETS = 3;
    static const QColor DEFAULT_COLORS[NUM_PRESETS];  // Same as Marker
    
    // Marker opacity (50% = 128/255) - shared with Marker tool
    // This is applied when emitting highlighterColorChanged to maintain consistency
    static constexpr int MARKER_OPACITY = 128;
    
    // QSettings keys
    // NOTE: Color keys are SHARED with Marker
    static const QString SETTINGS_GROUP_SHARED_COLORS;
    static const QString SETTINGS_GROUP_HIGHLIGHTER;
    static const QString KEY_COLOR_PREFIX;
    static const QString KEY_SELECTED_COLOR;
    static const QString KEY_AUTO_HIGHLIGHT;
};

#endif // HIGHLIGHTERSUBTOOLBAR_H

