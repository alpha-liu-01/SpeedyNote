#include "ModeToggleButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>

ModeToggleButton::ModeToggleButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void ModeToggleButton::setModeIcons(const QIcon& mode0Icon, const QIcon& mode1Icon)
{
    m_icons[0] = mode0Icon;
    m_icons[1] = mode1Icon;
    m_iconBaseNames[0].clear();  // Clear base names since we're using direct icons
    m_iconBaseNames[1].clear();
    update();
}

void ModeToggleButton::setModeIconNames(const QString& mode0BaseName, const QString& mode1BaseName)
{
    m_iconBaseNames[0] = mode0BaseName;
    m_iconBaseNames[1] = mode1BaseName;
    updateIcons();
}

void ModeToggleButton::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcons();
    }
}

void ModeToggleButton::updateIcons()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_iconBaseNames[i].isEmpty()) {
            QString path = m_darkMode
                ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseNames[i])
                : QString(":/resources/icons/%1.png").arg(m_iconBaseNames[i]);
            m_icons[i] = QIcon(path);
        }
    }
    update();
}

void ModeToggleButton::setModeToolTips(const QString& mode0Tip, const QString& mode1Tip)
{
    m_toolTips[0] = mode0Tip;
    m_toolTips[1] = mode1Tip;
    updateToolTip();
}

int ModeToggleButton::currentMode() const
{
    return m_currentMode;
}

void ModeToggleButton::setCurrentMode(int mode)
{
    // Clamp to valid range
    mode = qBound(0, mode, 1);
    
    if (m_currentMode != mode) {
        m_currentMode = mode;
        updateToolTip();
        update();
        emit modeChanged(m_currentMode);
    }
}

QSize ModeToggleButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize ModeToggleButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void ModeToggleButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background circle
    QColor bgColor = backgroundColor();
    
    // Apply press/hover effects
    if (m_pressed) {
        bgColor = bgColor.darker(120);
    } else if (m_hovered) {
        bgColor = bgColor.lighter(110);
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawEllipse(rect());
    
    // Draw current mode's icon centered
    const QIcon& currentIcon = m_icons[m_currentMode];
    if (!currentIcon.isNull()) {
        const int iconX = (BUTTON_SIZE - ICON_SIZE) / 2;
        const int iconY = (BUTTON_SIZE - ICON_SIZE) / 2;
        const QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        // Choose icon mode based on state
        QIcon::Mode iconMode = QIcon::Normal;
        if (m_pressed) {
            iconMode = QIcon::Active;
        }
        
        currentIcon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, QIcon::On);
    }
}

void ModeToggleButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void ModeToggleButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        // Check if release is within button bounds
        if (rect().contains(event->pos())) {
            // Toggle between mode 0 and mode 1
            setCurrentMode(m_currentMode == 0 ? 1 : 0);
        } else {
            update();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ModeToggleButton::enterEvent(QEnterEvent* event)
#else
void ModeToggleButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ModeToggleButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

bool ModeToggleButton::isDarkMode() const
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

QColor ModeToggleButton::backgroundColor() const
{
    // Neutral background (same as unchecked SubToolbarToggle)
    if (isDarkMode()) {
        return QColor(60, 60, 60);
    } else {
        return QColor(220, 220, 220);
    }
}

void ModeToggleButton::updateToolTip()
{
    if (!m_toolTips[m_currentMode].isEmpty()) {
        setToolTip(m_toolTips[m_currentMode]);
    }
}

