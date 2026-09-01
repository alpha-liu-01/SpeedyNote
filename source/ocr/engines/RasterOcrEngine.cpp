#include "RasterOcrEngine.h"

#include "../OcrLineGrouper.h"
#include "../OcrStrokeRasterizer.h"
#include "../OcrTextBlock.h" // isCjkLikeChar

#include <QLocale>
#include <QSet>
#include <QStringList>
#ifdef SPEEDYNOTE_DEBUG
#include <QDebug>
#endif

#include <algorithm>

// ----------------------------------------------------------------------------
// Line signature: an order-independent 64-bit hash over a group's sorted stroke
// IDs plus quantized point coordinates. Moving, adding, removing, or reshaping
// any stroke in the group changes the signature, invalidating only that line's
// cache entry (QA Q2.2 / Q11.5). Cosmetic attributes (color, pen width,
// pressure) are intentionally excluded: the rasterizer ignores them, so a
// color edit must not force re-recognition.
// ----------------------------------------------------------------------------
namespace {

constexpr quint64 kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr quint64 kFnvPrime       = 0x100000001b3ULL;
// Domain separators mixed into the hash so e.g. id "ab" + first point cannot
// collide with id "a" + 'b'-as-coordinate.
constexpr quint64 kSepIdPoints = 0x5EA1D000000FFFF1ULL; // between id and points
constexpr quint64 kSepStroke   = 0x5EA1D000000EEEE2ULL; // between strokes

inline void fnvMix(quint64& h, quint64 v)
{
    h ^= v;
    h *= kFnvPrime;
}

quint64 lineSignature(const StrokeLineGroup& group,
                      const QVector<VectorStroke>& strokes)
{
    QVector<QPair<QString, int>> items;
    items.reserve(group.strokeIndices.size());
    for (int idx : group.strokeIndices) {
        if (idx < 0 || idx >= strokes.size())
            continue;
        items.append({strokes[idx].id, idx});
    }
    std::sort(items.begin(), items.end(),
              [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                  return a.first < b.first;
              });

    quint64 h = kFnvOffsetBasis;
    for (const auto& it : items) {
        for (const QChar c : it.first)
            fnvMix(h, c.unicode());
        fnvMix(h, kSepIdPoints);
        const auto& pts = strokes[it.second].points;
        for (const auto& p : pts) {
            fnvMix(h, static_cast<quint64>(static_cast<qint64>(qRound(p.pos.x()))));
            fnvMix(h, static_cast<quint64>(static_cast<qint64>(qRound(p.pos.y()))));
        }
        fnvMix(h, kSepStroke);
    }
    return h;
}

} // namespace

RasterOcrEngine::RasterOcrEngine()
    : m_languageTag(QStringLiteral("en-US"))
{
}

RasterOcrEngine::~RasterOcrEngine() = default;

QString RasterOcrEngine::resolveAutoLanguage(const QString& langSubtag,
                                             const QString& script,
                                             const QString& bcp47Name,
                                             const QString& localeName,
                                             const QStringList& available)
{
    const QString lang = langSubtag.toLower();
    if (lang.isEmpty() || available.isEmpty())
        return {};

    // QLocale may report AnyScript for a Han language (e.g. "zh_CA"); infer the
    // script from the region so we can still pick zh-Hans vs zh-Hant.
    QString effScript = script;
    if (lang == QLatin1String("zh") && effScript.isEmpty()) {
        const QString region = localeName.section(QLatin1Char('_'), 1, 1).toUpper();
        effScript = (region == QLatin1String("TW") || region == QLatin1String("HK") ||
                     region == QLatin1String("MO"))
                        ? QStringLiteral("Hant")
                        : QStringLiteral("Hans");
    }

    // Prioritized candidate tags: most specific first.
    QStringList candidates;
    if (!effScript.isEmpty())
        candidates << (lang + QLatin1Char('-') + effScript);   // e.g. zh-Hans
    if (!bcp47Name.isEmpty())
        candidates << bcp47Name;                                // e.g. zh-Hant
    candidates << QString(localeName).replace(QLatin1Char('_'), QLatin1Char('-')); // zh-CA
    candidates << lang;                                         // zh

    // Pass 1: exact (case-insensitive) match against the backend's tags.
    for (const QString& c : candidates) {
        if (c.isEmpty())
            continue;
        for (const QString& a : available) {
            if (a.compare(c, Qt::CaseInsensitive) == 0)
                return a;
        }
    }

    // Pass 2: language-subtag fallback. Prefer a tag carrying the right script
    // (zh-Hans), otherwise the first tag sharing the primary subtag.
    QString langFallback;
    for (const QString& a : available) {
        if (a.section(QLatin1Char('-'), 0, 0).compare(lang, Qt::CaseInsensitive) != 0)
            continue;
        if (!effScript.isEmpty() && a.contains(effScript, Qt::CaseInsensitive))
            return a;
        if (langFallback.isEmpty())
            langFallback = a;
    }
    return langFallback;
}

