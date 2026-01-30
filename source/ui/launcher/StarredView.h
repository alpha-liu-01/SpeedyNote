#ifndef STARREDVIEW_H
#define STARREDVIEW_H

#include <QWidget>
#include <QLabel>

class StarredListView;
class StarredModel;
class NotebookCardDelegate;
class FolderHeaderDelegate;

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
     * @brief Emitted when a notebook card is long-pressed.
     */
    void notebookLongPressed(const QString& bundlePath);
    
    /**
     * @brief Emitted when a folder header is long-pressed.
     */
    void folderLongPressed(const QString& folderName);

private slots:
    // Slots for list view signals
    void onNotebookClicked(const QString& bundlePath);
    void onNotebookLongPressed(const QString& bundlePath, const QPoint& globalPos);
    void onFolderClicked(const QString& folderName);
    void onFolderLongPressed(const QString& folderName, const QPoint& globalPos);

private:
    void setupUi();
    void updateEmptyState();
    
    // Model/View components
    StarredListView* m_listView = nullptr;
    StarredModel* m_model = nullptr;
    NotebookCardDelegate* m_cardDelegate = nullptr;
    FolderHeaderDelegate* m_folderDelegate = nullptr;
    
    // Empty state
    QLabel* m_emptyLabel = nullptr;
    
    bool m_darkMode = false;
    bool m_needsReload = false;  // Deferred reload flag for when view becomes visible
    
    // Layout constants
    static constexpr int CONTENT_MARGIN = 16;
};

#endif // STARREDVIEW_H
