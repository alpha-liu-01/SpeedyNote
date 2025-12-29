// ============================================================================
// Document - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3, 1.2.4, 1.2.5)
// ============================================================================

#include "Document.h"
#include <cmath>

// ===== Constructor & Destructor =====

Document::Document()
{
    // Generate unique ID
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set timestamps
    created = QDateTime::currentDateTime();
    lastModified = created;
    
#ifdef QT_DEBUG
    qDebug() << "Document CREATED:" << this << "id=" << id.left(8);
#endif
}

Document::~Document()
{
#ifdef QT_DEBUG
    qDebug() << "Document DESTROYED:" << this << "id=" << id.left(8) 
             << "pages=" << m_pages.size() << "tiles=" << m_tiles.size();
#endif
    // Note: m_pages, m_tiles, and m_pdfProvider are unique_ptr, auto-cleaned
}

// ===== Factory Methods =====

std::unique_ptr<Document> Document::createNew(const QString& docName, Mode docMode)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = docMode;
    
    if (docMode == Mode::Edgeless) {
        // Don't create any tiles - they're created on-demand when user draws
        // m_tiles starts empty (default state)
    } else {
        // Paged mode: ensure at least one page exists
        doc->ensureMinimumPages();
    }
    
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
    
    // For compatibility, return origin tile (0,0)
    // Creates it if doesn't exist
    return getOrCreateTile(0, 0);
}

const Page* Document::edgelessPage() const
{
    if (mode != Mode::Edgeless) {
        return nullptr;
    }
    // Const version uses getTile (doesn't create)
    return getTile(0, 0);
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
// Edgeless Tile Management (Phase E1)
// =========================================================================

Document::TileCoord Document::tileCoordForPoint(QPointF docPt) const
{
    int tx = static_cast<int>(std::floor(docPt.x() / EDGELESS_TILE_SIZE));
    int ty = static_cast<int>(std::floor(docPt.y() / EDGELESS_TILE_SIZE));
    return {tx, ty};
}

Page* Document::getTile(int tx, int ty) const
{
    TileCoord coord(tx, ty);
    
    // 1. Check if already in memory
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end()) {
        return it->second.get();
    }
    
    // 2. If lazy loading enabled, try to load from disk
    // m_tiles is mutable, so this works on const Document
    if (m_lazyLoadEnabled && m_tileIndex.count(coord) > 0) {
        if (loadTileFromDisk(coord)) {
            Page* loadedTile = m_tiles.at(coord).get();
            // Phase 5.1: Sync layer structure with origin tile (if this isn't the origin)
            if (tx != 0 || ty != 0) {
                syncTileLayerStructure(loadedTile);
            }
            return loadedTile;
        }
    }
    
    // 3. Tile doesn't exist
    return nullptr;
}

Page* Document::getOrCreateTile(int tx, int ty)
{
    TileCoord coord(tx, ty);
    
    // 1. Check if already in memory
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end()) {
        return it->second.get();
    }
    
    // 2. If lazy loading enabled, try to load from disk
    if (m_lazyLoadEnabled && m_tileIndex.count(coord) > 0) {
        if (loadTileFromDisk(coord)) {
            Page* loadedTile = m_tiles.at(coord).get();
            // Phase 5.1: Sync layer structure with origin tile (if this isn't the origin)
            if (tx != 0 || ty != 0) {
                syncTileLayerStructure(loadedTile);
            }
            return loadedTile;
        }
    }
    
    // 3. Create new tile
    auto tile = std::make_unique<Page>();
    tile->size = QSizeF(EDGELESS_TILE_SIZE, EDGELESS_TILE_SIZE);
    tile->backgroundType = defaultBackgroundType;
    tile->backgroundColor = defaultBackgroundColor;
    tile->gridColor = defaultGridColor;
    tile->gridSpacing = defaultGridSpacing;
    tile->lineSpacing = defaultLineSpacing;
    
    // CR-8: Removed tile coord storage in pageIndex/pdfPageNumber - it was never read.
    // Tile coordinate is already the map key, no need to duplicate in Page.
    
    auto [insertIt, inserted] = m_tiles.emplace(coord, std::move(tile));
    
    // Mark new tile as dirty (needs saving)
    m_dirtyTiles.insert(coord);
    markModified();
    
#ifdef QT_DEBUG
    qDebug() << "Document: Created tile at (" << tx << "," << ty << ") total tiles:" << m_tiles.size();
