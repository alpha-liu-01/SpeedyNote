#ifndef STARREDVIEW_H
#define STARREDVIEW_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>

class StarredListView;
class StarredModel;
class NotebookCardDelegate;
class FolderHeaderDelegate;
class QStackedWidget;

/**
 * @brief iOS homescreen-style view for starred notebooks with folders.
 * 
 * StarredView displays starred notebooks organized in folders with an
 * "Unfiled" section for notebooks not assigned to any folder.
 * 
 * Features:
 * - Collapsible folder sections
 * - Virtualized list of folder headers and notebook cards (Model/View)
 * - Long-press folder header for context menu (rename/delete)
 * - Touch-friendly scrolling with kinetic momentum
 * - Dark mode support
 * - Smart reload (skips rebuild if only metadata changed)
 * 
 * Folder structure (per Q&A):
 * - Single-level folders (no nesting)
 * - Each notebook in one folder or "unfiled"
 * - Drag-and-drop reordering (future task)
 * 
 * Phase P.3: Refactored to use Model/View for virtualization and performance.
 */
class StarredView : public QWidget {
    Q_OBJECT

public:
    explicit StarredView(QWidget* parent = nullptr);
    
    /**
     * @brief Reload data from NotebookLibrary.
     * Uses smart reload - skips rebuild if only metadata changed.
     */
    void reload();
    
    /**
     * @brief Set dark mode for theming.
     */
    void setDarkMode(bool dark);

protected:
    void showEvent(QShowEvent* event) override;

signals:
    /**
     * @brief Emitted when a notebook card is clicked.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when the 3-dot menu button on a notebook card is clicked,
     * or when a notebook card is right-clicked or long-pressed.
     * 
     * This is the signal for showing the single-item context menu.
     */
    void notebookMenuRequested(const QString& bundlePath);
    
    /**
     * @brief Emitted when a notebook card is long-pressed.
     * @deprecated Use notebookMenuRequested for context menu.
     * This signal will be repurposed for batch select mode (L-007).
     */
    void notebookLongPressed(const QString& bundlePath);
    
    /**
     * @brief Emitted when a folder header is long-pressed or right-clicked.
     */
    void folderLongPressed(const QString& folderName);

private slots:
    // Slots for list view signals
    void onNotebookClicked(const QString& bundlePath);
    void onNotebookMenuRequested(const QString& bundlePath, const QPoint& globalPos);
    void onNotebookLongPressed(const QString& bundlePath, const QPoint& globalPos);
    void onFolderClicked(const QString& folderName);
    void onFolderLongPressed(const QString& folderName, const QPoint& globalPos);
    
    // Slots for select mode (L-007)
    void onSelectModeChanged(bool active);
    void onBatchSelectionChanged(int count);

private:
    void setupUi();
    void setupSelectModeHeader();
    void updateEmptyState();
    
    // -------------------------------------------------------------------------
    // Batch Select Mode (L-007)
    // -------------------------------------------------------------------------
    
    /**
     * @brief Show the select mode header with the given count.
     * @param count Number of selected items.
     */
    void showSelectModeHeader(int count);
    
    /**
     * @brief Hide the select mode header and show normal view.
     */
    void hideSelectModeHeader();
    
    /**
     * @brief Show the overflow menu with batch actions.
     */
    void showOverflowMenu();
    
    // Model/View components
    StarredListView* m_listView = nullptr;
    StarredModel* m_model = nullptr;
    NotebookCardDelegate* m_cardDelegate = nullptr;
    FolderHeaderDelegate* m_folderDelegate = nullptr;
    
    // Empty state
    QLabel* m_emptyLabel = nullptr;
    
    // Select mode header (L-007)
    QWidget* m_selectModeHeader = nullptr;      // Header bar for select mode
    QLabel* m_selectionCountLabel = nullptr;    // Shows "X selected"
    QPushButton* m_backButton = nullptr;        // ← back arrow to exit
    QPushButton* m_overflowMenuButton = nullptr; // ⋮ overflow menu
    
    bool m_darkMode = false;
    bool m_needsReload = false;  // Deferred reload flag for when view becomes visible
    
    // Layout constants
    static constexpr int CONTENT_MARGIN = 16;
    static constexpr int HEADER_HEIGHT = 48;
};

#endif // STARREDVIEW_H
