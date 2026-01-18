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

// ===== File I/O =====

bool MarkdownNote::saveToFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "MarkdownNote::saveToFile: Failed to open file for writing:" << filePath;
        return false;
    }
    
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
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
    in.setEncoding(QStringConverter::Utf8);
    QString fileContent = in.readAll();
    
    // Parse YAML front matter
    // Format:
    // ---
    // title: "Note title"
    // ---
    // 
    // Content here...
    
    if (fileContent.startsWith(QLatin1String("---\n"))) {
        int endMarker = fileContent.indexOf(QLatin1String("\n---\n"), 4);
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

