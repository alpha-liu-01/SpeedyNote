#include "CliHandler.h"
#include "CliProgress.h"
#include "CliSignal.h"
#include "../batch/BundleDiscovery.h"
#include "../core/NotebookLibrary.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>

/**
 * @file CliHandler.cpp
 * @brief Implementation of CLI command handlers.
 * 
 * @see CliHandler.h for API documentation
 */

namespace Cli {

// =============================================================================
// Helper Functions
// =============================================================================

OutputMode getOutputMode(const QCommandLineParser& parser)
{
    if (parser.isSet(QStringLiteral("json"))) {
        return OutputMode::Json;
    }
    if (parser.isSet(QStringLiteral("verbose"))) {
        return OutputMode::Verbose;
    }
    return OutputMode::Simple;
}

int exitCodeFromResult(const BatchOps::BatchResult& result)
{
    if (result.totalCount() == 0) {
        // No files processed - treat as error
        return ExitCode::InvalidArgs;
    }
    if (result.errorCount == 0) {
        return ExitCode::Success;
    }
    if (result.successCount == 0 && result.skippedCount == 0) {
        // All files failed
        return ExitCode::TotalFailure;
    }
    // Some files failed
    return ExitCode::PartialFailure;
}

// =============================================================================
// Export PDF Handler
// =============================================================================

int handleExportPdf(const QCommandLineParser& parser)
{
    // Get output mode for progress reporting
    OutputMode outputMode = getOutputMode(parser);
    ConsoleProgress progress(outputMode);
    
    // Get input paths
    QStringList inputPaths = parser.positionalArguments();
    if (inputPaths.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI", 
            "No input files specified. Use 'speedynote export-pdf --help' for usage."));
        return ExitCode::InvalidArgs;
    }
    
    // Get output path
    QString outputPath = parser.value(QStringLiteral("output"));
    if (outputPath.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Output path required. Use -o or --output to specify destination."));
        return ExitCode::InvalidArgs;
    }
    
    // Expand output path to absolute
    outputPath = QDir::cleanPath(QDir::current().absoluteFilePath(outputPath));
    
    // Set up discovery options
    BatchOps::DiscoveryOptions discoveryOpts;
    discoveryOpts.recursive = parser.isSet(QStringLiteral("recursive"));
    discoveryOpts.detectAll = parser.isSet(QStringLiteral("detect-all"));
    
    // Expand input paths to bundle list
    QStringList bundles = BatchOps::expandInputPaths(inputPaths, discoveryOpts);
    if (bundles.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "No valid notebooks found in the specified paths."));
        return ExitCode::InvalidArgs;
    }
    
    // Validate: can't export multiple notebooks to a single file
    bool outputIsFile = BatchOps::isSingleFileOutput(outputPath, ".pdf");
    if (bundles.size() > 1 && outputIsFile) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Cannot export %1 notebooks to a single PDF file.\n"
            "Use a directory as output destination, e.g.: -o ~/PDFs/")
            .arg(bundles.size()));
        return ExitCode::InvalidArgs;
    }
    
    // Parse PDF export options
    BatchOps::ExportPdfOptions options;
    options.outputPath = outputPath;
    options.overwrite = parser.isSet(QStringLiteral("overwrite"));
    options.dryRun = parser.isSet(QStringLiteral("dry-run"));
    
    // DPI
    bool dpiOk = false;
    int dpi = parser.value(QStringLiteral("dpi")).toInt(&dpiOk);
    if (dpiOk && dpi > 0) {
        options.dpi = dpi;
    }
    
    // Page range
    options.pageRange = parser.value(QStringLiteral("pages"));
    
    // Metadata and outline
    options.preserveMetadata = !parser.isSet(QStringLiteral("no-metadata"));
    options.preserveOutline = !parser.isSet(QStringLiteral("no-outline"));
    
    // Annotations only
    options.annotationsOnly = parser.isSet(QStringLiteral("annotations-only"));
    
    // Fail-fast support
    bool failFast = parser.isSet(QStringLiteral("fail-fast"));
    
    // Get global cancellation flag (set by Ctrl+C signal handler)
    std::atomic<bool>* cancelled = getCancellationFlag();
    
    // Execute batch operation
    BatchOps::BatchResult result = BatchOps::exportPdfBatch(
        bundles, options, progress.callback(), cancelled);
    
    // Report individual file results
    for (int i = 0; i < result.results.size(); ++i) {
        const auto& fileResult = result.results[i];
        progress.reportFile(fileResult);
        
        // Check for fail-fast after each file
        if (failFast && fileResult.status == BatchOps::FileStatus::Error) {
            if (i < result.results.size() - 1) {
                progress.reportWarning(QCoreApplication::translate("CLI",
                    "Stopping due to --fail-fast flag."));
            }
            break;
        }
    }
    
    // Report summary
    progress.reportSummary(result, options.dryRun);
    
    // Check if cancelled by Ctrl+C
    if (wasCancelled()) {
        return ExitCode::Cancelled;
    }
    
    return exitCodeFromResult(result);
}

// =============================================================================
// Export SNBX Handler
// =============================================================================

