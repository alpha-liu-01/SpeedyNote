#include "NotesTreePanel.h"

#include "NotesTreeDelegate.h"
#include "NotesTreeWidget.h"

#include "../../core/MarkdownNote.h"
#include "../../text/MarkdownNoteEntry.h"

#include <QCoreApplication>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

// ----------------------------------------------------------------------------
// Item-data roles used to route tree actions back to the outline.
// NotesTreeDelegate declares paint-side roles (Kind/Color/Snippet/Count/Missing).
// We reserve additional roles here for navigation metadata.
// ----------------------------------------------------------------------------
namespace {
constexpr int PageIndexRole = Qt::UserRole + 10;  ///< int (L1 paged)
constexpr int TileYRole     = Qt::UserRole + 11;  ///< int (L1 edgeless)
constexpr int LinkIdRole    = Qt::UserRole + 12;  ///< QString (L2, L3)
constexpr int NoteIdRole    = Qt::UserRole + 13;  ///< QString (L3)
constexpr int SlotIndexRole = Qt::UserRole + 14;  ///< int (L3)
} // namespace

// ============================================================================
// Construction
// ============================================================================

NotesTreePanel::NotesTreePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    applyStyle();
}

NotesTreePanel::~NotesTreePanel() = default;

void NotesTreePanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new NotesTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(20);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(false);  // L3 is taller than L1/L2
    m_tree->setMouseTracking(true);
    m_tree->viewport()->setMouseTracking(true);
    m_tree->setAttribute(Qt::WA_Hover, true);
    m_tree->viewport()->setAttribute(Qt::WA_Hover, true);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_delegate = new NotesTreeDelegate(this);
    m_tree->setItemDelegate(m_delegate);

    connect(m_tree, &QTreeWidget::itemClicked,
            this,   &NotesTreePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemExpanded,
            this,   &NotesTreePanel::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed,
            this,   &NotesTreePanel::onItemCollapsed);

    layout->addWidget(m_tree);
}

// ============================================================================
// Theme
// ============================================================================

void NotesTreePanel::setDarkMode(bool dark)
{
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    if (m_delegate) m_delegate->setDarkMode(dark);
    applyStyle();
    if (m_tree) m_tree->viewport()->update();
}

void NotesTreePanel::applyStyle()
{
    if (!m_tree) return;
    // Background matches the rest of the dark markdown sidebar (#2d2d2d).
    const QString bg  = m_darkMode ? QStringLiteral("#2d2d2d") : QStringLiteral("#F5F5F5");
    const QString rev = m_darkMode ? QStringLiteral("_reversed") : QString();
    m_tree->setStyleSheet(QStringLiteral(R"(
        QTreeWidget {
            background-color: %1;
            border: none;
            outline: none;
        }
        QTreeWidget::branch {
            background-color: %1;
        }
        QTreeWidget::branch:has-children:!has-siblings:closed,
        QTreeWidget::branch:closed:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/right_arrow%2.png);
        }
        QTreeWidget::branch:open:has-children:!has-siblings,
        QTreeWidget::branch:open:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/down_arrow%2.png);
        }
    )").arg(bg, rev));
}

// ============================================================================
// setOutline — the core rebuild entry point.
// ============================================================================

void NotesTreePanel::setOutline(const QVector<LinkOutlineEntry>& entries,
                                bool edgeless)
{
    // Remember expansion + focus for optimistic restoration.
    QSet<QString> preserveExpanded = m_expandedLinks;
    QString       preserveFocusLink = m_focusedLinkId;
    QString       preserveFocusNote = m_focusedNoteId;

    m_outline  = entries;
    m_edgeless = edgeless;

    m_linkIndex.clear();
    m_linkIndex.reserve(m_outline.size());
    for (int i = 0; i < m_outline.size(); ++i) {
        m_linkIndex.insert(m_outline[i].linkObjectId, i);
    }

    rebuildTree();

    // Re-apply expansion.
    for (const QString& id : preserveExpanded) {
        if (auto* item = linkItem(id)) {
            item->setExpanded(true);  // triggers populateLinkChildren
        }
    }

    // Re-focus if the note still exists.
    if (!preserveFocusLink.isEmpty() && !preserveFocusNote.isEmpty()) {
        auto it = m_linkIndex.find(preserveFocusLink);
        if (it != m_linkIndex.end()) {
            const LinkOutlineEntry& e = m_outline[*it];
            int slot = -1;
            for (const auto& s : e.markdownSlots) {
                if (s.noteId == preserveFocusNote) { slot = s.slotIndex; break; }
            }
            if (slot >= 0) openNote(preserveFocusLink, slot);
        }
    }

    applyRangeFilter();
}

