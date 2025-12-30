// ============================================================================
// DocumentViewport - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.1)
// ============================================================================

#include "DocumentViewport.h"
#include "../layers/VectorLayer.h"
#include "../pdf/PopplerPdfProvider.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QtMath>     // For qPow
#include <QtConcurrent>  // For async PDF rendering
#include <cmath>      // For std::floor, std::ceil
#include <algorithm>  // For std::remove_if
#include <limits>
#include <QDateTime>  // For timestamp
#include <QUuid>      // For stroke IDs
#include <QSet>       // For efficient ID lookup in eraseAt

// ===== Constructor & Destructor =====

DocumentViewport::DocumentViewport(QWidget* parent)
    : QWidget(parent)
{
    // Enable mouse tracking for hover effects (future)
    setMouseTracking(true);
    
    // Accept tablet events
    setAttribute(Qt::WA_TabletTracking, true);
    
    // CRITICAL: Reject touch events - we only want stylus/mouse input for drawing
    // Touch gestures are handled separately (will be added in Phase 4)
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    
    // Set focus policy for keyboard shortcuts
    setFocusPolicy(Qt::StrongFocus);
    
    // Set background color (will be painted over by pages)
    // CUSTOMIZABLE: Viewport background color (theme setting, visible in gaps between pages)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(64, 64, 64));  // Dark gray - TODO: Load from theme settings
    setPalette(pal);
    
    // Benchmark display timer - triggers repaint to update paint rate counter
    // Note: Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
    connect(&m_benchmarkDisplayTimer, &QTimer::timeout, this, [this]() {
        if (m_benchmarking) {
            // DebugOverlay widget handles its own updates, but we may want
            // to trigger viewport repaints for accurate paint rate measurement
            // during benchmarking (disabled for now to avoid unnecessary repaints)
        }
    });
    
    // PDF preload timer - debounces preload requests during rapid scrolling
    m_pdfPreloadTimer = new QTimer(this);
    m_pdfPreloadTimer->setSingleShot(true);
    connect(m_pdfPreloadTimer, &QTimer::timeout, this, &DocumentViewport::doAsyncPdfPreload);
    
    // Gesture timeout timer - fallback for detecting gesture end (zoom or pan)
    m_gestureTimeoutTimer = new QTimer(this);
    m_gestureTimeoutTimer->setSingleShot(true);
    connect(m_gestureTimeoutTimer, &QTimer::timeout, this, &DocumentViewport::onGestureTimeout);
    
    // Initialize PDF cache capacity based on default layout mode
    updatePdfCacheCapacity();
}

DocumentViewport::~DocumentViewport()
{
    // Cancel any pending preload requests
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->stop();
    }
    
    // Stop gesture timer
    if (m_gestureTimeoutTimer) {
        m_gestureTimeoutTimer->stop();
    }
    
    // Clear gesture cached frame (releases memory)
    m_gesture.reset();
    
    // Wait for and clean up any active async PDF watchers
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_activePdfWatchers.clear();
}

// ===== Document Management =====

void DocumentViewport::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    // End any active gesture (cached frame is from old document)
    if (m_gesture.isActive()) {
        m_gesture.reset();
        m_gestureTimeoutTimer->stop();
    }
    m_backtickHeld = false;  // Reset key tracking for new document
    
    // Clear undo/redo stacks (actions refer to old document)
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    m_undoStacks.clear();
    m_redoStacks.clear();
    m_edgelessUndoStack.clear();
    m_edgelessRedoStack.clear();
    
    m_document = doc;
    
    // Emit signals if undo/redo availability changed
    if (hadUndo) emit undoAvailableChanged(false);
    if (hadRedo) emit redoAvailableChanged(false);
    
    // Invalidate caches for new document
    invalidatePdfCache();
    invalidatePageLayoutCache();
    
    // Reset view state
    m_zoomLevel = 1.0;
    m_panOffset = QPointF(0, 0);
    m_currentPageIndex = 0;
    
    // If document exists, restore last accessed page
    if (m_document && m_document->lastAccessedPage > 0) {
        m_currentPageIndex = qMin(m_document->lastAccessedPage, 
                                   m_document->pageCount() - 1);
        // Will scroll to this page in a later task
    }
    
    // Trigger repaint
    update();
    
    // Emit signals
    emit zoomChanged(m_zoomLevel);
    emit panChanged(m_panOffset);
    emit currentPageChanged(m_currentPageIndex);
    emitScrollFractions();
}

// ===== Layout =====

void DocumentViewport::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == mode) {
        return;
    }
    
    m_layoutMode = mode;
    
    // Invalidate layout cache for new layout mode
    invalidatePageLayoutCache();
    
    // Update PDF cache capacity for new layout (Task 1.3.6)
    updatePdfCacheCapacity();
    
    // Recalculate layout and repaint
    clampPanOffset();
    update();
    emitScrollFractions();
}

void DocumentViewport::setPageGap(int gap)
{
    if (m_pageGap == gap) {
        return;
    }
    
    m_pageGap = qMax(0, gap);
    
    // Recalculate layout and repaint
    clampPanOffset();
    update();
    emitScrollFractions();
}

// ===== Document Change Notifications =====

void DocumentViewport::notifyDocumentStructureChanged()
{
    // Invalidate layout cache - page count or sizes changed
    invalidatePageLayoutCache();
    
    // Trigger repaint to show new/removed pages
    update();
    
    // Emit scroll signals (scroll range may have changed)
    emitScrollFractions();
}

// ===== Tool State Management (Task 2.1) =====

void DocumentViewport::setCurrentTool(ToolType tool)
{
    if (m_currentTool == tool) {
        return;
    }
    
    ToolType previousTool = m_currentTool;
    m_currentTool = tool;
    
    // CR-2B-1: Disable straight line mode when switching to Eraser or Lasso
    // (straight lines only work with Pen and Marker)
    if ((tool == ToolType::Eraser || tool == ToolType::Lasso) && m_straightLineMode) {
        m_straightLineMode = false;
        emit straightLineModeChanged(false);
    }
    
    // Task 2.10.9: Clear lasso selection when switching away from Lasso tool
    if (previousTool == ToolType::Lasso && tool != ToolType::Lasso) {
        // Apply any pending transform before switching
        if (m_lassoSelection.isValid() && m_lassoSelection.hasTransform()) {
            applySelectionTransform();
        } else {
            clearLassoSelection();
        }
    }
    
    // Update cursor and repaint for eraser cursor visibility
    update();
    
    emit toolChanged(tool);
}

void DocumentViewport::setPenColor(const QColor& color)
{
    if (m_penColor == color) {
        return;
    }
    
    m_penColor = color;
}

void DocumentViewport::setPenThickness(qreal thickness)
{
    // Clamp to reasonable range
    thickness = qBound(0.5, thickness, 100.0);
    
    if (qFuzzyCompare(m_penThickness, thickness)) {
        return;
    }
    
    m_penThickness = thickness;
}

void DocumentViewport::setEraserSize(qreal size)
{
    // Clamp to reasonable range
    size = qBound(5.0, size, 200.0);
    
    if (qFuzzyCompare(m_eraserSize, size)) {
        return;
    }
    
    m_eraserSize = size;
    
    // Repaint to update eraser cursor size
    if (m_currentTool == ToolType::Eraser) {
        update();
    }
}

// ===== Marker Tool (Task 2.8) =====

void DocumentViewport::setMarkerColor(const QColor& color)
{
    if (m_markerColor == color) {
        return;
    }
    m_markerColor = color;
}

void DocumentViewport::setMarkerThickness(qreal thickness)
{
    // Clamp to reasonable range (marker is typically wider than pen)
    thickness = qBound(1.0, thickness, 100.0);
    
    if (qFuzzyCompare(m_markerThickness, thickness)) {
        return;
    }
    m_markerThickness = thickness;
}

// ===== Straight Line Mode (Task 2.9) =====

void DocumentViewport::setStraightLineMode(bool enabled)
{
    if (m_straightLineMode == enabled) {
        return;
    }
    
    // If disabling while drawing, cancel the current straight line
    if (!enabled && m_isDrawingStraightLine) {
        m_isDrawingStraightLine = false;
        update();  // Clear the preview
    }
    
    // CR-2B-2: If enabling while on Eraser, switch to Pen first
    // (straight lines only work with Pen and Marker)
    if (enabled && m_currentTool == ToolType::Eraser) {
        m_currentTool = ToolType::Pen;
        emit toolChanged(ToolType::Pen);
    }
    
    m_straightLineMode = enabled;
    emit straightLineModeChanged(enabled);
}

// ===== View State Setters =====

void DocumentViewport::setZoomLevel(qreal zoom)
{
    // Apply mode-specific minimum zoom
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    
    // Clamp to valid range
    zoom = qBound(minZ, zoom, MAX_ZOOM);
    
    if (qFuzzyCompare(m_zoomLevel, zoom)) {
        return;
    }
    
    qreal oldDpi = effectivePdfDpi();
    m_zoomLevel = zoom;
    qreal newDpi = effectivePdfDpi();
    
    // Invalidate PDF cache if DPI changed significantly (Task 1.3.6)
    if (!qFuzzyCompare(oldDpi, newDpi)) {
        invalidatePdfCache();
    }
    
    // Note: Stroke caches are zoom-aware and will rebuild automatically
    // when ensureStrokeCacheValid() is called with the new zoom level.
    // No explicit invalidation needed - just lazy rebuild on next paint.
    
    // Clamp pan offset (bounds change with zoom)
    clampPanOffset();
    
    update();
    emit zoomChanged(m_zoomLevel);
    emitScrollFractions();
}

void DocumentViewport::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
    clampPanOffset();
    
    updateCurrentPageIndex();
    
    update();
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Preload PDF cache for adjacent pages after scroll (Task: PDF Performance Fix)
    // Safe here because scroll is user-initiated, not during rapid stroke drawing
    preloadPdfCache();
    
    // MEMORY FIX: Evict stroke caches for distant pages after scroll
    // This prevents unbounded memory growth when scrolling through large documents
    preloadStrokeCaches();
    
    // EDGELESS MEMORY FIX: Evict tiles that are far from visible area
    // This saves dirty tiles to disk and removes them from memory (Phase E5)
    evictDistantTiles();
}

void DocumentViewport::scrollToPage(int pageIndex)
{
    if (!m_document || m_document->pageCount() == 0) return;
    
    pageIndex = qBound(0, pageIndex, m_document->pageCount() - 1);
    
    // Get page position and scroll to show it at top of viewport
    QPointF pos = pagePosition(pageIndex);
    
    // Optionally add some margin from top
    pos.setY(pos.y() - 10);
    
    setPanOffset(pos);
    
    m_currentPageIndex = pageIndex;
    emit currentPageChanged(m_currentPageIndex);
}

void DocumentViewport::scrollBy(QPointF delta)
{
    setPanOffset(m_panOffset + delta);
}

void DocumentViewport::zoomToFit()
{
    if (!m_document || m_document->pageCount() == 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Get current page size
    const Page* page = m_document->page(m_currentPageIndex);
    if (!page) {
        setZoomLevel(1.0);
        return;
    }
    
    QSizeF pageSize = page->size;
    
    // Guard against zero-size pages
    if (pageSize.width() <= 0 || pageSize.height() <= 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Calculate zoom to fit page in viewport with some margin
    qreal marginFraction = 0.05;  // 5% margin on each side
    qreal availWidth = width() * (1.0 - 2 * marginFraction);
    qreal availHeight = height() * (1.0 - 2 * marginFraction);
    
    qreal zoomX = availWidth / pageSize.width();
    qreal zoomY = availHeight / pageSize.height();
    
    // Use the smaller zoom to fit both dimensions
    qreal newZoom = qMin(zoomX, zoomY);
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Set zoom and center on current page
    setZoomLevel(newZoom);
    
    // Center the page in viewport
    QPointF pagePos = pagePosition(m_currentPageIndex);
    QPointF pageCenter = pagePos + QPointF(pageSize.width() / 2, pageSize.height() / 2);
    
    // Calculate pan offset to center the page
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    m_panOffset = pageCenter - QPointF(viewWidth / 2, viewHeight / 2);
    
    clampPanOffset();
    update();
    emit panChanged(m_panOffset);
}

void DocumentViewport::zoomToWidth()
{
    if (!m_document || m_document->pageCount() == 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Get current page size
    const Page* page = m_document->page(m_currentPageIndex);
    if (!page) {
        setZoomLevel(1.0);
        return;
    }
    
    QSizeF pageSize = page->size;
    
    // Guard against zero-width pages
    if (pageSize.width() <= 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Calculate zoom to fit page width with some margin
    qreal marginFraction = 0.05;  // 5% margin on each side
    qreal availWidth = width() * (1.0 - 2 * marginFraction);
    
    qreal newZoom = availWidth / pageSize.width();
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Set zoom and adjust pan to keep current page visible
    setZoomLevel(newZoom);
    
    // Center horizontally on current page
    QPointF pagePos = pagePosition(m_currentPageIndex);
    qreal viewWidth = width() / m_zoomLevel;
    m_panOffset.setX(pagePos.x() + pageSize.width() / 2 - viewWidth / 2);
    
    clampPanOffset();
    update();
    emit panChanged(m_panOffset);
}

void DocumentViewport::scrollToHome()
{
    setPanOffset(QPointF(0, 0));
    m_currentPageIndex = 0;
    emit currentPageChanged(m_currentPageIndex);
}

void DocumentViewport::setHorizontalScrollFraction(qreal fraction)
{
    if (!m_document || m_document->pageCount() == 0) {
        return;
    }
    
    // Clamp fraction to valid range
    fraction = qBound(0.0, fraction, 1.0);
    
    // Calculate scrollable width
    QSizeF contentSize = totalContentSize();
    qreal viewportWidth = width() / m_zoomLevel;
    qreal scrollableWidth = contentSize.width() - viewportWidth;
    
    if (scrollableWidth <= 0) {
        // Content fits in viewport - no horizontal scroll needed
        return;
    }
    
    // Set pan offset based on fraction
    qreal newX = fraction * scrollableWidth;
    if (!qFuzzyCompare(m_panOffset.x(), newX)) {
        m_panOffset.setX(newX);
        clampPanOffset();
        emit panChanged(m_panOffset);
        update();
    }
}

void DocumentViewport::setVerticalScrollFraction(qreal fraction)
{
    if (!m_document || m_document->pageCount() == 0) {
        return;
    }
    
    // Clamp fraction to valid range
    fraction = qBound(0.0, fraction, 1.0);
    
    // Calculate scrollable height
    QSizeF contentSize = totalContentSize();
    qreal viewportHeight = height() / m_zoomLevel;
    qreal scrollableHeight = contentSize.height() - viewportHeight;
    
    if (scrollableHeight <= 0) {
        // Content fits in viewport - no vertical scroll needed
        return;
    }
    
    // Set pan offset based on fraction
    qreal newY = fraction * scrollableHeight;
    if (!qFuzzyCompare(m_panOffset.y(), newY)) {
        m_panOffset.setY(newY);
        clampPanOffset();
        updateCurrentPageIndex();
        emit panChanged(m_panOffset);
        update();
    }
}

// ===== Layout Engine (Task 1.3.2) =====

QPointF DocumentViewport::pagePosition(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QPointF(0, 0);
    }
    
    // For edgeless documents, there's only one page at origin
    if (m_document->isEdgeless()) {
        return QPointF(0, 0);
    }
    
    // Ensure cache is valid - O(n) rebuild only when dirty
    ensurePageLayoutCache();
    
    // O(1) lookup from cache
    qreal y = (pageIndex < m_pageYCache.size()) ? m_pageYCache[pageIndex] : 0;
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn:
            // X is always 0 for single column
            return QPointF(0, y);
        
        case LayoutMode::TwoColumn: {
            // Y comes from cache, just need to calculate X for right column
            int col = pageIndex % 2;
            qreal x = 0;
            
            if (col == 1) {
                // Right column - offset by left page width + gap
                int leftIdx = (pageIndex / 2) * 2;
                const Page* leftPage = m_document->page(leftIdx);
                if (leftPage) {
                    x = leftPage->size.width() + m_pageGap;
                }
            }
            
            return QPointF(x, y);
        }
    }
    
    return QPointF(0, 0);
}

QRectF DocumentViewport::pageRect(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QRectF();
    }
    
    const Page* page = m_document->page(pageIndex);
    if (!page) {
        return QRectF();
    }
    
    QPointF pos = pagePosition(pageIndex);
    return QRectF(pos, page->size);
}

QSizeF DocumentViewport::totalContentSize() const
{
    if (!m_document || m_document->pageCount() == 0) {
        return QSizeF(0, 0);
    }
    
    // For edgeless documents, return the single page size
    // (it can grow dynamically, but we report current size)
    if (m_document->isEdgeless()) {
        const Page* page = m_document->edgelessPage();
        return page ? page->size : QSizeF(0, 0);
    }
    
    qreal totalWidth = 0;
    qreal totalHeight = 0;
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn: {
            for (int i = 0; i < m_document->pageCount(); ++i) {
                const Page* page = m_document->page(i);
                if (page) {
                    totalWidth = qMax(totalWidth, page->size.width());
                    totalHeight += page->size.height();
                    if (i > 0) {
                        totalHeight += m_pageGap;
                    }
                }
            }
            break;
        }
        
        case LayoutMode::TwoColumn: {
            int numRows = (m_document->pageCount() + 1) / 2;
            
            for (int row = 0; row < numRows; ++row) {
                int leftIdx = row * 2;
                int rightIdx = row * 2 + 1;
                
                qreal rowWidth = 0;
                qreal rowHeight = 0;
                
                const Page* leftPage = m_document->page(leftIdx);
                if (leftPage) {
                    rowWidth += leftPage->size.width();
                    rowHeight = qMax(rowHeight, leftPage->size.height());
                }
                
                const Page* rightPage = m_document->page(rightIdx);
                if (rightPage) {
                    rowWidth += m_pageGap + rightPage->size.width();
                    rowHeight = qMax(rowHeight, rightPage->size.height());
                }
                
                totalWidth = qMax(totalWidth, rowWidth);
                totalHeight += rowHeight;
                if (row > 0) {
                    totalHeight += m_pageGap;
                }
            }
            break;
        }
    }
    
    return QSizeF(totalWidth, totalHeight);
}

int DocumentViewport::pageAtPoint(QPointF documentPt) const
{
    if (!m_document || m_document->pageCount() == 0) {
        return -1;
    }
    
    // For edgeless documents, the single page covers everything
    if (m_document->isEdgeless()) {
        const Page* page = m_document->edgelessPage();
        if (page) {
            return 0;
        }
        return -1;
    }
    
    // Ensure cache is valid for O(1) page position lookup
    ensurePageLayoutCache();
    
    int pageCount = m_document->pageCount();
    qreal y = documentPt.y();
    
    // For single column: use binary search on Y positions (O(log n))
    if (m_layoutMode == LayoutMode::SingleColumn && !m_pageYCache.isEmpty()) {
        // Binary search to find the page containing this Y coordinate
        int low = 0;
        int high = pageCount - 1;
        int candidate = -1;
        
        while (low <= high) {
            int mid = (low + high) / 2;
            qreal pageY = m_pageYCache[mid];
            
            if (y < pageY) {
                high = mid - 1;
            } else {
                candidate = mid;  // This page starts at or before our Y
                low = mid + 1;
            }
        }
        
        // Check if the point is actually within the candidate page
        if (candidate >= 0) {
            QRectF rect = pageRect(candidate);  // Now O(1)
            if (rect.contains(documentPt)) {
                return candidate;
            }
        }
        
        return -1;
    }
    
    // For two-column: linear search (still O(n) but pageRect is now O(1))
    for (int i = 0; i < pageCount; ++i) {
        QRectF rect = pageRect(i);
        if (rect.contains(documentPt)) {
            return i;
        }
    }
    
    return -1;
}

QRectF DocumentViewport::visibleRect() const
{
    // Convert viewport bounds to document coordinates
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    return QRectF(m_panOffset, QSizeF(viewWidth, viewHeight));
}

