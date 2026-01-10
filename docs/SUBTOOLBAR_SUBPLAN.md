# Subtoolbar Implementation Subplan

**Document Version:** 1.0  
**Date:** January 9, 2026  
**Status:** 🔵 READY TO IMPLEMENT  
**Prerequisites:** Toolbar extraction complete (Phase C of TOOLBAR_EXTRACTION_SUBPLAN.md)
**References:** `docs/SUBTOOLBAR_QA.md` for design decisions

---

## Overview

Implement subtoolbars that provide tool-specific options. Subtoolbars float on the left side of DocumentViewport, vertically centered, and swap based on the current tool.

### Final Design Summary

| Subtoolbar | Buttons | Contents |
|------------|---------|----------|
| Pen | 6 | 3 color presets + 3 thickness presets |
| Marker | 6 | 3 color presets (shared w/ Highlighter) + 3 thickness presets |
| Highlighter | 4 | 3 color presets (shared w/ Marker) + 1 auto-highlight toggle |
| ObjectSelect | 5 | 2 mode toggles + 3 slot buttons |
| Eraser | 0 | No subtoolbar (hidden) |
| Lasso | 0 | No subtoolbar (hidden) |

### Visual Layout
```
┌─────────────────────────────────────────────────────────────┐
│ Toolbar [Pen][Marker][Eraser][Lasso][Object][Text][📏][↩️][↪️] │
├─────────────────────────────────────────────────────────────┤
│        │                                                    │
│  ┌───┐ │                                                    │
│  │ ● │ │                                                    │
│  │ ● │ │                                                    │
│  │ ● │ │              DocumentViewport                      │
│  ├───┤ │                                                    │
│  │ ╱ │ │                                                    │
│  │ ╱ │ │                                                    │
│  │ ╱ │ │                                                    │
│  └───┘ │                                                    │
│   24px │                                                    │
│        │                                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Custom Widgets

### Task 1.1: ColorPresetButton ✅ COMPLETE

**File:** `source/ui/widgets/ColorPresetButton.h/cpp`

**Description:** A round button displaying a filled color circle. Click behavior:
- Click unselected → Select this preset (apply color)
- Click selected → Open QColorDialog to edit

**Visual States:**
- Unselected: Color-filled circle with thin neutral border
- Selected: Color-filled circle with white border (dark mode) or black border (light mode)
- Pressed: Darken/lighten effect

**Properties:**
```cpp
class ColorPresetButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    
public:
    explicit ColorPresetButton(QWidget* parent = nullptr);
    
    QColor color() const;
    void setColor(const QColor& color);
    
    bool isSelected() const;
    void setSelected(bool selected);
    
signals:
    void clicked();           // Emitted on any click
    void colorChanged(QColor color);
    void selectedChanged(bool selected);
    void editRequested();     // Emitted when selected button is clicked (open dialog)
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    QColor m_color = Qt::black;
    bool m_selected = false;
    bool m_pressed = false;
};
```

**Size:** 36×36 logical pixels, 18px border radius (fully round)

**Estimated:** ~150 lines

---

### Task 1.2: ThicknessPresetButton

**File:** `source/ui/widgets/ThicknessPresetButton.h/cpp`

**Description:** A round button displaying a diagonal line preview scaled to represent thickness. Click behavior same as ColorPresetButton.

**Visual States:**
- Unselected: Diagonal line with thin neutral border
- Selected: Diagonal line with white/black border
- Pressed: Darken/lighten effect

**Properties:**
```cpp
class ThicknessPresetButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor)  // For preview
    
public:
    explicit ThicknessPresetButton(QWidget* parent = nullptr);
    
    qreal thickness() const;
    void setThickness(qreal thickness);
    
    bool isSelected() const;
    void setSelected(bool selected);
    
    QColor lineColor() const;
    void setLineColor(const QColor& color);
    
signals:
    void clicked();
    void thicknessChanged(qreal thickness);
    void selectedChanged(bool selected);
    void editRequested();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    // ... mouse events same as ColorPresetButton
    
private:
    qreal m_thickness = 2.0;
    bool m_selected = false;
    bool m_pressed = false;
    QColor m_lineColor = Qt::black;
};
```

**Edit Dialog:** Modal `QDialog` with:
- `QSlider` (horizontal)
- `QDoubleSpinBox` (connected to slider)
- OK/Cancel buttons

**Size:** 36×36 logical pixels, fully round

**Estimated:** ~200 lines (including edit dialog)

---

### Task 1.3: ToggleButton

**File:** `source/ui/widgets/ToggleButton.h/cpp`

**Description:** Simple on/off toggle button with icon.

**Properties:**
```cpp
class ToggleButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon)
    Q_PROPERTY(QString toolTip READ toolTip WRITE setToolTip)
    
public:
    explicit ToggleButton(QWidget* parent = nullptr);
    
    bool isChecked() const;
    void setChecked(bool checked);
    
    QIcon icon() const;
    void setIcon(const QIcon& icon);
    
signals:
    void toggled(bool checked);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    bool m_checked = false;
    bool m_pressed = false;
    QIcon m_icon;
};
```

**Visual States:**
- Unchecked: Icon with neutral background
- Checked: Icon with accent/highlighted background
- Pressed: Darken/lighten

**Size:** 36×36 logical pixels, fully round

**Estimated:** ~100 lines

---

### Task 1.4: ModeToggleButton

**File:** `source/ui/widgets/ModeToggleButton.h/cpp`

**Description:** Two-state toggle that shows different icons based on current mode. Click toggles between states.

**Properties:**
```cpp
class ModeToggleButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int currentMode READ currentMode WRITE setCurrentMode NOTIFY modeChanged)
    
