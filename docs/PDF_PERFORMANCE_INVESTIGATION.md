# PDF Performance Investigation

> **Status:** 🔴 UNRESOLVED - Root cause not yet identified
> **Created:** Dec 26, 2024
> **Last Updated:** Dec 26, 2024

---

## Problem Description

### Symptoms
When a PDF is loaded as a page background in SpeedyNote:
- **Rapid-fire strokes cause severe lag** (user describes as "several hundred times" worse than non-PDF)
- Strokes appear delayed, with visible input latency
- After finishing drawing, debug messages continue for ~5 seconds before catching up
- The lag persists regardless of stroke complexity

### Baseline Comparison
When drawing on a non-PDF page (same document structure, just no PDF background):
- **Drawing is "blazing fast"** (user's words)
- No perceptible input lag
- Strokes appear immediately

---

## Architecture Overview

### Current Implementation (NEW Architecture)
```
DocumentViewport (container)
└── PageWidget (per visible page)
    ├── BackgroundWidget (HIDDEN - cache management only)
    │   └── Caches: background + PDF + grid + ALL committed strokes
    │
    └── LayerWidget (OPAQUE - only ONE, for active layer)
        └── paintEvent:
            1. Blit BackgroundWidget's cache (opaque RGB32 pixmap)
            2. Render current in-progress stroke only
```

### Design Goals
- **Decouple stroke performance from PDF complexity**
- BackgroundWidget cache built once, only updated incrementally
- LayerWidget only renders current stroke on top of cached background
- No transparency compositing (LayerWidget is opaque)

---

## What Was Tested and Ruled Out

### ❌ NOT THE CAUSE: LayerWidget Paint Time

**Test:** Measured paint timing in `LayerWidget::paintEvent()`

**Results:**
| Metric | With PDF | Without PDF |
|--------|----------|-------------|
| bgBlit | 219-231 µs | 167-203 µs |
| strokeCache | 0 µs | 0 µs |
| currentStroke | 16-18 µs | 29-75 µs |
| **TOTAL** | **236-248 µs** | **211-278 µs** |

**Conclusion:** Paint timing is **NEARLY IDENTICAL** (~250 µs both cases). Non-PDF is sometimes SLOWER due to longer strokes. Paint time is NOT the bottleneck.

---

### ❌ NOT THE CAUSE: Transparent Pixmap Blitting

**Issue Investigated:** Alpha blending overhead when blitting transparent pixmaps

**Fix Applied:** Changed BackgroundWidget cache from `QPixmap` (unknown format) to `QImage::Format_RGB32` (explicit opaque format)

**Before:** bgBlit could be 400-900 µs with PDF
**After:** bgBlit is 200-250 µs with PDF

**Conclusion:** This improved consistency but did NOT resolve the core lag issue. The difference was 2-3x, not "several hundred times."

---

### ❌ NOT THE CAUSE: VectorLayer Stroke Cache Rebuilds

**Issue Investigated:** O(n) cache rebuilds when strokes are added

**Evidence:** Debug log showed:
```
VectorLayer: FULL cache rebuild, rendering 64 strokes
VectorLayer: FULL cache rebuild, rendering 44 strokes
VectorLayer: FULL cache rebuild, rendering 25 strokes
```

**Fix Applied:** 
1. Modified `VectorLayer::addStroke()` to NOT touch the cache
2. Removed `invalidateStrokeCache()` calls from `LayerWidget::setPageSize()` and `setZoom()`
3. VectorLayer's stroke cache is now completely unused in NEW architecture

**Result:** Cache rebuilds now only happen during initial document load, NOT during stroke drawing

**Conclusion:** These rebuilds only happen once during load. Stroke drawing no longer triggers them.

---

### ❌ NOT THE CAUSE: Qt Transparency Compositing

**Issue Investigated:** Qt's widget model causes parent widgets to repaint when transparent children update

**Evidence:** Initial testing showed BackgroundWidget and LayerWidget paint counts were identical (both repainting on every stroke update)

**Fix Applied:**
1. `BackgroundWidget` is now **HIDDEN** (`m_backgroundWidget->hide()`)
2. `LayerWidget` is now **OPAQUE** (`setAttribute(Qt::WA_OpaquePaintEvent, true)`)
3. `LayerWidget` blits the hidden BackgroundWidget's cache directly
4. Only ONE LayerWidget exists (for the active layer)

**Result:** BackgroundWidget no longer paints during stroke drawing. Only LayerWidget paints.

**Conclusion:** Transparency compositing was eliminated. This was a necessary fix but did NOT resolve the core lag.

---

### ❌ NOT THE CAUSE: Background Cache Rebuilds During Strokes

**Test:** Checked if `BackgroundWidget::rebuildCache()` is called during stroke drawing

**Evidence:** Debug logs during stroke drawing show:
```
BackgroundWidget::appendStrokeToCache - rendered 1 stroke (O(1))
```
NOT:
```
BackgroundWidget::ensureCacheValid - rebuilding cache
BackgroundWidget: Rebuilding cache with activeLayer=
```

**Conclusion:** Background cache is NOT rebuilt during strokes. Only O(1) incremental updates occur.

---

### ❌ NOT THE CAUSE: syncPageWidgets() During Strokes

**Test:** Verified `syncPageWidgets()` is not called during normal stroke drawing

**Code Review:** `syncPageWidgets()` is called from:
- `setPanOffset()` (scrolling)
- `resizeEvent()` (window resize)
- `setUseNewArchitecture()` (toggle)
- Constructor/setup

**Result:** Has early-exit optimization - if visible pages haven't changed, just updates positions without widget create/destroy

**Conclusion:** Not called during normal stroke drawing on same page

---

### ❌ NOT THE CAUSE: Multiple LayerWidgets

**Test:** Verified only ONE LayerWidget is created per page

**Code Review:** `PageWidget::createLayerWidgets()` explicitly creates only one LayerWidget for the active layer:
```cpp
// Create just ONE LayerWidget for the active layer
// Inactive layers are rendered into BackgroundWidget's cache
VectorLayer* activeLayer = m_page->layer(m_activeLayerIndex);
if (activeLayer) {
    LayerWidget* lw = new LayerWidget(this);
    // ...
    m_layerWidgets.append(lw);
}
```

**Conclusion:** Only one LayerWidget exists. Multiple widgets are not the issue.

---

### ❌ NOT THE CAUSE: OLD Architecture Running

**Test:** Verified NEW architecture is active during stroke drawing

**Evidence:** Debug logs show:
```
Architecture toggled: NEW (PageWidgets)
LayerWidget::paintEvent # 1 layer: "Layer 1" active: true
NEW ARCH: beginStroke on page 1
PageWidget::beginStroke - activeLayerIndex: 0
```

**Conclusion:** NEW architecture IS active. OLD architecture code is properly skipped.

---

## What Remains Unexplained

### The Core Mystery
- Paint timing is ~250 µs in BOTH PDF and non-PDF cases
- Yet user experiences "several hundred times" worse lag with PDF
- This suggests ~250 µs × 100-300 = 25-75 ms of unexplained latency

### Possible Remaining Causes (NOT YET TESTED)

1. **Input Event Delivery Latency**
   - OS/driver-level delays with PDF in memory
   - Qt event queue processing differences
   - Tablet driver behavior changes

2. **Memory Pressure Effects**
   - PDF cache consumes ~14 MB per page
   - System may have more cache misses
   - Page faults during event processing

3. **Qt Event Loop Blocking**
   - Something blocking between input event and paint
   - Event coalescing behaving differently

4. **Poppler Library Background Work**
   - PDF library may be doing background processing
   - Thread contention with UI thread

5. **Document Structure Differences**
   - PDF documents have more strokes initially (64+44+25 = 133)
   - Non-PDF test may have started with fewer strokes

---

## Test Environment

- **Platform:** Windows 10 (WSL)
- **Qt Version:** Qt 6
- **PDF Library:** Poppler
- **Test Document:** `sleep.json` (3 pages with PDF background)

---

## Debug Output Samples

### PDF Case (Laggy)
```
loadDocument: Loaded 3 pages from "C:/Users/1/Documents/sleep.json"
VectorLayer: FULL cache rebuild, rendering 64 strokes
VectorLayer: FULL cache rebuild, rendering 44 strokes
VectorLayer: FULL cache rebuild, rendering 25 strokes
NEW ARCHITECTURE: Creating PageWidgets for document with 3 pages
Architecture toggled: NEW (PageWidgets)
LayerWidget::paintEvent # 1 layer: "Layer 1" active: true hasBgCache: true bgCacheSize: QSize(1588, 2244)
  TIMING AVG (us): bgBlit= 909 strokeCache= 0 currentStroke= 0 TOTAL= 909
[... many strokes later ...]
LayerWidget::paintEvent # 401 layer: "Layer 1" active: true hasBgCache: true bgCacheSize: QSize(1588, 2244)
  TIMING AVG (us): bgBlit= 219 strokeCache= 0 currentStroke= 16 TOTAL= 236
```

### Non-PDF Case (Blazing Fast)
```
LayerWidget::paintEvent # 601 layer: "Layer 1" active: true hasBgCache: true bgCacheSize: QSize(1632, 2112)
  TIMING AVG (us): bgBlit= 199 strokeCache= 0 currentStroke= 54 TOTAL= 254
[... many strokes ...]
LayerWidget::paintEvent # 1001 layer: "Layer 1" active: true hasBgCache: true bgCacheSize: QSize(1632, 2112)
  TIMING AVG (us): bgBlit= 203 strokeCache= 0 currentStroke= 75 TOTAL= 278
```

---

## Recommended Next Steps

1. **Add End-to-End Timing**
   - Measure from `tabletEvent()` arrival to `LayerWidget::paintEvent()` completion
   - Identify where the 25-75 ms of unexplained latency is occurring

2. **Test on Different Hardware**
   - Rule out system-specific memory/driver issues

3. **Profile with Qt Creator Profiler**
   - Identify hot spots outside of our measured code

4. **Reduce PDF Memory Footprint**
   - Test with smaller PDF cache (2 pages instead of 4-8)
   - See if memory pressure is the cause

5. **Test with Poppler Background Work Disabled**
   - If possible, disable any async Poppler operations

---

## Files Modified During Investigation

| File | Changes |
|------|---------|
| `source/viewport/BackgroundWidget.cpp` | RGB32 format, appendStrokeToCache |
| `source/viewport/LayerWidget.cpp` | WA_OpaquePaintEvent, timing debug, removed stroke cache blit |
| `source/viewport/PageWidget.cpp` | Hidden BackgroundWidget, single LayerWidget, cache management |
| `source/layers/VectorLayer.h` | Removed cache operations from addStroke() |

---

## Conclusion

The PDF performance issue remains **UNRESOLVED**. All measured timing shows similar performance between PDF and non-PDF cases (~250 µs per paint), yet the user experiences significantly worse lag with PDF.

The root cause appears to be **OUTSIDE** of our rendering code, possibly in:
- Input event delivery
- Memory/system-level effects
- Qt event loop behavior

Further investigation is needed with profiling tools that can measure the entire event pipeline, not just the paint cycle.

---

*Investigation document for SpeedyNote PDF performance issue*

