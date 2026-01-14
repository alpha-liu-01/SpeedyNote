#include "NotebookCard.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QFileInfo>

NotebookCard::NotebookCard(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(CARD_WIDTH, CARD_HEIGHT);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    
    // Long-press timer setup
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(LONG_PRESS_MS);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        m_longPressTriggered = true;
        m_pressed = false;
        update();
        emit longPressed();
    });
}

NotebookCard::NotebookCard(const NotebookInfo& info, QWidget* parent)
    : NotebookCard(parent)
{
    setNotebookInfo(info);
}

void NotebookCard::setNotebookInfo(const NotebookInfo& info)
{
    m_info = info;
    loadThumbnail();
    update();
}

void NotebookCard::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        update();
    }
}

void NotebookCard::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
    }
}

QSize NotebookCard::sizeHint() const
{
    return QSize(CARD_WIDTH, CARD_HEIGHT);
}

QSize NotebookCard::minimumSizeHint() const
{
    return QSize(CARD_WIDTH, CARD_HEIGHT);
}

void NotebookCard::loadThumbnail()
{
    QString path = NotebookLibrary::instance()->thumbnailPathFor(m_info.bundlePath);
    
    if (path != m_thumbnailPath) {
        m_thumbnailPath = path;
        m_thumbnail = QPixmap();
        
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            m_thumbnail.load(path);
        }
    }
}

void NotebookCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRect cardRect = rect();
    
    // === Background ===
    QColor bgColor = backgroundColor();
    
    if (m_pressed) {
        bgColor = bgColor.darker(115);
    } else if (m_hovered) {
        bgColor = bgColor.lighter(108);
    }
    
    // Draw card with shadow (light mode only)
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, CORNER_RADIUS, CORNER_RADIUS);
    
    if (!m_darkMode) {
        QRect shadowRect = cardRect.translated(0, 2);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, CORNER_RADIUS, CORNER_RADIUS);
        painter.fillPath(shadowPath, QColor(0, 0, 0, 25));
    }
    
    painter.fillPath(cardPath, bgColor);
    
    // Border (more visible if selected)
    if (m_selected) {
        QColor accentColor = m_darkMode ? QColor(138, 180, 248) : QColor(26, 115, 232);
        painter.setPen(QPen(accentColor, 2));
    } else {
        QColor borderColor = m_darkMode ? QColor(70, 70, 75) : QColor(220, 220, 225);
        painter.setPen(QPen(borderColor, 1));
    }
    painter.drawPath(cardPath);
    
    // === Thumbnail area ===
    QRect thumbRect(PADDING, PADDING, 
                    CARD_WIDTH - 2 * PADDING, 
                    THUMBNAIL_HEIGHT);
    drawThumbnail(&painter, thumbRect);
    
    // === Star indicator (top-right of thumbnail) ===
    if (m_info.isStarred) {
        QColor starColor = m_darkMode ? QColor(255, 200, 50) : QColor(230, 180, 30);
        painter.setPen(starColor);
        
        QFont starFont = painter.font();
        starFont.setPointSize(12);
        painter.setFont(starFont);
        
        QRect starRect(CARD_WIDTH - PADDING - 20, PADDING + 2, 18, 18);
        painter.drawText(starRect, Qt::AlignCenter, "★");
    }
    
    // === Name label ===
    int textY = PADDING + THUMBNAIL_HEIGHT + 6;
    int textWidth = CARD_WIDTH - 2 * PADDING;
    
    QFont nameFont = painter.font();
    nameFont.setPointSize(10);
    nameFont.setBold(true);
    painter.setFont(nameFont);
    
    QColor textColor = m_darkMode ? QColor(240, 240, 240) : QColor(30, 30, 30);
    painter.setPen(textColor);
    
    QString displayName = m_info.displayName();
    QFontMetrics fm(nameFont);
    QString elidedName = fm.elidedText(displayName, Qt::ElideRight, textWidth);
    
    QRect nameRect(PADDING, textY, textWidth, 18);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignTop, elidedName);
    
    // === Type indicator ===
    int typeY = textY + 20;
    
    QFont typeFont = painter.font();
    typeFont.setPointSize(8);
    typeFont.setBold(false);
    painter.setFont(typeFont);
    
    painter.setPen(typeIndicatorColor());
    
    QRect typeRect(PADDING, typeY, textWidth, 14);
    painter.drawText(typeRect, Qt::AlignLeft | Qt::AlignTop, typeIndicatorText());
}

