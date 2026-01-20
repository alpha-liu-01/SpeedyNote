#pragma once

// ============================================================================
// TouchGestureHandler - Smartphone Gallery Style Touch Gestures
// ============================================================================
// Redesigned for reliability on Android (BUG-A005 final fix)
//
// Design principles:
// - 1 finger = Tool mode (drawing/eraser) - NOT navigation
// - 2 fingers = Pan + Zoom simultaneously (like Google Maps/Photos)
// - No 1→2 finger transitions to track
// - Only process gestures when BOTH fingers have valid position data
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
 * Implements smartphone gallery-style gestures:
 * - 2 fingers: simultaneous pan + zoom
 * - 1 finger: passes through to tool handling (drawing/eraser)
 * - 3 fingers: tap detection (toggle sidebar)
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
     * @return True if event was handled (2+ fingers), false for 1-finger (pass to tool).
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
    bool isActive() const { return m_gestureActive || (m_inertiaTimer && m_inertiaTimer->isActive()); }

private slots:
    /**
     * @brief Handle inertia animation frame.
     * Called by m_inertiaTimer to apply friction and update pan position.
     */
    void onInertiaFrame();

private:
    // ===== Viewport Reference =====
    DocumentViewport* m_viewport;  ///< Viewport to control (not owned)
    
    // ===== Mode =====
    TouchGestureMode m_mode = TouchGestureMode::Disabled;
    
    // ===== Two-Finger Gesture State =====
    bool m_gestureActive = false;       ///< Whether a 2-finger gesture is in progress
    QPointF m_lastCentroid;             ///< Last centroid position (for pan delta)
    qreal m_lastDistance = 0;           ///< Last finger distance (for zoom scale)
    qreal m_initialDistance = 0;        ///< Distance at gesture start (for zoom threshold)
    bool m_zoomActivated = false;       ///< Whether zoom threshold has been exceeded
    qreal m_smoothedScale = 1.0;        ///< Exponentially smoothed scale factor
    
    // Zoom dead zone: don't zoom until finger distance changes by this percentage
    // This prevents zoom "shaking" during pan-only gestures
    static constexpr qreal ZOOM_ACTIVATION_THRESHOLD = 0.17;  ///< 17% change required
    
    // Scale dead zone: treat scale values within this range of 1.0 as exactly 1.0
    // This prevents zoom jitter from small finger distance variations during pan
    static constexpr qreal ZOOM_SCALE_DEAD_ZONE = 0.008;  ///< 1% - scales 0.99-1.01 become 1.0
    
    // Zoom smoothing: exponential moving average factor (0-1)
    // Higher = more responsive but jittery, Lower = smoother but laggy
    // 0.4 provides a good balance of responsiveness and smoothness
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
     * @brief End the current gesture.
     * @param startInertia If true, start inertia animation if velocity is sufficient.
     */
    void endGesture(bool startInertia);
    
    /**
     * @brief Handle a 3-finger tap gesture.
     */
    void on3FingerTap();
};

