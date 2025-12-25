# doc-1: Document Loading Integration Subplan

> **Purpose:** Complete document loading/saving integration with DocumentViewport
> **Created:** Dec 24, 2024
> **Status:** 🔄 PLANNING

---

## Overview

This subplan covers the integration of Document loading, saving, and PDF handling into MainWindow with DocumentViewport. The goal is to establish a working document persistence system before MainWindow modularization.

---

## Temporary Keyboard Shortcuts

These shortcuts are temporary until the toolbar is migrated. They will be removed or made customizable later.

| Shortcut | Action | Notes |
|----------|--------|-------|
| `Ctrl+S` | Save JSON | Opens file dialog, user picks location |
| `Ctrl+O` | Load JSON | Opens file dialog, user picks file |
| `Ctrl+Shift+O` | Open PDF | Opens file dialog, creates PDF-backed document |
| `Ctrl+Shift+N` | New Edgeless | Creates new edgeless document in new tab |
| `Ctrl+Shift+A` | Add Page | Appends new page at end of document |
| `Ctrl+Shift+I` | Insert Page | Inserts new page after current page |

---

## Task Breakdown

### Phase 1: Core Save/Load Infrastructure

#### 1.0 Implement Add Page (Prerequisite)
**Goal:** Add new page at end of document so we can test multi-page save/load

**Rationale:** Without this, we only have 1 page and can't properly test multi-page serialization.

**Requirements:**
- Get current Document from viewport
- Call `Document::addPage()`
- Trigger viewport repaint
- Optionally scroll to new page

**Code flow:**
```
User presses Ctrl+Shift+A
  → MainWindow::addPageToDocument()
    → Get Document from current viewport
    → Document::addPage()
    → Viewport::update()
```

#### 1.1 Implement Save Document Flow
**Goal:** Save current document to JSON file via file dialog

**Requirements:**
- Open OS file dialog for location selection
- Serialize Document using `toFullJson()`
- Write JSON to selected file path
- Show success/error feedback
- **Must be modular** - future features depend on this

**Files to modify:**
- `source/MainWindow.cpp` - Add save handler
- `source/MainWindow.h` - Declare save method

**Code flow:**
```
User presses Ctrl+S
  → MainWindow::saveDocument()
    → Get current DocumentViewport
    → Get Document from viewport
    → Show QFileDialog::getSaveFileName()
    → Document::toFullJson()
    → Write to file
    → Show status (success/error)
```

#### 1.2 Implement Load Document Flow
**Goal:** Load document from JSON file via file dialog

**Requirements:**
- Open OS file dialog for file selection
- Read JSON from file
- Deserialize using `Document::fromFullJson()`
- Create new tab with DocumentViewport
- Set document on viewport

**Files to modify:**
- `source/MainWindow.cpp` - Add load handler
- `source/MainWindow.h` - Declare load method

**Code flow:**
```
User presses Ctrl+O
  → MainWindow::loadDocument()
    → Show QFileDialog::getOpenFileName()
    → Read file contents
    → Document::fromFullJson()
    → Create new tab (via TabManager)
    → Set document on viewport
    → Show status (success/error)
```

#### 1.3 Connect Keyboard Shortcuts
**Goal:** Wire up Ctrl+S, Ctrl+O, and Ctrl+Shift+A to handlers

**Implementation:** Use `QShortcut` with `Qt::ApplicationShortcut` context for guaranteed behavior regardless of focus.

**Files to modify:**
- `source/MainWindow.cpp` - Create QShortcut instances

---

### Phase 2: PDF Loading Integration

#### 2.1 Implement Open PDF Flow
**Goal:** Load PDF file and create PDF-backed document

**Requirements:**
- Open OS file dialog for PDF selection
- Create Document using `Document::createForPdf()`
- Use PdfProvider interface (not PopplerPdfProvider directly)
- Create new tab with DocumentViewport
- Set document on viewport
- PDF pages should render in viewport

**Files to modify:**
- `source/MainWindow.cpp` - Add PDF open handler
- `source/MainWindow.h` - Declare method

**Code flow:**
```
User presses Ctrl+Shift+O
  → MainWindow::openPdfDocument()
    → Show QFileDialog::getOpenFileName() with PDF filter
    → Document::createForPdf(name, path)
    → Create new tab (via TabManager)
    → Set document on viewport
    → Viewport renders PDF pages
```

#### 2.2 Verify PDF Rendering
**Goal:** Confirm PDF pages render correctly in DocumentViewport

**Test cases:**
- Single page PDF
- Multi-page PDF
- PDF with different page sizes
- Zooming and scrolling work correctly

---

### Phase 3: Insert Page (PDF-Aware)

> **Note:** Add Page was moved to Phase 1.0 as a prerequisite for Save/Load testing.
> Insert Page is more complex because it must handle PDF-backed documents correctly.

#### 3.1 Implement Insert Page
**Goal:** Insert new page after current page

**Complexity with PDF:**
- For non-PDF documents: Simply insert a blank page
- For PDF documents: Must decide what background the new page gets
  - Option A: Insert blank page (no PDF background)
  - Option B: Duplicate current page's PDF background
  - Option C: Block insertion (PDF pages are fixed)
  
  → **Decision needed during implementation**

**Requirements:**
- Get current page index from viewport
- Determine document type (PDF vs non-PDF)
- Call `Document::insertPage(currentIndex + 1)` with appropriate settings
- Trigger viewport repaint
- Optionally scroll to new page

**Code flow:**
```
User presses Ctrl+Shift+I
  → MainWindow::insertPageInDocument()
    → Get current page index from viewport
    → Get Document from viewport
    → Check if document has PDF
    → Document::insertPage(currentIndex + 1, appropriateSettings)
    → Viewport::update()
```

