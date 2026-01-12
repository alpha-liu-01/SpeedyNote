# Markdown Notes Integration Q&A

## Executive Summary

This document explores the integration of markdown notes with the new LinkObject system, replacing the old InkCanvas-based text highlight linking.

---

## Section 1: Understanding the Old System

### Q1.1: What is the old markdown note storage model?

**Old `MarkdownNoteData` Structure:**
```cpp
struct MarkdownNoteData {
    QString id;          // Unique note ID (UUID)
    QString highlightId; // Links to TextHighlight in InkCanvas
    int pageNumber;      // Page where note was created
    QString title;       // Note title
    QString content;     // Markdown content
    QColor color;        // Color indicator (matches highlight)
};
```

**Storage Location:** Notes were stored in `InkCanvas::markdownNotes` list and serialized to notebook metadata JSON.

**Key Characteristics:**
- 1:1 relationship with `TextHighlight`
- Note color derived from highlight color
- Page number stored for search filtering
- O(n) lookup to find note by ID or highlight ID

---

### Q1.2: How did cascade delete work in the old system?

From `InkCanvas.cpp`:
```cpp
// CASCADE DELETE: Remove notes linked to deleted highlights
for (const QString &highlightId : removedHighlightIds) {
    for (int i = markdownNotes.size() - 1; i >= 0; --i) {
        if (markdownNotes[i].highlightId == highlightId) {
            markdownNotes.removeAt(i);
        }
    }
}
```

**Behavior:**
- When a highlight was deleted, its linked note was **automatically deleted**
- This was a **hard cascade delete** - no orphans allowed
- Double-direction linking: highlight had `markdownWindowId`, note had `highlightId`

---

### Q1.3: What UI components exist from the old system?

| Component | Reusable? | Notes |
|-----------|-----------|-------|
| `MarkdownNoteEntry` | ✅ Mostly | Widget for displaying/editing a single note. Color indicator, title edit, QMarkdownTextEdit. |
| `MarkdownNotesSidebar` | ✅ Partially | Search, page range filtering, scroll area. Layout is solid. |
| `QMarkdownTextEdit` | ✅ Yes | Third-party markdown editor widget, fully reusable. |
| Search scoring logic | ✅ Yes | Title/content matching with relevance scoring. |
| `noteProvider` callback | ⚠️ Modify | Currently stubbed. Needs to pull from Document. |

---

## Section 2: New Architecture Design

### Q2.1: What is the relationship between LinkObject and markdown notes?

**Target Architecture:**
- A `LinkObject` can have up to 3 slots
- Each slot can be `Empty`, `Position`, `Url`, or `Markdown`
- A `Markdown` slot contains `markdownNoteId` (UUID)
- **One LinkObject can link to multiple markdown notes** (one per slot)
- **One markdown note is linked to exactly one LinkObject slot**

**This is a 1:1 relationship from note → LinkObject slot, but N:1 from LinkObject → notes.**

```
LinkObject A
├── Slot 0: Position
├── Slot 1: Markdown → Note X
└── Slot 2: Markdown → Note Y

LinkObject B
├── Slot 0: Markdown → Note Z
├── Slot 1: Empty
└── Slot 2: URL
```

---

### Q2.2: Where should markdown note content be stored?

**Option A: Embedded in Document JSON (like old system)**
```json
{
  "pages": [...],
  "markdownNotes": [
    { "id": "uuid1", "title": "...", "content": "...", "linkObjectId": "...", "slotIndex": 0 }
  ]
}
```

**Option B: Separate files in document bundle**
```
document.speedynote/
├── document.json
├── notes/
│   ├── uuid1.md
│   ├── uuid2.md
│   └── uuid3.md
└── images/
    └── ...
```

**Option C: Hybrid (metadata in JSON, content in files)**
```json
// document.json
{
  "markdownNotes": [
    { "id": "uuid1", "title": "Note Title", "linkObjectId": "...", "slotIndex": 0 }
  ]
}
```
```
notes/uuid1.md → Contains only markdown content
```

