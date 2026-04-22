#include "MarkdownNotesSidebar.h"

#include "sidebars/NotesTreePanel.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QPalette>

// ============================================================================
// Construction
// ============================================================================

MarkdownNotesSidebar::MarkdownNotesSidebar(QWidget* parent)
    : QWidget(parent)
{
    isDarkMode = palette().color(QPalette::Window).lightness() < 128;
    setupUi();
    applyStyle();
}

MarkdownNotesSidebar::~MarkdownNotesSidebar() = default;

// ============================================================================
// UI construction
// ============================================================================

void MarkdownNotesSidebar::setupUi()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupSearchUi();

    // Outline view (tree).
    notesTreePanel = new NotesTreePanel(this);
    connect(notesTreePanel, &NotesTreePanel::navigateToPage,
            this, &MarkdownNotesSidebar::navigateToPage);
    connect(notesTreePanel, &NotesTreePanel::navigateToTileRow,
            this, &MarkdownNotesSidebar::navigateToTileRow);
    connect(notesTreePanel, &NotesTreePanel::navigateToLinkObject,
            this, &MarkdownNotesSidebar::linkObjectClicked);
    connect(notesTreePanel, &NotesTreePanel::noteContentSaved,
            this, &MarkdownNotesSidebar::noteContentSaved);
    connect(notesTreePanel, &NotesTreePanel::noteDeletedWithLink,
            this, &MarkdownNotesSidebar::noteDeletedWithLink);

    // Search-results view (flat).  Initially hidden.
    scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("NotesScrollArea");
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);

    scrollContent = new QWidget();
    scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(12, 12, 12, 12);
    scrollLayout->setSpacing(8);
    scrollLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    scrollArea->hide();

    emptyLabel = new QLabel(tr("No notes on this page"), this);
    emptyLabel->setObjectName("EmptyLabel");
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    emptyLabel->hide();

    mainLayout->addWidget(searchContainer);
    mainLayout->addWidget(notesTreePanel, 1);
    mainLayout->addWidget(scrollArea, 1);
    mainLayout->addWidget(emptyLabel);
}

