#pragma once

// ============================================================================
// RasterOcrEngine - Shared base for image/raster-based OCR backends
// ============================================================================
// Part of the OCR Phase 4 (macOS + Linux) raster pipeline.
//
// Platforms without a native vector/stroke recognizer (macOS Vision,
// Linux PaddleOCR/ONNX) recognize from images instead of stroke sequences.
// This base class owns everything platform-agnostic:
//   - the stroke buffer (mirrors MlKitOcrEngine)
//   - line grouping (reuses OcrLineGrouper, like the ML Kit path)
//   - normalized rasterization (OcrStrokeRasterizer)
//   - a line-signature cache that gives raster engines incremental-like
//     behavior: only changed lines are re-rendered/re-recognized (QA Q2.x)
//   - per-character geometry mapped back to canvas space and assembled into
//     Latin-word / CJK-glyph WordSegments (QA Q3.2)
//
// Concrete backends implement only recognizeImage() plus the OcrEngine
// availability/identity hooks (engineId/isAvailable/availableLanguages).
// ============================================================================

#include "../OcrEngine.h"
#include "../../strokes/VectorStroke.h"

#include <QHash>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

struct RasterTransform; // OcrStrokeRasterizer.h
struct StrokeLineGroup; // OcrLineGrouper.h

class RasterOcrEngine : public OcrEngine {
public:
    RasterOcrEngine();
    ~RasterOcrEngine() override;

    RasterOcrEngine(const RasterOcrEngine&) = delete;
    RasterOcrEngine& operator=(const RasterOcrEngine&) = delete;

    // engineId(), isAvailable(), availableLanguages() remain pure-virtual:
    // they are backend-specific, so this class stays abstract.

    void setLanguage(const QString& recognizerName) override;
    QString language() const override;

    /**
     * @brief Resolve an auto-detect locale to a tag the backend actually exposes.
     *
     * Used by setLanguage() when the UI passes the "auto" / empty sentinel. Pure
     * (no QLocale access) so it is deterministically unit-testable. The system
     * locale tag (e.g. "zh-CA") rarely equals a backend tag (Vision exposes only
     * script-tagged Chinese like "zh-Hans"/"zh-Hant"), so a naive exact match
     * fails and the engine wrongly falls back to English. This does a tiered
     * match instead.
     *
     * @param langSubtag Primary language subtag, e.g. "zh", "en" (case-insensitive).
     * @param script     Han script hint "Hans"/"Hant" or empty (from QLocale::script()).
     * @param bcp47Name  QLocale::bcp47Name(), e.g. "zh-Hans" (may already carry script).
     * @param localeName QLocale::name(), e.g. "zh_CA" (region used to infer script).
     * @param available  Backend's availableLanguages().
     * @return A member of @p available, or empty when nothing matches (caller then
     *         leaves the tag empty so the backend uses its own default).
     */
    static QString resolveAutoLanguage(const QString& langSubtag,
                                       const QString& script,
                                       const QString& bcp47Name,
                                       const QString& localeName,
                                       const QStringList& available);

    void addStrokes(const QVector<VectorStroke>& strokes) override;
    void removeStrokes(const QVector<QString>& strokeIds) override;
    void clearStrokes() override;
    QVector<Result> analyze() override;

protected:
    /// Result of an image recognition pass, in image-pixel space.
    struct ImageRecognition {
        QString text;
        /// Optional per-character boxes in image-pixel space. When populated,
        /// charBoxesImage.size() == text.length(); otherwise empty.
        QVector<QRectF> charBoxesImage;
        /// Recognition score in 0..1, on whatever scale the backend uses.
        /// 1.0 means "no score available"; providesConfidence() keeps such
        /// engines out of the UI anyway.
        float confidence = 1.0f;
    };

    /**
     * @brief Recognize one normalized strip. Implemented by each backend.
     * @param strip       Normalized monochrome strip (from OcrStrokeRasterizer).
     * @param languageTag Current language tag.
     */
    virtual ImageRecognition recognizeImage(const QImage& strip,
                                            const QString& languageTag) = 0;

    /// Target ink height (px) fed to the rasterizer; backends may tune this.
    virtual int targetStripHeightPx() const { return 48; }

    QString m_languageTag;

private:
    Result buildResult(const StrokeLineGroup& group,
                       const RasterTransform& transform,
                       const ImageRecognition& rec) const;

    QVector<VectorStroke> m_strokes;
    QHash<QString, int> m_strokeIndexById;

    struct CachedLine {
        OcrEngine::Result result;
    };
    QHash<quint64, CachedLine> m_lineCache; ///< key = line signature
};
