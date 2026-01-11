# Action Bar Implementation Subplan

**Document Version:** 1.0  
**Date:** January 10, 2026  
**Status:** 🔵 READY TO IMPLEMENT  
**Prerequisites:** Subtoolbar framework complete (Phase D.2 of plans.txt)
**References:** `docs/ACTIONBAR_QA.md` for design decisions

---

## Overview

Implement context-sensitive Action Bars that provide quick access to editing operations for tablet/touch users. Action Bars float on the right side of DocumentViewport (symmetrical to subtoolbars), and swap based on current tool and selection state.

### Final Design Summary

| Action Bar | Tool Context | Trigger | Buttons |
|------------|--------------|---------|---------|
| LassoActionBar | Lasso | Lasso selection exists | Copy, Cut, Paste*, Delete |
| ObjectSelectActionBar | ObjectSelect | Object(s) selected | Copy, Paste, Delete, Forward, Backward, ToFront, ToBack |
| TextSelectionActionBar | Highlighter | PDF text selected | Copy |
| ClipboardActionBar | ObjectSelect | Clipboard has image, no selection | Paste |

*Paste only visible if internal stroke clipboard is non-empty

### Visual Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Toolbar [Pen][Marker][Eraser][Lasso][Object][Text][📏][↩️][↪️] │
├─────────────────────────────────────────────────────────────┤
│        │                                            │       │
│  ┌───┐ │                                            │ ┌───┐ │
│  │ ● │ │                                            │ │📋│ │
│  │ ● │ │                                            │ │✂️│ │
│  │ ● │ │              DocumentViewport              │ │📄│ │
│  ├───┤ │                                            │ │🗑️│ │
│  │ ╱ │ │                                            │ └───┘ │
│  │ ╱ │ │                                            │  24px │
│  │ ╱ │ │                                            │       │
│  └───┘ │                                            │       │
│   24px │                                            │       │
│        │                                            │       │
│ SUBTOOLBAR                                      ACTION BAR  │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Framework

### Task 1.1: ActionBar Base Class

**File:** `source/ui/actionbars/ActionBar.h/cpp`

**Description:** Abstract base class for all action bars (mirrors SubToolbar).

```cpp
class ActionBar : public QWidget {
    Q_OBJECT

public:
    explicit ActionBar(QWidget* parent = nullptr);
    virtual ~ActionBar() = default;
    
    // Called to update button visibility based on current state
    virtual void updateButtonStates() = 0;
    
    // Dark mode support
    virtual void setDarkMode(bool darkMode);

protected:
    // Helper to add buttons
    void addButton(QWidget* button);
    void addSeparator();
    
    // Shared styling (same as SubToolbar)
    void setupStyle();
    bool isDarkMode() const;
    
    QVBoxLayout* m_layout = nullptr;
    
    static constexpr int ACTIONBAR_WIDTH = 44;
    static constexpr int PADDING = 4;
    static constexpr int BORDER_RADIUS = 8;
};
```

**Styling:** Same as SubToolbar - rounded corners, shadow, theme-aware.

**Estimated:** ~100 lines

---

### Task 1.2: ActionBarButton Widget

**File:** `source/ui/widgets/ActionBarButton.h/cpp`

**Description:** Round button for action bar (similar to existing toolbar buttons but 36x36).

```cpp
class ActionBarButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled)

public:
    explicit ActionBarButton(QWidget* parent = nullptr);
    
    void setIcon(const QIcon& icon);
    void setIconName(const QString& baseName);  // For dark mode switching
    void setDarkMode(bool darkMode);
    void setToolTip(const QString& tip);
    
    bool isEnabled() const;
    void setEnabled(bool enabled);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateIcon();
    
    QIcon m_icon;
    QString m_iconBaseName;
    bool m_darkMode = false;
    bool m_enabled = true;
    bool m_pressed = false;
    bool m_hovered = false;
    
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int ICON_SIZE = 20;
};
```

**Visual States:**
- Normal: Neutral gray background
- Hovered: Slightly lighter
- Pressed: Darker
- Disabled: Grayed out, no hover effects

**Estimated:** ~150 lines

---

### Task 1.3: ActionBarContainer

**File:** `source/ui/actionbars/ActionBarContainer.h/cpp`

**Description:** Manages action bar swapping and positioning (mirrors SubToolbarContainer).

