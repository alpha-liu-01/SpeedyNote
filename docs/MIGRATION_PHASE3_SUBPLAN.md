# Phase 3: MainWindow Integration Subplan

## Overview

Phase 3 integrates `DocumentViewport` into `MainWindow`, replacing the obsolete `InkCanvas`. This is a **breaking change** for version 1.0.0-rc1 - old `.spn` files are not supported.

**Key Architectural Additions:**
- `DocumentManager` - Owns and manages Document lifecycle
- `TabManager` - Thin wrapper for tab operations (code organization)
- `LayerPanel` - New UI for multi-layer editing
- Global tool state with per-document view state

**Status:** Phase 3.0-3.2 ✅ COMPLETE | Phase 3.3 IN PROGRESS (~60%)

**Last Updated:** Dec 24, 2024

---

## Prerequisites

- [x] Phase 1: Document, Page, VectorLayer classes complete
- [x] Phase 2A: Drawing (pen, eraser, undo/redo) working in DocumentViewport
- [x] `--test-viewport` shows working drawing/erasing
- [x] Decisions finalized: DocumentManager ✓, TabManager ✓, Layer Panel location ✓

---

## Architecture Diagram

### Target Architecture
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MainWindow                                      │
│                                                                              │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────────────────┐ │
│  │  GLOBAL STATE    │  │  TAB MANAGEMENT  │  │      LEFT SIDEBAR         │ │
│  │                  │  │                  │  │                           │ │
│  │  - currentTool   │  │  TabManager      │  │  ┌─────────────────────┐ │ │
│  │  - penColor      │  │  - tabWidget     │  │  │ PDF Outline         │ │ │
│  │  - penThickness  │  │  - createTab()   │  │  │ (collapsible)       │ │ │
│  │  - eraserSize    │  │  - closeTab()    │  │  └─────────────────────┘ │ │
│  │  - colorPresets  │  │  - currentVP()   │  │  ┌─────────────────────┐ │ │
│  │                  │  │                  │  │  │ Bookmarks           │ │ │
│  │  Applied to →→→→→│→→│→→ all viewports  │  │  │ (collapsible)       │ │ │
│  └──────────────────┘  └────────┬─────────┘  │  └─────────────────────┘ │ │
│                                 │            │  ┌─────────────────────┐ │ │
│  ┌──────────────────┐           │            │  │ LAYER PANEL (NEW)   │ │ │
│  │ DocumentManager  │           │            │  │                     │ │ │
│  │                  │           │            │  │  [Layer 2]          │ │ │
│  │  - documents[]   │           │            │  │  [Layer 1] ← active │ │ │
│  │  - createDoc()   │           │            │  │  [Layer 0]          │ │ │
│  │  - loadDoc()     │           │            │  │                     │ │ │
│  │  - saveDoc()     │           │            │  │  [+] [-] [↑] [↓]    │ │ │
│  │  - closeDoc()    │ OWNS      │            │  └──────────┬──────────┘ │ │
│  └────────┬─────────┘           │            └─────────────│────────────┘ │
│           │                     │                          │              │
│           ▼                     ▼                          │              │
│  ┌──────────────────────────────────────────┐              │              │
│  │              Document (MODEL)            │              │              │
│  │                                          │              │              │
│  │  - pages[]                               │              │              │
│  │  - metadata                              │              │              │
│  │  - pdfProvider                           │              │              │
│  └────────────────────┬─────────────────────┘              │              │
│                       │                                    │              │
│  ┌────────────────────▼─────────────────────┐              │              │
│  │          DocumentViewport (VIEW)         │              │              │
│  │                                          │              │              │
│  │  - renders document                      │              │              │
│  │  - handles input                         │              │              │
│  │  - zoom, pan state                       │              │              │
│  │  - currentPage() ─────────────────────────┼──────────────┘              │
│  └────────────────────┬─────────────────────┘                             │
│                       │                                                    │
│  ┌────────────────────▼─────────────────────┐                             │
│  │               Page (MODEL)               │ ◄── LayerPanel calls        │
│  │                                          │     Page methods directly   │
│  │  - layers[]                              │     (not through viewport)  │
│  │  - activeLayerIndex                      │                             │
│  │  - addLayer(), removeLayer()             │                             │
│  │  - moveLayer(), setActiveLayerIndex()    │                             │
│  └──────────────────────────────────────────┘                             │
└───────────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Owns | Responsible For |
|-----------|------|-----------------|
| **MainWindow** | TabManager, LayerPanel, menus, toolbars | Coordination, global UI, global tool state |
| **DocumentManager** | Document instances | Document lifecycle, file I/O, recent documents |
| **TabManager** | QTabWidget operations | Tab create/close/switch, viewport access |
| **DocumentViewport** | View state (zoom, pan) | Rendering, input routing, scroll position |
| **LayerPanel** | Layer list UI | Layer add/delete/reorder, talks directly to Page |
| **Document** | Pages, metadata | Data model, serialization |
| **Page** | Layers, objects | Per-page data, layer management |

