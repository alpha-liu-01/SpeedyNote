#include "ObjectSelectActionBar.h"
#include "../widgets/ActionBarButton.h"

#include <QFrame>

ObjectSelectActionBar::ObjectSelectActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupButtons();
}

void ObjectSelectActionBar::setupButtons()
{
    // === Clipboard operations ===
    
    // Create Copy button
    m_copyButton = new ActionBarButton(this);
    m_copyButton->setIconName("copy");
    m_copyButton->setToolTip(tr("Copy (Ctrl+C)"));
    addButton(m_copyButton);
    connect(m_copyButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::copyRequested);
    
    // Create Paste button
    m_pasteButton = new ActionBarButton(this);
    m_pasteButton->setIconName("paste");
    m_pasteButton->setToolTip(tr("Paste (Ctrl+V)"));
    addButton(m_pasteButton);
    connect(m_pasteButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::pasteRequested);
    
    // Create Cancel button (clears clipboard, shown in paste-only mode)
    m_cancelButton = new ActionBarButton(this);
    m_cancelButton->setIconName("cross");
    m_cancelButton->setToolTip(tr("Clear Clipboard (Esc)"));
    addButton(m_cancelButton);
    connect(m_cancelButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::cancelRequested);
    
    // Create Delete button
    m_deleteButton = new ActionBarButton(this);
    m_deleteButton->setIconName("trash");
    m_deleteButton->setToolTip(tr("Delete"));
    addButton(m_deleteButton);
    connect(m_deleteButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::deleteRequested);
    
    // === Separator ===
    // Create separator and store reference for visibility control
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(2);
    // Set initial color based on current theme
    if (isDarkMode()) {
        separator->setStyleSheet("background-color: #555555; border: none;");
    } else {
        separator->setStyleSheet("background-color: #CCCCCC; border: none;");
    }
    m_layout->addWidget(separator, 0, Qt::AlignHCenter);
    m_separator = separator;
    
    // === Layer ordering operations ===
    
    // Create Bring Forward button
    m_forwardButton = new ActionBarButton(this);
    m_forwardButton->setIconName("up_arrow");
    m_forwardButton->setToolTip(tr("Bring Forward (Ctrl+])"));
    addButton(m_forwardButton);
    connect(m_forwardButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::bringForwardRequested);
    
    // Create Send Backward button
    m_backwardButton = new ActionBarButton(this);
    m_backwardButton->setIconName("down_arrow");
    m_backwardButton->setToolTip(tr("Send Backward (Ctrl+[)"));
    addButton(m_backwardButton);
    connect(m_backwardButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::sendBackwardRequested);
    
    // Create Increase Affinity button
    m_increaseAffinityButton = new ActionBarButton(this);
    m_increaseAffinityButton->setIconName("layer_uparrow");
    m_increaseAffinityButton->setToolTip(tr("Increase Affinity (Alt+])"));
    addButton(m_increaseAffinityButton);
    connect(m_increaseAffinityButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::increaseAffinityRequested);
    
    // Create Decrease Affinity button
    m_decreaseAffinityButton = new ActionBarButton(this);
    m_decreaseAffinityButton->setIconName("layer_downarrow");
    m_decreaseAffinityButton->setToolTip(tr("Decrease Affinity (Alt+[)"));
    addButton(m_decreaseAffinityButton);
    connect(m_decreaseAffinityButton, &ActionBarButton::clicked, this, &ObjectSelectActionBar::decreaseAffinityRequested);
}

void ObjectSelectActionBar::updateButtonStates()
{
    // Copy, Delete, and layer ordering buttons: visible only when selection exists
    if (m_copyButton) {
        m_copyButton->setVisible(m_hasSelection);
    }
    if (m_deleteButton) {
        m_deleteButton->setVisible(m_hasSelection);
    }
    if (m_separator) {
        m_separator->setVisible(m_hasSelection);
    }
    if (m_forwardButton) {
        m_forwardButton->setVisible(m_hasSelection);
    }
    if (m_backwardButton) {
        m_backwardButton->setVisible(m_hasSelection);
    }
    if (m_increaseAffinityButton) {
        m_increaseAffinityButton->setVisible(m_hasSelection);
    }
    if (m_decreaseAffinityButton) {
        m_decreaseAffinityButton->setVisible(m_hasSelection);
    }
    
    // Paste button: visible when clipboard has object
    if (m_pasteButton) {
        m_pasteButton->setVisible(m_hasObjectInClipboard);
    }
    
    // Cancel button: visible when clipboard has content and no selection (paste-only mode)
    // This allows dismissing the action bar without keyboard
    if (m_cancelButton) {
        m_cancelButton->setVisible(m_hasObjectInClipboard && !m_hasSelection);
    }
    
    // Trigger re-layout to adjust height
    adjustSize();
    updateGeometry();
}

void ObjectSelectActionBar::setHasObjectInClipboard(bool hasObject)
{
    if (m_hasObjectInClipboard != hasObject) {
        m_hasObjectInClipboard = hasObject;
        updateButtonStates();
    }
}

void ObjectSelectActionBar::setHasSelection(bool hasSelection)
{
    if (m_hasSelection != hasSelection) {
        m_hasSelection = hasSelection;
        updateButtonStates();
    }
}

void ObjectSelectActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation (updates background, shadow, separators)
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to all buttons
    if (m_copyButton) {
        m_copyButton->setDarkMode(darkMode);
    }
    if (m_pasteButton) {
        m_pasteButton->setDarkMode(darkMode);
    }
    if (m_deleteButton) {
        m_deleteButton->setDarkMode(darkMode);
    }
    if (m_forwardButton) {
        m_forwardButton->setDarkMode(darkMode);
    }
    if (m_backwardButton) {
        m_backwardButton->setDarkMode(darkMode);
    }
    if (m_increaseAffinityButton) {
        m_increaseAffinityButton->setDarkMode(darkMode);
    }
    if (m_decreaseAffinityButton) {
        m_decreaseAffinityButton->setDarkMode(darkMode);
    }
    if (m_cancelButton) {
        m_cancelButton->setDarkMode(darkMode);
    }
}

