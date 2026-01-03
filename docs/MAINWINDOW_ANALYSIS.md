# MainWindow Deconstruction Analysis

**Document Version:** 1.0  
**Date:** January 3, 2026  
**Status:** Analysis Complete - Ready for Deconstruction Planning

---

## Executive Summary

MainWindow.cpp (~9,700 lines) is the last major piece of legacy code in SpeedyNote. Originally designed for the picture-based InkCanvas, it has been "hacked" to connect to the new DocumentViewport architecture. This document analyzes the current state and provides a roadmap for deconstruction into modular UI components.

### Key Findings

| Category | Lines | % of Total | Status |
|----------|-------|------------|--------|
| **Working Code** | ~3,500 | 36% | Connected to DocumentViewport |
| **Stubbed Code** | ~2,200 | 23% | Stubbed with TODO comments |
| **Dead Code** | ~1,500 | 15% | `#if 0` blocks from InkCanvas |
| **UI Setup/Styling** | ~2,500 | 26% | setupUi(), updateTheme(), etc. |

---

## 1. Constructor & Initialization (~500 lines: 108-353)

### Status: ✅ WORKING (with new architecture)

**What it does:**
- Window setup (title, icon, size)
- Creates `QTabWidget`, `DocumentManager`, `TabManager`
- Connects TabManager signals (tab close, viewport change, etc.)
- Controller manager initialization (SDL gamepad)
- Mouse dial timer setup
- Settings loading

**Key Connections (KEEP):**
```cpp
connect(m_tabManager, &TabManager::currentViewportChanged, ...)  // Tab switching
connect(m_tabManager, &TabManager::tabCloseRequested, ...)       // Tab close cleanup
connect(m_tabManager, &TabManager::tabCloseAttempted, ...)       // Unsaved edgeless prompt
```

**Recommendation:** 
- Extract controller setup → `source/input/ControllerManager.cpp` (already exists as SDLControllerManager)
- Extract single-instance logic → `source/app/SingleInstanceManager.cpp`

---

## 2. setupUi() (~1,450 lines: 356-1798)

### Status: ⚠️ BLOATED - Needs Splitting

**What it does:**
- Creates ALL buttons (~50+ buttons)
- Creates ALL sidebars (outline, bookmarks, layer panel, markdown notes)
- Creates dial container and mode buttons
- Creates scrollbars and zoom controls
- Creates responsive toolbar layouts
- Sets up ALL signal/slot connections
- Creates keyboard shortcuts

**Current Structure:**
```
setupUi()
├── Button creation (570-850) - ~280 lines
├── Sidebar creation (960-1100) - ~140 lines  
├── Tab widget setup (1100-1170) - ~70 lines
├── Page controls (1186-1230) - ~45 lines
├── Dial mode buttons (1264-1320) - ~56 lines
├── Layout creation (1425-1700) - ~275 lines
├── Keyboard shortcuts (1738-1798) - ~60 lines
```

**Recommendation:**
Extract into separate setup functions in dedicated modules:

| New Module | Lines | Purpose |
|------------|-------|---------|
| `ToolbarManager` | ~400 | Tool buttons, color buttons, mode buttons |
| `SidebarManager` | ~300 | Outline, bookmarks, layers, markdown |
| `DialController` | ~200 | Dial container, mode switching |
| `ShortcutManager` | ~100 | All keyboard shortcuts |

---

## 3. Tool/Color Management (~300 lines: 1932-2100)

### Status: ✅ WORKING

**Functions:**
- `changeTool(int index)` - Tool switching (pen/marker/eraser)
- `setPenTool()`, `setMarkerTool()`, `setEraserTool()` - Direct tool setters
- `updateToolButtonStates()` - Syncs button visual state with viewport tool
- `handleColorButtonClick()` - Switches from eraser to pen when color selected
- `updateThicknessSliderForCurrentTool()` - Syncs thickness slider

**Current Flow:**
```
User clicks button → MainWindow method → currentViewport()->setCurrentTool()
                                       → updateToolButtonStates()
                                       → updateThicknessSliderForCurrentTool()
                                       → updateDialDisplay()
```

