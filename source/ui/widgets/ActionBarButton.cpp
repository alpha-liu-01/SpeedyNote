#include "ActionBarButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>

ActionBarButton::ActionBarButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void ActionBarButton::setIcon(const QIcon& icon)
{
    m_icon = icon;
    m_iconBaseName.clear();  // Clear base name since we're using a direct icon
    update();
}

QIcon ActionBarButton::icon() const
{
    return m_icon;
}

void ActionBarButton::setIconName(const QString& baseName)
{
    m_iconBaseName = baseName;
    m_text.clear();  // Clear text when setting icon
    updateIcon();
}

void ActionBarButton::setText(const QString& text)
{
    m_text = text;
    // Clear icon when setting text
    if (!text.isEmpty()) {
        m_icon = QIcon();
        m_iconBaseName.clear();
    }
    update();
}

QString ActionBarButton::text() const
{
    return m_text;
}

void ActionBarButton::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcon();
    }
}

bool ActionBarButton::isEnabled() const
{
    return m_enabled;
}

void ActionBarButton::setEnabled(bool enabled)
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

QSize ActionBarButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize ActionBarButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void ActionBarButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background circle
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
    painter.drawEllipse(rect());
    
    // Draw icon or text centered
    if (!m_text.isEmpty()) {
        // Draw text instead of icon
        QColor textColor;
        if (!m_enabled) {
            textColor = isDarkMode() ? QColor(100, 100, 100) : QColor(150, 150, 150);
        } else {
            textColor = isDarkMode() ? QColor(255, 255, 255) : QColor(40, 40, 40);
        }
        
        painter.setPen(textColor);
        QFont font = painter.font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    } else if (!m_icon.isNull()) {
        const int iconX = (BUTTON_SIZE - ICON_SIZE) / 2;
        const int iconY = (BUTTON_SIZE - ICON_SIZE) / 2;
        const QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        // Choose icon mode based on state
        QIcon::Mode iconMode = QIcon::Normal;
        if (!m_enabled) {
            iconMode = QIcon::Disabled;
        } else if (m_pressed) {
            iconMode = QIcon::Active;
        }
        
        m_icon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, QIcon::On);
    }
}

void ActionBarButton::mousePressEvent(QMouseEvent* event)
{
    if (m_enabled && event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void ActionBarButton::mouseReleaseEvent(QMouseEvent* event)
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ActionBarButton::enterEvent(QEnterEvent* event)
#else
void ActionBarButton::enterEvent(QEvent* event)
#endif
{
    if (m_enabled) {
        m_hovered = true;
        update();
    }
    QWidget::enterEvent(event);
}

void ActionBarButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

bool ActionBarButton::isDarkMode() const
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

QColor ActionBarButton::backgroundColor() const
{
    if (!m_enabled) {
        // Disabled: more muted colors
        if (isDarkMode()) {
            return QColor(45, 45, 45);
        } else {
            return QColor(200, 200, 200);
        }
    }
    
    // Enabled: neutral background (same as unchecked SubToolbarToggle)
    if (isDarkMode()) {
        return QColor(60, 60, 60);
    } else {
        return QColor(220, 220, 220);
    }
}

void ActionBarButton::updateIcon()
{
    if (m_iconBaseName.isEmpty()) {
        return;
    }
    
    QString path = m_darkMode
        ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseName)
        : QString(":/resources/icons/%1.png").arg(m_iconBaseName);
    
    m_icon = QIcon(path);
    update();
}