### Ownership Clarification

```
DocumentManager OWNS → Document
TabManager CREATES → DocumentViewport
DocumentViewport REFERENCES → Document (pointer, not ownership)
LayerPanel REFERENCES → Page (pointer, updated when page/tab changes)
```

---

## Task Breakdown

### Task 3.0: Pre-Integration Setup

#### Task 3.0.1: Create DocumentManager Class (~150 lines) ✅ COMPLETE

**Files:** `source/core/DocumentManager.h`, `source/core/DocumentManager.cpp`

**Goal:** Centralize document lifecycle management.

**Interface:**
```cpp
// DocumentManager.h
#pragma once

#include <QObject>
#include <QVector>
#include <QStringList>
#include "Document.h"

class DocumentManager : public QObject {
    Q_OBJECT
public:
    explicit DocumentManager(QObject* parent = nullptr);
    ~DocumentManager();

    // Document lifecycle
    Document* createDocument();                              // New blank document
    Document* loadDocument(const QString& path);             // Load from file
    bool saveDocument(Document* doc);                        // Save to current path
    bool saveDocumentAs(Document* doc, const QString& path); // Save to new path
    void closeDocument(Document* doc);                       // Delete and cleanup

    // Access
    Document* documentAt(int index) const;
    int documentCount() const;
    int indexOf(Document* doc) const;

    // State queries
    bool hasUnsavedChanges(Document* doc) const;
    QString documentPath(Document* doc) const;

    // Recent documents (for File menu)
    QStringList recentDocuments() const;
    void addToRecent(const QString& path);
    void clearRecentDocuments();

signals:
    void documentCreated(Document* doc);
    void documentLoaded(Document* doc);
    void documentSaved(Document* doc);
    void documentClosed(Document* doc);
    void documentModified(Document* doc);
    void recentDocumentsChanged();

private:
    QVector<Document*> m_documents;
    QMap<Document*, QString> m_documentPaths;    // Document → file path
    QMap<Document*, bool> m_modifiedFlags;       // Track unsaved changes
    QStringList m_recentPaths;
    static const int MAX_RECENT = 10;

    void connectDocumentSignals(Document* doc);
};
```

**Key Implementation Details:**
1. `createDocument()`: Creates Document with one blank page, adds to m_documents
2. `loadDocument()`: Loads via Document::fromFile(), handles errors gracefully
3. `saveDocument()`: Calls Document::saveToFile(), updates modified flag
4. `closeDocument()`: Removes from m_documents, deletes Document
5. `documentModified` signal: Connected from Document, forwarded to UI

**Deliverable:** DocumentManager class that owns all Document instances ✓

---

#### Task 3.0.2: Create TabManager Class (~100 lines) ✅ COMPLETE

**Files:** `source/ui/TabManager.h`, `source/ui/TabManager.cpp`

**Goal:** Thin wrapper around QTabWidget for code organization.

**Interface:**
```cpp
// TabManager.h
#pragma once

#include <QObject>
#include <QTabWidget>
#include "core/DocumentViewport.h"

class TabManager : public QObject {
    Q_OBJECT
public:
    explicit TabManager(QTabWidget* tabWidget, QObject* parent = nullptr);

    // Tab operations
    int createTab(Document* doc, const QString& title);
    void closeTab(int index);
    void closeCurrentTab();

    // Access
    DocumentViewport* currentViewport() const;
    DocumentViewport* viewportAt(int index) const;
    Document* documentAt(int index) const;
    int currentIndex() const;
    int tabCount() const;

    // Title management
    void setTabTitle(int index, const QString& title);
    void markTabModified(int index, bool modified);  // Adds/removes * from title

signals:
    void currentViewportChanged(DocumentViewport* viewport);
    void tabCloseRequested(int index, DocumentViewport* viewport);

private slots:
    void onCurrentChanged(int index);
    void onTabCloseRequested(int index);

private:
    QTabWidget* m_tabWidget;  // Does NOT own - MainWindow owns
    QVector<DocumentViewport*> m_viewports;
};
```

**Key Implementation Details:**
1. `createTab()`: Creates DocumentViewport, adds to QTabWidget, returns index
2. `closeTab()`: Removes tab, deletes DocumentViewport (not Document!)
3. `currentViewport()`: Returns viewport of active tab
4. `markTabModified()`: Updates tab title with * prefix for unsaved changes
5. Signals: Relay tab changes to MainWindow for coordination

**Deliverable:** TabManager class for clean tab operations ✓

---