void RasterOcrEngine::setLanguage(const QString& recognizerName)
{
    // Normalize the "auto-detect" sentinels. The OCR UI uses two of them: an
    // empty string (Settings combo) and the literal "auto" (per-document
    // dialog). Neither is a valid recognizer tag, so passing them straight
    // through would make a raster backend mis-recognize - e.g. Apple Vision
    // would receive recognitionLanguages = @["auto"]. Resolve them to a tag the
    // backend actually exposes, otherwise to an empty tag, which each backend
    // treats as "use the engine default" (Vision omits recognitionLanguages;
    // Paddle falls back to its default latin model).
    QString resolved = recognizerName;
    if (resolved.isEmpty() || resolved.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        const QLocale sys = QLocale::system();
        QString script;
        switch (sys.script()) {
        case QLocale::SimplifiedHanScript:  script = QStringLiteral("Hans"); break;
        case QLocale::TraditionalHanScript: script = QStringLiteral("Hant"); break;
        default: break;
        }
        resolved = resolveAutoLanguage(sys.name().section(QLatin1Char('_'), 0, 0),
                                       script, sys.bcp47Name(), sys.name(),
                                       availableLanguages());
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[OCR] auto language: name" << sys.name()
                 << "bcp47" << sys.bcp47Name() << "script" << script
                 << "-> resolved" << (resolved.isEmpty() ? QStringLiteral("(engine default)") : resolved);
#endif
    }

    if (resolved != m_languageTag) {
        // Language change can alter recognized text for the same ink, so the
        // cached per-line results are no longer valid.
        m_lineCache.clear();
        m_languageTag = resolved;
    }
}

QString RasterOcrEngine::language() const
{
    return m_languageTag;
}

void RasterOcrEngine::addStrokes(const QVector<VectorStroke>& strokes)
{
    for (const auto& stroke : strokes) {
        m_strokeIndexById.insert(stroke.id, static_cast<int>(m_strokes.size()));
        m_strokes.append(stroke);
    }
}

void RasterOcrEngine::removeStrokes(const QVector<QString>& strokeIds)
{
    for (const auto& id : strokeIds) {
        auto it = m_strokeIndexById.find(id);
        if (it == m_strokeIndexById.end())
            continue;

        int idx = it.value();
        m_strokeIndexById.erase(it);

        // Swap-and-pop to keep the index map cheap (matches MlKitOcrEngine).
        if (idx < m_strokes.size() - 1) {
            m_strokes[idx] = m_strokes.last();
            m_strokeIndexById[m_strokes[idx].id] = idx;
        }
        m_strokes.removeLast();
    }
}

void RasterOcrEngine::clearStrokes()
{
    m_strokes.clear();
    m_strokeIndexById.clear();
    m_lineCache.clear();
}