**Recommendation:**
- Keep as-is OR extract to `ToolbarManager::setTool(ToolType)`
- These are thin wrappers around DocumentViewport - not worth major refactoring

---

## 4. Page Navigation (~100 lines: 2114-2170)

### Status: ⚠️ STUBBED

**Functions (ALL STUBBED):**
- `switchPage(int pageNumber)` - "Not implemented yet (Phase 3.3.4)"
- `switchPageWithDirection(int pageNumber, int direction)` - Stubbed
- `deleteCurrentPage()` - Shows confirmation dialog, TODO on actual delete

**Recommendation:**
- These need real implementation connecting to `DocumentViewport::scrollToPage()`
- Keep in MainWindow as they coordinate with UI (page spinbox, bookmarks, etc.)

---

## 5. PDF Export (~1,000 lines: 2265-3185)

### Status: 🔴 DEAD CODE

**Functions (ALL #if 0 DISABLED):**
- `exportAnnotatedPdf()` - Shows "Coming soon!" message
- `exportCanvasOnlyNotebook()` - Dead code
- `exportAnnotatedPdfFullRender()` - Dead code
- `createAnnotatedPagesPdf()` - Dead code
- `mergePdfWithPdftk()` - Dead code
- `extractPdfOutlineData()` - Dead code
- `filterAndAdjustOutline()` - Dead code
- `applyOutlineToPdf()` - Dead code

**Original Design:** 
Used InkCanvas to render pages, pdftk for merging, Poppler for outlines.

**Recommendation:**
- DELETE all `#if 0` blocks (1,000+ lines of dead code)
- Reimplement as `PdfExporter` class when needed
- New implementation will use DocumentViewport rendering

---

## 6. Document Operations (~250 lines: 3700-3970)

### Status: ✅ WORKING

**Functions:**
- `saveDocument()` - Calls `m_documentManager->saveDocument()`
- `loadDocument()` - Calls `m_documentManager->loadDocument()`
- `addPageToDocument()` - Adds page via `doc->addPage()`
- `insertPageInDocument()` - Inserts page after current
- `deletePageInDocument()` - Deletes non-PDF page
- `openPdfDocument()` - Opens PDF via DocumentManager

**These work correctly!** The new architecture connection is clean.

**Recommendation:**
- Keep in MainWindow (they coordinate UI + DocumentManager)
- Could extract to `DocumentOperations` class if MainWindow gets smaller

---

## 7. Tab Management (~250 lines: 4005-4200)

### Status: ✅ WORKING

**Functions:**
- `switchTab(int index)` - Tab switch handling
- `addNewTab()` - Creates paged document
- `addNewEdgelessTab()` - Creates edgeless document
- `loadFolderDocument()` - Loads .snb bundle
- `removeTabAt(int index)` - Tab closure

**These work correctly!** Using TabManager and DocumentManager properly.

**Recommendation:**
- Keep in MainWindow (coordinates with TabManager)
- Already clean architecture

---

## 8. Dial/Controller System (~800 lines: 4700-5500)

### Status: ✅ WORKING (but bloated)

**Functions:**
- `updateDialDisplay()` - Updates dial OLED display based on mode
- `handleDialInput(int angle)` - Processes dial rotation
- `onDialReleased()` - Finalizes dial action
- `handleToolSelection(int angle)` - Tool switching via dial
- `handlePresetSelection(int angle)` - Color preset via dial
- `handleDialZoom(int angle)` - Zoom via dial
- `handleDialThickness(int angle)` - Thickness via dial
- `handleDialPanScroll(int angle)` - Pan/scroll via dial
- `changeDialMode(DialMode mode)` - Mode switching

**Also includes:**
- Mouse dial control system (long-press mouse buttons → dial mode)
- Controller button mapping
- Dial sound initialization

**Recommendation:**
- Extract to `source/input/DialController.cpp/h`
- Move `DialMode` enum to that file
- DialController owns: pageDial, dialContainer, dialDisplay
- MainWindow just creates and positions it

---

## 9. Theme/Styling (~500 lines: 5800-6430)

### Status: ✅ WORKING

**Functions:**
- `isDarkMode()` - Detects system dark mode
- `loadThemedIcon()` / `loadThemedIconReversed()` - Theme-aware icons
- `updateButtonIcon()` - Updates button icon with selected state
- `createButtonStyle(bool darkMode)` - Creates button stylesheet
- `updateTheme()` - Applies theme to all widgets (~300 lines!)
- `saveThemeSettings()` / `loadThemeSettings()` - Persistence
- `getAccentColor()` - Gets system or custom accent color

**The massive `updateTheme()` function manually styles 50+ widgets!**

**Recommendation:**
- Extract to `source/ui/ThemeManager.cpp/h`
- ThemeManager owns: color palette, icon loading, stylesheet generation
- Widgets register for theme updates instead of manual list
- Use Qt's property system for selected/hover states

---

## 10. Settings/Persistence (~300 lines: 6440-6750)

### Status: ✅ WORKING

**Functions:**
- `loadUserSettings()` - Loads from QSettings
- `savePdfDPI()` / `loadDefaultBackgroundSettings()` / etc.
- `loadButtonMappings()` / `saveButtonMappings()` - Controller mappings
- `loadKeyboardMappings()` / `saveKeyboardMappings()` - Keyboard shortcuts
- `loadMouseDialMappings()` / `saveMouseDialMappings()` - Mouse dial

**Recommendation:**
- Extract to `source/app/SettingsManager.cpp/h`
- Centralized settings access via `SettingsManager::instance()`
- Signals for setting changes (widgets subscribe instead of manual updates)

---

## 11. Stylus Button Handling (~300 lines: 6620-6900)

### Status: ✅ WORKING

**Functions:**
- `enableStylusButtonMode()` / `disableStylusButtonMode()`
- `handleStylusButtonPress()` / `handleStylusButtonRelease()`
- `saveStylusButtonSettings()` / `loadStylusButtonSettings()`

**Feature:** Stylus side buttons can temporarily enable tools (hold eraser, hold straight line, etc.)

**Recommendation:**
- Extract to `source/input/StylusButtonManager.cpp/h`
- Already well-encapsulated, just needs file separation

---

## 12. Sidebar Toggles (~400 lines: 8350-8780)

### Status: ⚠️ WORKING (but repetitive)

**Functions:**
- `toggleOutlineSidebar()` - PDF outline panel
- `toggleBookmarksSidebar()` - User bookmarks panel
- `toggleLayerPanel()` - Layer management panel
- `toggleMarkdownNotesSidebar()` - Markdown notes panel

**Each toggle function:**
1. Hides other exclusive sidebars
2. Shows/hides the target sidebar
3. Updates button visual state
4. Forces layout update
5. Repositions floating tabs

**Recommendation:**
- Extract to `source/ui/SidebarManager.cpp/h`
- Generic `toggleSidebar(SidebarType type)` method
- SidebarManager tracks mutual exclusivity
- Auto-repositions floating tabs

---

## 13. Bookmarks & Markdown Notes (~200 lines: 8585-8830)

### Status: ⚠️ STUBBED

**Bookmark Functions (STUBBED):**
- `loadBookmarks()` - "TODO Phase 3.4: Load from document"
- `saveBookmarks()` - "Not implemented yet"
- `toggleCurrentPageBookmark()` - Uses local map, doesn't persist

**Markdown Functions (STUBBED):**
- `onMarkdownNotesUpdated()` - Stubbed
- `onMarkdownNoteContentChanged()` - Stubbed
- `loadMarkdownNotesForCurrentPage()` - Clears sidebar only

**Recommendation:**
- Bookmarks: Implement with Document's bookmark storage
- Markdown: Defer to Phase 3.4 (complex feature)

---

## 14. Event Handlers (~400 lines: 5233-5560)

### Status: ✅ WORKING

**eventFilter() handles:**
- IME focus for text inputs
- Canvas container resize
- Scrollbar hover visibility
- Tablet press/release (stylus button mapping)
- Wheel events (trackpad vs mouse detection)
- Dial container drag

**Recommendation:**
- Keep most in MainWindow (Qt event filter pattern)
- Could split tablet handling to StylusButtonManager

---

## 15. Single Instance & File Association (~400 lines: 9260-9510)

### Status: ✅ WORKING

**Functions:**
- `isInstanceRunning()` - Checks for existing instance (QSharedMemory)
- `sendToExistingInstance()` - IPC via QLocalSocket
- `setupSingleInstanceServer()` - Sets up server
- `onNewConnection()` - Handles file open from second instance
- `openFileInNewTab()` - Opens PDF or SPN
- `openSpnPackage()` - Opens .spn package (STUBBED for DocumentViewport)
- `openPdfFile()` - Opens PDF (STUBBED)

**Recommendation:**
- Extract to `source/app/SingleInstanceManager.cpp/h`
- Clean separation from UI concerns

---

## 16. Old InkCanvas Code (~1,500 lines)

### Status: 🔴 DEAD CODE

**Scattered throughout in `#if 0` blocks:**
- Old export functions
- Old PDF loading
- Old canvas methods
- Old tab management

**Recommendation:**
- **DELETE ALL `#if 0` BLOCKS**
- This is pure technical debt

---

## Deconstruction Plan

### Phase 1: Delete Dead Code
1. Remove all `#if 0` blocks
2. Remove unused InkCanvas forward declarations
3. Remove stubbed InkCanvas method signatures
4. **Estimated reduction: ~1,500 lines**

### Phase 2: Extract Independent Modules

| Module | Source Lines | New File |
|--------|-------------|----------|
| ThemeManager | ~500 | `source/ui/ThemeManager.cpp/h` |
| DialController | ~800 | `source/input/DialController.cpp/h` |
| StylusButtonManager | ~300 | `source/input/StylusButtonManager.cpp/h` |
| SidebarManager | ~400 | `source/ui/SidebarManager.cpp/h` |
| SettingsManager | ~300 | `source/app/SettingsManager.cpp/h` |
| SingleInstanceManager | ~400 | `source/app/SingleInstanceManager.cpp/h` |

**Estimated reduction: ~2,700 lines from MainWindow**

### Phase 3: Simplify setupUi()

| Task | Lines Saved |
|------|-------------|
| Button creation → ToolbarBuilder | ~300 |
| Sidebar creation → SidebarManager::setup() | ~200 |
| Keyboard shortcuts → ShortcutManager::setup() | ~100 |

### Phase 4: Implement Stubbed Features
- Page navigation (switchPage, etc.)
- Bookmark persistence
- PDF export (new PdfExporter class)

---

## Final Target Architecture

```
MainWindow (~2,500 lines)
├── Constructor/Destructor
├── Window state (fullscreen, control bar toggle)
├── Tab coordination (creates tabs, responds to tab changes)
├── Document operations (save/load/add page)
├── High-level UI coordination
└── Event routing

source/ui/
├── ToolbarManager.cpp/h    - Tool buttons, layouts
├── SidebarManager.cpp/h    - All sidebars, floating tabs
├── ThemeManager.cpp/h      - Theming, icons, stylesheets
├── LayerPanel.cpp/h        - (already exists)
├── TabManager.cpp/h        - (already exists)
└── DebugOverlay.cpp/h      - (already exists)

source/input/
├── DialController.cpp/h    - Magic dial, modes
├── StylusButtonManager.cpp/h - Stylus side buttons
└── SDLControllerManager.cpp/h - (already exists)

source/app/
├── SettingsManager.cpp/h   - All QSettings access
├── SingleInstanceManager.cpp/h - IPC, file association
└── main.cpp                - (already exists)
```

---

## Summary

| Current State | After Deconstruction |
|---------------|---------------------|
| MainWindow: ~9,700 lines | MainWindow: ~2,500 lines |
| Monolithic setupUi() | Modular setup via managers |
| 50+ manual widget updates | Theme signal subscriptions |
| Mixed UI/logic | Separated concerns |
| Dead InkCanvas code | Clean slate |

**Next Steps:**
1. Review this document and confirm priorities
2. Start with Phase 1 (dead code removal) - low risk, high impact
3. Extract modules one at a time, testing after each extraction

