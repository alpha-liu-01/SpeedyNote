#include "TouchGestureHandler.h"
#include "DocumentViewport.h"
#include <QTouchEvent>
#include <QLineF>
#include <QDateTime>
#include <QDebug>
#include <cmath>  // For std::sqrt, std::abs

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>

// ===== Native Android Touch Query Functions (JNI) =====
// These functions query the Java side for ground-truth touch state.
// Qt's touch event layer can become corrupted after sleep/wake on Android,
// so we use native Android touch tracking as verification.

namespace {

/**
 * Query native Android touch count.
 * @return Number of fingers currently touching, or -1 if JNI call fails.
 */
int getNativeTouchCount()
{
    jint result = QJniObject::callStaticMethod<jint>(
        "org/speedynote/app/SpeedyNoteActivity",
        "getNativeTouchCount", "()I");
    return static_cast<int>(result);
}

/**
 * Query time since last native touch event.
 * @return Milliseconds since last touch, or -1 if JNI call fails.
 */
qint64 getTimeSinceLastNativeTouch()
{
    jlong result = QJniObject::callStaticMethod<jlong>(
        "org/speedynote/app/SpeedyNoteActivity",
        "getTimeSinceLastTouch", "()J");
    return static_cast<qint64>(result);
}

/**
 * Query native touch positions for first 2 touch points.
 * @param pos1 Output: position of first touch point
 * @param pos2 Output: position of second touch point
 * @return true if positions retrieved successfully, false on JNI error
 */
bool getNativeTouchPositions(QPointF& pos1, QPointF& pos2)
{
    QJniEnvironment env;
    jclass activityClass = env->FindClass("org/speedynote/app/SpeedyNoteActivity");
    if (!activityClass) return false;
    
    jmethodID method = env->GetStaticMethodID(activityClass, "getNativeTouchPositions", "()[F");
    if (!method) {
        env->DeleteLocalRef(activityClass);
        return false;
    }
    
    jfloatArray result = (jfloatArray)env->CallStaticObjectMethod(activityClass, method);
    env->DeleteLocalRef(activityClass);  // No longer needed
    
    if (!result) return false;
    
    // Verify array has expected size (4 elements: x1, y1, x2, y2)
    jsize arrayLen = env->GetArrayLength(result);
    if (arrayLen < 4) {
        env->DeleteLocalRef(result);
        return false;
    }
    
    jfloat* positions = env->GetFloatArrayElements(result, nullptr);
    if (!positions) {
        env->DeleteLocalRef(result);
        return false;
    }
    
    pos1 = QPointF(positions[0], positions[1]);
    pos2 = QPointF(positions[2], positions[3]);
    
    env->ReleaseFloatArrayElements(result, positions, JNI_ABORT);  // JNI_ABORT: don't copy back
    env->DeleteLocalRef(result);  // Clean up local reference
    return true;
}

}  // anonymous namespace
#endif  // Q_OS_ANDROID

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

TouchGestureHandler::~TouchGestureHandler()
{
    // Stop inertia timer to prevent callbacks after destruction
    if (m_inertiaTimer) {
        m_inertiaTimer->stop();
        m_inertiaTimer->disconnect();
    }
}

// ===== Reset =====

void TouchGestureHandler::reset()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[TouchGestureHandler] reset() called"
             << "panActive:" << m_panActive
             << "pinchActive:" << m_pinchActive
             << "inertiaActive:" << (m_inertiaTimer && m_inertiaTimer->isActive());
