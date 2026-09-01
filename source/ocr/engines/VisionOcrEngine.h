#pragma once

#ifdef SPEEDYNOTE_HAS_VISION_OCR

// ============================================================================
// VisionOcrEngine - Apple Vision OCR backend (macOS)
// ============================================================================
// Part of the OCR Phase 4C raster pipeline.
//
// macOS has no public stroke-based handwriting API, so this backend recognizes
// from images: the shared RasterOcrEngine base renders the user's clean vector
// strokes to a normalized monochrome strip, and this class runs Apple Vision's
// VNRecognizeTextRequest on that strip. Because we control the rasterization,
// Vision's per-character geometry (boundingBoxForRange:) maps back to canvas
// coordinates exactly via the base's RasterTransform.
//
// Only recognizeImage()/availability/identity are platform-specific; the
// buffer, grouping, line-signature cache, and WordSegment assembly all live in
// RasterOcrEngine. recognizeImage() is implemented in VisionOcrEngine_macos.mm.
// ============================================================================

#include "RasterOcrEngine.h"

#include <QStringList>

class VisionOcrEngine : public RasterOcrEngine {
public:
    VisionOcrEngine() = default;

    QString engineId() const override { return QStringLiteral("apple_vision"); }
    bool isAvailable() const override;
    QStringList availableLanguages() const override;

    // VNRecognizedText reports a real per-observation score.
    bool providesConfidence() const override { return true; }

protected:
    ImageRecognition recognizeImage(const QImage& strip,
                                    const QString& languageTag) override;

    // Vision recognizes small strips well, but a slightly taller target keeps
    // anti-aliased ink crisp for the .accurate path. Tunable per QA Q4.1.
    int targetStripHeightPx() const override { return 64; }

private:
    mutable QStringList m_cachedLanguages;
};

#endif // SPEEDYNOTE_HAS_VISION_OCR
