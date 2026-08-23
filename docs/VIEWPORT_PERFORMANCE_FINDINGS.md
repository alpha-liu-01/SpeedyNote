# Viewport Rendering Performance: Findings

Results of an investigation into why SpeedyNote's canvas performs far worse on
Android than on Windows and Linux. Written so the conclusions can be trusted
without repeating the work, and so the dead ends are not retried.

Measurements come from the built-in performance HUD (F10, available in release
builds) across a deliberately wide device range, including two machines with the
same SoC running different operating systems.

---

## The headline conclusion

**SpeedyNote on Android is present-bound, not paint-bound.** Rasterization is
10-25% of a pan frame. The remaining 75-90% is Qt's Android presentation path,
which the application cannot reach.

| Device | OS | Paint | Frame | Paint share |
|---|---|---|---|---|
| Galaxy Note 8.0 (2013), Cortex-A9 | Android 12 | 7.5 ms | 45 ms | 17% |
| Galaxy Tab A6, Exynos 7870 | Android | 5 ms | 40 ms | 12% |
| Xperia XZ2 Compact, Snapdragon 845 | Android 15 | 4.1 ms | 16.1 ms | 25% |

The practical consequence: **further optimization of paint code cannot
meaningfully improve Android frame rates.** On the Exynos 7870, reducing paint
from 5 ms to zero would move 25 fps to 28.6 fps. That is the entire remaining
headroom available to rasterization work.

This is not true on desktop, where the same code is bandwidth-bound and paint
optimizations translate directly into frame rate.

---

## The controlled experiment

The most valuable data point was two devices with the same silicon:

| | Xperia XZ2 Compact | Galaxy Book 2 |
|---|---|---|
| SoC | Snapdragon 845 | Snapdragon 850 (same die, +6% clock) |
| RAM | 4 GiB LPDDR4X | 4 GiB LPDDR4X |
| OS | Android 15 | Windows 11 on ARM64 |
| Pan | 38.9 fps, 76 Mpix/s | 90.5 fps, 235 Mpix/s |
| Paint per megapixel | 4.60 ms | 0.886 ms |

Windows was ~5x faster per pixel on identical hardware. That gap turned out to
have two independent causes, one of which we fixed.

---

## What we fixed: alpha channel on blit sources

### The bug

Every full-viewport snapshot used `QWidget::grab()`, which allocates in the
platform's preferred pixmap format. On Android that format carries an alpha
channel because the backing store does. Qt selects its blend function from the
source and destination formats:

- **Alpha-carrying source** → per-pixel `argb32_on_argb32` blend. Hand-written
  SIMD exists for x86 (SSE2) and 32-bit ARM (pixman assembly), but **not for
  aarch64**, where it falls back to a scalar C loop.
- **Alpha-free source** → an unscaled SourceOver blit is provably a copy, and Qt
  does it with `memcpy` per scanline.

The snapshots were fully opaque regardless, so the alpha channel was pure cost.

### Measured, full-viewport blit

| Device | Alpha source | Opaque source | Gain |
|---|---|---|---|
| Snapdragon 845 | 229 Mpix/s | 1698 Mpix/s | 7.4x |
| Exynos 7870 | 110 Mpix/s | 428 Mpix/s | 3.9x |

Destination format sweep on the Exynos 7870, confirming the source format is
what matters rather than the destination:

| Pairing | Cost |
|---|---|
| `ARGB32_PM → ARGB32_PM` | 16.48 ms |
| `RGBA8888_PM → RGBA8888_PM` | 16.53 ms |
| `RGB32 → ARGB32_PM` | 4.25 ms |
| `RGBX8888 → RGBA8888_PM` | 3.88 ms |
| `RGB32 → RGBA8888_PM` | 6.95 ms |

### The fix

`DocumentViewport::grabOpaqueViewport()` renders into a `QImage::Format_RGB32`
image instead of a platform pixmap, otherwise mirroring `grab()`. It replaced
`grab()` at all four snapshot sites, each of which blits its snapshot back on
every frame of an interaction:

- `beginPanGesture()` — touch and wheel panning
- `beginZoomGesture()` — pinch zoom
- `captureSelectionBackground()` — lasso drag
- `captureObjectDragBackground()` — object drag and resize

