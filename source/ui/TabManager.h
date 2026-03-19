#pragma once

// ============================================================================
// TabManager - Manages QTabBar + QStackedWidget for document tabs
// ============================================================================
// Part of the SpeedyNote document architecture (Phase C - Toolbar Extraction)
//
// TabManager encapsulates tab-related operations for code organization.
// It does NOT own the QTabBar or QStackedWidget (MainWindow owns them).
// It DOES own the DocumentViewport widgets it creates.
//
// Responsibilities:
// - Create tabs with DocumentViewport widgets
// - Close tabs (delete DocumentViewport, but NOT Document)
// - Track viewport ↔ tab index mapping
// - Emit signals for tab changes
// - Manage tab titles (including modified indicator)
//
// What TabManager does NOT do:
// - Own Documents (DocumentManager does that)
// - Make UI decisions (MainWindow does that)
// - Handle document save/load (DocumentManager does that)
// ============================================================================

#include <QObject>
#include <QTabBar>
#include <QStackedWidget>
#include <QVector>

class Document;
class DocumentViewport;

/**
 * @brief Manages QTabBar + QStackedWidget for document tabs.
 * 
 * TabManager manages the relationship between tabs and DocumentViewports.
 * It creates viewports when tabs are opened and deletes them when closed.
 */
class TabManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor.
     * @param tabBar The QTabBar to manage (not owned).
     * @param viewportStack The QStackedWidget for viewports (not owned).
     * @param parent Parent QObject.
     */
    explicit TabManager(QTabBar* tabBar, QStackedWidget* viewportStack, QObject* parent = nullptr);
    
    /**
     * @brief Destructor.
     * Deletes all owned DocumentViewport widgets.
     */
    ~TabManager() override;

    // =========================================================================
    // Tab Operations
    // =========================================================================

    /**
     * @brief Create a new tab with a DocumentViewport.
     * @param doc The Document to display (not owned, just referenced).
     * @param title The tab title.
     * @return The index of the new tab.
     * 
     * Creates a DocumentViewport, sets its document, and adds it to both
     * the tab bar and viewport stack.
     * The viewport is owned by TabManager and will be deleted when the tab closes.
     */
    int createTab(Document* doc, const QString& title);

    /**
     * @brief Close a tab by index.
     * @param index The tab index to close.
     * 
     * Removes the tab from both tab bar and viewport stack, deletes the viewport.
     * Does NOT delete the Document - that's DocumentManager's responsibility.
     * Emits tabCloseRequested before closing.
     */
    void closeTab(int index);

    struct DetachedTab {
        DocumentViewport* viewport = nullptr;
        QString title;
        bool modified = false;
        int tabId = -1;
    };

    /**
     * @brief Detach a tab without deleting the viewport.
     * @param index The tab index to detach.
     * @return Struct with the viewport, title, modified flag, and tab ID.
     *         viewport is nullptr on failure.
     *
     * Removes the viewport from tracking, tab bar, and viewport stack,
     * but does NOT delete it. Caller takes ownership.
     * Used for moving tabs between split-view panes.
     */
    DetachedTab detachTab(int index);

    /**
     * @brief Attach an externally-owned viewport as a new tab.
     * @param viewport The viewport to adopt (takes ownership).
     * @param title The tab title.
     * @param modified Whether the tab should be marked as modified.
     * @param tabId Reuse a specific tab ID (-1 to generate a new one).
     * @return The index of the new tab.
     *
     * Used for receiving tabs moved from another pane in split view.
     */
    int attachTab(DocumentViewport* viewport, const QString& title,
                  bool modified = false, int tabId = -1);

    /**
     * @brief Close the currently active tab.
     */
    void closeCurrentTab();

    /**
     * @brief Switch to the next tab (wraps around to first if at last).
     */
    void switchToNextTab();

    /**
     * @brief Switch to the previous tab (wraps around to last if at first).
     */
    void switchToPrevTab();

    // =========================================================================
    // Access
    // =========================================================================

    /**
     * @brief Get the viewport of the current tab.
     * @return Pointer to the current DocumentViewport, or nullptr if no tabs.
     */
    DocumentViewport* currentViewport() const;

    /**
     * @brief Get the viewport at a specific tab index.
     * @param index The tab index.
     * @return Pointer to the DocumentViewport, or nullptr if index invalid.
     */
    DocumentViewport* viewportAt(int index) const;

    /**
     * @brief Get the document displayed in a tab.
     * @param index The tab index.
     * @return Pointer to the Document, or nullptr if index invalid.
     * 
     * Convenience method - equivalent to viewportAt(index)->document().
     */
    Document* documentAt(int index) const;

    /**
     * @brief Get the current tab index.
     * @return Current index, or -1 if no tabs.
     */
    int currentIndex() const;

    /**
     * @brief Get the number of open tabs.
     */
    int tabCount() const;

    /**
     * @brief Get the unique tab ID at a specific index.
     * @param index The tab index.
     * @return The unique tab ID, or -1 if index invalid.
     */
    int tabIdAt(int index) const;

    /**
     * @brief Get the unique tab ID of the current tab.
     * @return The unique tab ID, or -1 if no tabs.
     */
    int currentTabId() const;

    // =========================================================================
    // Title Management
    // =========================================================================

    /**
     * @brief Set the title of a tab.
     * @param index The tab index.
     * @param title The new title.
     */
    void setTabTitle(int index, const QString& title);

    /**
     * @brief Mark a tab as modified or unmodified.
     * @param index The tab index.
     * @param modified If true, prepends "* " to the title.
     * 
     * Uses internal tracking to avoid duplicate asterisks.
     */
    void markTabModified(int index, bool modified);

    /**
     * @brief Get the base title (without modified indicator) of a tab.
     * @param index The tab index.
     * @return The base title, or empty string if index invalid.
     */
    QString tabTitle(int index) const;

signals:
    /**
     * @brief Emitted when the current tab changes.
     * @param viewport The new current viewport (may be nullptr if no tabs).
     */
    void currentViewportChanged(DocumentViewport* viewport);

    /**
     * @brief Emitted just before a tab is closed (notification only).
     * @param index The tab index being closed.
     * @param viewport The viewport being closed.
     * 
     * This is emitted from closeTab() just before the actual close.
     * MainWindow can use this to clean up Document via DocumentManager.
     */
    void tabCloseRequested(int index, DocumentViewport* viewport);
    
    /**
     * @brief Emitted when user attempts to close a tab (via X button).
     * @param index The tab index the user wants to close.
     * @param viewport The viewport the user wants to close.
     * 
     * MainWindow should connect to this to check for unsaved changes
     * and prompt the user before calling closeTab().
     * The tab is NOT automatically closed - MainWindow must call closeTab().
     */
    void tabCloseAttempted(int index, DocumentViewport* viewport);

private slots:
    /**
     * @brief Handle QTabBar::currentChanged signal.
     */
    void onCurrentChanged(int index);

    /**
     * @brief Handle QTabBar::tabCloseRequested signal.
     */
    void onTabCloseRequested(int index);

private:
    QTabBar* m_tabBar;                          // Not owned - MainWindow owns
    QStackedWidget* m_viewportStack;            // Not owned - MainWindow owns
    QVector<DocumentViewport*> m_viewports;    // Owned - created by createTab()
    QVector<QString> m_baseTitles;             // Base titles (without * prefix)
    QVector<bool> m_modifiedFlags;             // Track modified state per tab
    QVector<int> m_tabIds;                     // Unique IDs per tab (collision-safe across panes)
    static int s_nextTabId;                    // Shared across all TabManager instances
};
