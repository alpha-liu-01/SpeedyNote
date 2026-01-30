#ifndef STARREDLISTVIEW_H
#define STARREDLISTVIEW_H

#include "KineticListView.h"

#include <QSet>

class StarredModel;

/**
 * @brief List view for starred notebooks with folders, kinetic scrolling and long-press.
 * 
 * Inherits from KineticListView for kinetic scrolling and long-press detection.
 * Handles:
 * - Folder headers (expand/collapse on click, context menu on long-press)
 * - Notebook cards with 3-dot menu button detection
 * - Batch select mode (L-007) for bulk operations
 * 
 * Works with StarredModel, NotebookCardDelegate, and FolderHeaderDelegate.
 */
class StarredListView : public KineticListView {
    Q_OBJECT

public:
    explicit StarredListView(QWidget* parent = nullptr);
    
    /**
     * @brief Set the StarredModel for this view.
     * Needed for folder toggle functionality.
     */
    void setStarredModel(StarredModel* model);
    
    // -------------------------------------------------------------------------
    // Batch Select Mode (L-007)
    // -------------------------------------------------------------------------
    
    /**
     * @brief Check if batch select mode is active.
     */
    bool isSelectMode() const { return m_selectMode; }
    
    /**
     * @brief Enter batch select mode with the first item already selected.
     * @param firstSelection Bundle path of the first selected notebook.
     * 
     * Called when user long-presses a notebook card. Enters select mode
     * and marks that notebook as selected.
     */
    void enterSelectMode(const QString& firstSelection);
    
    /**
     * @brief Exit batch select mode, clearing all selections.
     */
    void exitSelectMode();
    
    /**
     * @brief Toggle selection state of a notebook.
     * @param bundlePath The bundle path of the notebook to toggle.
     * 
     * If not in select mode, does nothing.
     */
    void toggleSelection(const QString& bundlePath);
    
    /**
     * @brief Select all visible notebook cards (not folder headers).
     */
    void selectAll();
    
    /**
     * @brief Deselect all notebooks.
     */
    void deselectAll();
    
    /**
     * @brief Get list of selected notebook bundle paths.
     */
    QStringList selectedBundlePaths() const;
    
    /**
     * @brief Get the number of selected notebooks.
     */
    int selectionCount() const { return m_selectedBundlePaths.size(); }
    
    /**
     * @brief Check if a specific notebook is selected.
     * @param bundlePath The bundle path to check.
     * 
     * Used by delegates to determine if they should draw selection indicator.
     */
    bool isSelected(const QString& bundlePath) const;
    
signals:
    /**
     * @brief Emitted when a notebook card is clicked/tapped (not on menu button).
     * Only emitted when NOT in select mode.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when the 3-dot menu button or right-click on a notebook card.
     * Only emitted when NOT in select mode.
     */
    void notebookMenuRequested(const QString& bundlePath, const QPoint& globalPos);
    
    /**
     * @brief Emitted when a notebook card is long-pressed (enters select mode).
     */
    void notebookLongPressed(const QString& bundlePath, const QPoint& globalPos);
    
    /**
     * @brief Emitted when a folder header is clicked/tapped.
     */
    void folderClicked(const QString& folderName);
    
    /**
     * @brief Emitted when a folder header is long-pressed or right-clicked.
     */
    void folderLongPressed(const QString& folderName, const QPoint& globalPos);
    
    /**
     * @brief Emitted when select mode is entered or exited.
     * @param active True if select mode is now active.
     */
    void selectModeChanged(bool active);
    
    /**
     * @brief Emitted when the selection changes (items added/removed).
     * @param count The new number of selected items.
     */
    void batchSelectionChanged(int count);

protected:
    void handleItemTap(const QModelIndex& index, const QPoint& pos) override;
    void handleRightClick(const QModelIndex& index, const QPoint& globalPos) override;
    void handleLongPress(const QModelIndex& index, const QPoint& globalPos) override;

private:
    bool isFolderHeader(const QModelIndex& index) const;
    QString folderNameForIndex(const QModelIndex& index) const;
    QString bundlePathForIndex(const QModelIndex& index) const;
    bool isOnMenuButton(const QModelIndex& index, const QPoint& pos) const;
    
    StarredModel* m_starredModel = nullptr;
    
    // Batch select mode state (L-007)
    bool m_selectMode = false;
    QSet<QString> m_selectedBundlePaths;
};

#endif // STARREDLISTVIEW_H
