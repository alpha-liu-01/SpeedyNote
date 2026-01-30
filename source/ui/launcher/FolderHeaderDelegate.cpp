#include "FolderHeaderDelegate.h"

#include <QPainter>

FolderHeaderDelegate::FolderHeaderDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void FolderHeaderDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    paintFolderHeader(painter, option.rect, option, index);
    
    painter->restore();
}

QSize FolderHeaderDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    
    // Folder headers span the full width of the view
    // Width will be set by the view, we just specify the height
    return QSize(100, HEADER_HEIGHT);
}

void FolderHeaderDelegate::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
    }
}

void FolderHeaderDelegate::paintFolderHeader(QPainter* painter, const QRect& rect,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const
{
    // Determine states from option
    bool pressed = option.state & QStyle::State_Sunken;
    bool hovered = option.state & QStyle::State_MouseOver;
    
    // === Background ===
    if (pressed || hovered) {
        QColor bgColor = backgroundColor(pressed, hovered);
        painter->fillRect(rect, bgColor);
    }
    
    // === Chevron (▶ or ▼) ===
    bool collapsed = index.data(IsCollapsedRole).toBool();
    
    QColor chevronColor = m_darkMode ? QColor(150, 150, 150) : QColor(100, 100, 100);
    painter->setPen(chevronColor);
    
    QFont chevronFont = painter->font();
    chevronFont.setPointSize(10);
    painter->setFont(chevronFont);
    
    QString chevron = collapsed ? "▶" : "▼";
    QRect chevronRect(rect.left() + CHEVRON_X, rect.top(), CHEVRON_WIDTH, rect.height());
    painter->drawText(chevronRect, Qt::AlignVCenter | Qt::AlignLeft, chevron);
    
    // === Folder name ===
    QString folderName = index.data(FolderNameRole).toString();
    if (folderName.isEmpty()) {
        folderName = index.data(Qt::DisplayRole).toString();
    }
    
    QColor textColor = m_darkMode ? QColor(220, 220, 220) : QColor(50, 50, 50);
    painter->setPen(textColor);
    
    QFont nameFont = painter->font();
    nameFont.setPointSize(14);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    
    QRect nameRect(rect.left() + NAME_X, rect.top(), 
                   rect.width() - NAME_X - NAME_MARGIN_RIGHT, rect.height());
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, folderName);
    
    // === Bottom separator line ===
    QColor lineColor = m_darkMode ? QColor(70, 70, 75) : QColor(220, 220, 225);
    painter->setPen(QPen(lineColor, 1));
    painter->drawLine(rect.left(), rect.bottom(), rect.right(), rect.bottom());
}

QColor FolderHeaderDelegate::backgroundColor(bool pressed, bool hovered) const
{
    if (pressed) {
        return m_darkMode ? QColor(60, 60, 65) : QColor(235, 235, 240);
    } else if (hovered) {
        return m_darkMode ? QColor(55, 55, 60) : QColor(245, 245, 248);
    }
    return Qt::transparent;
}
