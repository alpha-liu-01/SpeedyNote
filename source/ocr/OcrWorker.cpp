#include "OcrWorker.h"
#include "OcrLineGrouper.h"
#include "../strokes/VectorStroke.h"

#include <QHash>
#include <QUuid>
#include <cmath>

// Ensures the result has at least one wordSegment so OcrTextObject::render()
// can take the snap-aware rendering path. Engines like ML Kit Digital Ink
// return plain text with no per-word geometry; in snap mode the worker has
// already replaced r.boundingRect with the snapped group rect, so a single
// segment covering that rect is exactly what the CJK/line-band renderer
// expects. No-op for Windows Ink and any other engine that already provides
// word-level segments.
static void ensureWordSegment(OcrEngine::Result& r)
{
    if (!r.wordSegments.isEmpty() || r.text.isEmpty() || !r.boundingRect.isValid())
        return;
    OcrEngine::Result::WordSegment ws;
    ws.text = r.text;
    ws.boundingRect = r.boundingRect;
    r.wordSegments.append(ws);
}

// When the engine splits a single snap group into multiple chunks (long-line
// chunker in MlKitOcrEngine), each chunk needs its own snapped sub-rect
// inside the group so the renderer doesn't stack chunks on top of each other.
// CJK grid mode: snap the chunk's horizontal span to whole grid cells inside
// the group's cell range. Line-band mode: preserve the chunk's horizontal
// extent, keep the group's vertical band.
static QRectF snapChunkRect(const QRectF& engineSub,
                            const QRectF& groupRect,
                            const OcrSnapParams& snap)
{
    if (snap.cjkGridMode && snap.backgroundIsGrid && snap.gridSpacing > 0) {
        const qreal gs = static_cast<qreal>(snap.gridSpacing);
        const int groupStart = qRound(groupRect.left()  / gs);
        const int groupEnd   = qRound(groupRect.right() / gs) - 1;
        const int row        = qRound(groupRect.top()   / gs);

        int cellStart = static_cast<int>(std::floor(engineSub.left() / gs));
        int cellEnd   = static_cast<int>(std::floor((engineSub.right() - 1e-3) / gs));
        cellStart = qBound(groupStart, cellStart, groupEnd);
        cellEnd   = qBound(cellStart,  cellEnd,   groupEnd);

        return QRectF(cellStart * gs,
                      row * gs,
                      (cellEnd - cellStart + 1) * gs,
                      gs);
    }

    qreal left  = qMax(engineSub.left(),  groupRect.left());
    qreal right = qMin(engineSub.right(), groupRect.right());
    if (right <= left) {
        left  = groupRect.left();
        right = groupRect.right();
    }
    return QRectF(left, groupRect.top(), right - left, groupRect.height());
}

// Collapses the N chunks of one snap group (ML Kit long-line chunker) into a
// single Result. This produces ONE OcrTextObject per line band, which draws
// ONE gray background over the full band and then renders each chunk's text
// at its own snapped sub-rect via wordSegments. Without this merge, adjacent
// chunks would each draw their own full-height gray rect, and their
// horizontally-overlapping stroke bounding boxes would make the gray of one
// chunk paint over a word of its neighbour.
static OcrEngine::Result mergeChunkedGroup(QVector<OcrEngine::Result> chunks,
                                           const QRectF& groupRect,
                                           const OcrSnapParams& snap)
{
    std::sort(chunks.begin(), chunks.end(),
              [](const OcrEngine::Result& a, const OcrEngine::Result& b) {
                  return a.boundingRect.left() < b.boundingRect.left();
              });

    OcrEngine::Result merged;
    merged.boundingRect = groupRect;

    const bool cjk = snap.cjkGridMode;
    float confSum = 0.0f;
    int   confCount = 0;
    QStringList parts;
    parts.reserve(chunks.size());

    for (const auto& c : chunks) {
        const QRectF snapped = snapChunkRect(c.boundingRect, groupRect, snap);

        OcrEngine::Result::WordSegment ws;
        ws.text = c.text;
        ws.boundingRect = snapped;
        merged.wordSegments.append(ws);

        if (!c.text.isEmpty())
            parts.append(c.text);

        merged.sourceStrokeIds += c.sourceStrokeIds;

        confSum  += c.confidence;
        confCount += 1;
    }

    merged.text = parts.join(cjk ? QStringLiteral("") : QStringLiteral(" "));
    merged.confidence = confCount ? confSum / static_cast<float>(confCount) : 0.0f;
    return merged;
}