No-op on Windows, where `QPixmap` is already `RGB32`.

### Why the fix produced no frame-rate change on Android

It worked — live paint dropped to match the opaque pairing cost — but paint was
only 12% of the frame. The saved time became idle waiting behind vsync. The fix
is retained because it removes real CPU work (power, thermals, headroom), and
because it keeps paint out of the way on any device where present cost is lower.

**A fix can be correct, measurable, and still invisible in fps.** Check the
`paint` figure, not the frame rate, when evaluating rasterization changes.

---

## Other fixes that landed

- **O(n²) live stroke rendering.** `renderCurrentStrokeIncremental()` cleared the
  viewport pixmap and re-rendered the entire stroke on every new point. Now
  redraws only the volatile tail plus any older segments crossing that region
  (self-intersections would otherwise leave holes), padded with Catmull-Rom
  context points. This was the single largest improvement of the investigation:
  long strokes went from below 1 fps to a stable 50-60.
- **Edgeless tile walks.** `renderEdgelessMode()` now confines its walk to tiles
  intersecting the dirty rect. `eraseAtEdgeless()` uses the eraser's actual reach
  instead of a fixed 3x3 neighbourhood. `objectAtPoint()` uses
  `maxObjectExtent()` instead of scanning all loaded tiles on every pointer move.
- **Lasso hit-testing.** Bounding-box prefilters before per-point polygon tests,
  and edgeless scans restricted to tiles the lasso reaches.
- **Eraser-lasso cache.** `patchCacheAfterRemovals()` instead of
  `invalidateStrokeCache()`, so a bulk removal repairs its region rather than
  forcing a full rebuild.

---

## Dead ends — do not retry without new information

### Device pixel ratio is not a lever
Verified on a 2048x1536 tablet: DPR 1 made the UI tiny but left canvas
resolution and performance **identical**. Physical pixels are what get
rasterized.

### Rounding pan offset to whole device pixels
Disproven by local probe: Qt's `QRasterPaintEngine` already quantizes non-smooth
`drawPixmap` translations to whole device pixels internally.

### RGB565 / 16-bit colour
Infeasible on Android. Qt's platform plugin hardcodes OpenGL surface creation to
32-bit RGBA8888 and forces raster surfaces to `OpenGLSurface`, ignoring
`QSurfaceFormat` depth requests.

### `QWidget::scroll()` for panning
Structurally blocked on Android. `QRasterBackingStore::scroll()` always returns
false because the window is an `OpenGLSurface`, not a `RasterSurface`, so
`QWidget::scroll()` silently falls back to a full repaint.

### Reducing resolution during pan (the "Squid blur" trick)
Not applicable. Qt rasterizes into a fixed-size backing store; there is no
supported way to rasterize a smaller image and have the platform scale it up.

### Exposed-strip content fill
**Reverted.** Dropped a Z3735F from 60 to ~27 fps on an edgeless canvas full of
strokes. Cause: swapping `m_panOffset` to `targetPan` during band rendering
shifted `visibleRect()` every frame, which moved the stroke cache focus rect,
invalidating and rebuilding the Capped-tier cache per frame. PDF backgrounds were
unaffected because their caches key on page number and DPI, not viewport
position.

*The idea itself was not disproven* — only this implementation. Residual scales
with damaged area (see below), so damage reduction remains the one lever that
touches the dominant cost. A retry would need to pin the stroke cache's focus
rect for the duration of the gesture.

### Qt Canvas Painter (6.11)
Not a drop-in. Designed for GPU-only imperative 2D painting with no CPU backend;
adopting it means rewriting the render path, not configuring it.

### Qt version upgrades
6.7 was abysmal; 6.9 substantially better; 6.10 unusable (OpenGL threading
deadlock).

The Android toolchain has since been moved to Qt 6.11.2 (JDK 21, AGP 9.0.0,
compile/target SDK 36, NDK r27 unchanged). **The on-device result is not yet
recorded.** The prior is no change: nothing in 6.11 is known to touch the
`QRhiBackingStore` present path, which is where 75-90 percent of a pan frame
goes. Judge it on `residual`, not fps, against these 6.9.3 baselines:

