#ifndef SEARCHLISTVIEW_H
#define SEARCHLISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>

class KineticScrollHelper;

/**
 * @brief A QListView subclass for displaying search results with touch support.
 * 
 * SearchListView provides:
 * 1. Long-press detection for context menus (right-click also supported)
 * 2. Manual kinetic scrolling for reliable touch scrolling
 * 3. Proper tap/scroll discrimination for touch input
 * 
 * This is a simplified version of TimelineListView, without section header
 * handling (search results are a flat list of notebook cards).
 * 
 * Works with SearchModel and NotebookCardDelegate.
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class SearchListView : public QListView {
    Q_OBJECT

public:
    explicit SearchListView(QWidget* parent = nullptr);
    
signals:
    /**
     * @brief Emitted when a notebook is clicked/tapped.
     * @param bundlePath The bundle path of the clicked notebook.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when a notebook is long-pressed or right-clicked.
     * @param bundlePath The bundle path of the notebook.
     * @param globalPos The global position for context menu placement.
     */
    void notebookLongPressed(const QString& bundlePath, const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onLongPressTimeout();

private:
    /**
     * @brief Get the bundle path for an index from the model.
     * @param index The model index.
     * @return The bundle path, or empty string if invalid.
     */
    QString bundlePathForIndex(const QModelIndex& index) const;
    
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

#endif // SEARCHLISTVIEW_H
