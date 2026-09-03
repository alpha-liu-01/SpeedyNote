// ============================================================================
// MuPdfProvider - MuPDF implementation of PdfProvider
// ============================================================================

#include "MuPdfProvider.h"

#include "MuPdfLocks.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QDebug>
#include <QFile>
#include <QMutexLocker>

// CJK detection shared with PdfSearchEngine / DocumentViewport / OCR engines
// so the "one PdfTextBox per CJK glyph" rule below stays consistent with the
// space-joining heuristics used elsewhere.
#include "../ocr/OcrTextBlock.h"

// ============================================================================
// Construction / Destruction
// ============================================================================

static constexpr size_t SN_MUPDF_STORE_MAX = 32 << 20; // 32 MiB

MuPdfProvider::MuPdfProvider(const QString& pdfPath)
    : m_path(pdfPath)
{
    // Create MuPDF context. BUG-Q003: the shared lock handlers are mandatory —
    // MuPDF's OpenJPEG/HarfBuzz/FreeType globals are only safe when every
    // context in the process serialises through the same mutexes.
    m_ctx = fz_new_context(nullptr, snMuPdfLocks(), SN_MUPDF_STORE_MAX);
    if (!m_ctx) {
        qWarning() << "MuPdfProvider: Failed to create MuPDF context";
        return;
    }
    
    // Register document handlers (PDF, XPS, etc.)
    fz_try(m_ctx) {
        fz_register_document_handlers(m_ctx);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Failed to register document handlers";
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return;
    }
    
    // Open the document
    QByteArray pathUtf8 = pdfPath.toUtf8();
    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, pathUtf8.constData());
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Failed to open" << pdfPath 
                   << "-" << fz_caught_message(m_ctx);
        // FIX: Drop context to prevent memory leak when document fails to open
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return;
    }
    
    // Cache page count
    fz_try(m_ctx) {
        m_pageCount = fz_count_pages(m_ctx, m_doc);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Failed to get page count";
        m_pageCount = 0;
    }
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "MuPdfProvider: Loaded" << pdfPath << "with" << m_pageCount << "pages";
    #endif
}

