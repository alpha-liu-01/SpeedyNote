#include "LinkSlotButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QTimerEvent>

LinkSlotButton::LinkSlotButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    updateToolTip();
}

LinkSlotState LinkSlotButton::state() const
{
    return m_state;
}

void LinkSlotButton::setState(LinkSlotState state)
{
    if (m_state != state) {
        m_state = state;
        updateToolTip();
        update();
        emit stateChanged(m_state);
    }
}

bool LinkSlotButton::isSelected() const
{
    return m_selected;
}

void LinkSlotButton::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
        emit selectedChanged(m_selected);
    }
}

void LinkSlotButton::setStateIcons(const QIcon& emptyIcon,
                                    const QIcon& positionIcon,
                                    const QIcon& urlIcon,
                                    const QIcon& markdownIcon)
{
    m_icons[static_cast<int>(LinkSlotState::Empty)] = emptyIcon;
    m_icons[static_cast<int>(LinkSlotState::Position)] = positionIcon;
    m_icons[static_cast<int>(LinkSlotState::Url)] = urlIcon;
    m_icons[static_cast<int>(LinkSlotState::Markdown)] = markdownIcon;
    // Clear base names since we're using direct icons
    for (int i = 0; i < 4; ++i) {
        m_iconBaseNames[i].clear();
    }
    m_hasCustomIcons = true;
    update();
}

void LinkSlotButton::setStateIconNames(const QString& emptyBaseName,
                                        const QString& positionBaseName,
                                        const QString& urlBaseName,
                                        const QString& markdownBaseName)
{
    m_iconBaseNames[static_cast<int>(LinkSlotState::Empty)] = emptyBaseName;
    m_iconBaseNames[static_cast<int>(LinkSlotState::Position)] = positionBaseName;
    m_iconBaseNames[static_cast<int>(LinkSlotState::Url)] = urlBaseName;
    m_iconBaseNames[static_cast<int>(LinkSlotState::Markdown)] = markdownBaseName;
    m_hasCustomIcons = true;
    updateIcons();
}

void LinkSlotButton::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcons();
    }
}

void LinkSlotButton::updateIcons()
{
    for (int i = 0; i < 4; ++i) {
        if (!m_iconBaseNames[i].isEmpty()) {
            QString path = m_darkMode
                ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseNames[i])
                : QString(":/resources/icons/%1.png").arg(m_iconBaseNames[i]);
            m_icons[i] = QIcon(path);
        }
    }
    update();
}

QSize LinkSlotButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize LinkSlotButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void LinkSlotButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background rounded rect
    QColor bgColor = backgroundColor();
    if (m_pressed && !m_longPressTriggered) {
        bgColor = bgColor.darker(120);
    } else if (m_hovered && !m_selected) {
        bgColor = bgColor.lighter(110);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(QRectF(rect()), BORDER_RADIUS, BORDER_RADIUS);

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
    
    // Draw icon
    QIcon icon = currentIcon();
    if (!icon.isNull()) {
        const int iconX = (BUTTON_SIZE - ICON_SIZE) / 2;
        const int iconY = (BUTTON_SIZE - ICON_SIZE) / 2;
        const QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        QIcon::Mode iconMode = QIcon::Normal;
        if (m_pressed) {
            iconMode = QIcon::Active;
        }
        
        icon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, QIcon::On);
    } else {
        // Draw fallback symbols if no icon is set
        painter.setPen(m_darkMode ? Qt::white : Qt::black);
        QFont font = painter.font();
        font.setPixelSize(16);
        font.setBold(true);
        painter.setFont(font);
        
        QString symbol;
        switch (m_state) {
            case LinkSlotState::Empty:
                symbol = "+";
                break;
            case LinkSlotState::Position:
                symbol = "P";  // Position
                break;
            case LinkSlotState::Url:
                symbol = "U";  // URL
                break;
            case LinkSlotState::Markdown:
                symbol = "M";  // Markdown
                break;
        }
        
        painter.drawText(rect(), Qt::AlignCenter, symbol);
    }
}

void LinkSlotButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_longPressTriggered = false;
        update();
        
        // Start long-press timer only for non-empty slots
        if (m_state != LinkSlotState::Empty) {
            startLongPressTimer();
        }
    }
    QWidget::mousePressEvent(event);
}

void LinkSlotButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        stopLongPressTimer();
        m_pressed = false;
        
        // Only emit clicked if long-press wasn't triggered
        if (!m_longPressTriggered && rect().contains(event->pos())) {
            emit clicked();
        }
        
        m_longPressTriggered = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void LinkSlotButton::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_longPressTimerId) {
        stopLongPressTimer();
        m_longPressTriggered = true;
        
        // Emit delete request for non-empty slots
        if (m_state != LinkSlotState::Empty) {
            emit deleteRequested();
        }
        
        update();
    }
    QWidget::timerEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void LinkSlotButton::enterEvent(QEnterEvent* event)
#else
void LinkSlotButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void LinkSlotButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    stopLongPressTimer();
    m_longPressTriggered = false;
    update();
    QWidget::leaveEvent(event);
}

QColor LinkSlotButton::borderColor() const
{
    if (m_selected) {
        return m_darkMode ? Qt::white : Qt::black;
    } else {
        return Qt::transparent;
    }
}

QColor LinkSlotButton::backgroundColor() const
{
    return m_darkMode ? Qt::black : Qt::white;
}

QIcon LinkSlotButton::currentIcon() const
{
    if (m_hasCustomIcons) {
        return m_icons[static_cast<int>(m_state)];
    }
    // Return null icon - fallback text symbols will be drawn in paintEvent
    return QIcon();
}

void LinkSlotButton::startLongPressTimer()
{
    if (m_longPressTimerId == 0) {
        m_longPressTimerId = startTimer(LONG_PRESS_MS);
    }
}

void LinkSlotButton::stopLongPressTimer()
{
    if (m_longPressTimerId != 0) {
        killTimer(m_longPressTimerId);
        m_longPressTimerId = 0;
    }
}

void LinkSlotButton::updateToolTip()
{
    QString tip;
    switch (m_state) {
        case LinkSlotState::Empty:
            tip = tr("Empty slot (click to add link)");
            break;
        case LinkSlotState::Position:
            tip = tr("Position link (click to navigate, long-press to delete)");
            break;
        case LinkSlotState::Url:
            tip = tr("URL link (click to open, long-press to delete)");
            break;
        case LinkSlotState::Markdown:
            tip = tr("Markdown link (click to view, long-press to delete)");
            break;
    }
    setToolTip(tip);
}

