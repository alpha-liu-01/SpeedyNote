// ============================================================================
// MuPdfExporter - PDF Export Engine using MuPDF
// ============================================================================

#include "MuPdfExporter.h"

#ifdef SPEEDYNOTE_MUPDF_EXPORT

#include "../core/Document.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"
#include "../objects/ImageObject.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>

#include <algorithm> // for std::sort
#include <cmath>     // for cosf, sinf, M_PI

// Forward declarations for static helper functions defined later in this file
static fz_buffer* buildStrokesContentStream(fz_context* ctx, const Page* page);
static int getSourcePageRotation(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
static fz_rect getSourcePageBBox(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
static bool addImageToPage(fz_context* ctx, pdf_document* outputDoc,
                           const ImageObject* img, fz_buffer* contentBuf,
                           pdf_obj* resources, int imageIndex, float pageHeightPt,
                           const PdfExportOptions& options);

/**
 * @brief Scale factor from SpeedyNote units (96 DPI) to PDF points (72 DPI).
 * 
 * PDF points are 1/72 inch, SpeedyNote uses 96 pixels per inch.
 * Scale = 72/96 = 0.75
 */
static constexpr float SN_TO_PDF_SCALE = 72.0f / 96.0f;

// ============================================================================
// Construction / Destruction
// ============================================================================

MuPdfExporter::MuPdfExporter(QObject* parent)
    : QObject(parent)
{
}

MuPdfExporter::~MuPdfExporter()
{
    cleanup();
}

// ============================================================================
// Public API
// ============================================================================

void MuPdfExporter::setDocument(Document* document)
{
    m_document = document;
}

PdfExportResult MuPdfExporter::exportPdf(const PdfExportOptions& options)
{
    PdfExportResult result;
    
    // Validate inputs
    if (!m_document) {
        result.errorMessage = tr("No document set for export");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    if (options.outputPath.isEmpty()) {
        result.errorMessage = tr("No output path specified");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    // Parse page range
    QVector<int> pageIndices = parsePageRange(options.pageRange, m_document->pageCount());
    if (pageIndices.isEmpty()) {
        result.errorMessage = tr("Invalid page range");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    m_options = options;
    m_isExporting = true;
    m_cancelled.store(false);
    
    qDebug() << "[MuPdfExporter] Starting export:"
             << pageIndices.size() << "pages at" << options.dpi << "DPI"
             << "to" << options.outputPath;
    
    // Initialize MuPDF
    if (!initContext()) {
        result.errorMessage = tr("Failed to initialize PDF engine");
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Open source PDF if document has one
    if (!openSourcePdf()) {
        result.errorMessage = tr("Failed to open source PDF");
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Process each page
    int total = pageIndices.size();
    for (int i = 0; i < total; ++i) {
        if (m_cancelled.load()) {
            result.errorMessage = tr("Export cancelled");
            cleanup();
            emit exportCancelled();
            m_isExporting = false;
            return result;
        }
        
        int pageIndex = pageIndices[i];
        emit progressUpdated(i + 1, total);
        
        bool pageSuccess = false;
        
        // Determine how to handle this page
        Page* currentPage = m_document->page(pageIndex);
        if (!currentPage) {
            qWarning() << "[MuPdfExporter] Failed to get page" << pageIndex;
            pageSuccess = false;
        } else if (isPageModified(pageIndex)) {
            // Page has annotations - need to render
            if (currentPage->pdfPageNumber >= 0 && m_sourcePdf) {
                // Modified page with PDF background
                pageSuccess = renderModifiedPage(pageIndex);
            } else {
                // No PDF background (blank notebook page)
                pageSuccess = renderBlankPage(pageIndex);
            }
        } else if (m_sourcePdf && currentPage->pdfPageNumber >= 0) {
            // Unmodified page with PDF - graft directly
            pageSuccess = graftPage(pageIndex);
        } else {
            // Unmodified blank page - still need to render
            pageSuccess = renderBlankPage(pageIndex);
        }
        
        if (!pageSuccess) {
            result.errorMessage = tr("Failed to export page %1").arg(pageIndex + 1);
            cleanup();
            emit exportFailed(result.errorMessage);
            m_isExporting = false;
            return result;
        }
        
        result.pagesExported++;
    }
    
    // Write metadata
    if (options.preserveMetadata && !writeMetadata()) {
        qWarning() << "[MuPdfExporter] Failed to write metadata (non-fatal)";
    }
    
    // Write outline
    if (options.preserveOutline && !writeOutline(pageIndices)) {
        qWarning() << "[MuPdfExporter] Failed to write outline (non-fatal)";
    }
    
    // Save to disk
    if (!saveDocument(options.outputPath)) {
        result.errorMessage = tr("Failed to save PDF file");
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Get file size
    QFileInfo fileInfo(options.outputPath);
    result.fileSizeBytes = fileInfo.size();
    
    // Cleanup and signal success
    cleanup();
    result.success = true;
    m_isExporting = false;
    
    qDebug() << "[MuPdfExporter] Export complete:"
             << result.pagesExported << "pages,"
             << (result.fileSizeBytes / 1024) << "KB";
    
    emit exportComplete();
    return result;
}

void MuPdfExporter::cancel()
{
    m_cancelled.store(true);
}

QVector<int> MuPdfExporter::parsePageRange(const QString& rangeString, int totalPages)
{
    QVector<int> result;
    
    if (totalPages <= 0) {
        return result;
    }
    
    QString range = rangeString.trimmed().toLower();
    
    // Empty or "all" means all pages
    if (range.isEmpty() || range == "all") {
        result.reserve(totalPages);
        for (int i = 0; i < totalPages; ++i) {
            result.append(i);
        }
        return result;
    }
    
    // Parse comma-separated parts
    QStringList parts = range.split(',', Qt::SkipEmptyParts);
    QSet<int> seen; // Avoid duplicates
    
    static QRegularExpression rangePattern("^\\s*(\\d+)\\s*-\\s*(\\d+)\\s*$");
    static QRegularExpression singlePattern("^\\s*(\\d+)\\s*$");
    
    for (const QString& part : parts) {
        // Try range pattern (e.g., "1-10")
        QRegularExpressionMatch rangeMatch = rangePattern.match(part);
        if (rangeMatch.hasMatch()) {
            int start = rangeMatch.captured(1).toInt();
            int end = rangeMatch.captured(2).toInt();
            
            // Convert to 0-based and clamp
            start = qMax(1, qMin(start, totalPages)) - 1;
            end = qMax(1, qMin(end, totalPages)) - 1;
            
            // Handle reversed ranges
            if (start > end) {
                qSwap(start, end);
            }
            
            for (int i = start; i <= end; ++i) {
                if (!seen.contains(i)) {
                    result.append(i);
                    seen.insert(i);
                }
            }
            continue;
        }
        
        // Try single page pattern (e.g., "15")
        QRegularExpressionMatch singleMatch = singlePattern.match(part);
        if (singleMatch.hasMatch()) {
            int page = singleMatch.captured(1).toInt();
            
            // Convert to 0-based and clamp
            page = qMax(1, qMin(page, totalPages)) - 1;
            
            if (!seen.contains(page)) {
                result.append(page);
                seen.insert(page);
            }
            continue;
        }
        
        // Invalid part - skip with warning
        qWarning() << "[MuPdfExporter] Invalid page range part:" << part;
    }
    
    // Sort the result for consistent ordering
    std::sort(result.begin(), result.end());
    
    return result;
}

// ============================================================================
// Initialization
// ============================================================================

bool MuPdfExporter::initContext()
{
    // Create MuPDF context
    m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create MuPDF context";
        return false;
    }
    
    // Register document handlers
    fz_try(m_ctx) {
        fz_register_document_handlers(m_ctx);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to register handlers:" << fz_caught_message(m_ctx);
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    
    // Create new PDF document for output
    fz_try(m_ctx) {
        m_outputDoc = pdf_create_document(m_ctx);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create output PDF:" << fz_caught_message(m_ctx);
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    
    qDebug() << "[MuPdfExporter] Context initialized";
    return true;
}

void MuPdfExporter::cleanup()
{
    // Drop graft map first (it references both documents)
    if (m_graftMap) {
        pdf_drop_graft_map(m_ctx, m_graftMap);
        m_graftMap = nullptr;
    }
    
    if (m_sourcePdf) {
        // Note: m_sourcePdf and m_sourceDoc point to the same document
        // Only drop once via the fz_document handle
    }
    
    if (m_sourceDoc) {
        fz_drop_document(m_ctx, m_sourceDoc);
        m_sourceDoc = nullptr;
        m_sourcePdf = nullptr;
    }
    
    if (m_outputDoc) {
        pdf_drop_document(m_ctx, m_outputDoc);
        m_outputDoc = nullptr;
    }
    
    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

bool MuPdfExporter::openSourcePdf()
{
    if (!m_document) return true;
    
    QString pdfPath = m_document->pdfPath();
    if (pdfPath.isEmpty()) {
        // No source PDF - this is fine for blank notebooks
        qDebug() << "[MuPdfExporter] No source PDF (blank document)";
        return true;
    }
    
    if (!QFile::exists(pdfPath)) {
        qWarning() << "[MuPdfExporter] Source PDF not found:" << pdfPath;
        return false;
    }
    
    QByteArray pathUtf8 = pdfPath.toUtf8();
    
    fz_try(m_ctx) {
        m_sourceDoc = fz_open_document(m_ctx, pathUtf8.constData());
        
        // Verify it's a PDF (for grafting capabilities)
        m_sourcePdf = pdf_document_from_fz_document(m_ctx, m_sourceDoc);
        if (!m_sourcePdf) {
            qWarning() << "[MuPdfExporter] Source is not a PDF document";
            fz_drop_document(m_ctx, m_sourceDoc);
            m_sourceDoc = nullptr;
            return false;
        }
        
        // Create graft map for efficient multi-page grafting
        // This ensures shared resources (fonts, images) are only copied once
        m_graftMap = pdf_new_graft_map(m_ctx, m_outputDoc);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to open source PDF:" << fz_caught_message(m_ctx);
        return false;
    }
    
    qDebug() << "[MuPdfExporter] Opened source PDF:" << pdfPath
             << "with" << fz_count_pages(m_ctx, m_sourceDoc) << "pages";
    return true;
}

// ============================================================================
// Page Processing
// ============================================================================

bool MuPdfExporter::isPageModified(int pageIndex) const
{
    if (!m_document) return true;  // Assume modified if no document
    
    // For PDF export, a page is "modified" if it has ANY content that needs
    // to be rendered on top of (or instead of) the source PDF page.
    //
    // Modified pages require full rendering:
    //   - Strokes in any layer
    //   - Inserted objects (images)
    //
    // Unmodified pages with a PDF background can be "grafted" (byte-copied)
    // directly from the source PDF, which is much faster and preserves
    // the original PDF quality perfectly.
    //
    // Note: We intentionally do NOT use Document::isPageDirty() here.
    // That tracks changes since last save, but for export we need to know
    // if the page has ANY annotations, regardless of when they were made.
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;  // Non-existent page is not modified
    
    // Use Page's built-in content check (strokes in any layer + objects)
    return page->hasContent();
}

bool MuPdfExporter::graftPage(int pageIndex)
{
    if (!m_sourcePdf || !m_outputDoc || !m_ctx) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    int pdfPageNum = page->pdfPageNumber;
    if (pdfPageNum < 0) {
        qWarning() << "[MuPdfExporter] Page" << pageIndex << "has no PDF page number";
        return false;
    }
    
    // Validate source page number is in range
    int srcPageCount = pdf_count_pages(m_ctx, m_sourcePdf);
    if (pdfPageNum >= srcPageCount) {
        qWarning() << "[MuPdfExporter] PDF page" << pdfPageNum 
                   << "out of range (source has" << srcPageCount << "pages)";
        return false;
    }
    
    fz_try(m_ctx) {
        // Use pdf_graft_mapped_page() for efficient page copying
        // This is MuPDF's built-in page grafting function that:
        // 1. Copies the page object and all its resources (fonts, images, etc.)
        // 2. Uses the graft map to avoid duplicating shared resources
        // 3. Properly handles page tree insertion
        //
        // Arguments:
        // - m_graftMap: Reuses resources across multiple grafts
        // - -1: Insert at end of output document
        // - m_sourcePdf: Source document
        // - pdfPageNum: Source page index (0-based)
        pdf_graft_mapped_page(m_ctx, m_graftMap, -1, m_sourcePdf, pdfPageNum);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to graft page" << pageIndex
                   << "(PDF page" << pdfPageNum << "):" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

bool MuPdfExporter::renderModifiedPage(int pageIndex)
{
    if (!m_outputDoc || !m_ctx || !m_document) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    // Get the source PDF page number for this SpeedyNote page
    int pdfPageNum = page->pdfPageNumber;
    if (pdfPageNum < 0 || !m_sourcePdf) {
        // No PDF background - use blank page rendering
        return renderBlankPage(pageIndex);
    }
    
    QSizeF pageSize = page->size;
    
    // Convert page size from SpeedyNote units (96 DPI) to PDF points (72 DPI)
    float widthPt = pageSize.width() * SN_TO_PDF_SCALE;
    float heightPt = pageSize.height() * SN_TO_PDF_SCALE;
    
    // Import the source PDF page as an XObject
    pdf_obj* bgXObject = importPageAsXObject(pdfPageNum);
    if (!bgXObject) {
        qWarning() << "[MuPdfExporter] Failed to import PDF page as XObject, falling back to blank";
        return renderBlankPage(pageIndex);
    }
    
    // Build strokes content (may be nullptr if no strokes)
    fz_buffer* strokesContent = buildStrokesContentStream(m_ctx, page);
    
    // Build combined content stream: background XObject + strokes
    fz_buffer* combinedContent = nullptr;
    pdf_obj* resources = nullptr;
    
    // Get source page properties for rotation handling
    int srcRotation = getSourcePageRotation(m_ctx, m_sourcePdf, pdfPageNum);
    fz_rect srcBBox = getSourcePageBBox(m_ctx, m_sourcePdf, pdfPageNum);
    
    fz_try(m_ctx) {
        // Create content buffer
        combinedContent = fz_new_buffer(m_ctx, 1024);
        
        // Save graphics state, draw background XObject, restore
        // The XObject is referenced as /BGForm in the Resources dictionary
        fz_append_string(m_ctx, combinedContent, "q\n");
        
        // Apply transformation matrix for rotated pages
        // The XObject content is stored "unrotated", but the page had a /Rotate entry
        // We need to apply the rotation when drawing the XObject
        //
        // PDF transformation matrix: [a b c d e f]
        // Represents: x' = ax + cy + e, y' = bx + dy + f
        //
        // For rotation around origin:
        //   0°:   [1 0 0 1 0 0]       (identity)
        //   90°:  [0 1 -1 0 w 0]      (rotate + translate)
        //   180°: [-1 0 0 -1 w h]
        //   270°: [0 -1 1 0 0 h]
        //
        // Where w and h are the page dimensions
        
        if (srcRotation != 0) {
            char matrixCmd[128];
            float bboxW = srcBBox.x1 - srcBBox.x0;
            float bboxH = srcBBox.y1 - srcBBox.y0;
            
            switch (srcRotation) {
                case 90:
                    // Rotate 90° CW: [0 1 -1 0 bboxH 0]
                    snprintf(matrixCmd, sizeof(matrixCmd), 
                             "0 1 -1 0 %.4f 0 cm\n", bboxH);
                    break;
                case 180:
                    // Rotate 180°: [-1 0 0 -1 bboxW bboxH]
                    snprintf(matrixCmd, sizeof(matrixCmd), 
                             "-1 0 0 -1 %.4f %.4f cm\n", bboxW, bboxH);
                    break;
                case 270:
                    // Rotate 270° CW (90° CCW): [0 -1 1 0 0 bboxW]
                    snprintf(matrixCmd, sizeof(matrixCmd), 
                             "0 -1 1 0 0 %.4f cm\n", bboxW);
                    break;
                default:
                    matrixCmd[0] = '\0';
                    break;
            }
            
            if (matrixCmd[0] != '\0') {
                fz_append_string(m_ctx, combinedContent, matrixCmd);
                qDebug() << "[MuPdfExporter] Applied rotation" << srcRotation 
                         << "to page" << pageIndex;
            }
        }
        
        // Handle CropBox offset if it doesn't start at origin
        if (srcBBox.x0 != 0 || srcBBox.y0 != 0) {
            char translateCmd[64];
            snprintf(translateCmd, sizeof(translateCmd), 
                     "1 0 0 1 %.4f %.4f cm\n", -srcBBox.x0, -srcBBox.y0);
            fz_append_string(m_ctx, combinedContent, translateCmd);
        }
        
        // Draw the background XObject
        fz_append_string(m_ctx, combinedContent, "/BGForm Do\n");
        fz_append_string(m_ctx, combinedContent, "Q\n");
        
        // Append strokes content if we have any
        if (strokesContent) {
            fz_append_buffer(m_ctx, combinedContent, strokesContent);
        }
        
        // Create Resources dictionary with XObject reference
        resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
        
        // Create XObject subdictionary with background form
        pdf_obj* xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
        pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, "BGForm"), bgXObject);
        pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
        
        // Add images if any
        int imageIndex = 0;
        for (const auto& obj : page->objects) {
            if (obj->type() == QStringLiteral("image")) {
                const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj.get());
                if (imgObj && imgObj->isLoaded()) {
                    addImageToPage(m_ctx, m_outputDoc, imgObj, combinedContent, resources, imageIndex++, heightPt, m_options);
                }
            }
        }
        
        // Create the page with our resources and content
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, 
                                         resources, combinedContent);
        pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
    }
    fz_always(m_ctx) {
        if (combinedContent) {
            fz_drop_buffer(m_ctx, combinedContent);
        }
        if (strokesContent) {
            fz_drop_buffer(m_ctx, strokesContent);
        }
        // Note: resources and bgXObject are owned by the document now, don't drop
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create modified page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    qDebug() << "[MuPdfExporter] Rendered modified page" << pageIndex 
             << "(PDF page" << pdfPageNum << "+ strokes)";
    return true;
}

bool MuPdfExporter::renderBlankPage(int pageIndex)
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    QSizeF pageSize = page->size;
    
    // Convert page size from SpeedyNote units (96 DPI) to PDF points (72 DPI)
    float widthPt = pageSize.width() * SN_TO_PDF_SCALE;
    float heightPt = pageSize.height() * SN_TO_PDF_SCALE;
    
    // Build content stream with strokes (may be nullptr if no strokes)
    fz_buffer* strokesContent = buildStrokesContentStream(m_ctx, page);
    
    // Check if page has images to add
    bool hasImages = !page->objects.empty();
    
    fz_buffer* finalContent = nullptr;
    pdf_obj* resources = nullptr;
    
    fz_try(m_ctx) {
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        
        if (hasImages) {
            // Create resources dictionary for images
            resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
            
            // Create combined content buffer
            finalContent = fz_new_buffer(m_ctx, 1024);
            
            // First, add strokes if any
            if (strokesContent) {
                unsigned char* data;
                size_t len = fz_buffer_storage(m_ctx, strokesContent, &data);
                fz_append_data(m_ctx, finalContent, data, len);
            }
            
            // Add images
            int imageIndex = 0;
            for (const auto& obj : page->objects) {
                if (obj->type() == QStringLiteral("image")) {
                    const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj.get());
                    if (imgObj && imgObj->isLoaded()) {
                        addImageToPage(m_ctx, m_outputDoc, imgObj, finalContent, resources, imageIndex++, heightPt, m_options);
                    }
                }
            }
            
            // Create page with resources and combined content
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, resources, finalContent);
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        } else {
            // No images - simple path with just strokes
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, nullptr, strokesContent);
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        }
        
        // TODO: Phase 6 - Add page background (grid, lines, color)
    }
    fz_always(m_ctx) {
        if (strokesContent) {
            fz_drop_buffer(m_ctx, strokesContent);
        }
        if (finalContent) {
            fz_drop_buffer(m_ctx, finalContent);
        }
        // Note: resources is owned by the page, no need to drop
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

// ============================================================================
// Vector Stroke Conversion (Phase 3)
// ============================================================================

/**
 * @brief Kappa constant for approximating circles with cubic Bezier curves.
 * 
 * A circle can be approximated by 4 cubic Bezier curves. The control points
 * are placed at distance kappa * radius from the arc endpoints.
 * kappa = 4 * (sqrt(2) - 1) / 3 ≈ 0.5522847498
 */
static constexpr float CIRCLE_KAPPA = 0.5522847498f;

/**
 * @brief Transform a point from SpeedyNote coords to PDF coords.
 */
static inline void transformPoint(float& x, float& y, qreal pageHeightSn)
{
    x = x * SN_TO_PDF_SCALE;
    y = static_cast<float>(pageHeightSn - y) * SN_TO_PDF_SCALE;
}

/**
 * @brief Append a filled polygon to the content stream buffer.
 * 
 * Writes PDF path operators: m (moveto), l (lineto), h (closepath), f (fill).
 */
static void appendPolygonToBuffer(fz_context* ctx, fz_buffer* buf, 
                                   const QPolygonF& polygon, qreal pageHeightSn)
{
    if (polygon.isEmpty()) return;
    
    char cmd[64];
    
    // Move to first point
    float x = static_cast<float>(polygon[0].x());
    float y = static_cast<float>(polygon[0].y());
    transformPoint(x, y, pageHeightSn);
    snprintf(cmd, sizeof(cmd), "%.4f %.4f m\n", x, y);
    fz_append_string(ctx, buf, cmd);
    
    // Line to remaining points
    for (int i = 1; i < polygon.size(); ++i) {
        x = static_cast<float>(polygon[i].x());
        y = static_cast<float>(polygon[i].y());
        transformPoint(x, y, pageHeightSn);
        snprintf(cmd, sizeof(cmd), "%.4f %.4f l\n", x, y);
        fz_append_string(ctx, buf, cmd);
    }
    
    // Close and fill (non-zero winding rule for self-intersecting strokes)
    fz_append_string(ctx, buf, "h f\n");
}

/**
 * @brief Append a filled circle to the content stream buffer.
 * 
 * Approximates a circle using 4 cubic Bezier curves (standard PDF technique).
 * Uses operators: m (moveto), c (curveto), h (closepath), f (fill).
 */
static void appendCircleToBuffer(fz_context* ctx, fz_buffer* buf,
                                  const QPointF& center, qreal radius, qreal pageHeightSn)
{
    if (radius <= 0) return;
    
    // Transform center to PDF coords
    float cx = static_cast<float>(center.x());
    float cy = static_cast<float>(center.y());
    transformPoint(cx, cy, pageHeightSn);
    float r = static_cast<float>(radius) * SN_TO_PDF_SCALE;
    
    // Control point offset for Bezier approximation
    float k = r * CIRCLE_KAPPA;
    
    char cmd[128];
    
    // Start at right point of circle (3 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f m\n", cx + r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Top-right quadrant (to 12 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx + r, cy + k,      // control point 1
             cx + k, cy + r,      // control point 2
             cx, cy + r);         // end point
    fz_append_string(ctx, buf, cmd);
    
    // Top-left quadrant (to 9 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx - k, cy + r,
             cx - r, cy + k,
             cx - r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Bottom-left quadrant (to 6 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx - r, cy - k,
             cx - k, cy - r,
             cx, cy - r);
    fz_append_string(ctx, buf, cmd);
    
    // Bottom-right quadrant (back to 3 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx + k, cy - r,
             cx + r, cy - k,
             cx + r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Close and fill
    fz_append_string(ctx, buf, "h f\n");
}

// NOTE: This function is currently unused but reserved for Phase 4 (PDF XObject background).
// When rendering modified pages with PDF backgrounds, we may need fz_path objects
// for use with fz_fill_path() on a drawing device, rather than content stream operators.
fz_path* MuPdfExporter::polygonToPath(const QPolygonF& polygon, qreal pageHeightSn)
{
    if (polygon.isEmpty() || !m_ctx) {
        return nullptr;
    }
    
    fz_path* path = nullptr;
    
    fz_try(m_ctx) {
        path = fz_new_path(m_ctx);
        
        // Transform first point and move to it
        // SpeedyNote: origin at top-left, 96 DPI
        // PDF: origin at bottom-left, 72 DPI (points)
        float x = static_cast<float>(polygon[0].x()) * SN_TO_PDF_SCALE;
        float y = static_cast<float>(pageHeightSn - polygon[0].y()) * SN_TO_PDF_SCALE;
        fz_moveto(m_ctx, path, x, y);
        
        // Line to remaining points with same transformation
        for (int i = 1; i < polygon.size(); ++i) {
            x = static_cast<float>(polygon[i].x()) * SN_TO_PDF_SCALE;
            y = static_cast<float>(pageHeightSn - polygon[i].y()) * SN_TO_PDF_SCALE;
            fz_lineto(m_ctx, path, x, y);
        }
        
        // Close the path (for filled polygons)
        fz_closepath(m_ctx, path);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create path:" << fz_caught_message(m_ctx);
        if (path) {
            fz_drop_path(m_ctx, path);
        }
        return nullptr;
    }
    
    return path;
}

/**
 * @brief Build content stream buffer with all strokes from a page.
 * @param ctx MuPDF context
 * @param page The SpeedyNote page containing strokes
 * @return fz_buffer with PDF content stream commands (caller must drop), or nullptr
 * 
 * This is a static helper function (not a class method) because fz_buffer
 * cannot be forward declared in the header (it's a typedef in MuPDF).
 */
static fz_buffer* buildStrokesContentStream(fz_context* ctx, const Page* page)
{
    if (!page || !ctx) {
        return nullptr;
    }
    
    // Check if page has any strokes
    bool hasStrokes = false;
    for (const auto& layer : page->vectorLayers) {
        if (!layer->strokes().isEmpty()) {
            hasStrokes = true;
            break;
        }
    }
    
    if (!hasStrokes) {
        return nullptr;  // No strokes to render
    }
    
    fz_buffer* buf = nullptr;
    qreal pageHeightSn = page->size.height();
    
    fz_try(ctx) {
        buf = fz_new_buffer(ctx, 4096);  // Initial capacity
        
        // Save graphics state
        fz_append_string(ctx, buf, "q\n");
        
        // Iterate through all layers (bottom to top)
        // NOTE: Layer opacity and stroke alpha are not yet supported in PDF export.
        // This would require setting up ExtGState resources for transparency.
        // For now, all strokes are exported as fully opaque.
        for (const auto& layer : page->vectorLayers) {
            // Safety check for null shared_ptr
            if (!layer) {
                continue;
            }
            
            if (!layer->visible || layer->strokes().isEmpty()) {
                continue;
            }
            
            // Process each stroke in the layer
            for (const VectorStroke& stroke : layer->strokes()) {
                // Build the stroke polygon using existing VectorLayer logic
                VectorLayer::StrokePolygonResult polyResult = VectorLayer::buildStrokePolygon(stroke);
                
                // Set fill color (RGB values 0-1)
                // Note: Alpha is ignored for now (requires ExtGState for PDF transparency)
                float r = stroke.color.redF();
                float g = stroke.color.greenF();
                float b = stroke.color.blueF();
                
                // Format: "r g b rg" for RGB fill color
                char colorCmd[64];
                snprintf(colorCmd, sizeof(colorCmd), "%.4f %.4f %.4f rg\n", r, g, b);
                fz_append_string(ctx, buf, colorCmd);
                
                if (polyResult.isSinglePoint) {
                    // Single point - draw as a filled circle
                    appendCircleToBuffer(ctx, buf, polyResult.startCapCenter, 
                                        polyResult.startCapRadius, pageHeightSn);
                } else if (!polyResult.polygon.isEmpty()) {
                    // Multi-point stroke - draw the polygon
                    appendPolygonToBuffer(ctx, buf, polyResult.polygon, pageHeightSn);
                    
                    // Draw round end caps if needed
                    if (polyResult.hasRoundCaps) {
                        appendCircleToBuffer(ctx, buf, polyResult.startCapCenter,
                                            polyResult.startCapRadius, pageHeightSn);
                        appendCircleToBuffer(ctx, buf, polyResult.endCapCenter,
                                            polyResult.endCapRadius, pageHeightSn);
                    }
                }
            }
        }
        
        // Restore graphics state
        fz_append_string(ctx, buf, "Q\n");
    }
    fz_catch(ctx) {
        qWarning() << "[MuPdfExporter] Failed to build strokes content stream:" 
                   << fz_caught_message(ctx);
        if (buf) {
            fz_drop_buffer(ctx, buf);
        }
        return nullptr;
    }
    
    return buf;
}

// ============================================================================
// PDF Background (Phase 4)
// ============================================================================

/**
 * @brief Get the rotation value of a source PDF page.
 * @param ctx MuPDF context
 * @param srcPdf Source PDF document
 * @param pageIndex Page index (0-based)
 * @return Rotation in degrees (0, 90, 180, or 270), normalized
 */
static int getSourcePageRotation(fz_context* ctx, pdf_document* srcPdf, int pageIndex)
{
    int rotation = 0;
    
    fz_try(ctx) {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, srcPdf, pageIndex);
        pdf_obj* rotateObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Rotate));
        if (rotateObj) {
            rotation = pdf_to_int(ctx, rotateObj);
            // Normalize to 0, 90, 180, or 270
            rotation = ((rotation % 360) + 360) % 360;
            // Only accept valid rotation values
            if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
                rotation = 0;
            }
        }
    }
    fz_catch(ctx) {
        rotation = 0;
    }
    
    return rotation;
}

/**
 * @brief Get the BBox of a source PDF page (CropBox or MediaBox).
 * @param ctx MuPDF context
 * @param srcPdf Source PDF document
 * @param pageIndex Page index (0-based)
 * @return Page bounds as fz_rect
 */
static fz_rect getSourcePageBBox(fz_context* ctx, pdf_document* srcPdf, int pageIndex)
{
    fz_rect bbox = fz_empty_rect;
    
    fz_try(ctx) {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, srcPdf, pageIndex);
        
        // Try CropBox first, fall back to MediaBox
        pdf_obj* boxObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(CropBox));
        if (!boxObj) {
            boxObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(MediaBox));
        }
        
        if (boxObj) {
            bbox = pdf_to_rect(ctx, boxObj);
        }
    }
    fz_catch(ctx) {
        bbox = fz_empty_rect;
    }
    
    return bbox;
}

