#pragma once

/**
 * @file MacPlatformHelper.h
 * @brief macOS platform-specific helper utilities.
 *
 * Provides C++ wrappers for macOS-specific functionality that requires
 * Objective-C++ (AppKit) APIs:
 * - Dark mode detection
 *
 * @see source/ios/IOSPlatformHelper.h (the iOS equivalent)
 * @see source/Main.cpp (Android equivalents: isAndroidDarkMode, etc.)
 */

#include <QtGlobal>

namespace MacPlatformHelper {

/**
 * @brief Check whether the app is currently rendering with a dark appearance.
 *
 * Asks AppKit for the appearance the app actually resolved to, rather than
 * inferring it from QPalette. The palette route only reports dark if the Qt
 * build in use picks up the system appearance, which is why macOS previously
 * disagreed with itself across OS versions.
 *
 * @return true if the effective appearance is dark aqua, false otherwise
 *         (including when called before the NSApplication exists).
 */
bool isDarkMode();

} // namespace MacPlatformHelper
