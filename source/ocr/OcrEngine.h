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
#include <memory>

class VectorStroke;

class OcrEngine {
public:
    virtual ~OcrEngine() = default;

    virtual QString engineId() const = 0;

    virtual bool isAvailable() const = 0;

    virtual QStringList availableLanguages() const = 0;
    virtual void setLanguage(const QString& recognizerName) = 0;
    virtual QString language() const = 0;

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
        };
        QVector<WordSegment> wordSegments;
    };

    virtual QVector<Result> analyze() = 0;

    static std::unique_ptr<OcrEngine> createBest();
};