pdf_obj* MuPdfExporter::importPageAsXObject(int sourcePageIndex)
{
    // Validate we have source PDF and output document
    if (!m_sourcePdf || !m_outputDoc || !m_ctx) {
        qWarning() << "[MuPdfExporter] importPageAsXObject: No source PDF or output document";
        return nullptr;
    }
    
    // Validate page index
    int srcPageCount = pdf_count_pages(m_ctx, m_sourcePdf);
    if (sourcePageIndex < 0 || sourcePageIndex >= srcPageCount) {
        qWarning() << "[MuPdfExporter] importPageAsXObject: Page" << sourcePageIndex
                   << "out of range (source has" << srcPageCount << "pages)";
        return nullptr;
    }
    
    pdf_obj* xobj = nullptr;
    
    fz_try(m_ctx) {
        // Load the source page object
        pdf_obj* srcPageObj = pdf_lookup_page_obj(m_ctx, m_sourcePdf, sourcePageIndex);
        
        // Get page properties
        // MediaBox defines the page coordinate system
        pdf_obj* mediaBox = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(MediaBox));
        if (!mediaBox) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Source page has no MediaBox");
        }
        
        // Get CropBox if it exists (actual visible area), otherwise use MediaBox
        pdf_obj* cropBox = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(CropBox));
        pdf_obj* bbox = cropBox ? cropBox : mediaBox;
        
        // Get page Resources (fonts, images, color spaces, etc.)
        pdf_obj* srcResources = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(Resources));
        
        // Get page Contents stream(s)
        pdf_obj* srcContents = pdf_dict_get(m_ctx, srcPageObj, PDF_NAME(Contents));
        
        // Create the Form XObject dictionary in output document
        xobj = pdf_new_dict(m_ctx, m_outputDoc, 8);
        
        // Set required Form XObject properties
        pdf_dict_put(m_ctx, xobj, PDF_NAME(Type), PDF_NAME(XObject));
        pdf_dict_put(m_ctx, xobj, PDF_NAME(Subtype), PDF_NAME(Form));
        pdf_dict_put(m_ctx, xobj, PDF_NAME(FormType), pdf_new_int(m_ctx, 1));
        
        // Copy BBox (use graft to handle indirect references)
        pdf_obj* graftedBBox = pdf_graft_mapped_object(m_ctx, m_graftMap, bbox);
        pdf_dict_put(m_ctx, xobj, PDF_NAME(BBox), graftedBBox);
        
        // Copy Resources (graft to resolve references and copy fonts/images)
        if (srcResources) {
            pdf_obj* graftedResources = pdf_graft_mapped_object(m_ctx, m_graftMap, srcResources);
            pdf_dict_put(m_ctx, xobj, PDF_NAME(Resources), graftedResources);
        }
        
        // Handle page contents
        // Contents can be a single stream or an array of streams
        if (srcContents) {
            fz_buffer* contentBuf = nullptr;
            
            fz_try(m_ctx) {
                if (pdf_is_array(m_ctx, srcContents)) {
                    // Multiple content streams - concatenate them
                    contentBuf = fz_new_buffer(m_ctx, 1024);
                    
                    int numStreams = pdf_array_len(m_ctx, srcContents);
                    for (int i = 0; i < numStreams; ++i) {
                        pdf_obj* stream = pdf_array_get(m_ctx, srcContents, i);
                        fz_buffer* streamBuf = pdf_load_stream(m_ctx, stream);
                        if (streamBuf) {
                            // Add space between streams
                            if (i > 0) {
                                fz_append_byte(m_ctx, contentBuf, ' ');
                            }
                            fz_append_buffer(m_ctx, contentBuf, streamBuf);
                            fz_drop_buffer(m_ctx, streamBuf);
                        }
                    }
                } else {
                    // Single content stream - copy it directly
                    contentBuf = pdf_load_stream(m_ctx, srcContents);
                }
                
                // Add the content stream to the XObject
                if (contentBuf) {
                    pdf_update_stream(m_ctx, m_outputDoc, xobj, contentBuf, 0);
                }
            }
            fz_always(m_ctx) {
                if (contentBuf) fz_drop_buffer(m_ctx, contentBuf);
            }
            fz_catch(m_ctx) {
                fz_rethrow(m_ctx);  // Re-throw to outer catch block
            }
        }
        
        // Add the XObject to the output document's object table
        pdf_update_object(m_ctx, m_outputDoc, pdf_to_num(m_ctx, xobj), xobj);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] importPageAsXObject failed:"
                   << fz_caught_message(m_ctx);
        // Don't drop xobj here - it may have been partially added to document
        return nullptr;
    }
    
    qDebug() << "[MuPdfExporter] Imported page" << sourcePageIndex << "as XObject";
    return xobj;
}