~~**Recommendation:** **Option A (Embedded)** for simplicity.~~

**Decision: Option B (Separate files in assets folder)**

**Rationale:**
- Consistency with how InsertedObjects (ImageObjects, etc.) load page-by-page
- LinkObjects live on Pages (or tiles in edgeless mode), and everything works page-by-page
- Edgeless documents could have an INFINITE number of LinkObjects - embedded JSON doesn't scale
- Notes are lazy-loaded only when needed (when LinkSlot is accessed)

**File Structure:**
```
document.speedynote/
├── document.json
├── pages/
│   ├── page_0.json      ← Contains LinkObjects with slot references
│   └── page_1.json
└── assets/
    ├── notes/
    │   ├── {uuid1}.md   ← Markdown content + title
    │   └── {uuid2}.md
    └── images/
        └── ...
```

**Note File Format (`.md` with YAML front matter):**
```markdown
---
title: "Note for: The process of osmosis..."
---

Actual markdown content here...
``` 
---

### Q2.3: What should the new data structure look like?

**On-Disk Format (assets/notes/{id}.md):**
```markdown
---
title: "Note for: The process of osmosis..."
---

Actual markdown content here...

## Section heading
More content...
```

**In-Memory Structure (for loaded notes):**
```cpp
struct MarkdownNote {
    QString id;       // UUID (also the filename without .md)
    QString title;    // From YAML front matter
    QString content;  // Markdown content (after front matter)
    
    // File I/O
    bool saveToFile(const QString& assetsPath) const;
    static MarkdownNote loadFromFile(const QString& filePath);
    
    // No linkObjectId/slotIndex stored - the LinkSlot has markdownNoteId
    // No color - derived from LinkObject.iconColor at display time
    // No pageNumber - derived from LinkObject's page at query time
};
```

**Reference Chain:**
```
Page JSON → LinkObject → LinkSlot.markdownNoteId → assets/notes/{id}.md
```

