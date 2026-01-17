#pragma once

// ============================================================================
// TouchGestureHandler - Handles touch gestures for DocumentViewport
// ============================================================================
// Part of the Touch Gesture Hub (Phase TG)
//
// This class encapsulates all touch gesture logic:
// - Single-finger pan with inertia
// - Pinch-to-zoom (in Full mode)
// - 3-finger tap detection
//
// It calls back to DocumentViewport's deferred gesture APIs for smooth rendering.
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
 * Processes touch events and translates them into pan/zoom operations
 * by calling the viewport's deferred gesture API. This keeps touch logic
 * separate from the main viewport rendering code.
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
     * @return True if event was handled, false otherwise.
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
     * Includes active pan, pinch, or inertia animation.
     */
    bool isActive() const { return m_panActive || m_pinchActive || (m_inertiaTimer && m_inertiaTimer->isActive()); }

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
    
    // ===== Single-finger Pan Tracking =====
    bool m_panActive = false;                ///< Whether a touch pan is in progress
    QPointF m_lastPos;                       ///< Last touch position (viewport coords)
    
    // ===== Pinch-to-zoom Tracking =====
    bool m_pinchActive = false;              ///< Whether a pinch gesture is in progress
    qreal m_pinchStartDistance = 0;          ///< Distance between fingers at start (for incremental scaling)
    QPointF m_pinchCentroid;                 ///< Centroid of pinch gesture (fixed at start)
    
    // ===== Velocity Tracking for Inertia =====
    QVector<QPointF> m_velocitySamples;      ///< Recent velocity samples (pixels/ms) for averaging
    QElapsedTimer m_velocityTimer;           ///< Timer for velocity calculation
    static constexpr int MAX_VELOCITY_SAMPLES = 5;  ///< Max samples to keep
    
    // ===== Inertia Animation =====
    QTimer* m_inertiaTimer = nullptr;        ///< Timer for inertia animation frames
    QPointF m_inertiaVelocity;               ///< Current inertia velocity (doc coords/ms)
    static constexpr qreal INERTIA_FRICTION = 0.92;     ///< Velocity multiplier per frame (lower = more resistance)
    static constexpr qreal INERTIA_MIN_VELOCITY = 0.05; ///< Stop threshold (doc coords/ms)
    static constexpr int INERTIA_INTERVAL_MS = 16;      ///< ~60 FPS
    
    // ===== Multi-touch Tracking =====
    int m_activeTouchPoints = 0;             ///< Number of active touch points
    
    // ===== 3-Finger Tap Detection =====
    QElapsedTimer m_threeFingerTimer;        ///< Timer for 3-finger tap detection
    bool m_threeFingerTimerActive = false;   ///< Whether 3-finger timer is running
    static constexpr qint64 TAP_MAX_DURATION_MS = 300;  ///< Max duration for tap detection
    
    // ===== Helper Methods =====
    
    /**
     * @brief End the current touch pan gesture.
     * @param startInertia If true, start inertia animation if velocity is sufficient.
     */
    void endTouchPan(bool startInertia);
    
    /**
     * @brief End the current pinch-to-zoom gesture.
     */
    void endTouchPinch();
    
    /**
     * @brief Handle a 3-finger tap gesture.
     */
    void on3FingerTap();
};
