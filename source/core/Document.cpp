// ============================================================================
// Document - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3, 1.2.4, 1.2.5)
// ============================================================================

#include "Document.h"
#include <QCryptographicHash>
#include <cmath>
#include <algorithm>  // Phase 5.4: for std::sort, std::greater in merge

// ===== Constructor & Destructor =====

Document::Document()
{
    // Generate unique ID
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set timestamps
    created = QDateTime::currentDateTime();
    lastModified = created;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document CREATED:" << this << "id=" << id.left(8);
#endif
}

Document::~Document()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document DESTROYED:" << this << "id=" << id.left(8) 
             << "pages=" << m_pageOrder.size() << "tiles=" << m_tiles.size();
#endif
    // Note: m_loadedPages, m_tiles, and m_pdfProvider are unique_ptr, auto-cleaned
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
        
        // Phase 5.6: Create default layer in manifest
        LayerDefinition defaultLayer;
        defaultLayer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        defaultLayer.name = "Layer 1";
        doc->m_edgelessLayers.push_back(defaultLayer);
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

QString Document::computePdfHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    
    // Read first 1MB for hashing (fast even for large PDFs)
    constexpr qint64 HASH_CHUNK_SIZE = 1024 * 1024; // 1 MB
    QByteArray data = file.read(HASH_CHUNK_SIZE);
    file.close();
    
    if (data.isEmpty()) {
        return QString();
    }
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    QByteArray result = hash.result();
    
    return QStringLiteral("sha256:") + result.toHex();
}

qint64 Document::getPdfFileSize(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return -1;
    }
    return info.size();
}

bool Document::verifyPdfHash(const QString& path) const
{
    // Legacy document without hash - can't verify, assume OK
    if (m_pdfHash.isEmpty()) {
        return true;
    }
    
    // Compute hash of the candidate file
    QString candidateHash = computePdfHash(path);
    if (candidateHash.isEmpty()) {
        return false; // Can't read file
    }
    
    return (candidateHash == m_pdfHash);
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
    
    // Compute and store hash if not already set (first load or legacy document)
    if (m_pdfHash.isEmpty()) {
        m_pdfHash = computePdfHash(path);
        m_pdfSize = getPdfFileSize(path);
    }
    
    return true;
}

bool Document::relinkPdf(const QString& newPath)
{
    if (loadPdf(newPath)) {
        // Always update hash for relinked PDF (it may be a different file)
        m_pdfHash = computePdfHash(newPath);
        m_pdfSize = getPdfFileSize(newPath);
        
        // Clear the relink flag since we successfully relinked
        m_needsPdfRelink = false;
        
        // Update relative path if we know the bundle location
        if (!m_bundlePath.isEmpty()) {
            m_pdfRelativePath = QDir(m_bundlePath).relativeFilePath(newPath);
        }
        
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
    m_pdfRelativePath.clear();  // Phase SHARE: Clear relative path too
    m_needsPdfRelink = false;   // No PDF = no need to relink
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

int Document::notebookPageIndexForPdfPage(int pdfPageIndex) const
{
    // Use m_pagePdfIndex which maps UUID → PDF page index
    // We need to find the UUID with matching PDF page, then use pageIndexByUuid() for O(1) lookup
    for (const auto& [uuid, pdfIdx] : m_pagePdfIndex) {
        if (pdfIdx == pdfPageIndex) {
            // Found the UUID, use cached lookup for O(1) instead of indexOf() O(n)
            return pageIndexByUuid(uuid);
        }
    }
    return -1;  // Not found (PDF page not in notebook, or non-PDF document)
}

int Document::pdfPageIndexForNotebookPage(int notebookPageIndex) const
{
    // Bounds check
    if (notebookPageIndex < 0 || notebookPageIndex >= m_pageOrder.size()) {
        return -1;
    }
    
    // Get the UUID at this notebook page index
    QString uuid = m_pageOrder[notebookPageIndex];
    
    // Look up the PDF page index for this UUID
    auto it = m_pagePdfIndex.find(uuid);
    if (it != m_pagePdfIndex.end()) {
        return it->second;
    }
    
    return -1;  // Not a PDF page (blank or custom background)
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
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return nullptr;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Check if already loaded
    auto it = m_loadedPages.find(uuid);
    if (it != m_loadedPages.end()) {
        return it->second.get();
    }
    
    // Load on demand
    if (!loadPageFromDisk(index)) {
        return nullptr;
    }
    
    // Use find() instead of [] to avoid inserting nullptr if something went wrong
    // (defensive programming - loadPageFromDisk should have inserted it)
    it = m_loadedPages.find(uuid);
    return it != m_loadedPages.end() ? it->second.get() : nullptr;
}

const Page* Document::page(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return nullptr;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Check if already loaded
    auto it = m_loadedPages.find(uuid);
    if (it != m_loadedPages.end()) {
        return it->second.get();
    }
    
    // Load on demand
    if (!loadPageFromDisk(index)) {
        return nullptr;
    }
    
    // Use find() instead of at() to avoid potential std::out_of_range exception
    // (defensive programming - loadPageFromDisk should have inserted it)
    it = m_loadedPages.find(uuid);
    return it != m_loadedPages.end() ? it->second.get() : nullptr;
}

// ===== Paged Mode Lazy Loading Accessors (Phase O1.7) =====

bool Document::isPageLoaded(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    QString uuid = m_pageOrder[index];
    return m_loadedPages.find(uuid) != m_loadedPages.end();
}

QVector<int> Document::loadedPageIndices() const
{
    QVector<int> result;
    result.reserve(static_cast<int>(m_loadedPages.size()));
    
    // Iterate through loaded pages and use cached UUID→index lookup
    // This is O(loaded) after cache is built (vs O(loaded * pageCount) before)
    for (const auto& [uuid, page] : m_loadedPages) {
        int idx = pageIndexByUuid(uuid);  // O(1) cached lookup
        if (idx >= 0) {
            result.append(idx);
        }
    }
    return result;
}

QString Document::pageUuidAt(int index) const
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return QString();
    }
    return m_pageOrder[index];
}

QSizeF Document::pageSizeAt(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return QSizeF();
    }
    
    // Use cached metadata (avoids loading the full page)
    QString uuid = m_pageOrder[index];
    auto it = m_pageMetadata.find(uuid);
    if (it != m_pageMetadata.end()) {
        return it->second;
    }
    
    // Fallback: load the page and get its size
    const Page* p = page(index);
    return p ? p->size : QSizeF();
}

bool Document::loadPageFromDisk(int index) const
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    
    QFile file(pagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // File doesn't exist - check if we can synthesize a pristine PDF page
        auto pdfIt = m_pagePdfIndex.find(uuid);
        if (pdfIt != m_pagePdfIndex.end()) {
            // Synthesize pristine PDF page from manifest metadata
            auto page = std::make_unique<Page>();
            page->uuid = uuid;
            page->pageIndex = index;
            page->backgroundType = Page::BackgroundType::PDF;
            page->pdfPageNumber = pdfIt->second;
            
            // Get size from metadata
            auto sizeIt = m_pageMetadata.find(uuid);
            if (sizeIt != m_pageMetadata.end()) {
                page->size = sizeIt->second;
            } else {
                // Fallback to PDF page size if available
                if (isPdfLoaded() && pdfIt->second >= 0 && pdfIt->second < pdfPageCount()) {
                    QSizeF pdfSize = pdfPageSize(pdfIt->second);
                    qreal scale = 96.0 / 72.0;  // PDF points to 96 dpi
                    page->size = QSizeF(pdfSize.width() * scale, pdfSize.height() * scale);
                }
            }
            
            // Apply document defaults for colors/spacing
            page->backgroundColor = defaultBackgroundColor;
            page->gridColor = defaultGridColor;
            page->gridSpacing = defaultGridSpacing;
            page->lineSpacing = defaultLineSpacing;
            
            m_loadedPages[uuid] = std::move(page);
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Synthesized pristine PDF page" << index << "(" << uuid.left(8) << ")";
#endif
            return true;
        }
        
        // Not a PDF page and file doesn't exist - actual error
        qWarning() << "Cannot load page: file not found" << pagePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Cannot load page: JSON parse error" << parseError.errorString();
        return false;
    }
    
    auto page = Page::fromJson(jsonDoc.object());
    if (!page) {
        qWarning() << "Cannot load page: Page::fromJson failed";
        return false;
    }
    
    // Phase O2 (BF.3): Load image objects from assets folder.
    // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
    // We must call loadImages() to load image files into memory for rendering.
    int imagesLoaded = page->loadImages(m_bundlePath);
    if (imagesLoaded > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "loadPageFromDisk: Loaded" << imagesLoaded << "images for page" << index;
        #endif
    }
    
    // Phase O1.5: Update max object extent from loaded objects
    for (const auto& object : page->objects) {
        int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
        if (extent > m_maxObjectExtent) {
            m_maxObjectExtent = extent;
        }
    }
    
    m_loadedPages[uuid] = std::move(page);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Loaded page" << index << "(" << uuid.left(8) << ") from disk";