| Device | Paint | Frame | Residual |
|--------|-------|-------|----------|
| Sony Xperia XZ2 Compact (SD845) | 4.1 ms | 16.1 ms | 12.0 ms |
| Galaxy Tab A6 (Exynos 7870) | 5 ms | 40 ms | 35 ms |

Also unresolved: the 6.10 deadlock reports (QTBUG-141579 family) extend to 6.11,
and one upstream stack trace is `QRhi::beginFrame` in a QWidgets app, which is
this exact code path. If that reproduces, revert rather than patch qtbase.

---

## Where the remaining time goes

On Android, Qt uses `QRhiBackingStore`: rasterize to a CPU image, upload it to a
GPU texture, draw a textured quad, swap EGL buffers. The GPU is involved in every
frame even though all drawing is CPU raster.

**Residual scales with damaged area, so it is not a fixed toll.** On the Note
8.0, `Partial` frames (small damage) ran at ~52 fps while `Pan` frames (full
damage) ran at 22 fps, on the same device and same presentation path. That points
at the texture upload being the dominant term, which in turn means reducing
damaged area is the only remaining approach with access to the 75-90%.

Corroborating split-screen data from the Note 8.0: single viewport 22 Mpix/s,
split with another app 19, split viewport 14. On Android the smaller viewport
halved fill rate without a frame-rate gain; on Windows and Linux (including an
RK3399) the same split doubled frame rate at roughly constant fill rate.

Untested and cheap, if anyone resumes this: whether the top-level window carries
attributes that force Qt to treat it as possibly translucent
(`WA_TranslucentBackground`, absent `WA_OpaquePaintEvent`). If so, the Android
composite blends a full-window quad every frame that could otherwise be opaque.

---

## Measuring: the performance HUD

Press **F10** (works in release builds; F-keys on a Bluetooth keyboard work on
Android). Kept deliberately, so users can report performance conditions.

```
Pan:     38.9 fps | paint 4.1ms avg, 6.2 p95, 9.8 max
Compose: 18.2 fps | paint 25.3ms avg, 31.0 p95, 44.1 max
Partial: 52.1 fps | paint 1.9ms avg, 2.4 p95, 5.0 max
Fill:    76 Mpix/s | 1.96 MP/frame
Verdict: PRESENT-BOUND - paint 4.1 of 16.1ms, residual 12.0ms
Surface: 1080x1812 phys @2.00dpr | vp 540x906 | 60.0Hz | tier Capped
```

Buckets are separated because they have entirely different costs and mixing them
hides the diagnosis:

- **Pan** — gesture pan and zoom. Blits a cached snapshot; content-independent.
- **Compose** — full re-composite of pages, strokes, PDF backgrounds.
- **Partial** — incremental stroke updates during writing.

Reading it:

- `Verdict` is the first thing to check. PRESENT-BOUND means paint optimizations
  cannot help.
- `residual` is frame time minus paint: backing store flush, texture upload,
  compositing, vsync wait.
- `Fill` normalizes for resolution and is the only fair figure to compare across
  devices with different screens.
- `tier` matters because the stroke cache tier depends on `zoom * DPR`, so a
  high-DPR tablet leaves the cheap cached tier at roughly half the zoom a
  desktop monitor would.

### Interpretation pitfalls learned the hard way

- **Android fps is vsync-quantized.** A frame period between 16.67 and 33.3 ms
  means frames are alternating between one and two vsync intervals. Averages
  like 25.7 ms are a mix, not a steady state, and shaving work below the next
  boundary produces no fps change at all.
- **Windows fps is uncapped**, so it reports genuine throughput. Comparing raw
  fps across the two platforms compares different things.
- **Never benchmark blit behaviour on x86 and extrapolate to ARM.** Qt's SIMD
  coverage differs per format pairing *per architecture*. A desktop probe found
  `ARGB32_PM → ARGB32_PM` to be the *fastest* pairing; on aarch64 it is the
  slowest by 4-7x. This actively misled the investigation.
- **Watch for dead-code elimination in microbenchmarks.** A `memcpy` whose
  destination is never read is deleted at `-O2`, producing an absurd result.
- **Defeat the cache.** Reusing one source and destination buffer measures L3
  bandwidth, not memory bandwidth.
