#ifdef SPEEDYNOTE_HAS_WINDOWS_INK

#include "WindowsInkOcrEngine.h"
#include "../OcrTextBlock.h"
#include "../../strokes/VectorStroke.h"

#include <QDebug>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <unordered_map>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.Inking.h>
#include <winrt/Windows.UI.Input.Inking.Analysis.h>

namespace winrt_ink = winrt::Windows::UI::Input::Inking;
namespace winrt_analysis = winrt::Windows::UI::Input::Inking::Analysis;

// ============================================================================
// Forward declaration of tree walkers
// ============================================================================

static void collectLines(
    const winrt_analysis::IInkAnalysisNode& node,
    const QHash<uint32_t, QString>& idMap,
    QVector<OcrEngine::Result>& out);

// Layout-only twin of collectLines: yields one stroke-id set per analysis
// line and ignores RecognizedText. Used by the manual-recognizer path, which
// wants the analyzer's line segmentation but not its choice of language.
static void collectLineStrokeIds(
    const winrt_analysis::IInkAnalysisNode& node,
    QVector<QVector<uint32_t>>& out);

// ============================================================================
// Vertical-overlap banding
// ============================================================================

// Groups rects into horizontal bands: a rect joins a band when it overlaps it
// vertically by more than half its own height. Returns groups of indices into
// the input, in top-to-bottom band order. Shared by the word-to-line grouping
// in the recognizer path and the single-band test that decides whether the
// caller already handed us one line.
static QVector<QVector<int>> groupRectsIntoBands(const QVector<QRectF>& rects)
{
    QVector<int> order(rects.size());
    for (int i = 0; i < rects.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return rects[a].center().y() < rects[b].center().y();
    });

    struct Band {
        QVector<int> indices;
        qreal top = 0, bottom = 0;
    };
    QVector<Band> bands;

    for (int idx : order) {
        const QRectF& r = rects[idx];
        bool merged = false;
        for (auto& band : bands) {
            const qreal overlap = qMax(0.0, qMin(r.bottom(), band.bottom)
                                              - qMax(r.top(), band.top));
            if (overlap > r.height() * 0.5) {
                band.indices.append(idx);
                band.top = qMin(band.top, r.top());
                band.bottom = qMax(band.bottom, r.bottom());
                merged = true;
                break;
            }
        }
        if (!merged)
            bands.append({{idx}, r.top(), r.bottom()});
    }

    QVector<QVector<int>> groups;
    groups.reserve(bands.size());
    for (const auto& band : bands)
        groups.append(band.indices);
    return groups;
}

// ============================================================================
// PIMPL
// ============================================================================

struct WindowsInkOcrEngine::Impl {
    bool available = false;
    bool apartmentInitialized = false;

    winrt_analysis::InkAnalyzer analyzer{nullptr};
    winrt_ink::InkStrokeContainer strokeContainer{nullptr};
    winrt_ink::InkStrokeBuilder strokeBuilder{nullptr};

    // Language-aware recognition via InkRecognizerContainer
    winrt_ink::InkRecognizerContainer recognizerContainer{nullptr};
    QVector<QPair<QString, winrt_ink::InkRecognizer>> recognizers;
    QString selectedLanguage;

    QHash<QString, uint32_t> uuidToWinrtId;
    QHash<uint32_t, QString> winrtIdToUuid;

    bool ensureApartment() {
        if (apartmentInitialized)
            return true;
        try {
            // MTA avoids STA deadlocks with InkRecognizerContainer.RecognizeAsync().
            // All APIs we use (InkAnalyzer, InkRecognizerContainer, InkStrokeBuilder)
            // are agile / Threading(Both), so MTA is safe.
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error&) {
        }
        apartmentInitialized = true;
        return true;
    }

