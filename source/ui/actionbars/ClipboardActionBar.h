#ifndef CLIPBOARDACTIONBAR_H
#define CLIPBOARDACTIONBAR_H

#include "ActionBar.h"

class ActionBarButton;

/**
 * @brief Action bar for paste-only operations.
 * 
 * Provides quick access to paste an image from the system clipboard.
 * This is a single-button action bar that appears when:
 * - Current tool is ObjectSelect
 * - System clipboard contains an image
 * - No object is currently selected
 * 
 * Layout:
 * - [Paste]  - Only button
 * 
 * This action bar is triggered automatically via QClipboard::dataChanged
 * signal detection in ActionBarContainer.
 */
class ClipboardActionBar : public ActionBar {
    Q_OBJECT

public:
    explicit ClipboardActionBar(QWidget* parent = nullptr);
    
    /**
     * @brief Update button visibility based on current state.
     * 
     * The Paste button is always visible when this action bar is shown.
     */
    void updateButtonStates() override;
    
    /**
     * @brief Set dark mode and update button icon.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode) override;

signals:
    /**
     * @brief Emitted when Paste button is clicked.
     */
    void pasteRequested();

private:
    void setupButtons();
    
    ActionBarButton* m_pasteButton = nullptr;
};

#endif // CLIPBOARDACTIONBAR_H

