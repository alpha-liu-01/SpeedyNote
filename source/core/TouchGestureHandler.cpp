// ============================================================================
// TouchGestureHandler - Option 2: Clean Break + Hysteresis
// ============================================================================
// Redesigned for reliability on Android (BUG-A005 final fix v2)
//
// Key design:
// - 1-finger = pan gesture
// - 2-finger = zoom gesture (no pan mixing)
// - Clean break on finger count change: fully end one gesture before starting another
// - Hysteresis: require stable finger count for N frames before switching modes
// - This prevents 1↔2 finger transition bugs that plagued the old design
// ============================================================================

#include "TouchGestureHandler.h"
#include "DocumentViewport.h"

#include <QTouchEvent>
#include <QLineF>

#ifdef SPEEDYNOTE_DEBUG
#include <QDebug>
#endif

TouchGestureHandler::TouchGestureHandler(DocumentViewport* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    // Inertia timer for smooth deceleration after gesture ends
    m_inertiaTimer = new QTimer(this);
    m_inertiaTimer->setInterval(INERTIA_INTERVAL_MS);
    connect(m_inertiaTimer, &QTimer::timeout, this, &TouchGestureHandler::onInertiaFrame);
}

void TouchGestureHandler::setMode(TouchGestureMode mode)
{
    if (m_mode == mode) {
        return;
    }
    
    // End any active gesture before mode change
    resetAllState();
    m_mode = mode;
}

void TouchGestureHandler::resetAllState()
{
    // End any active gesture
    if (m_gestureType != GestureType::None) {
        endGesture(false);  // No inertia on reset
    }
    
    // Stop inertia if running
    if (m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
    }
    
    // Clear all state
    m_gestureType = GestureType::None;
    m_pendingFingerCount = 0;
    m_hysteresisCounter = 0;
    m_velocitySamples.clear();
    m_zoomActivated = false;
    m_smoothedScale = 1.0;
    m_waitingForFreshTouch = false;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] resetAllState() called";
#endif
}