QVector<int> DocumentViewport::visiblePages() const
{
    QVector<int> result;
    
    if (!m_document || m_document->pageCount() == 0) {
        return result;
    }
    
    // For edgeless documents, page 0 is always visible
    if (m_document->isEdgeless()) {
        result.append(0);
        return result;
    }
    
    // Ensure cache is valid for O(1) page position lookup
    ensurePageLayoutCache();
    
    QRectF viewRect = visibleRect();
    int pageCount = m_document->pageCount();
    
    // For single column: use binary search to find visible range (O(log n))
    if (m_layoutMode == LayoutMode::SingleColumn && !m_pageYCache.isEmpty()) {
        qreal viewTop = viewRect.top();
        qreal viewBottom = viewRect.bottom();
        
        // Binary search for first page that might be visible
        int low = 0;
        int high = pageCount - 1;
        int firstCandidate = pageCount;  // Beyond last page
        
        while (low <= high) {
            int mid = (low + high) / 2;
            qreal pageY = m_pageYCache[mid];
            const Page* page = m_document->page(mid);
            qreal pageBottom = page ? (pageY + page->size.height()) : pageY;
            
            if (pageBottom < viewTop) {
                // Page is entirely above viewport
                low = mid + 1;
            } else {
                // Page might be visible
                firstCandidate = mid;
                high = mid - 1;
            }
        }
        
        // Now iterate from first candidate until pages are below viewport
        for (int i = firstCandidate; i < pageCount; ++i) {
            qreal pageY = m_pageYCache[i];
            if (pageY > viewBottom) {
                // This and all subsequent pages are below viewport
                break;
            }
            
            QRectF rect = pageRect(i);  // O(1) now
            if (rect.intersects(viewRect)) {
                result.append(i);
            }
        }
        
        return result;
    }
    
    // For two-column: linear search (pageRect is now O(1))
    for (int i = 0; i < pageCount; ++i) {
        QRectF rect = pageRect(i);
        if (rect.intersects(viewRect)) {
            result.append(i);
        }
    }
    
    return result;
}

// ===== Qt Event Overrides =====

void DocumentViewport::paintEvent(QPaintEvent* event)
{
    // Benchmark: track paint timestamps (Task 2.6)
    if (m_benchmarking) {
        m_paintTimestamps.push_back(m_benchmarkTimer.elapsed());
    }
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // ========== FAST PATH: Viewport Gesture (Zoom or Pan) ==========
    // During viewport gestures, draw transformed cached frame instead of re-rendering.
    // This provides 60+ FPS during rapid zoom/pan operations.
    if (m_gesture.isActive() && !m_gesture.cachedFrame.isNull() 
        && m_gesture.startZoom > 0) {  // Guard against division by zero
        
        // Fill background (for areas outside transformed frame)
        painter.fillRect(rect(), QColor(64, 64, 64));
        
        // Calculate frame size in LOGICAL pixels (not physical)
        // grab() returns a pixmap at device pixel ratio, so we must divide by DPR
        // to get the logical size that matches the widget's coordinate system
        qreal dpr = m_gesture.frameDevicePixelRatio;
        QSizeF logicalSize(m_gesture.cachedFrame.width() / dpr,
                           m_gesture.cachedFrame.height() / dpr);
        
        // Draw based on gesture type
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);  // Speed over quality
        
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            // ZOOM: Scale the cached frame around zoom center
            qreal relativeScale = m_gesture.targetZoom / m_gesture.startZoom;
            QSizeF scaledSize = logicalSize * relativeScale;
            
            // The zoom center should remain fixed in viewport coords
            QPointF center = m_gesture.zoomCenter;
            QPointF scaledOrigin = center - (center * relativeScale);
            
            painter.drawPixmap(QRectF(scaledOrigin, scaledSize), m_gesture.cachedFrame, 
                              m_gesture.cachedFrame.rect());
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            // PAN: Shift the cached frame by pan delta
            // Pan delta in document coords → convert to viewport pixels
            QPointF panDeltaDoc = m_gesture.targetPan - m_gesture.startPan;
            QPointF panDeltaPixels = panDeltaDoc * m_gesture.startZoom * -1.0;  // Negate: pan offset increase = viewport moves opposite
            
            painter.drawPixmap(panDeltaPixels, m_gesture.cachedFrame);
        }
        
        // Skip normal rendering during gesture
        return;
    }
    
    // ========== OPTIMIZATION: Dirty Region Rendering ==========
    // Only repaint what's needed. During stroke drawing, the dirty region is small.
    QRect dirtyRect = event->rect();
    bool isPartialUpdate = (dirtyRect.width() < width() / 2 || dirtyRect.height() < height() / 2);
    
    // Fill background - only the dirty region for partial updates
    if (isPartialUpdate) {
        painter.fillRect(dirtyRect, QColor(64, 64, 64));
    } else {
        painter.fillRect(rect(), QColor(64, 64, 64));
    }
    
    if (!m_document) {
        // No document - draw placeholder
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, 
                         tr("No document loaded"));
        return;
    }
    
    // ========== EDGELESS MODE ==========
    // Edgeless uses tiled rendering instead of page-based rendering
    if (m_document->isEdgeless()) {
        renderEdgelessMode(painter);
        
        // Draw eraser cursor
        if (!m_isDrawing || !isPartialUpdate) {
            drawEraserCursor(painter);
        }
        
        // Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
        // Toggle with Ctrl+Shift+D
        
        return;  // Done with edgeless rendering
    }
    
    // ========== PAGED MODE ==========
    // Get visible pages to render
    QVector<int> visible = visiblePages();
    
    // Apply view transform
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // Render each visible page
    // For partial updates, only render pages that intersect the dirty region
    for (int pageIdx : visible) {
        Page* page = m_document->page(pageIdx);
        if (!page) continue;
        
        // Get page position once (O(1) with cache, but avoid redundant calls)
        QPointF pos = pagePosition(pageIdx);
        
        // Check if this page intersects the dirty region (optimization for partial updates)
        if (isPartialUpdate) {
            QRectF pageRectInViewport = QRectF(
                (pos.x() - m_panOffset.x()) * m_zoomLevel,
                (pos.y() - m_panOffset.y()) * m_zoomLevel,
                page->size.width() * m_zoomLevel,
                page->size.height() * m_zoomLevel
            );
            if (!pageRectInViewport.intersects(dirtyRect)) {
                continue;  // Skip this page - it doesn't intersect dirty region
            }
        }
        
        painter.save();
        painter.translate(pos);
        
        // Render the page (background + content)
        renderPage(painter, page, pageIdx);
        
        painter.restore();
    }
    
    painter.restore();
    
    // Render current stroke with incremental caching (Task 2.3)
    // This is done AFTER restoring the painter transform because the cache
    // is in viewport coordinates (not document coordinates)
    if (m_isDrawing && !m_currentStroke.points.isEmpty() && m_activeDrawingPage >= 0) {
        renderCurrentStrokeIncremental(painter);
    }
    
    // Task 2.9: Draw straight line preview
    if (m_isDrawingStraightLine) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Transform coordinates to viewport
        QPointF vpStart, vpEnd;
        if (m_document && m_document->isEdgeless()) {
            // Edgeless: coordinates are in document space
            vpStart = documentToViewport(m_straightLineStart);
            vpEnd = documentToViewport(m_straightLinePreviewEnd);
        } else {
            // Paged: coordinates are in page-local space
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            vpStart = documentToViewport(m_straightLineStart + pageOrigin);
            vpEnd = documentToViewport(m_straightLinePreviewEnd + pageOrigin);
        }
        
        // Use current tool's color and thickness
        QColor previewColor = (m_currentTool == ToolType::Marker) 
                              ? m_markerColor : m_penColor;
        qreal previewThickness = (m_currentTool == ToolType::Marker)
                                 ? m_markerThickness : m_penThickness;
        
        QPen pen(previewColor, previewThickness * m_zoomLevel, 
                 Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(vpStart, vpEnd);
        
        painter.restore();
    }
    
    // Task 2.10: Draw lasso selection path while drawing
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if (m_isDrawingLasso && m_lassoPath.size() > 1) {
        renderLassoPathIncremental(painter);
    }
    
    // Task 2.10.3: Draw lasso selection (selected strokes + bounding box)
    if (m_lassoSelection.isValid()) {
        renderLassoSelection(painter);
    }
    
    // Draw eraser cursor (Task 2.4)
    // Skip during stroke drawing (partial updates for pen don't need eraser cursor)
    if (!m_isDrawing || !isPartialUpdate) {
        drawEraserCursor(painter);
    }
    
    // Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
    // Toggle with Ctrl+Shift+D
}

void DocumentViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    // End any gesture if active (cached frame size no longer matches)
    if (m_gesture.isActive()) {
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            endZoomGesture();
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
    }
    
    // Keep the same document point at viewport center after resize
    // This ensures content doesn't jump around during window resize or rotation
    
    if (!m_document || event->oldSize().isEmpty()) {
        // No document or first resize - just clamp and update
        clampPanOffset();
        update();
        emitScrollFractions();
        return;
    }
    
    // Calculate the document point that was at the center of the OLD viewport
    QPointF oldCenter(event->oldSize().width() / 2.0, event->oldSize().height() / 2.0);
    QPointF docPointAtOldCenter = oldCenter / m_zoomLevel + m_panOffset;
    
    // Calculate where the NEW center is in viewport coordinates
    QPointF newCenter(width() / 2.0, height() / 2.0);
    
    // Adjust pan offset so the same document point is at the NEW center
    // docPointAtOldCenter = newCenter / m_zoomLevel + m_panOffset
    // m_panOffset = docPointAtOldCenter - newCenter / m_zoomLevel
    m_panOffset = docPointAtOldCenter - newCenter / m_zoomLevel;
    
    // Clamp to valid bounds (content may now be smaller/larger relative to viewport)
    clampPanOffset();
    
    // Update current page index (visible area changed)
    updateCurrentPageIndex();
    
    // Emit signals and repaint
    emit panChanged(m_panOffset);
    emitScrollFractions();
    update();
}

void DocumentViewport::mousePressEvent(QMouseEvent* event)
{
    // Only handle left button for drawing
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    // CRITICAL: Reject touch-synthesized mouse events
    // Touch input should not draw - only stylus and real mouse
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active (avoid duplicate events)
    if (m_pointerActive && m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Press);
    handlePointerEvent(pe);
    event->accept();
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event)
{
    // CRITICAL: Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active
    if (m_pointerActive && m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    // Process move if we have an active pointer or for hover
    if (m_pointerActive || (event->buttons() & Qt::LeftButton)) {
        PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Move);
        handlePointerEvent(pe);
    } else {
        // Track position for eraser cursor even when not pressing (hover)
        QPointF oldPos = m_lastPointerPos;
        m_lastPointerPos = event->position();
        
        // Request repaint if eraser tool is active (to update cursor)
        // Use targeted update for efficiency
        if (m_currentTool == ToolType::Eraser) {
            qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
            // Union of old and new cursor positions
            QRectF dirtyRect(m_lastPointerPos.x() - eraserRadius, m_lastPointerPos.y() - eraserRadius,
                             eraserRadius * 2, eraserRadius * 2);
            QRectF oldRect(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                           eraserRadius * 2, eraserRadius * 2);
            update(dirtyRect.united(oldRect).toRect());
        }
    }
    event->accept();
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    // CRITICAL: Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active
    if (m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Release);
    handlePointerEvent(pe);
    event->accept();
}

void DocumentViewport::wheelEvent(QWheelEvent* event)
{
    if (!m_document) {
        event->ignore();
        return;
    }
    
    // Get scroll delta (in degrees * 8, or pixels for high-res touchpads)
    QPoint pixelDelta = event->pixelDelta();
    QPoint angleDelta = event->angleDelta();
    
    // Check for Ctrl modifier → Zoom (deferred rendering)
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom at cursor position using deferred gesture API
        qreal zoomDelta = 0;
        
        if (!angleDelta.isNull()) {
            // Mouse wheel: 120 units = 15 degrees = one "step"
            zoomDelta = angleDelta.y() / 120.0;
        } else if (!pixelDelta.isNull()) {
            // Touchpad: use pixel delta scaled down
            zoomDelta = pixelDelta.y() / 50.0;
        }
        
        if (qFuzzyIsNull(zoomDelta)) {
            event->accept();
            return;
        }
        
        // Calculate zoom factor (multiplicative for consistent feel)
        qreal zoomFactor = qPow(1.1, zoomDelta);  // 10% per step
        
        // Use deferred zoom gesture API (will capture frame on first call)
        updateZoomGesture(zoomFactor, event->position());
        
        event->accept();
        return;
    }
    
    // Scroll with deferred rendering for Shift/backtick modifiers
    QPointF scrollDelta;
    
    if (!pixelDelta.isNull()) {
        // Touchpad: use pixel delta directly (in viewport pixels)
        // Convert to document units
        scrollDelta = QPointF(-pixelDelta.x(), -pixelDelta.y()) / m_zoomLevel;
    } else if (!angleDelta.isNull()) {
        // Mouse wheel: convert degrees to scroll distance
        // 120 units = one step, scroll by ~40 document units per step
        // CUSTOMIZABLE: Scroll speed (user preference, range: 10-100)
        qreal scrollSpeed = 40.0;  // TODO: Load from user settings
        scrollDelta.setX(-angleDelta.x() / 120.0 * scrollSpeed);
        scrollDelta.setY(-angleDelta.y() / 120.0 * scrollSpeed);
    }
    
    if (!scrollDelta.isNull()) {
        // Check for Shift modifier → Deferred horizontal pan
        if (event->modifiers() & Qt::ShiftModifier) {
            // Swap X and Y for horizontal scroll, then use deferred pan
            QPointF horizontalDelta(scrollDelta.y(), scrollDelta.x());
            updatePanGesture(horizontalDelta);
            event->accept();
            return;
        }
        
        // Check for backtick (`) key → Deferred vertical pan
        // Using custom key tracking since ` is not a modifier key
        if (m_backtickHeld) {
            // Vertical scroll with deferred rendering
            updatePanGesture(scrollDelta);
            event->accept();
            return;
        }
        
        // Plain wheel (no modifier) → Immediate scroll (unchanged behavior)
        scrollBy(scrollDelta);
    }
    
    event->accept();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event)
{
    // Track backtick key for deferred vertical pan
    // Ignore auto-repeat events - only track actual key press
    if (event->key() == Qt::Key_QuoteLeft && !event->isAutoRepeat()) {
        m_backtickHeld = true;
        event->accept();
        return;
    }
    
    // Task 2.10.8: Lasso selection keyboard shortcuts
    if (m_currentTool == ToolType::Lasso) {
        // Copy (Ctrl+C)
        if (event->matches(QKeySequence::Copy)) {
            copySelection();
            event->accept();
            return;
        }
        // Cut (Ctrl+X)
        if (event->matches(QKeySequence::Cut)) {
            cutSelection();
            event->accept();
            return;
        }
        // Paste (Ctrl+V)
        if (event->matches(QKeySequence::Paste)) {
            pasteSelection();
            event->accept();
            return;
        }
        // Delete selection
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            if (m_lassoSelection.isValid()) {
                deleteSelection();
                event->accept();
                return;
            }
        }
        // Cancel selection (Escape)
        if (event->key() == Qt::Key_Escape) {
            if (m_lassoSelection.isValid() || m_isDrawingLasso) {
                cancelSelectionTransform();
                event->accept();
                return;
            }
        }
    }
    
    // Tool switching shortcuts (for testing and quick access)
    switch (event->key()) {
        case Qt::Key_P:
            // P = Pen tool
            setCurrentTool(ToolType::Pen);
            event->accept();
            return;
            
        case Qt::Key_E:
            // E = Eraser tool
            setCurrentTool(ToolType::Eraser);
            event->accept();
            return;
            
        case Qt::Key_B:
            // B = Toggle benchmark
            if (m_benchmarking) {
                stopBenchmark();
            } else {
                startBenchmark();
            }
            update();
            event->accept();
            return;
            
        case Qt::Key_Z:
            // Ctrl+Z = Undo
            if (event->modifiers() & Qt::ControlModifier) {
                undo();
                event->accept();
                return;
            }
            break;
            
        case Qt::Key_Y:
            // Ctrl+Y = Redo
            if (event->modifiers() & Qt::ControlModifier) {
                redo();
                event->accept();
                return;
            }
            break;
    }
    
    // Pass unhandled keys to parent
    QWidget::keyPressEvent(event);
}

void DocumentViewport::keyReleaseEvent(QKeyEvent* event)
{
    // Ctrl release ends zoom gesture (if active)
    if (event->key() == Qt::Key_Control && m_gesture.activeType == ViewportGestureState::Zoom) {
        endZoomGesture();
        event->accept();
        return;
    }
    
    // Shift release ends pan gesture (if active)
    if (event->key() == Qt::Key_Shift && m_gesture.activeType == ViewportGestureState::Pan) {
        endPanGesture();
        event->accept();
        return;
    }
    
    // Backtick (`) release ends pan gesture (if active)
    // Ignore auto-repeat events - only handle actual key release
    if (event->key() == Qt::Key_QuoteLeft && !event->isAutoRepeat()) {
        m_backtickHeld = false;
        if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
        event->accept();
        return;
    }
    
    // Pass unhandled keys to parent
    QWidget::keyReleaseEvent(event);
}

void DocumentViewport::focusOutEvent(QFocusEvent* event)
{
    // Reset backtick tracking (user can't release key if we don't have focus)
    m_backtickHeld = false;
    
    // End any active gesture if window loses focus (user can't release modifier otherwise)
    if (m_gesture.isActive()) {
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            endZoomGesture();
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
    }
    
    QWidget::focusOutEvent(event);
}

void DocumentViewport::tabletEvent(QTabletEvent* event)
{
    // Determine event type
    PointerEvent::Type peType;
    switch (event->type()) {
        case QEvent::TabletPress:
            peType = PointerEvent::Press;
            break;
        case QEvent::TabletMove:
            peType = PointerEvent::Move;
            break;
        case QEvent::TabletRelease:
            peType = PointerEvent::Release;
            break;
        default:
            event->ignore();
            return;
    }
    
    PointerEvent pe = tabletToPointerEvent(event, peType);
    handlePointerEvent(pe);
    event->accept();
}

// ===== Coordinate Transforms (Task 1.3.5) =====

QPointF DocumentViewport::viewportToDocument(QPointF viewportPt) const
{
    // Viewport coordinates are in logical (widget) pixels
    // Document coordinates are in our custom unit system
    // 
    // The viewport shows a portion of the document:
    // - panOffset is the top-left corner of the viewport in document coords
    // - zoomLevel scales the document (zoom 2.0 = document appears twice as large)
    //
    // viewportPt = (docPt - panOffset) * zoomLevel
    // So: docPt = viewportPt / zoomLevel + panOffset
    
    return viewportPt / m_zoomLevel + m_panOffset;
}

QPointF DocumentViewport::documentToViewport(QPointF docPt) const
{
    // Inverse of viewportToDocument
    // viewportPt = (docPt - panOffset) * zoomLevel
    
    return (docPt - m_panOffset) * m_zoomLevel;
}

PageHit DocumentViewport::viewportToPage(QPointF viewportPt) const
{
    // Convert viewport → document → page
    QPointF docPt = viewportToDocument(viewportPt);
    return documentToPage(docPt);
}

QPointF DocumentViewport::pageToViewport(int pageIndex, QPointF pagePt) const
{
    // Convert page → document → viewport
    QPointF docPt = pageToDocument(pageIndex, pagePt);
    return documentToViewport(docPt);
}

QPointF DocumentViewport::pageToDocument(int pageIndex, QPointF pagePt) const
{
    // Page-local coordinates are relative to the page's top-left corner
    // Document coordinates are absolute within the document
    //
    // docPt = pagePosition + pagePt
    
    QPointF pagePos = pagePosition(pageIndex);
    return pagePos + pagePt;
}

PageHit DocumentViewport::documentToPage(QPointF docPt) const
{
    PageHit hit;
    
    // Find which page contains this document point
    int pageIdx = pageAtPoint(docPt);
    if (pageIdx < 0) {
        // Point is not on any page (in the gaps or outside content)
        return hit;  // Invalid hit
    }
    
    // Convert document point to page-local coordinates
    QPointF pagePos = pagePosition(pageIdx);
    
    hit.pageIndex = pageIdx;
    hit.pagePoint = docPt - pagePos;
    
    return hit;
}

// ===== Pan & Zoom Helpers (Task 1.3.4) =====

QPointF DocumentViewport::viewportCenter() const
{
    // Get center of viewport in document coordinates
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    return m_panOffset + QPointF(viewWidth / 2, viewHeight / 2);
}

void DocumentViewport::zoomAtPoint(qreal newZoom, QPointF viewportPt)
{
    if (qFuzzyCompare(newZoom, m_zoomLevel)) {
        return;
    }
    
    // Convert viewport point to document coordinates at current zoom
    QPointF docPt = viewportPt / m_zoomLevel + m_panOffset;
    
    // Set new zoom
    qreal oldZoom = m_zoomLevel;
    m_zoomLevel = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Calculate new pan offset to keep docPt at the same viewport position
    // viewportPt = (docPt - m_panOffset) * m_zoomLevel
    // m_panOffset = docPt - viewportPt / m_zoomLevel
    m_panOffset = docPt - viewportPt / m_zoomLevel;
    
    clampPanOffset();
    updateCurrentPageIndex();
    
    if (!qFuzzyCompare(oldZoom, m_zoomLevel)) {
        emit zoomChanged(m_zoomLevel);
    }
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    update();
}

// ===== Deferred Zoom Gesture (Task 2.3 - Zoom Optimization) =====

