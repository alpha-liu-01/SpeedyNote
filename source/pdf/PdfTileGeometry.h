#pragma once

// ============================================================================
// PdfTileGeometry - region math for high-DPI ("tiled") PDF background rendering
// ============================================================================
// Why this exists
// ---------------
// DocumentViewport::effectivePdfDpi() caps whole-page PDF rasterisation at
// 300 DPI. Beyond that zoom level the on-screen image is a 300 DPI bitmap
// stretched to the screen, so any PDF -- including one whose source detail
// goes to 1000+ DPI -- turns soft as soon as 96 * zoom * dpr exceeds the cap.
//
// Raising the cap is not an option: a 1024x1366 pt page is already ~24 MP
// (~97 MB) at 300 DPI, and MuPdfProvider rejects whole-page renders above
// 64 MP, so the page would go blank.
//
// The fix is to rasterise only the region the user can actually see, at the
// DPI the screen really needs. Peak memory then follows the window size
// instead of the page size, and zoom is no longer limited by the cap.
//
// This header holds that region math with no Qt and no MuPDF dependency, so it
// can be unit tested on its own (see PdfTileGeometryTests.h).
//
// Coordinate systems
// ------------------
// * page units  : SpeedyNote page space, 96 DPI, Y down, origin at the page's
//                 top-left corner. This is what DocumentViewport paints in.
// * PDF points  : 72 DPI, Y down, origin at the page's top-left corner. This is
//                 the convention PdfProvider::renderPageRegionToImage() takes
//                 (MuPDF's page space, whose bounds are offsets from that
//                 corner).
// * device px   : physical screen pixels = page units * zoom * devicePixelRatio.
//
// At the DPI we ask for, one page unit maps to exactly one device pixel, which
// is what keeps a tile pixel-exact instead of slightly resampled.
// ============================================================================

#include <algorithm>
#include <cmath>

namespace PdfTileGeometry {

/// A page region to rasterise, expressed in both coordinate systems.
struct TileRegion {
    bool valid = false;

    /// Region in PDF points from the page's top-left corner (Y down).
    double xPt = 0.0;
    double yPt = 0.0;
    double wPt = 0.0;
    double hPt = 0.0;

    /// The same region in page units (96 DPI, Y down).
    double xUnits = 0.0;
    double yUnits = 0.0;
    double wUnits = 0.0;
    double hUnits = 0.0;

