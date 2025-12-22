# Phase 1.2 Detailed Subplan: Document Class

> **Purpose:** Create the Document class that owns Pages, PDF reference, and metadata.
> **Goal:** A complete in-memory representation of a notebook file.

---

## Overview

The Document class is the central data structure representing an open notebook. It:
- Owns all Pages (paged or edgeless mode)
- References external PDF (not embedded)
- Stores metadata (name, author, dates, settings)
- Manages bookmarks
- Handles serialization to/from the existing `.spn` format

---

## Directory Structure (New Files)

```
source/
├── core/
│   ├── Page.h / Page.cpp           ← (already done in 1.1)
│   ├── Document.h / Document.cpp   ← NEW
│   └── DocumentTests.h             ← NEW (unit tests)
│
├── pdf/                            ← NEW: PDF abstraction layer
│   ├── PdfProvider.h               ← Abstract interface
│   └── PopplerPdfProvider.h/.cpp   ← Poppler implementation
```

---

## Task Breakdown

### Task 1.2.1: Create `pdf/` Directory and PdfProvider Interface ✅ COMPLETE

Abstract interface for PDF operations, enabling future backend swaps (Android, MuPDF, etc.).

**File created:** `source/pdf/PdfProvider.h` ✅

**Features implemented:**
- **Data structs:** `PdfTextBox`, `PdfLink`, `PdfOutlineItem` (platform-independent)
- **Document info:** `isValid()`, `isLocked()`, `pageCount()`, `title()`, `author()`, `subject()`, `filePath()`
- **Outline (TOC):** `outline()`, `hasOutline()`
- **Page info:** `pageSize()`
- **Rendering:** `renderPageToImage()`, `renderPageToPixmap()` (with default impl)
- **Text selection:** `textBoxes()`, `supportsTextExtraction()`
- **Links:** `links()`, `supportsLinks()`
- **Factory:** `create()`, `isAvailable()`

**Design decisions:**
- Uses simple data structs (not Poppler types) for portability
- Link areas use normalized coordinates (0-1) for consistency
- Text boxes use PDF points (72 dpi) coordinates
- `renderPageToPixmap()` has default implementation using `renderPageToImage()`

**Dependencies:** None
**Actual size:** ~220 lines (with documentation)

---

### Task 1.2.2: Create PopplerPdfProvider Implementation ✅ COMPLETE

Wraps Poppler-Qt6 calls behind the abstract interface.

**Files created:**
- `source/pdf/PopplerPdfProvider.h` ✅
- `source/pdf/PopplerPdfProvider.cpp` ✅

**Features implemented:**
- **Constructor:** Loads PDF and applies render hints (Antialiasing, TextAntialiasing, TextHinting, TextSlightHinting)
- **Document info:** All metadata methods (title, author, subject, etc.)
- **Outline:** Recursive conversion of Poppler::OutlineItem to PdfOutlineItem
- **Page rendering:** `renderPageToImage()` at specified DPI
- **Text extraction:** Converts Poppler::TextBox to PdfTextBox with character-level bounding boxes
- **Links:** Converts Poppler::Link types (Goto, Browse) to PdfLink with proper page number conversion
- **Factory methods:** `PdfProvider::create()` and `PdfProvider::isAvailable()` implemented

**Key implementation details:**
- Poppler uses 1-based page numbers → converted to 0-based
- Link areas already normalized (0-1) from Poppler
- Character bounding boxes extracted for precise text selection
- Render hints match existing InkCanvas implementation for consistency

**Dependencies:** Task 1.2.1, Poppler-Qt6
**Actual size:** ~80 lines header + ~220 lines cpp = ~300 lines total

---

### Task 1.2.3: Create Document Class Skeleton ✅ COMPLETE

Basic Document structure with identity, mode, and page storage.

**Files created:**
- `source/core/Document.h` ✅
- `source/core/Document.cpp` ✅

**Features implemented:**
- **Identity:** id (UUID), name, author, created, lastModified, formatVersion
- **Mode:** enum class Mode { Paged, Edgeless }
- **Default settings:** defaultBackgroundType, defaultBackgroundColor, defaultGridColor, defaultGridSpacing, defaultLineSpacing, defaultPageSize
- **State:** modified, lastAccessedPage
- **Rule of Five:** Non-copyable, movable
- **Factory methods:** `createNew()`, `createForPdf()` (stub for 1.2.4)
- **Utility:** `markModified()`, `clearModified()`, `displayName()`, `isEdgeless()`, `isPaged()`

**Design notes:**
- Document is a skeleton; page management (1.2.5), PDF (1.2.4), bookmarks (1.2.6), and serialization (1.2.7) will be added incrementally
- `createForPdf()` is a stub - full implementation in Task 1.2.4
- Default page size is US Letter at 96 DPI (816x1056 pixels)

**Dependencies:** Page class (1.1)
**Actual size:** ~140 lines header + ~50 lines cpp

---

### Task 1.2.4: Add PDF Reference Management ✅ COMPLETE