#endif
    
    // Stop inertia timer
    if (m_inertiaTimer && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
    }
    
    // Clear gesture state (don't call endTouchPan/endTouchPinch as viewport may be in invalid state)
    m_panActive = false;
    m_pinchActive = false;
    
    // Clear position-based state (prevents stale values after resume)
    m_lastPos = QPointF();
    m_pinchStartDistance = 0;
    
    // Clear zoom smoothing state
    m_initialDistance = 0;
    m_zoomActivated = false;
    m_smoothedScale = 1.0;
    
    // Clear velocity samples
    m_velocitySamples.clear();
    m_inertiaVelocity = QPointF();
    
    // Clear tracking state
    m_activeTouchPoints = 0;
    m_trackedTouchIds.clear();
    m_lastTouchPositions.clear();
    m_touchIdLastSeenTime.clear();
    
    // Clear 3-finger tap state
    m_threeFingerTimerActive = false;
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
    m_lastTouchPositions.clear();
    m_touchIdLastSeenTime.clear();
    m_activeTouchPoints = 0;
    
    // Clear zoom smoothing state
    m_initialDistance = 0;
    m_zoomActivated = false;
    m_smoothedScale = 1.0;
    
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
    //
    // BUG-A005 v4: On Android, spurious TouchCancel events can be generated when
    // input focus changes (e.g., stylus hovers to toolbar). These cancels can
    // corrupt the ID tracking state. On TouchBegin, we reset tracking to ensure
    // a clean start for the new gesture sequence.
    if (event->type() == QEvent::TouchBegin) {
        // NUCLEAR RESET - forget everything from before
        // This prevents stale state from previous (possibly corrupted) sequence
        m_trackedTouchIds.clear();
        m_lastTouchPositions.clear();
        m_touchIdLastSeenTime.clear();
        m_activeTouchPoints = 0;
        
        // Stop inertia if running (must happen first)
        if (m_inertiaTimer && m_inertiaTimer->isActive()) {
            m_inertiaTimer->stop();
            m_velocitySamples.clear();
        }
        
        // Clear handler's gesture flags
        m_panActive = false;
        m_pinchActive = false;
        
        // CRITICAL: Also reset the viewport's gesture state
        // Without this, the viewport keeps a stale grabbed pixmap and
        // subsequent gestures would transform it instead of capturing fresh.
        // This was the difference between Android and desktop behavior.
        if (m_viewport->isPanGestureActive()) {
            m_viewport->endPanGesture();
        }
        if (m_viewport->isZoomGestureActive()) {
            m_viewport->endZoomGesture();
        }
        
#ifdef Q_OS_ANDROID
        // Debug: Log native touch count at gesture start for diagnostics
        int nativeCount = getNativeTouchCount();
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[TouchGestureHandler] TouchBegin NUCLEAR RESET"
                 << "nativeCount:" << nativeCount;
#else
        Q_UNUSED(nativeCount);
#endif
#endif
    }
    
    // BUG-A005 v6/v7: Track when each ID was last seen
    // Used to detect stale IDs that Android "forgot" to send Released for
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    for (const auto& pt : points) {
        int id = pt.id();
        
        if (pt.state() == QEventPoint::Pressed) {
            m_trackedTouchIds.insert(id);
            m_lastTouchPositions.insert(id, pt.position());
            m_touchIdLastSeenTime.insert(id, currentTime);
        } else if (pt.state() == QEventPoint::Released) {
            m_trackedTouchIds.remove(id);
            m_lastTouchPositions.remove(id);
            m_touchIdLastSeenTime.remove(id);
        } else {
            // Updated or Stationary - cache latest position and update timestamp
            m_lastTouchPositions.insert(id, pt.position());
            m_touchIdLastSeenTime.insert(id, currentTime);
        }
    }
    
    // BUG-A005 v7: Time-based stale detection
    // After sleep/wake, Android may not send Released events for lifted fingers.
    // If a tracked ID hasn't been seen for STALE_TIMEOUT_MS, assume it lifted.
    // Using time-based detection prevents removing stationary fingers that are
    // simply not included in fast-firing move events.
    QVector<int> staleIds;
    for (int trackedId : m_trackedTouchIds) {
        qint64 lastSeen = m_touchIdLastSeenTime.value(trackedId, 0);
        qint64 elapsed = currentTime - lastSeen;
        if (elapsed > STALE_TIMEOUT_MS) {
            staleIds.append(trackedId);
        }
    }
    
    // Remove stale IDs (can't modify set while iterating)
    bool staleRemovalDuringPinch = false;
    for (int staleId : staleIds) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[TouchGestureHandler] Removing stale touch ID" << staleId
                 << "(not seen for" << STALE_TIMEOUT_MS << "ms)";
#endif
        m_trackedTouchIds.remove(staleId);
        m_lastTouchPositions.remove(staleId);
        m_touchIdLastSeenTime.remove(staleId);
        
        if (m_pinchActive) {
            staleRemovalDuringPinch = true;
        }
    }
    
    // Use tracked ID count for true finger count
    m_activeTouchPoints = m_trackedTouchIds.size();
    
    // BUG-A005 v7: If stale removal during pinch dropped us to 1 finger,
    // now we're confident the second finger is truly gone (500ms passed).
    // End pinch and transition to pan with remaining finger.
    if (staleRemovalDuringPinch && m_activeTouchPoints == 1 && m_pinchActive) {
        endTouchPinch();
        // Note: Pan transition will happen in the 2→1 block below since m_pinchActive is now false
    }
    
    // Also build activePoints list for accessing current event's point data
    // (still needed for position calculations)
    QVector<const QEventPoint*> activePoints;
    activePoints.reserve(points.size());
    for (const auto& pt : points) {
        if (pt.state() != QEventPoint::Released) {
            activePoints.append(&pt);
        }
    }
    