void MarkdownNotesSidebar::setupSearchUi()
{
    searchContainer = new QWidget(this);
    searchLayout = new QVBoxLayout(searchContainer);
    searchLayout->setContentsMargins(12, 12, 12, 8);
    searchLayout->setSpacing(8);

    auto* searchBarLayout = new QHBoxLayout();
    searchBarLayout->setSpacing(8);

    searchInput = new QLineEdit(searchContainer);
    searchInput->setObjectName("SearchInput");
    searchInput->setPlaceholderText(tr("Search notes..."));
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumHeight(36);
    connect(searchInput, &QLineEdit::returnPressed,
            this, &MarkdownNotesSidebar::onSearchButtonClicked);

    searchButton = new QPushButton(searchContainer);
    searchButton->setObjectName("SearchButton");
    searchButton->setFixedSize(36, 36);
    searchButton->setToolTip(tr("Search"));
    const QString zoomIconPath = isDarkMode
        ? QStringLiteral(":/resources/icons/zoom_reversed.png")
        : QStringLiteral(":/resources/icons/zoom.png");
    searchButton->setIcon(QIcon(zoomIconPath));
    searchButton->setIconSize(QSize(20, 20));
    connect(searchButton, &QPushButton::clicked,
            this, &MarkdownNotesSidebar::onSearchButtonClicked);

    exitSearchButton = new QPushButton(QStringLiteral("\u00D7"), searchContainer);
    exitSearchButton->setObjectName("ExitSearchButton");
    exitSearchButton->setFixedSize(36, 36);
    exitSearchButton->setToolTip(tr("Exit search mode"));
    exitSearchButton->setVisible(false);
    connect(exitSearchButton, &QPushButton::clicked,
            this, &MarkdownNotesSidebar::onExitSearchClicked);

    searchBarLayout->addWidget(searchInput);
    searchBarLayout->addWidget(searchButton);
    searchBarLayout->addWidget(exitSearchButton);

    pageRangeContainer = new QWidget(searchContainer);
    pageRangeContainer->setObjectName("PageRangeContainer");
    pageRangeLayout = new QHBoxLayout(pageRangeContainer);
    pageRangeLayout->setContentsMargins(0, 0, 0, 0);
    pageRangeLayout->setSpacing(6);

    pageRangeLabel = new QLabel(tr("Pages:"), pageRangeContainer);
    pageRangeLabel->setObjectName("PageRangeLabel");

    fromPageSpinBox = new QSpinBox(pageRangeContainer);
    fromPageSpinBox->setObjectName("PageSpinBox");
    fromPageSpinBox->setMinimum(1);
    fromPageSpinBox->setMaximum(9999);
    fromPageSpinBox->setValue(1);
    fromPageSpinBox->setMinimumHeight(32);

    toLabel = new QLabel(tr("to"), pageRangeContainer);
    toLabel->setObjectName("ToLabel");

    toPageSpinBox = new QSpinBox(pageRangeContainer);
    toPageSpinBox->setObjectName("PageSpinBox");
    toPageSpinBox->setMinimum(1);
    toPageSpinBox->setMaximum(9999);
    toPageSpinBox->setValue(10);
    toPageSpinBox->setMinimumHeight(32);

    searchAllPagesCheckBox = new QCheckBox(tr("All"), pageRangeContainer);
    searchAllPagesCheckBox->setObjectName("SearchAllCheckbox");
    searchAllPagesCheckBox->setToolTip(tr("Search all pages in the notebook"));
    searchAllPagesCheckBox->setMinimumHeight(32);
    connect(searchAllPagesCheckBox, &QCheckBox::toggled,
            this, &MarkdownNotesSidebar::onSearchAllPagesToggled);

    // Edgeless: live-filter the outline as spinboxes change.  In paged mode
    // these connections are harmless no-ops (isEdgeless gate inside the slot).
    auto applyEdgelessFilterFromSpinboxes = [this]() {
        if (!isEdgeless) return;
        if (searchAllPagesCheckBox->isChecked()) {
            clearRangeFilter();
        } else {
            const int from = fromPageSpinBox->value() - 1;  // 1-based → 0-based
            const int to   = toPageSpinBox->value() - 1;
            setRangeFilter(from, to);
        }
    };
    connect(fromPageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [applyEdgelessFilterFromSpinboxes](int){ applyEdgelessFilterFromSpinboxes(); });
    connect(toPageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [applyEdgelessFilterFromSpinboxes](int){ applyEdgelessFilterFromSpinboxes(); });

    pageRangeLayout->addWidget(pageRangeLabel);
    pageRangeLayout->addWidget(fromPageSpinBox);
    pageRangeLayout->addWidget(toLabel);
    pageRangeLayout->addWidget(toPageSpinBox);
    pageRangeLayout->addWidget(searchAllPagesCheckBox);
    pageRangeLayout->addStretch();

    searchStatusLabel = new QLabel(searchContainer);
    searchStatusLabel->setObjectName("SearchStatusLabel");
    searchStatusLabel->setVisible(false);

    searchLayout->addLayout(searchBarLayout);
    searchLayout->addWidget(pageRangeContainer);
    searchLayout->addWidget(searchStatusLabel);
}

// ============================================================================
// Theme
// ============================================================================

void MarkdownNotesSidebar::applyStyle()
{
    const QString qssPath = isDarkMode
        ? QStringLiteral(":/resources/styles/markdown_sidebar_dark.qss")
        : QStringLiteral(":/resources/styles/markdown_sidebar.qss");

    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        setStyleSheet(qssFile.readAll());
        qssFile.close();
    }

    const QString zoomIconPath = isDarkMode
        ? QStringLiteral(":/resources/icons/zoom_reversed.png")
        : QStringLiteral(":/resources/icons/zoom.png");
    if (searchButton) searchButton->setIcon(QIcon(zoomIconPath));

    if (notesTreePanel) notesTreePanel->setDarkMode(isDarkMode);
}

void MarkdownNotesSidebar::setDarkMode(bool darkMode)
{
    if (isDarkMode == darkMode) return;
    isDarkMode = darkMode;
    applyStyle();
}

// ============================================================================
// Outline / notes-dir / search API
// ============================================================================

void MarkdownNotesSidebar::setNotesDir(const QString& notesDir)
{
    if (notesTreePanel) notesTreePanel->setNotesDir(notesDir);
}

