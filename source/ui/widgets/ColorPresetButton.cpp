#include "ColorPresetButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>

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
    
    const qreal radius = BUTTON_SIZE / 2.0;
    const QPointF center(radius, radius);
    
    // Determine border width based on selection state
    const int borderWidth = m_selected ? BORDER_WIDTH_SELECTED : BORDER_WIDTH_NORMAL;
    
    // Draw border circle
    QPen borderPen(borderColor());
    borderPen.setWidth(borderWidth);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    
    const qreal borderOffset = borderWidth / 2.0;
    painter.drawEllipse(QRectF(borderOffset, borderOffset, 
                               BUTTON_SIZE - borderWidth, 
                               BUTTON_SIZE - borderWidth));
    
    // Draw filled color circle (slightly inset from border)
    const qreal fillInset = borderWidth + 1.0;
    const qreal fillRadius = radius - fillInset;
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(adjustedFillColor());
    painter.drawEllipse(center, fillRadius, fillRadius);
    
    // Optional: Draw subtle inner shadow for depth
    if (!m_pressed) {
        QRadialGradient shadowGradient(center, fillRadius);
        shadowGradient.setColorAt(0.0, Qt::transparent);
        shadowGradient.setColorAt(0.85, Qt::transparent);
        shadowGradient.setColorAt(1.0, QColor(0, 0, 0, 30));
        painter.setBrush(shadowGradient);
        painter.drawEllipse(center, fillRadius, fillRadius);
    }
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

bool ColorPresetButton::isDarkMode() const
{
    // Detect dark mode by checking the window background luminance
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance (simplified)
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

QColor ColorPresetButton::borderColor() const
{
    if (m_selected) {
        // Selected: high contrast border (white in dark mode, black in light mode)
        return isDarkMode() ? Qt::white : Qt::black;
    } else {
        // Unselected: subtle neutral border
        return isDarkMode() ? QColor(100, 100, 100) : QColor(180, 180, 180);
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

