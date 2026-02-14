#include "CliProgress.h"

#include <QFileInfo>
#include <QCoreApplication>

/**
 * @file CliProgress.cpp
 * @brief Implementation of console progress reporter.
 * 
 * @see CliProgress.h for API documentation
 */

namespace Cli {

// =============================================================================
// Constructor
// =============================================================================

ConsoleProgress::ConsoleProgress(OutputMode mode)
    : m_mode(mode)
    , m_out(stdout)
    , m_err(stderr)
{
}

// =============================================================================
// Progress Callback
// =============================================================================

BatchOps::ProgressCallback ConsoleProgress::callback()
{
    return [this](int current, int total, const QString& currentFile, const QString& status) {
        m_currentIndex = current;
        m_totalCount = total;
        
        if (m_mode == OutputMode::Json) {
            // In JSON mode, we don't output progress, only file results
            return;
        }
        
        if (m_mode == OutputMode::Verbose) {
            // Verbose: show what we're about to process
            m_out << QStringLiteral("[%1/%2] %3: %4\n")
                     .arg(current)
                     .arg(total)
                     .arg(shortName(currentFile))
                     .arg(status);
            m_out.flush();
        }
        // Simple mode: progress is shown as part of reportFile()
    };
}

// =============================================================================
// File Result Reporting
// =============================================================================

void ConsoleProgress::reportFile(const BatchOps::FileResult& result)
{
    switch (m_mode) {
        case OutputMode::Simple:
            reportFileSimple(result);
            break;
        case OutputMode::Verbose:
            reportFileVerbose(result);
            break;
        case OutputMode::Json:
            reportFileJson(result);
            break;
    }
}

void ConsoleProgress::reportFile(int index, int total, const BatchOps::FileResult& result)
{
    m_currentIndex = index;
    m_totalCount = total;
    reportFile(result);
}

void ConsoleProgress::reportFileSimple(const BatchOps::FileResult& result)
{
    // Format: [1/10] MyNote.snb... OK
    // Format: [2/10] Canvas.snb... SKIPPED (edgeless)
    // Format: [3/10] Broken.snb... ERROR: message
    
    QString statusStr;
    switch (result.status) {
        case BatchOps::FileStatus::Success:
            statusStr = QCoreApplication::translate("CLI", "OK");
            if (result.pagesProcessed > 0) {
                statusStr += QStringLiteral(" (%1 pages)").arg(result.pagesProcessed);
            }
            break;
        case BatchOps::FileStatus::Skipped:
            statusStr = QCoreApplication::translate("CLI", "SKIPPED");
            if (!result.message.isEmpty()) {
                statusStr += QStringLiteral(" (%1)").arg(result.message);
            }
            break;
        case BatchOps::FileStatus::Error:
            statusStr = QCoreApplication::translate("CLI", "ERROR");
            if (!result.message.isEmpty()) {
                statusStr += QStringLiteral(": %1").arg(result.message);
            }
            break;
    }
    
    m_out << QStringLiteral("[%1/%2] %3... %4\n")
             .arg(m_currentIndex)
             .arg(m_totalCount)
             .arg(shortName(result.inputPath))
             .arg(statusStr);
    m_out.flush();
}

void ConsoleProgress::reportFileVerbose(const BatchOps::FileResult& result)
{
    // Verbose: show detailed info
    // Input:  /path/to/MyNote.snb
    // Output: /path/to/MyNote.pdf
    // Status: Success (12 pages, 1.5 MB)
    
    m_out << QCoreApplication::translate("CLI", "  Input:  ") << result.inputPath << "\n";
    
    if (!result.outputPath.isEmpty()) {
        m_out << QCoreApplication::translate("CLI", "  Output: ") << result.outputPath << "\n";
    }
    
    m_out << QCoreApplication::translate("CLI", "  Status: ");
    switch (result.status) {
        case BatchOps::FileStatus::Success:
            m_out << QCoreApplication::translate("CLI", "Success");
            if (result.pagesProcessed > 0) {
                m_out << QStringLiteral(" (%1 pages").arg(result.pagesProcessed);
                if (result.outputSize > 0) {
                    m_out << ", " << formatSize(result.outputSize);
                }
                m_out << ")";
            } else if (result.outputSize > 0) {
                m_out << " (" << formatSize(result.outputSize) << ")";
            }
            break;
        case BatchOps::FileStatus::Skipped:
            m_out << QCoreApplication::translate("CLI", "Skipped");
            if (!result.message.isEmpty()) {
                m_out << " - " << result.message;
            }
            break;
        case BatchOps::FileStatus::Error:
            m_out << QCoreApplication::translate("CLI", "Error");
            if (!result.message.isEmpty()) {
                m_out << " - " << result.message;
            }
            break;
    }
    m_out << "\n\n";
    m_out.flush();
}

void ConsoleProgress::reportFileJson(const BatchOps::FileResult& result)
{
    // JSON format (one object per line):
    // {"type":"file","input":"/path/to/Note.snb","output":"/path/to/Note.pdf","status":"success","size":245000,"pages":12}
    
    m_out << "{\"type\":\"file\""
          << ",\"input\":\"" << jsonEscape(result.inputPath) << "\"";
    
    if (!result.outputPath.isEmpty()) {
        m_out << ",\"output\":\"" << jsonEscape(result.outputPath) << "\"";
    } else {
        m_out << ",\"output\":\"\"";
    }
    
    m_out << ",\"status\":\"" << statusString(result.status) << "\"";
    
    if (result.outputSize > 0) {
        m_out << ",\"size\":" << result.outputSize;
    }
    
    if (result.pagesProcessed > 0) {
        m_out << ",\"pages\":" << result.pagesProcessed;
    }
    
    if (!result.message.isEmpty()) {
        m_out << ",\"message\":\"" << jsonEscape(result.message) << "\"";
    }
    
    m_out << "}\n";
    m_out.flush();
}

// =============================================================================
// Summary Reporting
// =============================================================================

void ConsoleProgress::reportSummary(const BatchOps::BatchResult& result, bool dryRun)
{
    if (m_mode == OutputMode::Json) {
        reportSummaryJson(result, dryRun);
    } else {
        reportSummaryText(result, dryRun);
    }
}

void ConsoleProgress::reportSummaryText(const BatchOps::BatchResult& result, bool dryRun)
{
    m_out << "\n";
    
    if (dryRun) {
        m_out << QCoreApplication::translate("CLI", "=== Dry Run Summary ===\n");
    } else {
        m_out << QCoreApplication::translate("CLI", "=== Summary ===\n");
    }
    
    m_out << QCoreApplication::translate("CLI", "Total:    ") 
          << result.totalCount() << QCoreApplication::translate("CLI", " files\n");
    m_out << QCoreApplication::translate("CLI", "Success:  ") << result.successCount << "\n";
    
    if (result.skippedCount > 0) {
        m_out << QCoreApplication::translate("CLI", "Skipped:  ") << result.skippedCount << "\n";
    }
    
    if (result.errorCount > 0) {
        m_out << QCoreApplication::translate("CLI", "Errors:   ") << result.errorCount << "\n";
    }
    
    if (result.totalOutputSize > 0 && !dryRun) {
        m_out << QCoreApplication::translate("CLI", "Size:     ") 
              << formatSize(result.totalOutputSize) << "\n";
    }
    
    m_out << QCoreApplication::translate("CLI", "Time:     ") 
          << formatDuration(result.elapsedMs) << "\n";
    
    m_out.flush();
}

void ConsoleProgress::reportSummaryJson(const BatchOps::BatchResult& result, bool dryRun)
{
    // JSON format:
    // {"type":"summary","success":8,"skipped":1,"errors":1,"total_size":1950000,"elapsed_ms":4523,"dry_run":false}
    
    m_out << "{\"type\":\"summary\""
          << ",\"total\":" << result.totalCount()
          << ",\"success\":" << result.successCount
          << ",\"skipped\":" << result.skippedCount
          << ",\"errors\":" << result.errorCount
          << ",\"total_size\":" << result.totalOutputSize
          << ",\"elapsed_ms\":" << result.elapsedMs
          << ",\"dry_run\":" << (dryRun ? "true" : "false")
          << "}\n";
    m_out.flush();
}

// =============================================================================
// Error/Warning Reporting
// =============================================================================

void ConsoleProgress::reportError(const QString& message)
{
    if (m_mode == OutputMode::Json) {
        m_err << "{\"type\":\"error\",\"message\":\"" << jsonEscape(message) << "\"}\n";
    } else {
        m_err << QCoreApplication::translate("CLI", "Error: ") << message << "\n";
    }
    m_err.flush();
}

void ConsoleProgress::reportWarning(const QString& message)
{
    if (m_mode == OutputMode::Json) {
        m_err << "{\"type\":\"warning\",\"message\":\"" << jsonEscape(message) << "\"}\n";
    } else {
        m_err << QCoreApplication::translate("CLI", "Warning: ") << message << "\n";
    }
    m_err.flush();
}

// =============================================================================
// Utility Functions
// =============================================================================

QString ConsoleProgress::formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024 * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString ConsoleProgress::formatDuration(qint64 ms)
{
    if (ms < 1000) {
        return QStringLiteral("%1 ms").arg(ms);
    }
    if (ms < 60 * 1000) {
        return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', 1);
    }
    qint64 minutes = ms / (60 * 1000);
    qint64 seconds = (ms % (60 * 1000)) / 1000;
    return QStringLiteral("%1m %2s").arg(minutes).arg(seconds);
}

QString ConsoleProgress::shortName(const QString& path)
{
    return QFileInfo(path).fileName();
}

QString ConsoleProgress::statusString(BatchOps::FileStatus status)
{
    switch (status) {
        case BatchOps::FileStatus::Success: return QStringLiteral("success");
        case BatchOps::FileStatus::Skipped: return QStringLiteral("skipped");
        case BatchOps::FileStatus::Error:   return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QString ConsoleProgress::jsonEscape(const QString& str)
{
    QString result;
    result.reserve(str.size() + 10);
    
    for (const QChar& c : str) {
        switch (c.unicode()) {
            case '"':  result += QStringLiteral("\\\""); break;
            case '\\': result += QStringLiteral("\\\\"); break;
            case '\n': result += QStringLiteral("\\n"); break;
            case '\r': result += QStringLiteral("\\r"); break;
            case '\t': result += QStringLiteral("\\t"); break;
            default:
                if (c.unicode() < 32) {
                    // Control character - escape as \uXXXX
                    // Cast to uint for cross-platform QString::arg() compatibility
                    result += QStringLiteral("\\u%1").arg(static_cast<uint>(c.unicode()), 4, 16, QLatin1Char('0'));
                } else {
                    result += c;
                }
                break;
        }
    }
    
    return result;
}

} // namespace Cli