void MarkdownNotesSidebar::setOutline(const QVector<LinkOutlineEntry>& entries,
                                      bool edgeless)
{
    if (!notesTreePanel) return;
    notesTreePanel->setOutline(entries, edgeless);

    if (!searchMode) {
        showOutlineView();
        // Show the "no notes" hint only when the outline is genuinely empty.
        if (entries.isEmpty()) {
            notesTreePanel->hide();
            emptyLabel->setText(edgeless
                                ? tr("No notes in this document")
                                : tr("No notes in this document"));
            emptyLabel->show();
        } else {
            emptyLabel->hide();
            notesTreePanel->show();
        }
    }
}

void MarkdownNotesSidebar::updateLinkObject(const QString& linkObjectId,
                                             const QString& description,
                                             const QColor&  iconColor)
{
    if (notesTreePanel) {
        notesTreePanel->updateLinkObject(linkObjectId, description, iconColor);
    }
}

void MarkdownNotesSidebar::openNote(const QString& linkObjectId, int slotIndex)
{
    if (searchMode) exitSearchMode();
    showOutlineView();
    if (notesTreePanel) notesTreePanel->openNote(linkObjectId, slotIndex);
}

void MarkdownNotesSidebar::openNoteById(const QString& linkObjectId,
                                         const QString& noteId)
{
    if (searchMode) exitSearchMode();
    showOutlineView();
    if (notesTreePanel) notesTreePanel->openNoteById(linkObjectId, noteId);
}

void MarkdownNotesSidebar::highlightPage(int pageIndex)
{
    if (notesTreePanel) notesTreePanel->highlightPage(pageIndex);
}

void MarkdownNotesSidebar::setEdgelessMode(bool edgeless)
{
    if (isEdgeless == edgeless) return;
    isEdgeless = edgeless;

    // Range container is useful in both modes now:
    //  - Paged:    "Pages:"  (controls search range; outline filter is a no-op).
    //  - Edgeless: "Rows:"   (controls L1-group filter on the outline *and*
    //                         the row band for the flat search).
    pageRangeContainer->setVisible(true);
    pageRangeLabel->setText(edgeless ? tr("Rows:") : tr("Pages:"));

    // Entering edgeless: start with "All" on and no filter; let the user
    // opt into a band.
    if (edgeless) {
        searchAllPagesCheckBox->setChecked(true);
        clearRangeFilter();
    } else {
        // Leaving edgeless: make sure no stale filter hangs around.
        clearRangeFilter();
    }
}

void MarkdownNotesSidebar::setRangeFilter(int fromIndex, int toIndex)
{
    if (notesTreePanel && isEdgeless) {
        notesTreePanel->setEdgelessRangeFilter(fromIndex, toIndex);
    }
}

void MarkdownNotesSidebar::clearRangeFilter()
{
    if (notesTreePanel) notesTreePanel->setEdgelessRangeFilter(-1, -1);
}

void MarkdownNotesSidebar::setCurrentPageInfo(int page, int total)
{
    currentPage = page;
    totalPages = qMax(1, total);
    fromPageSpinBox->setMaximum(totalPages);
    toPageSpinBox->setMaximum(totalPages);
    if (!searchMode) updateSearchRangeDefaults();
}

void MarkdownNotesSidebar::updateSearchRangeDefaults()
{
    const int fromPage = qMax(1, currentPage + 1 - 4);
    const int toPage   = qMin(totalPages, currentPage + 1 + 5);
    fromPageSpinBox->setValue(fromPage);
    toPageSpinBox->setValue(toPage);
}

// ============================================================================
// Outline / search mode switching
// ============================================================================

void MarkdownNotesSidebar::showOutlineView()
{
    if (notesTreePanel && notesTreePanel->hasOutline()) {
        emptyLabel->hide();
        notesTreePanel->show();
    } else if (emptyLabel) {
        notesTreePanel->hide();
        emptyLabel->setText(tr("No notes in this document"));
        emptyLabel->show();
    }
    if (scrollArea) scrollArea->hide();
    if (searchStatusLabel) searchStatusLabel->setVisible(false);
}

