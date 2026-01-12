# Markdown Notes Integration - Implementation Subplan

## Overview

Integrate markdown notes with LinkObjects, replacing the old InkCanvas-based text highlight system.

**Key Architecture:**
- Notes stored as separate `.md` files in `assets/notes/`
- Unidirectional reference: `LinkSlot.markdownNoteId` → file
- Color/description derived from LinkObject at display time
- Sidebar shows notes for current page only

**Reference:** See `MARKDOWN_NOTES_QA.md` for detailed design decisions.

---

# Phase M.1: Data Model & File I/O

**Goal:** Create `MarkdownNote` class with file-based storage.

---

## Task M.1.1: Create MarkdownNote Class

**File:** `source/core/MarkdownNote.h`

```cpp
#pragma once

#include <QString>
#include <QJsonObject>

/**
 * @brief A markdown note linked to a LinkObject slot.
 * 
 * Notes are stored as separate .md files with YAML front matter.
 * The note file does NOT store back-references to LinkObject -
 * the connection is maintained via LinkSlot.markdownNoteId.
 */
class MarkdownNote {
public:
    QString id;       ///< UUID (matches filename without .md)
    QString title;    ///< Note title (from YAML front matter)
    QString content;  ///< Markdown content (after front matter)
    
    /**
     * @brief Save note to file with YAML front matter.
     * @param filePath Full path to .md file
     * @return true if successful
     */
    bool saveToFile(const QString& filePath) const;
    
    /**
     * @brief Load note from .md file with YAML front matter.
     * @param filePath Full path to .md file
     * @return Loaded note (id will be empty if load failed)
     */
    static MarkdownNote loadFromFile(const QString& filePath);
    
    /**
     * @brief Check if this note is valid (has ID).
     */
    bool isValid() const { return !id.isEmpty(); }
};
```

**Estimated lines:** ~40

---

## Task M.1.2: Implement MarkdownNote File I/O

**File:** `source/core/MarkdownNote.cpp`

```cpp
#include "MarkdownNote.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>

bool MarkdownNote::saveToFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open note file for writing:" << filePath;
        return false;
    }
    
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    // Write YAML front matter
    out << "---\n";
    out << "title: \"" << title.replace("\"", "\\\"") << "\"\n";
    out << "---\n\n";
    
    // Write content
    out << content;
    
    return true;
}

MarkdownNote MarkdownNote::loadFromFile(const QString& filePath)
{
    MarkdownNote note;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return note;  // Return invalid note
    }
    
    // Extract ID from filename
    QFileInfo info(filePath);
    note.id = info.baseName();
    
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString fileContent = in.readAll();
    
    // Parse YAML front matter
    if (fileContent.startsWith("---\n")) {
        int endMarker = fileContent.indexOf("\n---\n", 4);
        if (endMarker != -1) {
            QString frontMatter = fileContent.mid(4, endMarker - 4);
            note.content = fileContent.mid(endMarker + 5).trimmed();
            
            // Parse title from front matter
            QRegularExpression titleRe("title:\\s*\"(.*)\"");
            QRegularExpressionMatch match = titleRe.match(frontMatter);
            if (match.hasMatch()) {
                note.title = match.captured(1).replace("\\\"", "\"");
            }
        } else {
            // Malformed front matter - treat as plain content
            note.content = fileContent;
        }
    } else {
        // No front matter - treat as plain content
        note.content = fileContent;
        note.title = "Untitled";
    }
    
    return note;
}
```

**Estimated lines:** ~70

---

## Task M.1.3: Add Document Helper Methods

**File:** `source/core/Document.h` (additions)

```cpp
// Add to Document class:

/**
 * @brief Get the assets directory path for this document.
 * @return Path to assets/ folder, or empty if document not saved
 */
QString assetsPath() const;

/**
 * @brief Get the notes directory path for this document.
 * Creates the directory if it doesn't exist.
 * @return Path to assets/notes/ folder
 */
QString notesPath() const;

/**
 * @brief Delete a markdown note file.
 * @param noteId The note UUID
 * @return true if deleted or didn't exist
 */
bool deleteNoteFile(const QString& noteId);
```