QVector<OcrEngine::Result> RasterOcrEngine::analyze()
{
    if (m_strokes.isEmpty()) {
        m_lineCache.clear();
        return {};
    }

    // Internal default grouping mirrors MlKitOcrEngine::analyze(): adaptive
    // line detection then horizontal-gap splitting. The worker handles
    // OcrSnapParams-driven grid/band grouping upstream (QA Q2.3 Option B).
    const auto lineGroups = groupStrokesIntoLines(m_strokes);

    QVector<Result> results;
    QSet<quint64> liveSigs;

    for (const auto& line : lineGroups) {
        const auto subGroups = splitLineByHorizontalGaps(line, m_strokes);

        for (const auto& group : subGroups) {
            if (group.strokeIndices.isEmpty())
                continue;

            const quint64 sig = lineSignature(group, m_strokes);
            liveSigs.insert(sig);

            auto cached = m_lineCache.constFind(sig);
            if (cached != m_lineCache.constEnd()) {
                results.append(cached->result);
                continue;
            }

            const RasterStrip strip =
                rasterizeStrokes(m_strokes, group.strokeIndices, targetStripHeightPx());
            if (strip.image.isNull())
                continue;

            const ImageRecognition rec = recognizeImage(strip.image, m_languageTag);
            if (rec.text.isEmpty())
                continue;

            // Move the freshly built Result into the cache, then append a copy
            // from the cache (one copy, matching the cache-hit path above).
            const auto inserted =
                m_lineCache.insert(sig, CachedLine{buildResult(group, strip.transform, rec)});
            results.append(inserted.value().result);
        }
    }

    // Evict cache entries for lines that no longer exist (moved/edited/removed).
    for (auto it = m_lineCache.begin(); it != m_lineCache.end();) {
        if (!liveSigs.contains(it.key()))
            it = m_lineCache.erase(it);
        else
            ++it;
    }

    return results;
}

OcrEngine::Result RasterOcrEngine::buildResult(const StrokeLineGroup& group,
                                               const RasterTransform& transform,
                                               const ImageRecognition& rec) const
{
    Result r;
    r.text = rec.text;
    r.boundingRect = group.boundingRect;
    r.confidence = rec.confidence;
    r.sourceStrokeIds.reserve(group.strokeIndices.size());
    for (int idx : group.strokeIndices) {
        if (idx >= 0 && idx < m_strokes.size())
            r.sourceStrokeIds.append(m_strokes[idx].id);
    }

    const bool haveChars = !rec.text.isEmpty()
                        && rec.charBoxesImage.size() == rec.text.length();
    if (!haveChars) {
        // Graceful fallback: a single line-level segment (text + group rect,
        // no per-char geometry) so search/selection still work coarsely.
        Result::WordSegment seg;
        seg.text = rec.text;
        seg.boundingRect = group.boundingRect;
        r.wordSegments.append(seg);
        return r;
    }

    // Map every character box back to canvas space via the exact inverse.
    QVector<QRectF> canvasBoxes;
    canvasBoxes.reserve(rec.charBoxesImage.size());
    for (const auto& ib : rec.charBoxesImage)
        canvasBoxes.append(transform.imageToCanvas(ib));

    // Assemble WordSegments: Latin words split on whitespace, one segment per
    // CJK glyph (QA Q3.2). Each segment carries its per-char boxes.
    QString curWord;
    QVector<QRectF> curBoxes;
    auto flushWord = [&]() {
        if (curWord.isEmpty())
            return;
        Result::WordSegment seg;
        seg.text = curWord;
        QRectF u;
        for (const auto& b : curBoxes)
            u = u.isNull() ? b : u.united(b);
        seg.boundingRect = u;
        seg.charBoundingBoxes = curBoxes;
        r.wordSegments.append(seg);
        curWord.clear();
        curBoxes.clear();
    };

    const QString& text = rec.text;
    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text[i];
        if (ch.isSpace()) {
            flushWord();
            continue;
        }
        if (isCjkLikeChar(ch)) {
            flushWord();
            Result::WordSegment seg;
            seg.text = QString(ch);
            seg.boundingRect = canvasBoxes[i];
            seg.charBoundingBoxes.append(canvasBoxes[i]);
            r.wordSegments.append(seg);
            continue;
        }
        curWord.append(ch);
        curBoxes.append(canvasBoxes[i]);
    }
    flushWord();

    return r;
}
