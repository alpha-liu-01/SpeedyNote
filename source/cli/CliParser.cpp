#include "CliParser.h"
#include "CliHandler.h"
#include "CliSignal.h"

#include <QCoreApplication>
#include <QTextStream>
#include <cstring>

/**
 * @file CliParser.cpp
 * @brief Implementation of CLI argument parsing.
 * 
 * @see CliParser.h for API documentation
 */

namespace Cli {

// Application version (matches CMakeLists.txt project VERSION)
static const char* APP_VERSION = "1.2.0";

// =============================================================================
// CLI Detection
// =============================================================================

bool isCliMode(int argc, char* argv[])
{
    if (argc < 2) {
        return false;  // No arguments - launch GUI
    }
    
    const char* arg1 = argv[1];
    
    // Check for known commands
    if (std::strcmp(arg1, "export-pdf") == 0 ||
        std::strcmp(arg1, "export-snbx") == 0 ||
        std::strcmp(arg1, "import") == 0) {
        return true;
    }
    
    // Check for help/version flags at position 1
    // (e.g., "speedynote --help" or "speedynote -v")
    if (std::strcmp(arg1, "--help") == 0 ||
        std::strcmp(arg1, "-h") == 0 ||
        std::strcmp(arg1, "--version") == 0 ||
        std::strcmp(arg1, "-v") == 0) {
        return true;
    }
    
    return false;  // Unknown argument - launch GUI
}

Command parseCommand(int argc, char* argv[])
{
    if (argc < 2) {
        return Command::None;
    }
    
    const char* arg1 = argv[1];
    
    // Check for commands
    if (std::strcmp(arg1, "export-pdf") == 0) {
        return Command::ExportPdf;
    }
    if (std::strcmp(arg1, "export-snbx") == 0) {
        return Command::ExportSnbx;
    }
    if (std::strcmp(arg1, "import") == 0) {
        return Command::Import;
    }
    
    // Check for global flags
    if (std::strcmp(arg1, "--help") == 0 || std::strcmp(arg1, "-h") == 0) {
        return Command::Help;
    }
    if (std::strcmp(arg1, "--version") == 0 || std::strcmp(arg1, "-v") == 0) {
        return Command::Version;
    }
    
    return Command::None;
}

QString commandName(Command cmd)
{
    switch (cmd) {
        case Command::ExportPdf:  return QStringLiteral("export-pdf");
        case Command::ExportSnbx: return QStringLiteral("export-snbx");
        case Command::Import:     return QStringLiteral("import");
        case Command::Help:       return QStringLiteral("help");
        case Command::Version:    return QStringLiteral("version");
        default:                  return QString();
    }
}

// =============================================================================
// Parser Setup
// =============================================================================

void setupParser(QCommandLineParser& parser, Command cmd)
{
    parser.setApplicationDescription(
        QCoreApplication::translate("CLI", "SpeedyNote - A fast note-taking application"));
    
    // Add standard help option (--help, -h)
    parser.addHelpOption();
    
    // Add version option (--version, -v)
    parser.addVersionOption();
    
    switch (cmd) {
        case Command::ExportPdf:
            parser.addPositionalArgument(
                QStringLiteral("input"),
                QCoreApplication::translate("CLI", "Notebook paths (.snb folders) or directories"),
                QStringLiteral("[input...]"));
            
            parser.addOption(QCommandLineOption(
                {QStringLiteral("o"), QStringLiteral("output")},
                QCoreApplication::translate("CLI", "Output file (single) or directory (batch)"),
                QStringLiteral("path")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("dpi"),
                QCoreApplication::translate("CLI", "Export DPI (default: 150)"),
                QStringLiteral("N"),
                QStringLiteral("150")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("pages"),
                QCoreApplication::translate("CLI", "Page range, e.g., \"1-10,15,20-25\""),
                QStringLiteral("range")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("no-metadata"),
                QCoreApplication::translate("CLI", "Don't preserve PDF metadata")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("no-outline"),
                QCoreApplication::translate("CLI", "Don't preserve PDF outline/bookmarks")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("annotations-only"),
                QCoreApplication::translate("CLI", "Export strokes only (blank background)")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("overwrite"),
                QCoreApplication::translate("CLI", "Overwrite existing output files")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("recursive"),
                QCoreApplication::translate("CLI", "Search input directories recursively")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("detect-all"),
                QCoreApplication::translate("CLI", "Find bundles without .snb extension")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("fail-fast"),
                QCoreApplication::translate("CLI", "Stop on first error")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("verbose"),
                QCoreApplication::translate("CLI", "Show detailed progress")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("json"),
                QCoreApplication::translate("CLI", "Output results as JSON")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("dry-run"),
                QCoreApplication::translate("CLI", "Preview without creating files")));
            break;
            
        case Command::ExportSnbx:
            parser.addPositionalArgument(
                QStringLiteral("input"),
                QCoreApplication::translate("CLI", "Notebook paths (.snb folders) or directories"),
                QStringLiteral("[input...]"));
            
            parser.addOption(QCommandLineOption(
                {QStringLiteral("o"), QStringLiteral("output")},
                QCoreApplication::translate("CLI", "Output file (single) or directory (batch)"),
                QStringLiteral("path")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("no-pdf"),
                QCoreApplication::translate("CLI", "Don't embed source PDF in package")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("overwrite"),
                QCoreApplication::translate("CLI", "Overwrite existing output files")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("recursive"),
                QCoreApplication::translate("CLI", "Search input directories recursively")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("detect-all"),
                QCoreApplication::translate("CLI", "Find bundles without .snb extension")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("fail-fast"),
                QCoreApplication::translate("CLI", "Stop on first error")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("verbose"),
                QCoreApplication::translate("CLI", "Show detailed progress")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("json"),
                QCoreApplication::translate("CLI", "Output results as JSON")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("dry-run"),
                QCoreApplication::translate("CLI", "Preview without creating files")));
            break;
            
        case Command::Import:
            parser.addPositionalArgument(
                QStringLiteral("input"),
                QCoreApplication::translate("CLI", "SNBX package files or directories"),
                QStringLiteral("[input...]"));
            
            parser.addOption(QCommandLineOption(
                {QStringLiteral("d"), QStringLiteral("dest")},
                QCoreApplication::translate("CLI", "Destination directory for notebooks"),
                QStringLiteral("path")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("overwrite"),
                QCoreApplication::translate("CLI", "Overwrite existing notebooks")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("add-to-library"),
                QCoreApplication::translate("CLI", "Add imported notebooks to the launcher timeline")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("recursive"),
                QCoreApplication::translate("CLI", "Search input directories recursively")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("fail-fast"),
                QCoreApplication::translate("CLI", "Stop on first error")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("verbose"),
                QCoreApplication::translate("CLI", "Show detailed progress")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("json"),
                QCoreApplication::translate("CLI", "Output results as JSON")));
            
            parser.addOption(QCommandLineOption(
                QStringLiteral("dry-run"),
                QCoreApplication::translate("CLI", "Preview without creating files")));
            break;
            
        default:
            // No command-specific options for Help/Version/None
            break;
    }
}

// =============================================================================
// Help and Version
// =============================================================================

void showHelp(const QCommandLineParser& parser, Command cmd)
{
    QTextStream out(stdout);
    
    if (cmd == Command::None || cmd == Command::Help) {
        // General help - show available commands
        out << QCoreApplication::translate("CLI",
            "Usage: speedynote [command] [options] [files...]\n"
            "\n"
            "SpeedyNote - A fast note-taking application with PDF annotation support.\n"
            "Includes a powerful CLI for batch operations, scripting, and automation.\n"
            "\n"
            "COMMANDS:\n"
            "  export-pdf      Export notebooks to PDF format\n"
            "  export-snbx     Export notebooks to .snbx packages (portable backup)\n"
            "  import          Import .snbx packages as notebooks\n"
            "  (no command)    Launch GUI application\n"
            "\n"
            "GLOBAL OPTIONS:\n"
            "  -h, --help      Show this help message\n"
            "  -v, --version   Show version information\n"
            "\n"
            "COMMON OPTIONS (work with all commands):\n"
            "  --verbose       Show detailed progress\n"
            "  --json          Output results as JSON (for scripting)\n"
            "  --fail-fast     Stop on first error\n"
            "  --dry-run       Preview without creating files\n"
            "  --recursive     Search directories recursively\n"
            "  --overwrite     Overwrite existing files\n"
            "\n"
            "QUICK START:\n"
            "  # Export all notebooks to PDF\n"
            "  speedynote export-pdf ~/Notes/ -o ~/PDFs/\n"
            "\n"
            "  # Backup notebooks to .snbx packages\n"
            "  speedynote export-snbx ~/Notes/ -o ~/Backup/\n"
            "\n"
            "  # Import .snbx packages\n"
            "  speedynote import ~/Downloads/*.snbx -d ~/Notes/\n"
            "\n"
            "EXIT CODES:\n"
            "  0   All operations succeeded\n"
            "  1   Some files failed or were skipped\n"
            "  2   All files failed\n"
            "  3   Invalid arguments\n"
            "  5   Cancelled (Ctrl+C)\n"
            "\n"
            "Run 'speedynote <command> --help' for command-specific options.\n");
    } else if (cmd == Command::ExportPdf) {
        // PDF export help
        out << QCoreApplication::translate("CLI",
            "Usage: speedynote export-pdf [OPTIONS] <input>... -o <output>\n"
            "\n"
            "Export notebooks to PDF format.\n"
            "\n"
            "ARGUMENTS:\n"
            "  <input>...              Notebook paths (.snb folders) or directories\n"
            "\n"
            "OUTPUT OPTIONS:\n"
            "  -o, --output <path>     Output file (single) or directory (batch) [required]\n"
            "  --overwrite             Overwrite existing files\n"
            "\n"
            "EXPORT OPTIONS:\n"
            "  --dpi <N>               Export resolution (default: 150)\n"
            "                          Common values: 96 (screen), 150 (draft), 300 (print)\n"
            "  --pages <RANGE>         Page range, e.g., \"1-10,15,20-25\"\n"
            "  --annotations-only      Export strokes only (blank background, no PDF/grid)\n"
            "  --no-metadata           Don't preserve PDF metadata\n"
            "  --no-outline            Don't preserve PDF bookmarks/outline\n"
            "\n"
            "DISCOVERY OPTIONS:\n"
            "  --recursive             Search directories recursively\n"
            "  --detect-all            Find bundles without .snb extension\n"
            "\n"
            "COMMON OPTIONS:\n"
            "  --verbose               Show detailed progress\n"
            "  --json                  Output results as JSON\n"
            "  --fail-fast             Stop on first error\n"
            "  --dry-run               Preview without creating files\n"
            "  -h, --help              Show this help\n"
            "\n"
            "EXAMPLES:\n"
            "  # Single notebook to PDF\n"
            "  speedynote export-pdf ~/Notes/Lecture.snb -o ~/Desktop/lecture.pdf\n"
            "\n"
            "  # All notebooks at 300 DPI (high quality)\n"
            "  speedynote export-pdf ~/Notes/ -o ~/PDFs/ --dpi 300 --recursive\n"
            "\n"
            "  # Export only annotations (no background)\n"
            "  speedynote export-pdf ~/Notes/*.snb -o ~/PDFs/ --annotations-only\n"
            "\n"
            "  # Preview what would be exported\n"
            "  speedynote export-pdf ~/Notes/ -o ~/PDFs/ --dry-run\n"
            "\n"
            "NOTE: Edgeless canvas notebooks are skipped (PDF export requires pages).\n");
    } else if (cmd == Command::ExportSnbx) {
        // SNBX export help
        out << QCoreApplication::translate("CLI",
            "Usage: speedynote export-snbx [OPTIONS] <input>... -o <output>\n"
            "\n"
            "Export notebooks to .snbx packages (portable backup format).\n"
            "\n"
            "ARGUMENTS:\n"
            "  <input>...              Notebook paths (.snb folders) or directories\n"
            "\n"
            "OUTPUT OPTIONS:\n"
            "  -o, --output <path>     Output file (single) or directory (batch) [required]\n"
            "  --overwrite             Overwrite existing files\n"
            "\n"
            "EXPORT OPTIONS:\n"
            "  --no-pdf                Don't embed source PDF (smaller package files)\n"
            "\n"
            "DISCOVERY OPTIONS:\n"
            "  --recursive             Search directories recursively\n"
            "  --detect-all            Find bundles without .snb extension\n"
            "\n"
            "COMMON OPTIONS:\n"
            "  --verbose               Show detailed progress\n"
            "  --json                  Output results as JSON\n"
            "  --fail-fast             Stop on first error\n"
            "  --dry-run               Preview without creating files\n"
            "  -h, --help              Show this help\n"
            "\n"
            "EXAMPLES:\n"
            "  # Backup all notebooks with embedded PDFs\n"
            "  speedynote export-snbx ~/Notes/ -o ~/Backup/\n"
            "\n"
            "  # Backup without PDFs (smaller files)\n"
            "  speedynote export-snbx ~/Notes/ -o ~/Backup/ --no-pdf\n"
            "\n"
            "  # Single notebook backup\n"
            "  speedynote export-snbx ~/Notes/Project.snb -o ~/Desktop/project.snbx\n"
            "\n"
            "  # Recursively backup with dry-run preview\n"
            "  speedynote export-snbx ~/Notes/ -o ~/Backup/ --recursive --dry-run\n"
            "\n"
            "NOTE: .snbx packages can be imported on any device with SpeedyNote.\n");
    } else if (cmd == Command::Import) {
        // Import help
        out << QCoreApplication::translate("CLI",
            "Usage: speedynote import [OPTIONS] <input>... -d <dest>\n"
            "\n"
            "Import .snbx packages as notebooks.\n"
            "\n"
            "ARGUMENTS:\n"
            "  <input>...              .snbx package files or directories containing them\n"
            "\n"
            "OUTPUT OPTIONS:\n"
            "  -d, --dest <path>       Destination directory for notebooks [required]\n"
            "  --overwrite             Overwrite existing notebooks\n"
            "\n"
            "LIBRARY OPTIONS:\n"
            "  --add-to-library        Add imported notebooks to the launcher timeline\n"
            "                          (Without this, notebooks won't appear in launcher)\n"
            "\n"
            "DISCOVERY OPTIONS:\n"
            "  --recursive             Search directories recursively for .snbx files\n"
            "\n"
            "COMMON OPTIONS:\n"
            "  --verbose               Show detailed progress\n"
            "  --json                  Output results as JSON\n"
            "  --fail-fast             Stop on first error\n"
            "  --dry-run               Preview without importing\n"
            "  -h, --help              Show this help\n"
            "\n"
            "EXAMPLES:\n"
            "  # Import packages\n"
            "  speedynote import ~/Downloads/*.snbx -d ~/Notes/\n"
            "\n"
            "  # Import and add to library (shows in launcher)\n"
            "  speedynote import ~/Downloads/*.snbx -d ~/Notes/ --add-to-library\n"
            "\n"
            "  # Import from a backup directory recursively\n"
            "  speedynote import ~/Backup/ -d ~/Notes/ --recursive --add-to-library\n"
            "\n"
            "  # Preview what would be imported\n"
            "  speedynote import ~/Backup/*.snbx -d ~/Notes/ --dry-run\n"
            "\n"
            "NOTE: On Android, imported notebooks are automatically added to the library.\n"
            "      On desktop, use --add-to-library to make them appear in the launcher.\n");
    } else {
        // Fallback to parser's help text
        out << parser.helpText();
    }
}

void showVersion()
{
    QTextStream out(stdout);
    out << "SpeedyNote " << APP_VERSION << "\n";
}

// =============================================================================
// Main Entry Point
// =============================================================================

int run(QCoreApplication& app, int argc, char* argv[])
{
    Q_UNUSED(app)
    
    // Install signal handlers for graceful Ctrl+C handling
    installSignalHandlers();
    
    // Parse the command
    Command cmd = parseCommand(argc, argv);
    
    // Handle help and version immediately
    if (cmd == Command::Version) {
        showVersion();
        return ExitCode::Success;
    }
    
    if (cmd == Command::Help || cmd == Command::None) {
        QCommandLineParser parser;
        setupParser(parser, Command::None);
        showHelp(parser, cmd);
        return (cmd == Command::Help) ? ExitCode::Success : ExitCode::InvalidArgs;
    }
    
    // Set up parser for the specific command
    QCommandLineParser parser;
    setupParser(parser, cmd);
    
    // Build argument list without the command name
    // (QCommandLineParser doesn't understand subcommands)
    QStringList args;
    args << QString::fromLocal8Bit(argv[0]);  // Program name
    for (int i = 2; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }
    
    // Parse arguments
    if (!parser.parse(args)) {
        QTextStream err(stderr);
        err << QCoreApplication::translate("CLI", "Error: ") 
            << parser.errorText() << "\n\n";
        showHelp(parser, cmd);
        return ExitCode::InvalidArgs;
    }
    
    // Check for help flag on specific command
    if (parser.isSet(QStringLiteral("help"))) {
        showHelp(parser, cmd);
        return ExitCode::Success;
    }
    
    // Check for version flag
    if (parser.isSet(QStringLiteral("version"))) {
        showVersion();
        return ExitCode::Success;
    }
    
    // Dispatch to command handlers
    switch (cmd) {
        case Command::ExportPdf:
            return handleExportPdf(parser);
        case Command::ExportSnbx:
            return handleExportSnbx(parser);
        case Command::Import:
            return handleImport(parser);
        default:
            // Should not reach here - Help/Version/None handled above
            return ExitCode::InvalidArgs;
    }
}

} // namespace Cli
