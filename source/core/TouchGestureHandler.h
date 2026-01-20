#pragma once

// ============================================================================
// TouchGestureHandler - Option 2: Clean Break + Hysteresis
// ============================================================================
// Redesigned for reliability on Android (BUG-A005 final fix v2)
//
// Design principles:
// - 1 finger = Pan gesture (for navigation)
// - 2 fingers = Zoom gesture (pinch to zoom)
// - Clean break on finger count change (end gesture, clear state, start fresh)
// - Hysteresis: require N stable frames before changing gesture modes
// - No mixed transitions (the source of all bugs)
// ============================================================================

#include <QObject>
#include <QPointF>
#include <QVector>
#include <QElapsedTimer>
#include <QTimer>

// Forward declarations
class QTouchEvent;
class DocumentViewport;

// TouchGestureMode enum (shared definition with MainWindow.h)
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,     // Touch gestures completely off
    YAxisOnly,    // Only Y-axis panning allowed (no X-axis, no zoom)
    Full          // Full touch gestures (pan + pinch-to-zoom)
};
#endif

/**
 * @brief Handles touch gestures for DocumentViewport.
 * 
 * Option 2 Design:
 * - 1 finger: pan gesture (with inertia)
 * - 2 fingers: pinch-to-zoom gesture (no pan during zoom)
 * - Clean break on finger count change: fully end one gesture before starting another
 * - Hysteresis: require stable finger count for N frames before switching modes
 */
class TouchGestureHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a touch gesture handler.
     * @param viewport The viewport to control (not owned).
     * @param parent QObject parent for memory management.
     */
    explicit TouchGestureHandler(DocumentViewport* viewport, QObject* parent = nullptr);
    ~TouchGestureHandler() = default;
    
    /**
     * @brief Handle a touch event.
     * @param event The touch event to process.
     * @return True if event was handled, false to pass through.
     */
    bool handleTouchEvent(QTouchEvent* event);
    
    /**
     * @brief Set the touch gesture mode.
     * @param mode New mode (Disabled, YAxisOnly, Full).
     * 
     * Ends any active gesture if mode changes.
     */
    void setMode(TouchGestureMode mode);
    
    /**
     * @brief Get the current touch gesture mode.
     */
    TouchGestureMode mode() const { return m_mode; }
    
    /**
     * @brief Check if a touch gesture is currently active.
     * Includes active pan/zoom or inertia animation.
     */
    bool isActive() const { return m_gestureType != GestureType::None || (m_inertiaTimer && m_inertiaTimer->isActive()); }
    
    /**
     * @brief Force reset all gesture state.
     * Call this on tab switch, hideEvent, etc. to prevent stale state.
     */
    void resetAllState();

private slots:
    /**
     * @brief Handle inertia animation frame.
     * Called by m_inertiaTimer to apply friction and update pan position.
     */
    void onInertiaFrame();

private:
    // ===== Gesture Type =====
    enum class GestureType {
        None,       // No gesture active
        OneFinger,  // 1-finger pan
        TwoFinger   // 2-finger zoom
    };
    
    // ===== Viewport Reference =====
    DocumentViewport* m_viewport;  ///< Viewport to control (not owned)
    
    // ===== Mode =====
    TouchGestureMode m_mode = TouchGestureMode::Disabled;
    
    // ===== Current Gesture State =====
    GestureType m_gestureType = GestureType::None;
    
    // ===== Hysteresis State =====
    int m_pendingFingerCount = 0;       ///< The finger count we're transitioning to
    int m_hysteresisCounter = 0;        ///< How many frames we've seen this count
    static constexpr int HYSTERESIS_THRESHOLD = 3;  ///< Frames required before mode switch
    
    // ===== Transition Lockout =====
    // After ending a gesture via hysteresis (e.g., 2→1 finger), don't immediately
    // start a new gesture. Wait for a fresh TouchBegin to avoid stale position data.
    bool m_waitingForFreshTouch = false;
    
    // ===== 1-Finger Pan State =====
    QPointF m_lastPanPosition;          ///< Last position for delta calculation
    
    // ===== 2-Finger Zoom State =====
    QPointF m_lastCentroid;             ///< Last centroid position
    qreal m_lastDistance = 0;           ///< Last finger distance (for zoom scale)
    qreal m_initialDistance = 0;        ///< Distance at gesture start (for zoom threshold)
    bool m_zoomActivated = false;       ///< Whether zoom threshold has been exceeded
    qreal m_smoothedScale = 1.0;        ///< Exponentially smoothed scale factor
    
    // Zoom dead zone: don't zoom until finger distance changes by this percentage
    // This prevents zoom "shaking" during pan-only gestures
    static constexpr qreal ZOOM_ACTIVATION_THRESHOLD = 0.1;  ///< 10% change required
    
    // Scale dead zone: treat scale values within this range of 1.0 as exactly 1.0
    // This prevents zoom jitter from small finger distance variations
    static constexpr qreal ZOOM_SCALE_DEAD_ZONE = 0.002;  ///< 0.2%
    
    // Zoom smoothing: exponential moving average factor (0-1)
    // Higher = more responsive but jittery, Lower = smoother but laggy
    static constexpr qreal ZOOM_SMOOTHING_FACTOR = 0.4;
    
    // ===== Velocity Tracking for Inertia =====
    QVector<QPointF> m_velocitySamples;      ///< Recent velocity samples (pixels/ms)
    QElapsedTimer m_velocityTimer;           ///< Timer for velocity calculation
    static constexpr int MAX_VELOCITY_SAMPLES = 5;
    
    // ===== Inertia Animation =====
    QTimer* m_inertiaTimer = nullptr;        ///< Timer for inertia animation frames
    QPointF m_inertiaVelocity;               ///< Current inertia velocity (doc coords/ms)
    static constexpr qreal INERTIA_FRICTION = 0.92;
    static constexpr qreal INERTIA_MIN_VELOCITY = 0.05;
    static constexpr int INERTIA_INTERVAL_MS = 16;  ///< ~60 FPS
    
    // ===== 3-Finger Tap Detection =====
    QElapsedTimer m_threeFingerTimer;
    bool m_threeFingerTimerActive = false;
    static constexpr qint64 TAP_MAX_DURATION_MS = 300;
    
    // ===== Helper Methods =====
    
    /**
     * @brief End the current gesture completely and clear state.
     * @param startInertia If true, start inertia animation if velocity is sufficient.
     */
    void endGesture(bool startInertia);
    
    /**
     * @brief Start a 1-finger pan gesture.
     * @param position The initial finger position.
     */
    void beginOneFinger(QPointF position);
    
    /**
     * @brief Update a 1-finger pan gesture.
     * @param position The current finger position.
     */
    void updateOneFinger(QPointF position);
    
    /**
     * @brief Start a 2-finger zoom gesture.
     * @param p1 Position of first finger.
     * @param p2 Position of second finger.
     */
    void beginTwoFinger(QPointF p1, QPointF p2);
    
    /**
     * @brief Update a 2-finger zoom gesture.
     * @param p1 Position of first finger.
     * @param p2 Position of second finger.
     */
    void updateTwoFinger(QPointF p1, QPointF p2);
    
    /**
     * @brief Handle a 3-finger tap gesture.
     */
    void on3FingerTap();
};
