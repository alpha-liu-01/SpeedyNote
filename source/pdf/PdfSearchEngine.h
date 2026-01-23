#pragma once

// ============================================================================
// PdfSearchEngine - PDF text search functionality with caching
// ============================================================================
// Provides streaming search through PDF text content with match highlighting.
//
// Design:
// - Searches one page at a time to minimize memory usage
// - Caches search results per page for fast navigation
// - Uses background thread for non-blocking search
// - Pre-caches nearby pages after finding first result
// ============================================================================

#include <QString>
#include <QRectF>
#include <QVector>
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>

class Document;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief A single search match within a PDF page.
 */
struct PdfSearchMatch {
    int pageIndex = -1;      ///< Which page this match is on (0-based)
    int matchIndex = -1;     ///< Index within page matches (for cycling)
    QRectF boundingRect;     ///< Bounding rectangle in PDF coordinates (points)
    
    bool isValid() const { return pageIndex >= 0 && matchIndex >= 0; }
};

/**
 * @brief Current state of a search session.
 * 
 * Tracks the search parameters and current position for navigation.
 */
struct PdfSearchState {
    QString searchText;              ///< The text being searched for
    bool caseSensitive = false;      ///< Case-sensitive matching
    bool wholeWord = false;          ///< Whole word matching only
    
    int currentPageIndex = -1;       ///< Page of current match (-1 if none)
    int currentMatchIndex = -1;      ///< Index of current match on page (-1 if none)
    
    /// All matches on the current page (for cycling through them)
    QVector<PdfSearchMatch> currentPageMatches;
    
    /**
     * @brief Check if there is a current match.
     */
    bool hasCurrentMatch() const {
        return currentPageIndex >= 0 && 
               currentMatchIndex >= 0 && 
               currentMatchIndex < currentPageMatches.size();
    }
    
    /**
     * @brief Get the current match.
     * @return Current match, or invalid match if none.
     */
    PdfSearchMatch currentMatch() const {
        if (hasCurrentMatch()) {
            return currentPageMatches[currentMatchIndex];
        }
        return PdfSearchMatch();
    }
    
    /**
     * @brief Clear all state.
     */
    void clear() {
        searchText.clear();
        caseSensitive = false;
        wholeWord = false;
        currentPageIndex = -1;
        currentMatchIndex = -1;
        currentPageMatches.clear();
    }
    
    /**
     * @brief Reset match state but keep search parameters.
     */
    void resetMatch() {
        currentPageIndex = -1;
        currentMatchIndex = -1;
        currentPageMatches.clear();
    }
};

// ============================================================================
// Search Cache Entry
// ============================================================================

/**
 * @brief Cached search results for a single page.
 */
struct PdfSearchCacheEntry {
    int pageIndex = -1;
    QVector<PdfSearchMatch> matches;
    bool searched = false;  ///< True if page has been searched (even if no matches)
};

// ============================================================================
// Search Engine with Caching
// ============================================================================

/**
 * @brief Engine for searching text within PDF documents.
 * 
 * Features:
 * - Caches search results per page for fast repeat navigation
 * - Runs search in background thread for responsive UI
 * - Pre-caches nearby pages after finding first result
 * - LRU cache eviction to limit memory usage
 */
class PdfSearchEngine : public QObject {
    Q_OBJECT
    
public:
    explicit PdfSearchEngine(QObject *parent = nullptr);
    ~PdfSearchEngine() override;
    
    /**
     * @brief Set the document to search.
     * @param doc Document containing the PDF.
     */
    void setDocument(Document *doc);
    
    /**
     * @brief Find the next match.
     * @param text Text to search for.
     * @param caseSensitive Case-sensitive matching.
     * @param wholeWord Whole word matching only.
     * @param startPage Page to start searching from.
     * @param startMatchIndex Match index on start page to start after (-1 for beginning).
     * 
     * Searches forward from the given position. Wraps around to page 0
     * if the end is reached without finding a match.
     */
    void findNext(const QString& text, bool caseSensitive, bool wholeWord,
                  int startPage, int startMatchIndex = -1);
    
