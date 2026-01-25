#pragma once

// ============================================================================
// ThumbnailRenderer - Async thumbnail generation for PagePanel
// ============================================================================
// Part of the Page Panel feature (Task 3.1)
// Renders page thumbnails in background threads using QtConcurrent.
// Emits thumbnailReady signal when rendering completes.
//
// Thread Safety (BUG-PERF-003 fix):
// Page data is snapshot-copied on the main thread before async rendering.
// Background threads never access live Document/Page objects.
// ============================================================================

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QMutex>
#include <QFuture>
#include <QFutureWatcher>
#include <QColor>

#include "../strokes/VectorStroke.h"
#include "../core/Page.h"

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
    /**
     * @brief Thread-safe snapshot of a layer's stroke data.
     * 
     * Contains deep copies of strokes that can be safely accessed
     * from background threads without synchronization.
     */
    struct LayerSnapshot {
        bool visible = true;
        qreal opacity = 1.0;
        QVector<VectorStroke> strokes;  // Deep copy of stroke data
    };
    
    /**
     * @brief Thread-safe snapshot of all data needed to render a thumbnail.
     * 
     * Created on the main thread by capturing page state, then passed
     * to background threads for rendering. Background threads never
     * access live Document/Page objects.
     */
    struct ThumbnailSnapshot {
        // Basic info
        int pageIndex = -1;
        int width = 0;
        qreal dpr = 1.0;
        QSizeF pageSize;
        
        // Background settings
        Page::BackgroundType backgroundType = Page::BackgroundType::None;
        QColor backgroundColor = Qt::white;
        QColor gridColor = QColor(200, 200, 200);
        int gridSpacing = 32;
        int lineSpacing = 32;
        
        // Pre-rendered PDF background (rendered on main thread)
        QPixmap pdfBackground;
        
        // Stroke layers (deep copied)
        QVector<LayerSnapshot> layers;
        
        // Pre-rendered objects layer (rendered on main thread)
        // Objects may contain QPixmap data that isn't safe to copy across threads
        QPixmap objectsLayer;
        bool hasObjects = false;
        
        // Validity flag
        bool valid = false;
    };
    
    /**
     * @brief Create a thread-safe snapshot of page data.
     * 
     * MUST be called on the main thread. Copies all data needed for
     * thumbnail rendering so background threads don't need to access
     * live Document/Page objects.
     * 
     * @param doc The document (main thread access only).
     * @param pageIndex The page index to snapshot.
     * @param width Target thumbnail width.
     * @param dpr Device pixel ratio.
     * @return Snapshot with all render data, or invalid snapshot on failure.
     */
    static ThumbnailSnapshot createSnapshot(Document* doc, int pageIndex, int width, qreal dpr);
    
    /**
     * @brief Render a thumbnail from a snapshot (called in worker thread).
     * 
     * This method is fully thread-safe - it only accesses the snapshot data
     * which was copied on the main thread. No live Document/Page access.
     * 
     * @param snapshot The thread-safe page snapshot.
     * @return The rendered thumbnail, or null pixmap on failure.
     */
    static QPixmap renderFromSnapshot(const ThumbnailSnapshot& snapshot);
    
    /**
     * @brief Start the next pending task if slots are available.
     */
    void startNextTask();
    
    // Pending pages that have been requested but not yet started
    QList<ThumbnailSnapshot> m_pendingTasks;
    
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