// ============================================================================
// Image Handling (Phase 5 - TODO)
// ============================================================================

static bool addImageToPage(fz_context* ctx, pdf_document* outputDoc,
                           const ImageObject* img, fz_buffer* contentBuf,
                           pdf_obj* resources, int imageIndex, float pageHeightPt,
                           const PdfExportOptions& options)
{
    if (!img || !contentBuf || !resources || !ctx || !outputDoc) {
        return false;
    }
    
    // Check if image is loaded
    if (!img->isLoaded() || img->pixmap().isNull()) {
        qWarning() << "[MuPdfExporter] Image not loaded:" << img->imagePath;
        return false;
    }
    
    // Skip invisible images
    if (!img->visible) {
        return true;
    }
    
    // Get image data
    QImage qimg = img->pixmap().toImage();
    if (qimg.isNull()) {
        qWarning() << "[MuPdfExporter] Failed to convert pixmap to image";
        return false;
    }
    
    // Determine if image has alpha
    bool hasAlpha = qimg.hasAlphaChannel();
    
    // Calculate display size in PDF points
    // SpeedyNote uses 96 DPI, PDF uses 72 DPI
    float displayWidthPt = static_cast<float>(img->size.width()) * SN_TO_PDF_SCALE;
    float displayHeightPt = static_cast<float>(img->size.height()) * SN_TO_PDF_SCALE;
    
    // Skip zero-size images (would cause invalid transformation matrix)
    if (displayWidthPt <= 0 || displayHeightPt <= 0) {
        qWarning() << "[MuPdfExporter] Skipping zero-size image";
        return true;
    }
    
    QSizeF displaySizePt(displayWidthPt, displayHeightPt);
    
    // Compress with downsampling
    QByteArray compressedData = MuPdfExporter::compressImage(qimg, hasAlpha, displaySizePt, options.dpi);
    if (compressedData.isEmpty()) {
        qWarning() << "[MuPdfExporter] Failed to compress image";
        return false;
    }
    
    fz_buffer* imgBuf = nullptr;
    fz_image* fzImage = nullptr;
    
    fz_try(ctx) {
        // Create image from compressed data
        imgBuf = fz_new_buffer_from_copied_data(ctx, 
            reinterpret_cast<const unsigned char*>(compressedData.constData()),
            compressedData.size());
        
        fzImage = fz_new_image_from_buffer(ctx, imgBuf);
        fz_drop_buffer(ctx, imgBuf);
        imgBuf = nullptr;  // Prevent double-drop in fz_always
        
        // Add image to PDF as XObject
        pdf_obj* imgXObj = pdf_add_image(ctx, outputDoc, fzImage);
        fz_drop_image(ctx, fzImage);
        fzImage = nullptr;  // Prevent double-drop in fz_always
        
        // Get or create XObject dictionary in resources
        pdf_obj* xobjectDict = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
        if (!xobjectDict) {
            xobjectDict = pdf_new_dict(ctx, outputDoc, 4);
            pdf_dict_put(ctx, resources, PDF_NAME(XObject), xobjectDict);
        }
        
        // Add image XObject with unique name
        char imgName[16];
        snprintf(imgName, sizeof(imgName), "Img%d", imageIndex);
        pdf_dict_put(ctx, xobjectDict, pdf_new_name(ctx, imgName), imgXObj);
        
        // Build transformation matrix for position, scale, and rotation
        // PDF image XObjects are 1x1 unit, so we need to scale to display size
        // Position is relative to page origin (bottom-left in PDF)
        
        float posX = static_cast<float>(img->position.x()) * SN_TO_PDF_SCALE;
        float posY = static_cast<float>(img->position.y()) * SN_TO_PDF_SCALE;
        
        // Convert Y from top-left origin to bottom-left origin
        // The image's top-left corner in PDF coords
        float pdfY = pageHeightPt - posY - displayHeightPt;
        
        // Append drawing commands to content buffer
        fz_append_string(ctx, contentBuf, "q\n");  // Save graphics state
        
        if (img->rotation != 0.0) {
            // For rotation, we need to:
            // 1. Translate to image center
            // 2. Rotate
            // 3. Translate back
            // 4. Scale and position
            
            float centerX = posX + displayWidthPt / 2.0f;
            float centerY = pdfY + displayHeightPt / 2.0f;
            float radians = static_cast<float>(img->rotation * M_PI / 180.0);
            float cosR = cosf(radians);
            float sinR = sinf(radians);
            
            // Combined matrix: translate to center, rotate, translate back, then scale/position
            // This is complex, so let's build it step by step in the content stream
            char cmd[256];
            
            // Translate to center, rotate, translate back
            snprintf(cmd, sizeof(cmd), 
                     "1 0 0 1 %.4f %.4f cm\n",  // Translate to center
                     centerX, centerY);
            fz_append_string(ctx, contentBuf, cmd);
            
            snprintf(cmd, sizeof(cmd),
                     "%.4f %.4f %.4f %.4f 0 0 cm\n",  // Rotate
                     cosR, sinR, -sinR, cosR);
            fz_append_string(ctx, contentBuf, cmd);
            
            snprintf(cmd, sizeof(cmd),
                     "1 0 0 1 %.4f %.4f cm\n",  // Translate back
                     -displayWidthPt / 2.0f, -displayHeightPt / 2.0f);
            fz_append_string(ctx, contentBuf, cmd);
            
            // Scale to display size (image XObject is 1x1)
            snprintf(cmd, sizeof(cmd),
                     "%.4f 0 0 %.4f 0 0 cm\n",
                     displayWidthPt, displayHeightPt);
            fz_append_string(ctx, contentBuf, cmd);
        } else {
            // No rotation - simple scale and position
            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                     "%.4f 0 0 %.4f %.4f %.4f cm\n",
                     displayWidthPt, displayHeightPt, posX, pdfY);
            fz_append_string(ctx, contentBuf, cmd);
        }
        
        // Draw the image
        char doCmd[32];
        snprintf(doCmd, sizeof(doCmd), "/%s Do\n", imgName);
        fz_append_string(ctx, contentBuf, doCmd);
        
        fz_append_string(ctx, contentBuf, "Q\n");  // Restore graphics state
        
        qDebug() << "[MuPdfExporter] Added image" << imageIndex 
                 << "at (" << posX << "," << pdfY << ")"
                 << "size" << displayWidthPt << "x" << displayHeightPt
                 << "rotation" << img->rotation;
    }
    fz_always(ctx) {
        // Clean up any resources that weren't transferred to the document
        if (imgBuf) fz_drop_buffer(ctx, imgBuf);
        if (fzImage) fz_drop_image(ctx, fzImage);
    }
    fz_catch(ctx) {
        qWarning() << "[MuPdfExporter] Failed to add image:" << fz_caught_message(ctx);
        return false;
    }
    
    return true;
}

