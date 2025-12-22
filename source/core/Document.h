#pragma once

// ============================================================================
// Document - The central data structure for a notebook
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3)
//
// Document represents an open notebook and owns:
// - All Pages (paged or edgeless mode)
// - PDF reference (external, not embedded)
// - Metadata (name, author, dates, settings)
// - Bookmarks
//
// Document is a pure data class - rendering and input are handled by
// DocumentViewport (Phase 1.3).
// ============================================================================

#include "Page.h"
#include "../pdf/PdfProvider.h"

#include <QString>
#include <QDateTime>
#include <QColor>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <memory>

/**
 * @brief The central data structure representing an open notebook.
 * 
 * Document is the in-memory representation of a .spn notebook file.
 * It owns all pages, references external PDFs, and manages metadata.
 * 
 * Supports two modes:
 * - Paged: Traditional page-based document (multiple pages)
 * - Edgeless: Single infinite canvas (one unbounded page)
 */
class Document {
public:
    // ===== Document Mode =====
    
    /**
     * @brief The document layout mode.
     */
    enum class Mode {
        Paged,      ///< Traditional page-based document
        Edgeless    ///< Single infinite canvas
    };
    
    // ===== Identity & Metadata =====
    QString id;                         ///< UUID for tracking
    QString name;                       ///< Display name (notebook title)
    QString author;                     ///< Optional author field
    QDateTime created;                  ///< Creation timestamp
    QDateTime lastModified;             ///< Last modification timestamp
    QString formatVersion = "2.0";      ///< File format version
    
    // ===== Document Mode =====
    Mode mode = Mode::Paged;            ///< Layout mode
    
    // ===== Default Page Settings =====
    // These are applied to new pages created in this document
    Page::BackgroundType defaultBackgroundType = Page::BackgroundType::None;
    QColor defaultBackgroundColor = Qt::white;
    QColor defaultGridColor = QColor(200, 200, 200);
    int defaultGridSpacing = 20;
    int defaultLineSpacing = 24;
    QSizeF defaultPageSize = QSizeF(816, 1056);  ///< Default page size (US Letter at 96 DPI)
    
    // ===== State =====
    bool modified = false;              ///< True if document has unsaved changes
    int lastAccessedPage = 0;           ///< Last viewed page index (for restoring position)
    
    // ===== Constructors & Rule of Five =====
    
    /**
     * @brief Default constructor.
     * Creates a new document with a unique ID and current timestamp.
     */
    Document();
    
    /**
     * @brief Destructor.
     */
    ~Document() = default;
    
    // Document is non-copyable due to unique_ptr members
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    
    // Document is movable
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;
    
    // ===== Factory Methods =====
    
    /**
     * @brief Create a new empty document.
     * @param docName Display name for the document.
     * @param docMode Layout mode (Paged or Edgeless).
     * @return New document with one empty page.
     */
    static std::unique_ptr<Document> createNew(const QString& docName, 
                                                Mode docMode = Mode::Paged);
    
    /**
     * @brief Create a new document for annotating a PDF.
     * @param docName Display name for the document.
     * @param pdfPath Path to the PDF file.
     * @return New document configured for PDF annotation, or nullptr on failure.
     * 
     * Creates one page per PDF page, each with BackgroundType::PDF.
     */
    static std::unique_ptr<Document> createForPdf(const QString& docName,
                                                   const QString& pdfPath);
    
    // ===== Utility =====
    
    /**
     * @brief Mark the document as modified.
     */
    void markModified() { modified = true; lastModified = QDateTime::currentDateTime(); }
    
    /**
     * @brief Clear the modified flag.
     * Call after saving.
     */
    void clearModified() { modified = false; }
    
    /**
     * @brief Get a display title for the document.
     * @return The name if set, otherwise "Untitled".
     */
    QString displayName() const { return name.isEmpty() ? QStringLiteral("Untitled") : name; }
    
    /**
     * @brief Check if this is an edgeless (infinite canvas) document.
     */
    bool isEdgeless() const { return mode == Mode::Edgeless; }
    
    /**
     * @brief Check if this is a paged document.
     */
    bool isPaged() const { return mode == Mode::Paged; }
    
    // =========================================================================
    // The following sections will be added in subsequent tasks:
    // - PDF Reference Management (Task 1.2.4)
    // - Page Management (Task 1.2.5)
    // - Bookmarks (Task 1.2.6)
    // - Serialization (Task 1.2.7)
    // =========================================================================
};