void NotesTreePanel::clear()
{
    focusNote(nullptr);            // destroys the inflated editor, if any
    m_tree->clear();
    m_outline.clear();
    m_linkIndex.clear();
    m_linkItems.clear();
    m_expandedLinks.clear();
    m_lastHighlightedPage = -1;
    m_filterFromTileY = m_filterToTileY = -1;
}

// ============================================================================
// Tree rebuild — groups by page/tileY, sorts L2 by (y, x) within each group.
// ============================================================================

void NotesTreePanel::rebuildTree()
{
    // Destroy existing widget state before nuking the tree.
    focusNote(nullptr);
    m_tree->clear();
    m_linkItems.clear();

    if (m_outline.isEmpty()) return;

    // Group entries by page (paged) or tileY (edgeless).
    // Use a sorted map so groups come out top-down.
    std::map<int, QVector<int>> groups;  // groupKey → indices into m_outline
    for (int i = 0; i < m_outline.size(); ++i) {
        const int key = m_edgeless ? m_outline[i].tileY
                                   : m_outline[i].pageIndex;
        groups[key].append(i);
    }

    for (auto& [key, idxs] : groups) {
        // Sort links within a group by docPos (y then x).
        std::sort(idxs.begin(), idxs.end(),
                  [this](int a, int b) {
                      const QPointF& pa = m_outline[a].docPos;
                      const QPointF& pb = m_outline[b].docPos;
                      if (pa.y() != pb.y()) return pa.y() < pb.y();
                      return pa.x() < pb.x();
                  });

        auto* group = new QTreeWidgetItem(m_tree);
        group->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
        group->setData(0, NotesTreeDelegate::KindRole,
                       static_cast<int>(NotesTreeDelegate::Kind::Group));

        int totalSlots = 0;
        for (int idx : idxs) totalSlots += m_outline[idx].markdownSlots.size();
        group->setData(0, NotesTreeDelegate::CountRole, totalSlots);

        if (m_edgeless) {
            group->setText(0, QCoreApplication::translate(
                                   "NotesTreePanel", "Row %1").arg(key + 1));
            group->setData(0, TileYRole, key);
        } else {
            group->setText(0, QCoreApplication::translate(
                                   "NotesTreePanel", "Page %1").arg(key + 1));
            group->setData(0, PageIndexRole, key);
        }

        for (int idx : idxs) {
            const LinkOutlineEntry& entry = m_outline[idx];

            auto* link = new QTreeWidgetItem(group);
            link->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            link->setData(0, NotesTreeDelegate::KindRole,
                          static_cast<int>(NotesTreeDelegate::Kind::Link));
            link->setData(0, NotesTreeDelegate::ColorRole, entry.iconColor);
            link->setData(0, NotesTreeDelegate::CountRole,
                          entry.markdownSlots.size());
            link->setData(0, LinkIdRole, entry.linkObjectId);

            const QString desc = entry.description.isEmpty()
                ? QCoreApplication::translate(
                      "NotesTreePanel", "(no description)")
                : entry.description;
            link->setText(0, desc);
            link->setToolTip(0, entry.description);

            m_linkItems.insert(entry.linkObjectId, link);
        }
        group->setExpanded(true);  // L1 groups open by default
    }
}

// ============================================================================
// Lazy L2 population
// ============================================================================

void NotesTreePanel::populateLinkChildren(QTreeWidgetItem* linkItem)
{
    if (!linkItem) return;
    if (linkItem->childCount() > 0) return;  // already populated

    const QString linkId = linkItem->data(0, LinkIdRole).toString();
    auto it = m_linkIndex.find(linkId);
    if (it == m_linkIndex.end()) return;
    const LinkOutlineEntry& entry = m_outline[*it];

    for (const LinkOutlineSlot& slot : entry.markdownSlots) {
        auto* note = new QTreeWidgetItem(linkItem);
        note->setData(0, NotesTreeDelegate::KindRole,
                      static_cast<int>(NotesTreeDelegate::Kind::Note));
        note->setData(0, NotesTreeDelegate::ColorRole, entry.iconColor);
        note->setData(0, LinkIdRole,    entry.linkObjectId);
        note->setData(0, NoteIdRole,    slot.noteId);
        note->setData(0, SlotIndexRole, slot.slotIndex);

        // Preview (lightweight — no full readAll).
        MarkdownNotePreview preview;
        if (!m_notesDir.isEmpty()) {
            preview = MarkdownNote::loadPreviewFromFile(
                m_notesDir + QStringLiteral("/") + slot.noteId
                           + QStringLiteral(".md"));
        }

        if (!preview.isValid()) {
            note->setText(0, QCoreApplication::translate(
                                 "NotesTreePanel", "(missing note)"));
            note->setData(0, NotesTreeDelegate::MissingRole, true);
            note->setData(0, NotesTreeDelegate::SnippetRole,
                          slot.noteId.left(8));
        } else {
            QString title = preview.title;
            if (title.isEmpty()) {
                title = QCoreApplication::translate(
                            "NotesTreePanel", "Untitled");
            }
            note->setText(0, title);
            note->setData(0, NotesTreeDelegate::SnippetRole, preview.snippet);
            note->setData(0, NotesTreeDelegate::MissingRole, false);
        }
    }
}

