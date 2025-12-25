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
    // Note: loadPdf() stores the path regardless of success (for relink)
    if (doc->loadPdf(pdfPath)) {
        // Create pages for all PDF pages
        doc->createPagesForPdf();
    } else {
        // PDF failed to load, path is already stored by loadPdf()
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
    
    // Pre-allocate to avoid repeated vector reallocations
    m_pages.reserve(static_cast<size_t>(count));
    
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

// =========================================================================
// Serialization (Task 1.2.7)
// =========================================================================

QJsonObject Document::toJson() const
{
    QJsonObject obj;
    
    // Format version (for compatibility checks)
    obj["format_version"] = formatVersion;
    
    // Identity
    obj["notebook_id"] = id;
    obj["name"] = name;
    obj["author"] = author;
    obj["created"] = created.toString(Qt::ISODate);
    obj["last_modified"] = lastModified.toString(Qt::ISODate);
    
    // Mode
    obj["mode"] = modeToString(mode);
    
    // PDF reference (path only, provider is runtime)
    obj["pdf_path"] = m_pdfPath;
    
    // State
    obj["last_accessed_page"] = lastAccessedPage;
    
    // Default background settings
    obj["default_background"] = defaultBackgroundToJson();
    
    // Page count (for quick info without loading pages)
    obj["page_count"] = pageCount();
    
    return obj;
}

std::unique_ptr<Document> Document::fromJson(const QJsonObject& obj)
{
    auto doc = std::make_unique<Document>();
    
    // Clear the auto-generated ID, we'll load it from JSON
    doc->id = obj["notebook_id"].toString();
    if (doc->id.isEmpty()) {
        // Generate new ID if not present (legacy format)
        doc->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    // Format version
    doc->formatVersion = obj["format_version"].toString("1.0");
    
    // Identity
    doc->name = obj["name"].toString();
    doc->author = obj["author"].toString();
    
    // Timestamps
    QString createdStr = obj["created"].toString();
    if (!createdStr.isEmpty()) {
        doc->created = QDateTime::fromString(createdStr, Qt::ISODate);
    }
    QString modifiedStr = obj["last_modified"].toString();
    if (!modifiedStr.isEmpty()) {
        doc->lastModified = QDateTime::fromString(modifiedStr, Qt::ISODate);
    }
    
    // Mode
    doc->mode = stringToMode(obj["mode"].toString("paged"));
    
    // PDF reference (don't load yet, just store path)
    doc->m_pdfPath = obj["pdf_path"].toString();
    
    // State
    doc->lastAccessedPage = obj["last_accessed_page"].toInt(0);
    
    // Default background settings
    if (obj.contains("default_background")) {
        doc->loadDefaultBackgroundFromJson(obj["default_background"].toObject());
    } else {
        // Legacy format: read flat fields
        QString bgStyle = obj["background_style"].toString("None");
        doc->defaultBackgroundType = stringToBackgroundType(bgStyle);
        QString bgColor = obj["background_color"].toString("#ffffff");
        doc->defaultBackgroundColor = QColor(bgColor);
        doc->defaultGridSpacing = obj["background_density"].toInt(20);
        doc->defaultLineSpacing = obj["background_density"].toInt(24);
    }
    
    // Note: Pages are NOT loaded here - call loadPagesFromJson() separately
    // or use fromFullJson() to load everything
    
    doc->modified = false;
    return doc;
}

QJsonObject Document::toFullJson() const
{
    QJsonObject obj = toJson();
    
    // Add full page content
    obj["pages"] = pagesToJson();
    
    return obj;
}

std::unique_ptr<Document> Document::fromFullJson(const QJsonObject& obj)
{
    // First, load metadata
    auto doc = fromJson(obj);
    // Note: fromJson() always returns a valid document (uses make_unique),
    // but we keep this check for defensive programming / future changes
    if (!doc) {
        return nullptr;
    }
    
    // Load page content
    if (obj.contains("pages")) {
        doc->loadPagesFromJson(obj["pages"].toArray());
    } else {
        // No pages in JSON, ensure minimum
        doc->ensureMinimumPages();
    }
    
    return doc;
}

int Document::loadPagesFromJson(const QJsonArray& pagesArray)
{
    // Clear existing pages
    m_pages.clear();
    
    // Pre-allocate to avoid repeated vector reallocations
    m_pages.reserve(static_cast<size_t>(pagesArray.size()));
    
    int loadedCount = 0;
    
    for (const auto& val : pagesArray) {
        auto page = Page::fromJson(val.toObject());
        if (page) {
            m_pages.push_back(std::move(page));
            ++loadedCount;
        }
    }
    
    // Ensure at least one page exists
    ensureMinimumPages();
    
    return loadedCount;
}

QJsonArray Document::pagesToJson() const
{
    QJsonArray pagesArray;
    
    for (const auto& page : m_pages) {
        pagesArray.append(page->toJson());
    }
    
    return pagesArray;
}

QJsonObject Document::defaultBackgroundToJson() const
{
    QJsonObject bg;
    bg["type"] = backgroundTypeToString(defaultBackgroundType);
    bg["color"] = defaultBackgroundColor.name(QColor::HexArgb);
    bg["grid_color"] = defaultGridColor.name(QColor::HexRgb);  // Use 6-char hex (#RRGGBB) for clarity
    bg["grid_spacing"] = defaultGridSpacing;
    bg["line_spacing"] = defaultLineSpacing;
    bg["page_width"] = defaultPageSize.width();
    bg["page_height"] = defaultPageSize.height();
    return bg;
}

void Document::loadDefaultBackgroundFromJson(const QJsonObject& obj)
{
    defaultBackgroundType = stringToBackgroundType(obj["type"].toString("None"));
    
    QString bgColor = obj["color"].toString("#ffffffff");
    defaultBackgroundColor = QColor(bgColor);
    
    QString gridColor = obj["grid_color"].toString("#c8c8c8");  // Gray (200,200,200) in 6-char hex
    defaultGridColor = QColor(gridColor);
    
    defaultGridSpacing = obj["grid_spacing"].toInt(20);
    defaultLineSpacing = obj["line_spacing"].toInt(24);
    
    if (obj.contains("page_width") && obj.contains("page_height")) {
        defaultPageSize = QSizeF(
            obj["page_width"].toDouble(816),
            obj["page_height"].toDouble(1056)
        );
    }
}

QString Document::backgroundTypeToString(Page::BackgroundType type)
{
    switch (type) {
        case Page::BackgroundType::None:   return "none";
        case Page::BackgroundType::PDF:    return "pdf";
        case Page::BackgroundType::Custom: return "custom";
        case Page::BackgroundType::Grid:   return "grid";
        case Page::BackgroundType::Lines:  return "lines";
        default: return "none";
    }
}

Page::BackgroundType Document::stringToBackgroundType(const QString& str)
{
    QString lower = str.toLower();
    if (lower == "pdf")    return Page::BackgroundType::PDF;
    if (lower == "custom") return Page::BackgroundType::Custom;
    if (lower == "grid")   return Page::BackgroundType::Grid;
    if (lower == "lines")  return Page::BackgroundType::Lines;
    return Page::BackgroundType::None;
}

QString Document::modeToString(Mode m)
{
    switch (m) {
        case Mode::Paged:    return "paged";
        case Mode::Edgeless: return "edgeless";
        default: return "paged";
    }
}

Document::Mode Document::stringToMode(const QString& str)
{
    QString lower = str.toLower();
    if (lower == "edgeless") return Mode::Edgeless;
    return Mode::Paged;
}