**File:** `source/core/Document.cpp` (additions)

```cpp
QString Document::assetsPath() const
{
    if (m_filePath.isEmpty()) {
        return QString();
    }
    
    QFileInfo info(m_filePath);
    return info.absolutePath() + "/assets";
}

QString Document::notesPath() const
{
    QString assets = assetsPath();
    if (assets.isEmpty()) {
        return QString();
    }
    
    QString notes = assets + "/notes";
    QDir().mkpath(notes);
    return notes;
}

bool Document::deleteNoteFile(const QString& noteId)
{
    QString notePath = notesPath();
    if (notePath.isEmpty()) {
        return false;
    }
    
    QString filePath = notePath + "/" + noteId + ".md";
    if (QFile::exists(filePath)) {
        return QFile::remove(filePath);
    }
    return true;  // File didn't exist, consider it deleted
}
```

**Estimated lines:** ~40

---

## Task M.1.4: Update CMakeLists.txt

Add new source files:

```cmake
set(CORE_SOURCES
    # ... existing files ...
    source/core/MarkdownNote.h
    source/core/MarkdownNote.cpp
)
```

**Estimated lines:** ~2

---

## M.1 Testing Checklist

- [ ] Create MarkdownNote, save to file, verify YAML format
- [ ] Load MarkdownNote from file, verify title and content parsed
- [ ] Load malformed file (no front matter), verify graceful handling
- [ ] Document.notesPath() creates directory if needed
- [ ] Document.deleteNoteFile() removes file

---

# Phase M.2: Core Operations

**Goal:** Implement create, open, and delete operations for markdown notes.

---

## Task M.2.1: Create Markdown Note from LinkSlot

**File:** `source/core/DocumentViewport.h` (additions)

```cpp
signals:
    /**
     * @brief Emitted when a markdown note should be opened in sidebar.
     * @param noteId The note UUID
     * @param linkObjectId The parent LinkObject ID (for navigation)
     */
    void requestOpenMarkdownNote(const QString& noteId, const QString& linkObjectId);

public:
    /**
     * @brief Create a new markdown note for the specified slot.
     * @param slotIndex Slot index (0-2)
     * 
     * Requires a LinkObject to be selected with an empty slot at slotIndex.
     * Creates note file and updates slot reference.
     */
    void createMarkdownNoteForSlot(int slotIndex);
```

**File:** `source/core/DocumentViewport.cpp` (additions)

```cpp
void DocumentViewport::createMarkdownNoteForSlot(int slotIndex)
{
    // Validate selection
    if (m_selectedObjects.size() != 1) return;
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) return;
    
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) return;
    if (!link->linkSlots[slotIndex].isEmpty()) return;
    
    // Check document is saved (needed for file path)
    QString notesDir = m_document->notesPath();
    if (notesDir.isEmpty()) {
        qWarning() << "Cannot create note: document not saved";
        // TODO: Show user message
        return;
    }
    
    // Generate note ID
    QString noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Create note with default title from description
    MarkdownNote note;
    note.id = noteId;
    note.title = link->description.isEmpty() 
        ? tr("Untitled Note") 
        : link->description.left(50);
    note.content = "";
    
    // Save note file
    QString filePath = notesDir + "/" + noteId + ".md";
    if (!note.saveToFile(filePath)) {
        qWarning() << "Failed to create note file:" << filePath;
        return;
    }
    
    // Update slot
    link->linkSlots[slotIndex].type = LinkSlot::Type::Markdown;
    link->linkSlots[slotIndex].markdownNoteId = noteId;
    
    emit documentModified();
    emit requestOpenMarkdownNote(noteId, link->id);
}
```

**Estimated lines:** ~50

---

## Task M.2.2: Open Markdown Note from LinkSlot

**File:** `source/core/DocumentViewport.cpp` (modify `activateLinkSlot`)

