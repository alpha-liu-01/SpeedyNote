#ifndef HIGHLIGHTERSUBTOOLBAR_H
#define HIGHLIGHTERSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>
#include <QHash>

class ColorPresetButton;
class ModeToggleButton;
class QToolButton;
class QMenu;
class QAction;

/**
 * @brief Subtoolbar for the Highlighter (text selection) tool.
 *
 * Layout:
 * - 3 color preset buttons (SHARED with Marker)
 * - Separator
 * - Auto-highlight style dropdown (None / Cover / Underline / Dotted underline)
 * - Separator
 * - Selection source toggle (PDF text vs OCR text)
 *
 * Key features:
 * - Colors are SHARED with MarkerSubToolbar via same QSettings keys
 * - Auto-highlight dropdown selects the style of stroke generated on release
 *   (HighlightStyle::None disables auto-highlighting entirely)
 * - No thickness controls (highlighter has fixed thickness)
 *
 * Features:
 * - Click unselected color preset → select and apply
 * - Click selected color preset → open editor dialog
 * - Pick an auto-highlight style from the dropdown
 * - Per-tab state for preset values and selection
 * - Global persistence via QSettings
 */
class HighlighterSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    /**
     * @brief Which layer the Highlighter tool extracts text from.
     *
     * Mirrors DocumentViewport::HighlighterMode but declared locally so the
     * subtoolbar does not need to include the viewport header.
     */
    enum class SelectionSource { Pdf = 0, Ocr = 1 };

    /**
     * @brief Style of auto-generated highlight stroke.
     *
     * Mirrors DocumentViewport::HighlightStyle (same ordering so a
     * `static_cast` is safe when marshalling across the two APIs).
     * Persisted as an int under the existing "autoHighlight" QSettings key
     * (backward-compat: old bool `true` => Cover, `false` => None).
     */
    enum class HighlightStyle {
        None = 0,
        Cover = 1,
        Underline = 2,
        DottedUnderline = 3,
    };

    /// Number of entries in @ref HighlightStyle (indices 0..kNumStyles-1).
    static constexpr int kNumStyles = 4;

    explicit HighlighterSubToolbar(QWidget* parent = nullptr);
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;
    void setDarkMode(bool darkMode) override;
    
    /**
     * @brief Sync shared state from QSettings (overrides SubToolbar::syncSharedState).
     * 
     * Reloads shared colors from QSettings to sync with Marker edits.
     */
    void syncSharedState() override;
    
    /**
     * @brief Reload shared colors from QSettings.
     * 
     * Called when switching from Marker to Highlighter to sync shared color presets.
     * This only reloads colors (not selection), preserving per-tab state.
     */
    void syncSharedColorsFromSettings();
    
    /**
     * @brief Set the auto-highlight style from outside.
     * @param style The new style.
     *
     * Used to sync the dropdown when auto-highlight is changed via keyboard
     * shortcut (Ctrl+H) or when switching to a viewport with a different
     * active style. Updates the UI without emitting autoHighlightStyleChanged
     * to avoid feedback loops.
     */
    void setAutoHighlightStyle(HighlightStyle style);

    /**
     * @brief Get the currently selected auto-highlight style.
     */
    HighlightStyle currentAutoHighlightStyle() const { return m_autoHighlightStyle; }

    /**
     * @brief Set the selection-source toggle state from outside.
     * @param src The new selection source (PDF or OCR).
     *
     * Used to sync the toggle when the active viewport's mode changes (tab switch,
     * programmatic change). Updates UI without emitting selectionSourceChanged.
     */
    void setSelectionSourceState(SelectionSource src);

    /**
     * @brief Get the currently selected source (PDF or OCR).
     */
    SelectionSource currentSelectionSource() const { return m_selectionSource; }

    /**
     * @brief Shortcut-driven style selection.
     *
     * Reuses the existing menu-click path (QAction::trigger() on the matching
     * dropdown action) so settings persistence, check-state, icon refresh,
     * and the single `autoHighlightStyleChanged` emission all happen through
     * one code path. No-op when @p style already matches the current style.
     */
    void selectAutoHighlightStyleFromShortcut(HighlightStyle style);

    /**
     * @brief Shortcut-driven source toggle.
     *
     * Flips PDF <-> OCR by driving the underlying ModeToggleButton so the
     * normal `selectionSourceChanged` signal fires through `onSelectionSourceToggled`.
     */
    void toggleSelectionSourceFromShortcut();
    
    /**
     * @brief Emit the currently selected preset values.
     * 
     * Call this when connecting to a new viewport to sync its
     * color with the subtoolbar's current selection.
     */
    void emitCurrentValues();
    
    /**
     * @brief Get the currently selected highlighter color (with MARKER_OPACITY applied).
     * @return The color from the selected preset button with 50% opacity.
     */
    QColor currentColor() const;

signals:
    /**
     * @brief Emitted when the highlighter color changes.
     * @param color The new highlighter color.
     */
    void highlighterColorChanged(const QColor& color);
    
    /**
     * @brief Emitted when the auto-highlight style changes via the dropdown.
     * @param style The new style (HighlightStyle::None disables auto-highlight).
     */
    void autoHighlightStyleChanged(HighlightStyle style);

    /**
     * @brief Emitted when the selection source (PDF vs OCR) changes via the toggle.
     * @param src The new source.
     */
    void selectionSourceChanged(SelectionSource src);

private slots:
    void onColorPresetClicked(int index);
    void onColorEditRequested(int index);
    void onAutoHighlightStyleTriggered(QAction* action);
    void onSelectionSourceToggled(int mode);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveColorsToSettings();
    void saveAutoHighlightToSettings();
    void saveSelectionSourceToSettings();
    void selectColorPreset(int index);

    /// Cheap refresh: updates the trigger-button icon and per-action check state
    /// to match @ref m_autoHighlightStyle. Called on every style change.
    void updateAutoHighlightButtonIcon();

    /// One-shot restyling: applies per-action icons and the dark/light stylesheets
    /// to both the trigger button and the dropdown menu. Only depends on
    /// @c isDarkMode(), so it is called from construction and @ref setDarkMode(),
    /// never from per-click style changes.
    void applyAutoHighlightStyling();

    // Widgets
    ColorPresetButton* m_colorButtons[3] = {nullptr, nullptr, nullptr};
    QToolButton*       m_autoHighlightButton = nullptr;
    QMenu*             m_autoHighlightMenu   = nullptr;
    QAction*           m_styleActions[kNumStyles] = {};
    ModeToggleButton*  m_selectionSourceToggle = nullptr;

    // Current state
    int m_selectedColorIndex = 0;  // Default: first color
    HighlightStyle m_autoHighlightStyle = HighlightStyle::None;
    SelectionSource m_selectionSource = SelectionSource::Pdf;
    
    // Per-tab state storage
    // NOTE: autoHighlightStyle is NOT stored here - DocumentViewport is the source of truth.
    // The subtoolbar syncs its dropdown state from the viewport via setAutoHighlightStyle().
    struct TabState {
        QColor colors[3];
        int selectedColorIndex;
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
    static const QString KEY_SELECTION_SOURCE;
};

#endif // HIGHLIGHTERSUBTOOLBAR_H

