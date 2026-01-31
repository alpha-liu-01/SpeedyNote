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
static const char* APP_VERSION = "1.1.6";

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
            "\n"
            "Commands:\n"
            "  export-pdf      Export notebooks to PDF format\n"
            "  export-snbx     Export notebooks to SNBX package format\n"
            "  import          Import SNBX packages as notebooks\n"
            "  (no command)    Launch GUI application\n"
            "\n"
            "Global options:\n"
            "  -h, --help      Show this help message\n"
            "  -v, --version   Show version information\n"
            "\n"
            "Run 'speedynote <command> --help' for command-specific options.\n"
            "\n"
            "Examples:\n"
            "  speedynote export-pdf ~/Notes/MyNote.snb -o ~/Desktop/notes.pdf\n"
            "  speedynote export-snbx ~/Notes/ -o ~/Backup/\n"
            "  speedynote import ~/Downloads/*.snbx -d ~/Notes/\n");
    } else {
        // Command-specific help
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