#endif
    
    return true;
}

bool Document::savePage(int index)
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) {
        return false;  // Not loaded, nothing to save
    }
    
    // Ensure pages directory exists
    QDir().mkpath(m_bundlePath + "/pages");
    
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    QFile file(pagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save page:" << pagePath;
        return false;
    }
    
    QJsonDocument jsonDoc(it->second->toJson());
    file.write(jsonDoc.toJson(QJsonDocument::Compact));
    file.close();
    
    // Clear dirty flag
    m_dirtyPages.erase(uuid);
    
    // Update metadata
    m_pageMetadata[uuid] = it->second->size;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Saved page" << index << "(" << uuid.left(8) << ") to disk";
#endif
    
    return true;
}

void Document::evictPage(int index)
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return;
    }
    
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) {
        return;  // Not loaded, nothing to evict
    }
    
    // Save if dirty
    if (m_dirtyPages.count(uuid) > 0) {
        if (!savePage(index)) {
            qWarning() << "Failed to save page before eviction" << index;
            // Continue with eviction anyway to free memory
        }
    }
    
    // Remove from memory
    m_loadedPages.erase(it);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Evicted page" << index << "(" << uuid.left(8) << ") from memory";
#endif
}

void Document::markPageDirty(int index)
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return;
    }
    QString uuid = m_pageOrder[index];
    m_dirtyPages.insert(uuid);
    markModified();
}

bool Document::isPageDirty(int index) const
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    QString uuid = m_pageOrder[index];
    return m_dirtyPages.count(uuid) > 0;
}

// =========================================================================
// UUID→Index Cache (Phase C.0.2)
// =========================================================================

void Document::rebuildUuidCache() const
{
    m_uuidToIndexCache.clear();
    
    // Build cache from m_pageOrder (no disk I/O needed)
    for (int i = 0; i < m_pageOrder.size(); i++) {
        const QString& uuid = m_pageOrder[i];
        if (!uuid.isEmpty()) {
            m_uuidToIndexCache[uuid] = i;
        }
    }
    
    m_uuidCacheDirty = false;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Rebuilt UUID cache with" << m_uuidToIndexCache.size() << "entries";
#endif
}

int Document::pageIndexByUuid(const QString& uuid) const
{
    if (uuid.isEmpty()) {
        return -1;
    }
    
    if (m_uuidCacheDirty) {
        rebuildUuidCache();  // O(n) but only once per page change
    }
    
    return m_uuidToIndexCache.value(uuid, -1);  // O(1)
}

void Document::invalidateUuidCache()
{
    m_uuidCacheDirty = true;
}

Page* Document::addPage()
{
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    
    // Use page's own UUID (generated in Page constructor)
    QString uuid = newPage->uuid;
    
    // Add to page order and metadata
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    
    // Store in loaded pages
    m_loadedPages[uuid] = std::move(newPage);
    
    // Mark as dirty
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();
    
    markModified();
    return pagePtr;
}

Page* Document::insertPage(int index)
{
    // Allow inserting at the end (index == size)
    if (index < 0 || index > m_pageOrder.size()) {
        return nullptr;
    }
    
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    
    // Use page's own UUID (generated in Page constructor)
    QString uuid = newPage->uuid;
    
    // Insert into page order
    m_pageOrder.insert(index, uuid);
    m_pageMetadata[uuid] = newPage->size;
    
    // Store in loaded pages
    m_loadedPages[uuid] = std::move(newPage);
    
    // Mark as dirty
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();
    
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
    
    // Use lazy loading mode from the start
    QString uuid = newPage->uuid;
    Page* pagePtr = newPage.get();
    
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    m_pagePdfIndex[uuid] = pdfPageIndex;  // Track PDF page mapping
    m_loadedPages[uuid] = std::move(newPage);
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();
    
    markModified();
    return pagePtr;
}

bool Document::removePage(int index)
{
    // Cannot remove if index invalid
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    // Cannot remove the last page
    if (m_pageOrder.size() <= 1) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Remove from page order
    m_pageOrder.removeAt(index);
    
    // Evict from memory if loaded
    m_loadedPages.erase(uuid);
    
    // Remove from dirty tracking
    m_dirtyPages.erase(uuid);
    
    // Remove metadata
    m_pageMetadata.erase(uuid);
    
    // Remove PDF page index tracking
    m_pagePdfIndex.erase(uuid);
    
    // Track for deletion on next save
    m_deletedPages.insert(uuid);
    
    invalidateUuidCache();
    markModified();
    return true;
}

bool Document::movePage(int from, int to)
{
    int count = m_pageOrder.size();
    
    // Validate indices
    if (from < 0 || from >= count || to < 0 || to >= count) {
        return false;
    }
    
    // No-op if same position
    if (from == to) {
        return true;
    }
    
    // Just reorder the UUID list - no file changes needed!
    QString uuid = m_pageOrder[from];
    m_pageOrder.removeAt(from);
    m_pageOrder.insert(to, uuid);
    
    invalidateUuidCache();
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
    // Check if we already have pages
    if (!m_pageOrder.isEmpty()) {
        return;
    }
    
    auto newPage = createDefaultPage();
    
    // For edgeless mode, mark the page as unbounded
    if (mode == Mode::Edgeless) {
        // Edgeless pages have no fixed size (effectively infinite)
        // We use a large default but it can extend beyond
        newPage->size = QSizeF(4096, 4096);
    }
    
    // Use lazy loading mode from the start
    QString uuid = newPage->uuid;
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    m_loadedPages[uuid] = std::move(newPage);
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();
}