#### Task 3.0.3: Create LayerPanel Class (~250 lines) ✅ COMPLETE

**Files:** `source/ui/LayerPanel.h`, `source/ui/LayerPanel.cpp`

**Goal:** SAI2-style layer panel for bottom-left sidebar.

**Interface:**
```cpp
// LayerPanel.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include "core/Page.h"

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    // Page connection
    void setCurrentPage(Page* page);
    Page* currentPage() const { return m_page; }

    // Refresh UI from page state
    void refreshLayerList();

signals:
    // Emitted after layer changes (for undo system, MainWindow sync)
    void layerAdded(int index);
    void layerRemoved(int index);
    void layerMoved(int from, int to);
    void activeLayerChanged(int index);
    void layerVisibilityChanged(int index, bool visible);

private slots:
    void onAddLayerClicked();
    void onRemoveLayerClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onLayerSelected(int index);
    void onVisibilityToggled(QListWidgetItem* item);

private:
    Page* m_page = nullptr;
    QListWidget* m_layerList;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_moveUpButton;
    QPushButton* m_moveDownButton;

    void setupUI();
    void updateButtonStates();
    QListWidgetItem* createLayerItem(int index);
};
```

**UI Layout:**
```
┌─────────────────────────────────┐
│ Layers          [v] (collapse)  │
├─────────────────────────────────┤
│ ┌─────────────────────────────┐ │
│ │ [👁] Layer 2               │ │
│ ├─────────────────────────────┤ │
│ │ [👁] Layer 1    ← selected │ │  (highlighted = active layer)
│ ├─────────────────────────────┤ │
│ │ [👁] Layer 0               │ │
│ └─────────────────────────────┘ │
│                                 │
│  [+]  [-]  [↑]  [↓]            │
└─────────────────────────────────┘
```

**Key Implementation Details:**
1. **Layer List:** QListWidget with custom items showing visibility checkbox + name
2. **Selection = Active Layer:** Clicking a layer sets it as active for drawing
3. **Visibility Toggle:** Checkbox hides layer without deleting
4. **Direct Page Access:** All operations call `m_page->addLayer()`, etc. directly
5. **Refresh:** `refreshLayerList()` called when page/tab changes

**Button Actions:**
| Button | Action | Page Method |
|--------|--------|-------------|
| [+] | Add layer above active | `page->addLayer("Layer N")` |
| [-] | Remove active layer | `page->removeLayer(activeIndex)` |
| [↑] | Move active layer up | `page->moveLayer(active, active+1)` |
| [↓] | Move active layer down | `page->moveLayer(active, active-1)` |

**Deliverable:** LayerPanel widget for multi-layer management ✓

---

#### Task 3.0.4: Add Command Line Flag (~20 lines) ✅ COMPLETE

**Files:** `source/Main.cpp`, `source/MainWindow.h`, `source/MainWindow.cpp`

**Goal:** Add `--use-new-viewport` flag for controlled rollout.

**Implementation:**
```cpp
// In Main.cpp
bool useNewViewport = false;
for (int i = 1; i < argc; ++i) {
    if (QString(argv[i]) == "--use-new-viewport") {
        useNewViewport = true;
    }
}

MainWindow mainWindow(useNewViewport);
```

**Deliverable:** Can launch with new or old viewport ✓

---

### Task 3.1: Disconnect InkCanvas and Replace Tab System

> **REVISED:** InkCanvas is COMPLETELY REMOVED, not toggled.
> See detailed breakdown: `docs/MIGRATION_PHASE3_1_SUBPLAN.md`

**Goal:** Remove all InkCanvas dependencies. Replace custom tab system with QTabWidget.

**Reference files created:**
- `source/MainWindow_OLD.cpp` - Original before changes
- `source/MainWindow_OLD.h` - Original header before changes

#### Sub-tasks:

| Task | Description | Est. Lines |
|------|-------------|------------|
| 3.1.1 | Replace `tabList` + `canvasStack` with QTabWidget + TabManager | ~200 |
| 3.1.2 | Remove `addNewTab()` InkCanvas code | ~150 |
| 3.1.3 | Remove VectorCanvas and its buttons (VP, VE, VUndo) | ~100 |
| 3.1.4 | Create `currentViewport()`, replace `currentCanvas()` calls | ~300 |
| 3.1.5 | Stub InkCanvas signal handlers | ~150 |
| 3.1.6 | Remove page navigation methods (`switchPage`, etc.) | ~200 |
| 3.1.7 | Remove InkCanvas includes and members | ~50 |
| 3.1.8 | Disable ControlPanelDialog (depends on InkCanvas) | ~30 |
| 3.1.9 | Stub markdown/highlight handlers | ~50 |

