// ============================================================================
// DocumentViewport - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.1)
// ============================================================================

#include "DocumentViewport.h"
#include "../layers/VectorLayer.h"
#include "../viewport/PageWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QtMath>     // For qPow
#include <algorithm>  // For std::remove_if
#include <limits>
#include <QDateTime>  // For timestamp
#include <QUuid>      // For stroke IDs
#include <QSet>       // For efficient ID lookup in eraseAt
#include <QDebug>     // For architecture toggle debug output

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
    
    // Benchmark display timer - updates debug overlay periodically when benchmarking
    connect(&m_benchmarkDisplayTimer, &QTimer::timeout, this, [this]() {
        if (m_benchmarking && m_showDebugOverlay) {
            // Only update the debug overlay region (top-left corner)
            update(QRect(0, 0, 500, 100));
        }
    });
}

DocumentViewport::~DocumentViewport()
{
    // Document is not owned, so nothing to delete
    
    // Clean up PageWidgets (new architecture)
    destroyAllPageWidgets();
}

// ===== Document Management =====

void DocumentViewport::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    m_document = doc;
    
    // Invalidate PDF cache for new document (Task 1.3.6)
    invalidatePdfCache();
    
    // Destroy existing PageWidgets (new architecture)
    destroyAllPageWidgets();
    
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
    
    // Rebuild PageWidgets if new architecture is enabled
    if (m_useNewArchitecture && m_document) {
        syncPageWidgets();
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

// ===== Tool State Management (Task 2.1) =====

void DocumentViewport::setCurrentTool(ToolType tool)
{
    if (m_currentTool == tool) {
        return;
    }
    
    m_currentTool = tool;
    
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

// ===== View State Setters =====

void DocumentViewport::setZoomLevel(qreal zoom)
{
    // Clamp to valid range
    zoom = qBound(MIN_ZOOM, zoom, MAX_ZOOM);
    
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
    
    // NEW ARCHITECTURE: Update PageWidgets with new zoom
    if (m_useNewArchitecture) {
        for (PageWidget* pw : m_pageWidgets) {
            pw->setZoom(m_zoomLevel);
            pw->invalidateBackgroundCache();
        }
        // Re-provide PDF pixmaps at new DPI
        for (auto it = m_pageIndexToWidget.begin(); it != m_pageIndexToWidget.end(); ++it) {
            providePdfToPageWidget(it.key(), it.value());
        }
        updatePageWidgetPositions();
    }
    
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
    
    // NEW ARCHITECTURE: Update PageWidget positions and sync visible pages
    if (m_useNewArchitecture) {
        syncPageWidgets();  // Creates/destroys PageWidgets as pages scroll in/out
    }
    
    update();
    emit panChanged(m_panOffset);
    emitScrollFractions();
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
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn: {
            // Pages stacked vertically, centered horizontally
            qreal y = 0;
            for (int i = 0; i < pageIndex; ++i) {
                const Page* page = m_document->page(i);
                if (page) {
                    y += page->size.height() + m_pageGap;
                }
            }
            
            // Center horizontally within the widest page
            // For now, just use x = 0 (will center in rendering)
            return QPointF(0, y);
        }
        
        case LayoutMode::TwoColumn: {
            // Pages arranged in pairs: (0,1), (2,3), (4,5), ...
            int row = pageIndex / 2;
            int col = pageIndex % 2;
            
            qreal y = 0;
            for (int r = 0; r < row; ++r) {
                // Get the height of the row (max of two pages)
                qreal rowHeight = 0;
                int leftIdx = r * 2;
                int rightIdx = r * 2 + 1;
                
                const Page* leftPage = m_document->page(leftIdx);
                if (leftPage) {
                    rowHeight = qMax(rowHeight, leftPage->size.height());
                }
                
                const Page* rightPage = m_document->page(rightIdx);
                if (rightPage) {
                    rowHeight = qMax(rowHeight, rightPage->size.height());
                }
                
                y += rowHeight + m_pageGap;
            }
            
            qreal x = 0;
            if (col == 1) {
                // Right column - offset by left page width + gap
                int leftIdx = row * 2;
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
        // Check if within page bounds (for edgeless, always return 0 if within bounds)
        const Page* page = m_document->edgelessPage();
        if (page) {
            // Edgeless pages can extend beyond their nominal size
            // For now, any point returns page 0
            return 0;
        }
        return -1;
    }
    
    // Check each page's rect
    for (int i = 0; i < m_document->pageCount(); ++i) {
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
    
    QRectF viewRect = visibleRect();
    
    // Check each page for intersection with visible rect
    for (int i = 0; i < m_document->pageCount(); ++i) {
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
    
    // ========== NEW ARCHITECTURE: PageWidgets handle their own rendering ==========
    // When using new architecture, DocumentViewport only paints:
    // - The gray background (already done above)
    // - The eraser cursor
    // - The debug overlay
    // PageWidgets paint themselves (background + layers)
    if (m_useNewArchitecture) {
        // NOTE: Do NOT call syncPageWidgets() here - it's called from setPanOffset/resize
        // Calling it here causes hundreds of redundant calls per second
        
        // Draw eraser cursor
        drawEraserCursor(painter);
        
        // Skip to debug overlay (drawn below)
        goto draw_debug_overlay;
    }
    
    // ========== OLD ARCHITECTURE: Direct rendering ==========
    {
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
            
            // Check if this page intersects the dirty region (optimization for partial updates)
            if (isPartialUpdate) {
                QPointF pos = pagePosition(pageIdx);
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
            
            QPointF pos = pagePosition(pageIdx);
            
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
        
        // Draw eraser cursor (Task 2.4)
        // Skip during stroke drawing (partial updates for pen don't need eraser cursor)
        if (!m_isDrawing || !isPartialUpdate) {
            drawEraserCursor(painter);
        }
    }
    
draw_debug_overlay:
    
    // Draw debug info overlay if enabled
    // Skip during partial updates unless the dirty region includes the overlay area
    QRect debugOverlayArea(0, 0, 500, 120);
    if (m_showDebugOverlay && (!isPartialUpdate || dirtyRect.intersects(debugOverlayArea))) {
        painter.setPen(Qt::white);
        QFont smallFont = painter.font();
        smallFont.setPointSize(10);
        painter.setFont(smallFont);
        
        QSizeF contentSize = totalContentSize();
        
        // Tool name for debug display
        QString toolName;
        switch (m_currentTool) {
            case ToolType::Pen: toolName = "Pen"; break;
            case ToolType::Marker: toolName = "Marker"; break;
            case ToolType::Eraser: toolName = "Eraser"; break;
            case ToolType::Highlighter: toolName = "Highlighter"; break;
            case ToolType::Lasso: toolName = "Lasso"; break;
        }
        
        QString info = QString("Document: %1 | Pages: %2 | Current: %3\n"
                               "Zoom: %4% | Pan: (%5, %6)\n"
                               "Layout: %7 | Content: %8x%9\n"
                               "Tool: %10%11 | Undo:%12 Redo:%13\n"
                               "Arch: %14 | Paint Rate: %15 [P/E/B/T]")
            .arg(m_document->displayName())
            .arg(m_document->pageCount())
            .arg(m_currentPageIndex + 1)
            .arg(m_zoomLevel * 100, 0, 'f', 0)
            .arg(m_panOffset.x(), 0, 'f', 1)
            .arg(m_panOffset.y(), 0, 'f', 1)
            .arg(m_layoutMode == LayoutMode::SingleColumn ? "Single Column" : "Two Column")
            .arg(contentSize.width(), 0, 'f', 0)
            .arg(contentSize.height(), 0, 'f', 0)
            .arg(toolName)
            .arg(m_hardwareEraserActive ? " (HW Eraser)" : "")
            .arg(canUndo() ? "Y" : "N")
            .arg(canRedo() ? "Y" : "N")
            .arg(m_useNewArchitecture ? "NEW" : "OLD")
            .arg(m_benchmarking ? QString("%1 Hz").arg(getPaintRate()) : "OFF");
        
        // Draw with background for readability
        QRect textRect = painter.fontMetrics().boundingRect(
            rect().adjusted(10, 10, -10, -10), 
            Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, info);
        textRect.adjust(-5, -5, 5, 5);
        painter.fillRect(textRect, QColor(0, 0, 0, 180));
        painter.drawText(rect().adjusted(10, 10, -10, -10), 
                         Qt::AlignTop | Qt::AlignLeft, info);
    }
}

void DocumentViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
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
    
    // NEW ARCHITECTURE: Sync PageWidgets (visible pages may have changed)
    if (m_useNewArchitecture) {
        syncPageWidgets();
    }
    
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
    
    // Check for Ctrl modifier → Zoom
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom at cursor position
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
        
        // Calculate new zoom level
        // Use multiplicative zoom for consistent feel
        qreal zoomFactor = qPow(1.1, zoomDelta);  // 10% per step
        qreal newZoom = m_zoomLevel * zoomFactor;
        newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
        
        // Zoom towards cursor position
        zoomAtPoint(newZoom, event->position());
        
        event->accept();
        return;
    }
    
    // Scroll
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
        // Check for Shift modifier → Horizontal scroll
        if (event->modifiers() & Qt::ShiftModifier) {
            // Swap X and Y for horizontal scroll
            scrollDelta = QPointF(scrollDelta.y(), scrollDelta.x());
        }
        
        scrollBy(scrollDelta);
    }
    
    event->accept();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event)
{
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
            
        case Qt::Key_T:
            // T = Toggle new architecture (for testing)
            setUseNewArchitecture(!m_useNewArchitecture);
            qDebug() << "Architecture toggled:" << (m_useNewArchitecture ? "NEW (PageWidgets)" : "OLD (direct render)");
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

// ===== PDF Cache Helpers (Task 1.3.6) =====

QPixmap DocumentViewport::getCachedPdfPage(int pageIndex, qreal dpi)
{
    if (!m_document || !m_document->isPdfLoaded()) {
        return QPixmap();
    }
    
    // Check if we have this page cached at the right DPI
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(pageIndex, dpi)) {
            return entry.pixmap;
        }
    }
    
    // Not cached - render it now
    QImage pdfImage = m_document->renderPdfPageToImage(pageIndex, dpi);
    if (pdfImage.isNull()) {
        return QPixmap();
    }
    
    QPixmap pixmap = QPixmap::fromImage(pdfImage);
    
    // Add to cache
    PdfCacheEntry entry;
    entry.pageIndex = pageIndex;
    entry.dpi = dpi;
    entry.pixmap = pixmap;
    
    // If cache is full, remove the oldest entry
    if (m_pdfCache.size() >= m_pdfCacheCapacity) {
        m_pdfCache.removeFirst();
    }
    
    m_pdfCache.append(entry);
    m_cachedDpi = dpi;
    
    return pixmap;
}

void DocumentViewport::preloadPdfCache()
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
    
    // Pre-load pages (this also adds them to cache)
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        Page* page = m_document->page(i);
        if (page && page->backgroundType == Page::BackgroundType::PDF) {
            getCachedPdfPage(page->pdfPageNumber, dpi);
        }
    }
}