void DocumentViewport::beginZoomGesture(QPointF centerPoint)
{
    if (m_gesture.isActive()) {
        return;  // Already in gesture
    }
    
    m_gesture.activeType = ViewportGestureState::Zoom;
    m_gesture.startZoom = m_zoomLevel;
    m_gesture.targetZoom = m_zoomLevel;
    m_gesture.zoomCenter = centerPoint;
    m_gesture.startPan = m_panOffset;
    m_gesture.targetPan = m_panOffset;
    
    // Capture current viewport as cached frame for fast scaling
    m_gesture.cachedFrame = grab();
    // Store device pixel ratio for correct scaling on high-DPI displays
    m_gesture.frameDevicePixelRatio = m_gesture.cachedFrame.devicePixelRatio();
    
    // Grab keyboard focus to receive keyReleaseEvent when modifier is released
    setFocus(Qt::OtherFocusReason);
    
    // Start timeout timer (fallback for gesture end detection)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
}

void DocumentViewport::updateZoomGesture(qreal scaleFactor, QPointF centerPoint)
{
    // Auto-begin gesture if not already active
    if (!m_gesture.isActive()) {
        beginZoomGesture(centerPoint);
    }
    
    // Accumulate zoom (multiplicative for smooth feel)
    m_gesture.targetZoom *= scaleFactor;
    m_gesture.targetZoom = qBound(MIN_ZOOM, m_gesture.targetZoom, MAX_ZOOM);
    m_gesture.zoomCenter = centerPoint;
    
    // Restart timeout timer (each event resets the timeout)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
    
    // Trigger repaint (will use fast cached frame scaling)
    update();
}

void DocumentViewport::endZoomGesture()
{
    if (m_gesture.activeType != ViewportGestureState::Zoom) {
        return;  // Not in zoom gesture
    }
    
    // Stop timeout timer
    m_gestureTimeoutTimer->stop();
    
    // Get final zoom level with mode-specific min zoom
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    qreal finalZoom = qBound(minZ, m_gesture.targetZoom, MAX_ZOOM);
    
    // Calculate new pan offset to keep center point fixed
    // The center point should map to the same document point before and after zoom
    QPointF center = m_gesture.zoomCenter;
    QPointF docPtAtCenter = center / m_gesture.startZoom + m_gesture.startPan;
    QPointF newPan = docPtAtCenter - center / finalZoom;
    
    // Clear gesture state BEFORE applying zoom (to avoid recursion in paintEvent)
    m_gesture.reset();
    
    // Apply final zoom and pan
    m_zoomLevel = finalZoom;
    m_panOffset = newPan;
    
    // Invalidate PDF cache (DPI changed)
    invalidatePdfCache();
    
    // Clamp and emit signals
    clampPanOffset();
    updateCurrentPageIndex();
    
    emit zoomChanged(m_zoomLevel);
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Trigger full re-render at new DPI
    update();
    
    // Preload PDF cache for new zoom level
    preloadPdfCache();
}

void DocumentViewport::beginPanGesture()
{
    if (m_gesture.isActive()) {
        return;  // Already in gesture
    }
    
    m_gesture.activeType = ViewportGestureState::Pan;
    m_gesture.startZoom = m_zoomLevel;
    m_gesture.targetZoom = m_zoomLevel;
    m_gesture.startPan = m_panOffset;
    m_gesture.targetPan = m_panOffset;
    
    // Capture current viewport as cached frame for fast shifting
    m_gesture.cachedFrame = grab();
    // Store device pixel ratio for correct positioning on high-DPI displays
    m_gesture.frameDevicePixelRatio = m_gesture.cachedFrame.devicePixelRatio();
    
    // Grab keyboard focus to receive keyReleaseEvent when modifier is released
    setFocus(Qt::OtherFocusReason);
    
    // Start timeout timer (fallback for gesture end detection)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
}

void DocumentViewport::updatePanGesture(QPointF panDelta)
{
    // Auto-begin gesture if not already active
    if (!m_gesture.isActive()) {
        beginPanGesture();
    }
    
    // Accumulate pan offset (additive)
    m_gesture.targetPan += panDelta;
    
    // Note: We don't clamp targetPan here - let endPanGesture handle clamping
    // This allows the visual feedback to show unclamped pan during the gesture
    
    // Restart timeout timer (each event resets the timeout)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
    
    // Trigger repaint (will use fast cached frame shifting)
    update();
}

void DocumentViewport::endPanGesture()
{
    if (m_gesture.activeType != ViewportGestureState::Pan) {
        return;  // Not in pan gesture
    }
    
    // Stop timeout timer
    m_gestureTimeoutTimer->stop();
    
    // Get final pan offset
    QPointF finalPan = m_gesture.targetPan;
    
    // Clear gesture state BEFORE applying pan (to avoid recursion in paintEvent)
    m_gesture.reset();
    
    // Apply final pan
    m_panOffset = finalPan;
    
    // Clamp and emit signals
    clampPanOffset();
    updateCurrentPageIndex();
    
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Trigger full re-render
    update();
    
    // Preload PDF cache for new viewport position
    preloadPdfCache();
    
    // Evict distant tiles if in edgeless mode
    if (m_document && m_document->isEdgeless()) {
        evictDistantTiles();
    }
}

void DocumentViewport::onGestureTimeout()
{
    // Timeout reached - end the active gesture
    if (m_gesture.activeType == ViewportGestureState::Zoom) {
        endZoomGesture();
    } else if (m_gesture.activeType == ViewportGestureState::Pan) {
        endPanGesture();
    }
}

// ===== PDF Cache Helpers (Task 1.3.6) =====

QPixmap DocumentViewport::getCachedPdfPage(int pageIndex, qreal dpi)
{
    if (!m_document || !m_document->isPdfLoaded()) {
        return QPixmap();
    }
    
    // Thread-safe cache lookup
    QMutexLocker locker(&m_pdfCacheMutex);
    
    // Check if we have this page cached at the right DPI
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(pageIndex, dpi)) {
            return entry.pixmap;  // Cache hit - fast path
        }
    }
    
    // Cache miss - render synchronously (for visible pages that MUST be shown)
    // This should only happen on first paint of a new page
    locker.unlock();  // Release mutex during expensive render
    
    // Build cache contents string for debug
    QString cacheContents;
    {
        QMutexLocker debugLocker(&m_pdfCacheMutex);
        for (const PdfCacheEntry& e : m_pdfCache) {
            if (!cacheContents.isEmpty()) cacheContents += ",";
            cacheContents += QString::number(e.pageIndex);
        }
    }
    qDebug() << "PDF CACHE MISS: rendering page" << pageIndex 
             << "| cache has [" << cacheContents << "] capacity=" << m_pdfCacheCapacity;
    
    // Render the page (expensive operation - done outside mutex)
    QImage pdfImage = m_document->renderPdfPageToImage(pageIndex, dpi);
    if (pdfImage.isNull()) {
        return QPixmap();
    }
    
    QPixmap pixmap = QPixmap::fromImage(pdfImage);
    
    // Add to cache (thread-safe)
    locker.relock();
    
    // Double-check it wasn't added by another thread while we were rendering
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(pageIndex, dpi)) {
            return entry.pixmap;  // Another thread added it
        }
    }
    
    PdfCacheEntry entry;
    entry.pageIndex = pageIndex;
    entry.dpi = dpi;
    entry.pixmap = pixmap;
    
    // If cache is full, evict the page FURTHEST from current page (smart eviction)
    // This prevents evicting pages we're about to need (like the next visible page)
    if (m_pdfCache.size() >= m_pdfCacheCapacity) {
        int evictIndex = 0;
        int maxDistance = -1;
        for (int i = 0; i < m_pdfCache.size(); ++i) {
            int distance = qAbs(m_pdfCache[i].pageIndex - pageIndex);
            if (distance > maxDistance) {
                maxDistance = distance;
                evictIndex = i;
            }
        }
        m_pdfCache.removeAt(evictIndex);
    }
    
    m_pdfCache.append(entry);
    m_cachedDpi = dpi;
    
    return pixmap;
}

void DocumentViewport::preloadPdfCache()
{
    // Debounce: restart timer on each call
    // Actual preloading happens after user stops scrolling
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->start(PDF_PRELOAD_DELAY_MS);
    }
}

void DocumentViewport::doAsyncPdfPreload()
{
    if (!m_document || !m_document->isPdfLoaded()) {
        return;
    }
    
    QVector<int> visible = visiblePages();
    if (visible.isEmpty()) {
        return;
    }
    
    int first = visible.first();
    int last = visible.last();
    
    // Pre-load ±1 pages beyond visible
    int preloadStart = qMax(0, first - 1);
    int preloadEnd = qMin(m_document->pageCount() - 1, last + 1);
    
    qreal dpi = effectivePdfDpi();
    QString pdfPath = m_document->pdfPath();
    
    if (pdfPath.isEmpty()) {
        return;  // No PDF path available
    }
    
    // Collect pages that need preloading
    QList<int> pagesToPreload;
    {
        QMutexLocker locker(&m_pdfCacheMutex);
        for (int i = preloadStart; i <= preloadEnd; ++i) {
            Page* page = m_document->page(i);
            if (page && page->backgroundType == Page::BackgroundType::PDF) {
                int pdfPageNum = page->pdfPageNumber;
                
                // Check if already cached
                bool alreadyCached = false;
                for (const PdfCacheEntry& entry : m_pdfCache) {
                    if (entry.matches(pdfPageNum, dpi)) {
                        alreadyCached = true;
                        break;
                    }
                }
                
                if (!alreadyCached) {
                    pagesToPreload.append(pdfPageNum);
                }
            }
        }
    }
    
    if (pagesToPreload.isEmpty()) {
        return;  // All pages already cached
    }
    
    // Launch async render for each page that needs caching
    for (int pdfPageNum : pagesToPreload) {
        QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
        
        // Track watcher for cleanup
        m_activePdfWatchers.append(watcher);
        
        // THREAD SAFETY FIX: QPixmap must only be created on the main thread.
        // The background thread returns QImage, and we convert to QPixmap here
        // in the finished handler which runs on the main thread.
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, pdfPageNum, dpi]() {
            m_activePdfWatchers.removeOne(watcher);
            
            // Get the rendered image from the background task
            QImage pdfImage = watcher->result();
            watcher->deleteLater();
            
            // Check if rendering failed
            if (pdfImage.isNull()) {
                return;
            }
            
            // SAFE: QPixmap::fromImage on main thread
            QPixmap pixmap = QPixmap::fromImage(pdfImage);
            
            // Add to cache (thread-safe access to shared cache)
            QMutexLocker locker(&m_pdfCacheMutex);
            
            // Check if already added (race condition prevention)
            for (const PdfCacheEntry& entry : m_pdfCache) {
                if (entry.matches(pdfPageNum, dpi)) {
                    return;  // Already cached by another path
                }
            }
            
            PdfCacheEntry entry;
            entry.pageIndex = pdfPageNum;
            entry.dpi = dpi;
            entry.pixmap = pixmap;
            
            // Evict page FURTHEST from this page (smart eviction)
            if (m_pdfCache.size() >= m_pdfCacheCapacity) {
                int evictIndex = 0;
                int maxDistance = -1;
                for (int i = 0; i < m_pdfCache.size(); ++i) {
                    int distance = qAbs(m_pdfCache[i].pageIndex - pdfPageNum);
                    if (distance > maxDistance) {
                        maxDistance = distance;
                        evictIndex = i;
                    }
                }
                m_pdfCache.removeAt(evictIndex);
            }
            
            m_pdfCache.append(entry);
            m_cachedDpi = dpi;
            
            // Trigger repaint to show newly cached page
            update();
        });
        
        // Background thread: render PDF to QImage (thread-safe)
        // NOTE: QImage is explicitly documented as thread-safe for read operations
        // and can be safely passed between threads.
        QFuture<QImage> future = QtConcurrent::run([pdfPageNum, dpi, pdfPath]() -> QImage {
            // Create thread-local PDF provider (each thread loads its own copy)
            PopplerPdfProvider threadPdf(pdfPath);
            if (!threadPdf.isValid()) {
                return QImage();  // Return null image on failure
            }
            
            // Render page using thread-local provider
            // This is the expensive operation (50-200ms) that we're offloading
            return threadPdf.renderPageToImage(pdfPageNum, dpi);
        });
        
        watcher->setFuture(future);
    }
    
    if (!pagesToPreload.isEmpty()) {
        qDebug() << "PDF async preload: started" << pagesToPreload.size() 
                 << "background renders for pages" << pagesToPreload;
    }
}

void DocumentViewport::invalidatePdfCache()
{
    // Cancel pending async preloads
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->stop();
    }
    
    // Thread-safe cache clear
    QMutexLocker locker(&m_pdfCacheMutex);
    if (!m_pdfCache.isEmpty()) {
        qDebug() << "PDF CACHE INVALIDATED: cleared" << m_pdfCache.size() << "entries";
    }
    m_pdfCache.clear();
    m_cachedDpi = 0;
}

void DocumentViewport::invalidatePdfCachePage(int pageIndex)
{
    // Thread-safe page removal
    QMutexLocker locker(&m_pdfCacheMutex);
    m_pdfCache.erase(
        std::remove_if(m_pdfCache.begin(), m_pdfCache.end(),
                       [pageIndex](const PdfCacheEntry& entry) {
                           return entry.pageIndex == pageIndex;
                       }),
        m_pdfCache.end()
    );
}

void DocumentViewport::updatePdfCacheCapacity()
{
    // Set capacity based on layout mode:
    // - Single column: visible (2) + ±2 buffer = 6 pages
    // - Two column: visible (4) + ±1 row buffer = 12 pages
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn:
            m_pdfCacheCapacity = 6;  // Visible + generous buffer for scroll direction changes
            break;
        case LayoutMode::TwoColumn:
            m_pdfCacheCapacity = 12;  // Visible + row buffer
            break;
    }
    
    // Thread-safe trim
    QMutexLocker locker(&m_pdfCacheMutex);
    while (m_pdfCache.size() > m_pdfCacheCapacity) {
        m_pdfCache.removeFirst();
    }
}

// ===== Page Layout Cache (Performance Optimization) =====

void DocumentViewport::ensurePageLayoutCache() const
{
    if (!m_pageLayoutDirty || !m_document) {
        return;
    }
    
    int pageCount = m_document->pageCount();
    m_pageYCache.resize(pageCount);
    
    if (m_document->isEdgeless() || pageCount == 0) {
        m_pageLayoutDirty = false;
        return;
    }
    
    // Build cache based on layout mode
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn: {
            qreal y = 0;
            for (int i = 0; i < pageCount; ++i) {
                m_pageYCache[i] = y;
                const Page* page = m_document->page(i);
                if (page) {
                    y += page->size.height() + m_pageGap;
                }
            }
            break;
        }
        
        case LayoutMode::TwoColumn: {
            // For two-column, we store the Y of each row
            // Y position is same for both pages in a row
            qreal y = 0;
            for (int i = 0; i < pageCount; ++i) {
                int row = i / 2;
                
                if (i % 2 == 0) {
                    // First page of row - calculate and store Y
                    m_pageYCache[i] = y;
                } else {
                    // Second page of row - same Y as first
                    m_pageYCache[i] = m_pageYCache[i - 1];
                    
                    // After second page, advance Y
                    qreal rowHeight = 0;
                    const Page* leftPage = m_document->page(i - 1);
                    const Page* rightPage = m_document->page(i);
                    if (leftPage) rowHeight = qMax(rowHeight, leftPage->size.height());
                    if (rightPage) rowHeight = qMax(rowHeight, rightPage->size.height());
                    y += rowHeight + m_pageGap;
                }
            }
            // Handle odd page count (last page is alone)
            if (pageCount % 2 == 1 && pageCount > 0) {
                // Last row Y already set, just need to ensure it's correct
            }
            break;
        }
    }
    
    m_pageLayoutDirty = false;
}

// ===== Stroke Cache Helpers (Task 1.3.7) =====

void DocumentViewport::preloadStrokeCaches()
{
    if (!m_document) {
        return;
    }
    
    QVector<int> visible = visiblePages();
    if (visible.isEmpty()) {
        return;
    }
    
    int first = visible.first();
    int last = visible.last();
    int pageCount = m_document->pageCount();
    
    // Pre-load ±1 pages beyond visible
    int preloadStart = qMax(0, first - 1);
    int preloadEnd = qMin(pageCount - 1, last + 1);
    
    // MEMORY OPTIMIZATION: Evict stroke caches for pages far from visible area
    // Keep caches for visible ±2 pages, release everything else
    // This prevents unbounded memory growth when scrolling through large documents
    static constexpr int STROKE_CACHE_BUFFER = 2;
    int keepStart = qMax(0, first - STROKE_CACHE_BUFFER);
    int keepEnd = qMin(pageCount - 1, last + STROKE_CACHE_BUFFER);
    
    // Evict caches for pages outside the keep range
    for (int i = 0; i < pageCount; ++i) {
        if (i < keepStart || i > keepEnd) {
            Page* page = m_document->page(i);
            if (page && page->hasLayerCachesAllocated()) {
                page->releaseLayerCaches();
            }
        }
    }
    
    // Get device pixel ratio for cache
    qreal dpr = devicePixelRatioF();
    
    // Preload nearby pages
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        Page* page = m_document->page(i);
        if (!page) continue;
        
        // Pre-generate zoom-aware stroke cache for all layers on this page
        for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
            VectorLayer* layer = page->layer(layerIdx);
            if (layer && layer->visible && !layer->isEmpty()) {
                // Build cache at current zoom level for sharp rendering
                layer->ensureStrokeCacheValid(page->size, m_zoomLevel, dpr);
            }
        }
    }
}

void DocumentViewport::evictDistantTiles()
{
    // Only applies to edgeless mode with lazy loading
    if (!m_document || !m_document->isEdgeless() || !m_document->isLazyLoadEnabled()) {
        return;
    }
    
    QRectF viewRect = visibleRect();
    
    // Keep tiles within 2 tiles of viewport, evict the rest
    constexpr int KEEP_MARGIN = 2;
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    QRectF keepRect = viewRect.adjusted(
        -KEEP_MARGIN * tileSize, -KEEP_MARGIN * tileSize,
        KEEP_MARGIN * tileSize, KEEP_MARGIN * tileSize);
    
    // Get all loaded tiles and check which to evict
    QVector<Document::TileCoord> loadedTiles = m_document->allLoadedTileCoords();
    
    int evictedCount = 0;
    for (const auto& coord : loadedTiles) {
        // Phase 5.6.5: No longer need to protect origin tile - layer structure comes from manifest
        
        QRectF tileRect(coord.first * tileSize, coord.second * tileSize,
                        tileSize, tileSize);
        
        if (!keepRect.intersects(tileRect)) {
            m_document->evictTile(coord);
            ++evictedCount;
        }
    }
    
#ifdef QT_DEBUG
    if (evictedCount > 0) {
        qDebug() << "Evicted" << evictedCount << "tiles, remaining:" << m_document->tileCount();
    }
#endif
}

// ===== Input Routing (Task 1.3.8) =====

PointerEvent DocumentViewport::mouseToPointerEvent(QMouseEvent* event, PointerEvent::Type type)
{
    PointerEvent pe;
    pe.type = type;
    pe.source = PointerEvent::Mouse;
    pe.viewportPos = event->position();
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Mouse has no pressure sensitivity
    pe.pressure = 1.0;
    pe.tiltX = 0;
    pe.tiltY = 0;
    pe.rotation = 0;
    
    // Hardware state
    pe.isEraser = false;
    pe.stylusButtons = 0;
    pe.buttons = event->buttons();
    pe.modifiers = event->modifiers();
    pe.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    return pe;
}

PointerEvent DocumentViewport::tabletToPointerEvent(QTabletEvent* event, PointerEvent::Type type)
{
    PointerEvent pe;
    pe.type = type;
    pe.source = PointerEvent::Stylus;
    pe.viewportPos = event->position();
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Tablet pressure and tilt
    pe.pressure = event->pressure();
    pe.tiltX = event->xTilt();
    pe.tiltY = event->yTilt();
    pe.rotation = event->rotation();
    
    // Check for eraser - either eraser end of stylus or eraser button
    // Qt6: pointerType() returns the type of pointing device
    // Also check deviceType() as a fallback - some drivers report eraser via device type
    pe.isEraser = (event->pointerType() == QPointingDevice::PointerType::Eraser);
    
    // Alternative detection: some tablets report eraser via deviceType() instead of pointerType()
    if (!pe.isEraser && event->deviceType() == QInputDevice::DeviceType::Stylus) {
        // Check if this might be an eraser based on the pointing device
        const QPointingDevice* device = event->pointingDevice();
        if (device && device->name().contains("eraser", Qt::CaseInsensitive)) {
            pe.isEraser = true;
        }
    }
    
    // Barrel buttons - Qt provides via buttons()
    // Common mappings: barrel button 1 = Qt::MiddleButton, barrel button 2 = Qt::RightButton
    pe.stylusButtons = static_cast<int>(event->buttons());
    pe.buttons = event->buttons();
    pe.modifiers = event->modifiers();
    pe.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    return pe;
}

