#include "TouchGestureHandler.h"
#include "DocumentViewport.h"
#include <QTouchEvent>
#include <QLineF>
#include <QDebug>
#include <cmath>

// ===== Constructor =====

TouchGestureHandler::TouchGestureHandler(DocumentViewport* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    // Inertia animation timer
    m_inertiaTimer = new QTimer(this);
    m_inertiaTimer->setTimerType(Qt::PreciseTimer);
    connect(m_inertiaTimer, &QTimer::timeout, this, &TouchGestureHandler::onInertiaFrame);
}

// ===== Mode =====

void TouchGestureHandler::setMode(TouchGestureMode mode)
{
    if (m_mode == mode) {
        return;
    }
    
    // Stop inertia timer if running (critical: must happen before mode change)
    // Without this, inertia would continue even after mode changes to Disabled
    if (m_inertiaTimer && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
        m_velocitySamples.clear();
    }
    
    // End any active gesture when mode changes
    if (m_panActive) {
        endTouchPan(false);  // No inertia when mode changes
    }
    if (m_pinchActive) {
        endTouchPinch();
    }
    
    // Clear all tracking state for clean start in new mode
    m_trackedTouchIds.clear();
    m_activeTouchPoints = 0;
    
    m_mode = mode;
}

// ===== Touch Event Handling =====

