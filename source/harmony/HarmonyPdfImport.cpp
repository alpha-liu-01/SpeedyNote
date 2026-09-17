#include "HarmonyPdfImport.h"

#include "HarmonyEnvironment.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace HarmonyPdfImport {

namespace {

// Shared with Android and iOS on purpose: Launcher::findImportedPdfPath() treats
// this directory as "copies we made, safe to delete with the notebook", and that
// is exactly what these are.
QString importDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/pdfs");
}

} // namespace

QString ensureReachable(const QString& pickedPath)
{
    if (pickedPath.isEmpty()) {
        return pickedPath;
    }

    // A device with user folders can read the file again tomorrow, so copying
    // would only duplicate it and hide later edits to the original.
    if (HarmonyEnvironment::hasUserDocumentsAccess()) {
        return pickedPath;
    }

    const QFileInfo picked(pickedPath);
    if (!picked.exists() || !picked.isFile()) {
        return pickedPath;
    }

    // Already ours: a PDF from a previous import, or one extracted from a .snbx
    // package. Copying again would duplicate on every open.
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty() && picked.absoluteFilePath().startsWith(appData)) {
        return pickedPath;
    }

    const QString dir = importDir();
    if (!QDir().mkpath(dir)) {
        qWarning() << "HarmonyPdfImport: cannot create" << dir
                   << "- keeping the picked path, which will not survive a restart";
        return pickedPath;
    }

    // Same file, already imported: match on name and size, as the Android copy
    // does. Hash would be stronger, but Document computes and stores one anyway,
    // and a name and size collision between two different PDFs the same user
    // picked twice is not worth a second read of the whole file.
    QString target = QDir(dir).absoluteFilePath(picked.fileName());
    const QFileInfo existing(target);
    if (existing.exists() && existing.size() == picked.size()) {
        return target;
    }

    // Different file wearing a name we already used.
    if (existing.exists()) {
        const QString base = picked.completeBaseName();
        const QString suffix = picked.suffix().isEmpty()
                                   ? QString()
                                   : QLatin1Char('.') + picked.suffix();
        for (int n = 1; QFileInfo::exists(target); ++n) {
            target = QDir(dir).absoluteFilePath(
                QStringLiteral("%1_%2%3").arg(base).arg(n).arg(suffix));
        }
    }

    if (!QFile::copy(pickedPath, target)) {
        qWarning() << "HarmonyPdfImport: cannot copy" << pickedPath << "to" << target
                   << "- keeping the picked path, which will not survive a restart";
        return pickedPath;
    }

    qInfo() << "HarmonyPdfImport: copied" << picked.fileName() << '(' << picked.size()
            << "bytes ) into app storage so the notebook can reopen it";
    return target;
}

} // namespace HarmonyPdfImport
