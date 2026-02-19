#include "PdfPickerIOS.h"

#ifdef Q_OS_IOS

#include <QDir>
#include <QStandardPaths>
#include <QDebug>

// TODO Phase 4: Add UIDocumentPickerViewController integration
// #import <UIKit/UIKit.h>

namespace PdfPickerIOS {

QString pickPdfFile()
{
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
    return pickPdfFile(destDir);
}

QString pickPdfFile(const QString& destDir)
{
    Q_UNUSED(destDir);
    // TODO Phase 4: Present UIDocumentPickerViewController for PDF selection,
    // copy the selected file to destDir, and return the local path.
    qDebug() << "PdfPickerIOS::pickPdfFile: Not yet implemented (stub)";
    return QString();
}

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