#### 3.2 Connect Keyboard Shortcut
**Goal:** Wire up Ctrl+Shift+I

---

### Phase 4: Edgeless Mode

#### 4.1 Implement New Edgeless Document
**Goal:** Create new edgeless document in new tab

**Requirements:**
- Create Document using `Document::createNew(name, Mode::Edgeless)`
- Create new tab with DocumentViewport
- Set document on viewport
- Verify edgeless page renders correctly

**Code flow:**
```
User presses Ctrl+Shift+N
  → MainWindow::newEdgelessDocument()
    → Document::createNew("Untitled", Document::Mode::Edgeless)
    → Create new tab (via TabManager)
    → Set document on viewport
```

#### 4.2 Test Edgeless Behavior
**Goal:** Verify edgeless mode works correctly

**Test cases:**
- Edgeless page has large size (4096x4096 per Document.cpp)
- Drawing works across the large canvas
- Pan/zoom works correctly
- Save/load preserves edgeless mode
- Strokes are saved and loaded correctly

---

### Phase 5: LayerPanel Integration

#### 5.1 Add LayerPanel to MainWindow
**Goal:** Show LayerPanel in UI

**Requirements:**
- Create LayerPanel instance
- Add to appropriate sidebar/container
- Connect to current page

**Files to modify:**
- `source/MainWindow.cpp` - Create and place LayerPanel
- `source/MainWindow.h` - Declare LayerPanel member

#### 5.2 Connect LayerPanel to Page
**Goal:** LayerPanel updates when page changes

**Requirements:**
- When tab changes → update LayerPanel's page
- When page changes within document → update LayerPanel
- LayerPanel signals trigger viewport repaint

**Connections:**
```
TabManager::currentViewportChanged
  → Get new viewport's current page
  → LayerPanel::setCurrentPage(page)

DocumentViewport::currentPageChanged
  → Get new page
  → LayerPanel::setCurrentPage(page)

LayerPanel::layerVisibilityChanged
  → Viewport::update()

LayerPanel::activeLayerChanged
  → (Document handles this internally)
```

#### 5.3 Test Multi-Layer Editing
**Goal:** Verify multiple layers work in GUI

**Test cases:**
- Add new layer via LayerPanel
- Switch between layers, draw on each
- Toggle layer visibility
- Reorder layers
- Save/load preserves layer structure and content
- Delete layer (not last one)

---

## Reference Files

| File | Purpose |
|------|---------|
| `source/core/DocumentTests.h` | Document serialization tests (all pass) |
| `source/core/DocumentViewportTests.h` | Viewport integration tests (all pass) |
| `source/core/Document.cpp` | Document implementation with toFullJson/fromFullJson |
| `source/core/Page.cpp` | Page implementation with layer management |
| `source/ui/LayerPanel.cpp` | LayerPanel implementation (ready to integrate) |
| `source/pdf/PdfProvider.h` | PDF interface (use this, not Poppler directly) |

---

## Success Criteria

### Phase 1: Core Save/Load
- [x] Can add page to document (Ctrl+Shift+A) - prerequisite ✅
- [ ] Can save multi-page document to JSON file (Ctrl+S)
- [ ] Can load document from JSON file (Ctrl+O)
- [ ] Strokes and layers preserved on save/load

### Phase 2: PDF Loading
- [ ] Can open PDF and view in DocumentViewport (Ctrl+Shift+O)
- [ ] PDF pages render correctly
- [ ] Multi-page PDF works

### Phase 3: Insert Page
- [ ] Can insert page after current (Ctrl+Shift+I)
- [ ] Insert works correctly for non-PDF documents
- [ ] Insert behavior defined for PDF documents

### Phase 4: Edgeless Mode
- [ ] Can create edgeless document (Ctrl+Shift+N)
- [ ] Edgeless mode works correctly (large canvas, drawing, save/load)

### Phase 5: LayerPanel
- [ ] LayerPanel shows in UI
- [ ] Multi-layer editing works
- [ ] Layer changes trigger viewport repaint
- [ ] All existing DocumentTests still pass

---

## Notes

- Save/Load flow must be **modular and clear** - many future features depend on it
- Keyboard shortcuts are **temporary** - will be removed/customized later
- Use `PdfProvider` interface, not `PopplerPdfProvider` directly
- Tab creation should go through `TabManager`
- Document ownership: `DocumentManager` owns documents

---

## Keyboard Shortcut Architecture

### Decision: Distributed Shortcuts (No Hub Needed)

Qt's event propagation naturally supports shortcuts at different levels:

```
Key Press Event
    ↓
DocumentViewport (focused widget)
    ↓ (if not handled, propagates up)
MainWindow
```

### Separation Rule

| Scope | Handler | Examples |
|-------|---------|----------|
| **Page/Viewport** | DocumentViewport | P/E (tools), Ctrl+Z/Y (undo/redo), B (benchmark) |
| **Document** | MainWindow | Ctrl+S (save), Ctrl+Shift+A (add page) |
| **Application** | MainWindow | Ctrl+O (open), Ctrl+Shift+O (open PDF) |

**Principle:** Shortcuts live where the action happens.

### Implementation

MainWindow uses `QShortcut` with `Qt::ApplicationShortcut` context for guaranteed behavior:

```cpp
QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
saveShortcut->setContext(Qt::ApplicationShortcut);
connect(saveShortcut, &QShortcut::activated, this, &MainWindow::saveDocument);
```

This ensures the shortcut works regardless of which widget has focus.

---

*Subplan for doc-1 task in SpeedyNote Phase 3 migration*

