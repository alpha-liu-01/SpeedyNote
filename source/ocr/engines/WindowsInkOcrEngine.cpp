#ifdef SPEEDYNOTE_HAS_WINDOWS_INK

#include "WindowsInkOcrEngine.h"
#include "../../strokes/VectorStroke.h"

#include <QDebug>
#include <QHash>
#include <QStringList>
#include <algorithm>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.Inking.h>
#include <winrt/Windows.UI.Input.Inking.Analysis.h>

namespace winrt_ink = winrt::Windows::UI::Input::Inking;
namespace winrt_analysis = winrt::Windows::UI::Input::Inking::Analysis;

// ============================================================================
// Forward declaration of tree walker
// ============================================================================

static void collectLines(
    const winrt_analysis::IInkAnalysisNode& node,
    const QHash<uint32_t, QString>& idMap,
    QVector<OcrEngine::Result>& out);

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

bool WindowsInkOcrEngine::supportsIncrementalUpdates() const
{
    // InkRecognizerContainer reads directly from InkStrokeContainer which has
    // no per-stroke removal API, so incremental remove+analyze still sees
    // "deleted" strokes when a non-default recognizer is active.
    return !m_impl || m_impl->selectedLanguage.isEmpty();
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

static bool isCjkChar(QChar ch)
{
    ushort u = ch.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF)
        || (u >= 0x3400 && u <= 0x4DBF)
        || (u >= 0xF900 && u <= 0xFAFF)
        || (u >= 0x3000 && u <= 0x303F)
        || (u >= 0x3040 && u <= 0x309F)
        || (u >= 0x30A0 && u <= 0x30FF);
}

QVector<OcrEngine::Result> WindowsInkOcrEngine::analyzeWithRecognizer()
{
    QVector<Result> results;

    if (!m_impl || !m_impl->recognizerContainer || m_impl->uuidToWinrtId.isEmpty())
        return results;

    try {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "WindowsInkOcrEngine: RecognizeAsync starting, strokes:"
                 << m_impl->uuidToWinrtId.size()
                 << "recognizer:" << m_impl->selectedLanguage;
        #endif

        auto recogResults = m_impl->recognizerContainer.RecognizeAsync(
            m_impl->strokeContainer,
            winrt_ink::InkRecognitionTarget::All).get();

        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "WindowsInkOcrEngine: RecognizeAsync completed,"
                 << (recogResults ? recogResults.Size() : 0) << "results";
        #endif

        if (!recogResults || recogResults.Size() == 0)
            return results;

        struct WordResult {
            QString text;
            QRectF rect;
            QVector<QString> strokeIds;
        };
        QVector<WordResult> words;

        for (uint32_t i = 0, sz = recogResults.Size(); i < sz; ++i) {
            auto recog = recogResults.GetAt(i);

            auto candidates = recog.GetTextCandidates();
            if (!candidates || candidates.Size() == 0) continue;
            winrt::hstring ht = candidates.GetAt(0);
            QString text = QString::fromWCharArray(ht.c_str(), static_cast<int>(ht.size()));
            if (text.isEmpty()) continue;

            auto wr = recog.BoundingRect();
            QRectF rect(wr.X, wr.Y, wr.Width, wr.Height);

            QVector<QString> sids;
            auto strokes = recog.GetStrokes();
            if (strokes) {
                for (const auto& stroke : strokes) {
                    uint32_t wid = stroke.Id();
                    auto it = m_impl->winrtIdToUuid.find(wid);
                    if (it != m_impl->winrtIdToUuid.end())
                        sids.append(it.value());
                }
            }

            words.append({text, rect, sids});
        }

        if (words.isEmpty())
            return results;

        // Group word results into lines by vertical overlap.
        // Sort by Y midpoint first.
        std::sort(words.begin(), words.end(), [](const WordResult& a, const WordResult& b) {
            return a.rect.center().y() < b.rect.center().y();
        });

        struct LineGroup {
            QVector<int> indices;
            qreal top = 0, bottom = 0;
        };
        QVector<LineGroup> lines;

        for (int i = 0; i < words.size(); ++i) {
            const auto& w = words[i];
            qreal wTop = w.rect.top();
            qreal wBottom = w.rect.bottom();
            qreal wH = w.rect.height();

            bool merged = false;
            for (auto& line : lines) {
                qreal overlapTop = qMax(wTop, line.top);
                qreal overlapBottom = qMin(wBottom, line.bottom);
                qreal overlap = qMax(0.0, overlapBottom - overlapTop);
                if (overlap > wH * 0.5) {
                    line.indices.append(i);
                    line.top = qMin(line.top, wTop);
                    line.bottom = qMax(line.bottom, wBottom);
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                LineGroup g;
                g.indices.append(i);
                g.top = wTop;
                g.bottom = wBottom;
                lines.append(g);
            }
        }

        // Build OcrEngine::Result for each line
        for (const auto& line : lines) {
            // Sort words within this line by X
            QVector<int> sorted = line.indices;
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
                return words[a].rect.left() < words[b].rect.left();
            });

            Result r;
            r.confidence = 1.0f;
            QRectF lineRect;

            for (int idx : sorted) {
                const auto& w = words[idx];

                if (!r.text.isEmpty()) {
                    bool needSpace = true;
                    if (isCjkChar(r.text.back()) || isCjkChar(w.text.front()))
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

                Result::WordSegment seg;
                seg.text = w.text;
                seg.boundingRect = w.rect;
                r.wordSegments.append(seg);
            }

            r.boundingRect = lineRect;
            if (!r.text.isEmpty())
                results.append(r);
        }

    } catch (const winrt::hresult_error& e) {
        qWarning() << "WindowsInkOcrEngine: recognizer analyze failed:"
                   << QString::fromWCharArray(e.message().c_str());
    }

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

#endif // SPEEDYNOTE_HAS_WINDOWS_INK
