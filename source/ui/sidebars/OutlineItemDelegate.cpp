#include "OutlineItemDelegate.h"

#include <QPainter>
#include <QApplication>

// ============================================================================
// Constructor
// ============================================================================

OutlineItemDelegate::OutlineItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

// ============================================================================
// Size Hint
// ============================================================================

QSize OutlineItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    
    // Fixed row height for touch-friendly interaction
    return QSize(100, ROW_HEIGHT);
}

// ============================================================================
// Paint
// ============================================================================

void OutlineItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // Get data
    QString title = index.data(Qt::DisplayRole).toString();
    int pageNumber = index.data(PageRole).toInt();
    bool hasPage = pageNumber >= 0;

    // Determine colors based on state and theme
    QColor bgColor;
    QColor textColor;
    QColor pageColor;
    QColor dotColor;

    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;

    // Unified gray colors: dark #2a2e32/#3a3e42/#4d4d4d, light #F5F5F5/#E8E8E8/#D0D0D0
    if (m_darkMode) {
        textColor = QColor("#E0E0E0");
        pageColor = QColor("#A0A0A0");
        dotColor = QColor("#4d4d4d");
        
        if (isSelected) {
            bgColor = QColor("#4d4d4d");
        } else if (isHovered) {
            bgColor = QColor("#3a3e42");
        } else {
            bgColor = QColor("#2a2e32");
        }
    } else {
        textColor = QColor("#333333");
        pageColor = QColor("#666666");
        dotColor = QColor("#D0D0D0");
        
        if (isSelected) {
            bgColor = QColor("#D0D0D0");
        } else if (isHovered) {
            bgColor = QColor("#E8E8E8");
        } else {
            bgColor = QColor("#F5F5F5");
        }
    }

    // 1. Draw background
    painter->fillRect(option.rect, bgColor);

    // 2. Calculate layout
    QRect contentRect = option.rect.adjusted(PADDING, 0, -PADDING, 0);
    
    // Page number area (right side)
    QString pageStr = hasPage ? QString::number(pageNumber + 1) : "";  // Display 1-based
    QRect pageRect = contentRect;
    pageRect.setLeft(contentRect.right() - PAGE_NUMBER_WIDTH);

    // Title area (left side, leaving space for page number)
    QRect titleRect = contentRect;
    titleRect.setRight(pageRect.left() - PADDING);

    // 3. Draw title text (elided if needed)
    QFont titleFont = option.font;
    painter->setFont(titleFont);
    painter->setPen(textColor);
    
    QFontMetrics fm(titleFont);
    int textWidth = fm.horizontalAdvance(title);
    int availableWidth = titleRect.width();
    
    QString displayTitle = title;
    int actualTextWidth = textWidth;
    
    if (textWidth > availableWidth) {
        // Need to elide
        displayTitle = fm.elidedText(title, Qt::ElideRight, availableWidth);
        actualTextWidth = fm.horizontalAdvance(displayTitle);
    }
    
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, displayTitle);

    // 4. Draw leader dots (between title and page number)
    if (hasPage) {
        int dotsStart = titleRect.left() + actualTextWidth + DOT_SPACING;
        int dotsEnd = pageRect.left() - DOT_SPACING;
        
        if (dotsEnd > dotsStart + DOT_SPACING * 2) {
            painter->setPen(dotColor);
            
            // Calculate dot positions
            int y = contentRect.center().y();
            int dotSize = 2;
            
            for (int x = dotsStart; x < dotsEnd; x += DOT_SPACING + dotSize) {
                painter->drawEllipse(QPoint(x, y), dotSize / 2, dotSize / 2);
            }
        }

        // 5. Draw page number (right-aligned)
        painter->setPen(pageColor);
        painter->drawText(pageRect, Qt::AlignRight | Qt::AlignVCenter, pageStr);
    }

    painter->restore();
}