void Document::createPagesForPdf()
{
    // Clear existing pages (lazy loading structures)
    m_pageOrder.clear();
    m_pageMetadata.clear();
    m_pagePdfIndex.clear();
    m_loadedPages.clear();
    m_dirtyPages.clear();
    invalidateUuidCache();
    
    if (!isPdfLoaded()) {
        // No PDF loaded, create a single default page
        ensureMinimumPages();
        return;
    }
    
    // Create one page per PDF page
    int count = pdfPageCount();
    m_pageOrder.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        addPageForPdf(i);
    }
    
    // Ensure at least one page
    if (m_pageOrder.isEmpty()) {
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

void Document::loadAllEvictedTiles()
{
    // CR-L13: Load all tiles that exist on disk but aren't in memory.
    // This ensures destructive layer operations affect ALL tiles.
    
    if (!m_lazyLoadEnabled) {
        return;  // No evicted tiles if lazy loading isn't enabled
    }
    
    // Copy tile index since loadTileFromDisk modifies m_tiles
    std::set<TileCoord> tilesToLoad;
    for (const auto& coord : m_tileIndex) {
        if (m_tiles.find(coord) == m_tiles.end()) {
            tilesToLoad.insert(coord);
        }
    }
    
    if (!tilesToLoad.empty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "CR-L13: Loading" << tilesToLoad.size() << "evicted tiles for layer operation";
#endif
    }
    
    for (const auto& coord : tilesToLoad) {
        loadTileFromDisk(coord);
    }
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
            // Phase 5.6.5: No sync needed - loadTileFromDisk reconstructs layers from manifest
            return m_tiles.at(coord).get();
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
            // Phase 5.6.5: No sync needed - loadTileFromDisk reconstructs layers from manifest
            return m_tiles.at(coord).get();
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
    
    // Phase 5.6.6: Initialize tile layer structure from manifest
    if (isEdgeless() && !m_edgelessLayers.empty()) {
        tile->vectorLayers.clear();  // Clear default layer from Page constructor
        for (const auto& layerDef : m_edgelessLayers) {
            auto layer = std::make_unique<VectorLayer>(layerDef.name);
            layer->id = layerDef.id;
            layer->visible = layerDef.visible;
            layer->opacity = layerDef.opacity;
            layer->locked = layerDef.locked;
            tile->vectorLayers.push_back(std::move(layer));
        }
        tile->activeLayerIndex = m_edgelessActiveLayerIndex;
    }
    
    auto [insertIt, inserted] = m_tiles.emplace(coord, std::move(tile));
    
    // Mark new tile as dirty (needs saving)
    m_dirtyTiles.insert(coord);
    markModified();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document: Created tile at (" << tx << "," << ty << ") total tiles:" << m_tiles.size();
#endif
    
    return insertIt->second.get();
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
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Document: Removed empty tile at (" << tx << "," << ty << ") remaining tiles:" << m_tiles.size();
#endif
    }
}

// =========================================================================
// Object Extent Tracking (Phase O1.5)
// =========================================================================

void Document::updateMaxObjectExtent(const InsertedObject* obj)
{
    if (!obj) return;
    
    // Get the largest dimension of this object
    int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
    
    // Update maximum if this object is larger
    if (extent > m_maxObjectExtent) {
        m_maxObjectExtent = extent;
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Document: Updated max object extent to" << m_maxObjectExtent;
#endif
    }
}

void Document::recalculateMaxObjectExtent()
{
    int newMax = 0;
    
    if (isEdgeless()) {
        // Scan all loaded tiles
        for (const auto& pair : m_tiles) {
            Page* tile = pair.second.get();
            for (const auto& obj : tile->objects) {
                int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
                newMax = qMax(newMax, extent);
            }
        }
        
        // Note: Evicted tiles are not scanned. This is acceptable because:
        // - Evicted tiles will be loaded when viewport moves to them
        // - When loaded, their objects will update maxObjectExtent via addObject
        // - Worst case: margin is temporarily too small until tiles are loaded
    } else {
        // Scan loaded pages (lazy loading mode)
        for (const auto& pair : m_loadedPages) {
            Page* page = pair.second.get();
            for (const auto& obj : page->objects) {
                int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
                newMax = qMax(newMax, extent);
            }
        }
    }
    
    m_maxObjectExtent = newMax;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document: Recalculated max object extent =" << m_maxObjectExtent;
#endif
}

// =========================================================================
// Bookmarks (Task 1.2.6)
// =========================================================================