// Char count used by the CJK self-heal check. QString::size() is UTF-16 code
// units; adequate for the BMP-CJK characters handled by ML Kit Digital Ink.
// Rare SIP characters (surrogate pairs) will falsely trigger per-cell re-run,
// which still produces a correct result - just with a few extra recognize calls.
static int countCjkCharsForSelfHeal(const QString& text)
{
    return text.size();
}

// When the single per-group ML Kit recognition returned a char count that does
// not match the occupied-cell count, fall back to one ML Kit call per cell.
// Each cell is recognized in isolation from its own strokes (bucketed by the
// same stroke-center rule groupStrokesByGridCells uses), then the per-cell
// Results are fed through mergeChunkedGroup so the output has the exact same
// shape as the existing fast path: one Result per row, one wordSegment per
// occupied cell, text joined CJK-style.
static OcrEngine::Result rerunPerCell(const StrokeLineGroup& group,
                                      const QVector<VectorStroke>& filtered,
                                      OcrEngine* engine,
                                      const OcrSnapParams& snap)
{
    OcrEngine::Result empty;
    if (!engine || snap.gridSpacing <= 0 || group.strokeIndices.isEmpty())
        return empty;

    const qreal gs = static_cast<qreal>(snap.gridSpacing);
    const int row       = qRound(group.boundingRect.top()   / gs);
    const int runStart  = qRound(group.boundingRect.left()  / gs);
    const int cellCount = qRound(group.boundingRect.width() / gs);
    const int runEnd    = runStart + cellCount - 1;

    QHash<int, QVector<VectorStroke>> byCol;
    for (int idx : group.strokeIndices) {
        if (idx < 0 || idx >= filtered.size()) continue;
        const auto& s = filtered[idx];
        if (s.points.isEmpty() || s.boundingBox.isNull()) continue;
        int col = static_cast<int>(s.boundingBox.center().x() / gs);
        byCol[col].append(s);
    }

    QVector<OcrEngine::Result> cellResults;
    cellResults.reserve(cellCount);

    for (int col = runStart; col <= runEnd; ++col) {
        auto it = byCol.constFind(col);
        if (it == byCol.constEnd() || it->isEmpty())
            continue;

        engine->clearStrokes();
        engine->addStrokes(*it);
        auto r = engine->analyze();
        if (r.isEmpty())
            continue;

        QString txt;
        float confSum = 0.0f;
        int   confCount = 0;
        QVector<QString> sids;
        for (const auto& subR : r) {
            if (!subR.text.isEmpty())
                txt += subR.text;
            confSum  += subR.confidence;
            confCount += 1;
            sids += subR.sourceStrokeIds;
        }

        if (txt.isEmpty())
            continue;

        OcrEngine::Result cellR;
        cellR.text = std::move(txt);
        cellR.boundingRect = QRectF(col * gs, row * gs, gs, gs);
        cellR.confidence = confCount ? confSum / static_cast<float>(confCount) : 0.0f;
        cellR.sourceStrokeIds = std::move(sids);
        cellResults.append(std::move(cellR));
    }

    if (cellResults.isEmpty())
        return empty;

    return mergeChunkedGroup(std::move(cellResults), group.boundingRect, snap);
}

