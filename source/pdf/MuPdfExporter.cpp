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
#include <map>       // for ExtGState alpha cache

// Forward declarations for static helper functions defined later in this file
static void appendLayerStrokesToBuffer(fz_context* ctx, pdf_document* outputDoc,
                                       fz_buffer* buf, pdf_obj* resources,
                                       const VectorLayer* layer, qreal pageHeightSn,
                                       int& gsIndex, std::map<int, QString>& alphaToGsName);
static int getSourcePageRotation(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
static fz_rect getSourcePageBBox(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
static pdf_obj* writeOutlineRecursive(fz_context* ctx, pdf_document* outputDoc,
                                      fz_outline* srcOutline, 
                                      const std::map<int, int>& pdfToExportIndex);
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
    m_lastError.clear();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Starting export:"
             << pageIndices.size() << "pages at" << options.dpi << "DPI"
             << "to" << options.outputPath;
    #endif
    
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
        // Use detailed error message if available
        result.errorMessage = m_lastError.isEmpty() 
            ? tr("Failed to open source PDF") 
            : m_lastError;
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Process each page
    int total = static_cast<int>(pageIndices.size());
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
        
        // Clean up partial output file if it exists
        if (QFile::exists(options.outputPath)) {
            QFile::remove(options.outputPath);
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[MuPdfExporter] Removed partial output file";
            #endif
        }
        
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
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Export complete:"
             << result.pagesExported << "pages,"
             << (result.fileSizeBytes / 1024) << "KB";
    #endif
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
            
            // Validate range is within document bounds
            // Return empty (error) if entire range is out of bounds
            if (start > totalPages && end > totalPages) {
                qWarning() << "[MuPdfExporter] Page range" << start << "-" << end 
                           << "is completely out of bounds (document has" << totalPages << "pages)";
                return QVector<int>();  // Invalid range
            }
            if (start < 1 && end < 1) {
                qWarning() << "[MuPdfExporter] Page range" << start << "-" << end << "is invalid";
                return QVector<int>();  // Invalid range
            }
            
            // Clamp partial overlaps (e.g., "1-100" on a 10-page doc exports 1-10)
            start = qMax(1, qMin(start, totalPages));
            end = qMax(1, qMin(end, totalPages));
            
            // Convert to 0-based
            start -= 1;
            end -= 1;
            
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
            
            // Validate page is within document bounds
            if (page < 1 || page > totalPages) {
                qWarning() << "[MuPdfExporter] Page" << page 
                           << "is out of bounds (document has" << totalPages << "pages)";
                return QVector<int>();  // Invalid page
            }
            
            // Convert to 0-based
            int pageIndex = page - 1;
            
            if (!seen.contains(pageIndex)) {
                result.append(pageIndex);
                seen.insert(pageIndex);
            }
            continue;
        }
        
        // Invalid part - return empty to signal error
        qWarning() << "[MuPdfExporter] Invalid page range part:" << part;
        return QVector<int>();
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
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Context initialized";
    #endif
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
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] No source PDF (blank document)";
        #endif
        return true;
    }
    
    if (!QFile::exists(pdfPath)) {
        qWarning() << "[MuPdfExporter] Source PDF not found:" << pdfPath;
        m_lastError = tr("Source PDF file not found: %1").arg(pdfPath);
        return false;
    }
    
    QByteArray pathUtf8 = pdfPath.toUtf8();
    
    fz_try(m_ctx) {
        m_sourceDoc = fz_open_document(m_ctx, pathUtf8.constData());
        
        // Check for password-protected PDF
        if (fz_needs_password(m_ctx, m_sourceDoc)) {
            qWarning() << "[MuPdfExporter] Source PDF is password-protected";
            fz_drop_document(m_ctx, m_sourceDoc);
            m_sourceDoc = nullptr;
            m_lastError = tr("Cannot export password-protected PDF.\nPlease remove the password and try again.");
            return false;
        }
        
        // Verify it's a PDF (for grafting capabilities)
        m_sourcePdf = pdf_document_from_fz_document(m_ctx, m_sourceDoc);
        if (!m_sourcePdf) {
            qWarning() << "[MuPdfExporter] Source is not a PDF document";
            fz_drop_document(m_ctx, m_sourceDoc);
            m_sourceDoc = nullptr;
            m_lastError = tr("Source file is not a valid PDF document.");
            return false;
        }
        
        // Create graft map for efficient multi-page grafting
        // This ensures shared resources (fonts, images) are only copied once
        m_graftMap = pdf_new_graft_map(m_ctx, m_outputDoc);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to open source PDF:" << fz_caught_message(m_ctx);
        m_lastError = tr("Failed to open source PDF: %1").arg(QString::fromUtf8(fz_caught_message(m_ctx)));
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Opened source PDF:" << pdfPath
             << "with" << fz_count_pages(m_ctx, m_sourceDoc) << "pages";
    #endif
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
    
    // For annotations-only mode, skip PDF background entirely but keep page dimensions
    // This renders only strokes/images on a blank white page
    pdf_obj* bgXObject = nullptr;
    if (!m_options.annotationsOnly) {
        // Import the source PDF page as an XObject
        bgXObject = importPageAsXObject(pdfPageNum);
        if (!bgXObject) {
            qWarning() << "[MuPdfExporter] Failed to import PDF page as XObject, falling back to blank";
            return renderBlankPage(pageIndex);
        }
    }
    
    // Build combined content stream: background XObject + layers + objects
    fz_buffer* combinedContent = nullptr;
    pdf_obj* resources = nullptr;
    
    fz_try(m_ctx) {
        // Create content buffer
        combinedContent = fz_new_buffer(m_ctx, 1024);
        
        // Create Resources dictionary
        resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
        
        // Draw background XObject if present (not in annotations-only mode)
        if (bgXObject) {
            // Get source page properties for rotation handling
            // Only needed when drawing the background XObject
            int srcRotation = getSourcePageRotation(m_ctx, m_sourcePdf, pdfPageNum);
            fz_rect srcBBox = getSourcePageBBox(m_ctx, m_sourcePdf, pdfPageNum);
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
                    #ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "[MuPdfExporter] Applied rotation" << srcRotation 
                             << "to page" << pageIndex;
                    #endif
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
            
            // Create XObject subdictionary with background form
            pdf_obj* xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
            pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, "BGForm"), bgXObject);
            pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
        }
        
        // Render content with proper layer affinity ordering:
        // 1. Objects with affinity -1 (below all strokes)
        // 2. Layer 0 strokes
        // 3. Objects with affinity 0
        // 4. Layer 1 strokes
        // 5. Objects with affinity 1
        // ... and so on
        // N. Objects with affinity >= numLayers (always on top)
        
        int imageIndex = 0;
        int gsIndex = 0;  // Counter for ExtGState names (for stroke transparency)
        std::map<int, QString> alphaToGsName;  // Cache: alpha (0-100) -> GS name
        int numLayers = static_cast<int>(page->vectorLayers.size());
        qreal pageHeightSn = page->size.height();
        
        // Save graphics state for strokes/objects
        fz_append_string(m_ctx, combinedContent, "q\n");
        
        // Helper lambda to add objects with a specific affinity (sorted by zOrder)
        auto addObjectsWithAffinity = [&](int affinity) {
            auto it = page->objectsByAffinity.find(affinity);
            if (it == page->objectsByAffinity.end()) return;
            
            // Sort by zOrder (the map stores pointers, not owned objects)
            std::vector<InsertedObject*> sorted = it->second;
            std::sort(sorted.begin(), sorted.end(), 
                      [](const InsertedObject* a, const InsertedObject* b) {
                          return a->zOrder < b->zOrder;
                      });
            
            for (const InsertedObject* obj : sorted) {
                if (obj->type() == QStringLiteral("image")) {
                    const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                    if (imgObj && imgObj->isLoaded()) {
                        addImageToPage(m_ctx, m_outputDoc, imgObj, combinedContent, resources, imageIndex++, heightPt, m_options);
                    }
                }
            }
        };
        
        // 1. Objects with affinity -1 (below all strokes)
        addObjectsWithAffinity(-1);
        
        // 2. Interleave layers and objects
        for (int layerIdx = 0; layerIdx < numLayers; ++layerIdx) {
            // Render this layer's strokes (with transparency support)
            appendLayerStrokesToBuffer(m_ctx, m_outputDoc, combinedContent, resources, 
                                      page->vectorLayers[layerIdx].get(), pageHeightSn, gsIndex, alphaToGsName);
            
            // Render objects with affinity = layerIdx (above this layer, below next)
            addObjectsWithAffinity(layerIdx);
        }
        
        // 3. Objects with affinity >= numLayers (always on top of all strokes)
        for (const auto& [affinity, objects] : page->objectsByAffinity) {
            if (affinity >= numLayers) {
                // Sort by zOrder
                std::vector<InsertedObject*> sorted = objects;
                std::sort(sorted.begin(), sorted.end(), 
                          [](const InsertedObject* a, const InsertedObject* b) {
                              return a->zOrder < b->zOrder;
                          });
                
                for (const InsertedObject* obj : sorted) {
                    if (obj->type() == QStringLiteral("image")) {
                        const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                        if (imgObj && imgObj->isLoaded()) {
                            addImageToPage(m_ctx, m_outputDoc, imgObj, combinedContent, resources, imageIndex++, heightPt, m_options);
                        }
                    }
                }
            }
        }
        
        // Restore graphics state
        fz_append_string(m_ctx, combinedContent, "Q\n");
        
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
        // Note: resources and bgXObject are owned by the document now, don't drop
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create modified page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Rendered modified page" << pageIndex 
             << "(PDF page" << pdfPageNum << "+ layers/objects)";
    #endif
    return true;
}