void DocumentViewport::handlePointerEvent(const PointerEvent& pe)
{
    switch (pe.type) {
        case PointerEvent::Press:
            handlePointerPress(pe);
            break;
        case PointerEvent::Move:
            handlePointerMove(pe);
            break;
        case PointerEvent::Release:
            handlePointerRelease(pe);
            break;
    }
}

void DocumentViewport::handlePointerPress(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Set active state
    m_pointerActive = true;
    m_activeSource = pe.source;
    m_lastPointerPos = pe.viewportPos;
    
    // Track hardware eraser state for entire stroke
    // Initialize from the press event's eraser state
    m_hardwareEraserActive = pe.isEraser;
    
    // Determine which page to draw on
    if (pe.pageHit.valid()) {
        m_activeDrawingPage = pe.pageHit.pageIndex;
    } else {
        // Pointer is not on any page (in gap or outside content)
        m_activeDrawingPage = -1;
    }
    
    // Handle tool-specific actions
    // Hardware eraser (stylus eraser end) always erases, regardless of selected tool
    bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
    
    if (isErasing) {
        eraseAt(pe);
        // CRITICAL FIX: Always update cursor area on press to show the eraser cursor
        // eraseAt() only updates when strokes are removed, but we need to show cursor immediately
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        QRectF cursorRect(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                          eraserRadius * 2, eraserRadius * 2);
        update(cursorRect.toRect());
    } else if (m_currentTool == ToolType::Pen || m_currentTool == ToolType::Marker) {
        // Task 2.9: Straight line mode - record start point instead of normal stroke
        if (m_straightLineMode) {
            // Use document coords for edgeless, page coords for paged mode
            if (m_document->isEdgeless()) {
                m_straightLineStart = viewportToDocument(pe.viewportPos);
                m_straightLinePageIndex = -1;  // Not used in edgeless
            } else if (pe.pageHit.valid()) {
                m_straightLineStart = pe.pageHit.pagePoint;
                m_straightLinePageIndex = pe.pageHit.pageIndex;
            } else {
                return;  // No valid page hit in paged mode
            }
            m_straightLinePreviewEnd = m_straightLineStart;
            m_isDrawingStraightLine = true;
            m_pointerActive = true;  // Keep pointer active for move/release
            return;
        }
        
        startStroke(pe);
    } else if (m_currentTool == ToolType::Lasso) {
        // Task 2.10: Lasso selection tool
        handlePointerPress_Lasso(pe);
    }
}

void DocumentViewport::handlePointerMove(const PointerEvent& pe)
{
    if (!m_document || !m_pointerActive) return;
    
    // Store old position for cursor update
    QPointF oldPos = m_lastPointerPos;
    
    // Update last pointer position for cursor tracking
    m_lastPointerPos = pe.viewportPos;
    
    // CRITICAL: Some tablet drivers don't report eraser on Press but DO report it on Move.
    // If ANY event in the stroke has isEraser, treat the whole stroke as eraser.
    // This is the same pattern used in InkCanvas.
    if (pe.isEraser && !m_hardwareEraserActive) {
        m_hardwareEraserActive = true;
    }
    
    // Handle tool-specific actions
    // Hardware eraser: use m_hardwareEraserActive because some tablets
    // don't consistently report pointerType() == Eraser in every move event
    bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
    
    // Erasing works in edgeless mode even without a valid drawing page
    // (eraseAtEdgeless uses document coordinates, not page coordinates)
    if (isErasing) {
        eraseAt(pe);
        // CRITICAL FIX: eraseAt() only calls update() when strokes are removed!
        // We must ALWAYS update the cursor area to show cursor movement.
        // Update region around BOTH old and new cursor positions.
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        QRectF oldRect(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                       eraserRadius * 2, eraserRadius * 2);
        QRectF newRect(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                       eraserRadius * 2, eraserRadius * 2);
        update(oldRect.united(newRect).toRect());
        return;  // Don't fall through to stroke continuation
    }
    
    // Task 2.9: Straight line mode - update preview end point
    if (m_isDrawingStraightLine) {
        // Use document coords for edgeless, page coords for paged mode
        if (m_document->isEdgeless()) {
            m_straightLinePreviewEnd = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_straightLinePageIndex) {
            m_straightLinePreviewEnd = pe.pageHit.pagePoint;
        } else {
            // Moved off the original page - extrapolate position
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            m_straightLinePreviewEnd = docPos - pageOrigin;
        }
        update();  // Trigger repaint for preview
        return;
    }
    
    // Task 2.10: Lasso tool - update lasso path OR handle transform
    // CR-2B-5: Must check m_isTransformingSelection too, not just m_isDrawingLasso
    if (m_isDrawingLasso || m_isTransformingSelection) {
        handlePointerMove_Lasso(pe);
        return;
    }
    
    // For stroke drawing, require an active drawing page
    if (m_activeDrawingPage < 0) {
        return;
    }
    
    if (m_isDrawing && (m_currentTool == ToolType::Pen || m_currentTool == ToolType::Marker)) {
        continueStroke(pe);
    }
}

void DocumentViewport::handlePointerRelease(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.9: Straight line mode - create the actual stroke
    if (m_isDrawingStraightLine) {
        // Get final end point
        QPointF endPoint;
        if (m_document->isEdgeless()) {
            endPoint = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_straightLinePageIndex) {
            endPoint = pe.pageHit.pagePoint;
        } else {
            // Moved off the original page - extrapolate position
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            endPoint = docPos - pageOrigin;
        }
        
        // Create the straight line stroke
        createStraightLineStroke(m_straightLineStart, endPoint);
        
        // Clear straight line state
        m_isDrawingStraightLine = false;
        m_straightLinePageIndex = -1;
        
        // Clear active state
        m_pointerActive = false;
        m_activeSource = PointerEvent::Unknown;
        m_hardwareEraserActive = false;
        
        update();
        preloadStrokeCaches();
        return;
    }
    
    // Task 2.10: Lasso tool - finalize lasso selection OR transform
    // CR-2B-5: Must check m_isTransformingSelection too, not just m_isDrawingLasso
    if (m_isDrawingLasso || m_isTransformingSelection) {
        handlePointerRelease_Lasso(pe);
        return;
    }
    
    Q_UNUSED(pe);
    
    // Finish stroke if we were drawing
    if (m_isDrawing) {
        finishStroke();
    }
    
    // Clear active state
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;  // Reset source
    m_activeDrawingPage = -1;
    m_hardwareEraserActive = false;  // Clear hardware eraser state
    // Note: Don't clear m_lastPointerPos - keep it for eraser cursor during hover
    
    // Pre-load stroke caches after interaction (but NOT PDF cache - it causes thrashing during rapid strokes)
    // PDF cache is preloaded during scroll/zoom, not during drawing
    preloadStrokeCaches();
    
    update();
}

// ===== Stroke Drawing (Task 2.2) =====

void DocumentViewport::startStroke(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Only drawing tools start strokes (Pen, Marker)
    if (m_currentTool != ToolType::Pen && m_currentTool != ToolType::Marker) {
        return;
    }
    
    // Determine stroke properties based on current tool (Task 2.8: Marker support)
    QColor strokeColor;
    qreal strokeThickness;
    bool useFixedPressure = false;  // Marker uses fixed thickness (ignores pressure)
    
    if (m_currentTool == ToolType::Marker) {
        strokeColor = m_markerColor;        // Includes alpha for opacity
        strokeThickness = m_markerThickness;
        useFixedPressure = true;            // Fixed thickness, no pressure variation
    } else {
        strokeColor = m_penColor;
        strokeThickness = m_penThickness;
        useFixedPressure = false;           // Pen uses pressure for thickness
    }
    
    // For edgeless mode, we don't require a page hit - we use document coordinates
    if (m_document->isEdgeless()) {
        m_isDrawing = true;
        // CR-4: m_activeDrawingPage = 0 is used for edgeless mode to satisfy
        // the m_activeDrawingPage >= 0 checks in renderCurrentStrokeIncremental().
        // The actual tile is tracked in m_edgelessDrawingTile.
        m_activeDrawingPage = 0;
        
        // Initialize new stroke
        m_currentStroke = VectorStroke();
        m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_currentStroke.color = strokeColor;
        m_currentStroke.baseThickness = strokeThickness;
        
        // Reset incremental rendering cache
        resetCurrentStrokeCache();
        
        // Get document coordinates for the first point
        QPointF docPt = viewportToDocument(pe.viewportPos);
        
        // Store the tile coordinate where stroke starts
        m_edgelessDrawingTile = m_document->tileCoordForPoint(docPt);
        
        // Add first point (stored in DOCUMENT coordinates for edgeless)
        // Marker uses fixed pressure (1.0) for consistent thickness
        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = useFixedPressure ? 1.0 : qBound(0.1, pe.pressure, 1.0);
        m_currentStroke.points.append(pt);
        return;
    }
    
    // Paged mode - require valid page hit
    if (!pe.pageHit.valid()) return;
    
    m_isDrawing = true;
    m_activeDrawingPage = pe.pageHit.pageIndex;
    
    // Initialize new stroke
    m_currentStroke = VectorStroke();
    m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentStroke.color = strokeColor;
    m_currentStroke.baseThickness = strokeThickness;
    
    // Reset incremental rendering cache (Task 2.3)
    resetCurrentStrokeCache();
    
    // Add first point (in page-local coordinates)
    // Marker uses fixed pressure (1.0) for consistent thickness
    qreal effectivePressure = useFixedPressure ? 1.0 : pe.pressure;
    addPointToStroke(pe.pageHit.pagePoint, effectivePressure);
}

void DocumentViewport::continueStroke(const PointerEvent& pe)
{
    if (!m_isDrawing || !m_document) return;
    
    // Task 2.8: Marker uses fixed pressure (1.0) for consistent thickness
    bool useFixedPressure = (m_currentTool == ToolType::Marker);
    qreal effectivePressure = useFixedPressure ? 1.0 : qBound(0.1, pe.pressure, 1.0);
    
    // For edgeless mode, use document coordinates directly
    if (m_document->isEdgeless()) {
        QPointF docPt = viewportToDocument(pe.viewportPos);
        
        // Point decimation (same logic as addPointToStroke but for document coords)
        if (!m_currentStroke.points.isEmpty()) {
            const QPointF& lastPos = m_currentStroke.points.last().pos;
            qreal dx = docPt.x() - lastPos.x();
            qreal dy = docPt.y() - lastPos.y();
            qreal distSq = dx * dx + dy * dy;
            
            if (distSq < MIN_DISTANCE_SQ) {
                // Point too close - but update pressure if higher (only for pen, not marker)
                if (!useFixedPressure && pe.pressure > m_currentStroke.points.last().pressure) {
                    m_currentStroke.points.last().pressure = pe.pressure;
                }
                return;
            }
        }
        
        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = effectivePressure;
        m_currentStroke.points.append(pt);
        
        // Dirty region update for edgeless (document coords → viewport coords)
        // Use current stroke thickness (may be pen or marker)
        qreal padding = m_currentStroke.baseThickness * 2 * m_zoomLevel;
        QPointF vpPos = documentToViewport(docPt);
        QRectF dirtyRect(vpPos.x() - padding, vpPos.y() - padding, padding * 2, padding * 2);
        
        if (m_currentStroke.points.size() > 1) {
            const auto& prevPt = m_currentStroke.points[m_currentStroke.points.size() - 2];
            QPointF prevVpPos = documentToViewport(prevPt.pos);
            dirtyRect = dirtyRect.united(QRectF(prevVpPos.x() - padding, prevVpPos.y() - padding, 
                                                 padding * 2, padding * 2));
        }
        
        update(dirtyRect.toAlignedRect());
        return;
    }
    
    // Paged mode
    if (m_activeDrawingPage < 0) return;
    
    // Get page-local coordinates
    // Note: Even if pointer moves off the active page, we continue drawing
    // to that page (don't switch pages mid-stroke)
    QPointF pagePos;
    if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_activeDrawingPage) {
        pagePos = pe.pageHit.pagePoint;
    } else {
        // Pointer moved off active page - extrapolate position
        QPointF docPos = viewportToDocument(pe.viewportPos);
        QPointF pageOrigin = pagePosition(m_activeDrawingPage);
        pagePos = docPos - pageOrigin;
    }
    
    // Use effective pressure (fixed 1.0 for marker, actual pressure for pen)
    addPointToStroke(pagePos, effectivePressure);
}

void DocumentViewport::finishStroke()
{
    if (!m_isDrawing) return;
    
    // Don't save empty strokes
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        m_currentStrokeCache = QPixmap();  // Release cache memory
        return;
    }
    
    // Finalize stroke
    m_currentStroke.updateBoundingBox();
    
    // Branch for edgeless mode
    if (m_document && m_document->isEdgeless()) {
        finishStrokeEdgeless();
        return;
    }
    
    // Paged mode: add to page's active layer
    Page* page = m_document ? m_document->page(m_activeDrawingPage) : nullptr;
    if (page) {
        VectorLayer* layer = page->activeLayer();
        if (layer) {
            layer->addStroke(m_currentStroke);
            
            // Push to undo stack
            pushUndoAction(m_activeDrawingPage, PageUndoAction::AddStroke, m_currentStroke);
        }
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;  // Reset incremental rendering state
    
    // MEMORY FIX: Release the incremental stroke cache
    // This cache is viewport-sized (~33MB at 4K) and should be freed after stroke completes.
    // It will be lazily reallocated on the next stroke start.
    m_currentStrokeCache = QPixmap();
    
    emit documentModified();
}

void DocumentViewport::finishStrokeEdgeless()
{
    // In edgeless mode, stroke points are in DOCUMENT coordinates.
    // We split the stroke at tile boundaries so each segment is stored in its home tile.
    // This allows the stroke cache to work per-tile while strokes can span multiple tiles.
    
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        m_currentStrokeCache = QPixmap();
        return;
    }
    
    // ========== STROKE SPLITTING AT TILE BOUNDARIES ==========
    // Strategy: Walk through all points, group consecutive points by tile.
    // When crossing a tile boundary, end current segment and start a new one.
    // Overlapping point at boundary ensures visual continuity.
    
    struct TileSegment {
        Document::TileCoord coord;
        QVector<StrokePoint> points;
    };
    QVector<TileSegment> segments;
    
    // Start first segment
    TileSegment currentSegment;
    currentSegment.coord = m_document->tileCoordForPoint(m_currentStroke.points.first().pos);
    currentSegment.points.append(m_currentStroke.points.first());
    
    // Walk through remaining points
    for (int i = 1; i < m_currentStroke.points.size(); ++i) {
        const StrokePoint& pt = m_currentStroke.points[i];
        Document::TileCoord ptTile = m_document->tileCoordForPoint(pt.pos);
        
        if (ptTile != currentSegment.coord) {
            // Tile boundary crossed!
            // End current segment (include this point for overlap at boundary)
            currentSegment.points.append(pt);
            segments.append(currentSegment);
            
            // Start new segment (also include this point for overlap)
            currentSegment.coord = ptTile;
            currentSegment.points.clear();
            currentSegment.points.append(pt);  // Overlap point
        } else {
            // Same tile, just add point
            currentSegment.points.append(pt);
        }
    }
    
    // Don't forget the last segment
    if (!currentSegment.points.isEmpty()) {
        segments.append(currentSegment);
    }
    
#ifdef QT_DEBUG
    qDebug() << "Edgeless: Stroke split into" << segments.size() << "segments";
#endif
    
    // ========== ADD EACH SEGMENT TO ITS TILE ==========
    QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;  // For undo
    
    for (const TileSegment& seg : segments) {
        // Get or create tile
        Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
        if (!tile) continue;
        
        // Ensure tile has enough layers
        while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        
        VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
        if (!layer) continue;
        
        // Create local stroke (convert from document coords to tile-local)
        VectorStroke localStroke = m_currentStroke;  // Copy base properties (color, width, etc.)
        localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New unique ID for each segment
        localStroke.points.clear();
        
        QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                           seg.coord.second * Document::EDGELESS_TILE_SIZE);
        
        for (const StrokePoint& pt : seg.points) {
            StrokePoint localPt = pt;
            localPt.pos -= tileOrigin;
            localStroke.points.append(localPt);
        }
        localStroke.updateBoundingBox();
        
        // Add to tile's layer
        layer->addStroke(localStroke);
        layer->invalidateStrokeCache();
        
        // Mark tile as dirty for persistence (Phase E5)
        m_document->markTileDirty(seg.coord);
        
        addedStrokes.append({seg.coord, localStroke});
        
#ifdef QT_DEBUG
        qDebug() << "  -> Tile" << seg.coord.first << "," << seg.coord.second
                 << "points:" << localStroke.points.size();
#endif
    }
    
    // ========== PUSH TO EDGELESS UNDO STACK (Phase E6) ==========
    // All segments from this stroke = one atomic undo action
    if (!addedStrokes.isEmpty()) {
        EdgelessUndoAction undoAction;
        undoAction.type = PageUndoAction::AddStroke;
        undoAction.layerIndex = m_edgelessActiveLayerIndex;
        for (const auto& pair : addedStrokes) {
            undoAction.segments.append({pair.first, pair.second});
        }
        pushEdgelessUndoAction(undoAction);
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;
    m_currentStrokeCache = QPixmap();
    
    // Trigger repaint
    update();
    
    emit documentModified();
}

QVector<QPair<Document::TileCoord, VectorStroke>> DocumentViewport::addStrokeToEdgelessTiles(
    const VectorStroke& stroke, int layerIndex)
{
    // ========== STROKE SPLITTING AT TILE BOUNDARIES ==========
    // This method is shared by finishStrokeEdgeless() and applySelectionTransform()
    // to ensure consistent behavior when strokes cross tile boundaries.
    //
    // Input: stroke with points in DOCUMENT coordinates
    // Output: multiple segments, each added to appropriate tile in tile-local coords
    
    QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;
    
    if (!m_document || stroke.points.isEmpty()) {
        return addedStrokes;
    }
    
    // Strategy: Walk through all points, group consecutive points by tile.
    // When crossing a tile boundary, end current segment and start a new one.
    // Overlapping point at boundary ensures visual continuity.
    
    struct TileSegment {
        Document::TileCoord coord;
        QVector<StrokePoint> points;
    };
    QVector<TileSegment> segments;
    
    // Start first segment
    TileSegment currentSegment;
    currentSegment.coord = m_document->tileCoordForPoint(stroke.points.first().pos);
    currentSegment.points.append(stroke.points.first());
    
    // Walk through remaining points
    for (int i = 1; i < stroke.points.size(); ++i) {
        const StrokePoint& pt = stroke.points[i];
        Document::TileCoord ptTile = m_document->tileCoordForPoint(pt.pos);
        
        if (ptTile != currentSegment.coord) {
            // Tile boundary crossed!
            // End current segment (include this point for overlap at boundary)
            currentSegment.points.append(pt);
            segments.append(currentSegment);
            
            // Start new segment (also include this point for overlap)
            currentSegment.coord = ptTile;
            currentSegment.points.clear();
            currentSegment.points.append(pt);  // Overlap point
        } else {
            // Same tile, just add point
            currentSegment.points.append(pt);
        }
    }
    
    // Don't forget the last segment
    if (!currentSegment.points.isEmpty()) {
        segments.append(currentSegment);
    }
    
#ifdef QT_DEBUG
    if (segments.size() > 1) {
        qDebug() << "addStrokeToEdgelessTiles: stroke split into" << segments.size() << "segments";
    }
#endif
    
    // ========== ADD EACH SEGMENT TO ITS TILE ==========
    for (const TileSegment& seg : segments) {
        // Get or create tile
        Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
        if (!tile) continue;
        
        // Ensure tile has enough layers
        while (tile->layerCount() <= layerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        
        VectorLayer* layer = tile->layer(layerIndex);
        if (!layer) continue;
        
        // Create local stroke (convert from document coords to tile-local)
        VectorStroke localStroke = stroke;  // Copy base properties (color, width, etc.)
        localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New unique ID
        localStroke.points.clear();
        
        QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                           seg.coord.second * Document::EDGELESS_TILE_SIZE);
        
        for (const StrokePoint& pt : seg.points) {
            StrokePoint localPt = pt;
            localPt.pos -= tileOrigin;
            localStroke.points.append(localPt);
        }
        localStroke.updateBoundingBox();
        
        // Add to tile's layer
        layer->addStroke(localStroke);
        layer->invalidateStrokeCache();
        
        // Mark tile as dirty for persistence
        m_document->markTileDirty(seg.coord);
        
        addedStrokes.append({seg.coord, localStroke});
    }
    
    return addedStrokes;
}

// ===== Straight Line Mode (Task 2.9) =====

