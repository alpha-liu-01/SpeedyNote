#include "MarkdownNotesSidebar.h"
#include <QPalette>
#include <QApplication>
#include <QDebug>
#include <QFile>

MarkdownNotesSidebar::MarkdownNotesSidebar(QWidget *parent)
    : QWidget(parent)
{
    isDarkMode = palette().color(QPalette::Window).lightness() < 128;
    setupUI();
    applyStyle();
}

MarkdownNotesSidebar::~MarkdownNotesSidebar() = default;

void MarkdownNotesSidebar::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Setup search UI first (at top)
    setupSearchUI();
    
    // Scroll area for notes
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
    scrollLayout->addStretch(); // Push notes to top
    
    scrollArea->setWidget(scrollContent);
    
    // Empty state label
    emptyLabel = new QLabel(tr("No notes on this page"), this);
    emptyLabel->setObjectName("EmptyLabel");
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    
    mainLayout->addWidget(searchContainer);
    mainLayout->addWidget(scrollArea, 1); // Give scroll area stretch priority
    mainLayout->addWidget(emptyLabel);
    mainLayout->addStretch(); // Push everything to the top when scroll area is hidden
    
    emptyLabel->show();
    scrollArea->hide();
}

void MarkdownNotesSidebar::setupSearchUI() {
    searchContainer = new QWidget(this);
    searchLayout = new QVBoxLayout(searchContainer);
    searchLayout->setContentsMargins(12, 12, 12, 8);
    searchLayout->setSpacing(8);
    
    // Search bar row with pill-shaped elements
    searchBarLayout = new QHBoxLayout();
    searchBarLayout->setSpacing(8);
    
    searchInput = new QLineEdit(searchContainer);
    searchInput->setObjectName("SearchInput");
    searchInput->setPlaceholderText(tr("Search notes..."));
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumHeight(36);
    connect(searchInput, &QLineEdit::returnPressed, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    searchButton = new QPushButton(searchContainer);
    searchButton->setObjectName("SearchButton");
    searchButton->setFixedSize(36, 36);
    searchButton->setToolTip(tr("Search"));
    // Use zoom icon with dark/light mode support
    QString zoomIconPath = isDarkMode ? ":/resources/icons/zoom_reversed.png" : ":/resources/icons/zoom.png";
    searchButton->setIcon(QIcon(zoomIconPath));
    searchButton->setIconSize(QSize(20, 20));
    connect(searchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    exitSearchButton = new QPushButton("×", searchContainer);
    exitSearchButton->setObjectName("ExitSearchButton");
    exitSearchButton->setFixedSize(36, 36);
    exitSearchButton->setToolTip(tr("Exit search mode"));
    exitSearchButton->setVisible(false);
    connect(exitSearchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onExitSearchClicked);
    
    searchBarLayout->addWidget(searchInput);
    searchBarLayout->addWidget(searchButton);
    searchBarLayout->addWidget(exitSearchButton);
    
    // Page range row
    pageRangeLayout = new QHBoxLayout();
    pageRangeLayout->setSpacing(6);
    
    pageRangeLabel = new QLabel(tr("Pages:"), searchContainer);
    pageRangeLabel->setObjectName("PageRangeLabel");
    
    fromPageSpinBox = new QSpinBox(searchContainer);
    fromPageSpinBox->setObjectName("PageSpinBox");
    fromPageSpinBox->setMinimum(1);
    fromPageSpinBox->setMaximum(9999);
    fromPageSpinBox->setValue(1);
    fromPageSpinBox->setMinimumHeight(32);
    
    toLabel = new QLabel(tr("to"), searchContainer);
    toLabel->setObjectName("ToLabel");
    
    toPageSpinBox = new QSpinBox(searchContainer);
    toPageSpinBox->setObjectName("PageSpinBox");
    toPageSpinBox->setMinimum(1);
    toPageSpinBox->setMaximum(9999);
    toPageSpinBox->setValue(10);
    toPageSpinBox->setMinimumHeight(32);
    
    searchAllPagesCheckBox = new QCheckBox(tr("All"), searchContainer);
    searchAllPagesCheckBox->setObjectName("SearchAllCheckbox");
    searchAllPagesCheckBox->setToolTip(tr("Search all pages in the notebook"));
    searchAllPagesCheckBox->setMinimumHeight(32);
    connect(searchAllPagesCheckBox, &QCheckBox::toggled, this, &MarkdownNotesSidebar::onSearchAllPagesToggled);
    
    pageRangeLayout->addWidget(pageRangeLabel);
    pageRangeLayout->addWidget(fromPageSpinBox);
    pageRangeLayout->addWidget(toLabel);
    pageRangeLayout->addWidget(toPageSpinBox);
    pageRangeLayout->addWidget(searchAllPagesCheckBox);
    pageRangeLayout->addStretch();
    
    // Search status label
    searchStatusLabel = new QLabel(searchContainer);
    searchStatusLabel->setObjectName("SearchStatusLabel");
    searchStatusLabel->setVisible(false);
    
    searchLayout->addLayout(searchBarLayout);
    searchLayout->addLayout(pageRangeLayout);
    searchLayout->addWidget(searchStatusLabel);
}

void MarkdownNotesSidebar::applyStyle() {
    // Load QSS from resource file
    QString qssPath = isDarkMode 
        ? ":/resources/styles/markdown_sidebar_dark.qss"
        : ":/resources/styles/markdown_sidebar.qss";
    
    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = qssFile.readAll();
        setStyleSheet(styleSheet);
        qssFile.close();
    } else {
        qDebug() << "MarkdownNotesSidebar: Failed to load QSS from" << qssPath;
    }
    
    // Update search button icon for theme
    QString zoomIconPath = isDarkMode 
        ? ":/resources/icons/zoom_reversed.png" 
        : ":/resources/icons/zoom.png";
    searchButton->setIcon(QIcon(zoomIconPath));
}

void MarkdownNotesSidebar::setDarkMode(bool darkMode) {
    if (isDarkMode != darkMode) {
        isDarkMode = darkMode;
        applyStyle();
    }
}

void MarkdownNotesSidebar::removeNote(const QString &noteId) {
    for (int i = 0; i < noteEntries.size(); ++i) {
        if (noteEntries[i]->getNoteId() == noteId) {
            MarkdownNoteEntry *entry = noteEntries.takeAt(i);
            scrollLayout->removeWidget(entry);
            entry->deleteLater();
            break;
        }
    }
    
    // Update visibility
    if (noteEntries.isEmpty()) {
        scrollArea->hide();
        emptyLabel->show();
    }
}

void MarkdownNotesSidebar::clearNotes() {
    for (MarkdownNoteEntry *entry : noteEntries) {
        scrollLayout->removeWidget(entry);
        entry->deleteLater();
    }
    noteEntries.clear();
    
    scrollArea->hide();
    emptyLabel->show();
}

// Load notes from LinkObject-based NoteDisplayData
void MarkdownNotesSidebar::loadNotesForPage(const QList<NoteDisplayData>& notes)
{
    // Clear existing notes
    clearNotes();
    
    // Add each note with the new NoteDisplayData constructor
    for (const NoteDisplayData& data : notes) {
        // Create entry using the new constructor
        MarkdownNoteEntry* entry = new MarkdownNoteEntry(data, scrollContent);
        
        // Connect signals
        connect(entry, &MarkdownNoteEntry::contentChanged, this, &MarkdownNotesSidebar::onNoteContentChanged);
        connect(entry, &MarkdownNoteEntry::linkObjectClicked, this, &MarkdownNotesSidebar::onLinkObjectClicked);
        connect(entry, &MarkdownNoteEntry::deleteWithLinkRequested, this, &MarkdownNotesSidebar::onNoteDeletedWithLink);
        
        noteEntries.append(entry);
        
        // Insert before the stretch
        scrollLayout->insertWidget(scrollLayout->count() - 1, entry);
    }
    
    // Update visibility
    if (noteEntries.isEmpty()) {
        scrollArea->hide();
        emptyLabel->setText(tr("No notes on this page"));
        emptyLabel->show();
    } else {
        emptyLabel->hide();
        scrollArea->show();
    }
}

MarkdownNoteEntry* MarkdownNotesSidebar::findNoteEntry(const QString &noteId) {
    for (MarkdownNoteEntry *entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            return entry;
        }
    }
    return nullptr;
}