void NotesTreePanel::releaseLinkChildren(QTreeWidgetItem* linkItem)
{
    if (!linkItem) return;
    // Drop the focused editor first if it lives inside this subtree.
    if (m_focusedItem) {
        for (QTreeWidgetItem* p = m_focusedItem->parent(); p; p = p->parent()) {
            if (p == linkItem) { focusNote(nullptr); break; }
        }
    }
    while (linkItem->childCount() > 0) {
        delete linkItem->takeChild(0);
    }
}

// ============================================================================
// Focus management — the single-alive-editor invariant.
// ============================================================================

void NotesTreePanel::focusNote(QTreeWidgetItem* noteItem)
{
    if (m_focusedItem == noteItem) return;

    // Tear down the previous editor (if any).
    // Note: QTreeWidget::removeItemWidget() deletes the widget it owns.
    // We must NOT deleteLater() afterwards, hence the single-path teardown.
    if (m_focusedItem) {
        // Drop any signal connections back to this panel before the widget
        // gets destroyed.  Keeps `updateFocusedItemSizeHint()` from firing
        // against a half-torn-down state.
        if (m_focusedEntry) {
            disconnect(m_focusedEntry.data(), nullptr, this, nullptr);
        }
        m_tree->removeItemWidget(m_focusedItem, 0);
        m_focusedEntry = nullptr;
        // Restore delegate painting + compact sizeHint so the row renders as
        // a normal compact L3 again.
        m_focusedItem->setData(0, NotesTreeDelegate::FocusedRole, false);
        m_focusedItem->setData(0, Qt::SizeHintRole, QVariant());
        m_focusedItem = nullptr;
        m_focusedLinkId.clear();
        m_focusedNoteId.clear();
    }

    if (!noteItem) return;

    const QString linkId = noteItem->data(0, LinkIdRole).toString();
    const QString noteId = noteItem->data(0, NoteIdRole).toString();
    if (linkId.isEmpty() || noteId.isEmpty() || m_notesDir.isEmpty()) return;

    auto it = m_linkIndex.find(linkId);
    if (it == m_linkIndex.end()) return;
    const LinkOutlineEntry& entry = m_outline[*it];

    // Load the full note once.  This is intentionally the only place in the
    // panel that calls loadFromFile().
    const QString path = m_notesDir + QStringLiteral("/") + noteId
                                    + QStringLiteral(".md");
    const MarkdownNote note = MarkdownNote::loadFromFile(path);

    NoteDisplayData data;
    data.noteId       = noteId;
    data.title        = note.title;
    data.content      = note.content;
    data.linkObjectId = entry.linkObjectId;
    data.color        = entry.iconColor;
    data.description  = entry.description;

    m_focusedEntry = new MarkdownNoteEntry(data);
    // Cache IDs now so setOutline() can find the note to re-focus even if the
    // item pointer becomes invalid during rebuild.
    m_focusedItem   = noteItem;
    m_focusedLinkId = linkId;
    m_focusedNoteId = noteId;

    // Forward the editor's signals unchanged.
    connect(m_focusedEntry, &MarkdownNoteEntry::contentChanged,
            this, [this](const QString& id) {
        if (m_focusedEntry && m_focusedEntry->getNoteId() == id) {
            emit noteContentSaved(id,
                                  m_focusedEntry->getTitle(),
                                  m_focusedEntry->getContent());
        }
    });
    connect(m_focusedEntry, &MarkdownNoteEntry::deleteWithLinkRequested,
            this, [this](const QString& nId, const QString& lId) {
        emit noteDeletedWithLink(nId, lId);
    });
    connect(m_focusedEntry, &MarkdownNoteEntry::linkObjectClicked,
            this, [this](const QString& lId) {
        emit navigateToLinkObject(lId);
    });

    // Keep the row height in sync with the entry's true layout size.
    //  * Entry-side `layoutMetricsChanged` fires whenever intrinsic height
    //    changes (preview auto-resize, preview<->edit swap).
    //  * An immediate call covers the initial state.
    //  * A QTimer::singleShot(0, ...) fallback handles the typical case where
    //    `previewBrowser->viewport()->width()` is still 0 at this point; once
    //    the first paint lays the widget out, `adjustPreviewHeight` runs and
    //    will emit the signal again, but the timer guarantees we don't stay
    //    stuck on a too-small initial hint even for empty notes.
    connect(m_focusedEntry, &MarkdownNoteEntry::layoutMetricsChanged,
            this, &NotesTreePanel::updateFocusedItemSizeHint);

    // Tell the delegate to stand down for this row — the widget owns the paint.
    // Must be set before setItemWidget() so the very first repaint already
    // skips the built-in stripe.
    noteItem->setData(0, NotesTreeDelegate::FocusedRole, true);
    m_tree->setItemWidget(noteItem, 0, m_focusedEntry);
    updateFocusedItemSizeHint();
    QTimer::singleShot(0, this, &NotesTreePanel::updateFocusedItemSizeHint);
    m_tree->scrollToItem(noteItem, QAbstractItemView::EnsureVisible);
}

