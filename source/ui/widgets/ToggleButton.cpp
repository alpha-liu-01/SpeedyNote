#include "ToggleButton.h"

#include <QPainter>
#include <QMouseEvent>

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
    painter.drawRoundedRect(QRectF(rect()), BORDER_RADIUS, BORDER_RADIUS);
    
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void SubToolbarToggle::enterEvent(QEnterEvent* event)
#else
void SubToolbarToggle::enterEvent(QEvent* event)
#endif
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

QColor SubToolbarToggle::backgroundColor() const
{
    if (m_checked) {
        return m_darkMode ? QColor(70, 130, 180) : QColor(100, 149, 237);
    } else {
        return m_darkMode ? Qt::black : Qt::white;
    }
}