    bool initialize() {
        if (!ensureApartment())
            return false;

        try {
            analyzer = winrt_analysis::InkAnalyzer();
            strokeContainer = winrt_ink::InkStrokeContainer();
            strokeBuilder = winrt_ink::InkStrokeBuilder();
            available = true;
        } catch (const winrt::hresult_class_not_registered&) {
            qWarning() << "WindowsInkOcrEngine: InkAnalyzer class not registered";
            available = false;
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: init failed:"
                       << QString::fromStdWString(std::wstring(e.message().c_str()));
            available = false;
        }

        try {
            recognizerContainer = winrt_ink::InkRecognizerContainer();
            auto recogList = recognizerContainer.GetRecognizers();
            for (uint32_t i = 0, sz = recogList.Size(); i < sz; ++i) {
                auto rec = recogList.GetAt(i);
                winrt::hstring hn = rec.Name();
                QString name = QString::fromWCharArray(hn.c_str(), static_cast<int>(hn.size()));
                recognizers.append({name, rec});
            }
            qDebug() << "WindowsInkOcrEngine:" << recognizers.size() << "recognizers available";
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: recognizer enumeration failed:"
                       << QString::fromStdWString(std::wstring(e.message().c_str()));
        }

        return available;
    }

    /// The strokes the container still holds that are also live in the id map.
    /// removeStrokes() cannot delete from an InkStrokeContainer, so the
    /// container accumulates erased strokes; filtering by the map is what keeps
    /// them out of recognition.
    // std::unordered_map rather than QHash: InkStroke has no default
    // constructor, which QHash's value access requires.
    std::unordered_map<uint32_t, winrt_ink::InkStroke> liveStrokes() const {
        std::unordered_map<uint32_t, winrt_ink::InkStroke> byId;
        if (!strokeContainer)
            return byId;
        auto all = strokeContainer.GetStrokes();
        if (!all)
            return byId;
        for (const auto& stroke : all) {
            const uint32_t id = stroke.Id();
            if (winrtIdToUuid.contains(id))
                byId.emplace(id, stroke);
        }
        return byId;
    }

    /// Recognize one group of strokes with the user's chosen recognizer.
    ///
    /// The strokes are cloned into a throwaway container: an InkStroke cannot
    /// live in two containers, and recognizing a subset in isolation is the
    /// whole point - handing a recognizer a full page is what made CJK collapse
    /// into a single result.
    QVector<OcrEngine::Result> recognizeGroup(
        const QVector<uint32_t>& groupIds,
        const std::unordered_map<uint32_t, winrt_ink::InkStroke>& strokeById)
    {
        QVector<OcrEngine::Result> results;
        if (groupIds.isEmpty())
            return results;

        winrt_ink::InkStrokeContainer temp{nullptr};
        QHash<uint32_t, uint32_t> cloneToOriginal;
        QVector<QString> groupUuids;

        try {
            temp = winrt_ink::InkStrokeContainer();
            for (uint32_t id : groupIds) {
                auto it = strokeById.find(id);
                if (it == strokeById.end())
                    continue;
                auto clone = it->second.Clone();
                temp.AddStroke(clone);
                cloneToOriginal.insert(clone.Id(), id);

                auto uuidIt = winrtIdToUuid.find(id);
                if (uuidIt != winrtIdToUuid.end())
                    groupUuids.append(uuidIt.value());
            }
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: group clone failed:"
                       << QString::fromWCharArray(e.message().c_str());
            return results;
        }

        if (groupUuids.isEmpty())
            return results;

        struct WordResult {
            QString text;
            QRectF rect;
            QVector<QString> strokeIds;
        };
        QVector<WordResult> words;

        try {
            auto recogResults = recognizerContainer.RecognizeAsync(
                temp, winrt_ink::InkRecognitionTarget::All).get();

            if (!recogResults || recogResults.Size() == 0)
                return results;

            for (uint32_t i = 0, sz = recogResults.Size(); i < sz; ++i) {
                auto recog = recogResults.GetAt(i);

                auto candidates = recog.GetTextCandidates();
                if (!candidates || candidates.Size() == 0) continue;
                winrt::hstring ht = candidates.GetAt(0);
                QString text = QString::fromWCharArray(ht.c_str(),
                                                       static_cast<int>(ht.size()));
                if (text.isEmpty()) continue;

                auto wr = recog.BoundingRect();
                QRectF rect(wr.X, wr.Y, wr.Width, wr.Height);

                // Map the clone ids the recognizer reports back onto our own
                // stroke uuids. If that mapping comes up empty the word still
                // belongs to this group, so fall back to the whole group rather
                // than dropping the attribution.
                QVector<QString> sids;
                auto strokes = recog.GetStrokes();
                if (strokes) {
                    for (const auto& stroke : strokes) {
                        auto origIt = cloneToOriginal.find(stroke.Id());
                        if (origIt == cloneToOriginal.end())
                            continue;
                        auto uuidIt = winrtIdToUuid.find(origIt.value());
                        if (uuidIt != winrtIdToUuid.end() && !sids.contains(uuidIt.value()))
                            sids.append(uuidIt.value());
                    }
                }
                if (sids.isEmpty())
                    sids = groupUuids;

                words.append({text, rect, sids});
            }
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: recognizer analyze failed:"
                       << QString::fromWCharArray(e.message().c_str());
            return results;
        }

        if (words.isEmpty())
            return results;

        QVector<QRectF> wordRects;
        wordRects.reserve(words.size());
        for (const auto& w : words)
            wordRects.append(w.rect);

        for (const auto& band : groupRectsIntoBands(wordRects)) {
            QVector<int> sorted = band;
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
                return words[a].rect.left() < words[b].rect.left();
            });

