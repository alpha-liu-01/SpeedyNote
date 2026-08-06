#pragma once

#include "NotebookLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace NotebookLibraryTests {

inline bool check(bool condition, const QString& message)
{
    if (!condition) {
        qCritical() << "FAIL:" << message;
    }
    return condition;
}

inline bool writeManifest(const QString& bundlePath, const QString& name,
                          const QString& documentId)
{
    QJsonObject manifest;
    manifest["name"] = name;
    manifest["notebook_id"] = documentId;
    manifest["mode"] = "paged";

    QFile file(bundlePath + "/document.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    return true;
}

inline QString createBundle(const QString& rootPath, const QString& directoryName,
                            const QString& displayName, const QString& documentId)
{
    QDir root(rootPath);
    if (!root.mkdir(directoryName)) {
        return QString();
    }

    const QString bundlePath = root.filePath(directoryName);
    if (!writeManifest(bundlePath, displayName, documentId)) {
        return QString();
    }
    return bundlePath;
}

inline QString renameBundle(const QString& rootPath, const QString& oldDirectoryName,
                            const QString& newDirectoryName, const QString& newDisplayName,
                            const QString& documentId)
{
    QDir root(rootPath);
    if (!root.rename(oldDirectoryName, newDirectoryName)) {
        return QString();
    }

    const QString newPath = root.filePath(newDirectoryName);
    if (!writeManifest(newPath, newDisplayName, documentId)) {
        return QString();
    }
    return newPath;
}

inline NotebookInfo notebookAtPath(NotebookLibrary* library, const QString& path)
{
    for (const NotebookInfo& notebook : library->recentNotebooks()) {
        if (notebook.bundlePath == path) {
            return notebook;
        }
    }
    return NotebookInfo();
}

inline bool listContainsPath(const QList<NotebookInfo>& notebooks, const QString& path)
{
    for (const NotebookInfo& notebook : notebooks) {
        if (notebook.bundlePath == path) {
            return true;
        }
    }
    return false;
}

inline bool persistedEntryMatches(const QString& libraryFilePath, const QString& path,
                                  bool isStarred, const QString& starredFolder)
{
    QFile file(libraryFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonArray notebooks = document.object().value("notebooks").toArray();
    for (const QJsonValue& value : notebooks) {
        const QJsonObject notebook = value.toObject();
        if (notebook.value("path").toString() == path) {
            return notebook.value("isStarred").toBool() == isStarred
                && notebook.value("starredFolder").toString() == starredFolder;
        }
    }
    return false;
}

inline bool testBundlePathMigration()
{
    qDebug() << "=== Test: Notebook library bundle path migration ===";

    QTemporaryDir bundles;
    if (!check(bundles.isValid(), "Could not create temporary bundle directory")) {
        return false;
    }

    const QString folderOldPath =
        createBundle(bundles.path(), "FolderStarred.snb", "Folder Starred", "folder-id");
    const QString unfiledOldPath =
        createBundle(bundles.path(), "Unfiled.snb", "Unfiled", "unfiled-id");
    const QString plainOldPath =
        createBundle(bundles.path(), "Plain.snb", "Plain", "plain-id");

    bool success = true;
    success &= check(!folderOldPath.isEmpty(), "Could not create folder-starred bundle");
    success &= check(!unfiledOldPath.isEmpty(), "Could not create unfiled-starred bundle");
    success &= check(!plainOldPath.isEmpty(), "Could not create non-starred bundle");
    if (!success) {
        return false;
    }

    NotebookLibrary* library = NotebookLibrary::instance();
    library->addToRecent(folderOldPath);
    library->addToRecent(unfiledOldPath);
    library->addToRecent(plainOldPath);

    const QString folderName = "Migration Test Folder";
    library->createStarredFolder(folderName);
    library->setStarredFolder(folderOldPath, folderName);
    library->setStarred(unfiledOldPath, true);

    const NotebookInfo folderBefore = notebookAtPath(library, folderOldPath);
    const NotebookInfo unfiledBefore = notebookAtPath(library, unfiledOldPath);
    const NotebookInfo plainBefore = notebookAtPath(library, plainOldPath);

    const QString folderNewPath =
        renameBundle(bundles.path(), "FolderStarred.snb", "FolderStarredRenamed.snb",
                     "Folder Starred Renamed", "folder-id");
    const QString unfiledNewPath =
        renameBundle(bundles.path(), "Unfiled.snb", "UnfiledRenamed.snb",
                     "Unfiled Renamed", "unfiled-id");
    const QString plainNewPath =
        renameBundle(bundles.path(), "Plain.snb", "PlainRenamed.snb",
                     "Plain Renamed", "plain-id");

    success &= check(!folderNewPath.isEmpty(), "Could not rename folder-starred bundle");
    success &= check(!unfiledNewPath.isEmpty(), "Could not rename unfiled-starred bundle");
    success &= check(!plainNewPath.isEmpty(), "Could not rename non-starred bundle");
    if (!success) {
        return false;
    }

    success &= check(library->updateBundlePath(folderOldPath, folderNewPath),
                     "Folder-starred path migration failed");
    success &= check(library->updateBundlePath(unfiledOldPath, unfiledNewPath),
                     "Unfiled-starred path migration failed");
    success &= check(library->updateBundlePath(plainOldPath, plainNewPath),
                     "Non-starred path migration failed");

    const NotebookInfo folderAfter = notebookAtPath(library, folderNewPath);
    const NotebookInfo unfiledAfter = notebookAtPath(library, unfiledNewPath);
    const NotebookInfo plainAfter = notebookAtPath(library, plainNewPath);

    success &= check(!notebookAtPath(library, folderOldPath).isValid(),
                     "Old folder-starred path is still tracked");
    success &= check(!notebookAtPath(library, unfiledOldPath).isValid(),
                     "Old unfiled-starred path is still tracked");
    success &= check(!notebookAtPath(library, plainOldPath).isValid(),
                     "Old non-starred path is still tracked");

    success &= check(folderAfter.isStarred, "Folder-assigned notebook lost its star");
    success &= check(folderAfter.starredFolder == folderName,
                     "Folder-assigned notebook lost its starred folder");
    success &= check(folderAfter.documentId == folderBefore.documentId,
                     "Folder-assigned notebook document ID changed");
    success &= check(folderAfter.lastAccessed == folderBefore.lastAccessed,
                     "Folder-assigned notebook access timestamp changed");
    success &= check(folderAfter.displayName() == "Folder Starred Renamed",
                     "Folder-assigned notebook name was not refreshed");
    success &= check(folderAfter.lastModified
                         == QFileInfo(folderNewPath + "/document.json").lastModified(),
                     "Folder-assigned notebook modified timestamp was not refreshed");

    success &= check(unfiledAfter.isStarred, "Unfiled notebook lost its star");
    success &= check(unfiledAfter.starredFolder.isEmpty(),
                     "Unfiled notebook gained a starred folder");
    success &= check(unfiledAfter.documentId == unfiledBefore.documentId,
                     "Unfiled notebook document ID changed");
    success &= check(unfiledAfter.lastAccessed == unfiledBefore.lastAccessed,
                     "Unfiled notebook access timestamp changed");
    success &= check(unfiledAfter.displayName() == "Unfiled Renamed",
                     "Unfiled notebook name was not refreshed");

    success &= check(!plainAfter.isStarred, "Non-starred notebook became starred");
    success &= check(plainAfter.starredFolder.isEmpty(),
                     "Non-starred notebook gained a starred folder");
    success &= check(plainAfter.documentId == plainBefore.documentId,
                     "Non-starred notebook document ID changed");
    success &= check(plainAfter.lastAccessed == plainBefore.lastAccessed,
                     "Non-starred notebook access timestamp changed");

    const QList<NotebookInfo> starred = library->starredNotebooks();
    success &= check(listContainsPath(starred, folderNewPath),
                     "Folder-assigned notebook is missing from starred notebooks");
    success &= check(listContainsPath(starred, unfiledNewPath),
                     "Unfiled notebook is missing from starred notebooks");
    success &= check(!listContainsPath(starred, plainNewPath),
                     "Non-starred notebook appears in starred notebooks");

    library->save();
    const QString libraryFilePath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/notebook_library.json";
    success &= check(persistedEntryMatches(libraryFilePath, folderNewPath, true, folderName),
                     "Folder-assigned starred state was not persisted");
    success &= check(persistedEntryMatches(libraryFilePath, unfiledNewPath, true, QString()),
                     "Unfiled starred state was not persisted");
    success &= check(persistedEntryMatches(libraryFilePath, plainNewPath, false, QString()),
                     "Non-starred state was not persisted");

    library->removeFromRecent(folderNewPath);
    library->removeFromRecent(unfiledNewPath);
    library->removeFromRecent(plainNewPath);
    library->deleteStarredFolder(folderName);
    library->save();

    return success;
}

inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running NotebookLibrary Unit Tests";
    qDebug() << "========================================";

    QCoreApplication::setApplicationName("SpeedyNote-NotebookLibraryTests");
    QStandardPaths::setTestModeEnabled(true);

    const QString dataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cachePath =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir(dataPath).removeRecursively();
    QDir(cachePath).removeRecursively();

    const bool success = testBundlePathMigration();

    QDir(dataPath).removeRecursively();
    QDir(cachePath).removeRecursively();

    qDebug() << "========================================";
    qDebug() << (success ? "All NotebookLibrary tests PASSED"
                         : "Some NotebookLibrary tests FAILED");
    qDebug() << "========================================\n";
    return success;
}

} // namespace NotebookLibraryTests