QByteArray MuPdfExporter::compressImage(const QImage& image, bool hasAlpha,
                                         const QSizeF& displaySizePt, int targetDpi)
{
    if (image.isNull()) {
        return QByteArray();
    }
    
    // Calculate if downsampling is needed
    // Display size is in PDF points (72 DPI)
    // Calculate the pixel size needed at target DPI
    QImage workImage = image;
    
    if (displaySizePt.width() > 0 && displaySizePt.height() > 0 && targetDpi > 0) {
        // Display size in inches
        qreal displayWidthInches = displaySizePt.width() / 72.0;
        qreal displayHeightInches = displaySizePt.height() / 72.0;
        
        // Required pixels at target DPI
        int requiredWidth = qRound(displayWidthInches * targetDpi);
        int requiredHeight = qRound(displayHeightInches * targetDpi);
        
        // Only downsample if image is larger than needed
        // (never upsample - that would increase file size without quality benefit)
        if (image.width() > requiredWidth || image.height() > requiredHeight) {
            // Calculate scale factor (maintain aspect ratio)
            qreal scaleX = static_cast<qreal>(requiredWidth) / image.width();
            qreal scaleY = static_cast<qreal>(requiredHeight) / image.height();
            qreal scale = qMin(scaleX, scaleY);
            
            int newWidth = qRound(image.width() * scale);
            int newHeight = qRound(image.height() * scale);
            
            // Ensure minimum size of 1x1
            newWidth = qMax(1, newWidth);
            newHeight = qMax(1, newHeight);
            
            qDebug() << "[MuPdfExporter] Downsampling image from"
                     << image.width() << "x" << image.height()
                     << "to" << newWidth << "x" << newHeight
                     << "(target:" << targetDpi << "DPI)";
            
            // Use smooth transformation for high quality downsampling
            workImage = image.scaled(newWidth, newHeight, 
                                     Qt::KeepAspectRatio, 
                                     Qt::SmoothTransformation);
        }
    }
    
    // Compress the (possibly downsampled) image
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    
    if (hasAlpha) {
        // PNG for images with transparency
        // PNG is lossless and preserves alpha channel
        if (!workImage.save(&buffer, "PNG")) {
            qWarning() << "[MuPdfExporter] Failed to compress image as PNG";
            return QByteArray();
        }
        qDebug() << "[MuPdfExporter] Compressed image as PNG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
    } else {
        // JPEG for opaque images (photos)
        // JPEG is lossy but much smaller for photographic content
        // Quality 85 is a good balance between size and quality
        QImage opaqueImage = workImage;
        
        // Convert to RGB if necessary (JPEG doesn't support alpha)
        if (opaqueImage.hasAlphaChannel()) {
            // Create an opaque version by compositing on white background
            QImage rgb(opaqueImage.size(), QImage::Format_RGB888);
            rgb.fill(Qt::white);
            QPainter painter(&rgb);
            painter.drawImage(0, 0, opaqueImage);
            painter.end();
            opaqueImage = rgb;
        } else if (opaqueImage.format() != QImage::Format_RGB888 &&
                   opaqueImage.format() != QImage::Format_RGB32) {
            opaqueImage = opaqueImage.convertToFormat(QImage::Format_RGB888);
        }
        
        if (!opaqueImage.save(&buffer, "JPEG", 85)) {
            qWarning() << "[MuPdfExporter] Failed to compress image as JPEG";
            return QByteArray();
        }
        qDebug() << "[MuPdfExporter] Compressed image as JPEG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
    }
    
    buffer.close();
    return result;
}