            OcrEngine::Result r;
            r.confidence = 1.0f;
            QRectF lineRect;

            for (int idx : sorted) {
                const auto& w = words[idx];

                if (!r.text.isEmpty()) {
                    bool needSpace = true;
                    if (isCjkLikeChar(r.text.back()) || isCjkLikeChar(w.text.front()))
                        needSpace = false;
                    if (needSpace)
                        r.text += QLatin1Char(' ');
                }
                r.text += w.text;

                lineRect = lineRect.isNull() ? w.rect : lineRect.united(w.rect);

                for (const auto& sid : w.strokeIds) {
                    if (!r.sourceStrokeIds.contains(sid))
                        r.sourceStrokeIds.append(sid);
                }

                OcrEngine::Result::WordSegment seg;
                seg.text = w.text;
                seg.boundingRect = w.rect;
                r.wordSegments.append(seg);
            }

            r.boundingRect = lineRect;
            if (!r.text.isEmpty())
                results.append(r);
        }

        return results;
    }
};

// ============================================================================
// Public API
// ============================================================================

WindowsInkOcrEngine::WindowsInkOcrEngine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->initialize();
}

WindowsInkOcrEngine::~WindowsInkOcrEngine() = default;

bool WindowsInkOcrEngine::isAvailable() const
{
    return m_impl && m_impl->available;
}

QStringList WindowsInkOcrEngine::availableLanguages() const
{
    QStringList names;
    if (!m_impl) return names;
    names.reserve(m_impl->recognizers.size());
    for (const auto& pair : m_impl->recognizers)
        names.append(pair.first);
    return names;
}

void WindowsInkOcrEngine::setLanguage(const QString& recognizerName)
{
    if (!m_impl) return;

    if (recognizerName.isEmpty() || recognizerName == QLatin1String("auto")) {
        m_impl->selectedLanguage.clear();
        return;
    }

    for (const auto& pair : m_impl->recognizers) {
        if (pair.first == recognizerName) {
            try {
                m_impl->recognizerContainer.SetDefaultRecognizer(pair.second);
                m_impl->selectedLanguage = recognizerName;
            } catch (const winrt::hresult_error& e) {
                qWarning() << "WindowsInkOcrEngine: SetDefaultRecognizer failed:"
                           << QString::fromStdWString(std::wstring(e.message().c_str()));
                m_impl->selectedLanguage.clear();
            }
            return;
        }
    }

    qWarning() << "WindowsInkOcrEngine: recognizer not found:" << recognizerName;
    m_impl->selectedLanguage.clear();
}

QString WindowsInkOcrEngine::language() const
{
    return m_impl ? m_impl->selectedLanguage : QString();
}