bool TouchGestureHandler::handleTouchEvent(QTouchEvent* event)
{
    if (m_mode == TouchGestureMode::Disabled) {
        return false;  // Don't consume event
    }
    
    const auto& points = event->points();
    
    // ===== Track touch point IDs across events (Android fix) =====
    // On Android, touch events may not include all active points in every event.
    // For example, a TouchUpdate may only include the point that moved, not
    // stationary points. By tracking IDs across events, we know the TRUE finger count.
    //
    // BUG-A005 v3: This fixes ~40% failure rate where 2-finger pinch was detected
    // as 1-finger pan because Android only reported one finger per event.
    for (const auto& pt : points) {
        if (pt.state() == QEventPoint::Pressed) {
            m_trackedTouchIds.insert(pt.id());
        } else if (pt.state() == QEventPoint::Released) {
            m_trackedTouchIds.remove(pt.id());
        }
    }
    
    // Use tracked ID count for true finger count
    m_activeTouchPoints = m_trackedTouchIds.size();
    
    // Also build activePoints list for accessing current event's point data
    // (still needed for position calculations)
    QVector<const QEventPoint*> activePoints;
    activePoints.reserve(points.size());
    for (const auto& pt : points) {
        if (pt.state() != QEventPoint::Released) {
            activePoints.append(&pt);
        }
    }
    
    // ===== Interrupt inertia on any new touch =====
    if (event->type() == QEvent::TouchBegin && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();  // Finalize the inertia pan
        m_velocitySamples.clear();
    }
    
    // ===== TouchBegin =====
    if (event->type() == QEvent::TouchBegin) {
        if (activePoints.size() == 1) {
            // TG.2.1: Single finger touch - start pan
            const auto& point = *activePoints.first();
            m_lastPos = point.position();
            m_panActive = true;
            m_pinchActive = false;  // Ensure clean state
            
            // Start velocity tracking
            m_velocitySamples.clear();
            m_velocityTimer.start();
            
            // Begin deferred pan gesture (captures frame for smooth scrolling)
            m_viewport->beginPanGesture();
            
            event->accept();
            return true;
        } else if (activePoints.size() == 2) {
            // TG.4: Two fingers touch simultaneously - start pinch directly
            // This is common on Android where both fingers can arrive in same event
            if (m_mode == TouchGestureMode::YAxisOnly) {
                // Y-axis only mode: ignore pinch, just accept the event
                event->accept();
                return true;
            }
            
            const auto& p1 = *activePoints[0];
            const auto& p2 = *activePoints[1];
            
            QPointF pos1 = p1.position();
            QPointF pos2 = p2.position();
            QPointF centroid = (pos1 + pos2) / 2.0;
            qreal distance = QLineF(pos1, pos2).length();
            
            if (distance < 1.0) {
                distance = 1.0;
            }
            
            m_panActive = false;  // Ensure clean state
            m_pinchActive = true;
            m_pinchStartDistance = distance;
            m_pinchCentroid = centroid;
            
            m_viewport->beginZoomGesture(centroid);
            
            event->accept();
            return true;
        } else if (activePoints.size() >= 3) {
            // TG.5: 3+ finger touch - start tap timer and suspend other gestures
            m_threeFingerTimer.start();
            m_threeFingerTimerActive = true;
            
            // Suspend any active gesture (will resume if fingers reduce)
            if (m_panActive) {
                endTouchPan(false);
            }
            if (m_pinchActive) {
                endTouchPinch();
            }
            
            event->accept();
            return true;
        }
    }
    
    // ===== TouchUpdate =====
    if (event->type() == QEvent::TouchUpdate) {
        // ===== Handle 3+ finger gestures =====
        if (m_activeTouchPoints >= 3) {
            // TG.5: Track when we reach 3 fingers (may be added one-by-one)
            if (!m_threeFingerTimerActive) {
                m_threeFingerTimer.start();
                m_threeFingerTimerActive = true;
            }
            
            // Suspend any active gesture while 3+ fingers are down
            if (m_panActive) {
                endTouchPan(false);
            }
            if (m_pinchActive) {
                endTouchPinch();
            }
            
            event->accept();
            return true;
        }
        
        // ===== Handle 3+→2 finger transition =====
        // Guard: need position data for both fingers
        if (m_activeTouchPoints == 2 && !m_pinchActive && !m_panActive && activePoints.size() >= 2) {
            // Coming from 3+ fingers, start pinch
            if (m_mode != TouchGestureMode::YAxisOnly) {
                const auto& p1 = *activePoints[0];
                const auto& p2 = *activePoints[1];
                
                QPointF pos1 = p1.position();
                QPointF pos2 = p2.position();
                QPointF centroid = (pos1 + pos2) / 2.0;
                qreal distance = QLineF(pos1, pos2).length();
                
                if (distance < 1.0) {
                    distance = 1.0;
                }
                
                m_pinchActive = true;
                m_pinchStartDistance = distance;
                m_pinchCentroid = centroid;
                m_viewport->beginZoomGesture(centroid);
            }
            
            event->accept();
            return true;
        }
        
        // ===== Handle 3+→1 or 2→1 finger transition =====
        // Guard: need at least 1 point's data
        if (m_activeTouchPoints == 1 && !m_panActive && !activePoints.isEmpty()) {
            // Coming from pinch or 3+ fingers, start pan with remaining finger
            if (m_pinchActive) {
                endTouchPinch();
            }
            
            const auto& point = *activePoints.first();
            m_lastPos = point.position();
            m_panActive = true;
            
            m_velocitySamples.clear();
            m_velocityTimer.start();
            m_viewport->beginPanGesture();
            
            event->accept();
            return true;
        }
        
        // TG.2.2: Single-finger pan update
        // Guard: need at least 1 point's data
        if (m_panActive && m_activeTouchPoints == 1 && !activePoints.isEmpty()) {
            const auto& point = *activePoints.first();
            QPointF currentPos = point.position();
            QPointF delta = currentPos - m_lastPos;
            
            // Track velocity for inertia (pixels per ms, negated for correct direction)
            qint64 elapsed = m_velocityTimer.elapsed();
            if (elapsed > 0) {
                // Negate velocity to match pan direction convention
                // Y-axis only mode: zero out X velocity
                qreal vx = (m_mode == TouchGestureMode::YAxisOnly) ? 0.0 : -delta.x() / elapsed;
                qreal vy = -delta.y() / elapsed;
                
                m_velocitySamples.append(QPointF(vx, vy));
                if (m_velocitySamples.size() > MAX_VELOCITY_SAMPLES) {
                    m_velocitySamples.removeFirst();
                }
                m_velocityTimer.restart();
            }
            
            // Convert pixel delta to document coords and NEGATE
            // When finger moves UP (negative delta.y), we want content to move UP
            // which means pan offset increases (same convention as wheel events)
            QPointF panDelta = -delta / m_viewport->zoomLevel();
            
            // Y-axis only mode: lock X axis
            if (m_mode == TouchGestureMode::YAxisOnly) {
                panDelta.setX(0);
            }
            
            // Update pan via deferred API (uses cached frame for smooth display)
            m_viewport->updatePanGesture(panDelta);
            
            m_lastPos = currentPos;
            event->accept();
            return true;
        }
        
        // ===== Handle 1→2 finger transition (pan to pinch) =====
        // Note: m_activeTouchPoints uses ID tracking, but we need position data for both fingers
        // Guard against case where event only has 1 point's data (wait for full event)
        if (m_activeTouchPoints == 2 && !m_pinchActive && activePoints.size() >= 2) {
            // Y-axis only mode: disable pinch-to-zoom
            if (m_mode == TouchGestureMode::YAxisOnly) {
                event->accept();
                return true;  // Consume but ignore
            }
            
            // Stop any running inertia first
            if (m_inertiaTimer->isActive()) {
                m_inertiaTimer->stop();
                m_viewport->endPanGesture();
                m_velocitySamples.clear();
            }
            
            // Transition from pan to pinch
            if (m_panActive) {
                endTouchPan(false);  // No inertia when transitioning to pinch
            }
            
            const auto& p1 = *activePoints[0];
            const auto& p2 = *activePoints[1];
            
            QPointF pos1 = p1.position();
            QPointF pos2 = p2.position();
            QPointF centroid = (pos1 + pos2) / 2.0;
            qreal distance = QLineF(pos1, pos2).length();
            
            if (distance < 1.0) {
                distance = 1.0;
            }
            
            // Start pinch gesture
            m_pinchActive = true;
            m_pinchStartDistance = distance;
            m_pinchCentroid = centroid;
            
            // Begin deferred zoom gesture at centroid
            m_viewport->beginZoomGesture(centroid);
            
            event->accept();
            return true;
        }
        
        // TG.4: Pinch-to-zoom update (already in pinch mode)
        // Guard: need position data for both fingers
        if (m_activeTouchPoints == 2 && m_pinchActive && activePoints.size() >= 2) {
            const auto& p1 = *activePoints[0];
            const auto& p2 = *activePoints[1];
            
            QPointF pos1 = p1.position();
            QPointF pos2 = p2.position();
            qreal distance = QLineF(pos1, pos2).length();
            
            // Avoid division by zero
            if (distance < 1.0) {
                distance = 1.0;
            }
            
            // Update pinch - use frame-to-frame ratio for smooth incremental zoom
            // Each frame: scaleFactor = currentDistance / previousDistance
            // This compounds correctly: total scale = product of all frame ratios
            qreal incrementalScale = distance / m_pinchStartDistance;
            
            // FIX: Use ORIGINAL centroid (m_pinchCentroid) instead of current centroid
            // When zoom center changes frame-to-frame, it creates an implicit pan effect
            // that's mathematically derived from the zoom transform, NOT from finger direction.
            // This causes counterintuitive "opposite direction" panning.
            // Using fixed center = predictable zoom behavior, no unexpected panning.
            m_viewport->updateZoomGesture(incrementalScale, m_pinchCentroid);
            
            // Update distance tracking for next frame's scale calculation
            m_pinchStartDistance = distance;
            // Note: NOT updating m_pinchCentroid - zoom stays centered on initial touch
            
            event->accept();
            return true;
        }
    }
    
    // ===== TouchEnd / TouchCancel =====
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        // TG.2.3: End pan gesture
        if (m_panActive) {
            // Start inertia only on normal end (not cancel)
            endTouchPan(event->type() != QEvent::TouchCancel);
        }
        
        if (m_pinchActive) {
            endTouchPinch();
        }
        
        // TG.5: Check for 3-finger tap
        // A valid tap requires: 3 fingers were down, duration < 300ms, all fingers released
        if (event->type() == QEvent::TouchEnd && m_threeFingerTimerActive) {
            // Check if all touch points are now released
            bool allReleased = (m_activeTouchPoints == 0);
            
            if (allReleased) {
                qint64 duration = m_threeFingerTimer.elapsed();
                if (duration > 0 && duration < TAP_MAX_DURATION_MS) {
                    on3FingerTap();
                }
            }
        }
        
        // Reset all tracking for next touch sequence
        m_threeFingerTimerActive = false;
        m_activeTouchPoints = 0;
        m_trackedTouchIds.clear();  // Clear ID tracking for next sequence
        event->accept();
        return true;
    }
    
    // Fallback - accept but don't claim handling
    event->accept();
    return true;
}