void MarkdownNotesSidebar::setCurrentPageInfo(int page, int total) {
    currentPage = page;
    totalPages = total;
    
    // Update spinbox maximums
    fromPageSpinBox->setMaximum(total);
    toPageSpinBox->setMaximum(total);
    
    // Update default range (previous 4, current, next 5 = 10 pages)
    if (!searchMode) {
        updateSearchRangeDefaults();
    }
}

void MarkdownNotesSidebar::updateSearchRangeDefaults() {
    // Default: previous 4 pages, current page, next 5 pages
    int fromPage = qMax(1, currentPage + 1 - 4);  // +1 for 1-based display
    int toPage = qMin(totalPages, currentPage + 1 + 5);
    
    fromPageSpinBox->setValue(fromPage);
    toPageSpinBox->setValue(toPage);
}

void MarkdownNotesSidebar::exitSearchMode() {
    if (!searchMode) return;
    
    searchMode = false;
    lastSearchQuery.clear();
    
    // Update UI
    exitSearchButton->setVisible(false);
    searchStatusLabel->setVisible(false);
    searchInput->clear();
    
    // Request MainWindow to reload notes for current page
    emit reloadNotesRequested();
}

void MarkdownNotesSidebar::onNewNoteCreated() {
    // Auto-exit search mode when a new note is created
    // so the user can see and edit the new note
    if (searchMode) {
        exitSearchMode();
    }
}