**Order of implementation:**
1. 3.1.3 - Remove VectorCanvas (isolated)
2. 3.1.1 - Replace tab system
3. 3.1.2 - Remove addNewTab InkCanvas
4. 3.1.7 - Remove InkCanvas includes
5. 3.1.4 - Create currentViewport()
6. 3.1.6 - Remove page navigation
7. 3.1.5 - Stub signal handlers
8. 3.1.8 - Disable ControlPanelDialog
9. 3.1.9 - Stub markdown handlers

**Success criteria:**
- [ ] MainWindow compiles without InkCanvas
- [ ] VectorCanvas removed from build
- [ ] App launches without crashes
- [ ] TabManager integrated with QTabWidget

**Deliverable:** MainWindow compiles, ready for DocumentViewport integration ✓

---

### Task 3.2: Add DocumentViewport

#### Task 3.2.1: Initialize Managers (~50 lines)

**Files:** `source/MainWindow.h`, `source/MainWindow.cpp`

**Goal:** Create DocumentManager and TabManager in MainWindow.

**Implementation:**
```cpp
// MainWindow.h
#include "core/DocumentManager.h"
#include "ui/TabManager.h"

class MainWindow : public QMainWindow {
    // ...
private:
    // New members
    bool m_useNewViewport = false;
    DocumentManager* m_documentManager = nullptr;
    TabManager* m_tabManager = nullptr;
    
    // Global tool state
    ToolType m_currentTool = ToolType::Pen;
    QColor m_penColor = Qt::black;
    qreal m_penThickness = 5.0;
    qreal m_eraserSize = 20.0;
};

// MainWindow.cpp constructor
if (m_useNewViewport) {
    m_documentManager = new DocumentManager(this);
    m_tabManager = new TabManager(tabWidget, this);  // tabWidget is existing QTabWidget
    
    connect(m_tabManager, &TabManager::currentViewportChanged,
            this, &MainWindow::onViewportChanged);
}
```

**Deliverable:** DocumentManager and TabManager initialized ✓

---

#### Task 3.2.2: Create Tab with DocumentViewport (~100 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** New tabs use DocumentViewport + Document.

**Implementation:**
```cpp
void MainWindow::createNewTab() {
    if (m_useNewViewport) {
        // Create new document
        Document* doc = m_documentManager->createDocument();
        
        // Create tab with viewport
        int tabIndex = m_tabManager->createTab(doc, "Untitled");
        
        // Apply current global tool state
        DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
        viewport->setCurrentTool(m_currentTool);
        viewport->setPenColor(m_penColor);
        viewport->setPenThickness(m_penThickness);
        viewport->setEraserSize(m_eraserSize);
        
        // Connect viewport signals
        connectViewportSignals(viewport);
    } else {
        // Existing InkCanvas tab creation
    }
}

void MainWindow::connectViewportSignals(DocumentViewport* viewport) {
    connect(viewport, &DocumentViewport::currentPageChanged,
            this, &MainWindow::onCurrentPageChanged);
    connect(viewport, &DocumentViewport::zoomChanged,
            this, &MainWindow::onZoomChanged);
    connect(viewport, &DocumentViewport::documentModified,
            this, &MainWindow::onDocumentModified);
    connect(viewport, &DocumentViewport::undoAvailableChanged,
            this, &MainWindow::onUndoAvailableChanged);
    connect(viewport, &DocumentViewport::redoAvailableChanged,
            this, &MainWindow::onRedoAvailableChanged);
}
```

**Deliverable:** Can create new tabs with DocumentViewport ✓

---

#### Task 3.2.3: Basic Rendering Verification (~20 lines)

**Files:** Test only

**Goal:** Verify DocumentViewport renders correctly in MainWindow context.

**Test Checklist:**
- [ ] New tab shows blank page with correct background
- [ ] Multiple tabs work (switch between them)
- [ ] Tab close works (viewport deleted, document deleted)
- [ ] Window resize doesn't crash

**Deliverable:** Basic multi-tab rendering works ✓

---

### Task 3.3: Core Features Reconnection

#### Task 3.3.1: Tool Selection (~80 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Connect toolbar tool buttons to DocumentViewport.

**Global Tool State Pattern:**
```cpp
// When user clicks pen button:
void MainWindow::onPenToolClicked() {
    m_currentTool = ToolType::Pen;
    
    if (m_useNewViewport) {
        // Update current viewport
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->setCurrentTool(ToolType::Pen);
        }
    } else {
        // Existing InkCanvas code
        currentCanvas()->setCurrentTool(ToolType::Pen);
    }
    
    updateToolbarState();
}

// When tab changes, apply global tool state:
void MainWindow::onViewportChanged(DocumentViewport* viewport) {
    if (viewport) {
        viewport->setCurrentTool(m_currentTool);
        viewport->setPenColor(m_penColor);
        viewport->setPenThickness(m_penThickness);
        viewport->setEraserSize(m_eraserSize);
    }
    
    // Update per-document UI
    updatePageSpinbox();
    updateZoomDisplay();
    updateLayerPanel();  // Task 3.4
}
```

