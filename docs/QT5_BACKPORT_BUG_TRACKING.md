# SpeedyNote Qt5 Backport - Bug Tracking

## Build Information

| Property | Value |
|----------|-------|
| Build Type | Release / Debug |
| Qt Version | 5.15.2 |
| Target | Windows 32-bit |
| Toolchain | MinGW 8.1 (i686), standalone Qt install |
| PDF Backend | MuPDF 1.24.10 (compiled from source, full bundled HarfBuzz) |

## Bug Summary

| Bug | Description | Status |
|-----|-------------|--------|
| **BUG-Q001** | Stroke cache renders at wrong scale/position below zoom threshold | ✅ Fixed |
| **BUG-Q002** | Inconsistent font character spacing on Windows | ✅ Fixed |
| **BUG-Q003** | SEGV in MuPDF JPEG2000 decoder during concurrent PDF renders | ✅ Fixed |

---

## Fixed Bugs

### BUG-Q001: Stroke Cache Misrender Below Zoom Threshold

**Status:** ✅ Fixed
**Priority:** Critical
**Category:** Rendering / Stroke Cache
**Affects:** Qt5 only (Qt6 handles sub-1.0 `devicePixelRatio` correctly)

**Symptom:**
At zoom levels where `zoom * devicePixelRatio < 1.0` (e.g., below 100% at 1x Windows scaling, below 50% at 2x scaling), two rendering defects appeared:

1. **Page/tile size stops shrinking** — the stroke cache no longer scales down with zoom.
2. **Strokes jump on pen-up** — after lifting the stylus, the committed stroke appears bigger and displaced toward the bottom-right of the page/tile, relative to the live preview during drawing.

After fixing the initial scale issue, a **secondary inconsistency** appeared: strokes became randomly too big or too small after zoom actions, with no predictable pattern.

**Root Cause (two bugs):**

**Bug A — Qt5 `QPixmap::setDevicePixelRatio()` breaks below 1.0:**

The zoom-aware stroke cache (`VectorLayer`) creates a `QPixmap` at `pageSize * zoom * dpr` physical pixels and sets its `devicePixelRatio` to `zoom * dpr`. Qt uses this DPR to map between the pixmap's physical pixel grid and the logical coordinate system the painter sees.

In Qt6, sub-1.0 DPR values work correctly: a 500-pixel pixmap with DPR=0.5 presents a 1000-unit logical canvas. In Qt5, this mapping breaks — the internal coordinate translation produces incorrect results, causing strokes to render at the wrong scale and position.

**Bug B — Stale state in `applyCachePainterScale()`:**

The initial fix for Bug A was to clamp DPR to `max(1.0, rawScale)` and compensate with a manual `QPainter::scale()` transform via `applyCachePainterScale()`. However, this helper read the scale from member variables (`m_cacheZoom`, `m_cacheDpr`, `m_cacheDivisor`) which store the **previous** cache build's parameters. In `rebuildStrokeCache()`, these members are updated **after** the painter finishes, so the scale applied during rendering came from the wrong zoom level:

| Transition | Old rawScale | Applied | Needed | Result |
|---|---|---|---|---|
| zoom 2.0 → 0.5 | 2.0 (no-op) | none | scale(0.5) | strokes too big |
| zoom 0.5 → 0.3 | 0.5 | scale(0.5) | scale(0.3) | wrong size |
| zoom 0.3 → 2.0 | 0.3 | scale(0.3) | none | strokes too small |
| zoom 0.5 → 0.5 | 0.5 | scale(0.5) | scale(0.5) | correct |

This produced the "random after zoom" inconsistency: the result depended on the previous zoom level, not the current one.

**Fix:**

All changes are in `source/layers/VectorLayer.h`, guarded with `#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)`. Qt6 behavior and performance are completely unchanged.

1. **Cache DPR clamping** (`rebuildStrokeCache`): When `zoom * dpr / divisor < 1.0`, set the pixmap's `devicePixelRatio` to `max(1.0, rawScale)` instead of the raw value. The physical pixel count stays at the correct zoomed-out resolution (same as Qt6).

2. **Manual painter scale** (`applyCachePainterScale`): When DPR is clamped, apply `QPainter::scale(rawScale, rawScale)` to the cache painter so strokes rasterize at the correct target resolution with proper anti-aliasing. This replaces the DPR's role in coordinate mapping. The scale value is passed as a parameter (not read from members) to avoid stale state.

