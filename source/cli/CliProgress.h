#ifndef CLIPROGRESS_H
#define CLIPROGRESS_H

/**
 * @file CliProgress.h
 * @brief Console progress reporter for CLI batch operations.
 * 
 * Formats progress updates and results for terminal display.
 * Supports three output modes:
 * - Simple: One line per file (`[1/10] MyNote.snb... OK`)
 * - Verbose: Detailed info (size, pages, timing)
 * - JSON: Structured output for scripting
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include "CliParser.h"
#include "../batch/BatchOperations.h"

#include <QTextStream>
#include <QElapsedTimer>

namespace Cli {

/**
 * @brief Progress reporter for console output.
 * 
 * Provides formatted progress output during batch operations.
 * Can be used as a callback for BatchOps functions and for
 * reporting individual file results and final summaries.
 * 
 * Usage:
 * @code
 *   ConsoleProgress progress(OutputMode::Simple);
 *   auto result = BatchOps::exportPdfBatch(bundles, options, progress.callback());
 *   for (const auto& fileResult : result.results) {
 *       progress.reportFile(fileResult);
 *   }
 *   progress.reportSummary(result, options.dryRun);
 * @endcode
 */
class ConsoleProgress {
public:
    /**
     * @brief Construct a progress reporter.
     * @param mode Output mode (Simple, Verbose, or Json)
     */
    explicit ConsoleProgress(OutputMode mode = OutputMode::Simple);
    
    /**
     * @brief Get progress callback for batch operations.
     * 
     * Returns a callback that can be passed to BatchOps functions.
     * The callback displays progress updates before each file is processed.
     * 
     * @return Progress callback function
     */
    BatchOps::ProgressCallback callback();
    
    /**
     * @brief Report completion of a file operation.
     * 
     * Called after each file is processed to display the result.
     * Output format depends on the mode:
     * - Simple: `[1/10] MyNote.snb... OK` (status on same line)
     * - Verbose: Multi-line with details
     * - JSON: `{"type":"file",...}`
     * 
     * @param result The file operation result
     */
    void reportFile(const BatchOps::FileResult& result);
    
    /**
     * @brief Report final batch summary.
     * 
     * Called after all files are processed to display the summary.
     * 
     * @param result The batch result with all file results and summary
     * @param dryRun Whether this was a dry run (affects messaging)
     */
    void reportSummary(const BatchOps::BatchResult& result, bool dryRun);
    
    /**
     * @brief Report an error message.
     * 
     * Outputs an error to stderr. In JSON mode, outputs as JSON object.
     * 
     * @param message Error message to display
     */
    void reportError(const QString& message);
    
    /**
     * @brief Report a warning message.
     * 
     * Outputs a warning to stderr. In JSON mode, outputs as JSON object.
     * 
     * @param message Warning message to display
     */
    void reportWarning(const QString& message);

private:
    // Output a file result in Simple mode
    void reportFileSimple(const BatchOps::FileResult& result);
    
    // Output a file result in Verbose mode
    void reportFileVerbose(const BatchOps::FileResult& result);
    
    // Output a file result in JSON mode
    void reportFileJson(const BatchOps::FileResult& result);
    
    // Output summary in Simple/Verbose mode
    void reportSummaryText(const BatchOps::BatchResult& result, bool dryRun);
    
    // Output summary in JSON mode
    void reportSummaryJson(const BatchOps::BatchResult& result, bool dryRun);
    
    // Format file size for display (e.g., "1.5 MB")
    static QString formatSize(qint64 bytes);
    
    // Format duration for display (e.g., "1.5s" or "125ms")
    static QString formatDuration(qint64 ms);
    
    // Get short filename from full path
    static QString shortName(const QString& path);
    
    // Convert FileStatus to string
    static QString statusString(BatchOps::FileStatus status);
    
    // Escape string for JSON output
    static QString jsonEscape(const QString& str);

private:
    OutputMode m_mode;
    QTextStream m_out;      ///< stdout stream
    QTextStream m_err;      ///< stderr stream
    int m_currentIndex = 0; ///< Current file index (for progress callback)
    int m_totalCount = 0;   ///< Total file count (for progress callback)
};

} // namespace Cli

#endif // CLIPROGRESS_H
