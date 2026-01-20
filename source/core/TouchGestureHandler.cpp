// ============================================================================
// TouchGestureHandler - Smartphone Gallery Style Touch Gestures
// ============================================================================
// Redesigned for reliability on Android (BUG-A005 final fix)
//
// Key changes from legacy design:
// - No 1-finger pan (1 finger = tool mode, passed through)
// - 2 fingers = simultaneous pan + zoom
// - No position caching (only process when both fingers have data)
// - No 1→2 transition tracking (the source of all bugs)
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
    if (m_gestureActive) {
        endGesture(false);  // No inertia on mode change
    }
    
    // Stop inertia if running
    if (m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
    }
    
    m_mode = mode;
}

bool TouchGestureHandler::handleTouchEvent(QTouchEvent* event)
{
    if (m_mode == TouchGestureMode::Disabled) {
        return false;
    }
    
    // Stop inertia if any new touch comes in
    if (event->type() == QEvent::TouchBegin && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
        m_velocitySamples.clear();
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
                 << "gestureActive:" << m_gestureActive;
    }
#endif
    
    // ===== TouchEnd/TouchCancel - End gesture =====
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        if (m_gestureActive) {
            // Inertia only on clean TouchEnd, not TouchCancel
            endGesture(event->type() == QEvent::TouchEnd);
        }
        event->accept();
        return true;
    }
    
    // ===== 3+ fingers - Suspend gesture, detect tap =====
    if (fingerCount >= 3) {
        if (m_gestureActive) {
            endGesture(false);  // No inertia when interrupted
        }
        
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
    
    // ===== 2 fingers - Pan + Zoom =====
    // In Full mode: Use zoom gesture with dead zone for zoom activation.
    // Pan works immediately, but zoom only activates after distance changes by threshold.
    // In YAxisOnly mode: Use ONLY pan gesture (no zoom).
    if (fingerCount == 2) {
        QPointF p1 = activePoints[0]->position();
        QPointF p2 = activePoints[1]->position();
        QPointF centroid = (p1 + p2) / 2.0;
        qreal distance = QLineF(p1, p2).length();
        
        // Avoid division by zero
        if (distance < 1.0) {
            distance = 1.0;
        }
        
        if (!m_gestureActive) {
            // ===== Start gesture =====
            m_lastCentroid = centroid;
            m_lastDistance = distance;
            m_initialDistance = distance;  // Store for zoom threshold calculation
            m_zoomActivated = false;       // Zoom starts inactive (dead zone)
            m_smoothedScale = 1.0;         // Reset smoothed scale
            m_gestureActive = true;
            
            // Start appropriate gesture based on mode
            if (m_mode == TouchGestureMode::Full) {
                // Zoom gesture handles both zoom AND pan (via centroid movement)
                m_viewport->beginZoomGesture(centroid);
            } else if (m_mode == TouchGestureMode::YAxisOnly) {
                // Pan gesture only (no zoom in this mode)
                m_viewport->beginPanGesture();
            }
            
            // Reset velocity tracking
            m_velocitySamples.clear();
            m_velocityTimer.start();
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[TouchGestureHandler] Starting 2-finger gesture"
                     << "mode:" << (m_mode == TouchGestureMode::Full ? "Full" : "YAxisOnly")
                     << "distance:" << distance
                     << "centroid:" << centroid;
#endif
        } else {
            // ===== Update gesture =====
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
                
                // Apply zoom only if activated, otherwise just pan (scale = 1.0)
                qreal targetScale = 1.0;
                if (m_zoomActivated) {
                    // Apply scale dead zone: treat values very close to 1.0 as exactly 1.0
                    // This prevents zoom jitter from small finger distance variations during pan
                    if (std::abs(scale - 1.0) > ZOOM_SCALE_DEAD_ZONE) {
                        targetScale = scale;
                    }
                    // else: targetScale stays 1.0, no zoom this frame
                }
                
                // Apply exponential smoothing for smoother zoom
                // smoothedScale = previousSmoothed * (1-alpha) + target * alpha
                // This is very CPU-efficient (just a few multiplications)
                m_smoothedScale = m_smoothedScale * (1.0 - ZOOM_SMOOTHING_FACTOR) 
                                + targetScale * ZOOM_SMOOTHING_FACTOR;
                
                m_viewport->updateZoomGesture(m_smoothedScale, centroid);
            } else if (m_mode == TouchGestureMode::YAxisOnly) {
                // Pan gesture: Y-axis only
                panDelta.setX(0);
                m_viewport->updatePanGesture(-panDelta);  // Negate for correct direction
            }
            
            // Track velocity for inertia (NOT negated - inertia will negate)
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
        
        event->accept();
        return true;
    }
    
    // ===== 1 finger - Accept but don't start gesture =====
    // We must accept ALL touch events to keep receiving updates.
    // If we return false for TouchBegin, Qt stops sending subsequent events
    // and we never see when the second finger is added.
    if (fingerCount == 1) {
        if (m_gestureActive) {
            // One finger lifted during 2-finger gesture - end it
            endGesture(true);  // With inertia
        }
        
        // Accept event but don't start gesture
        // Drawing with finger is disabled (stylus-only mode)
        // User can use 2 fingers for pan+zoom
        event->accept();
        return true;
    }
    
    // ===== 0 fingers (shouldn't happen in TouchUpdate) =====
    if (fingerCount == 0) {
        if (m_gestureActive) {
            endGesture(true);
        }
        event->accept();
        return true;
    }
    
    // Accept any remaining cases
    event->accept();
    return true;
}

void TouchGestureHandler::endGesture(bool startInertia)
{
    if (!m_gestureActive) {
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] Ending gesture, startInertia:" << startInertia;
#endif
    
    m_gestureActive = false;
    
    if (m_mode == TouchGestureMode::Full) {
        // End zoom gesture - this also applies the final pan correction
        m_viewport->endZoomGesture();
        
        // In Full mode, we can start inertia for smooth pan continuation
        if (startInertia && !m_velocitySamples.isEmpty()) {
            QPointF avgVelocity(0, 0);
            for (const auto& v : m_velocitySamples) {
                avgVelocity += v;
            }
            avgVelocity /= m_velocitySamples.size();
            
            qreal speed = std::sqrt(avgVelocity.x() * avgVelocity.x() + 
                                    avgVelocity.y() * avgVelocity.y());
            
            if (speed > INERTIA_MIN_VELOCITY) {
                m_inertiaVelocity = avgVelocity;
                m_viewport->beginPanGesture();  // Start pan for inertia
                m_inertiaTimer->start();
                return;
            }
        }
    } else if (m_mode == TouchGestureMode::YAxisOnly) {
        // Calculate inertia before ending pan
        if (startInertia && !m_velocitySamples.isEmpty()) {
            QPointF avgVelocity(0, 0);
            for (const auto& v : m_velocitySamples) {
                avgVelocity += v;
            }
            avgVelocity /= m_velocitySamples.size();
            
            qreal speed = std::sqrt(avgVelocity.x() * avgVelocity.x() + 
                                    avgVelocity.y() * avgVelocity.y());
            
            if (speed > INERTIA_MIN_VELOCITY) {
                m_inertiaVelocity = avgVelocity;
                m_inertiaTimer->start();
                // Pan continues during inertia
                return;
            }
        }
        
        // No inertia - end pan now
        m_viewport->endPanGesture();
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

