#include "PdfSearchEngine.h"
#include "PdfProvider.h"
#include "../core/Document.h"
#include "../core/Page.h"
#include "../ocr/OcrTextBlock.h"
#include "../objects/TextBoxObject.h"
#include "../objects/OcrTextObject.h"

#include <QDebug>
#include <QDir>
#include <QHash>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QtConcurrent/QtConcurrent>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PdfSearchEngine::PdfSearchEngine(QObject *parent)
    : QObject(parent)
{
    // SBS2: pageScanned is emitted from a worker thread, so its arguments must
    // be registered metatypes for the queued cross-thread connection.
    qRegisterMetaType<PdfSearchMatch>("PdfSearchMatch");
    qRegisterMetaType<QVector<PdfSearchMatch>>("QVector<PdfSearchMatch>");

    connect(&m_searchWatcher, &QFutureWatcher<void>::finished,
            this, &PdfSearchEngine::onSearchFinished);
    connect(&m_precacheWatcher, &QFutureWatcher<void>::finished,
            this, &PdfSearchEngine::onPrecacheFinished);
}

PdfSearchEngine::~PdfSearchEngine()
{
    cancel();
    m_scanCancelled.store(true);
    m_searchWatcher.waitForFinished();
    m_precacheWatcher.waitForFinished();
    m_scanWatcher.waitForFinished();
}

void PdfSearchEngine::setDocument(Document *doc)
{
    if (m_document != doc) {
        // Cancel any ongoing operations before changing document
        cancel();
        m_scanCancelled.store(true);
        m_searchWatcher.waitForFinished();
        m_precacheWatcher.waitForFinished();
        m_scanWatcher.waitForFinished();
        
        m_document = doc;
        clearCache();
        
        // Clear result state
        {
            QMutexLocker lock(&m_resultMutex);
            m_hasResult = false;
            m_searchNotFound = false;
            m_foundMatch = PdfSearchMatch();
            m_foundPageMatches.clear();
        }
        
        // Reset cancellation flags
        m_searchCancelled.store(false);
        m_precacheCancelled.store(false);
        m_scanCancelled.store(false);
    }
}

// ============================================================================
// Cache Management
// ============================================================================

void PdfSearchEngine::clearCache()
{
    QMutexLocker lock(&m_cacheMutex);
    m_cache.clear();
    m_edgelessTileOrder.clear();
    m_edgelessTileOrderBuilt = false;
}

int PdfSearchEngine::cacheSize() const
{
    QMutexLocker lock(&m_cacheMutex);
    return static_cast<int>(m_cache.size());
}

bool PdfSearchEngine::isPageCached(int pageIndex) const
{
    QMutexLocker lock(&m_cacheMutex);
    return m_cache.contains(pageIndex) && m_cache[pageIndex].searched;
}

void PdfSearchEngine::addToCache(int pageIndex, const QVector<PdfSearchMatch>& matches)
{
    QMutexLocker lock(&m_cacheMutex);
    
    // Note: We don't evict anymore since we want to cache the entire document
    // Memory impact is minimal: ~50-100 bytes per page entry (mostly empty QVectors)
    // For a 2000-page document: ~100-200 KB which is acceptable
    
    PdfSearchCacheEntry entry;
    entry.pageIndex = pageIndex;
    entry.matches = matches;
    entry.searched = true;
    m_cache[pageIndex] = entry;
}

QVector<PdfSearchMatch> PdfSearchEngine::getCachedOrSearch(int pageIndex)
{
    // Check cache first
    {
        QMutexLocker lock(&m_cacheMutex);
        if (m_cache.contains(pageIndex) && m_cache[pageIndex].searched) {
            return m_cache[pageIndex].matches;
        }
    }
    
    // Safety check: document may have been deleted during background operation
    if (!m_document) {
        return QVector<PdfSearchMatch>();
    }
    
    // Not in cache, search the page
    QVector<PdfSearchMatch> matches = searchPage(pageIndex, m_searchText, 
                                                  m_caseSensitive, m_wholeWord);
    
    // Add to cache (check document again in case it was deleted during search)
    if (m_document) {
        addToCache(pageIndex, matches);
    }
    
    return matches;
}

// ============================================================================
// Single Page Search
// ============================================================================

