#ifndef TIMELINELISTVIEW_H
#define TIMELINELISTVIEW_H

#include "KineticListView.h"

/**
 * @brief List view for Timeline items with kinetic scrolling and long-press support.
 * 
 * Inherits from KineticListView for kinetic scrolling and long-press detection.
 * Handles:
 * - Section headers (Today, Yesterday, etc.) - not clickable for menus
 * - Notebook cards with 3-dot menu button detection
 * - Long-press for future batch select mode (L-007)
 */
class TimelineListView : public KineticListView {
    Q_OBJECT

public:
    explicit TimelineListView(QWidget* parent = nullptr);
    
signals:
    /**
     * @brief Emitted when the 3-dot menu button on a notebook card is clicked.
     * @param index The model index of the notebook.
     * @param globalPos The global position for context menu placement.
     */
    void menuRequested(const QModelIndex& index, const QPoint& globalPos);
    
    /**
     * @brief Emitted when user long-presses on an item.
     * @param index The model index of the long-pressed item.
     * @param globalPos The global position where the long-press occurred.
     */
    void longPressed(const QModelIndex& index, const QPoint& globalPos);

protected:
    void handleItemTap(const QModelIndex& index, const QPoint& pos) override;
    void handleRightClick(const QModelIndex& index, const QPoint& globalPos) override;
    void handleLongPress(const QModelIndex& index, const QPoint& globalPos) override;

private:
    /**
     * @brief Check if an index is a section header.
     */
    bool isSectionHeader(const QModelIndex& index) const;
    
    /**
     * @brief Check if a point is within the menu button area of a notebook card.
     */
    bool isOnMenuButton(const QModelIndex& index, const QPoint& pos) const;
};

#endif // TIMELINELISTVIEW_H
