#include "ClipboardActionBar.h"
#include "../widgets/ActionBarButton.h"

ClipboardActionBar::ClipboardActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupButtons();
}

void ClipboardActionBar::setupButtons()
{
    // Create Paste button - the only button for clipboard paste
    m_pasteButton = new ActionBarButton(this);
    m_pasteButton->setIconName("paste");
    m_pasteButton->setToolTip(tr("Paste (Ctrl+V)"));
    addButton(m_pasteButton);
    connect(m_pasteButton, &ActionBarButton::clicked, this, &ClipboardActionBar::pasteRequested);
}

void ClipboardActionBar::updateButtonStates()
{
    // Paste button is always visible when this action bar is shown
    // No state changes needed
}

void ClipboardActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation (updates background, shadow, separators)
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to button
    if (m_pasteButton) {
        m_pasteButton->setDarkMode(darkMode);
    }
}

