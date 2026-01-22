#include "PdfSearchEngine.h"
#include "PdfProvider.h"
#include "../core/Document.h"

#include <QDebug>
#include <QtConcurrent/QtConcurrent>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PdfSearchEngine::PdfSearchEngine(QObject *parent)
    : QObject(parent)
{
    connect(&m_searchWatcher, &QFutureWatcher<void>::finished,
            this, &PdfSearchEngine::onSearchFinished);
    connect(&m_precacheWatcher, &QFutureWatcher<void>::finished,
            this, &PdfSearchEngine::onPrecacheFinished);
}

PdfSearchEngine::~PdfSearchEngine()
{
    cancel();
    m_searchWatcher.waitForFinished();
    m_precacheWatcher.waitForFinished();
}

void PdfSearchEngine::setDocument(Document *doc)
{
    if (m_document != doc) {
        // Cancel any ongoing operations before changing document
        cancel();
        m_searchWatcher.waitForFinished();
        m_precacheWatcher.waitForFinished();
        
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
    }
}

// ============================================================================
// Cache Management
// ============================================================================

void PdfSearchEngine::clearCache()
{
    QMutexLocker lock(&m_cacheMutex);
    m_cache.clear();
}

int PdfSearchEngine::cacheSize() const
{
    QMutexLocker lock(&m_cacheMutex);
    return m_cache.size();
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
    
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->supportsTextExtraction()) {
        return matches;
    }
    
    // Get all text boxes on this page
    QVector<PdfTextBox> textBoxes = pdf->textBoxes(pageIndex);
    if (textBoxes.isEmpty()) {
        return matches;
    }
    
    // Build full page text and track positions
    QString pageText;
    QVector<QPair<int, int>> boxMapping;
    
    for (int i = 0; i < textBoxes.size(); ++i) {
        pageText += textBoxes[i].text;
        
        for (int j = 0; j < textBoxes[i].text.length(); ++j) {
            boxMapping.append({i, j});
        }
        
        if (i < textBoxes.size() - 1 && !pageText.endsWith(' ')) {
            pageText += ' ';
            boxMapping.append({-1, -1});
        }
    }
    
    Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    
    int searchPos = 0;
    int matchIndex = 0;
    
    while (searchPos < pageText.length()) {
        int foundPos = pageText.indexOf(text, searchPos, cs);
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
            int endPos = foundPos + text.length();
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
            match.pageIndex = pageIndex;
            match.matchIndex = matchIndex++;
            match.boundingRect = matchRect;
            matches.append(match);
        }
        
        searchPos = foundPos + 1;
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
    
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->isValid()) {
        QMutexLocker lock(&m_resultMutex);
        m_searchNotFound = true;
        m_hasResult = true;
        return;
    }
    
    int totalPages = pdf->pageCount();
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
                    for (int i = pageMatches.size() - 1; i >= 0; --i) {
                        if (startMatchIndex < 0 || pageMatches[i].matchIndex < startMatchIndex) {
                            foundIdx = i;
                            break;
                        }
                    }
                }
            } else {
                foundIdx = (direction > 0) ? 0 : pageMatches.size() - 1;
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
                int foundIdx = (direction > 0) ? 0 : startPageMatches.size() - 1;
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
    
    // Check if document is already fully cached
    if (m_document) {
        const PdfProvider* pdf = m_document->pdfProvider();
        if (pdf && pdf->isValid()) {
            int totalPages = pdf->pageCount();
            if (cacheSize() >= totalPages) {
                return;  // Already fully cached, no need to pre-cache
            }
        }
    }
    
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
    
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->isValid()) {
        return;
    }
    
    int totalPages = pdf->pageCount();
    
    // Cache the ENTIRE document for instant subsequent navigation
    // This runs in background, so it won't block UI
    for (int page = 0; page < totalPages; ++page) {
        if (m_precacheCancelled.load()) {
            return;
        }
        
        // Use getCachedOrSearch directly - it handles the cache check internally
        // This avoids double mutex locking (isPageCached + getCachedOrSearch)
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
        
        clearCache();
        m_searchText = text;
        m_caseSensitive = caseSensitive;
        m_wholeWord = wholeWord;
    }
    
    if (!m_document || text.isEmpty()) {
        emit notFound(false);
        return;
    }
    
    // Reset result state
    {
        QMutexLocker lock(&m_resultMutex);
        m_hasResult = false;
        m_searchNotFound = false;
    }
    
    // Start background search
    QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
        doSearch(startPage, startMatchIndex, 1);  // direction = 1 (forward)
    });
    m_searchWatcher.setFuture(future);
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
        
        clearCache();
        m_searchText = text;
        m_caseSensitive = caseSensitive;
        m_wholeWord = wholeWord;
    }
    
    if (!m_document || text.isEmpty()) {
        emit notFound(false);
        return;
    }
    
    // Reset result state
    {
        QMutexLocker lock(&m_resultMutex);
        m_hasResult = false;
        m_searchNotFound = false;
    }
    
    // Start background search
    QFuture<void> future = QtConcurrent::run([this, startPage, startMatchIndex]() {
        doSearch(startPage, startMatchIndex, -1);  // direction = -1 (backward)
    });
    m_searchWatcher.setFuture(future);
}

// ============================================================================
// Cancel
// ============================================================================

void PdfSearchEngine::cancel()
{
    m_searchCancelled.store(true);
    m_precacheCancelled.store(true);
}