QVector<Document::Bookmark> Document::getBookmarks() const
{
    QVector<Bookmark> result;
    
    int count = pageCount();
    for (int i = 0; i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
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
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from 0 to fromPage
    for (int i = 0; i <= fromPage && i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
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
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from end down to fromPage
    for (int i = count - 1; i >= fromPage && i >= 0; --i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
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
    int result = 0;
    int count = pageCount();
    for (int i = 0; i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            ++result;
        }
    }
    return result;
}

// =========================================================================
// Serialization (Task 1.2.7)
// =========================================================================

QJsonObject Document::toJson() const
{
    QJsonObject obj;
    
    // Bundle format version (integer, for forward compatibility checks)
    obj["bundle_format_version"] = BUNDLE_FORMAT_VERSION;
    
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
    if (!m_pdfHash.isEmpty()) {
        obj["pdf_hash"] = m_pdfHash;
    }
    if (m_pdfSize > 0) {
        obj["pdf_size"] = m_pdfSize;
    }
    
    // State
    obj["last_accessed_page"] = lastAccessedPage;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document::toJson: lastAccessedPage =" << lastAccessedPage;
#endif
    
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
    
    // NOTE: format_version is no longer read - use bundle_format_version instead
    // Old files may have format_version but it's ignored for backward compatibility
    
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
    
    // PDF reference (don't load yet, just store paths)
    doc->m_pdfPath = obj["pdf_path"].toString();
    doc->m_pdfRelativePath = obj["pdf_relative_path"].toString();  // Phase SHARE
    doc->m_pdfHash = obj["pdf_hash"].toString();
    doc->m_pdfSize = obj["pdf_size"].toVariant().toLongLong();
    
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
    // Clear existing pages (lazy loading structures)
    m_pageOrder.clear();
    m_pageMetadata.clear();
    m_pagePdfIndex.clear();
    m_loadedPages.clear();
    m_dirtyPages.clear();
    invalidateUuidCache();
    
    // Phase O1.5: Reset max object extent when reloading pages
    m_maxObjectExtent = 0;
    
    m_pageOrder.reserve(pagesArray.size());
    
    int loadedCount = 0;
    
    for (const auto& val : pagesArray) {
        auto page = Page::fromJson(val.toObject());
        if (page) {
            // Phase O2 (BF.3): Load image objects from assets folder.
            // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
            // We must call loadImages() to load the image files into memory.
            if (!m_bundlePath.isEmpty()) {
                page->loadImages(m_bundlePath);
            }
            
            // Phase O1.5: Update max object extent from loaded objects
            for (const auto& object : page->objects) {
                int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
                if (extent > m_maxObjectExtent) {
                    m_maxObjectExtent = extent;
                }
            }
            
            // Use lazy loading structures
            QString uuid = page->uuid;
            m_pageOrder.append(uuid);
            m_pageMetadata[uuid] = page->size;
            if (page->backgroundType == Page::BackgroundType::PDF) {
                m_pagePdfIndex[uuid] = page->pdfPageNumber;
            }
            m_loadedPages[uuid] = std::move(page);
            m_dirtyPages.insert(uuid);  // Mark as dirty since loaded from JSON
            
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
    
    // Iterate pages in order
    for (const QString& uuid : m_pageOrder) {
        auto it = m_loadedPages.find(uuid);
        if (it != m_loadedPages.end()) {
            pagesArray.append(it->second->toJson());
        }
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
    
    // Only include page size for paged documents
    // Edgeless documents use tiles (1024x1024), not pages, so these fields are unused
    if (mode == Mode::Paged) {
        bg["page_width"] = defaultPageSize.width();
        bg["page_height"] = defaultPageSize.height();
    }
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
    
    // Phase 5.6.3: For edgeless mode, use compact format:
    // - layers: array of {id, strokes} (layer properties stored in manifest)
    // - objects: array of InsertedObjects (Phase O2)
    // - coord_x, coord_y: tile coordinates for debugging
    QJsonObject tileObj;
    Page* tile = it->second.get();
    
    if (isEdgeless()) {
        QJsonArray layersArray;
        for (int i = 0; i < tile->layerCount(); ++i) {
            VectorLayer* layer = tile->layer(i);
            if (layer && !layer->isEmpty()) {
                QJsonObject layerObj;
                layerObj["id"] = layer->id;
                
                QJsonArray strokesArray;
                for (const auto& stroke : layer->strokes()) {
                    strokesArray.append(stroke.toJson());
                }
                layerObj["strokes"] = strokesArray;
                
                layersArray.append(layerObj);
            }
        }
        tileObj["layers"] = layersArray;
        
        // Phase O2: Save objects to tile (BF.5)
        // Objects are stored in tile-local coordinates
        if (!tile->objects.empty()) {
            QJsonArray objectsArray;
            for (const auto& obj : tile->objects) {
                objectsArray.append(obj->toJson());
            }
            tileObj["objects"] = objectsArray;
        }
        
        // Store tile coordinate for debugging/verification
        tileObj["coord_x"] = coord.first;
        tileObj["coord_y"] = coord.second;
    } else {
        // Paged mode: use full Page serialization (legacy behavior)
        tileObj = tile->toJson();
    }
    
    QJsonDocument jsonDoc(tileObj);
    file.write(jsonDoc.toJson(QJsonDocument::Compact));
    file.close();
    
    // Update state
    m_dirtyTiles.erase(coord);
    m_tileIndex.insert(coord);
    
#ifdef SPEEDYNOTE_DEBUG
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
    
    QJsonObject obj = jsonDoc.object();
    
    // Phase 5.6.4: For edgeless mode, reconstruct layers from manifest
    // Tile files only contain {id, strokes} per layer, not full layer properties.
    // We check for coord_x/coord_y as markers of the new compact format.
    bool isNewFormat = obj.contains("coord_x") && obj.contains("coord_y");
    
    if (mode == Mode::Edgeless && isNewFormat && !m_edgelessLayers.empty()) {
        // New compact format: reconstruct full VectorLayers from manifest
        
        // Build map of layerId → strokes from tile file
        std::map<QString, QVector<VectorStroke>> strokesByLayerId;
        QJsonArray tileLayersArray = obj["layers"].toArray();
        for (const auto& val : tileLayersArray) {
            QJsonObject layerObj = val.toObject();
            QString layerId = layerObj["id"].toString();
            QVector<VectorStroke> strokes;
            for (const auto& strokeVal : layerObj["strokes"].toArray()) {
                strokes.append(VectorStroke::fromJson(strokeVal.toObject()));
            }
            strokesByLayerId[layerId] = strokes;
        }
        
        // Create tile with default page settings
        auto tile = std::make_unique<Page>();
        tile->size = QSizeF(EDGELESS_TILE_SIZE, EDGELESS_TILE_SIZE);
        tile->backgroundType = defaultBackgroundType;
        tile->backgroundColor = defaultBackgroundColor;
        tile->gridColor = defaultGridColor;
        tile->gridSpacing = defaultGridSpacing;
        tile->lineSpacing = defaultLineSpacing;
        
        // Clear default layer and reconstruct from manifest
        tile->vectorLayers.clear();
        for (const auto& layerDef : m_edgelessLayers) {
            auto layer = std::make_unique<VectorLayer>(layerDef.name);
            layer->id = layerDef.id;
            layer->visible = layerDef.visible;
            layer->opacity = layerDef.opacity;
            layer->locked = layerDef.locked;
            
            // Add strokes if this tile has any for this layer
            auto it = strokesByLayerId.find(layerDef.id);
            if (it != strokesByLayerId.end()) {
                for (const auto& stroke : it->second) {
                    layer->addStroke(stroke);
                }
            }
            
            tile->vectorLayers.push_back(std::move(layer));
        }
        
        tile->activeLayerIndex = m_edgelessActiveLayerIndex;
        
        // Phase O1.5: Load objects from tile file
        if (obj.contains("objects")) {
            QJsonArray objectsArray = obj["objects"].toArray();
            for (const auto& val : objectsArray) {
                auto object = InsertedObject::fromJson(val.toObject());
                if (object) {
                    // Update max object extent
                    int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
                    if (extent > m_maxObjectExtent) {
                        m_maxObjectExtent = extent;
                    }
                    tile->objects.push_back(std::move(object));
                }
            }
            // Rebuild affinity map after loading objects
            tile->rebuildAffinityMap();
            
            // Phase O2 (BF.3): Load image objects from assets folder.
            // InsertedObject::fromJson() only sets imagePath; it does NOT load the pixmap.
            tile->loadImages(m_bundlePath);
        }
        
        m_tiles[coord] = std::move(tile);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded tile" << coord.first << "," << coord.second 
                 << "from disk (manifest reconstruction)";
#endif
    } else {
        // Legacy format or paged mode: use full Page deserialization
        auto tile = Page::fromJson(obj);
        if (!tile) {
            qWarning() << "Cannot load tile: Page::fromJson failed";
            m_tileIndex.erase(coord);  // CR-6: Remove from index
            return false;
        }
        
        // Phase O2 (BF.3): Load image objects from assets folder.
        // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
        tile->loadImages(m_bundlePath);
        
        // Phase O1.5: Update max object extent from loaded objects
        for (const auto& object : tile->objects) {
            int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
            if (extent > m_maxObjectExtent) {
                m_maxObjectExtent = extent;
            }
        }
        
        m_tiles[coord] = std::move(tile);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded tile" << coord.first << "," << coord.second << "from disk (legacy)";
#endif
    }
    
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
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Evicted tile" << coord.first << "," << coord.second << "from memory";
#endif
}

int Document::saveUnsavedImages(const QString& bundlePath)
{
    if (bundlePath.isEmpty()) {
        return 0;
    }
    
    int savedCount = 0;
    
    // CR-O2: Use virtual saveAssets() instead of type-specific code
    // This allows future object types with assets (audio, video, etc.) to work automatically.
    // 
    // Note: saveAssets() handles already-saved assets gracefully (deduplication check).
    // We call it for all objects with loaded assets - the virtual method no-ops for
    // objects without external assets (base class returns true immediately).
    auto processPage = [&](Page* page) {
        if (!page) return;
        
        for (auto& obj : page->objects) {
            // Only process objects with loaded assets that might need saving
            // isAssetLoaded() returns false for objects without external assets (base class)
            // For ImageObject, it returns !cachedPixmap.isNull()
            if (obj->isAssetLoaded()) {
                // saveAssets() handles deduplication internally - safe to call even
                // if asset was previously saved (just updates imagePath if needed)
                if (!obj->saveAssets(bundlePath)) {
                    qWarning() << "saveUnsavedImages: Failed to save asset for" 
                               << obj->type() << "object" << obj->id;
                    } else {
                    savedCount++;
                }
            }
        }
    };
    
    if (mode == Mode::Edgeless) {
        // Process all loaded tiles
        for (auto& pair : m_tiles) {
            processPage(pair.second.get());
        }
    } else {
        // Paged mode: process loaded pages
        for (auto& pair : m_loadedPages) {
            processPage(pair.second.get());
        }
    }
    
    if (savedCount > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "saveUnsavedImages: Saved" << savedCount << "images to assets";
        #endif
    }
    
    return savedCount;
}

// =========================================================================
// Asset Cleanup (Phase C.0.4)
// =========================================================================

void Document::cleanupOrphanedAssets()
{
    if (m_bundlePath.isEmpty()) {
        return;  // Unsaved document, nothing on disk
    }
    
    QString assetsPath = m_bundlePath + "/assets/images";
    QDir assetsDir(assetsPath);
    if (!assetsDir.exists()) {
        return;  // No assets folder
    }
    
    // Step 1: Collect all referenced image filenames
    QSet<QString> referencedFiles;
    
    // Helper to scan a page's objects for image references
    auto collectFromPage = [&](Page* p) {
        if (!p) return;
        
        for (const auto& obj : p->objects) {
            if (auto* img = dynamic_cast<ImageObject*>(obj.get())) {
                if (!img->imagePath.isEmpty()) {
                    referencedFiles.insert(img->imagePath);
                }
            }
        }
    };
    
    // Step 2: Scan all pages/tiles based on mode
    if (isEdgeless()) {
        // Edgeless mode: scan all loaded tiles
        for (const auto& coord : allLoadedTileCoords()) {
            Page* tile = getTile(coord.first, coord.second);
            collectFromPage(tile);
        }
        
        // Note: Evicted tiles are NOT scanned. If an image is only referenced
        // by an evicted tile, we don't delete it. This is safe but may leave
        // some orphans until the tile is loaded and document is closed again.
    } else {
        // Paged mode: scan all pages
        // 
        // PERF NOTE: For lazy-loaded mode, page(i) loads pages on demand.
        // This means cleanup will load ALL pages into memory for large documents.
        // This is intentional: we need to scan all pages to avoid deleting
        // images that are still referenced by unloaded pages.
        // 
        // This only runs on document close, so the memory usage is temporary.
        // Future optimization: track image references in manifest to avoid
        // loading all pages.
        for (int i = 0; i < pageCount(); i++) {
            Page* p = page(i);
            collectFromPage(p);
        }
    }
    
    // Step 3: List files on disk and delete orphans
    QStringList filesOnDisk = assetsDir.entryList(QDir::Files);
    int deletedCount = 0;
    
    for (const QString& filename : filesOnDisk) {
        if (!referencedFiles.contains(filename)) {
            QString fullPath = assetsPath + "/" + filename;
            if (QFile::remove(fullPath)) {
                deletedCount++;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cleaned up orphaned asset:" << filename;
#endif
            }
        }
    }
    
#ifdef SPEEDYNOTE_DEBUG
    if (deletedCount > 0) {
        qDebug() << "Cleaned up" << deletedCount << "orphaned assets";
    }
#endif
}

// ===== Markdown Notes (Phase M.1) =====

QString Document::notesPath() const
{
    QString assets = assetsPath();
    if (assets.isEmpty()) {
        return QString();
    }
    
    QString notes = assets + "/notes";
    QDir().mkpath(notes);
    return notes;
}

bool Document::deleteNoteFile(const QString& noteId)
{
    QString notes = notesPath();
    if (notes.isEmpty()) {
        return false;
    }
    
    QString filePath = notes + "/" + noteId + ".md";
    if (QFile::exists(filePath)) {
        return QFile::remove(filePath);
    }
    return true;  // File didn't exist, consider it successfully "deleted"
}

bool Document::saveBundle(const QString& path)
{
    // Save old bundle path before overwriting - needed for copying evicted tiles/pages
    QString oldBundlePath = m_bundlePath;
    m_bundlePath = path;
    
    // Phase P.1.1: Write .snb_marker file to identify this as a SpeedyNote bundle
    QString markerPath = path + "/.snb_marker";
    if (!QFile::exists(markerPath)) {
        QFile markerFile(markerPath);
        if (markerFile.open(QIODevice::WriteOnly)) {
            // Empty file - existence is enough to identify the bundle
            markerFile.close();
        }
    }
    
    // Phase O1.6: Create assets directory for object files (images, etc.)
    if (!QDir().mkpath(path + "/assets/images")) {
        qWarning() << "Cannot create assets/images directory" << path;
        return false;
    }
    
    // Phase O2 (BF.2): Save any unsaved images to assets folder BEFORE saving page JSON.
    // 
    // This is critical for images pasted into a NEW document before first save:
    // - When paste happens, bundlePath is empty, so saveToAssets() is skipped
    // - The image exists only as cachedPixmap with imagePath = ""
    // - Here we finally have a bundle path, so we can save images and set imagePath
    // - Then the serialized page JSON will have the correct imagePath reference
    saveUnsavedImages(path);
    
    // Build manifest
    QJsonObject manifest = toJson();  // Metadata only
    
    // For edgeless mode: track all tile coordinates (shared between blocks)
    std::set<TileCoord> allTileCoords;
    
    // ========== MODE-SPECIFIC SAVE ==========
    if (mode == Mode::Edgeless) {
        // Create tiles directory
        if (!QDir().mkpath(path + "/tiles")) {
            #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cannot create tiles directory" << path;
            #endif
            return false;
        }
        
        // Build tile index (union of disk tiles and memory tiles)
        allTileCoords = m_tileIndex;
        for (const auto& pair : m_tiles) {
            allTileCoords.insert(pair.first);
        }
        
        QJsonArray tileIndexArray;
        for (const auto& coord : allTileCoords) {
            tileIndexArray.append(QString("%1,%2").arg(coord.first).arg(coord.second));
        }
        manifest["tile_index"] = tileIndexArray;
        manifest["tile_size"] = EDGELESS_TILE_SIZE;
        
        // Phase 5.6: Write layer definitions to manifest
        QJsonArray layersArray;
        for (const auto& layerDef : m_edgelessLayers) {
            layersArray.append(layerDef.toJson());
        }
        manifest["layers"] = layersArray;
        manifest["active_layer_index"] = m_edgelessActiveLayerIndex;
        
        // Phase 4: Write position history to manifest
        QJsonObject lastPosObj;
        lastPosObj["x"] = m_edgelessLastPosition.x();
        lastPosObj["y"] = m_edgelessLastPosition.y();
        manifest["last_position"] = lastPosObj;
        
        QJsonArray posHistoryArray;
        for (const QPointF& pos : m_edgelessPositionHistory) {
            QJsonObject posObj;
            posObj["x"] = pos.x();
            posObj["y"] = pos.y();
            posHistoryArray.append(posObj);
        }
        manifest["position_history"] = posHistoryArray;
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "saveToBundle: Saved edgeless position" << m_edgelessLastPosition
                 << "with" << m_edgelessPositionHistory.size() << "history entries";
#endif
    } else {
        // ========== PAGED MODE SAVE (Phase O1.7.4) ==========
        if (!QDir().mkpath(path + "/pages")) {
            #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cannot create pages directory" << path;
            #endif
            return false;
        }
        
        // Write page_order to manifest
        QJsonArray pageOrderArray;
        for (const QString& uuid : m_pageOrder) {
            pageOrderArray.append(uuid);
        }
        manifest["page_order"] = pageOrderArray;
        
        // Write page_metadata to manifest (includes pdf_page for pristine PDF page synthesis)
        QJsonObject pageMetadataObj;
        for (const auto& [uuid, size] : m_pageMetadata) {
            QJsonObject metaObj;
            metaObj["width"] = size.width();
            metaObj["height"] = size.height();
            
            // Include PDF page index if this is a PDF page
            auto pdfIt = m_pagePdfIndex.find(uuid);
            if (pdfIt != m_pagePdfIndex.end()) {
                metaObj["pdf_page"] = pdfIt->second;
            }
            
            pageMetadataObj[uuid] = metaObj;
        }
        manifest["page_metadata"] = pageMetadataObj;
    }
    
    // Phase SHARE: Calculate and write pdf_relative_path for portability
    if (!m_pdfPath.isEmpty()) {
        QDir bundleDir(path);
        QString relativePath = bundleDir.relativeFilePath(m_pdfPath);
        manifest["pdf_relative_path"] = relativePath;
        
        // Also update the member variable for consistency
        m_pdfRelativePath = relativePath;
    }
    
    // Write manifest
    QString manifestPath = path + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write manifest" << manifestPath;
        return false;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();
    
    bool savingToNewLocation = !oldBundlePath.isEmpty() && oldBundlePath != path;
    
    // ========== COPY ASSETS WHEN SAVING TO NEW LOCATION (Phase O1.6 fix) ==========
    if (savingToNewLocation) {
        QString oldAssetsPath = oldBundlePath + "/assets/images";
        QString newAssetsPath = path + "/assets/images";
        
        QDir oldAssetsDir(oldAssetsPath);
        if (oldAssetsDir.exists()) {
            QStringList assetFiles = oldAssetsDir.entryList(QDir::Files);
            for (const QString& fileName : assetFiles) {
                QString oldFilePath = oldAssetsPath + "/" + fileName;
                QString newFilePath = newAssetsPath + "/" + fileName;
                
                // Skip if already exists (e.g., newly added images saved above)
                if (!QFile::exists(newFilePath)) {
                    if (QFile::copy(oldFilePath, newFilePath)) {
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied asset" << fileName;
#endif
                    } else {
                        qWarning() << "Failed to copy asset" << oldFilePath << "to" << newFilePath;
                    }
                }
            }
        }
    }
    
    // ========== MODE-SPECIFIC FILE HANDLING ==========
    if (mode == Mode::Edgeless) {
        // Clear manifest dirty flag after save
        m_edgelessManifestDirty = false;
        
        // ========== HANDLE TILES WHEN SAVING TO NEW LOCATION ==========
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
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied evicted tile" << coord.first << "," << coord.second;
#endif
                    } else {
                        qWarning() << "Failed to copy tile" << oldTilePath << "to" << newTilePath;
                    }
                }
            }
        }
        
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
        for (const auto& coord : m_deletedTiles) {
            QString tileFileName = QString("%1,%2.json").arg(coord.first).arg(coord.second);
            QString tilePath = path + "/tiles/" + tileFileName;
            if (QFile::exists(tilePath)) {
                if (QFile::remove(tilePath)) {
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted empty tile file:" << tileFileName;
#endif
                } else {
                    #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Failed to delete empty tile file:" << tilePath;
                    #endif
                }
            }
        }
        m_deletedTiles.clear();
        m_dirtyTiles.clear();
        m_tileIndex = allTileCoords;
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Saved edgeless bundle to" << path << "with" << allTileCoords.size() << "tiles";
#endif
    } else {
        // ========== PAGED MODE FILE HANDLING (Phase O1.7.4) ==========
        
        // Copy evicted pages when saving to new location
        if (savingToNewLocation) {
            for (const QString& uuid : m_pageOrder) {
                // Skip pages that are in memory - they'll be saved below
                if (m_loadedPages.find(uuid) != m_loadedPages.end()) {
                    continue;
                }
                
                QString pageFileName = uuid + ".json";
                QString oldPagePath = oldBundlePath + "/pages/" + pageFileName;
                QString newPagePath = path + "/pages/" + pageFileName;
                
                if (QFile::exists(oldPagePath)) {
                    if (QFile::copy(oldPagePath, newPagePath)) {
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied evicted page" << uuid;
#endif
                    } else {
                        #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "Failed to copy page" << oldPagePath << "to" << newPagePath;
                        #endif
                    }
                }
            }
        }
        
        // Save pages in memory
        for (const auto& [uuid, pagePtr] : m_loadedPages) {
            // Skip pristine PDF pages - they can be synthesized from manifest
            // A page is "pristine" if it has PDF background, no user content, and no bookmark
            bool isPristinePdfPage = (pagePtr->backgroundType == Page::BackgroundType::PDF) 
                                     && !pagePtr->hasContent()
                                     && !pagePtr->isBookmarked;
            if (isPristinePdfPage) {
                // Ensure PDF page index is tracked for synthesis
                if (m_pagePdfIndex.find(uuid) == m_pagePdfIndex.end()) {
                    m_pagePdfIndex[uuid] = pagePtr->pdfPageNumber;
                }
                
                // Delete any stale file from when page had content
                QString pagePath = path + "/pages/" + uuid + ".json";
                if (QFile::exists(pagePath)) {
                    QFile::remove(pagePath);
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted stale file for pristine PDF page" << uuid;
#endif
                }
                
                continue;  // Don't save file - synthesize on load
            }
            
            // When saving to new location: save ALL in-memory pages (with content)
            // When saving to same location: only save dirty pages
            bool needsSave = savingToNewLocation || m_dirtyPages.count(uuid) > 0;
            if (needsSave) {
                QString pagePath = path + "/pages/" + uuid + ".json";
                QFile file(pagePath);
                if (file.open(QIODevice::WriteOnly)) {
                    QJsonDocument doc(pagePtr->toJson());
                    file.write(doc.toJson(QJsonDocument::Compact));
                    file.close();
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Saved page" << uuid;
#endif
                } else {
                    #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Failed to save page" << pagePath;
                    #endif
                }
            }
        }
        
        // ========== DELETE REMOVED PAGES FROM DISK ==========
        for (const QString& uuid : m_deletedPages) {
            QString pagePath = path + "/pages/" + uuid + ".json";
            if (QFile::exists(pagePath)) {
                if (QFile::remove(pagePath)) {
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted page file:" << uuid;
#endif
                } else {
                    qWarning() << "Failed to delete page file:" << pagePath;
                }
            }
        }
        m_deletedPages.clear();
        m_dirtyPages.clear();
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Saved paged bundle to" << path << "with" << m_pageOrder.size() << "pages";
#endif
    }
    
    m_lazyLoadEnabled = true;
    clearModified();
    
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
    
    // Phase P.1.1: Check bundle format version for forward compatibility
    int bundleVersion = obj["bundle_format_version"].toInt(1);
    if (bundleVersion > BUNDLE_FORMAT_VERSION) {
        qWarning() << "Bundle was created with a newer version of SpeedyNote (format version"
                   << bundleVersion << ", current version" << BUNDLE_FORMAT_VERSION << ")."
                   << "Some features may not work correctly. Please update SpeedyNote.";
    }
    
    // Load document metadata
    auto doc = Document::fromJson(obj);
    if (!doc) {
        qWarning() << "Failed to parse document metadata";
        return nullptr;
    }
    
    // Set bundle path and enable lazy loading
    doc->m_bundlePath = path;
    doc->m_lazyLoadEnabled = true;
    
    // ========== MODE-SPECIFIC LOADING ==========
    if (doc->mode == Mode::Edgeless) {
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
        
        // Phase 5.6: Parse layer definitions from manifest
        if (obj.contains("layers")) {
            QJsonArray layersArray = obj["layers"].toArray();
            doc->m_edgelessLayers.clear();
            for (const auto& val : layersArray) {
                doc->m_edgelessLayers.push_back(LayerDefinition::fromJson(val.toObject()));
            }
            doc->m_edgelessActiveLayerIndex = obj["active_layer_index"].toInt(0);
            
            // Clamp active layer index
            if (doc->m_edgelessActiveLayerIndex >= static_cast<int>(doc->m_edgelessLayers.size())) {
                doc->m_edgelessActiveLayerIndex = qMax(0, static_cast<int>(doc->m_edgelessLayers.size()) - 1);
            }
        }
        
        // Ensure at least one layer exists for edgeless mode
        if (doc->m_edgelessLayers.empty()) {
            LayerDefinition defaultLayer;
            defaultLayer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            defaultLayer.name = "Layer 1";
            doc->m_edgelessLayers.push_back(defaultLayer);
        }
        
        // Phase 4: Parse position history from manifest
        if (obj.contains("last_position")) {
            QJsonObject lastPosObj = obj["last_position"].toObject();
            doc->m_edgelessLastPosition = QPointF(
                lastPosObj["x"].toDouble(0.0),
                lastPosObj["y"].toDouble(0.0)
            );
        }
        
        if (obj.contains("position_history")) {
            QJsonArray posHistoryArray = obj["position_history"].toArray();
            doc->m_edgelessPositionHistory.clear();
            for (const auto& val : posHistoryArray) {
                QJsonObject posObj = val.toObject();
                doc->m_edgelessPositionHistory.append(QPointF(
                    posObj["x"].toDouble(0.0),
                    posObj["y"].toDouble(0.0)
                ));
            }
        }
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded edgeless bundle from" << path << "with" << doc->m_tileIndex.size() 
                 << "tiles indexed," << doc->m_edgelessLayers.size() << "layers"
                 << "| last position:" << doc->m_edgelessLastPosition
                 << "| history size:" << doc->m_edgelessPositionHistory.size();
#endif
    } else {
        // ========== PAGED MODE LOADING (Phase O1.7.4) ==========
        // Parse page_order (just UUIDs, no actual content loading!)
        if (obj.contains("page_order")) {
            QJsonArray pageOrderArray = obj["page_order"].toArray();
            for (const auto& val : pageOrderArray) {
                doc->m_pageOrder.append(val.toString());
            }
            
            // Parse page_metadata (sizes and PDF page indices for layout/synthesis)
            if (obj.contains("page_metadata")) {
                QJsonObject pageMetadataObj = obj["page_metadata"].toObject();
                for (auto it = pageMetadataObj.begin(); it != pageMetadataObj.end(); ++it) {
                    QString uuid = it.key();
                    QJsonObject metaObj = it.value().toObject();
                    QSizeF size(metaObj["width"].toDouble(595.0), 
                               metaObj["height"].toDouble(842.0));
                    doc->m_pageMetadata[uuid] = size;
                    
                    // Parse PDF page index for pristine page synthesis
                    if (metaObj.contains("pdf_page")) {
                        doc->m_pagePdfIndex[uuid] = metaObj["pdf_page"].toInt();
                    }
                }
            } else {
                // Fallback: assign default size to pages missing metadata
                for (const QString& uuid : doc->m_pageOrder) {
                    if (doc->m_pageMetadata.find(uuid) == doc->m_pageMetadata.end()) {
                        doc->m_pageMetadata[uuid] = QSizeF(595.0, 842.0); // A4 default
                    }
                }
            }
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Loaded paged bundle from" << path << "with" 
                     << doc->m_pageOrder.size() << "pages indexed";
#endif
        } else {
            // No page_order - this shouldn't happen for paged bundles
            qWarning() << "Paged bundle missing page_order in manifest";
        }
    }
    
    // ========== LOAD PDF IF REFERENCED (Phase SHARE: Dual Path Resolution) ==========
    // Document::fromJson() stores both pdf_path (absolute) and pdf_relative_path.
    // We try both paths and use whichever works, updating the other for consistency.
    if (!doc->m_pdfPath.isEmpty() || !doc->m_pdfRelativePath.isEmpty()) {
        QString bundleDir = QFileInfo(manifestPath).absolutePath();
        bool absoluteExists = !doc->m_pdfPath.isEmpty() && QFile::exists(doc->m_pdfPath);
        bool relativeExists = false;
        QString resolvedRelativePath;
        
        // Resolve relative path to absolute (canonicalize to remove .. components)
        if (!doc->m_pdfRelativePath.isEmpty()) {
            QString rawPath = QDir(bundleDir).absoluteFilePath(doc->m_pdfRelativePath);
            QFileInfo fileInfo(rawPath);
            if (fileInfo.exists()) {
                resolvedRelativePath = fileInfo.canonicalFilePath();  // Clean absolute path
                relativeExists = true;
            }
        }
        
        if (absoluteExists) {
            // Absolute path works - load PDF and update relative path for portability
            if (doc->loadPdf(doc->m_pdfPath)) {
                // Update relative path to keep in sync
                doc->m_pdfRelativePath = QDir(bundleDir).relativeFilePath(doc->m_pdfPath);
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "loadBundle: Loaded PDF from absolute path:" << doc->m_pdfPath;
#endif
        } else {
                qWarning() << "loadBundle: Failed to load PDF from absolute path:" << doc->m_pdfPath;
                doc->m_needsPdfRelink = true;
            }
        } else if (relativeExists) {
            // Relative path works - load PDF
            // Note: loadPdf() already sets m_pdfPath = resolvedRelativePath internally
            if (doc->loadPdf(resolvedRelativePath)) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "loadBundle: Loaded PDF from relative path:" << doc->m_pdfRelativePath 
                         << "-> resolved to:" << resolvedRelativePath;
#endif
            } else {
                qWarning() << "loadBundle: Failed to load PDF from relative path:" << resolvedRelativePath;
                doc->m_needsPdfRelink = true;
            }
        } else {
            // Neither path works - flag for relink
            qWarning() << "loadBundle: PDF not found at absolute path:" << doc->m_pdfPath
                       << "or relative path:" << doc->m_pdfRelativePath;
            doc->m_needsPdfRelink = true;
        }
    }
    
    return doc;
}

