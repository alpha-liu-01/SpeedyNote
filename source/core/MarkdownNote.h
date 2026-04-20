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
 * @brief Lightweight preview (title + first-line snippet) for a markdown note.
 *
 * Returned by MarkdownNote::loadPreviewFromFile().  Contains only what the
 * right-sidebar tree needs to render an L3 row in compact form, without
 * having to read the whole note into memory.
 *
 * An `isValid()` check distinguishes "file exists and was parsed" from
 * "file missing / unreadable".
 */
struct MarkdownNotePreview {
    QString id;       ///< Filename base (matches MarkdownNote::id)
    QString title;    ///< Parsed front-matter title (empty if none)
    QString snippet;  ///< First non-blank line of the body, truncated

    bool isValid() const { return !id.isEmpty(); }
};

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

    /**
     * @brief Load just the title + a short snippet, without reading the full body.
     * @param filePath     Full path to the .md file.
     * @param maxBodyBytes Maximum number of bytes to pull from the body to build
     *                     the snippet (default 256).  Front matter is parsed
     *                     fully but its size is typically negligible.
     *
     * Used by the right-sidebar NotesTreePanel (Phase M.8) to render compact
     * L3 rows for dozens of notes without instantiating a QTextBrowser per
     * note.  Reading is capped so a pathological one-line megabyte note
     * cannot stall the UI.
     *
     * Returns an invalid preview if the file cannot be opened.
     */
    static MarkdownNotePreview loadPreviewFromFile(const QString& filePath,
                                                   int maxBodyBytes = 256);

    // ===== Validation =====
    
    /**
     * @brief Check if this note is valid (has ID).
     * @return true if the note has a non-empty ID.
     */
    bool isValid() const { return !id.isEmpty(); }
};

