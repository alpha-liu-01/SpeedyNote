#include "ActionBarButton.h"

#include "../../compat/qt_compat.h"  // Qt5/Qt6 tablet position shims

#include <QPainter>
#include <QMouseEvent>
#include <QTabletEvent>

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

void ActionBarButton::setCheckable(bool checkable)
{
    m_checkable = checkable;
}

void ActionBarButton::setChecked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        update();
    }
}

bool ActionBarButton::isChecked() const
{
    return m_checked;
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
            textColor = m_darkMode ? QColor(100, 100, 100) : QColor(150, 150, 150);
        } else {
            textColor = m_darkMode ? QColor(255, 255, 255) : QColor(40, 40, 40);
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
    if (event->button() == Qt::LeftButton) {
        if (m_enabled) {
            m_pressed = true;
            update();
        }
        // Accepted even while disabled: the base implementation ignores the
        // event, and a button placed over the canvas would then have every
        // press run as a canvas gesture by DocumentViewport as well.
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ActionBarButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_pressed) {
            m_pressed = false;

            // Check if release is within button bounds and button is enabled
            if (m_enabled && rect().contains(event->pos())) {
                emit clicked();
            }

            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ActionBarButton::tabletEvent(QTabletEvent* event)
{
    // Handled here rather than left to the platform promoting an unhandled
    // tablet event into a mouse event. That promotion is not tagged as
    // synthesized, so for a button parented to DocumentViewport it arms an
    // off-page pan, and the viewport then swallows the pen release that would
    // have completed the click. Accepting these also suppresses the promotion,
    // so the mouse path cannot fire a second time.
    switch (event->type()) {
        case QEvent::TabletPress:
            if (m_enabled) {
                m_pressed = true;
                update();
            }
            event->accept();
            return;

        case QEvent::TabletMove:
            if (m_pressed) {
                event->accept();
                return;
            }
            break;

        case QEvent::TabletRelease:
            if (m_pressed) {
                m_pressed = false;

                if (m_enabled && rect().contains(SN_EVENT_POS(event).toPoint())) {
                    emit clicked();
                }

                update();
            }
            event->accept();
            return;

        default:
            break;
    }

    QWidget::tabletEvent(event);
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

QColor ActionBarButton::backgroundColor() const
{
    if (!m_enabled) {
        return m_darkMode ? QColor(45, 45, 45) : QColor(200, 200, 200);
    }
    
    if (m_checkable && m_checked) {
        return m_darkMode ? QColor(70, 130, 180) : QColor(100, 149, 237);
    }
    
    return m_darkMode ? QColor(60, 60, 60) : QColor(220, 220, 220);
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

