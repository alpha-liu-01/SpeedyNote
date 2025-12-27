// ============================================================================
// PopplerPdfProvider - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.2)
// ============================================================================

#include "PopplerPdfProvider.h"

// ===== Constructor =====

PopplerPdfProvider::PopplerPdfProvider(const QString& pdfPath)
    : m_path(pdfPath)
{
    m_document = Poppler::Document::load(pdfPath);
    
    if (m_document && !m_document->isLocked()) {
        // Apply rendering hints for high-quality output
        m_document->setRenderHint(Poppler::Document::Antialiasing, true);
        m_document->setRenderHint(Poppler::Document::TextAntialiasing, true);
        m_document->setRenderHint(Poppler::Document::TextHinting, true);
        m_document->setRenderHint(Poppler::Document::TextSlightHinting, true);
    }
}

// ===== Document Info =====

bool PopplerPdfProvider::isValid() const
{
    return m_document != nullptr && !m_document->isLocked();
}

bool PopplerPdfProvider::isLocked() const
{
    return m_document != nullptr && m_document->isLocked();
}

int PopplerPdfProvider::pageCount() const
{
    return m_document ? m_document->numPages() : 0;
}

QString PopplerPdfProvider::title() const
{
    return m_document ? m_document->title() : QString();
}

QString PopplerPdfProvider::author() const
{
    return m_document ? m_document->author() : QString();
}

QString PopplerPdfProvider::subject() const
{
    return m_document ? m_document->subject() : QString();
}

QString PopplerPdfProvider::filePath() const
{
    return m_path;
}

// ===== Outline =====

QVector<PdfOutlineItem> PopplerPdfProvider::outline() const
{
    QVector<PdfOutlineItem> result;
    
    if (!m_document) {
        return result;
    }
    
    QVector<Poppler::OutlineItem> popplerOutline = m_document->outline();
    result.reserve(popplerOutline.size());
    
    for (const Poppler::OutlineItem& item : popplerOutline) {
        if (!item.isNull()) {
            result.append(convertOutlineItem(item));
        }
    }
    
    return result;
}

bool PopplerPdfProvider::hasOutline() const
{
    if (!m_document) {
        return false;
    }
    return !m_document->outline().isEmpty();
}

PdfOutlineItem PopplerPdfProvider::convertOutlineItem(const Poppler::OutlineItem& popplerItem) const
{
    PdfOutlineItem item;
    item.title = popplerItem.name();
    item.isOpen = popplerItem.isOpen();
    
    // Get target page from destination
    // Note: destination() returns QSharedPointer<const LinkDestination>
    auto dest = popplerItem.destination();
    if (dest && dest->pageNumber() > 0) {
        // Poppler uses 1-based page numbers, we use 0-based
        item.targetPage = dest->pageNumber() - 1;
    }
    
    // Convert children recursively
    if (popplerItem.hasChildren()) {
        QVector<Poppler::OutlineItem> children = popplerItem.children();
        item.children.reserve(children.size());
        
        for (const Poppler::OutlineItem& child : children) {
            if (!child.isNull()) {
                item.children.append(convertOutlineItem(child));
            }
        }
    }
    
    return item;
}

// ===== Page Info =====

QSizeF PopplerPdfProvider::pageSize(int pageIndex) const
{
    auto page = getPage(pageIndex);
    if (!page) {
        return QSizeF();
    }
    return page->pageSizeF();
}

// ===== Rendering =====

QImage PopplerPdfProvider::renderPageToImage(int pageIndex, qreal dpi) const
{
    auto page = getPage(pageIndex);
    if (!page) {
        return QImage();
    }
    qDebug() << "renderPageToImage: pageIndex=" << pageIndex << " dpi=" << dpi;
    return page->renderToImage(dpi, dpi);
}

// ===== Text Selection =====

QVector<PdfTextBox> PopplerPdfProvider::textBoxes(int pageIndex) const
{
    QVector<PdfTextBox> result;
    
    auto page = getPage(pageIndex);
    if (!page) {
        return result;
    }
    
    // Get text boxes from Poppler
    // Note: textList() returns std::vector<std::unique_ptr<Poppler::TextBox>>
    auto popplerBoxes = page->textList();
    result.reserve(static_cast<int>(popplerBoxes.size()));
    
    for (const auto& popplerBox : popplerBoxes) {
        if (!popplerBox) continue;
        
        PdfTextBox box;
        box.text = popplerBox->text();
        box.boundingBox = popplerBox->boundingBox();
        
        // Get character-level bounding boxes if available
        // Poppler provides charBoundingBox(int i) for each character
        int charCount = box.text.length();
        box.charBoundingBoxes.reserve(charCount);
        for (int i = 0; i < charCount; ++i) {
            box.charBoundingBoxes.append(popplerBox->charBoundingBox(i));
        }
        
        result.append(std::move(box));
    }
    
    return result;
}

// ===== Links =====

QVector<PdfLink> PopplerPdfProvider::links(int pageIndex) const
{
    QVector<PdfLink> result;
    
    auto page = getPage(pageIndex);
    if (!page) {
        return result;
    }
    
    // Get links from Poppler
    auto popplerLinks = page->links();
    result.reserve(static_cast<int>(popplerLinks.size()));
    
    for (const auto& popplerLink : popplerLinks) {
        if (!popplerLink) continue;
        
        PdfLink link;
        link.area = popplerLink->linkArea();  // Already normalized (0-1)
        
        switch (popplerLink->linkType()) {
            case Poppler::Link::Goto: {
                link.type = PdfLinkType::Goto;
                auto* gotoLink = static_cast<Poppler::LinkGoto*>(popplerLink.get());
                if (gotoLink) {
                    Poppler::LinkDestination dest = gotoLink->destination();
                    if (dest.pageNumber() > 0) {
                        // Poppler uses 1-based, we use 0-based
                        link.targetPage = dest.pageNumber() - 1;
                    }
                }
                break;
            }
            
            case Poppler::Link::Browse: {
                link.type = PdfLinkType::Uri;
                auto* browseLink = static_cast<Poppler::LinkBrowse*>(popplerLink.get());
                if (browseLink) {
                    link.uri = browseLink->url();
                }
                break;
            }
            
            case Poppler::Link::Execute:
                link.type = PdfLinkType::Execute;
                break;
                
            default:
                link.type = PdfLinkType::None;
                break;
        }
        
        result.append(link);
    }
    
    return result;
}

// ===== Private Helpers =====

std::unique_ptr<Poppler::Page> PopplerPdfProvider::getPage(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->numPages()) {
        return nullptr;
    }
    return std::unique_ptr<Poppler::Page>(m_document->page(pageIndex));
}

// ===== Factory Methods (defined in PdfProvider interface) =====

std::unique_ptr<PdfProvider> PdfProvider::create(const QString& pdfPath)
{
    auto provider = std::make_unique<PopplerPdfProvider>(pdfPath);
    if (provider->isValid()) {
        return provider;
    }
    return nullptr;
}

bool PdfProvider::isAvailable()
{
    // Poppler is always available when this code compiles
    // (it's a compile-time dependency)
    return true;
}
