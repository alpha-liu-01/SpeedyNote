#pragma once

// ============================================================================
// MuPdfExporter - PDF Export Engine using MuPDF
// ============================================================================
// Part of SpeedyNote PDF Export feature
//
// Uses MuPDF library to create PDF files from SpeedyNote documents.
// Key features:
// - Page grafting: Copy unmodified PDF pages efficiently (no re-rendering)
// - Vector strokes: Convert SpeedyNote strokes to PDF vector paths
// - PDF backgrounds: Embed source PDF pages as XObjects (preserves quality)
// - Image embedding: Export ImageObjects with smart compression
// - Metadata: Preserve original PDF metadata and outline
//
// This is used on all platforms (desktop and Android) for PDF export,
// while viewing continues to use Poppler (desktop) or MuPdfProvider (Android).
// ============================================================================

// Qt includes needed by both real and stub class
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <map>

// Forward declarations (shared by both real and stub)
class Document;

/**
 * @brief Export options for PDF generation.
 */
struct PdfExportOptions {
    QString outputPath;             ///< Path to output PDF file
    QString pageRange;              ///< Page range string (e.g., "1-10, 15, 20-30") or empty for all
    int dpi = 300;                  ///< Target DPI for rasterized content
    bool preserveMetadata = true;   ///< Copy metadata from source PDF
    bool preserveOutline = true;    ///< Copy outline/bookmarks from source PDF
};

/**
 * @brief Result of a PDF export operation.
 */
struct PdfExportResult {
    bool success = false;
    QString errorMessage;
    int pagesExported = 0;
    qint64 fileSizeBytes = 0;
};

#ifdef SPEEDYNOTE_MUPDF_EXPORT

#include <QPolygonF>
#include <QColor>
#include <QImage>
#include <QSizeF>
#include <QRectF>

// Forward declarations for MuPDF implementation
class Page;
class VectorStroke;
class ImageObject;

// Forward declarations for MuPDF types (avoid exposing mupdf headers in public API)
// Note: fz_buffer cannot be forward declared (it's a typedef in MuPDF)
struct fz_context;
struct fz_document;
struct fz_path;
struct pdf_document;
struct pdf_page;
struct pdf_obj;

/**
 * @brief PDF Export Engine using MuPDF.
 * 
 * Exports SpeedyNote documents to PDF format with efficient handling of
 * partially-modified documents. Unmodified pages are copied directly from
 * the source PDF (page grafting), while modified pages are rendered with
 * vector strokes and embedded images.
 * 
 * Thread Safety: This class is NOT thread-safe. Export operations should
 * be run from a single thread, though progress signals are emitted for UI updates.
 * 
 * Usage:
 * @code
 * MuPdfExporter exporter;
 * exporter.setDocument(document);
 * 
 * PdfExportOptions options;
 * options.outputPath = "/path/to/output.pdf";
 * options.dpi = 300;
 * 
 * connect(&exporter, &MuPdfExporter::progressUpdated, this, &MyClass::onProgress);
 * 
 * PdfExportResult result = exporter.exportPdf(options);
 * if (!result.success) {
 *     qWarning() << "Export failed:" << result.errorMessage;
 * }
 * @endcode
 */
class MuPdfExporter : public QObject {
    Q_OBJECT

public:
    explicit MuPdfExporter(QObject* parent = nullptr);
    ~MuPdfExporter() override;
    
    // Disable copy (MuPDF context is not copyable)
    MuPdfExporter(const MuPdfExporter&) = delete;
    MuPdfExporter& operator=(const MuPdfExporter&) = delete;
    
    /**
     * @brief Set the document to export.
     * @param document The SpeedyNote document (must remain valid during export).
     */
    void setDocument(Document* document);
    
    /**
     * @brief Export the document to PDF.
     * @param options Export options (output path, page range, DPI, etc.)
     * @return Result containing success status and any error message.
     * 
     * This is a blocking operation. Connect to progressUpdated() for UI updates.
     * The document must be saved before export (no unsaved changes allowed).
     */
    PdfExportResult exportPdf(const PdfExportOptions& options);
    
    /**
     * @brief Cancel an ongoing export operation.
     * 
     * Safe to call from any thread. The export will stop at the next page boundary.
     */
    void cancel();
    
    /**
     * @brief Check if an export is currently in progress.
     */
    bool isExporting() const { return m_isExporting; }
    
    /**
     * @brief Parse a page range string into a list of page indices.
     * @param rangeString Range like "1-10, 15, 20-30" (1-based for user display)
     * @param totalPages Total number of pages in document
     * @return List of 0-based page indices, or empty if invalid
     * 
     * Examples:
     * - "1-5" → {0, 1, 2, 3, 4}
     * - "1, 3, 5" → {0, 2, 4}
     * - "1-3, 7-9" → {0, 1, 2, 6, 7, 8}
     * - "" or "all" → all pages
     */
    static QVector<int> parsePageRange(const QString& rangeString, int totalPages);
    
    /**
     * @brief Compress an image for PDF embedding with optional downsampling.
     * @param image Source image
     * @param hasAlpha Whether image has transparency
     * @param displaySizePt Display size in PDF points (72 DPI)
     * @param targetDpi Target resolution for downsampling (e.g., 300)
     * @return Compressed image data (JPEG or PNG)
     * 
     * If the image resolution exceeds what's needed for targetDpi at the given
     * display size, it will be downsampled to reduce file size.
     * 
     * Example: A 2000x1000 image displayed at 100x50 pt (1.39" x 0.69")
     * has effective DPI of 1440. If targetDpi is 300, it will be downsampled
     * to ~417x208 pixels.
     */
    static QByteArray compressImage(const QImage& image, bool hasAlpha, 
                                    const QSizeF& displaySizePt, int targetDpi);

signals:
    /**
     * @brief Emitted when export progress changes.
     * @param current Current page being processed (1-based)
     * @param total Total pages to export
     */
    void progressUpdated(int current, int total);
    