#ifdef Q_OS_ANDROID
    // ===== Verify Qt touch count with native Android =====
    // After sleep/wake, Qt's touch tracking can become corrupted.
    // If native Android says we have 2 fingers but Qt only reports 1,
    // use native positions to ensure the pinch gesture works.
    int nativeCount = getNativeTouchCount();
    qint64 timeSinceNative = getTimeSinceLastNativeTouch();
    
    // Only trust native if data is fresh (< 100ms old)
    if (nativeCount > 0 && timeSinceNative >= 0 && timeSinceNative < 100) {
        int qtActivePoints = activePoints.size();
        
        // If native says 2+ fingers but Qt only has 0-1, Qt lost a touch point
        if (nativeCount >= 2 && qtActivePoints < 2) {
            // Get native positions (in screen/activity coordinates, physical pixels)
            QPointF nativePos1, nativePos2;
            if (getNativeTouchPositions(nativePos1, nativePos2)) {
                // Convert from screen coordinates to widget-local coordinates
                // Native positions are in PHYSICAL pixels relative to the screen
                // mapToGlobal returns LOGICAL pixels
                // We need widget-local LOGICAL pixels
                QPointF widgetGlobalPos = m_viewport->mapToGlobal(QPointF(0, 0));
                qreal dpr = m_viewport->devicePixelRatio();
                
                // Convert: divide by DPR first (physical→logical), then subtract widget offset
                // Order matters! mapToGlobal returns logical coords, so we must convert
                // native to logical before subtracting.
                QPointF localPos1 = nativePos1 / dpr - widgetGlobalPos;
                QPointF localPos2 = nativePos2 / dpr - widgetGlobalPos;
                
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[TouchGestureHandler] MISMATCH: native=" << nativeCount
                         << "qt=" << qtActivePoints
                         << "nativePos1:" << nativePos1 << "nativePos2:" << nativePos2
                         << "localPos1:" << localPos1 << "localPos2:" << localPos2
                         << "widgetGlobal:" << widgetGlobalPos << "dpr:" << dpr;
#endif
                // Force 2-finger gesture with converted positions
                return handleTwoFingerGestureNative(event, localPos1, localPos2);
            }
        }
    }
#endif
    
    // ===== Interrupt inertia on any new touch =====
    if (event->type() == QEvent::TouchBegin && m_inertiaTimer->isActive()) {
        m_inertiaTimer->stop();
        m_viewport->endPanGesture();  // Finalize the inertia pan
        m_velocitySamples.clear();
    }
    
    // ===== TouchBegin =====
    if (event->type() == QEvent::TouchBegin) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[TouchGestureHandler] TouchBegin"
                 << "activePoints:" << activePoints.size()
                 << "trackedIds:" << m_trackedTouchIds.size()
                 << "activeTouchPoints:" << m_activeTouchPoints;
