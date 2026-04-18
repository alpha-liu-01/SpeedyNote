#include "MlKitOcrEngine.h"

#ifdef SPEEDYNOTE_HAS_MLKIT_INK

#include "../OcrLineGrouper.h"

#include <QDebug>
#include <QLocale>

// ML Kit Digital Ink tends to truncate when given too many strokes/points in
// one call ("undergeneration"): the row gets cut off in the middle. Splitting
// oversized sub-groups at their widest internal horizontal gap (which is
// where a word boundary is most likely) keeps each native call under the
// model's practical limit while preserving word integrity. Tuned empirically
// for zh / ja / ko / latin models; adjust here if models change.
static constexpr int kMaxStrokesPerRecognize = 30;
static constexpr int kMaxPointsPerRecognize  = 2500;

static bool exceedsRecognizeLimits(const QVector<int>& idx,
                                   const QVector<VectorStroke>& all)
{
    if (idx.size() > kMaxStrokesPerRecognize)
        return true;
    int pts = 0;
    for (int i : idx) {
        pts += all[i].points.size();
        if (pts > kMaxPointsPerRecognize)
            return true;
    }
    return false;
}

// Splits idx (left-to-right order) at the widest horizontal gap between two
// consecutive strokes. Returns {left, right} or {idx} if no viable split
// exists (single stroke, or a split would produce an empty side).
static QVector<QVector<int>> splitStrokesAtWidestGap(
    const QVector<int>& idx, const QVector<VectorStroke>& all)
{
    if (idx.size() < 2)
        return {idx};

    QVector<int> sorted = idx;
    std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return all[a].boundingBox.left() < all[b].boundingBox.left();
    });

    int bestSplit = -1;
    qreal bestGap = -1.0;
    qreal clusterRight = all[sorted[0]].boundingBox.right();
    for (int i = 1; i < sorted.size(); ++i) {
        const qreal gap = all[sorted[i]].boundingBox.left() - clusterRight;
        if (gap > bestGap) {
            bestGap = gap;
            bestSplit = i;
        }
        clusterRight = qMax(clusterRight, all[sorted[i]].boundingBox.right());
    }

    if (bestSplit <= 0 || bestSplit >= sorted.size())
        return {idx};

    QVector<int> left(sorted.begin(), sorted.begin() + bestSplit);
    QVector<int> right(sorted.begin() + bestSplit, sorted.end());
    return {left, right};
}

// Recursively chunks a sub-group until each chunk fits inside the recognize
// limits. If a chunk can't be split any further (one mega-stroke or gapless
// dense ink), it is emitted as-is and logged once -- ML Kit may truncate it,
// but we can't split inside a single stroke without corrupting it.
static QVector<StrokeLineGroup> chunkLargeSubgroup(
    const StrokeLineGroup& sub, const QVector<VectorStroke>& all)
{
    QVector<StrokeLineGroup> out;
    QVector<QVector<int>> stack;
    stack.append(sub.strokeIndices);

    while (!stack.isEmpty()) {
        QVector<int> cur = stack.takeLast();
        if (cur.isEmpty())
            continue;

        if (!exceedsRecognizeLimits(cur, all)) {
            StrokeLineGroup g;
            g.strokeIndices = cur;
            QRectF rect;
            for (int i : cur) {
                const auto& s = all[i];
                if (s.boundingBox.isNull())
                    continue;
                rect = rect.isNull() ? s.boundingBox : rect.united(s.boundingBox);
            }
            g.boundingRect = rect;
            out.append(g);
            continue;
        }

        QVector<QVector<int>> halves = splitStrokesAtWidestGap(cur, all);
        if (halves.size() < 2) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                qWarning() << "MlKitOcrEngine: cannot split oversized stroke chunk ("
                           << cur.size() << "strokes); recognition may truncate";
            }
            StrokeLineGroup g;
            g.strokeIndices = cur;
            QRectF rect;
            for (int i : cur) {
                const auto& s = all[i];
                if (s.boundingBox.isNull())
                    continue;
                rect = rect.isNull() ? s.boundingBox : rect.united(s.boundingBox);
            }
            g.boundingRect = rect;
            out.append(g);
            continue;
        }

        // Push in reverse so left half is processed first (left-to-right order).
        for (int i = halves.size() - 1; i >= 0; --i)
            stack.append(halves[i]);
    }

    return out;
}

