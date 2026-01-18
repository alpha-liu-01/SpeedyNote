#include "LayerPanelPillButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>
#include <QFont>

LayerPanelPillButton::LayerPanelPillButton(const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
{
    setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void LayerPanelPillButton::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        update();
    }
}

QString LayerPanelPillButton::text() const
{
    return m_text;
}

void LayerPanelPillButton::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        update();
    }
}

bool LayerPanelPillButton::isEnabled() const
{
    return m_enabled;
}

void LayerPanelPillButton::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        
        // Update cursor based on enabled state
        setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        
        // Cancel any ongoing press if disabled
        if (!enabled) {
            m_pressed = false;
            m_hovered = false;
        }
        
        update();
    }
}

QSize LayerPanelPillButton::sizeHint() const
{
    return QSize(BUTTON_WIDTH, BUTTON_HEIGHT);
}

QSize LayerPanelPillButton::minimumSizeHint() const
{
    return QSize(BUTTON_WIDTH, BUTTON_HEIGHT);
}

void LayerPanelPillButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background pill shape
    QColor bgColor = backgroundColor();
    
    // Apply press/hover effects (only if enabled)
    if (m_enabled) {
        if (m_pressed) {
            bgColor = bgColor.darker(120);
        } else if (m_hovered) {
            bgColor = bgColor.lighter(110);
        }
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect(), CORNER_RADIUS, CORNER_RADIUS);
    
    // Draw text centered
    QColor txtColor = textColor();
    
    // Dim text when disabled
    if (!m_enabled) {
        txtColor.setAlpha(128);
    }
    
    painter.setPen(txtColor);
    
    QFont font = painter.font();
    font.setPointSize(10);
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

void LayerPanelPillButton::mousePressEvent(QMouseEvent* event)
{
    if (m_enabled && event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void LayerPanelPillButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        // Check if release is within button bounds and button is enabled
        if (m_enabled && rect().contains(event->pos())) {
            emit clicked();
        }
        
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void LayerPanelPillButton::enterEvent(QEnterEvent* event)
{
    if (m_enabled) {
        m_hovered = true;
        update();
    }
    QWidget::enterEvent(event);
}

void LayerPanelPillButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

bool LayerPanelPillButton::isDarkMode() const
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

QColor LayerPanelPillButton::backgroundColor() const
{
    if (!m_enabled) {
        // Disabled: more muted colors
        if (isDarkMode()) {
            return QColor(45, 45, 45);
        } else {
            return QColor(200, 200, 200);
        }
    }
    
    // Enabled: neutral background (same as ActionBarButton)
    if (isDarkMode()) {
        return QColor(60, 60, 60);
    } else {
        return QColor(220, 220, 220);
    }
}

QColor LayerPanelPillButton::textColor() const
{
    if (isDarkMode()) {
        return QColor(240, 240, 240);
    } else {
        return QColor(30, 30, 30);
    }
}
