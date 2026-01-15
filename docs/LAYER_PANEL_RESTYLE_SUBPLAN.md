# LayerPanel Touch-Friendly Restyle Subplan

> **Purpose:** Restyle LayerPanel to be touchscreen-friendly and consistent with other UI elements
> **Created:** Jan 14, 2026
> **Status:** 🆕 PLANNING

---

## Overview

The LayerPanel is functionally complete but uses small desktop-style buttons that are difficult to use on touchscreens. This subplan covers restyling to match the ActionBarButton style used elsewhere in SpeedyNote.

### Current Problems

| Issue | Impact |
|-------|--------|
| 28x28px buttons | Too small for touch (44px minimum recommended) |
| Text buttons ("All", "None", "Merge") | Inconsistent with icon-based UI |
| Click-area visibility toggle | Hacky, unreliable on touch |
| Small checkboxes | Difficult to tap accurately |
| QListWidget default styling | Doesn't match app theme |

---

## Target Design

### Button Specifications

| Button Type | Size | Style |
|-------------|------|-------|
| Icon buttons | 36×36px | ActionBarButton style, themed icons |
| Text/pill buttons | 72×36px | Pill-shaped, ActionBarButton style |

### Layout

```
┌─────────────────────────────────────────┐
│  Layers                          (title)│
├─────────────────────────────────────────┤
│  ┌─────────────────────────────────────┐│
│  │ [👁] [⬜] Layer 3              (top) ││
│  │ [👁] [⬜] Layer 2                    ││
│  │ [👁] [⬜] Layer 1           (bottom) ││
│  └─────────────────────────────────────┘│
├─────────────────────────────────────────┤
│  [  All/None  ] [   Merge   ]    (row 1)│
│  [ + ][ - ][ ↑ ][ ↓ ][ ⧉ ]       (row 2)│
└─────────────────────────────────────────┘
```

### Layer List Items

Each layer item will be a custom widget containing:
- **Visibility toggle** (36×36px button with eye icon)
- **Selection toggle** (toggle switch, touch-friendly)
- **Layer name** (tappable to select, double-tap to edit)

Item height: ~48px for comfortable touch targets

---

## Phase Breakdown

### Phase L.1: Custom Layer Item Widget

**Goal:** Replace QListWidget items with custom touch-friendly widgets.

**New class: `LayerItemWidget`**

```cpp
class LayerItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit LayerItemWidget(int layerIndex, QWidget* parent = nullptr);
    
    void setLayerName(const QString& name);
    void setVisible(bool visible);
    void setSelected(bool selected);  // Checkbox/toggle state
    void setActive(bool active);      // Currently editing layer
    void setDarkMode(bool dark);
    
signals:
    void visibilityToggled(int index, bool visible);
    void selectionToggled(int index, bool selected);
    void clicked(int index);          // Single click to select as active
    void editRequested(int index);    // Double-click to rename
    
private:
    int m_layerIndex;
    QPushButton* m_visibilityButton;  // 36x36 eye icon
    QCheckBox* m_selectionToggle;     // Or custom toggle switch
    QLabel* m_nameLabel;
    bool m_isActive = false;
};
```

**Files to create:**
- `source/ui/widgets/LayerItemWidget.h`
- `source/ui/widgets/LayerItemWidget.cpp`

**Tasks:**
1. [x] Create LayerItemWidget class
2. [x] Implement visibility button with themed eye icon
3. [x] Implement selection toggle (checkbox-style with checkmark)
4. [x] Implement name label with active/inactive styling
5. [x] Handle single-click (select as active) vs double-click (edit name)
6. [x] Support dark/light mode theming

---

### Phase L.2: Replace QListWidget with QScrollArea

**Goal:** Use QScrollArea with custom LayerItemWidgets instead of QListWidget.

**Changes to LayerPanel:**
- Replace `QListWidget* m_layerList` with `QScrollArea* m_layerScrollArea`
- Add `QVBoxLayout* m_layerLayout` inside scroll area
- Store `QVector<LayerItemWidget*> m_layerItems`

**Tasks:**
1. [x] Replace QListWidget with QScrollArea + QVBoxLayout
2. [x] Update `refreshLayerList()` to create/update LayerItemWidgets
3. [x] Update `rowToLayerIndex()` / `layerIndexToRow()` for new structure (renamed to widgetIndexToLayerIndex/layerIndexToWidgetIndex)
4. [x] Connect LayerItemWidget signals to LayerPanel slots
5. [x] Ensure proper cleanup when layer count changes (`clearLayerItems()`)

---

### Phase L.3: Restyle Action Buttons

**Goal:** Replace current buttons with ActionBarButton-style buttons.

**Top row (72×36px pill buttons):**
| Button | Text | Action |
|--------|------|--------|
| All/None | "All" / "None" | Toggle between select all and deselect all |
| Merge | "Merge" | Merge selected layers (enabled when 2+ selected) |

