#include "MlKitOcrEngine.h"

#ifdef SPEEDYNOTE_HAS_MLKIT_INK

#include "../OcrLineGrouper.h"

#include <QLocale>

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

MlKitOcrEngine::~MlKitOcrEngine() = default;

bool MlKitOcrEngine::isAvailable() const
{
    return m_available;
}

QStringList MlKitOcrEngine::availableLanguages() const
{
    return queryLanguagesNative();
}

void MlKitOcrEngine::setLanguage(const QString& languageTag)
{
    QString resolved = languageTag;
    if (resolved.isEmpty() || resolved == QLatin1String("auto"))
        resolved = localeToMlKitTag(QLocale::system().name());
    else if (resolved.contains(QLatin1Char('_')))
        resolved = localeToMlKitTag(resolved);

    m_languageTag = resolved;
    ensureModelDownloadedNative(resolved);
}

QString MlKitOcrEngine::language() const
{
    return m_languageTag;
}

void MlKitOcrEngine::addStrokes(const QVector<VectorStroke>& strokes)
{
    for (const auto& stroke : strokes) {
        m_strokeIndexById.insert(stroke.id, m_strokes.size());
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
        QVector<VectorStroke> lineStrokes;
        lineStrokes.reserve(group.strokeIndices.size());

        QVector<QString> sourceIds;
        sourceIds.reserve(group.strokeIndices.size());

        for (int idx : group.strokeIndices) {
            lineStrokes.append(m_strokes[idx]);
            sourceIds.append(m_strokes[idx].id);
        }

        const QString text = recognizeStrokesNative(lineStrokes);
        if (text.isEmpty())
            continue;

        Result r;
        r.text = text;
        r.boundingRect = group.boundingRect;
        r.confidence = 1.0f;
        r.sourceStrokeIds = sourceIds;
        results.append(r);
    }

    return results;
}

#endif // SPEEDYNOTE_HAS_MLKIT_INK
