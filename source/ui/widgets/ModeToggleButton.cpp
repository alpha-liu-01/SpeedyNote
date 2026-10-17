#include "ModeToggleButton.h"

#include <QPainter>
#include <QMouseEvent>

ModeToggleButton::ModeToggleButton(QWidget* parent)
    : QWidget(parent)
    , m_icons(2)
    , m_iconBaseNames(2)
    , m_toolTips(2)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void ModeToggleButton::setModeCount(int count)
{
    count = qBound(2, count, kMaxModes);
    if (count == modeCount()) {
        return;
    }
    m_icons.resize(count);
    m_iconBaseNames.resize(count);
    m_toolTips.resize(count);
    if (m_currentMode >= count) {
        m_currentMode = count - 1;
    }
    updateToolTip();
    update();
}

void ModeToggleButton::setModeIcons(const QVector<QIcon>& icons)
{
    setModeCount(icons.size());
    for (int i = 0; i < modeCount() && i < icons.size(); ++i) {
        m_icons[i] = icons[i];
        m_iconBaseNames[i].clear();  // Direct icons win over base names
    }
    update();
}

void ModeToggleButton::setModeIcons(const QIcon& mode0Icon, const QIcon& mode1Icon)
{
    setModeCount(2);
    m_icons[0] = mode0Icon;
    m_icons[1] = mode1Icon;
    m_iconBaseNames[0].clear();  // Clear base names since we're using direct icons
    m_iconBaseNames[1].clear();
    update();
}

void ModeToggleButton::setModeIconNames(const QStringList& baseNames)
{
    setModeCount(baseNames.size());
    for (int i = 0; i < modeCount() && i < baseNames.size(); ++i) {
        m_iconBaseNames[i] = baseNames[i];
    }
    updateIcons();
}

void ModeToggleButton::setModeIconNames(const QString& mode0BaseName, const QString& mode1BaseName)
{
    setModeCount(2);
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
    for (int i = 0; i < modeCount(); ++i) {
        if (!m_iconBaseNames[i].isEmpty()) {
            QString path = m_darkMode
                ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseNames[i])
                : QString(":/resources/icons/%1.png").arg(m_iconBaseNames[i]);
            m_icons[i] = QIcon(path);
        }
    }
    update();
}

void ModeToggleButton::setModeToolTips(const QStringList& toolTips)
{
    setModeCount(toolTips.size());
    for (int i = 0; i < modeCount() && i < toolTips.size(); ++i) {
        m_toolTips[i] = toolTips[i];
    }
    updateToolTip();
}

void ModeToggleButton::setModeToolTips(const QString& mode0Tip, const QString& mode1Tip)
{
    setModeCount(2);
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
    mode = qBound(0, mode, modeCount() - 1);
    
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
    painter.drawRoundedRect(QRectF(rect()), BORDER_RADIUS, BORDER_RADIUS);
    
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
            // Advance to the next mode, wrapping at the end.
            setCurrentMode((m_currentMode + 1) % modeCount());
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

QColor ModeToggleButton::backgroundColor() const
{
    return m_darkMode ? Qt::black : Qt::white;
}

void ModeToggleButton::updateToolTip()
{
    if (m_currentMode >= 0 && m_currentMode < m_toolTips.size()
        && !m_toolTips[m_currentMode].isEmpty()) {
        setToolTip(m_toolTips[m_currentMode]);
    }
}

