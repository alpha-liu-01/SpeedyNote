# Markdown Notes Architecture

**Last Updated:** January 15, 2026

---

## Overview

The Markdown Notes system allows users to attach markdown notes to PDF annotations (LinkObjects). Notes are stored as separate `.md` files and linked via UUID references.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              USER INTERFACE                                      │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌────────────────────────────────────────────────────────────────────────────┐ │
│  │                        MarkdownNotesSidebar                                 │ │
│  │                    (source/ui/MarkdownNotesSidebar.cpp)                     │ │
│  │                                                                             │ │
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐          │ │
│  │  │ MarkdownNoteEntry│  │ MarkdownNoteEntry│  │ MarkdownNoteEntry│ ...      │ │
│  │  │ (source/text/)   │  │                  │  │                  │          │ │
│  │  │                  │  │                  │  │                  │          │ │
│  │  │ ┌──────────────┐ │  │                  │  │                  │          │ │
│  │  │ │QMarkdownText │ │  │                  │  │                  │          │ │
│  │  │ │Edit (bundled)│ │  │                  │  │                  │          │ │
│  │  │ └──────────────┘ │  │                  │  │                  │          │ │
│  │  └──────────────────┘  └──────────────────┘  └──────────────────┘          │ │
│  │                                                                             │ │
│  │  Signals OUT:                           Signals IN:                         │ │
│  │  - noteContentSaved(id,title,content)   - loadNotesForPage(NoteDisplayData)│ │
│  │  - linkObjectClicked(linkObjId)         - displaySearchResults(...)        │ │
│  │  - noteDeletedWithLink(noteId,linkId)   - scrollToNote(noteId)             │ │
│  │  - searchRequested(query,from,to)       - setNoteEditMode(noteId,edit)     │ │
│  │  - reloadNotesRequested()                                                   │ │
│  └────────────────────────────────────────────────────────────────────────────┘ │
│                                     │                                            │
│                                     │ signals                                    │
│                                     ▼                                            │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              ORCHESTRATION                                       │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌────────────────────────────────────────────────────────────────────────────┐ │
│  │                            MainWindow                                       │ │
│  │                       (source/MainWindow.cpp)                               │ │
│  │                                                                             │ │
│  │  Key Methods:                                                               │ │
│  │  ┌─────────────────────────────────────────────────────────────────────┐   │ │
│  │  │ loadNotesForCurrentPage()                                            │   │ │
│  │  │   - Iterates Page/Tile → LinkObjects → LinkSlots                     │   │ │
│  │  │   - Loads .md files via MarkdownNote::loadFromFile()                 │   │ │
│  │  │   - Builds NoteDisplayData list with color/description from LinkObj  │   │ │
│  │  │   - Calls sidebar->loadNotesForPage()                                │   │ │
│  │  └─────────────────────────────────────────────────────────────────────┘   │ │
│  │                                                                             │ │
│  │  ┌─────────────────────────────────────────────────────────────────────┐   │ │
│  │  │ searchMarkdownNotes(query, fromPage, toPage)                         │   │ │
│  │  │   - Same iteration pattern but across page range                     │   │ │
│  │  │   - Scores matches: description(100) > title(75) > content(50)       │   │ │
│  │  │   - Returns sorted NoteDisplayData list                              │   │ │
│  │  └─────────────────────────────────────────────────────────────────────┘   │ │
│  │                                                                             │ │
│  │  ┌─────────────────────────────────────────────────────────────────────┐   │ │
│  │  │ navigateToLinkObject(linkObjectId)                                   │   │ │
│  │  │   - Finds LinkObject across all pages                                │   │ │
│  │  │   - Navigates viewport to that page                                  │   │ │
│  │  │   - Selects the LinkObject                                           │   │ │
│  │  └─────────────────────────────────────────────────────────────────────┘   │ │
│  │                                                                             │ │
│  │  Signal Connections:                                                        │ │
│  │  - sidebar.noteContentSaved → save file via MarkdownNote::saveToFile()     │ │
│  │  - sidebar.noteDeletedWithLink → Document::deleteNoteFile() + clear slot   │ │
│  │  - sidebar.linkObjectClicked → navigateToLinkObject()                      │ │
│  │  - sidebar.searchRequested → searchMarkdownNotes() → displaySearchResults()│ │
│  │  - viewport.requestOpenMarkdownNote → show sidebar + scrollToNote()        │ │
│  └────────────────────────────────────────────────────────────────────────────┘ │
│                                     │                                            │
│                                     │ calls                                      │
│                                     ▼                                            │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              DOCUMENT MODEL                                      │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌──────────────────────────┐    ┌──────────────────────────────────────────┐  │
│  │    DocumentViewport       │    │              Document                     │  │
│  │ (source/core/Document     │    │       (source/core/Document.cpp)          │  │
│  │        Viewport.cpp)      │    │                                           │  │
│  │                           │    │  Key Methods:                             │  │
│  │ Key Methods:              │    │  - notesPath() → "assets/notes/"          │  │
│  │ - createMarkdownNote      │    │  - deleteNoteFile(noteId)                 │  │
│  │   ForSlot(slotIndex)      │    │                                           │  │
│  │ - activateLinkSlot()      │    │  Contains:                                │  │
│  │ - clearLinkSlot()         │    │  - pages[] (paged mode)                   │  │
│  │ - deleteSelectedObjects() │    │  - tiles{} (edgeless mode)                │  │
│  │                           │    │                                           │  │
│  │ Signal:                   │    │  Each Page/Tile has:                      │  │
│  │ - requestOpenMarkdownNote │    │  - insertedObjects[] → LinkObject[]       │  │
│  │   (noteId, linkObjId)     │    │                                           │  │
│  └──────────────────────────┘    └──────────────────────────────────────────┘  │
│                                                                                  │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                           LinkObject                                       │  │
│  │                    (source/objects/LinkObject.h)                           │  │
│  │                                                                            │  │
│  │  Properties:                                                               │  │
│  │  - id: QString (UUID)                                                      │  │
│  │  - iconColor: QColor                    ─────────┐                         │  │
│  │  - description: QString                 ─────────┼──► Used in              │  │
│  │  - linkSlots[3]: LinkSlot[]            ─────────┘    NoteDisplayData       │  │
│  │                                                                            │  │
│  │  LinkSlot:                                                                 │  │
│  │  - type: Empty | PageJump | WebLink | Markdown                             │  │
│  │  - markdownNoteId: QString  ←──────── UUID reference to .md file           │  │
│  │                                                                            │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              FILE STORAGE                                        │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌──────────────────────────┐    ┌──────────────────────────────────────────┐  │
│  │      MarkdownNote         │    │            Disk Files                     │  │
│  │ (source/core/MarkdownNote │    │                                           │  │
│  │         .cpp)             │    │  notebook.snb/                            │  │
│  │                           │    │  └── assets/                              │  │
│  │ Data:                     │    │      └── notes/                           │  │
│  │ - id: QString (UUID)      │    │          ├── a1b2c3d4-....md             │  │
│  │ - title: QString          │    │          ├── e5f6g7h8-....md             │  │
│  │ - content: QString        │    │          └── ...                         │  │
│  │                           │    │                                           │  │
│  │ Static Methods:           │    │  File Format (YAML front matter):        │  │
│  │ - loadFromFile(path)      │◄───┤  ---                                      │  │
│  │ - saveToFile(path)        │───►│  title: "Note Title"                      │  │
│  │                           │    │  ---                                      │  │
│  │                           │    │                                           │  │
│  │                           │    │  Markdown content here...                 │  │
│  └──────────────────────────┘    └──────────────────────────────────────────┘  │
│                                                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Flow Diagrams