// ============================================================================
// Page Background Rendering (Phase 6)
// ============================================================================

/**
 * @brief Build PDF content stream for page background (color, grid, lines).
 * @param ctx MuPDF context
 * @param page SpeedyNote page with background settings
 * @param widthPt Page width in PDF points
 * @param heightPt Page height in PDF points
 * @return Buffer containing background content stream, or nullptr if no background needed
 * 
 * This renders:
 * - Background color (if not white)
 * - Grid pattern (BackgroundType::Grid)
 * - Ruled lines (BackgroundType::Lines)
 * 
 * Note: Custom background images are handled separately as XObjects.
 */
static fz_buffer* buildBackgroundContentStream(fz_context* ctx, const Page* page,
                                                float widthPt, float heightPt)
{
    if (!ctx || !page) return nullptr;
    
    // Check if we need to render any background
    bool needsColorFill = (page->backgroundColor != Qt::white);
    bool needsGrid = (page->backgroundType == Page::BackgroundType::Grid);
    bool needsLines = (page->backgroundType == Page::BackgroundType::Lines);
    
    if (!needsColorFill && !needsGrid && !needsLines) {
        return nullptr;  // Default white background, nothing to draw
    }
    
    fz_buffer* buf = nullptr;
    
    fz_try(ctx) {
        buf = fz_new_buffer(ctx, 512);
        char cmd[128];
        
        // 1. Fill background color (if not white)
        if (needsColorFill) {
            // Set fill color (RGB, 0-1 range)
            float r = page->backgroundColor.redF();
            float g = page->backgroundColor.greenF();
            float b = page->backgroundColor.blueF();
            
            snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f rg\n", r, g, b);
            fz_append_string(ctx, buf, cmd);
            
            // Draw filled rectangle covering entire page
            snprintf(cmd, sizeof(cmd), "0 0 %.4f %.4f re f\n", widthPt, heightPt);
            fz_append_string(ctx, buf, cmd);
        }
        
        // 2. Draw grid or lines
        if (needsGrid || needsLines) {
            // Set stroke color for grid/lines
            float r = page->gridColor.redF();
            float g = page->gridColor.greenF();
            float b = page->gridColor.blueF();
            
            snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f RG\n", r, g, b);
            fz_append_string(ctx, buf, cmd);
            
            // Set line width (0.5 pt is a good default for grid lines)
            fz_append_string(ctx, buf, "0.5 w\n");
            
            if (needsGrid) {
                // Grid spacing in PDF points
                float spacingPt = static_cast<float>(page->gridSpacing) * SN_TO_PDF_SCALE;
                if (spacingPt < 1.0f) spacingPt = 10.0f;  // Minimum spacing
                
                // Draw vertical lines (same in both coordinate systems)
                for (float x = spacingPt; x < widthPt; x += spacingPt) {
                    snprintf(cmd, sizeof(cmd), "%.4f 0 m %.4f %.4f l S\n", x, x, heightPt);
                    fz_append_string(ctx, buf, cmd);
                }
                
                // Draw horizontal lines
                // SpeedyNote: first line at y=spacing from top
                // PDF: y=0 is at bottom, so first line is at heightPt - spacingPt
                for (float pdfY = heightPt - spacingPt; pdfY > 0; pdfY -= spacingPt) {
                    snprintf(cmd, sizeof(cmd), "0 %.4f m %.4f %.4f l S\n", pdfY, widthPt, pdfY);
                    fz_append_string(ctx, buf, cmd);
                }
            } else if (needsLines) {
                // Line spacing in PDF points
                float spacingPt = static_cast<float>(page->lineSpacing) * SN_TO_PDF_SCALE;
                if (spacingPt < 1.0f) spacingPt = 10.0f;  // Minimum spacing
                
                // Draw horizontal lines only (ruled paper)
                // SpeedyNote: first line at y=spacing from top
                // PDF: y=0 is at bottom, so first line is at heightPt - spacingPt
                for (float pdfY = heightPt - spacingPt; pdfY > 0; pdfY -= spacingPt) {
                    snprintf(cmd, sizeof(cmd), "0 %.4f m %.4f %.4f l S\n", pdfY, widthPt, pdfY);
                    fz_append_string(ctx, buf, cmd);
                }
            }
        }
    }
    fz_catch(ctx) {
        if (buf) fz_drop_buffer(ctx, buf);
        return nullptr;
    }
    
    return buf;
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
    
    // Build background content stream (color, grid, lines)
    // Skip background in annotations-only mode
    fz_buffer* backgroundContent = nullptr;
    if (!m_options.annotationsOnly) {
        backgroundContent = buildBackgroundContentStream(m_ctx, page, widthPt, heightPt);
    }
    
    // Check if page has images to add
    bool hasImages = !page->objects.empty();
    
    // Check for custom background image (also skip in annotations-only mode)
    bool hasCustomBackground = !m_options.annotationsOnly &&
                               (page->backgroundType == Page::BackgroundType::Custom && 
                                !page->customBackground.isNull());
    
    // Check if page has strokes
    bool hasStrokes = false;
    for (const auto& layer : page->vectorLayers) {
        if (layer && !layer->strokes().isEmpty()) {
            hasStrokes = true;
            break;
        }
    }
    
    // Determine if we need a combined content buffer
    bool needsCombined = (backgroundContent != nullptr) || hasImages || hasCustomBackground || hasStrokes;
    
    fz_buffer* finalContent = nullptr;
    pdf_obj* resources = nullptr;
    
    fz_try(m_ctx) {
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        
        if (needsCombined) {
            // Create combined content buffer
            finalContent = fz_new_buffer(m_ctx, 1024);
            
            // Create resources dictionary (needed for custom background, images, or stroke transparency)
            // We create it whenever we have content, as strokes may need ExtGState for transparency
            resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
            
            int imageIndex = 0;
            int gsIndex = 0;  // Counter for ExtGState names (for stroke transparency)
            std::map<int, QString> alphaToGsName;  // Cache: alpha (0-100) -> GS name
            
            // 1. Background color/grid/lines first
            if (backgroundContent) {
                unsigned char* data;
                size_t len = fz_buffer_storage(m_ctx, backgroundContent, &data);
                fz_append_data(m_ctx, finalContent, data, len);
            }
            
            // 2. Custom background image (covers entire page, before strokes)
            if (hasCustomBackground) {
                // Convert QPixmap to QImage
                QImage bgImage = page->customBackground.toImage();
                if (!bgImage.isNull()) {
                    // Compress the background image
                    bool hasAlpha = bgImage.hasAlphaChannel();
                    QSizeF displaySizePt(widthPt, heightPt);
                    QByteArray compressedData = compressImage(bgImage, hasAlpha, displaySizePt, m_options.dpi);
                    
                    if (!compressedData.isEmpty()) {
                        // Create image XObject with proper memory management
                        fz_buffer* imgBuf = nullptr;
                        fz_image* fzImage = nullptr;
                        
                        fz_try(m_ctx) {
                            imgBuf = fz_new_buffer_from_copied_data(m_ctx,
                                reinterpret_cast<const unsigned char*>(compressedData.constData()),
                                compressedData.size());
                            
                            fzImage = fz_new_image_from_buffer(m_ctx, imgBuf);
                            fz_drop_buffer(m_ctx, imgBuf);
                            imgBuf = nullptr;  // Prevent double-drop
                            
                            pdf_obj* imgXObj = pdf_add_image(m_ctx, m_outputDoc, fzImage);
                            fz_drop_image(m_ctx, fzImage);
                            fzImage = nullptr;  // Prevent double-drop
                            
                            // Add to resources
                            pdf_obj* xobjectDict = pdf_dict_get(m_ctx, resources, PDF_NAME(XObject));
                            if (!xobjectDict) {
                                xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
                                pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
                            }
                            
                            char imgName[16];
                            snprintf(imgName, sizeof(imgName), "Img%d", imageIndex++);
                            pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, imgName), imgXObj);
                            
                            // Draw image covering entire page
                            char cmd[128];
                            fz_append_string(m_ctx, finalContent, "q\n");
                            snprintf(cmd, sizeof(cmd), "%.4f 0 0 %.4f 0 0 cm\n", widthPt, heightPt);
                            fz_append_string(m_ctx, finalContent, cmd);
                            snprintf(cmd, sizeof(cmd), "/%s Do\n", imgName);
                            fz_append_string(m_ctx, finalContent, cmd);
                            fz_append_string(m_ctx, finalContent, "Q\n");
                            
                            #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "[MuPdfExporter] Added custom background image";
                            #endif
                        }
                        fz_always(m_ctx) {
                            if (imgBuf) fz_drop_buffer(m_ctx, imgBuf);
                            if (fzImage) fz_drop_image(m_ctx, fzImage);
                        }
                        fz_catch(m_ctx) {
                            qWarning() << "[MuPdfExporter] Failed to add custom background:" 
                                       << fz_caught_message(m_ctx);
                            // Continue without background (non-fatal)
                        }
                    }
                }
            }
            
            // 3. Render content with proper layer affinity ordering:
            //    - Objects with affinity -1 (below all strokes)
            //    - Layer 0 strokes
            //    - Objects with affinity 0
            //    - Layer 1 strokes
            //    - Objects with affinity 1
            //    - ... and so on
            //    - Objects with affinity >= numLayers (always on top)
            
            int numLayers = static_cast<int>(page->vectorLayers.size());
            qreal pageHeightSn = page->size.height();
            
            // Save graphics state for strokes/objects
            fz_append_string(m_ctx, finalContent, "q\n");
            
            // Helper lambda to add objects with a specific affinity (sorted by zOrder)
            auto addObjectsWithAffinity = [&](int affinity) {
                auto it = page->objectsByAffinity.find(affinity);
                if (it == page->objectsByAffinity.end()) return;
                
                // Sort by zOrder
                std::vector<InsertedObject*> sorted = it->second;
                std::sort(sorted.begin(), sorted.end(), 
                          [](const InsertedObject* a, const InsertedObject* b) {
                              return a->zOrder < b->zOrder;
                          });
                
                for (const InsertedObject* obj : sorted) {
                    if (obj->type() == QStringLiteral("image")) {
                        const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                        if (imgObj && imgObj->isLoaded()) {
                            addImageToPage(m_ctx, m_outputDoc, imgObj, finalContent, resources, imageIndex++, heightPt, m_options);
                        }
                    }
                }
            };
            
            // Objects with affinity -1 (below all strokes)
            addObjectsWithAffinity(-1);
            
            // Interleave layers and objects
            for (int layerIdx = 0; layerIdx < numLayers; ++layerIdx) {
                // Render this layer's strokes (with transparency support)
                appendLayerStrokesToBuffer(m_ctx, m_outputDoc, finalContent, resources,
                                          page->vectorLayers[layerIdx].get(), pageHeightSn, gsIndex, alphaToGsName);
                
                // Render objects with affinity = layerIdx (above this layer, below next)
                addObjectsWithAffinity(layerIdx);
            }
            
            // Objects with affinity >= numLayers (always on top of all strokes)
            for (const auto& [affinity, objects] : page->objectsByAffinity) {
                if (affinity >= numLayers) {
                    // Sort by zOrder
                    std::vector<InsertedObject*> sorted = objects;
                    std::sort(sorted.begin(), sorted.end(), 
                              [](const InsertedObject* a, const InsertedObject* b) {
                                  return a->zOrder < b->zOrder;
                              });
                    
                    for (const InsertedObject* obj : sorted) {
                        if (obj->type() == QStringLiteral("image")) {
                            const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                            if (imgObj && imgObj->isLoaded()) {
                                addImageToPage(m_ctx, m_outputDoc, imgObj, finalContent, resources, imageIndex++, heightPt, m_options);
                            }
                        }
                    }
                }
            }
            
            // Restore graphics state
            fz_append_string(m_ctx, finalContent, "Q\n");
            
            // Create page with resources and combined content
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, resources, finalContent);
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        } else {
            // Simple path - completely empty page (no strokes, no objects, no background)
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, nullptr, nullptr);
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        }
    }
    fz_always(m_ctx) {
        if (backgroundContent) {
            fz_drop_buffer(m_ctx, backgroundContent);
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

// NOTE: This function is currently unused. The implementation uses content stream operators
// (via appendPolygonToBuffer) instead of fz_path objects. Kept for potential future use
// with fz_fill_path() on a drawing device if needed for advanced rendering scenarios.
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
 * @brief Get or create an ExtGState resource for a given alpha value.
 * @param ctx MuPDF context
 * @param outputDoc Output PDF document
 * @param resources Resources dictionary to add ExtGState to
 * @param alpha The fill alpha value (0.0 to 1.0)
 * @param gsIndex Current graphics state index (will be incremented only if new entry created)
 * @param alphaToGsName Cache mapping alpha values to existing GS names (for reuse)
 * @return The name of the ExtGState (e.g., "GS0", "GS1", etc.) or empty if alpha is 1.0
 * 
 * Creates an ExtGState dictionary with:
 *   /Type /ExtGState
 *   /ca <alpha>   (fill alpha)
 * 
 * The ExtGState is added to resources under /ExtGState/<name>.
 * 
 * OPTIMIZATION: Caches ExtGState entries by alpha value (quantized to 2 decimal places).
 * Multiple strokes with the same opacity reuse the same ExtGState entry.
 */
static QString getOrCreateExtGState(fz_context* ctx, pdf_document* outputDoc,
                                     pdf_obj* resources, float alpha, int& gsIndex,
                                     std::map<int, QString>& alphaToGsName)
{
    // If fully opaque, no need for ExtGState
    if (alpha >= 0.999f) {
        return QString();
    }
    
    // Clamp alpha to valid range
    alpha = qBound(0.0f, alpha, 1.0f);
    
    // Quantize alpha to 2 decimal places (0-100 integer key)
    // This avoids creating separate entries for alpha 0.501 vs 0.502
    int alphaKey = qRound(alpha * 100.0f);
    
    // Check if we already have an ExtGState for this alpha
    auto it = alphaToGsName.find(alphaKey);
    if (it != alphaToGsName.end()) {
        return it->second;  // Reuse existing ExtGState
    }
    
    // Generate unique name for this graphics state
    QString gsName = QStringLiteral("GS%1").arg(gsIndex++);
    QByteArray gsNameUtf8 = gsName.toUtf8();
    
    // Get or create ExtGState dictionary in resources
    pdf_obj* extGStateDict = pdf_dict_get(ctx, resources, PDF_NAME(ExtGState));
    if (!extGStateDict) {
        extGStateDict = pdf_new_dict(ctx, outputDoc, 4);
        pdf_dict_put(ctx, resources, PDF_NAME(ExtGState), extGStateDict);
    }
    
    // Create the graphics state dictionary
    pdf_obj* gsDict = pdf_new_dict(ctx, outputDoc, 2);
    pdf_dict_put(ctx, gsDict, PDF_NAME(Type), PDF_NAME(ExtGState));
    pdf_dict_put_real(ctx, gsDict, PDF_NAME(ca), alpha);  // Fill alpha (lowercase 'ca')
    
    // Add to ExtGState dictionary
    pdf_dict_put(ctx, extGStateDict, pdf_new_name(ctx, gsNameUtf8.constData()), gsDict);
    
    // Cache for reuse
    alphaToGsName[alphaKey] = gsName;
    
    return gsName;
}

/**
 * @brief Append a single layer's strokes to the content buffer.
 * @param ctx MuPDF context
 * @param outputDoc Output PDF document (for creating ExtGState resources)
 * @param buf Buffer to append to (must not be null)
 * @param resources Resources dictionary (for adding ExtGState entries)
 * @param layer The vector layer to render
 * @param pageHeightSn Page height in SpeedyNote coordinates (for Y-flip)
 * @param gsIndex Current graphics state index counter (modified by function)
 * @param alphaToGsName Cache for ExtGState names by alpha value (for reuse)
 * 
 * This is used by the interleaved rendering to render layers one at a time,
 * allowing objects to be inserted between layers based on their affinity.
 * 
 * Opacity handling:
 * - Layer opacity is applied to all strokes in the layer
 * - Stroke color alpha is multiplied with layer opacity
 * - Total alpha < 1.0 creates an ExtGState with fill alpha (ca)
 */
static void appendLayerStrokesToBuffer(fz_context* ctx, pdf_document* outputDoc,
                                       fz_buffer* buf, pdf_obj* resources,
                                       const VectorLayer* layer, qreal pageHeightSn,
                                       int& gsIndex, std::map<int, QString>& alphaToGsName)
{
    if (!ctx || !buf || !layer) return;
    
    if (!layer->visible || layer->strokes().isEmpty()) {
        return;
    }
    
    // Get layer opacity
    float layerOpacity = static_cast<float>(layer->opacity);
    
    for (const VectorStroke& stroke : layer->strokes()) {
        // Build the stroke polygon using existing VectorLayer logic
        VectorLayer::StrokePolygonResult polyResult = VectorLayer::buildStrokePolygon(stroke);
        
        // Calculate effective alpha (stroke alpha × layer opacity)
        float strokeAlpha = static_cast<float>(stroke.color.alphaF());
        float effectiveAlpha = strokeAlpha * layerOpacity;
        bool needsTransparency = (effectiveAlpha < 0.999f) && resources && outputDoc;
        
        // Save graphics state if using transparency (so we can restore after)
        if (needsTransparency) {
            fz_append_string(ctx, buf, "q\n");
            
            // Apply transparency via ExtGState (reuses existing entry if same alpha)
            QString gsName = getOrCreateExtGState(ctx, outputDoc, resources, effectiveAlpha, gsIndex, alphaToGsName);
            if (!gsName.isEmpty()) {
                char gsCmd[32];
                snprintf(gsCmd, sizeof(gsCmd), "/%s gs\n", gsName.toUtf8().constData());
                fz_append_string(ctx, buf, gsCmd);
            }
        }
        
        // Set fill color (RGB values 0-1)
        float r = stroke.color.redF();
        float g = stroke.color.greenF();
        float b = stroke.color.blueF();
        
        char colorCmd[64];
        snprintf(colorCmd, sizeof(colorCmd), "%.4f %.4f %.4f rg\n", r, g, b);
        fz_append_string(ctx, buf, colorCmd);
        
        if (polyResult.isSinglePoint) {
            appendCircleToBuffer(ctx, buf, polyResult.startCapCenter, 
                                polyResult.startCapRadius, pageHeightSn);
        } else if (!polyResult.polygon.isEmpty()) {
            appendPolygonToBuffer(ctx, buf, polyResult.polygon, pageHeightSn);
            
            if (polyResult.hasRoundCaps) {
                appendCircleToBuffer(ctx, buf, polyResult.startCapCenter,
                                    polyResult.startCapRadius, pageHeightSn);
                appendCircleToBuffer(ctx, buf, polyResult.endCapCenter,
                                    polyResult.endCapRadius, pageHeightSn);
            }
        }
        
        // Restore graphics state if we saved it for transparency
        if (needsTransparency) {
            fz_append_string(ctx, buf, "Q\n");
        }
    }
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
        
        // Add the XObject to the output document's object table FIRST
        // This converts it from a direct object to an indirect object with a proper
        // object number. This must be done before pdf_update_stream() which requires
        // an indirect object.
        //
        // BUG FIX: Previously we used pdf_update_object(ctx, doc, pdf_to_num(xobj), xobj)
        // but pdf_to_num() returns 0 for direct objects (no object number assigned yet),
        // which caused the XObject to overwrite the root object, breaking the PDF.
        xobj = pdf_add_object(m_ctx, m_outputDoc, xobj);
        
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
                
                // Add the content stream to the XObject (now an indirect object)
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
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] importPageAsXObject failed:"
                   << fz_caught_message(m_ctx);
        // Don't drop xobj here - it may have been partially added to document
        return nullptr;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Imported page" << sourcePageIndex << "as XObject";
    #endif
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
            
            // Negate rotation angle to account for Y-axis flip
            // SpeedyNote: Y increases downward, positive rotation = counterclockwise
            // PDF: Y increases upward, so we need to negate to preserve visual rotation direction
            float radians = static_cast<float>(-img->rotation * M_PI / 180.0);
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
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Added image" << imageIndex 
                 << "at (" << posX << "," << pdfY << ")"
                 << "size" << displayWidthPt << "x" << displayHeightPt
                 << "rotation" << img->rotation;
        #endif
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
            
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[MuPdfExporter] Downsampling image from"
                     << image.width() << "x" << image.height()
                     << "to" << newWidth << "x" << newHeight
                     << "(target:" << targetDpi << "DPI)";
            #endif
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
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Compressed image as PNG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
        #endif
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
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Compressed image as JPEG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
        #endif
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
        // Get or create Info dictionary
        pdf_obj* info = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info));
        if (!info) {
            info = pdf_new_dict(m_ctx, m_outputDoc, 8);
            pdf_dict_put_drop(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info), info);
        }
        
        // Copy metadata from source PDF if available
        if (m_sourceDoc) {
            // Copy Title
            char titleBuf[512] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc, 
                                   "info:Title", titleBuf, sizeof(titleBuf)) > 0 && titleBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Title), titleBuf);
            }
            
            // Copy Author
            char authorBuf[256] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Author", authorBuf, sizeof(authorBuf)) > 0 && authorBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Author), authorBuf);
            }
            
            // Copy Subject
            char subjectBuf[512] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Subject", subjectBuf, sizeof(subjectBuf)) > 0 && subjectBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Subject), subjectBuf);
            }
            
            // Copy Keywords
            char keywordsBuf[1024] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Keywords", keywordsBuf, sizeof(keywordsBuf)) > 0 && keywordsBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Keywords), keywordsBuf);
            }
            
            // Copy Creator (original authoring application)
            char creatorBuf[256] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Creator", creatorBuf, sizeof(creatorBuf)) > 0 && creatorBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Creator), creatorBuf);
            }
        }
        
        // Add/override Producer with SpeedyNote attribution
        pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Producer), "SpeedyNote 1.0");
        
        // Update ModDate to current time (PDF date format: D:YYYYMMDDHHmmSS)
        QDateTime now = QDateTime::currentDateTime();
        QString modDateStr = QStringLiteral("D:%1").arg(now.toString("yyyyMMddHHmmss"));
        QByteArray modDateUtf8 = modDateStr.toUtf8();
        pdf_dict_put_text_string(m_ctx, info, PDF_NAME(ModDate), modDateUtf8.constData());
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Wrote metadata, ModDate:" << modDateStr;
        #endif
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to write metadata:" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

