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

### Task 1.1: ColorPresetButton

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

