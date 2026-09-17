#pragma once

// ============================================================================
// Which PDFs a notebook owns, on platforms where the user cannot see storage
// ============================================================================
// Android, iOS and HarmonyOS all confine the app to a private directory the user
// has no file manager access to, and all three copy a picked PDF into it so the
// reference survives a restart (PdfPickerAndroid, PdfPickerIOS, HarmonyPdfImport).
// Those copies are ours: nothing else will ever reclaim them, so deleting a
// notebook has to delete them too, or the space is unrecoverable short of
// uninstalling the app.
//
// The decision is worth its own home because it is easy to get wrong in two
// opposite and equally bad ways -- missing a reference leaks a file the user
// cannot delete, while deleting one that another notebook still uses silently
// blanks that notebook's page backgrounds. It is separated from Launcher so it can
// be tested without a UI; see SandboxPdfOwnershipTests.h.
// ============================================================================

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace SandboxPdfOwnership {

// True when path is dir itself or something inside it. Written out rather than
// using startsWith() on the bare directory, which also matches a sibling whose
// name merely begins the same way -- "<AppData>/pdfs" against "<AppData>/pdfs-old".
inline bool isUnder(const QString& path, const QString& dir)
{
    return path == dir || path.startsWith(dir + QLatin1Char('/'));
}

// Every PDF a manifest references, both the modern pdf_sources[] array and the
// legacy top-level pdf_path. Reading only the latter used to miss every source but
// the primary, because that is the only one mirrored to it -- so a notebook with
// pages added from a second PDF leaked all the others.
inline QStringList referencedPdfPaths(const QJsonObject& manifest)
{
    QStringList paths;
    const QString legacy = manifest[QStringLiteral("pdf_path")].toString();
    if (!legacy.isEmpty()) {
        paths << legacy;
    }
    const QJsonArray sources = manifest[QStringLiteral("pdf_sources")].toArray();
    for (const QJsonValue& source : sources) {
        const QString path = source.toObject()[QStringLiteral("path")].toString();
        if (!path.isEmpty() && !paths.contains(path)) {
            paths << path;
        }
    }
    return paths;
}

// Of those, the ones inside app-private storage.
//
// Containment in that directory is the whole test, rather than a list of the
// specific subdirectories each import flow writes to. The user cannot put a file
// there, so anything that ends up there is ours by construction -- which covers
// PDFs copied in at pick time, PDFs extracted from a .snbx package next to
// wherever it was imported, and anything added later, with no list to keep in
// sync. A PDF outside it is the user's own file and is never touched.
inline QStringList ownedPdfPaths(const QJsonObject& manifest, const QString& appDataDir)
{
    if (appDataDir.isEmpty()) {
        return {};
    }
    const QString root = QDir(appDataDir).absolutePath();

    QStringList owned;
    const QStringList referenced = referencedPdfPaths(manifest);
    for (const QString& path : referenced) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (isUnder(absolute, root) && !owned.contains(absolute)) {
            owned << absolute;
        }
    }
    return owned;
}

// Same, reading the manifest out of a bundle directory. An unreadable or malformed
// manifest yields nothing, which errs towards leaving files alone.
inline QStringList ownedPdfPathsForBundle(const QString& bundlePath, const QString& appDataDir)
{
    QFile manifestFile(bundlePath + QStringLiteral("/document.json"));
    if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QByteArray data = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return ownedPdfPaths(doc.object(), appDataDir);
}

// Of the PDFs owned by the notebooks about to be deleted, the ones no surviving
// notebook still refers to.
//
// The subtraction is the point. Imports are deduplicated -- two notebooks made from
// the same file name the same copy on disk -- so ownership is shared, and deleting
// one notebook's copy would blank the other notebook's page backgrounds. Must be
// called while both sets of bundles are still on disk, since it reads their
// manifests; a bundle whose manifest has already gone contributes nothing, which on
// the surviving side would be a vote to delete.
inline QStringList deletablePdfPaths(const QStringList& bundlesBeingDeleted,
                                     const QStringList& survivingBundles,
                                     const QString& appDataDir)
{
    QStringList retained;
    for (const QString& bundle : survivingBundles) {
        retained << ownedPdfPathsForBundle(bundle, appDataDir);
    }

    QStringList deletable;
    for (const QString& bundle : bundlesBeingDeleted) {
        const QStringList owned = ownedPdfPathsForBundle(bundle, appDataDir);
        for (const QString& path : owned) {
            if (!retained.contains(path) && !deletable.contains(path)) {
                deletable << path;
            }
        }
    }
    return deletable;
}

} // namespace SandboxPdfOwnership