#endif
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
            m_initialDistance = distance;   // For zoom threshold calculation
            m_zoomActivated = false;        // Zoom starts inactive (dead zone)
            m_smoothedScale = 1.0;          // Reset smoothed scale
            
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
                m_initialDistance = distance;   // For zoom threshold calculation
                m_zoomActivated = false;        // Zoom starts inactive (dead zone)
                m_smoothedScale = 1.0;          // Reset smoothed scale
                m_viewport->beginZoomGesture(centroid);
            }
            
            event->accept();
            return true;
        }
        
        // ===== Handle 3+→1 or 2→1 finger transition =====
        // Guard: need at least 1 point's data
        if (m_activeTouchPoints == 1 && !m_panActive && !activePoints.isEmpty()) {
            // If coming from pinch (2→1 transition), end pinch and start pan
            if (m_pinchActive) {
                endTouchPinch();  // End the zoom gesture
            }
            
            // Start pan with remaining finger
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
#ifdef SPEEDYNOTE_DEBUG
        // Log mismatch between tracked IDs and active points (helps diagnose Android issues)
        if (m_activeTouchPoints == 2 && !m_pinchActive && activePoints.size() < 2) {
            qDebug() << "[TouchGestureHandler] MISMATCH: trackedIds=" << m_activeTouchPoints
                     << "but activePoints=" << activePoints.size()
                     << "(waiting for full event data)";
        }
#endif
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
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[TouchGestureHandler] Starting pinch (1→2 transition)"
                     << "trackedIds:" << m_trackedTouchIds.size()
                     << "viewportGestureActive:" << m_viewport->isGestureActive();
#endif
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
            m_initialDistance = distance;   // For zoom threshold calculation
            m_zoomActivated = false;        // Zoom starts inactive (dead zone)
            m_smoothedScale = 1.0;          // Reset smoothed scale
            
            // Begin deferred zoom gesture at centroid
            m_viewport->beginZoomGesture(centroid);
            
            event->accept();
            return true;
        }
        
        // TG.4: Pinch-to-zoom update (already in pinch mode)
        // BUG-A005 v5: After sleep/wake, Android may only send 1 point per TouchUpdate
        // even when 2 fingers are down. Use cached positions to get both finger locations.
        if (m_activeTouchPoints == 2 && m_pinchActive) {
            // Get positions for both fingers - use event data if available, else cached positions
            QPointF pos1, pos2;
            bool havePos1 = false, havePos2 = false;
            int pos1Id = -1;  // Track which ID we used for pos1
            
            // Get positions from current event (prefer fresh data)
            for (const auto* pt : activePoints) {
                if (!havePos1) {
                    pos1 = pt->position();
                    pos1Id = pt->id();
                    havePos1 = true;
                } else if (!havePos2) {
                    pos2 = pt->position();
                    havePos2 = true;
                    break;
                }
            }
            
            // If we only got 1 position from event, get the other finger's position from cache
            if (havePos1 && !havePos2 && m_lastTouchPositions.size() >= 2) {
                // Find the cached position for the OTHER finger (not pos1Id)
                for (auto it = m_lastTouchPositions.constBegin(); it != m_lastTouchPositions.constEnd(); ++it) {
                    if (it.key() != pos1Id) {
                        pos2 = it.value();
                        havePos2 = true;
                        break;
                    }
                }
            }
            
            // Only process if we have both positions
            if (havePos1 && havePos2) {
#ifdef SPEEDYNOTE_DEBUG
                static int pinchUpdateCount = 0;
                pinchUpdateCount++;
                if (pinchUpdateCount % 20 == 1) {  // Log every 20th to avoid spam
                    qDebug() << "[TouchGestureHandler] Pinch update #" << pinchUpdateCount
                             << "activePoints:" << activePoints.size()
                             << "cachedPositions:" << m_lastTouchPositions.size()
                             << "zoomActivated:" << m_zoomActivated;
                }
#endif
                qreal distance = QLineF(pos1, pos2).length();
                
                // Avoid division by zero
                if (distance < 1.0) {
                    distance = 1.0;
                }
                
                // Check if zoom should be activated (activation threshold)
                // This prevents accidental zoom during 2-finger pan
                if (!m_zoomActivated && m_initialDistance > 0) {
                    qreal distanceChange = std::abs(distance - m_initialDistance) / m_initialDistance;
                    if (distanceChange > ZOOM_ACTIVATION_THRESHOLD) {
                        m_zoomActivated = true;
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "[TouchGestureHandler] Zoom activated! change:" << distanceChange;
#endif
                    }
                }
                
                // Calculate frame-to-frame scale ratio
                qreal rawScale = distance / m_pinchStartDistance;
                
                // Apply zoom only if activated, otherwise scale = 1.0 (no zoom)
                qreal targetScale = 1.0;
                if (m_zoomActivated) {
                    // Apply scale dead zone: treat values very close to 1.0 as exactly 1.0
                    // This prevents zoom jitter from small finger distance variations
                    if (std::abs(rawScale - 1.0) > ZOOM_SCALE_DEAD_ZONE) {
                        targetScale = rawScale;
                    }
                }
                
                // Apply exponential smoothing for smoother zoom
                // smoothedScale = previousSmoothed * (1-alpha) + target * alpha
                m_smoothedScale = m_smoothedScale * (1.0 - ZOOM_SMOOTHING_FACTOR) 
                                + targetScale * ZOOM_SMOOTHING_FACTOR;
                
                // Pass CURRENT centroid to viewport for combined zoom+pan
                // The viewport tracks centroid movement (delta from initial) and applies pan
                // Zoom stays centered on the moving centroid, which feels natural
                QPointF currentCentroid = (pos1 + pos2) / 2.0;
                m_viewport->updateZoomGesture(m_smoothedScale, currentCentroid);
                
                // Update distance tracking for next frame's scale calculation
                m_pinchStartDistance = distance;
            }
#ifdef SPEEDYNOTE_DEBUG
            else {
                qDebug() << "[TouchGestureHandler] Pinch update SKIPPED - missing position data"
                         << "havePos1:" << havePos1 << "havePos2:" << havePos2
                         << "activePoints:" << activePoints.size()
                         << "cachedPositions:" << m_lastTouchPositions.size();
            }
#endif
            
            event->accept();
            return true;
        }
    }
    
    // ===== TouchEnd / TouchCancel =====
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
#ifdef Q_OS_ANDROID
        // Verify with native Android before ending gesture
        // Qt may send spurious TouchEnd when native still has fingers down
        if (event->type() == QEvent::TouchEnd) {
            int nativeCount = getNativeTouchCount();
            qint64 timeSinceNative = getTimeSinceLastNativeTouch();
            
            // Only trust native if data is fresh
            if (nativeCount >= 2 && timeSinceNative >= 0 && timeSinceNative < 100) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[TouchGestureHandler] TouchEnd but native has" << nativeCount
                         << "touches - ignoring spurious end event";
#endif
                event->accept();
                return true;  // Ignore spurious TouchEnd, keep gesture active
            }
        }