```cpp
// In activateLinkSlot(), add/modify Markdown case:
case LinkSlot::Type::Markdown:
{
    QString noteId = slot.markdownNoteId;
    QString notePath = m_document->notesPath() + "/" + noteId + ".md";
    
    if (!QFile::exists(notePath)) {
        qDebug() << "Markdown note file not found, clearing broken reference";
        link->linkSlots[slotIndex].clear();
        emit documentModified();
        // TODO: Notify user
        return;
    }
    
    emit requestOpenMarkdownNote(noteId, link->id);
    break;
}
```

**Estimated lines:** ~15

---

## Task M.2.3: Clear LinkSlot with Markdown Note

**File:** `source/core/DocumentViewport.cpp` (modify `clearLinkSlot`)

```cpp
void DocumentViewport::clearLinkSlot(int slotIndex)
{
    if (m_selectedObjects.size() != 1) return;
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) return;
    
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) return;
    
    LinkSlot& slot = link->linkSlots[slotIndex];
    if (slot.isEmpty()) return;
    
    // If markdown slot, delete the note file
    if (slot.type == LinkSlot::Type::Markdown) {
        m_document->deleteNoteFile(slot.markdownNoteId);
    }
    
    slot.clear();
    emit documentModified();
    
    // Update slot button state
    emit linkSlotCleared(slotIndex);
}
```

**Estimated lines:** ~25

---

## Task M.2.4: Cascade Delete on LinkObject Removal

**File:** `source/core/DocumentViewport.cpp` (modify delete logic)

```cpp
void DocumentViewport::deleteSelectedObjects()
{
    if (m_selectedObjects.isEmpty()) return;
    
    // Check for markdown notes that will be deleted
    int noteCount = 0;
    for (InsertedObject* obj : m_selectedObjects) {
        if (LinkObject* link = dynamic_cast<LinkObject*>(obj)) {
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                    noteCount++;
                }
            }
        }
    }
    
    // Confirm if notes will be deleted
    if (noteCount > 0) {
        // TODO: Show confirmation dialog
        // "This will delete N linked note(s). Continue?"
        // For now, proceed without confirmation
        qDebug() << "Deleting" << noteCount << "markdown note(s)";
    }
    
    // Delete objects and their markdown notes
    for (InsertedObject* obj : m_selectedObjects) {
        if (LinkObject* link = dynamic_cast<LinkObject*>(obj)) {
            // Delete markdown note files
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                    m_document->deleteNoteFile(link->linkSlots[i].markdownNoteId);
                }
            }
        }
        
        // Remove object from page
        // ... existing deletion logic ...
    }
    
    m_selectedObjects.clear();
    emit selectionChanged();
    emit documentModified();
}
```

**Estimated lines:** ~45

---

## M.2 Testing Checklist

- [ ] Create note from empty slot → file created, slot updated
- [ ] Create note → requestOpenMarkdownNote signal emitted
- [ ] Open existing note → requestOpenMarkdownNote signal emitted
- [ ] Open broken reference → slot cleared, documentModified emitted
- [ ] Clear markdown slot → file deleted
- [ ] Delete LinkObject with notes → all note files deleted

---

# Phase M.3: Sidebar Integration

**Goal:** Update `MarkdownNoteEntry` and `MarkdownNotesSidebar` for LinkObject connection.

---

## Task M.3.1: Update MarkdownNoteEntry for LinkObject

**File:** `source/MarkdownNoteEntry.h` (modifications)

```cpp
// New data structure for display
struct NoteDisplayData {
    QString noteId;
    QString title;
    QString content;
    QString linkObjectId;  // For jump navigation
    QColor color;          // From LinkObject.iconColor
    QString description;   // From LinkObject.description (for tooltip)
};

class MarkdownNoteEntry : public QFrame {
    // ...
    
public:
    explicit MarkdownNoteEntry(const NoteDisplayData& data, QWidget *parent = nullptr);
    
    // Updated to include linkObjectId for deletion
    QString getLinkObjectId() const { return m_linkObjectId; }
    
signals:
    void deleteRequested(const QString& noteId, const QString& linkObjectId);
    void linkObjectClicked(const QString& linkObjectId);  // Replaces highlightLinkClicked
    void contentChanged(const QString& noteId);
    
private:
    QString m_linkObjectId;
    // Remove: QString highlightId - no longer needed
};
```

