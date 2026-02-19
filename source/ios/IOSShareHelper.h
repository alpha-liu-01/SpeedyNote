#pragma once

/**
 * @file IOSShareHelper.h
 * @brief iOS file sharing utilities via UIActivityViewController.
 *
 * Provides C++ wrappers for iOS share sheet functionality,
 * allowing export operations to share files with other apps.
 *
 * On non-iOS platforms, these functions are no-ops.
 *
 * @see source/android/AndroidShareHelper.h (Android equivalent)
 */

#include <QString>
#include <QStringList>

namespace IOSShareHelper {

/**
 * @brief Share a single file using the iOS share sheet.
 *
 * Presents UIActivityViewController allowing the user to share
 * the file via AirDrop, Files, Mail, or any other share extension.
 *
 * @param filePath Absolute path to the file to share
 * @param mimeType MIME type (e.g., "application/pdf", "application/octet-stream")
 * @param title Title for the share operation
 *
 * @note No-op on non-iOS platforms.
 */
void shareFile(const QString& filePath, const QString& mimeType, const QString& title);

/**
 * @brief Share multiple files using the iOS share sheet.
 *
 * Presents UIActivityViewController with multiple file URLs.
 * Non-existent files in the list are silently skipped.
 *
 * @param filePaths List of absolute paths to files to share
 * @param mimeType MIME type for all files (e.g., "application/pdf")
 * @param title Title for the share operation
 *
 * @note No-op on non-iOS platforms.
 */
void shareMultipleFiles(const QStringList& filePaths, const QString& mimeType, const QString& title);

/**
 * @brief Check if iOS sharing is available.
 * @return true on iOS, false on other platforms.
 */
bool isAvailable();

} // namespace IOSShareHelper