void DocumentViewport::createStraightLineStroke(const QPointF& start, const QPointF& end)
{
    if (!m_document) return;
    
    // Don't create zero-length lines
    if ((start - end).manhattanLength() < 1.0) {
        return;
    }
    
    // Determine color and thickness based on current tool
    QColor strokeColor;
    qreal strokeThickness;
    if (m_currentTool == ToolType::Marker) {
        strokeColor = m_markerColor;
        strokeThickness = m_markerThickness;
    } else {
        strokeColor = m_penColor;
        strokeThickness = m_penThickness;
    }
    
    // Create stroke with just two points (start and end)
    VectorStroke stroke;
    stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    stroke.color = strokeColor;
    stroke.baseThickness = strokeThickness;
    
    // Both points have pressure 1.0 (no pressure variation for straight lines)
    StrokePoint startPt;
    startPt.pos = start;
    startPt.pressure = 1.0;
    stroke.points.append(startPt);
    
    StrokePoint endPt;
    endPt.pos = end;
    endPt.pressure = 1.0;
    stroke.points.append(endPt);
    
    stroke.updateBoundingBox();
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE: Handle tile splitting ==========
        // A straight line may cross multiple tiles. We use a simplified approach:
        // Find all tiles the line passes through and add the appropriate segment.
        
        Document::TileCoord startTile = m_document->tileCoordForPoint(start);
        Document::TileCoord endTile = m_document->tileCoordForPoint(end);
        
        if (startTile == endTile) {
            // Simple case: line is within one tile
            Page* tile = m_document->getOrCreateTile(startTile.first, startTile.second);
            if (!tile) return;
            
            // Ensure tile has enough layers
            while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer) return;
            
            // Convert to tile-local coordinates
            QPointF tileOrigin(startTile.first * Document::EDGELESS_TILE_SIZE,
                               startTile.second * Document::EDGELESS_TILE_SIZE);
            VectorStroke localStroke = stroke;
            localStroke.points[0].pos -= tileOrigin;
            localStroke.points[1].pos -= tileOrigin;
            localStroke.updateBoundingBox();
            
            layer->addStroke(localStroke);
            layer->invalidateStrokeCache();
            m_document->markTileDirty(startTile);
            
            // Push to undo stack
            EdgelessUndoAction undoAction;
            undoAction.type = PageUndoAction::AddStroke;
            undoAction.layerIndex = m_edgelessActiveLayerIndex;
            undoAction.segments.append({startTile, localStroke});
            pushEdgelessUndoAction(undoAction);
        } else {
            // Line crosses tile boundaries - sample points along the line
            // and split at tile boundaries (same algorithm as freehand strokes)
            
            // Generate intermediate points along the line
            qreal lineLength = std::sqrt(std::pow(end.x() - start.x(), 2) + 
                                         std::pow(end.y() - start.y(), 2));
            int numPoints = qMax(2, static_cast<int>(lineLength / 10.0));  // ~10px spacing
            
            QVector<StrokePoint> linePoints;
            for (int i = 0; i <= numPoints; ++i) {
                qreal t = static_cast<qreal>(i) / numPoints;
                StrokePoint pt;
                pt.pos = start + t * (end - start);
                pt.pressure = 1.0;
                linePoints.append(pt);
            }
            
            // Split at tile boundaries (same logic as finishStrokeEdgeless)
            struct TileSegment {
                Document::TileCoord coord;
                QVector<StrokePoint> points;
            };
            QVector<TileSegment> segments;
            
            TileSegment currentSegment;
            currentSegment.coord = m_document->tileCoordForPoint(linePoints.first().pos);
            currentSegment.points.append(linePoints.first());
            
            for (int i = 1; i < linePoints.size(); ++i) {
                const StrokePoint& pt = linePoints[i];
                Document::TileCoord ptTile = m_document->tileCoordForPoint(pt.pos);
                
                if (ptTile != currentSegment.coord) {
                    currentSegment.points.append(pt);
                    segments.append(currentSegment);
                    
                    currentSegment.coord = ptTile;
                    currentSegment.points.clear();
                    currentSegment.points.append(pt);
                } else {
                    currentSegment.points.append(pt);
                }
            }
            if (!currentSegment.points.isEmpty()) {
                segments.append(currentSegment);
            }
            
            // Add each segment to its tile
            QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;
            
            for (const TileSegment& seg : segments) {
                Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
                if (!tile) continue;
                
                while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
                    tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
                }
                
                VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
                if (!layer) continue;
                
                VectorStroke localStroke;
                localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                localStroke.color = strokeColor;
                localStroke.baseThickness = strokeThickness;
                
                QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                                   seg.coord.second * Document::EDGELESS_TILE_SIZE);
                
                for (const StrokePoint& pt : seg.points) {
                    StrokePoint localPt = pt;
                    localPt.pos -= tileOrigin;
                    localStroke.points.append(localPt);
                }
                localStroke.updateBoundingBox();
                
                layer->addStroke(localStroke);
                layer->invalidateStrokeCache();
                m_document->markTileDirty(seg.coord);
                
                addedStrokes.append({seg.coord, localStroke});
            }
            
            // Push to undo stack (all segments as one atomic action)
            if (!addedStrokes.isEmpty()) {
                EdgelessUndoAction undoAction;
                undoAction.type = PageUndoAction::AddStroke;
                undoAction.layerIndex = m_edgelessActiveLayerIndex;
                for (const auto& pair : addedStrokes) {
                    undoAction.segments.append({pair.first, pair.second});
                }
                pushEdgelessUndoAction(undoAction);
            }
        }
    } else {
        // ========== PAGED MODE: Add directly to page ==========
        if (m_straightLinePageIndex < 0 || m_straightLinePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_straightLinePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) return;
        
        layer->addStroke(stroke);
        layer->invalidateStrokeCache();
        
        // Push to undo stack (same pattern as finishStroke)
        pushUndoAction(m_straightLinePageIndex, PageUndoAction::AddStroke, stroke);
    }
    
    emit documentModified();
}

// ===== Lasso Selection Tool (Task 2.10) =====

// P1: Reset lasso path cache for new drawing session
void DocumentViewport::resetLassoPathCache()
{
    // Create cache at viewport size with device pixel ratio for high DPI
    qreal dpr = devicePixelRatioF();
    m_lassoPathCache = QPixmap(static_cast<int>(width() * dpr), 
                               static_cast<int>(height() * dpr));
    m_lassoPathCache.setDevicePixelRatio(dpr);
    m_lassoPathCache.fill(Qt::transparent);
    
    m_lastRenderedLassoIdx = 0;
    m_lassoPathCacheZoom = m_zoomLevel;
    m_lassoPathCachePan = m_panOffset;
    m_lassoPathLength = 0;
}

// P1: Incrementally render lasso path with consistent dash pattern
void DocumentViewport::renderLassoPathIncremental(QPainter& painter)
{
    if (m_lassoPath.size() < 2) return;
    
    // Check if cache needs reset (zoom/pan changed)
    if (m_lassoPathCache.isNull() ||
        !qFuzzyCompare(m_lassoPathCacheZoom, m_zoomLevel) ||
        m_lassoPathCachePan != m_panOffset) {
        // Zoom or pan changed - need to re-render everything
        resetLassoPathCache();
    }
    
    // Render new segments to cache
    if (m_lastRenderedLassoIdx < m_lassoPath.size() - 1) {
        QPainter cachePainter(&m_lassoPathCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Determine coordinate conversion based on mode
        bool isEdgeless = m_document && m_document->isEdgeless();
        QPointF pageOrigin;
        if (!isEdgeless && m_lassoSelection.sourcePageIndex >= 0) {
            pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        }
        
        // Render each new segment with proper dash offset
        for (int i = m_lastRenderedLassoIdx; i < m_lassoPath.size() - 1; ++i) {
            QPointF pt1 = m_lassoPath.at(i);
            QPointF pt2 = m_lassoPath.at(i + 1);
            
            // Convert to viewport coordinates
            QPointF vp1, vp2;
            if (isEdgeless) {
                vp1 = documentToViewport(pt1);
                vp2 = documentToViewport(pt2);
            } else {
                vp1 = documentToViewport(pt1 + pageOrigin);
                vp2 = documentToViewport(pt2 + pageOrigin);
            }
            
            // Calculate segment length in viewport coordinates
            qreal segLen = QLineF(vp1, vp2).length();
            
            // Create pen with dash offset for continuous pattern
            // Qt dash pattern: [dash, gap] - default DashLine is [4, 2] (in pen width units)
            // For 1.5px pen: [6, 3] pixel pattern
            QPen lassoPen(QColor(0, 120, 215), 1.5, Qt::DashLine);
            lassoPen.setCosmetic(true);  // Constant width regardless of transform
            lassoPen.setDashOffset(m_lassoPathLength / 1.5);  // Offset in pen-width units
            cachePainter.setPen(lassoPen);
            
            cachePainter.drawLine(vp1, vp2);
            
            // Accumulate path length for next segment's dash offset
            m_lassoPathLength += segLen;
        }
        
        m_lastRenderedLassoIdx = m_lassoPath.size() - 1;
    }
    
    // Blit cache to painter
    painter.drawPixmap(0, 0, m_lassoPathCache);
}

void DocumentViewport::handlePointerPress_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Check for handle/transform hit on existing selection
    if (m_lassoSelection.isValid()) {
        HandleHit hit = hitTestSelectionHandles(pe.viewportPos);
        
        if (hit != HandleHit::None) {
            // Start transform operation
            startSelectionTransform(hit, pe.viewportPos);
            m_pointerActive = true;
            return;
        }
        
        // Task 2.10.6: Click outside selection - apply transform (if any) and clear
        // Check if there's a non-identity transform to apply
        bool hasTransform = !qFuzzyIsNull(m_lassoSelection.offset.x()) ||
                            !qFuzzyIsNull(m_lassoSelection.offset.y()) ||
                            !qFuzzyCompare(m_lassoSelection.scaleX, 1.0) ||
                            !qFuzzyCompare(m_lassoSelection.scaleY, 1.0) ||
                            !qFuzzyIsNull(m_lassoSelection.rotation);
        
        if (hasTransform) {
            applySelectionTransform();  // This also clears the selection
        } else {
            clearLassoSelection();
        }
    }
    
    // Start new lasso path
    m_lassoPath.clear();
    resetLassoPathCache();  // P1: Initialize cache for incremental rendering
    
    // Use appropriate coordinates based on mode
    QPointF pt;
    if (m_document->isEdgeless()) {
        pt = viewportToDocument(pe.viewportPos);
    } else if (pe.pageHit.valid()) {
        pt = pe.pageHit.pagePoint;
        m_lassoSelection.sourcePageIndex = pe.pageHit.pageIndex;
    } else {
        return;  // No valid page hit in paged mode
    }
    
    m_lassoPath << pt;
    m_isDrawingLasso = true;
    m_pointerActive = true;
    
    update();
}

void DocumentViewport::handlePointerMove_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Handle transform updates
    if (m_isTransformingSelection) {
        updateSelectionTransform(pe.viewportPos);
        return;
    }
    
    if (!m_isDrawingLasso) return;
    
    // Add point to lasso path
    QPointF pt;
    if (m_document->isEdgeless()) {
        pt = viewportToDocument(pe.viewportPos);
    } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_lassoSelection.sourcePageIndex) {
        pt = pe.pageHit.pagePoint;
    } else if (m_lassoSelection.sourcePageIndex >= 0) {
        // Pointer moved off page - extrapolate
        QPointF docPos = viewportToDocument(pe.viewportPos);
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        pt = docPos - pageOrigin;
    } else {
        return;
    }
    
    // Point decimation for lasso path (similar to stroke)
    QPointF lastPt;
    bool hasLastPoint = !m_lassoPath.isEmpty();
    if (hasLastPoint) {
        lastPt = m_lassoPath.last();
        qreal dx = pt.x() - lastPt.x();
        qreal dy = pt.y() - lastPt.y();
        if (dx * dx + dy * dy < 4.0) {  // 2px minimum distance
            return;  // Skip this point
        }
    }
    
    m_lassoPath << pt;
    
    // P2: Dirty region update - only repaint the new segment's bounding rect
    if (hasLastPoint) {
        // Convert both points to viewport coordinates
        QPointF vpLast, vpCurrent;
        if (m_document->isEdgeless()) {
            vpLast = documentToViewport(lastPt);
            vpCurrent = documentToViewport(pt);
        } else {
            QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
            vpLast = documentToViewport(lastPt + pageOrigin);
            vpCurrent = documentToViewport(pt + pageOrigin);
        }
        
        // Calculate dirty rect with padding for line width and antialiasing
        QRectF dirtyRect = QRectF(vpLast, vpCurrent).normalized();
        dirtyRect.adjust(-4, -4, 4, 4);  // Account for line width (1.5) + padding
        update(dirtyRect.toRect());
    } else {
        // First point - update a small region around it
        QPointF vpPt = m_document->isEdgeless() 
            ? documentToViewport(pt)
            : documentToViewport(pt + pagePosition(m_lassoSelection.sourcePageIndex));
        QRectF dirtyRect(vpPt.x() - 5, vpPt.y() - 5, 10, 10);
        update(dirtyRect.toRect());
    }
}

void DocumentViewport::handlePointerRelease_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Finalize transform if active
    if (m_isTransformingSelection) {
        finalizeSelectionTransform();
        m_pointerActive = false;
        return;
    }
    
    if (m_isDrawingLasso) {
        // Add final point
        QPointF pt;
        if (m_document->isEdgeless()) {
            pt = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid()) {
            pt = pe.pageHit.pagePoint;
        } else if (m_lassoSelection.sourcePageIndex >= 0) {
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
            pt = docPos - pageOrigin;
        }
        
        if (!pt.isNull()) {
            m_lassoPath << pt;
        }
        
        // Task 2.10.2: Find strokes within the lasso path
        finalizeLassoSelection();
        m_isDrawingLasso = false;
    }
    
    m_pointerActive = false;
    update();
}

void DocumentViewport::finalizeLassoSelection()
{
    if (!m_document || m_lassoPath.size() < 3) {
        // Need at least 3 points to form a valid selection polygon
        m_lassoPath.clear();
        // P1: Reset cache state
        m_lastRenderedLassoIdx = 0;
        m_lassoPathLength = 0;
        return;
    }
    
    // BUG FIX: Save sourcePageIndex BEFORE clearing selection
    // (it was set during handlePointerPress_Lasso)
    int savedSourcePageIndex = m_lassoSelection.sourcePageIndex;
    
    // Clear any existing selection (but we saved the page index)
    m_lassoSelection.clear();
    
    // Restore the source page index for paged mode
    m_lassoSelection.sourcePageIndex = savedSourcePageIndex;
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Check strokes across all visible tiles
        // Lasso path is in document coordinates
        // Tile strokes are in tile-local coordinates
        
        m_lassoSelection.sourceLayerIndex = m_edgelessActiveLayerIndex;
        
        // Get all loaded tiles
        auto tiles = m_document->allLoadedTileCoords();
        
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer || layer->isEmpty()) continue;
            
            // Calculate tile origin in document coordinates
            QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                               coord.second * Document::EDGELESS_TILE_SIZE);
            
            const auto& strokes = layer->strokes();
            for (int i = 0; i < strokes.size(); ++i) {
                const VectorStroke& stroke = strokes[i];
                
                // Transform stroke to document coordinates for hit test
                // We create a temporary copy with document coords
                VectorStroke docStroke = stroke;
                for (auto& pt : docStroke.points) {
                    pt.pos += tileOrigin;
                }
                docStroke.updateBoundingBox();
                
                if (strokeIntersectsLasso(docStroke, m_lassoPath)) {
                    // Store the document-coordinate version for rendering
                    m_lassoSelection.selectedStrokes.append(docStroke);
                    m_lassoSelection.originalIndices.append(i);
                    // For edgeless, we store the tile coord; for simplicity,
                    // just store the first tile's coord (cross-tile selection is complex)
                    if (m_lassoSelection.sourceTileCoord == std::pair<int,int>(0,0) && 
                        m_lassoSelection.selectedStrokes.size() == 1) {
                        m_lassoSelection.sourceTileCoord = coord;
                    }
                }
            }
        }
    } else {
        // ========== PAGED MODE ==========
        // Check strokes on the active layer of the current page
        // Lasso path is in page-local coordinates
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            m_lassoPath.clear();
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) {
            m_lassoPath.clear();
            return;
        }
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) {
            m_lassoPath.clear();
            return;
        }
        
        m_lassoSelection.sourceLayerIndex = page->activeLayerIndex;
        
        const auto& strokes = layer->strokes();
        for (int i = 0; i < strokes.size(); ++i) {
            const VectorStroke& stroke = strokes[i];
            
            if (strokeIntersectsLasso(stroke, m_lassoPath)) {
                m_lassoSelection.selectedStrokes.append(stroke);
                m_lassoSelection.originalIndices.append(i);
            }
        }
    }
    
    // Calculate bounding box and transform origin if we have a selection
    if (m_lassoSelection.isValid()) {
        m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
        m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
    }
    
    // Clear the lasso path now that selection is complete
    m_lassoPath.clear();
    
    // P1: Reset cache state (cache is no longer needed after selection)
    m_lastRenderedLassoIdx = 0;
    m_lassoPathLength = 0;
    
    update();
}

bool DocumentViewport::strokeIntersectsLasso(const VectorStroke& stroke, 
                                              const QPolygonF& lasso) const
{
    // Check if any point of the stroke is inside the lasso polygon
    for (const auto& pt : stroke.points) {
        if (lasso.containsPoint(pt.pos, Qt::OddEvenFill)) {
            return true;
        }
    }
    return false;
}

QRectF DocumentViewport::calculateSelectionBoundingBox() const
{
    if (m_lassoSelection.selectedStrokes.isEmpty()) {
        return QRectF();
    }
    
    QRectF bounds = m_lassoSelection.selectedStrokes[0].boundingBox;
    for (int i = 1; i < m_lassoSelection.selectedStrokes.size(); ++i) {
        bounds = bounds.united(m_lassoSelection.selectedStrokes[i].boundingBox);
    }
    return bounds;
}

QTransform DocumentViewport::buildSelectionTransform() const
{
    // Build transform: rotate/scale around origin, then apply offset
    // 
    // CR-2B-6: Qt transforms are composed in REVERSE order (last added = first applied)
    // To achieve: 1) rotate/scale around origin, 2) then apply offset
    // We must add offset FIRST (so it's applied LAST to points)
    //
    // Application order (to point P):
    //   1. translate(-origin)     -> P - origin
    //   2. scale                  -> scale * (P - origin)
    //   3. rotate                 -> rotate * scale * (P - origin)
    //   4. translate(+origin)     -> origin + rotate * scale * (P - origin)
    //   5. translate(offset)      -> offset + origin + rotate * scale * (P - origin)
    //
    // Qt composition order (reverse):
    QTransform t;
    QPointF origin = m_lassoSelection.transformOrigin;
    
    t.translate(m_lassoSelection.offset.x(), m_lassoSelection.offset.y());  // Applied 5th (last)
    t.translate(origin.x(), origin.y());                                      // Applied 4th
    t.rotate(m_lassoSelection.rotation);                                      // Applied 3rd
    t.scale(m_lassoSelection.scaleX, m_lassoSelection.scaleY);                // Applied 2nd
    t.translate(-origin.x(), -origin.y());                                    // Applied 1st
    
    return t;
}

void DocumentViewport::renderLassoSelection(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Get the current selection transform
    QTransform transform = buildSelectionTransform();
    
    // Render each selected stroke with transform applied
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        // Transform each point and render
        VectorStroke transformedStroke;
        transformedStroke.id = stroke.id;
        transformedStroke.color = stroke.color;
        transformedStroke.baseThickness = stroke.baseThickness;
        
        for (const StrokePoint& pt : stroke.points) {
            StrokePoint tPt;
            tPt.pos = transform.map(pt.pos);
            tPt.pressure = pt.pressure;
            transformedStroke.points.append(tPt);
        }
        transformedStroke.updateBoundingBox();
        
        // Convert from document/page coords to viewport and render
        // We need to render at viewport scale
        painter.save();
        
        if (m_document->isEdgeless()) {
            // Edgeless: stroke coords are in document space
            // Apply zoom and pan to get to viewport
            painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
            painter.scale(m_zoomLevel, m_zoomLevel);
        } else {
            // Paged: stroke coords are in page-local space
            // Need to add page offset first
            QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
            painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
            painter.scale(m_zoomLevel, m_zoomLevel);
            painter.translate(pageOrigin);
        }
        
        VectorLayer::renderStroke(painter, transformedStroke);
        painter.restore();
    }
    
    // Draw the bounding box
    drawSelectionBoundingBox(painter);
    
    // Draw transform handles
    drawSelectionHandles(painter);
    
    painter.restore();
}