QVector<PdfSearchMatch> PdfSearchEngine::searchPage(int pageIndex, 
                                                     const QString& text,
                                                     bool caseSensitive, 
                                                     bool wholeWord) const
{
    QVector<PdfSearchMatch> matches;
    
    if (!m_document || text.isEmpty()) {
        return matches;
    }
    
    // --- PDF text search (existing logic) ---
    // pageIndex is a notebook page index; resolve it to its own PDF source + page
    // number so that pages backed by ANY source (not just primary) are searchable.
    QString srcId;
    int pdfPageIdx = -1;
    if (m_document->pdfBindingForNotebookPage(pageIndex, srcId, pdfPageIdx)) {
        // Provider is pre-opened on the main thread (ensureAllPdfProvidersLoaded);
        // here we only read the cached provider from the worker thread.
        const PdfProvider* pdf = m_document->providerForSource(srcId);
        // Translate the original page number to the provider's index (bundled sources
        // remap into a compact mini-PDF via pageMap).
        const int providerPage = m_document->resolveSourcePageIndex(srcId, pdfPageIdx);
        QVector<PdfTextBox> textBoxes = (pdf && pdf->supportsTextExtraction() && providerPage >= 0)
            ? pdf->textBoxes(providerPage) : QVector<PdfTextBox>();
        if (!textBoxes.isEmpty()) {
            QString pageText;
            QVector<QPair<int, int>> boxMapping;
            
            for (int i = 0; i < textBoxes.size(); ++i) {
                pageText += textBoxes[i].text;
                
                for (int j = 0; j < textBoxes[i].text.length(); ++j) {
                    boxMapping.append({i, j});
                }
                
                // CJK-aware synthetic separator: MuPdfProvider emits one
                // PdfTextBox per CJK glyph, so blindly inserting a space
                // between every adjacent box pair would break multi-char
                // CJK searches (e.g. searching "中文" against a pageText
                // that became "中 文"). Mirrors the same predicate used by
                // searchOcrBlocks() below.
                if (i < textBoxes.size() - 1 && !pageText.endsWith(' ')) {
                    QChar prevTrailing = textBoxes[i].text.isEmpty()
                        ? QChar() : textBoxes[i].text.back();
                    QChar nextLeading  = textBoxes[i + 1].text.isEmpty()
                        ? QChar() : textBoxes[i + 1].text.front();
                    bool needsSpace = !isCjkLikeChar(prevTrailing)
                                   && !isCjkLikeChar(nextLeading);
                    if (needsSpace) {
                        pageText += ' ';
                        boxMapping.append({-1, -1});
                    }
                }
            }
            
            Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
            
            int searchPos = 0;
            int matchIndex = 0;
            
            while (searchPos < pageText.length()) {
                int foundPos = static_cast<int>(pageText.indexOf(text, searchPos, cs));
                if (foundPos < 0) {
                    break;
                }
                
                if (wholeWord) {
                    if (foundPos > 0) {
                        QChar before = pageText[foundPos - 1];
                        if (before.isLetterOrNumber() || before == '_') {
                            searchPos = foundPos + 1;
                            continue;
                        }
                    }
                    int endPos = foundPos + static_cast<int>(text.length());
                    if (endPos < pageText.length()) {
                        QChar after = pageText[endPos];
                        if (after.isLetterOrNumber() || after == '_') {
                            searchPos = foundPos + 1;
                            continue;
                        }
                    }
                }
                
                QRectF matchRect;
                for (int i = foundPos; i < foundPos + text.length(); ++i) {
                    if (i >= boxMapping.size()) break;
                    
                    int boxIdx = boxMapping[i].first;
                    int charIdx = boxMapping[i].second;
                    
                    if (boxIdx < 0) continue;
                    
                    const PdfTextBox& box = textBoxes[boxIdx];
                    
                    if (charIdx >= 0 && charIdx < box.charBoundingBoxes.size()) {
                        if (matchRect.isNull()) {
                            matchRect = box.charBoundingBoxes[charIdx];
                        } else {
                            matchRect = matchRect.united(box.charBoundingBoxes[charIdx]);
                        }
                    } else {
                        if (matchRect.isNull()) {
                            matchRect = box.boundingBox;
                        } else {
                            matchRect = matchRect.united(box.boundingBox);
                        }
                    }
                }
                
                if (!matchRect.isNull()) {
                    PdfSearchMatch match;
                    match.source = PdfSearchMatch::PdfText;
                    match.pageIndex = pageIndex;
                    match.matchIndex = matchIndex++;
                    match.boundingRect = matchRect;
                    matches.append(match);
                }
                
                searchPos = foundPos + 1;
            }
        }
    }
    
    // --- OCR text + TextBox / locked OCR object search (paged mode) ---
    if (pageIndex >= 0 && pageIndex < m_document->pageCount()) {
        const Page* page = m_document->page(pageIndex);
        if (page) {
            if (!page->ocrTextBlocks.isEmpty()) {
                QVector<OcrTextBlock> blocks = page->ocrBlocksForSearch();
                QVector<PdfSearchMatch> ocrMatches = searchOcrBlocks(
                    pageIndex, blocks, text, caseSensitive, wholeWord,
                    PdfSearchMatch::OcrText, 0, 0,
                    matches.size());
                matches.append(ocrMatches);
            }

            QVector<PdfSearchMatch> objMatches = searchTextBoxObjects(
                pageIndex, page, text, caseSensitive, wholeWord,
                PdfSearchMatch::TextBoxObj, 0, 0, matches.size());
            matches.append(objMatches);
        }
    }
    
    return matches;
}

