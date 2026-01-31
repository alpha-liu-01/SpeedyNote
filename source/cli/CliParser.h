#ifndef CLIPARSR_H
#define CLIPARSR_H

/**
 * @file CliParser.h
 * @brief Command-line argument parsing for SpeedyNote batch operations.
 * 
 * This module provides CLI detection and argument parsing for the desktop version
 * of SpeedyNote. When CLI arguments are detected, the application runs in headless
 * mode without launching the GUI.
 * 
 * Supported commands:
 * - export-pdf: Export notebooks to PDF format
 * - export-snbx: Export notebooks to SNBX package format
 * - import: Import SNBX packages as notebooks
 * 
 * @see docs/private/BATCH_OPERATIONS.md for design documentation
 */

#include <QString>
#include <QStringList>
#include <QCommandLineParser>

class QCoreApplication;

namespace Cli {

// =============================================================================
// CLI Commands
// =============================================================================

/**
 * @brief Known CLI commands.
 */
enum class Command {
    None,           ///< No command - launch GUI
    Help,           ///< Show help message
    Version,        ///< Show version information
    ExportPdf,      ///< Export notebooks to PDF
    ExportSnbx,     ///< Export notebooks to SNBX packages
    Import          ///< Import SNBX packages
};

/**
 * @brief Output mode for CLI progress/results.
 */
enum class OutputMode {
    Simple,         ///< One line per file (default)
    Verbose,        ///< Detailed per-file info
    Json            ///< JSON format for scripting
};

// =============================================================================
// Exit Codes
// =============================================================================

/**
 * @brief Exit codes for CLI operations.
 */
namespace ExitCode {
    constexpr int Success = 0;        ///< All operations succeeded
    constexpr int PartialFailure = 1; ///< Some files failed/skipped
    constexpr int TotalFailure = 2;   ///< All files failed
    constexpr int InvalidArgs = 3;    ///< Bad command line arguments
    constexpr int IoError = 4;        ///< Can't read/write files
    constexpr int Cancelled = 5;      ///< Operation cancelled (Ctrl+C)
}

// =============================================================================
// CLI Detection
// =============================================================================

/**
 * @brief Quick check if the application should run in CLI mode.
 * 
 * This is a fast check that looks at the first argument to determine
 * if the user wants to run a batch command. Should be called before
 * creating any Qt application object.
 * 
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return true if a CLI command was detected, false for GUI mode
 */
bool isCliMode(int argc, char* argv[]);

/**
 * @brief Parse the command from command-line arguments.
 * 
 * Extracts the command keyword from argv[1] if present.
 * This is a lightweight check before full argument parsing.
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return The detected command, or Command::None for GUI mode
 */
Command parseCommand(int argc, char* argv[]);

/**
 * @brief Get command name as string.
 * @param cmd The command
 * @return Command name string (e.g., "export-pdf")
 */
QString commandName(Command cmd);

// =============================================================================
// Parser Setup
// =============================================================================

/**
 * @brief Configure QCommandLineParser for a specific command.
 * 
 * Sets up the parser with the appropriate options and positional
 * arguments for the given command.
 * 
 * @param parser The parser to configure
 * @param cmd The command to set up options for
 */
void setupParser(QCommandLineParser& parser, Command cmd);

/**
 * @brief Show help message for a command and exit.
 * 
 * If cmd is Command::None, shows general help with available commands.
 * Otherwise shows command-specific help.
 * 
 * @param parser The configured parser
 * @param cmd The command (or Command::None for general help)
 */
void showHelp(const QCommandLineParser& parser, Command cmd);

/**
 * @brief Show version information and exit.
 */
void showVersion();

// =============================================================================
// Main Entry Point
// =============================================================================

/**
 * @brief Run CLI operations.
 * 
 * This is the main entry point for CLI mode. It parses arguments,
 * executes the requested command, and returns an exit code.
 * 
 * @param app The QCoreApplication instance
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code (see ExitCode namespace)
 */
int run(QCoreApplication& app, int argc, char* argv[]);

} // namespace Cli

#endif // CLIPARSR_H
