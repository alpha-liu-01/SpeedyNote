#pragma once

// ============================================================================
// OcrWorker - Background OCR processing worker
// ============================================================================
// Part of the OCR Phase 1A infrastructure.
//
// Runs OCR on a dedicated QThread. Receives stroke data by value
// (implicit sharing / COW), never accesses Page directly.
// Results come back via signals (queued connection to main thread).
// ============================================================================

#include <QObject>
#include <QVector>
#include <QSet>
#include <atomic>
#include <memory>

#include "OcrTextBlock.h"
#include "OcrEngine.h"

class VectorStroke;

class OcrWorker : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(QObject* parent = nullptr);
    ~OcrWorker() override;

    void setEngine(std::unique_ptr<OcrEngine> engine);
    bool isEngineAvailable() const;
    bool isBusy() const;

public slots:
    void initEngine();
    void processPage(const QString& pageId,
                     const QVector<VectorStroke>& strokes,
                     const QSet<QString>& suppressedStrokeIds);

    void processPageIncremental(const QString& pageId,
                                const QVector<VectorStroke>& strokes,
                                const QSet<QString>& suppressedStrokeIds);

    void processBatch(const QVector<QString>& pageIds,
                      const QVector<QVector<VectorStroke>>& strokeSets,
                      const QVector<QSet<QString>>& suppressedSets);

    void cancel();

signals:
    void engineReady(bool available);
    void resultsReady(const QString& pageId,
                      const QVector<OcrTextBlock>& blocks);
    void batchProgress(int completed, int total);
    void batchFinished(int pagesScanned, int pagesWithText);
    void error(const QString& pageId, const QString& message);

private:
    QVector<OcrTextBlock> buildBlocks(const QVector<OcrEngine::Result>& results);

    std::unique_ptr<OcrEngine> m_engine;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_busy{false};

    QString m_lastPageId;
    QSet<QString> m_knownStrokeIds;
};
