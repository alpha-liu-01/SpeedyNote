#include "TimelineDelegate.h"
#include "TimelineModel.h"

#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QLocale>
#include <QFileInfo>

TimelineDelegate::TimelineDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void TimelineDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
    
    if (isHeader) {
        QString title = index.data(Qt::DisplayRole).toString();
        paintSectionHeader(painter, option.rect, title);
    } else {
        paintNotebookCard(painter, option.rect, option, index);
    }
    
    painter->restore();
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    Q_UNUSED(option)
    
    bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
    
    if (isHeader) {
        return QSize(100, HEADER_HEIGHT);
    } else {
        return QSize(100, CARD_HEIGHT + CARD_MARGIN);
    }
}

void TimelineDelegate::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
    }
}

void TimelineDelegate::paintSectionHeader(QPainter* painter, const QRect& rect,
                                          const QString& title) const
{
    // Colors
    QColor textColor = m_darkMode ? QColor(180, 180, 180) : QColor(100, 100, 100);
    QColor lineColor = m_darkMode ? QColor(60, 60, 60) : QColor(220, 220, 220);
    
    // Draw text
    QFont font = painter->font();
    font.setPointSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(textColor);
    
    QRect textRect = rect.adjusted(CARD_MARGIN + CARD_PADDING, 0, -CARD_MARGIN, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
    
    // Draw underline
    int textWidth = painter->fontMetrics().horizontalAdvance(title);
    int lineY = rect.bottom() - 2;
    painter->setPen(QPen(lineColor, 1));
    painter->drawLine(textRect.left(), lineY, 
                     textRect.left() + textWidth + 20, lineY);
}

void TimelineDelegate::paintNotebookCard(QPainter* painter, const QRect& rect,
                                         const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const
{
    // Card rect with margins
    QRect cardRect = rect.adjusted(CARD_MARGIN, 0, -CARD_MARGIN, -CARD_MARGIN);
    
    // === Background ===
    QColor bgColor;
    if (option.state & QStyle::State_Selected) {
        bgColor = m_darkMode ? QColor(50, 80, 120) : QColor(220, 235, 250);
    } else if (option.state & QStyle::State_MouseOver) {
        bgColor = m_darkMode ? QColor(55, 55, 60) : QColor(248, 248, 252);
    } else {
        bgColor = m_darkMode ? QColor(45, 45, 50) : QColor(255, 255, 255);
    }
    
    // Draw rounded rect with shadow
    QPainterPath path;
    path.addRoundedRect(cardRect, CARD_CORNER_RADIUS, CARD_CORNER_RADIUS);
    
    // Shadow (subtle)
    if (!m_darkMode) {
        QRect shadowRect = cardRect.translated(0, 2);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, CARD_CORNER_RADIUS, CARD_CORNER_RADIUS);
        painter->fillPath(shadowPath, QColor(0, 0, 0, 20));
    }
    
    painter->fillPath(path, bgColor);
    
    // Border
    QColor borderColor = m_darkMode ? QColor(70, 70, 75) : QColor(230, 230, 235);
    painter->setPen(QPen(borderColor, 1));
    painter->drawPath(path);
    
    // === Content layout ===
    int contentLeft = cardRect.left() + CARD_PADDING;
    int contentTop = cardRect.top() + CARD_PADDING;
    int contentRight = cardRect.right() - CARD_PADDING;
    int contentBottom = cardRect.bottom() - CARD_PADDING;
    
    // === Thumbnail ===
    QString thumbnailPath = index.data(TimelineModel::ThumbnailPathRole).toString();
    QRect thumbRect(contentLeft, contentTop, THUMBNAIL_WIDTH, contentBottom - contentTop);
    drawThumbnail(painter, thumbRect, thumbnailPath);
    
    // === Text content ===
    int textLeft = contentLeft + THUMBNAIL_WIDTH + CARD_PADDING;
    int textWidth = contentRight - textLeft;
    
    // Name (bold)
    QString name = index.data(Qt::DisplayRole).toString();
    QFont nameFont = painter->font();
    nameFont.setPointSize(12);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    
    QColor textColor = m_darkMode ? QColor(240, 240, 240) : QColor(30, 30, 30);
    painter->setPen(textColor);
    
    QRect nameRect(textLeft, contentTop, textWidth, 20);
    QString elidedName = painter->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
    
    // Date
    QDateTime lastModified = index.data(TimelineModel::LastModifiedRole).toDateTime();
    QString dateText = formatDateTime(lastModified);
    
    QFont dateFont = painter->font();
    dateFont.setPointSize(10);
    dateFont.setBold(false);
    painter->setFont(dateFont);
    
    QColor dateColor = m_darkMode ? QColor(150, 150, 150) : QColor(120, 120, 120);
    painter->setPen(dateColor);
    
    QRect dateRect(textLeft, contentTop + 22, textWidth, 16);
    painter->drawText(dateRect, Qt::AlignLeft | Qt::AlignVCenter, dateText);
    
    // === Bottom row: Type indicator + Star ===
    int bottomY = contentBottom - 16;
    
    // Type indicator
    bool isPdf = index.data(TimelineModel::IsPdfBasedRole).toBool();
    bool isEdgeless = index.data(TimelineModel::IsEdgelessRole).toBool();
    QString typeText = typeIndicatorText(isPdf, isEdgeless);
    
    QFont typeFont = painter->font();
    typeFont.setPointSize(9);
    painter->setFont(typeFont);
    
    QColor typeColor;
    if (isPdf) {
        typeColor = m_darkMode ? QColor(200, 100, 100) : QColor(180, 60, 60);
    } else if (isEdgeless) {
        typeColor = m_darkMode ? QColor(100, 180, 100) : QColor(60, 140, 60);
    } else {
        typeColor = m_darkMode ? QColor(100, 140, 200) : QColor(60, 100, 180);
    }
    painter->setPen(typeColor);
    
    QRect typeRect(textLeft, bottomY, textWidth - 20, 16);
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, typeText);
    
    // Star indicator
    bool isStarred = index.data(TimelineModel::IsStarredRole).toBool();
    if (isStarred) {
        QColor starColor = m_darkMode ? QColor(255, 200, 50) : QColor(230, 180, 30);
        painter->setPen(starColor);
        
        QFont starFont = painter->font();
        starFont.setPointSize(12);
        painter->setFont(starFont);
        
        QRect starRect(contentRight - 20, bottomY - 2, 20, 20);
        painter->drawText(starRect, Qt::AlignCenter, "★");
    }
}

