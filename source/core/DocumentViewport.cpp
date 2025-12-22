// ============================================================================
// DocumentViewport - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.1)
// ============================================================================

#include "DocumentViewport.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QtMath>  // For qPow
#include <limits>

// ===== Constructor & Destructor =====

DocumentViewport::DocumentViewport(QWidget* parent)
    : QWidget(parent)
{
    // Enable mouse tracking for hover effects (future)
    setMouseTracking(true);
    
    // Accept tablet events
    setAttribute(Qt::WA_TabletTracking, true);
    
    // Set focus policy for keyboard shortcuts
    setFocusPolicy(Qt::StrongFocus);
    
    // Set background color (will be painted over by pages)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(64, 64, 64));  // Dark gray background
    setPalette(pal);
}

DocumentViewport::~DocumentViewport()
{
    // Document is not owned, so nothing to delete
}

// ===== Document Management =====

void DocumentViewport::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    m_document = doc;
    
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

// ===== View State Setters =====

void DocumentViewport::setZoomLevel(qreal zoom)
{
    // Clamp to valid range
    zoom = qBound(MIN_ZOOM, zoom, MAX_ZOOM);
    
    if (qFuzzyCompare(m_zoomLevel, zoom)) {
        return;
    }
    
    m_zoomLevel = zoom;
    
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
}

void DocumentViewport::scrollToPage(int pageIndex)
{
    if (!m_document) return;
    
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
    // TODO (Task 1.3.10): Implement based on total content size
    Q_UNUSED(fraction);
}

void DocumentViewport::setVerticalScrollFraction(qreal fraction)
{
    // TODO (Task 1.3.10): Implement based on total content size
    Q_UNUSED(fraction);
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
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Fill background (visible in gaps between pages)
    painter.fillRect(rect(), QColor(64, 64, 64));
    
    if (!m_document) {
        // No document - draw placeholder
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, 
                         tr("No document loaded"));
        return;
    }
    
    // Get visible pages to render
    QVector<int> visible = visiblePages();
    
    // Apply view transform
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // Render each visible page
    for (int pageIdx : visible) {
        Page* page = m_document->page(pageIdx);
        if (!page) continue;
        
        QPointF pos = pagePosition(pageIdx);
        
        painter.save();
        painter.translate(pos);
        
        // Render the page (background + content)
        renderPage(painter, page, pageIdx);
        
        painter.restore();
    }
    
    painter.restore();
    
    // Draw debug info overlay if enabled
    if (m_showDebugOverlay) {
        painter.setPen(Qt::white);
        QFont smallFont = painter.font();
        smallFont.setPointSize(10);
        painter.setFont(smallFont);
        
        QSizeF contentSize = totalContentSize();
        QString info = QString("Document: %1 | Pages: %2 | Current: %3\n"
                               "Zoom: %4% | Pan: (%5, %6)\n"
                               "Layout: %7 | Content: %8x%9\n"
                               "Visible pages: %10")
            .arg(m_document->displayName())
            .arg(m_document->pageCount())
            .arg(m_currentPageIndex + 1)
            .arg(m_zoomLevel * 100, 0, 'f', 0)
            .arg(m_panOffset.x(), 0, 'f', 1)
            .arg(m_panOffset.y(), 0, 'f', 1)
            .arg(m_layoutMode == LayoutMode::SingleColumn ? "Single Column" : "Two Column")
            .arg(contentSize.width(), 0, 'f', 0)
            .arg(contentSize.height(), 0, 'f', 0)
            .arg(visible.size());
        
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
    
    // TODO (Task 1.3.9): Maintain view position on resize
    // For now, just clamp and update
    clampPanOffset();
    update();
    emitScrollFractions();
}

void DocumentViewport::mousePressEvent(QMouseEvent* event)
{
    // TODO (Task 1.3.8): Route to correct page
    Q_UNUSED(event);
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event)
{
    // TODO (Task 1.3.8): Route to correct page
    Q_UNUSED(event);
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event)
{
    // TODO (Task 1.3.8): Route to correct page
    Q_UNUSED(event);
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
        qreal scrollSpeed = 40.0;
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

void DocumentViewport::tabletEvent(QTabletEvent* event)
{
    // TODO (Task 1.3.8): Route to correct page
    Q_UNUSED(event);
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

// ===== Rendering Helpers (Task 1.3.3) =====

void DocumentViewport::renderPage(QPainter& painter, Page* page, int pageIndex)
{
    if (!page) return;
    
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
            // Render PDF page
            if (m_document->isPdfLoaded() && page->pdfPageNumber >= 0) {
                // TODO (Task 1.3.6): Use PDF cache instead of live rendering
                // For now, render live at effective DPI
                qreal dpi = effectivePdfDpi();
                QImage pdfImage = m_document->renderPdfPageToImage(page->pdfPageNumber, dpi);
                
                if (!pdfImage.isNull()) {
                    // Scale image to fit page rect
                    // The PDF was rendered at effective DPI, we need to scale to page size
                    painter.drawImage(pageRect, pdfImage);
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
    
    // 3. Render vector layers and objects via Page::render()
    // Note: Pass zoom=1.0 since we're already applying zoom via painter transform
    // Pass nullptr for PDF background since we've already rendered it
    page->render(painter, nullptr, 1.0);
    
    // 4. Draw page border (optional, for visual separation)
    painter.setPen(QPen(QColor(180, 180, 180), 1.0 / m_zoomLevel));
    painter.drawRect(pageRect);
}

qreal DocumentViewport::effectivePdfDpi() const
{
    // Base DPI for 100% zoom
    constexpr qreal baseDpi = 96.0;
    
    // Scale DPI with zoom level for crisp rendering
    // At zoom > 1.0, we want higher DPI to avoid pixelation
    // At zoom < 1.0, we can use lower DPI to save memory/time
    // 
    // However, for very high zoom, cap the DPI to avoid excessive memory usage
    qreal scaledDpi = baseDpi * m_zoomLevel;
    
    // Cap at reasonable maximum (300 DPI is print quality)
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
    }
}

void DocumentViewport::emitScrollFractions()
{
    // TODO (Task 1.3.10): Calculate based on content size
    // For now, emit 0
    emit horizontalScrollChanged(0.0);
    emit verticalScrollChanged(0.0);
}
