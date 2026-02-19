#include "IOSShareHelper.h"

#ifdef Q_OS_IOS

#include <QDebug>

// TODO Phase 4: Add UIActivityViewController integration
// #import <UIKit/UIKit.h>

namespace IOSShareHelper {

void shareFile(const QString& filePath, const QString& mimeType, const QString& title)
{
    Q_UNUSED(filePath);
    Q_UNUSED(mimeType);
    Q_UNUSED(title);
    // TODO Phase 4: Present UIActivityViewController with the file URL
    qDebug() << "IOSShareHelper::shareFile: Not yet implemented (stub)";
}

void shareMultipleFiles(const QStringList& filePaths, const QString& mimeType, const QString& title)
{
    Q_UNUSED(filePaths);
    Q_UNUSED(mimeType);
    Q_UNUSED(title);
    // TODO Phase 4: Present UIActivityViewController with multiple file URLs
    qDebug() << "IOSShareHelper::shareMultipleFiles: Not yet implemented (stub)";
}

bool isAvailable()
{
    // TODO Phase 4: Return true once UIActivityViewController integration is done
    return false;
}

} // namespace IOSShareHelper

#else // !Q_OS_IOS

namespace IOSShareHelper {

void shareFile(const QString& /*filePath*/, const QString& /*mimeType*/, const QString& /*title*/) {}
void shareMultipleFiles(const QStringList& /*filePaths*/, const QString& /*mimeType*/, const QString& /*title*/) {}
bool isAvailable() { return false; }

} // namespace IOSShareHelper

#endif // Q_OS_IOS