bool MuPdfExporter::writeOutline(const QVector<int>& exportedPages)
{
    // No source PDF means no outline to copy
    if (!m_sourcePdf || !m_outputDoc || !m_ctx || !m_document) {
        return true;  // Not an error, just nothing to do
    }
    
    // Load outline from source PDF
    fz_outline* srcOutline = nullptr;
    fz_try(m_ctx) {
        srcOutline = fz_load_outline(m_ctx, m_sourceDoc);
    }
    fz_catch(m_ctx) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] No outline in source PDF";
        #endif
        return true;  // No outline is fine
    }
    
    if (!srcOutline) {
        return true;  // No outline to copy
    }
    
    // Build mapping: PDF page index → export page index
    // 
    // Document pages can be:
    // - PDF-backed: page->pdfPageNumber >= 0
    // - Inserted: page->pdfPageNumber < 0 (blank, grid, custom, etc.)
    //
    // exportedPages[i] = document page index
    // We need: pdfPageIndex → i (the export index)
    
    std::map<int, int> pdfToExportIndex;
    
    for (int exportIdx = 0; exportIdx < exportedPages.size(); ++exportIdx) {
        int docPageIdx = exportedPages[exportIdx];
        const Page* page = m_document->page(docPageIdx);
        if (page && page->pdfPageNumber >= 0) {
            // This document page is backed by a PDF page
            pdfToExportIndex[page->pdfPageNumber] = exportIdx;
        }
    }
    
    if (pdfToExportIndex.empty()) {
        // No PDF pages in export, outline would be useless
        fz_drop_outline(m_ctx, srcOutline);
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] No PDF pages in export, skipping outline";
        #endif
        return true;
    }
    
    // Recursively build and write the outline
    fz_try(m_ctx) {
        pdf_obj* outlines = writeOutlineRecursive(m_ctx, m_outputDoc, srcOutline, pdfToExportIndex);
        
        if (outlines) {
            // Set the document catalog's Outlines entry
            pdf_obj* catalog = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Root));
            if (catalog) {
                pdf_dict_put(m_ctx, catalog, PDF_NAME(Outlines), outlines);
                pdf_dict_put(m_ctx, catalog, PDF_NAME(PageMode), PDF_NAME(UseOutlines));
            }
        }
    }
    fz_always(m_ctx) {
        fz_drop_outline(m_ctx, srcOutline);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to write outline:" << fz_caught_message(m_ctx);
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Wrote outline with" << pdfToExportIndex.size() << "PDF page mappings";
    #endif
    return true;
}

