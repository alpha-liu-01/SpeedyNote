#include "MarkdownNotesSidebar.h"
#include <QPalette>
#include <QApplication>
#include <QDebug>
#include <QSet>
#include <algorithm>

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
    scrollLayout->setContentsMargins(8, 8, 8, 8);
    scrollLayout->setSpacing(8);
    scrollLayout->addStretch(); // Push notes to top
    
    scrollArea->setWidget(scrollContent);
    
    // Empty state label
    emptyLabel = new QLabel(tr("No notes on this page"), this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet("color: gray; font-style: italic; padding: 20px;");
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
    searchLayout->setContentsMargins(8, 8, 8, 8);
    searchLayout->setSpacing(6);
    
    // Search bar row
    searchBarLayout = new QHBoxLayout();
    searchBarLayout->setSpacing(4);
    
    searchInput = new QLineEdit(searchContainer);
    searchInput->setPlaceholderText(tr("Search notes..."));
    searchInput->setClearButtonEnabled(true);
    connect(searchInput, &QLineEdit::returnPressed, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    searchButton = new QPushButton(searchContainer);
    searchButton->setFixedSize(28, 28);
    searchButton->setToolTip(tr("Search"));
    // Use zoom icon with dark/light mode support
    QString zoomIconPath = isDarkMode ? ":/resources/icons/zoom_reversed.png" : ":/resources/icons/zoom.png";
    searchButton->setIcon(QIcon(zoomIconPath));
    searchButton->setIconSize(QSize(20, 20));
    connect(searchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onSearchButtonClicked);
    
    exitSearchButton = new QPushButton("×", searchContainer);
    exitSearchButton->setFixedSize(28, 28);
    exitSearchButton->setToolTip(tr("Exit search mode"));
    exitSearchButton->setVisible(false);
    connect(exitSearchButton, &QPushButton::clicked, this, &MarkdownNotesSidebar::onExitSearchClicked);
    
    searchBarLayout->addWidget(searchInput);
    searchBarLayout->addWidget(searchButton);
    searchBarLayout->addWidget(exitSearchButton);
    
    // Page range row
    pageRangeLayout = new QHBoxLayout();
    pageRangeLayout->setSpacing(4);
    
    pageRangeLabel = new QLabel(tr("Pages:"), searchContainer);
    
    fromPageSpinBox = new QSpinBox(searchContainer);
    fromPageSpinBox->setMinimum(1);
    fromPageSpinBox->setMaximum(9999);
    fromPageSpinBox->setValue(1);
    fromPageSpinBox->setFixedWidth(60);
    
    toLabel = new QLabel(tr("to"), searchContainer);
    
    toPageSpinBox = new QSpinBox(searchContainer);
    toPageSpinBox->setMinimum(1);
    toPageSpinBox->setMaximum(9999);
    toPageSpinBox->setValue(10);
    toPageSpinBox->setFixedWidth(60);
    
    searchAllPagesCheckBox = new QCheckBox(tr("All"), searchContainer);
    searchAllPagesCheckBox->setToolTip(tr("Search all pages in the notebook"));
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
    
    setStyleSheet(QString(R"(
        MarkdownNotesSidebar {
            background-color: %1;
        }
    )").arg(bgColor));
    
    // Style search input
    searchInput->setStyleSheet(QString(R"(
        QLineEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QLineEdit:focus {
            border: 1px solid #0078d4;
        }
    )").arg(inputBgColor, borderColor));
    
    // Style buttons
    QString buttonStyle = QString(R"(
        QPushButton {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            font-size: 14px;
        }
        QPushButton:hover {
            background-color: %3;
        }
        QPushButton:pressed {
            background-color: %4;
        }
    )").arg(inputBgColor, borderColor,
            isDarkMode ? "#3d3d3d" : "#e5e5e5",
            isDarkMode ? "#4d4d4d" : "#d5d5d5");
    
    searchButton->setStyleSheet(buttonStyle);
    exitSearchButton->setStyleSheet(buttonStyle.replace(borderColor, "#ff4444"));
    
    // Style spin boxes
    QString spinBoxStyle = QString(R"(
        QSpinBox {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
            padding: 2px;
        }
    )").arg(inputBgColor, borderColor);
    
    fromPageSpinBox->setStyleSheet(spinBoxStyle);
    toPageSpinBox->setStyleSheet(spinBoxStyle);
}

void MarkdownNotesSidebar::addNote(const MarkdownNoteData &data) {
    // Check if note already exists
    for (MarkdownNoteEntry *entry : noteEntries) {
        if (entry->getNoteId() == data.id) {
            entry->setNoteData(data);
            return;
        }
    }
    
    // Create new entry
    MarkdownNoteEntry *entry = new MarkdownNoteEntry(data, scrollContent);
    connect(entry, &MarkdownNoteEntry::contentChanged, this, &MarkdownNotesSidebar::onNoteContentChanged);
    connect(entry, &MarkdownNoteEntry::deleteRequested, this, &MarkdownNotesSidebar::onNoteDeleted);
    connect(entry, &MarkdownNoteEntry::highlightLinkClicked, this, &MarkdownNotesSidebar::onHighlightLinkClicked);
    
    noteEntries.append(entry);
    
    // Insert before the stretch
    scrollLayout->insertWidget(scrollLayout->count() - 1, entry);
    
    // Update visibility
    emptyLabel->hide();
    scrollArea->show();
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

void MarkdownNotesSidebar::updateNote(const MarkdownNoteData &data) {
    for (MarkdownNoteEntry *entry : noteEntries) {
        if (entry->getNoteId() == data.id) {
            entry->setNoteData(data);
            return;
        }
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

void MarkdownNotesSidebar::loadNotesForPages(const QList<MarkdownNoteData> &notes) {
    // If in search mode, store the notes for later but don't display them
    if (searchMode) {
        normalModeNotes = notes;
        return;
    }
    
    // Store for potential search mode exit
    normalModeNotes = notes;
    
    // ✅ OPTIMIZATION: Reuse existing widgets instead of destroy+recreate
    // This avoids expensive widget construction during rapid page switches
    
    // Build a set of note IDs we need to display
    QSet<QString> newNoteIds;
    for (const MarkdownNoteData &note : notes) {
        newNoteIds.insert(note.id);
    }
    
    // Track which existing widgets to keep
    QSet<QString> existingNoteIds;
    for (MarkdownNoteEntry *entry : noteEntries) {
        existingNoteIds.insert(entry->getNoteId());
    }
    
    // Remove widgets that are no longer needed
    QList<MarkdownNoteEntry*> entriesToRemove;
    for (MarkdownNoteEntry *entry : noteEntries) {
        if (!newNoteIds.contains(entry->getNoteId())) {
            entriesToRemove.append(entry);
        }
    }
    for (MarkdownNoteEntry *entry : entriesToRemove) {
        noteEntries.removeOne(entry);
        scrollLayout->removeWidget(entry);
        entry->deleteLater();
    }
    
    // Update existing widgets and add new ones
    for (const MarkdownNoteData &note : notes) {
        if (existingNoteIds.contains(note.id)) {
            // Update existing widget (very fast - just updates data)
            for (MarkdownNoteEntry *entry : noteEntries) {
                if (entry->getNoteId() == note.id) {
                    entry->setNoteData(note);
                    break;
                }
            }
        } else {
            // Create new widget only for truly new notes
            addNote(note);
        }
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

QList<MarkdownNoteData> MarkdownNotesSidebar::getAllNotes() const {
    QList<MarkdownNoteData> notes;
    for (const MarkdownNoteEntry *entry : noteEntries) {
        notes.append(entry->getNoteData());
    }
    return notes;
}

MarkdownNoteEntry* MarkdownNotesSidebar::findNoteEntry(const QString &noteId) {
    for (MarkdownNoteEntry *entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            return entry;
        }
    }
    return nullptr;
}

void MarkdownNotesSidebar::setNoteProvider(std::function<QList<MarkdownNoteData>()> provider) {
    noteProvider = provider;
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
    
    // Restore normal mode notes
    clearNotes();
    for (const MarkdownNoteData &note : normalModeNotes) {
        addNote(note);
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
        emit noteContentChanged(noteId, entry->getNoteData());
    }
}

void MarkdownNotesSidebar::onNoteDeleted(const QString &noteId) {
    removeNote(noteId);
    emit noteDeleted(noteId);
}

void MarkdownNotesSidebar::onHighlightLinkClicked(const QString &highlightId) {
    // In search mode, we may need to navigate to a different page
    // The MainWindow will handle the navigation
    emit highlightLinkClicked(highlightId);
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
    
    if (!noteProvider) {
        qWarning() << "No note provider set for search";
        return;
    }
    
    // Enter search mode
    searchMode = true;
    lastSearchQuery = query;
    exitSearchButton->setVisible(true);
    
    // Get all notes from provider
    QList<MarkdownNoteData> allNotes = noteProvider();
    
    // Determine page range
    int fromPage = 0; // 0-based internally
    int toPage = totalPages - 1;
    
    if (!searchAllPagesCheckBox->isChecked()) {
        fromPage = fromPageSpinBox->value() - 1; // Convert to 0-based
        toPage = toPageSpinBox->value() - 1;
    }
    
    // Search and score results
    struct ScoredNote {
        MarkdownNoteData note;
        int score; // Higher = better match
    };
    
    QList<ScoredNote> scoredResults;
    
    for (const MarkdownNoteData &note : allNotes) {
        // Check page range
        if (note.pageNumber < fromPage || note.pageNumber > toPage) {
            continue;
        }
        
        // Check if query matches title or content
        bool titleMatch = note.title.contains(query, Qt::CaseInsensitive);
        bool contentMatch = note.content.contains(query, Qt::CaseInsensitive);
        
        if (titleMatch || contentMatch) {
            ScoredNote scored;
            scored.note = note;
            
            // Calculate relevance score
            // Title match is worth more than content match
            // Exact match is worth more than partial match
            scored.score = 0;
            
            if (titleMatch) {
                scored.score += 100;
                // Bonus for exact title match
                if (note.title.compare(query, Qt::CaseInsensitive) == 0) {
                    scored.score += 50;
                }
                // Bonus for title starting with query
                if (note.title.startsWith(query, Qt::CaseInsensitive)) {
                    scored.score += 25;
                }
            }
            
            if (contentMatch) {
                scored.score += 50;
                // Count occurrences in content (more = more relevant)
                int count = 0;
                int pos = 0;
                QString lowerContent = note.content.toLower();
                QString lowerQuery = query.toLower();
                while ((pos = lowerContent.indexOf(lowerQuery, pos)) != -1) {
                    count++;
                    pos += lowerQuery.length();
                }
                scored.score += qMin(count * 5, 25); // Cap at 25 bonus points
            }
            
            scoredResults.append(scored);
        }
    }
    
    // Sort by score (descending), then by page number (ascending)
    std::sort(scoredResults.begin(), scoredResults.end(), [](const ScoredNote &a, const ScoredNote &b) {
        if (a.score != b.score) {
            return a.score > b.score; // Higher score first
        }
        return a.note.pageNumber < b.note.pageNumber; // Lower page first for same score
    });
    
    // Extract sorted notes
    QList<MarkdownNoteData> results;
    for (const ScoredNote &scored : scoredResults) {
        results.append(scored.note);
    }
    
    // Update status label
    if (results.isEmpty()) {
        searchStatusLabel->setText(tr("No results found for \"%1\"").arg(query));
    } else {
        searchStatusLabel->setText(tr("%n result(s) found", "", results.size()));
    }
    searchStatusLabel->setVisible(true);
    
    // Display results
    displaySearchResults(results);
}

void MarkdownNotesSidebar::displaySearchResults(const QList<MarkdownNoteData> &results) {
    // Clear current notes
    clearNotes();
    
    // Add search results
    for (const MarkdownNoteData &note : results) {
        addNote(note);
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