**Estimated lines:** ~30 changes

---

## Task M.3.2: Update MarkdownNoteEntry Implementation

**File:** `source/MarkdownNoteEntry.cpp` (modifications)

Key changes:
- Constructor takes `NoteDisplayData` instead of `MarkdownNoteData`
- Color comes from data.color (derived from LinkObject)
- Jump button emits `linkObjectClicked(m_linkObjectId)`
- Delete emits `deleteRequested(noteId, m_linkObjectId)`
- Remove highlightId references

**Estimated lines:** ~40 changes

---

## Task M.3.3: Update MarkdownNotesSidebar for File-Based Loading

**File:** `source/MarkdownNotesSidebar.h` (modifications)

```cpp
class MarkdownNotesSidebar : public QWidget {
    // ...
    
public:
    /**
     * @brief Load notes for the current page from LinkObjects.
     * @param notes List of note display data (loaded from files)
     */
    void loadNotesForPage(const QList<NoteDisplayData>& notes);
    
signals:
    void noteContentChanged(const QString& noteId, const QString& title, const QString& content);
    void noteDeleted(const QString& noteId, const QString& linkObjectId);
    void linkObjectClicked(const QString& linkObjectId);
};
```

**File:** `source/MarkdownNotesSidebar.cpp` (modifications)

Key changes:
- `loadNotesForPage()` takes `NoteDisplayData` list
- Forward `linkObjectClicked` signal from entries
- Update `noteDeleted` signal signature

**Estimated lines:** ~50 changes

---

## Task M.3.4: Add Note Loading Helper to MainWindow

**File:** `source/MainWindow.cpp` (additions)

```cpp
QList<NoteDisplayData> MainWindow::loadNotesForCurrentPage()
{
    QList<NoteDisplayData> results;
    
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return results;
    
    Document* doc = vp->document();
    int pageIndex = vp->currentPageIndex();
    Page* page = doc->page(pageIndex);
    if (!page) return results;
    
    QString notesDir = doc->notesPath();
    if (notesDir.isEmpty()) return results;
    
    for (InsertedObject* obj : page->insertedObjects) {
        LinkObject* link = dynamic_cast<LinkObject*>(obj);
        if (!link) continue;
        
        for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
            const LinkSlot& slot = link->linkSlots[i];
            if (slot.type != LinkSlot::Type::Markdown) continue;
            
            QString filePath = notesDir + "/" + slot.markdownNoteId + ".md";
            MarkdownNote note = MarkdownNote::loadFromFile(filePath);
            
            if (!note.isValid()) continue;  // File not found
            
            results.append({
                note.id,
                note.title,
                note.content,
                link->id,
                link->iconColor,
                link->description
            });
        }
    }
    
    return results;
}
```

**Estimated lines:** ~45

---

## Task M.3.5: Connect Sidebar Signals

**File:** `source/MainWindow.cpp` (additions)

```cpp
// In setupMarkdownNotesSidebar() or similar:

// When page changes, reload notes
connect(currentViewport(), &DocumentViewport::currentPageChanged, this, [this]() {
    if (markdownNotesSidebar) {
        markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
    }
});

// Handle note content changes
connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteContentChanged,
        this, [this](const QString& noteId, const QString& title, const QString& content) {
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return;
    
    QString filePath = vp->document()->notesPath() + "/" + noteId + ".md";
    MarkdownNote note;
    note.id = noteId;
    note.title = title;
    note.content = content;
    note.saveToFile(filePath);
});

// Handle note deletion from sidebar
connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeleted,
        this, [this](const QString& noteId, const QString& linkObjectId) {
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return;
    
    // Delete file
    vp->document()->deleteNoteFile(noteId);
    
    // Clear slot on LinkObject
    // ... find LinkObject and clear slot ...
    
    // Refresh sidebar
    markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
});

// Handle jump to LinkObject
connect(markdownNotesSidebar, &MarkdownNotesSidebar::linkObjectClicked,
        this, &MainWindow::navigateToLinkObject);
```