/**
 * Static helper to recursively build outline tree for output PDF.
 * Uses fz_outline which cannot be forward-declared in the header.
 */
static pdf_obj* writeOutlineRecursive(fz_context* ctx, pdf_document* outputDoc,
                                      fz_outline* srcOutline,
                                      const std::map<int, int>& pdfToExportIndex)
{
    if (!srcOutline || !outputDoc || !ctx) {
        return nullptr;
    }
    
    // First pass: collect valid outline entries (those pointing to exported pages)
    // We need this to set up First/Last/Prev/Next pointers correctly
    struct OutlineEntry {
        fz_outline* src;
        int exportPageIndex;
        pdf_obj* pdfObj;
        pdf_obj* childrenContainer;  // Store children result from recursive call
    };
    std::vector<OutlineEntry> validEntries;
    
    for (fz_outline* ol = srcOutline; ol; ol = ol->next) {
        int pdfPage = ol->page.page;
        
        // Check if this entry points to an exported page
        auto it = pdfToExportIndex.find(pdfPage);
        bool pointsToExportedPage = (it != pdfToExportIndex.end());
        
        // For entries with children, we need to check if any child (recursively)
        // points to an exported page. We do this by attempting the recursive call
        // and seeing if it produces any valid entries.
        pdf_obj* childrenContainer = nullptr;
        bool hasValidChildren = false;
        
        if (ol->down) {
            childrenContainer = writeOutlineRecursive(ctx, outputDoc, ol->down, pdfToExportIndex);
            hasValidChildren = (childrenContainer != nullptr);
        }
        
        // Entry is valid if:
        // - It points to an exported page, OR
        // - It has children that recursively contain valid entries
        if (pointsToExportedPage || hasValidChildren) {
            OutlineEntry entry;
            entry.src = ol;
            entry.exportPageIndex = pointsToExportedPage ? it->second : -1;
            entry.pdfObj = nullptr;
            entry.childrenContainer = childrenContainer;
            validEntries.push_back(entry);
        }
        // If neither condition is met, the entry and its children are all outside
        // the export range - skip it entirely
    }
    
    if (validEntries.empty()) {
        return nullptr;
    }
    
    // Create outline items
    for (size_t i = 0; i < validEntries.size(); ++i) {
        OutlineEntry& entry = validEntries[i];
        fz_outline* ol = entry.src;
        
        // Create the outline item dictionary and add it as an indirect object
        // This is crucial - pdf_new_dict creates a direct object, but outline items
        // need to be indirect objects (with proper object numbers) for the PDF's
        // xref table. Without this, pdf_save_document causes infinite recursion
        // in pdf_set_obj_parent when trying to serialize the object tree.
        pdf_obj* item = pdf_new_dict(ctx, outputDoc, 6);
        item = pdf_add_object(ctx, outputDoc, item);
        entry.pdfObj = item;
        
        // Set title
        if (ol->title && ol->title[0]) {
            pdf_dict_put_text_string(ctx, item, PDF_NAME(Title), ol->title);
        }
        
        // Set destination if this entry points to an exported page
        if (entry.exportPageIndex >= 0) {
            // Create destination array: [page /Fit] or [page /XYZ x y z]
            // For simplicity, use /Fit (fit page in window)
            pdf_obj* dest = pdf_new_array(ctx, outputDoc, 2);
            
            // Get the page object by index
            pdf_obj* pageRef = pdf_lookup_page_obj(ctx, outputDoc, entry.exportPageIndex);
            pdf_array_push(ctx, dest, pageRef);
            pdf_array_push(ctx, dest, PDF_NAME(Fit));
            
            pdf_dict_put_drop(ctx, item, PDF_NAME(Dest), dest);
        }
        
        // Handle children (already processed in first pass)
        if (entry.childrenContainer) {
            pdf_obj* children = entry.childrenContainer;
            // children is the root outline object, get First/Last from it
            pdf_obj* firstChild = pdf_dict_get(ctx, children, PDF_NAME(First));
            pdf_obj* lastChild = pdf_dict_get(ctx, children, PDF_NAME(Last));
            int childCount = pdf_dict_get_int(ctx, children, PDF_NAME(Count));
            
            if (firstChild && lastChild) {
                pdf_dict_put(ctx, item, PDF_NAME(First), firstChild);
                pdf_dict_put(ctx, item, PDF_NAME(Last), lastChild);
                
                // Set parent on all children
                for (pdf_obj* child = firstChild; child; 
                     child = pdf_dict_get(ctx, child, PDF_NAME(Next))) {
                    pdf_dict_put(ctx, child, PDF_NAME(Parent), item);
                }
                
                // Set Count (negative means closed)
                int count = ol->is_open ? childCount : -childCount;
                if (count != 0) {
                    pdf_dict_put_int(ctx, item, PDF_NAME(Count), count);
                }
            }
        }
    }
    
    // Link items with Prev/Next
    for (size_t i = 0; i < validEntries.size(); ++i) {
        pdf_obj* item = validEntries[i].pdfObj;
        
        if (i > 0) {
            pdf_dict_put(ctx, item, PDF_NAME(Prev), validEntries[i-1].pdfObj);
        }
        if (i < validEntries.size() - 1) {
            pdf_dict_put(ctx, item, PDF_NAME(Next), validEntries[i+1].pdfObj);
        }
    }
    
    // Create container with First/Last/Count
    // The container also needs to be an indirect object for proper xref handling
    pdf_obj* container = pdf_new_dict(ctx, outputDoc, 4);
    container = pdf_add_object(ctx, outputDoc, container);
    pdf_dict_put(ctx, container, PDF_NAME(Type), PDF_NAME(Outlines));
    pdf_dict_put(ctx, container, PDF_NAME(First), validEntries.front().pdfObj);
    pdf_dict_put(ctx, container, PDF_NAME(Last), validEntries.back().pdfObj);
    pdf_dict_put_int(ctx, container, PDF_NAME(Count), static_cast<int>(validEntries.size()));
    
    // Set Parent on top-level items to point to the container
    for (const auto& entry : validEntries) {
        pdf_dict_put(ctx, entry.pdfObj, PDF_NAME(Parent), container);
    }
    
    return container;
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
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Saved to" << outputPath;
    #endif
    return true;
}

#endif // SPEEDYNOTE_MUPDF_EXPORT