void NotesTreePanel::updateFocusedItemSizeHint()
{
    if (!m_focusedItem || !m_focusedEntry || !m_tree) return;
    const int w = qMax(1, m_tree->viewport()->width());
    const int h = qMax(m_focusedEntry->minimumSizeHint().height(),
                       m_focusedEntry->sizeHint().height());
    const QSize wanted(w, h);
    if (m_focusedItem->sizeHint(0) != wanted) {
        // The delegate's overridden sizeHint() honors SizeHintRole, so this
        // drives the row height directly for compact rows.  For persistent
        // widgets QTreeView also takes max(delegate, widget.sizeHint) — both
        // return the same value here, so the row matches the widget exactly.
        m_focusedItem->setSizeHint(0, wanted);
    }

    // Force the persistent widget's geometry to the current visual rect.
    // dataChanged(SizeHintRole) invalidates the row-height cache, but Qt
    // only re-runs updateEditorGeometries() on the next layout pass, so the
    // widget can be transiently sized to the *old* row.  Pinning the widget
    // here guarantees the editor covers the full new row immediately —
    // critical to stop the delegate's #4d4d4d selection fill from showing
    // under a not-yet-stretched widget.  Skip the call when the widget is
    // already at the right rect to avoid spurious repaints from the flood
    // of layoutMetricsChanged signals during preview auto-resize.
    const QRect r = m_tree->visualItemRect(m_focusedItem);
    if (r.isValid() && r.height() > 0 && m_focusedEntry->geometry() != r) {
        m_focusedEntry->setGeometry(r);
    }
}

// ============================================================================
// Event handlers
// ============================================================================

void NotesTreePanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    const auto kind = static_cast<NotesTreeDelegate::Kind>(
        item->data(0, NotesTreeDelegate::KindRole).toInt());

    switch (kind) {
    case NotesTreeDelegate::Kind::Group:
        if (m_edgeless) {
            emit navigateToTileRow(item->data(0, TileYRole).toInt());
        } else {
            emit navigateToPage(item->data(0, PageIndexRole).toInt());
        }
        break;
    case NotesTreeDelegate::Kind::Link:
        emit navigateToLinkObject(item->data(0, LinkIdRole).toString());
        break;
    case NotesTreeDelegate::Kind::Note:
        focusNote(item);
        break;
    }
}

void NotesTreePanel::onItemExpanded(QTreeWidgetItem* item)
{
    if (!item) return;
    if (static_cast<NotesTreeDelegate::Kind>(
            item->data(0, NotesTreeDelegate::KindRole).toInt())
        != NotesTreeDelegate::Kind::Link) return;

    populateLinkChildren(item);
    const QString linkId = item->data(0, LinkIdRole).toString();
    if (!linkId.isEmpty()) m_expandedLinks.insert(linkId);
}

void NotesTreePanel::onItemCollapsed(QTreeWidgetItem* item)
{
    if (!item) return;
    if (static_cast<NotesTreeDelegate::Kind>(
            item->data(0, NotesTreeDelegate::KindRole).toInt())
        != NotesTreeDelegate::Kind::Link) return;

    releaseLinkChildren(item);
    const QString linkId = item->data(0, LinkIdRole).toString();
    if (!linkId.isEmpty()) m_expandedLinks.remove(linkId);
}