int handleExportSnbx(const QCommandLineParser& parser)
{
    // Get output mode for progress reporting
    OutputMode outputMode = getOutputMode(parser);
    ConsoleProgress progress(outputMode);
    
    // Get input paths
    QStringList inputPaths = parser.positionalArguments();
    if (inputPaths.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "No input files specified. Use 'speedynote export-snbx --help' for usage."));
        return ExitCode::InvalidArgs;
    }
    
    // Get output path
    QString outputPath = parser.value(QStringLiteral("output"));
    if (outputPath.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Output path required. Use -o or --output to specify destination."));
        return ExitCode::InvalidArgs;
    }
    
    // Expand output path to absolute
    outputPath = QDir::cleanPath(QDir::current().absoluteFilePath(outputPath));
    
    // Set up discovery options
    BatchOps::DiscoveryOptions discoveryOpts;
    discoveryOpts.recursive = parser.isSet(QStringLiteral("recursive"));
    discoveryOpts.detectAll = parser.isSet(QStringLiteral("detect-all"));
    
    // Expand input paths to bundle list
    QStringList bundles = BatchOps::expandInputPaths(inputPaths, discoveryOpts);
    if (bundles.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "No valid notebooks found in the specified paths."));
        return ExitCode::InvalidArgs;
    }
    
    // Validate: can't export multiple notebooks to a single file
    bool outputIsFile = BatchOps::isSingleFileOutput(outputPath, ".snbx");
    if (bundles.size() > 1 && outputIsFile) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Cannot export %1 notebooks to a single SNBX file.\n"
            "Use a directory as output destination, e.g.: -o ~/Backup/")
            .arg(bundles.size()));
        return ExitCode::InvalidArgs;
    }
    
    // Parse SNBX export options
    BatchOps::ExportSnbxOptions options;
    options.outputPath = outputPath;
    options.overwrite = parser.isSet(QStringLiteral("overwrite"));
    options.dryRun = parser.isSet(QStringLiteral("dry-run"));
    options.includePdf = !parser.isSet(QStringLiteral("no-pdf"));
    
    // Fail-fast support
    bool failFast = parser.isSet(QStringLiteral("fail-fast"));
    
    // Get global cancellation flag (set by Ctrl+C signal handler)
    std::atomic<bool>* cancelled = getCancellationFlag();
    
    // Execute batch operation
    BatchOps::BatchResult result = BatchOps::exportSnbxBatch(
        bundles, options, progress.callback(), cancelled);
    
    // Report individual file results
    for (int i = 0; i < result.results.size(); ++i) {
        const auto& fileResult = result.results[i];
        progress.reportFile(fileResult);
        
        // Check for fail-fast after each file
        if (failFast && fileResult.status == BatchOps::FileStatus::Error) {
            if (i < result.results.size() - 1) {
                progress.reportWarning(QCoreApplication::translate("CLI",
                    "Stopping due to --fail-fast flag."));
            }
            break;
        }
    }
    
    // Report summary
    progress.reportSummary(result, options.dryRun);
    
    // Check if cancelled by Ctrl+C
    if (wasCancelled()) {
        return ExitCode::Cancelled;
    }
    
    return exitCodeFromResult(result);
}

// =============================================================================
// Import Handler
// =============================================================================

int handleImport(const QCommandLineParser& parser)
{
    // Get output mode for progress reporting
    OutputMode outputMode = getOutputMode(parser);
    ConsoleProgress progress(outputMode);
    
    // Get input paths
    QStringList inputPaths = parser.positionalArguments();
    if (inputPaths.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "No input files specified. Use 'speedynote import --help' for usage."));
        return ExitCode::InvalidArgs;
    }
    
    // Get destination directory
    QString destDir = parser.value(QStringLiteral("dest"));
    if (destDir.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Destination directory required. Use -d or --dest to specify."));
        return ExitCode::InvalidArgs;
    }
    
    // Expand destination path to absolute
    destDir = QDir::cleanPath(QDir::current().absoluteFilePath(destDir));
    
    // Check if destination is a valid directory (or can be created)
    QFileInfo destInfo(destDir);
    if (destInfo.exists() && !destInfo.isDir()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "Destination must be a directory, not a file."));
        return ExitCode::InvalidArgs;
    }
    
    // Expand input paths to package list
    bool recursive = parser.isSet(QStringLiteral("recursive"));
    QStringList packages = BatchOps::expandPackagePaths(inputPaths, recursive);
    if (packages.isEmpty()) {
        progress.reportError(QCoreApplication::translate("CLI",
            "No valid .snbx packages found in the specified paths."));
        return ExitCode::InvalidArgs;
    }
    
    // Parse import options
    BatchOps::ImportOptions options;
    options.destDir = destDir;
    options.overwrite = parser.isSet(QStringLiteral("overwrite"));
    options.dryRun = parser.isSet(QStringLiteral("dry-run"));
    options.addToLibrary = parser.isSet(QStringLiteral("add-to-library"));
    
    // Fail-fast support
    bool failFast = parser.isSet(QStringLiteral("fail-fast"));
    
    // Get global cancellation flag (set by Ctrl+C signal handler)
    std::atomic<bool>* cancelled = getCancellationFlag();
    
    // Execute batch operation
    BatchOps::BatchResult result = BatchOps::importSnbxBatch(
        packages, options, progress.callback(), cancelled);
    
    // Report individual file results
    for (int i = 0; i < result.results.size(); ++i) {
        const auto& fileResult = result.results[i];
        progress.reportFile(fileResult);
        
        // Check for fail-fast after each file
        if (failFast && fileResult.status == BatchOps::FileStatus::Error) {
            if (i < result.results.size() - 1) {
                progress.reportWarning(QCoreApplication::translate("CLI",
                    "Stopping due to --fail-fast flag."));
            }
            break;
        }
    }
    
    // Report summary
    progress.reportSummary(result, options.dryRun);
    
    // If we added notebooks to library, save it immediately
    // (CLI exits before the debounced save timer would fire)
    if (options.addToLibrary && result.successCount > 0 && !options.dryRun) {
        NotebookLibrary::instance()->save();
    }
    
    // Check if cancelled by Ctrl+C
    if (wasCancelled()) {
        return ExitCode::Cancelled;
    }
    
    return exitCodeFromResult(result);
}

} // namespace Cli
