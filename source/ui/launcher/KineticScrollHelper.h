#ifndef KINETICSCROLLHELPER_H
#define KINETICSCROLLHELPER_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QMouseEvent>

class QScrollBar;

/**
 * @brief Helper class for manual kinetic scrolling.
 * 
 * This class encapsulates the kinetic scrolling logic used by both
 * LauncherScrollArea and TimelineListView, eliminating code duplication.
 * 
 * Features:
 * - Velocity tracking with exponential smoothing
 * - Kinetic animation with configurable deceleration
 * - Velocity capping to prevent extreme scroll distances
 * - Proper boundary handling (stops at edges)
 * 
 * Usage:
 * 1. Create instance with target scroll bar
 * 2. Call startTracking() on touch/mouse press
 * 3. Call updateVelocity() on each move event
 * 4. Call finishTracking() on release - starts kinetic scroll if appropriate
 * 5. Call stop() to cancel any ongoing kinetic scroll
 */
class KineticScrollHelper : public QObject {
    Q_OBJECT

public:
    explicit KineticScrollHelper(QScrollBar* scrollBar, QObject* parent = nullptr);
    
    /**
     * @brief Check if a mouse event originated from touch input.
     * @param event The mouse event to check.
     * @return True if the event was synthesized from touch input.
     */
    static bool isTouchInput(QMouseEvent* event);
    
    /**
     * @brief Start tracking velocity for a new gesture.
     * Also stops any ongoing kinetic scroll.
     */
    void startTracking();
    
    /**
     * @brief Update velocity based on scroll position change.
     * Call this on each move event during scrolling.
     * @param scrollDelta The change in scroll position since last call.
     */
    void updateVelocity(int scrollDelta);
    
    /**
     * @brief Finish tracking and start kinetic scroll if velocity is sufficient.
     * @return True if kinetic scroll was started.
     */
    bool finishTracking();
    
    /**
     * @brief Stop any ongoing kinetic scroll.
     */
    void stop();
    
    /**
     * @brief Check if kinetic scrolling is currently active.
     */
    bool isActive() const { return m_kineticTimer.isActive(); }
    
    /**
     * @brief Get the current tracked velocity.
     */
    qreal velocity() const { return m_lastVelocity; }

private slots:
    void onKineticTick();

private:
    QScrollBar* m_scrollBar;
    
    // Velocity tracking
    QElapsedTimer m_velocityTimer;
    qreal m_lastVelocity = 0.0;
    
    // Kinetic animation
    QTimer m_kineticTimer;
    qreal m_kineticVelocity = 0.0;
    
    // Constants
    static constexpr int KINETIC_TICK_MS = 16;          // ~60 FPS
    static constexpr qreal KINETIC_DECELERATION = 0.92; // Per-tick multiplier
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.5;  // Stop threshold (px/ms)
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;  // Cap extreme velocities
    static constexpr qreal VELOCITY_SMOOTHING = 0.4;    // Exponential smoothing alpha
};

#endif // KINETICSCROLLHELPER_H