void WindowsInkOcrEngine::addStrokes(const QVector<VectorStroke>& strokes)
{
    if (!m_impl || !m_impl->available)
        return;

    for (const auto& stroke : strokes) {
        if (stroke.points.isEmpty())
            continue;

        try {
            auto inkPoints = winrt::single_threaded_vector<winrt_ink::InkPoint>();
            for (const auto& pt : stroke.points) {
                float pressure = static_cast<float>(pt.pressure);
                if (pressure < 0.1f) pressure = 0.1f;
                inkPoints.Append(winrt_ink::InkPoint(
                    winrt::Windows::Foundation::Point(
                        static_cast<float>(pt.pos.x()),
                        static_cast<float>(pt.pos.y())),
                    pressure));
            }

            winrt::Windows::Foundation::Numerics::float3x2 identity{
                1.0f, 0.0f,
                0.0f, 1.0f,
                0.0f, 0.0f
            };

            auto inkStroke = m_impl->strokeBuilder.CreateStrokeFromInkPoints(
                inkPoints.GetView(), identity);

            // Set pen tip size to match the actual stroke width.
            // The default 2x2 tip makes the InkAnalyzer underestimate
            // spatial coverage, fragmenting CJK character grouping.
            auto attr = inkStroke.DrawingAttributes();
            float tipSize = qMax(2.0f, static_cast<float>(stroke.baseThickness));
            attr.Size(winrt::Windows::Foundation::Size(tipSize, tipSize));
            inkStroke.DrawingAttributes(attr);

            m_impl->strokeContainer.AddStroke(inkStroke);
            m_impl->analyzer.AddDataForStroke(inkStroke);

            uint32_t winrtId = inkStroke.Id();
            m_impl->uuidToWinrtId[stroke.id] = winrtId;
            m_impl->winrtIdToUuid[winrtId] = stroke.id;

        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: addStroke failed:"
                       << QString::fromStdWString(std::wstring(e.message().c_str()));
        }
    }
}

void WindowsInkOcrEngine::removeStrokes(const QVector<QString>& strokeIds)
{
    if (!m_impl || !m_impl->available)
        return;

    for (const auto& uuid : strokeIds) {
        auto it = m_impl->uuidToWinrtId.find(uuid);
        if (it == m_impl->uuidToWinrtId.end())
            continue;

        try {
            m_impl->analyzer.RemoveDataForStroke(it.value());
            m_impl->winrtIdToUuid.remove(it.value());
            m_impl->uuidToWinrtId.erase(it);
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: removeStroke failed:"
                       << QString::fromStdWString(std::wstring(e.message().c_str()));
        }
    }
}

void WindowsInkOcrEngine::clearStrokes()
{
    if (!m_impl || !m_impl->available)
        return;

    try {
        // Recreate both analyzer and container from scratch.
        // ClearDataForAllStrokes() only removes stroke data but leaves the
        // spatial layout tree (WritingRegion/Paragraph/Line rects) intact,
        // which biases subsequent analyses toward old text regions.
        m_impl->analyzer = winrt_analysis::InkAnalyzer();
        m_impl->strokeContainer = winrt_ink::InkStrokeContainer();
        m_impl->uuidToWinrtId.clear();
        m_impl->winrtIdToUuid.clear();
    } catch (const winrt::hresult_error& e) {
        qWarning() << "WindowsInkOcrEngine: clearStrokes failed:"
                   << QString::fromStdWString(std::wstring(e.message().c_str()));
    }
}

// CJK detection for the space-joining logic is shared via isCjkLikeChar
// (OcrTextBlock.h) so all OCR paths use the same range definitions.

