#pragma once

// ============================================================================
// NotesTreeDelegate — custom painter for the right-sidebar notes tree.
// ============================================================================
// Paints three row kinds based on the KindRole stored on each QTreeWidgetItem:
//   - Group (L1) — "Page N" / "Row N" header, right-aligned count badge.
//   - Link  (L2) — description + 4-px iconColor stripe + small slot count.
//   - Note  (L3) — iconColor stripe, title, dim first-line snippet (2x row).
//
// When a row has an inflated item-widget (the focused MarkdownNoteEntry),
// NotesTreePanel sets FocusedRole=true on the item; the delegate then paints
// only the row background and defers all visuals to the widget, so the
// built-in stripe/title/snippet don't bleed out from under the editor.
// ============================================================================

#include <QStyledItemDelegate>

class NotesTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum class Kind { Group = 0, Link = 1, Note = 2 };

    // Custom roles used on QTreeWidgetItem (must match NotesTreePanel).
    static constexpr int KindRole      = Qt::UserRole;
    static constexpr int ColorRole     = Qt::UserRole + 1;  ///< QColor (L2/L3 stripe)
    static constexpr int SnippetRole   = Qt::UserRole + 2;  ///< QString (L3 dim line)
    static constexpr int CountRole     = Qt::UserRole + 3;  ///< int (L1/L2 badge)
    static constexpr int MissingRole   = Qt::UserRole + 4;  ///< bool (L3 "missing" state)
    static constexpr int FocusedRole   = Qt::UserRole + 5;  ///< bool (row owns a persistent widget)

    explicit NotesTreeDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void setDarkMode(bool dark) { m_darkMode = dark; }

private:
    static constexpr int ROW_HEIGHT   = 36;   ///< L1/L2 compact height
    static constexpr int NOTE_HEIGHT  = 64;   ///< L3 compact height (~= 2x ROW_HEIGHT)
    static constexpr int PADDING      = 8;
    static constexpr int STRIPE_WIDTH = 4;

    bool m_darkMode = false;
};