```cpp
class ActionBarContainer : public QWidget {
    Q_OBJECT

public:
    explicit ActionBarContainer(QWidget* parent = nullptr);
    
    // Register action bars
    void setActionBar(const QString& type, ActionBar* actionBar);
    
    // Show/hide based on context
    void showActionBar(const QString& type);
    void hideActionBar();
    
    // Position update (call on viewport resize)
    void updatePosition(const QRect& viewportRect);
    
    // Dark mode
    void setDarkMode(bool darkMode);
    
    // Animation control
    void setAnimationEnabled(bool enabled);

public slots:
    // Context updates from DocumentViewport
    void onLassoSelectionChanged(bool hasSelection);
    void onObjectSelectionChanged();
    void onTextSelectionChanged(bool hasSelection);
    void onClipboardChanged();
    void onToolChanged(ToolType tool);
    
private:
    void updateVisibility();
    void animateShow();
    void animateHide();
    
    QHash<QString, ActionBar*> m_actionBars;
    ActionBar* m_currentActionBar = nullptr;
    QString m_currentType;
    ToolType m_currentTool = ToolType::Pen;
    QRect m_viewportRect;
    
    // Clipboard state (cached)
    bool m_clipboardHasImage = false;
    
    // Animation
    QPropertyAnimation* m_animation = nullptr;
    bool m_animationEnabled = true;
    
    static constexpr int RIGHT_OFFSET = 24;
    static constexpr int ANIMATION_DURATION = 150;
};
```

**Positioning:**
- 24px from right edge of viewport
- Vertically centered based on current action bar's height

**Estimated:** ~200 lines

---

## Phase 2: Action Bars

### Task 2.1: LassoActionBar

**File:** `source/ui/actionbars/LassoActionBar.h/cpp`

**Layout:**
```
[Copy]    ← ActionBarButton
[Cut]     ← ActionBarButton
[Paste]   ← ActionBarButton (only if internal clipboard non-empty)
[Delete]  ← ActionBarButton
```

**Signals:**
```cpp
signals:
    void copyRequested();
    void cutRequested();
    void pasteRequested();
    void deleteRequested();
```

**Button visibility:**
- Copy, Cut, Delete: Always visible when action bar is shown
- Paste: Only visible if `hasStrokesInClipboard()` returns true

**Estimated:** ~120 lines

---

### Task 2.2: ObjectSelectActionBar

**File:** `source/ui/actionbars/ObjectSelectActionBar.h/cpp`

**Layout:**
```
[Copy]      ← ActionBarButton
[Paste]     ← ActionBarButton
[Delete]    ← ActionBarButton
─────────── ← Separator
[Forward]   ← ActionBarButton (Ctrl+])
[Backward]  ← ActionBarButton (Ctrl+[)
[ToFront]   ← ActionBarButton (Ctrl+Shift+])
[ToBack]    ← ActionBarButton (Ctrl+Shift+[)
```

**Signals:**
```cpp
signals:
    void copyRequested();
    void pasteRequested();
    void deleteRequested();
    void bringForwardRequested();
    void sendBackwardRequested();
    void bringToFrontRequested();
    void sendToBackRequested();
```

**Estimated:** ~180 lines

---

### Task 2.3: TextSelectionActionBar

**File:** `source/ui/actionbars/TextSelectionActionBar.h/cpp`

**Layout:**
```
[Copy]  ← ActionBarButton (only button)
```

**Signals:**
```cpp
signals:
    void copyRequested();
```

**Note:** This is the simplest action bar - only Copy is relevant for PDF text.

**Estimated:** ~80 lines

---

### Task 2.4: ClipboardActionBar

**File:** `source/ui/actionbars/ClipboardActionBar.h/cpp`

**Layout:**
```
[Paste]  ← ActionBarButton (only button)
```

**Signals:**
```cpp
signals:
    void pasteRequested();
```

**Trigger:** Shows in ObjectSelect tool when:
1. Clipboard has an image (detected via `QClipboard::dataChanged`)
2. No object is currently selected

**Estimated:** ~80 lines

---

## Phase 3: Integration

### Task 3.1: DocumentViewport Signals

**File:** `source/core/DocumentViewport.h/cpp`

Add missing signals for action bar triggers:

```cpp
signals:
    // Existing
    void objectSelectionChanged();
    
    // New - needed for action bars
    void lassoSelectionChanged(bool hasSelection);
    void textSelectionChanged(bool hasSelection);
    void strokeClipboardChanged(bool hasStrokes);  // For Paste button visibility
```

