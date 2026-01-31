#ifndef TIMELINELISTVIEW_H
#define TIMELINELISTVIEW_H

#include "KineticListView.h"

#include <QSet>

class TimelineModel;

/**
 * @brief List view for Timeline items with kinetic scrolling and long-press support.
 * 
 * Inherits from KineticListView for kinetic scrolling and long-press detection.
 * Handles:
 * - Section headers (Today, Yesterday, etc.) - not clickable for menus
 * - Notebook cards with 3-dot menu button detection
 * - Batch select mode (L-007) for bulk operations
 */
class TimelineListView : public KineticListView {
    Q_OBJECT

public:
    explicit TimelineListView(QWidget* parent = nullptr);
    
    /**
     * @brief Set the TimelineModel for this view.
     * Needed for select mode state synchronization.
     */
    void setTimelineModel(TimelineModel* model);
    
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
     */
    void enterSelectMode(const QString& firstSelection);
    
    /**
     * @brief Exit batch select mode, clearing all selections.
     */
    void exitSelectMode();
    
    /**
     * @brief Toggle selection state of a notebook.
     * @param bundlePath The bundle path of the notebook to toggle.
     */
    void toggleSelection(const QString& bundlePath);
    
    /**
     * @brief Select all visible notebook cards (not section headers).
     */
    void selectAll() override;
    
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
     */
    bool isSelected(const QString& bundlePath) const;
    
signals:
    /**
     * @brief Emitted when the 3-dot menu button on a notebook card is clicked.
     * Only emitted when NOT in select mode.
     */
    void menuRequested(const QModelIndex& index, const QPoint& globalPos);
    
    /**
     * @brief Emitted when user long-presses on an item (enters select mode).
     */
    void longPressed(const QModelIndex& index, const QPoint& globalPos);
    
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
    bool isSectionHeader(const QModelIndex& index) const;
    QString bundlePathForIndex(const QModelIndex& index) const;
    bool isOnMenuButton(const QModelIndex& index, const QPoint& pos) const;
    
    TimelineModel* m_timelineModel = nullptr;
    
    // Batch select mode state (L-007)
    bool m_selectMode = false;
    QSet<QString> m_selectedBundlePaths;
};

#endif // TIMELINELISTVIEW_H
