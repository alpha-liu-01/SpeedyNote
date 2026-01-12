#include "MarkdownNotesSidebar.h"
#include <QPalette>
#include <QApplication>
#include <QDebug>

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
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    scrollContent = new QWidget();
    scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(12, 12, 12, 12);
    scrollLayout->setSpacing(10);
    scrollLayout->addStretch(); // Push notes to top
    
    scrollArea->setWidget(scrollContent);
    
    // Empty state label
    emptyLabel = new QLabel(tr("No notes on this page"), this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet("color: gray; font-style: italic; padding: 30px; font-size: 14px;");
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
    searchLayout->setContentsMargins(12, 12, 12, 12);
    searchLayout->setSpacing(10);
    
    // ✅ TOUCH-FRIENDLY: Search bar row with larger elements
    searchBarLayout = new QHBoxLayout();
    searchBarLayout->setSpacing(8);
    
    searchInput = new QLineEdit(searchContainer);
    searchInput->setPlaceholderText(tr("Search notes..."));
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumHeight(36);
    connect(searchInput, &QLineEdit::returnPressed, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    searchButton = new QPushButton(searchContainer);
    searchButton->setFixedSize(36, 36);
    searchButton->setToolTip(tr("Search"));
    // Use zoom icon with dark/light mode support
    QString zoomIconPath = isDarkMode ? ":/resources/icons/zoom_reversed.png" : ":/resources/icons/zoom.png";
    searchButton->setIcon(QIcon(zoomIconPath));
    searchButton->setIconSize(QSize(24, 24));
    connect(searchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    exitSearchButton = new QPushButton("×", searchContainer);
    exitSearchButton->setFixedSize(36, 36);
    exitSearchButton->setToolTip(tr("Exit search mode"));
    exitSearchButton->setVisible(false);
    connect(exitSearchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onExitSearchClicked);
    
    searchBarLayout->addWidget(searchInput);
    searchBarLayout->addWidget(searchButton);
    searchBarLayout->addWidget(exitSearchButton);
    
    // ✅ TOUCH-FRIENDLY: Page range row with larger touch targets
    pageRangeLayout = new QHBoxLayout();
    pageRangeLayout->setSpacing(8);
    
    pageRangeLabel = new QLabel(tr("Pages:"), searchContainer);
    
    fromPageSpinBox = new QSpinBox(searchContainer);
    fromPageSpinBox->setMinimum(1);
    fromPageSpinBox->setMaximum(9999);
    fromPageSpinBox->setValue(1);
    fromPageSpinBox->setMinimumSize(36, 30);
    
    toLabel = new QLabel(tr("to"), searchContainer);
    
    toPageSpinBox = new QSpinBox(searchContainer);
    toPageSpinBox->setMinimum(1);
    toPageSpinBox->setMaximum(9999);
    toPageSpinBox->setValue(10);
    toPageSpinBox->setMinimumSize(36, 30);
    
    searchAllPagesCheckBox = new QCheckBox(tr("All"), searchContainer);
    searchAllPagesCheckBox->setToolTip(tr("Search all pages in the notebook"));
    searchAllPagesCheckBox->setMinimumHeight(36);
    connect(searchAllPagesCheckBox, &QCheckBox::toggled, this, &MarkdownNotesSidebar::onSearchAllPagesToggled);
    
    pageRangeLayout->addWidget(pageRangeLabel);
    pageRangeLayout->addWidget(fromPageSpinBox);
    pageRangeLayout->addWidget(toLabel);
    pageRangeLayout->addWidget(toPageSpinBox);
    pageRangeLayout->addWidget(searchAllPagesCheckBox);
    pageRangeLayout->addStretch();
    
    // Search status label
    searchStatusLabel = new QLabel(searchContainer);
    searchStatusLabel->setStyleSheet("color: gray; font-style: italic;");
    searchStatusLabel->setVisible(false);
    
    searchLayout->addLayout(searchBarLayout);
    searchLayout->addLayout(pageRangeLayout);
    searchLayout->addWidget(searchStatusLabel);
}

void MarkdownNotesSidebar::applyStyle() {
    QString bgColor = isDarkMode ? "#1e1e1e" : "#fafafa";
    QString inputBgColor = isDarkMode ? "#2d2d2d" : "#ffffff";
    QString borderColor = isDarkMode ? "#555555" : "#cccccc";
    QString hoverColor = isDarkMode ? "#3d3d3d" : "#e5e5e5";
    QString pressedColor = isDarkMode ? "#4d4d4d" : "#d5d5d5";
    QString textColor = isDarkMode ? "#ffffff" : "#000000";
    
    setStyleSheet(QString(R"(
        MarkdownNotesSidebar {
            background-color: %1;
        }
    )").arg(bgColor));
    
    // ✅ TOUCH-FRIENDLY: Larger padding and font for search input
    searchInput->setStyleSheet(QString(R"(
        QLineEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 0px;
            padding: 8px 12px;
            font-size: 14px;
        }
        QLineEdit:focus {
            border: 2px solid #0078d4;
        }
    )").arg(inputBgColor, borderColor));
    
    // ✅ TOUCH-FRIENDLY: Larger buttons with clear visual feedback
    QString buttonStyle = QString(R"(
        QPushButton {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 0px;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: %3;
        }
        QPushButton:pressed {
            background-color: %4;
        }
    )").arg(inputBgColor, borderColor, hoverColor, pressedColor);
    
    searchButton->setStyleSheet(buttonStyle);
    
    // Exit button with red accent
    exitSearchButton->setStyleSheet(QString(R"(
        QPushButton {
            background-color: %1;
            border: 1px solid #ff4444;
            border-radius: 0px;
            font-size: 18px;
            font-weight: bold;
            color: #ff4444;
        }
        QPushButton:hover {
            background-color: #ffeeee;
            border: 2px solid #ff4444;
        }
        QPushButton:pressed {
            background-color: #ff4444;
            color: white;
        }
    )").arg(inputBgColor));
    
    // ✅ NO CUSTOM SPINBOX STYLING - Let them inherit global/system styles
    // This ensures consistency with the main toolbar spinbox
    // Just clear any previous custom styles
    fromPageSpinBox->setStyleSheet("");
    toPageSpinBox->setStyleSheet("");
    
    // ✅ Style the checkbox for better touch visibility
    searchAllPagesCheckBox->setStyleSheet(QString(R"(
        QCheckBox {
            spacing: 8px;
            font-size: 13px;
        }
        QCheckBox::indicator {
            width: 20px;
            height: 20px;
        }
    )"));
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