**Emit points:**
- `lassoSelectionChanged`: In `clearLassoSelection()`, `applySelectionTransform()`, after lasso completion
- `textSelectionChanged`: In text selection code, `m_textSelection.clear()`
- `strokeClipboardChanged`: In `copySelection()`, `cutSelection()`

**Estimated:** ~50 lines

---

### Task 3.2: MainWindow Integration

**File:** `source/MainWindow.h/cpp`

**Changes:**
1. Create `ActionBarContainer` as child of canvas container
2. Create all action bar instances
3. Register action bars with container
4. Connect signals:

```cpp
// In MainWindow constructor or setup method:

m_actionBarContainer = new ActionBarContainer(m_canvasContainer);

m_lassoActionBar = new LassoActionBar();
m_objectSelectActionBar = new ObjectSelectActionBar();
m_textSelectionActionBar = new TextSelectionActionBar();
m_clipboardActionBar = new ClipboardActionBar();

m_actionBarContainer->setActionBar("lasso", m_lassoActionBar);
m_actionBarContainer->setActionBar("objectSelect", m_objectSelectActionBar);
m_actionBarContainer->setActionBar("textSelection", m_textSelectionActionBar);
m_actionBarContainer->setActionBar("clipboard", m_clipboardActionBar);

// Connect tool changes
connect(m_toolbar, &Toolbar::toolSelected, 
        m_actionBarContainer, &ActionBarContainer::onToolChanged);

// Connect clipboard changes
connect(QApplication::clipboard(), &QClipboard::dataChanged,
        m_actionBarContainer, &ActionBarContainer::onClipboardChanged);

// Connect action bar signals to viewport
connect(m_lassoActionBar, &LassoActionBar::copyRequested,
        this, [this]() { if (auto vp = currentViewport()) vp->copySelection(); });
// ... etc for all signals
```

**Estimated:** ~150 lines

---

### Task 3.3: Viewport Connection Handler

**File:** `source/MainWindow.cpp`

Update `connectViewportScrollSignals()` to connect action bar signals:

```cpp
// In connectViewportScrollSignals():

// Disconnect previous action bar connections
QObject::disconnect(m_lassoSelectionConn);
QObject::disconnect(m_objectSelectionConn);
QObject::disconnect(m_textSelectionConn);

// Connect new viewport's signals
m_lassoSelectionConn = connect(vp, &DocumentViewport::lassoSelectionChanged,
    m_actionBarContainer, &ActionBarContainer::onLassoSelectionChanged);
    
m_objectSelectionConn = connect(vp, &DocumentViewport::objectSelectionChanged,
    [this]() { m_actionBarContainer->onObjectSelectionChanged(); });
    
m_textSelectionConn = connect(vp, &DocumentViewport::textSelectionChanged,
    m_actionBarContainer, &ActionBarContainer::onTextSelectionChanged);
```

**Estimated:** ~50 lines

---

### Task 3.4: Position Update

**File:** `source/MainWindow.cpp`

Add position update call alongside subtoolbar:

```cpp
void MainWindow::updateActionBarPosition()
{
    if (!m_actionBarContainer) return;
    
    QRect viewportRect = /* get viewport geometry */;
    m_actionBarContainer->updatePosition(viewportRect);
}
```

Call from `updateScrollbarPositions()` or similar.

**Estimated:** ~20 lines

---

## Phase 4: Polish

### Task 4.1: Animation Implementation

**File:** `source/ui/actionbars/ActionBarContainer.cpp`

Implement slide-in animation:

```cpp
void ActionBarContainer::animateShow()
{
    if (!m_animationEnabled || !m_currentActionBar) {
        show();
        return;
    }
    
    // Stop any existing animation
    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
    }
    
    // Start position: 50px to the right of final position
    QPoint startPos = pos() + QPoint(50, 0);
    QPoint endPos = pos();
    
    m_animation = new QPropertyAnimation(this, "pos", this);
    m_animation->setDuration(ANIMATION_DURATION);
    m_animation->setStartValue(startPos);
    m_animation->setEndValue(endPos);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    
    show();
    move(startPos);
    m_animation->start(QAbstractAnimation::DeleteWhenStopped);
}
```

**Estimated:** ~50 lines

---

### Task 4.2: Dark Mode Support

Ensure all action bar components support dark mode:

```cpp
void MainWindow::updateTheme()
{
    bool darkMode = isDarkMode();
    // ... existing code ...
    m_actionBarContainer->setDarkMode(darkMode);
}
```

**Estimated:** ~20 lines

---

### Task 4.3: Tooltips

Add tooltips to all action bar buttons:

| Button | Tooltip |
|--------|---------|
| Copy | "Copy (Ctrl+C)" |
| Cut | "Cut (Ctrl+X)" |
| Paste | "Paste (Ctrl+V)" |
| Delete | "Delete" |
| Forward | "Bring Forward (Ctrl+])" |
| Backward | "Send Backward (Ctrl+[)" |
| ToFront | "Bring to Front (Ctrl+Shift+])" |
| ToBack | "Send to Back (Ctrl+Shift+[)" |

**Estimated:** ~10 lines

---

### Task 4.4: Tab Change Handling

Update action bar visibility on tab change:

```cpp
// In tab change handler:
void MainWindow::onTabChanged(DocumentViewport* newViewport)
{
    // ... existing code ...
    
    // Update action bar based on new viewport's state
    if (newViewport) {
        ToolType tool = newViewport->currentTool();
        m_actionBarContainer->onToolChanged(tool);
        
        // Trigger re-evaluation of selection state
        // Action bars will update based on new viewport's selection
    }
}
```

**Estimated:** ~20 lines

---

## Summary

| Phase | Tasks | Estimated Lines |
|-------|-------|-----------------|
| 1. Framework | 3 tasks | ~450 |
| 2. Action Bars | 4 action bars | ~460 |
| 3. Integration | 4 tasks | ~270 |
| 4. Polish | 4 tasks | ~100 |
| **Total** | | **~1280 lines** |

### Implementation Order

1. **ActionBarButton** widget (reusable button)
2. **ActionBar** base class (styling, layout)
3. **ActionBarContainer** (positioning, swapping)
4. **LassoActionBar** (test the system end-to-end)
5. **DocumentViewport signals** (lassoSelectionChanged, etc.)
6. **MainWindow integration** (create, connect signals)
7. **ObjectSelectActionBar**
8. **TextSelectionActionBar**
9. **ClipboardActionBar** + clipboard detection
10. **Animation & Polish**

---

## Files to Create

```
source/ui/actionbars/
├── ActionBar.h
├── ActionBar.cpp
├── ActionBarContainer.h
├── ActionBarContainer.cpp
├── LassoActionBar.h
├── LassoActionBar.cpp
├── ObjectSelectActionBar.h
├── ObjectSelectActionBar.cpp
├── TextSelectionActionBar.h
├── TextSelectionActionBar.cpp
├── ClipboardActionBar.h
└── ClipboardActionBar.cpp

source/ui/widgets/
├── ActionBarButton.h
└── ActionBarButton.cpp
```

**Total: 14 new files**

---

## Dependencies

- `source/ui/subtoolbars/SubToolbar.h` - Styling reference
- `source/core/DocumentViewport.h` - Selection signals, clipboard methods
- `source/core/ToolType.h` - Tool type enum
- `QPropertyAnimation` - For slide animation
- `QClipboard` - For clipboard detection

---

## Testing Checklist

- [ ] LassoActionBar appears when lasso selection exists
- [ ] LassoActionBar Paste button only shows when stroke clipboard non-empty
- [ ] LassoActionBar disappears when selection cleared
- [ ] ObjectSelectActionBar appears when object selected
- [ ] ObjectSelectActionBar layer buttons work
- [ ] TextSelectionActionBar appears when PDF text selected
- [ ] ClipboardActionBar appears when clipboard has image + ObjectSelect tool + no selection
- [ ] ClipboardActionBar hides when object is selected
- [ ] Action Bar positioned correctly (right side, 24px from edge, vertically centered)
- [ ] Slide-in animation works
- [ ] Dark mode icons switch correctly
- [ ] Tab switching updates action bar correctly
- [ ] All tooltips display correctly

---

## Implementation Status

### Phase 1: Framework
- [x] Task 1.1: ActionBar base class ✅
- [x] Task 1.2: ActionBarButton widget ✅
- [x] Task 1.3: ActionBarContainer ✅

### Phase 2: Action Bars
- [x] Task 2.1: LassoActionBar ✅
- [x] Task 2.2: ObjectSelectActionBar ✅
- [x] Task 2.3: TextSelectionActionBar ✅
- [x] Task 2.4: ClipboardActionBar ✅