    /**
     * @brief Emitted when export completes successfully.
     */
    void exportComplete();
    
    /**
     * @brief Emitted when export is cancelled.
     */
    void exportCancelled();
    
    /**
     * @brief Emitted when export fails with an error.
     * @param errorMessage Description of the error
     */
    void exportFailed(const QString& errorMessage);

private:
    // ===== Initialization =====
    
    /**
     * @brief Initialize MuPDF context and create output document.
     * @return true if successful
     */
    bool initContext();
    
    /**
     * @brief Clean up MuPDF resources.
     */
    void cleanup();
    
    /**
     * @brief Open source PDF for page grafting.
     * @return true if source PDF opened (or no source PDF needed)
     */
    bool openSourcePdf();
    
    // ===== Page Processing =====
    
    /**
     * @brief Check if a page has been modified and needs rendering.
     * @param pageIndex 0-based page index
     * @return true if page needs rendering, false if can be grafted
     */
    bool isPageModified(int pageIndex) const;
    
    /**
     * @brief Graft an unmodified page from source PDF.
     * @param pageIndex 0-based page index
     * @return true if successful
     */
    bool graftPage(int pageIndex);
    
    /**
     * @brief Render a modified page (strokes, images, background).
     * @param pageIndex 0-based page index
     * @return true if successful
     */
    bool renderModifiedPage(int pageIndex);
    
    /**
     * @brief Render a page without PDF background (blank notebook page).
     * @param pageIndex 0-based page index
     * @return true if successful
     */
    bool renderBlankPage(int pageIndex);
    
    // ===== Vector Stroke Conversion =====
    
    /**
     * @brief Convert a QPolygonF to a MuPDF path with coordinate transformation.
     * @param polygon The polygon representing a stroke outline (SpeedyNote coords)
     * @param pageHeightSn Page height in SpeedyNote units (96 DPI), for Y-axis flip
     * @return MuPDF path object (caller must free with fz_drop_path)
     * 
     * Coordinate transformation applied:
     * - SpeedyNote uses 96 DPI with top-left origin
     * - PDF uses 72 DPI (points) with bottom-left origin
     * - X: x_pdf = x_sn * (72/96)
     * - Y: y_pdf = (pageHeight - y_sn) * (72/96)
     */
    fz_path* polygonToPath(const QPolygonF& polygon, qreal pageHeightSn);
    
    // Note: buildStrokesContentStream() is implemented as a static helper
    // function in MuPdfExporter.cpp to avoid exposing fz_buffer in the header
    // (fz_buffer is a typedef in MuPDF and cannot be forward declared)
    
    // ===== PDF Background =====
    
    /**
     * @brief Import a source PDF page as an XObject.
     * @param sourcePageIndex 0-based index in source PDF
     * @return XObject reference, or nullptr on failure
     */
    pdf_obj* importPageAsXObject(int sourcePageIndex);
    
    // ===== Image Handling =====
    
    // Note: addImageToPage is implemented as a static helper function in MuPdfExporter.cpp
    // to avoid exposing MuPDF types (fz_buffer, pdf_obj) in the header.
    
    // ===== Metadata and Outline =====
    
    /**
     * @brief Copy metadata from source PDF and add SpeedyNote attribution.
     * @return true if successful
     */
    bool writeMetadata();
    
    /**
     * @brief Copy and adjust outline/bookmarks for page range.
     * @param exportedPages List of exported page indices (for remapping)
     * @return true if successful
     */
    bool writeOutline(const QVector<int>& exportedPages);
    // Note: writeOutlineRecursive is a static helper in MuPdfExporter.cpp
    // (uses fz_outline which cannot be forward-declared)
    
    // ===== Finalization =====
    
    /**
     * @brief Write the PDF to disk.
     * @param outputPath Path to output file
     * @return true if successful
     */
    bool saveDocument(const QString& outputPath);

private:
    // Document reference
    Document* m_document = nullptr;
    
    // MuPDF contexts (mutable because MuPDF modifies internal state on "read" ops)
    mutable fz_context* m_ctx = nullptr;
    pdf_document* m_outputDoc = nullptr;
    fz_document* m_sourceDoc = nullptr;
    pdf_document* m_sourcePdf = nullptr;
    
    // Graft map for efficient multi-page grafting
    // When grafting multiple pages, shared resources (fonts, images) are only
    // copied once when using a graft map. This is created in openSourcePdf()
    // and dropped in cleanup().
    struct pdf_graft_map* m_graftMap = nullptr;
    
    // Export state
    bool m_isExporting = false;
    std::atomic<bool> m_cancelled{false};  ///< Thread-safe cancellation flag
    PdfExportOptions m_options;
};

#else // SPEEDYNOTE_MUPDF_EXPORT not defined

/**
 * @brief Stub class when MuPDF is not available.
 * 
 * All export operations will fail with an appropriate error message.
 */
class MuPdfExporter : public QObject {
    Q_OBJECT
public:
    explicit MuPdfExporter(QObject* parent = nullptr) : QObject(parent) {}
    
    void setDocument(Document*) {}
    
    PdfExportResult exportPdf(const PdfExportOptions&) { 
        PdfExportResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("PDF export requires MuPDF. Install libmupdf-dev and rebuild.");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    void cancel() {}
    bool isExporting() const { return false; }
    
    static QVector<int> parsePageRange(const QString&, int) { return {}; }

signals:
    void progressUpdated(int current, int total);
    void exportComplete();
    void exportCancelled();
    void exportFailed(const QString& errorMessage);
};

#endif // SPEEDYNOTE_MUPDF_EXPORT