// ============================================================================
// OCR Block Search
// ============================================================================

QVector<PdfSearchMatch> PdfSearchEngine::searchOcrBlocks(
    int pageIndex,
    const QVector<OcrTextBlock>& blocks,
    const QString& text,
    bool caseSensitive,
    bool wholeWord,
    PdfSearchMatch::Source source,
    int tileX, int tileY,
    int matchIndexOffset) const
{
    QVector<PdfSearchMatch> matches;
    if (blocks.isEmpty() || text.isEmpty()) {
        return matches;
    }

    const Qt::CaseSensitivity cs =
        caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int matchIndex = matchIndexOffset;

    // Build concatenated text with character-to-block mapping.
    // CJK scripts don't use inter-word spaces, so we only insert a space
    // separator when the trailing char of the previous block AND the leading
    // char of the current block are both non-CJK. Range definitions are
    // shared via isCjkLikeChar() in OcrTextBlock.h.

    QString fullText;
    QVector<int> charToBlockIndex;
    QVector<int> charToBlockOffset;   // position of each char within its block's text
    QChar prevTrailingChar;
    bool hasPrev = false;

    for (int b = 0; b < blocks.size(); ++b) {
        const OcrTextBlock& block = blocks[b];
        if (block.dirty || block.text.isEmpty()) continue;

        if (hasPrev) {
            QChar leadChar = block.text.at(0);
            bool needsSpace = !isCjkLikeChar(prevTrailingChar) && !isCjkLikeChar(leadChar);
            if (needsSpace) {
                fullText += ' ';
                charToBlockIndex.append(-1);
                charToBlockOffset.append(-1);
            }
        }

        fullText += block.text;
        for (int c = 0; c < block.text.length(); ++c) {
            charToBlockIndex.append(b);
            charToBlockOffset.append(c);
        }
        prevTrailingChar = block.text.at(block.text.length() - 1);
        hasPrev = true;
    }

    // Lazily-built per-block flattened char-rect cache (block index -> rects of
    // size block.text.length(), or empty when the block lacks per-char geometry).
    QHash<int, QVector<QRectF>> flatCharRectsCache;

    int searchPos = 0;
    while (searchPos < fullText.length()) {
        int foundPos = static_cast<int>(fullText.indexOf(text, searchPos, cs));
        if (foundPos < 0) break;

        if (wholeWord) {
            if (foundPos > 0) {
                QChar before = fullText[foundPos - 1];
                if (before.isLetterOrNumber() || before == '_') {
                    searchPos = foundPos + 1;
                    continue;
                }
            }
            int endPos = foundPos + static_cast<int>(text.length());
            if (endPos < fullText.length()) {
                QChar after = fullText[endPos];
                if (after.isLetterOrNumber() || after == '_') {
                    searchPos = foundPos + 1;
                    continue;
                }
            }
        }

        // Compute a tight highlight rect. Prefer the engine's real per-character
        // boxes (flattened from wordSegments); fall back to proportional sub-rects
        // of the block bounding rect when char geometry is unavailable. This
        // mirrors the PDF text path (box.charBoundingBoxes -> boundingBox).
        QRectF matchRect;
        int matchEnd = foundPos + static_cast<int>(text.length());
        int prevBlockIdx = -1;
        int blockCharStart = -1;
        int blockCharEnd = -1;

        auto flushBlock = [&]() {
            if (prevBlockIdx < 0 || prevBlockIdx >= blocks.size()) return;
            const OcrTextBlock& b = blocks[prevBlockIdx];

            auto it = flatCharRectsCache.find(prevBlockIdx);
            if (it == flatCharRectsCache.end())
                it = flatCharRectsCache.insert(prevBlockIdx, flattenOcrBlockCharRects(b));
            const QVector<QRectF>& flat = it.value();

            QRectF subRect;
            if (!flat.isEmpty()) {
                for (int c = blockCharStart; c <= blockCharEnd && c < flat.size(); ++c) {
                    const QRectF& cr = flat[c];
                    if (cr.isNull()) continue;  // whitespace edge with no span
                    subRect = subRect.isNull() ? cr : subRect.united(cr);
                }
            }
            if (subRect.isNull()) {
                // Fallback: proportional split of the block bounding rect.
                int blockLen = b.text.length();
                subRect = b.boundingRect;
                if (blockLen > 1) {
                    qreal charW = subRect.width() / blockLen;
                    subRect.setLeft(subRect.left() + charW * blockCharStart);
                    subRect.setWidth(charW * (blockCharEnd - blockCharStart + 1));
                }
            }
            if (matchRect.isNull())
                matchRect = subRect;
            else
                matchRect = matchRect.united(subRect);
        };

        for (int i = foundPos; i < matchEnd; ++i) {
            if (i >= charToBlockIndex.size()) break;
            int bIdx = charToBlockIndex[i];
            int bOff = charToBlockOffset[i];
            if (bIdx < 0) continue;

            if (bIdx != prevBlockIdx) {
                flushBlock();
                prevBlockIdx = bIdx;
                blockCharStart = bOff;
                blockCharEnd = bOff;
            } else {
                if (bOff > blockCharEnd) blockCharEnd = bOff;
                if (bOff < blockCharStart) blockCharStart = bOff;
            }
        }
        flushBlock();

        if (!matchRect.isNull()) {
            PdfSearchMatch match;
            match.source = source;
            match.pageIndex = pageIndex;
            match.matchIndex = matchIndex++;
            match.boundingRect = matchRect;
            match.tileX = tileX;
            match.tileY = tileY;
            matches.append(match);
        }

        searchPos = foundPos + 1;
    }

    return matches;
}