3. **Explicit target rect** (`renderWithZoomCache`): When compositing the cache, use `drawPixmap(QRectF(0,0,pageW,pageH), cache, QRectF(0,0,cacheW,cacheH))` instead of `drawPixmap(0,0,cache)`. This maps the cache to the page rect explicitly, bypassing Qt5's broken DPR-based logical size calculation. The math produces a 1:1 pixel mapping between cache and screen at any zoom level.

4. **Consistent incremental updates** (`appendPendingStrokes`, `patchCacheAfterRemoval`): These functions also apply the painter scale when painting to an existing cache. Here the member variables are valid (cache was already built), so computing `rawScale` from them is safe.

**Why the initial "clamp physical size" approach caused aliasing:**

The first attempted fix clamped both the DPR and the physical size to `max(1.0, ...)`, which meant the cache was built at page resolution (1:1) instead of the zoomed-out resolution. Strokes rasterized at full resolution and the viewport painter downscaled the finished pixmap — but `QPainter::drawPixmap` downscaling doesn't apply anti-aliasing the way stroke rasterization does, producing jagged edges at low zoom.

The final fix keeps the cache at the correct pixel count (`pageSize * zoom * dpr`) and only clamps the DPR metadata, using a painter `scale()` transform instead. Strokes rasterize directly at the target resolution with QPainter's anti-aliasing active during the rasterization pass.

**Affected files:**
- `source/layers/VectorLayer.h` — `rebuildStrokeCache()`, `applyCachePainterScale()`, `renderWithZoomCache()`, `appendPendingStrokes()`, `patchCacheAfterRemoval()`

**Verification:**
- Draw strokes at various zoom levels (10%–500%) on both 1x and 2x Windows scaling
- Strokes should not jump, resize, or shift on pen-up at any zoom
- Zoom in/out rapidly and verify strokes remain consistent
- Erase strokes at low zoom and verify the eraser region patches correctly
- Compare visual quality at low zoom with the Qt6 build (should match)

---

### BUG-Q002: Inconsistent Font Character Spacing on Windows

**Status:** ✅ Fixed
**Priority:** Medium
**Category:** UI / Font Rendering
**Affects:** Qt5 and Qt6 (also observed on Windows 10 1809 with Qt6/DirectWrite)

**Symptom:**
UI text rendered with Segoe UI has visibly inconsistent character spacing — some characters appear too close together while others have noticeably wider gaps. The irregularity is random across the text, not tied to specific character pairs.

**Root Cause:**
Qt5 on Windows uses the **GDI font engine** by default. GDI positions glyphs at integer pixel boundaries (full hinting), but the text layout engine computes fractional advance widths. Each glyph's position is independently rounded to the nearest pixel, so accumulated rounding errors produce inconsistent spacing: some pairs round closer together, others further apart.

The application explicitly set `QFont::PreferFullHinting`, which maximizes this integer-snapping behavior.

Qt6 switched to **DirectWrite** as the default Windows font engine, which supports fractional glyph positioning natively. `PreferFullHinting` on Qt6+DirectWrite doesn't cause the same spacing irregularity because advances are computed at sub-pixel precision.

**Fix:**
In `applyWindowsFonts()` (`source/Main.cpp`), use `QFont::PreferNoHinting` unconditionally to disable integer-pixel glyph snapping.

```cpp
font.setHintingPreference(QFont::PreferNoHinting);
```

`PreferNoHinting` lets glyphs position at fractional pixel coordinates, producing uniform spacing. The visual tradeoff is very slightly softer text at small sizes (same approach used by Chrome and Firefox on Windows).

**Affected files:**
- `source/Main.cpp` — `applyWindowsFonts()`

**Verification:**
- Compare UI text spacing (menus, labels, dialogs) between Qt5 and Qt6 builds
- Text should have uniform character spacing with no irregular gaps
- Test at both 100% and 200% Windows display scaling

---

### BUG-Q003: SEGV in MuPDF JPEG2000 Decoder During Concurrent PDF Renders

**Status:** ✅ Fixed
**Priority:** Critical
**Category:** PDF Rendering / Thread Safety
**Affects:** Qt5/Win32 only (Qt6 uses a prebuilt MuPDF with proper pthread locking)

**Symptom:**
When scrolling through a PDF document quickly (fast enough that no previously rendered pages remain in the viewport), the application crashes with a SIGSEGV in `fz_free()`, called from MuPDF's OpenJPEG allocator hook during JPEG2000 image decoding on a QtConcurrent background thread.

