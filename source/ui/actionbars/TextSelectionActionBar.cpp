#include "TextSelectionActionBar.h"
#include "../widgets/ActionBarButton.h"

TextSelectionActionBar::TextSelectionActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupButtons();
}

void TextSelectionActionBar::setupButtons()
{
    // Create Copy button - the only button for PDF text selection
    m_copyButton = new ActionBarButton(this);
    m_copyButton->setIconName("copy");
    m_copyButton->setToolTip(tr("Copy (Ctrl+C)"));
    addButton(m_copyButton);
    connect(m_copyButton, &ActionBarButton::clicked, this, &TextSelectionActionBar::copyRequested);
}

void TextSelectionActionBar::updateButtonStates()
{
    // Copy button is always visible when this action bar is shown
    // No state changes needed
}

void TextSelectionActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation (updates background, shadow, separators)
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to button
    if (m_copyButton) {
        m_copyButton->setDarkMode(darkMode);
    }
}

