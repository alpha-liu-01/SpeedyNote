#ifndef TIMELINELISTVIEW_H
#define TIMELINELISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QElapsedTimer>
#include <QPoint>

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
    void onKineticScrollTick();

private:
    void startKineticScroll(qreal velocity);
    void stopKineticScroll();
    bool isTouchInput(QMouseEvent* event) const;
    
    // Long-press detection
    QTimer m_longPressTimer;
    QPoint m_pressPos;              // Position where press started (viewport coords)
    QModelIndex m_pressedIndex;     // Index that was pressed
    bool m_longPressTriggered = false;
    
    // Touch scrolling state
    bool m_touchScrolling = false;
    int m_scrollStartValue = 0;
    
    // Velocity tracking for kinetic scrolling
    QElapsedTimer m_velocityTimer;
    qreal m_lastVelocity = 0.0;
    
    // Kinetic scrolling animation
    QTimer m_kineticTimer;
    qreal m_kineticVelocity = 0.0;
    
    // Constants
    static constexpr int LONG_PRESS_MS = 500;           // Time to trigger long-press
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 20; // Max movement in pixels
    static constexpr int KINETIC_TICK_MS = 16;          // ~60 FPS
    static constexpr qreal KINETIC_DECELERATION = 0.92; // Per-tick multiplier
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.5;  // Stop threshold (px/ms)
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;  // Cap extreme velocities
};

#endif // TIMELINELISTVIEW_H
