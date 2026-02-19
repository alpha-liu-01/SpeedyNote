#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#ifdef Q_OS_IOS

/**
 * iOS .snbx Package Picker Utility
 *
 * Uses UIDocumentPickerViewController to let the user pick one or more
 * .snbx notebook package files. Selected files are copied to app-private
 * storage and the local paths are returned via a completion callback.
 *
 * Fully asynchronous — see PdfPickerIOS.h for rationale.
 *
 * Thread-safety: Must be called from the main thread only.
 * Only one picker can be active at a time.
 *
 * @see source/android/ ImportHelper.java (Android equivalent)
 */

namespace SnbxPickerIOS {

/**
 * Opens the iOS document picker for .snbx files (multi-select enabled).
 *
 * @param completion Called on the main thread with a list of local paths,
 *                   or an empty list if the user cancelled.
 *
 * Copied files go to: <AppData>/imports/filename.snbx
 */
void pickSnbxFiles(std::function<void(const QStringList&)> completion);

/**
 * Opens the iOS document picker for .snbx files with a custom destination.
 *
 * @param destDir Directory to copy .snbx files to (created if needed)
 * @param completion Called on the main thread with a list of local paths or empty list.
 */
void pickSnbxFiles(const QString& destDir, std::function<void(const QStringList&)> completion);

} // namespace SnbxPickerIOS

#endif // Q_OS_IOS