// ============================================================================
// Metadata and Outline (Phase 7 - TODO)
// ============================================================================

bool MuPdfExporter::writeMetadata()
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    fz_try(m_ctx) {
        // Set Producer to SpeedyNote
        pdf_obj* info = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info));
        if (!info) {
            info = pdf_new_dict(m_ctx, m_outputDoc, 4);
            pdf_dict_put_drop(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info), info);
        }
        
        // Add SpeedyNote as Producer
        pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Producer), "SpeedyNote 1.0");
        
        // TODO: Copy metadata from source PDF if available
        // TODO: Update modification date
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to write metadata:" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

bool MuPdfExporter::writeOutline(const QVector<int>& exportedPages)
{
    // TODO: Phase 7 implementation
    // 1. Read outline from source PDF
    // 2. Filter to only include entries for exported pages
    // 3. Remap page indices
    // 4. Write to output PDF
    
    Q_UNUSED(exportedPages)
    
    return true;
}

// ============================================================================
// Finalization
// ============================================================================

bool MuPdfExporter::saveDocument(const QString& outputPath)
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    QByteArray pathUtf8 = outputPath.toUtf8();
    
    fz_try(m_ctx) {
        // Write PDF with default options
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;       // Compress streams
        opts.do_compress_images = 1; // Compress images
        opts.do_compress_fonts = 1;  // Compress fonts
        
        pdf_save_document(m_ctx, m_outputDoc, pathUtf8.constData(), &opts);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to save document:" << fz_caught_message(m_ctx);
        return false;
    }
    
    qDebug() << "[MuPdfExporter] Saved to" << outputPath;
    return true;
}

#endif // SPEEDYNOTE_MUPDF_EXPORT