void DocumentViewport::invalidatePdfCache()
{
    m_pdfCache.clear();
    m_cachedDpi = 0;
}

void DocumentViewport::invalidatePdfCachePage(int pageIndex)
{
    // Remove all entries for this page
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
    // - Single column: cache 2 extra pages (1 above, 1 below visible)
    // - Two column: cache 4 extra pages (1 row above, 1 row below)
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn:
            m_pdfCacheCapacity = 4;  // Visible + 2 buffer
            break;
        case LayoutMode::TwoColumn:
            m_pdfCacheCapacity = 8;  // Visible + 4 buffer
            break;
    }
    
    // Trim cache if over capacity
    while (m_pdfCache.size() > m_pdfCacheCapacity) {
        m_pdfCache.removeFirst();
    }
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
    
    // Pre-load ±1 pages beyond visible
    int preloadStart = qMax(0, first - 1);
    int preloadEnd = qMin(m_document->pageCount() - 1, last + 1);
    
    // Get device pixel ratio for cache
    qreal dpr = devicePixelRatioF();
    
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
        // NEW ARCHITECTURE: Route stroke to PageWidget
        if (m_useNewArchitecture && m_activeDrawingPage >= 0) {
            PageWidget* pw = pageWidgetForPage(m_activeDrawingPage);
            if (pw) {
                qDebug() << "NEW ARCH: beginStroke on page" << m_activeDrawingPage;
                
                // Create initial stroke
                VectorStroke stroke;
                stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                stroke.color = m_penColor;
                stroke.baseThickness = m_penThickness;
                
                // Add first point
                StrokePoint sp;
                sp.pos = pe.pageHit.pagePoint;
                sp.pressure = pe.pressure;
                stroke.points.append(sp);
                stroke.updateBoundingBox();
                
                pw->beginStroke(stroke);
                m_isDrawing = true;
            } else {
                qDebug() << "NEW ARCH: No PageWidget for page" << m_activeDrawingPage;
            }
        } else {
            // OLD ARCHITECTURE: Handle stroke directly
            if (m_useNewArchitecture) {
                qDebug() << "NEW ARCH: Stroke NOT routed (activeDrawingPage=" << m_activeDrawingPage << ")";
            }
            startStroke(pe);
        }
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
    
    // Only process if we have an active drawing page
    if (m_activeDrawingPage < 0) {
        return;
    }
    
    // Handle tool-specific actions
    // Hardware eraser: use m_hardwareEraserActive because some tablets
    // don't consistently report pointerType() == Eraser in every move event
    bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
    
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
    } else if (m_isDrawing && (m_currentTool == ToolType::Pen || m_currentTool == ToolType::Marker)) {
        // NEW ARCHITECTURE: Route stroke update to PageWidget
        if (m_useNewArchitecture) {
            PageWidget* pw = pageWidgetForPage(m_activeDrawingPage);
            if (pw && pw->isDrawing()) {
                // Get the current stroke and add a point
                VectorStroke stroke = pw->currentStroke();
                
                // Add point with decimation
                QPointF pagePos = pe.pageHit.pagePoint;
                if (!stroke.points.isEmpty()) {
                    QPointF lastPos = stroke.points.last().pos;
                    qreal dx = pagePos.x() - lastPos.x();
                    qreal dy = pagePos.y() - lastPos.y();
                    if (dx * dx + dy * dy < MIN_DISTANCE_SQ) {
                        return;  // Skip - too close to last point
                    }
                }
                
                StrokePoint sp;
                sp.pos = pagePos;
                sp.pressure = pe.pressure;
                stroke.points.append(sp);
                stroke.updateBoundingBox();
                
                pw->updateStroke(stroke);
            }
        } else {
            // OLD ARCHITECTURE: Handle stroke directly
            continueStroke(pe);
        }
    }
}