### Phase 3: Integration
- [x] Task 3.1: DocumentViewport signals ✅
- [x] Task 3.2: MainWindow integration ✅
- [x] Task 3.3: Viewport connection handler ✅
- [x] Task 3.4: Position update ✅ (completed in Task 3.2)

### Phase 4: Polish
- [ ] Task 4.1: Animation implementation
- [ ] Task 4.2: Dark mode support
- [ ] Task 4.3: Tooltips
- [ ] Task 4.4: Tab change handling

---

## Bug Fixes (Post-Implementation)

### Fix 1: Action Bars Not Working on First Tab

**Problem:** Action bars did not work on the first tab created after application launch. Creating a second tab would make action bars functional.

**Root Cause:** In `TabManager::createTab()`, when `m_tabBar->addTab()` is called for the first tab, Qt automatically selects it and emits `currentChanged(0)`. At that moment, `m_viewports` is still empty because `m_viewports.append(viewport)` hadn't been executed yet. This caused `onCurrentChanged(0)` to pass a null viewport to signal handlers.

**Solution:** Block signals on `m_tabBar` during the entire tab creation process, then manually sync the viewport stack and emit `currentViewportChanged(viewport)` after all data structures are properly initialized.

**File:** `source/ui/TabManager.cpp`

```cpp
// BUG FIX: Block signals during tab creation
m_tabBar->blockSignals(true);

int index = m_tabBar->addTab(title);
m_viewportStack->addWidget(viewport);
m_viewports.append(viewport);
// ...
m_tabBar->setCurrentIndex(index);

m_tabBar->blockSignals(false);

// Manually sync viewport stack (blocked signal normally does this)
m_viewportStack->setCurrentIndex(index);

// Manually emit with valid viewport
emit currentViewportChanged(viewport);
```

---

### Fix 2: Pasted Objects Have Incorrect zOrder

**Problem:** Pasted objects (from clipboard) and newly inserted objects (via Ctrl+V from external clipboard) had zOrder=0 by default. If existing objects on the page had higher zOrders (e.g., from previous "bring to front" operations), the new objects would render below them despite being inserted later.

**Root Cause:** When objects are first created (`insertImageFromClipboard()`, `insertImageFromFile()`, `createLinkObjectAtPosition()`, `createLinkObjectForHighlight()`, `pasteObjects()`), they used the default `zOrder = 0` from `InsertedObject`. The `zOrder` was only changed when users explicitly used "bring to front/back" operations.

**Solution:** Added `getNextZOrderForAffinity(Page* page, int affinity)` helper function that calculates `max(existing zOrders) + 1` for objects with the same affinity. This ensures newly inserted/pasted objects always appear on top.

**Files Modified:**
- `source/core/DocumentViewport.h` - Added `getNextZOrderForAffinity()` declaration
- `source/core/DocumentViewport.cpp` - Implemented helper, updated:
  - `insertImageFromClipboard()`
  - `insertImageFromFile()`
  - `pasteObjects()`
  - `createLinkObjectForHighlight()`
  - `createLinkObjectAtPosition()`

```cpp
int DocumentViewport::getNextZOrderForAffinity(Page* page, int affinity) const
{
    if (!page) return 0;
    
    int maxZOrder = -1;  // Start below 0 so first object gets zOrder = 0
    for (const auto& obj : page->objects) {
        if (obj && obj->getLayerAffinity() == affinity) {
            maxZOrder = qMax(maxZOrder, obj->zOrder);
        }
    }
    return maxZOrder + 1;
}
```

**Note:** Undo/redo operations correctly restore objects with their original serialized zOrder values, so they were not modified.

---

### Fix 3: ObjectSelectActionBar Paste Button Dismissal

**Problem:** The paste button on ObjectSelectActionBar persisted indefinitely after copying an object. Users had no way to dismiss it without a keyboard (pressing Escape), which is problematic for tablet users.

**Solution:** Added a Cancel button to ObjectSelectSubToolbar and Escape key handling for ObjectSelect tool in DocumentViewport.

**Behavior:**
1. First press: If objects are selected, deselect them
2. Second press: If no objects selected but clipboard has content, clear the clipboard

