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

// Which of these PDFs nothing needs any more: the ones no bundle in `bundles`
// refers to.
//
// The subtraction is the point. Imports are deduplicated and .snbx packages extract
// into a folder shared by every notebook imported alongside them, so one file can
// have several users, and freeing it on the strength of a single notebook going away
// would blank another notebook's page backgrounds. Every bundle that might still
// want the file has to be in `bundles`, and each has to still be on disk, since this
// reads their manifests -- a bundle whose manifest has gone contributes nothing,
// which here amounts to a vote to delete.
inline QStringList unreferencedPdfPaths(const QStringList& candidates,
                                        const QStringList& bundles,
                                        const QString& appDataDir)
{
    QStringList retained;
    for (const QString& bundle : bundles) {
        retained << ownedPdfPathsForBundle(bundle, appDataDir);
    }

    QStringList orphans;
    for (const QString& candidate : candidates) {
        const QString absolute = QFileInfo(candidate).absoluteFilePath();
        if (!retained.contains(absolute) && !orphans.contains(absolute)) {
            orphans << absolute;
        }
    }
    return orphans;
}

// Of the PDFs owned by the notebooks about to be deleted, the ones no surviving
// notebook still refers to. Reads the departing manifests before they go.
inline QStringList deletablePdfPaths(const QStringList& bundlesBeingDeleted,
                                     const QStringList& survivingBundles,
                                     const QString& appDataDir)
{
    QStringList owned;
    for (const QString& bundle : bundlesBeingDeleted) {
        owned << ownedPdfPathsForBundle(bundle, appDataDir);
    }
    return unreferencedPdfPaths(owned, survivingBundles, appDataDir);
}

// Every .snb bundle in these directories. The caller pairs this with the library's
// own list when working out who still holds a reference, because a notebook that is
// on disk but absent from the library index still opens and still needs its PDF.
inline QStringList bundlesInDirectories(const QStringList& directories)
{
    QStringList bundles;
    for (const QString& directory : directories) {
        if (directory.isEmpty()) {
            continue;
        }
        const QDir dir(directory);
        const QStringList names =
            dir.entryList({QStringLiteral("*.snb")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : names) {
            const QString path = QDir::cleanPath(dir.absoluteFilePath(name));
            if (!bundles.contains(path)) {
                bundles << path;
            }
        }
    }
    return bundles;
}

} // namespace SandboxPdfOwnership
