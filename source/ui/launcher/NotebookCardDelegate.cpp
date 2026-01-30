#include "NotebookCardDelegate.h"
#include "../../core/NotebookLibrary.h"
#include "../ThemeColors.h"

#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>
#include <QDateTime>
#include <QDate>

NotebookCardDelegate::NotebookCardDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void NotebookCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    paintNotebookCard(painter, option.rect, option, index);
    
    painter->restore();
}

QSize NotebookCardDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    
    return QSize(CARD_WIDTH, CARD_HEIGHT);
}

void NotebookCardDelegate::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
    }
}

void NotebookCardDelegate::invalidateThumbnail(const QString& bundlePath)
{
    // Remove stale thumbnail when NotebookLibrary::thumbnailUpdated fires
    // The cache key is the thumbnail file path, not the bundle path
    QString thumbnailPath = NotebookLibrary::instance()->thumbnailPathFor(bundlePath);
    if (!thumbnailPath.isEmpty()) {
        m_thumbnailCache.remove(thumbnailPath);
    }
}

void NotebookCardDelegate::clearThumbnailCache()
{
    m_thumbnailCache.clear();
}

void NotebookCardDelegate::paintNotebookCard(QPainter* painter, const QRect& rect,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const
{
    // Determine states from option
    bool selected = option.state & QStyle::State_Selected;
    bool hovered = option.state & QStyle::State_MouseOver;
    
    // Check batch select mode state
    bool inSelectMode = index.data(IsInSelectModeRole).toBool();
    bool selectedInBatch = index.data(IsSelectedInBatchRole).toBool();
    
    // The rect from option is the cell rect - use it directly as card rect
    QRect cardRect = rect;
    
    // === Card Background ===
    QColor bgColor = backgroundColor(selected, hovered);
    
    // Draw card with shadow (light mode only)
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, CORNER_RADIUS, CORNER_RADIUS);
    
    if (!m_darkMode) {
        QRect shadowRect = cardRect.translated(0, 2);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, CORNER_RADIUS, CORNER_RADIUS);
        painter->fillPath(shadowPath, ThemeColors::cardShadow());
    }
    
    painter->fillPath(cardPath, bgColor);
    
    // Border (more visible if selected)
    if (selected) {
        // For selection border: inset rect by 1px so the 2px stroke stays within bounds
        // This prevents corner clipping where the stroke extends outside the item rect
        QRect borderRect = cardRect.adjusted(1, 1, -1, -1);
        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, CORNER_RADIUS - 1, CORNER_RADIUS - 1);
        painter->setPen(QPen(ThemeColors::selectionBorder(m_darkMode), 2));
        painter->drawPath(borderPath);
    } else {
        painter->setPen(QPen(ThemeColors::cardBorder(m_darkMode), 1));
        painter->drawPath(cardPath);
    }
    
    // === Thumbnail area ===
    QRect thumbRect(cardRect.left() + PADDING, cardRect.top() + PADDING,
                    cardRect.width() - 2 * PADDING, THUMBNAIL_HEIGHT);
    
    QString thumbnailPath = index.data(ThumbnailPathRole).toString();
    drawThumbnail(painter, thumbRect, thumbnailPath);
    
    // === Star indicator (top-right of thumbnail) ===
    bool isStarred = index.data(IsStarredRole).toBool();
    if (isStarred) {
        painter->setPen(ThemeColors::star(m_darkMode));
        
        QFont starFont = painter->font();
        starFont.setPointSize(12);
        painter->setFont(starFont);
        
        QRect starRect(cardRect.right() - PADDING - 20, cardRect.top() + PADDING + 2, 18, 18);
        painter->drawText(starRect, Qt::AlignCenter, "★");
    }
    
    // === Name label ===
    int textY = cardRect.top() + PADDING + THUMBNAIL_HEIGHT + 6;
    int textWidth = cardRect.width() - 2 * PADDING;
    
    QFont nameFont = painter->font();
    nameFont.setPointSize(10);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    
    painter->setPen(ThemeColors::textPrimary(m_darkMode));
    
    QString displayName = index.data(DisplayNameRole).toString();
    if (displayName.isEmpty()) {
        displayName = index.data(Qt::DisplayRole).toString();
    }
    
    QFontMetrics fm(nameFont);
    QString elidedName = fm.elidedText(displayName, Qt::ElideRight, textWidth);
    
    QRect nameRect(cardRect.left() + PADDING, textY, textWidth, 18);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignTop, elidedName);
    
    // === Date/time (if available) ===
    int dateY = textY + 18;
    QDateTime lastModified = index.data(LastModifiedRole).toDateTime();
    if (lastModified.isValid()) {
        QFont dateFont = painter->font();
        dateFont.setPointSize(8);
        dateFont.setBold(false);
        painter->setFont(dateFont);
        
        painter->setPen(ThemeColors::textSecondary(m_darkMode));
        
        QString dateStr = formatDateTime(lastModified);
        QRect dateRect(cardRect.left() + PADDING, dateY, textWidth, 14);
        painter->drawText(dateRect, Qt::AlignLeft | Qt::AlignTop, dateStr);
        
        dateY += 14;
    }
    
    // === Type indicator ===
    int typeY = dateY + 2;
    
    QFont typeFont = painter->font();
    typeFont.setPointSize(8);
    typeFont.setBold(false);
    painter->setFont(typeFont);
    
    bool isPdf = index.data(IsPdfBasedRole).toBool();
    bool isEdgeless = index.data(IsEdgelessRole).toBool();
    
    painter->setPen(typeIndicatorColor(isPdf, isEdgeless));
    
    // Reduce text width to make room for menu button
    int typeTextWidth = textWidth - MENU_BUTTON_SIZE - MENU_BUTTON_MARGIN;
    QRect typeRect(cardRect.left() + PADDING, typeY, typeTextWidth, 14);
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignTop, typeIndicatorText(isPdf, isEdgeless));
    
    // === 3-dot menu button OR selection indicator ===
    if (inSelectMode) {
        // In select mode: draw selection indicator (top-left), hide menu button
        drawSelectionIndicator(painter, cardRect, selectedInBatch);
    } else {
        // Normal mode: draw 3-dot menu button (bottom-right)
        QRect menuRect = menuButtonRect(cardRect);
        drawMenuButton(painter, menuRect, hovered);
    }
}