MuPdfProvider::~MuPdfProvider()
{
    if (m_doc) {
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
    }
    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

// ============================================================================
// Document Info
// ============================================================================

bool MuPdfProvider::isValid() const
{
    return m_ctx != nullptr && m_doc != nullptr && m_pageCount > 0;
}

bool MuPdfProvider::isLocked() const
{
    if (!m_doc) return false;
    
    // Check if document needs password
    int needs = fz_needs_password(m_ctx, m_doc);
    return needs != 0;
}

int MuPdfProvider::pageCount() const
{
    return m_pageCount;
}

QString MuPdfProvider::title() const
{
    return getMetadata("info:Title");
}

QString MuPdfProvider::author() const
{
    return getMetadata("info:Author");
}

QString MuPdfProvider::subject() const
{
    return getMetadata("info:Subject");
}

QString MuPdfProvider::filePath() const
{
    return m_path;
}

QString MuPdfProvider::getMetadata(const char* key) const
{
    if (!isValid()) return QString();
    
    char buf[256] = {0};
    fz_try(m_ctx) {
        fz_lookup_metadata(m_ctx, m_doc, key, buf, sizeof(buf));
    }
    fz_catch(m_ctx) {
        return QString();
    }
    
    return QString::fromUtf8(buf);
}

// ============================================================================
// Outline (Table of Contents)
// ============================================================================

bool MuPdfProvider::hasOutline() const
{
    if (!isValid()) return false;
    
    fz_outline* ol = nullptr;
    fz_try(m_ctx) {
        ol = fz_load_outline(m_ctx, m_doc);
    }
    fz_catch(m_ctx) {
        return false;
    }
    
    bool has = (ol != nullptr);
    if (ol) {
        fz_drop_outline(m_ctx, ol);
    }
    return has;
}

QVector<PdfOutlineItem> MuPdfProvider::outline() const
{
    if (!isValid()) return {};
    
    fz_outline* ol = nullptr;
    fz_try(m_ctx) {
        ol = fz_load_outline(m_ctx, m_doc);
    }
    fz_catch(m_ctx) {
        return {};
    }
    
    QVector<PdfOutlineItem> result = convertOutline(ol);
    
    if (ol) {
        fz_drop_outline(m_ctx, ol);
    }
    
    return result;
}

QVector<PdfOutlineItem> MuPdfProvider::convertOutline(fz_outline* ol) const
{
    QVector<PdfOutlineItem> items;
    
    while (ol) {
        PdfOutlineItem item;
        QString rawTitle = QString::fromUtf8(ol->title ? ol->title : "");
        QString cleaned;
        cleaned.reserve(rawTitle.size());
        for (const QChar ch : rawTitle) {
            if (ch.isPrint() && ch != QChar(0xFFFD)) {
                cleaned.append(ch);
            }
        }
        item.title = cleaned.trimmed();
        item.isOpen = ol->is_open;
        
        // Get destination page
        if (ol->page.page >= 0) {
            item.targetPage = ol->page.page;
        } else {
            item.targetPage = -1;
        }
        
        // Convert children recursively
        if (ol->down) {
            item.children = convertOutline(ol->down);
        }
        
        items.append(item);
        ol = ol->next;
    }
    
    return items;
}

// ============================================================================
// Page Info
// ============================================================================

QSizeF MuPdfProvider::pageSize(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QSizeF();
    }
    
    fz_rect bounds = fz_empty_rect;
    fz_try(m_ctx) {
        fz_page* page = fz_load_page(m_ctx, m_doc, pageIndex);
        bounds = fz_bound_page(m_ctx, page);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        return QSizeF();
    }
    
    return QSizeF(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
}

// ============================================================================
// Rendering
// ============================================================================

QImage MuPdfProvider::renderPageToImage(int pageIndex, qreal dpi) const
{
    // Thread safety: MuPDF context is not thread-safe, so we need to serialize access
    // This is especially important on Android where main thread sync renders can
    // overlap with background async renders (BUG-A006)
    QMutexLocker locker(&m_mutex);
    
    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QImage();
    }
    
    // Additional safety check for context
    if (!m_ctx || !m_doc) {
        qWarning() << "MuPdfProvider: Context or document is null";
        return QImage();
    }
    
    // Scale factor: PDF points are 72 dpi
    float scale = dpi / 72.0f;
    
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    QImage result;
    
    fz_try(m_ctx) {
        // Load page
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        if (!page) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page %d", pageIndex);
        }
        
        // Create transformation matrix
        fz_matrix ctm = fz_scale(scale, scale);
        
        // Get page bounds at this scale
        fz_rect bounds = fz_bound_page(m_ctx, page);
        fz_irect bbox = fz_round_rect(fz_transform_rect(bounds, ctm));
        
        // Bounds guard. Render DPI is already capped upstream (effectivePdfDpi()
        // caps at 300 DPI), so a full-page render is inherently memory-bounded by
        // the page's physical size. We only reject degenerate bounds and
        // genuinely pathological (poster-sized) pages here. The previous per-axis
        // 10000 px limit wrongly blanked tall/narrow pages (e.g. 2112 x 10328) at
        // >120% zoom; we keep full resolution so clarity is preserved.
        const int imgWidth  = bbox.x1 - bbox.x0;
        const int imgHeight = bbox.y1 - bbox.y0;
        constexpr int    kMaxAxis   = 30000;                  // raster-backend safe
        constexpr qint64 kMaxPixels = 64LL * 1024 * 1024;     // ~64 MP (~256 MB BGRA)
        const qint64 totalPixels = qint64(imgWidth) * qint64(imgHeight);
        if (imgWidth <= 0 || imgHeight <= 0 ||
            imgWidth > kMaxAxis || imgHeight > kMaxAxis ||
            totalPixels > kMaxPixels) {
            qWarning() << "MuPdfProvider: Invalid page bounds" << imgWidth << "x" << imgHeight;
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Invalid page bounds");
        }
        
        // Create pixmap (BGRA for Qt compatibility)
        pix = fz_new_pixmap_with_bbox(m_ctx, fz_device_bgr(m_ctx), bbox, nullptr, 1);
        if (!pix) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create pixmap");
        }
        fz_clear_pixmap_with_value(m_ctx, pix, 255); // White background
        
        // Render page to pixmap
        fz_device* dev = fz_new_draw_device(m_ctx, ctm, pix);
        if (!dev) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create draw device");
        }
        fz_run_page(m_ctx, page, dev, fz_identity, nullptr);
        fz_close_device(m_ctx, dev);
        fz_drop_device(m_ctx, dev);
        
        // Convert to QImage
        int width = fz_pixmap_width(m_ctx, pix);
        int height = fz_pixmap_height(m_ctx, pix);
        int stride = fz_pixmap_stride(m_ctx, pix);
        unsigned char* samples = fz_pixmap_samples(m_ctx, pix);
        
        // Verify data is valid before copy
        if (!samples || stride < width * 4) {
            qWarning() << "MuPdfProvider: Invalid pixmap data - samples:" << (samples ? "valid" : "null")
                       << "stride:" << stride << "expected:" << (width * 4);
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Invalid pixmap data");
        }
        
        // Create QImage and copy data
        // Use Format_ARGB32 which matches BGRA byte order on little-endian (ARM)
        result = QImage(width, height, QImage::Format_ARGB32);
        if (result.isNull()) {
            qWarning() << "MuPdfProvider: Failed to allocate QImage" << width << "x" << height;
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to allocate QImage");
        }
        
        // Copy row by row, using QImage's bytesPerLine for destination stride
        for (int y = 0; y < height; ++y) {
            unsigned char* dst = result.scanLine(y);
            const unsigned char* src = samples + y * stride;
            // Use memmove instead of memcpy for safety with potential overlap/alignment
            memmove(dst, src, width * 4);
        }
    }
    fz_always(m_ctx) {
        if (pix) fz_drop_pixmap(m_ctx, pix);
        if (page) fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Render failed for page" << pageIndex 
                   << "-" << fz_caught_message(m_ctx);
        return QImage();
    }
    
    return result;
}

