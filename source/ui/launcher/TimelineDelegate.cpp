#include "TimelineDelegate.h"
#include "TimelineModel.h"
#include "../ThemeColors.h"

#include <QPainter>

TimelineDelegate::TimelineDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void TimelineDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    // This delegate only handles section headers
    // Notebook cards are handled by NotebookCardDelegate via CompositeTimelineDelegate
    bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
    
    if (isHeader) {
        QString title = index.data(Qt::DisplayRole).toString();
        paintSectionHeader(painter, option.rect, title);
    }
    // Non-header items should be dispatched to NotebookCardDelegate by the composite
    
    painter->restore();
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    
    // This delegate only provides sizeHint for section headers
    // The CompositeTimelineDelegate handles the full width calculation
    return QSize(100, HEADER_HEIGHT);
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
    QColor textColor = ThemeColors::textSecondary(m_darkMode);
    QColor lineColor = ThemeColors::separator(m_darkMode);
    
    // Draw text
    QFont font = painter->font();
    font.setPointSize(11);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(textColor);
    
    QRect textRect = rect.adjusted(HEADER_PADDING, 0, -HEADER_PADDING, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
    
    // Draw underline
    int textWidth = painter->fontMetrics().horizontalAdvance(title);
    int lineY = rect.bottom() - 2;
    painter->setPen(QPen(lineColor, 1));
    painter->drawLine(textRect.left(), lineY, 
                     textRect.left() + textWidth + 20, lineY);
}
