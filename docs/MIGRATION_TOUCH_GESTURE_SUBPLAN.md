# Touch Gesture Hub - Implementation Subplan

## Overview

This document details the implementation plan for the Touch Gesture Hub. The goal is to provide smooth, touch-friendly navigation (pan/zoom) for tablet PC users while maintaining stylus drawing capability.

**Status**: ✅ COMPLETE - All phases implemented (TG.1 through TG.6)

### Current State (Verified Working)

| Feature | Status | Notes |
|---------|--------|-------|
| Single-finger pan | ✅ Working | Correct direction, smooth deferred rendering |
| Y-axis only mode | ✅ Working | Locks horizontal movement |
| Inertia scrolling | ✅ Working | Natural momentum with friction decay |
| Mode toggle (toolbar) | ✅ Working | Cycles Disabled → YAxisOnly → Full |
| Pinch-to-zoom | ✅ Working | Zooms at centroid, disabled in YAxisOnly mode |
| 3-finger tap | ✅ Working | Debug message on detection, connect features later |

---

## Design Decisions Summary

### Touch Gesture Modes

| Mode | Single-finger Pan | Pinch-to-zoom | Inertia Direction |
|------|-------------------|---------------|-------------------|
| **Disabled** | ❌ | ❌ | N/A |
| **YAxisOnly** | ✅ Y-axis only | ❌ | Y-axis only |
| **Full** | ✅ X and Y | ✅ (at centroid) | X and Y |

### Key Behaviors

| Aspect | Decision |
|--------|----------|
| Palm rejection | Deferred to OS (KDE Plasma, Windows, etc.) |
| Touch drawing | Banned (touch-synthesized mouse events rejected) |
| Two-finger pan | Not needed (single-finger pan is sufficient) |
| Pan start | Immediate (no delay - palm rejection already filters) |
| Touch + Stylus conflict | Finish touch pan first, then stylus takes over |
| Deferred rendering | Touch gestures trigger optimization (like Shift+wheel / Ctrl+wheel) |
| 3-finger tap | Debug message for now (connect other features later) |
| Trackpad | Already works via wheel events, no changes needed |

---

## Architecture (REVISED)

The Touch Gesture Hub is implemented as a **separate class** (`TouchGestureHandler`) to keep `DocumentViewport` focused and maintainable.

### File Structure

```
source/core/
├── DocumentViewport.h/.cpp       (owns handler, forwards events, exposes minimal API)
├── TouchGestureHandler.h/.cpp    (NEW - all touch logic, state, inertia)
```

### Class Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        MainWindow                                │
│  ┌───────────────┐                                              │
│  │ Touch Toggle  │─────────────────────────────────────────────┐│
│  │ (3-mode)      │                                             ││
│  └───────────────┘                                             ││
└────────────────────────────────────────────────────────────────┘│
                                                                   │