// Runs the full "recognize one snap group" pipeline: feed strokes to the
// engine, merge multi-chunk output into a single Result with one WordSegment
// per chunk, then in CJK grid mode fall back to per-cell recognition when
// the recognized char count does not match the occupied cell count.
//
// Returns false when the group produced no usable result (engine returned
// empty, or cancellation); callers should skip the group in that case.
// Extracted from processPage/processBatch so future fixes only need to touch
// one place. Keeps the per-group clearStrokes call out of the helper so the
// caller owns engine state bookkeeping.
static bool recognizeSnapGroup(OcrEngine* engine,
                               const StrokeLineGroup& group,
                               const QVector<VectorStroke>& filtered,
                               const OcrSnapParams& snap,
                               OcrEngine::Result& outMerged)
{
    QVector<VectorStroke> groupStrokes;
    groupStrokes.reserve(group.strokeIndices.size());
    for (int idx : group.strokeIndices) {
        if (idx >= 0 && idx < filtered.size())
            groupStrokes.append(filtered[idx]);
    }

    engine->clearStrokes();
    engine->addStrokes(groupStrokes);
    auto groupResults = engine->analyze();

    if (groupResults.size() == 1) {
        outMerged = std::move(groupResults[0]);
        outMerged.boundingRect = group.boundingRect;
        ensureWordSegment(outMerged);
    } else if (!groupResults.isEmpty()) {
        // Engine split this group into multiple chunks (MlKit long-line
        // chunker). Collapse them into ONE Result with per-chunk
        // wordSegments so the renderer draws a single unified background
        // over the whole band.
        outMerged = mergeChunkedGroup(std::move(groupResults),
                                      group.boundingRect, snap);
    } else {
        return false;
    }

    // CJK grid-mode self-heal: when ML Kit drops or merges chars mid-row,
    // the recognized length stops matching the occupied cell count. Re-run
    // per cell only for those mismatched rows so the renderer sees one char
    // per cell and the row stays aligned.
    if (snap.cjkGridMode && snap.backgroundIsGrid && snap.gridSpacing > 0) {
        const int occupiedCells = qRound(group.boundingRect.width()
                                         / static_cast<qreal>(snap.gridSpacing));
        const int charCount = countCjkCharsForSelfHeal(outMerged.text);

        if (occupiedCells >= 2 && charCount != occupiedCells) {
            OcrEngine::Result healed = rerunPerCell(group, filtered, engine, snap);
            if (!healed.wordSegments.isEmpty())
                outMerged = std::move(healed);
        }
    }

    return true;
}

OcrWorker::OcrWorker(QObject* parent)
    : QObject(parent)
{
}

OcrWorker::~OcrWorker() = default;

void OcrWorker::setEngine(std::unique_ptr<OcrEngine> engine)
{
    m_engine = std::move(engine);
}

bool OcrWorker::isEngineAvailable() const
{
    return m_engine && m_engine->isAvailable();
}

bool OcrWorker::isBusy() const
{
    return m_busy.load();
}

QStringList OcrWorker::availableLanguages() const
{
    return m_engine ? m_engine->availableLanguages() : QStringList();
}

void OcrWorker::initEngine()
{
    m_engine = OcrEngine::createBest();
    bool ok = m_engine && m_engine->isAvailable();
    emit engineReady(ok);
    if (ok)
        emit languagesAvailable(m_engine->availableLanguages());
}

void OcrWorker::setLanguage(const QString& recognizerName)
{
    if (!m_engine) return;

    QString prev = m_engine->language();
    m_engine->setLanguage(recognizerName);

    if (m_engine->language() != prev) {
        m_lastPageId.clear();
        m_knownStrokeIds.clear();
    }
}

void OcrWorker::cancel()
{
    m_cancelled = true;
}

QVector<OcrTextBlock> OcrWorker::buildBlocks(const QVector<OcrEngine::Result>& results)
{
    QVector<OcrTextBlock> blocks;
    blocks.reserve(results.size());
    QString eid = m_engine->engineId();
    for (const auto& r : results) {
        OcrTextBlock block = OcrTextBlock::create();
        block.text = r.text;
        block.boundingRect = r.boundingRect;
        block.confidence = r.confidence;
        block.sourceStrokeIds = r.sourceStrokeIds;
        block.engineId = eid;
        for (const auto& ws : r.wordSegments) {
            OcrTextBlock::WordSegment seg;
            seg.text = ws.text;
            seg.boundingRect = ws.boundingRect;
            block.wordSegments.append(seg);
        }
        blocks.append(block);
    }
    return blocks;
}

