// ============================================================================
// qt_compat.h - Qt5 / Qt6 compatibility shims for SpeedyNote
// ============================================================================
// Include this header in .cpp files that use the complex Qt5/Qt6 APIs listed
// below.  Simple one-liner differences (QEnterEvent, nativeEvent signature,
// QTextStream::setEncoding, QLibraryInfo::path) are handled with inline
// #if QT_VERSION_CHECK guards directly in each source file.
// ============================================================================
#pragma once

#include <QtCore/qglobal.h>
#include <QTouchEvent>

// ============================================================================
// Touch-point types
// ============================================================================
// Qt6: QEventPoint, event->points(), pt.position(), QEventPoint::State enum
// Qt5: QTouchEvent::TouchPoint, event->touchPoints(), pt.pos(), Qt::TouchPointState
// ============================================================================
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QEventPoint>
   using SN_TouchPoint = QEventPoint;
#  define SN_TOUCH_POINTS(event)   (event)->points()
#  define SN_TP_POS(pt)            (pt).position()
#  define SN_TP_POS_PTR(pt)       (pt)->position()
#  define SN_TP_PRESSED            QEventPoint::Pressed
#  define SN_TP_RELEASED           QEventPoint::Released
#else
   using SN_TouchPoint = QTouchEvent::TouchPoint;
#  define SN_TOUCH_POINTS(event)   (event)->touchPoints()
#  define SN_TP_POS(pt)            (pt).pos()
#  define SN_TP_POS_PTR(pt)       (pt)->pos()
#  define SN_TP_PRESSED            Qt::TouchPointPressed
#  define SN_TP_RELEASED           Qt::TouchPointReleased
#endif

// ============================================================================
// Pointer event position (QPointF)
// ============================================================================
// Qt6 unified all events under QSinglePointEvent::position().
// Qt5 has different methods per event class:
//   QMouseEvent      → localPos()  (QPointF, Qt5+)
//   QWheelEvent      → posF()      (QPointF, deprecated in 5.15 but functional)
//   QTabletEvent     → posF()      (QPointF)
//   QDragMoveEvent   → pos()       (QPoint  - use directly, no macro needed)
//   QNativeGestureEvent → localPos() (QPointF)
// ============================================================================
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define SN_MOUSE_POS(event)    (event)->position()   // QMouseEvent
#  define SN_EVENT_POS(event)    (event)->position()   // QTabletEvent
#  define SN_NGE_POS(event)     (event)->position()   // QNativeGestureEvent
#else
#  define SN_MOUSE_POS(event)    (event)->localPos()   // QMouseEvent::localPos() → QPointF
#  define SN_EVENT_POS(event)    (event)->posF()       // QTabletEvent::posF() → QPointF
#  define SN_NGE_POS(event)     (event)->localPos()   // QNativeGestureEvent::localPos()
#endif
// QWheelEvent::position() exists since Qt 5.14, so works in both Qt5.15 and Qt6.
// Use this instead of SN_EVENT_POS for wheel events to avoid the posF() warning.
#define SN_WHEEL_POS(event)      (event)->position()

// ============================================================================
// Input / pointing device types
// ============================================================================
// Qt6: QInputDevice, QPointingDevice (from <QInputDevice> / <QPointingDevice>)
// Qt5: QTouchDevice, QTabletEvent (no QInputDevice / QPointingDevice)
// ============================================================================
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QInputDevice>
#  include <QPointingDevice>
#  define SN_TOUCHPAD_DEVICE_TYPE        QInputDevice::DeviceType::TouchPad
#  define SN_IS_ERASER_TABLET(event)     ((event)->pointerType() == QPointingDevice::PointerType::Eraser)
#  define SN_IS_STYLUS_TABLET(event)     ((event)->deviceType()  == QInputDevice::DeviceType::Stylus)
// Qt6 only: access the QPointingDevice for name-based eraser detection
#  define SN_HAS_POINTING_DEVICE 1
#else
#  include <QTouchDevice>
#  define SN_TOUCHPAD_DEVICE_TYPE        QTouchDevice::TouchPad
#  define SN_IS_ERASER_TABLET(event)     ((event)->pointerType() == QTabletEvent::Eraser)
#  define SN_IS_STYLUS_TABLET(event)     ((event)->deviceType() == QTabletEvent::Stylus)
// Qt5: QPointingDevice does not exist; name-based detection is unavailable
#  undef  SN_HAS_POINTING_DEVICE
#endif

// ============================================================================
// Single-shot signal connections
// ============================================================================
// Qt6: Qt::SingleShotConnection auto-disconnects after the first emit.
// Qt5: No equivalent — emulate with a shared connection handle that
//      disconnects itself from inside the lambda on first invocation.
//
// Usage (same syntax for both Qt versions):
//   SN_CONNECT_ONCE(sender, &Sender::signal, context, [=]() { ... });
// ============================================================================
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define SN_CONNECT_ONCE(sender, signal, context, slot) \
     QObject::connect(sender, signal, context, slot, Qt::SingleShotConnection)
#else
#  include <QSharedPointer>
#  include <QMetaObject>
template<typename Sender, typename Signal, typename Context, typename Functor>
static inline void snConnectOnce(Sender* sender, Signal signal, Context* context, Functor functor)
{
    auto conn = QSharedPointer<QMetaObject::Connection>::create();
    *conn = QObject::connect(sender, signal, context,
        [conn, functor]() mutable {
            QObject::disconnect(*conn);
            functor();
        });
}
#  define SN_CONNECT_ONCE(sender, signal, context, slot) \
     snConnectOnce(sender, signal, context, slot)
#endif
