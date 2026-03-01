#include "ColorPresetButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

// ============================================================================
// ColorPresetButton
// ============================================================================

ColorPresetButton::ColorPresetButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    // Set tooltip
    setToolTip(tr("Click to select, click again to edit"));
}

QColor ColorPresetButton::color() const
{
    return m_color;
}

void ColorPresetButton::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        update();
        emit colorChanged(m_color);
    }
}

bool ColorPresetButton::isSelected() const
{
    return m_selected;
}

void ColorPresetButton::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
        emit selectedChanged(m_selected);
    }
}

QSize ColorPresetButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize ColorPresetButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void ColorPresetButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Border (only when selected)
    if (m_selected) {
        const qreal bw = BORDER_WIDTH_SELECTED / 2.0;
        QRectF outerRect = QRectF(rect()).adjusted(bw, bw, -bw, -bw);
        QPen borderPen(borderColor());
        borderPen.setWidthF(BORDER_WIDTH_SELECTED);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(outerRect, BORDER_RADIUS, BORDER_RADIUS);
    }

    // Color fill (inset more when selected to leave room for border)
    const qreal fillInset = m_selected ? (BORDER_WIDTH_SELECTED + 1.0) : 1.0;
    QRectF fillRect = QRectF(rect()).adjusted(fillInset, fillInset, -fillInset, -fillInset);
    const qreal fillRadius = fillRect.width() / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(adjustedFillColor());
    painter.drawRoundedRect(fillRect, fillRadius, fillRadius);
}

void ColorPresetButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void ColorPresetButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        update();
        
        // Check if release is within button bounds
        if (rect().contains(event->pos())) {
            // Capture selection state BEFORE clicked() might change it via signal handler
            bool wasSelected = m_selected;
            
            emit clicked();
            
            // If was already selected BEFORE this click, emit edit request
            // This ensures clicking an unselected button only selects it (no dialog)
            if (wasSelected) {
                emit editRequested();
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ColorPresetButton::enterEvent(QEnterEvent* event)
#else
void ColorPresetButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ColorPresetButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

QColor ColorPresetButton::borderColor() const
{
    if (m_selected) {
        return m_color;
    } else {
        return Qt::transparent;
    }
}

QColor ColorPresetButton::adjustedFillColor() const
{
    QColor fill = m_color;
    
    if (m_pressed) {
        // Darken when pressed
        fill = fill.darker(120);
    } else if (m_hovered && !m_selected) {
        // Slightly brighten on hover (only if not selected)
        fill = fill.lighter(110);
    }
    
    return fill;
}