┌──────────────────────────────────────────────────────────────────┼───┐
│                     DocumentViewport                              │   │
│                                                                   ▼   │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  Minimal touch integration:                                    │  │
│  │  - TouchGestureHandler* m_touchHandler                         │  │
│  │  - event() override → forwards to handler                      │  │
│  │  - setTouchGestureMode() → delegates to handler                │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                              │                                        │
│                              │ calls existing APIs:                   │
│                              │ beginPanGesture(), updatePanGesture()  │
│                              │ beginZoomGesture(), updateZoomGesture()│
│                              ▼                                        │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │         Deferred Rendering API (existing, unchanged)           │  │
│  │  - ViewportGestureState                                        │  │
│  │  - cachedFrame, targetPan, targetZoom                          │  │
│  │  - gestureTimeoutTimer                                         │  │
│  └────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────┐
│              TouchGestureHandler (NEW FILE)                           │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  State:                                                         │  │
│  │  - TouchGestureMode m_mode                                      │  │
│  │  - bool m_panActive, m_pinchActive                              │  │
│  │  - QPointF m_lastPos, m_startPos                                │  │
│  │  - qreal m_pinchStartZoom, m_pinchStartDistance                 │  │
│  │  - int m_activeTouchPoints                                      │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  Velocity Tracking:                                             │  │
│  │  - QVector<VelocitySample> m_velocitySamples                    │  │
│  │  - QElapsedTimer m_velocityTimer                                │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  Inertia Animation:                                             │  │
│  │  - QTimer* m_inertiaTimer                                       │  │
│  │  - QPointF m_inertiaVelocity                                    │  │
│  │  - onInertiaFrame() slot                                        │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  Methods:                                                       │  │
│  │  - bool handleTouchEvent(QTouchEvent*)                          │  │
│  │  - void setMode(TouchGestureMode)                               │  │
│  │  - void endTouchPan(bool startInertia)                          │  │
│  │  - void endTouchPinch()                                         │  │
│  │  - void on3FingerTap()                                          │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  Viewport Interface (pointer to owner):                         │  │
│  │  - DocumentViewport* m_viewport                                 │  │
│  │  - Calls: m_viewport->beginPanGesture()                         │  │
│  │  - Calls: m_viewport->updatePanGesture(delta)                   │  │
│  │  - Calls: m_viewport->endPanGesture()                           │  │
│  │  - Calls: m_viewport->beginZoomGesture(center)                  │  │
│  │  - Calls: m_viewport->updateZoomGesture(scale, center)          │  │
│  │  - Calls: m_viewport->endZoomGesture()                          │  │
│  │  - Reads: m_viewport->zoomLevel()                               │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
```

### Benefits of This Architecture

1. **Separation of concerns**: Touch logic is isolated from rendering/coordinate code
2. **Maintainability**: `DocumentViewport` stays ~6000 lines, not growing to 6500+
3. **Testability**: Could unit test `TouchGestureHandler` in isolation
4. **Consistency**: Matches pattern of `DebugOverlay`, `LayerPanel` as separate modules
5. **Reusability**: Could potentially reuse handler in other widgets

---

## Task Breakdown

### Phase TG.1: Basic Infrastructure

#### Task TG.1.1: Enable Touch Event Reception ✅ COMPLETE

**Goal**: Allow `DocumentViewport` to receive touch events.

**Changes Made**:
- `DocumentViewport.h`: Added `TouchGestureMode` enum, `m_touchGestureMode` member, `setTouchGestureMode()` method
- `DocumentViewport.cpp`: Changed `setAttribute(Qt::WA_AcceptTouchEvents, true)`

---

#### Task TG.1.R: Refactor - Create TouchGestureHandler Class ✅ COMPLETE

**Goal**: Move touch state from `DocumentViewport` to new `TouchGestureHandler` class.

**Step 1: Create new files**

`source/core/TouchGestureHandler.h`:
```cpp
#pragma once

#include <QObject>
#include <QPointF>
#include <QVector>
#include <QElapsedTimer>
#include <QTimer>

// Forward declarations
class QTouchEvent;
class DocumentViewport;

// TouchGestureMode enum (shared definition)
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,
    YAxisOnly,
    Full
};
#endif

/**
 * @brief Handles touch gestures for DocumentViewport.
 * 
 * Processes touch events and translates them into pan/zoom operations
 * by calling the viewport's deferred gesture API.
 */
class TouchGestureHandler : public QObject
{
    Q_OBJECT

public:
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
     */
    void setMode(TouchGestureMode mode);
    
    /**
     * @brief Get the current touch gesture mode.
     */
    TouchGestureMode mode() const { return m_mode; }
    
    /**
     * @brief Check if a touch gesture is currently active.
     */
    bool isActive() const { return m_panActive || m_pinchActive; }

private slots:
    void onInertiaFrame();

private:
    // Viewport reference (not owned)
    DocumentViewport* m_viewport;
    
    // Mode
    TouchGestureMode m_mode = TouchGestureMode::Disabled;
    
    // Single-finger pan tracking
    bool m_panActive = false;
    QPointF m_lastPos;
    QPointF m_startPos;
    
    // Pinch-to-zoom tracking
    bool m_pinchActive = false;
    qreal m_pinchStartZoom = 1.0;
    qreal m_pinchStartDistance = 0;
    QPointF m_pinchCentroid;
    
    // Velocity tracking for inertia
    struct VelocitySample {
        QPointF velocity;  // pixels per millisecond
        qint64 timestamp;
    };
    QVector<VelocitySample> m_velocitySamples;
    QElapsedTimer m_velocityTimer;
    static constexpr int MAX_VELOCITY_SAMPLES = 5;
    
    // Inertia animation
    QTimer* m_inertiaTimer = nullptr;
    QPointF m_inertiaVelocity;
    static constexpr qreal INERTIA_FRICTION = 0.95;
    static constexpr qreal INERTIA_MIN_VELOCITY = 0.05;
    static constexpr int INERTIA_INTERVAL_MS = 16;
    
    // Multi-touch tracking
    int m_activeTouchPoints = 0;
    
    // 3-finger tap detection
    qint64 m_threeFingerTapStart = 0;
    static constexpr qint64 TAP_MAX_DURATION_MS = 300;
    