// ============================================================================
// Navigation + lookup helpers
// ============================================================================

QTreeWidgetItem* NotesTreePanel::linkItem(const QString& linkObjectId) const
{
    auto it = m_linkItems.constFind(linkObjectId);
    if (it == m_linkItems.constEnd()) return nullptr;
    return it.value();
}

QTreeWidgetItem* NotesTreePanel::noteItem(const QString& linkObjectId,
                                          int slotIndex) const
{
    QTreeWidgetItem* parent = linkItem(linkObjectId);
    if (!parent) return nullptr;
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* child = parent->child(i);
        if (child->data(0, SlotIndexRole).toInt() == slotIndex) return child;
    }
    return nullptr;
}

void NotesTreePanel::openNoteById(const QString& linkObjectId,
                                   const QString& noteId)
{
    auto it = m_linkIndex.find(linkObjectId);
    if (it == m_linkIndex.end()) return;
    for (const auto& slot : m_outline[*it].markdownSlots) {
        if (slot.noteId == noteId) {
            openNote(linkObjectId, slot.slotIndex);
            return;
        }
    }
}

void NotesTreePanel::openNote(const QString& linkObjectId, int slotIndex)
{
    QTreeWidgetItem* link = linkItem(linkObjectId);
    if (!link) return;

    // Ensure the chain above is expanded (L1 parent, then L2).
    if (QTreeWidgetItem* group = link->parent()) {
        if (!group->isExpanded()) group->setExpanded(true);
    }
    if (!link->isExpanded()) link->setExpanded(true);  // populates children

    if (QTreeWidgetItem* child = noteItem(linkObjectId, slotIndex)) {
        focusNote(child);
    }
}

void NotesTreePanel::highlightPage(int pageIndex)
{
    if (m_edgeless || m_outline.isEmpty() || pageIndex < 0) return;
    if (pageIndex == m_lastHighlightedPage) return;
    m_lastHighlightedPage = pageIndex;

    // Floor-match across L1 group items.
    QTreeWidgetItem* best = nullptr;
    int bestPage = -1;
    const int topCount = m_tree->topLevelItemCount();
    for (int i = 0; i < topCount; ++i) {
        QTreeWidgetItem* g = m_tree->topLevelItem(i);
        if (!g) continue;
        const int p = g->data(0, PageIndexRole).toInt();
        if (p >= 0 && p <= pageIndex && p > bestPage) {
            best = g;
            bestPage = p;
        }
    }
    if (!best) return;

    // RAII-scoped signal block — unblocks even if anything below throws or
    // early-returns, unlike the raw blockSignals(true/false) pair.
    const QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    best->setSelected(true);
    m_tree->scrollToItem(best, QAbstractItemView::EnsureVisible);
}

void NotesTreePanel::updateLinkObject(const QString& linkObjectId,
                                      const QString& description,
                                      const QColor&  iconColor)
{
    auto it = m_linkIndex.find(linkObjectId);
    if (it != m_linkIndex.end()) {
        m_outline[*it].description = description;
        m_outline[*it].iconColor   = iconColor;
    }
    if (QTreeWidgetItem* item = linkItem(linkObjectId)) {
        const QString label = description.isEmpty()
            ? QCoreApplication::translate(
                  "NotesTreePanel", "(no description)")
            : description;
        item->setText(0, label);
        item->setToolTip(0, description);
        item->setData(0, NotesTreeDelegate::ColorRole, iconColor);

        // Update the color stripe on any currently-populated L3 children too.
        for (int i = 0; i < item->childCount(); ++i) {
            item->child(i)->setData(0, NotesTreeDelegate::ColorRole, iconColor);
        }
        m_tree->viewport()->update();
    }
}

// ============================================================================
// Edgeless range filter
// ============================================================================

void NotesTreePanel::setEdgelessRangeFilter(int fromTileY, int toTileY)
{
    m_filterFromTileY = fromTileY;
    m_filterToTileY   = toTileY;
    applyRangeFilter();
}

void NotesTreePanel::applyRangeFilter()
{
    if (!m_edgeless) return;
    const bool active = (m_filterFromTileY >= 0 && m_filterToTileY >= 0
                         && m_filterFromTileY <= m_filterToTileY);

    const int topCount = m_tree->topLevelItemCount();
    for (int i = 0; i < topCount; ++i) {
        QTreeWidgetItem* g = m_tree->topLevelItem(i);
        if (!g) continue;
        const int ty = g->data(0, TileYRole).toInt();
        const bool hide = active
                       && (ty < m_filterFromTileY || ty > m_filterToTileY);
        g->setHidden(hide);
    }
}
