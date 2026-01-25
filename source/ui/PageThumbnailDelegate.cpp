#include "PageThumbnailDelegate.h"
#include "PageThumbnailModel.h"

#include <QPainter>
#include <QPainterPath>
#include <QApplication>

// ============================================================================
// Constructor
// ============================================================================

PageThumbnailDelegate::PageThumbnailDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

// ============================================================================
// Size Hint
// ============================================================================

QSize PageThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const
{
    Q_UNUSED(option);
    
    // Get the actual page's aspect ratio from model, or use default
    qreal aspectRatio = m_pageAspectRatio;
    if (index.isValid()) {
        QVariant ratioVar = index.data(PageThumbnailModel::PageAspectRatioRole);
        if (ratioVar.isValid()) {
            aspectRatio = ratioVar.toReal();
        }
    }
    
    // Calculate thumbnail height from width and actual page aspect ratio
    const int thumbHeight = static_cast<int>(m_thumbnailWidth * aspectRatio);
    
    // Total item height: padding + thumbnail + spacing + page number + padding
    const int totalHeight = VERTICAL_PADDING + thumbHeight + ITEM_SPACING + 
                            PAGE_NUMBER_HEIGHT + VERTICAL_PADDING;
    
    // Width: thumbnail width + horizontal padding on both sides
    const int totalWidth = HORIZONTAL_PADDING + m_thumbnailWidth + HORIZONTAL_PADDING;
    
    return QSize(totalWidth, totalHeight);
}

// ============================================================================
// Paint
// ============================================================================

void PageThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    
    // Get data from model
    const int pageIndex = index.data(PageThumbnailModel::PageIndexRole).toInt();
    const QPixmap thumbnail = index.data(PageThumbnailModel::ThumbnailRole).value<QPixmap>();
    const bool isCurrentPage = index.data(PageThumbnailModel::IsCurrentPageRole).toBool();
    const bool isPdfPage = index.data(PageThumbnailModel::IsPdfPageRole).toBool();
    
    // Get the actual page's aspect ratio from model
    qreal aspectRatio = m_pageAspectRatio;
    QVariant ratioVar = index.data(PageThumbnailModel::PageAspectRatioRole);
    if (ratioVar.isValid()) {
        aspectRatio = ratioVar.toReal();
    }
    
    // Determine state
    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;
    
    // Calculate thumbnail dimensions using actual page aspect ratio
    const int thumbHeight = static_cast<int>(m_thumbnailWidth * aspectRatio);
    
    // Calculate thumbnail rect (centered horizontally in the item)
    const int thumbX = option.rect.left() + (option.rect.width() - m_thumbnailWidth) / 2;
    const int thumbY = option.rect.top() + VERTICAL_PADDING;
    const QRect thumbRect(thumbX, thumbY, m_thumbnailWidth, thumbHeight);
    
    // Calculate page number rect
    const int pageNumY = thumbRect.bottom() + ITEM_SPACING;
    const QRect pageNumRect(option.rect.left(), pageNumY, 
                            option.rect.width(), PAGE_NUMBER_HEIGHT);
    
    // 1. Draw background (for selection/hover feedback)
    if (isSelected || isHovered) {
        painter->fillRect(option.rect, backgroundColor(isSelected, isHovered));
    }
    
    // 2. Draw thumbnail or placeholder
    if (!thumbnail.isNull()) {
        // Draw the actual thumbnail with rounded corners
        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbRect, BORDER_RADIUS, BORDER_RADIUS);
        
        painter->save();
        painter->setClipPath(clipPath);
        
        // Use drawPixmap(rect, pixmap) which properly handles device pixel ratio
        // This scales the thumbnail to fit the rect while respecting DPR
        painter->drawPixmap(thumbRect, thumbnail);
        
        painter->restore();
    } else {
        // Draw placeholder
        drawPlaceholder(painter, thumbRect, isPdfPage);
    }
    
    // 3. Draw border
    drawBorder(painter, thumbRect, isCurrentPage, isSelected, isHovered);
    
    // 4. Draw page number
    painter->setPen(textColor());
    QFont font = option.font;
    font.setPixelSize(12);
    painter->setFont(font);
    
    const QString pageText = tr("Page %1").arg(pageIndex + 1);
    painter->drawText(pageNumRect, Qt::AlignHCenter | Qt::AlignTop, pageText);
    
    painter->restore();
}

// ============================================================================
// Settings
// ============================================================================

void PageThumbnailDelegate::setThumbnailWidth(int width)
{
    if (width > 0) {
        m_thumbnailWidth = width;
    }
}

void PageThumbnailDelegate::setDarkMode(bool dark)
{
    m_darkMode = dark;
}

