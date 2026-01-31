#ifndef CLIHANDLER_H
#define CLIHANDLER_H

/**
 * @file CliHandler.h
 * @brief Command handlers for CLI batch operations.
 * 
 * Provides handler functions for each CLI command:
 * - export-pdf: Export notebooks to PDF format
 * - export-snbx: Export notebooks to SNBX packages
 * - import: Import SNBX packages as notebooks
 * 
 * Each handler parses command-specific options, expands input paths,
 * executes the batch operation, and reports results.
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include "CliParser.h"
#include "../batch/BatchOperations.h"

#include <QCommandLineParser>

namespace Cli {

/**
 * @brief Handle the export-pdf command.
 * 
 * Parses PDF-specific options (--dpi, --pages, --annotations-only, etc.),
 * expands input paths to bundle list, exports each to PDF, and reports results.
 * 
 * @param parser The QCommandLineParser with parsed arguments
 * @return Exit code (see ExitCode namespace)
 */
int handleExportPdf(const QCommandLineParser& parser);

/**
 * @brief Handle the export-snbx command.
 * 
 * Parses SNBX-specific options (--no-pdf, etc.), expands input paths
 * to bundle list, exports each to SNBX package, and reports results.
 * 
 * @param parser The QCommandLineParser with parsed arguments
 * @return Exit code (see ExitCode namespace)
 */
int handleExportSnbx(const QCommandLineParser& parser);

/**
 * @brief Handle the import command.
 * 
 * Parses import options (--dest, --overwrite, etc.), expands input paths
 * to .snbx file list, imports each package, and reports results.
 * 
 * @param parser The QCommandLineParser with parsed arguments
 * @return Exit code (see ExitCode namespace)
 */
int handleImport(const QCommandLineParser& parser);

/**
 * @brief Determine the output mode from parser options.
 * 
 * Checks for --verbose and --json flags.
 * Priority: --json > --verbose > Simple
 * 
 * @param parser The QCommandLineParser with parsed arguments
 * @return The output mode to use
 */
OutputMode getOutputMode(const QCommandLineParser& parser);

/**
 * @brief Determine exit code from batch result.
 * 
 * Maps batch operation results to CLI exit codes:
 * - All succeeded → Success (0)
 * - Some failed → PartialFailure (1)
 * - All failed → TotalFailure (2)
 * 
 * @param result The batch operation result
 * @return Exit code
 */
int exitCodeFromResult(const BatchOps::BatchResult& result);

} // namespace Cli

#endif // CLIHANDLER_H
