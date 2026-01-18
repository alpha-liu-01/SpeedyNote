#include "ToggleButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>

SubToolbarToggle::SubToolbarToggle(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

bool SubToolbarToggle::isChecked() const
{
    return m_checked;
}

void SubToolbarToggle::setChecked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        update();
        emit toggled(m_checked);
    }
}

QIcon SubToolbarToggle::icon() const
{
    return m_icon;
}

void SubToolbarToggle::setIcon(const QIcon& icon)
{
    m_icon = icon;
    m_iconBaseName.clear();  // Clear base name since we're using a direct icon
    update();
}

void SubToolbarToggle::setIconName(const QString& baseName)
{
    m_iconBaseName = baseName;
    updateIcon();
}

void SubToolbarToggle::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcon();
    }
}

void SubToolbarToggle::updateIcon()
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

QSize SubToolbarToggle::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize SubToolbarToggle::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void SubToolbarToggle::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background circle
    QColor bgColor = backgroundColor();
    
    // Apply press/hover effects
    if (m_pressed) {
        bgColor = bgColor.darker(120);
    } else if (m_hovered && !m_checked) {
        bgColor = bgColor.lighter(110);
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawEllipse(rect());
    
    // Draw icon centered
    if (!m_icon.isNull()) {
        const int iconX = (BUTTON_SIZE - ICON_SIZE) / 2;
        const int iconY = (BUTTON_SIZE - ICON_SIZE) / 2;
        const QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        // Choose icon mode based on state
        QIcon::Mode iconMode = QIcon::Normal;
        if (m_pressed) {
            iconMode = QIcon::Active;
        }
        
        // For checked state in dark mode, we might want to use a different color
        // The icon should be visible against the background
        QIcon::State iconState = m_checked ? QIcon::On : QIcon::Off;
        
        m_icon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, iconState);
    }
}

void SubToolbarToggle::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void SubToolbarToggle::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        // Check if release is within button bounds
        if (rect().contains(event->pos())) {
            // Toggle the state
            setChecked(!m_checked);
        } else {
            update();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void SubToolbarToggle::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void SubToolbarToggle::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

bool SubToolbarToggle::isDarkMode() const
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

QColor SubToolbarToggle::backgroundColor() const
{
    if (m_checked) {
        // Checked: accent/highlighted background
        // Use a noticeable but not garish accent color
        if (isDarkMode()) {
            return QColor(70, 130, 180);  // Steel blue - visible in dark mode
        } else {
            return QColor(100, 149, 237); // Cornflower blue - visible in light mode
        }
    } else {
        // Unchecked: neutral background
        if (isDarkMode()) {
            return QColor(60, 60, 60);
        } else {
            return QColor(220, 220, 220);
        }
    }
}

