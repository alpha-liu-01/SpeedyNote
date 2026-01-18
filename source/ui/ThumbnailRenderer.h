#pragma once

// ============================================================================
// ThumbnailRenderer - Async thumbnail generation for PagePanel
// ============================================================================
// Part of the Page Panel feature (Task 3.1)
// Renders page thumbnails in background threads using QtConcurrent.
// Emits thumbnailReady signal when rendering completes.
// ============================================================================

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QMutex>
#include <QFuture>
#include <QFutureWatcher>

class Document;

/**
 * @brief Async thumbnail renderer for the Page Panel.
 * 
 * Uses QtConcurrent to render page thumbnails in background threads.
 * Limits concurrent renders to avoid overwhelming the system.
 * Supports cancellation when document changes or panel scrolls fast.
 */
class ThumbnailRenderer : public QObject {
    Q_OBJECT
    
public:
    explicit ThumbnailRenderer(QObject* parent = nullptr);
    ~ThumbnailRenderer();
    
    /**
     * @brief Request a thumbnail for a specific page.
     * 
     * Returns immediately. When rendering completes, thumbnailReady is emitted.
     * If a request for the same page is already pending, this is a no-op.
     * 
     * @param doc The document to render from.
     * @param pageIndex The page index to render.
     * @param width Target thumbnail width in logical pixels.
     * @param dpr Device pixel ratio for high DPI support.
     */
    void requestThumbnail(Document* doc, int pageIndex, int width, qreal dpr);
    
    /**
     * @brief Cancel all pending thumbnail requests.
     * 
     * Call this when the document changes or when scrolling fast
     * to avoid rendering thumbnails that are no longer needed.
     */
    void cancelAll();
    
    /**
     * @brief Check if a thumbnail request is pending for a page.
     * @param pageIndex The page index to check.
     * @return True if a request is pending.
     */
    bool isPending(int pageIndex) const;
    
    /**
     * @brief Set maximum concurrent render tasks.
     * @param max Maximum number of concurrent renders (default: 2).
     */
    void setMaxConcurrentRenders(int max);
    
signals:
    /**
     * @brief Emitted when a thumbnail has been rendered.
     * @param pageIndex The page index that was rendered.
     * @param thumbnail The rendered thumbnail pixmap.
     */
    void thumbnailReady(int pageIndex, QPixmap thumbnail);
    
private slots:
    void onRenderFinished();
    
private:
    struct RenderTask {
        Document* doc;
        int pageIndex;
        int width;
        qreal dpr;
    };
    
    /**
     * @brief Render a thumbnail synchronously (called in worker thread).
     * 
     * This method is thread-safe and does not access any member variables
     * except through the task parameter.
     * 
     * @param task The render task parameters.
     * @return The rendered thumbnail, or null pixmap on failure.
     */
    static QPixmap renderThumbnailSync(const RenderTask& task);
    
    /**
     * @brief Start the next pending task if slots are available.
     */
    void startNextTask();
    
    // Pending pages that have been requested but not yet started
    QList<RenderTask> m_pendingTasks;
    
    // Pages currently being rendered
    QSet<int> m_activePages;
    
    // Future watchers for active renders
    QList<QFutureWatcher<QPair<int, QPixmap>>*> m_activeWatchers;
    
    // Mutex for thread safety
    mutable QMutex m_mutex;
    
    // Maximum concurrent renders
    int m_maxConcurrent = 2;
    
    // Flag to track if we're being destroyed
    bool m_shuttingDown = false;
};

