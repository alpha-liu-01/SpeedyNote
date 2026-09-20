#pragma once

// Tests for the rule that decides which PDFs die with a notebook on Android, iOS
// and HarmonyOS. They run on desktop, where the rule is not otherwise used, because
// both ways of getting it wrong are invisible until a user hits them: a missed
// reference leaks a file that platform's user cannot delete, and an over-eager
// delete quietly blanks the pages of a notebook nobody asked to touch.

#include "SandboxPdfOwnership.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace SandboxPdfOwnershipTests {

inline bool check(bool condition, const QString& message)
{
    if (!condition) {
        qCritical() << "FAIL:" << message;
    }
    return condition;
}

inline QJsonObject manifestWith(const QString& legacyPath, const QStringList& sourcePaths)
{
    QJsonObject manifest;
    if (!legacyPath.isEmpty()) {
        manifest["pdf_path"] = legacyPath;
    }
    if (!sourcePaths.isEmpty()) {
        QJsonArray sources;
        for (const QString& path : sourcePaths) {
            QJsonObject source;
            source["id"] = QStringLiteral("src-") + QFileInfo(path).completeBaseName();
            source["path"] = path;
            sources.append(source);
        }
        manifest["pdf_sources"] = sources;
    }
    return manifest;
}

// Writes a bundle directory containing just the manifest, which is all this rule
// reads.
inline bool writeBundle(const QString& bundlePath, const QJsonObject& manifest)
{
    if (!QDir().mkpath(bundlePath)) {
        return false;
    }
    QFile file(bundlePath + QStringLiteral("/document.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(manifest).toJson());
    file.close();
    return true;
}

inline bool testOwnership()
{
    bool success = true;

    QTemporaryDir tmp;
    if (!check(tmp.isValid(), "temporary directory")) {
        return false;
    }
    const QString appData = tmp.filePath(QStringLiteral("app"));
    const QString inside = appData + QStringLiteral("/pdfs/lecture.pdf");
    const QString outside = tmp.filePath(QStringLiteral("Documents/lecture.pdf"));

    // A copy we made is ours; the user's own file, wherever it sits, is not.
    success &= check(SandboxPdfOwnership::ownedPdfPaths(
                         manifestWith(inside, {}), appData) == QStringList{inside},
                     "legacy pdf_path inside app storage is owned");
    success &= check(SandboxPdfOwnership::ownedPdfPaths(
                         manifestWith(outside, {}), appData).isEmpty(),
                     "legacy pdf_path outside app storage is left alone");

    // The regression that motivated this: only the primary source is mirrored to
    // pdf_path, so a notebook with pages added from other PDFs leaked all but one.
    const QString second = appData + QStringLiteral("/pdfs/appendix.pdf");
    const QStringList mixed = SandboxPdfOwnership::ownedPdfPaths(
        manifestWith(inside, {inside, second, outside}), appData);
    success &= check(mixed == QStringList({inside, second}),
                     QStringLiteral("all sources inside app storage are owned, got %1")
                         .arg(mixed.join(QStringLiteral(", "))));

    // A path named twice must not be reported twice, or the second removal would
    // race with a file that is already gone.
    success &= check(SandboxPdfOwnership::ownedPdfPaths(
                         manifestWith(inside, {inside}), appData).size() == 1,
                     "a path listed in both places is reported once");

    // A directory whose name merely starts the same way is not inside app storage.
    const QString lookalike = appData + QStringLiteral("-backup/pdfs/lecture.pdf");
    success &= check(SandboxPdfOwnership::ownedPdfPaths(
                         manifestWith(lookalike, {}), appData).isEmpty(),
                     "a sibling directory with a shared name prefix is not app storage");

    // Nothing to compare against means nothing may be deleted.
    success &= check(SandboxPdfOwnership::ownedPdfPaths(
                         manifestWith(inside, {}), QString()).isEmpty(),
                     "an unknown app storage location owns nothing");

    // A manifest we cannot read is not evidence that a file is unused.
    success &= check(SandboxPdfOwnership::ownedPdfPathsForBundle(
                         tmp.filePath(QStringLiteral("gone.snb")), appData).isEmpty(),
                     "a missing manifest owns nothing");
    const QString brokenBundle = tmp.filePath(QStringLiteral("broken.snb"));
    QDir().mkpath(brokenBundle);
    QFile broken(brokenBundle + QStringLiteral("/document.json"));
    if (broken.open(QIODevice::WriteOnly)) {
        broken.write("{ not json");
        broken.close();
    }
    success &= check(SandboxPdfOwnership::ownedPdfPathsForBundle(brokenBundle, appData).isEmpty(),
                     "a malformed manifest owns nothing");

    return success;
}

inline bool testSharedCopiesSurvive()
{
    bool success = true;

    QTemporaryDir tmp;
    if (!check(tmp.isValid(), "temporary directory")) {
        return false;
    }
    const QString appData = tmp.filePath(QStringLiteral("app"));
    const QString shared = appData + QStringLiteral("/pdfs/textbook.pdf");
    const QString solo = appData + QStringLiteral("/pdfs/handout.pdf");

    // Two notebooks made from the same PDF name the same copy, because importing
    // deduplicates on name and size.
    const QString first = appData + QStringLiteral("/notebooks/first.snb");
    const QString second = appData + QStringLiteral("/notebooks/second.snb");
    success &= check(writeBundle(first, manifestWith(shared, {shared, solo})), "write first bundle");
    success &= check(writeBundle(second, manifestWith(shared, {})), "write second bundle");

    const QStringList deletingFirst =
        SandboxPdfOwnership::deletablePdfPaths({first}, {second}, appData);
    success &= check(deletingFirst == QStringList{solo},
                     QStringLiteral("a copy the other notebook still uses is kept, got %1")
                         .arg(deletingFirst.join(QStringLiteral(", "))));

    // Once the last notebook using it goes, so does the copy.
    const QStringList deletingBoth =
        SandboxPdfOwnership::deletablePdfPaths({first, second}, {}, appData);
    success &= check(deletingBoth.size() == 2 && deletingBoth.contains(shared)
                         && deletingBoth.contains(solo),
                     QStringLiteral("deleting every user frees the copy, got %1")
                         .arg(deletingBoth.join(QStringLiteral(", "))));

    // Deleting the same notebook twice in one batch must not queue the file twice.
    success &= check(SandboxPdfOwnership::deletablePdfPaths({first, first}, {}, appData).size() == 2,
                     "a repeated bundle does not duplicate its PDFs");

    return success;
}

// Overwriting a notebook on import removes its bundle, but its PDFs sit outside it:
// a picked one was copied into app storage, a packaged one was extracted into an
// embedded/ folder shared with the other notebooks imported to the same place. The
// question is asked after the replacement lands, because re-importing the same
// package reuses the same embedded file.
inline bool testOverwriteOnImport()
{
    bool success = true;

    QTemporaryDir tmp;
    if (!check(tmp.isValid(), "temporary directory")) {
        return false;
    }
    const QString appData = tmp.filePath(QStringLiteral("app"));
    const QString destDir = appData + QStringLiteral("/notebooks");
    const QString bundle = destDir + QStringLiteral("/Lecture.snb");
    const QString neighbour = destDir + QStringLiteral("/Seminar.snb");

    const QString oldEmbedded = destDir + QStringLiteral("/embedded/Lecture_week1.pdf");
    const QString sharedEmbedded = destDir + QStringLiteral("/embedded/Seminar_reader.pdf");

    // What the overwritten notebook was using, noted before its bundle went away.
    const QStringList noted = SandboxPdfOwnership::ownedPdfPaths(
        manifestWith(oldEmbedded, {oldEmbedded, sharedEmbedded}), appData);
    success &= check(noted.size() == 2, "both PDFs of the overwritten notebook are noted");

    // The replacement brought a differently named PDF, and a neighbour still wants
    // the shared one.
    const QString newEmbedded = destDir + QStringLiteral("/embedded/Lecture_week2.pdf");
    success &= check(writeBundle(bundle, manifestWith(newEmbedded, {})), "write replacement");
    success &= check(writeBundle(neighbour, manifestWith(sharedEmbedded, {})), "write neighbour");

    const QStringList holders =
        SandboxPdfOwnership::bundlesInDirectories({destDir});
    success &= check(holders.size() == 2,
                     QStringLiteral("both bundles are found on disk, got %1").arg(holders.size()));

    const QStringList orphans =
        SandboxPdfOwnership::unreferencedPdfPaths(noted, holders, appData);
    success &= check(orphans == QStringList{oldEmbedded},
                     QStringLiteral("only the PDF nothing kept is freed, got %1")
                         .arg(orphans.join(QStringLiteral(", "))));

    // Re-importing the same package: the replacement points at the same file it did
    // before, so noting it earlier must not turn into deleting it.
    success &= check(SandboxPdfOwnership::unreferencedPdfPaths(
                         {newEmbedded}, holders, appData).isEmpty(),
                     "a PDF the replacement still points at is kept");

    // A directory with no bundles in it keeps nothing, and must not throw the caller
    // off by looking like an answer.
    success &= check(SandboxPdfOwnership::bundlesInDirectories(
                         {tmp.filePath(QStringLiteral("nowhere")), QString()}).isEmpty(),
                     "missing and empty directories contribute no bundles");

    return success;
}

inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running SandboxPdfOwnership Unit Tests";
    qDebug() << "========================================";

    bool success = testOwnership();
    success &= testSharedCopiesSurvive();
    success &= testOverwriteOnImport();

    qDebug() << "========================================";
    qDebug() << (success ? "All SandboxPdfOwnership tests PASSED"
                         : "Some SandboxPdfOwnership tests FAILED");
    qDebug() << "========================================\n";
    return success;
}

} // namespace SandboxPdfOwnershipTests
