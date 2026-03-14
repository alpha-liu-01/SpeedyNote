// ============================================================================
// DarkModeUtils - Implementation
// ============================================================================

#include "DarkModeUtils.h"
#include "../compat/qt_compat.h"

#include <algorithm>
#include <cmath>

namespace DarkModeUtils {

// ---------------------------------------------------------------------------
// Fast integer HSL ↔ RGB helpers (avoid QColor per-pixel overhead)
// ---------------------------------------------------------------------------

struct HSL {
    int h;  // 0–359 (degrees), -1 for achromatic
    int s;  // 0–255
    int l;  // 0–255
};

static inline int minOf3(int a, int b, int c) { return std::min({a, b, c}); }
static inline int maxOf3(int a, int b, int c) { return std::max({a, b, c}); }

static HSL rgbToHsl(int r, int g, int b)
{
    int cMax = maxOf3(r, g, b);
    int cMin = minOf3(r, g, b);
    int sum  = cMax + cMin;        // 0–510
    int l    = (sum * 255 + 255) / 510;   // lightness 0–255
    int delta = cMax - cMin;

    if (delta == 0) {
        return { -1, 0, l };      // achromatic
    }

    // Saturation (HSL definition)
    int s;
    if (sum <= 255) {
        s = (delta * 255) / sum;
    } else {
        s = (delta * 255) / (510 - sum);
    }

    // Hue in 0–360
    int h;
    if (cMax == r) {
        h = 60 * (g - b) / delta;
        if (h < 0) h += 360;
    } else if (cMax == g) {
        h = 60 * (b - r) / delta + 120;
    } else {
        h = 60 * (r - g) / delta + 240;
    }

    return { h, s, l };
}

static inline int hslComponent(int p, int q, int t)
{
    if (t < 0)   t += 360;
    if (t >= 360) t -= 360;

    if (t < 60)  return p + (q - p) * t / 60;
    if (t < 180) return q;
    if (t < 240) return p + (q - p) * (240 - t) / 60;
    return p;
}

static void hslToRgb(const HSL& hsl, int& r, int& g, int& b)
{
    if (hsl.h < 0 || hsl.s == 0) {
        r = g = b = hsl.l;
        return;
    }

    int q = (hsl.l < 128)
          ? hsl.l + (hsl.l * hsl.s + 127) / 255
          : hsl.l + hsl.s - (hsl.l * hsl.s + 127) / 255;
    int p = 2 * hsl.l - q;

    r = std::clamp(hslComponent(p, q, hsl.h + 120), 0, 255);
    g = std::clamp(hslComponent(p, q, hsl.h),       0, 255);
    b = std::clamp(hslComponent(p, q, hsl.h - 120), 0, 255);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void invertImageLightness(QImage& image, const QVector<QRect>& imageRegions)
{
    if (image.isNull()) return;

    // Convert to non-premultiplied ARGB32 for correct per-pixel manipulation
    if (image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    const int w = image.width();
    const int h = image.height();

    // Build a per-scanline skip mask so that pixels inside raster-image
    // bounding boxes are left untouched.  For each row we store a sorted
    // list of (x_start, x_end) spans.
    // For pages without images this is empty and costs almost nothing.
    QVector<QVector<std::pair<int,int>>> skipSpans(h);
    for (const QRect& r : imageRegions) {
        int y0 = std::clamp(r.top(),    0, h - 1);
        int y1 = std::clamp(r.bottom(), 0, h - 1);
        int x0 = std::clamp(r.left(),   0, w - 1);
        int x1 = std::clamp(r.right(),  0, w - 1);
        for (int y = y0; y <= y1; ++y) {
            skipSpans[y].append({x0, x1});
        }
    }

    // Pure HSL lightness inversion for non-image pixels.
    // Image regions are skipped entirely (structural masking via MuPDF).
    for (int y = 0; y < h; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
        const auto& spans = skipSpans[y];

        for (int x = 0; x < w; ++x) {
            // Skip pixels inside image regions
            bool inImage = false;
            for (const auto& span : spans) {
                if (x >= span.first && x <= span.second) {
                    inImage = true;
                    x = span.second;
                    break;
                }
            }
            if (inImage) continue;

            QRgb px = scanline[x];
            int a = qAlpha(px);
            if (a == 0) continue;

            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);

            HSL hsl = rgbToHsl(r, g, b);
            hsl.l = 255 - hsl.l;
            hslToRgb(hsl, r, g, b);

            scanline[x] = qRgba(r, g, b, a);
        }
    }
}

QColor invertColorLightness(const QColor& color)
{
    SN_ColorFloat h, s, l, a;
    color.getHslF(&h, &s, &l, &a);
    return QColor::fromHslF(h, s, SN_ColorFloat(1) - l, a);
}

QColor darkenColorForExport(const QColor& color)
{
    SN_ColorFloat h, s, l, a;
    color.getHslF(&h, &s, &l, &a);
    if (l > SN_ColorFloat(0.5)) {
        l = SN_ColorFloat(1) - l;
    }
    return QColor::fromHslF(h, s, l, a);
}

} // namespace DarkModeUtils
