#include "NotebookCardDelegate.h"
#include "../../core/NotebookLibrary.h"
#include "../ThemeColors.h"

#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>

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
    
    // The rect from option is the cell rect - use it directly as card rect
    QRect cardRect = rect;
    
    // === Background ===
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
        painter->setPen(QPen(ThemeColors::selectionBorder(m_darkMode), 2));
    } else {
        painter->setPen(QPen(ThemeColors::cardBorder(m_darkMode), 1));
    }
    painter->drawPath(cardPath);
    
    // === Thumbnail area ===
    QRect thumbRect(cardRect.left() + PADDING, cardRect.top() + PADDING,
                    CARD_WIDTH - 2 * PADDING, THUMBNAIL_HEIGHT);
    
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
    int textWidth = CARD_WIDTH - 2 * PADDING;
    
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
    
    // === Type indicator ===
    int typeY = textY + 20;
    
    QFont typeFont = painter->font();
    typeFont.setPointSize(8);
    typeFont.setBold(false);
    painter->setFont(typeFont);
    
    bool isPdf = index.data(IsPdfBasedRole).toBool();
    bool isEdgeless = index.data(IsEdgelessRole).toBool();
    
    painter->setPen(typeIndicatorColor(isPdf, isEdgeless));
    
    QRect typeRect(cardRect.left() + PADDING, typeY, textWidth, 14);
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignTop, typeIndicatorText(isPdf, isEdgeless));
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
