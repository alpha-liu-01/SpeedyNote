#pragma once

// ============================================================================
// NotesTreePanel — right-sidebar markdown notes tree (3-level hierarchy).
// ============================================================================
// Layout:
//   L1  Group      — "Page N" (paged) / "Row N" (edgeless), sorted top-down.
//   L2  LinkObject — one row per LinkObject that has >= 1 markdown slot.
//   L3  Note       — one row per markdown slot.  Compact by default; the
//                    single currently-focused L3 is inflated into a full
//                    MarkdownNoteEntry via QTreeWidget::setItemWidget().
//
// Memory policy (hard invariant):
//   * .md files are NOT read on setOutline(); only LinkOutlineEntry data.
//   * Expanding an L2 triggers a bounded preview read per slot (no full body).
//   * At most ONE MarkdownNoteEntry is alive in the whole panel at any time.
//   * Collapsing an L2, swapping focus, or clearing the outline destroys
//     the focused widget and all its children.
// ============================================================================

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include "LinkOutlineEntry.h"

class MarkdownNoteEntry;
class NotesTreeDelegate;
class NotesTreeWidget;
class QTreeWidgetItem;

class NotesTreePanel : public QWidget {
    Q_OBJECT

public:
    explicit NotesTreePanel(QWidget* parent = nullptr);
    ~NotesTreePanel() override;

    // ---------- Data ----------
    /// Rebuild the tree from the supplied outline.  Preserves (when possible)
    /// the previously-expanded L2 rows and the focused L3 row.
    void setOutline(const QVector<LinkOutlineEntry>& entries, bool edgeless);

    /// Drop the entire model and release the focused widget.
    void clear();

    /// Where to resolve note IDs to .md files.  Must be called before
    /// L2 expansion / focus is used.
    void setNotesDir(const QString& notesDir) { m_notesDir = notesDir; }

    /// Update L2 metadata in place without touching L3 (avoids preview reload).
    /// No-op if the LinkObject is not currently represented in the tree.
    void updateLinkObject(const QString& linkObjectId,
                          const QString& description,
                          const QColor&  iconColor);

    // ---------- Navigation ----------
    /// Expand the chain down to the given note and inflate it as the focused L3.
    /// If the note / link is not in the outline, does nothing.
    void openNote(const QString& linkObjectId, int slotIndex);

    /// Convenience: find the slot whose noteId matches, then openNote().
    void openNoteById(const QString& linkObjectId, const QString& noteId);

    /// Paged-mode highlighting (mirrors OutlinePanel::highlightPage).
    void highlightPage(int pageIndex);

    /// Edgeless-mode range filter: hide L1 groups whose tileY is not in
    /// [fromTileY, toTileY] (inclusive).  Pass (-1, -1) to clear the filter.
    void setEdgelessRangeFilter(int fromTileY, int toTileY);

    // ---------- Theme ----------
    void setDarkMode(bool dark);

    // ---------- Introspection ----------
    bool hasOutline() const { return !m_outline.isEmpty(); }

signals:
    /// Paged L1 tap — move viewport to this page.
    void navigateToPage(int pageIndex);
    /// Edgeless L1 tap — recenter viewport on this tile row.
    void navigateToTileRow(int tileY);
    /// L2 tap — select/scroll to the LinkObject on the canvas.
    void navigateToLinkObject(const QString& linkObjectId);

    /// Focused editor saved content.  Arguments mirror the existing
    /// MarkdownNotesSidebar signal so MainWindow wiring stays intact.
    void noteContentSaved(const QString& noteId,
                          const QString& title,
                          const QString& content);

    /// User clicked "delete" inside the focused editor.
    void noteDeletedWithLink(const QString& noteId, const QString& linkObjectId);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);

private:
    // ---------- Internal data ----------
    struct LinkRecord {
        LinkOutlineEntry entry;
        QTreeWidgetItem* item = nullptr;   ///< L2 tree item (or nullptr if not in tree)
    };

    // ---------- Helpers ----------
    void setupUi();
    void applyStyle();

    /// Build tree from m_outline, honouring m_edgeless.
    void rebuildTree();

    /// Lazily populate an L2 node with its L3 slot previews.
    void populateLinkChildren(QTreeWidgetItem* linkItem);

    /// Destroy L3 children and release the focused widget if inside.
    void releaseLinkChildren(QTreeWidgetItem* linkItem);

    /// Swap the inflated editor onto the given L3 item (or pass nullptr
    /// to simply destroy the current one).  Enforces "at most one alive".
    void focusNote(QTreeWidgetItem* noteItem);

    /// Find the L2 item for a given linkObjectId, or nullptr.
    QTreeWidgetItem* linkItem(const QString& linkObjectId) const;

    /// Find the L3 item for (linkObjectId, slotIndex) under its expanded L2,
    /// or nullptr if the parent L2 is not expanded or the slot doesn't exist.
    QTreeWidgetItem* noteItem(const QString& linkObjectId, int slotIndex) const;

    void applyRangeFilter();

    // ---------- State ----------
    NotesTreeWidget*   m_tree     = nullptr;
    NotesTreeDelegate* m_delegate = nullptr;

    // Full outline (owning copy).
    QVector<LinkOutlineEntry> m_outline;
    bool m_edgeless = false;

    // Link-id → index into m_outline, for O(1) navigation lookups.
    QHash<QString, int> m_linkIndex;

    // L2 item pointers keyed by linkObjectId, for fast metadata updates.
    // NOTE: QTreeWidgetItem is not a QObject, so we use raw pointers.  The
    // lifetime of these items is always <= the lifetime of m_tree, and every
    // code path that destroys items (clear() / rebuildTree() / takeChildren())
    // also rebuilds or clears this map, so dangling is avoided by construction.
    QHash<QString, QTreeWidgetItem*> m_linkItems;

    // Focused L3 state.  m_focusedEntry is a QPointer so that if the tree
    // destroys the widget (e.g. on clear()) we don't dangle.
    QTreeWidgetItem*              m_focusedItem  = nullptr;
    QPointer<MarkdownNoteEntry>   m_focusedEntry;
    QString                       m_focusedLinkId;
    QString                       m_focusedNoteId;

    // Expanded-row memory (linkObjectId).  Used to restore expansion across
    // setOutline() rebuilds caused by `linkObjectListMayHaveChanged`.
    QSet<QString> m_expandedLinks;

    // Edgeless range filter (inclusive).  (-1, -1) = no filter.
    int m_filterFromTileY = -1;
    int m_filterToTileY   = -1;

    // Misc.
    QString m_notesDir;
    bool    m_darkMode = false;
    int     m_lastHighlightedPage = -1;
};
