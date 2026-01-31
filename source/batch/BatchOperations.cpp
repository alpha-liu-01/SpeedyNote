#include "BatchOperations.h"
#include "BundleDiscovery.h"

#include "../core/Document.h"
#include "../core/NotebookLibrary.h"
#include "../sharing/NotebookExporter.h"
#include "../sharing/NotebookImporter.h"
#include "../pdf/MuPdfExporter.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDebug>

/**
 * @file BatchOperations.cpp
 * @brief Implementation of batch export/import operations.
 * 
 * @see BatchOperations.h for API documentation
 */

namespace BatchOps {

// =============================================================================
// Utility Functions
// =============================================================================

QString generateOutputPath(const QString& inputPath,
                           const QString& outputDir,
                           const QString& extension)
{
    // Extract bundle name from input path
    QFileInfo inputInfo(inputPath);
    QString bundleName = inputInfo.fileName();
    
    // Remove .snb extension if present
    if (bundleName.endsWith(".snb", Qt::CaseInsensitive)) {
        bundleName = bundleName.left(bundleName.length() - 4);
    }
    
    // Ensure output directory path ends properly
    QString outDir = outputDir;
    if (!outDir.endsWith('/') && !outDir.endsWith('\\')) {
        outDir += '/';
    }
    
    // Ensure extension starts with dot
    QString ext = extension;
    if (!ext.startsWith('.')) {
        ext = '.' + ext;
    }
    
    return outDir + bundleName + ext;
}

bool isSingleFileOutput(const QString& outputPath, const QString& extension)
{
    if (outputPath.isEmpty()) {
        return false;
    }
    
    // Check if ends with the expected extension
    if (outputPath.endsWith(extension, Qt::CaseInsensitive)) {
        return true;
    }
    
    // Check if ends with directory separator
    if (outputPath.endsWith('/') || outputPath.endsWith('\\')) {
        return false;
    }
    
    // Check if it's an existing directory
    QFileInfo info(outputPath);
    if (info.exists() && info.isDir()) {
        return false;
    }
    
    // Default: assume directory (safer for batch operations)
    return false;
}

// =============================================================================
// SNBX Batch Export
// =============================================================================

BatchResult exportSnbxBatch(const QStringList& bundlePaths,
                            const ExportSnbxOptions& options,
                            ProgressCallback progress,
                            std::atomic<bool>* cancelled)
{
    BatchResult result;
    QElapsedTimer timer;
    timer.start();
    
    const int total = bundlePaths.size();
    
    // Validate inputs
    if (bundlePaths.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    if (options.outputPath.isEmpty()) {
        // No output path - add error for all files
        for (const QString& bundlePath : bundlePaths) {
            FileResult fr;
            fr.inputPath = bundlePath;
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("No output path specified");
            result.results.append(fr);
            result.errorCount++;
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    // Determine single-file vs batch mode
    bool singleFileMode = (bundlePaths.size() == 1) && 
                          isSingleFileOutput(options.outputPath, ".snbx");
    
    // For batch mode, ensure output directory exists
    QString outputDir;
    if (!singleFileMode) {
        outputDir = options.outputPath;
        QDir dir(outputDir);
        if (!dir.exists()) {
            if (!options.dryRun) {
                if (!dir.mkpath(".")) {
                    // Can't create output directory
                    for (const QString& bundlePath : bundlePaths) {
                        FileResult fr;
                        fr.inputPath = bundlePath;
                        fr.status = FileStatus::Error;
                        fr.message = QObject::tr("Failed to create output directory: %1").arg(outputDir);
                        result.results.append(fr);
                        result.errorCount++;
                    }
                    result.elapsedMs = timer.elapsed();
                    return result;
                }
            }
        }
    }
    
    // Process each bundle
    for (int i = 0; i < total; ++i) {
        const QString& bundlePath = bundlePaths.at(i);
        FileResult fr;
        fr.inputPath = bundlePath;
        
        // Check cancellation
        if (cancelled && cancelled->load()) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Cancelled");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Report progress
        if (progress) {
            progress(i + 1, total, bundlePath, QObject::tr("Exporting..."));
        }
        
        // Determine output path
        QString outputPath;
        if (singleFileMode) {
            outputPath = options.outputPath;
        } else {
            outputPath = generateOutputPath(bundlePath, outputDir, ".snbx");
        }
        fr.outputPath = outputPath;
        
        // Check if output exists
        if (QFile::exists(outputPath) && !options.overwrite) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Output file already exists");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Dry run - just report what would happen
        if (options.dryRun) {
            fr.status = FileStatus::Success;
            fr.message = QObject::tr("Would export to: %1").arg(outputPath);
            result.results.append(fr);
            result.successCount++;
            continue;
        }
        
        // Validate bundle
        if (!isValidBundle(bundlePath)) {
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("Not a valid SpeedyNote bundle");
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Load document from bundle
        std::unique_ptr<Document> doc = Document::loadBundle(bundlePath);
        if (!doc) {
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("Failed to load document");
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Prepare export options
        NotebookExporter::ExportOptions exportOpts;
        exportOpts.destPath = outputPath;
        exportOpts.includePdf = options.includePdf;
        
        // Call NotebookExporter
        NotebookExporter::ExportResult exportResult = 
            NotebookExporter::exportPackage(doc.get(), exportOpts);
        
        if (exportResult.success) {
            fr.status = FileStatus::Success;
            fr.outputPath = exportResult.exportedPath;
            fr.outputSize = exportResult.fileSize;
            result.successCount++;
            result.totalOutputSize += exportResult.fileSize;
        } else {
            fr.status = FileStatus::Error;
            fr.message = exportResult.errorMessage;
            result.errorCount++;
        }
        
        result.results.append(fr);
        
        // Document is automatically destroyed here (unique_ptr goes out of scope)
    }
    
    result.elapsedMs = timer.elapsed();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[BatchOps] exportSnbxBatch complete:"
             << result.successCount << "success,"
             << result.skippedCount << "skipped,"
             << result.errorCount << "errors,"
             << result.elapsedMs << "ms";
#endif
    
    return result;
}

// =============================================================================
// PDF Batch Export
// =============================================================================

BatchResult exportPdfBatch(const QStringList& bundlePaths,
                           const ExportPdfOptions& options,
                           ProgressCallback progress,
                           std::atomic<bool>* cancelled)
{
    BatchResult result;
    QElapsedTimer timer;
    timer.start();
    
    const int total = bundlePaths.size();
    
    // Validate inputs
    if (bundlePaths.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    if (options.outputPath.isEmpty()) {
        // No output path - add error for all files
        for (const QString& bundlePath : bundlePaths) {
            FileResult fr;
            fr.inputPath = bundlePath;
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("No output path specified");
            result.results.append(fr);
            result.errorCount++;
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    // Determine single-file vs batch mode
    bool singleFileMode = (bundlePaths.size() == 1) && 
                          isSingleFileOutput(options.outputPath, ".pdf");
    
    // For batch mode, ensure output directory exists
    QString outputDir;
    if (!singleFileMode) {
        outputDir = options.outputPath;
        QDir dir(outputDir);
        if (!dir.exists()) {
            if (!options.dryRun) {
                if (!dir.mkpath(".")) {
                    // Can't create output directory
                    for (const QString& bundlePath : bundlePaths) {
                        FileResult fr;
                        fr.inputPath = bundlePath;
                        fr.status = FileStatus::Error;
                        fr.message = QObject::tr("Failed to create output directory: %1").arg(outputDir);
                        result.results.append(fr);
                        result.errorCount++;
                    }
                    result.elapsedMs = timer.elapsed();
                    return result;
                }
            }
        }
    }
    
    // Process each bundle
    for (int i = 0; i < total; ++i) {
        const QString& bundlePath = bundlePaths.at(i);
        FileResult fr;
        fr.inputPath = bundlePath;
        
        // Check cancellation
        if (cancelled && cancelled->load()) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Cancelled");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Report progress
        if (progress) {
            progress(i + 1, total, bundlePath, QObject::tr("Exporting to PDF..."));
        }
        
        // Determine output path
        QString outputPath;
        if (singleFileMode) {
            outputPath = options.outputPath;
        } else {
            outputPath = generateOutputPath(bundlePath, outputDir, ".pdf");
        }
        fr.outputPath = outputPath;
        
        // Check if output exists
        if (QFile::exists(outputPath) && !options.overwrite) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Output file already exists");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Validate bundle before loading
        if (!isValidBundle(bundlePath)) {
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("Not a valid SpeedyNote bundle");
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Load document from bundle
        std::unique_ptr<Document> doc = Document::loadBundle(bundlePath);
        if (!doc) {
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("Failed to load document");
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Check if edgeless - PDF export not supported for edgeless notebooks
        if (doc->isEdgeless()) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Edgeless notebooks cannot be exported to PDF");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Dry run - just report what would happen
        if (options.dryRun) {
            fr.status = FileStatus::Success;
            fr.message = QObject::tr("Would export to: %1").arg(outputPath);
            fr.pagesProcessed = doc->pageCount();
            result.results.append(fr);
            result.successCount++;
            continue;
        }
        
        // Create MuPdfExporter and configure options
        MuPdfExporter exporter;
        exporter.setDocument(doc.get());
        
        PdfExportOptions pdfOpts;
        pdfOpts.outputPath = outputPath;
        pdfOpts.dpi = options.dpi;
        pdfOpts.pageRange = options.pageRange;
        pdfOpts.preserveMetadata = options.preserveMetadata;
        pdfOpts.preserveOutline = options.preserveOutline;
        pdfOpts.annotationsOnly = options.annotationsOnly;
        
        // Perform export
        PdfExportResult exportResult = exporter.exportPdf(pdfOpts);
        
        if (exportResult.success) {
            fr.status = FileStatus::Success;
            fr.outputSize = exportResult.fileSizeBytes;
            fr.pagesProcessed = exportResult.pagesExported;
            result.successCount++;
            result.totalOutputSize += exportResult.fileSizeBytes;
        } else {
            fr.status = FileStatus::Error;
            fr.message = exportResult.errorMessage;
            result.errorCount++;
        }
        
        result.results.append(fr);
        
        // Document is automatically destroyed here (unique_ptr goes out of scope)
    }
    
    result.elapsedMs = timer.elapsed();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[BatchOps] exportPdfBatch complete:"
             << result.successCount << "success,"
             << result.skippedCount << "skipped,"
             << result.errorCount << "errors,"
             << result.elapsedMs << "ms";
#endif
    
    return result;
}

// =============================================================================
// SNBX Batch Import
// =============================================================================

/**
 * @brief Derive expected bundle name from .snbx filename.
 * 
 * Converts "MyNote.snbx" → "MyNote.snb"
 * If input doesn't end with .snbx, appends .snb
 */
static QString deriveExpectedBundleName(const QString& snbxPath)
{
    QFileInfo info(snbxPath);
    QString baseName = info.completeBaseName();  // "MyNote" from "MyNote.snbx"
    
    // Ensure .snb extension
    if (!baseName.endsWith(".snb", Qt::CaseInsensitive)) {
        baseName += ".snb";
    }
    
    return baseName;
}

/**
 * @brief Ensure a bundle path has .snb extension, renaming if necessary.
 * @param bundlePath Path to the bundle directory
 * @return New path if renamed, original path if already correct, empty if rename failed
 */
static QString ensureSnbExtension(const QString& bundlePath)
{
    if (bundlePath.endsWith(".snb", Qt::CaseInsensitive)) {
        return bundlePath;  // Already has correct extension
    }
    
    // Need to rename
    QString newPath = bundlePath + ".snb";
    
    // Check if target already exists
    if (QDir(newPath).exists()) {
        // Find a unique name
        int counter = 1;
        while (QDir(newPath).exists()) {
            newPath = bundlePath + QString(" (%1).snb").arg(counter++);
        }
    }
    
    QDir dir;
    if (dir.rename(bundlePath, newPath)) {
        return newPath;
    }
    
    return QString();  // Rename failed
}

BatchResult importSnbxBatch(const QStringList& snbxPaths,
                            const ImportOptions& options,
                            ProgressCallback progress,
                            std::atomic<bool>* cancelled)
{
    BatchResult result;
    QElapsedTimer timer;
    timer.start();
    
    const int total = snbxPaths.size();
    
    // Validate inputs
    if (snbxPaths.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    if (options.destDir.isEmpty()) {
        // No destination directory - add error for all files
        for (const QString& snbxPath : snbxPaths) {
            FileResult fr;
            fr.inputPath = snbxPath;
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("No destination directory specified");
            result.results.append(fr);
            result.errorCount++;
        }
        result.elapsedMs = timer.elapsed();
        return result;
    }
    
    // Ensure destination directory exists
    QDir destDir(options.destDir);
    if (!destDir.exists()) {
        if (!options.dryRun) {
            if (!destDir.mkpath(".")) {
                // Can't create destination directory
                for (const QString& snbxPath : snbxPaths) {
                    FileResult fr;
                    fr.inputPath = snbxPath;
                    fr.status = FileStatus::Error;
                    fr.message = QObject::tr("Failed to create destination directory: %1").arg(options.destDir);
                    result.results.append(fr);
                    result.errorCount++;
                }
                result.elapsedMs = timer.elapsed();
                return result;
            }
        }
    }
    
    // Process each .snbx file
    for (int i = 0; i < total; ++i) {
        const QString& snbxPath = snbxPaths.at(i);
        FileResult fr;
        fr.inputPath = snbxPath;
        
        // Check cancellation
        if (cancelled && cancelled->load()) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Cancelled");
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Report progress
        if (progress) {
            progress(i + 1, total, snbxPath, QObject::tr("Importing..."));
        }
        
        // Check if input file exists
        if (!QFile::exists(snbxPath)) {
            fr.status = FileStatus::Error;
            fr.message = QObject::tr("File not found");
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Derive expected output path for overwrite checking
        QString expectedBundleName = deriveExpectedBundleName(snbxPath);
        QString expectedOutputPath = options.destDir;
        if (!expectedOutputPath.endsWith('/') && !expectedOutputPath.endsWith('\\')) {
            expectedOutputPath += '/';
        }
        expectedOutputPath += expectedBundleName;
        fr.outputPath = expectedOutputPath;
        
        // Check if output already exists (based on expected name)
        if (QDir(expectedOutputPath).exists() && !options.overwrite) {
            fr.status = FileStatus::Skipped;
            fr.message = QObject::tr("Notebook already exists: %1").arg(expectedBundleName);
            result.results.append(fr);
            result.skippedCount++;
            continue;
        }
        
        // Dry run - just report what would happen
        if (options.dryRun) {
            fr.status = FileStatus::Success;
            fr.message = QObject::tr("Would import to: %1").arg(expectedOutputPath);
            result.results.append(fr);
            result.successCount++;
            continue;
        }
        
        // If overwrite is enabled and target exists, remove it first
        if (QDir(expectedOutputPath).exists() && options.overwrite) {
            QDir existingDir(expectedOutputPath);
            if (!existingDir.removeRecursively()) {
                fr.status = FileStatus::Error;
                fr.message = QObject::tr("Failed to remove existing notebook for overwrite");
                result.results.append(fr);
                result.errorCount++;
                continue;
            }
        }
        
        // Call NotebookImporter
        NotebookImporter::ImportResult importResult = 
            NotebookImporter::importPackage(snbxPath, options.destDir);
        
        if (!importResult.success) {
            fr.status = FileStatus::Error;
            fr.message = importResult.errorMessage;
            result.results.append(fr);
            result.errorCount++;
            continue;
        }
        
        // Ensure .snb extension on imported bundle
        QString finalPath = importResult.extractedSnbPath;
        if (!finalPath.endsWith(".snb", Qt::CaseInsensitive)) {
            QString renamedPath = ensureSnbExtension(finalPath);
            if (renamedPath.isEmpty()) {
                // Rename failed, but import succeeded - report warning
                fr.status = FileStatus::Success;
                fr.outputPath = finalPath;
                fr.message = QObject::tr("Imported but could not add .snb extension");
            } else {
                finalPath = renamedPath;
            }
        }
        
        // Calculate output size (bundle directory size)
        qint64 bundleSize = 0;
        QDirIterator it(finalPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            bundleSize += it.fileInfo().size();
        }
        
        // Register in NotebookLibrary if requested
        if (options.addToLibrary) {
            NotebookLibrary::instance()->addToRecent(finalPath);
        }
        
        fr.status = FileStatus::Success;
        fr.outputPath = finalPath;
        fr.outputSize = bundleSize;
        result.successCount++;
        result.totalOutputSize += bundleSize;
        
        result.results.append(fr);
    }
    
    result.elapsedMs = timer.elapsed();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[BatchOps] importSnbxBatch complete:"
             << result.successCount << "success,"
             << result.skippedCount << "skipped,"
             << result.errorCount << "errors,"
             << result.elapsedMs << "ms";
#endif
    
    return result;
}

} // namespace BatchOps
