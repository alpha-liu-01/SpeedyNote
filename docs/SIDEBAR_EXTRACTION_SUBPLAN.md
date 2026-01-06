# Sidebar Extraction Subplan

**Document Version:** 1.0  
**Date:** January 5, 2026  
**Status:** Planning  
**Prerequisites:** Toolbar Extraction complete, MainWindow cleanup Phase 3 done  
**Related:** `MAINWINDOW_CLEANUP_SUBPLAN.md` (Phase 4), `TOOLBAR_EXTRACTION_SUBPLAN.md`

---

## Overview

Extract sidebar-related code from MainWindow into a dedicated `source/ui/sidebars/` folder. Create a tabbed `LeftSidebarContainer` that will eventually hold multiple panels.

### Goals
1. Create `source/ui/sidebars/` folder structure
2. Create `LeftSidebarContainer` (QTabWidget-based)
3. Move existing `LayerPanel` into sidebars folder
4. Remove floating toggle buttons from MainWindow
5. Wire NavigationBar's left sidebar toggle to the container

### Non-Goals (Deferred)
- Connecting OutlinePanel (not implemented yet)
- Connecting BookmarksPanel (not implemented yet)
- Creating PagePanel (doesn't exist yet)
- Panel visibility memory via QSettings (deferred until control panel reconnected)

### Current State

**Floating toggle buttons in MainWindow:**
```cpp
QPushButton *toggleOutlineButton;      // Floating tab for outline sidebar
QPushButton *toggleBookmarksButton;    // Floating tab for bookmarks sidebar  
QPushButton *toggleLayerPanelButton;   // Floating tab for layer panel
```

**Existing LayerPanel:**
- Location: `source/ui/LayerPanel.h`, `source/ui/LayerPanel.cpp`
- Already functional and connected to MainWindow

**NavigationBar signal:**
- `leftSidebarToggled(bool checked)` - currently stubbed in MainWindow

---

## Phase S1: Create Folder Structure ✅ COMPLETED

### Task S1.1: Create Directory ✅

**Created:** `source/ui/sidebars/`

---

### Task S1.2: Move LayerPanel ✅

**Moved files:**
- `source/ui/LayerPanel.h` → `source/ui/sidebars/LayerPanel.h`
- `source/ui/LayerPanel.cpp` → `source/ui/sidebars/LayerPanel.cpp`

**Updated includes:**
- `source/MainWindow.cpp` - changed to `#include "ui/sidebars/LayerPanel.h"`
- `source/ui/sidebars/LayerPanel.cpp` - updated relative paths:
  - `../core/Page.h` → `../../core/Page.h`
  - `../core/Document.h` → `../../core/Document.h`
  - `../layers/VectorLayer.h` → `../../layers/VectorLayer.h`

**Updated CMakeLists.txt:**
- Changed `source/ui/LayerPanel.cpp` to `source/ui/sidebars/LayerPanel.cpp`

**Verification:**
- [x] `./compile.sh` succeeds
- [ ] LayerPanel still works (manual verification needed)

---

## Phase S2: Create LeftSidebarContainer ✅ COMPLETED

### Task S2.1: Create Container Class ✅

**Created:** `source/ui/sidebars/LeftSidebarContainer.h`

```cpp
#ifndef LEFTSIDEBARCONTAINER_H
#define LEFTSIDEBARCONTAINER_H

#include <QTabWidget>

class LayerPanel;

/**
 * @brief Tabbed container for left sidebar panels.
 * 
 * Uses QTabWidget to hold multiple panels:
 * - LayerPanel (connected)
 * - OutlinePanel (future)
 * - BookmarksPanel (future)
 * - PagePanel (future)
 * 
 * NavigationBar's left sidebar toggle shows/hides this container.
 */
class LeftSidebarContainer : public QTabWidget
{
    Q_OBJECT

public:
    explicit LeftSidebarContainer(QWidget *parent = nullptr);
    
    /**
     * @brief Get the LayerPanel instance.
     * @return Pointer to LayerPanel (owned by this container).
     */
    LayerPanel* layerPanel() const { return m_layerPanel; }
    
    /**
     * @brief Update theme colors.
     * @param darkMode True for dark theme
     */
    void updateTheme(bool darkMode);

private:
    void setupUi();
    
    LayerPanel *m_layerPanel = nullptr;
    // Future: OutlinePanel, BookmarksPanel, PagePanel
};

#endif // LEFTSIDEBARCONTAINER_H
```

---

### Task S2.2: Implement Container ✅

**Created:** `source/ui/sidebars/LeftSidebarContainer.cpp`

```cpp
#include "LeftSidebarContainer.h"
#include "LayerPanel.h"

LeftSidebarContainer::LeftSidebarContainer(QWidget *parent)
    : QTabWidget(parent)
{
    setupUi();
}

void LeftSidebarContainer::setupUi()
{
    // Configure tab widget
    setTabPosition(QTabWidget::West);  // Tabs on left side
    setDocumentMode(true);
    
    // Create LayerPanel
    m_layerPanel = new LayerPanel(this);
    addTab(m_layerPanel, tr("Layers"));
    
    // Future: Add other panels here
    // addTab(m_outlinePanel, tr("Outline"));
    // addTab(m_bookmarksPanel, tr("Bookmarks"));
    // addTab(m_pagePanel, tr("Pages"));
}

void LeftSidebarContainer::updateTheme(bool darkMode)
{
    // Apply theme styling to container
    // LayerPanel handles its own theming
    Q_UNUSED(darkMode);
    
    // Future: Apply QSS styling if needed
}
```

---

### Task S2.3: Update CMakeLists.txt ✅

**Added to sources:**
```cmake
source/ui/sidebars/LeftSidebarContainer.cpp  # Phase S2: Sidebar container
```

**Note:** `source/ui/LayerPanel.cpp` was already moved to `source/ui/sidebars/LayerPanel.cpp` in Phase S1.

---

## Phase S3: Integrate into MainWindow ✅ COMPLETED

### Task S3.1: Update MainWindow Header ✅

**Added include:**
```cpp
#include "ui/sidebars/LeftSidebarContainer.h"  // Phase S3: Left sidebar container
```

**Added member:**
```cpp
LeftSidebarContainer *m_leftSidebar = nullptr;  // Tabbed container for left panels
```

**Commented out:**
```cpp
// QPushButton *toggleOutlineButton;
// QPushButton *toggleBookmarksButton;
// QPushButton *toggleLayerPanelButton;
// QWidget *m_leftSideContainer = nullptr;
// void positionLeftSidebarTabs();
```

---

### Task S3.2: Update MainWindow Constructor ✅

**Replaced LayerPanel creation:**
```cpp
m_leftSidebar = new LeftSidebarContainer(this);
m_leftSidebar->setFixedWidth(250);
m_leftSidebar->setVisible(false);   // Hidden by default
m_layerPanel = m_leftSidebar->layerPanel();  // Get reference for signal connections
```

**Added to contentLayout:**
```cpp
contentLayout->addWidget(m_leftSidebar, 0);
```

---

### Task S3.3: Connect NavigationBar Signal ✅

**Updated handler:**
```cpp
connect(m_navigationBar, &NavigationBar::leftSidebarToggled, this, [this](bool checked) {
    if (m_leftSidebar) {
        m_leftSidebar->setVisible(checked);
    }
});
```

---

### Additional Cleanup (User completed)

- Removed floating button creation (~60 lines)
- Removed floating button styling (~50 lines)
- Removed `positionLeftSidebarTabs()` function (~50 lines)
- Updated `toggleOutlineSidebar()` and `toggleBookmarksSidebar()` to use `m_leftSidebar`
- Updated `toggleLayerPanel()` to work without floating button
- Updated controller actions to call toggle functions directly
- Added `m_leftSidebar->updateTheme(darkMode)` in `updateTheme()`

---

### Task S3.4: Remove Floating Toggle Buttons

**Delete from MainWindow.cpp:**
1. `toggleOutlineButton` creation and setup
2. `toggleBookmarksButton` creation and setup
3. `toggleLayerPanelButton` creation and setup
4. Positioning logic for floating buttons
5. Signal connections for these buttons
6. Style updates in `updateTheme()` for these buttons

**Delete from MainWindow.h:**
1. `toggleOutlineButton` declaration
2. `toggleBookmarksButton` declaration
3. `toggleLayerPanelButton` declaration
4. Any related positioning variables

---

### Task S3.5: Update LayerPanel Connections

**Ensure existing LayerPanel signal connections still work:**
```cpp
// These should now use m_leftSidebar->layerPanel() or m_layerPanel reference
connect(m_layerPanel, &LayerPanel::activeLayerChanged, ...);
connect(m_layerPanel, &LayerPanel::layerVisibilityChanged, ...);
// etc.
```

---

### Task S3.6: Update updateTheme()

**Add:**
```cpp
if (m_leftSidebar) {
    m_leftSidebar->updateTheme(darkMode);
}
```

**Remove:**
- Style updates for `toggleOutlineButton`
- Style updates for `toggleBookmarksButton`
- Style updates for `toggleLayerPanelButton`

---

## Phase S4: Verification

### Task S4.1: Compile and Test

- [ ] `./compile.sh` succeeds
- [ ] No undefined references
- [ ] Application starts without crash

### Task S4.2: Functional Testing

- [ ] NavigationBar left sidebar button toggles container visibility
- [ ] LayerPanel displays correctly in container
- [ ] LayerPanel functionality works (add/remove/reorder layers)
- [ ] Theme switching updates container correctly

### Task S4.3: Cleanup Verification

- [ ] No floating toggle buttons visible
- [ ] No orphaned positioning code
- [ ] No orphaned style code for removed buttons

---

## Summary Checklist

### Phase S1: Folder Structure ✅
- [x] S1.1: Create `source/ui/sidebars/` directory
- [x] S1.2: Move LayerPanel to sidebars folder

### Phase S2: Container ✅
- [x] S2.1: Create LeftSidebarContainer header
- [x] S2.2: Create LeftSidebarContainer implementation
- [x] S2.3: Update CMakeLists.txt

### Phase S3: Integration ✅
- [x] S3.1: Update MainWindow header
- [x] S3.2: Update MainWindow constructor
- [x] S3.3: Connect NavigationBar signal
- [x] S3.4: Remove floating toggle buttons (user completed)
- [x] S3.5: Update LayerPanel connections (kept working)
- [x] S3.6: Update updateTheme()

### Phase S4: Verification
- [ ] S4.1: Compile and test
- [ ] S4.2: Functional testing
- [ ] S4.3: Cleanup verification

---

## Future Work (Not This Subplan)

| Feature | Status | Notes |
|---------|--------|-------|
| OutlinePanel | Not connected | Panel exists but not wired |
| BookmarksPanel | Not connected | Panel exists but not wired |
| PagePanel | Not created | Future feature |
| Panel visibility memory | Deferred | QSettings integration later |
| Right sidebar container | Planned | Markdown notes sidebar |

---

## Appendix: File Changes

### Files to Create
| File | Purpose |
|------|---------|
| `source/ui/sidebars/LeftSidebarContainer.h` | Container header |
| `source/ui/sidebars/LeftSidebarContainer.cpp` | Container implementation |

### Files to Move
| From | To |
|------|-----|
| `source/ui/LayerPanel.h` | `source/ui/sidebars/LayerPanel.h` |
| `source/ui/LayerPanel.cpp` | `source/ui/sidebars/LayerPanel.cpp` |

### Files to Modify
| File | Changes |
|------|---------|
| `source/MainWindow.h` | Add container member, remove toggle button declarations |
| `source/MainWindow.cpp` | Replace floating buttons with container |
| `CMakeLists.txt` | Update source paths |

