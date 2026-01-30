#include "TimelineDelegate.h"
#include "TimelineModel.h"
#include "../../core/NotebookLibrary.h"
#include "../ThemeColors.h"

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

void TimelineDelegate::invalidateThumbnail(const QString& bundlePath)
{
    // CR-P.1: Remove stale thumbnail when NotebookLibrary::thumbnailUpdated fires
    // The cache key is the thumbnail file path, not the bundle path, so we need
    // to look up what the thumbnail path would be
    QString thumbnailPath = NotebookLibrary::instance()->thumbnailPathFor(bundlePath);
    if (!thumbnailPath.isEmpty()) {
        m_thumbnailCache.remove(thumbnailPath);
    }
}

void TimelineDelegate::clearThumbnailCache()
{
    m_thumbnailCache.clear();
}

void TimelineDelegate::paintSectionHeader(QPainter* painter, const QRect& rect,
                                          const QString& title) const
{
    // Colors
    QColor textColor = ThemeColors::textSecondary(m_darkMode);
    QColor lineColor = ThemeColors::separator(m_darkMode);
    
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
        bgColor = ThemeColors::selection(m_darkMode);
    } else if (option.state & QStyle::State_MouseOver) {
        bgColor = ThemeColors::itemHover(m_darkMode);
    } else {
        bgColor = ThemeColors::itemBackground(m_darkMode);
    }
    
    // Draw rounded rect with shadow
    QPainterPath path;
    path.addRoundedRect(cardRect, CARD_CORNER_RADIUS, CARD_CORNER_RADIUS);
    
    // Shadow (subtle)
    if (!m_darkMode) {
        QRect shadowRect = cardRect.translated(0, 2);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, CARD_CORNER_RADIUS, CARD_CORNER_RADIUS);
        painter->fillPath(shadowPath, ThemeColors::cardShadow());
    }
    
    painter->fillPath(path, bgColor);
    
    // Border
    painter->setPen(QPen(ThemeColors::cardBorder(m_darkMode), 1));
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
    
    painter->setPen(ThemeColors::textPrimary(m_darkMode));
    
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
    
    painter->setPen(ThemeColors::textMuted(m_darkMode));
    
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
        typeColor = ThemeColors::typePdf(m_darkMode);
    } else if (isEdgeless) {
        typeColor = ThemeColors::typeEdgeless(m_darkMode);
    } else {
        typeColor = ThemeColors::typePaged(m_darkMode);
    }
    painter->setPen(typeColor);
    
    QRect typeRect(textLeft, bottomY, textWidth - 20, 16);
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, typeText);
    
    // Star indicator
    bool isStarred = index.data(TimelineModel::IsStarredRole).toBool();
    if (isStarred) {
        painter->setPen(ThemeColors::star(m_darkMode));
        
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
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(rect, THUMBNAIL_CORNER_RADIUS, THUMBNAIL_CORNER_RADIUS);
    painter->fillPath(thumbPath, ThemeColors::thumbnailBg(m_darkMode));
    
    if (thumbnailPath.isEmpty() || !QFileInfo::exists(thumbnailPath)) {
        // Draw placeholder
        painter->setPen(ThemeColors::thumbnailPlaceholder(m_darkMode));
        
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