// ============================================================================
// TextBox / Locked OCR Object Search
// ============================================================================

QVector<PdfSearchMatch> PdfSearchEngine::searchTextBoxObjects(
    int pageIndex,
    const Page* page,
    const QString& text,
    bool caseSensitive,
    bool wholeWord,
    PdfSearchMatch::Source source,
    int tileX, int tileY,
    int matchIndexOffset) const
{
    QVector<PdfSearchMatch> matches;
    if (!page || text.isEmpty()) return matches;

    int matchIndex = matchIndexOffset;

    for (const auto& objPtr : page->objects) {
        if (!objPtr) continue;
        const InsertedObject* rawObj = objPtr.get();

        const TextBoxObject* textBox = nullptr;
        if (rawObj->type() == QLatin1String("textbox")) {
            textBox = static_cast<const TextBoxObject*>(rawObj);
        } else if (rawObj->type() == QLatin1String("ocr_text")) {
            auto* ocrObj = static_cast<const OcrTextObject*>(rawObj);
            if (ocrObj->ocrLocked)
                textBox = ocrObj;
        }
        if (!textBox || textBox->text.isEmpty()) continue;

        const QPointF& objPos = textBox->position;
        // PdfSearchEngine runs this on QtConcurrent workers. Build a local
        // layout from copied scalar inputs instead of touching the GUI cache.
        const TextBoxLayoutInput input = textBox->layoutInput();
        const std::unique_ptr<TextBoxLayoutResult> layout =
            TextBoxObject::buildLayout(input);
        if (!layout)
            continue;

        const QVector<QRectF> localRects =
            layout->findTextRects(text, caseSensitive, wholeWord);
        for (const QRectF& localRect : localRects) {
            PdfSearchMatch match;
            match.source = source;
            match.pageIndex = pageIndex;
            match.matchIndex = matchIndex++;
            match.boundingRect = localRect.translated(objPos);
            match.tileX = tileX;
            match.tileY = tileY;
            matches.append(match);
        }
    }

    return matches;
}

// ============================================================================
// Background Search Thread
// ============================================================================

