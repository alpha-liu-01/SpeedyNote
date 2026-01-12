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
    m_pageUpButton->setIconName("pageUp");
    m_pageUpButton->setToolTip(tr("Previous Page (Page Up)"));
    addButton(m_pageUpButton);
    
    // Page wheel picker
    m_wheelPicker = new PageWheelPicker(this);
    m_wheelPicker->setToolTip(tr("Drag to scroll through pages"));
    addButton(m_wheelPicker);
    
    // Page Down button
    m_pageDownButton = new ActionBarButton(this);
    m_pageDownButton->setIconName("pageDown");
    m_pageDownButton->setToolTip(tr("Next Page (Page Down)"));
    addButton(m_pageDownButton);
    
    // Separator between navigation and management
    addSeparator();
    
    // === Page Management Section ===
    
    // Add Page button
    m_addPageButton = new ActionBarButton(this);
    m_addPageButton->setIconName("addPage");
    m_addPageButton->setToolTip(tr("Add Page at End"));
    addButton(m_addPageButton);
    
    // Insert Page button
    m_insertPageButton = new ActionBarButton(this);
    m_insertPageButton->setIconName("insertPage");
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
    
    connect(m_wheelPicker, &PageWheelPicker::currentPageChanged,
            this, &PagePanelActionBar::onWheelPageChanged);
    
    // Page management signals
    connect(m_addPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::addPageClicked);
    
    connect(m_insertPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::insertPageClicked);
    
    // Delete button signals (3-way: request, confirm, undo)
    connect(m_deleteButton, &UndoDeleteButton::deleteRequested,
            this, &PagePanelActionBar::onDeleteRequested);
    
    connect(m_deleteButton, &UndoDeleteButton::deleteConfirmed,
            this, &PagePanelActionBar::onDeleteConfirmedInternal);
    
    connect(m_deleteButton, &UndoDeleteButton::undoRequested,
            this, &PagePanelActionBar::onUndoRequested);
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

void PagePanelActionBar::updateButtonStates()
{
    // Page Up: disabled on first page
    if (m_pageUpButton) {
        m_pageUpButton->setEnabled(m_currentPage > 0);
    }
    
    // Page Down: disabled on last page
    if (m_pageDownButton) {
        m_pageDownButton->setEnabled(m_currentPage < m_pageCount - 1);
    }
    
    // Add/Insert: always enabled (documents can always have more pages)
    if (m_addPageButton) {
        m_addPageButton->setEnabled(true);
    }
    if (m_insertPageButton) {
        m_insertPageButton->setEnabled(true);
    }
    
    // Delete: disabled when only 1 page remains
    if (m_deleteButton) {
        m_deleteButton->setEnabled(m_pageCount > 1);
    }
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

void PagePanelActionBar::onDeleteRequested()
{
    // Forward the delete request
    emit deletePageClicked();
}

void PagePanelActionBar::onDeleteConfirmedInternal()
{
    // Forward the delete confirmation
    emit deleteConfirmed();
}

void PagePanelActionBar::onUndoRequested()
{
    // Forward the undo request
    emit undoDeleteClicked();
}

