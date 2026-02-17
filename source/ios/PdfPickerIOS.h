#pragma once

#include <QString>

#ifdef Q_OS_IOS

/**
 * iOS PDF Picker Utility
 *
 * Uses UIDocumentPickerViewController to let the user pick a PDF file.
 * The selected file is copied to app-private storage and the local path
 * is returned.
 *
 * This utility is shared between:
 * - MainWindow (opening PDF documents)
 * - PdfRelinkDialog (relinking missing PDFs)
 *
 * Thread-safety: Must be called from the main thread only.
 * Only one picker can be active at a time.
 */

namespace PdfPickerIOS {

/**
 * Opens the iOS document picker for PDFs and waits for the result.
 *
 * @return Local file path of the copied PDF, or empty string if cancelled.
 *
 * The returned path is in app-private storage:
 *   <AppData>/pdfs/filename.pdf
 *
 * This function blocks until the user picks a file or cancels.
 */
QString pickPdfFile();

/**
 * Opens the iOS document picker for PDFs with a custom destination directory.
 *
 * @param destDir Directory to copy the PDF to (will be created if needed)
 * @return Local file path of the copied PDF, or empty string if cancelled.
 */
QString pickPdfFile(const QString& destDir);

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