**Estimated lines:** ~50

---

## Task M.3.6: Implement Jump to LinkObject

**File:** `source/MainWindow.cpp` (additions)

```cpp
void MainWindow::navigateToLinkObject(const QString& linkObjectId)
{
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return;
    
    // Find LinkObject and its page
    Document* doc = vp->document();
    for (int pageIdx = 0; pageIdx < doc->pageCount(); ++pageIdx) {
        Page* page = doc->page(pageIdx);
        if (!page) continue;
        
        for (InsertedObject* obj : page->insertedObjects) {
            if (obj->id == linkObjectId) {
                // Navigate to page
                vp->goToPage(pageIdx);
                
                // Center on object
                vp->centerOnPoint(obj->position);
                
                // Select it
                vp->selectObject(obj);
                return;
            }
        }
    }
    
    qWarning() << "LinkObject not found:" << linkObjectId;
}
```

**Estimated lines:** ~30

---

## M.3 Testing Checklist

- [ ] Page change → sidebar shows notes for new page
- [ ] Note entry shows correct color from LinkObject
- [ ] Note entry shows description as tooltip
- [ ] Edit note → file saved
- [ ] Delete note from sidebar → file deleted, slot cleared
- [ ] Click jump button → navigates to LinkObject, selects it

---

# Phase M.4: Search Integration

**Goal:** Implement search across notes with LinkObject.description support.

---

## Task M.4.1: Update Search Implementation

**File:** `source/MarkdownNotesSidebar.cpp` (modify `performSearch`)

```cpp
void MarkdownNotesSidebar::performSearch()
{
    QString query = searchInput->text().trimmed();
    if (query.isEmpty()) {
        exitSearchMode();
        return;
    }
    
    searchMode = true;
    exitSearchButton->setVisible(true);
    
    // Get search range
    int fromPage = searchAllPagesCheckBox->isChecked() ? 0 : fromPageSpinBox->value() - 1;
    int toPage = searchAllPagesCheckBox->isChecked() ? totalPages - 1 : toPageSpinBox->value() - 1;
    
    // Request search from MainWindow
    emit searchRequested(query, fromPage, toPage);
}
```

**File:** `source/MainWindow.cpp` (add search handler)

```cpp
QList<NoteDisplayData> MainWindow::searchMarkdownNotes(
    const QString& query, int fromPage, int toPage)
{
    struct ScoredNote {
        NoteDisplayData data;
        int score;
    };
    
    QList<ScoredNote> results;
    
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return {};
    
    Document* doc = vp->document();
    QString notesDir = doc->notesPath();
    if (notesDir.isEmpty()) return {};
    
    for (int pageIdx = fromPage; pageIdx <= toPage && pageIdx < doc->pageCount(); ++pageIdx) {
        Page* page = doc->page(pageIdx);
        if (!page) continue;
        
        for (InsertedObject* obj : page->insertedObjects) {
            LinkObject* link = dynamic_cast<LinkObject*>(obj);
            if (!link) continue;
            
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                const LinkSlot& slot = link->linkSlots[i];
                if (slot.type != LinkSlot::Type::Markdown) continue;
                
                // Load note
                QString filePath = notesDir + "/" + slot.markdownNoteId + ".md";
                MarkdownNote note = MarkdownNote::loadFromFile(filePath);
                if (!note.isValid()) continue;
                
                // Score matches
                int score = 0;
                if (link->description.contains(query, Qt::CaseInsensitive)) {
                    score += 100;  // Description match highest
                }
                if (note.title.contains(query, Qt::CaseInsensitive)) {
                    score += 75;
                }
                if (note.content.contains(query, Qt::CaseInsensitive)) {
                    score += 50;
                }
                
                if (score > 0) {
                    results.append({
                        {note.id, note.title, note.content, 
                         link->id, link->iconColor, link->description},
                        score
                    });
                }
            }
        }
    }
    
    // Sort by score descending
    std::sort(results.begin(), results.end(), 
              [](const ScoredNote& a, const ScoredNote& b) {
                  return a.score > b.score;
              });
    
    // Extract data
    QList<NoteDisplayData> output;
    for (const ScoredNote& item : results) {
        output.append(item.data);
    }
    return output;
}
```

