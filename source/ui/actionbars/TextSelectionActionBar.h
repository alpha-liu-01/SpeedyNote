#ifndef TEXTSELECTIONACTIONBAR_H
#define TEXTSELECTIONACTIONBAR_H

#include "ActionBar.h"

class ActionBarButton;

/**
 * @brief Action bar for PDF text selection operations.
 * 
 * Provides quick access to copy selected PDF text to clipboard.
 * This is the simplest action bar - only Copy is relevant for PDF text
 * (cannot cut or delete PDF text content).
 * 
 * Layout:
 * - [Copy]  - Only button
 * 
 * This action bar appears when:
 * - Current tool is Highlighter
 * - PDF text is selected
 */
class TextSelectionActionBar : public ActionBar {
    Q_OBJECT

public:
    explicit TextSelectionActionBar(QWidget* parent = nullptr);
    
    /**
     * @brief Update button visibility based on current state.
     * 
     * The Copy button is always visible when this action bar is shown.
     */
    void updateButtonStates() override;
    
    /**
     * @brief Set dark mode and update button icon.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode) override;

signals:
    /**
     * @brief Emitted when Copy button is clicked.
     */
    void copyRequested();

private:
    void setupButtons();
    
    ActionBarButton* m_copyButton = nullptr;
};

#endif // TEXTSELECTIONACTIONBAR_H