void DocumentViewport::handlePointerRelease(const PointerEvent& pe)
{
    if (!m_document) return;
    
    Q_UNUSED(pe);
    
    // Finish stroke if we were drawing
    if (m_isDrawing) {
        // NEW ARCHITECTURE: Route stroke finish to PageWidget
        if (m_useNewArchitecture && m_activeDrawingPage >= 0) {
            PageWidget* pw = pageWidgetForPage(m_activeDrawingPage);
            if (pw && pw->isDrawing()) {
                pw->endStroke();
                
                // Push undo action (stroke was added to the layer by PageWidget)
                // Note: PageWidget emits strokeCompleted signal for this
            }
            m_isDrawing = false;
        } else {
            // OLD ARCHITECTURE: Handle stroke directly
            finishStroke();
        }
    }
    
    // Clear active state
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;  // Reset source
    m_activeDrawingPage = -1;
    m_hardwareEraserActive = false;  // Clear hardware eraser state
    // Note: Don't clear m_lastPointerPos - keep it for eraser cursor during hover
    
    // Pre-load caches after interaction
    preloadPdfCache();
    preloadStrokeCaches();
    
    update();
}

// ===== Stroke Drawing (Task 2.2) =====

void DocumentViewport::startStroke(const PointerEvent& pe)
{
    if (!m_document || !pe.pageHit.valid()) return;
    
    // Only drawing tools start strokes (Pen, Marker)
    if (m_currentTool != ToolType::Pen && m_currentTool != ToolType::Marker) {
        return;
    }
    
    m_isDrawing = true;
    m_activeDrawingPage = pe.pageHit.pageIndex;
    
    // Initialize new stroke
    m_currentStroke = VectorStroke();
    m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentStroke.color = m_penColor;
    m_currentStroke.baseThickness = m_penThickness;
    
    // Reset incremental rendering cache (Task 2.3)
    resetCurrentStrokeCache();
    
    // Add first point
    addPointToStroke(pe.pageHit.pagePoint, pe.pressure);
}

