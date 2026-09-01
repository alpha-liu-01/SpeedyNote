#pragma once

// ============================================================================
// OcrEngine - Abstract OCR engine interface
// ============================================================================
// Part of the OCR Phase 1A infrastructure.
//
// Stateful, incremental interface modeled after Windows Ink InkAnalyzer:
// add/remove strokes, then call analyze() to get results.
// Concrete implementations: WindowsInkOcrEngine (Phase 1), future engines.
// ============================================================================

#include <QString>
#include <QStringList>
#include <QVector>
#include <QRectF>
#include <functional>
#include <memory>

class VectorStroke;

class OcrEngine {
public:
    virtual ~OcrEngine() = default;

    // Optional status hook. Engines may report user-facing progress (e.g. the
    // Linux on-demand model download) through this callback, which the worker
    // forwards to the UI via a queued signal. Invoked on the OCR worker thread,
    // so the callback must be thread-safe (the worker just emits a signal).
    using StatusCallback = std::function<void(const QString&)>;
    void setStatusCallback(StatusCallback cb) { m_statusCallback = std::move(cb); }

    virtual QString engineId() const = 0;

    virtual bool isAvailable() const = 0;

    virtual QStringList availableLanguages() const = 0;
    /// Subset of availableLanguages() whose recognizer data is already present
    /// locally (no download needed). Default: every available language is
    /// considered ready. Engines with on-demand model downloads (e.g. Linux
    /// PaddleOCR) override this so the UI can flag languages that need fetching.
    virtual QStringList downloadedLanguages() const { return availableLanguages(); }
    virtual void setLanguage(const QString& recognizerName) = 0;
    virtual QString language() const = 0;
    /// Whether the "auto" language sentinel really detects the script, as
    /// Apple Vision does. Engines that instead fall back to a system-chosen
    /// recognizer (Windows Ink) return false so the UI can say so rather than
    /// promising detection that will not happen.
    virtual bool supportsAutoLanguageDetection() const { return true; }
    /// Whether Result::confidence carries a real recognition score. Default
    /// false: an engine has to opt in, because the confidence UI is worse than
    /// useless when every result reports the same number.
    virtual bool providesConfidence() const { return false; }

    virtual void addStrokes(const QVector<VectorStroke>& strokes) = 0;
    virtual void removeStrokes(const QVector<QString>& strokeIds) = 0;
    virtual void clearStrokes() = 0;
    virtual bool supportsIncrementalUpdates() const { return true; }

    struct Result {
        QString text;
        QRectF boundingRect;
        float confidence = 0.0f;
        QVector<QString> sourceStrokeIds;

        struct WordSegment {
            QString text;
            QRectF boundingRect;
            // Optional per-character geometry. When populated, the invariant
            // charBoundingBoxes.size() == text.length() holds (mirrors
            // PdfTextBox). Empty when the engine cannot provide char geometry;
            // consumers then fall back to boundingRect.
            QVector<QRectF> charBoundingBoxes;
        };
        QVector<WordSegment> wordSegments;
    };

    virtual QVector<Result> analyze() = 0;

    static std::unique_ptr<OcrEngine> createBest();

protected:
    /// Forward a user-facing status message if a callback is installed.
    void reportStatus(const QString& message) const {
        if (m_statusCallback)
            m_statusCallback(message);
    }

private:
    StatusCallback m_statusCallback;
};