**Tools to Connect:**
| Tool | Toolbar Button | ToolType |
|------|----------------|----------|
| Pen | penButton | `ToolType::Pen` |
| Eraser | eraserButton | `ToolType::Eraser` |
| Marker | markerButton | `ToolType::Marker` |

**Deliverable:** Tool buttons switch tools on current viewport ✓

---

#### Task 3.3.2: Pen Color & Thickness (~60 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Connect color picker and thickness slider.

**Implementation:**
```cpp
void MainWindow::onPenColorChanged(const QColor& color) {
    m_penColor = color;
    
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->setPenColor(color);
        }
    } else {
        // Existing code
    }
}

void MainWindow::onPenThicknessChanged(int value) {
    m_penThickness = value;
    
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->setPenThickness(value);
        }
    } else {
        // Existing code
    }
}
```

**Deliverable:** Color and thickness affect drawing ✓

---

#### Task 3.3.3: Pan/Zoom Controls (~100 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Connect zoom slider, zoom buttons, scroll bars.

**Per-Document State (read from viewport):**
```cpp
void MainWindow::onViewportChanged(DocumentViewport* viewport) {
    if (!viewport) return;
    
    // Read viewport state, update UI
    m_zoomSlider->setValue(viewport->zoomLevel() * 100);
    // Scrollbars: More complex, may need scroll fraction getters
}

void MainWindow::onZoomSliderChanged(int percent) {
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->setZoomLevel(percent / 100.0);
        }
    }
}

void MainWindow::onZoomInClicked() {
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->setZoomLevel(vp->zoomLevel() * 1.2);
        }
    }
}
```

**Deliverable:** Zoom controls work on current viewport ✓

---

#### Task 3.3.4: Page Navigation (~80 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Connect page spinbox, prev/next buttons.

**Implementation:**
```cpp
void MainWindow::onPageSpinboxChanged(int page) {
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->scrollToPage(page - 1);  // Spinbox is 1-indexed
        }
    }
}

void MainWindow::onCurrentPageChanged(int pageIndex) {
    m_pageSpinbox->blockSignals(true);
    m_pageSpinbox->setValue(pageIndex + 1);
    m_pageSpinbox->blockSignals(false);
    
    // Update layer panel to show this page's layers
    updateLayerPanel();
}

void MainWindow::updatePageSpinbox() {
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            m_pageSpinbox->setMaximum(vp->document()->pageCount());
            m_pageSpinbox->setValue(vp->currentPageIndex() + 1);
        }
    }
}
```

**Deliverable:** Page navigation works with spinbox and buttons ✓

---

#### Task 3.3.5: Undo/Redo (~40 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Connect Edit menu Undo/Redo to viewport.

**Implementation:**
```cpp
void MainWindow::onUndoTriggered() {
    if (m_useNewViewport) {
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->undo();
        }
    } else {
        // Existing code
    }
}

void MainWindow::onUndoAvailableChanged(bool available) {
    m_undoAction->setEnabled(available);
}

void MainWindow::onRedoAvailableChanged(bool available) {
    m_redoAction->setEnabled(available);
}
```

**Deliverable:** Ctrl+Z / Ctrl+Y work from menu ✓

---

### Task 3.4: Layer Management UI

#### Task 3.4.1: Add LayerPanel to Sidebar (~60 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Place LayerPanel below PDF outline/bookmarks in left sidebar.

**Layout Structure:**
```cpp
// Existing left sidebar structure (approximate):
QWidget* leftSidebar = new QWidget();
QVBoxLayout* sidebarLayout = new QVBoxLayout(leftSidebar);

// Existing collapsible sections
sidebarLayout->addWidget(m_outlineSection);    // PDF outline
sidebarLayout->addWidget(m_bookmarksSection);  // Bookmarks

// NEW: Layer panel section
if (m_useNewViewport) {
    m_layerPanel = new LayerPanel();
    m_layerPanelSection = createCollapsibleSection("Layers", m_layerPanel);
    sidebarLayout->addWidget(m_layerPanelSection);
}

sidebarLayout->addStretch();
```

**Deliverable:** LayerPanel visible in sidebar ✓

---

#### Task 3.4.2: Connect LayerPanel to Current Page (~50 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Update LayerPanel when tab/page changes.

**Implementation:**
```cpp
void MainWindow::updateLayerPanel() {
    if (!m_useNewViewport || !m_layerPanel) return;
    
    Page* currentPage = nullptr;
    if (auto* vp = m_tabManager->currentViewport()) {
        currentPage = vp->currentPage();
    }
    
    m_layerPanel->setCurrentPage(currentPage);
}

// Called from:
// - onViewportChanged() (tab switch)
// - onCurrentPageChanged() (scroll to new page)
```