void PageThumbnailDelegate::setPageAspectRatio(qreal ratio)
{
    if (ratio > 0.1 && ratio < 10.0) {
        m_pageAspectRatio = ratio;
    }
}

QRect PageThumbnailDelegate::thumbnailRect(const QRect& itemRect, qreal aspectRatio) const
{
    // Use provided aspect ratio or default
    if (aspectRatio < 0) {
        aspectRatio = m_pageAspectRatio;
    }
    
    // Calculate thumbnail height from width and aspect ratio
    const int thumbHeight = static_cast<int>(m_thumbnailWidth * aspectRatio);
    
    // Calculate thumbnail rect (centered horizontally in the item)
    const int thumbX = itemRect.left() + (itemRect.width() - m_thumbnailWidth) / 2;
    const int thumbY = itemRect.top() + VERTICAL_PADDING;
    
    return QRect(thumbX, thumbY, m_thumbnailWidth, thumbHeight);
}

// ============================================================================
// Private Helpers
// ============================================================================

void PageThumbnailDelegate::drawPlaceholder(QPainter* painter, const QRect& thumbRect,
                                             bool isPdfPage) const
{
    // Draw rounded rectangle background
    QPainterPath path;
    path.addRoundedRect(thumbRect, BORDER_RADIUS, BORDER_RADIUS);
    
    painter->fillPath(path, placeholderColor(isPdfPage));
    
    // Draw a subtle "loading" indicator - three dots in the center
    painter->setPen(Qt::NoPen);
    QColor dotColor = m_darkMode ? QColor(100, 100, 100) : QColor(180, 180, 180);
    painter->setBrush(dotColor);
    
    const int dotSize = 6;
    const int dotSpacing = 12;
    const int totalWidth = dotSize * 3 + dotSpacing * 2;
    const int startX = thumbRect.center().x() - totalWidth / 2;
    const int y = thumbRect.center().y();
    
    for (int i = 0; i < 3; ++i) {
        const int x = startX + i * (dotSize + dotSpacing);
        painter->drawEllipse(QPoint(x + dotSize / 2, y), dotSize / 2, dotSize / 2);
    }
}

void PageThumbnailDelegate::drawBorder(QPainter* painter, const QRect& thumbRect,
                                        bool isCurrentPage, bool isSelected, bool isHovered) const
{
    Q_UNUSED(isSelected);
    Q_UNUSED(isHovered);
    
    // Determine border style
    const int borderWidth = isCurrentPage ? BORDER_WIDTH_CURRENT : BORDER_WIDTH_NORMAL;
    const QColor borderColor = isCurrentPage ? accentColor() : neutralBorderColor();
    
    // Draw border
    QPen pen(borderColor);
    pen.setWidth(borderWidth);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    
    // Inset rect by half border width for proper drawing
    const qreal inset = borderWidth / 2.0;
    QRectF borderRect = QRectF(thumbRect).adjusted(inset, inset, -inset, -inset);
    
    painter->drawRoundedRect(borderRect, BORDER_RADIUS, BORDER_RADIUS);
}

QColor PageThumbnailDelegate::accentColor() const
{
    // Use a nice blue accent color (consistent with the app theme)
    if (m_darkMode) {
        return QColor(100, 149, 237);  // Cornflower blue
    } else {
        return QColor(66, 133, 244);   // Google blue
    }
}

QColor PageThumbnailDelegate::neutralBorderColor() const
{
    if (m_darkMode) {
        return QColor(80, 80, 80);
    } else {
        return QColor(200, 200, 200);
    }
}

QColor PageThumbnailDelegate::placeholderColor(bool isPdfPage) const
{
    if (isPdfPage) {
        // Gray for PDF pages
        if (m_darkMode) {
            return QColor(50, 50, 55);
        } else {
            return QColor(230, 230, 235);
        }
    } else {
        // Off-white for regular pages (simulating paper)
        if (m_darkMode) {
            return QColor(55, 55, 50);
        } else {
            return QColor(250, 250, 245);
        }
    }
}

QColor PageThumbnailDelegate::textColor() const
{
    if (m_darkMode) {
        return QColor(200, 200, 200);
    } else {
        return QColor(80, 80, 80);
    }
}

QColor PageThumbnailDelegate::backgroundColor(bool isSelected, bool isHovered) const
{
    if (m_darkMode) {
        if (isSelected) {
            return QColor(60, 60, 65);
        } else if (isHovered) {
            return QColor(50, 50, 55);
        } else {
            return Qt::transparent;
        }
    } else {
        if (isSelected) {
            return QColor(230, 240, 250);
        } else if (isHovered) {
            return QColor(240, 245, 250);
        } else {
            return Qt::transparent;
        }
    }
}

