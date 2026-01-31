#pragma once

// ============================================================================
// PdfProvider - Abstract interface for PDF operations
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.1)
//
// This abstraction layer enables:
// - Swapping PDF backends (currently MuPDF on all platforms)
// - Future extensibility for other backends if needed
// - Easier testing with mock providers
//
// As of v1.2.1, SpeedyNote uses MuPDF exclusively on all platforms for
// consistent rendering and to eliminate symbol conflicts.
//
// Design: Uses simple data structs instead of passing backend-specific types.
// This ensures any implementation can provide the same interface.
// ============================================================================

#include <QString>
#include <QSizeF>
#include <QRectF>
#include <QPointF>
#include <QImage>
#include <QPixmap>
#include <QVector>
#include <memory>

/**
 * @brief Simple data struct for a text box in a PDF page.
 * 
 * Represents a single word/text fragment with its bounding box.
 * Used for text selection features.
 */
struct PdfTextBox {
    QString text;           ///< The text content
    QRectF boundingBox;     ///< Bounding rectangle in PDF coordinates (points)
    
    /**
     * @brief Get all character bounding boxes (if available).
     * @return List of rectangles for each character.
     * 
     * May be empty if backend doesn't support character-level boxes.
     */
    QVector<QRectF> charBoundingBoxes;
};

/**
 * @brief Types of PDF links.
 */
enum class PdfLinkType {
    None,           ///< Unknown or unsupported link type
    Goto,           ///< Internal link to another page
    Uri,            ///< External URL
    Execute,        ///< Execute action (usually ignored)
    Browse          ///< Browse action
};

/**
 * @brief Simple data struct for a link in a PDF page.
 */
struct PdfLink {
    PdfLinkType type = PdfLinkType::None;
    QRectF area;            ///< Link hotspot area in PDF coordinates (normalized 0-1)
    int targetPage = -1;    ///< Target page number for Goto links (0-based)
    QString uri;            ///< URI for external links
};

/**
 * @brief Simple data struct for an outline (TOC) item.
 * 
 * Used by OutlinePanel to display PDF table of contents and enable
 * navigation to specific locations within the document.
 */
struct PdfOutlineItem {
    QString title;                          ///< Display title
    int targetPage = -1;                    ///< Target page (0-based), -1 if none
    
    /**
     * @brief Target position within the page (normalized 0.0-1.0).
     * 
     * PDF coordinates: (0,0) is bottom-left, (1,1) is top-right.
     * Value of -1 means "not specified" for that axis.
     * If both x and y are -1, scroll to top of page.
     */
    QPointF targetPosition = QPointF(-1, -1);
    
    /**
     * @brief Suggested zoom level for this destination.
     * 
     * Value of -1 means "keep current zoom".
     * Typical values: 1.0 = 100%, 2.0 = 200%, etc.
     */
    qreal targetZoom = -1;
    
    bool isOpen = false;                    ///< Whether item is expanded by default
    QVector<PdfOutlineItem> children;       ///< Child items
};

/**
 * @brief Abstract interface for PDF document operations.
 * 
 * Currently implemented by MuPdfProvider which uses MuPDF
 * for all PDF operations across all platforms.
 */
class PdfProvider {
public:
    virtual ~PdfProvider() = default;
    
    // ===== Document Info =====
    
    /**
     * @brief Check if the PDF was loaded successfully.
     * @return True if a valid PDF is loaded.
     */
    virtual bool isValid() const = 0;
    
    /**
     * @brief Check if the PDF is password-protected and locked.
     * @return True if the PDF requires a password.
     */
    virtual bool isLocked() const = 0;
    
    /**
     * @brief Get the total number of pages.
     * @return Page count, or 0 if invalid.
     */
    virtual int pageCount() const = 0;
    
    /**
     * @brief Get the PDF title from metadata.
     * @return Title string, or empty if not available.
     */
    virtual QString title() const = 0;
    
    /**
     * @brief Get the PDF author from metadata.
     * @return Author string, or empty if not available.
     */
    virtual QString author() const = 0;
    
    /**
     * @brief Get the PDF subject from metadata.
     * @return Subject string, or empty if not available.
     */
    virtual QString subject() const = 0;
    
    /**
     * @brief Get the file path this provider was loaded from.
     * @return The PDF file path.
     */
    virtual QString filePath() const = 0;
    
    // ===== Outline (Table of Contents) =====
    
    /**
     * @brief Get the PDF outline (table of contents).
     * @return List of top-level outline items (may be empty).
     */
    virtual QVector<PdfOutlineItem> outline() const = 0;
    
    /**
     * @brief Check if the PDF has an outline.
     * @return True if outline is available.
     */
    virtual bool hasOutline() const = 0;
    
    // ===== Page Info =====
    
    /**
     * @brief Get the size of a page in points (1/72 inch).
     * @param pageIndex 0-based page index.
     * @return Page size in points, or empty QSizeF if invalid.
     */
    virtual QSizeF pageSize(int pageIndex) const = 0;
    
    // ===== Rendering =====
    
    /**
     * @brief Render a page to a QImage.
     * @param pageIndex 0-based page index.
     * @param dpi Resolution in dots per inch.
     * @return Rendered image, or null QImage on error.
     * 
     * This is the primary rendering method. Implementations should
     * apply appropriate antialiasing and text hinting.
     */
    virtual QImage renderPageToImage(int pageIndex, qreal dpi) const = 0;
    
    /**
     * @brief Render a page to a QPixmap.
     * @param pageIndex 0-based page index.
     * @param dpi Resolution in dots per inch.
     * @return Rendered pixmap, or null QPixmap on error.
     * 
     * Default implementation converts from renderPageToImage().
     * Subclasses may override for better performance.
     */
    virtual QPixmap renderPageToPixmap(int pageIndex, qreal dpi) const {
        QImage img = renderPageToImage(pageIndex, dpi);
        return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
    }
    
    // ===== Text Selection =====
    
    /**
     * @brief Get all text boxes on a page.
     * @param pageIndex 0-based page index.
     * @return List of text boxes with their positions.
     * 
     * Text boxes are typically individual words or text fragments.
     * Coordinates are in PDF points (72 dpi).
     */
    virtual QVector<PdfTextBox> textBoxes(int pageIndex) const = 0;
    
    /**
     * @brief Check if text extraction is supported.
     * @return True if textBoxes() returns useful data.
     */
    virtual bool supportsTextExtraction() const = 0;
    
    // ===== Links =====
    
    /**
     * @brief Get all links on a page.
     * @param pageIndex 0-based page index.
     * @return List of links with their hotspot areas.
     * 
     * Link areas are in normalized coordinates (0.0 to 1.0).
     */
    virtual QVector<PdfLink> links(int pageIndex) const = 0;
    
    /**
     * @brief Check if link extraction is supported.
     * @return True if links() returns useful data.
     */
    virtual bool supportsLinks() const = 0;
    
    // ===== Factory =====
    
    /**
     * @brief Create a PdfProvider for the given file.
     * @param pdfPath Path to the PDF file.
     * @return Provider instance, or nullptr on failure.
     * 
     * This factory method creates the appropriate implementation
     * based on the current platform and available libraries.
     */
    static std::unique_ptr<PdfProvider> create(const QString& pdfPath);
    
    /**
     * @brief Check if PDF support is available on this platform.
     * @return True if a PDF backend is available.
     */
    static bool isAvailable();
};
