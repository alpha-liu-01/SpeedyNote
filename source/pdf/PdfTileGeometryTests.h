#pragma once

// ============================================================================
// PdfTileGeometryTests - unit tests for the high-DPI PDF tile geometry
// ============================================================================
// Run with: speedynote --test-pdf-tiles
//
// Deliberately Qt-free (the header under test is Qt-free too) so this suite can
// also be compiled standalone on a machine without Qt:
//     g++ -std=c++17 -I source/pdf tools/run_pdf_tile_tests.cpp
// ============================================================================

#include "PdfTileGeometry.h"

#include <cmath>
#include <cstdio>

namespace PdfTileGeometryTests {

inline bool nearly(double a, double b, double eps = 1e-6)
{
    return std::fabs(a - b) <= eps;
}

struct Counter {
    int passed = 0;
    int failed = 0;

    void check(bool ok, const char* what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
            std::printf("    FAIL: %s\n", what);
        }
    }
};

/// Viewport fully inside the page: region must cover it, be snapped, and be
/// clamped to the page.
inline void testRegionCoversViewport(Counter& c)
{
    using namespace PdfTileGeometry;

    const double pageW = 1024.0 * 96.0 / 72.0;   // 1024 pt page in page units
    const double pageH = 1366.0 * 96.0 / 72.0;

    Params p;
    p.deviceScale = 7.0;        // zoom 7 at dpr 1
    p.tileDpi = 96.0 * 7.0;     // screen-accurate
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    const double vx = 400.0, vy = 500.0, vw = 1920.0 / 7.0, vh = 1080.0 / 7.0;
    const TileRegion r = computeRegion(pageW, pageH, vx, vy, vw, vh, p);

    c.check(r.valid, "region is valid for an on-screen viewport");
    c.check(r.xUnits <= vx && r.yUnits <= vy, "region starts at or before the viewport");
    c.check(r.xUnits + r.wUnits >= vx + vw, "region covers the viewport horizontally");
    c.check(r.yUnits + r.hUnits >= vy + vh, "region covers the viewport vertically");
    c.check(nearly(std::fmod(r.xUnits, p.quantumUnits), 0.0), "x is snapped to the quantum grid");
    c.check(nearly(std::fmod(r.yUnits, p.quantumUnits), 0.0), "y is snapped to the quantum grid");    c.check(nearly(std::fmod(r.wUnits, p.quantumUnits), 0.0), "width is a whole number of quanta");
    c.check(r.xUnits >= 0.0 && r.yUnits >= 0.0, "region stays inside the page (min)");
    c.check(r.xUnits + r.wUnits <= pageW + 1e-9 && r.yUnits + r.hUnits <= pageH + 1e-9,
            "region stays inside the page (max)");

    // PDF points are 72/96 of page units, measured from the page top-left.
    c.check(nearly(r.xPt, r.xUnits * 0.75), "xPt converts page units with 72/96");
    c.check(nearly(r.hPt, r.hUnits * 0.75), "hPt converts page units with 72/96");
}

/// The whole point of the exercise: at the requested DPI one page unit is one
/// device pixel, so the tile lands 1:1 on screen.
inline void testPixelExactAtScreenDpi(Counter& c)
{
    using namespace PdfTileGeometry;

    const double zoom = 7.3, dpr = 1.5;
    const double deviceScale = zoom * dpr;
    const double pageW = 1000.0, pageH = 1400.0;

    Params p;
    p.deviceScale = deviceScale;
    p.tileDpi = 96.0 * deviceScale;    // requiredPdfDpi()
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    const TileRegion r = computeRegion(pageW, pageH, 100.0, 200.0, 1200.0, 800.0, p);
    c.check(r.valid, "region valid");

    // Width in device pixels, two ways: from the screen scale and from the tile DPI.
    const double screenPx = r.wUnits * deviceScale;
    const double tilePx = r.pixelWidth(p.tileDpi);
    c.check(nearly(screenPx, tilePx, 1e-9), "tile pixel width equals on-screen device width");
    c.check(nearly(r.hUnits * deviceScale, r.pixelHeight(p.tileDpi), 1e-9),
            "tile pixel height equals on-screen device height");
}