#endif
    
    Page* newTile = insertIt->second.get();
    
    // Phase 5.1: Sync layer structure with origin tile (if this isn't the origin)
    if (tx != 0 || ty != 0) {
        syncTileLayerStructure(newTile);
    }
    
    return newTile;
}

void Document::syncTileLayerStructure(Page* tile) const
{
    if (!tile) return;
    
    // Get the origin tile (0,0) as the source of truth
    TileCoord originCoord(0, 0);
    auto originIt = m_tiles.find(originCoord);
    if (originIt == m_tiles.end()) {
        // Origin tile not loaded - nothing to sync with
        return;
    }
    
    Page* origin = originIt->second.get();
    if (!origin) return;
    
    int originLayerCount = origin->layerCount();
    int tileLayerCount = tile->layerCount();
    
    // Add layers if tile has fewer
    while (tile->layerCount() < originLayerCount) {
        int idx = tile->layerCount();
        VectorLayer* srcLayer = origin->layer(idx);
        if (srcLayer) {
            VectorLayer* newLayer = tile->addLayer(srcLayer->name);
            if (newLayer) {
                newLayer->id = srcLayer->id;  // Same ID for consistency
                newLayer->visible = srcLayer->visible;
                newLayer->opacity = srcLayer->opacity;
                newLayer->locked = srcLayer->locked;
            }
        } else {
            tile->addLayer(QString("Layer %1").arg(idx + 1));
        }
    }
    
    // Remove layers if tile has more (shouldn't normally happen, but handle it)
    while (tile->layerCount() > originLayerCount && tile->layerCount() > 1) {
        tile->removeLayer(tile->layerCount() - 1);
    }
    
    // Sync layer properties (visibility, name, etc.) but NOT strokes
    for (int i = 0; i < qMin(tile->layerCount(), originLayerCount); ++i) {
        VectorLayer* srcLayer = origin->layer(i);
        VectorLayer* dstLayer = tile->layer(i);
        if (srcLayer && dstLayer) {
            dstLayer->name = srcLayer->name;
            dstLayer->visible = srcLayer->visible;
            dstLayer->opacity = srcLayer->opacity;
            dstLayer->locked = srcLayer->locked;
            // Don't sync id or strokes - those are tile-specific
        }
    }
    
    // Sync active layer index
    tile->activeLayerIndex = origin->activeLayerIndex;
}

QVector<Document::TileCoord> Document::tilesInRect(QRectF docRect) const
{
    QVector<TileCoord> result;
    
    // Calculate tile range
    int minTx = static_cast<int>(std::floor(docRect.left() / EDGELESS_TILE_SIZE));
    int maxTx = static_cast<int>(std::floor(docRect.right() / EDGELESS_TILE_SIZE));
    int minTy = static_cast<int>(std::floor(docRect.top() / EDGELESS_TILE_SIZE));
    int maxTy = static_cast<int>(std::floor(docRect.bottom() / EDGELESS_TILE_SIZE));
    
    // Return all coordinates in range (even if tiles don't exist yet)
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            result.append({tx, ty});
        }
    }
    
    return result;
}

QVector<Document::TileCoord> Document::allTileCoords() const
{
    QVector<TileCoord> result;
    result.reserve(static_cast<int>(m_tiles.size()));
    
    for (const auto& pair : m_tiles) {
        result.append(pair.first);
    }
    
    return result;
}