// ============================================================================
// Store Management
// ============================================================================

void MuPdfProvider::trimStore() const
{
    QMutexLocker locker(&m_mutex);
    if (m_ctx) {
        fz_shrink_store(m_ctx, 0);
    }
}

// ============================================================================
// Image Region Detection (for dark-mode inversion masking)
// ============================================================================

// Minimal fz_device that records bounding boxes of raster images.
// All other callbacks (text, paths, shading) are left as defaults (no-ops).

struct ImageCollector {
    fz_device super;            // must be first member
    QVector<QRect>* rects;
};

static void img_collect_fill_image(fz_context*, fz_device* dev_,
    fz_image*, fz_matrix ctm, float, fz_color_params)
{
    auto* dev = reinterpret_cast<ImageCollector*>(dev_);
    fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    dev->rects->append(QRect(
        static_cast<int>(r.x0), static_cast<int>(r.y0),
        static_cast<int>(r.x1 - r.x0 + 0.5f),
        static_cast<int>(r.y1 - r.y0 + 0.5f)));
}

static void img_collect_fill_image_mask(fz_context*, fz_device* dev_,
    fz_image*, fz_matrix ctm, fz_colorspace*, const float*, float, fz_color_params)
{
    auto* dev = reinterpret_cast<ImageCollector*>(dev_);
    fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    dev->rects->append(QRect(
        static_cast<int>(r.x0), static_cast<int>(r.y0),
        static_cast<int>(r.x1 - r.x0 + 0.5f),
        static_cast<int>(r.y1 - r.y0 + 0.5f)));
}

QVector<QRect> MuPdfProvider::imageRegions(int pageIndex, qreal dpi) const
{
    QMutexLocker locker(&m_mutex);
    QVector<QRect> result;

    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount)
        return result;

    float scale = dpi / 72.0f;
    fz_matrix ctm = fz_scale(scale, scale);
    fz_page* page = nullptr;

    // fz_new_derived_device macro internally references a bare 'ctx' variable
    fz_context* ctx = m_ctx;

    fz_device* dev = nullptr;

    fz_try(ctx) {
        page = fz_load_page(ctx, m_doc, pageIndex);

        // Create a lightweight device that only records image positions
        ImageCollector* collector =
            fz_new_derived_device(ctx, ImageCollector);
        collector->super.fill_image      = img_collect_fill_image;
        collector->super.fill_image_mask = img_collect_fill_image_mask;
        collector->rects = &result;

        dev = &collector->super;
        fz_run_page(ctx, page, dev, ctm, nullptr);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx) {
        if (dev) fz_drop_device(ctx, dev);
        if (page) fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        qWarning() << "MuPdfProvider: imageRegions failed for page" << pageIndex
                   << "-" << fz_caught_message(ctx);
        result.clear();
    }

    return result;
}

// ============================================================================
// Text Selection
// ============================================================================

