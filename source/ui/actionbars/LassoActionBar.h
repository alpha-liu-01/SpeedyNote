#ifndef LASSOACTIONBAR_H
#define LASSOACTIONBAR_H

#include "ActionBar.h"

class ActionBarButton;

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

private:
    void setupButtons();
    
    ActionBarButton* m_copyButton = nullptr;
    ActionBarButton* m_cutButton = nullptr;
    ActionBarButton* m_pasteButton = nullptr;
    ActionBarButton* m_deleteButton = nullptr;
    
    bool m_hasStrokesInClipboard = false;
    bool m_hasSelection = false;
};

#endif // LASSOACTIONBAR_H