void OcrWorker::processPage(const QString& pageId,
                            const QVector<VectorStroke>& strokes,
                            const QSet<QString>& suppressedStrokeIds,
                            const OcrSnapParams& snap)
{
    if (!m_engine || !m_engine->isAvailable()) {
        emit error(pageId, QStringLiteral("OCR engine not available"));
        return;
    }

    m_busy = true;
    m_cancelled = false;

    QVector<VectorStroke> filtered;
    filtered.reserve(strokes.size());
    for (const auto& stroke : strokes) {
        if (!suppressedStrokeIds.contains(stroke.id))
            filtered.append(stroke);
    }

    bool useSnap = snap.enabled && (snap.backgroundIsGrid || snap.backgroundIsLines);

    if (useSnap) {
        QVector<StrokeLineGroup> groups;
        if (snap.cjkGridMode && snap.backgroundIsGrid) {
            groups = groupStrokesByGridCells(filtered, snap.gridSpacing);
        } else if (snap.backgroundIsLines) {
            groups = groupStrokesByLineBands(filtered, snap.lineSpacing);
        } else {
            groups = groupStrokesByLineBands(filtered, snap.gridSpacing);
        }

        QVector<OcrEngine::Result> allResults;

        for (const auto& group : groups) {
            if (m_cancelled) break;

            OcrEngine::Result merged;
            if (recognizeSnapGroup(m_engine.get(), group, filtered, snap, merged))
                allResults.append(std::move(merged));
        }

        if (m_cancelled) { m_busy = false; return; }

        m_lastPageId = pageId;
        m_knownStrokeIds.clear();
        for (const auto& s : filtered)
            m_knownStrokeIds.insert(s.id);

        m_busy = false;
        emit resultsReady(pageId, buildBlocks(allResults));
    } else {
        m_engine->clearStrokes();
        m_engine->addStrokes(filtered);

        if (m_cancelled) { m_busy = false; return; }

        QVector<OcrEngine::Result> results = m_engine->analyze();

        if (m_cancelled) { m_busy = false; return; }

        for (auto& r : results)
            ensureWordSegment(r);

        m_lastPageId = pageId;
        m_knownStrokeIds.clear();
        for (const auto& s : filtered)
            m_knownStrokeIds.insert(s.id);

        m_busy = false;
        emit resultsReady(pageId, buildBlocks(results));
    }
}

void OcrWorker::processPageIncremental(const QString& pageId,
                                       const QVector<VectorStroke>& strokes,
                                       const QSet<QString>& suppressedStrokeIds,
                                       const OcrSnapParams& snap)
{
    // When snap is enabled, always do a full re-scan (pre-grouping invalidates incremental state)
    if (snap.enabled && (snap.backgroundIsGrid || snap.backgroundIsLines)) {
        processPage(pageId, strokes, suppressedStrokeIds, snap);
        return;
    }

    if (pageId != m_lastPageId || m_knownStrokeIds.isEmpty()
        || !m_engine->supportsIncrementalUpdates()) {
        processPage(pageId, strokes, suppressedStrokeIds, snap);
        return;
    }

    if (!m_engine || !m_engine->isAvailable()) {
        emit error(pageId, QStringLiteral("OCR engine not available"));
        return;
    }

    m_busy = true;
    m_cancelled = false;

    QSet<QString> currentIds;
    QHash<QString, const VectorStroke*> currentMap;
    for (const auto& stroke : strokes) {
        if (!suppressedStrokeIds.contains(stroke.id)) {
            currentIds.insert(stroke.id);
            currentMap.insert(stroke.id, &stroke);
        }
    }

    QSet<QString> removedIds = m_knownStrokeIds - currentIds;
    QSet<QString> addedIds = currentIds - m_knownStrokeIds;

    if (removedIds.isEmpty() && addedIds.isEmpty()) {
        m_busy = false;
        return;
    }

    if (!removedIds.isEmpty()) {
        QVector<QString> removeList(removedIds.begin(), removedIds.end());
        m_engine->removeStrokes(removeList);
    }

    if (!addedIds.isEmpty()) {
        QVector<VectorStroke> addedStrokes;
        addedStrokes.reserve(addedIds.size());
        for (const auto& id : addedIds) {
            auto it = currentMap.find(id);
            if (it != currentMap.end())
                addedStrokes.append(*it.value());
        }
        m_engine->addStrokes(addedStrokes);
    }

    if (m_cancelled) {
        m_lastPageId.clear();
        m_knownStrokeIds.clear();
        m_busy = false;
        return;
    }

    QVector<OcrEngine::Result> results = m_engine->analyze();

    if (m_cancelled) {
        m_lastPageId.clear();
        m_knownStrokeIds.clear();
        m_busy = false;
        return;
    }

    for (auto& r : results)
        ensureWordSegment(r);

    m_knownStrokeIds = currentIds;

    m_busy = false;
    emit resultsReady(pageId, buildBlocks(results));
}