void PdfSearchEngine::doSearch(int startPage, int startMatchIndex, int direction)
{
    if (!m_document) {
        QMutexLocker lock(&m_resultMutex);
        m_searchNotFound = true;
        m_hasResult = true;
        return;
    }
    
    // Determine total pages: use notebook page count (covers both PDF and non-PDF docs)
    int totalPages = m_document->pageCount();

    // Fall back to PDF page count if notebook has no pages but PDF does
    const PdfProvider* pdf = m_document->pdfProvider();
    if (totalPages == 0 && pdf && pdf->isValid()) {
        totalPages = pdf->pageCount();
    }

    if (totalPages == 0) {
        QMutexLocker lock(&m_resultMutex);
        m_searchNotFound = true;
        m_hasResult = true;
        return;
    }
    
    if (startPage < 0 || startPage >= totalPages) {
        startPage = (direction > 0) ? 0 : totalPages - 1;
    }
    
    int pagesSearched = 0;
    int currentPage = startPage;
    bool wrapped = false;
    
    while (pagesSearched < totalPages) {
        if (m_searchCancelled.load()) {
            return;
        }
        
        // Emit progress on main thread would require queued connection
        // For simplicity, we skip progress updates in background mode
        
        QVector<PdfSearchMatch> pageMatches = getCachedOrSearch(currentPage);
        
        if (!pageMatches.isEmpty()) {
            int foundIdx = -1;
            
            if (currentPage == startPage && pagesSearched == 0) {
                if (direction > 0) {
                    // Forward: find match after startMatchIndex
                    for (int i = 0; i < pageMatches.size(); ++i) {
                        if (pageMatches[i].matchIndex > startMatchIndex) {
                            foundIdx = i;
                            break;
                        }
                    }
                } else {
                    // Backward: find match before startMatchIndex
                    for (int i = static_cast<int>(pageMatches.size()) - 1; i >= 0; --i) {
                        if (startMatchIndex < 0 || pageMatches[i].matchIndex < startMatchIndex) {
                            foundIdx = i;
                            break;
                        }
                    }
                }
            } else {
                foundIdx = (direction > 0) ? 0 : static_cast<int>(pageMatches.size()) - 1;
            }
            
            if (foundIdx >= 0) {
                QMutexLocker lock(&m_resultMutex);
                m_foundMatch = pageMatches[foundIdx];
                m_foundPageMatches = pageMatches;
                m_searchWrapped = wrapped;
                m_hasResult = true;
                return;
            }
        }
        
        // Move to next/prev page
        currentPage += direction;
        pagesSearched++;
        
        if (direction > 0 && currentPage >= totalPages) {
            currentPage = 0;
            wrapped = true;
        } else if (direction < 0 && currentPage < 0) {
            currentPage = totalPages - 1;
            wrapped = true;
        }
        
        // Check if we've wrapped all the way around
        if (currentPage == startPage && pagesSearched > 0) {
            QVector<PdfSearchMatch> startPageMatches = getCachedOrSearch(startPage);
            if (!startPageMatches.isEmpty()) {
                int foundIdx = (direction > 0) ? 0 : static_cast<int>(startPageMatches.size()) - 1;
                QMutexLocker lock(&m_resultMutex);
                m_foundMatch = startPageMatches[foundIdx];
                m_foundPageMatches = startPageMatches;
                m_searchWrapped = true;
                m_hasResult = true;
                return;
            }
            break;
        }
    }
    
    QMutexLocker lock(&m_resultMutex);
    m_searchNotFound = true;
    m_searchWrapped = wrapped;
    m_hasResult = true;
}

void PdfSearchEngine::onSearchFinished()
{
    QMutexLocker lock(&m_resultMutex);
    
    if (!m_hasResult) {
        return;  // Search was cancelled
    }
    
    if (m_searchNotFound) {
        m_hasResult = false;
        m_searchNotFound = false;
        emit notFound(m_searchWrapped);
    } else {
        PdfSearchMatch match = m_foundMatch;
        QVector<PdfSearchMatch> pageMatches = m_foundPageMatches;
        m_hasResult = false;
        
        emit matchFound(match, pageMatches);
        
        // Start pre-caching nearby pages in background
        startPrecaching(match.pageIndex, 1);  // Pre-cache forward
    }
}

// ============================================================================
// Pre-caching
// ============================================================================

void PdfSearchEngine::startPrecaching(int centerPage, int direction)
{
    if (m_precaching.load()) {
        return;  // Already pre-caching
    }
    
    if (!m_document || m_document->isEdgeless()) {
        return;  // No precache for edgeless (tiles are searched on-demand)
    }

    int totalPages = m_document->pageCount();
    if (totalPages == 0 || cacheSize() >= totalPages) {
        return;  // Already fully cached or empty document
    }

    // Ensure all providers are open on the main thread before the worker reads them.
    m_document->ensureAllPdfProvidersLoaded();

    m_precaching.store(true);
    
    QFuture<void> future = QtConcurrent::run([this, centerPage, direction]() {
        doPrecache(centerPage, direction);
    });
    m_precacheWatcher.setFuture(future);
}