/// A modest pan must stay inside the tile already on screen, so no re-render is
/// needed. Snapping cannot promise this on its own (the snapped key moves a
/// whole grid step as soon as the viewport crosses a grid line), which is why
/// the cache asks regionCovers() rather than comparing keys.
inline void testSmallPanReusesRegion(Counter& c)
{
    using namespace PdfTileGeometry;

    Params p;
    p.deviceScale = 6.0;
    p.tileDpi = 96.0 * 6.0;
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    const double vw = 1600.0 / 6.0, vh = 900.0 / 6.0;
    const double vx = 500.0, vy = 500.0;
    const TileRegion a = computeRegion(2000.0, 2000.0, vx, vy, vw, vh, p);
    c.check(a.valid, "region valid");

    // Pan by 100 device pixels == 100/6 page units, inside the 160 px margin.
    const double dx = 100.0 / 6.0;
    c.check(regionCovers(a, vx + dx, vy, vw, vh),
            "a 100px pan is still covered by the cached tile");
    c.check(regionCovers(a, vx, vy + dx, vw, vh),
            "a 100px vertical pan is still covered too");
    c.check(regionCovers(a, vx, vy, vw, vh), "the tile covers the viewport it was made for");

    // A pan past the margin must ask for a new tile.
    // (Not named `far`: that is a legacy empty macro in the Windows SDK headers.)
    const double beyondMargin = 600.0 / 6.0;
    c.check(!regionCovers(a, vx + beyondMargin, vy, vw, vh),
            "a 600px pan falls outside the cached tile and needs a new one");

    // Overlap helper used by the paint path.
    c.check(regionIntersects(a, vx, vy, vw, vh), "region intersects its own viewport");
    c.check(!regionIntersects(a, 0.0, 0.0, 10.0, 10.0), "region does not intersect a far-away rect");
}

/// Same viewport state must always produce the same region (cache identity).
inline void testDeterminism(Counter& c)
{
    using namespace PdfTileGeometry;

    Params p;
    p.deviceScale = 4.25;
    p.tileDpi = 96.0 * 4.25;
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    const TileRegion a = computeRegion(900.0, 1200.0, 33.3, 44.4, 500.0, 300.0, p);
    const TileRegion b = computeRegion(900.0, 1200.0, 33.3, 44.4, 500.0, 300.0, p);
    c.check(a.xUnits == b.xUnits && a.yUnits == b.yUnits &&
            a.wUnits == b.wUnits && a.hUnits == b.hUnits,
            "identical inputs produce identical regions");
}

/// Degenerate inputs must not produce a renderable region.
inline void testInvalidInputs(Counter& c)
{
    using namespace PdfTileGeometry;

    Params p;
    p.deviceScale = 8.0;
    p.tileDpi = 768.0;

    c.check(!computeRegion(0.0, 100.0, 0.0, 0.0, 100.0, 100.0, p).valid, "zero page width is invalid");
    c.check(!computeRegion(100.0, 100.0, 0.0, 0.0, 0.0, 100.0, p).valid, "zero viewport width is invalid");
    c.check(!computeRegion(100.0, 100.0, 0.0, 0.0, 50.0, 50.0,
                           Params{0.0, 96.0, 32.0, 160.0}).valid, "zero device scale is invalid");
    c.check(!computeRegion(100.0, 100.0, 0.0, 0.0, 50.0, 50.0,
                           Params{1.0, 0.0, 32.0, 160.0}).valid, "zero tile DPI is invalid");

    // Page entirely off-screen (viewport below the page's bottom edge).
    c.check(!computeRegion(500.0, 500.0, 0.0, 900.0, 400.0, 300.0, p).valid,
            "off-screen page yields no region");
}

/// Regions must be clamped to the page instead of running past its edges.
inline void testClampedToPage(Counter& c)
{
    using namespace PdfTileGeometry;

    Params p;
    p.deviceScale = 7.0;
    p.tileDpi = 672.0;
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    // Viewport straddling the page's bottom-right corner.
    const TileRegion r = computeRegion(400.0, 600.0, 380.0, 580.0, 100.0, 100.0, p);
    c.check(r.valid, "region valid near the page corner");
    c.check(nearly(r.xUnits + r.wUnits, 400.0), "right edge clamped to the page width");
    c.check(nearly(r.yUnits + r.hUnits, 600.0), "bottom edge clamped to the page height");
    c.check(nearly(r.wPt, r.wUnits * 0.75), "points follow the clamped extent");
}

/// effectiveTileDpi() honours the screen demand, the ceiling and the budget.
inline void testEffectiveTileDpi(Counter& c)
{
    using namespace PdfTileGeometry;

    c.check(nearly(effectiveTileDpi(672.0, 1200.0, 300.0, 200.0, 24e6), 672.0),
            "screen demand below the ceiling is used as-is");
    c.check(nearly(effectiveTileDpi(3000.0, 1200.0, 300.0, 200.0, 24e6), 1200.0),
            "extreme zoom is capped by maxDpi");
    c.check(nearly(effectiveTileDpi(0.0, 1200.0, 300.0, 200.0, 24e6), 0.0),
            "zero demand yields zero");
    // 3000x2000 page units at 1200 DPI would be 62 MP; a 4 MP budget must shrink it.
    const double dpi = effectiveTileDpi(1200.0, 1200.0, 3000.0, 2000.0, 4e6);
    c.check(dpi < 1200.0, "pixel budget shrinks the DPI");
    c.check((3000.0 * dpi / 96.0) * (2000.0 * dpi / 96.0) <= 4e6 * 1.0001,
            "shrunken DPI fits the pixel budget");
}

