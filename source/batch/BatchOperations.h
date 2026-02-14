#ifndef BATCHOPERATIONS_H
#define BATCHOPERATIONS_H

/**
 * @file BatchOperations.h
 * @brief Batch export/import operations for SpeedyNote notebooks.
 * 
 * This module provides headless batch processing capabilities:
 * - Export multiple notebooks to SNBX packages
 * - Export multiple notebooks to PDF
 * - Import multiple SNBX packages
 * 
 * Used by:
 * - Desktop CLI (Phase 2)
 * - Android Launcher batch UI (Phase 3, future)
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include <QString>
#include <QStringList>
#include <QList>
#include <functional>
#include <atomic>

namespace BatchOps {

// =============================================================================
// Result Types
// =============================================================================

/**
 * @brief Status of a single file operation.
 */
enum class FileStatus {
    Success,        ///< Operation completed successfully
    Skipped,        ///< Skipped (output exists, edgeless for PDF, etc.)
    Error           ///< Operation failed
};

/**
 * @brief Result for a single file operation.
 */
struct FileResult {
    QString inputPath;              ///< Path to input file/bundle
    QString outputPath;             ///< Path to output file (empty if skipped/error)
    FileStatus status = FileStatus::Error;
    QString message;                ///< Error message or skip reason
    qint64 outputSize = 0;          ///< Output file size in bytes (0 if not created)
    int pagesProcessed = 0;         ///< Number of pages exported (PDF only)
};

/**
 * @brief Summary result for a batch operation.
 */
struct BatchResult {
    QList<FileResult> results;      ///< Per-file results
    int successCount = 0;           ///< Number of successful operations
    int skippedCount = 0;           ///< Number of skipped files
    int errorCount = 0;             ///< Number of failed operations
    qint64 totalOutputSize = 0;     ///< Total size of all output files
    qint64 elapsedMs = 0;           ///< Total elapsed time in milliseconds
    
    /// @brief Check if any errors occurred.
    bool hasErrors() const { return errorCount > 0; }
    
    /// @brief Check if all files were processed successfully (no errors or skips).
    bool allSucceeded() const { return errorCount == 0 && skippedCount == 0; }
    
    /// @brief Get total number of files processed.
    int totalCount() const { return successCount + skippedCount + errorCount; }
};

// =============================================================================
// Progress Callback
// =============================================================================

/**
 * @brief Progress callback signature.
 * 
 * Called before processing each file to report progress.
 * 
 * @param current Current file index (1-based)
 * @param total Total number of files to process
 * @param currentFile Path to file being processed
 * @param status Brief status message (e.g., "Exporting...", "Skipped")
 */
using ProgressCallback = std::function<void(int current, int total,
                                            const QString& currentFile,
                                            const QString& status)>;

/**
 * @brief Result callback signature.
 * 
 * Called after each file is processed with the result. This enables
 * progressive output (printing results as files complete) rather than
 * waiting until the entire batch finishes.
 * 
 * Return true to continue processing the next file, or false to stop
 * early (e.g., for fail-fast behavior on error).
 * 
 * @param current Current file index (1-based)
 * @param total Total number of files to process
 * @param result The completed file result
 * @return true to continue, false to stop processing remaining files
 */
using ResultCallback = std::function<bool(int current, int total,
                                         const FileResult& result)>;

// =============================================================================
// Export Options
// =============================================================================

/**
 * @brief Options for SNBX (package) export.
 */
struct ExportSnbxOptions {
    QString outputPath;             ///< Output file (single) or directory (batch)
    bool includePdf = true;         ///< Embed source PDF in package
    bool overwrite = false;         ///< Overwrite existing output files
    bool dryRun = false;            ///< Preview only, don't create files
};

/**
 * @brief Options for PDF export.
 */
struct ExportPdfOptions {
    QString outputPath;             ///< Output file (single) or directory (batch)
    int dpi = 150;                  ///< Export resolution (DPI)
    QString pageRange;              ///< Page range (e.g., "1-10,15") or empty for all
    bool preserveMetadata = true;   ///< Preserve PDF metadata from source
    bool preserveOutline = true;    ///< Preserve PDF outline/bookmarks from source
    bool annotationsOnly = false;   ///< Export strokes only on blank background
    bool overwrite = false;         ///< Overwrite existing output files
    bool dryRun = false;            ///< Preview only, don't create files
};