void PdfSearchEngine::doPrecache(int centerPage, int direction)
{
    Q_UNUSED(centerPage)
    Q_UNUSED(direction)
    
    if (!m_document) {
        return;
    }
    
    int totalPages = m_document->pageCount();
    
    // Cache the ENTIRE document for instant subsequent navigation
    // This runs in background, so it won't block UI
    for (int page = 0; page < totalPages; ++page) {
        if (m_precacheCancelled.load()) {
            return;
        }
        
        getCachedOrSearch(page);
    }
}

void PdfSearchEngine::onPrecacheFinished()
{
    m_precaching.store(false);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PdfSearchEngine] Pre-cache complete, cache size:" << cacheSize();
#endif
}

// ============================================================================
// Edgeless Search
// ============================================================================

void PdfSearchEngine::buildEdgelessTileOrder()
{
    m_edgelessTileOrder.clear();
    m_edgelessTileOrderBuilt = false;

    if (!m_document || !m_document->isEdgeless()) return;

    // Gather loaded tiles
    QSet<Document::TileCoord> allCoords;
    for (const auto& c : m_document->allLoadedTileCoords())
        allCoords.insert(c);

    // Gather unloaded tiles from disk (.ocr.json files)
    QString tilesPath = m_document->bundlePath() + "/tiles";
    QDir tilesDir(tilesPath);
    if (tilesDir.exists()) {
        QStringList ocrFiles = tilesDir.entryList({"*.ocr.json"}, QDir::Files);
        for (const QString& fileName : ocrFiles) {
            QString base = fileName.left(fileName.indexOf(QLatin1String(".ocr.json")));
            QStringList parts = base.split(',');
            if (parts.size() == 2) {
                bool okX, okY;
                int tx = parts[0].toInt(&okX);
                int ty = parts[1].toInt(&okY);
                if (okX && okY) {
                    allCoords.insert({tx, ty});
                }
            }
        }
    }

    m_edgelessTileOrder.reserve(allCoords.size());
    for (const auto& c : allCoords)
        m_edgelessTileOrder.append(c);

    // Sort top-to-bottom, left-to-right
    std::sort(m_edgelessTileOrder.begin(), m_edgelessTileOrder.end(),
              [](const Document::TileCoord& a, const Document::TileCoord& b) {
                  if (a.second != b.second) return a.second < b.second;
                  return a.first < b.first;
              });

    m_edgelessTileOrderBuilt = true;
}