void Document::removeTileIfEmpty(int tx, int ty)
{
    TileCoord coord(tx, ty);
    auto it = m_tiles.find(coord);
    
    if (it == m_tiles.end()) {
        return;  // Tile doesn't exist in memory
    }
    
    Page* tile = it->second.get();
    
    // Use Page::hasContent() to check if tile has any strokes or objects
    if (!tile->hasContent()) {
        // Remove from memory
        m_tiles.erase(it);
        
        // Remove from dirty tracking (don't need to save an empty tile)
        m_dirtyTiles.erase(coord);
        
        // Track for deletion from disk on next saveBundle()
        // If tile was in m_tileIndex, it exists on disk and needs deletion
        if (m_tileIndex.count(coord) > 0) {
            m_deletedTiles.insert(coord);
            m_tileIndex.erase(coord);
        }
        
        markModified();
        
#ifdef QT_DEBUG
        qDebug() << "Document: Removed empty tile at (" << tx << "," << ty << ") remaining tiles:" << m_tiles.size();
#endif
    }
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
        doc->defaultGridSpacing = obj["background_density"].toInt(32);
        doc->defaultLineSpacing = obj["background_density"].toInt(32);
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
    
    defaultGridSpacing = obj["grid_spacing"].toInt(32);
    defaultLineSpacing = obj["line_spacing"].toInt(32);
    
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

// =============================================================================
// Tile Persistence (Phase E5)
// =============================================================================

QVector<Document::TileCoord> Document::allLoadedTileCoords() const
{
    QVector<TileCoord> coords;
    coords.reserve(static_cast<int>(m_tiles.size()));
    for (const auto& pair : m_tiles) {
        coords.append(pair.first);
    }
    return coords;
}

void Document::markTileDirty(TileCoord coord)
{
    m_dirtyTiles.insert(coord);
    markModified();
}

bool Document::saveTile(TileCoord coord)
{
    if (m_bundlePath.isEmpty()) {
        qWarning() << "Cannot save tile: bundle path not set";
        return false;
    }
    
    auto it = m_tiles.find(coord);
    if (it == m_tiles.end()) {
        qWarning() << "Cannot save tile: not loaded in memory" << coord.first << coord.second;
        return false;
    }
    
    // Ensure tiles directory exists
    QString tilesDir = m_bundlePath + "/tiles";
    QDir().mkpath(tilesDir);
    
    // Build tile file path
    QString tilePath = tilesDir + "/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save tile: failed to open file" << tilePath;
        return false;
    }
    
    QJsonDocument jsonDoc(it->second->toJson());
    file.write(jsonDoc.toJson(QJsonDocument::Compact));
    file.close();
    
    // Update state
    m_dirtyTiles.erase(coord);
    m_tileIndex.insert(coord);
    
#ifdef QT_DEBUG
    qDebug() << "Saved tile" << coord.first << "," << coord.second << "to" << tilePath;
#endif
    
    return true;
}

bool Document::loadTileFromDisk(TileCoord coord) const
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    QString tilePath = m_bundlePath + "/tiles/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load tile: file not found" << tilePath;
        // CR-6: Remove from index to prevent repeated failed loads
        m_tileIndex.erase(coord);
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Cannot load tile: JSON parse error" << parseError.errorString();
        m_tileIndex.erase(coord);  // CR-6: Remove from index
        return false;
    }
    
    auto tile = Page::fromJson(jsonDoc.object());
    if (!tile) {
        qWarning() << "Cannot load tile: Page::fromJson failed";
        m_tileIndex.erase(coord);  // CR-6: Remove from index
        return false;
    }
    
    m_tiles[coord] = std::move(tile);
    
#ifdef QT_DEBUG
    qDebug() << "Loaded tile" << coord.first << "," << coord.second << "from disk";
#endif
    
    return true;
}

void Document::evictTile(TileCoord coord)
{
    auto it = m_tiles.find(coord);
    if (it == m_tiles.end()) {
        return;  // Not loaded, nothing to evict
    }
    
    // Save if dirty
    if (m_dirtyTiles.count(coord) > 0) {
        if (!saveTile(coord)) {
            qWarning() << "Failed to save tile before eviction" << coord.first << coord.second;
            // Continue with eviction anyway to free memory
        }
    }
    
    // Remove from memory
    m_tiles.erase(it);
    
#ifdef QT_DEBUG
    qDebug() << "Evicted tile" << coord.first << "," << coord.second << "from memory";
#endif
}