bool TouchGestureHandler::handleTouchEvent(QTouchEvent* event)
{
    if (m_mode == TouchGestureMode::Disabled) {
        return false;
    }
    
    // ===== TouchBegin - Fresh touch, clear lockout =====
    if (event->type() == QEvent::TouchBegin) {
        // Clear transition lockout - this is a fresh touch sequence
        m_waitingForFreshTouch = false;
        
        // Stop inertia if running
        if (m_inertiaTimer->isActive()) {
            m_inertiaTimer->stop();
            m_viewport->endPanGesture();
            m_velocitySamples.clear();
        }
    }
    
    // Collect active points (not Released)
    const auto& points = event->points();
    QVector<const QEventPoint*> activePoints;
    activePoints.reserve(points.size());
    
    for (const auto& pt : points) {
        if (pt.state() != QEventPoint::Released) {
            activePoints.append(&pt);
        }
    }
    
    int fingerCount = activePoints.size();
    
#ifdef SPEEDYNOTE_DEBUG
    static int debugCounter = 0;
    if (++debugCounter % 20 == 0 || event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchEnd) {
        qDebug() << "[TouchGestureHandler]"
                 << (event->type() == QEvent::TouchBegin ? "TouchBegin" :
                     event->type() == QEvent::TouchEnd ? "TouchEnd" :
                     event->type() == QEvent::TouchCancel ? "TouchCancel" : "TouchUpdate")
                 << "fingers:" << fingerCount
                 << "gestureType:" << (m_gestureType == GestureType::None ? "None" :
                                       m_gestureType == GestureType::OneFinger ? "OneFinger" : "TwoFinger")
                 << "hysteresis:" << m_hysteresisCounter << "/" << HYSTERESIS_THRESHOLD;
    }
#endif
    
    // ===== TouchEnd/TouchCancel - End gesture =====
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        if (m_gestureType != GestureType::None) {
            // Inertia only on clean TouchEnd, not TouchCancel
            endGesture(event->type() == QEvent::TouchEnd);
        }
        // Reset all transition state
        m_pendingFingerCount = 0;
        m_hysteresisCounter = 0;
        m_waitingForFreshTouch = false;  // Next touch is fresh by definition
        event->accept();
        return true;
    }
    
    // ===== 3+ fingers - End gesture, detect tap =====
    if (fingerCount >= 3) {
        if (m_gestureType != GestureType::None) {
            endGesture(false);  // No inertia when interrupted
        }
        m_pendingFingerCount = 0;
        m_hysteresisCounter = 0;
        
        // Start 3-finger tap timer if not already running
        if (!m_threeFingerTimerActive) {
            m_threeFingerTimer.start();
            m_threeFingerTimerActive = true;
        }
        
        event->accept();
        return true;
    }
    
    // ===== Check for 3-finger tap completion =====
    if (m_threeFingerTimerActive && fingerCount < 3) {
        qint64 duration = m_threeFingerTimer.elapsed();
        if (duration < TAP_MAX_DURATION_MS) {
            on3FingerTap();
        }
        m_threeFingerTimerActive = false;
    }
    
    // ===== Determine expected gesture type for current finger count =====
    GestureType expectedType = GestureType::None;
    if (fingerCount == 1) {
        expectedType = GestureType::OneFinger;
    } else if (fingerCount == 2) {
        expectedType = GestureType::TwoFinger;
    }
    
    // ===== Handle finger count changes with hysteresis =====
    if (m_gestureType != GestureType::None && expectedType != m_gestureType) {
        // Finger count is different from current gesture
        if (m_pendingFingerCount != fingerCount) {
            // New pending count, start counting
            m_pendingFingerCount = fingerCount;
            m_hysteresisCounter = 1;
        } else {
            // Same pending count, increment
            m_hysteresisCounter++;
        }
        
        // Check if we've reached threshold
        if (m_hysteresisCounter >= HYSTERESIS_THRESHOLD) {
            GestureType previousType = m_gestureType;
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[TouchGestureHandler] Hysteresis threshold reached, switching from"
                     << (m_gestureType == GestureType::OneFinger ? "1-finger" : "2-finger")
                     << "to" << fingerCount << "fingers";
#endif
            // CLEAN BREAK: End current gesture completely
            endGesture(false);  // No inertia on mode switch
            
            // Reset hysteresis
            m_pendingFingerCount = 0;
            m_hysteresisCounter = 0;
            
            // TRANSITION LOCKOUT: Only when going from MORE fingers to FEWER fingers
            // - 2→1: The remaining finger has stale position data → LOCKOUT
            // - 1→2: The new finger is fresh data → NO LOCKOUT, start 2-finger immediately
            if (previousType == GestureType::TwoFinger && fingerCount == 1) {
                m_waitingForFreshTouch = true;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[TouchGestureHandler] 2→1 transition: waiting for fresh touch";
#endif
                event->accept();
                return true;
            }
            // For 1→2 transition, fall through to start 2-finger gesture below
#ifdef SPEEDYNOTE_DEBUG
            else {
                qDebug() << "[TouchGestureHandler] 1→2 transition: starting 2-finger gesture immediately";
            }
#endif
        } else {
            // Still in hysteresis, continue current gesture with available data
            // Don't process the new finger count yet
            if (m_gestureType == GestureType::OneFinger && fingerCount >= 1) {
                // Continue 1-finger pan with first finger
                updateOneFinger(activePoints[0]->position());
            } else if (m_gestureType == GestureType::TwoFinger && fingerCount >= 2) {
                // Continue 2-finger zoom
                updateTwoFinger(activePoints[0]->position(), activePoints[1]->position());
            }
            event->accept();
            return true;
        }
    } else {
        // Finger count matches current gesture (or no gesture active)
        m_pendingFingerCount = 0;
        m_hysteresisCounter = 0;
    }
    
    // ===== Transition Lockout Check =====
    // If we're waiting for a fresh touch, don't start any new gestures
    // during TouchUpdate. Just accept the event and wait.
    if (m_waitingForFreshTouch && m_gestureType == GestureType::None) {
#ifdef SPEEDYNOTE_DEBUG
        static int lockoutLogCount = 0;
        if (++lockoutLogCount % 10 == 1) {
            qDebug() << "[TouchGestureHandler] In lockout, waiting for fresh touch (fingers:" << fingerCount << ")";
        }
#endif
        // No gesture active and waiting for fresh touch - just accept and wait
        event->accept();
        return true;
    }
    
    // ===== 2 fingers - Zoom Gesture =====
    if (fingerCount == 2) {
        QPointF p1 = activePoints[0]->position();
        QPointF p2 = activePoints[1]->position();
        
        if (m_gestureType != GestureType::TwoFinger) {
            // Start 2-finger zoom
            beginTwoFinger(p1, p2);
        } else {
            // Update 2-finger zoom
            updateTwoFinger(p1, p2);
        }
        
        event->accept();
        return true;
    }
    
    // ===== 1 finger - Pan Gesture =====
    if (fingerCount == 1) {
        QPointF position = activePoints[0]->position();
        
        if (m_gestureType != GestureType::OneFinger) {
            // Start 1-finger pan
            beginOneFinger(position);
        } else {
            // Update 1-finger pan
            updateOneFinger(position);
        }
        
        event->accept();
        return true;
    }
    
    // ===== 0 fingers (shouldn't happen in TouchUpdate) =====
    if (fingerCount == 0) {
        if (m_gestureType != GestureType::None) {
            endGesture(true);
        }
        event->accept();
        return true;
    }
    
    // Accept any remaining cases
    event->accept();
    return true;
}