void DocumentViewport::continueStroke(const PointerEvent& pe)
{
    if (!m_isDrawing || m_activeDrawingPage < 0) return;
    
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
    
    addPointToStroke(pagePos, pe.pressure);
}

void DocumentViewport::finishStroke()
{
    if (!m_isDrawing) return;
    
    // Don't save empty strokes
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        return;
    }
    
    // Finalize stroke
    m_currentStroke.updateBoundingBox();
    
    // Add to page's active layer
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
    
    emit documentModified();
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
    if (n < 1 || m_activeDrawingPage < 0) return;
    
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
    
    // Render new segments to the cache (if any)
    if (n > m_lastRenderedPointIndex && n >= 2) {
        QPainter cachePainter(&m_currentStrokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Apply the same transform as paintEvent to convert page coords to viewport coords
        // The cache is in viewport coordinates (widget pixels)
        QPointF pagePos = pagePosition(m_activeDrawingPage);
        cachePainter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        cachePainter.scale(m_zoomLevel, m_zoomLevel);
        cachePainter.translate(pagePos);
        
        // Use line-based rendering for incremental updates (fast)
        QPen pen(m_currentStroke.color, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
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
            cachePainter.setBrush(m_currentStroke.color);
            cachePainter.drawEllipse(m_currentStroke.points[0].pos, startRadius, startRadius);
        }
        
        m_lastRenderedPointIndex = n;
    }
    
    // Blit the cached current stroke to the viewport
    // The painter here should be in raw viewport coordinates (no transform)
    painter.drawPixmap(0, 0, m_currentStrokeCache);
    
    // Draw end cap at current position (always needs updating as it moves)
    if (n >= 1) {
        // Apply transform to draw end cap at correct position
        painter.save();
        painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        painter.scale(m_zoomLevel, m_zoomLevel);
        painter.translate(pagePosition(m_activeDrawingPage));
        
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
    if (!m_document || !pe.pageHit.valid()) return;
    
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

void DocumentViewport::trimUndoStack(int pageIndex)
{
    // Limit stack size to prevent unbounded memory growth
    // Using a simple approach: if over limit, convert to list, remove front, convert back
    // This is O(n) but only runs when stack exceeds limit (rare)
    if (m_undoStacks[pageIndex].size() > MAX_UNDO_PER_PAGE) {
        QStack<PageUndoAction>& stack = m_undoStacks[pageIndex];
        
        // Convert to list for efficient front removal
        QList<PageUndoAction> list;
        list.reserve(stack.size());
        while (!stack.isEmpty()) {
            list.prepend(stack.pop());
        }
        
        // Remove oldest entries until within limit
        while (list.size() > MAX_UNDO_PER_PAGE) {
            list.removeFirst();
        }
        
        // Convert back to stack
        for (const auto& action : list) {
            stack.push(action);
        }
    }
}

void DocumentViewport::undo()
{
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
    return m_undoStacks.contains(m_currentPageIndex) && 
           !m_undoStacks[m_currentPageIndex].isEmpty();
}

bool DocumentViewport::canRedo() const
{
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
    
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer && layer->visible) {
            // Use zoom-aware cache for maximum performance
            // The painter is scaled by zoom, cache is at zoom * dpr resolution
            layer->renderWithZoomCache(painter, pageSize, m_zoomLevel, dpr);
        }
    }
    
    // 4. Render inserted objects (sorted by z-order)
    page->renderObjects(painter, 1.0);
    
    // 5. Draw page border (optional, for visual separation)
    // CUSTOMIZABLE: Page border color (theme setting)
    painter.setPen(QPen(QColor(180, 180, 180), 1.0 / m_zoomLevel));  // Light gray border
    painter.drawRect(pageRect);
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
    if (!m_document || m_document->pageCount() == 0) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    // For edgeless documents, allow unlimited pan (infinite canvas)
    if (m_document->isEdgeless()) {
        // No clamping for edgeless - user can pan anywhere
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

// ============================================================================
// NEW ARCHITECTURE: PageWidget Management (Viewport Reconstruction)
// ============================================================================

void DocumentViewport::setUseNewArchitecture(bool enable)
{
    if (m_useNewArchitecture == enable) {
        return;
    }
    
    m_useNewArchitecture = enable;
    
    if (enable && m_document) {
        // Switch to new architecture: create PageWidgets
        qDebug() << "NEW ARCHITECTURE: Creating PageWidgets for document with" 
                 << m_document->pageCount() << "pages";
        syncPageWidgets();
        qDebug() << "NEW ARCHITECTURE: Created" << m_pageWidgets.count() << "PageWidgets";
    } else {
        // Switch to old architecture: destroy PageWidgets
        qDebug() << "OLD ARCHITECTURE: Destroying" << m_pageWidgets.count() << "PageWidgets";
        destroyAllPageWidgets();
    }
    
    update();
}

void DocumentViewport::syncPageWidgets()
{
    if (!m_useNewArchitecture || !m_document) {
        return;
    }
    
    // Get currently visible pages
    QVector<int> visible = visiblePages();
    QSet<int> visibleSet(visible.begin(), visible.end());
    
    // Quick check: if visible pages haven't changed, just update positions
    QSet<int> currentSet;
    for (auto it = m_pageIndexToWidget.begin(); it != m_pageIndexToWidget.end(); ++it) {
        currentSet.insert(it.key());
    }
    
    if (visibleSet == currentSet) {
        // Same pages visible - just update positions (no widget create/destroy)
        updatePageWidgetPositions();
        return;
    }
    
    qDebug() << "syncPageWidgets: Visible pages changed:" << visible;
    
    // Find which PageWidgets to destroy (no longer visible)
    QVector<int> toDestroy;
    for (auto it = m_pageIndexToWidget.begin(); it != m_pageIndexToWidget.end(); ++it) {
        if (!visibleSet.contains(it.key())) {
            toDestroy.append(it.key());
        }
    }
    
    // Destroy PageWidgets for pages that scrolled out
    for (int pageIndex : toDestroy) {
        PageWidget* pw = m_pageIndexToWidget.take(pageIndex);
        m_pageWidgets.removeOne(pw);
        delete pw;
        qDebug() << "syncPageWidgets: Destroyed PageWidget for page" << pageIndex;
    }
    
    // Create PageWidgets for newly visible pages
    for (int pageIndex : visible) {
        if (!m_pageIndexToWidget.contains(pageIndex)) {
            Page* page = m_document->page(pageIndex);
            if (page) {
                PageWidget* pw = new PageWidget(this);
                pw->setPage(page);
                pw->setZoom(m_zoomLevel);
                
                // Set active layer from page
                pw->setActiveLayerIndex(page->activeLayerIndex);
                
                // Provide PDF pixmap if this is a PDF page
                providePdfToPageWidget(pageIndex, pw);
                
                pw->show();
                
                m_pageWidgets.append(pw);
                m_pageIndexToWidget.insert(pageIndex, pw);
                
                qDebug() << "syncPageWidgets: Created PageWidget for page" << pageIndex
                         << "at zoom" << m_zoomLevel;
            }
        }
    }
    
    // Update positions for all PageWidgets
    updatePageWidgetPositions();
    qDebug() << "syncPageWidgets: PageWidgets updated, count:" << m_pageWidgets.count();
}

PageWidget* DocumentViewport::pageWidgetForPage(int pageIndex) const
{
    return m_pageIndexToWidget.value(pageIndex, nullptr);
}

void DocumentViewport::updatePageWidgetPositions()
{
    if (!m_useNewArchitecture || !m_document) {
        return;
    }
    
    for (auto it = m_pageIndexToWidget.begin(); it != m_pageIndexToWidget.end(); ++it) {
        int pageIndex = it.key();
        PageWidget* pw = it.value();
        
        Page* page = m_document->page(pageIndex);
        if (!page) continue;
        
        // Calculate page position in viewport coordinates
        QPointF docPos = pagePosition(pageIndex);
        QPointF viewportPos = documentToViewport(docPos);
        
        // Calculate page size at current zoom
        QSizeF scaledSize(page->size.width() * m_zoomLevel,
                          page->size.height() * m_zoomLevel);
        
        // Set geometry
        pw->setGeometry(static_cast<int>(viewportPos.x()),
                        static_cast<int>(viewportPos.y()),
                        static_cast<int>(scaledSize.width()),
                        static_cast<int>(scaledSize.height()));
        
        // Update zoom (triggers cache rebuilds if needed)
        pw->setZoom(m_zoomLevel);
    }
}

void DocumentViewport::destroyAllPageWidgets()
{
    for (PageWidget* pw : m_pageWidgets) {
        delete pw;
    }
    m_pageWidgets.clear();
    m_pageIndexToWidget.clear();
}

void DocumentViewport::providePdfToPageWidget(int pageIndex, PageWidget* pageWidget)
{
    if (!m_document || !pageWidget) {
        return;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        return;
    }
    
    // Use existing PDF cache to get the pixmap
    if (page->pdfPageNumber >= 0 && m_document->isPdfLoaded()) {
        qreal dpi = effectivePdfDpi();
        QPixmap pdfPixmap = getCachedPdfPage(page->pdfPageNumber, dpi);
        pageWidget->setPdfPixmap(pdfPixmap);
    }
}