**Estimated lines:** ~80

---

## M.4 Testing Checklist

- [ ] Search by LinkObject.description → matches found
- [ ] Search by note title → matches found
- [ ] Search by note content → matches found
- [ ] Search respects page range
- [ ] Results sorted by relevance (description > title > content)
- [ ] Click search result → jump to LinkObject

---

# Phase M.5: MainWindow Integration

**Goal:** Connect all pieces in MainWindow.

---

## Task M.5.1: Handle requestOpenMarkdownNote Signal

**File:** `source/MainWindow.cpp`

```cpp
// Connect viewport signal to sidebar
connect(vp, &DocumentViewport::requestOpenMarkdownNote,
        this, [this](const QString& noteId, const QString& linkObjectId) {
    // Switch to notes sidebar tab
    if (m_leftSidebar) {
        // TODO: Switch to markdown notes tab
    }
    
    // Load notes and scroll to the new one
    if (markdownNotesSidebar) {
        markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
        markdownNotesSidebar->scrollToNote(noteId);
        markdownNotesSidebar->setNoteEditMode(noteId, true);
    }
});
```

**Estimated lines:** ~20

---

## Task M.5.2: Add scrollToNote and setNoteEditMode to Sidebar

**File:** `source/MarkdownNotesSidebar.h/cpp`

```cpp
void MarkdownNotesSidebar::scrollToNote(const QString& noteId)
{
    for (MarkdownNoteEntry* entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            scrollArea->ensureWidgetVisible(entry);
            return;
        }
    }
}

void MarkdownNotesSidebar::setNoteEditMode(const QString& noteId, bool editMode)
{
    for (MarkdownNoteEntry* entry : noteEntries) {
        if (entry->getNoteId() == noteId) {
            entry->setPreviewMode(!editMode);
            return;
        }
    }
}
```

**Estimated lines:** ~20

---

## M.5 Testing Checklist

- [ ] Create note → sidebar switches to notes tab
- [ ] Create note → new note scrolled into view
- [ ] Create note → new note in edit mode
- [ ] Open note → sidebar switches to notes tab
- [ ] Open note → note scrolled into view

---

# Summary

## Total Estimated Lines by Phase

| Phase | Description | Lines |
|-------|-------------|-------|
| M.1 | Data Model & File I/O | ~150 |
| M.2 | Core Operations | ~135 |
| M.3 | Sidebar Integration | ~245 |
| M.4 | Search Integration | ~80 |
| M.5 | MainWindow Integration | ~40 |
| **Total** | | **~650** |

## Implementation Order

1. **M.1** - Must be first (MarkdownNote class)
2. **M.2** - Depends on M.1 (file operations)
3. **M.3** - Depends on M.1, M.2 (sidebar display)
4. **M.4** - Depends on M.3 (search uses sidebar)
5. **M.5** - Final integration

## Files to Create

- `source/core/MarkdownNote.h`
- `source/core/MarkdownNote.cpp`

## Files to Modify

- `source/core/Document.h` / `Document.cpp`
- `source/core/DocumentViewport.h` / `DocumentViewport.cpp`
- `source/MarkdownNoteEntry.h` / `MarkdownNoteEntry.cpp`
- `source/MarkdownNotesSidebar.h` / `MarkdownNotesSidebar.cpp`
- `source/MainWindow.h` / `MainWindow.cpp`
- `CMakeLists.txt`

## Success Criteria

- [ ] Can create markdown note from empty LinkSlot
- [ ] Note content saved to `assets/notes/{id}.md`
- [ ] Sidebar shows notes for current page
- [ ] Note color matches LinkObject.iconColor
- [ ] Edit note → file saved
- [ ] Delete note → file deleted, slot cleared
- [ ] Delete LinkObject → cascade deletes note files
- [ ] Search finds notes by description, title, content
- [ ] Jump to LinkObject works from sidebar

---

*Subplan created for SpeedyNote Markdown Notes Integration*