public:
    explicit ModeToggleButton(QWidget* parent = nullptr);
    
    void setModeIcons(const QIcon& mode0Icon, const QIcon& mode1Icon);
    void setModeToolTips(const QString& mode0Tip, const QString& mode1Tip);
    
    int currentMode() const;  // 0 or 1
    void setCurrentMode(int mode);
    
signals:
    void modeChanged(int mode);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    int m_currentMode = 0;
    bool m_pressed = false;
    QIcon m_icons[2];
    QString m_toolTips[2];
};
```

**Usage:**
- Insert mode: Image (0) ↔ Link (1)
- Action mode: Select (0) ↔ Create (1)

**Size:** 36×36 logical pixels, fully round

**Estimated:** ~120 lines

---

### Task 1.5: LinkSlotButton

**File:** `source/ui/widgets/LinkSlotButton.h/cpp`

**Description:** Shows LinkObject slot state with appropriate icon. Supports long-press to delete slot content.

**States:**
- Empty: Plus icon (+)
- Position: Position icon (📍 or similar)
- URL: Link icon (🔗)
- Markdown: Markdown icon (📝)

**Properties:**
```cpp
enum class LinkSlotState { Empty, Position, Url, Markdown };

class LinkSlotButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(LinkSlotState state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    
public:
    explicit LinkSlotButton(QWidget* parent = nullptr);
    
    LinkSlotState state() const;
    void setState(LinkSlotState state);
    
    bool isSelected() const;
    void setSelected(bool selected);
    
signals:
    void clicked();
    void stateChanged(LinkSlotState state);
    void selectedChanged(bool selected);
    void deleteRequested();   // Emitted on long-press of non-empty slot
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;  // For long-press detection
    
private:
    LinkSlotState m_state = LinkSlotState::Empty;
    bool m_selected = false;
    bool m_pressed = false;
    int m_longPressTimer = 0;
    
    static constexpr int LONG_PRESS_MS = 500;
};
```

**Long-press behavior:**
- Empty slot: Do nothing
- Filled slot: Emit `deleteRequested()` → show confirmation → clear slot content

**Size:** 36×36 logical pixels, fully round

**Estimated:** ~180 lines

---

## Phase 2: Framework

### Task 2.1: SubToolbar Base Class

**File:** `source/ui/subtoolbars/SubToolbar.h/cpp`

**Description:** Abstract base class for all subtoolbars.

```cpp
class SubToolbar : public QWidget {
    Q_OBJECT
    
public:
    explicit SubToolbar(QWidget* parent = nullptr);
    virtual ~SubToolbar() = default;
    
    // Called when subtoolbar becomes visible (refresh from settings)
    virtual void refreshFromSettings() = 0;
    
    // Called when tab changes (restore per-tab state)
    virtual void restoreTabState(int tabIndex) = 0;
    virtual void saveTabState(int tabIndex) = 0;
    
protected:
    // Helper to add separator line
    void addSeparator();
    
    // Shared styling
    void setupStyle();
    
    QVBoxLayout* m_layout = nullptr;
};
```

**Styling:**
- Fixed width: ~44px (36 button + 8 padding)
- Rounded corners
- Shadow/border
- Background color from theme

**Estimated:** ~80 lines

---

### Task 2.2: SubToolbarContainer

**File:** `source/ui/subtoolbars/SubToolbarContainer.h/cpp`

**Description:** Manages subtoolbar swapping and positioning.

```cpp
class SubToolbarContainer : public QWidget {
    Q_OBJECT
    
public:
    explicit SubToolbarContainer(QWidget* parent = nullptr);
    
    // Register subtoolbars for each tool
    void setSubToolbar(ToolType tool, SubToolbar* subtoolbar);
    
    // Show subtoolbar for tool (hide if nullptr registered)
    void showForTool(ToolType tool);
    
    // Update position (call when viewport resizes)
    void updatePosition(const QRect& viewportRect);
    
    // Tab state management
    void onTabChanged(int newTabIndex, int oldTabIndex);
    
public slots:
    void onToolChanged(ToolType tool);
    
private:
    QHash<ToolType, SubToolbar*> m_subtoolbars;
    SubToolbar* m_currentSubToolbar = nullptr;
    ToolType m_currentTool = ToolType::Pen;
    int m_currentTabIndex = 0;
    
