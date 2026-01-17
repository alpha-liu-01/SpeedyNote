#pragma once

#include <QString>

#ifdef Q_OS_ANDROID

/**
 * Android PDF Picker Utility
 * 
 * Uses PdfFileHelper.java to pick PDFs via Storage Access Framework (SAF).
 * The Java helper copies the file to app-private storage while SAF permission
 * is valid, then returns the local file path.
 * 
 * This utility is shared between:
 * - MainWindow (opening PDF documents)
 * - PdfRelinkDialog (relinking missing PDFs)
 * 
 * Thread-safety: Must be called from the main thread only.
 * Only one picker can be active at a time.
 */

namespace PdfPickerAndroid {

/**
 * Opens the Android file picker for PDFs and waits for the result.
 * 
 * @return Local file path of the copied PDF, or empty string if cancelled.
 * 
 * The returned path is in app-private storage:
 *   /data/data/org.speedynote.app/files/pdfs/filename.pdf
 * 
 * This function blocks until the user picks a file or cancels.
 * It uses a QEventLoop internally to wait for the Java callback.
 */
QString pickPdfFile();

/**
 * Opens the Android file picker for PDFs with a custom destination directory.
 * 
 * @param destDir Directory to copy the PDF to (will be created if needed)
 * @return Local file path of the copied PDF, or empty string if cancelled.
 */
QString pickPdfFile(const QString& destDir);

} // namespace PdfPickerAndroid

#endif // Q_OS_ANDROID