**Deliverable:** LayerPanel shows current page's layers ✓

---

#### Task 3.4.3: Connect LayerPanel Signals (~40 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Handle layer changes for undo/UI sync.

**Implementation:**
```cpp
void MainWindow::setupLayerPanelConnections() {
    connect(m_layerPanel, &LayerPanel::layerAdded, [this](int index) {
        // Mark document as modified
        if (auto* vp = m_tabManager->currentViewport()) {
            emit vp->documentModified();
        }
        m_tabManager->markTabModified(m_tabManager->currentIndex(), true);
    });
    
    connect(m_layerPanel, &LayerPanel::activeLayerChanged, [this](int index) {
        // DocumentViewport renders all layers, just needs repaint
        if (auto* vp = m_tabManager->currentViewport()) {
            vp->update();
        }
    });
    
    // Similar for layerRemoved, layerMoved
}
```

**Deliverable:** Layer changes trigger document modified ✓

---

#### Task 3.4.4: Test Multi-Layer Editing (~manual test)

**Test Checklist:**
- [ ] Default document has 1 layer
- [ ] Can add layer (appears in list)
- [ ] Can select different layer (drawing goes to selected layer)
- [ ] Can delete layer (removes from list and page)
- [ ] Can reorder layers (visual order changes)
- [ ] Layer visibility toggle works
- [ ] Switching tabs shows correct layers
- [ ] Switching pages shows correct layers

**Deliverable:** Multi-layer editing fully functional ✓

---

### Task 3.5: File Operations

#### Task 3.5.1: New Document (~30 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** File → New creates Document via DocumentManager.

**Implementation:**
```cpp
void MainWindow::onNewDocument() {
    if (m_useNewViewport) {
        createNewTab();  // Already implemented in Task 3.2.2
    } else {
        // Existing code
    }
}
```

**Deliverable:** File → New works ✓

---

#### Task 3.5.2: Save Document (~80 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** File → Save uses DocumentManager.

**Implementation:**
```cpp
void MainWindow::onSaveDocument() {
    if (!m_useNewViewport) {
        // Existing code
        return;
    }
    
    auto* viewport = m_tabManager->currentViewport();
    if (!viewport) return;
    
    Document* doc = viewport->document();
    
    if (m_documentManager->documentPath(doc).isEmpty()) {
        // No path yet - do Save As
        onSaveDocumentAs();
        return;
    }
    
    if (m_documentManager->saveDocument(doc)) {
        m_tabManager->markTabModified(m_tabManager->currentIndex(), false);
        statusBar()->showMessage("Saved", 2000);
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save document.");
    }
}

void MainWindow::onSaveDocumentAs() {
    if (!m_useNewViewport) {
        // Existing code
        return;
    }
    
    QString path = QFileDialog::getSaveFileName(
        this, "Save Document", QString(), "SpeedyNote Files (*.snx)");
    
    if (path.isEmpty()) return;
    
    auto* viewport = m_tabManager->currentViewport();
    Document* doc = viewport->document();
    
    if (m_documentManager->saveDocumentAs(doc, path)) {
        m_tabManager->setTabTitle(m_tabManager->currentIndex(), 
                                   QFileInfo(path).fileName());
        m_tabManager->markTabModified(m_tabManager->currentIndex(), false);
        statusBar()->showMessage("Saved", 2000);
    }
}
```

**Note:** New format uses `.snx` extension (SpeedyNote eXtended).

**Deliverable:** Save/Save As works ✓

---

#### Task 3.5.3: Load Document (~80 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** File → Open loads new format only.

**Implementation:**
```cpp
void MainWindow::onOpenDocument() {
    if (!m_useNewViewport) {
        // Existing code - opens .spn
        return;
    }
    
    QString path = QFileDialog::getOpenFileName(
        this, "Open Document", QString(), 
        "SpeedyNote Files (*.snx);;PDF Files (*.pdf)");
    
    if (path.isEmpty()) return;
    
    Document* doc = m_documentManager->loadDocument(path);
    if (!doc) {
        QMessageBox::warning(this, "Open Failed", "Could not open document.");
        return;
    }
    
    int tabIndex = m_tabManager->createTab(doc, QFileInfo(path).fileName());
    DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
    connectViewportSignals(viewport);
    applyGlobalToolState(viewport);
}
```

**Breaking Change Notice:**
- Old `.spn` files are NOT supported with `--use-new-viewport`
- Error message if user tries to open `.spn` with new viewport
- This is intentional for version 1.0.0-rc1

**Deliverable:** Open works for new format ✓

---

#### Task 3.5.4: PDF Loading (~60 lines)

