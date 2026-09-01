#pragma once

#ifdef SPEEDYNOTE_HAS_PADDLE_OCR

// ============================================================================
// PaddleOcrEngine - Linux raster OCR backend (PaddleOCR via ONNX Runtime)
// ============================================================================
// Part of OCR Phase 4B. Subclass of RasterOcrEngine (Phase 4A): the shared base
// owns the stroke buffer, line grouping, line-signature cache, transform
// inversion, and Latin-word / CJK-glyph WordSegment assembly. This class
// implements only the one platform-specific bridge -- recognizeImage() -- by
// running a PP-OCRv5 mobile *recognition* model (detection skipped; the grouper
// already segments lines/cells, QA Q5.2) through ONNX Runtime (CPU EP only,
// QA Q11.3), CTC-decoding the logits to text plus approximate per-character X
// boxes (good X, full-height Y, QA Q11.2).
//
// The recognition models are the RapidAI/RapidOCR pre-converted ONNX exports,
// which embed their character dictionary in the ONNX metadata (key
// "character"), so no separate dict files are needed (see fetch-ocr-models.sh).
//
// ONNX Runtime types are kept out of this header via a PIMPL so the rest of the
// build need not see <onnxruntime_cxx_api.h>.
// ============================================================================

#include "RasterOcrEngine.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

class PaddleOcrEngine : public RasterOcrEngine {
public:
    PaddleOcrEngine();
    ~PaddleOcrEngine() override;

    PaddleOcrEngine(const PaddleOcrEngine&) = delete;
    PaddleOcrEngine& operator=(const PaddleOcrEngine&) = delete;

    QString engineId() const override { return QStringLiteral("paddle_ocr"); }
    bool isAvailable() const override;
    QStringList availableLanguages() const override;
    QStringList downloadedLanguages() const override;

    // The CTC decode yields a real per-line softmax score.
    bool providesConfidence() const override { return true; }

    /// Clears the per-session failed-download cache on a real language change,
    /// then defers to the base (cache invalidation + tag normalization).
    void setLanguage(const QString& recognizerName) override;

protected:
    ImageRecognition recognizeImage(const QImage& strip,
                                    const QString& languageTag) override;

    /// PP-OCRv5 mobile recognition models expect ~48 px input height.
    int targetStripHeightPx() const override { return 48; }

    /// Map a BCP-47-ish language tag to a recognition model file name (by
    /// script). The mapped file may be bundled or downloadable on demand.
    /// Protected so test harnesses can exercise the mapping without I/O.
    static QString modelFileForLanguage(const QString& languageTag);

private:
    struct Model;  ///< Ort::Session + decoded char table + IO names (defined in .cpp)
    struct Impl;   ///< shared Ort::Env (defined in .cpp)

    /// Resolve + lazily load (and cache) the recognition model for a language
    /// tag. Triggers an on-demand download into the writable dir if the file is
    /// absent (Phase 4D). Returns nullptr if no model can be found or loaded.
    Model* modelForLanguage(const QString& languageTag);

    /// Ordered list of directories to probe for model files. The user-writable
    /// XDG data dir comes first so on-demand downloads (Phase 4D) shadow the
    /// read-only install. The remaining entries are the bundled/dev locations.
    QStringList modelSearchDirs() const;
    /// User-writable directory for on-demand-downloaded models (XDG data
    /// location). Not guaranteed to exist yet; created on first download.
    QString writableModelsDir() const;
    /// Absolute path to @p fileName in the first search dir that has it, or
    /// empty if no search dir contains it. Re-scans each call so a freshly
    /// downloaded model is picked up without restart.
    QString findModelFile(const QString& fileName) const;
    /// Download @p fileName (a catalog model) into the writable dir, verifying
    /// its pinned SHA256. Blocking; runs on the OCR worker thread. Returns true
    /// only when the verified file is in place.
    bool ensureModelDownloaded(const QString& fileName);

    std::unique_ptr<Impl> m_impl;                        ///< shared Ort::Env
    std::map<QString, std::unique_ptr<Model>> m_models;  ///< key = model file name
                                                         ///< (std::map: move-only values OK)
    QSet<QString> m_downloadFailed;                      ///< model files whose download
                                                         ///< failed this session (skip retry
                                                         ///< until the language changes)
};

#endif // SPEEDYNOTE_HAS_PADDLE_OCR
