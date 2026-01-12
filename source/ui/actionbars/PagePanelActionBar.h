#ifndef PAGEPANELACTIONBAR_H
#define PAGEPANELACTIONBAR_H

#include "ActionBar.h"

class ActionBarButton;
class PageWheelPicker;
class UndoDeleteButton;

/**
 * @brief Action bar for page panel navigation and management.
 * 
 * Provides controls for navigating between pages and managing pages
 * (add, insert, delete). Appears in the left column of the ActionBarContainer
 * when the Page Panel sidebar tab is active.
 * 
 * Layout (top to bottom):
 * - [Page Up]        - Navigate to previous page
 * - [Wheel Picker]   - iPhone-style page number scroll picker
 * - [Page Down]      - Navigate to next page
 * - ──────────────── - Separator
 * - [Add Page]       - Add a new page at the end
 * - [Insert Page]    - Insert a new page after current
 * - [Delete Page]    - Delete current page (with undo support)
 * 
 * This action bar is always visible when the Page Panel tab is shown.
 * Unlike context-sensitive action bars, it doesn't depend on selection state.
 */
class PagePanelActionBar : public ActionBar {
    Q_OBJECT

public:
    explicit PagePanelActionBar(QWidget* parent = nullptr);
    
    /**
     * @brief Set the current page index (0-based).
     * @param page Current page index.
     * 
     * Updates the wheel picker and button enabled states.
     */
    void setCurrentPage(int page);
    
    /**
     * @brief Set the total page count.
     * @param count Number of pages in the document.
     * 
     * Updates the wheel picker and button enabled states.
     */
    void setPageCount(int count);
    
    /**
     * @brief Update button enabled states based on current page/count.
     * 
     * Called automatically when page or count changes.
     */
    void updateButtonStates() override;
    
    /**
     * @brief Set dark mode and update all child widgets.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode) override;
    
    /**
     * @brief Reset the delete button to normal state.
     * 
     * Call this when the delete operation is cancelled externally.
     */
    void resetDeleteButton();
    
    /**
     * @brief Confirm the pending delete operation.
     * 
     * Call this when the delete has been committed (e.g., after undo timeout).
     */
    void confirmDelete();

signals:
    /**
     * @brief Emitted when Page Up button is clicked.
     */
    void pageUpClicked();
    
    /**
     * @brief Emitted when Page Down button is clicked.
     */
    void pageDownClicked();
    
    /**
     * @brief Emitted when a page is selected via the wheel picker.
     * @param page The selected page index (0-based).
     */
    void pageSelected(int page);
    
    /**
     * @brief Emitted when Add Page button is clicked.
     */
    void addPageClicked();
    
    /**
     * @brief Emitted when Insert Page button is clicked.
     */
    void insertPageClicked();
    
    /**
     * @brief Emitted when Delete is first clicked (delete requested).
     * 
     * The caller should perform a soft delete (keep data for undo).
     */
    void deletePageClicked();
    
    /**
     * @brief Emitted when delete is confirmed (after timeout or external confirmation).
     * 
     * The caller can now permanently discard the deleted page data.
     */
    void deleteConfirmed();
    
    /**
     * @brief Emitted when Undo is clicked within the timeout period.
     * 
     * The caller should restore the deleted page.
     */
    void undoDeleteClicked();

private slots:
    void onWheelPageChanged(int page);
    void onDeleteRequested();
    void onDeleteConfirmedInternal();
    void onUndoRequested();

private:
    void setupUI();
    void setupConnections();

    // Navigation buttons
    ActionBarButton* m_pageUpButton = nullptr;
    PageWheelPicker* m_wheelPicker = nullptr;
    ActionBarButton* m_pageDownButton = nullptr;
    
    // Page management buttons
    ActionBarButton* m_addPageButton = nullptr;
    ActionBarButton* m_insertPageButton = nullptr;
    UndoDeleteButton* m_deleteButton = nullptr;
    
    // State
    int m_currentPage = 0;
    int m_pageCount = 1;
};

#endif // PAGEPANELACTIONBAR_H

