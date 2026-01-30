#ifndef STARREDLISTVIEW_H
#define STARREDLISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>

class KineticScrollHelper;
class StarredModel;

/**
 * @brief A QListView subclass for displaying starred notebooks with folders.
 * 
 * StarredListView provides:
 * 1. Handling of two item types: folder headers and notebook cards
 * 2. Long-press detection for context menus (right-click also supported)
 * 3. Manual kinetic scrolling for reliable touch scrolling
 * 4. Proper tap/scroll discrimination for touch input
 * 5. Folder toggle on click (expand/collapse)
 * 
 * Works with StarredModel, NotebookCardDelegate, and FolderHeaderDelegate.
 * The view uses ItemTypeRole to distinguish between folder headers and
 * notebook cards for appropriate signal emission.
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class StarredListView : public QListView {
    Q_OBJECT

public:
    explicit StarredListView(QWidget* parent = nullptr);
    
    /**
     * @brief Set the StarredModel for this view.
     * 
     * This is needed for folder toggle functionality.
     * @param model The StarredModel to use.
     */
    void setStarredModel(StarredModel* model);
    
signals:
    /**
     * @brief Emitted when a notebook card is clicked/tapped.
     * @param bundlePath The bundle path of the clicked notebook.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when a notebook card is long-pressed or right-clicked.
     * @param bundlePath The bundle path of the notebook.
     * @param globalPos The global position for context menu placement.
     */
    void notebookLongPressed(const QString& bundlePath, const QPoint& globalPos);
    
    /**
     * @brief Emitted when a folder header is clicked/tapped.
     * 
     * The view automatically toggles the folder's collapsed state.
     * @param folderName The name of the clicked folder.
     */
    void folderClicked(const QString& folderName);
    
    /**
     * @brief Emitted when a folder header is long-pressed or right-clicked.
     * @param folderName The name of the folder.
     * @param globalPos The global position for context menu placement.
     */
    void folderLongPressed(const QString& folderName, const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onLongPressTimeout();

private:
    /**
     * @brief Check if an index is a folder header.
     * @param index The model index to check.
     * @return True if folder header, false if notebook card.
     */
    bool isFolderHeader(const QModelIndex& index) const;
    
    /**
     * @brief Get the folder name for an index (folder headers only).
     * @param index The model index.
     * @return The folder name, or empty string if not a folder header.
     */
    QString folderNameForIndex(const QModelIndex& index) const;
    
    /**
     * @brief Get the bundle path for an index (notebook cards only).
     * @param index The model index.
     * @return The bundle path, or empty string if not a notebook card.
     */
    QString bundlePathForIndex(const QModelIndex& index) const;
    
    // Reference to model for folder toggling
    StarredModel* m_starredModel = nullptr;
    
    // Long-press detection
    QTimer m_longPressTimer;
    QPoint m_pressPos;              // Position where press started (viewport coords)
    QModelIndex m_pressedIndex;     // Index that was pressed
    bool m_longPressTriggered = false;
    
    // Touch scrolling state
    bool m_touchScrolling = false;
    int m_scrollStartValue = 0;
    
    // Kinetic scrolling (uses shared helper)
    KineticScrollHelper* m_kineticHelper = nullptr;
    
    // Constants
    static constexpr int LONG_PRESS_MS = 500;           // Time to trigger long-press
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 20; // Max movement in pixels
};

#endif // STARREDLISTVIEW_H