void TouchGestureHandler::beginOneFinger(QPointF position)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] beginOneFinger at" << position;
#endif
    
    m_gestureType = GestureType::OneFinger;
    m_lastPanPosition = position;
    
    // Start pan gesture on viewport
    m_viewport->beginPanGesture();
    
    // Reset velocity tracking
    m_velocitySamples.clear();
    m_velocityTimer.start();
}

void TouchGestureHandler::updateOneFinger(QPointF position)
{
    QPointF delta = position - m_lastPanPosition;
    
    // Apply mode restrictions
    if (m_mode == TouchGestureMode::YAxisOnly) {
        delta.setX(0);
    }
    
    // Update pan (negate for correct direction: finger moves right → content pans left)
    m_viewport->updatePanGesture(-delta);
    
    // Track velocity for inertia
    qint64 elapsed = m_velocityTimer.elapsed();
    if (elapsed > 0) {
        QPointF velocity(delta.x() / elapsed, delta.y() / elapsed);
        if (m_mode == TouchGestureMode::YAxisOnly) {
            velocity.setX(0);
        }
        m_velocitySamples.append(velocity);
        if (m_velocitySamples.size() > MAX_VELOCITY_SAMPLES) {
            m_velocitySamples.removeFirst();
        }
        m_velocityTimer.restart();
    }
    
    m_lastPanPosition = position;
}

void TouchGestureHandler::beginTwoFinger(QPointF p1, QPointF p2)
{
    QPointF centroid = (p1 + p2) / 2.0;
    qreal distance = QLineF(p1, p2).length();
    
    // Avoid division by zero
    if (distance < 1.0) {
        distance = 1.0;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] beginTwoFinger"
             << "centroid:" << centroid
             << "distance:" << distance;
#endif
    
    m_gestureType = GestureType::TwoFinger;
    m_lastCentroid = centroid;
    m_lastDistance = distance;
    m_initialDistance = distance;  // For zoom threshold
    m_zoomActivated = false;       // Zoom starts inactive (dead zone)
    m_smoothedScale = 1.0;         // Reset smoothed scale
    
    // Start zoom gesture on viewport (Full mode only)
    if (m_mode == TouchGestureMode::Full) {
        m_viewport->beginZoomGesture(centroid);
    } else if (m_mode == TouchGestureMode::YAxisOnly) {
        // In YAxisOnly mode, 2-finger does Y-axis pan only (no zoom)
        m_viewport->beginPanGesture();
    }
    
    // Reset velocity tracking
    m_velocitySamples.clear();
    m_velocityTimer.start();
}