    /**
     * @brief Find the previous match.
     * @param text Text to search for.
     * @param caseSensitive Case-sensitive matching.
     * @param wholeWord Whole word matching only.
     * @param startPage Page to start searching from.
     * @param startMatchIndex Match index on start page to start before (-1 for end).
     * 
     * Searches backward from the given position. Wraps around to last page
     * if the beginning is reached without finding a match.
     */
    void findPrev(const QString& text, bool caseSensitive, bool wholeWord,
                  int startPage, int startMatchIndex = -1);
    
    /**
     * @brief Cancel any ongoing search.
     */
    void cancel();
    
    /**
     * @brief Clear the search cache.
     * Call when search parameters change or document changes.
     */
    void clearCache();
    
    /**
     * @brief Get current cache size.
     */
    int cacheSize() const;
    
signals:
    /**
     * @brief Emitted when a match is found.
     * @param match The found match.
     * @param allPageMatches All matches on the same page (for highlighting).
     */
    void matchFound(const PdfSearchMatch& match, 
                    const QVector<PdfSearchMatch>& allPageMatches);
    
    /**
     * @brief Emitted when search completes without finding a match.
     * @param wrapped True if the search wrapped around the entire document.
     */
    void notFound(bool wrapped);
    
    /**
     * @brief Emitted to update search progress.
     * @param currentPage Current page being searched.
     * @param totalPages Total pages in document.
     */
    void progressUpdated(int currentPage, int totalPages);
    
private slots:
    void onSearchFinished();
    void onPrecacheFinished();
    
private:
    /**
     * @brief Search a single page for matches.
     * @param pageIndex Page to search.
     * @param text Text to search for.
     * @param caseSensitive Case-sensitive matching.
     * @param wholeWord Whole word matching only.
     * @return All matches on the page.
     */
    QVector<PdfSearchMatch> searchPage(int pageIndex, const QString& text,
                                        bool caseSensitive, bool wholeWord) const;
    
    /**
     * @brief Get cached results for a page, or search if not cached.
     * @param pageIndex Page to get results for.
     * @return Cached or newly computed results.
     */
    QVector<PdfSearchMatch> getCachedOrSearch(int pageIndex);
    
    /**
     * @brief Check if a page is in the cache.
     */
    bool isPageCached(int pageIndex) const;
    
    /**
     * @brief Add results to cache.
     */
    void addToCache(int pageIndex, const QVector<PdfSearchMatch>& matches);
    
    /**
     * @brief Start background pre-caching around a page.
     */
    void startPrecaching(int centerPage, int direction);
    
    /**
     * @brief Background thread function for main search.
     */
    void doSearch(int startPage, int startMatchIndex, int direction);
    
    /**
     * @brief Background thread function for pre-caching.
     */
    void doPrecache(int centerPage, int direction);
    
    Document *m_document = nullptr;
    std::atomic<bool> m_searchCancelled{false};   ///< Cancellation for main search only
    std::atomic<bool> m_precacheCancelled{false}; ///< Cancellation for pre-cache only
    
    // Current search parameters
    QString m_searchText;
    bool m_caseSensitive = false;
    bool m_wholeWord = false;
    
    // Cache: pageIndex -> matches
    mutable QMutex m_cacheMutex;
    QHash<int, PdfSearchCacheEntry> m_cache;
    static constexpr int MAX_CACHE_SIZE = 2000;  ///< Max pages to cache (entire document)
    
    // Background search
    QFutureWatcher<void> m_searchWatcher;
    QFutureWatcher<void> m_precacheWatcher;
    
    // Result from background search (protected by mutex)
    mutable QMutex m_resultMutex;
    bool m_hasResult = false;
    PdfSearchMatch m_foundMatch;
    QVector<PdfSearchMatch> m_foundPageMatches;
    bool m_searchWrapped = false;
    bool m_searchNotFound = false;
    
    // Precache state
    std::atomic<bool> m_precaching{false};
};

