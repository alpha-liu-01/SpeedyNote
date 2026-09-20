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

    // Leftovers from a run that was killed mid-copy. Harmless but unreclaimable:
    // the user has no file manager access to app storage.
    const QDir importRoot(dir);
    const QStringList stale =
        importRoot.entryList({QStringLiteral("*.part")}, QDir::Files);
    for (const QString& name : stale) {
        QFile::remove(importRoot.absoluteFilePath(name));
    }

    // Reuse an identical import rather than making a second copy, matching on name
    // and size as the Android copy does. Hash would be stronger, but Document
    // computes and stores one anyway, and reading whole files here to catch a
    // same-name same-size collision is not worth it.
    //
    // The numbered candidates have to be checked too, not just the plain name:
    // when a different PDF already holds the plain name, picking the same file
    // repeatedly would otherwise add a copy on every open.
    const QString base = picked.completeBaseName();
    const QString suffix =
        picked.suffix().isEmpty() ? QString() : QLatin1Char('.') + picked.suffix();
    QString target;
    for (int n = 0; target.isEmpty(); ++n) {
        const QString candidate = importRoot.absoluteFilePath(
            n == 0 ? picked.fileName()
                   : QStringLiteral("%1_%2%3").arg(base).arg(n).arg(suffix));
        const QFileInfo existing(candidate);
        if (!existing.exists()) {
            target = candidate;
        } else if (existing.size() == picked.size()) {
            return candidate;
        }
    }

    // Copy to a temporary name and rename into place. An interrupted copy would
    // otherwise leave a short file that the size check above reads as a different
    // PDF, so every subsequent open would copy again. rename(2) is safe here: the
    // sharefs restriction that defeats QSaveFile applies to the user's folders,
    // not to app-private storage.
    const QString partial = target + QStringLiteral(".part");
    QFile::remove(partial);
    if (!QFile::copy(pickedPath, partial)) {
        qWarning() << "HarmonyPdfImport: cannot copy" << pickedPath << "to" << partial
                   << "- keeping the picked path, which will not survive a restart";
        QFile::remove(partial);
        return pickedPath;
    }
    if (!QFile::rename(partial, target)) {
        qWarning() << "HarmonyPdfImport: cannot rename" << partial << "to" << target
                   << "- keeping the picked path, which will not survive a restart";
        QFile::remove(partial);
        return pickedPath;
    }

    qInfo() << "HarmonyPdfImport: copied" << picked.fileName() << '(' << picked.size()
            << "bytes ) into app storage so the notebook can reopen it";
    return target;
}

} // namespace HarmonyPdfImport