Backtrace:
```
#0  fz_free ()
#1  opj_free ()
#2  opj_thread_pool_submit_job ()
#3  opj_t1_decode_cblks ()
...
#38 MuPdfProvider::renderPageToImage(int, double) const ()
#39 QtConcurrent::RunFunctionTask<QImage>::run() ()
```

**Root Cause:**

MuPDF's JPEG2000 decoder (`source/fitz/load-jpx.c`) routes OpenJPEG's memory allocations through a **plain global variable** `opj_secret` that holds the current `fz_context*`:

```c
static fz_context *opj_secret = NULL;        // global, NOT thread-local

void opj_lock(fz_context *ctx)  { fz_ft_lock(ctx);  opj_secret = ctx;  }
void opj_unlock(fz_context *ctx){ opj_secret = NULL; fz_ft_unlock(ctx); }
void opj_free(void *ptr)        { fz_free(opj_secret, ptr); }  // uses global
```

Access is "protected" by `fz_ft_lock()`/`fz_ft_unlock()`, which call `fz_lock(ctx, FZ_LOCK_FREETYPE)`. However, `fz_lock` dispatches through the `fz_locks_context` provided at context creation. `MuPdfProvider` creates contexts with `fz_new_context(NULL, NULL, ...)` — the `NULL` locks parameter triggers `fz_locks_default`, whose lock/unlock are **empty no-ops** (`source/fitz/memory.c`).

SpeedyNote uses thread-local `MuPdfProvider` instances (one per QtConcurrent worker thread, via `QThreadStorage<ThreadPdfCache>`). Each provider has its own independent `fz_context` with independent no-op locks. When two workers render simultaneously:

1. Thread A: `opj_lock(ctxA)` → no-op lock → `opj_secret = ctxA`
2. Thread B: `opj_lock(ctxB)` → no-op lock → `opj_secret = ctxB` (overwrites A!)
3. Thread A finishes: `opj_unlock(ctxA)` → `opj_secret = NULL`
4. Thread B calls `opj_free(ptr)` → `fz_free(NULL, ptr)` → **SIGSEGV**

The same pattern exists for HarfBuzz (`fz_hb_secret` in `harfbuzz.c`) and FreeType (`ftmemory.user` in `font.c`).

**Why Qt6 is unaffected:**
The Qt6 build uses a prebuilt MuPDF package from MSYS2 (64-bit), which was compiled with `HAVE_PTHREAD=yes`. That build's `fz_ft_lock` acquires a real pthread mutex, serialising access to the global variables. The Qt5/Win32 build compiles MuPDF from source without `HAVE_PTHREAD`, so all internal locking is no-ops.

**Fix:**
Provide a real `fz_locks_context` (backed by static `QMutex[FZ_LOCK_MAX]`) to `fz_new_context()` on Qt5, so that every `MuPdfProvider` — including each thread-local instance — shares the same set of mutexes. This makes MuPDF's own fine-grained locking (`fz_ft_lock`/`fz_ft_unlock`) functional, serialising only the critical sections (JPEG2000 decode, FreeType access, HarfBuzz access, allocation refcounting) while the rest of each render runs in parallel.

```cpp
// Shared lock callbacks (file scope)
static QMutex s_mupdfLocks[FZ_LOCK_MAX];
static void sn_mupdf_lock(void*, int lock)   { s_mupdfLocks[lock].lock();   }
static void sn_mupdf_unlock(void*, int lock) { s_mupdfLocks[lock].unlock(); }
static fz_locks_context s_mupdfLocksCtx = { nullptr, sn_mupdf_lock, sn_mupdf_unlock };

// In constructor
m_ctx = fz_new_context(nullptr, &s_mupdfLocksCtx, FZ_STORE_DEFAULT);
```

This is strictly better than a blanket global mutex around the entire render, which would eliminate all page-level parallelism. With fine-grained locks, most of the render (page loading, rasterisation, pixel copying) proceeds concurrently; only the small sections that touch global state block.

**Affected files:**
- `source/pdf/MuPdfProvider.cpp` — `renderPageToImage()`

**Verification:**
- Open a PDF with JPEG2000 images (or any complex PDF)
- Scroll rapidly through the document until no pages remain in the viewport
- Repeat 10–20 times — no crash should occur
- Compare render performance with Qt6 to confirm no visible degradation
