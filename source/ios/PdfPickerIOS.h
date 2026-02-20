#pragma once

#include <QString>
#include <functional>

#ifdef Q_OS_IOS

/**
 * iOS PDF Picker Utility
 *
 * Uses UIDocumentPickerViewController to let the user pick a PDF file.
 * The selected file is copied to app-private storage and the local path
 * is returned via a completion callback.
 *
 * IMPORTANT: This is fully asynchronous — the function returns immediately
 * and the completion callback fires later on the main thread. This is
 * required because UIDocumentPickerViewController is a remote view controller
 * whose result is delivered via XPC, which cannot be processed by a nested
 * QEventLoop.
 *
 * Thread-safety: Must be called from the main thread only.
 * Only one picker can be active at a time.
 */

namespace PdfPickerIOS {

/**
 * Opens the iOS document picker for PDFs.
 *
 * @param completion Called on the main thread with the local file path,
 *                   or an empty string if the user cancelled.
 *
 * The copied PDF lives in: <AppData>/pdfs/filename.pdf
 */
void pickPdfFile(std::function<void(const QString&)> completion);

/**
 * Opens the iOS document picker for PDFs with a custom destination.
 *
 * @param destDir Directory to copy the PDF to (will be created if needed)
 * @param completion Called on the main thread with the local path or empty string.
 */
void pickPdfFile(const QString& destDir, std::function<void(const QString&)> completion);

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