QString Document::peekBundleId(const QString& path)
{
    // Lightweight manifest peek - only reads enough to get the document ID
    QString manifestPath = path + "/document.json";
    
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        // Not a valid bundle or file doesn't exist
        return QString();
    }
    
    QByteArray data = manifestFile.readAll();
    manifestFile.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QString();
    }
    
    QJsonObject obj = jsonDoc.object();
    // Try "notebook_id" first (current format), fall back to "id" (legacy)
    QString docId = obj["notebook_id"].toString();
    if (docId.isEmpty()) {
        docId = obj["id"].toString();
    }
    return docId;
}

// =============================================================================
// Edgeless Layer Manifest API (Phase 5.6)
// =============================================================================

const LayerDefinition* Document::edgelessLayerDef(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return nullptr;
    }
    return &m_edgelessLayers[index];
}

QString Document::edgelessLayerId(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return QString();
    }
    return m_edgelessLayers[index].id;
}

int Document::addEdgelessLayer(const QString& name)
{
    LayerDefinition layerDef;
    layerDef.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layerDef.name = name.isEmpty() ? QString("Layer %1").arg(m_edgelessLayers.size() + 1) : name;
    
    m_edgelessLayers.push_back(layerDef);
    m_edgelessManifestDirty = true;
    markModified();
    
    int newIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    
    // Phase 5.6.8: Add layer to all loaded tiles
    for (auto& [coord, tile] : m_tiles) {
        auto layer = std::make_unique<VectorLayer>(layerDef.name);
        layer->id = layerDef.id;
        layer->visible = layerDef.visible;
        layer->opacity = layerDef.opacity;
        layer->locked = layerDef.locked;
        tile->vectorLayers.push_back(std::move(layer));
        m_dirtyTiles.insert(coord);
    }
    
    return newIndex;
}

