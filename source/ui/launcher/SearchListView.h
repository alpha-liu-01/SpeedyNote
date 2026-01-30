#ifndef SEARCHLISTVIEW_H
#define SEARCHLISTVIEW_H

#include "KineticListView.h"

/**
 * @brief List view for search results with kinetic scrolling and long-press support.
 * 
 * Inherits from KineticListView for kinetic scrolling and long-press detection.
 * Handles:
 * - Notebook cards with 3-dot menu button detection
 * - Long-press shows context menu (no batch select in search view)
 * 
 * Works with SearchModel and NotebookCardDelegate.
 */
class SearchListView : public KineticListView {
    Q_OBJECT

public:
    explicit SearchListView(QWidget* parent = nullptr);
    
signals:
    /**
     * @brief Emitted when a notebook is clicked/tapped (not on menu button).
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when the 3-dot menu button or right-click on a notebook.
     */
    void notebookMenuRequested(const QString& bundlePath, const QPoint& globalPos);

protected:
    void handleItemTap(const QModelIndex& index, const QPoint& pos) override;
    void handleRightClick(const QModelIndex& index, const QPoint& globalPos) override;
    void handleLongPress(const QModelIndex& index, const QPoint& globalPos) override;

private:
    QString bundlePathForIndex(const QModelIndex& index) const;
    bool isOnMenuButton(const QModelIndex& index, const QPoint& pos) const;
};

#endif // SEARCHLISTVIEW_H
