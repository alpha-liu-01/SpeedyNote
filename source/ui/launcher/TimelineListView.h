#ifndef TIMELINELISTVIEW_H
#define TIMELINELISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>

class KineticScrollHelper;

/**
 * @brief A QListView subclass with long-press support and manual kinetic scrolling.
 * 
 * This class extends QListView with:
 * 1. Long-press detection: Emits longPressed signal after LONG_PRESS_MS for context menus
 * 2. Manual kinetic scrolling: Replaces QScroller for reliable touch scrolling
 * 
 * The manual kinetic scrolling avoids known QScroller issues:
 * - Inertia scrolling reversing direction and accelerating
 * - Unreliable behavior on Linux/Wayland tablet devices
 * - Conflicts between scroll gestures and item tap detection
 * 
 * Based on the same pattern used in PagePanelListView and OutlinePanelTreeWidget.
 */
class TimelineListView : public QListView {
    Q_OBJECT

public:
    explicit TimelineListView(QWidget* parent = nullptr);
    
signals:
    /**
     * @brief Emitted when user long-presses on an item.
     * @param index The model index of the long-pressed item.
     * @param globalPos The global position where the long-press occurred.
     */
    void longPressed(const QModelIndex& index, const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onLongPressTimeout();

private:
    // Long-press detection
    QTimer m_longPressTimer;
    QPoint m_pressPos;              // Position where press started (viewport coords)
    QModelIndex m_pressedIndex;     // Index that was pressed
    bool m_longPressTriggered = false;
    
    // Touch scrolling state
    bool m_touchScrolling = false;
    int m_scrollStartValue = 0;
    int m_lastScrollValue = 0;
    
    // Kinetic scrolling (uses shared helper)
    KineticScrollHelper* m_kineticHelper = nullptr;
    
    // Constants
    static constexpr int LONG_PRESS_MS = 500;           // Time to trigger long-press
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 20; // Max movement in pixels
};

#endif // TIMELINELISTVIEW_H