bool Document::removeEdgelessLayer(int index)
{
    // Don't remove the last layer
    if (m_edgelessLayers.size() <= 1) {
        return false;
    }
    
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // CR-L13: Load all evicted tiles so we remove strokes everywhere
    loadAllEvictedTiles();
    
    m_edgelessLayers.erase(m_edgelessLayers.begin() + index);
    
    // CR-L12: Properly adjust active layer index
    if (index < m_edgelessActiveLayerIndex) {
        // Removed layer was below active, shift down
        m_edgelessActiveLayerIndex--;
    } else if (m_edgelessActiveLayerIndex >= static_cast<int>(m_edgelessLayers.size())) {
        // Active was the removed layer or above
        m_edgelessActiveLayerIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    }
    if (m_edgelessActiveLayerIndex < 0) {
        m_edgelessActiveLayerIndex = 0;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    // Phase 5.6.8: Remove layer from all loaded tiles
    // Collect tile coords first since we may need to remove empty tiles
    std::vector<TileCoord> tileCoords;
    tileCoords.reserve(m_tiles.size());
    for (const auto& [coord, tile] : m_tiles) {
        tileCoords.push_back(coord);
    }
    
    for (const auto& coord : tileCoords) {
        auto it = m_tiles.find(coord);
        if (it == m_tiles.end()) continue;
        
        Page* tile = it->second.get();
        if (index < tile->layerCount()) {
            tile->removeLayer(index);
        }
        // Also adjust tile's active layer index
        if (tile->activeLayerIndex >= tile->layerCount()) {
            tile->activeLayerIndex = tile->layerCount() - 1;
        }
        m_dirtyTiles.insert(coord);
        
        // CR-L13 fix: Remove tile if it's now empty
        removeTileIfEmpty(coord.first, coord.second);
    }
    
    return true;
}

bool Document::moveEdgelessLayer(int from, int to)
{
    int count = static_cast<int>(m_edgelessLayers.size());
    
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }
    
    // Extract the layer
    LayerDefinition layer = std::move(m_edgelessLayers[from]);
    m_edgelessLayers.erase(m_edgelessLayers.begin() + from);
    m_edgelessLayers.insert(m_edgelessLayers.begin() + to, std::move(layer));
    
    // Adjust active layer index
    if (m_edgelessActiveLayerIndex == from) {
        m_edgelessActiveLayerIndex = to;
    } else if (from < m_edgelessActiveLayerIndex && to >= m_edgelessActiveLayerIndex) {
        m_edgelessActiveLayerIndex--;
    } else if (from > m_edgelessActiveLayerIndex && to <= m_edgelessActiveLayerIndex) {
        m_edgelessActiveLayerIndex++;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    // Phase 5.6.8: Move layer on all loaded tiles
    for (auto& [coord, tile] : m_tiles) {
        tile->moveLayer(from, to);
        m_dirtyTiles.insert(coord);
    }
    
    return true;
}

bool Document::mergeEdgelessLayers(int targetIndex, const QVector<int>& sourceIndices)
{
    // Validate target index
    if (targetIndex < 0 || targetIndex >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // Validate all source indices
    for (int idx : sourceIndices) {
        if (idx < 0 || idx >= static_cast<int>(m_edgelessLayers.size())) {
            return false;
        }
        if (idx == targetIndex) {
            return false;  // Can't merge layer into itself
        }
    }
    
    // Ensure we don't remove all layers
    if (sourceIndices.size() >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // CR-L13: Load all evicted tiles so we merge strokes everywhere
    loadAllEvictedTiles();
    
    // For each loaded tile: move strokes from source layers to target layer
    for (auto& [coord, tile] : m_tiles) {
        VectorLayer* target = tile->layer(targetIndex);
        if (!target) continue;
        
        // Collect strokes from all source layers
        for (int srcIdx : sourceIndices) {
            VectorLayer* source = tile->layer(srcIdx);
            if (source) {
                // Move all strokes to target
                for (VectorStroke& stroke : source->strokes()) {
                    target->addStroke(std::move(stroke));
                }
                source->clear();
            }
        }
        
        m_dirtyTiles.insert(coord);
    }
    
    // Remove source layers from manifest and tiles (in reverse order to preserve indices)
    QVector<int> sortedSources = sourceIndices;
    std::sort(sortedSources.begin(), sortedSources.end(), std::greater<int>());
    
    // Collect tile coords first since we may need to remove empty tiles later
    std::vector<TileCoord> tileCoords;
    tileCoords.reserve(m_tiles.size());
    for (const auto& [coord, tile] : m_tiles) {
        tileCoords.push_back(coord);
    }
    
    for (int srcIdx : sortedSources) {
        // Remove from manifest
        m_edgelessLayers.erase(m_edgelessLayers.begin() + srcIdx);
        
        // Remove from all tiles
        for (const auto& coord : tileCoords) {
            auto it = m_tiles.find(coord);
            if (it == m_tiles.end()) continue;
            
            Page* tile = it->second.get();
            if (srcIdx < tile->layerCount()) {
                tile->removeLayer(srcIdx);
            }
        }
        
        // CR-L12: Adjust active layer index when removing layers below it
        if (srcIdx < m_edgelessActiveLayerIndex) {
            m_edgelessActiveLayerIndex--;
        }
    }
    
    // CR-L13: Check for empty tiles after all layer removals
    // (In merge, strokes are moved to target, so tiles typically won't be empty,
    // but check anyway for safety and code consistency)
    for (const auto& coord : tileCoords) {
        removeTileIfEmpty(coord.first, coord.second);
    }
    
    // Final clamp in case active was one of the removed layers
    if (m_edgelessActiveLayerIndex >= static_cast<int>(m_edgelessLayers.size())) {
        m_edgelessActiveLayerIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    }
    if (m_edgelessActiveLayerIndex < 0) {
        m_edgelessActiveLayerIndex = 0;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    return true;
}

int Document::duplicateEdgelessLayer(int index)
{
    // Validate index
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return -1;
    }
    
    // Create new layer definition as a copy of the original
    LayerDefinition newDef;
    newDef.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    newDef.name = m_edgelessLayers[index].name + " Copy";
    newDef.visible = m_edgelessLayers[index].visible;
    newDef.opacity = m_edgelessLayers[index].opacity;
    newDef.locked = false;  // Unlock the copy for immediate editing
    
    // Insert at index + 1 (above the original)
    int newIndex = index + 1;
    m_edgelessLayers.insert(m_edgelessLayers.begin() + newIndex, newDef);
    
    // For each loaded tile: duplicate the layer's strokes
    for (auto& [coord, tile] : m_tiles) {
        VectorLayer* source = tile->layer(index);
        
        // Create new layer in tile at the same position
        // First, add a layer at the end, then move it to newIndex
        VectorLayer* newLayer = tile->addLayer(newDef.name);
        if (!newLayer) continue;
        
        // Copy properties
        newLayer->visible = newDef.visible;
        newLayer->opacity = newDef.opacity;
        newLayer->locked = newDef.locked;
        
        // Deep copy strokes with new UUIDs
        if (source) {
            for (const VectorStroke& stroke : source->strokes()) {
                VectorStroke copy = stroke;  // Copy all properties
                copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New UUID
                newLayer->addStroke(std::move(copy));
            }
        }
        
        // Move the new layer from the end to the correct position
        int lastIndex = tile->layerCount() - 1;
        if (lastIndex != newIndex) {
            tile->moveLayer(lastIndex, newIndex);
        }
        
        m_dirtyTiles.insert(coord);
    }
    
    // Adjust active layer index if it's at or above the insertion point
    if (m_edgelessActiveLayerIndex >= newIndex) {
        m_edgelessActiveLayerIndex++;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    return newIndex;
}

void Document::setEdgelessLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].visible != visible) {
        m_edgelessLayers[index].visible = visible;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync visibility to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->visible = visible;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerName(int index, const QString& name)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].name != name) {
        m_edgelessLayers[index].name = name;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync name to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->name = name;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerOpacity(int index, qreal opacity)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    opacity = qBound(0.0, opacity, 1.0);
    
    if (!qFuzzyCompare(m_edgelessLayers[index].opacity, opacity)) {
        m_edgelessLayers[index].opacity = opacity;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync opacity to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->opacity = opacity;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerLocked(int index, bool locked)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].locked != locked) {
        m_edgelessLayers[index].locked = locked;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync locked state to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->locked = locked;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessActiveLayerIndex(int index)
{
    // CR-L7: Defensive - handle empty layers case
    if (m_edgelessLayers.empty()) {
        return;
    }
    
    // Clamp index to valid range
    index = qBound(0, index, static_cast<int>(m_edgelessLayers.size()) - 1);
    
    if (m_edgelessActiveLayerIndex != index) {
        m_edgelessActiveLayerIndex = index;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync active layer index to all loaded tiles
        // CR-L9: Don't mark tiles dirty - activeLayerIndex is stored in manifest,
        // not per-tile. In-memory sync is for runtime use only.
        for (auto& [coord, tile] : m_tiles) {
            tile->activeLayerIndex = index;
        }
    }
}