void DocumentViewport::drawSelectionBoundingBox(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    QRectF box = m_lassoSelection.boundingBox;
    QTransform transform = buildSelectionTransform();
    
    // Transform the four corners
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport coordinates
    QPolygonF vpCorners;
    if (m_document->isEdgeless()) {
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt);
        }
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    // Draw dashed bounding box (marching ants style)
    // Use static offset that increments for animation effect
    static int dashOffset = 0;
    
    QPen blackPen(Qt::black, 1, Qt::DashLine);
    blackPen.setDashOffset(dashOffset);
    QPen whitePen(Qt::white, 1, Qt::DashLine);
    whitePen.setDashOffset(dashOffset + 4);  // Offset for contrast
    
    painter.setPen(whitePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(vpCorners);
    
    painter.setPen(blackPen);
    painter.drawPolygon(vpCorners);
    
    // Note: For animated marching ants, call update() from a timer
    // and increment dashOffset. For now, static dashed line.
    // dashOffset = (dashOffset + 1) % 16;
}

QVector<QPointF> DocumentViewport::getHandlePositions() const
{
    // Returns 9 positions: 8 scale handles + 1 rotation handle
    // Positions are in document/page coordinates (before transform)
    QRectF box = m_lassoSelection.boundingBox;
    
    QVector<QPointF> positions;
    positions.reserve(9);
    
    // Scale handles: TL, T, TR, L, R, BL, B, BR (8 handles)
    positions << box.topLeft();                                    // 0: TopLeft
    positions << QPointF(box.center().x(), box.top());             // 1: Top
    positions << box.topRight();                                   // 2: TopRight
    positions << QPointF(box.left(), box.center().y());            // 3: Left
    positions << QPointF(box.right(), box.center().y());           // 4: Right
    positions << box.bottomLeft();                                 // 5: BottomLeft
    positions << QPointF(box.center().x(), box.bottom());          // 6: Bottom
    positions << box.bottomRight();                                // 7: BottomRight
    
    // Rotation handle: above top center
    // Use a fixed offset in document coords (will scale with zoom)
    qreal rotateOffset = ROTATE_HANDLE_OFFSET / m_zoomLevel;
    positions << QPointF(box.center().x(), box.top() - rotateOffset);  // 8: Rotate
    
    return positions;
}

void DocumentViewport::drawSelectionHandles(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    QTransform transform = buildSelectionTransform();
    QVector<QPointF> handlePositions = getHandlePositions();
    
    // Determine page origin for coordinate conversion
    QPointF pageOrigin;
    if (!m_document->isEdgeless()) {
        pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
    }
    
    // Convert handle positions to viewport coordinates
    auto toViewport = [&](const QPointF& docPt) -> QPointF {
        QPointF transformed = transform.map(docPt);
        if (m_document->isEdgeless()) {
            return documentToViewport(transformed);
        } else {
            return documentToViewport(transformed + pageOrigin);
        }
    };
    
    // Draw style for handles
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen handlePen(Qt::black, 1);
    painter.setPen(handlePen);
    painter.setBrush(Qt::white);
    
    // Draw the 8 scale handles (squares)
    qreal halfSize = HANDLE_VISUAL_SIZE / 2.0;
    for (int i = 0; i < 8; ++i) {
        QPointF vpPos = toViewport(handlePositions[i]);
        QRectF handleRect(vpPos.x() - halfSize, vpPos.y() - halfSize,
                          HANDLE_VISUAL_SIZE, HANDLE_VISUAL_SIZE);
        painter.drawRect(handleRect);
    }
    
    // Draw rotation handle (circle) and connecting line
    QPointF topCenterVp = toViewport(handlePositions[1]);  // Top center
    QPointF rotateVp = toViewport(handlePositions[8]);     // Rotation handle
    
    // Line from top center to rotation handle
    painter.setPen(QPen(Qt::black, 1));
    painter.drawLine(topCenterVp, rotateVp);
    
    // Rotation handle circle
    painter.setBrush(Qt::white);
    painter.drawEllipse(rotateVp, halfSize, halfSize);
    
    // Draw a small rotation indicator inside the circle
    painter.setPen(QPen(Qt::black, 1));
    QPointF arrowStart(rotateVp.x() - halfSize * 0.4, rotateVp.y());
    QPointF arrowEnd(rotateVp.x() + halfSize * 0.4, rotateVp.y() - halfSize * 0.3);
    painter.drawLine(arrowStart, rotateVp);
    painter.drawLine(rotateVp, arrowEnd);
}

DocumentViewport::HandleHit DocumentViewport::hitTestSelectionHandles(const QPointF& viewportPos) const
{
    if (!m_lassoSelection.isValid()) {
        return HandleHit::None;
    }
    
    QTransform transform = buildSelectionTransform();
    QVector<QPointF> handlePositions = getHandlePositions();
    
    // Determine page origin for coordinate conversion
    QPointF pageOrigin;
    if (!m_document->isEdgeless()) {
        pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
    }
    
    // Convert handle positions to viewport coordinates
    auto toViewport = [&](const QPointF& docPt) -> QPointF {
        QPointF transformed = transform.map(docPt);
        if (m_document->isEdgeless()) {
            return documentToViewport(transformed);
        } else {
            return documentToViewport(transformed + pageOrigin);
        }
    };
    
    // Touch-friendly hit area (larger than visual)
    qreal hitRadius = HANDLE_HIT_SIZE / 2.0;
    
    // Map handle indices to HandleHit enum
    // Order matches getHandlePositions(): TL(0), T(1), TR(2), L(3), R(4), BL(5), B(6), BR(7), Rotate(8)
    static const HandleHit handleTypes[] = {
        HandleHit::TopLeft, HandleHit::Top, HandleHit::TopRight,
        HandleHit::Left, HandleHit::Right,
        HandleHit::BottomLeft, HandleHit::Bottom, HandleHit::BottomRight,
        HandleHit::Rotate
    };
    
    // Test rotation handle first (highest priority, on top visually)
    {
        QPointF vpPos = toViewport(handlePositions[8]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return HandleHit::Rotate;
        }
    }
    
    // Test scale handles in reverse order (corners have priority over edges)
    // Test corners: TL, TR, BL, BR (indices 0, 2, 5, 7)
    int cornerIndices[] = {0, 2, 5, 7};
    for (int idx : cornerIndices) {
        QPointF vpPos = toViewport(handlePositions[idx]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return handleTypes[idx];
        }
    }
    
    // Test edge handles: T, L, R, B (indices 1, 3, 4, 6)
    int edgeIndices[] = {1, 3, 4, 6};
    for (int idx : edgeIndices) {
        QPointF vpPos = toViewport(handlePositions[idx]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return handleTypes[idx];
        }
    }
    
    // Test if inside bounding box (for move)
    // Transform the bounding box corners and check if point is inside
    QRectF box = m_lassoSelection.boundingBox;
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport
    QPolygonF vpCorners;
    for (const QPointF& pt : corners) {
        if (m_document->isEdgeless()) {
            vpCorners << documentToViewport(pt);
        } else {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    if (vpCorners.containsPoint(viewportPos, Qt::OddEvenFill)) {
        return HandleHit::Inside;
    }
    
    return HandleHit::None;
}

void DocumentViewport::startSelectionTransform(HandleHit handle, const QPointF& viewportPos)
{
    if (!m_lassoSelection.isValid() || handle == HandleHit::None) {
        return;
    }
    
    m_isTransformingSelection = true;
    m_transformHandle = handle;
    m_transformStartPos = viewportPos;
    
    // Store document position for coordinate-independent calculations
    if (m_document->isEdgeless()) {
        m_transformStartDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        m_transformStartDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    // CR-2B-8 + CR-2B-9: Before starting a new transform, "bake in" only the OFFSET.
    // 
    // We must NOT bake in rotation or scale because:
    // - Rotation: Baking creates an axis-aligned bounding box, losing the tilt.
    //   Subsequent operations would use X/Y axes instead of the rotated axes.
    // - Scale: Similar issue - we'd lose the local coordinate orientation.
    //
    // ONLY offset is safe to bake in because it's pure translation.
    // Rotation and scale remain as cumulative values.
    if (!m_lassoSelection.offset.isNull()) {
        // Translate bounding box and origin by the offset
        m_lassoSelection.boundingBox.translate(m_lassoSelection.offset);
        m_lassoSelection.transformOrigin += m_lassoSelection.offset;
        
        // Translate stored strokes to match
        for (VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            for (StrokePoint& pt : stroke.points) {
                pt.pos += m_lassoSelection.offset;
            }
            stroke.updateBoundingBox();
        }
        
        // Reset offset only (rotation and scale remain)
        m_lassoSelection.offset = QPointF(0, 0);
    }
    
    // Store current transform state so we can compute deltas
    m_transformStartBounds = m_lassoSelection.boundingBox;
    m_transformStartRotation = m_lassoSelection.rotation;
    m_transformStartScaleX = m_lassoSelection.scaleX;
    m_transformStartScaleY = m_lassoSelection.scaleY;
    m_transformStartOffset = m_lassoSelection.offset;
}

void DocumentViewport::updateSelectionTransform(const QPointF& viewportPos)
{
    if (!m_isTransformingSelection || !m_lassoSelection.isValid()) {
        return;
    }
    
    // Get current document position
    QPointF currentDocPos;
    if (m_document->isEdgeless()) {
        currentDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        currentDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    switch (m_transformHandle) {
        case HandleHit::Inside: {
            // Move: offset by delta in document coordinates
            QPointF delta = currentDocPos - m_transformStartDocPos;
            m_lassoSelection.offset = m_transformStartOffset + delta;
            break;
        }
        
        case HandleHit::Rotate: {
            // Rotate around transform origin
            // Calculate angle from origin to start and current positions
            QPointF origin = m_lassoSelection.transformOrigin;
            
            // Use viewport coordinates for angle calculation (more intuitive for user)
            QPointF originVp;
            if (m_document->isEdgeless()) {
                originVp = documentToViewport(origin);
            } else {
                QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
                originVp = documentToViewport(origin + pageOrigin);
            }
            
            qreal startAngle = std::atan2(m_transformStartPos.y() - originVp.y(),
                                          m_transformStartPos.x() - originVp.x());
            qreal currentAngle = std::atan2(viewportPos.y() - originVp.y(),
                                            viewportPos.x() - originVp.x());
            
            qreal deltaAngle = (currentAngle - startAngle) * 180.0 / M_PI;
            m_lassoSelection.rotation = m_transformStartRotation + deltaAngle;
            break;
        }
        
        case HandleHit::TopLeft:
        case HandleHit::Top:
        case HandleHit::TopRight:
        case HandleHit::Left:
        case HandleHit::Right:
        case HandleHit::BottomLeft:
        case HandleHit::Bottom:
        case HandleHit::BottomRight:
            // Scale handles
            updateScaleFromHandle(m_transformHandle, viewportPos);
            break;
            
        case HandleHit::None:
            break;
    }
    
    // P2: Dirty region update - only repaint selection area + handles
    // Calculate visual bounds in viewport coordinates
    QRectF visualBoundsVp = getSelectionVisualBounds();
    if (!visualBoundsVp.isEmpty()) {
        // Expand for handles and rotation handle offset
        visualBoundsVp.adjust(
            -HANDLE_HIT_SIZE, 
            -ROTATE_HANDLE_OFFSET - HANDLE_HIT_SIZE,  // Rotation handle above
            HANDLE_HIT_SIZE, 
            HANDLE_HIT_SIZE
        );
        update(visualBoundsVp.toRect());
    } else {
        update();  // Fallback to full update
    }
}

QRectF DocumentViewport::getSelectionVisualBounds() const
{
    // Calculate the visual bounding box of the selection in viewport coordinates
    if (!m_lassoSelection.isValid()) {
        return QRectF();
    }
    
    QRectF box = m_lassoSelection.boundingBox;
    QTransform transform = buildSelectionTransform();
    
    // Transform the four corners
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport coordinates and get bounding rect
    QPolygonF vpCorners;
    if (m_document && m_document->isEdgeless()) {
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt);
        }
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    return vpCorners.boundingRect();
}

void DocumentViewport::updateScaleFromHandle(HandleHit handle, const QPointF& viewportPos)
{
    // Get current document position
    QPointF currentDocPos;
    if (m_document->isEdgeless()) {
        currentDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        currentDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    QPointF origin = m_lassoSelection.transformOrigin;
    QRectF startBounds = m_transformStartBounds;
    
    // Calculate original distances from center to edges
    qreal origLeft = startBounds.left() - origin.x();
    qreal origRight = startBounds.right() - origin.x();
    qreal origTop = startBounds.top() - origin.y();
    qreal origBottom = startBounds.bottom() - origin.y();
    
    // Calculate new distance from origin to current position
    qreal dx = currentDocPos.x() - origin.x();
    qreal dy = currentDocPos.y() - origin.y();
    
    // Apply rotation to get the position relative to the unrotated bounds
    qreal rotRad = m_transformStartRotation * M_PI / 180.0;
    qreal cosR = std::cos(-rotRad);
    qreal sinR = std::sin(-rotRad);
    qreal localX = dx * cosR - dy * sinR;
    qreal localY = dx * sinR + dy * cosR;
    
    // Calculate scale factors based on which handle is being dragged
    qreal newScaleX = m_transformStartScaleX;
    qreal newScaleY = m_transformStartScaleY;
    
    switch (handle) {
        case HandleHit::TopLeft:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::Top:
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::TopRight:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::Left:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            break;
            
        case HandleHit::Right:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            break;
            
        case HandleHit::BottomLeft:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        case HandleHit::Bottom:
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        case HandleHit::BottomRight:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        default:
            break;
    }
    
    // Clamp scale to reasonable values (prevent inversion and extreme scaling)
    // Use 0.1 minimum to allow shrinking but prevent disappearance
    newScaleX = qBound(0.1, newScaleX, 10.0);
    newScaleY = qBound(0.1, newScaleY, 10.0);
    
    m_lassoSelection.scaleX = newScaleX;
    m_lassoSelection.scaleY = newScaleY;
}

void DocumentViewport::finalizeSelectionTransform()
{
    m_isTransformingSelection = false;
    m_transformHandle = HandleHit::None;
    // Transform is applied visually; actual stroke modification happens on:
    // - Click elsewhere (apply and clear)
    // - Paste (apply to new location)
    // - Delete (remove originals)
    update();
}

void DocumentViewport::transformStrokePoints(VectorStroke& stroke, const QTransform& transform)
{
    for (StrokePoint& pt : stroke.points) {
        pt.pos = transform.map(pt.pos);
    }
    stroke.updateBoundingBox();
}

void DocumentViewport::applySelectionTransform()
{
    if (!m_lassoSelection.isValid() || !m_document) {
        return;
    }
    
    QTransform transform = buildSelectionTransform();
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // More complex: strokes may span multiple tiles after transform
        // Strategy: remove all original strokes, then add transformed strokes
        // using the same tile-splitting logic as regular stroke creation
        
        // First, remove original strokes from their source tiles
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;
            
            // Find and remove strokes that match our selection by ID
            QVector<VectorStroke>& layerStrokes = layer->strokes();
            for (int i = layerStrokes.size() - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        layerStrokes.removeAt(i);
                        layer->invalidateStrokeCache();
                        m_document->markTileDirty(coord);
                        break;
                    }
                }
            }
        }
        
        // Now add transformed strokes back using the same tile-splitting logic
        // as finishStrokeEdgeless() to handle strokes crossing tile boundaries
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            // Note: addStrokeToEdgelessTiles() generates new IDs for each segment
            
            // Use the shared helper that properly splits strokes at tile boundaries
            addStrokeToEdgelessTiles(transformedStroke, m_lassoSelection.sourceLayerIndex);
        }
        
        // TODO: Push to edgeless undo stack
        
    } else {
        // ========== PAGED MODE ==========
        // Simpler: all strokes are on the same page/layer
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;
        
        // Remove original strokes by ID
        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = layerStrokes.size() - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        
        // Add transformed strokes with new IDs
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            layer->addStroke(transformedStroke);
        }
        
        layer->invalidateStrokeCache();
        
        // Push to undo stack
        // TODO: Create a compound undo action for remove + add
    }
    
    clearLassoSelection();
    emit documentModified();
}

void DocumentViewport::cancelSelectionTransform()
{
    // Simply clear the selection without applying the transform
    // The original strokes remain untouched
    clearLassoSelection();
}

// ===== Clipboard Operations (Task 2.10.7) =====

void DocumentViewport::copySelection()
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    m_clipboard.clear();
    
    // Get current transform and apply it to strokes before copying
    QTransform transform = buildSelectionTransform();
    
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        VectorStroke transformedStroke = stroke;
        transformStrokePoints(transformedStroke, transform);
        // Give new ID to avoid conflicts when pasting
        transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_clipboard.strokes.append(transformedStroke);
    }
    
    m_clipboard.hasContent = true;
}

void DocumentViewport::cutSelection()
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    // Copy first
    copySelection();
    
    // Then delete
    deleteSelection();
}

void DocumentViewport::pasteSelection()
{
    if (!m_clipboard.hasContent || m_clipboard.strokes.isEmpty() || !m_document) {
        return;
    }
    
    // Calculate clipboard bounding box
    QRectF clipboardBounds;
    for (const VectorStroke& stroke : m_clipboard.strokes) {
        if (clipboardBounds.isNull()) {
            clipboardBounds = stroke.boundingBox;
        } else {
            clipboardBounds = clipboardBounds.united(stroke.boundingBox);
        }
    }
    
    // Calculate paste offset: center clipboard content at viewport center
    QPointF viewCenter(width() / 2.0, height() / 2.0);
    QPointF docCenter = viewportToDocument(viewCenter);
    QPointF clipboardCenter = clipboardBounds.center();
    QPointF offset = docCenter - clipboardCenter;
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Add strokes to appropriate tiles, splitting at tile boundaries
        // Uses the same logic as finishStrokeEdgeless() for consistency
        
        for (const VectorStroke& stroke : m_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            
            // Apply paste offset (stroke is now in document coordinates)
            for (StrokePoint& pt : pastedStroke.points) {
                pt.pos += offset;
            }
            pastedStroke.updateBoundingBox();
            // Note: addStrokeToEdgelessTiles() generates new IDs for each segment
            
            // Use the shared helper that properly splits strokes at tile boundaries
            addStrokeToEdgelessTiles(pastedStroke, m_edgelessActiveLayerIndex);
        }
        
        // TODO: Push to edgeless undo stack
        
    } else {
        // ========== PAGED MODE ==========
        // Paste to current page's active layer
        
        int pageIndex = currentPageIndex();
        if (pageIndex < 0 || pageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(pageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) return;
        
        // Adjust offset for paged mode (use page-local coordinates)
        QPointF pageOrigin = pagePosition(pageIndex);
        QPointF pageCenter = docCenter - pageOrigin;
        offset = pageCenter - clipboardCenter;
        
        for (const VectorStroke& stroke : m_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            
            // Apply paste offset
            for (StrokePoint& pt : pastedStroke.points) {
                pt.pos += offset;
            }
            pastedStroke.updateBoundingBox();
            pastedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            
            layer->addStroke(pastedStroke);
        }
        
        layer->invalidateStrokeCache();
        
        // TODO: Push to undo stack
    }
    
    update();
    emit documentModified();
}

void DocumentViewport::deleteSelection()
{
    if (!m_lassoSelection.isValid() || !m_document) {
        return;
    }
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Remove strokes from their tiles by ID
        
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;
            
            QVector<VectorStroke>& layerStrokes = layer->strokes();
            bool modified = false;
            
            for (int i = layerStrokes.size() - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        layerStrokes.removeAt(i);
                        modified = true;
                        break;
                    }
                }
            }
            
            if (modified) {
                layer->invalidateStrokeCache();
                m_document->markTileDirty(coord);
            }
        }
        
        // TODO: Push to edgeless undo stack
        
    } else {
        // ========== PAGED MODE ==========
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;
        
        // Remove strokes by ID
        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = layerStrokes.size() - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        
        layer->invalidateStrokeCache();
        
        // TODO: Push to undo stack
    }
    
    clearLassoSelection();
    update();
    emit documentModified();
}

void DocumentViewport::clearLassoSelection()
{
    m_lassoSelection.clear();
    m_lassoPath.clear();
    m_isDrawingLasso = false;
    
    // P1: Reset cache state
    m_lastRenderedLassoIdx = 0;
    m_lassoPathLength = 0;
    
    update();
}

