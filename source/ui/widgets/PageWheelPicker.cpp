#include "PageWheelPicker.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>
#include <QApplication>
#include <QTimer>
#include <QPropertyAnimation>
#include <QFont>
#include <cmath>

// ============================================================================
// PageWheelPicker
// ============================================================================

PageWheelPicker::PageWheelPicker(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(WIDGET_WIDTH, WIDGET_HEIGHT);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    
    // Inertia timer for smooth scrolling
    m_inertiaTimer = new QTimer(this);
    m_inertiaTimer->setInterval(INERTIA_INTERVAL_MS);
    connect(m_inertiaTimer, &QTimer::timeout, this, &PageWheelPicker::onInertiaTimer);
    
    // Snap animation
    m_snapAnimation = new QPropertyAnimation(this, "scrollOffsetProperty", this);
    m_snapAnimation->setDuration(SNAP_DURATION_MS);
    m_snapAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_snapAnimation, &QPropertyAnimation::finished, this, &PageWheelPicker::onSnapFinished);
    
    setToolTip(tr("Drag to scroll through pages"));
}

PageWheelPicker::~PageWheelPicker()
{
    // Timers and animations are parented, will be deleted automatically
}

int PageWheelPicker::currentPage() const
{
    return m_currentPage;
}

void PageWheelPicker::setCurrentPage(int page)
{
    // Clamp to valid range
    page = qBound(0, page, qMax(0, m_pageCount - 1));
    
    if (m_currentPage != page) {
        m_currentPage = page;
        m_scrollOffset = static_cast<qreal>(page);
        update();
        emit currentPageChanged(m_currentPage);
    }
}

int PageWheelPicker::pageCount() const
{
    return m_pageCount;
}

void PageWheelPicker::setPageCount(int count)
{
    count = qMax(1, count);
    
    if (m_pageCount != count) {
        m_pageCount = count;
        
        // Clamp current page if necessary
        if (m_currentPage >= m_pageCount) {
            setCurrentPage(m_pageCount - 1);
        }
        
        update();
        emit pageCountChanged(m_pageCount);
    }
}

void PageWheelPicker::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        update();
    }
}

QSize PageWheelPicker::sizeHint() const
{
    return QSize(WIDGET_WIDTH, WIDGET_HEIGHT);
}

QSize PageWheelPicker::minimumSizeHint() const
{
    return QSize(WIDGET_WIDTH, WIDGET_HEIGHT);
}

void PageWheelPicker::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    
    // Draw background (rounded rectangle - "hotdog" shape)
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundColor());
    painter.drawRoundedRect(rect(), BORDER_RADIUS, BORDER_RADIUS);
    
    // Set clipping to rounded rect
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect(), BORDER_RADIUS, BORDER_RADIUS);
    painter.setClipPath(clipPath);
    
    // Calculate which pages to display
    // The center of the widget shows the page at m_scrollOffset
    // We display pages centered around m_scrollOffset
    
    const qreal centerY = height() / 2.0;
    const qreal fractionalOffset = m_scrollOffset - std::floor(m_scrollOffset);
    const int basePageIndex = static_cast<int>(std::floor(m_scrollOffset));
    
    // Draw pages: one before center, center, one after center
    // Plus extra for smooth scrolling at edges
    for (int i = -2; i <= 2; ++i) {
        const int pageIndex = basePageIndex + i;
        
        // Skip invalid pages
        if (pageIndex < 0 || pageIndex >= m_pageCount) {
            continue;
        }
        
        // Calculate Y position for this page number
        // i=0 is the base page at fractionalOffset from center
        // When fractionalOffset=0, page at basePageIndex is exactly centered
        const qreal yOffset = (i - fractionalOffset) * ROW_HEIGHT;
        const qreal y = centerY + yOffset;
        
        // Calculate distance from center (0 = center, 1 = one row away)
        const qreal distanceFromCenter = std::abs(i - fractionalOffset);
        
        // Determine if this is the "current" page (closest to center)
        const bool isCenterPage = (distanceFromCenter < 0.5);
        
        // Set font based on whether this is the center page
        QFont font = painter.font();
        if (isCenterPage) {
            font.setPixelSize(CENTER_FONT_SIZE);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(centerTextColor());
        } else {
            font.setPixelSize(ADJACENT_FONT_SIZE);
            font.setBold(false);
            painter.setFont(font);
            
            // Calculate opacity based on distance (fade out further pages)
            QColor textColor = adjacentTextColor();
            const qreal opacity = qMax(0.0, 1.0 - distanceFromCenter * 0.5) * ADJACENT_OPACITY;
            textColor.setAlphaF(opacity);
            painter.setPen(textColor);
        }
        
        // Draw page number (1-based display)
        const QString pageText = QString::number(pageIndex + 1);
        const QRectF textRect(0, y - ROW_HEIGHT / 2.0, width(), ROW_HEIGHT);
        painter.drawText(textRect, Qt::AlignCenter, pageText);
    }
}

void PageWheelPicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Stop any ongoing animations
        stopInertia();
        m_snapAnimation->stop();
        
        m_dragging = true;
        m_lastPos = event->position();
        m_velocity = 0.0;
        m_velocityTimer.start();
    }
    QWidget::mousePressEvent(event);
}

