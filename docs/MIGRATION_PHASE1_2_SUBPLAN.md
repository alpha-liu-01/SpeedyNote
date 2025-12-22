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

### Task 1.2.4: Add PDF Reference Management

Add PDF path, provider, and relink functionality to Document.

**Additions to Document class:**

```cpp
// ===== PDF Reference =====
QString pdfPath;                                    // Path to external PDF
std::unique_ptr<PdfProvider> pdfProvider;           // Loaded PDF (may be null)

// Methods
bool hasPdf() const;
bool loadPdf(const QString& path);
bool relinkPdf(const QString& newPath);
void unloadPdf();
QPixmap renderPdfPage(int pageIndex, qreal dpi) const;
int pdfPageCount() const;
QSizeF pdfPageSize(int pageIndex) const;
```

**Dependencies:** Tasks 1.2.1, 1.2.2, 1.2.3
**Estimated size:** ~80 lines

---

### Task 1.2.5: Add Page Management Methods

Methods to add, remove, insert, reorder pages.

**Additions to Document class:**

```cpp
// ===== Pages =====
private:
    std::vector<std::unique_ptr<Page>> m_pages;

public:
    // Access
    int pageCount() const;
    Page* page(int index);
    const Page* page(int index) const;
    
    // Modification
    Page* addPage();                                    // Add at end
    Page* insertPage(int index);                        // Insert at position
    Page* addPageForPdf(int pdfPageIndex);              // Add with PDF background
    bool removePage(int index);
    bool movePage(int from, int to);
    
    // Edgeless mode
    Page* edgelessPage();                               // Returns the single page (mode must be Edgeless)
    
    // Utility
    void ensureMinimumPages();                          // Ensure at least 1 page exists
    int findPageByPdfPage(int pdfPageIndex) const;      // Find doc page for PDF page (-1 if not found)
```

**Dependencies:** Task 1.2.3
**Estimated size:** ~150 lines

---

### Task 1.2.6: Add Bookmarks Support

Bookmark storage and management.

**Design Decision:** Page-centric with Document index
- Each Page has `isBookmarked` flag and `bookmarkLabel`
- Document provides quick access methods

**Additions to Page class:**

```cpp
// ===== Bookmarks (add to Page) =====
bool isBookmarked = false;
QString bookmarkLabel;
```

**Additions to Document class:**

```cpp
// ===== Bookmarks =====
struct Bookmark {
    int pageIndex;
    QString label;
};

QVector<Bookmark> getBookmarks() const;                 // Get all bookmarked pages
void setBookmark(int pageIndex, const QString& label);
void removeBookmark(int pageIndex);
bool hasBookmark(int pageIndex) const;
int nextBookmark(int fromPage) const;                   // Navigate to next bookmark
int prevBookmark(int fromPage) const;                   // Navigate to previous bookmark
```

**Dependencies:** Task 1.2.5
**Estimated size:** ~80 lines

---

### Task 1.2.7: Add Serialization

JSON serialization matching extended `.speedynote_metadata.json` format.

**Additions to Document class:**

```cpp
// ===== Serialization =====
QJsonObject toJson() const;                             // Metadata only (not page content)
static std::unique_ptr<Document> fromJson(const QJsonObject& obj);

QJsonObject toFullJson() const;                         // Include all pages
static std::unique_ptr<Document> fromFullJson(const QJsonObject& obj);

// Save/Load helpers (uses SpnPackageManager internally)
bool saveToSpn(const QString& spnPath);
static std::unique_ptr<Document> loadFromSpn(const QString& spnPath);
```

**Metadata JSON structure:**

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
        "color": "#ffffff",
        "grid_spacing": 20,
        "line_spacing": 24
    },
    "page_count": 10,
    "pages": [ ... ]
}
```

**Dependencies:** Tasks 1.2.3-1.2.6
**Estimated size:** ~200 lines

---

### Task 1.2.8: Unit Tests for Document

Test serialization, page management, PDF reference, and bookmarks.

**File:** `source/core/DocumentTests.h`

```cpp
namespace DocumentTests {
    bool testDocumentCreation();
    bool testPageManagement();
    bool testBookmarks();
    bool testSerializationRoundTrip();
    bool testPdfReference();          // Mock PDF or skip if not available
    bool runAllTests();
}
```

**Usage:** `NoteApp.exe --test-document`

**Dependencies:** All above tasks
**Estimated size:** ~300 lines

---

## Task Summary Table

| Task | Description | Dependencies | Est. Lines | Status |
|------|-------------|--------------|------------|--------|
| 1.2.1 | PdfProvider interface | None | 60 | [✅] |
| 1.2.2 | PopplerPdfProvider | 1.2.1 | 150 | [✅] |
| 1.2.3 | Document skeleton | Page (1.1) | 150 | [✅] |
| 1.2.4 | PDF reference management | 1.2.1-1.2.3 | 80 | [ ] |
| 1.2.5 | Page management | 1.2.3 | 150 | [ ] |
| 1.2.6 | Bookmarks | 1.2.5 | 80 | [ ] |
| 1.2.7 | Serialization | 1.2.3-1.2.6 | 200 | [ ] |
| 1.2.8 | Unit tests | All above | 300 | [ ] |
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