void PdfSearchEngine::doSearchEdgeless(int startVirtualPage, int startMatchIndex, int direction)
{
    if (!m_document || !m_document->isEdgeless()) {
        QMutexLocker lock(&m_resultMutex);
        m_searchNotFound = true;
        m_hasResult = true;
        return;
    }

    if (!m_edgelessTileOrderBuilt) {
        buildEdgelessTileOrder();
    }

    int totalTiles = m_edgelessTileOrder.size();
    if (totalTiles == 0) {
        QMutexLocker lock(&m_resultMutex);
        m_searchNotFound = true;
        m_hasResult = true;
        return;
    }

    if (startVirtualPage < 0 || startVirtualPage >= totalTiles) {
        startVirtualPage = (direction > 0) ? 0 : totalTiles - 1;
    }

    int tilesSearched = 0;
    int currentTile = startVirtualPage;
    bool wrapped = false;

    while (tilesSearched < totalTiles) {
        if (m_searchCancelled.load()) return;

        // Check cache first
        QVector<PdfSearchMatch> tileMatches;
        bool cacheHit = false;
        {
            QMutexLocker lock(&m_cacheMutex);
            if (m_cache.contains(currentTile) && m_cache[currentTile].searched) {
                tileMatches = m_cache[currentTile].matches;
                cacheHit = true;
            }
        }

        if (!cacheHit) {
            const Document::TileCoord& coord = m_edgelessTileOrder[currentTile];
            int tx = coord.first;
            int ty = coord.second;

            QVector<OcrTextBlock> blocks;

            // Try loaded tile first
            Page* tile = m_document->getTile(tx, ty);
            if (tile && !tile->ocrTextBlocks.isEmpty()) {
                blocks = tile->ocrBlocksForSearch();
            } else {
                // Read from disk
                QString ocrPath = m_document->bundlePath()
                    + QStringLiteral("/tiles/%1,%2.ocr.json").arg(tx).arg(ty);
                blocks = Document::loadOcrBlocksFromFile(ocrPath);
            }

            tileMatches = searchOcrBlocks(
                currentTile, blocks, m_searchText, m_caseSensitive, m_wholeWord,
                PdfSearchMatch::OcrTextTile, tx, ty, 0);

            if (tile) {
                QVector<PdfSearchMatch> objMatches = searchTextBoxObjects(
                    currentTile, tile, m_searchText, m_caseSensitive, m_wholeWord,
                    PdfSearchMatch::TextBoxObjTile, tx, ty, tileMatches.size());
                tileMatches.append(objMatches);
            }

            addToCache(currentTile, tileMatches);
        }

        if (!tileMatches.isEmpty()) {
            int foundIdx = -1;

            if (currentTile == startVirtualPage && tilesSearched == 0) {
                if (direction > 0) {
                    for (int i = 0; i < tileMatches.size(); ++i) {
                        if (tileMatches[i].matchIndex > startMatchIndex) {
                            foundIdx = i;
                            break;
                        }
                    }
                } else {
                    for (int i = static_cast<int>(tileMatches.size()) - 1; i >= 0; --i) {
                        if (startMatchIndex < 0 || tileMatches[i].matchIndex < startMatchIndex) {
                            foundIdx = i;
                            break;
                        }
                    }
                }
            } else {
                foundIdx = (direction > 0) ? 0 : static_cast<int>(tileMatches.size()) - 1;
            }

            if (foundIdx >= 0) {
                QMutexLocker lock(&m_resultMutex);
                m_foundMatch = tileMatches[foundIdx];
                m_foundPageMatches = tileMatches;
                m_searchWrapped = wrapped;
                m_hasResult = true;
                return;
            }
        }

        currentTile += direction;
        tilesSearched++;

        if (direction > 0 && currentTile >= totalTiles) {
            currentTile = 0;
            wrapped = true;
        } else if (direction < 0 && currentTile < 0) {
            currentTile = totalTiles - 1;
            wrapped = true;
        }

        if (currentTile == startVirtualPage && tilesSearched > 0) {
            QVector<PdfSearchMatch> startMatches;
            {
                QMutexLocker lock(&m_cacheMutex);
                if (m_cache.contains(startVirtualPage) && m_cache[startVirtualPage].searched) {
                    startMatches = m_cache[startVirtualPage].matches;
                }
            }
            if (!startMatches.isEmpty()) {
                int foundIdx = (direction > 0) ? 0 : static_cast<int>(startMatches.size()) - 1;
                QMutexLocker lock(&m_resultMutex);
                m_foundMatch = startMatches[foundIdx];
                m_foundPageMatches = startMatches;
                m_searchWrapped = true;
                m_hasResult = true;
                return;
            }
            break;
        }
    }

    QMutexLocker lock(&m_resultMutex);
    m_searchNotFound = true;
    m_searchWrapped = wrapped;
    m_hasResult = true;
}

// ============================================================================
// Find Next / Find Previous
// ============================================================================

void PdfSearchEngine::findNext(const QString& text, bool caseSensitive, bool wholeWord,
                                int startPage, int startMatchIndex)
{
    // Cancel any ongoing search (but NOT pre-cache - let it continue)
    m_searchCancelled.store(true);
    m_searchWatcher.waitForFinished();
    m_searchCancelled.store(false);
    
    // Check if search parameters changed - clear cache and cancel pre-cache
    if (text != m_searchText || caseSensitive != m_caseSensitive || wholeWord != m_wholeWord) {
        // Cancel pre-cache since parameters changed
        m_precacheCancelled.store(true);
        m_precacheWatcher.waitForFinished();
        m_precacheCancelled.store(false);

        // SBS2: also stop any in-flight whole-document scan so it cannot read
        // m_searchText / the cache while we mutate them below.
        m_scanCancelled.store(true);
        m_scanWatcher.waitForFinished();
        m_scanCancelled.store(false);
        
        clearCache();
        m_searchText = text;
        m_caseSensitive = caseSensitive;
        m_wholeWord = wholeWord;
    }
    
    if (!m_document || text.isEmpty()) {
        emit notFound(false);
        return;
    }

    // Pre-open every source's provider on the main thread so the background search
    // (and pre-cache) worker only reads the provider cache; providerForSource()
    // lazily mutates it, which must not race across threads.
    m_document->ensureAllPdfProvidersLoaded();

    // Reset result state
    {
        QMutexLocker lock(&m_resultMutex);
        m_hasResult = false;
        m_searchNotFound = false;
    }
    
    // Branch on document mode
    if (m_document->isEdgeless()) {
        QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
            doSearchEdgeless(startPage, startMatchIndex, 1);
        });
        m_searchWatcher.setFuture(future);
    } else {
        QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
            doSearch(startPage, startMatchIndex, 1);
        });
        m_searchWatcher.setFuture(future);
    }
}