    static constexpr int LEFT_OFFSET = 24;  // px from viewport left edge
};
```

**Positioning:**
- 24px from left edge of viewport
- Vertically centered based on current subtoolbar's height
- Recalculate on viewport resize and subtoolbar swap

**Estimated:** ~150 lines

---

## Phase 3: Tool Subtoolbars

### Task 3.1: PenSubToolbar

**File:** `source/ui/subtoolbars/PenSubToolbar.h/cpp`

**Layout:**
```
[Color1]  ← ColorPresetButton (red default)
[Color2]  ← ColorPresetButton (blue default)
[Color3]  ← ColorPresetButton (black default)
─────────
[Thick1]  ← ThicknessPresetButton (2.0 default)
[Thick2]  ← ThicknessPresetButton (5.0 default)
[Thick3]  ← ThicknessPresetButton (10.0 default)
```

**Connections:**
- `colorPreset->clicked()` → select preset, emit `penColorChanged(color)`
- `colorPreset->editRequested()` → open QColorDialog, save to QSettings
- `thicknessPreset->clicked()` → select preset, emit `penThicknessChanged(thickness)`
- `thicknessPreset->editRequested()` → open edit dialog, save to QSettings

**QSettings Keys:**
- `pen/color1`, `pen/color2`, `pen/color3`
- `pen/thickness1`, `pen/thickness2`, `pen/thickness3`
- `pen/selectedColor` (0-2)
- `pen/selectedThickness` (0-2)

**Per-Tab State:**
- Store modified colors/thicknesses in memory per tab
- Store selection indices per tab

**Estimated:** ~250 lines

---

### Task 3.2: MarkerSubToolbar

**File:** `source/ui/subtoolbars/MarkerSubToolbar.h/cpp`

**Layout:** Same as Pen (6 buttons)

**Key Difference:** Colors are SHARED with Highlighter
- QSettings keys: `marker/color1`, `marker/color2`, `marker/color3`
- These same keys are used by HighlighterSubToolbar

**Thickness is separate:**
- `marker/thickness1` (8.0 default), etc.

**Estimated:** ~200 lines (similar to Pen, shares some logic)

---

### Task 3.3: HighlighterSubToolbar

**File:** `source/ui/subtoolbars/HighlighterSubToolbar.h/cpp`

**Layout:**
```
[Color1]  ← ColorPresetButton (shared with Marker)
[Color2]  ← ColorPresetButton (shared with Marker)
[Color3]  ← ColorPresetButton (shared with Marker)
─────────
[Auto✓]   ← ToggleButton (auto-highlight mode)
```

**Connections:**
- Color presets: Same QSettings keys as Marker (`marker/color1`, etc.)
- `autoToggle->toggled()` → emit `autoHighlightChanged(bool)`

**Estimated:** ~150 lines

---

### Task 3.4: ObjectSelectSubToolbar

**File:** `source/ui/subtoolbars/ObjectSelectSubToolbar.h/cpp`

**Layout:**
```
[🖼️↔🔗]  ← ModeToggleButton (Insert mode: Image/Link)
[👆↔➕]  ← ModeToggleButton (Action mode: Select/Create)
─────────
[Slot1]   ← LinkSlotButton
[Slot2]   ← LinkSlotButton
[Slot3]   ← LinkSlotButton
```

**Slot Button Visibility:**
- Always visible, but state reflects selected LinkObject
- If no LinkObject selected: all show Empty state
- If LinkObject selected: show actual slot states

**Connections:**
- `insertModeToggle->modeChanged()` → emit `insertModeChanged(ObjectInsertMode)`
- `actionModeToggle->modeChanged()` → emit `actionModeChanged(ObjectActionMode)`
- `slotButton->clicked()` → emit `slotActivated(int index)`
- `slotButton->deleteRequested()` → confirm, then emit `slotCleared(int index)`

**Estimated:** ~200 lines

---

## Phase 4: Integration

### Task 4.1: MainWindow Integration

**File:** `source/MainWindow.cpp`

**Changes:**
1. Create `SubToolbarContainer` as child of central widget
2. Create all subtoolbar instances
3. Register subtoolbars with container
4. Connect signals:

```cpp
// In MainWindow constructor or setup method:

m_subtoolbarContainer = new SubToolbarContainer(this);

m_penSubToolbar = new PenSubToolbar();
m_markerSubToolbar = new MarkerSubToolbar();
m_highlighterSubToolbar = new HighlighterSubToolbar();
m_objectSelectSubToolbar = new ObjectSelectSubToolbar();

m_subtoolbarContainer->setSubToolbar(ToolType::Pen, m_penSubToolbar);
m_subtoolbarContainer->setSubToolbar(ToolType::Marker, m_markerSubToolbar);
m_subtoolbarContainer->setSubToolbar(ToolType::Highlighter, m_highlighterSubToolbar);
m_subtoolbarContainer->setSubToolbar(ToolType::ObjectSelect, m_objectSelectSubToolbar);
// Eraser, Lasso - no subtoolbar (nullptr)

// Connect tool changes
connect(m_toolbar, &Toolbar::toolSelected, 
        m_subtoolbarContainer, &SubToolbarContainer::onToolChanged);

// Connect subtoolbar signals to viewport
connect(m_penSubToolbar, &PenSubToolbar::penColorChanged,
        m_viewport, &DocumentViewport::setPenColor);
connect(m_penSubToolbar, &PenSubToolbar::penThicknessChanged,
        m_viewport, &DocumentViewport::setPenThickness);
