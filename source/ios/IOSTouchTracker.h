#pragma once

/**
 * @file IOSTouchTracker.h
 * @brief Native iOS touch tracking for gesture reliability.
 *
 * Qt's touch event layer on iOS can lose track of touch points, causing
 * 2-finger gestures (pinch-to-zoom) to fail. This module installs a
 * passive UIGestureRecognizer on the key window's root view that tracks
 * the true finger count and positions at the UIKit level.
 *
 * TouchGestureHandler queries these values as ground truth when Qt's
 * reported touch count seems wrong — the same strategy used on Android
 * via JNI (see SpeedyNoteActivity.java).
 *
 * @see source/core/TouchGestureHandler.cpp
 * @see android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java
 */

#include <QtGlobal>

#ifdef Q_OS_IOS

#include <QPointF>

namespace IOSTouchTracker {

/**
 * @brief Install the native touch tracker on the key window.
 *
 * Call once after the first QWidget::show() so that Qt's UIWindow exists.
 * Safe to call multiple times (subsequent calls are no-ops).
 */
void install();

/**
 * @brief Get the current native touch count.
 * @return Number of fingers currently touching the screen (0–10).
 */
int getNativeTouchCount();

/**
 * @brief Get milliseconds since last touch event.
 * @return Time in ms, or -1 if no touch has been received yet.
 */
qint64 getTimeSinceLastTouch();

/**
 * @brief Get native touch positions for first 2 touch points.
 * Positions are in screen coordinates (logical pixels / points).
 *
 * @param pos1 Output: position of first touch point
 * @param pos2 Output: position of second touch point
 * @return true if 2+ touches are active and positions are valid
 */
bool getNativeTouchPositions(QPointF& pos1, QPointF& pos2);

} // namespace IOSTouchTracker

#endif // Q_OS_IOS
