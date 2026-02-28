#include "LauncherNavButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>
#include <QFontMetrics>

LauncherNavButton::LauncherNavButton(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    // Set initial size
    setFixedSize(EXPANDED_WIDTH, BUTTON_HEIGHT);
}

void LauncherNavButton::setIconName(const QString& baseName)
{
    m_iconBaseName = baseName;
    updateIcon();
}

void LauncherNavButton::setIcon(const QIcon& icon)
{
    m_icon = icon;
    m_iconBaseName.clear();
    update();
}

void LauncherNavButton::setText(const QString& text)
{
    m_text = text;
    update();
}

void LauncherNavButton::setCheckable(bool checkable)
{
    m_checkable = checkable;
}

void LauncherNavButton::setChecked(bool checked)
{
    if (m_checkable && m_checked != checked) {
        m_checked = checked;
        emit toggled(checked);
        update();
    }
}

void LauncherNavButton::setCompact(bool compact)
{
    if (m_compact != compact) {
        m_compact = compact;
        
        if (compact) {
            setFixedSize(BUTTON_HEIGHT, BUTTON_HEIGHT); // 44x44 circle
        } else {
            setFixedSize(EXPANDED_WIDTH, BUTTON_HEIGHT); // 132x44 pill
        }
        
        update();
    }
}

void LauncherNavButton::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        updateIcon();
    }
}

bool LauncherNavButton::isDarkMode() const
{
    // Use cached value if explicitly set, otherwise detect from palette
    if (m_darkMode) {
        return true;
    }
    
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    return luminance < 0.5;
}

QSize LauncherNavButton::sizeHint() const
{
    if (m_compact) {
        return QSize(BUTTON_HEIGHT, BUTTON_HEIGHT);
    }
    return QSize(EXPANDED_WIDTH, BUTTON_HEIGHT);
}

QSize LauncherNavButton::minimumSizeHint() const
{
    return sizeHint();
}

void LauncherNavButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // === Background ===
    QColor bgColor = backgroundColor();
    
    // Apply hover/press effects
    if (m_pressed) {
        bgColor = bgColor.darker(115);
    } else if (m_hovered) {
        bgColor = bgColor.lighter(108);
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    
    if (m_compact) {
        // Circle in compact mode
        painter.drawEllipse(rect());
    } else {
        // Pill shape in expanded mode
        painter.drawRoundedRect(rect(), BORDER_RADIUS, BORDER_RADIUS);
    }
    
    // === Icon ===
    if (!m_icon.isNull()) {
        int iconX, iconY;
        
        if (m_compact) {
            // Centered in compact mode
            iconX = (width() - ICON_SIZE) / 2;
            iconY = (height() - ICON_SIZE) / 2;
        } else {
            // Left-aligned with margin in expanded mode
            iconX = ICON_MARGIN;
            iconY = (height() - ICON_SIZE) / 2;
        }
        
        QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        QIcon::Mode iconMode = QIcon::Normal;
        if (m_pressed) {
            iconMode = QIcon::Active;
        }
        
        m_icon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, QIcon::On);
    }
    
    // === Text (only in expanded mode) ===
    if (!m_compact && !m_text.isEmpty()) {
        QColor txtColor = textColor();
        painter.setPen(txtColor);
        
        QFont font = painter.font();
        font.setPointSize(11);
        font.setWeight(m_checked ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        
        int textX = ICON_MARGIN + ICON_SIZE + TEXT_MARGIN;
        int textWidth = width() - textX - TEXT_MARGIN;
        QRect textRect(textX, 0, textWidth, height());
        
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_text);
    }
}

void LauncherNavButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void LauncherNavButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        if (rect().contains(event->pos())) {
            if (m_checkable) {
                setChecked(true); // For nav buttons, clicking always selects
            }
            emit clicked();
        }
        
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void LauncherNavButton::enterEvent(QEnterEvent* event)
#else
void LauncherNavButton::enterEvent(QEvent* event)
#endif
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void LauncherNavButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QWidget::leaveEvent(event);
}

void LauncherNavButton::updateIcon()
{
    if (m_iconBaseName.isEmpty()) {
        return;
    }
    
    QString path = isDarkMode()
        ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseName)
        : QString(":/resources/icons/%1.png").arg(m_iconBaseName);
    
    m_icon = QIcon(path);
    update();
}

QColor LauncherNavButton::backgroundColor() const
{
    bool dark = isDarkMode();
    
    if (m_checked) {
        // Accent color when checked
        if (dark) {
            return QColor(138, 180, 248, 50); // Light blue with alpha
        } else {
            return QColor(66, 133, 244, 40); // Google blue with alpha
        }
    }
    
    // Normal unchecked state
    if (dark) {
        return QColor(60, 60, 60);
    } else {
        return QColor(230, 230, 230);
    }
}

QColor LauncherNavButton::textColor() const
{
    bool dark = isDarkMode();
    
    if (m_checked) {
        // Accent color for text when checked
        if (dark) {
            return QColor(138, 180, 248); // Light blue
        } else {
            return QColor(26, 115, 232); // Google blue
        }
    }
    
    // Normal text color
    if (dark) {
        return QColor(224, 224, 224);
    } else {
        return QColor(51, 51, 51);
    }
}