QVector<PdfTextBox> MuPdfProvider::textBoxes(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount) {
        return {};
    }
    
    QVector<PdfTextBox> boxes;
    fz_page* page = nullptr;
    fz_stext_page* textPage = nullptr;
    
    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        
        // Extract text with positions
        fz_stext_options opts = {0};
        textPage = fz_new_stext_page_from_page(m_ctx, page, &opts);
        
        // Iterate through text blocks.
        //
        // Emission invariant (consumers rely on this):
        //   One PdfTextBox == one whitespace-delimited Latin token
        //                  OR one CJK glyph.
        //   box.charBoundingBoxes.size() == box.text.length() in both cases.
        //
        // Why per-CJK-char boxes: CJK scripts have no spaces, so the old
        // "flush on whitespace" loop emitted one giant box per visual line,
        // which collapsed text selection (DocumentViewport::findCharacterAtPoint,
        // updateSelectedTextAndRects_Pdf) and search highlights
        // (PdfSearchEngine::searchPage match-rect union) down to line granularity.
        // Splitting each CJK glyph into its own box restores character precision
        // without affecting Latin word semantics (double-click word, triple-click
        // line still work). PdfSearchEngine's synthetic-space insertion is
        // CJK-aware so multi-char CJK searches still match across the split.
        for (fz_stext_block* block = textPage->first_block; block; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
            
            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                // In-progress Latin word state. CJK glyphs are emitted
                // inline (one box per glyph) and don't touch these.
                QString word;
                QRectF wordRect;
                QVector<QRectF> wordChars;
                
                auto flushWord = [&]() {
                    if (word.isEmpty()) return;
                    PdfTextBox box;
                    box.text = word;
                    box.boundingBox = wordRect;
                    box.charBoundingBoxes = wordChars;
                    boxes.append(box);
                    word.clear();
                    wordRect = QRectF();
                    wordChars.clear();
                };
                
                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                    // Get character rectangle
                    fz_rect r = fz_rect_from_quad(ch->quad);
                    QRectF charRect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
                    
                    QChar qch(ch->c);
                    if (qch.isSpace()) {
                        flushWord();
                    } else if (isCjkLikeChar(qch)) {
                        // Flush any pending Latin word first so the per-glyph
                        // CJK box appears in reading order.
                        flushWord();
                        PdfTextBox cjkBox;
                        cjkBox.text = QString(qch);
                        cjkBox.boundingBox = charRect;
                        cjkBox.charBoundingBoxes = { charRect };
                        boxes.append(cjkBox);
                    } else {
                        word += qch;
                        wordChars.append(charRect);
                        if (wordRect.isNull()) {
                            wordRect = charRect;
                        } else {
                            wordRect = wordRect.united(charRect);
                        }
                    }
                }
                
                // Save trailing Latin word in line (CJK already emitted inline)
                flushWord();
            }
        }
    }
    fz_always(m_ctx) {
        if (textPage) fz_drop_stext_page(m_ctx, textPage);
        if (page) fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Text extraction failed for page" << pageIndex;
        return {};
    }
    
    return boxes;
}

// ============================================================================
// Links
// ============================================================================

QVector<PdfLink> MuPdfProvider::links(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount) {
        return {};
    }
    
    QVector<PdfLink> result;
    fz_page* page = nullptr;
    fz_link* links = nullptr;
    
    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, pageIndex);
        links = fz_load_links(m_ctx, page);
        
        // Get page bounds for normalization
        fz_rect pageBounds = fz_bound_page(m_ctx, page);
        qreal pageWidth = pageBounds.x1 - pageBounds.x0;
        qreal pageHeight = pageBounds.y1 - pageBounds.y0;
        
        for (fz_link* link = links; link; link = link->next) {
            PdfLink pdfLink;
            
            // Normalize link area to 0-1 range
            pdfLink.area = QRectF(
                (link->rect.x0 - pageBounds.x0) / pageWidth,
                (link->rect.y0 - pageBounds.y0) / pageHeight,
                (link->rect.x1 - link->rect.x0) / pageWidth,
                (link->rect.y1 - link->rect.y0) / pageHeight
            );
            
            // Parse URI
            if (link->uri) {
                QString uri = QString::fromUtf8(link->uri);
                
                if (uri.startsWith("http://") || uri.startsWith("https://") ||
                    uri.startsWith("mailto:") || uri.startsWith("file://")) {
                    // External link
                    pdfLink.type = PdfLinkType::Uri;
                    pdfLink.uri = uri;
                } else if (uri.startsWith("#")) {
                    // Internal link - MuPDF uses "#<page_number>" format (1-based)
                    // or "#<named_destination>"
                    QString fragment = uri.mid(1);
                    bool ok = false;
                    int pageNum = fragment.toInt(&ok);
                    if (ok && pageNum > 0) {
                        // Direct page number link (1-based, convert to 0-based)
                        pdfLink.type = PdfLinkType::Goto;
                        pdfLink.targetPage = pageNum - 1;
                    } else {
                        // Named destination - try to resolve
                        float xp, yp;
                        fz_location loc = fz_resolve_link(m_ctx, m_doc, link->uri, &xp, &yp);
                        if (loc.page >= 0) {
                            pdfLink.type = PdfLinkType::Goto;
                            pdfLink.targetPage = loc.page;
                        }
                    }
                } else {
                    // Other format - try to resolve as destination name
                    float xp, yp;
                    fz_location loc = fz_resolve_link(m_ctx, m_doc, link->uri, &xp, &yp);
                    if (loc.page >= 0) {
                        pdfLink.type = PdfLinkType::Goto;
                        pdfLink.targetPage = loc.page;
                    }
                }
            }
            
            if (pdfLink.type != PdfLinkType::None) {
                result.append(pdfLink);
            }
        }
    }
    fz_always(m_ctx) {
        if (links) fz_drop_link(m_ctx, links);
        if (page) fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Link extraction failed for page" << pageIndex;
        return {};
    }
    
    return result;
}