void DocumentViewport::addPointToStroke(const QPointF& pagePos, qreal pressure)
{
    // ========== OPTIMIZATION: Point Decimation ==========
    // At 360Hz, consecutive points are often <1 pixel apart.
    // Skip points that are too close to reduce memory and rendering work.
    // This typically reduces point count by 50-70% with no visible quality loss.
    
    if (!m_currentStroke.points.isEmpty()) {
        const QPointF& lastPos = m_currentStroke.points.last().pos;
        qreal dx = pagePos.x() - lastPos.x();
        qreal dy = pagePos.y() - lastPos.y();
        qreal distSq = dx * dx + dy * dy;
        
        if (distSq < MIN_DISTANCE_SQ) {
            // Point too close - but update pressure if higher (preserve pressure peaks)
            if (pressure > m_currentStroke.points.last().pressure) {
                m_currentStroke.points.last().pressure = pressure;
            }
            return;  // Skip this point
        }
    }
    
    StrokePoint pt;
    pt.pos = pagePos;
    pt.pressure = qBound(0.1, pressure, 1.0);
    m_currentStroke.points.append(pt);
    
    // ========== OPTIMIZATION: Dirty Region Update ==========
    // Only repaint the small region around the new point instead of the entire widget.
    // This significantly improves performance, especially on lower-end hardware.
    
    qreal padding = m_penThickness * 2 * m_zoomLevel;  // Extra padding for stroke width
    
    // Convert page position to viewport coordinates
    QPointF vpPos = pageToViewport(m_activeDrawingPage, pagePos);
    QRectF dirtyRect(vpPos.x() - padding, vpPos.y() - padding, padding * 2, padding * 2);
    
    // Include line from previous point if exists
    if (m_currentStroke.points.size() > 1) {
        const auto& prevPt = m_currentStroke.points[m_currentStroke.points.size() - 2];
        QPointF prevVpPos = pageToViewport(m_activeDrawingPage, prevPt.pos);
        QRectF prevRect(prevVpPos.x() - padding, prevVpPos.y() - padding, padding * 2, padding * 2);
        dirtyRect = dirtyRect.united(prevRect);
    }
    
    // Update only the dirty region (much faster than full widget repaint)
    update(dirtyRect.toRect().adjusted(-2, -2, 2, 2));
}

// ===== Incremental Stroke Rendering (Task 2.3) =====

void DocumentViewport::resetCurrentStrokeCache()
{
    // Create cache at viewport size with high DPI support
    qreal dpr = devicePixelRatioF();
    QSize physicalSize(static_cast<int>(width() * dpr), 
                       static_cast<int>(height() * dpr));
    
    m_currentStrokeCache = QPixmap(physicalSize);
    m_currentStrokeCache.setDevicePixelRatio(dpr);
    m_currentStrokeCache.fill(Qt::transparent);
    m_lastRenderedPointIndex = 0;
    
    // Track the transform state when cache was created
    m_cacheZoom = m_zoomLevel;
    m_cachePan = m_panOffset;
}

void DocumentViewport::renderCurrentStrokeIncremental(QPainter& painter)
{
    // ========== OPTIMIZATION: Incremental Stroke Rendering ==========
    // Instead of re-rendering the entire current stroke every frame,
    // we accumulate rendered segments in m_currentStrokeCache and only
    // render NEW segments to the cache. This reduces CPU load significantly
    // when drawing long strokes at high poll rates (360Hz).
    
    const int n = m_currentStroke.points.size();
    if (n < 1) return;
    
    // For paged mode, require valid drawing page
    bool isEdgeless = m_document && m_document->isEdgeless();
    if (!isEdgeless && m_activeDrawingPage < 0) return;
    
    // Ensure cache is valid (may need recreation after resize or transform change)
    qreal dpr = devicePixelRatioF();
    QSize expectedSize(static_cast<int>(width() * dpr), 
                       static_cast<int>(height() * dpr));
    
    // Check if cache needs full rebuild (size changed, or transform changed during drawing)
    bool needsRebuild = m_currentStrokeCache.isNull() || 
                        m_currentStrokeCache.size() != expectedSize ||
                        !qFuzzyCompare(m_cacheZoom, m_zoomLevel) ||
                        m_cachePan != m_panOffset;
    
    if (needsRebuild) {
        resetCurrentStrokeCache();
        // Must re-render all points since transform changed
    }
    
    // ========== FIX: Semi-Transparent Stroke Rendering ==========
    // For strokes with alpha < 255 (e.g., marker at 50% opacity), we must draw
    // with FULL OPACITY to the cache, then blit with the desired opacity.
    // Otherwise, overlapping segments at joints would compound the alpha,
    // making in-progress strokes appear darker than finished strokes.
    
    int strokeAlpha = m_currentStroke.color.alpha();
    bool hasSemiTransparency = (strokeAlpha < 255);
    
    // Create the drawing color - use full opacity for cache, apply alpha on blit
    QColor drawColor = m_currentStroke.color;
    if (hasSemiTransparency) {
        drawColor.setAlpha(255);  // Draw opaque to cache
    }
    
    // Render new segments to the cache (if any)
    if (n > m_lastRenderedPointIndex && n >= 2) {
        QPainter cachePainter(&m_currentStrokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Apply transform to convert coords to viewport coords
        // The cache is in viewport coordinates (widget pixels)
        cachePainter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        cachePainter.scale(m_zoomLevel, m_zoomLevel);
        
        // For paged mode, translate to page position
        // For edgeless, stroke points are already in document coords - no extra translate
        if (!isEdgeless) {
            cachePainter.translate(pagePosition(m_activeDrawingPage));
        }
        
        // Use line-based rendering for incremental updates (fast)
        QPen pen(drawColor, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
        // Start from the last rendered point (or 1 if starting fresh)
        int startIdx = qMax(1, m_lastRenderedPointIndex);
        
        // Render each new segment
        for (int i = startIdx; i < n; ++i) {
            const auto& p0 = m_currentStroke.points[i - 1];
            const auto& p1 = m_currentStroke.points[i];
            
            qreal avgPressure = (p0.pressure + p1.pressure) / 2.0;
            qreal width = qMax(m_currentStroke.baseThickness * avgPressure, 1.0);
            
            pen.setWidthF(width);
            cachePainter.setPen(pen);
            cachePainter.drawLine(p0.pos, p1.pos);
        }
        
        // Draw start cap if this is the first render
        if (m_lastRenderedPointIndex == 0 && n >= 1) {
            qreal startRadius = qMax(m_currentStroke.baseThickness * m_currentStroke.points[0].pressure, 1.0) / 2.0;
            cachePainter.setPen(Qt::NoPen);
            cachePainter.setBrush(drawColor);
            cachePainter.drawEllipse(m_currentStroke.points[0].pos, startRadius, startRadius);
        }
        
        m_lastRenderedPointIndex = n;
    }
    
    // Blit the cached current stroke to the viewport
    // For semi-transparent strokes, apply the alpha here (not per-segment)
    if (hasSemiTransparency) {
        painter.setOpacity(strokeAlpha / 255.0);
    }
    painter.drawPixmap(0, 0, m_currentStrokeCache);
    if (hasSemiTransparency) {
        painter.setOpacity(1.0);  // Restore full opacity
    }
    
    // Draw end cap at current position (always needs updating as it moves)
    if (n >= 1) {
        // Apply transform to draw end cap at correct position
        painter.save();
        painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        painter.scale(m_zoomLevel, m_zoomLevel);
        
        // For paged mode, translate to page position
        // For edgeless, stroke points are already in document coords
        if (!isEdgeless) {
            painter.translate(pagePosition(m_activeDrawingPage));
        }
        
        qreal endRadius = qMax(m_currentStroke.baseThickness * m_currentStroke.points[n - 1].pressure, 1.0) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_currentStroke.color);
        painter.drawEllipse(m_currentStroke.points[n - 1].pos, endRadius, endRadius);
        
        painter.restore();
    }
}

// ===== Eraser Tool (Task 2.4) =====

void DocumentViewport::eraseAt(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Branch for edgeless mode (Phase E4)
    if (m_document->isEdgeless()) {
        eraseAtEdgeless(pe.viewportPos);
        return;
    }
    
    // Paged mode: require valid page hit
    if (!pe.pageHit.valid()) return;
    
    Page* page = m_document->page(pe.pageHit.pageIndex);
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer || layer->locked) return;
    
    // Find strokes at eraser position
    QVector<QString> hitIds = layer->strokesAtPoint(pe.pageHit.pagePoint, m_eraserSize);
    
    if (hitIds.isEmpty()) return;
    
    // Collect strokes for undo before removing
    // Use a set for O(1) lookup instead of O(n) per ID
    QSet<QString> hitIdSet(hitIds.begin(), hitIds.end());
    QVector<VectorStroke> removedStrokes;
    removedStrokes.reserve(hitIds.size());
    
    for (const VectorStroke& s : layer->strokes()) {
        if (hitIdSet.contains(s.id)) {
            removedStrokes.append(s);
            if (removedStrokes.size() == hitIds.size()) {
                break;  // Found all strokes, no need to continue
            }
        }
    }
    
    // Remove strokes
    for (const QString& id : hitIds) {
        layer->removeStroke(id);
    }
    
    // Stroke cache is automatically invalidated by removeStroke()
    
    // Push undo action
    if (removedStrokes.size() == 1) {
        pushUndoAction(pe.pageHit.pageIndex, PageUndoAction::RemoveStroke, removedStrokes[0]);
    } else if (removedStrokes.size() > 1) {
        pushUndoAction(pe.pageHit.pageIndex, PageUndoAction::RemoveMultiple, removedStrokes);
    }
    
    emit documentModified();
    
    // ========== OPTIMIZATION: Dirty Region Update for Eraser ==========
    // Calculate region around eraser position for targeted repaint
    qreal eraserRadius = m_eraserSize * m_zoomLevel;
    QPointF vpPos = pe.viewportPos;
    QRectF dirtyRect(vpPos.x() - eraserRadius - 10, vpPos.y() - eraserRadius - 10,
                     (eraserRadius + 10) * 2, (eraserRadius + 10) * 2);
    update(dirtyRect.toRect());
}

void DocumentViewport::eraseAtEdgeless(QPointF viewportPos)
{
    // ========== EDGELESS ERASER (Phase E4) ==========
    // In edgeless mode, strokes are split across tiles. The eraser must:
    // 1. Convert viewport position to document coordinates
    // 2. Check the center tile AND neighboring tiles (for cross-tile strokes)
    // 3. Convert document coords to tile-local coords for hit testing
    // 4. Collect strokes for undo, then remove them
    // 5. Mark tiles dirty and remove if empty
    
    if (!m_document || !m_document->isEdgeless()) return;
    
    // Convert viewport position to document coordinates
    QPointF docPt = viewportToDocument(viewportPos);
    
    // Get center tile coordinate
    Document::TileCoord centerTile = m_document->tileCoordForPoint(docPt);
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Collect all erased strokes for undo (Phase E6)
    EdgelessUndoAction undoAction;
    undoAction.type = PageUndoAction::RemoveStroke;
    undoAction.layerIndex = m_edgelessActiveLayerIndex;
    
    // Check center tile + 8 neighbors (3x3 grid)
    // This catches strokes that span tile boundaries
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            int tx = centerTile.first + dx;
            int ty = centerTile.second + dy;
            
            Page* tile = m_document->getTile(tx, ty);
            if (!tile) continue;  // Empty tile
            
            // Get the active layer (use edgeless active layer index)
            if (m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer || layer->locked) continue;
            
            // Convert document point to tile-local coordinates
            QPointF tileOrigin(tx * tileSize, ty * tileSize);
            QPointF localPt = docPt - tileOrigin;
            
            // Find strokes at eraser position
            QVector<QString> hitIds = layer->strokesAtPoint(localPt, m_eraserSize);
            
            if (hitIds.isEmpty()) continue;
            
            // Collect strokes for undo BEFORE removing (Phase E6)
            for (const QString& id : hitIds) {
                // Find the stroke by ID and copy it for undo
                for (const VectorStroke& stroke : layer->strokes()) {
                    if (stroke.id == id) {
                        undoAction.segments.append({{tx, ty}, stroke});
                        break;
                    }
                }
            }
            
            // Remove strokes
            for (const QString& id : hitIds) {
                layer->removeStroke(id);
            }
            
            // Mark tile as dirty for persistence (before potential removal)
            m_document->markTileDirty({tx, ty});
            
            // Remove tile if now empty (saves memory, tile file deleted on next save)
            m_document->removeTileIfEmpty(tx, ty);
        }
    }
    
    // Push undo action if any strokes were erased
    if (!undoAction.segments.isEmpty()) {
        pushEdgelessUndoAction(undoAction);
        emit documentModified();
        
        // Dirty region update
        qreal eraserRadius = m_eraserSize * m_zoomLevel;
        QRectF dirtyRect(viewportPos.x() - eraserRadius - 10, viewportPos.y() - eraserRadius - 10,
                         (eraserRadius + 10) * 2, (eraserRadius + 10) * 2);
        update(dirtyRect.toRect());
    }
}

void DocumentViewport::drawEraserCursor(QPainter& painter)
{
    // Show eraser cursor for: selected eraser tool OR active hardware eraser
    bool showCursor = (m_currentTool == ToolType::Eraser || m_hardwareEraserActive);
    
    if (!showCursor) {
        return;
    }
    
    // Only draw if pointer is within the viewport
    // Note: Don't use isNull() - QPointF(0,0) is a valid position!
    if (!rect().contains(m_lastPointerPos.toPoint())) {
        return;
    }
    
    // Draw eraser circle at last pointer position (in viewport coordinates)
    // The eraser size is in document units, so scale by zoom for screen display
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    
    qreal screenRadius = m_eraserSize * m_zoomLevel;
    painter.drawEllipse(m_lastPointerPos, screenRadius, screenRadius);
}

// ===== Undo/Redo System (Task 2.5) =====