**Bottom row (36×36px icon buttons):**
| Button | Icon | Action |
|--------|------|--------|
| Add | `layer_add.png` | Add new layer |
| Remove | `layer_remove.png` | Remove selected layer |
| Move Up | `layer_up.png` | Move layer up (higher z-order) |
| Move Down | `layer_down.png` | Move layer down (lower z-order) |
| Duplicate | `layer_duplicate.png` | Duplicate selected layer |

**Icon files needed (regular + _reversed variants):**
- `layer_add.png` / `layer_add_reversed.png`
- `layer_remove.png` / `layer_remove_reversed.png`
- `layer_up.png` / `layer_up_reversed.png`
- `layer_down.png` / `layer_down_reversed.png`
- `layer_duplicate.png` / `layer_duplicate_reversed.png`
- `visible.png` / `visible_reversed.png`
- `notvisible.png` / `notvisible_reversed.png` (already exists)

**Tasks:**
1. [x] Create styled 72×36px pill button class (or reuse/extend ActionBarButton) - Created `LayerPanelPillButton`
2. [x] Create 36×36px icon buttons using ActionBarButton pattern
3. [x] Implement All/None toggle button (switches text on click) - Toggle logic in `onSelectAllClicked()`
4. [x] Load themed icons based on dark/light mode - Uses `setIconName()` and `setDarkMode()`
5. [x] Update button layout to match design spec - Two rows: top (pills), bottom (icons)
6. [x] Connect new buttons to existing slot handlers

---

### Phase L.4: Theme Integration

**Goal:** Ensure all elements respect dark/light mode.

**Tasks:**
1. [x] Add `setDarkMode(bool)` method to LayerPanel - Done in L.2/L.3
2. [x] Propagate dark mode to all LayerItemWidgets - Done in L.3
3. [x] Update icon loading for all buttons - Done in L.3 via ActionBarButton
4. [x] Style scroll area background to match theme - `updateScrollAreaStyle()` with inset background
5. [x] Style active layer highlight color - Updated to match app theme (desaturated steel/cornflower blue)

---

### Phase L.5: Polish & Testing

**Goal:** Final testing and edge case handling.

**Tasks:**
1. [ ] Test with 1 layer (remove button disabled)
2. [ ] Test with 10+ layers (scrolling)
3. [ ] Test layer rename via double-tap
4. [ ] Test visibility toggle
5. [ ] Test selection toggle for merge
6. [ ] Test All/None toggle behavior
7. [ ] Test dark/light mode switching
8. [ ] Verify touch targets are comfortable on tablet

---

## Files to Modify

| File | Changes |
|------|---------|
| `source/ui/widgets/LayerItemWidget.h` | NEW - Custom layer item widget |
| `source/ui/widgets/LayerItemWidget.cpp` | NEW - Implementation |
| `source/ui/sidebars/LayerPanel.h` | Replace QListWidget, add new button members |
| `source/ui/sidebars/LayerPanel.cpp` | New layout, new button handling |
| `CMakeLists.txt` | Add new source files |
| `resources/icons/` | New layer icons (user will provide) |

---

## Icon Specifications

All icons should be:
- **Size:** 24×24px (will be displayed in 36×36 or 72×36 buttons)
- **Format:** PNG with transparency
- **Variants:** Regular (dark icon for light mode) + `_reversed` (light icon for dark mode)

| Icon Name | Description |
|-----------|-------------|
| `layer_add` | Plus symbol or layer with plus |
| `layer_remove` | Minus symbol or layer with minus |
| `layer_up` | Up arrow or layer moving up |
| `layer_down` | Down arrow or layer moving down |
| `layer_duplicate` | Two overlapping layers or copy symbol |
| `visible` | Open eye |
| `notvisible` | Crossed-out eye (already exists) |

---

## Success Criteria

- [ ] All buttons are at least 36×36px
- [ ] Layer items are easy to tap on touchscreen
- [ ] Visibility toggle is a proper button, not click-area detection
- [ ] Selection uses toggle switch instead of checkbox
- [ ] All/None button toggles between states
- [ ] Icons load correctly for dark/light modes
- [ ] Layout matches other panels in SpeedyNote
- [ ] No functional regressions from current LayerPanel

---

## Dependencies

| Dependency | Status |
|------------|--------|
| ActionBarButton pattern | ✅ Exists |
| Dark/light mode detection | ✅ Exists |
| Layer icons | ⏳ User will provide |
| Existing LayerPanel logic | ✅ Complete, don't modify |

---

## Notes

- This is a **visual-only** restyle - all layer functionality remains unchanged
- The abstracted layer access methods (Phase 5.6.7) make this easier
- Consider extracting common button styling into shared utility if not already done
- Toggle switch may need a new custom widget or styled QCheckBox

---

*Subplan for LayerPanel Touch-Friendly Restyle - Jan 14, 2026*