/**
 * @brief Options for SNBX import.
 */
struct ImportOptions {
    QString destDir;                ///< Destination directory for .snb bundles
    bool overwrite = false;         ///< Overwrite existing bundles with same name
    bool dryRun = false;            ///< Preview only, don't extract files
    bool addToLibrary = false;      ///< Register imported notebooks in NotebookLibrary
};

// =============================================================================
// Batch Operation Functions
// =============================================================================

/**
 * @brief Export multiple notebooks to SNBX packages.
 * 
 * Iterates through bundle paths, loads each document, and exports to SNBX format.
 * 
 * Output path handling:
 * - Single bundle + file path: exports to that exact file
 * - Multiple bundles + directory: generates filenames from bundle names
 * - Single bundle + directory: generates filename from bundle name
 * 
 * @param bundlePaths List of .snb bundle paths (directories)
 * @param options Export options
 * @param progress Optional progress callback (called before each file)
 * @param cancelled Optional cancellation flag (checked between files)
 * @return BatchResult with per-file results and summary
 */
BatchResult exportSnbxBatch(const QStringList& bundlePaths,
                            const ExportSnbxOptions& options,
                            ProgressCallback progress = nullptr,
                            std::atomic<bool>* cancelled = nullptr,
                            ResultCallback resultCb = nullptr);

/**
 * @brief Export multiple notebooks to PDF.
 * 
 * Iterates through bundle paths, loads each document, and exports to PDF format.
 * 
 * Important notes:
 * - Edgeless notebooks are skipped with FileStatus::Skipped
 * - Page range applies to all documents (pages out of range are clamped)
 * - annotationsOnly mode exports strokes on blank background
 * 
 * @param bundlePaths List of .snb bundle paths (directories)
 * @param options Export options
 * @param progress Optional progress callback
 * @param cancelled Optional cancellation flag
 * @return BatchResult with per-file results and summary
 */
BatchResult exportPdfBatch(const QStringList& bundlePaths,
                           const ExportPdfOptions& options,
                           ProgressCallback progress = nullptr,
                           std::atomic<bool>* cancelled = nullptr,
                           ResultCallback resultCb = nullptr);

/**
 * @brief Import multiple SNBX packages.
 * 
 * Extracts SNBX packages to the destination directory.
 * Imported bundles are given .snb extension if the original didn't have one.
 * 
 * @param snbxPaths List of .snbx file paths
 * @param options Import options
 * @param progress Optional progress callback
 * @param cancelled Optional cancellation flag
 * @return BatchResult with per-file results and summary
 */
BatchResult importSnbxBatch(const QStringList& snbxPaths,
                            const ImportOptions& options,
                            ProgressCallback progress = nullptr,
                            std::atomic<bool>* cancelled = nullptr,
                            ResultCallback resultCb = nullptr);

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Generate output file path for export.
 * 
 * Extracts the notebook name from the input bundle path and creates
 * an output path with the specified extension.
 * 
 * Example: "/path/to/MyNotes.snb" + "/output/" + ".pdf" -> "/output/MyNotes.pdf"
 * 
 * @param inputPath Input bundle path (directory)
 * @param outputDir Output directory
 * @param extension Output file extension (including dot, e.g., ".pdf")
 * @return Full output file path
 */
QString generateOutputPath(const QString& inputPath,
                           const QString& outputDir,
                           const QString& extension);

/**
 * @brief Determine if output path represents a single file or directory.
 * 
 * Heuristics:
 * - Ends with .pdf or .snbx: single file
 * - Ends with / or is existing directory: directory
 * - Otherwise: assumed to be directory
 * 
 * @param outputPath Output path to check
 * @param extension Expected extension for single-file mode (e.g., ".pdf")
 * @return true if output path is a single file, false if directory
 */
bool isSingleFileOutput(const QString& outputPath, const QString& extension);

} // namespace BatchOps

#endif // BATCHOPERATIONS_H