void NotebookCardDelegate::drawThumbnail(QPainter* painter, const QRect& rect,
                                          const QString& thumbnailPath) const
{
    // Background for thumbnail area
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(rect, THUMBNAIL_CORNER_RADIUS, THUMBNAIL_CORNER_RADIUS);
    painter->fillPath(thumbPath, ThemeColors::thumbnailBg(m_darkMode));
    
    if (thumbnailPath.isEmpty() || !QFileInfo::exists(thumbnailPath)) {
        // Draw placeholder
        painter->setPen(ThemeColors::thumbnailPlaceholder(m_darkMode));
        
        QFont font = painter->font();
        font.setPointSize(28);
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
    
    // Clip to rounded rect and draw
    painter->save();
    painter->setClipPath(thumbPath);
    painter->drawPixmap(destRect, thumbnail, sourceRect);
    painter->restore();
}

QString NotebookCardDelegate::typeIndicatorText(bool isPdf, bool isEdgeless) const
{
    if (isPdf) {
        return tr("PDF");
    } else if (isEdgeless) {
        return tr("Edgeless");
    } else {
        return tr("Paged");
    }
}

QColor NotebookCardDelegate::typeIndicatorColor(bool isPdf, bool isEdgeless) const
{
    if (isPdf) {
        return ThemeColors::typePdf(m_darkMode);
    } else if (isEdgeless) {
        return ThemeColors::typeEdgeless(m_darkMode);
    } else {
        return ThemeColors::typePaged(m_darkMode);
    }
}

QColor NotebookCardDelegate::backgroundColor(bool selected, bool hovered) const
{
    if (selected) {
        return ThemeColors::selection(m_darkMode);
    } else if (hovered) {
        return ThemeColors::itemHover(m_darkMode);
    } else {
        return ThemeColors::itemBackground(m_darkMode);
    }
}

QString NotebookCardDelegate::formatDateTime(const QDateTime& dateTime) const
{
    if (!dateTime.isValid()) {
        return QString();
    }
    
    QDate today = QDate::currentDate();
    QDate date = dateTime.date();
    QString timeStr = dateTime.time().toString("h:mm AP");
    
    if (date == today) {
        return tr("Today %1").arg(timeStr);
    } else if (date == today.addDays(-1)) {
        return tr("Yesterday %1").arg(timeStr);
    } else if (date.year() == today.year()) {
        // Same year: "Jan 15, 2:30 PM"
        return date.toString("MMM d") + ", " + timeStr;
    } else {
        // Different year: "Jan 15, 2024"
        return date.toString("MMM d, yyyy");
    }
}

QRect NotebookCardDelegate::menuButtonRect(const QRect& cardRect)
{
    // Position menu button at bottom-right of card
    // Align center horizontally with star indicator
    // Star: right edge at cardRect.right() - PADDING - 2, width = 18
    // Star center: cardRect.right() - PADDING - 2 - 9 = cardRect.right() - PADDING - 11
    int starCenterX = cardRect.right() - PADDING - 11;
    int x = starCenterX - MENU_BUTTON_SIZE / 2;
    int y = cardRect.bottom() - PADDING - MENU_BUTTON_SIZE + MENU_BUTTON_MARGIN;
    return QRect(x, y, MENU_BUTTON_SIZE, MENU_BUTTON_SIZE);
}

void NotebookCardDelegate::drawMenuButton(QPainter* painter, const QRect& buttonRect, 
                                           bool /*hovered*/) const
{
    // Note: We don't show hover effect here because delegates can't track
    // per-element hover state. The whole card hover is not useful for this.
    // The dots are always visible as a clickable affordance.
    
    // Draw three vertical dots (⋮)
    QColor dotColor = ThemeColors::textSecondary(m_darkMode);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor);
    
    int dotSize = 3;
    int dotSpacing = 5;
    int centerX = buttonRect.center().x();
    int centerY = buttonRect.center().y();
    
    // Top dot
    painter->drawEllipse(QPoint(centerX, centerY - dotSpacing), dotSize / 2, dotSize / 2);
    // Middle dot
    painter->drawEllipse(QPoint(centerX, centerY), dotSize / 2, dotSize / 2);
    // Bottom dot
    painter->drawEllipse(QPoint(centerX, centerY + dotSpacing), dotSize / 2, dotSize / 2);
}