    /// Pixel size the region must be rendered at, for the given DPI.
    double pixelWidth(double dpi) const { return wUnits * dpi / 96.0; }
    double pixelHeight(double dpi) const { return hUnits * dpi / 96.0; }
};

/// Tuning knobs for computeRegion().
struct Params {
    /// Screen pixels per page unit (zoom * devicePixelRatio).
    double deviceScale = 1.0;
    /// DPI the region will be rasterised at.
    double tileDpi = 96.0;
    /// Grid the region is snapped to, in page units. Snapping lets a small pan
    /// reuse the cached tile instead of forcing a re-render.
    double quantumUnits = 32.0;
    /// Extra margin kept around the viewport, in device pixels, so a modest pan
    /// stays inside the already-rendered tile.
    double marginPx = 160.0;
};

/**
 * @brief Region of a page that must be rasterised to fill the viewport.
 *
 * @param pageW,pageH  Page size in page units.
 * @param viewX,viewY  Viewport origin in page coordinates.
 * @param viewW,viewH  Viewport size in page units.
 * @param p            Tuning knobs; see Params.
 * @return The region to render, or an invalid region when the page is not
 *         visible / the inputs are degenerate.
 *
 * The region is the visible part of the page expanded by p.marginPx pixels,
 * snapped outwards to the p.quantumUnits grid, and clamped to the page. The
 * result is therefore deterministic for a given viewport, which is what lets
 * the cache key on it.
 */
inline TileRegion computeRegion(double pageW, double pageH,
                                double viewX, double viewY,
                                double viewW, double viewH,
                                const Params& p)
{
    TileRegion r;

    if (!(pageW > 0.0) || !(pageH > 0.0) || !(viewW > 0.0) || !(viewH > 0.0))
        return r;
    if (!(p.deviceScale > 0.0) || !(p.tileDpi > 0.0) || !(p.quantumUnits > 0.0))
        return r;

    // 1. Visible part of the page, in page units.
    double x0 = std::max(0.0, viewX);
    double y0 = std::max(0.0, viewY);
    double x1 = std::min(pageW, viewX + viewW);
    double y1 = std::min(pageH, viewY + viewH);
    if (!(x1 > x0) || !(y1 > y0))
        return r;                       // page off-screen

    // 2. Margin, converted from device pixels to page units.
    const double marginUnits = p.marginPx / p.deviceScale;
    x0 -= marginUnits;
    y0 -= marginUnits;
    x1 += marginUnits;
    y1 += marginUnits;

    // 3. Snap outwards to the grid, then clamp back to the page.
    x0 = std::max(0.0, std::floor(x0 / p.quantumUnits) * p.quantumUnits);
    y0 = std::max(0.0, std::floor(y0 / p.quantumUnits) * p.quantumUnits);
    x1 = std::min(pageW, std::ceil(x1 / p.quantumUnits) * p.quantumUnits);
    y1 = std::min(pageH, std::ceil(y1 / p.quantumUnits) * p.quantumUnits);
    if (!(x1 > x0) || !(y1 > y0))
        return r;

    r.xUnits = x0;
    r.yUnits = y0;
    r.wUnits = x1 - x0;
    r.hUnits = y1 - y0;

    constexpr double kUnitsToPoints = 72.0 / 96.0;
    r.xPt = x0 * kUnitsToPoints;
    r.yPt = y0 * kUnitsToPoints;
    r.wPt = r.wUnits * kUnitsToPoints;
    r.hPt = r.hUnits * kUnitsToPoints;

    r.valid = true;
    return r;
}

/**
 * @brief Does an already-rendered region fully cover the given viewport?
 *
 * Snapping alone cannot make a small pan reuse a tile: the snapped key moves a
 * whole grid step as soon as the viewport crosses a grid line. So the cache
 * lookup asks this instead -- a tile is good enough while it still covers what
 * the user can see, no matter which grid cell the viewport currently lands in.
 */
inline bool regionCovers(const TileRegion& r,
                         double viewX, double viewY, double viewW, double viewH)
{
    if (!r.valid)
        return false;
    constexpr double kEps = 1e-6;
    return viewX >= r.xUnits - kEps && viewY >= r.yUnits - kEps &&
           viewX + viewW <= r.xUnits + r.wUnits + kEps &&
           viewY + viewH <= r.yUnits + r.hUnits + kEps;
}

/// Overlap test between a rendered region and a page-space rectangle.
inline bool regionIntersects(const TileRegion& r,
                             double x, double y, double w, double h)
{
    if (!r.valid)
        return false;
    return x < r.xUnits + r.wUnits && x + w > r.xUnits &&
           y < r.yUnits + r.hUnits && y + h > r.yUnits;
}

/**
 * @brief Tile DPI to use for a given on-screen demand.
 *
 * The requested DPI is what the screen actually needs (96 * zoom * dpr), so a
 * tile is rendered 1:1 with the display. It is capped only to keep a
 * pathologically large window (or an extreme zoom) from allocating an absurd
 * pixmap.
 *
 * @param requiredDpi Screen demand in DPI.
 * @param maxDpi      Hard ceiling for a single tile.
 * @param wUnits,hUnits Region size in page units (for the pixel budget).
 * @param maxPixels   Pixel budget for one tile.
 */
inline double effectiveTileDpi(double requiredDpi, double maxDpi,
                               double wUnits, double hUnits, double maxPixels)
{
    if (!(requiredDpi > 0.0))
        return 0.0;

    double dpi = std::min(requiredDpi, maxDpi);

    // Shrink further if the region would blow the pixel budget (only reachable
    // with a very large window at a very high zoom).
    if (maxPixels > 0.0 && wUnits > 0.0 && hUnits > 0.0) {
        const double areaUnits = wUnits * hUnits;
        const double areaPx = areaUnits * (dpi / 96.0) * (dpi / 96.0);
        if (areaPx > maxPixels) {
            const double maxDpiForBudget =
                std::sqrt(maxPixels * 96.0 * 96.0 / areaUnits);
            dpi = std::min(dpi, maxDpiForBudget);
        }
    }

    return std::max(1.0, dpi);
}

/**
 * @brief Clip whole-page pixel rectangles to a tile and shift them to tile-local
 *        coordinates.
 *
 * Used to keep dark-mode image masking working for a region render: the backend
 * reports raster-image rectangles relative to the whole page, but
 * invertImageLightness() needs them relative to the tile.
 */
struct RectD {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

inline bool clipRectsToTile(const RectD* in, int count,
                            const TileRegion& region, double dpi,
                            RectD* out, int* outCount)
{
    if (!out || !outCount || !region.valid)
        return false;

    const double originX = region.xUnits * dpi / 96.0;
    const double originY = region.yUnits * dpi / 96.0;
    const double tileW = region.pixelWidth(dpi);
    const double tileH = region.pixelHeight(dpi);

    int n = 0;
    for (int i = 0; i < count; ++i) {
        const double x0 = std::max(0.0, in[i].x - originX);
        const double y0 = std::max(0.0, in[i].y - originY);
        const double x1 = std::min(tileW, in[i].x + in[i].w - originX);
        const double y1 = std::min(tileH, in[i].y + in[i].h - originY);
        if (!(x1 > x0) || !(y1 > y0))
            continue;                   // fully outside this tile
        out[n].x = x0;
        out[n].y = y0;
        out[n].w = x1 - x0;
        out[n].h = y1 - y0;
        ++n;
    }

    *outCount = n;
    return true;
}

} // namespace PdfTileGeometry