QVector<OcrEngine::Result> WindowsInkOcrEngine::analyzeWithRecognizer()
{
    QVector<Result> results;

    if (!m_impl || !m_impl->recognizerContainer || m_impl->uuidToWinrtId.isEmpty())
        return results;

    const auto strokeById = m_impl->liveStrokes();
    if (strokeById.empty())
        return results;

    QVector<uint32_t> liveIds;
    QVector<QRectF> strokeRects;
    liveIds.reserve(static_cast<int>(strokeById.size()));
    strokeRects.reserve(static_cast<int>(strokeById.size()));
    for (const auto& entry : strokeById) {
        liveIds.append(entry.first);
        const auto rect = entry.second.BoundingRect();
        strokeRects.append(QRectF(rect.X, rect.Y, rect.Width, rect.Height));
    }

    // A recognizer segments a page far worse than the analyzer does - the CJK
    // one returns the whole page as a single result - so ask the analyzer where
    // the lines are and recognize each one on its own.
    QVector<QVector<uint32_t>> groups;

    if (liveIds.size() < 2 || groupRectsIntoBands(strokeRects).size() <= 1) {
        // The caller already handed us a single line (OcrWorker's snap-group
        // mode does exactly this). Running the analyzer would only add a
        // round-trip and risk splitting a group the caller deliberately formed.
        groups.append(liveIds);
    } else {
        try {
            m_impl->analyzer.AnalyzeAsync().get();
            auto root = m_impl->analyzer.AnalysisRoot();
            if (root) {
                QVector<QVector<uint32_t>> lineGroups;
                for (const auto& child : root.Children())
                    collectLineStrokeIds(child, lineGroups);

                for (auto& line : lineGroups) {
                    QVector<uint32_t> live;
                    for (uint32_t id : line) {
                        if (strokeById.count(id))
                            live.append(id);
                    }
                    if (!live.isEmpty())
                        groups.append(live);
                }
            }
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WindowsInkOcrEngine: line analysis failed:"
                       << QString::fromWCharArray(e.message().c_str());
        }

        // Analyzer found no writing (everything classified as drawing, or the
        // analysis failed). Recognize the page as one group rather than
        // returning nothing.
        if (groups.isEmpty())
            groups.append(liveIds);
    }

    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "WindowsInkOcrEngine: recognizing" << groups.size() << "group(s),"
             << liveIds.size() << "strokes, recognizer:" << m_impl->selectedLanguage;
    #endif

    for (const auto& group : groups)
        results += m_impl->recognizeGroup(group, strokeById);

    return results;
}

QVector<OcrEngine::Result> WindowsInkOcrEngine::analyzeWithInkAnalyzer()
{
    QVector<Result> results;

    if (!m_impl || !m_impl->available || m_impl->uuidToWinrtId.isEmpty())
        return results;

    try {
        auto analysisResult = m_impl->analyzer.AnalyzeAsync().get();
        auto status = analysisResult.Status();
        Q_UNUSED(status);

        auto root = m_impl->analyzer.AnalysisRoot();
        if (!root)
            return results;

        for (const auto& child : root.Children())
            collectLines(child, m_impl->winrtIdToUuid, results);

        int recognizedStrokes = 0;
        for (const auto& r : results)
            recognizedStrokes += r.sourceStrokeIds.size();
        if (recognizedStrokes < m_impl->uuidToWinrtId.size())
            qDebug() << "WindowsInkOcrEngine: only" << recognizedStrokes
                     << "of" << m_impl->uuidToWinrtId.size()
                     << "strokes appear in recognized text results";

    } catch (const winrt::hresult_error& e) {
        qWarning() << "WindowsInkOcrEngine: analyze failed:"
                   << QString::fromWCharArray(e.message().c_str());
    }

    return results;
}

QVector<OcrEngine::Result> WindowsInkOcrEngine::analyze()
{
    if (m_impl && !m_impl->selectedLanguage.isEmpty())
        return analyzeWithRecognizer();
    return analyzeWithInkAnalyzer();
}

// ============================================================================
// Analysis tree walker — extracts at line level for better accuracy and visuals
// ============================================================================

