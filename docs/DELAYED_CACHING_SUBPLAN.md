# Delayed Stroke Caching Subplan

> **Purpose:** Fix PDF performance degradation during rapid stroke drawing
> **Created:** Dec 26, 2024
> **Status:** 📋 PLANNING

---

## Problem Statement

### Current Behavior (Slow)
```
Stroke 1 complete → addStroke() → invalidateStrokeCache()
Next paint → ensureStrokeCacheValid() → rebuildStrokeCache() [O(n) - renders ALL strokes]

Stroke 2 complete → addStroke() → invalidateStrokeCache()
Next paint → ensureStrokeCacheValid() → rebuildStrokeCache() [O(n) - renders ALL strokes again]

... 60 strokes = 60 full rebuilds = O(n²) total!
```

### Why This Causes PDF Lag
When cache rebuilds, `renderWithZoomCache()` takes longer. Combined with PDF background rendering in DocumentViewport, the frame budget is exceeded, causing visible lag.

---

## Existing Optimizations in VectorLayer

| # | Optimization | Location | Description |
|---|--------------|----------|-------------|
| 1 | **Filled Polygon Rendering** | Lines 196-272 | Single GPU-friendly polygon per stroke instead of N drawLine() calls |
| 2 | **Stroke Pixmap Cache** | Lines 323-459 | Pre-rendered strokes as QPixmap, single blit during paint |
| 3 | **Zoom-Aware Cache** | Part of #2 | Cache built at zoom*dpr resolution for sharp display |
| 4 | **High-DPI Support** | Lines 430-436 | devicePixelRatio handling for retina displays |

---

## The Root Cause

### Location: `addStroke()` (Lines 64-76)
```cpp
void addStroke(const VectorStroke& stroke) {
    m_strokes.append(stroke);
    invalidateStrokeCache();  // ← TRIGGERS FULL REBUILD!
}
```

### Location: `rebuildStrokeCache()` (Lines 429-459)
```cpp
void rebuildStrokeCache(...) const {
    // ...
    for (const auto& stroke : m_strokes) {  // ← O(n) every time!
        renderStroke(cachePainter, stroke);
    }
}
```

### Location: `renderWithZoomCache()` (Lines 386-404)
```cpp
void renderWithZoomCache(...) {
    ensureStrokeCacheValid(size, zoom, dpr);  // ← May trigger O(n) rebuild!
    painter.drawPixmap(0, 0, m_strokeCache);
}
```

---

## Solution: Delayed/Batched Caching

### Core Idea
1. **Don't invalidate cache on stroke addition** - just append
2. **Track how many strokes are cached** - `m_cachedStrokeCount`
3. **Render in two parts:**
   - Blit cache (strokes 0 to cachedStrokeCount-1)
   - Render uncached strokes directly (cachedStrokeCount to end)
4. **Periodically flush to cache** - timer or threshold

### New Behavior (Fast)
```
Stroke 1 complete → addStroke() → just append [O(1)]
Paint → blit cache + render 1 uncached stroke directly [O(1)]

Stroke 2 complete → addStroke() → just append [O(1)]
Paint → blit cache + render 2 uncached strokes directly [O(2)]

... 60 strokes = 60 appends + render 60 directly = O(n) total!

Timer fires (5 sec) → flushStrokeCache() → rebuild with all 60 [O(n) once]
```

---

## Implementation Plan

### Phase 1: Modify VectorLayer

#### 1.1 Add new member variables
```cpp
// In private section (after line 418):
mutable int m_cachedStrokeCount = 0;  ///< How many strokes are in the cache
```

#### 1.2 Add threshold constant
```cpp
// After line 33 or in public section:
static constexpr int CACHE_FLUSH_THRESHOLD = 20;  ///< Suggest flush when this many uncached
```

#### 1.3 Modify `addStroke()` - DON'T invalidate
```cpp
void addStroke(const VectorStroke& stroke) {
    m_strokes.append(stroke);
    // DON'T invalidate cache - stroke will be rendered directly
    // Cache will be updated on next flushStrokeCache() call
}
```

#### 1.4 Keep `removeStroke()` and `clear()` invalidating
These MUST invalidate because strokes are removed, not added.
```cpp
bool removeStroke(const QString& strokeId) {
    // ... existing code ...
    invalidateStrokeCache();  // ← KEEP THIS
    m_cachedStrokeCount = 0;  // ← ADD THIS (force full rebuild)
}

void clear() { 
    m_strokes.clear(); 
    invalidateStrokeCache();  // ← KEEP THIS
    m_cachedStrokeCount = 0;  // ← ADD THIS
}
```

#### 1.5 Add helper methods
```cpp
// Public interface:
int uncachedStrokeCount() const { 
    return m_strokes.size() - m_cachedStrokeCount; 
}

bool needsCacheFlush() const { 
    return uncachedStrokeCount() >= CACHE_FLUSH_THRESHOLD; 
}

void flushStrokeCache(const QSizeF& size, qreal zoom, qreal dpr) {
    rebuildStrokeCache(size, zoom, dpr);
    m_cachedStrokeCount = m_strokes.size();
}
```

