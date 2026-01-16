// ============================================================================
// MuPdfProvider - MuPDF implementation of PdfProvider
// ============================================================================

#include "MuPdfProvider.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QDebug>
#include <QFile>

// ============================================================================
// Construction / Destruction
// ============================================================================

MuPdfProvider::MuPdfProvider(const QString& pdfPath)
    : m_path(pdfPath)
{
    // Create MuPDF context
    m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
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
    
    qDebug() << "MuPdfProvider: Loaded" << pdfPath << "with" << m_pageCount << "pages";
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
        item.title = QString::fromUtf8(ol->title ? ol->title : "");
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
    if (!isValid() || pageIndex < 0 || pageIndex >= m_pageCount) {
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
        
        // Create transformation matrix
        fz_matrix ctm = fz_scale(scale, scale);
        
        // Get page bounds at this scale
        fz_rect bounds = fz_bound_page(m_ctx, page);
        fz_irect bbox = fz_round_rect(fz_transform_rect(bounds, ctm));
        
        // Create pixmap (BGRA for Qt compatibility)
        pix = fz_new_pixmap_with_bbox(m_ctx, fz_device_bgr(m_ctx), bbox, nullptr, 1);
        fz_clear_pixmap_with_value(m_ctx, pix, 255); // White background
        
        // Render page to pixmap
        fz_device* dev = fz_new_draw_device(m_ctx, ctm, pix);
        fz_run_page(m_ctx, page, dev, fz_identity, nullptr);
        fz_close_device(m_ctx, dev);
        fz_drop_device(m_ctx, dev);
        
        // Convert to QImage
        int width = fz_pixmap_width(m_ctx, pix);
        int height = fz_pixmap_height(m_ctx, pix);
        int stride = fz_pixmap_stride(m_ctx, pix);
        unsigned char* samples = fz_pixmap_samples(m_ctx, pix);
        
        // Copy data to QImage (MuPDF pixmap will be freed)
        result = QImage(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) {
            memcpy(result.scanLine(y), samples + y * stride, width * 4);
        }
    }
    fz_always(m_ctx) {
        if (pix) fz_drop_pixmap(m_ctx, pix);
        if (page) fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        qWarning() << "MuPdfProvider: Render failed for page" << pageIndex;
        return QImage();
    }
    
    return result;
}

// ============================================================================
// Text Selection
// ============================================================================

QVector<PdfTextBox> MuPdfProvider::textBoxes(int pageIndex) const
{
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
        
        // Iterate through text blocks
        for (fz_stext_block* block = textPage->first_block; block; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
            
            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                // Build word from characters
                QString word;
                QRectF wordRect;
                
                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                    // Get character rectangle
                    fz_rect r = fz_rect_from_quad(ch->quad);
                    QRectF charRect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
                    
                    // Check if this is a space (word boundary)
                    QChar qch(ch->c);
                    if (qch.isSpace() && !word.isEmpty()) {
                        // Save current word
                        PdfTextBox box;
                        box.text = word;
                        box.boundingBox = wordRect;
                        boxes.append(box);
                        
                        word.clear();
                        wordRect = QRectF();
                    } else if (!qch.isSpace()) {
                        word += qch;
                        if (wordRect.isNull()) {
                            wordRect = charRect;
                        } else {
                            wordRect = wordRect.united(charRect);
                        }
                    }
                }
                
                // Save last word in line
                if (!word.isEmpty()) {
                    PdfTextBox box;
                    box.text = word;
                    box.boundingBox = wordRect;
                    boxes.append(box);
                }
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
                
                if (uri.startsWith("#page=")) {
                    // Internal page link
                    pdfLink.type = PdfLinkType::Goto;
                    pdfLink.targetPage = uri.mid(6).toInt() - 1; // Convert to 0-based
                } else if (uri.startsWith("http://") || uri.startsWith("https://")) {
                    pdfLink.type = PdfLinkType::Uri;
                    pdfLink.uri = uri;
                } else {
                    // Try to resolve as destination name
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