void DocumentViewport::pushUndoAction(int pageIndex, PageUndoAction::Type type, const VectorStroke& stroke)
{
    PageUndoAction action;
    action.type = type;
    action.pageIndex = pageIndex;
    action.stroke = stroke;
    
    m_undoStacks[pageIndex].push(action);
    trimUndoStack(pageIndex);
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::pushUndoAction(int pageIndex, PageUndoAction::Type type, const QVector<VectorStroke>& strokes)
{
    PageUndoAction action;
    action.type = type;
    action.pageIndex = pageIndex;
    action.strokes = strokes;
    
    m_undoStacks[pageIndex].push(action);
    trimUndoStack(pageIndex);
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::clearRedoStack(int pageIndex)
{
    if (m_redoStacks.contains(pageIndex)) {
        bool hadRedo = !m_redoStacks[pageIndex].isEmpty();
        m_redoStacks[pageIndex].clear();
        if (hadRedo) {
            emit redoAvailableChanged(false);
        }
    }
}

void DocumentViewport::clearUndoStacksFrom(int pageIndex)
{
    // Clear undo/redo stacks for all pages >= pageIndex
    // Used when inserting/deleting pages to prevent stale undo from applying to wrong pages
    // Preserves undo for pages before the affected index (user's "done" work)
    
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    
    // Clear undo stacks for affected pages
    for (auto it = m_undoStacks.begin(); it != m_undoStacks.end(); ) {
        if (it.key() >= pageIndex) {
            it = m_undoStacks.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear redo stacks for affected pages
    for (auto it = m_redoStacks.begin(); it != m_redoStacks.end(); ) {
        if (it.key() >= pageIndex) {
            it = m_redoStacks.erase(it);
        } else {
            ++it;
        }
    }
    
    // Emit signals if availability changed
    if (hadUndo && !canUndo()) {
        emit undoAvailableChanged(false);
    }
    if (hadRedo && !canRedo()) {
        emit redoAvailableChanged(false);
    }
}

// ============================================================================
// Layer Management (Phase 5)
// ============================================================================

void DocumentViewport::setEdgelessActiveLayerIndex(int layerIndex)
{
    if (layerIndex < 0) layerIndex = 0;
    m_edgelessActiveLayerIndex = layerIndex;
}

void DocumentViewport::trimUndoStack(int pageIndex)
{
    // Limit stack size to prevent unbounded memory growth
    // QStack inherits from QVector, so we can use remove() for O(n) trimming
    // This only runs when stack exceeds limit (rare - once every MAX_UNDO_PER_PAGE actions)
    QStack<PageUndoAction>& stack = m_undoStacks[pageIndex];
    
    while (stack.size() > MAX_UNDO_PER_PAGE) {
        // Remove oldest entry (at the bottom of the stack = index 0)
        // QStack inherits QVector's remove() method
        stack.remove(0);
    }
}

// ===== Edgeless Undo/Redo (Phase E6) =====

void DocumentViewport::pushEdgelessUndoAction(const EdgelessUndoAction& action)
{
    m_edgelessUndoStack.push(action);
    trimEdgelessUndoStack();
    clearEdgelessRedoStack();
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::undoEdgeless()
{
    if (m_edgelessUndoStack.isEmpty() || !m_document) return;
    
    EdgelessUndoAction action = m_edgelessUndoStack.pop();
    
    // Apply undo to each segment (may span multiple tiles)
    for (const auto& seg : action.segments) {
        Page* tile = nullptr;
        
        if (action.type == PageUndoAction::AddStroke) {
            // Undoing an add = remove the stroke (tile might not exist if already removed)
            tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
        } else {
            // Undoing a remove = add the stroke back (may need to recreate tile)
            tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
        }
        
        if (!tile) continue;
        
        // Ensure layer exists
        while (tile->layerCount() <= action.layerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        VectorLayer* layer = tile->layer(action.layerIndex);
        if (!layer) continue;
        
        switch (action.type) {
            case PageUndoAction::AddStroke:
                // Undo add = remove the stroke
                layer->removeStroke(seg.stroke.id);
                // Mark dirty BEFORE potential removal (removeTileIfEmpty clears dirty flag)
                m_document->markTileDirty(seg.tileCoord);
                // Check if tile is now empty
                m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
                break;
                
            case PageUndoAction::RemoveStroke:
            case PageUndoAction::RemoveMultiple:
                // Undo remove = add the stroke back
                layer->addStroke(seg.stroke);
                m_document->markTileDirty(seg.tileCoord);
                break;
        }
    }
    
    // Push to redo stack
    m_edgelessRedoStack.push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

void DocumentViewport::redoEdgeless()
{
    if (m_edgelessRedoStack.isEmpty() || !m_document) return;
    
    EdgelessUndoAction action = m_edgelessRedoStack.pop();
    
    // Apply redo to each segment
    for (const auto& seg : action.segments) {
        Page* tile = nullptr;
        
        if (action.type == PageUndoAction::AddStroke) {
            // Redoing an add = add the stroke back (may need to recreate tile)
            tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
        } else {
            // Redoing a remove = remove the stroke (tile might not exist if already removed)
            tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
        }
        
        if (!tile) continue;
        
        // Ensure layer exists
        while (tile->layerCount() <= action.layerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        VectorLayer* layer = tile->layer(action.layerIndex);
        if (!layer) continue;
        
        switch (action.type) {
            case PageUndoAction::AddStroke:
                // Redo add = add the stroke again
                layer->addStroke(seg.stroke);
                m_document->markTileDirty(seg.tileCoord);
                break;
                
            case PageUndoAction::RemoveStroke:
            case PageUndoAction::RemoveMultiple:
                // Redo remove = remove the stroke again
                layer->removeStroke(seg.stroke.id);
                // Mark dirty BEFORE potential removal (removeTileIfEmpty clears dirty flag)
                m_document->markTileDirty(seg.tileCoord);
                // Check if tile is now empty
                m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
                break;
        }
    }
    
    // Push to undo stack
    m_edgelessUndoStack.push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

void DocumentViewport::clearEdgelessRedoStack()
{
    if (!m_edgelessRedoStack.isEmpty()) {
        m_edgelessRedoStack.clear();
        emit redoAvailableChanged(canRedo());
    }
}

void DocumentViewport::trimEdgelessUndoStack()
{
    while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
        // Remove oldest entry (at the bottom of the stack = index 0)
        m_edgelessUndoStack.remove(0);
    }
}

void DocumentViewport::undo()
{
    // Edgeless mode uses global undo stack
    if (m_document && m_document->isEdgeless()) {
        undoEdgeless();
        return;
    }
    
    // Paged mode: per-page undo
    int pageIdx = m_currentPageIndex;
    
    if (!m_undoStacks.contains(pageIdx) || m_undoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer) return;
    
    PageUndoAction action = m_undoStacks[pageIdx].pop();
    
    switch (action.type) {
        case PageUndoAction::AddStroke:
            // Undo adding = remove the stroke
            layer->removeStroke(action.stroke.id);
            break;
            
        case PageUndoAction::RemoveStroke:
            // Undo removing = add the stroke back
            layer->addStroke(action.stroke);
            break;
            
        case PageUndoAction::RemoveMultiple:
            // Undo removing multiple = add all strokes back
            for (const auto& s : action.strokes) {
                layer->addStroke(s);
            }
            break;
    }
    
    // Push to redo stack
    m_redoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

void DocumentViewport::redo()
{
    // Edgeless mode uses global redo stack
    if (m_document && m_document->isEdgeless()) {
        redoEdgeless();
        return;
    }
    
    // Paged mode: per-page redo
    int pageIdx = m_currentPageIndex;
    
    if (!m_redoStacks.contains(pageIdx) || m_redoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer) return;
    
    PageUndoAction action = m_redoStacks[pageIdx].pop();
    
    switch (action.type) {
        case PageUndoAction::AddStroke:
            // Redo adding = add the stroke again
            layer->addStroke(action.stroke);
            break;
            
        case PageUndoAction::RemoveStroke:
            // Redo removing = remove the stroke again
            layer->removeStroke(action.stroke.id);
            break;
            
        case PageUndoAction::RemoveMultiple:
            // Redo removing multiple = remove all strokes again
            for (const auto& s : action.strokes) {
                layer->removeStroke(s.id);
            }
            break;
    }
    
    // Push back to undo stack
    m_undoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

bool DocumentViewport::canUndo() const
{
    // Edgeless mode uses global undo stack
    if (m_document && m_document->isEdgeless()) {
        return !m_edgelessUndoStack.isEmpty();
    }
    // Paged mode: per-page undo
    return m_undoStacks.contains(m_currentPageIndex) && 
           !m_undoStacks[m_currentPageIndex].isEmpty();
}

bool DocumentViewport::canRedo() const
{
    // Edgeless mode uses global redo stack
    if (m_document && m_document->isEdgeless()) {
        return !m_edgelessRedoStack.isEmpty();
    }
    // Paged mode: per-page redo
    return m_redoStacks.contains(m_currentPageIndex) && 
           !m_redoStacks[m_currentPageIndex].isEmpty();
}

// ===== Benchmark (Task 2.6) =====

void DocumentViewport::startBenchmark()
{
    m_benchmarking = true;
    m_paintTimestamps.clear();
    m_benchmarkTimer.start();
    
    // Start periodic display updates (1000ms = 1 update/sec)
    m_benchmarkDisplayTimer.start(1000);
}

void DocumentViewport::stopBenchmark()
{
    m_benchmarking = false;
    m_benchmarkDisplayTimer.stop();
}

int DocumentViewport::getPaintRate() const
{
    if (!m_benchmarking) return 0;
    
    qint64 now = m_benchmarkTimer.elapsed();
    
    // Remove timestamps older than 1 second
    while (!m_paintTimestamps.empty() && now - m_paintTimestamps.front() > 1000) {
        m_paintTimestamps.pop_front();
    }
    
    return static_cast<int>(m_paintTimestamps.size());
}

// ===== Rendering Helpers (Task 1.3.3) =====

void DocumentViewport::renderPage(QPainter& painter, Page* page, int pageIndex)
{
    if (!page || !m_document) return;
    
    Q_UNUSED(pageIndex);  // Used for PDF page lookup via page->pdfPageNumber
    
    QSizeF pageSize = page->size;
    QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
    
    // 1. Fill with page background color
    painter.fillRect(pageRect, page->backgroundColor);
    
    // 2. Render background based on type
    switch (page->backgroundType) {
        case Page::BackgroundType::None:
            // Just the background color (already filled)
            break;
            
        case Page::BackgroundType::PDF:
            // Render PDF page from cache (Task 1.3.6)
            if (m_document->isPdfLoaded() && page->pdfPageNumber >= 0) {
                qreal dpi = effectivePdfDpi();
                QPixmap pdfPixmap = getCachedPdfPage(page->pdfPageNumber, dpi);
                
                if (!pdfPixmap.isNull()) {
                    // Scale pixmap to fit page rect
                    painter.drawPixmap(pageRect.toRect(), pdfPixmap);
                }
            }
            break;
            
        case Page::BackgroundType::Custom:
            // Draw custom background image
            if (!page->customBackground.isNull()) {
                painter.drawPixmap(pageRect.toRect(), page->customBackground);
            }
            break;
            
        case Page::BackgroundType::Grid:
            {
                // Draw grid lines
                painter.setPen(QPen(page->gridColor, 1.0 / m_zoomLevel));  // Constant line width
                qreal spacing = page->gridSpacing;
                
                // Vertical lines
                for (qreal x = spacing; x < pageSize.width(); x += spacing) {
                    painter.drawLine(QPointF(x, 0), QPointF(x, pageSize.height()));
                }
                
                // Horizontal lines
                for (qreal y = spacing; y < pageSize.height(); y += spacing) {
                    painter.drawLine(QPointF(0, y), QPointF(pageSize.width(), y));
                }
            }
            break;
            
        case Page::BackgroundType::Lines:
            {
                // Draw horizontal ruled lines
                painter.setPen(QPen(page->gridColor, 1.0 / m_zoomLevel));  // Constant line width
                qreal spacing = page->lineSpacing;
                
                for (qreal y = spacing; y < pageSize.height(); y += spacing) {
                    painter.drawLine(QPointF(0, y), QPointF(pageSize.width(), y));
                }
            }
            break;
    }
    
    // 3. Render vector layers with ZOOM-AWARE stroke cache
    // The cache is built at pageSize * zoom * dpr physical pixels, ensuring
    // sharp rendering at any zoom level. The cache's devicePixelRatio is set
    // to zoom * dpr, so Qt handles coordinate mapping correctly.
    painter.setRenderHint(QPainter::Antialiasing, true);
    qreal dpr = devicePixelRatioF();
    
    // CR-2B-7: Check if this page has selected strokes that should be excluded
    bool hasSelectionOnThisPage = m_lassoSelection.isValid() && 
                                   m_lassoSelection.sourcePageIndex == pageIndex;
    QSet<QString> excludeIds;
    if (hasSelectionOnThisPage) {
        excludeIds = m_lassoSelection.getSelectedIds();
    }
    
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer && layer->visible) {
            // CR-2B-7: If this layer contains selected strokes, render with exclusion
            // to hide originals (they'll be rendered transformed in renderLassoSelection)
            if (hasSelectionOnThisPage && layerIdx == m_lassoSelection.sourceLayerIndex) {
                // Render manually, skipping selected strokes (bypasses cache)
                painter.save();
                painter.scale(m_zoomLevel, m_zoomLevel);
                layer->renderExcluding(painter, excludeIds);
                painter.restore();
            } else {
                // Use zoom-aware cache for maximum performance
                // The painter is scaled by zoom, cache is at zoom * dpr resolution
                layer->renderWithZoomCache(painter, pageSize, m_zoomLevel, dpr);
            }
        }
    }
    
    // 4. Render inserted objects (sorted by z-order)
    page->renderObjects(painter, 1.0);
    
    // 5. Draw page border (optional, for visual separation)
    // CUSTOMIZABLE: Page border color (theme setting)
    // The border does not need to be redrawn every time the page is rendered. 
    painter.setPen(QPen(QColor(180, 180, 180), 1.0 / m_zoomLevel));  // Light gray border
    painter.drawRect(pageRect);
}

// ===== Edgeless Mode Rendering (Phase E2) =====

void DocumentViewport::renderEdgelessMode(QPainter& painter)
{
    if (!m_document || !m_document->isEdgeless()) return;
    
    // Get visible rect in document coordinates
    QRectF viewRect = visibleRect();
    
    // ========== TILE RENDERING STRATEGY ==========
    // With stroke splitting, cross-tile strokes are stored as separate segments in each tile.
    // Each segment is rendered when its tile is rendered - no margin needed for cross-tile!
    // Small margin handles thick strokes extending slightly beyond tile boundary.
    // CR-9: STROKE_MARGIN is max expected stroke width + anti-aliasing buffer
    constexpr int STROKE_MARGIN = 100;
    
    // CR-5: Single tilesInRect() call - use stroke margin for all tiles
    // Background pass will filter to viewRect bounds
    QRectF strokeRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN, STROKE_MARGIN, STROKE_MARGIN);
    QVector<Document::TileCoord> allTiles = m_document->tilesInRect(strokeRect);
    
    // Pre-calculate visible tile range for background filtering
    int tileSize = Document::EDGELESS_TILE_SIZE;
    int minVisibleTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxVisibleTx = static_cast<int>(std::floor(viewRect.right() / tileSize));
    int minVisibleTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxVisibleTy = static_cast<int>(std::floor(viewRect.bottom() / tileSize));
    
    // Apply view transform (same as paged mode)
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // ========== PASS 1: Render backgrounds for VISIBLE tiles only ==========
    // This ensures non-blank canvas without wasting time on off-screen tiles.
    // For 1920x1080 viewport with 1024x1024 tiles: up to 9 tiles (3x3 worst case)
    // 
    // Uses Page::renderBackgroundPattern() to share grid/lines logic with Page::renderBackground().
    // Empty tile coordinates use document defaults; existing tiles use their own settings.
    for (const auto& coord : allTiles) {
        // CR-5: Skip tiles outside visible rect (margin tiles are for strokes only)
        if (coord.first < minVisibleTx || coord.first > maxVisibleTx ||
            coord.second < minVisibleTy || coord.second > maxVisibleTy) {
            continue;
        }
        
        QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
        QRectF tileRect(tileOrigin.x(), tileOrigin.y(), tileSize, tileSize);
        
        // Check if tile exists - use its settings, otherwise use document defaults
        Page* tile = m_document->getTile(coord.first, coord.second);
        
        if (tile) {
            // Existing tile: use its background settings
            Page::renderBackgroundPattern(
                painter,
                tileRect,
                tile->backgroundColor,
                tile->backgroundType,
                tile->gridColor,
                tile->gridSpacing,
                tile->lineSpacing,
                1.0 / m_zoomLevel  // Constant pen width in screen pixels
            );
        } else {
            // Empty tile coordinate: use document defaults
            Page::renderBackgroundPattern(
                painter,
                tileRect,
                m_document->defaultBackgroundColor,
                m_document->defaultBackgroundType,
                m_document->defaultGridColor,
                m_document->defaultGridSpacing,
                m_document->defaultLineSpacing,
                1.0 / m_zoomLevel  // Constant pen width in screen pixels
            );
        }
    }
    
    // ========== PASS 2: Render strokes (includes margin tiles for thick strokes) ==========
    // Strokes are split at tile boundaries, so each tile renders its own segments.
    // Margin tiles ensure thick strokes at edges are fully rendered.
    for (const auto& coord : allTiles) {
        Page* tile = m_document->getTile(coord.first, coord.second);
        if (!tile) continue;  // Skip empty tiles
        
        // Calculate tile origin in document coordinates
        QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
        
        painter.save();
        painter.translate(tileOrigin);
        renderTileStrokes(painter, tile, coord);  // Only render strokes, not background
        painter.restore();
    }
    
    // Draw tile boundary grid (debug)
    if (m_showTileBoundaries) {
        drawTileBoundaries(painter, viewRect);
    }
    
    painter.restore();
    
    // Render current stroke with incremental caching
    if (m_isDrawing && !m_currentStroke.points.isEmpty() && m_activeDrawingPage >= 0) {
        renderCurrentStrokeIncremental(painter);
    }
    
    // Task 2.9: Draw straight line preview (edgeless mode)
    if (m_isDrawingStraightLine) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Edgeless: coordinates are in document space
        QPointF vpStart = documentToViewport(m_straightLineStart);
        QPointF vpEnd = documentToViewport(m_straightLinePreviewEnd);
        
        // Use current tool's color and thickness
        QColor previewColor = (m_currentTool == ToolType::Marker) 
                              ? m_markerColor : m_penColor;
        qreal previewThickness = (m_currentTool == ToolType::Marker)
                                 ? m_markerThickness : m_penThickness;
        
        QPen pen(previewColor, previewThickness * m_zoomLevel, 
                 Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(vpStart, vpEnd);
        
        painter.restore();
    }
    
    // Task 2.10: Draw lasso selection path (edgeless mode)
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if (m_isDrawingLasso && m_lassoPath.size() > 1) {
        renderLassoPathIncremental(painter);
    }
    
    // Task 2.10.3: Draw lasso selection (edgeless mode)
    if (m_lassoSelection.isValid()) {
        renderLassoSelection(painter);
    }
}

// NOTE: renderTile() was removed (CR-2) - it was dead code duplicating 
// renderEdgelessMode() + renderTileStrokes()

void DocumentViewport::renderTileStrokes(QPainter& painter, Page* tile, Document::TileCoord coord)
{
    if (!tile) return;
    
    QSizeF tileSize = tile->size;
    
    // Render only vector layers (strokes may extend beyond tile bounds - OK!)
    painter.setRenderHint(QPainter::Antialiasing, true);
    qreal dpr = devicePixelRatioF();
    
    // CR-2B-7: Check if this tile has selected strokes that should be excluded
    // Note: In edgeless mode, selected strokes are stored in document coordinates,
    // but they originated from specific tiles. We check by ID across all tiles
    // since a selection might span multiple tiles.
    QSet<QString> excludeIds;
    if (m_lassoSelection.isValid()) {
        excludeIds = m_lassoSelection.getSelectedIds();
    }
    
    for (int layerIdx = 0; layerIdx < tile->layerCount(); ++layerIdx) {
        VectorLayer* layer = tile->layer(layerIdx);
        if (layer && layer->visible) {
            // CR-2B-7: If there's a selection on the active layer, exclude selected strokes
            if (!excludeIds.isEmpty() && layerIdx == m_edgelessActiveLayerIndex) {
                // Render manually, skipping selected strokes
                // Note: painter is already in tile-local coordinates
                layer->renderExcluding(painter, excludeIds);
            } else {
                layer->renderWithZoomCache(painter, tileSize, m_zoomLevel, dpr);
            }
        }
    }
    
    // Render inserted objects
    tile->renderObjects(painter, 1.0);
}

void DocumentViewport::drawTileBoundaries(QPainter& painter, QRectF viewRect)
{
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Calculate visible tile range
    int minTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxTx = static_cast<int>(std::ceil(viewRect.right() / tileSize));
    int minTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxTy = static_cast<int>(std::ceil(viewRect.bottom() / tileSize));
    
    // Semi-transparent dashed lines
    painter.setPen(QPen(QColor(100, 100, 100, 100), 1.0 / m_zoomLevel, Qt::DashLine));
    
    // Vertical lines
    for (int tx = minTx; tx <= maxTx; ++tx) {
        qreal x = tx * tileSize;
        painter.drawLine(QPointF(x, viewRect.top()), QPointF(x, viewRect.bottom()));
    }
    
    // Horizontal lines
    for (int ty = minTy; ty <= maxTy; ++ty) {
        qreal y = ty * tileSize;
        painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
    }
    
    // Draw origin marker (tile 0,0 corner)
    QPointF origin(0, 0);
    if (viewRect.contains(origin)) {
        painter.setPen(QPen(QColor(255, 100, 100), 2.0 / m_zoomLevel));
        painter.drawLine(QPointF(-20 / m_zoomLevel, 0), QPointF(20 / m_zoomLevel, 0));
        painter.drawLine(QPointF(0, -20 / m_zoomLevel), QPointF(0, 20 / m_zoomLevel));
    }
}

qreal DocumentViewport::minZoomForEdgeless() const
{
    // ========== EDGELESS MIN ZOOM CALCULATION ==========
    // With 1024x1024 tiles, a 1920x1080 viewport can show up to:
    //   - Best case (aligned): 2x2 = 4 tiles
    //   - Worst case (straddling): 3x3 = 9 tiles
    //
    // This limit prevents zooming out so far that too many tiles are visible.
    // We allow ~2 tiles worth of document space per viewport dimension.
    // At worst case (pan straddling tile boundaries), this means up to 9 tiles.
    //
    // Memory: 9 tiles × ~4MB each = ~36MB stroke cache at zoom 1.0, DPR 1.0
    
    constexpr qreal maxVisibleSize = 2.0 * Document::EDGELESS_TILE_SIZE;  // 2048
    
    // Use logical pixels (Qt handles DPI automatically)
    qreal minZoomX = static_cast<qreal>(width()) / maxVisibleSize;
    qreal minZoomY = static_cast<qreal>(height()) / maxVisibleSize;
    
    // Take the larger (more restrictive) value, with 10% floor
    return qMax(qMax(minZoomX, minZoomY), 0.1);
}

qreal DocumentViewport::effectivePdfDpi() const
{
    // Base DPI for 100% zoom on a 1x DPR screen
    constexpr qreal baseDpi = 96.0;
    
    // Get device pixel ratio for high DPI support
    // This handles Retina displays, Windows scaling (125%, 150%, 200%), etc.
    // Qt caches this value internally, so calling it is very fast
    qreal dpr = devicePixelRatioF();
    
    // Scale DPI with zoom level AND device pixel ratio for crisp rendering
    // At zoom > 1.0, we want higher DPI to avoid pixelation
    // At zoom < 1.0, we can use lower DPI to save memory/time
    // On high DPI screens, we need extra resolution to match physical pixels
    // 
    // Example: 200% Windows scaling (dpr=2.0) at zoom 1.0 → 192 DPI
    // Example: 100% scaling (dpr=1.0) at zoom 2.0 → 192 DPI
    qreal scaledDpi = baseDpi * m_zoomLevel * dpr;
    
    // Cap at reasonable maximum (300 DPI is print quality)
    // This prevents excessive memory usage at very high zoom on high DPI screens
    return qMin(scaledDpi, 300.0);
}

// ===== Private Methods =====

void DocumentViewport::clampPanOffset()
{
    if (!m_document) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    // For edgeless documents, allow unlimited pan (infinite canvas)
    if (m_document->isEdgeless()) {
        // No clamping for edgeless - user can pan anywhere
        return;
    }
    
    // Paged mode: require at least one page
    if (m_document->pageCount() == 0) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    QSizeF contentSize = totalContentSize();
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    // Allow some overscroll (50% of viewport)
    qreal overscrollX = viewWidth * 0.5;
    qreal overscrollY = viewHeight * 0.5;
    
    // Minimum pan: allow some overscroll at start
    qreal minX = -overscrollX;
    qreal minY = -overscrollY;
    
    // Maximum pan: can scroll to show end of content
    // If content is smaller than viewport, center it
    qreal maxX = qMax(0.0, contentSize.width() - viewWidth + overscrollX);
    qreal maxY = qMax(0.0, contentSize.height() - viewHeight + overscrollY);
    
    m_panOffset.setX(qBound(minX, m_panOffset.x(), maxX));
    m_panOffset.setY(qBound(minY, m_panOffset.y(), maxY));
}

void DocumentViewport::updateCurrentPageIndex()
{
    if (!m_document || m_document->pageCount() == 0) {
        m_currentPageIndex = 0;
        return;
    }
    
    // For edgeless documents, always page 0
    if (m_document->isEdgeless()) {
        m_currentPageIndex = 0;
        return;
    }
    
    int oldIndex = m_currentPageIndex;
    
    // Find the page that is most visible (has most area in viewport center)
    QRectF viewRect = visibleRect();
    QPointF viewCenter = viewRect.center();
    
    // First, try to find which page contains the viewport center
    int centerPage = pageAtPoint(viewCenter);
    if (centerPage >= 0) {
        m_currentPageIndex = centerPage;
    } else {
        // No page at center - find the closest page
        QVector<int> visible = visiblePages();
        if (!visible.isEmpty()) {
            // Use the first visible page
            m_currentPageIndex = visible.first();
        } else {
            // No visible pages - estimate based on scroll position
            // Find the page whose y-position is closest to pan offset
            qreal minDist = std::numeric_limits<qreal>::max();
            int closestPage = 0;
            
            for (int i = 0; i < m_document->pageCount(); ++i) {
                QRectF rect = pageRect(i);
                qreal dist = qAbs(rect.top() - m_panOffset.y());
                if (dist < minDist) {
                    minDist = dist;
                    closestPage = i;
                }
            }
            m_currentPageIndex = closestPage;
        }
    }
    
    if (m_currentPageIndex != oldIndex) {
        emit currentPageChanged(m_currentPageIndex);
        // Undo/redo availability may change when page changes
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
    }
}

void DocumentViewport::emitScrollFractions()
{
    if (!m_document || m_document->pageCount() == 0) {
        emit horizontalScrollChanged(0.0);
        emit verticalScrollChanged(0.0);
        return;
    }
    
    QSizeF contentSize = totalContentSize();
    qreal viewportWidth = width() / m_zoomLevel;
    qreal viewportHeight = height() / m_zoomLevel;
    
    // Calculate horizontal scroll fraction
    qreal scrollableWidth = contentSize.width() - viewportWidth;
    qreal hFraction = 0.0;
    if (scrollableWidth > 0) {
        hFraction = qBound(0.0, m_panOffset.x() / scrollableWidth, 1.0);
    }
    
    // Calculate vertical scroll fraction
    qreal scrollableHeight = contentSize.height() - viewportHeight;
    qreal vFraction = 0.0;
    if (scrollableHeight > 0) {
        vFraction = qBound(0.0, m_panOffset.y() / scrollableHeight, 1.0);
    }
    
    emit horizontalScrollChanged(hFraction);
    emit verticalScrollChanged(vFraction);
}