**Files Modified:**
- `source/core/DocumentViewport.h` - Added `cancelObjectSelectAction()` and `clearObjectClipboard()` methods
- `source/core/DocumentViewport.cpp` - Implemented methods and added Escape key handling in ObjectSelect tool
- `source/ui/actionbars/ObjectSelectActionBar.h` - Added `cancelRequested()` signal and `m_cancelButton` member
- `source/ui/actionbars/ObjectSelectActionBar.cpp` - Added cancel button with cross icon (visible in paste-only mode)
- `source/MainWindow.cpp` - Connected `ObjectSelectActionBar::cancelRequested` to `DocumentViewport::cancelObjectSelectAction()`

```cpp
// DocumentViewport::cancelObjectSelectAction()
void DocumentViewport::cancelObjectSelectAction()
{
    // Step 1: If objects are selected, deselect them
    if (!m_selectedObjects.isEmpty()) {
        deselectAllObjects();
        return;
    }
    
    // Step 2: If no objects selected but clipboard has content, clear clipboard
    if (!m_objectClipboard.isEmpty()) {
        clearObjectClipboard();
    }
}
```

The Cancel button appears on the **ActionBar** (floating panel on the right side) alongside the Paste button when in paste-only mode. This is the correct placement since Cancel is an action, not a tool setting - the subtoolbar (on the left) is reserved for tool settings/modes.

---

## Post-Implementation Code Review

### CR-AB-1: Separator Initial Color Hardcoded ✅
**Status:** FIXED

**Problem:** In `ObjectSelectActionBar::setupButtons()`, the separator was created with a hardcoded light mode color (`#CCCCCC`) instead of checking the current theme.

**Fix:** Added `isDarkMode()` check when setting initial separator color.

**File Modified:** `source/ui/actionbars/ObjectSelectActionBar.cpp`

---

### CR-AB-2: Context State Not Synced on Tab Switch ✅
**Status:** FIXED

**Problem:** When switching tabs, only `onToolChanged()` and `onObjectSelectionChanged()` were called. Other context states (`m_hasLassoSelection`, `m_hasTextSelection`, `m_hasStrokesInClipboard`, `m_hasObjectsInClipboard`) were NOT synced, causing the action bar to potentially show/hide incorrectly after a tab switch.

**Example bug scenario:**
1. Tab 1: Make a lasso selection (LassoActionBar shows)
2. Switch to Tab 2 (LassoActionBar should hide because Tab 2 has no selection)
3. Bug: LassoActionBar might still show because `m_hasLassoSelection` retained its old value

**Fix:** 
1. Added getter methods to `DocumentViewport.h`:
   - `hasLassoSelection()` - checks `m_lassoSelection.isValid()`
   - `hasTextSelection()` - checks `m_textSelection.isValid()`
   - `hasStrokesInClipboard()` - checks `m_clipboard.hasContent`
   - `hasObjectsInClipboard()` - checks `!m_objectClipboard.isEmpty()`

2. Updated `MainWindow::connectViewportScrollSignals()` to sync ALL context states:
```cpp
// Sync all selection/clipboard states
m_actionBarContainer->onLassoSelectionChanged(viewport->hasLassoSelection());
m_actionBarContainer->onObjectSelectionChanged(viewport->hasSelectedObjects());
m_actionBarContainer->onTextSelectionChanged(viewport->hasTextSelection());
m_actionBarContainer->onStrokeClipboardChanged(viewport->hasStrokesInClipboard());
m_actionBarContainer->onObjectClipboardChanged(viewport->hasObjectsInClipboard());
```

**Files Modified:**
- `source/core/DocumentViewport.h` (added getter methods)
- `source/MainWindow.cpp` (sync all states on tab switch)

---

### CR-AB-3: isDarkMode() Duplication (Low Severity)
**Status:** NOT FIXED (Minor, documented)

**Problem:** The `isDarkMode()` function is duplicated in multiple files:
- `ActionBar.cpp`
- `ActionBarButton.cpp`
- All subtoolbar widgets

**Decision:** Same as CR-ST-4 from subtoolbar review - this is a minor code smell, not a bug. The function is simple (5 lines) and extracting it would require either a utility class or common base class. Acceptable for now.

---

### Code Review Summary

| Issue | Severity | Status |
|-------|----------|--------|
| CR-AB-1: Separator hardcoded color | **Low** | ✅ FIXED |
| CR-AB-2: Tab switch state sync | **Medium** | ✅ FIXED |
| CR-AB-3: isDarkMode() duplication | **Low** | Documented |