### 1. Creating a New Note

```
User clicks empty LinkSlot button
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ DocumentViewport::createMarkdownNoteForSlot(slotIndex)        │
│   │                                                           │
│   ├── Generate UUID: noteId = QUuid::createUuid()             │
│   │                                                           │
│   ├── Create MarkdownNote { id, title, content="" }           │
│   │                                                           │
│   ├── note.saveToFile(notesPath + "/" + noteId + ".md")       │
│   │         │                                                 │
│   │         └──► Writes YAML front matter + empty content     │
│   │                                                           │
│   ├── Update LinkSlot:                                        │
│   │     slot.type = Markdown                                  │
│   │     slot.markdownNoteId = noteId                          │
│   │                                                           │
│   └── emit requestOpenMarkdownNote(noteId, linkObjectId)      │
└───────────────────────────────────────────────────────────────┘
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MainWindow (signal handler)                                   │
│   │                                                           │
│   ├── Show/switch to markdown notes sidebar                   │
│   │                                                           │
│   ├── sidebar->loadNotesForPage(loadNotesForCurrentPage())    │
│   │                                                           │
│   ├── sidebar->scrollToNote(noteId)                           │
│   │                                                           │
│   └── sidebar->setNoteEditMode(noteId, true)                  │
└───────────────────────────────────────────────────────────────┘
```

### 2. Loading Notes for Current Page

```
Page navigation / Sidebar opened / Tab switched
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MainWindow::loadNotesForCurrentPage()                         │
│   │                                                           │
│   │  QList<NoteDisplayData> results;                          │
│   │                                                           │
│   ├── For each Page/Tile:                                     │
│   │     │                                                     │
│   │     └── For each InsertedObject:                          │
│   │           │                                               │
│   │           └── If LinkObject:                              │
│   │                 │                                         │
│   │                 └── For each LinkSlot (0-2):              │
│   │                       │                                   │
│   │                       └── If slot.type == Markdown:       │
│   │                             │                             │
│   │                             ├── Load file:                │
│   │                             │   MarkdownNote::loadFromFile│
│   │                             │                             │
│   │                             └── Build NoteDisplayData:    │
│   │                                   noteId, title, content  │
│   │                                   linkObjectId            │
│   │                                   link->iconColor         │
│   │                                   link->description       │
│   │                                                           │
│   └── return results                                          │
└───────────────────────────────────────────────────────────────┘
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MarkdownNotesSidebar::loadNotesForPage(notes)                 │
│   │                                                           │
│   ├── clearNotes()  // Delete old MarkdownNoteEntry widgets   │
│   │                                                           │
│   └── For each NoteDisplayData:                               │
│         │                                                     │
│         ├── new MarkdownNoteEntry(data)                       │
│         │     └── Uses data.color for color indicator         │
│         │     └── Uses data.description for tooltip           │
│         │     └── Contains QMarkdownTextEdit for editing      │
│         │                                                     │
│         └── Connect signals (contentChanged, etc.)            │
└───────────────────────────────────────────────────────────────┘
```