**Files:** `source/MainWindow.cpp`

**Goal:** Open PDF creates Document with PDF background.

**Implementation:**
```cpp
// In onOpenDocument(), detect PDF:
if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
    Document* doc = Document::createForPdf(path);
    if (!doc || doc->pageCount() == 0) {
        QMessageBox::warning(this, "PDF Error", "Could not load PDF.");
        delete doc;
        return;
    }
    
    // Register with DocumentManager (takes ownership)
    // ... rest of tab creation
}
```

**Deliverable:** Can open PDF with new viewport ✓

---

### Task 3.6: Verification

#### Task 3.6.1: Feature Verification Checklist

**Drawing:**
- [ ] Pen draws pressure-sensitive strokes
- [ ] Eraser removes strokes
- [ ] Hardware eraser (stylus flip) works
- [ ] Undo/Redo works per-page
- [ ] Drawing goes to active layer only

**Multi-Layer:**
- [ ] Can add layers
- [ ] Can delete layers (with confirmation if strokes exist?)
- [ ] Can reorder layers
- [ ] Selecting layer changes where strokes go
- [ ] Layer visibility toggle hides/shows strokes
- [ ] Layer state persists across page switches
- [ ] Layer state persists across tab switches

**File Operations:**
- [ ] New document works
- [ ] Save creates valid `.snx` file
- [ ] Load restores all layers and strokes
- [ ] PDF backgrounds render correctly
- [ ] PDF loading creates correct number of pages

**UI State:**
- [ ] Tool buttons reflect current tool
- [ ] Color/thickness changes work
- [ ] Zoom slider/buttons work
- [ ] Page navigation works
- [ ] Tab titles show filename
- [ ] Tab titles show * when modified

---

#### Task 3.6.2: Move Debug Overlay (~30 lines)

**Files:** `source/core/DocumentViewport.cpp`, `source/MainWindow.cpp`

**Goal:** Move debug overlay from viewport to status bar.

**Implementation:**
```cpp
// DocumentViewport: Add method to get debug info
QString DocumentViewport::debugInfo() const {
    return QString("Tool: %1 | Page: %2/%3 | Zoom: %4% | Undo: %5")
        .arg(toolName())
        .arg(m_currentPageIndex + 1)
        .arg(m_document ? m_document->pageCount() : 0)
        .arg(qRound(m_zoomLevel * 100))
        .arg(canUndo() ? "Y" : "N");
}

// MainWindow: Show in status bar (debug builds only)
#ifdef QT_DEBUG
void MainWindow::updateDebugStatus() {
    if (auto* vp = m_tabManager->currentViewport()) {
        m_debugLabel->setText(vp->debugInfo());
    }
}
#endif
```

**Deliverable:** Debug info in status bar, not painted on viewport ✓

---

## Task Summary Table

| Task | Description | Est. Lines | Status |
|------|-------------|------------|--------|
| **3.0 Pre-Integration** | | | ✅ COMPLETE |
| 3.0.1 | DocumentManager class | ~150 | ✅ |
| 3.0.2 | TabManager class | ~100 | ✅ |
| 3.0.3 | LayerPanel class | ~250 | ✅ |
| 3.0.4 | Command line flag | ~20 | ✅ REMOVED |
| **3.1 Disconnect InkCanvas** | | | ✅ COMPLETE |
| 3.1.1 | Replace tab system (QTabWidget) | ~200 | ✅ |
| 3.1.2 | Remove addNewTab InkCanvas | ~150 | ✅ |
| 3.1.3 | Remove VectorCanvas + buttons | ~100 | ✅ |
| 3.1.4 | Create currentViewport() | ~300 | ✅ |
| 3.1.5 | Stub signal handlers | ~150 | ✅ |
| 3.1.6 | Remove page navigation | ~200 | ✅ |
| 3.1.7 | Remove InkCanvas includes | ~50 | ✅ |
| 3.1.8 | Disable ControlPanelDialog | ~30 | ✅ |
| 3.1.9 | Stub markdown handlers | ~50 | ✅ |
| **3.2 Add DocumentViewport** | | | ✅ COMPLETE |
| 3.2.1 | Initialize managers | ~50 | ✅ |
| 3.2.2 | Create tab with viewport | ~100 | ✅ |
| 3.2.3 | Basic rendering verification | ~20 | ✅ |
| **3.3 Core Features** | | | 🔄 IN PROGRESS |
| 3.3.1 | Tool selection | ~80 | ✅ |
| 3.3.2 | Pen color & thickness | ~60 | ✅ |
| 3.3.3 | Pan/zoom controls | ~100 | ✅ (partial) |
| 3.3.4 | Page navigation | ~80 | [ ] stubbed |
| 3.3.5 | Undo/redo | ~40 | [ ] |
| **3.4 Layer Management UI** | | | |
| 3.4.1 | Add LayerPanel to sidebar | ~60 | [ ] |
| 3.4.2 | Connect to current page | ~50 | [ ] |
| 3.4.3 | Connect signals | ~40 | [ ] |
| 3.4.4 | Test multi-layer editing | Manual | [ ] |
| **3.5 File Operations** | | | |
| 3.5.1 | New document | ~30 | [ ] |
| 3.5.2 | Save document | ~80 | [ ] |
| 3.5.3 | Load document | ~80 | [ ] |
| 3.5.4 | PDF loading | ~60 | [ ] |
| **3.6 Verification** | | | |
| 3.6.1 | Feature checklist | Manual | [ ] |
| 3.6.2 | Move debug overlay | ~30 | [ ] |