void TouchGestureHandler::updateTwoFinger(QPointF p1, QPointF p2)
{
    QPointF centroid = (p1 + p2) / 2.0;
    qreal distance = QLineF(p1, p2).length();
    
    // Avoid division by zero
    if (distance < 1.0) {
        distance = 1.0;
    }
    
    QPointF panDelta = centroid - m_lastCentroid;
    qreal scale = distance / m_lastDistance;
    
    if (m_mode == TouchGestureMode::Full) {
        // Check if zoom should be activated (activation threshold)
        if (!m_zoomActivated && m_initialDistance > 0) {
            qreal distanceChange = std::abs(distance - m_initialDistance) / m_initialDistance;
            if (distanceChange > ZOOM_ACTIVATION_THRESHOLD) {
                m_zoomActivated = true;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[TouchGestureHandler] Zoom activated! change:" << distanceChange;
#endif
            }
        }
        
        // Apply zoom only if activated, otherwise scale = 1.0
        qreal targetScale = 1.0;
        if (m_zoomActivated) {
            // Apply scale dead zone
            if (std::abs(scale - 1.0) > ZOOM_SCALE_DEAD_ZONE) {
                targetScale = scale;
            }
        }
        
        // Apply exponential smoothing
        m_smoothedScale = m_smoothedScale * (1.0 - ZOOM_SMOOTHING_FACTOR) 
                        + targetScale * ZOOM_SMOOTHING_FACTOR;
        
        // Update zoom gesture (centroid provides the zoom center)
        m_viewport->updateZoomGesture(m_smoothedScale, centroid);
        
    } else if (m_mode == TouchGestureMode::YAxisOnly) {
        // Y-axis pan only
        panDelta.setX(0);
        m_viewport->updatePanGesture(-panDelta);
    }
    
    // Track velocity for inertia (pan component only)
    qint64 elapsed = m_velocityTimer.elapsed();
    if (elapsed > 0) {
        QPointF velocity(panDelta.x() / elapsed, panDelta.y() / elapsed);
        if (m_mode == TouchGestureMode::YAxisOnly) {
            velocity.setX(0);
        }
        m_velocitySamples.append(velocity);
        if (m_velocitySamples.size() > MAX_VELOCITY_SAMPLES) {
            m_velocitySamples.removeFirst();
        }
        m_velocityTimer.restart();
    }
    
    m_lastCentroid = centroid;
    m_lastDistance = distance;
}

void TouchGestureHandler::endGesture(bool startInertia)
{
    if (m_gestureType == GestureType::None) {
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] endGesture"
             << "type:" << (m_gestureType == GestureType::OneFinger ? "OneFinger" : "TwoFinger")
             << "startInertia:" << startInertia;
#endif
    
    GestureType endingType = m_gestureType;
    m_gestureType = GestureType::None;
    
    // Calculate average velocity for potential inertia
    QPointF avgVelocity(0, 0);
    if (startInertia && !m_velocitySamples.isEmpty()) {
        for (const auto& v : m_velocitySamples) {
            avgVelocity += v;
        }
        avgVelocity /= m_velocitySamples.size();
    }
    
    qreal speed = std::sqrt(avgVelocity.x() * avgVelocity.x() + 
                            avgVelocity.y() * avgVelocity.y());
    
    if (endingType == GestureType::OneFinger) {
        // End 1-finger pan
        if (startInertia && speed > INERTIA_MIN_VELOCITY) {
            // Continue pan with inertia
            m_inertiaVelocity = avgVelocity;
            m_inertiaTimer->start();
            // Pan continues during inertia
        } else {
            // No inertia - end pan now
            m_viewport->endPanGesture();
        }
        
    } else if (endingType == GestureType::TwoFinger) {
        if (m_mode == TouchGestureMode::Full) {
            // End zoom gesture
            m_viewport->endZoomGesture();
            
            // Optionally start pan inertia after zoom
            if (startInertia && speed > INERTIA_MIN_VELOCITY) {
                m_inertiaVelocity = avgVelocity;
                m_viewport->beginPanGesture();
                m_inertiaTimer->start();
            }
        } else if (m_mode == TouchGestureMode::YAxisOnly) {
            // End Y-axis pan
            if (startInertia && speed > INERTIA_MIN_VELOCITY) {
                m_inertiaVelocity = avgVelocity;
                m_inertiaTimer->start();
            } else {
                m_viewport->endPanGesture();
            }
        }
    }
    
    m_velocitySamples.clear();
}

void TouchGestureHandler::onInertiaFrame()
{
    // Apply friction
    m_inertiaVelocity *= INERTIA_FRICTION;
    
    qreal speed = std::sqrt(m_inertiaVelocity.x() * m_inertiaVelocity.x() + 
                            m_inertiaVelocity.y() * m_inertiaVelocity.y());
    
    if (speed < INERTIA_MIN_VELOCITY) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
        m_velocitySamples.clear();
        return;
    }
    
    // Apply velocity as pan delta (velocity is in pixels/ms, timer interval is in ms)
    QPointF delta = m_inertiaVelocity * INERTIA_INTERVAL_MS;
    m_viewport->updatePanGesture(-delta);  // Negate to match finger movement direction
}

void TouchGestureHandler::on3FingerTap()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] 3-finger tap detected!";
#endif
    
    // Future: Connect this to a viewport signal for sidebar toggle
    // Currently just logs in debug mode
}
