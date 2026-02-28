#include "IOSTouchTracker.h"

#ifdef Q_OS_IOS

#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <CoreFoundation/CoreFoundation.h>

// ============================================================================
// Native iOS touch tracking via UIApplication.sendEvent: swizzle
//
// Mirrors Android's SpeedyNoteActivity.dispatchTouchEvent() approach.
// By intercepting sendEvent: on UIApplication, we see every touch event
// before it reaches any UIView, UIGestureRecognizer, or Qt's QIOSView.
// This guarantees accurate finger count and position data regardless of
// how Qt's touch event layer translates UITouch objects to QTouchEvent.
// ============================================================================

static int            s_nativeTouchCount = 0;
static CFAbsoluteTime s_lastTouchTime    = 0;
static CGPoint        s_touchPos1        = {0, 0};
static CGPoint        s_touchPos2        = {0, 0};
static bool           s_installed        = false;

static void (*s_originalSendEvent)(id, SEL, UIEvent *) = nullptr;

static void updateTouchState(UIEvent *event)
{
    NSSet<UITouch *> *allTouches = [event allTouches];
    int activeCount = 0;
    UITouch *first  = nil;
    UITouch *second = nil;

    for (UITouch *touch in allTouches) {
        UITouchPhase phase = touch.phase;
        if (phase == UITouchPhaseBegan ||
            phase == UITouchPhaseMoved ||
            phase == UITouchPhaseStationary) {
            activeCount++;
            if (!first)       first  = touch;
            else if (!second) second = touch;
        }
    }

    s_nativeTouchCount = activeCount;
    s_lastTouchTime    = CFAbsoluteTimeGetCurrent();

    // Positions in screen coordinates (points = logical pixels).
    // locationInView:nil gives the window/screen coordinate system.
    if (first)  s_touchPos1 = [first  locationInView:nil];
    if (second) s_touchPos2 = [second locationInView:nil];
}

static void SN_swizzledSendEvent(id self, SEL _cmd, UIEvent *event)
{
    if (event.type == UIEventTypeTouches)
        updateTouchState(event);

    s_originalSendEvent(self, _cmd, event);
}

// ============================================================================
// C++ interface — mirrors the Android JNI functions in TouchGestureHandler.cpp
// ============================================================================

namespace IOSTouchTracker {

void install()
{
    if (s_installed)
        return;

    Method m = class_getInstanceMethod([UIApplication class], @selector(sendEvent:));
    if (!m) return;

    s_originalSendEvent = (void (*)(id, SEL, UIEvent *))method_getImplementation(m);
    method_setImplementation(m, (IMP)SN_swizzledSendEvent);
    s_installed = true;
    fprintf(stderr, "[IOSTouchTracker] sendEvent: swizzle installed\n");
}

int getNativeTouchCount()
{
    return s_nativeTouchCount;
}

qint64 getTimeSinceLastTouch()
{
    if (s_lastTouchTime == 0) return -1;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    return static_cast<qint64>((now - s_lastTouchTime) * 1000.0);
}

bool getNativeTouchPositions(QPointF& pos1, QPointF& pos2)
{
    if (s_nativeTouchCount < 2) return false;
    pos1 = QPointF(s_touchPos1.x, s_touchPos1.y);
    pos2 = QPointF(s_touchPos2.x, s_touchPos2.y);
    return true;
}

} // namespace IOSTouchTracker

#endif // Q_OS_IOS