static void collectLines(
    const winrt_analysis::IInkAnalysisNode& node,
    const QHash<uint32_t, QString>& idMap,
    QVector<OcrEngine::Result>& out)
{
    if (!node)
        return;

    try {
        auto kind = node.Kind();

        if (kind == winrt_analysis::InkAnalysisNodeKind::Line) {
            auto line = node.try_as<winrt_analysis::InkAnalysisLine>();
            if (!line)
                return;

            OcrEngine::Result r;

            winrt::hstring htext = line.RecognizedText();
            r.text = QString::fromWCharArray(htext.c_str(), static_cast<int>(htext.size()));

            if (r.text.isEmpty())
                return;

            auto rect = node.BoundingRect();
            r.boundingRect = QRectF(rect.X, rect.Y, rect.Width, rect.Height);

            auto strokeIds = node.GetStrokeIds();
            if (strokeIds) {
                for (const auto& winrtId : strokeIds) {
                    auto it = idMap.find(winrtId);
                    if (it != idMap.end())
                        r.sourceStrokeIds.append(it.value());
                }
            }

            r.confidence = 1.0f;

            auto lineChildren = node.Children();
            if (lineChildren) {
                for (uint32_t ci = 0, csz = lineChildren.Size(); ci < csz; ++ci) {
                    auto child = lineChildren.GetAt(ci);
                    if (!child) continue;
                    if (child.Kind() != winrt_analysis::InkAnalysisNodeKind::InkWord)
                        continue;
                    auto word = child.try_as<winrt_analysis::InkAnalysisInkWord>();
                    if (!word) continue;
                    winrt::hstring wt = word.RecognizedText();
                    auto wr = child.BoundingRect();
                    OcrEngine::Result::WordSegment seg;
                    seg.text = QString::fromWCharArray(wt.c_str(), static_cast<int>(wt.size()));
                    seg.boundingRect = QRectF(wr.X, wr.Y, wr.Width, wr.Height);
                    r.wordSegments.append(seg);
                }
            }

            out.append(r);
            return;
        }

        // Fallback: for InkWord nodes not under a Line (e.g. unclassified ink),
        // collect at word level so we don't silently drop results.
        if (kind == winrt_analysis::InkAnalysisNodeKind::InkWord) {
            auto word = node.try_as<winrt_analysis::InkAnalysisInkWord>();
            if (!word)
                return;

            OcrEngine::Result r;

            winrt::hstring htext = word.RecognizedText();
            r.text = QString::fromWCharArray(htext.c_str(), static_cast<int>(htext.size()));

            if (r.text.isEmpty())
                return;

            auto rect = node.BoundingRect();
            r.boundingRect = QRectF(rect.X, rect.Y, rect.Width, rect.Height);

            auto strokeIds = node.GetStrokeIds();
            if (strokeIds) {
                for (const auto& winrtId : strokeIds) {
                    auto it = idMap.find(winrtId);
                    if (it != idMap.end())
                        r.sourceStrokeIds.append(it.value());
                }
            }

            r.confidence = 1.0f;
            out.append(r);
            return;
        }

        // Recurse into children (WritingRegion, Paragraph, etc.) to find Lines
        auto children = node.Children();
        if (!children)
            return;

        for (uint32_t i = 0, sz = children.Size(); i < sz; ++i) {
            auto child = children.GetAt(i);
            if (child)
                collectLines(child, idMap, out);
        }
    } catch (const winrt::hresult_error& e) {
        qWarning() << "collectLines: WinRT error:"
                   << QString::fromWCharArray(e.message().c_str());
    } catch (...) {
        qWarning() << "collectLines: unknown exception";
    }
}

static void collectLineStrokeIds(
    const winrt_analysis::IInkAnalysisNode& node,
    QVector<QVector<uint32_t>>& out)
{
    if (!node)
        return;

    try {
        if (node.Kind() == winrt_analysis::InkAnalysisNodeKind::Line) {
            QVector<uint32_t> ids;
            auto strokeIds = node.GetStrokeIds();
            if (strokeIds) {
                for (const auto& winrtId : strokeIds)
                    ids.append(winrtId);
            }
            if (!ids.isEmpty())
                out.append(ids);
            return;
        }

        auto children = node.Children();
        if (!children)
            return;

        for (uint32_t i = 0, sz = children.Size(); i < sz; ++i) {
            auto child = children.GetAt(i);
            if (child)
                collectLineStrokeIds(child, out);
        }
    } catch (const winrt::hresult_error& e) {
        qWarning() << "collectLineStrokeIds: WinRT error:"
                   << QString::fromWCharArray(e.message().c_str());
    } catch (...) {
        qWarning() << "collectLineStrokeIds: unknown exception";
    }
}

#endif // SPEEDYNOTE_HAS_WINDOWS_INK
