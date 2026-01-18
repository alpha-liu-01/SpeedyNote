#include "PagePanelActionBar.h"
#include "../widgets/ActionBarButton.h"
#include "../widgets/PageWheelPicker.h"
#include "../widgets/UndoDeleteButton.h"

PagePanelActionBar::PagePanelActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupUI();
    setupConnections();
    updateButtonStates();
}

void PagePanelActionBar::setupUI()
{
    // === Navigation Section ===
    
    // Page Up button
    m_pageUpButton = new ActionBarButton(this);
    m_pageUpButton->setIconName("up_arrow");
    m_pageUpButton->setToolTip(tr("Previous Page (Page Up)"));
    addButton(m_pageUpButton);
    
    // Page wheel picker
    m_wheelPicker = new PageWheelPicker(this);
    m_wheelPicker->setToolTip(tr("Drag to scroll through pages"));
    addButton(m_wheelPicker);
    
    // Page Down button
    m_pageDownButton = new ActionBarButton(this);
    m_pageDownButton->setIconName("down_arrow");
    m_pageDownButton->setToolTip(tr("Next Page (Page Down)"));
    addButton(m_pageDownButton);
    
    // Layout toggle button (1-column / Auto) - right below page down
    m_layoutToggleButton = new ActionBarButton(this);
    m_layoutToggleButton->setText("1");  // Default: 1-column mode
    m_layoutToggleButton->setToolTip(tr("Toggle Column Layout (Ctrl+2)\n1 = Single Column\nA = Auto 1/2 Columns"));
    addButton(m_layoutToggleButton);
    
    // Separator between navigation and management
    addSeparator();
    
    // === Page Management Section ===
    
    // Add Page button
    m_addPageButton = new ActionBarButton(this);
    m_addPageButton->setIconName("addtab");
    m_addPageButton->setToolTip(tr("Add Page at End"));
    addButton(m_addPageButton);
    
    // Insert Page button
    m_insertPageButton = new ActionBarButton(this);
    m_insertPageButton->setIconName("import");
    m_insertPageButton->setToolTip(tr("Insert Page After Current"));
    addButton(m_insertPageButton);
    
    // Delete Page button (with undo support)
    m_deleteButton = new UndoDeleteButton(this);
    m_deleteButton->setToolTip(tr("Delete Current Page"));
    addButton(m_deleteButton);
}

void PagePanelActionBar::setupConnections()
{
    // Navigation signals
    connect(m_pageUpButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::pageUpClicked);
    
    connect(m_pageDownButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::pageDownClicked);
    
    connect(m_layoutToggleButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::layoutToggleClicked);
    
    connect(m_wheelPicker, &PageWheelPicker::currentPageChanged,
            this, &PagePanelActionBar::onWheelPageChanged);
    
    // Page management signals
    connect(m_addPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::addPageClicked);
    
    connect(m_insertPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::insertPageClicked);
    
    // Delete button signals (3-way: request, confirm, undo)
    // Use direct signal-to-signal connections to avoid trivial wrapper slots
    connect(m_deleteButton, &UndoDeleteButton::deleteRequested,
            this, &PagePanelActionBar::deletePageClicked);
    
    connect(m_deleteButton, &UndoDeleteButton::deleteConfirmed,
            this, &PagePanelActionBar::deleteConfirmed);
    
    connect(m_deleteButton, &UndoDeleteButton::undoRequested,
            this, &PagePanelActionBar::undoDeleteClicked);
}

void PagePanelActionBar::setCurrentPage(int page)
{
    if (m_currentPage != page) {
        m_currentPage = page;
        
        // Update wheel picker without triggering signals
        m_wheelPicker->blockSignals(true);
        m_wheelPicker->setCurrentPage(page);
        m_wheelPicker->blockSignals(false);
        
        updateButtonStates();
    }
}

void PagePanelActionBar::setPageCount(int count)
{
    if (m_pageCount != count && count > 0) {
        m_pageCount = count;
        
        // Update wheel picker
        m_wheelPicker->setPageCount(count);
        
        // Clamp current page if necessary
        if (m_currentPage >= m_pageCount) {
            m_currentPage = m_pageCount - 1;
            m_wheelPicker->blockSignals(true);
            m_wheelPicker->setCurrentPage(m_currentPage);
            m_wheelPicker->blockSignals(false);
        }
        
        updateButtonStates();
    }
}

void PagePanelActionBar::setAutoLayoutEnabled(bool enabled)
{
    if (m_autoLayoutEnabled != enabled) {
        m_autoLayoutEnabled = enabled;
        // Update button text: "A" for auto, "1" for single column
        m_layoutToggleButton->setText(enabled ? "A" : "1");
    }
}

void PagePanelActionBar::updateButtonStates()
{
    // Page Up: disabled on first page
    m_pageUpButton->setEnabled(m_currentPage > 0);
    
    // Page Down: disabled on last page
    m_pageDownButton->setEnabled(m_currentPage < m_pageCount - 1);
    
    // Delete: disabled when only 1 page remains
    m_deleteButton->setEnabled(m_pageCount > 1);
    
    // Note: Add/Insert and Layout toggle are always enabled, no state updates needed
}

void PagePanelActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to all child widgets
    if (m_pageUpButton) {
        m_pageUpButton->setDarkMode(darkMode);
    }
    if (m_pageDownButton) {
        m_pageDownButton->setDarkMode(darkMode);
    }
    if (m_wheelPicker) {
        m_wheelPicker->setDarkMode(darkMode);
    }
    if (m_addPageButton) {
        m_addPageButton->setDarkMode(darkMode);
    }
    if (m_insertPageButton) {
        m_insertPageButton->setDarkMode(darkMode);
    }
    if (m_deleteButton) {
        m_deleteButton->setDarkMode(darkMode);
    }
    if (m_layoutToggleButton) {
        m_layoutToggleButton->setDarkMode(darkMode);
    }
}

void PagePanelActionBar::resetDeleteButton()
{
    if (m_deleteButton) {
        m_deleteButton->reset();
    }
}

void PagePanelActionBar::confirmDelete()
{
    if (m_deleteButton) {
        m_deleteButton->confirmDelete();
    }
}

// ============================================================================
// Private Slots
// ============================================================================

void PagePanelActionBar::onWheelPageChanged(int page)
{
    if (page != m_currentPage) {
        m_currentPage = page;
        updateButtonStates();
        emit pageSelected(page);
    }
}