static QString localeToMlKitTag(const QString& qtLocale)
{
    static const QHash<QString, QString> specialMappings = {
        {QStringLiteral("zh_CN"), QStringLiteral("zh-Hani-CN")},
        {QStringLiteral("zh_TW"), QStringLiteral("zh-Hani-TW")},
        {QStringLiteral("zh_HK"), QStringLiteral("zh-Hani-HK")},
        {QStringLiteral("ja_JP"), QStringLiteral("ja")},
        {QStringLiteral("ja"),    QStringLiteral("ja")},
        {QStringLiteral("ko_KR"), QStringLiteral("ko")},
        {QStringLiteral("ko"),    QStringLiteral("ko")},
    };
    auto it = specialMappings.find(qtLocale);
    if (it != specialMappings.end())
        return it.value();
    return QString(qtLocale).replace(QLatin1Char('_'), QLatin1Char('-'));
}

MlKitOcrEngine::MlKitOcrEngine()
    : m_languageTag(QStringLiteral("en-US"))
{
    m_available = checkAvailabilityNative();
}

MlKitOcrEngine::~MlKitOcrEngine()
{
    invalidateNativeRecognizer();
}

bool MlKitOcrEngine::isAvailable() const
{
    return m_available;
}

QStringList MlKitOcrEngine::availableLanguages() const
{
    if (m_cachedLanguages.isEmpty())
        m_cachedLanguages = queryLanguagesNative();
    return m_cachedLanguages;
}

void MlKitOcrEngine::setLanguage(const QString& languageTag)
{
    QString resolved = languageTag;
    if (resolved.isEmpty() || resolved == QLatin1String("auto"))
        resolved = localeToMlKitTag(QLocale::system().name());
    else if (resolved.contains(QLatin1Char('_')))
        resolved = localeToMlKitTag(resolved);

    if (resolved == m_languageTag && m_modelDownloaded)
        return;

    if (resolved != m_languageTag)
        invalidateNativeRecognizer();

    m_languageTag = resolved;
    m_modelDownloaded = ensureModelDownloadedNative(resolved);
}

QString MlKitOcrEngine::language() const
{
    return m_languageTag;
}

void MlKitOcrEngine::addStrokes(const QVector<VectorStroke>& strokes)
{
    for (const auto& stroke : strokes) {
        m_strokeIndexById.insert(stroke.id, static_cast<int>(m_strokes.size()));
        m_strokes.append(stroke);
    }
}

void MlKitOcrEngine::removeStrokes(const QVector<QString>& strokeIds)
{
    for (const auto& id : strokeIds) {
        auto it = m_strokeIndexById.find(id);
        if (it == m_strokeIndexById.end())
            continue;

        int idx = it.value();
        m_strokeIndexById.erase(it);

        if (idx < m_strokes.size() - 1) {
            m_strokes[idx] = m_strokes.last();
            m_strokeIndexById[m_strokes[idx].id] = idx;
        }
        m_strokes.removeLast();
    }
}

void MlKitOcrEngine::clearStrokes()
{
    m_strokes.clear();
    m_strokeIndexById.clear();
}

QVector<OcrEngine::Result> MlKitOcrEngine::analyze()
{
    if (m_strokes.isEmpty())
        return {};

    const auto lineGroups = groupStrokesIntoLines(m_strokes);

    QVector<Result> results;
    results.reserve(lineGroups.size());

    for (const auto& group : lineGroups) {
        const auto subGroups = splitLineByHorizontalGaps(group, m_strokes);

        for (const auto& sub : subGroups) {
            // Split any oversized sub-group before feeding it to the native
            // recognizer to avoid ML Kit's long-input truncation.
            const auto chunks = chunkLargeSubgroup(sub, m_strokes);

            for (const auto& chunk : chunks) {
                QVector<VectorStroke> subStrokes;
                subStrokes.reserve(chunk.strokeIndices.size());

                QVector<QString> sourceIds;
                sourceIds.reserve(chunk.strokeIndices.size());

                for (int idx : chunk.strokeIndices) {
                    subStrokes.append(m_strokes[idx]);
                    sourceIds.append(m_strokes[idx].id);
                }

                const QString text = recognizeStrokesNative(subStrokes);
                if (text.isEmpty())
                    continue;

                Result r;
                r.text = text;
                r.boundingRect = chunk.boundingRect;
                r.confidence = 1.0f;
                r.sourceStrokeIds = sourceIds;
                results.append(r);
            }
        }
    }

    return results;
}

#endif // SPEEDYNOTE_HAS_MLKIT_INK