// ===== Gesture End Helpers =====

void TouchGestureHandler::endTouchPan(bool startInertia)
{
    if (!m_panActive) {
        return;
    }
    
    m_panActive = false;
    
    // TG.3: Calculate average velocity and potentially start inertia
    if (startInertia && !m_velocitySamples.isEmpty()) {
        // Calculate average velocity from recent samples (pixels per ms)
        QPointF avgVelocity(0, 0);
        for (const QPointF& velocity : m_velocitySamples) {
            avgVelocity += velocity;
        }
        avgVelocity /= m_velocitySamples.size();
        
        // Convert velocity from pixels/ms to document coords/ms
        m_inertiaVelocity = avgVelocity / m_viewport->zoomLevel();
        
        // Y-axis only mode: zero out X inertia
        if (m_mode == TouchGestureMode::YAxisOnly) {
            m_inertiaVelocity.setX(0);
        }
        
        // Check minimum velocity threshold (in document coords/ms)
        qreal speed = std::sqrt(m_inertiaVelocity.x() * m_inertiaVelocity.x() +
                                m_inertiaVelocity.y() * m_inertiaVelocity.y());
        
        if (speed > INERTIA_MIN_VELOCITY) {
            // Start inertia animation (keep using deferred pan)
            m_inertiaTimer->start(INERTIA_INTERVAL_MS);
            m_velocitySamples.clear();
            return;  // Don't end pan gesture yet - inertia will use it
        }
    }
    
    // No inertia - end pan gesture immediately
    m_viewport->endPanGesture();
    m_velocitySamples.clear();
}

void TouchGestureHandler::endTouchPinch()
{
    if (!m_pinchActive) {
        return;
    }
    
    m_pinchActive = false;
    m_viewport->endZoomGesture();
}

void TouchGestureHandler::on3FingerTap()
{
    #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[Touch] 3-finger tap detected - ready for future feature connection";
    #endif
}

// ===== Inertia Animation =====

void TouchGestureHandler::onInertiaFrame()
{
    // Apply friction to velocity
    m_inertiaVelocity *= INERTIA_FRICTION;
    
    // Check if velocity is below threshold
    qreal speed = std::sqrt(m_inertiaVelocity.x() * m_inertiaVelocity.x() +
                            m_inertiaVelocity.y() * m_inertiaVelocity.y());
    
    if (speed < INERTIA_MIN_VELOCITY) {
        // Stop inertia animation
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();
        m_velocitySamples.clear();
        return;
    }
    
    // Apply velocity as pan delta (velocity is in doc coords per ms, interval is in ms)
    QPointF panDelta = m_inertiaVelocity * INERTIA_INTERVAL_MS;
    m_viewport->updatePanGesture(panDelta);
}
