#ifndef ANDROIDSHAREHELPER_H
#define ANDROIDSHAREHELPER_H

/**
 * @file AndroidShareHelper.h
 * @brief Android file sharing utilities via JNI.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * This module provides C++ wrappers for Android's share sheet functionality,
 * allowing batch export to share files with other apps via ACTION_SEND or
 * ACTION_SEND_MULTIPLE intents.
 * 
 * On non-Android platforms, these functions are no-ops.
 * 
 * @see android/app-resources/src/org/speedynote/app/ShareHelper.java
 * @see docs/private/BATCH_OPERATIONS.md
 */

#include <QString>
#include <QStringList>

namespace AndroidShareHelper {

/**
 * @brief Share a single file using Android's share sheet.
 * 
 * Opens the system share sheet allowing the user to send the file
 * to any app that accepts the given MIME type.
 * 
 * @param filePath Absolute path to the file to share
 * @param mimeType MIME type (e.g., "application/pdf", "application/octet-stream")
 * @param chooserTitle Title for the share sheet (e.g., "Share PDF")
 * 
 * @note No-op on non-Android platforms.
 */
void shareFile(const QString& filePath, const QString& mimeType, const QString& chooserTitle);

/**
 * @brief Share multiple files using Android's share sheet.
 * 
 * Uses ACTION_SEND_MULTIPLE for 2+ files, falls back to ACTION_SEND for
 * a single file. Non-existent files in the list are silently skipped.
 * 
 * @param filePaths List of absolute paths to files to share
 * @param mimeType MIME type for all files (e.g., "application/pdf")
 * @param chooserTitle Title for the share sheet
 * 
 * @note No-op on non-Android platforms.
 */
void shareMultipleFiles(const QStringList& filePaths, const QString& mimeType, const QString& chooserTitle);

/**
 * @brief Check if Android sharing is available.
 * @return true on Android, false on other platforms.
 */
bool isAvailable();

} // namespace AndroidShareHelper

#endif // ANDROIDSHAREHELPER_H