    // Helper methods
    void endTouchPan(bool startInertia);
    void endTouchPinch();
    void on3FingerTap();
};
```

`source/core/TouchGestureHandler.cpp`:
```cpp
#include "TouchGestureHandler.h"
#include "DocumentViewport.h"
#include <QTouchEvent>
#include <QLineF>
#include <QDateTime>
#include <QDebug>
#include <cmath>

TouchGestureHandler::TouchGestureHandler(DocumentViewport* viewport, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
{
    // Inertia animation timer
    m_inertiaTimer = new QTimer(this);
    m_inertiaTimer->setTimerType(Qt::PreciseTimer);
    connect(m_inertiaTimer, &QTimer::timeout, this, &TouchGestureHandler::onInertiaFrame);
}

void TouchGestureHandler::setMode(TouchGestureMode mode)
{
    if (m_mode == mode) return;
    
    // End any active gesture when mode changes
    if (m_panActive) {
        endTouchPan(false);  // No inertia
    }
    if (m_pinchActive) {
        endTouchPinch();
    }
    
    m_mode = mode;
}

bool TouchGestureHandler::handleTouchEvent(QTouchEvent* event)
{
    if (m_mode == TouchGestureMode::Disabled) {
        return false;  // Don't consume event
    }
    
    // Implementation in TG.2, TG.3, TG.4
    // For now, just accept the event
    event->accept();
    return true;
}

void TouchGestureHandler::endTouchPan(bool startInertia)
{
    // Implementation in TG.2.3, TG.3
}

void TouchGestureHandler::endTouchPinch()
{
    // Implementation in TG.4.2
}

void TouchGestureHandler::on3FingerTap()
{
    qDebug() << "[Touch] 3-finger tap detected";
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
    
    QPointF panDelta = m_inertiaVelocity * INERTIA_INTERVAL_MS;
    m_viewport->updatePanGesture(panDelta);
}
```

**Step 2: Update DocumentViewport**

Remove from `DocumentViewport.h`:
- All touch state members added in TG.1.2 (pan tracking, pinch tracking, velocity, inertia, etc.)
- `onInertiaFrame()` slot

Add to `DocumentViewport.h`:
```cpp
// Forward declaration
class TouchGestureHandler;

// In private section:
TouchGestureHandler* m_touchHandler = nullptr;
```

Update `DocumentViewport.cpp`:
- Remove inertia timer initialization
- Remove `onInertiaFrame()` implementation
- Add in constructor: `m_touchHandler = new TouchGestureHandler(this, this);`
- Update `setTouchGestureMode()` to delegate: `m_touchHandler->setMode(mode);`

**Step 3: Add event() override in DocumentViewport**

```cpp
// In DocumentViewport.h protected section:
bool event(QEvent* event) override;

// In DocumentViewport.cpp:
bool DocumentViewport::event(QEvent* event)
{
    if (event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd ||
        event->type() == QEvent::TouchCancel) {
        
        if (m_touchHandler->handleTouchEvent(static_cast<QTouchEvent*>(event))) {
            return true;
        }
    }
    
    return QWidget::event(event);
}
```

---

### Phase TG.2: Single-Finger Pan ✅ COMPLETE

#### Task TG.2.1: Implement Touch Pan Start ✅

In `TouchGestureHandler::handleTouchEvent()`:
- On `TouchBegin` with 1 finger: record start position, begin velocity tracking, call `m_viewport->beginPanGesture()`

#### Task TG.2.2: Implement Touch Pan Update ✅

- On `TouchUpdate` with 1 finger: calculate delta, track velocity, call `m_viewport->updatePanGesture(delta)`
- Respect Y-axis only mode (locks X-axis movement)
- **FIX: Pan direction negation** - Delta must be negated to match wheel event convention:
  ```cpp
  QPointF panDelta = -delta / m_viewport->zoomLevel();
  ```

#### Task TG.2.3: Implement Touch Pan End ✅

- On `TouchEnd`: call `endTouchPan(true)` to start inertia if velocity is sufficient
- On `TouchCancel`: call `endTouchPan(false)` - no inertia

#### Bug Fix: Inertia Not Interrupted by New Touch

**Problem**: When inertia was running (after 1-finger pan release), new touch inputs (2-finger pinch, 3-finger tap) couldn't interrupt it.

**Root Cause**: The inertia timer continued running even when new touch events arrived. The code only checked `m_panActive` which is false during inertia.

**Fix**: Added inertia interruption at two points:
1. At the start of `handleTouchEvent()` for any `TouchBegin`:
   ```cpp
   if (event->type() == QEvent::TouchBegin && m_inertiaTimer->isActive()) {
       m_inertiaTimer->stop();
       m_viewport->endPanGesture();
       m_velocitySamples.clear();
   }
   ```
2. In the 2-finger pinch handler (belt and suspenders):
   ```cpp
   if (!m_pinchActive) {
       if (m_inertiaTimer->isActive()) {
           m_inertiaTimer->stop();
           m_viewport->endPanGesture();
           m_velocitySamples.clear();
       }
       // ... start pinch
   }
   ```

---

#### Bug Fix: Pan Direction Reversed

**Problem**: Touch pan was inverted - pulling up moved document down, pulling left moved document right.

**Root Cause**: The existing wheel/trackpad code negates the delta:
```cpp
scrollDelta = QPointF(-pixelDelta.x(), -pixelDelta.y()) / m_zoomLevel;
```
This is because scroll UP (positive input) → content moves UP → pan offset increases.

**Fix**: Negated both pan delta and velocity tracking:
```cpp
// Pan delta
QPointF panDelta = -delta / m_viewport->zoomLevel();

// Velocity tracking (for inertia)
qreal vx = (m_mode == TouchGestureMode::YAxisOnly) ? 0.0 : -delta.x() / elapsed;
qreal vy = -delta.y() / elapsed;
```

---

### Phase TG.3: Inertia Scrolling ✅ COMPLETE

#### Task TG.3.1: Implement Velocity Averaging ✅

In `endTouchPan()`:
- Calculate average velocity from recent samples
- Convert from pixels/ms to document coords/ms
- Respect Y-axis only mode for inertia direction
- Check minimum velocity threshold before starting inertia

#### Task TG.3.2: Implement Inertia Animation ✅

In `onInertiaFrame()`:
- Apply friction (0.95 multiplier per frame)
- Check velocity threshold (0.05 doc coords/ms)
- Update pan gesture with velocity * interval
- Stop and end pan gesture when below threshold

---

### Phase TG.4: Pinch-to-Zoom ✅ COMPLETE

#### Task TG.4.1: Detect Pinch Gesture ✅

In `handleTouchEvent()`:
- On `TouchUpdate` with 2 fingers: calculate distance and centroid using `QLineF`
- If in YAxisOnly mode, ignore (consume but don't process)
- On first 2-finger detection: end any active pan, call `m_viewport->beginZoomGesture(centroid)`
- On subsequent updates: calculate frame-to-frame scale ratio, call `updateZoomGesture(scaleFactor, centroid)`

**Implementation Details**:
```cpp
qreal distance = QLineF(pos1, pos2).length();
QPointF centroid = (pos1 + pos2) / 2.0;

if (!m_pinchActive) {
    // Start pinch
    m_pinchStartDistance = distance;
    m_viewport->beginZoomGesture(centroid);
} else {
    // Frame-to-frame scale for smooth zoom
    qreal incrementalScale = distance / m_pinchStartDistance;
    m_viewport->updateZoomGesture(incrementalScale, centroid);
    m_pinchStartDistance = distance;  // Track for next frame
}
```

#### Task TG.4.2: Implement Pinch End ✅

- On finger lift: `endTouchPinch()` calls `m_viewport->endZoomGesture()`
- Already implemented in TG.1.R

---

### Phase TG.5: 3-Finger Tap ✅ COMPLETE

#### Task TG.5.1: Detect 3-Finger Tap ✅

**Implementation Details**:

1. **Start tracking** when 3 fingers are detected:
   - On `TouchBegin` with 3 fingers: record `m_threeFingerTapStart = currentMSecsSinceEpoch()`
   - On `TouchUpdate` when `m_activeTouchPoints == 3` and not yet tracked: also record timestamp
   - This handles both simultaneous 3-finger touch and fingers added one-by-one

2. **Detect tap** on `TouchEnd`:
   - Check if `m_threeFingerTapStart > 0` (was tracking)
   - Count released fingers using `QEventPoint::Released` state
   - If all fingers released and duration < 300ms: call `on3FingerTap()`
   - Reset `m_threeFingerTapStart = 0`

3. **Action**: Currently prints debug message:
   ```cpp
   void TouchGestureHandler::on3FingerTap()
   {
       qDebug() << "[Touch] 3-finger tap detected";
   }
   ```
   Connect to actual features later (undo, menu, etc.)

---

### Phase TG.6: MainWindow Integration ✅ COMPLETE

#### Task TG.6.1: Connect Toggle to Handler ✅

Updated `MainWindow::setTouchGestureMode()` to:
1. Call `currentViewport()->setTouchGestureMode(mode)` for immediate effect
2. Connected `currentViewportChanged` signal to apply mode to new viewports

---

## Code Review Fixes

### CR-TG.1: Redundant Pinch Calculations
**Issue**: Lines 182-204 had multiple unused intermediate variables (`scaleFactor`, `frameScale`, `prevDistance`) making the code confusing.

**Fix**: Simplified to single calculation:
```cpp
qreal incrementalScale = distance / m_pinchStartDistance;
m_viewport->updateZoomGesture(incrementalScale, centroid);
m_pinchStartDistance = distance;  // Update for next frame
```

### CR-TG.2: 3-Finger Tap Logic Issue
**Issue**: The condition `releasedCount == points.size() || m_activeTouchPoints == 3` was redundant - the `m_activeTouchPoints == 3` check didn't add value since we already verified 3 fingers via `m_threeFingerTapStart > 0`.

**Fix**: Simplified to check if all points are released:
```cpp
bool allReleased = true;
for (const auto& pt : points) {
    if (pt.state() != QEventPoint::Released) {
        allReleased = false;
        break;
    }
}
if (allReleased) { /* trigger tap */ }
```

### CR-TG.3: `isActive()` Missing Inertia State
**Issue**: `isActive()` returned `m_panActive || m_pinchActive`, but during inertia scrolling `m_panActive` is false even though a gesture is effectively still running.

**Fix**: Include inertia timer state:
```cpp
bool isActive() const { 
    return m_panActive || m_pinchActive || (m_inertiaTimer && m_inertiaTimer->isActive()); 
}
```

### CR-TG.4: Unused VelocitySample Timestamp
**Issue**: The `VelocitySample` struct had a `timestamp` field that was stored but never used for weighting.

**Fix**: Simplified to `QVector<QPointF>` storing just velocity values. Averaging is now cleaner:
```cpp
QPointF avgVelocity(0, 0);
for (const QPointF& velocity : m_velocitySamples) {
    avgVelocity += velocity;
}
avgVelocity /= m_velocitySamples.size();
```

---

## File Changes Summary

| File | Changes |
|------|---------|
| `TouchGestureHandler.h` | NEW - class declaration with all touch state |
| `TouchGestureHandler.cpp` | NEW - touch event handling, inertia, pinch logic |
| `DocumentViewport.h` | Remove TG.1.2 members, add handler pointer, add event() override |
| `DocumentViewport.cpp` | Remove TG.1.2 code, create handler, forward events |
| `MainWindow.cpp` | Minor update to `setTouchGestureMode()` |

---

## Implementation Order

1. **TG.1.R**: Create `TouchGestureHandler` class, move members from viewport, wire up event forwarding
2. **TG.2**: Single-finger pan (start, update, end)
3. **TG.3**: Inertia scrolling
4. **TG.4**: Pinch-to-zoom
5. **TG.5**: 3-finger tap
6. **TG.6**: MainWindow integration

---

## Test Cases

### TC-TG.1: Mode Disabled
- Set mode to Disabled → touch has no effect

### TC-TG.2: Y-Axis Only Mode
- Single-finger pan vertically → scrolls
- Single-finger pan horizontally → no horizontal movement
- Pinch-to-zoom → no effect
- Inertia → Y-axis only

### TC-TG.3: Full Mode - Pan
- Single-finger pan → scrolls both axes
- Lift with velocity → inertia continues
- Inertia decays and stops

### TC-TG.4: Full Mode - Pinch Zoom
- Two-finger pinch → zooms at centroid
- Release → zoom applied

### TC-TG.5: 3-Finger Tap
- Quick 3-finger tap → debug message
- Hold > 300ms → no message

### TC-TG.6: Deferred Rendering
- During touch pan → cached frame shifted (smooth)
- Pan ends → full re-render

---

## Success Criteria

1. ✅ Touch gesture logic in separate `TouchGestureHandler` class
2. ✅ `DocumentViewport` only forwards events and owns handler
3. ✅ Single-finger pan works with deferred rendering - **VERIFIED**
4. ✅ Y-axis only mode correctly locks horizontal movement - **VERIFIED**
5. ✅ Inertia scrolling provides natural feel - **VERIFIED**
6. ✅ Pinch-to-zoom works with correct centroid - **IMPLEMENTED**
7. ✅ 3-finger tap detected and logged - **IMPLEMENTED**
8. ✅ MainWindow toggle correctly applies mode - **VERIFIED**
9. ✅ Touch doesn't interfere with stylus drawing - **VERIFIED** (touch-synthesized mouse events rejected)
10. ✅ Works in both paged and edgeless modes - **VERIFIED**