void OcrWorker::processBatch(const QVector<QString>& pageIds,
                             const QVector<QVector<VectorStroke>>& strokeSets,
                             const QVector<QSet<QString>>& suppressedSets,
                             const QVector<OcrSnapParams>& snapParams)
{
    if (!m_engine || !m_engine->isAvailable()) {
        for (const auto& pid : pageIds)
            emit error(pid, QStringLiteral("OCR engine not available"));
        emit batchFinished(0, 0);
        return;
    }

    int total = qMin(pageIds.size(), strokeSets.size());
    int completed = 0;
    int pagesWithText = 0;

    static const QSet<QString> emptySet;
    static const OcrSnapParams defaultSnap;

    m_busy = true;
    m_cancelled = false;

    for (int i = 0; i < total; ++i) {
        if (m_cancelled)
            break;

        const QSet<QString>& suppressed = (i < suppressedSets.size())
            ? suppressedSets[i] : emptySet;
        const OcrSnapParams& snap = (i < snapParams.size())
            ? snapParams[i] : defaultSnap;

        QVector<VectorStroke> filtered;
        const auto& strokes = strokeSets[i];
        filtered.reserve(strokes.size());
        for (const auto& stroke : strokes) {
            if (!suppressed.contains(stroke.id))
                filtered.append(stroke);
        }

        bool useSnap = snap.enabled && (snap.backgroundIsGrid || snap.backgroundIsLines);

        QVector<OcrEngine::Result> results;

        if (useSnap) {
            QVector<StrokeLineGroup> groups;
            if (snap.cjkGridMode && snap.backgroundIsGrid) {
                groups = groupStrokesByGridCells(filtered, snap.gridSpacing);
            } else if (snap.backgroundIsLines) {
                groups = groupStrokesByLineBands(filtered, snap.lineSpacing);
            } else {
                groups = groupStrokesByLineBands(filtered, snap.gridSpacing);
            }

            for (const auto& group : groups) {
                if (m_cancelled) break;

                OcrEngine::Result merged;
                if (recognizeSnapGroup(m_engine.get(), group, filtered, snap, merged))
                    results.append(std::move(merged));
            }
        } else {
            m_engine->clearStrokes();
            m_engine->addStrokes(filtered);

            if (m_cancelled) break;

            results = m_engine->analyze();

            for (auto& r : results)
                ensureWordSegment(r);
        }

        if (m_cancelled)
            break;

        QVector<OcrTextBlock> blocks = buildBlocks(results);

        if (!blocks.isEmpty())
            ++pagesWithText;

        emit resultsReady(pageIds[i], blocks);

        ++completed;
        emit batchProgress(completed, total);
    }

    m_lastPageId.clear();
    m_knownStrokeIds.clear();

    m_busy = false;
    emit batchFinished(completed, pagesWithText);
}
