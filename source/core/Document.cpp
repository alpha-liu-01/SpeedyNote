// ============================================================================
// Document - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3, 1.2.4, 1.2.5)
// ============================================================================

#include "Document.h"

// ===== Constructor =====

Document::Document()
{
    // Generate unique ID
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set timestamps
    created = QDateTime::currentDateTime();
    lastModified = created;
}

// ===== Factory Methods =====

std::unique_ptr<Document> Document::createNew(const QString& docName, Mode docMode)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = docMode;
    
    // Ensure at least one page exists
    doc->ensureMinimumPages();
    
    return doc;
}

std::unique_ptr<Document> Document::createForPdf(const QString& docName, const QString& pdfPath)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = Mode::Paged;
    
    // Try to load the PDF
    if (doc->loadPdf(pdfPath)) {
        // Create pages for all PDF pages
        doc->createPagesForPdf();
    } else {
        // PDF failed to load, but we still create the document
        // The path is stored for potential relink later
        doc->m_pdfPath = pdfPath;
        // Create a single default page
        doc->ensureMinimumPages();
    }
    
    return doc;
}

// =========================================================================
// PDF Reference Management (Task 1.2.4)
// =========================================================================

bool Document::pdfFileExists() const
{
    if (m_pdfPath.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(m_pdfPath);
}

bool Document::loadPdf(const QString& path)
{
    // Unload any existing PDF first
    m_pdfProvider.reset();
    
    // Store the path regardless of load success (for relink)
    m_pdfPath = path;
    
    if (path.isEmpty()) {
        return false;
    }
    
    // Check if file exists
    if (!QFileInfo::exists(path)) {
        return false;
    }
    
    // Check if PDF provider is available
    if (!PdfProvider::isAvailable()) {
        return false;
    }
    
    // Try to load the PDF
    m_pdfProvider = PdfProvider::create(path);
    
    if (!m_pdfProvider || !m_pdfProvider->isValid()) {
        m_pdfProvider.reset();
        return false;
    }
    
    return true;
}

bool Document::relinkPdf(const QString& newPath)
{
    if (loadPdf(newPath)) {
        markModified();
        return true;
    }
    return false;
}

void Document::unloadPdf()
{
    m_pdfProvider.reset();
    // Note: m_pdfPath is preserved for potential relink
}

void Document::clearPdfReference()
{
    m_pdfProvider.reset();
    m_pdfPath.clear();
    markModified();
}

QImage Document::renderPdfPageToImage(int pageIndex, qreal dpi) const
{
    if (!isPdfLoaded()) {
        return QImage();
    }
    return m_pdfProvider->renderPageToImage(pageIndex, dpi);
}

QPixmap Document::renderPdfPageToPixmap(int pageIndex, qreal dpi) const
{
    if (!isPdfLoaded()) {
        return QPixmap();
    }
    return m_pdfProvider->renderPageToPixmap(pageIndex, dpi);
}

int Document::pdfPageCount() const
{
    if (!isPdfLoaded()) {
        return 0;
    }
    return m_pdfProvider->pageCount();
}

QSizeF Document::pdfPageSize(int pageIndex) const
{
    if (!isPdfLoaded()) {
        return QSizeF();
    }
    return m_pdfProvider->pageSize(pageIndex);
}

QString Document::pdfTitle() const
{
    if (!isPdfLoaded()) {
        return QString();
    }
    return m_pdfProvider->title();
}

QString Document::pdfAuthor() const
{
    if (!isPdfLoaded()) {
        return QString();
    }
    return m_pdfProvider->author();
}

bool Document::pdfHasOutline() const
{
    if (!isPdfLoaded()) {
        return false;
    }
    return m_pdfProvider->hasOutline();
}

QVector<PdfOutlineItem> Document::pdfOutline() const
{
    if (!isPdfLoaded()) {
        return QVector<PdfOutlineItem>();
    }
    return m_pdfProvider->outline();
}

// =========================================================================
// Page Management (Task 1.2.5)
// =========================================================================

Page* Document::page(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return nullptr;
    }
    return m_pages[index].get();
}

const Page* Document::page(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return nullptr;
    }
    return m_pages[index].get();
}

Page* Document::addPage()
{
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    m_pages.push_back(std::move(newPage));
    markModified();
    return pagePtr;
}

Page* Document::insertPage(int index)
{
    // Allow inserting at the end (index == size)
    if (index < 0 || index > static_cast<int>(m_pages.size())) {
        return nullptr;
    }
    
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    m_pages.insert(m_pages.begin() + index, std::move(newPage));
    markModified();
    return pagePtr;
}

Page* Document::addPageForPdf(int pdfPageIndex)
{
    auto newPage = createDefaultPage();
    
    // Configure for PDF background
    newPage->backgroundType = Page::BackgroundType::PDF;
    newPage->pdfPageNumber = pdfPageIndex;
    
    // Set page size from PDF (convert from 72 dpi to 96 dpi)
    if (isPdfLoaded() && pdfPageIndex >= 0 && pdfPageIndex < pdfPageCount()) {
        QSizeF pdfSize = pdfPageSize(pdfPageIndex);
        // PDF points are at 72 dpi, convert to 96 dpi
        qreal scale = 96.0 / 72.0;
        newPage->size = QSizeF(pdfSize.width() * scale, pdfSize.height() * scale);
    }
    
    Page* pagePtr = newPage.get();
    m_pages.push_back(std::move(newPage));
    markModified();
    return pagePtr;
}