**Progress:** Phase 3.0-3.2 complete, Phase 3.3 ~60% complete

---

## Directory Structure After Phase 3

```
source/
├── core/
│   ├── Document.h / Document.cpp           (existing)
│   ├── DocumentManager.h / DocumentManager.cpp  ← NEW
│   ├── DocumentViewport.h / DocumentViewport.cpp (existing)
│   ├── Page.h / Page.cpp                   (existing)
│   └── ToolType.h                          (existing)
├── ui/                                     ← NEW FOLDER
│   ├── TabManager.h / TabManager.cpp       ← NEW
│   └── LayerPanel.h / LayerPanel.cpp       ← NEW
├── layers/
│   └── VectorLayer.h                       (existing)
├── strokes/
│   ├── StrokePoint.h                       (existing)
│   └── VectorStroke.h                      (existing)
├── objects/
│   ├── InsertedObject.h / InsertedObject.cpp (existing)
│   └── ImageObject.h / ImageObject.cpp     (existing)
├── pdf/
│   ├── PdfProvider.h                       (existing)
│   └── PopplerPdfProvider.h / PopplerPdfProvider.cpp (existing)
├── InkCanvas.h / InkCanvas.cpp             (legacy, conditional)
├── VectorCanvas.h / VectorCanvas.cpp       (frozen, to be removed Phase 5)
└── MainWindow.h / MainWindow.cpp           (heavily modified)
```

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **DocumentManager** | Yes | Clean document lifecycle, file I/O separation |
| **TabManager** | Thin wrapper | Code organization, ~100 lines |
| **Layer Panel** | Direct to Page | Page owns layers, no viewport mediation |
| **Tool state** | Global | Industry standard (Photoshop, SAI2, etc.) |
| **View state** | Per-document | Each document independent |
| **Old .spn files** | Not supported | Breaking change for 1.0.0-rc1 |
| **New file format** | `.snx` | Clean slate, JSON + binary strokes |

---

## Success Criteria

### Current Status (Dec 24, 2024):

1. [x] ~~Can launch with `--use-new-viewport` flag~~ → Flag removed, new viewport is DEFAULT
2. [x] New tabs use DocumentViewport (not InkCanvas)
3. [x] Can draw/erase on any page
4. [ ] Multi-layer editing works (add/delete/reorder/select) → Phase 3.4
5. [ ] Drawing goes to active layer only → Phase 3.4
6. [ ] Can save to `.snx` format → Phase 3.5
7. [ ] Can load `.snx` files with all layers preserved → Phase 3.5
8. [ ] Can open PDF files → Phase 3.5
9. [x] Tool state is global (switches affect all tabs)
10. [x] View state is per-document (zoom, pan, current page)
11. [x] No crashes when switching tabs rapidly
12. [ ] Memory doesn't leak when closing tabs → Needs testing

### Phase 3 Complete When:
All items above are checked ✅

---

## Notes

### Breaking Change: Old .spn Files

Version 1.0.0-rc1 with `--use-new-viewport`:
- Cannot open old `.spn` files
- Shows error message explaining the limitation
- Users should keep old SpeedyNote version for existing files

### Future: Migration Tool (Phase 5+)

After architecture is stable, consider:
- Import tool to convert `.spn` → `.snx`
- Would need to rasterize pixmap strokes or discard them
- Not a priority for MVP

### LayerPanel ↔ DocumentViewport Relationship

**Important:** LayerPanel does NOT go through DocumentViewport for layer operations.

```
CORRECT:                           WRONG:
LayerPanel → Page.addLayer()       LayerPanel → DocumentViewport → Page
```

DocumentViewport only provides `currentPage()` so MainWindow knows which Page to give to LayerPanel.

### Testing Strategy

1. **Unit tests:** DocumentManager (mock Document)
2. **Integration:** `--test-viewport` for drawing
3. **Manual:** Full feature checklist in Task 3.6.1
4. **Stress:** Open/close many tabs, switch rapidly
