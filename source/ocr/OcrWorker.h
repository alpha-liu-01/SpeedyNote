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

struct OcrSnapParams {
    bool enabled = false;
    bool cjkGridMode = false;
    int gridSpacing = 32;
    int lineSpacing = 32;
    bool backgroundIsGrid = false;
    bool backgroundIsLines = false;
};

class OcrWorker : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(QObject* parent = nullptr);
    ~OcrWorker() override;

    void setEngine(std::unique_ptr<OcrEngine> engine);
    bool isEngineAvailable() const;
    bool isBusy() const;
    QStringList availableLanguages() const;

public slots:
    void initEngine();
    void setLanguage(const QString& recognizerName);
    void processPage(const QString& pageId,
                     const QVector<VectorStroke>& strokes,
                     const QSet<QString>& suppressedStrokeIds,
                     const OcrSnapParams& snap = OcrSnapParams());

    void processPageIncremental(const QString& pageId,
                                const QVector<VectorStroke>& strokes,
                                const QSet<QString>& suppressedStrokeIds,
                                const OcrSnapParams& snap = OcrSnapParams());

    void processBatch(const QVector<QString>& pageIds,
                      const QVector<QVector<VectorStroke>>& strokeSets,
                      const QVector<QSet<QString>>& suppressedSets,
                      const QVector<OcrSnapParams>& snapParams = QVector<OcrSnapParams>());

    void cancel();

signals:
    void engineReady(bool available);
    void languagesAvailable(const QStringList& languages);
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

Q_DECLARE_METATYPE(OcrSnapParams)
Q_DECLARE_METATYPE(QVector<OcrSnapParams>)