### 3. Saving Note Content

```
User types in QMarkdownTextEdit
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MarkdownNoteEntry::onContentChanged()                         │
│   │                                                           │
│   └── emit contentChanged(noteId)                             │
└───────────────────────────────────────────────────────────────┘
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MarkdownNotesSidebar::onNoteContentChanged(noteId)            │
│   │                                                           │
│   └── emit noteContentSaved(noteId, title, content)           │
└───────────────────────────────────────────────────────────────┘
            │
            ▼
┌───────────────────────────────────────────────────────────────┐
│ MainWindow (signal handler)                                   │
│   │                                                           │
│   ├── Build MarkdownNote { noteId, title, content }           │
│   │                                                           │
│   └── note.saveToFile(notesPath + "/" + noteId + ".md")       │
│               │                                               │
│               └──► Writes to disk immediately                 │
└───────────────────────────────────────────────────────────────┘
```

---

## Component Relationships

```
                    ┌─────────────────────┐
                    │  QMarkdownTextEdit  │
                    │    (markdown/)      │
                    │                     │
                    │ Bundled 3rd-party   │
                    │ markdown editor     │
                    └──────────┬──────────┘
                               │ contains
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                     MarkdownNoteEntry                             │
│                   (source/text/)                                  │
│                                                                   │
│  - UI widget for single note display/edit                         │
│  - Color indicator from NoteDisplayData.color                     │
│  - Jump button → emits linkObjectClicked                          │
│  - Delete button → emits deleteWithLinkRequested                  │
└──────────────────────────────────────────────────────────────────┘
                               │ contained by (multiple)
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                    MarkdownNotesSidebar                           │
│                   (source/ui/)                                    │
│                                                                   │
│  - Container for MarkdownNoteEntry widgets                        │
│  - Search functionality (page range, query)                       │
│  - Signal routing to MainWindow                                   │
└──────────────────────────────────────────────────────────────────┘
                               │ managed by
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                        MainWindow                                 │
│                   (source/)                                       │
│                                                                   │
│  - Loads notes via MarkdownNote::loadFromFile()                   │
│  - Builds NoteDisplayData from LinkObject properties              │
│  - Saves notes via MarkdownNote::saveToFile()                     │
│  - Connects signals between sidebar ↔ viewport                    │
└──────────────────────────────────────────────────────────────────┘
                               │ uses
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                        MarkdownNote                               │
│                   (source/core/)                                  │
│                                                                   │
│  - Pure data class (id, title, content)                           │
│  - Static file I/O: loadFromFile(), saveToFile()                  │
│  - YAML front matter format                                       │
│  - NO back-reference to LinkObject (unidirectional)               │
└──────────────────────────────────────────────────────────────────┘
                               │ stored as
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                     .md Files on Disk                             │
│                (assets/notes/{uuid}.md)                           │
│                                                                   │
│  ---                                                              │
│  title: "Note Title"                                              │
│  ---                                                              │
│                                                                   │
│  Markdown content here...                                         │
└──────────────────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

1. **Unidirectional Reference**: `LinkSlot.markdownNoteId` → file. The `.md` file does NOT store a back-reference to the LinkObject. This avoids sync issues.

2. **Color/Description at Display Time**: Note color and description come from the LinkObject when loading, not stored in the file. This allows changing colors without updating files.

3. **File-Based Storage**: Notes are separate `.md` files, not embedded in the notebook JSON. This enables:
   - Standard markdown editing tools
   - Git-friendly diffs
   - Future export features

4. **No RAM Cache**: Notes are loaded fresh each time. The `MarkdownNoteEntry` widget holds the content while displayed, then it's freed on page change.

5. **Immediate Save**: Content changes are saved immediately (no debouncing). Simple but may cause frequent disk I/O.

---

## File Locations

| Component | Path | Purpose |
|-----------|------|---------|
| `MarkdownNote` | `source/core/MarkdownNote.h/.cpp` | Data class + file I/O |
| `MarkdownNoteEntry` | `source/text/MarkdownNoteEntry.h/.cpp` | Single note UI widget |
| `MarkdownNotesSidebar` | `source/ui/MarkdownNotesSidebar.h/.cpp` | Notes container + search |
| `QMarkdownTextEdit` | `markdown/qmarkdowntextedit.h/.cpp` | Bundled editor (3rd party) |
| `LinkObject` | `source/objects/LinkObject.h/.cpp` | Annotation with slots |
| Note files | `{notebook}/assets/notes/*.md` | On-disk storage |

---

## See Also

- [MARKDOWN_NOTES_SUBPLAN.md](MARKDOWN_NOTES_SUBPLAN.md) - Implementation details and task breakdown

