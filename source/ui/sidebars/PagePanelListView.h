#ifndef PAGEPANELLISTVIEW_H
#define PAGEPANELLISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>
#include <QElapsedTimer>

class QDragMoveEvent;

/**
 * @brief A QListView subclass that enables touch drag-and-drop via long-press.
 * 
 * Resolves the conflict between QScroller (touch scrolling) and Qt's drag-and-drop:
 * - Short touch/move → QScroller handles scrolling
 * - Long-press (400ms) → Initiates drag, QScroller disabled during drag
 * - Stylus/mouse → Immediate drag (no long-press needed)
 * 
 * This class manages its own QScroller lifecycle to properly handle the
 * ungrab/regrab cycle needed for touch drag-and-drop.
 */
class PagePanelListView : public QListView {
    Q_OBJECT

public:
    explicit PagePanelListView(QWidget* parent = nullptr);
    
    /**
     * @brief Start a drag operation (public wrapper for protected startDrag).
     * @param supportedActions The drop actions to support.
     */
    void beginDrag(Qt::DropActions supportedActions);
    
    /**
     * @brief Get the last mouse press position (viewport coordinates).
     * Used to determine if click was within thumbnail region.
     */
    QPoint lastPressPosition() const { return m_pressPos; }
    
signals:
    /**
     * @brief Emitted when a drag should start for the given index.
     * @param index The model index to drag.
     * 
     * Connect this to manually start a QDrag operation.
     */
    void dragRequested(const QModelIndex& index);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    
    // Handle touch end/cancel as fallback cleanup
    bool viewportEvent(QEvent* event) override;

private slots:
    void onLongPressTimeout();
    void onKineticScrollTick();

private:
    void ungrabScroller();
    void regrabScroller();
    void setupTouchScrolling();
    void startKineticScroll(qreal velocity);
    void stopKineticScroll();
    
    QTimer m_longPressTimer;
    QPoint m_pressPos;              // Position where press started
    QModelIndex m_pressedIndex;     // Index that was pressed
    bool m_longPressTriggered = false;
    bool m_isTouchInput = false;    // True if current input is touch (not stylus/mouse)
    bool m_scrollerGrabbed = true;  // Track QScroller grab state
    
    // Manual touch scrolling state
    int m_touchScrollStartPos = 0;  // Scroll position at touch start
    bool m_touchScrolling = false;  // True if currently touch-scrolling
    
    // Kinetic scrolling state
    QTimer m_kineticTimer;
    QElapsedTimer m_velocityTimer;  // For calculating velocity
    qreal m_kineticVelocity = 0.0;  // Current velocity in pixels/ms
    qreal m_lastVelocity = 0.0;     // Last calculated velocity (for release)
    
    static constexpr int LONG_PRESS_MS = 400;            // Time to trigger long-press
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 15; // Max movement in pixels
    static constexpr int AUTO_SCROLL_MARGIN = 50;        // Pixels from edge to trigger
    static constexpr int AUTO_SCROLL_MAX_SPEED = 10;     // Max pixels per event
    static constexpr int KINETIC_TICK_MS = 16;           // ~60 FPS
    static constexpr qreal KINETIC_DECELERATION = 0.92;  // Velocity multiplier per tick (faster decay)
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.05;  // Stop below this (pixels/ms)
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;   // Cap velocity (pixels/ms)
};

#endif // PAGEPANELLISTVIEW_H