// ... etc for all signals
```

**Estimated:** ~100 lines of new code

---

### Task 4.2: Viewport Resize Handling

**File:** `source/MainWindow.cpp`

Connect viewport resize to container position update:

```cpp
// In resizeEvent or via signal:
m_subtoolbarContainer->updatePosition(m_viewport->geometry());
```

**Estimated:** ~10 lines

---

### Task 4.3: Tab Change Handling

**File:** `source/MainWindow.cpp`

```cpp
// When tab changes:
void MainWindow::onTabChanged(int newIndex) {
    int oldIndex = m_previousTabIndex;
    m_subtoolbarContainer->onTabChanged(newIndex, oldIndex);
    m_previousTabIndex = newIndex;
    // ... existing tab change logic
}
```

**Estimated:** ~20 lines

---

### Task 4.4: Toolbar Straight Line Toggle

**File:** `source/ui/Toolbar.cpp`

Change Shape button to Straight Line toggle:
- Keep existing button position
- Change icon to straight line (📏 or ruler icon)
- Change behavior from tool selection to toggle
- Connect to `DocumentViewport::setStraightLineMode(bool)`

**Estimated:** ~30 lines changed

---

## Phase 5: Polish & Testing

### Task 5.1: Theme Support

Ensure all custom widgets respect dark/light theme:
- Border colors (white in dark, black in light)
- Background colors
- Icon variants

### Task 5.2: Tooltip Support

Add tooltips to all buttons:
- ColorPresetButton: "Red (click to select, click again to edit)"
- ThicknessPresetButton: "2.0pt (click to select, click again to edit)"
- ModeToggleButton: "Image insert mode (click to switch to Link)"
- etc.

### Task 5.3: Testing

- [ ] Color preset selection works
- [ ] Color preset editing opens dialog, saves
- [ ] Thickness preset selection works
- [ ] Thickness preset editing works
- [ ] Switching tools swaps subtoolbar
- [ ] Subtoolbar vertically centers correctly
- [ ] Per-tab state works (switch tabs, presets restore)
- [ ] App close saves current tab's presets
- [ ] Marker/Highlighter colors stay in sync
- [ ] ObjectSelect mode toggles work
- [ ] Slot buttons reflect LinkObject state
- [ ] Long-press delete works with confirmation
- [ ] Straight line toggle in toolbar works

---

## Summary

| Phase | Tasks | Estimated Lines |
|-------|-------|-----------------|
| 1. Custom Widgets | 5 widgets | ~750 |
| 2. Framework | 2 classes | ~230 |
| 3. Subtoolbars | 4 subtoolbars | ~800 |
| 4. Integration | 4 tasks | ~160 |
| 5. Polish | 3 tasks | ~100 |
| **Total** | | **~2040 lines** |

### Implementation Order

1. **ColorPresetButton** + **ThicknessPresetButton** (core widgets)
2. **SubToolbar** base + **SubToolbarContainer**
3. **PenSubToolbar** (test the system end-to-end)
4. **MarkerSubToolbar** (test shared color state)
5. **ToggleButton** + **HighlighterSubToolbar**
6. **ModeToggleButton** + **LinkSlotButton**
7. **ObjectSelectSubToolbar**
8. **Toolbar modification** (Shape → Straight Line)
9. **Polish & Testing**

---

## Dependencies

- `source/ui/Toolbar.h/cpp` - Already exists
- `source/core/DocumentViewport.h/cpp` - Already exists
- `source/core/ToolType.h` - Already exists
- QSettings - Qt built-in

## Files to Create

```
source/ui/widgets/
├── ColorPresetButton.h
├── ColorPresetButton.cpp
├── ThicknessPresetButton.h
├── ThicknessPresetButton.cpp
├── ToggleButton.h
├── ToggleButton.cpp
├── ModeToggleButton.h
├── ModeToggleButton.cpp
├── LinkSlotButton.h
└── LinkSlotButton.cpp

source/ui/subtoolbars/
├── SubToolbar.h
├── SubToolbar.cpp
├── SubToolbarContainer.h
├── SubToolbarContainer.cpp
├── PenSubToolbar.h
├── PenSubToolbar.cpp
├── MarkerSubToolbar.h
├── MarkerSubToolbar.cpp
├── HighlighterSubToolbar.h
├── HighlighterSubToolbar.cpp
├── ObjectSelectSubToolbar.h
└── ObjectSelectSubToolbar.cpp
```

**Total: 22 new files**

---

## Implementation Status

### Phase 1: Custom Button Widgets

#### Task 1.1: ColorPresetButton ✅
**Status:** COMPLETED
- Created `source/ui/widgets/ColorPresetButton.h` (~90 lines)
- Created `source/ui/widgets/ColorPresetButton.cpp` (~200 lines)
- Includes `ColorEditDialog` for custom color selection
- Click unselected → select, click selected → open color picker
- Visual states: unselected (thin neutral border), selected (high contrast border), pressed (darken effect)
- 36×36 pixels, fully round

#### Task 1.2: ThicknessPresetButton ✅
**Status:** COMPLETED
- Created `source/ui/widgets/ThicknessPresetButton.h` (~95 lines)
- Created `source/ui/widgets/ThicknessPresetButton.cpp` (~250 lines)
- Includes `ThicknessEditDialog` with synchronized slider + spinbox
- Diagonal line preview with logarithmic scaling for visual representation
- Line color property for preview (matches current tool color)
- Same click behavior as ColorPresetButton
- 36×36 pixels, fully round

#### Task 1.3: SubToolbarToggle ✅
**Status:** COMPLETED
- Created `source/ui/widgets/ToggleButton.h` (~85 lines) - contains `SubToolbarToggle` class
- Created `source/ui/widgets/ToggleButton.cpp` (~160 lines)
- **Note:** Class renamed to `SubToolbarToggle` to avoid conflict with existing `ToggleButton` in `ToolbarButtons.h`
- Simple on/off toggle with icon
- Visual states: unchecked (neutral gray bg), checked (accent blue bg), pressed (darken)
- Hover effect on unchecked state (lighten)
- Click toggles between checked/unchecked
- Emits `toggled(bool)` signal on state change
- Dark/light mode aware background colors
- 36×36 pixels, fully round

#### Task 1.4: ModeToggleButton ✅
**Status:** COMPLETED
- Created `source/ui/widgets/ModeToggleButton.h` (~95 lines)
- Created `source/ui/widgets/ModeToggleButton.cpp` (~160 lines)
- Two-state toggle showing different icons based on current mode (0 or 1)
- `setModeIcons()` to configure icons for each mode
- `setModeToolTips()` to configure tooltips for each mode (auto-updates on mode change)
- Click toggles between mode 0 ↔ mode 1
- Emits `modeChanged(int)` signal on state change
- Visual states: normal (neutral bg), hovered (lighten), pressed (darken)
- Dark/light mode aware background colors
- 36×36 pixels, fully round
- **Usage:** Insert mode (Image↔Link), Action mode (Select↔Create)

#### Task 1.5: LinkSlotButton ✅
**Status:** COMPLETED
- Created `source/ui/widgets/LinkSlotButton.h` (~155 lines)
- Created `source/ui/widgets/LinkSlotButton.cpp` (~270 lines)
- Shows LinkObject slot state with appropriate icon/symbol
- 4 states: `Empty` (+), `Position` (P), `Url` (U), `Markdown` (M)
- `setStateIcons()` for custom icons per state (fallback text symbols if not set)
- `selected` property with visual indicator (thick white/black border)
- Long-press detection (500ms) via `timerEvent()` for filled slots
- Emits `clicked()` on short press, `deleteRequested()` on long-press of non-empty slot
- Empty slots have subtle "inviting" appearance (lighter border/bg)
- Auto-updating tooltips based on state
- Dark/light mode aware colors
- 36×36 pixels, fully round

### Phase 1 Complete! ✅
All 5 custom button widgets implemented:
1. ColorPresetButton - color circles with edit dialog
2. ThicknessPresetButton - diagonal line preview with edit dialog
3. SubToolbarToggle - on/off toggle with icon
4. ModeToggleButton - two-state toggle with different icons
5. LinkSlotButton - slot state display with long-press delete

---

### Phase 2: Framework

#### Task 2.1: SubToolbar Base Class ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/SubToolbar.h` (~90 lines)
- Created `source/ui/subtoolbars/SubToolbar.cpp` (~90 lines)
- Abstract base class with pure virtual methods:
  - `refreshFromSettings()` - load presets from QSettings
  - `restoreTabState(int)` - restore per-tab state
  - `saveTabState(int)` - save per-tab state