#### 1.6 Modify `renderWithZoomCache()` - render in two parts
```cpp
void renderWithZoomCache(QPainter& painter, const QSizeF& size, qreal zoom, qreal dpr) {
    if (!visible || m_strokes.isEmpty()) {
        return;
    }
    
    int totalStrokes = m_strokes.size();
    int uncachedCount = totalStrokes - m_cachedStrokeCount;
    
    // 1. Ensure cache is valid for the CACHED strokes only
    //    (Don't rebuild if only new strokes were added)
    if (m_strokeCacheDirty || /* zoom/size changed */) {
        // Full rebuild needed
        rebuildStrokeCache(size, zoom, dpr);
        m_cachedStrokeCount = totalStrokes;  // All strokes now cached
    } else if (m_cachedStrokeCount > 0) {
        // Check if zoom/size changed
        QSize physicalSize(...);
        if (m_strokeCache.size() != physicalSize || 
            !qFuzzyCompare(m_cacheZoom, zoom) || 
            !qFuzzyCompare(m_cacheDpr, dpr)) {
            // Zoom changed - rebuild with cached strokes only
            rebuildStrokeCachePartial(size, zoom, dpr, m_cachedStrokeCount);
        }
    }
    
    // 2. Blit the cache (if we have cached strokes)
    if (m_cachedStrokeCount > 0 && !m_strokeCache.isNull()) {
        painter.drawPixmap(0, 0, m_strokeCache);
    }
    
    // 3. Render uncached strokes directly (fast for small numbers)
    if (uncachedCount > 0) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int i = m_cachedStrokeCount; i < totalStrokes; ++i) {
            renderStroke(painter, m_strokes[i]);
        }
    }
}
```

#### 1.7 Add partial rebuild method
```cpp
void rebuildStrokeCachePartial(const QSizeF& size, qreal zoom, qreal dpr, int strokeCount) const {
    // Same as rebuildStrokeCache but only renders first strokeCount strokes
    // ...
    for (int i = 0; i < strokeCount; ++i) {
        renderStroke(cachePainter, m_strokes[i]);
    }
    m_cachedStrokeCount = strokeCount;
}
```

### Phase 2: Modify DocumentViewport

#### 2.1 Add cache flush timer
```cpp
// In DocumentViewport.h - add member:
QTimer m_cacheFlushTimer;

// In constructor:
m_cacheFlushTimer.setInterval(5000);  // 5 seconds
m_cacheFlushTimer.setSingleShot(false);
connect(&m_cacheFlushTimer, &QTimer::timeout, 
        this, &DocumentViewport::flushLayerCaches);
m_cacheFlushTimer.start();
```

#### 2.2 Add flush method
```cpp
void DocumentViewport::flushLayerCaches() {
    if (!m_document || !m_isVisible) return;  // Don't flush if hidden
    
    qreal dpr = devicePixelRatioF();
    bool anyFlushed = false;
    
    for (int pageIdx : visiblePages()) {
        Page* page = m_document->page(pageIdx);
        if (!page) continue;
        
        for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
            VectorLayer* layer = page->layer(layerIdx);
            if (layer && layer->needsCacheFlush()) {
                layer->flushStrokeCache(page->size, m_zoomLevel, dpr);
                anyFlushed = true;
            }
        }
    }
    
    if (anyFlushed) {
        update();  // Repaint with newly cached strokes
    }
}
```

#### 2.3 Also flush on specific events
```cpp
// In finishStroke() - after adding stroke to layer:
// Check if too many uncached strokes (immediate flush for heavy users)
VectorLayer* layer = page->activeLayer();
if (layer && layer->uncachedStrokeCount() > CACHE_FLUSH_THRESHOLD * 2) {
    layer->flushStrokeCache(page->size, m_zoomLevel, devicePixelRatioF());
}
```

### Phase 3: Handle Edge Cases

#### 3.1 Zoom/Pan changes
When zoom changes, the cache needs rebuilding at new resolution.
```cpp
void setZoomLevel(qreal zoom) {
    // ... existing code ...
    // Note: renderWithZoomCache() will detect zoom mismatch and rebuild
    // Only cached strokes will be rebuilt, uncached will be rendered directly
}
```

#### 3.2 Layer visibility changes
```cpp
// When layer becomes visible, may need to rebuild cache
// (handled by existing invalidateStrokeCache logic)
```

#### 3.3 Document load
After loading from JSON, strokes exist but no cache:
```cpp
// In VectorLayer::fromJson():
// m_cachedStrokeCount stays 0
// m_strokeCacheDirty stays true
// First render will build cache with all loaded strokes
```

---

## Testing Checklist

- [ ] Rapid-fire 60 strokes in 10 seconds - should be smooth
- [ ] Wait 5 seconds after drawing - cache should flush (check via debug log)
- [ ] Draw on PDF page - should be as smooth as non-PDF page
- [ ] Zoom in/out - strokes should remain sharp
- [ ] Undo/redo - should work correctly
- [ ] Save/load - strokes should persist correctly
- [ ] Switch layers - each layer has independent cache
- [ ] Switch pages - cache per layer per page

---

## Performance Expectations

### Before (O(n²))
```
60 rapid strokes:
  60 × rebuildStrokeCache() with increasing stroke counts
  = 1 + 2 + 3 + ... + 60 = 1830 stroke renders
  + 60 × PDF background operations
  = LAGGY
```

### After (O(n))
```
60 rapid strokes:
  60 × render 1-20 strokes directly (uncached)
  + 1 × rebuildStrokeCache() at the end
  = ~60-1200 stroke renders (depends on threshold)
  + PDF only touched on full repaints
  = SMOOTH
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `source/layers/VectorLayer.h` | Add delayed caching logic |
| `source/core/DocumentViewport.h` | Add cache flush timer |
| `source/core/DocumentViewport.cpp` | Implement flush logic |

---

## Success Criteria

1. **Performance:** Rapid strokes on PDF pages are as smooth as non-PDF
2. **Correctness:** All strokes render correctly at all times
3. **Responsiveness:** No visible pause during cache flush
4. **Memory:** No increase in memory usage

---

*Subplan for delayed stroke caching optimization in SpeedyNote*

