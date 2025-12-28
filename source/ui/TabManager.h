#pragma once

// ============================================================================
// TabManager - Thin wrapper for QTabWidget tab operations
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.2)
//
// TabManager encapsulates tab-related operations for code organization.
// It does NOT own the QTabWidget (MainWindow owns it).
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
#include <QTabWidget>
#include <QVector>

class Document;
class DocumentViewport;

/**
 * @brief Thin wrapper around QTabWidget for tab operations.
 * 
 * TabManager manages the relationship between tabs and DocumentViewports.
 * It creates viewports when tabs are opened and deletes them when closed.
 */
class TabManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor.
     * @param tabWidget The QTabWidget to manage (not owned).
     * @param parent Parent QObject.
     */
    explicit TabManager(QTabWidget* tabWidget, QObject* parent = nullptr);
    
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
     * Creates a DocumentViewport, sets its document, and adds it to the tab widget.
     * The viewport is owned by TabManager and will be deleted when the tab closes.
     */
    int createTab(Document* doc, const QString& title);

    /**
     * @brief Close a tab by index.
     * @param index The tab index to close.
     * 
     * Removes the tab from the widget and deletes the DocumentViewport.
     * Does NOT delete the Document - that's DocumentManager's responsibility.
     * Emits tabCloseRequested before closing.
     */
    void closeTab(int index);

    /**
     * @brief Close the currently active tab.
     */
    void closeCurrentTab();

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
     * @brief Handle QTabWidget::currentChanged signal.
     */
    void onCurrentChanged(int index);

    /**
     * @brief Handle QTabWidget::tabCloseRequested signal.
     */
    void onTabCloseRequested(int index);

private:
    QTabWidget* m_tabWidget;                    // Not owned - MainWindow owns
    QVector<DocumentViewport*> m_viewports;    // Owned - created by createTab()
    QVector<QString> m_baseTitles;             // Base titles (without * prefix)
    QVector<bool> m_modifiedFlags;             // Track modified state per tab
};