void NotebookCard::drawThumbnail(QPainter* painter, const QRect& rect) const
{
    // Background for thumbnail area
    QColor bgColor = m_darkMode ? QColor(50, 50, 55) : QColor(235, 235, 240);
    
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(rect, THUMBNAIL_CORNER_RADIUS, THUMBNAIL_CORNER_RADIUS);
    painter->fillPath(thumbPath, bgColor);
    
    if (m_thumbnail.isNull()) {
        // Draw placeholder
        QColor placeholderColor = m_darkMode ? QColor(100, 100, 105) : QColor(180, 180, 185);
        painter->setPen(placeholderColor);
        
        QFont font = painter->font();
        font.setPointSize(28);
        painter->setFont(font);
        painter->drawText(rect, Qt::AlignCenter, "📄");
        return;
    }
    
    // Calculate source and destination rects per Q&A (C+D hybrid)
    qreal thumbAspect = static_cast<qreal>(m_thumbnail.height()) / m_thumbnail.width();
    qreal rectAspect = static_cast<qreal>(rect.height()) / rect.width();
    
    QRect sourceRect;
    QRect destRect;
    
    if (thumbAspect > rectAspect) {
        // Thumbnail is taller than card - top-align crop
        int sourceHeight = static_cast<int>(m_thumbnail.width() * rectAspect);
        sourceRect = QRect(0, 0, m_thumbnail.width(), sourceHeight);
        destRect = rect;
    } else if (thumbAspect < rectAspect) {
        // Thumbnail is shorter than card - letterbox (center vertically)
        int destHeight = static_cast<int>(rect.width() * thumbAspect);
        int yOffset = (rect.height() - destHeight) / 2;
        sourceRect = QRect(0, 0, m_thumbnail.width(), m_thumbnail.height());
        destRect = QRect(rect.left(), rect.top() + yOffset, rect.width(), destHeight);
    } else {
        // Aspect ratios match
        sourceRect = m_thumbnail.rect();
        destRect = rect;
    }
    
    // Clip to rounded rect and draw
    painter->save();
    painter->setClipPath(thumbPath);
    painter->drawPixmap(destRect, m_thumbnail, sourceRect);
    painter->restore();
}

QString NotebookCard::typeIndicatorText() const
{
    if (m_info.isPdfBased) {
        return tr("PDF");
    } else if (m_info.isEdgeless) {
        return tr("Edgeless");
    } else {
        return tr("Paged");
    }
}

QColor NotebookCard::typeIndicatorColor() const
{
    if (m_info.isPdfBased) {
        return m_darkMode ? QColor(200, 100, 100) : QColor(180, 60, 60);
    } else if (m_info.isEdgeless) {
        return m_darkMode ? QColor(100, 180, 100) : QColor(60, 140, 60);
    } else {
        return m_darkMode ? QColor(100, 140, 200) : QColor(60, 100, 180);
    }
}

QColor NotebookCard::backgroundColor() const
{
    if (m_selected) {
        return m_darkMode ? QColor(50, 80, 120) : QColor(220, 235, 250);
    }
    return m_darkMode ? QColor(45, 45, 50) : QColor(255, 255, 255);
}

void NotebookCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = event->pos();
        m_longPressTriggered = false;
        m_longPressTimer.start();
        update();
    }
    QWidget::mousePressEvent(event);
}

void NotebookCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_longPressTimer.stop();
        
        if (m_pressed && !m_longPressTriggered && rect().contains(event->pos())) {
            emit clicked();
        }
        
        m_pressed = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void NotebookCard::mouseMoveEvent(QMouseEvent* event)
{
    // Cancel long-press if moved too far
    if (m_longPressTimer.isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_longPressTimer.stop();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void NotebookCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void NotebookCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    m_longPressTimer.stop();
    update();
    QWidget::leaveEvent(event);
}