Add PDF path, provider, and relink functionality to Document.

**Files modified:**
- `source/core/Document.h` ✅ (added ~100 lines)
- `source/core/Document.cpp` ✅ (added ~120 lines)

**Features implemented:**
- **State queries:** `hasPdfReference()`, `isPdfLoaded()`, `pdfFileExists()`, `pdfPath()`
- **PDF loading:** `loadPdf()`, `relinkPdf()`, `unloadPdf()`, `clearPdfReference()`
- **Rendering:** `renderPdfPageToImage()`, `renderPdfPageToPixmap()`
- **PDF info:** `pdfPageCount()`, `pdfPageSize()`, `pdfTitle()`, `pdfAuthor()`
- **Outline:** `pdfHasOutline()`, `pdfOutline()`
- **Advanced access:** `pdfProvider()` returns const pointer for text boxes, links, etc.

**Design decisions:**
- `m_pdfPath` is stored even if loading fails (enables relink workflow)
- `loadPdf()` stores path first, then attempts load
- `relinkPdf()` calls `loadPdf()` and marks document modified on success
- `unloadPdf()` releases resources but preserves path for relink
- `clearPdfReference()` removes both provider and path
- `createForPdf()` now actually loads the PDF

**Dependencies:** Tasks 1.2.1, 1.2.2, 1.2.3
**Actual size:** ~220 lines added (header + implementation)

---

### Task 1.2.5: Add Page Management Methods ✅ COMPLETE

Methods to add, remove, insert, reorder pages.

**Files modified:**
- `source/core/Document.h` ✅ (added ~90 lines)
- `source/core/Document.cpp` ✅ (added ~170 lines)

**Features implemented:**
- **Storage:** `std::vector<std::unique_ptr<Page>> m_pages`
- **Access:** `pageCount()`, `page(int index)` (const and non-const)
- **Modification:** `addPage()`, `insertPage()`, `addPageForPdf()`, `removePage()`, `movePage()`
- **Edgeless mode:** `edgelessPage()` (const and non-const)
- **Utility:** `ensureMinimumPages()`, `findPageByPdfPage()`, `createPagesForPdf()`
- **Helper:** `createDefaultPage()` (private)

**Design decisions:**
- `removePage()` prevents removing the last page (enforces minimum of 1)
- `addPageForPdf()` converts PDF page size from 72 dpi to 96 dpi
- `createPagesForPdf()` clears existing pages and creates fresh ones
- `ensureMinimumPages()` creates a 4096x4096 page for edgeless mode
- Factory methods now create initial pages automatically
- `createForPdf()` creates one page per PDF page with BackgroundType::PDF

**Dependencies:** Task 1.2.3, Page class (1.1)
**Actual size:** ~260 lines added (header + implementation)

---

### Task 1.2.6: Add Bookmarks Support ✅ COMPLETE

Bookmark storage and management.

**Design Decision:** Page-centric with Document index
- Each Page has `isBookmarked` flag and `bookmarkLabel`
- Document provides quick access methods

**Files modified:**
- `source/core/Page.h` ✅ (added 2 fields)
- `source/core/Page.cpp` ✅ (added serialization ~6 lines)
- `source/core/Document.h` ✅ (added ~75 lines)
- `source/core/Document.cpp` ✅ (added ~100 lines)

**Features implemented:**
- **Page fields:** `isBookmarked`, `bookmarkLabel`
- **Page serialization:** Bookmark fields included in toJson/fromJson
- **Document struct:** `Bookmark { pageIndex, label }`
- **Document methods:**
  - `getBookmarks()` - returns all bookmarks sorted by page
  - `setBookmark()` - add/update bookmark with optional label
  - `removeBookmark()` - remove bookmark from page
  - `hasBookmark()` - check if page is bookmarked
  - `bookmarkLabel()` - get label for bookmarked page
  - `nextBookmark()` / `prevBookmark()` - navigate with wrap-around
  - `toggleBookmark()` - convenience method for toggle
  - `bookmarkCount()` - total number of bookmarks

**Design notes:**
- Default bookmark label is "Bookmark N" (1-based page number)
- Navigation methods wrap around (next from last goes to first)
- Compatible with existing MainWindow bookmark UI patterns

**Dependencies:** Task 1.2.5
**Actual size:** ~180 lines added

---

### Task 1.2.7: Add Serialization ✅ COMPLETE

JSON serialization matching extended `.speedynote_metadata.json` format.

**Files modified:**
- `source/core/Document.h` ✅ (added ~80 lines)
- `source/core/Document.cpp` ✅ (added ~200 lines)

**Features implemented:**
- **Metadata serialization:** `toJson()` / `fromJson()` - document metadata only
- **Full serialization:** `toFullJson()` / `fromFullJson()` - includes all page content
- **Page serialization:** `pagesToJson()` / `loadPagesFromJson()`
- **Background settings:** `defaultBackgroundToJson()` / `loadDefaultBackgroundFromJson()`
- **Enum converters:** `backgroundTypeToString()`, `stringToBackgroundType()`, `modeToString()`, `stringToMode()`

