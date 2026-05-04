#ifndef LASSOACTIONBAR_H
#define LASSOACTIONBAR_H

#include "ActionBar.h"

#include <QColor>

class ActionBarButton;
class ColorPresetButton;

/**
 * @brief Action bar for lasso selection operations.
 * 
 * Provides quick access to clipboard and delete operations when
 * a lasso selection exists or strokes are in the clipboard.
 * 
 * Layout (when selection exists):
 * - [Cut]    - Visible when selection exists
 * - [Copy]   - Visible when selection exists
 * - [Paste]  - Visible if internal stroke clipboard has content
 * - [Delete] - Visible when selection exists
 * 
 * Layout (paste-only mode, no selection but clipboard has strokes):
 * - [Paste]  - Only visible button
 * 
 * This action bar appears when:
 * - Current tool is Lasso AND (has selection OR clipboard has strokes)
 */
class LassoActionBar : public ActionBar {
    Q_OBJECT

public:
    explicit LassoActionBar(QWidget* parent = nullptr);
    
    /**
     * @brief Update button visibility based on clipboard state.
     * 
     * Shows/hides the Paste button based on whether the internal
     * stroke clipboard has content.
     */
    void updateButtonStates() override;
    
    /**
     * @brief Set whether strokes are in the clipboard.
     * @param hasStrokes True if internal stroke clipboard has content.
     * 
     * Call this when the stroke clipboard changes.
     */
    void setHasStrokesInClipboard(bool hasStrokes);
    
    /**
     * @brief Set whether a lasso selection exists.
     * @param hasSelection True if strokes are currently selected.
     * 
     * When false and clipboard has strokes, shows paste-only mode.
     * When true, shows full action bar (Cut, Copy, Paste, Delete).
     */
    void setHasSelection(bool hasSelection);
    
    /**
     * @brief Set dark mode and update button icons.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode) override;

    /**
     * @brief Push a new color into the recolor swatch.
     *
     * Called by MainWindow whenever the pen color changes or whenever
     * a new lasso selection appears (so the swatch's "default" tracks
     * the current pen color). Does NOT emit @ref recolorRequested.
     *
     * @param color New swatch color.
     */
    void setOverrideColor(const QColor& color);

    /**
     * @brief Get the current swatch color.
     */
    QColor overrideColor() const { return m_overrideColor; }

signals:
    /**
     * @brief Emitted when Copy button is clicked.
     */
    void copyRequested();
    
    /**
     * @brief Emitted when Cut button is clicked.
     */
    void cutRequested();
    
    /**
     * @brief Emitted when Paste button is clicked.
     */
    void pasteRequested();
    
    /**
     * @brief Emitted when Delete button is clicked.
     */
    void deleteRequested();

    /**
     * @brief Emitted on first click of the recolor swatch, or after the
     *        user picks a color via the editor (re-apply with new color).
     * @param color The swatch color to apply to every selected stroke.
     */
    void recolorRequested(const QColor& color);

    /**
     * @brief Emitted when the recolor swatch is clicked while already
     *        selected (request to open the color customization dialog).
     */
    void recolorEditRequested();

private:
    void setupButtons();

    ColorPresetButton* m_recolorButton = nullptr;
    ActionBarButton* m_copyButton = nullptr;
    ActionBarButton* m_cutButton = nullptr;
    ActionBarButton* m_pasteButton = nullptr;
    ActionBarButton* m_deleteButton = nullptr;

    QColor m_overrideColor;        ///< Last color pushed into the swatch (pen color or dialog result)
    bool m_hasStrokesInClipboard = false;
    bool m_hasSelection = false;
};

#endif // LASSOACTIONBAR_H

