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

class QApplication;

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

} // namespace IOSPlatformHelper