void MarkdownNotesSidebar::showSearchResultsView()
{
    if (notesTreePanel) notesTreePanel->hide();
    if (scrollArea) scrollArea->show();
}

void MarkdownNotesSidebar::exitSearchMode()
{
    if (!searchMode) return;
    searchMode = false;
    lastSearchQuery.clear();
    exitSearchButton->setVisible(false);
    searchStatusLabel->setVisible(false);
    searchInput->clear();

    // Dump the flat results list.
    for (MarkdownNoteEntry* entry : searchResultEntries) {
        scrollLayout->removeWidget(entry);
        entry->deleteLater();
    }
    searchResultEntries.clear();

    showOutlineView();
    emit reloadNotesRequested();
}

void MarkdownNotesSidebar::clearNotes()
{
    // Tree
    if (notesTreePanel) notesTreePanel->clear();

    // Flat scroll area (search results).
    for (MarkdownNoteEntry* entry : searchResultEntries) {
        scrollLayout->removeWidget(entry);
        entry->deleteLater();
    }
    searchResultEntries.clear();

    if (emptyLabel) emptyLabel->hide();
    if (scrollArea) scrollArea->hide();
    if (notesTreePanel) notesTreePanel->hide();
}

// ============================================================================
// Search
// ============================================================================

void MarkdownNotesSidebar::onSearchButtonClicked() { performSearch(); }

void MarkdownNotesSidebar::onExitSearchClicked() { exitSearchMode(); }

void MarkdownNotesSidebar::onSearchAllPagesToggled(bool checked)
{
    fromPageSpinBox->setEnabled(!checked);
    toPageSpinBox->setEnabled(!checked);

    // Mirror the checkbox onto the outline filter in edgeless mode: "All" =
    // no filter, unchecked = band defined by the spinboxes.  In paged mode
    // this is a no-op because NotesTreePanel ignores the filter when
    // !m_edgeless.
    if (isEdgeless) {
        if (checked) {
            clearRangeFilter();
        } else {
            setRangeFilter(fromPageSpinBox->value() - 1,
                           toPageSpinBox->value() - 1);
        }
    }
}

void MarkdownNotesSidebar::performSearch()
{
    const QString query = searchInput->text().trimmed();
    if (query.isEmpty()) {
        exitSearchMode();
        return;
    }

    searchMode = true;
    lastSearchQuery = query;
    exitSearchButton->setVisible(true);

    int fromPage = 0;
    int toPage   = totalPages - 1;
    if (!searchAllPagesCheckBox->isChecked()) {
        fromPage = fromPageSpinBox->value() - 1;
        toPage   = toPageSpinBox->value() - 1;
    }
    emit searchRequested(query, fromPage, toPage);
}

void MarkdownNotesSidebar::displaySearchResults(
    const QList<NoteDisplayData>& results)
{
    // Rebuild the flat list.
    for (MarkdownNoteEntry* entry : searchResultEntries) {
        scrollLayout->removeWidget(entry);
        entry->deleteLater();
    }
    searchResultEntries.clear();

    searchStatusLabel->setText(
        results.isEmpty()
            ? tr("No results found for \"%1\"").arg(lastSearchQuery)
            : tr("%n result(s) found", "", static_cast<int>(results.size())));
    searchStatusLabel->setVisible(true);

    for (const NoteDisplayData& data : results) {
        auto* entry = new MarkdownNoteEntry(data, scrollContent);
        scrollLayout->insertWidget(scrollLayout->count() - 1, entry);
        searchResultEntries.append(entry);

        connect(entry, &MarkdownNoteEntry::linkObjectClicked,
                this,  &MarkdownNotesSidebar::linkObjectClicked);
        connect(entry, &MarkdownNoteEntry::deleteWithLinkRequested,
                this,  &MarkdownNotesSidebar::noteDeletedWithLink);
        connect(entry, &MarkdownNoteEntry::contentChanged,
                this, [this, entry](const QString& id) {
            emit noteContentSaved(id, entry->getTitle(), entry->getContent());
        });
    }

    showSearchResultsView();
    if (results.isEmpty()) {
        scrollArea->hide();
        emptyLabel->setText(tr("No matching notes found"));
        emptyLabel->show();
    } else {
        emptyLabel->hide();
        scrollArea->show();
    }
}
