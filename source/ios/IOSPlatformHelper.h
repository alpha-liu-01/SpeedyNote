#pragma once

/**
 * @file IOSPlatformHelper.h
 * @brief iOS platform-specific helper utilities.
 *
 * Provides C++ wrappers for iOS-specific functionality that requires
 * Objective-C++ (UIKit, Foundation) APIs:
 * - Dark mode detection
 * - System palette/font application
 *
 * @see source/Main.cpp (Android equivalents: isAndroidDarkMode, applyAndroidPalette, etc.)
 */

#include <QtGlobal>

class QApplication;
class QObject;

namespace IOSPlatformHelper {

/**
 * @brief Check if the device is currently in dark mode.
 * @return true if the iOS system appearance is dark, false otherwise.
 */
bool isDarkMode();

/**
 * @brief Apply an iOS-appropriate palette to the application.
 *
 * Detects dark/light mode and sets a matching QPalette on the app.
 *
 * @param app The QApplication instance.
 */
void applyPalette(QApplication& app);

/**
 * @brief Apply iOS-appropriate default fonts.
 *
 * Sets San Francisco (the iOS system font) as the application default
 * with appropriate sizing for iPad screens.
 *
 * @param app The QApplication instance.
 */
void applyFonts(QApplication& app);

/**
 * @brief Remove Qt's QIOSTapRecognizer to prevent a use-after-free crash.
 *
 * Qt's iOS text input overlay installs a QIOSTapRecognizer on the root
 * UIView. This recognizer dispatches blocks asynchronously to show the
 * edit menu (copy/paste). If the window is destroyed before the block
 * runs, QPlatformWindow::window() dereferences a stale pointer (SIGSEGV).
 *
 * SpeedyNote is a stylus drawing app and does not need the iOS edit menu.
 * Call this after the first QWidget::show() so Qt has created its UIView.
 */
void disableEditMenuOverlay();

/**
 * @brief Install an event filter that prevents the virtual keyboard from
 *        appearing on non-text widgets.
 *
 * Qt's iOS plugin shows the keyboard whenever any QWidget receives focus,
 * because it creates a UITextInput-conforming responder for every widget.
 * This filter intercepts FocusIn events and disables input methods on
 * widgets that are not text editors (QLineEdit, QTextEdit, QPlainTextEdit).
 *
 * @param app The QApplication to install the filter on.
 */
void installKeyboardFilter(QApplication& app);

} // namespace IOSPlatformHelper
