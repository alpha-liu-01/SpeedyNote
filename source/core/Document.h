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
#include <QFileInfo>
#include <QPixmap>
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
    // PDF Reference Management (Task 1.2.4)
    // =========================================================================
    
    /**
     * @brief Check if this document has a PDF reference (path set).
     * @return True if pdfPath is set, even if PDF is not currently loaded.
     */
    bool hasPdfReference() const { return !m_pdfPath.isEmpty(); }
    
    /**
     * @brief Check if the PDF is currently loaded and valid.
     * @return True if pdfProvider is valid and the PDF is loaded.
     */
    bool isPdfLoaded() const { return m_pdfProvider && m_pdfProvider->isValid(); }
    
    /**
     * @brief Check if the PDF file exists at the referenced path.
     * @return True if the file exists on disk.
     */
    bool pdfFileExists() const;
    
    /**
     * @brief Get the path to the referenced PDF file.
     * @return The PDF path, or empty string if no PDF is referenced.
     */
    QString pdfPath() const { return m_pdfPath; }
    
    /**
     * @brief Get the PDF provider for advanced operations.
     * @return Pointer to the provider, or nullptr if not loaded.
     * 
     * Use this for accessing text boxes, links, outline, etc.
     */
    const PdfProvider* pdfProvider() const { return m_pdfProvider.get(); }
    
    /**
     * @brief Load a PDF file.
     * @param path Path to the PDF file.
     * @return True if loaded successfully.
     * 
     * If a PDF is already loaded, it will be unloaded first.
     * Sets m_pdfPath even if loading fails (for relink functionality).
     */
    bool loadPdf(const QString& path);
    
    /**
     * @brief Relink to a different PDF file.
     * @param newPath Path to the new PDF file.
     * @return True if the new PDF was loaded successfully.
     * 
     * Use this when the user locates a moved/renamed PDF.
     * Marks the document as modified if successful.
     */
    bool relinkPdf(const QString& newPath);
    
    /**
     * @brief Unload the PDF and clear the reference.
     * 
     * Releases PDF resources but keeps the path for potential relink.
     */
    void unloadPdf();
    
    /**
     * @brief Clear the PDF reference entirely.
     * 
     * Unloads PDF and clears the path. Document becomes a blank notebook.
     */
    void clearPdfReference();
    
    /**
     * @brief Render a PDF page to an image.
     * @param pageIndex 0-based page index.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered image, or null image if not available.
     */
    QImage renderPdfPageToImage(int pageIndex, qreal dpi = 96.0) const;
    
    /**
     * @brief Render a PDF page to a pixmap.
     * @param pageIndex 0-based page index.
     * @param dpi Rendering DPI (default 96 for screen).
     * @return Rendered pixmap, or null pixmap if not available.
     */
    QPixmap renderPdfPageToPixmap(int pageIndex, qreal dpi = 96.0) const;
    
    /**
     * @brief Get the number of pages in the PDF.
     * @return Page count, or 0 if no PDF is loaded.
     */
    int pdfPageCount() const;
    
    /**
     * @brief Get the size of a PDF page.
     * @param pageIndex 0-based page index.
     * @return Page size in PDF points (72 dpi), or invalid size if not available.
     */
    QSizeF pdfPageSize(int pageIndex) const;
    
    /**
     * @brief Get the PDF title metadata.
     * @return Title string, or empty if not available.
     */
    QString pdfTitle() const;
    
    /**
     * @brief Get the PDF author metadata.
     * @return Author string, or empty if not available.
     */
    QString pdfAuthor() const;
    
    /**
     * @brief Check if the PDF has an outline (table of contents).
     * @return True if outline is available.
     */
    bool pdfHasOutline() const;
    
    /**
     * @brief Get the PDF outline.
     * @return Vector of outline items, or empty if not available.
     */
    QVector<PdfOutlineItem> pdfOutline() const;
    
    // =========================================================================
    // Page Management (Task 1.2.5)
    // =========================================================================
    
    /**
     * @brief Get the number of pages in the document.
     * @return Page count (always >= 1 after ensureMinimumPages).
     */
    int pageCount() const { return static_cast<int>(m_pages.size()); }
    
    /**
     * @brief Get a page by index.
     * @param index 0-based page index.
     * @return Pointer to the page, or nullptr if index is out of range.
     */
    Page* page(int index);
    
    /**
     * @brief Get a page by index (const version).
     * @param index 0-based page index.
     * @return Const pointer to the page, or nullptr if index is out of range.
     */
    const Page* page(int index) const;
    
    /**
     * @brief Add a new page at the end of the document.
     * @return Pointer to the newly created page.
     * 
     * The page inherits default settings from the document.
     * Marks the document as modified.
     */
    Page* addPage();
    
    /**
     * @brief Insert a new page at a specific position.
     * @param index Position to insert (0 = beginning).
     * @return Pointer to the newly created page, or nullptr if index invalid.
     * 
     * Existing pages at and after the index are shifted.
     * Marks the document as modified.
     */
    Page* insertPage(int index);
    
    /**
     * @brief Add a page configured for a specific PDF page.
     * @param pdfPageIndex 0-based PDF page index.
     * @return Pointer to the newly created page.
     * 
     * Sets the page's background to BackgroundType::PDF and stores the PDF page index.
     * Page size is set to match the PDF page size (scaled from 72 dpi to 96 dpi).
     * Marks the document as modified.
     */
    Page* addPageForPdf(int pdfPageIndex);
    
    /**
     * @brief Remove a page from the document.
     * @param index 0-based page index.
     * @return True if removed, false if index invalid or only one page remains.
     * 
     * Cannot remove the last page (use ensureMinimumPages constraint).
     * Marks the document as modified.
     */
    bool removePage(int index);
    
    /**
     * @brief Move a page from one position to another.
     * @param from Source index.
     * @param to Destination index.
     * @return True if moved, false if indices invalid.
     * 
     * Marks the document as modified.
     */
    bool movePage(int from, int to);
    
    /**
     * @brief Get the single page in edgeless mode.
     * @return Pointer to the edgeless page, or nullptr if not in edgeless mode.
     * 
     * In edgeless mode, there is exactly one unbounded page.
     */
    Page* edgelessPage();
    
    /**
     * @brief Get the single page in edgeless mode (const version).
     */
    const Page* edgelessPage() const;
    
    /**
     * @brief Ensure at least one page exists.
     * 
     * If the document has no pages, creates one with default settings.
     * Called automatically by factory methods.
     */
    void ensureMinimumPages();
    
    /**
     * @brief Find the document page index for a given PDF page.
     * @param pdfPageIndex 0-based PDF page index.
     * @return Document page index, or -1 if not found.
     * 
     * Searches pages with BackgroundType::PDF for matching pdfPageNumber.
     */
    int findPageByPdfPage(int pdfPageIndex) const;
    
    /**
     * @brief Create pages for all PDF pages.
     * 
     * Creates one document page per PDF page, each configured with
     * BackgroundType::PDF and the appropriate page size.
     * Clears existing pages first.
     */
    void createPagesForPdf();
    
    // =========================================================================
    // Bookmarks (Task 1.2.6)
    // =========================================================================
    
    /**
     * @brief Bookmark info structure for quick access.
     */
    struct Bookmark {
        int pageIndex;      ///< 0-based page index
        QString label;      ///< Bookmark label/title
    };
    
    /**
     * @brief Get all bookmarks in the document.
     * @return Vector of bookmarks sorted by page index.
     */
    QVector<Bookmark> getBookmarks() const;
    
    /**
     * @brief Set a bookmark on a page.
     * @param pageIndex 0-based page index.
     * @param label Bookmark label (optional, defaults to "Bookmark N").
     * 
     * If the page already has a bookmark, updates the label.
     * Marks the document as modified.
     */
    void setBookmark(int pageIndex, const QString& label = QString());
    
    /**
     * @brief Remove a bookmark from a page.
     * @param pageIndex 0-based page index.
     * 
     * No-op if page doesn't have a bookmark.
     * Marks the document as modified if bookmark was removed.
     */
    void removeBookmark(int pageIndex);
    
    /**
     * @brief Check if a page has a bookmark.
     * @param pageIndex 0-based page index.
     * @return True if page has a bookmark.
     */
    bool hasBookmark(int pageIndex) const;
    
    /**
     * @brief Get the bookmark label for a page.
     * @param pageIndex 0-based page index.
     * @return Bookmark label, or empty string if no bookmark.
     */
    QString bookmarkLabel(int pageIndex) const;
    
    /**
     * @brief Find the next bookmarked page after a given page.
     * @param fromPage 0-based page index to search from (exclusive).
     * @return Page index of next bookmark, or -1 if none found.
     * 
     * Wraps around to the beginning if no bookmark found after fromPage.
     */
    int nextBookmark(int fromPage) const;
    
    /**
     * @brief Find the previous bookmarked page before a given page.
     * @param fromPage 0-based page index to search from (exclusive).
     * @return Page index of previous bookmark, or -1 if none found.
     * 
     * Wraps around to the end if no bookmark found before fromPage.
     */
    int prevBookmark(int fromPage) const;
    
    /**
     * @brief Toggle bookmark on a page.
     * @param pageIndex 0-based page index.
     * @param label Label to use if adding bookmark.
     * @return True if bookmark was added, false if removed.
     */
    bool toggleBookmark(int pageIndex, const QString& label = QString());
    
    /**
     * @brief Get the total number of bookmarks.
     */
    int bookmarkCount() const;
    
    // =========================================================================
    // Serialization (Task 1.2.7)
    // =========================================================================
    
    /**
     * @brief Serialize document metadata to JSON.
     * @return JSON object containing document metadata.
     * 
     * Does NOT include page content (strokes, objects).
     * Use toFullJson() for complete serialization.
     */
    QJsonObject toJson() const;
    
    /**
     * @brief Create a document from metadata JSON.
     * @param obj JSON object containing document metadata.
     * @return New document with metadata loaded, or nullptr on error.
     * 
     * Pages are created but content is NOT loaded - call loadPagesFromJson() 
     * or read page data separately.
     */
    static std::unique_ptr<Document> fromJson(const QJsonObject& obj);
    
    /**
     * @brief Serialize complete document to JSON.
     * @return JSON object containing document metadata AND all page content.
     * 
     * Warning: Can be very large for documents with many strokes.
     */
    QJsonObject toFullJson() const;
    
    /**
     * @brief Create a complete document from full JSON.
     * @param obj JSON object containing document metadata and pages.
     * @return New document with all data loaded, or nullptr on error.
     */
    static std::unique_ptr<Document> fromFullJson(const QJsonObject& obj);
    
    /**
     * @brief Load page content from a pages JSON array.
     * @param pagesArray JSON array of page objects.
     * @return Number of pages successfully loaded.
     * 
     * Clears existing pages and creates new ones from JSON.
     * Use after fromJson() to load page content.
     */
    int loadPagesFromJson(const QJsonArray& pagesArray);
    
    /**
     * @brief Get pages as JSON array.
     * @return JSON array of page objects.
     */
    QJsonArray pagesToJson() const;
    
    /**
     * @brief Get default background settings as JSON.
     * @return JSON object with background settings.
     */
    QJsonObject defaultBackgroundToJson() const;
    
    /**
     * @brief Load default background settings from JSON.
     * @param obj JSON object with background settings.
     */
    void loadDefaultBackgroundFromJson(const QJsonObject& obj);
    
    /**
     * @brief Convert BackgroundType enum to string.
     */
    static QString backgroundTypeToString(Page::BackgroundType type);
    
    /**
     * @brief Convert string to BackgroundType enum.
     */
    static Page::BackgroundType stringToBackgroundType(const QString& str);
    
    /**
     * @brief Convert Mode enum to string.
     */
    static QString modeToString(Mode m);
    
    /**
     * @brief Convert string to Mode enum.
     */
    static Mode stringToMode(const QString& str);
    
private:
    // ===== PDF Reference (Task 1.2.4) =====
    QString m_pdfPath;                              ///< Path to external PDF file
    std::unique_ptr<PdfProvider> m_pdfProvider;    ///< Loaded PDF (may be null)
    
    // ===== Pages (Task 1.2.5) =====
    std::vector<std::unique_ptr<Page>> m_pages;    ///< All pages in the document
    
    /**
     * @brief Create a new page with document defaults applied.
     * @return Unique pointer to the new page.
     */
    std::unique_ptr<Page> createDefaultPage();
};