#endif
        
#ifdef SPEEDYNOTE_DEBUG
        if (event->type() == QEvent::TouchCancel) {
            qDebug() << "[TouchGestureHandler] TouchCancel received!"
                     << "panActive:" << m_panActive
                     << "pinchActive:" << m_pinchActive
                     << "trackedIds:" << m_trackedTouchIds.size();
        }
#endif
        
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
        // Note: Even if this is a spurious TouchCancel, the next TouchBegin will
        // reset tracking anyway (BUG-A005 v4 fix), so clearing here is safe.
        m_threeFingerTimerActive = false;
        m_activeTouchPoints = 0;
        m_trackedTouchIds.clear();
        m_lastTouchPositions.clear();
        m_touchIdLastSeenTime.clear();
        
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

#ifdef Q_OS_ANDROID
// ===== Native Android Two-Finger Gesture Handler =====

bool TouchGestureHandler::handleTwoFingerGestureNative(QTouchEvent* event,
                                                        QPointF pos1, QPointF pos2)
{
    QPointF centroid = (pos1 + pos2) / 2.0;
    qreal distance = QLineF(pos1, pos2).length();
    if (distance < 1.0) distance = 1.0;
    
    if (!m_pinchActive) {
        // Transition from pan to pinch (or start fresh pinch)
        if (m_panActive) {
            endTouchPan(false);  // No inertia when transitioning
        }
        
        // Stop inertia if running
        if (m_inertiaTimer && m_inertiaTimer->isActive()) {
            m_inertiaTimer->stop();
            m_viewport->endPanGesture();
            m_velocitySamples.clear();
        }
        
        m_pinchActive = true;
        m_pinchStartDistance = distance;
        m_initialDistance = distance;
        m_zoomActivated = false;
        m_smoothedScale = 1.0;
        m_viewport->beginZoomGesture(centroid);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[TouchGestureHandler] Started NATIVE 2-finger gesture"
                 << "distance:" << distance << "centroid:" << centroid;
#endif
    } else {
        // Update existing pinch - apply zoom smoothing
        qreal rawScale = distance / m_pinchStartDistance;
        
        // Activate zoom if threshold exceeded
        if (!m_zoomActivated && m_initialDistance > 0) {
            qreal distanceChange = std::abs(distance - m_initialDistance) / m_initialDistance;
            if (distanceChange > ZOOM_ACTIVATION_THRESHOLD) {
                m_zoomActivated = true;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[TouchGestureHandler] NATIVE zoom activated! change:" << distanceChange;
#endif
            }
        }
        
        // Apply scale dead zone and smoothing
        qreal targetScale = 1.0;
        if (m_zoomActivated) {
            if (std::abs(rawScale - 1.0) > ZOOM_SCALE_DEAD_ZONE) {
                targetScale = rawScale;
            }
        }
        
        m_smoothedScale = m_smoothedScale * (1.0 - ZOOM_SMOOTHING_FACTOR) 
                        + targetScale * ZOOM_SMOOTHING_FACTOR;
        
        m_viewport->updateZoomGesture(m_smoothedScale, centroid);
        m_pinchStartDistance = distance;
    }
    
    event->accept();
    return true;
}
#endif  // Q_OS_ANDROID