void TimelineDelegate::drawThumbnail(QPainter* painter, const QRect& rect,
                                     const QString& thumbnailPath) const
{
    // Background (for letterboxing or missing thumbnail)
    QColor bgColor = m_darkMode ? QColor(60, 60, 65) : QColor(240, 240, 245);
    
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(rect, THUMBNAIL_CORNER_RADIUS, THUMBNAIL_CORNER_RADIUS);
    painter->fillPath(thumbPath, bgColor);
    
    if (thumbnailPath.isEmpty() || !QFileInfo::exists(thumbnailPath)) {
        // Draw placeholder
        QColor placeholderColor = m_darkMode ? QColor(100, 100, 105) : QColor(200, 200, 205);
        painter->setPen(placeholderColor);
        
        QFont font = painter->font();
        font.setPointSize(20);
        painter->setFont(font);
        painter->drawText(rect, Qt::AlignCenter, "📄");
        return;
    }
    
    // Load thumbnail (with caching)
    QPixmap thumbnail;
    if (m_thumbnailCache.contains(thumbnailPath)) {
        thumbnail = m_thumbnailCache[thumbnailPath];
    } else {
        thumbnail.load(thumbnailPath);
        if (!thumbnail.isNull()) {
            m_thumbnailCache[thumbnailPath] = thumbnail;
        }
    }
    
    if (thumbnail.isNull()) {
        return;
    }
    
    // Calculate source and destination rects per Q&A (C+D hybrid)
    qreal thumbAspect = static_cast<qreal>(thumbnail.height()) / thumbnail.width();
    qreal rectAspect = static_cast<qreal>(rect.height()) / rect.width();
    
    QRect sourceRect;
    QRect destRect;
    
    if (thumbAspect > rectAspect) {
        // Thumbnail is taller than card - top-align crop
        int sourceHeight = static_cast<int>(thumbnail.width() * rectAspect);
        sourceRect = QRect(0, 0, thumbnail.width(), sourceHeight);
        destRect = rect;
    } else if (thumbAspect < rectAspect) {
        // Thumbnail is shorter than card - letterbox (center vertically)
        int destHeight = static_cast<int>(rect.width() * thumbAspect);
        int yOffset = (rect.height() - destHeight) / 2;
        sourceRect = QRect(0, 0, thumbnail.width(), thumbnail.height());
        destRect = QRect(rect.left(), rect.top() + yOffset, rect.width(), destHeight);
    } else {
        // Aspect ratios match
        sourceRect = thumbnail.rect();
        destRect = rect;
    }
    
    // Clip to rounded rect
    painter->save();
    painter->setClipPath(thumbPath);
    painter->drawPixmap(destRect, thumbnail, sourceRect);
    painter->restore();
}

QString TimelineDelegate::typeIndicatorText(bool isPdf, bool isEdgeless) const
{
    if (isPdf) {
        return tr("PDF Annotation");
    } else if (isEdgeless) {
        return tr("Edgeless Canvas");
    } else {
        return tr("Paged Notebook");
    }
}

QString TimelineDelegate::formatDateTime(const QDateTime& dt) const
{
    if (!dt.isValid()) {
        return tr("Unknown date");
    }
    
    QDate today = QDate::currentDate();
    QDate date = dt.date();
    
    if (date == today) {
        return tr("Today at %1").arg(dt.toString("h:mm AP"));
    } else if (date == today.addDays(-1)) {
        return tr("Yesterday at %1").arg(dt.toString("h:mm AP"));
    } else if (date >= today.addDays(-7)) {
        return dt.toString("dddd, h:mm AP");  // e.g., "Monday, 3:45 PM"
    } else if (date.year() == today.year()) {
        return dt.toString("MMM d");  // e.g., "Jan 5"
    } else {
        return dt.toString("MMM d, yyyy");  // e.g., "Dec 25, 2025"
    }
}