- Helper methods: `addSeparator()`, `addWidget()`, `addStretch()`
- Auto-applied styling in constructor:
  - Fixed width: 44px (36 button + 8 padding)
  - Rounded corners (8px radius)
  - Drop shadow effect for depth
  - Theme-aware background/border colors
- Dark/light mode detection via `isDarkMode()`

#### Task 2.2: SubToolbarContainer ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/SubToolbarContainer.h` (~100 lines)
- Created `source/ui/subtoolbars/SubToolbarContainer.cpp` (~130 lines)
- Manages subtoolbar registration and swapping via `setSubToolbar(ToolType, SubToolbar*)`
- Shows/hides appropriate subtoolbar via `showForTool(ToolType)` and `onToolChanged(ToolType)` slot
- Positioning logic in `updatePosition(QRect viewportRect)`:
  - 24px from viewport left edge
  - Vertically centered based on subtoolbar height
  - Clamps to stay within viewport bounds
- Tab state management via `onTabChanged(int newIndex, int oldIndex)`:
  - Calls `saveTabState()` on all subtoolbars for old tab
  - Calls `restoreTabState()` on all subtoolbars for new tab
- Auto-hides when no subtoolbar registered for current tool
- Calls `refreshFromSettings()` when subtoolbar becomes visible

### Phase 2 Complete! ✅
Framework classes implemented:
1. SubToolbar - Abstract base class with shared styling and tab state interface
2. SubToolbarContainer - Manager for swapping and positioning subtoolbars

---

### Phase 3: Tool Subtoolbars

