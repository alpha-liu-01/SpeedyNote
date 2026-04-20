#ifndef MARKDOWNNOTESSIDEBAR_H
#define MARKDOWNNOTESSIDEBAR_H

// ============================================================================
// MarkdownNotesSidebar (Phase M.8)
// ============================================================================
// Thin host for the right-sidebar note UI.  It owns:
//   * a search bar (unchanged),
//   * an optional "hidden tiles" warning for edgeless mode,
//   * a NotesTreePanel that renders the 3-level outline (L1/L2/L3),
//   * a flat QScrollArea shown only in search-results mode.
//
// MainWindow interacts with this widget through a small contract:
//   - `setOutline(...)` on any `linkObjectListMayHaveChanged` / first open.
//   - `displaySearchResults(...)` when a search completes.
//   - `setRangeFilter(...)` to scope the outline view by page/tile-row.
//
// The fat part of the old implementation — `loadNotesForPage()` feeding one
// MarkdownNoteEntry per visible note — is deliberately gone.  At most one
// inflated editor now lives inside the tree (enforced by NotesTreePanel).
// ============================================================================

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "../text/MarkdownNoteEntry.h"            // NoteDisplayData
#include "sidebars/LinkOutlineEntry.h"

class NotesTreePanel;

class MarkdownNotesSidebar : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownNotesSidebar(QWidget* parent = nullptr);
    ~MarkdownNotesSidebar() override;

    // ---------- Outline ----------
    /// Where the panel resolves note IDs to .md files.  Safe to call every
    /// time the active document changes.
    void setNotesDir(const QString& notesDir);

    /// Replace the entire outline.  Lazy L2 expansion / focus are preserved
    /// when linkObjectIds are stable across rebuilds.
    void setOutline(const QVector<LinkOutlineEntry>& entries, bool edgeless);

    /// Update a single L2 row's description/color without reloading L3 previews.
    void updateLinkObject(const QString& linkObjectId,
                          const QString& description,
                          const QColor&  iconColor);

    /// Expand the chain for the note and inflate its editor.  Auto-exits
    /// search mode so the tree is visible.
    void openNote(const QString& linkObjectId, int slotIndex);

    /// Convenience overload — picks the slot whose noteId matches.
    void openNoteById(const QString& linkObjectId, const QString& noteId);

    /// Paged: highlight the L1 for `pageIndex`.  No-op in edgeless mode.
    void highlightPage(int pageIndex);

    // ---------- Search results ----------
    void displaySearchResults(const QList<NoteDisplayData>& results);

    bool isInSearchMode() const { return searchMode; }
    void exitSearchMode();

    // ---------- Page / range state ----------
    void setCurrentPageInfo(int currentPage, int totalPages);
    void setEdgelessMode(bool edgeless);
    void setHiddenTilesWarning(bool hasHiddenTiles,
                               int loadedCount = 0,
                               int totalCount  = 0);

    /// Applied on top of the outline view.  In edgeless mode the from/to
    /// values are treated as tile-row indices (inclusive).  In paged mode the
    /// filter is currently advisory — paged search already limits by page.
    void setRangeFilter(int fromIndex, int toIndex);
    void clearRangeFilter();

    // ---------- Misc ----------
    /// Reset both outline (tree) and search-results state.
    void clearNotes();

    void setDarkMode(bool darkMode);

signals:
    // Tree actions
    void navigateToPage(int pageIndex);
    void navigateToTileRow(int tileY);
    void linkObjectClicked(const QString& linkObjectId);

    // Note editing (from the focused editor).  Signature matches the old
    // sidebar so MainWindow wiring does not need to change.
    void noteContentSaved(const QString& noteId,
                          const QString& title,
                          const QString& content);
    void noteDeletedWithLink(const QString& noteId,
                             const QString& linkObjectId);

    // Search
    void searchRequested(const QString& query, int fromPage, int toPage);
    void reloadNotesRequested();

private slots:
    void onSearchButtonClicked();
    void onExitSearchClicked();
    void onSearchAllPagesToggled(bool checked);

private:
    void setupUi();
    void setupSearchUi();
    void applyStyle();
    void performSearch();
    void updateSearchRangeDefaults();

    /// Show tree, hide search results.
    void showOutlineView();
    /// Show flat results scroll area, hide tree.
    void showSearchResultsView();

    // Main layout
    QVBoxLayout* mainLayout = nullptr;

    // Search UI
    QWidget*    searchContainer   = nullptr;
    QVBoxLayout* searchLayout     = nullptr;
    QLineEdit*  searchInput       = nullptr;
    QPushButton* searchButton     = nullptr;
    QPushButton* exitSearchButton = nullptr;
    QWidget*    pageRangeContainer = nullptr;
    QHBoxLayout* pageRangeLayout  = nullptr;
    QLabel*     pageRangeLabel    = nullptr;
    QSpinBox*   fromPageSpinBox   = nullptr;
    QLabel*     toLabel           = nullptr;
    QSpinBox*   toPageSpinBox     = nullptr;
    QCheckBox*  searchAllPagesCheckBox = nullptr;
    QLabel*     searchStatusLabel = nullptr;

    // Outline view
    NotesTreePanel* notesTreePanel = nullptr;

    // Search-results view (flat)
    QScrollArea* scrollArea      = nullptr;
    QWidget*     scrollContent   = nullptr;
    QVBoxLayout* scrollLayout    = nullptr;
    QLabel*      emptyLabel      = nullptr;
    QList<MarkdownNoteEntry*> searchResultEntries;

    // Edgeless warning
    QLabel* hiddenTilesWarningLabel = nullptr;

    bool isDarkMode = false;
    bool isEdgeless = false;

    // Search state
    bool searchMode = false;
    QString lastSearchQuery;
    int currentPage = 0;
    int totalPages = 1;
};

#endif // MARKDOWNNOTESSIDEBAR_H
