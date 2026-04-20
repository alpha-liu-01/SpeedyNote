#include "NotesTreeDelegate.h"

#include <QFontMetrics>
#include <QPainter>

// ============================================================================
// Constructor
// ============================================================================

NotesTreeDelegate::NotesTreeDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

// ============================================================================
// sizeHint — L3 rows are ~2x so they can fit a title + snippet line.
// ============================================================================

QSize NotesTreeDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    Q_UNUSED(option);
    const Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
    const int h = (kind == Kind::Note) ? NOTE_HEIGHT : ROW_HEIGHT;
    return QSize(100, h);
}

// ============================================================================
// paint
// ============================================================================

void NotesTreeDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // ---- Palette (match OutlineItemDelegate gray tones) ----
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered  = option.state & QStyle::State_MouseOver;

    QColor bg, text, dim, stripeFallback;
    if (m_darkMode) {
        text           = QColor("#E0E0E0");
        dim            = QColor("#A0A0A0");
        stripeFallback = QColor("#6a6e72");
        if (selected)      bg = QColor("#4d4d4d");
        else if (hovered)  bg = QColor("#3a3e42");
        else               bg = QColor("#2a2e32");
    } else {
        text           = QColor("#333333");
        dim            = QColor("#666666");
        stripeFallback = QColor("#C0C0C0");
        if (selected)      bg = QColor("#D0D0D0");
        else if (hovered)  bg = QColor("#E8E8E8");
        else               bg = QColor("#F5F5F5");
    }

    painter->fillRect(option.rect, bg);

    const Kind kind  = static_cast<Kind>(index.data(KindRole).toInt());
    QRect content    = option.rect.adjusted(PADDING, 0, -PADDING, 0);
    const QString title = index.data(Qt::DisplayRole).toString();

    switch (kind) {
    case Kind::Group: {
        // Bold header + right-aligned note count.
        QFont f = option.font;
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(text);

        const int count    = index.data(CountRole).toInt();
        const QString badge = count > 0 ? QString::number(count) : QString();
        QFontMetrics fm(f);
        const int badgeW = badge.isEmpty()
                         ? 0
                         : fm.horizontalAdvance(badge) + PADDING;

        QRect titleRect = content;
        titleRect.setRight(titleRect.right() - badgeW);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(title, Qt::ElideRight, titleRect.width()));

        if (!badge.isEmpty()) {
            painter->setPen(dim);
            painter->drawText(content, Qt::AlignRight | Qt::AlignVCenter, badge);
        }
        break;
    }
    case Kind::Link: {
        // Color stripe + description + slot-count badge.
        QColor stripe = index.data(ColorRole).value<QColor>();
        if (!stripe.isValid()) stripe = stripeFallback;

        QRect stripeRect(content.left(), content.top() + 4,
                         STRIPE_WIDTH, content.height() - 8);
        painter->fillRect(stripeRect, stripe);

        QRect textRect = content.adjusted(STRIPE_WIDTH + PADDING, 0, 0, 0);
        const int count  = index.data(CountRole).toInt();
        const QString badge = count > 0 ? QString::number(count) : QString();
        QFontMetrics fm(option.font);
        const int badgeW = badge.isEmpty()
                         ? 0
                         : fm.horizontalAdvance(badge) + PADDING;

        QRect labelRect = textRect;
        labelRect.setRight(labelRect.right() - badgeW);

        painter->setFont(option.font);
        painter->setPen(text);
        const QString display = title.isEmpty()
                              ? QStringLiteral("(untitled link)")
                              : title;
        painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(display, Qt::ElideRight, labelRect.width()));

        if (!badge.isEmpty()) {
            painter->setPen(dim);
            painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, badge);
        }
        break;
    }
    case Kind::Note: {
        // Stripe + 2-line layout (title / snippet).
        const bool missing = index.data(MissingRole).toBool();
        QColor stripe = index.data(ColorRole).value<QColor>();
        if (!stripe.isValid()) stripe = stripeFallback;
        if (missing) stripe = dim;

        QRect stripeRect(content.left(), content.top() + 6,
                         STRIPE_WIDTH, content.height() - 12);
        painter->fillRect(stripeRect, stripe);

        QRect textRect = content.adjusted(STRIPE_WIDTH + PADDING, 4, 0, -4);
        const int half = textRect.height() / 2;

        QFont titleFont = option.font;
        titleFont.setBold(true);
        QFontMetrics tfm(titleFont);
        QRect titleRect = textRect;
        titleRect.setHeight(half);

        QString displayTitle = title;
        if (missing && displayTitle.isEmpty()) {
            displayTitle = QStringLiteral("(missing)");
        } else if (displayTitle.isEmpty()) {
            displayTitle = QStringLiteral("(untitled)");
        }

        painter->setFont(titleFont);
        painter->setPen(missing ? dim : text);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          tfm.elidedText(displayTitle, Qt::ElideRight, titleRect.width()));

        QRect snippetRect = textRect;
        snippetRect.setTop(titleRect.bottom());
        QString snippet = index.data(SnippetRole).toString();
        if (!snippet.isEmpty()) {
            QFontMetrics sfm(option.font);
            painter->setFont(option.font);
            painter->setPen(dim);
            painter->drawText(snippetRect, Qt::AlignLeft | Qt::AlignVCenter,
                              sfm.elidedText(snippet, Qt::ElideRight, snippetRect.width()));
        }
        break;
    }
    }

    painter->restore();
}
