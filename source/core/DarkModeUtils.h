// ============================================================================
// DarkModeUtils - HSL lightness inversion for dark mode rendering
// ============================================================================
// Shared utility for PDF dark mode (canvas) and PDF export stroke darkening.
// Uses HSL lightness inversion which preserves hue and saturation while
// flipping light↔dark, avoiding the crude color shifts of full RGB inversion.
// ============================================================================

#ifndef DARKMODEUTILS_H
#define DARKMODEUTILS_H

#include <QColor>
#include <QImage>

namespace DarkModeUtils {

/**
 * @brief Invert the lightness of every pixel in an image (in-place).
 *
 * For each pixel, the HSL lightness is replaced with (1 - L) while hue
 * and saturation are preserved.  White backgrounds become black, black
 * text becomes white, and colours keep their hue.
 *
 * Alpha channel is preserved unchanged.
 *
 * @param image  Must be Format_ARGB32 or Format_ARGB32_Premultiplied.
 *               Converted automatically if needed.
 */
void invertImageLightness(QImage& image);

/**
 * @brief Invert the lightness of a single colour.
 *
 * HSL: L' = 1.0 - L, hue and saturation unchanged.
 */
QColor invertColorLightness(const QColor& color);

/**
 * @brief Conditionally darken a colour for print-friendly export.
 *
 * Only inverts lightness when L > 0.5 (i.e. light colours become dark).
 * Dark colours (L <= 0.5) are returned unchanged so that existing
 * dark strokes are not lightened.
 */
QColor darkenColorForExport(const QColor& color);

} // namespace DarkModeUtils

#endif // DARKMODEUTILS_H
