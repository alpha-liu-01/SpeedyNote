#include "IconPresetButton.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

IconPresetButton::IconPresetButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    const QColor window = QApplication::palette().color(QPalette::Window);
    // Same luminance test the other preset buttons use, so all three look
    // consistent under a custom palette.
    const qreal luminance = 0.299 * window.redF()
                          + 0.587 * window.greenF()
                          + 0.114 * window.blueF();
    m_darkMode = luminance < 0.5;
}

void IconPresetButton::setIcon(const QIcon& icon)
{
    m_icon = icon;
    m_iconBaseName.clear();   // A direct icon wins over any base name.
    update();
}

void IconPresetButton::setIconName(const QString& baseName)
{
    m_iconBaseName = baseName;
    updateIcon();
}

void IconPresetButton::setDarkMode(bool darkMode)
{
    if (m_darkMode == darkMode) {
        return;
    }
    m_darkMode = darkMode;
    updateIcon();
}

void IconPresetButton::updateIcon()
{
    if (m_iconBaseName.isEmpty()) {
        return;
    }
    const QString path = m_darkMode
        ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(m_iconBaseName)
        : QStringLiteral(":/resources/icons/%1.png").arg(m_iconBaseName);
    m_icon = QIcon(path);
    update();
}

void IconPresetButton::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

QSize IconPresetButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize IconPresetButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void IconPresetButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor bgColor = m_darkMode ? Qt::black : Qt::white;
    if (m_pressed) {
        bgColor = m_darkMode ? bgColor.lighter(135) : bgColor.darker(115);
    } else if (m_hovered && !m_selected) {
        bgColor = m_darkMode ? bgColor.lighter(120) : bgColor.lighter(97);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(QRectF(rect()), BORDER_RADIUS, BORDER_RADIUS);

    if (m_selected) {
        // Accent ring, matching the app's blue (PageThumbnailDelegate uses the
        // same pair for its current-page marker).
        const QColor accent = m_darkMode ? QColor(100, 149, 237)
                                         : QColor(66, 133, 244);
        const qreal half = BORDER_WIDTH_SELECTED / 2.0;
        const QRectF ring = QRectF(rect()).adjusted(half, half, -half, -half);
        QPen pen(accent);
        pen.setWidthF(BORDER_WIDTH_SELECTED);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(ring, BORDER_RADIUS, BORDER_RADIUS);
    }

    if (!m_icon.isNull()) {
        const QRect iconRect((BUTTON_SIZE - ICON_SIZE) / 2,
                             (BUTTON_SIZE - ICON_SIZE) / 2,
                             ICON_SIZE, ICON_SIZE);
        painter.drawPixmap(iconRect, m_icon.pixmap(iconRect.size()));
    }
}

void IconPresetButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void IconPresetButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        update();
        // Only a release inside the button counts, so dragging off cancels.
        if (rect().contains(event->pos())) {
            emit clicked();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void IconPresetButton::enterEvent(QEnterEvent* event)
#else
void IconPresetButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void IconPresetButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;   // Cancel a press that ends outside the widget.
    update();
    QWidget::leaveEvent(event);
}
