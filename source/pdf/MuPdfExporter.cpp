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

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm> // for std::sort

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
    // TODO: Phase 3-4 implementation
    // 1. Create new page with source PDF dimensions
    // 2. Import source PDF page as XObject (preserves vectors)
    // 3. Add XObject reference to content stream
    // 4. Convert strokes to MuPDF paths and add to content stream
    // 5. Add images
    
    qDebug() << "[MuPdfExporter] renderModifiedPage" << pageIndex << "(TODO)";
    
    // For now, fall back to blank page rendering
    return renderBlankPage(pageIndex);
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
    float widthPt = pageSize.width() * 72.0f / 96.0f;
    float heightPt = pageSize.height() * 72.0f / 96.0f;
    
    fz_try(m_ctx) {
        // Create a new blank page with the correct dimensions
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, nullptr, nullptr);
        pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        
        // TODO: Phase 3 - Add strokes as vector paths
        // TODO: Phase 5 - Add images
        // TODO: Phase 6 - Add page background (grid, lines, color)
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create blank page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

// ============================================================================
// Vector Stroke Conversion (Phase 3 - TODO)
// ============================================================================

fz_path* MuPdfExporter::polygonToPath(const QPolygonF& polygon)
{
    if (polygon.isEmpty() || !m_ctx) {
        return nullptr;
    }
    
    fz_path* path = nullptr;
    
    fz_try(m_ctx) {
        path = fz_new_path(m_ctx);
        
        // Move to first point
        fz_moveto(m_ctx, path, polygon[0].x(), polygon[0].y());
        
        // Line to remaining points
        for (int i = 1; i < polygon.size(); ++i) {
            fz_lineto(m_ctx, path, polygon[i].x(), polygon[i].y());
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

bool MuPdfExporter::addStrokesToPage(pdf_page* pdfPage, const Page* page)
{
    // TODO: Phase 3 implementation
    // 1. Get all strokes from all layers
    // 2. For each stroke, build polygon using VectorLayer logic
    // 3. Convert polygon to MuPDF path
    // 4. Set fill color
    // 5. Add path to page content stream
    
    Q_UNUSED(pdfPage)
    Q_UNUSED(page)
    
    return true;
}

// ============================================================================
// PDF Background (Phase 4 - TODO)
// ============================================================================

pdf_obj* MuPdfExporter::importPageAsXObject(int sourcePageIndex)
{
    // TODO: Phase 4 implementation
    // Use pdf_page_contentes_from_annot or similar to import page as XObject
    
    Q_UNUSED(sourcePageIndex)
    return nullptr;
}

// ============================================================================
// Image Handling (Phase 5 - TODO)
// ============================================================================

bool MuPdfExporter::addImageToPage(const ImageObject* img, pdf_page* pdfPage)
{
    // TODO: Phase 5 implementation
    // 1. Get QImage from ImageObject
    // 2. Apply transforms (position, scale, rotation)
    // 3. Compress appropriately (JPEG/PNG)
    // 4. Embed as Image XObject
    // 5. Add to page content stream
    
    Q_UNUSED(img)
    Q_UNUSED(pdfPage)
    
    return true;
}

QByteArray MuPdfExporter::compressImage(const QImage& image, bool hasAlpha, int targetDpi)
{
    // TODO: Phase 5 implementation
    // Smart compression: JPEG for photos, PNG for transparency
    
    Q_UNUSED(image)
    Q_UNUSED(hasAlpha)
    Q_UNUSED(targetDpi)
    
    return QByteArray();
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

