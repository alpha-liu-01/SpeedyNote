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
    
    // TODO (Task 1.3.2): Calculate page position and scroll to it
    // For now, just update the current page index
    m_currentPageIndex = pageIndex;
    emit currentPageChanged(m_currentPageIndex);
    
    update();
}

void DocumentViewport::scrollBy(QPointF delta)
{
    setPanOffset(m_panOffset + delta);
}

void DocumentViewport::zoomToFit()
{
    // TODO (Task 1.3.4): Calculate zoom to fit entire document
    // For now, just reset to 1.0
    setZoomLevel(1.0);
}

void DocumentViewport::zoomToWidth()
{
    // TODO (Task 1.3.4): Calculate zoom to fit page width
    // For now, just reset to 1.0
    setZoomLevel(1.0);
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

// ===== Qt Event Overrides =====

void DocumentViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    if (!m_document) {
        // No document - draw placeholder
        painter.fillRect(rect(), QColor(64, 64, 64));
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, 
                         tr("No document loaded"));
        return;
    }
    
    // TODO (Task 1.3.3): Implement full rendering
    // For now, draw a simple placeholder showing document info
    
    painter.fillRect(rect(), QColor(64, 64, 64));
    painter.setPen(Qt::white);
    
    QString info = QString("Document: %1\n"
                           "Pages: %2\n"
                           "Current Page: %3\n"
                           "Zoom: %4%\n"
                           "Pan: (%5, %6)\n"
                           "Layout: %7")
        .arg(m_document->displayName())
        .arg(m_document->pageCount())
        .arg(m_currentPageIndex + 1)
        .arg(m_zoomLevel * 100, 0, 'f', 0)
        .arg(m_panOffset.x(), 0, 'f', 1)
        .arg(m_panOffset.y(), 0, 'f', 1)
        .arg(m_layoutMode == LayoutMode::SingleColumn ? "Single Column" : "Two Column");
    
    painter.drawText(rect().adjusted(20, 20, -20, -20), 
                     Qt::AlignTop | Qt::AlignLeft, info);
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
    // TODO (Task 1.3.4): Implement scroll/zoom with wheel
    Q_UNUSED(event);
}

void DocumentViewport::tabletEvent(QTabletEvent* event)
{
    // TODO (Task 1.3.8): Route to correct page
    Q_UNUSED(event);
}

// ===== Private Methods =====

void DocumentViewport::clampPanOffset()
{
    // TODO (Task 1.3.4): Clamp based on content size and viewport
    // For now, allow overscroll with simple limits
    if (!m_document || m_document->pageCount() == 0) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    // Basic clamping - will be refined in Task 1.3.4
    // Allow some negative pan (overscroll at start)
    qreal minX = -width() * 0.5 / m_zoomLevel;
    qreal minY = -height() * 0.5 / m_zoomLevel;
    
    // Allow scrolling to end of content plus overscroll
    // For now, use a simple estimate
    qreal maxX = 2000;  // Placeholder
    qreal maxY = m_document->pageCount() * 1200;  // Rough estimate
    
    m_panOffset.setX(qBound(minX, m_panOffset.x(), maxX));
    m_panOffset.setY(qBound(minY, m_panOffset.y(), maxY));
}

void DocumentViewport::updateCurrentPageIndex()
{
    // TODO (Task 1.3.4): Calculate based on visible area
    // For now, estimate based on pan offset
    if (!m_document || m_document->pageCount() == 0) {
        m_currentPageIndex = 0;
        return;
    }
    
    // Rough estimate: assume pages are ~1000 pixels tall
    int estimatedPage = static_cast<int>(m_panOffset.y() / 1000);
    int oldIndex = m_currentPageIndex;
    m_currentPageIndex = qBound(0, estimatedPage, m_document->pageCount() - 1);
    
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
