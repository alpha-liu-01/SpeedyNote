#include "LassoActionBar.h"
#include "../widgets/ActionBarButton.h"

LassoActionBar::LassoActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupButtons();
}

void LassoActionBar::setupButtons()
{
    // Create Cut button (top)
    // Note: Using cross icon as cut icon is not available
    m_cutButton = new ActionBarButton(this);
    m_cutButton->setIconName("cut");
    m_cutButton->setToolTip(tr("Cut (Ctrl+X)"));
    addButton(m_cutButton);
    connect(m_cutButton, &ActionBarButton::clicked, this, &LassoActionBar::cutRequested);
    
    // Create Copy button
    m_copyButton = new ActionBarButton(this);
    m_copyButton->setIconName("copy");
    m_copyButton->setToolTip(tr("Copy (Ctrl+C)"));
    addButton(m_copyButton);
    connect(m_copyButton, &ActionBarButton::clicked, this, &LassoActionBar::copyRequested);
    
    // Create Paste button (initially hidden - shown only if clipboard has strokes)
    m_pasteButton = new ActionBarButton(this);
    m_pasteButton->setIconName("paste");
    m_pasteButton->setToolTip(tr("Paste (Ctrl+V)"));
    m_pasteButton->hide();  // Hidden by default
    addButton(m_pasteButton);
    connect(m_pasteButton, &ActionBarButton::clicked, this, &LassoActionBar::pasteRequested);
    
    // Create Delete button (bottom)
    m_deleteButton = new ActionBarButton(this);
    m_deleteButton->setIconName("trash");
    m_deleteButton->setToolTip(tr("Delete"));
    addButton(m_deleteButton);
    connect(m_deleteButton, &ActionBarButton::clicked, this, &LassoActionBar::deleteRequested);
}

void LassoActionBar::updateButtonStates()
{
    // Cut, Copy, Delete buttons: visible only when selection exists
    if (m_cutButton) {
        m_cutButton->setVisible(m_hasSelection);
    }
    if (m_copyButton) {
        m_copyButton->setVisible(m_hasSelection);
    }
    if (m_deleteButton) {
        m_deleteButton->setVisible(m_hasSelection);
    }
    
    // Paste button: visible when clipboard has strokes
    if (m_pasteButton) {
        m_pasteButton->setVisible(m_hasStrokesInClipboard);
    }
    
    // Trigger re-layout to adjust height
    adjustSize();
    updateGeometry();
}

void LassoActionBar::setHasStrokesInClipboard(bool hasStrokes)
{
    if (m_hasStrokesInClipboard != hasStrokes) {
        m_hasStrokesInClipboard = hasStrokes;
        updateButtonStates();
    }
}

void LassoActionBar::setHasSelection(bool hasSelection)
{
    if (m_hasSelection != hasSelection) {
        m_hasSelection = hasSelection;
        updateButtonStates();
    }
}

void LassoActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation (updates background, shadow, separators)
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to all buttons
    if (m_copyButton) {
        m_copyButton->setDarkMode(darkMode);
    }
    if (m_cutButton) {
        m_cutButton->setDarkMode(darkMode);
    }
    if (m_pasteButton) {
        m_pasteButton->setDarkMode(darkMode);
    }
    if (m_deleteButton) {
        m_deleteButton->setDarkMode(darkMode);
    }
}

