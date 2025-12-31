#include "TouchGestureHandler.h"
#include "DocumentViewport.h"
#include <QTouchEvent>
#include <QLineF>
#include <QDateTime>
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
    
    // End any active gesture when mode changes
    if (m_panActive) {
        endTouchPan(false);  // No inertia when mode changes
    }
    if (m_pinchActive) {
        endTouchPinch();
    }
    
    m_mode = mode;
}

// ===== Touch Event Handling =====

bool TouchGestureHandler::handleTouchEvent(QTouchEvent* event)
{
    if (m_mode == TouchGestureMode::Disabled) {
        return false;  // Don't consume event
    }
    
    const auto& points = event->points();
    m_activeTouchPoints = points.size();
    
    // ===== Interrupt inertia on any new touch =====
    if (event->type() == QEvent::TouchBegin && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();  // Finalize the inertia pan
        m_velocitySamples.clear();
    }
    
    // ===== TouchBegin =====
    if (event->type() == QEvent::TouchBegin) {
        if (points.size() == 1) {
            // TG.2.1: Single finger touch - start pan
            const auto& point = points.first();
            m_startPos = point.position();
            m_lastPos = point.position();
            m_panActive = true;
            
            // Start velocity tracking
            m_velocitySamples.clear();
            m_velocityTimer.start();
            
            // Begin deferred pan gesture (captures frame for smooth scrolling)
            m_viewport->beginPanGesture();
            
            event->accept();
            return true;
        } else if (points.size() == 3) {
            // TG.5: 3-finger touch - record start time for tap detection
            m_threeFingerTapStart = QDateTime::currentMSecsSinceEpoch();
            event->accept();
            return true;
        }
    }
    
    // ===== TouchUpdate =====
    if (event->type() == QEvent::TouchUpdate) {
        // TG.5: Track when we reach 3 fingers (fingers may be added one-by-one)
        if (m_activeTouchPoints == 3 && m_threeFingerTapStart == 0) {
            m_threeFingerTapStart = QDateTime::currentMSecsSinceEpoch();
        }
        
        // TG.2.2: Single-finger pan update
        if (m_panActive && m_activeTouchPoints == 1) {
            const auto& point = points.first();
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
        
        // TG.4: Pinch-to-zoom update
        if (m_activeTouchPoints == 2) {
            // Y-axis only mode: disable pinch-to-zoom
            if (m_mode == TouchGestureMode::YAxisOnly) {
                event->accept();
                return true;  // Consume but ignore
            }
            
            // Need exactly 2 points
            if (points.size() < 2) {
                event->accept();
                return true;
            }
            
            const auto& p1 = points[0];
            const auto& p2 = points[1];
            
            QPointF pos1 = p1.position();
            QPointF pos2 = p2.position();
            QPointF centroid = (pos1 + pos2) / 2.0;
            qreal distance = QLineF(pos1, pos2).length();
            
            // Avoid division by zero
            if (distance < 1.0) {
                distance = 1.0;
            }
            
            if (!m_pinchActive) {
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
                
                // Start pinch gesture
                m_pinchActive = true;
                m_pinchStartZoom = m_viewport->zoomLevel();
                m_pinchStartDistance = distance;
                m_pinchCentroid = centroid;
                
                // Begin deferred zoom gesture at centroid
                m_viewport->beginZoomGesture(centroid);
            } else {
                // Update pinch - use frame-to-frame ratio for smooth incremental zoom
                // Each frame: scaleFactor = currentDistance / previousDistance
                // This compounds correctly: total scale = product of all frame ratios
                qreal incrementalScale = distance / m_pinchStartDistance;
                m_viewport->updateZoomGesture(incrementalScale, centroid);
                
                // Update tracking for next frame
                m_pinchStartDistance = distance;
                m_pinchCentroid = centroid;
            }
            
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
        // A valid tap requires: 3 fingers were down (m_threeFingerTapStart > 0), 
        // duration < 300ms, and now all fingers are released
        if (event->type() == QEvent::TouchEnd && m_threeFingerTapStart > 0) {
            // Check if all touch points are now released
            bool allReleased = true;
            for (const auto& pt : points) {
                if (pt.state() != QEventPoint::Released) {
                    allReleased = false;
                    break;
                }
            }
            
            if (allReleased) {
                qint64 duration = QDateTime::currentMSecsSinceEpoch() - m_threeFingerTapStart;
                if (duration > 0 && duration < TAP_MAX_DURATION_MS) {
                    on3FingerTap();
                }
            }
        }
        
        // Reset tap tracking
        m_threeFingerTapStart = 0;
        m_activeTouchPoints = 0;
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
    qDebug() << "[Touch] 3-finger tap detected - ready for future feature connection";
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