void PageWheelPicker::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        const QPointF currentPos = event->position();
        const qreal deltaY = currentPos.y() - m_lastPos.y();
        
        // Convert pixels to page offset (negative because drag down = previous pages)
        const qreal pagesDelta = -deltaY / ROW_HEIGHT;
        
        // Calculate velocity (pages per second)
        const qint64 elapsed = m_velocityTimer.elapsed();
        if (elapsed > 0) {
            const qreal instantVelocity = pagesDelta / (elapsed / 1000.0);
            // Smooth velocity with exponential moving average
            m_velocity = 0.3 * instantVelocity + 0.7 * m_velocity;
        }
        
        // Update scroll offset
        m_scrollOffset += pagesDelta;
        clampOffset();
        
        // Update current page during scroll
        updateFromOffset();
        
        m_lastPos = currentPos;
        m_velocityTimer.restart();
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PageWheelPicker::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        
        // Check if we have significant velocity for inertia
        if (std::abs(m_velocity) > SNAP_THRESHOLD) {
            startInertia();
        } else {
            snapToPage();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void PageWheelPicker::wheelEvent(QWheelEvent* event)
{
    // Stop any ongoing animations
    stopInertia();
    m_snapAnimation->stop();
    
    // Calculate scroll direction (typically 120 units per notch)
    // We move by exactly 1 page per wheel notch to avoid rounding issues
    // (0.5 would land exactly on rounding boundary and cause directional bias)
    const int notches = event->angleDelta().y() / 120;
    
    if (notches != 0) {
        // Move by whole pages: negative angleDelta = scroll down = increase page
        const int newPage = qBound(0, m_currentPage - notches, m_pageCount - 1);
        
        if (newPage != m_currentPage) {
            m_currentPage = newPage;
            m_scrollOffset = static_cast<qreal>(newPage);
            update();
            emit currentPageChanged(m_currentPage);
        }
    }
    
    event->accept();
}

void PageWheelPicker::onInertiaTimer()
{
    // Apply deceleration
    m_velocity *= DECELERATION;
    
    // Update scroll offset
    const qreal deltaOffset = m_velocity * (INERTIA_INTERVAL_MS / 1000.0);
    m_scrollOffset += deltaOffset;
    clampOffset();
    
    // Update current page during inertia
    updateFromOffset();
    update();
    
    // Check if velocity is low enough to snap
    if (std::abs(m_velocity) < SNAP_THRESHOLD) {
        stopInertia();
        snapToPage();
    }
}

void PageWheelPicker::onSnapFinished()
{
    // Final update to ensure we're exactly on a page
    m_scrollOffset = std::round(m_scrollOffset);
    clampOffset();
    updateFromOffset();
    update();
}

bool PageWheelPicker::isDarkMode() const
{
    // Use cached value if set, otherwise detect from palette
    if (m_darkMode) {
        return true;
    }
    
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

QColor PageWheelPicker::backgroundColor() const
{
    // Same as ActionBarButton
    if (isDarkMode()) {
        return QColor(60, 60, 60);
    } else {
        return QColor(220, 220, 220);
    }
}

QColor PageWheelPicker::centerTextColor() const
{
    // Full opacity text color
    if (isDarkMode()) {
        return Qt::white;
    } else {
        return Qt::black;
    }
}

QColor PageWheelPicker::adjacentTextColor() const
{
    // Base color for adjacent pages (opacity will be adjusted in paint)
    if (isDarkMode()) {
        return QColor(200, 200, 200);
    } else {
        return QColor(80, 80, 80);
    }
}

void PageWheelPicker::startInertia()
{
    m_inertiaTimer->start();
}

void PageWheelPicker::stopInertia()
{
    m_inertiaTimer->stop();
    m_velocity = 0.0;
}

void PageWheelPicker::snapToPage()
{
    // Calculate target page (nearest whole number)
    const qreal targetOffset = std::round(m_scrollOffset);
    
    // Only animate if we're not already at target
    if (std::abs(m_scrollOffset - targetOffset) > 0.01) {
        m_snapAnimation->setStartValue(m_scrollOffset);
        m_snapAnimation->setEndValue(targetOffset);
        m_snapAnimation->start();
    } else {
        // Already close enough, just set directly
        m_scrollOffset = targetOffset;
        updateFromOffset();
        update();
    }
}

void PageWheelPicker::updateFromOffset()
{
    // Calculate the page index closest to center
    const int newPage = qBound(0, static_cast<int>(std::round(m_scrollOffset)), m_pageCount - 1);
    
    if (newPage != m_currentPage) {
        m_currentPage = newPage;
        emit currentPageChanged(m_currentPage);
    }
}

void PageWheelPicker::clampOffset()
{
    // Allow slight overscroll for visual feedback, but clamp to valid range
    const qreal minOffset = -0.3;  // Slight overscroll at start
    const qreal maxOffset = m_pageCount - 1 + 0.3;  // Slight overscroll at end
    
    m_scrollOffset = qBound(minOffset, m_scrollOffset, maxOffset);
}

qreal PageWheelPicker::scrollOffset() const
{
    return m_scrollOffset;
}

void PageWheelPicker::setScrollOffset(qreal offset)
{
    if (!qFuzzyCompare(m_scrollOffset, offset)) {
        m_scrollOffset = offset;
        clampOffset();
        updateFromOffset();
        update();
    }
}