**Key Design Principles:**
- **Unidirectional reference:** LinkSlot → Note (note doesn't know its parent)
- **Lazy loading:** Note content loaded only when slot is activated
- **No redundant data:** Color/page derived from LinkObject at runtime
- **File-based:** Each note is an independent `.md` file
---

### Q2.4: How should we handle cascade delete?

**Old Behavior:** Hard cascade delete (note deleted when highlight deleted)

**Options for New System:**

| Option | On LinkObject Delete | On Slot Clear | Pros | Cons |
|--------|---------------------|---------------|------|------|
| A: Hard Delete | Delete all linked notes | Delete note | Simple, no orphans | Data loss risk |
| B: Soft Orphan | Orphan notes (keep) | Orphan note | Preserves data | Orphans accumulate |
| C: Confirm Dialog | Ask user | Ask user | User control | UX friction |
| D: Soft Delete + Cleanup | Mark as orphaned | Mark as orphaned | Recoverable | Complexity |

**Recommendation:** **Option A: Hard Delete** with confirmation dialog for LinkObjects that have markdown notes.

**Rationale:**
- Simplest implementation
- Matches old behavior
- Markdown notes are easily recreated
- Orphan files waste disk space and are confusing

**Implementation (with file deletion):**
```cpp
void DocumentViewport::deleteSelectedObject() {
    if (LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0])) {
        // Collect markdown note IDs from all slots
        QStringList noteIds;
        for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
            if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                noteIds.append(link->linkSlots[i].markdownNoteId);
            }
        }
        
        if (!noteIds.isEmpty()) {
            // Show confirmation: "This will delete N linked note(s). Continue?"
            if (!confirmDeleteWithNotes(noteIds.size())) {
                return;  // User cancelled
            }
        }
        
        // Delete note files from assets/notes/
        for (const QString& noteId : noteIds) {
            QString notePath = m_document->assetsPath() + "/notes/" + noteId + ".md";
            QFile::remove(notePath);
        }
        
        // Proceed with LinkObject deletion
        // ...
    }
}
```

**Clearing a single slot:**
```cpp
void DocumentViewport::clearLinkSlot(int slotIndex) {
    LinkObject* link = selectedLinkObject();
    if (!link) return;
    
    LinkSlot& slot = link->linkSlots[slotIndex];
    if (slot.type == LinkSlot::Type::Markdown) {
        // Delete the note file
        QString notePath = m_document->assetsPath() + "/notes/" + slot.markdownNoteId + ".md";
        QFile::remove(notePath);
    }
    
    slot.clear();
    emit documentModified();
}
```

---

### Q2.5: How should search work efficiently?

**Challenge with File-Based Storage:**
- Notes are stored as separate `.md` files
- Full content search requires loading files from disk
- Can't keep all notes in memory (defeats lazy loading for large documents)

**Search Strategy: Two-Tier Approach**

**Tier 1: Fast Search (no file I/O)**
- Search `LinkObject.description` (already in memory when page is loaded)
- This covers the primary use case: finding notes by the text they annotate

**Tier 2: Full Search (with file I/O)**
- When user explicitly searches, scan note files in `assets/notes/`
- Load and search `title` + `content` from each file
- Cache results for the search session

**Implementation:**

```cpp
struct SearchResult {
    QString noteId;
    QString linkObjectId;
    int pageIndex;
    QString title;        // Loaded from file
    QString matchContext; // Snippet showing match
    int score;
};

// Search across pages in range
QList<SearchResult> Document::searchMarkdownNotes(
    const QString& query,
    int fromPage,
    int toPage) const
{
    QList<SearchResult> results;
    
    // Iterate through pages in range
    for (int pageIdx = fromPage; pageIdx <= toPage; ++pageIdx) {
        Page* page = this->page(pageIdx);
        if (!page) continue;
        
        // Find all LinkObjects with markdown slots on this page
        for (InsertedObject* obj : page->insertedObjects) {
            LinkObject* link = dynamic_cast<LinkObject*>(obj);
            if (!link) continue;
            
            for (int slotIdx = 0; slotIdx < LinkObject::SLOT_COUNT; ++slotIdx) {
                const LinkSlot& slot = link->linkSlots[slotIdx];
                if (slot.type != LinkSlot::Type::Markdown) continue;
                
                // Score this note
                int score = 0;
                QString matchContext;
                
                // Check LinkObject description (fast, in memory)
                if (link->description.contains(query, Qt::CaseInsensitive)) {
                    score += 100;
                    matchContext = link->description;
                }
                
                // Load note file and check title + content
                MarkdownNote note = loadNoteFromFile(slot.markdownNoteId);
                if (note.title.contains(query, Qt::CaseInsensitive)) {
                    score += 75;
                    if (matchContext.isEmpty()) matchContext = note.title;
                }
                if (note.content.contains(query, Qt::CaseInsensitive)) {
                    score += 50;
                    if (matchContext.isEmpty()) {
                        // Extract context around match
                        matchContext = extractMatchContext(note.content, query);
                    }
                }
                
                if (score > 0) {
                    results.append({
                        slot.markdownNoteId,
                        link->id,
                        pageIdx,
                        note.title,
                        matchContext,
                        score
                    });
                }
            }
        }
    }
    
    // Sort by score desc, then page asc
    std::sort(results.begin(), results.end(), ...);
    return results;
}
```

**Optimization: Search Index (Optional Future Enhancement)**
For very large documents, could build a search index on document open:
```cpp
// Lightweight index: just titles (loaded from YAML front matter)
QHash<QString, QString> m_noteTitles;  // noteId -> title

// Build during page load (parse only front matter, not full content)
```

---

## Section 3: UI Considerations

### Q3.1: What changes are needed for `MarkdownNoteEntry`?

**Reusable:**
- Overall layout (header + preview/editor)
- Title editing
- Content editing with QMarkdownTextEdit
- Preview mode toggle
- Delete button

**Needs Modification:**
- Color indicator: Get from `LinkObject.iconColor` (passed as constructor arg, not stored in note)
- "Jump to highlight" button → "Jump to LinkObject"
- Signal changes: `highlightLinkClicked` → `linkObjectClicked`
- Constructor signature: Now takes `(noteId, title, content, linkObjectId, color)`

**New Feature:**
- Show LinkObject description as subtitle or tooltip

**Data Flow for Display:**
```cpp
// When loading notes for sidebar:
for (LinkObject* link : linkObjectsOnPage) {
    for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
        if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
            QString noteId = link->linkSlots[i].markdownNoteId;
            MarkdownNote note = MarkdownNote::loadFromFile(assetsPath + "/notes/" + noteId + ".md");
            
            // Pass LinkObject-derived info to widget
            MarkdownNoteEntry* entry = new MarkdownNoteEntry(
                note.id,
                note.title,
                note.content,
                link->id,           // For jump navigation
                link->iconColor,    // For color indicator
                link->description   // For tooltip/subtitle
            );
        }
    }
}
```

---

### Q3.2: What changes are needed for `MarkdownNotesSidebar`?

**Reusable:**
- Search UI (input, button, exit button)
- Page range filtering
- Scroll area layout
- Empty state handling
- Search scoring logic

**Needs Modification:**
- `setNoteProvider()` → Pull from Document instead of InkCanvas
- Search should also match `LinkObject.description`
- `highlightLinkClicked` → `linkObjectClicked`

**New Feature:**
- Click on note entry → Jump to LinkObject on canvas

---

### Q3.3: How should the sidebar interact with LinkObject selection?

**Scenario:** User is viewing notes sidebar, clicks on a note entry.

**Expected Behavior:**
1. Navigate to the page containing the LinkObject
2. Center viewport on LinkObject
3. Select the LinkObject
4. (Optional) Highlight/flash the LinkObject briefly

**Implementation:**
```cpp
// MarkdownNotesSidebar emits:
void linkObjectClicked(const QString& linkObjectId);

// MainWindow handles:
void MainWindow::onLinkObjectClicked(const QString& linkObjectId) {
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    // Find LinkObject and its page
    auto [pageIndex, object] = vp->document()->findInsertedObject(linkObjectId);
    if (!object) return;
    
    // Navigate to page
    vp->goToPage(pageIndex);
    
    // Center on object
    vp->centerOnPoint(object->position);
    
    // Select it
    vp->selectObject(object);
}
```

---

### Q3.4: How does the sidebar discover notes for the current page?

**With file-based storage, there's no central note list.** Notes are discovered by:
1. Iterating LinkObjects on the current page
2. Checking each slot for `Type::Markdown`
3. Loading the note file for each markdown slot

**Implementation:**
```cpp
QList<NoteDisplayData> loadNotesForPage(int pageIndex) {
    QList<NoteDisplayData> results;
    
    Page* page = m_document->page(pageIndex);
    if (!page) return results;
    
    QString notesDir = m_document->assetsPath() + "/notes/";
    
    for (InsertedObject* obj : page->insertedObjects) {
        LinkObject* link = dynamic_cast<LinkObject*>(obj);
        if (!link) continue;
        
        for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
            const LinkSlot& slot = link->linkSlots[i];
            if (slot.type != LinkSlot::Type::Markdown) continue;
            
            // Load note content from file
            MarkdownNote note = MarkdownNote::loadFromFile(
                notesDir + slot.markdownNoteId + ".md"
            );
            
            if (note.id.isEmpty()) continue;  // File not found
            
            results.append({
                .noteId = note.id,
                .title = note.title,
                .content = note.content,
                .linkObjectId = link->id,
                .color = link->iconColor,
                .description = link->description
            });
        }
    }
    
    return results;
}
```

**Performance Note:** This scans LinkObjects but only loads note files that exist. For pages with few LinkObjects, this is fast. For pages with many LinkObjects (rare), consider caching.

---

## Section 4: Data Flow

### Q4.1: How is a markdown note created?

**Trigger:** User presses Ctrl+0 (or similar) on an empty LinkSlot of a selected LinkObject.

**Flow:**
```
User: Ctrl+0 on empty slot
    ↓
DocumentViewport::createMarkdownNoteForSlot(slotIndex)
    ↓
1. Validate: LinkObject selected, slot is empty
    ↓
2. Generate note ID: QUuid::createUuid()
    ↓
3. Create note file: assets/notes/{noteId}.md
   ---
   title: "{LinkObject.description or 'Untitled'}"
   ---
   
   (empty content)
    ↓
4. Update LinkSlot:
   - type = Markdown
   - markdownNoteId = noteId
    ↓
5. Mark page as modified
    ↓
6. Emit documentModified()
    ↓
7. Open sidebar at markdown tab
    ↓
8. Load note into sidebar, focus for editing
```

**File Creation:**
```cpp
void DocumentViewport::createMarkdownNoteForSlot(int slotIndex) {
    LinkObject* link = selectedLinkObject();
    if (!link || !link->linkSlots[slotIndex].isEmpty()) return;
    
    // Generate ID
    QString noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Create note with default title from description
    MarkdownNote note;
    note.id = noteId;
    note.title = link->description.isEmpty() 
        ? tr("Untitled Note") 
        : link->description.left(50);
    note.content = "";
    
    // Ensure directory exists
    QString notesDir = m_document->assetsPath() + "/notes";
    QDir().mkpath(notesDir);
    
    // Save note file
    note.saveToFile(notesDir + "/" + noteId + ".md");
    
    // Update slot
    link->linkSlots[slotIndex].type = LinkSlot::Type::Markdown;
    link->linkSlots[slotIndex].markdownNoteId = noteId;
    
    emit documentModified();
    emit requestOpenMarkdownNote(noteId, link->id);  // Opens sidebar
}
```

---

### Q4.2: How is a markdown note opened?

**Trigger:** User clicks a filled Markdown slot on LinkObject, or clicks note in sidebar.

**Flow:**
```
User: Click slot with markdown note
    ↓
DocumentViewport::activateLinkSlot(slotIndex)
    ↓
case LinkSlot::Type::Markdown:
    ↓
1. Get noteId from slot.markdownNoteId
    ↓
2. Load note from file: assets/notes/{noteId}.md
    ↓
3. If not found: Clear broken reference, return
    ↓
4. emit requestOpenMarkdownNote(noteId)
    ↓
MainWindow::onOpenMarkdownNote(noteId)
    ↓
1. Switch to notes sidebar tab
    ↓
2. Find note entry in sidebar
    ↓
3. Scroll to and expand note entry
    ↓
4. Set note to edit mode (show full editor)
```

---

### Q4.3: How is a markdown note deleted?

**Trigger:** User clicks delete button on note entry, or clears the LinkSlot.

**Flow A: Delete from sidebar**
```
User: Click delete button on note entry
    ↓
MarkdownNoteEntry emits deleteRequested(noteId, linkObjectId)
    ↓
MainWindow::onMarkdownNoteDeleted(noteId, linkObjectId)
    ↓
1. Find LinkObject by linkObjectId
    ↓
2. Find which slot has this noteId
    ↓
3. Delete file: assets/notes/{noteId}.md
    ↓
4. Clear the LinkSlot (type = Empty)
    ↓
5. emit documentModified()
    ↓
6. Update sidebar display (remove entry)
```

**Flow B: Clear slot from subtoolbar (long-press)**
```
User: Long-press markdown slot button
    ↓
LinkSlotButton emits deleteRequested(slotIndex)
    ↓
DocumentViewport::clearLinkSlot(slotIndex)
    ↓
1. Get noteId from slot.markdownNoteId
    ↓
2. Delete file: assets/notes/{noteId}.md
    ↓
3. Clear the LinkSlot (type = Empty)
    ↓
4. emit documentModified()
```

**Flow C: Delete LinkObject (cascade)**
```
User: Delete LinkObject
    ↓
DocumentViewport::deleteSelectedObject()
    ↓
1. Check if LinkObject has any markdown slots
    ↓
2. If yes: Show confirmation dialog
    ↓
3. If confirmed or no notes:
   - For each markdown slot:
     - Delete file: assets/notes/{noteId}.md
   - Remove LinkObject from Page::insertedObjects
    ↓
4. emit documentModified()
```

---

## Section 5: Implementation Questions

### Q5.1: Should we keep backward compatibility with old note format?

**Options:**
1. **Migration on load:** Convert old `MarkdownNoteData` to new format when loading old documents
2. **Dual support:** Support both formats indefinitely
3. **Breaking change:** Old notes are lost

**Recommendation:** **Option 1: Migration on load**

The old format is not widely deployed, and migration is straightforward:
- Old notes without highlights become orphaned (delete them)
- Old notes with highlights... but we don't have text highlights anymore in DocumentViewport!

**Reality Check:** The old markdown system was tied to `InkCanvas` which is being phased out. There's no migration path because `TextHighlight` doesn't exist in the new architecture.

**Actual Recommendation:** **Option 3: Breaking change** (with explanation in release notes)
- Old notes stored in InkCanvas notebook metadata will simply not be loaded
- New notes are a fresh start with LinkObjects

I agree with Option 3. Never consider backwards compatibility. 
---

### Q5.2: What file changes are needed?

**New Files:**
- `source/core/MarkdownNote.h` - Data structure + file I/O (YAML front matter parsing)
- `source/core/MarkdownNote.cpp` - `saveToFile()`, `loadFromFile()`

**Modified Files:**
- `source/core/Document.h/cpp` - Add `assetsPath()` helper, note file management
- `source/core/DocumentViewport.h/cpp` - Note creation/deletion/opening methods
- `source/MainWindow.cpp` - Connect sidebar signals, handle note open requests
- `source/MarkdownNoteEntry.h/cpp` - Update for LinkObject connection (get color/description from parent)
- `source/MarkdownNotesSidebar.h/cpp` - Update to load notes from files, search via LinkObjects

**No Changes Needed:**
- `source/objects/LinkObject.h/cpp` - Already has `LinkSlot::Type::Markdown` and `markdownNoteId`

---

### Q5.3: What is the note title default behavior?

**Options:**
1. Default to LinkObject.description
2. Default to "Untitled Note"
3. Default to empty (placeholder shown)

**Recommendation:** **Option 1: Default to LinkObject.description**

For text highlights, the description contains the extracted text, which is the perfect default title.

```cpp
note.title = link->description.left(50);  // Truncate long extractions
```
I agree with Option 1.
---

## Section 6: Open Questions for Discussion

### Q6.1: Should the sidebar show notes for all LinkObjects on current page, or all LinkObjects in document?

**Option A: Current page only** (like old system)
- Shows notes linked to LinkObjects on the current page
- Page change → different notes shown
- Search expands to page range

**Option B: All pages (global view)**
- Shows all notes in document
- No filtering by current page
- Search filters within this global list

**Recommendation:** **Option A** to match existing UX expectations.
Option A. 
---

### Q6.2: What keyboard shortcut should create a markdown note?

**Current plan:** Ctrl+0 through Ctrl+2 activate slots 0-2

**For creating note on empty slot:**
- Same shortcut (Ctrl+0 on empty slot creates note and opens it)
- Or separate shortcut (Ctrl+Shift+N to create note in first empty slot)

**Recommendation:** **Same shortcut** - Ctrl+0/1/2 on empty markdown slot creates and opens note.
Ctrl+2 is already take by the 1-column/2-column layout toggle. So I don't think extra keyboard shortcuts are needed here. SpeedyNote is designed for tablet pcs. You may reserve the interface here for connecting to the control panel (for keyboard shortcut customization). 
---

### Q6.3: How should we handle the "Jump to LinkObject" action?

**Old behavior:** "🔗" button on note entry jumps to highlight

**New behavior options:**
1. Jump to LinkObject position on canvas
2. Jump and select the LinkObject
3. Jump, select, and open LinkObject's first position slot (if any)

**Recommendation:** **Option 2: Jump and select**
- Centers view on LinkObject
- Selects it (shows slot buttons in subtoolbar)
- User can then activate any slot

I agree with Option 2. 
---

### Q6.4: Should note color be editable independently of LinkObject color?

**Old system:** Note color matched highlight color, not editable separately

**Options:**
1. **Derived only:** Note always shows LinkObject.iconColor
2. **Optional override:** Note can have its own color, defaults to LinkObject color
3. **Independent:** Note has its own color, no connection to LinkObject

**Recommendation:** **Option 1: Derived only**
- Simpler data model
- Consistent visual connection between note and LinkObject
- Less UI to implement
I agree with Option 1. 
---

## Section 7: Summary of Key Decisions

| Decision | Answer | Status |
|----------|--------|--------|
| Storage location | Separate files in `assets/notes/` | ✅ Confirmed |
| File format | `.md` with YAML front matter for title | ✅ Confirmed |
| Cascade delete | Hard delete with confirmation | ✅ Confirmed |
| Note → LinkObject link | Unidirectional: `LinkSlot.markdownNoteId` | ✅ Confirmed |
| Note color | Derived from `LinkObject.iconColor` | ✅ Confirmed |
| Note title default | `LinkObject.description.left(50)` | ✅ Confirmed |
| Sidebar mode | Current page notes only | ✅ Confirmed |
| Keyboard shortcuts | No dedicated shortcuts (tablet-first design) | ✅ Confirmed |
| Jump to LinkObject | Jump and select (show in subtoolbar) | ✅ Confirmed |
| Old format migration | Breaking change (no migration) | ✅ Confirmed |
| Search fields | description + title + content | ✅ Confirmed |

**Design Notes:**
- SpeedyNote is designed for tablet PCs - UI should be touch-friendly
- Keyboard shortcut customization can be added via control panel in future
- Ctrl+2 is already taken by layout toggle

---

## Section 8: Implementation Plan Outline

### Phase 1: Data Model & File I/O
- [ ] Define `MarkdownNote` struct (id, title, content)
- [ ] Implement `saveToFile()` with YAML front matter
- [ ] Implement `loadFromFile()` parsing YAML + content
- [ ] Add `Document::assetsPath()` helper
- [ ] Create `assets/notes/` directory on first note creation

### Phase 2: Core Operations
- [ ] Create note from empty slot (Ctrl+0/1/2)
  - Generate UUID
  - Create file in `assets/notes/{id}.md`
  - Set `LinkSlot.markdownNoteId`
- [ ] Open note from filled slot
  - Load file from `assets/notes/{id}.md`
  - Display in sidebar
- [ ] Delete note (clear slot)
  - Delete file from `assets/notes/`
  - Clear `LinkSlot`
- [ ] Cascade delete (LinkObject deletion)
  - Delete all linked note files
  - Remove LinkObject

### Phase 3: Sidebar Integration
- [ ] Modify `MarkdownNoteEntry` for LinkObject connection
  - Get color from LinkObject.iconColor
  - Get description for display
  - Pass linkObjectId for jump navigation
- [ ] Modify `MarkdownNotesSidebar` to load notes from files
- [ ] Implement "Jump to LinkObject" action

### Phase 4: Search
- [ ] Scan `assets/notes/` files during search
- [ ] Include LinkObject.description in scoring
- [ ] Maintain relevance scoring (description > title > content)
- [ ] Page range filtering via LinkObject pages

### Phase 5: Testing
- [ ] Create note → verify file created + slot updated
- [ ] Edit note → verify file saved
- [ ] Delete note → verify file deleted + slot cleared
- [ ] Delete LinkObject → verify cascade file deletion
- [ ] Search → verify results from files
- [ ] Jump to LinkObject → verify navigation

---

*Document created for SpeedyNote Markdown Notes Integration*