bool Document::removePage(int index)
{
    // Cannot remove if index invalid
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return false;
    }
    
    // Cannot remove the last page
    if (m_pages.size() <= 1) {
        return false;
    }
    
    m_pages.erase(m_pages.begin() + index);
    markModified();
    return true;
}

bool Document::movePage(int from, int to)
{
    int count = static_cast<int>(m_pages.size());
    
    // Validate indices
    if (from < 0 || from >= count || to < 0 || to >= count) {
        return false;
    }
    
    // No-op if same position
    if (from == to) {
        return true;
    }
    
    // Extract the page
    auto pageToMove = std::move(m_pages[from]);
    m_pages.erase(m_pages.begin() + from);
    
    // Insert at new position
    m_pages.insert(m_pages.begin() + to, std::move(pageToMove));
    
    markModified();
    return true;
}

Page* Document::edgelessPage()
{
    if (mode != Mode::Edgeless) {
        return nullptr;
    }
    
    // Ensure page exists
    ensureMinimumPages();
    
    return m_pages.empty() ? nullptr : m_pages[0].get();
}

const Page* Document::edgelessPage() const
{
    if (mode != Mode::Edgeless) {
        return nullptr;
    }
    return m_pages.empty() ? nullptr : m_pages[0].get();
}

void Document::ensureMinimumPages()
{
    if (m_pages.empty()) {
        auto newPage = createDefaultPage();
        
        // For edgeless mode, mark the page as unbounded
        if (mode == Mode::Edgeless) {
            // Edgeless pages have no fixed size (effectively infinite)
            // We use a large default but it can extend beyond
            newPage->size = QSizeF(4096, 4096);
        }
        
        m_pages.push_back(std::move(newPage));
    }
}

int Document::findPageByPdfPage(int pdfPageIndex) const
{
    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        const Page* p = m_pages[i].get();
        if (p->backgroundType == Page::BackgroundType::PDF && 
            p->pdfPageNumber == pdfPageIndex) {
            return i;
        }
    }
    return -1;
}

void Document::createPagesForPdf()
{
    // Clear existing pages
    m_pages.clear();
    
    if (!isPdfLoaded()) {
        // No PDF loaded, create a single default page
        ensureMinimumPages();
        return;
    }
    
    // Create one page per PDF page
    int count = pdfPageCount();
    for (int i = 0; i < count; ++i) {
        addPageForPdf(i);
    }
    
    // Ensure at least one page
    if (m_pages.empty()) {
        ensureMinimumPages();
    }
    
    // Don't mark as modified since this is initial creation
    modified = false;
}

std::unique_ptr<Page> Document::createDefaultPage()
{
    auto page = std::make_unique<Page>();
    
    // Apply document defaults
    page->size = defaultPageSize;
    page->backgroundType = defaultBackgroundType;
    page->backgroundColor = defaultBackgroundColor;
    page->gridColor = defaultGridColor;
    page->gridSpacing = defaultGridSpacing;
    page->lineSpacing = defaultLineSpacing;
    
    return page;
}

// =========================================================================
// Bookmarks (Task 1.2.6)
// =========================================================================

QVector<Document::Bookmark> Document::getBookmarks() const
{
    QVector<Bookmark> result;
    
    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        const Page* p = m_pages[i].get();
        if (p->isBookmarked) {
            result.append({i, p->bookmarkLabel});
        }
    }
    
    return result;
}

void Document::setBookmark(int pageIndex, const QString& label)
{
    Page* p = page(pageIndex);
    if (!p) return;
    
    p->isBookmarked = true;
    
    if (label.isEmpty()) {
        // Generate default label
        p->bookmarkLabel = QStringLiteral("Bookmark %1").arg(pageIndex + 1);
    } else {
        p->bookmarkLabel = label;
    }
    
    markModified();
}

void Document::removeBookmark(int pageIndex)
{
    Page* p = page(pageIndex);
    if (!p || !p->isBookmarked) return;
    
    p->isBookmarked = false;
    p->bookmarkLabel.clear();
    markModified();
}

bool Document::hasBookmark(int pageIndex) const
{
    const Page* p = page(pageIndex);
    return p && p->isBookmarked;
}

QString Document::bookmarkLabel(int pageIndex) const
{
    const Page* p = page(pageIndex);
    if (!p || !p->isBookmarked) return QString();
    return p->bookmarkLabel;
}

int Document::nextBookmark(int fromPage) const
{
    int count = pageCount();
    if (count == 0) return -1;
    
    // Search from fromPage+1 to end
    for (int i = fromPage + 1; i < count; ++i) {
        if (m_pages[i]->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from 0 to fromPage
    for (int i = 0; i <= fromPage && i < count; ++i) {
        if (m_pages[i]->isBookmarked) {
            return i;
        }
    }
    
    return -1; // No bookmarks found
}

int Document::prevBookmark(int fromPage) const
{
    int count = pageCount();
    if (count == 0) return -1;
    
    // Search from fromPage-1 down to 0
    for (int i = fromPage - 1; i >= 0; --i) {
        if (m_pages[i]->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from end down to fromPage
    for (int i = count - 1; i >= fromPage && i >= 0; --i) {
        if (m_pages[i]->isBookmarked) {
            return i;
        }
    }
    
    return -1; // No bookmarks found
}

bool Document::toggleBookmark(int pageIndex, const QString& label)
{
    if (hasBookmark(pageIndex)) {
        removeBookmark(pageIndex);
        return false; // Removed
    } else {
        setBookmark(pageIndex, label);
        return true; // Added
    }
}

int Document::bookmarkCount() const
{
    int count = 0;
    for (const auto& p : m_pages) {
        if (p->isBookmarked) {
            ++count;
        }
    }
    return count;
}