void MarkdownNotesSidebar::onNoteContentChanged(const QString &noteId) {
    MarkdownNoteEntry *entry = findNoteEntry(noteId);
    if (entry) {
        emit noteContentSaved(noteId, entry->getTitle(), entry->getContent());
    }
}

// Handle jump to LinkObject
void MarkdownNotesSidebar::onLinkObjectClicked(const QString& linkObjectId) {
    emit linkObjectClicked(linkObjectId);
}

// Phase M.3: Handle note deletion with LinkObject reference
void MarkdownNotesSidebar::onNoteDeletedWithLink(const QString& noteId, const QString& linkObjectId) {
    removeNote(noteId);
    emit noteDeletedWithLink(noteId, linkObjectId);
}

void MarkdownNotesSidebar::onSearchButtonClicked() {
    performSearch();
}

void MarkdownNotesSidebar::onExitSearchClicked() {
    exitSearchMode();
}

void MarkdownNotesSidebar::onSearchAllPagesToggled(bool checked) {
    fromPageSpinBox->setEnabled(!checked);
    toPageSpinBox->setEnabled(!checked);
}

void MarkdownNotesSidebar::performSearch() {
    QString query = searchInput->text().trimmed();
    
    if (query.isEmpty()) {
        // Empty query - exit search mode
        exitSearchMode();
        return;
    }
    
    // Enter search mode
    searchMode = true;
    lastSearchQuery = query;
    exitSearchButton->setVisible(true);
    
    // Determine page range
    int fromPage = 0; // 0-based internally
    int toPage = totalPages - 1;
    
    if (!searchAllPagesCheckBox->isChecked()) {
        fromPage = fromPageSpinBox->value() - 1; // Convert to 0-based
        toPage = toPageSpinBox->value() - 1;
    }
    
    // Phase M.4: Emit signal for MainWindow to handle search
    emit searchRequested(query, fromPage, toPage);
}

// Display search results using NoteDisplayData
void MarkdownNotesSidebar::displaySearchResults(const QList<NoteDisplayData>& results) {
    // Clear current notes
    clearNotes();
    
    // Update status label
    if (results.isEmpty()) {
        searchStatusLabel->setText(tr("No results found for \"%1\"").arg(lastSearchQuery));
    } else {
        searchStatusLabel->setText(tr("%n result(s) found", "", results.size()));
    }
    searchStatusLabel->setVisible(true);
    
    // Add search results using new format
    for (const NoteDisplayData& data : results) {
        MarkdownNoteEntry* entry = new MarkdownNoteEntry(data, scrollContent);
        scrollLayout->insertWidget(scrollLayout->count() - 1, entry);
        noteEntries.append(entry);
        
        // Connect signals for LinkObject-based entries
        connect(entry, &MarkdownNoteEntry::linkObjectClicked,
                this, &MarkdownNotesSidebar::onLinkObjectClicked);
        connect(entry, &MarkdownNoteEntry::deleteWithLinkRequested,
                this, &MarkdownNotesSidebar::onNoteDeletedWithLink);
        connect(entry, &MarkdownNoteEntry::contentChanged,
                this, &MarkdownNotesSidebar::onNoteContentChanged);
    }
    
    // Update visibility
    if (results.isEmpty()) {
        scrollArea->hide();
        emptyLabel->setText(tr("No matching notes found"));
        emptyLabel->show();
    } else {
        emptyLabel->hide();
        scrollArea->show();
    }
}

// Phase M.5: Scroll to a specific note entry
void MarkdownNotesSidebar::scrollToNote(const QString& noteId)
{
    for (MarkdownNoteEntry* entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            scrollArea->ensureWidgetVisible(entry);
            return;
        }
    }
}

// Phase M.5: Set note to edit or preview mode
void MarkdownNotesSidebar::setNoteEditMode(const QString& noteId, bool editMode)
{
    for (MarkdownNoteEntry* entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            entry->setPreviewMode(!editMode);
            return;
        }
    }
}