void PdfSearchEngine::findPrev(const QString& text, bool caseSensitive, bool wholeWord,
                                int startPage, int startMatchIndex)
{
    // Cancel any ongoing search (but NOT pre-cache - let it continue)
    m_searchCancelled.store(true);
    m_searchWatcher.waitForFinished();
    m_searchCancelled.store(false);
    
    // Check if search parameters changed - clear cache and cancel pre-cache
    if (text != m_searchText || caseSensitive != m_caseSensitive || wholeWord != m_wholeWord) {
        // Cancel pre-cache since parameters changed
        m_precacheCancelled.store(true);
        m_precacheWatcher.waitForFinished();
        m_precacheCancelled.store(false);

        // SBS2: also stop any in-flight whole-document scan so it cannot read
        // m_searchText / the cache while we mutate them below.
        m_scanCancelled.store(true);
        m_scanWatcher.waitForFinished();
        m_scanCancelled.store(false);
        
        clearCache();
        m_searchText = text;
        m_caseSensitive = caseSensitive;
        m_wholeWord = wholeWord;
    }
    
    if (!m_document || text.isEmpty()) {
        emit notFound(false);
        return;
    }

    // Pre-open every source's provider on the main thread (see findNext()).
    m_document->ensureAllPdfProvidersLoaded();

    // Reset result state
    {
        QMutexLocker lock(&m_resultMutex);
        m_hasResult = false;
        m_searchNotFound = false;
    }
    
    // Branch on document mode
    if (m_document->isEdgeless()) {
        QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
            doSearchEdgeless(startPage, startMatchIndex, -1);
        });
        m_searchWatcher.setFuture(future);
    } else {
        QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
            doSearch(startPage, startMatchIndex, -1);
        });
        m_searchWatcher.setFuture(future);
    }
}

// ============================================================================
// Whole-document streaming scan (SBS2)
// ============================================================================

void PdfSearchEngine::scanAllPages(const QString& text, bool caseSensitive, bool wholeWord)
{
    // Cancel any in-flight scan first.
    m_scanCancelled.store(true);
    m_scanWatcher.waitForFinished();
    m_scanCancelled.store(false);

    // If the query changed, stop every worker before mutating the shared
    // parameters / cache (workers read m_searchText and m_cache lock-free-ish).
    if (text != m_searchText || caseSensitive != m_caseSensitive || wholeWord != m_wholeWord) {
        m_searchCancelled.store(true);
        m_searchWatcher.waitForFinished();
        m_searchCancelled.store(false);

        m_precacheCancelled.store(true);
        m_precacheWatcher.waitForFinished();
        m_precacheCancelled.store(false);

        clearCache();
        m_searchText = text;
        m_caseSensitive = caseSensitive;
        m_wholeWord = wholeWord;
    }

    // Edgeless documents are out of scope for page-axis markers (Q13.30).
    if (!m_document || text.isEmpty() || m_document->isEdgeless()) {
        emit scanComplete(0);
        return;
    }

    // Pre-open every source's provider on the main thread (see findNext()).
    m_document->ensureAllPdfProvidersLoaded();

    QFuture<void> future = QtConcurrent::run([this]() {
        doScanAll();
    });
    m_scanWatcher.setFuture(future);
}

void PdfSearchEngine::doScanAll()
{
    if (!m_document) {
        emit scanComplete(0);
        return;
    }

    const int totalPages = m_document->pageCount();
    int total = 0;

    for (int page = 0; page < totalPages; ++page) {
        if (m_scanCancelled.load()) {
            return;  // superseded/cancelled: do not emit scanComplete
        }

        QVector<PdfSearchMatch> matches = getCachedOrSearch(page);
        total += matches.size();
        if (!matches.isEmpty()) {
            emit pageScanned(page, matches);  // queued to the main thread
        }
    }

    if (!m_scanCancelled.load()) {
        emit scanComplete(total);
    }
}

void PdfSearchEngine::cancelScan()
{
    m_scanCancelled.store(true);
}

// ============================================================================
// Cancel
// ============================================================================

void PdfSearchEngine::cancel()
{
    m_searchCancelled.store(true);
    m_precacheCancelled.store(true);
}

void PdfSearchEngine::cancelAndWait()
{
    m_searchCancelled.store(true);
    m_precacheCancelled.store(true);
    m_scanCancelled.store(true);
    m_searchWatcher.waitForFinished();
    m_precacheWatcher.waitForFinished();
    m_scanWatcher.waitForFinished();
    m_searchCancelled.store(false);
    m_precacheCancelled.store(false);
    m_scanCancelled.store(false);
}

