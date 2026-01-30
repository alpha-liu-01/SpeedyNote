#ifndef STARREDLISTVIEW_H
#define STARREDLISTVIEW_H

#include "KineticListView.h"

class StarredModel;

/**
 * @brief List view for starred notebooks with folders, kinetic scrolling and long-press.
 * 
 * Inherits from KineticListView for kinetic scrolling and long-press detection.
 * Handles:
 * - Folder headers (expand/collapse on click, context menu on long-press)
 * - Notebook cards with 3-dot menu button detection
 * - Long-press for future batch select mode (L-007)
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
    
signals:
    /**
     * @brief Emitted when a notebook card is clicked/tapped (not on menu button).
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when the 3-dot menu button or right-click on a notebook card.
     */
    void notebookMenuRequested(const QString& bundlePath, const QPoint& globalPos);
    
    /**
     * @brief Emitted when a notebook card is long-pressed.
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
};

#endif // STARREDLISTVIEW_H