void NotebookCardDelegate::drawSelectionIndicator(QPainter* painter, const QRect& cardRect,
                                                   bool isSelected) const
{
    // Position in top-left corner of card
    QRect indicatorRect(
        cardRect.left() + SELECTION_INDICATOR_MARGIN,
        cardRect.top() + SELECTION_INDICATOR_MARGIN,
        SELECTION_INDICATOR_SIZE,
        SELECTION_INDICATOR_SIZE
    );
    
    // Colors
    QColor fillColor = isSelected 
        ? ThemeColors::selectionBorder(m_darkMode)  // Blue when selected
        : ThemeColors::itemBackground(m_darkMode);   // Card background when not
    QColor borderColor = isSelected
        ? ThemeColors::selectionBorder(m_darkMode)
        : ThemeColors::textSecondary(m_darkMode);
    
    // Draw circle background
    painter->setPen(QPen(borderColor, 2));
    painter->setBrush(fillColor);
    painter->drawEllipse(indicatorRect);
    
    // Draw checkmark if selected
    if (isSelected) {
        painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        
        // Checkmark path within the circle
        int cx = indicatorRect.center().x();
        int cy = indicatorRect.center().y();
        int size = SELECTION_INDICATOR_SIZE / 3;
        
        // Draw checkmark: short line down-left, then long line down-right
        QPoint p1(cx - size, cy);           // Start (left)
        QPoint p2(cx - size/3, cy + size);  // Bottom of short stroke
        QPoint p3(cx + size, cy - size/2);  // End (top-right)
        
        painter->drawLine(p1, p2);
        painter->drawLine(p2, p3);
    }
}
