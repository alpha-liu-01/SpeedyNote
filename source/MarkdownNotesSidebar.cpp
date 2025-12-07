#include "MarkdownNotesSidebar.h"
#include <QPalette>
#include <QApplication>
#include <QDebug>
#include <QSet>

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
    
    mainLayout->addWidget(scrollArea);
    mainLayout->addWidget(emptyLabel);
    
    emptyLabel->show();
    scrollArea->hide();
}

void MarkdownNotesSidebar::applyStyle() {
    QString bgColor = isDarkMode ? "#1e1e1e" : "#fafafa";
    
    setStyleSheet(QString(R"(
        MarkdownNotesSidebar {
            background-color: %1;
        }
    )").arg(bgColor));
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
    emit highlightLinkClicked(highlightId);
}