bool Document::saveBundle(const QString& path)
{
    // Save old bundle path before overwriting - needed for copying evicted tiles
    QString oldBundlePath = m_bundlePath;
    m_bundlePath = path;
    
    // Create directory structure
    if (!QDir().mkpath(path + "/tiles")) {
        qWarning() << "Cannot create bundle directory" << path;
        return false;
    }
    
    // Build manifest
    QJsonObject manifest = toJson();  // Metadata only
    
    // Build tile index (union of disk tiles and memory tiles)
    std::set<TileCoord> allTileCoords = m_tileIndex;
    for (const auto& pair : m_tiles) {
        allTileCoords.insert(pair.first);
    }
    
    QJsonArray tileIndexArray;
    for (const auto& coord : allTileCoords) {
        tileIndexArray.append(QString("%1,%2").arg(coord.first).arg(coord.second));
    }
    manifest["tile_index"] = tileIndexArray;
    manifest["tile_size"] = EDGELESS_TILE_SIZE;
    
    // Write manifest
    QString manifestPath = path + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write manifest" << manifestPath;
        return false;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();
    
    // ========== HANDLE TILES WHEN SAVING TO NEW LOCATION ==========
    bool savingToNewLocation = !oldBundlePath.isEmpty() && oldBundlePath != path;
    
    if (savingToNewLocation) {
        // Copy evicted tiles from old bundle (tiles on disk but not in memory)
        for (const auto& coord : m_tileIndex) {
            // Skip tiles that are in memory - they'll be saved below
            if (m_tiles.find(coord) != m_tiles.end()) {
                continue;
            }
            
            QString tileFileName = QString("%1,%2.json").arg(coord.first).arg(coord.second);
            QString oldTilePath = oldBundlePath + "/tiles/" + tileFileName;
            QString newTilePath = path + "/tiles/" + tileFileName;
            
            // Copy tile file from old location to new location
            if (QFile::exists(oldTilePath)) {
                if (QFile::copy(oldTilePath, newTilePath)) {
#ifdef QT_DEBUG
                    qDebug() << "Copied evicted tile" << coord.first << "," << coord.second;
#endif
                } else {
                    qWarning() << "Failed to copy tile" << oldTilePath << "to" << newTilePath;
                }
            }
        }
    }
    // =============================================================
    
    // Save tiles in memory
    for (const auto& pair : m_tiles) {
        TileCoord coord = pair.first;
        // When saving to new location: save ALL in-memory tiles
        // When saving to same location: only save dirty/new tiles
        bool needsSave = savingToNewLocation || 
                         m_dirtyTiles.count(coord) > 0 || 
                         m_tileIndex.count(coord) == 0;
        if (needsSave) {
            saveTile(coord);
        }
    }
    
    // ========== DELETE EMPTY TILES FROM DISK ==========
    // Tiles that were erased empty are tracked in m_deletedTiles.
    // Delete their files from disk now.
    for (const auto& coord : m_deletedTiles) {
        QString tileFileName = QString("%1,%2.json").arg(coord.first).arg(coord.second);
        QString tilePath = path + "/tiles/" + tileFileName;
        if (QFile::exists(tilePath)) {
            if (QFile::remove(tilePath)) {
#ifdef QT_DEBUG
                qDebug() << "Deleted empty tile file:" << tileFileName;
#endif
            } else {
                qWarning() << "Failed to delete empty tile file:" << tilePath;
            }
        }
    }
    m_deletedTiles.clear();
    // ==================================================
    
    m_dirtyTiles.clear();
    m_tileIndex = allTileCoords;
    m_lazyLoadEnabled = true;
    
    clearModified();
    
#ifdef QT_DEBUG
    qDebug() << "Saved bundle to" << path << "with" << allTileCoords.size() << "tiles";
#endif
    
    return true;
}

std::unique_ptr<Document> Document::loadBundle(const QString& path)
{
    QString manifestPath = path + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open bundle manifest" << manifestPath;
        return nullptr;
    }
    
    QByteArray data = manifestFile.readAll();
    manifestFile.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Bundle manifest parse error:" << parseError.errorString();
        return nullptr;
    }
    
    QJsonObject obj = jsonDoc.object();
    
    // Load document metadata
    auto doc = Document::fromJson(obj);
    if (!doc) {
        qWarning() << "Failed to parse document metadata";
        return nullptr;
    }
    
    // Set bundle path and enable lazy loading
    doc->m_bundlePath = path;
    doc->m_lazyLoadEnabled = true;
    
    // Parse tile index (just coordinates, no actual loading!)
    QJsonArray tileIndexArray = obj["tile_index"].toArray();
    for (const auto& val : tileIndexArray) {
        QStringList parts = val.toString().split(',');
        if (parts.size() == 2) {
            bool okX, okY;
            int tx = parts[0].toInt(&okX);
            int ty = parts[1].toInt(&okY);
            if (okX && okY) {
                doc->m_tileIndex.insert({tx, ty});
            }
        }
    }
    
#ifdef QT_DEBUG
    qDebug() << "Loaded bundle from" << path << "with" << doc->m_tileIndex.size() << "tiles indexed";
#endif
    
    return doc;
}