#### Task 3.1: PenSubToolbar ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/PenSubToolbar.h` (~90 lines)
- Created `source/ui/subtoolbars/PenSubToolbar.cpp` (~270 lines)
- Layout: 3 ColorPresetButtons + separator + 3 ThicknessPresetButtons
- Default colors: Red (#FF0000), Blue (#0000FF), Black (#000000)
- Default thicknesses: 2.0, 5.0, 10.0
- Click behavior: unselected → select & emit signal, selected → open edit dialog
- Signals: `penColorChanged(QColor)`, `penThicknessChanged(qreal)`
- QSettings persistence with keys: `pen/color1-3`, `pen/thickness1-3`, `pen/selectedColor`, `pen/selectedThickness`
- Per-tab state via `TabState` struct stored in `QHash<int, TabState>`
- Thickness preview lines update to match selected pen color
- Full SubToolbar interface implementation: `refreshFromSettings()`, `saveTabState()`, `restoreTabState()`

#### Task 3.2: MarkerSubToolbar ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/MarkerSubToolbar.h` (~95 lines)
- Created `source/ui/subtoolbars/MarkerSubToolbar.cpp` (~280 lines)
- Layout: 3 ColorPresetButtons + separator + 3 ThicknessPresetButtons
- **SHARED colors with Highlighter**: uses `marker/color1-3` keys (same as Highlighter will use)
- Default colors: Light red (#FFAAAA), Yellow (#FFFF00), Light blue (#AAAAFF)
- **Marker-specific thicknesses**: `marker/thickness1-3` (8.0, 16.0, 32.0 defaults)
- Separate `saveColorsToSettings()` and `saveThicknessesToSettings()` for clarity
- Signals: `markerColorChanged(QColor)`, `markerThicknessChanged(qreal)`
- Full SubToolbar interface implementation
- When color is edited, change is immediately shared with Highlighter via QSettings

#### Task 3.3: HighlighterSubToolbar ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/HighlighterSubToolbar.h` (~90 lines)
- Created `source/ui/subtoolbars/HighlighterSubToolbar.cpp` (~210 lines)
- Layout: 3 ColorPresetButtons + separator + 1 SubToolbarToggle (auto-highlight)
- **SHARED colors with Marker**: reads/writes `marker/color1-3` keys
- **Highlighter-specific settings**: `highlighter/selectedColor`, `highlighter/autoHighlight`
- No thickness controls (highlighter uses fixed thickness)
- Signals: `highlighterColorChanged(QColor)`, `autoHighlightChanged(bool)`
- Auto-highlight toggle persists to QSettings
- Full SubToolbar interface implementation
- When color is edited, change is immediately shared with Marker via QSettings

#### Task 3.4: ObjectSelectSubToolbar ✅
**Status:** COMPLETED
- Created `source/ui/subtoolbars/ObjectSelectSubToolbar.h` (~110 lines)
- Created `source/ui/subtoolbars/ObjectSelectSubToolbar.cpp` (~220 lines)
- Layout: 2 ModeToggleButtons + separator + 3 LinkSlotButtons
- Insert mode toggle: Image (mode 0) ↔ Link (mode 1) with icons
- Action mode toggle: Select (mode 0) ↔ Create (mode 1) with icons
- 3 LinkSlotButtons showing slot states (Empty/Position/Url/Markdown)
- `updateSlotStates(LinkSlotState[3])` to reflect selected LinkObject
- `clearSlotStates()` when no LinkObject is selected
- Long-press delete with confirmation dialog (`confirmSlotDelete()`)
- Signals: `insertModeChanged()`, `actionModeChanged()`, `slotActivated(int)`, `slotCleared(int)`
- QSettings persistence: `objectSelect/insertMode`, `objectSelect/actionMode`
- Full SubToolbar interface implementation

### Phase 3 Complete! ✅
All 4 tool subtoolbars implemented:
1. PenSubToolbar - 3 colors + 3 thicknesses (6 buttons)
2. MarkerSubToolbar - 3 colors (shared) + 3 thicknesses (6 buttons)
3. HighlighterSubToolbar - 3 colors (shared) + auto-highlight toggle (4 buttons)
4. ObjectSelectSubToolbar - 2 mode toggles + 3 slot buttons (5 buttons)

---

### Phase 4: Integration

#### Task 4.1: MainWindow Integration ✅
**Status:** COMPLETED
- Updated `source/MainWindow.h`:
  - Added forward declarations for subtoolbar classes
  - Added member variables: `m_subtoolbarContainer`, `m_penSubToolbar`, etc.
  - Added `m_canvasContainer` pointer for positioning
  - Added `setupSubToolbars()` and `updateSubToolbarPosition()` methods
- Updated `source/MainWindow.cpp`:
  - Added includes for all subtoolbar headers
  - Stored canvas container pointer for subtoolbar positioning
  - Implemented `setupSubToolbars()` (~100 lines):
    - Creates SubToolbarContainer as child of canvas container
    - Creates all 4 subtoolbar instances
    - Registers subtoolbars with container
    - Connects Toolbar::toolSelected to SubToolbarContainer::onToolChanged
    - Connects subtoolbar signals to DocumentViewport methods
    - Connects TabManager tab changes for per-tab state
  - Implemented `updateSubToolbarPosition()` (~20 lines):
    - Accounts for left sidebar visibility
    - Calls SubToolbarContainer::updatePosition()
  - Called from `updateScrollbarPositions()` for resize handling
- Signal connections:
  - PenSubToolbar → setPenColor, setPenThickness
  - MarkerSubToolbar → setMarkerColor, setMarkerThickness
  - HighlighterSubToolbar → setMarkerColor (shared), setAutoHighlightEnabled
  - ObjectSelectSubToolbar → setObjectInsertMode, setObjectActionMode, activateLinkSlot
- **TODOs for full functionality:**
  - ~~`clearLinkSlot(int)` - needs implementation in DocumentViewport~~ ✅ DONE
  - ~~`setAutoHighlightEnabled(bool)` - currently private, needs to be made public~~ ✅ DONE
  - ~~`setObjectInsertMode(ObjectInsertMode)` - needs to be added to DocumentViewport~~ ✅ DONE
  - ~~`setObjectActionMode(ObjectActionMode)` - needs to be added to DocumentViewport~~ ✅ DONE

---

### Bug Fixes (Post-Implementation)

#### Fix 4.1.1: ColorPresetButton/ThicknessPresetButton editRequested Signal Timing ✅
**Status:** FIXED

**Problem:** Clicking an **unselected** preset button was incorrectly opening the edit dialog, instead of just selecting the preset.

**Root Cause:** In `mouseReleaseEvent()`, the code checked `m_selected` AFTER emitting `clicked()`:
```cpp
emit clicked();
if (m_selected) {        // Bug: m_selected changed by clicked() handler!
    emit editRequested();
}
```
The `clicked()` signal handler in the subtoolbar would call `setSelected(true)`, which changed `m_selected` to `true`. When control returned to `mouseReleaseEvent`, the check saw the NEW state and incorrectly emitted `editRequested()`.

**Fix:** Capture the selection state BEFORE emitting `clicked()`:
```cpp
bool wasSelected = m_selected;  // Capture state BEFORE signal
emit clicked();
if (wasSelected) {              // Use captured state
    emit editRequested();
}
```

**Files Fixed:**
- `source/ui/widgets/ColorPresetButton.cpp`
- `source/ui/widgets/ThicknessPresetButton.cpp`

**Correct Behavior:**
- Click **unselected** button → Select preset, apply value, do NOT open dialog
- Click **selected** button → Open edit dialog

---

#### Fix 4.1.2: Marker/Highlighter Color Opacity Missing ✅
**Status:** FIXED

**Problem:** Marker strokes were appearing at approximately 25% opacity instead of the expected 50%. Additionally, color preset buttons displayed fully opaque colors instead of semi-transparent ones.

**Root Cause:** The `DEFAULT_COLORS` arrays in `MarkerSubToolbar` and `HighlighterSubToolbar` were defined without alpha channels (fully opaque). When these colors were emitted via `markerColorChanged`/`highlighterColorChanged` signals, they overwrote the default `m_markerColor` (which had alpha = 128) with fully opaque colors.

**Fix:** Added `MARKER_OPACITY = 128` constant to both subtoolbars. When emitting color change signals, the alpha channel is now set to `MARKER_OPACITY` before emitting:
```cpp
QColor colorWithOpacity = m_colorButtons[index]->color();
colorWithOpacity.setAlpha(MARKER_OPACITY);
emit markerColorChanged(colorWithOpacity);
```

**Design Decision:** Color buttons display "base" colors without alpha (easier to see in UI). The 50% marker opacity is applied when sending to DocumentViewport.

**Files Fixed:**
- `source/ui/subtoolbars/MarkerSubToolbar.h` (added `MARKER_OPACITY` constant)
- `source/ui/subtoolbars/MarkerSubToolbar.cpp` (apply opacity in emit calls)
- `source/ui/subtoolbars/HighlighterSubToolbar.h` (added `MARKER_OPACITY` constant)
- `source/ui/subtoolbars/HighlighterSubToolbar.cpp` (apply opacity in emit calls)

**Correct Behavior:**
- Marker strokes render at 50% opacity
- Highlighter strokes render at 50% opacity
- Color buttons show base color without transparency (better UX)
- Both Marker and Highlighter share the same opacity constant

---

#### Fix 4.1.3: Auto-Highlight Toggle Not Connected ✅
**Status:** FIXED

**Problem:** The auto-highlight toggle button in HighlighterSubToolbar was not properly connected to DocumentViewport. The toggle had a placeholder that only logged a message instead of calling `setAutoHighlightEnabled()`.

**Root Cause:** When the subtoolbar was originally implemented, `setAutoHighlightEnabled()` was in the private section of DocumentViewport. A placeholder was added with a TODO comment. After moving the method to public, the placeholder was never updated.

**Fix (3 parts):**

1. **MainWindow.cpp** - Updated the `autoHighlightChanged` signal connection to actually call `vp->setAutoHighlightEnabled(enabled)` instead of the placeholder.

2. **HighlighterSubToolbar** - Added `setAutoHighlightState(bool enabled)` public method to update the toggle button state from outside without emitting signals (uses `blockSignals()`).

3. **MainWindow.cpp** - Added bidirectional sync in `connectViewportScrollSignals()`:
   - Connect `autoHighlightEnabledChanged` signal from viewport to update subtoolbar toggle when Ctrl+H is used
   - Sync current state when viewport changes (tab switch)
   - Added `m_autoHighlightConn` member variable for proper cleanup

**Files Fixed:**
- `source/MainWindow.h` (added `m_autoHighlightConn` member)
- `source/MainWindow.cpp` (fixed connection, added viewport→subtoolbar sync)
- `source/ui/subtoolbars/HighlighterSubToolbar.h` (added `setAutoHighlightState()`)
- `source/ui/subtoolbars/HighlighterSubToolbar.cpp` (implemented `setAutoHighlightState()`)

**Correct Behavior:**
- Click toggle button → changes auto-highlight state in DocumentViewport
- Press Ctrl+H (keyboard shortcut) → toggle button updates to reflect new state
- Switch tabs → toggle button syncs to that viewport's auto-highlight state

---

#### Fix 4.1.4: ObjectSelectSubToolbar Mode Toggles Not Connected ✅
**Status:** FIXED

**Problem:** The insert mode toggle (Image ↔ Link) and action mode toggle (Select ↔ Create) buttons in ObjectSelectSubToolbar had placeholder connections that only logged messages instead of actually changing the modes in DocumentViewport.

**Root Cause:** When the subtoolbar was originally implemented, the mode setter methods (`setObjectInsertMode`, `setObjectActionMode`) were not yet implemented in DocumentViewport. Placeholders were added with TODO comments. After adding these methods, the placeholders were never updated.

**Fix (3 parts):**

1. **MainWindow.cpp** - Updated `insertModeChanged` and `actionModeChanged` signal connections to actually call `vp->setObjectInsertMode(mode)` and `vp->setObjectActionMode(mode)`.

2. **ObjectSelectSubToolbar** - Added `setInsertModeState()` and `setActionModeState()` public methods to update toggle button states from outside without emitting signals (uses `blockSignals()`).

3. **MainWindow.cpp** - Added bidirectional sync in `connectViewportScrollSignals()`:
   - Connect `objectInsertModeChanged` signal from viewport to update subtoolbar when Ctrl+< / Ctrl+> is used
   - Connect `objectActionModeChanged` signal from viewport to update subtoolbar when Ctrl+6 / Ctrl+7 is used
   - Sync current states when viewport changes (tab switch)
   - Added `m_insertModeConn` and `m_actionModeConn` member variables for proper cleanup

**Files Fixed:**
- `source/MainWindow.h` (added `m_insertModeConn`, `m_actionModeConn` members)
- `source/MainWindow.cpp` (fixed connections, added viewport→subtoolbar sync)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.h` (added state setter methods)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.cpp` (implemented state setter methods)

**Correct Behavior:**
- Click insert mode toggle → changes ObjectInsertMode in DocumentViewport
- Click action mode toggle → changes ObjectActionMode in DocumentViewport
- Press Ctrl+< / Ctrl+> (keyboard) → insert mode toggle updates to reflect new state
- Press Ctrl+6 / Ctrl+7 (keyboard) → action mode toggle updates to reflect new state
- Switch tabs → mode toggles sync to that viewport's current modes

---

#### Fix 4.1.5: LinkSlot Buttons Not Updating on Selection ✅
**Status:** FIXED

**Problem:** When selecting a LinkObject, the slot buttons in ObjectSelectSubToolbar did not update to show the actual slot states (Empty, Position, URL, Markdown). They always showed empty (+) icons regardless of what the LinkObject slots contained.

**Root Cause:** There was no connection between the viewport's `objectSelectionChanged` signal and the subtoolbar's slot button update logic.

**Fix:**

1. **MainWindow.h** - Added `m_selectionChangedConn` member variable for proper cleanup.

2. **MainWindow.cpp** - Added `#include "objects/LinkObject.h"` to access LinkSlot::Type.

3. **MainWindow.cpp** - Added `updateLinkSlotButtons(DocumentViewport* viewport)` helper function that:
   - Gets selected objects from viewport
   - If exactly one LinkObject is selected, converts `LinkSlot::Type` to `LinkSlotState` for each slot
   - Calls `m_objectSelectSubToolbar->updateSlotStates(states)` or `clearSlotStates()`

4. **MainWindow.cpp** - In `connectViewportScrollSignals()`:
   - Added connection from `objectSelectionChanged` to `updateLinkSlotButtons()`
   - Added initial sync call when viewport changes (tab switch)

**Files Fixed:**
- `source/MainWindow.h` (added `m_selectionChangedConn` member, `updateLinkSlotButtons()` declaration)
- `source/MainWindow.cpp` (added include, helper function, connection)

**Correct Behavior:**
- Select a LinkObject → slot buttons show actual slot states (Position/URL/Markdown/Empty)
- Select an ImageObject → slot buttons show Empty
- Select multiple objects → slot buttons show Empty
- Deselect all → slot buttons show Empty
- Switch tabs → slot buttons sync to that viewport's selection

---

#### Fix 4.1.6: ThicknessPresetButton Line Too Long and Marker Opacity Missing ✅
**Status:** FIXED

**Problems:**
1. The diagonal line in thickness preset buttons extended almost corner-to-corner, not fitting well in the round icon
2. Marker thickness preview didn't show the 50% opacity, making the preview look different from actual marker strokes

**Fixes:**

1. **ThicknessPresetButton.cpp** - Increased line inset from `borderWidth + 4.0` to `borderWidth + 8.0` to make the line shorter and fit nicely within the circular boundary.

2. **MarkerSubToolbar.cpp** - In `updateThicknessPreviewColors()`, added `previewColor.setAlpha(MARKER_OPACITY)` so the thickness preview line shows the actual marker opacity (50%).

**Files Fixed:**
- `source/ui/widgets/ThicknessPresetButton.cpp`
- `source/ui/subtoolbars/MarkerSubToolbar.cpp`

**Correct Behavior:**
- Thickness preview lines are shorter and fit nicely inside the round button
- Marker thickness previews show semi-transparent lines matching actual marker appearance

---

#### Fix 4.1.7: Subtoolbar Icons Not Switching with Dark/Light Mode ✅
**Status:** FIXED

**Problem:** Icons in subtoolbar buttons (`SubToolbarToggle`, `ModeToggleButton`, `LinkSlotButton`) did not switch between regular and `_reversed` versions when the application theme changed between light and dark modes. The icons were loaded once with hardcoded full paths and never updated.

**Root Cause:** The widgets were using `setIcon(QIcon(":/resources/icons/foo.png"))` with full paths, which doesn't support automatic switching. The main `ToolbarButtons` class had this functionality via `setThemedIcon(baseName)` and `setDarkMode(bool)`, but the subtoolbar widgets didn't.

**Fix (multi-part):**

1. **SubToolbarToggle** (`ToggleButton.h/cpp`) - Added:
   - `m_iconBaseName` and `m_darkMode` member variables
   - `setIconName(const QString& baseName)` - set icon by base name (enables dark mode switching)
   - `setDarkMode(bool darkMode)` - switch icon based on mode
   - `updateIcon()` - builds path: `baseName.png` or `baseName_reversed.png`

2. **ModeToggleButton** - Added:
   - `m_iconBaseNames[2]` and `m_darkMode` member variables
   - `setModeIconNames(mode0BaseName, mode1BaseName)` - set icons by base name
   - `setDarkMode(bool darkMode)` - switch icons based on mode
   - `updateIcons()` - builds paths for both modes

3. **LinkSlotButton** - Added:
   - `m_iconBaseNames[4]` and `m_darkMode` member variables
   - `setStateIconNames(empty, position, url, markdown)` - set icons by base name
   - `setDarkMode(bool darkMode)` - switch icons based on mode
   - `updateIcons()` - builds paths for all states

4. **SubToolbar** (base class) - Added virtual `setDarkMode(bool)` for subclasses to override.

5. **HighlighterSubToolbar** - Override `setDarkMode()` to propagate to `m_autoHighlightToggle`. Updated `createWidgets()` to use `setIconName("marker")` instead of full path.

6. **ObjectSelectSubToolbar** - Override `setDarkMode()` to propagate to mode toggles and slot buttons. Updated `createWidgets()` to use `setModeIconNames()` and `setStateIconNames()` instead of full paths.

7. **SubToolbarContainer** - Added `setDarkMode(bool)` to propagate to all registered subtoolbars.

8. **MainWindow::updateTheme()** - Added `m_subtoolbarContainer->setDarkMode(darkMode)` call.

**Files Modified:**
- `source/ui/widgets/ToggleButton.h` (added dark mode support)
- `source/ui/widgets/ToggleButton.cpp` (implemented dark mode support)
- `source/ui/widgets/ModeToggleButton.h` (added dark mode support)
- `source/ui/widgets/ModeToggleButton.cpp` (implemented dark mode support)
- `source/ui/widgets/LinkSlotButton.h` (added dark mode support)
- `source/ui/widgets/LinkSlotButton.cpp` (implemented dark mode support)
- `source/ui/subtoolbars/SubToolbar.h` (added virtual setDarkMode)
- `source/ui/subtoolbars/SubToolbar.cpp` (added base setDarkMode implementation)
- `source/ui/subtoolbars/HighlighterSubToolbar.h` (added setDarkMode override)
- `source/ui/subtoolbars/HighlighterSubToolbar.cpp` (implemented setDarkMode, use setIconName)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.h` (added setDarkMode override)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.cpp` (implemented setDarkMode, use icon name methods)
- `source/ui/subtoolbars/SubToolbarContainer.h` (added setDarkMode)
- `source/ui/subtoolbars/SubToolbarContainer.cpp` (implemented setDarkMode)
- `source/MainWindow.cpp` (call setDarkMode in updateTheme)

**Pattern for icon paths:**
- Light mode: `:/resources/icons/{baseName}.png`
- Dark mode: `:/resources/icons/{baseName}_reversed.png`

**Correct Behavior:**
- When app theme changes (light ↔ dark), all subtoolbar icons update to appropriate variant
- Icons are set via base names (e.g., "marker") instead of full paths
- Initial state is determined by `isDarkMode()` check at widget creation time