**JSON structure (metadata):**
```json
{
    "format_version": "2.0",
    "notebook_id": "uuid",
    "name": "My Notebook",
    "author": "",
    "created": "2025-01-01T12:00:00Z",
    "last_modified": "2025-01-15T09:30:00Z",
    "mode": "paged",
    "pdf_path": "/path/to/document.pdf",
    "last_accessed_page": 5,
    "default_background": {
        "type": "grid",
        "color": "#ffffffff",
        "grid_color": "#ffc8c8c8",
        "grid_spacing": 20,
        "line_spacing": 24,
        "page_width": 816,
        "page_height": 1056
    },
    "page_count": 10
}
```

**Design notes:**
- `fromJson()` does NOT auto-load PDF - call `loadPdf()` separately if needed
- Legacy format support: reads flat `background_style`, `background_color`, `background_density`
- Page content kept separate from metadata for efficiency (can read metadata without loading strokes)
- No `saveToSpn()`/`loadFromSpn()` yet - those will integrate with SpnPackageManager later

**Dependencies:** Tasks 1.2.3-1.2.6, Page serialization (1.1)
**Actual size:** ~280 lines added

---

### Task 1.2.8: Unit Tests for Document ✅ COMPLETE

Test serialization, page management, PDF reference, and bookmarks.

**Files created/modified:**
- `source/core/DocumentTests.h` ✅ (~600 lines)
- `source/Main.cpp` ✅ (added --test-document flag)

**Tests implemented:**

| Test | What it validates |
|------|-------------------|
| `testDocumentCreation()` | Factory methods (`createNew()`, edgeless mode), default values, UUIDs |
| `testPageManagement()` | `addPage()`, `insertPage()`, `removePage()`, `movePage()`, min page constraint |
| `testBookmarks()` | `setBookmark()`, `removeBookmark()`, `nextBookmark()`, `prevBookmark()` wrap-around, `toggleBookmark()` |
| `testSerializationRoundTrip()` | `toFullJson()` → `fromFullJson()` data integrity |
| `testPdfReference()` | `loadPdf()` failure handling, path stored for relink, `clearPdfReference()` |
| `testMetadataOnlySerialization()` | `toJson()` vs `toFullJson()`, `loadPagesFromJson()` |
| `testActualPdfLoad()` | **Real PDF tests** (requires `1.pdf` in exe folder): `createForPdf()`, page count, page size scaling (72→96 dpi), PDF background type, rendering at multiple DPIs, serialization with PDF, insert page in PDF doc, unload/reload |

**Usage:** `NoteApp.exe --test-document`

**Expected output:** All tests PASS, returning exit code 0

**Dependencies:** All above tasks (1.2.1-1.2.7)
**Actual size:** ~600 lines

---

## Task Summary Table

| Task | Description | Dependencies | Est. Lines | Status |
|------|-------------|--------------|------------|--------|
| 1.2.1 | PdfProvider interface | None | 60 | [✅] |
| 1.2.2 | PopplerPdfProvider | 1.2.1 | 150 | [✅] |
| 1.2.3 | Document skeleton | Page (1.1) | 150 | [✅] |
| 1.2.4 | PDF reference management | 1.2.1-1.2.3 | 80 | [✅] |
| 1.2.5 | Page management | 1.2.3 | 150 | [✅] |
| 1.2.6 | Bookmarks | 1.2.5 | 80 | [✅] |
| 1.2.7 | Serialization | 1.2.3-1.2.6 | 200 | [✅] |
| 1.2.8 | Unit tests | All above | 300 | [✅] |
| **TOTAL** | | | **~1170** | |

---

## Execution Order

```
1.2.1 (PdfProvider interface)
    ↓
1.2.2 (PopplerPdfProvider)
    ↓
1.2.3 (Document skeleton)
    ↓
┌───┴───┐
1.2.4   1.2.5
(PDF)   (Pages)
    ↓       ↓
    └───┬───┘
        ↓
      1.2.6
    (Bookmarks)
        ↓
      1.2.7
  (Serialization)
        ↓
      1.2.8
     (Tests)
```

Tasks 1.2.4 and 1.2.5 can be done in **parallel** after 1.2.3.

---

## Notes for Implementation

1. **Keep SpnPackageManager** - Document uses it for file I/O, don't duplicate
2. **PDF is optional** - Document works fine without PDF (blank notebook)
3. **Edgeless mode** - Single Page with no fixed size, coordinates can be negative
4. **Page indices** - Always 0-based, `pdfPageNumber` on Page is separate from position
5. **Backwards compatibility** - Read old metadata format, write new format
6. **No UI yet** - Document is pure data, Viewport comes in Phase 1.3

---

## After Phase 1.2

Once Document is complete:
- [ ] Phase 1.3: Create DocumentViewport (renders Document, handles input)
- [ ] Phase 1.4: Integration tests with real files

---

*Subplan created for SpeedyNote Document class migration*