/// Dark-mode masking rects must be shifted into tile-local space and clipped.
inline void testClipRectsToTile(Counter& c)
{
    using namespace PdfTileGeometry;

    Params p;
    p.deviceScale = 4.0;
    p.tileDpi = 384.0;
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    // Region at (128, 64) page units, 4 px per unit.
    const TileRegion r = computeRegion(2000.0, 2000.0, 128.0, 64.0, 400.0, 300.0, p);
    c.check(r.valid, "region valid");

    // Whole-page pixel rects (dpi = 384 -> 4 px per unit).
    RectD in[3];
    in[0] = {128.0 * 4.0 + 40.0, 64.0 * 4.0 + 40.0, 100.0, 50.0};   // fully inside
    in[1] = {0.0, 0.0, 100.0, 100.0};                               // fully outside
    in[2] = {0.0, 0.0, 128.0 * 4.0 + 60.0, 64.0 * 4.0 + 60.0};      // straddles the tile origin

    RectD out[3];
    int n = 0;
    c.check(clipRectsToTile(in, 3, r, 384.0, out, &n), "clipRectsToTile succeeds");
    c.check(n == 2, "fully-outside rects are dropped");
    c.check(nearly(out[0].x, in[0].x - r.xUnits * 4.0) &&
            nearly(out[0].y, in[0].y - r.yUnits * 4.0), "inside rects are shifted to tile space");

    // Third rect straddles the tile's top-left corner, so it must be clipped to
    // exactly the part inside the tile and pinned to the tile origin.
    const double expectedW = in[2].x + in[2].w - r.xUnits * 4.0;
    const double expectedH = in[2].y + in[2].h - r.yUnits * 4.0;
    c.check(nearly(out[1].x, 0.0) && nearly(out[1].y, 0.0),
            "straddling rects are pinned to the tile origin");
    c.check(nearly(out[1].w, expectedW) && nearly(out[1].h, expectedH),
            "straddling rects are clipped to the tile extent");
}

/// The scenario from the bug report: a 1024x1366 pt page at 700% zoom.
/// The old path rendered the whole page at 300 DPI; the tile must instead be
/// bounded by the window, and far smaller than that page-sized bitmap.
inline void testRegressionHugePageAtHighZoom(Counter& c)
{
    using namespace PdfTileGeometry;

    constexpr double kCap = 300.0;
    const double pageW = 1024.0 * 96.0 / 72.0;    // 1365.33 page units
    const double pageH = 1366.0 * 96.0 / 72.0;    // 1821.33 page units

    Params p;
    p.deviceScale = 7.0;
    p.tileDpi = 96.0 * 7.0;    // 672 DPI -- what the screen needs
    p.quantumUnits = 32.0;
    p.marginPx = 160.0;

    const double vw = 1920.0 / 7.0, vh = 1080.0 / 7.0;
    const TileRegion r = computeRegion(pageW, pageH, 300.0, 400.0, vw, vh, p);
    c.check(r.valid, "region valid");

    const double oldWholePagePx =
        (1024.0 * kCap / 72.0) * (1366.0 * kCap / 72.0);   // 24.3 MP
    const double tilePx = r.pixelWidth(p.tileDpi) * r.pixelHeight(p.tileDpi);

    std::printf("    [info] old whole-page bitmap at 300 DPI: %.1f MP\n", oldWholePagePx / 1e6);
    std::printf("    [info] new tile at %.0f DPI: %.1f MP (%.1f%% of the old one)\n",
                p.tileDpi, tilePx / 1e6, 100.0 * tilePx / oldWholePagePx);

    c.check(tilePx < 8e6, "tile stays within a few megapixels");
    c.check(p.tileDpi > kCap, "tile DPI exceeds the old whole-page cap");
    // The old path could not exceed 300 DPI, i.e. it was 2.24x short at 700%.
    c.check(p.tileDpi / kCap > 2.0, "tile DPI more than doubles the old resolution");
}

inline bool runAllTests()
{
    std::printf("=== PdfTileGeometry tests ===\n");
    Counter c;

    std::printf("  testRegionCoversViewport...\n");
    testRegionCoversViewport(c);
    std::printf("  testPixelExactAtScreenDpi...\n");
    testPixelExactAtScreenDpi(c);
    std::printf("  testSmallPanReusesRegion...\n");
    testSmallPanReusesRegion(c);
    std::printf("  testDeterminism...\n");
    testDeterminism(c);
    std::printf("  testInvalidInputs...\n");
    testInvalidInputs(c);
    std::printf("  testClampedToPage...\n");
    testClampedToPage(c);
    std::printf("  testEffectiveTileDpi...\n");
    testEffectiveTileDpi(c);
    std::printf("  testClipRectsToTile...\n");
    testClipRectsToTile(c);
    std::printf("  testRegressionHugePageAtHighZoom...\n");
    testRegressionHugePageAtHighZoom(c);

    std::printf("=== PdfTileGeometry: %d passed, %d failed ===\n", c.passed, c.failed);
    return c.failed == 0;
}

} // namespace PdfTileGeometryTests
