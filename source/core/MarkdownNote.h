#pragma once

// ============================================================================
// MarkdownNote - A markdown note linked to a LinkObject slot
// ============================================================================
// Part of the SpeedyNote markdown notes integration (Phase M.1)
// 
// MarkdownNote stores markdown content in a separate .md file with YAML 
// front matter for the title. The note file does NOT store back-references 
// to LinkObject - the connection is maintained via LinkSlot.markdownNoteId.
//
// File format example:
//   ---
//   title: "Note title here"
//   ---
//   
//   Markdown content here...
// ============================================================================

#include <QString>

/**
 * @brief A markdown note linked to a LinkObject slot.
 * 
 * Notes are stored as separate .md files in the document's assets/notes/ directory.
 * Each file contains YAML front matter for metadata, followed by markdown content.
 * 
 * The note ID matches the filename (without .md extension) and is a UUID.
 * The note does not store any reference back to its LinkObject - that relationship
 * is maintained unidirectionally via LinkSlot.markdownNoteId.
 */
class MarkdownNote {
public:
    // ===== Data =====
    
    QString id;       ///< UUID (matches filename without .md extension)
    QString title;    ///< Note title (stored in YAML front matter)
    QString content;  ///< Markdown content (after front matter)
    
    // ===== File I/O =====
    
    /**
     * @brief Save note to file with YAML front matter.
     * @param filePath Full path to .md file.
     * @return true if successful, false on error.
     * 
     * Output format:
     * ---
     * title: "Escaped title"
     * ---
     * 
     * <content>
     */
    bool saveToFile(const QString& filePath) const;
    
    /**
     * @brief Load note from .md file with YAML front matter.
     * @param filePath Full path to .md file.
     * @return Loaded note. Check isValid() to verify success.
     * 
     * If the file has no front matter, content is loaded as-is with
     * title defaulting to "Untitled".
     */
    static MarkdownNote loadFromFile(const QString& filePath);
    
    // ===== Validation =====
    
    /**
     * @brief Check if this note is valid (has ID).
     * @return true if the note has a non-empty ID.
     */
    bool isValid() const { return !id.isEmpty(); }
};

