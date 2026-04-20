// ============================================================================
// MarkdownNote - Implementation
// ============================================================================
// Part of the SpeedyNote markdown notes integration (Phase M.1)
// ============================================================================

#include "MarkdownNote.h"

#include <QCoreApplication>  // For translate()
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#  include <QTextCodec>
#endif

// ===== File I/O =====

bool MarkdownNote::saveToFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "MarkdownNote::saveToFile: Failed to open file for writing:" << filePath;
        return false;
    }
    
    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    
    // Write YAML front matter
    // Escape quotes in title for valid YAML
    QString escapedTitle = title;
    escapedTitle.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    escapedTitle.replace(QLatin1String("\""), QLatin1String("\\\""));
    
    out << "---\n";
    out << "title: \"" << escapedTitle << "\"\n";
    out << "---\n\n";
    
    // Write content
    out << content;
    
    return true;
}

MarkdownNote MarkdownNote::loadFromFile(const QString& filePath)
{
    MarkdownNote note;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return note;  // Return invalid note (empty id)
    }
    
    // Extract ID from filename (filename without .md extension)
    QFileInfo info(filePath);
    note.id = info.baseName();
    
    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif
    QString fileContent = in.readAll();
    
    // Parse YAML front matter
    // Format:
    // ---
    // title: "Note title"
    // ---
    // 
    // Content here...
    
    if (fileContent.startsWith(QLatin1String("---\n"))) {
        qsizetype endMarker = fileContent.indexOf(QLatin1String("\n---\n"), 4);
        if (endMarker != -1) {
            // Extract front matter section
            QString frontMatter = fileContent.mid(4, endMarker - 4);
            
            // Extract content after front matter (skip "\n---\n")
            note.content = fileContent.mid(endMarker + 5).trimmed();
            
            // Parse title from front matter using regex
            // Matches: title: "..." or title: '...' or title: ...
            static QRegularExpression titleRe(
                QStringLiteral("title:\\s*\"(.*)\""),
                QRegularExpression::InvertedGreedinessOption
            );
            QRegularExpressionMatch match = titleRe.match(frontMatter);
            if (match.hasMatch()) {
                note.title = match.captured(1);
                // Unescape quotes
                note.title.replace(QLatin1String("\\\""), QLatin1String("\""));
                note.title.replace(QLatin1String("\\\\"), QLatin1String("\\"));
            }
        } else {
            // Malformed front matter (no closing ---) - treat entire file as content
            note.content = fileContent;
            note.title = QCoreApplication::translate("MarkdownNote", "Untitled");
        }
    } else {
        // No front matter - treat entire file as content
        note.content = fileContent;
        note.title = QCoreApplication::translate("MarkdownNote", "Untitled");
    }
    
    return note;
}

// ----------------------------------------------------------------------------
// loadPreviewFromFile
// ----------------------------------------------------------------------------
//
// Streams a small prefix from disk instead of `readAll()`-ing the whole note,
// parses the optional YAML front matter for the title, and then extracts the
// first non-blank body line (truncated) as the snippet.
//
// This is the hot path for populating the NotesTreePanel when an L2 row is
// expanded.  On low-RAM targets a notebook can easily hold dozens of notes
// per LinkObject, so we:
//   - read at most ~4 KB (enough for any reasonable front matter + a line)
//     even when the caller asks for a smaller maxBodyBytes;
//   - never hold more than a tiny QString per note in memory.
// ----------------------------------------------------------------------------
MarkdownNotePreview MarkdownNote::loadPreviewFromFile(const QString& filePath,
                                                     int maxBodyBytes)
{
    MarkdownNotePreview preview;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return preview;  // invalid (id empty)
    }

    QFileInfo info(filePath);
    preview.id = info.baseName();

    // Read a bounded prefix: room for front matter + enough body to slice a
    // snippet.  4 KB upper bound ensures we never stall on a huge one-liner.
    constexpr qint64 kMaxPrefixBytes = 4096;
    const qint64 readLimit = qMin<qint64>(kMaxPrefixBytes,
                                          qMax(maxBodyBytes, 0) + 2048);
    const QByteArray rawBytes = file.read(readLimit);
    const QString raw = QString::fromUtf8(rawBytes);

    QString body;

    // Parse the same front matter format as loadFromFile().
    if (raw.startsWith(QLatin1String("---\n"))) {
        const qsizetype endMarker = raw.indexOf(QLatin1String("\n---\n"), 4);
        if (endMarker != -1) {
            const QString frontMatter = raw.mid(4, endMarker - 4);
            body = raw.mid(endMarker + 5);

            static QRegularExpression titleRe(
                QStringLiteral("title:\\s*\"(.*)\""),
                QRegularExpression::InvertedGreedinessOption);
            const QRegularExpressionMatch match = titleRe.match(frontMatter);
            if (match.hasMatch()) {
                preview.title = match.captured(1);
                preview.title.replace(QLatin1String("\\\""), QLatin1String("\""));
                preview.title.replace(QLatin1String("\\\\"), QLatin1String("\\"));
            }
        } else {
            body = raw;  // malformed front matter — treat all as body
        }
    } else {
        body = raw;      // no front matter
    }

    // First non-blank line of body, trimmed & length-capped.
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        preview.snippet = trimmed;
        break;
    }
    if (maxBodyBytes > 0 && preview.snippet.size() > maxBodyBytes) {
        preview.snippet.truncate(maxBodyBytes);
        preview.snippet.append(QLatin1String("..."));
    }

    return preview;
}

