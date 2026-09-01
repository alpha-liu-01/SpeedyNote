#include "PaddleOcrEngine.h"

#ifdef SPEEDYNOTE_HAS_PADDLE_OCR

// ============================================================================
// PaddleOcrEngine (Linux) - PP-OCRv5 recognition via ONNX Runtime (CPU EP).
// ============================================================================
// Implements the single RasterOcrEngine bridge, recognizeImage():
//   normalized strip -> resize/normalize/CHW tensor -> Ort::Session::Run ->
//   greedy CTC decode -> text + approximate per-character X boxes.
//
// The character dictionary is read from the ONNX model metadata (key
// "character"; RapidOCR convention), then the PaddleOCR CTC label table is
// built as: index 0 = "blank" (ignored), 1..N = dict chars, N+1 = " ".
// Greedy decode collapses consecutive duplicates and drops blanks (matching
// PaddleOCR / RapidOCR CTCLabelDecode).
// ============================================================================

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// PIMPL: a single shared Ort::Env for the engine.
// ----------------------------------------------------------------------------
struct PaddleOcrEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "speedynote-paddle-ocr"};
};

// ----------------------------------------------------------------------------
// One loaded recognition model: session + IO names + decoded CTC char table.
// ----------------------------------------------------------------------------
struct PaddleOcrEngine::Model {
    Ort::Session session{nullptr};
    std::string inputName;
    std::string outputName;
    int recHeight = 48;            ///< fixed model input height (px)
    QVector<QString> charTable;    ///< index 0 = blank; size == num classes (ideally)

    explicit Model(Ort::Session&& s) : session(std::move(s)) {}
};

namespace {

constexpr int kMinStripWidth = 16;
constexpr int kMaxStripWidth = 4096;

// ----------------------------------------------------------------------------
// Model catalog (Phase 4D). RapidAI/RapidOCR pre-converted PP-OCRv5 *mobile*
// recognition models. SHAs and the base URL match linux/fetch-ocr-models.sh
// (the 3 bundled models) plus the additional scripts published in RapidOCR's
// default_models.yaml v3.8.0. Each model embeds its own character dictionary,
// so a download is a single .onnx per script -- no separate dict files.
// ----------------------------------------------------------------------------
const QString kModelScopeBase =
    QStringLiteral("https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.8.0/onnx/PP-OCRv5/rec");
const QString kHuggingFaceBase =
    QStringLiteral("https://huggingface.co/RapidAI/RapidOCR/resolve/v3.8.0/onnx/PP-OCRv5/rec");

struct DownloadInfo {
    const char* localName;   ///< on-disk file name used by the resolver
    const char* remoteFile;  ///< file name on ModelScope / HuggingFace
    const char* sha256;      ///< pinned digest (lower-case hex)
};

// All recognition models the engine knows how to obtain. The first three are
// bundled by packaging; the rest are downloaded on demand.
const DownloadInfo kDownloadCatalog[] = {
    {"latin_rec.onnx",      "latin_PP-OCRv5_rec_mobile.onnx",      "b20bd37c168a570f583afbc8cd7925603890efbcdc000a59e22c269d160b5f5a"},
    {"ch_rec.onnx",         "ch_PP-OCRv5_rec_mobile.onnx",         "5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5"},
    {"korean_rec.onnx",     "korean_PP-OCRv5_rec_mobile.onnx",     "cd6e2ea50f6943ca7271eb8c56a877a5a90720b7047fe9c41a2e541a25773c9b"},
    {"cyrillic_rec.onnx",   "cyrillic_PP-OCRv5_rec_mobile.onnx",   "90f761b4bfcce0c8c561c0cb5c887b0971d3ec01c32164bdf7374a35b0982711"},
    {"arabic_rec.onnx",     "arabic_PP-OCRv5_rec_mobile.onnx",     "c1192e632d0baa9146ae5b756a0e635e3dc63c1733737ebfd1629e87144e9295"},
    {"devanagari_rec.onnx", "devanagari_PP-OCRv5_rec_mobile.onnx", "d6f0a906580e3fa6b324a318718f1f31f268b6ea8ef985f91c2012a37f52c91e"},
};

// Representative language tags surfaced to the UI, each mapped to its script
// model. Listed regardless of whether the model is present on disk yet, so the
// user can select one and trigger an on-demand download (mirrors ML Kit, which
// advertises every supported language whether or not it is downloaded).
struct CatalogLang {
    const char* tag;
    const char* file;
};
const CatalogLang kLanguageCatalog[] = {
    {"en-US", "latin_rec.onnx"},
    {"zh-CN", "ch_rec.onnx"},
    {"ja-JP", "ch_rec.onnx"},
    {"ko-KR", "korean_rec.onnx"},
    {"ru-RU", "cyrillic_rec.onnx"},
    {"uk-UA", "cyrillic_rec.onnx"},
    {"ar-SA", "arabic_rec.onnx"},
    {"hi-IN", "devanagari_rec.onnx"},
};

const DownloadInfo* catalogEntryFor(const QString& localName)
{
    const QByteArray name = localName.toUtf8();
    for (const auto& e : kDownloadCatalog) {
        if (name == e.localName)
            return &e;
    }
    return nullptr;
}

bool tagStartsWithAny(const QString& tag, std::initializer_list<const char*> prefixes)
{
    for (const char* p : prefixes) {
        if (tag.startsWith(QLatin1String(p)))
            return true;
    }
    return false;
}

// Blocking HTTP GET into memory that does NOT run the calling thread's event
// loop. The network I/O (QNetworkAccessManager + its own QEventLoop) runs on a
// private thread; the caller blocks on a semaphore. This is essential because
// the download is triggered from inside RasterOcrEngine::analyze() on the OCR
// worker thread -- pumping that thread's event loop here would dispatch queued
// OcrWorker slots (processPage/setLanguage/...) reentrantly into the engine
// mid-analysis. A transfer timeout bounds a stalled server so the worker can
// never hang permanently.
QByteArray blockingHttpGet(const QString& url, int timeoutMs, bool* ok)
{
    QByteArray payload;
    bool success = false;

    QThread* thread = QThread::create([&]() {
        QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(timeoutMs);

        QNetworkReply* reply = nam.get(req);
        QEventLoop loop; // local to this private thread
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            payload = reply->readAll();
            success = true;
        }
        delete reply;
    });

    thread->start();
    thread->wait(); // blocks WITHOUT pumping this thread's event loop
    delete thread;

    if (ok)
        *ok = success;
    return payload;
}

// Build the PaddleOCR CTC label table from a model's "character" metadata.
// Returns {} when the metadata is absent. Layout: ["blank"] + dict + [" "].
QVector<QString> buildCharTable(Ort::Session& session)
{
    QVector<QString> table;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::ModelMetadata md = session.GetModelMetadata();
    Ort::AllocatedStringPtr value =
        md.LookupCustomMetadataMapAllocated("character", allocator);
    if (!value)
        return table; // no embedded dict

    QString dict = QString::fromUtf8(value.get());
    // Match Python str.splitlines() semantics (which RapidOCR/PaddleOCR rely on
    // when building the label table): a terminating newline must NOT yield a
    // trailing empty entry. These dicts are stored newline-terminated, so a
    // naive QString::split('\n') would add one spurious "" element -- shifting
    // every class after it by one, which silently steals the CTC *space* token
    // (the appended " " below) and makes Latin words run together. Strip one
    // trailing line terminator ("\n" or "\r\n") first.
    if (dict.endsWith(QLatin1Char('\n')))
        dict.chop(1);
    if (dict.endsWith(QLatin1Char('\r')))
        dict.chop(1);
    const QStringList lines = dict.split(QLatin1Char('\n'));

    table.reserve(lines.size() + 2);
    table.append(QString());            // index 0 = blank (never emitted)
    for (const QString& raw : lines) {
        QString ch = raw;
        if (ch.endsWith(QLatin1Char('\r')))
            ch.chop(1);
        table.append(ch);
    }
    table.append(QStringLiteral(" "));  // PaddleOCR appends the space token
    return table;
}

} // namespace

PaddleOcrEngine::PaddleOcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

PaddleOcrEngine::~PaddleOcrEngine() = default;

QString PaddleOcrEngine::writableModelsDir() const
{
    // XDG data location: ~/.local/share/SpeedyNote/ocr-models (the app name is
    // taken from QCoreApplication, so this honors any override). Downloads land
    // here; it shadows the read-only install in modelSearchDirs().
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    return base + QStringLiteral("/ocr-models");
}

QStringList PaddleOcrEngine::modelSearchDirs() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList dirs;
    // Writable XDG dir first so on-demand downloads win over a stale bundle.
    const QString writable = writableModelsDir();
    if (!writable.isEmpty())
        dirs << writable;
    dirs << appDir + QStringLiteral("/ocr-models")
         << appDir + QStringLiteral("/../share/speedynote/ocr-models")
         << QStringLiteral("/usr/share/speedynote/ocr-models")
         << QStringLiteral("/usr/local/share/speedynote/ocr-models")
         // Dev tree: linux/ocr-models relative to the build output.
         << appDir + QStringLiteral("/../linux/ocr-models")
         << appDir + QStringLiteral("/../../linux/ocr-models");
    return dirs;
}

QString PaddleOcrEngine::findModelFile(const QString& fileName) const
{
    for (const QString& dir : modelSearchDirs()) {
        const QString path = dir + QLatin1Char('/') + fileName;
        if (QFileInfo::exists(path))
            return QDir(dir).absoluteFilePath(fileName);
    }
    return {};
}

QString PaddleOcrEngine::modelFileForLanguage(const QString& languageTag)
{
    const QString tag = languageTag.toLower();
    if (tag.startsWith(QStringLiteral("zh")) || tag.startsWith(QStringLiteral("ja")))
        return QStringLiteral("ch_rec.onnx");   // v5 ch model is multilingual (zh/en/ja/cht)
    if (tag.startsWith(QStringLiteral("ko")))
        return QStringLiteral("korean_rec.onnx");
    // Cyrillic-script languages (Russian, Ukrainian, Belarusian, Bulgarian,
    // Serbian, Macedonian, Mongolian-Cyrillic, Kazakh, Kyrgyz, Tajik, ...).
    if (tagStartsWithAny(tag, {"ru", "uk", "be", "bg", "sr", "mk", "mn", "kk", "ky", "tg", "ab"}))
        return QStringLiteral("cyrillic_rec.onnx");
    // Arabic-script languages (Arabic, Persian, Urdu, Uyghur, Pashto, Sindhi).
    if (tagStartsWithAny(tag, {"ar", "fa", "ur", "ug", "ps", "sd"}))
        return QStringLiteral("arabic_rec.onnx");
    // Devanagari-script languages (Hindi, Marathi, Nepali, Sanskrit, ...).
    if (tagStartsWithAny(tag, {"hi", "mr", "ne", "sa", "bh", "kok"}))
        return QStringLiteral("devanagari_rec.onnx");
    return QStringLiteral("latin_rec.onnx");    // default (en-US + Latin scripts)
}

bool PaddleOcrEngine::isAvailable() const
{
    // The vendored ONNX Runtime is link-time guaranteed when this TU is built;
    // availability hinges on the mandatory default (latin) model existing in
    // any of the search dirs (bundle or a previous download).
    return !findModelFile(QStringLiteral("latin_rec.onnx")).isEmpty();
}

QStringList PaddleOcrEngine::availableLanguages() const
{
    // Report the full supported catalog (bundled + downloadable) so the
    // language pickers expose every script; selecting one that is not yet
    // present triggers an on-demand download in modelForLanguage().
    if (findModelFile(QStringLiteral("latin_rec.onnx")).isEmpty())
        return {}; // engine not usable at all without the mandatory default

    QStringList langs;
    for (const auto& l : kLanguageCatalog) {
        const QString tag = QString::fromUtf8(l.tag);
        if (!langs.contains(tag))
            langs << tag;
    }
    return langs;
}

QStringList PaddleOcrEngine::downloadedLanguages() const
{
    // Only the catalog languages whose model file is actually present in a
    // search dir (bundled or previously downloaded). The UI uses this to flag
    // the remaining languages as needing a download.
    QStringList langs;
    for (const auto& l : kLanguageCatalog) {
        const QString tag = QString::fromUtf8(l.tag);
        if (langs.contains(tag))
            continue;
        if (!findModelFile(QString::fromUtf8(l.file)).isEmpty())
            langs << tag;
    }
    return langs;
}

bool PaddleOcrEngine::ensureModelDownloaded(const QString& fileName)
{
    const DownloadInfo* info = catalogEntryFor(fileName);
    if (!info)
        return false; // not a known/downloadable model

    const QString destDir = writableModelsDir();
    if (destDir.isEmpty())
        return false;
    if (!QDir().mkpath(destDir))
        return false;
    const QString destPath = destDir + QLatin1Char('/') + fileName;

    // Friendly script name for the status line ("cyrillic" from "cyrillic_rec.onnx").
    QString script = fileName;
    script.replace(QStringLiteral("_rec.onnx"), QString());
    reportStatus(QObject::tr("Downloading %1 OCR model...").arg(script));

    constexpr int kTransferTimeoutMs = 60000; // bound a stalled mirror
    const QStringList urls = {
        kModelScopeBase + QLatin1Char('/') + QString::fromUtf8(info->remoteFile),
        kHuggingFaceBase + QLatin1Char('/') + QString::fromUtf8(info->remoteFile),
    };

    for (const QString& url : urls) {
        bool ok = false;
        const QByteArray data = blockingHttpGet(url, kTransferTimeoutMs, &ok);
        if (!ok || data.isEmpty())
            continue; // try the next mirror

        const QByteArray digest =
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        if (digest != QByteArray(info->sha256))
            continue; // corrupted / wrong asset -> try the next mirror

        // Atomic publish: QSaveFile writes to a temp file then renames on commit.
        QSaveFile f(destPath);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(data);
        if (!f.commit())
            continue;
        return true;
    }

    reportStatus(QObject::tr("OCR model download failed"));
    return false;
}

void PaddleOcrEngine::setLanguage(const QString& recognizerName)
{
    const QString prev = language();
    RasterOcrEngine::setLanguage(recognizerName);
    // Only a genuine language change resets the negative-download cache, so
    // repeated scans of the same (unavailable) language don't keep retrying.
    if (language() != prev)
        m_downloadFailed.clear();
}

PaddleOcrEngine::Model* PaddleOcrEngine::modelForLanguage(const QString& languageTag)
{
    QString file = modelFileForLanguage(languageTag);

    // Cache hit: avoid all filesystem / network I/O on the hot path.
    auto it = m_models.find(file);
    if (it != m_models.end())
        return it->second.get();

    QString path = findModelFile(file);
    if (path.isEmpty() && !m_downloadFailed.contains(file)) {
        // Not present locally -> try an on-demand download into the writable dir.
        // A failure is remembered for the rest of the session so a multi-line
        // page (or repeated scans) doesn't re-hit the network for every line;
        // the cache is cleared in setLanguage() when the language actually
        // changes, which gives the user a way to retry.
        if (ensureModelDownloaded(file))
            path = findModelFile(file);
        else
            m_downloadFailed.insert(file);
    }
    if (path.isEmpty()) {
        // Fall back to the mandatory default (latin) model.
        file = QStringLiteral("latin_rec.onnx");
        auto lit = m_models.find(file);
        if (lit != m_models.end())
            return lit->second.get();
        path = findModelFile(file);
        if (path.isEmpty())
            return nullptr;
    }

    try {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetIntraOpNumThreads(1); // tiny single-line model; avoid oversubscription

        const std::string pathStd = path.toStdString();
        Ort::Session session(m_impl->env, pathStd.c_str(), opts);

        auto model = std::make_unique<Model>(std::move(session));

        Ort::AllocatorWithDefaultOptions allocator;
        Ort::AllocatedStringPtr inName  = model->session.GetInputNameAllocated(0, allocator);
        Ort::AllocatedStringPtr outName = model->session.GetOutputNameAllocated(0, allocator);
        model->inputName  = inName.get();
        model->outputName = outName.get();

        // Fixed input height if the model declares one (NCHW: dims[2]).
        const std::vector<int64_t> inShape =
            model->session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inShape.size() == 4 && inShape[2] > 0)
            model->recHeight = static_cast<int>(inShape[2]);

        model->charTable = buildCharTable(model->session);
        if (model->charTable.isEmpty())
            return nullptr; // can't decode without a dictionary

        Model* raw = model.get();
        m_models[file] = std::move(model);
        return raw;
    } catch (const Ort::Exception&) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

RasterOcrEngine::ImageRecognition
PaddleOcrEngine::recognizeImage(const QImage& strip, const QString& languageTag)
{
    ImageRecognition out;
    if (strip.isNull())
        return out;

    Model* model = modelForLanguage(languageTag);
    if (!model)
        return out;

    // --- 1. Preprocess: Grayscale8 strip -> resized RGB CHW float tensor. ---
    const QImage gray = strip.convertToFormat(QImage::Format_Grayscale8);
    const int stripW = gray.width();
    const int stripH = gray.height();
    if (stripW <= 0 || stripH <= 0)
        return out;

    const int H = model->recHeight;
    int W = static_cast<int>(std::lround(static_cast<double>(H) * stripW / stripH));
    W = std::clamp(W, kMinStripWidth, kMaxStripWidth);

    const QImage resized =
        gray.scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_Grayscale8);

    // CHW, 3 channels (grayscale replicated), PP-OCR normalize (x/255-0.5)/0.5.
    std::vector<float> input(static_cast<size_t>(3) * H * W);
    for (int y = 0; y < H; ++y) {
        const uchar* row = resized.constScanLine(y);
        for (int x = 0; x < W; ++x) {
            const float v = (static_cast<float>(row[x]) / 255.0f - 0.5f) / 0.5f;
            const size_t idx = static_cast<size_t>(y) * W + x;
            input[idx] = v;                                  // R
            input[static_cast<size_t>(H) * W + idx] = v;     // G
            input[static_cast<size_t>(2) * H * W + idx] = v; // B
        }
    }

    // --- 2. Run inference. --------------------------------------------------
    // Keep the output Ort::Value alive past the try so the CTC decoder can read
    // its logits in place -- copying the whole [T x C] buffer out would cost
    // tens of MB for wide lines with the large (C ~= 18k) CJK dictionary.
    std::vector<Ort::Value> outputs;
    try {
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 3, H, W};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, input.data(), input.size(), shape.data(), shape.size());

        const char* inNames[]  = {model->inputName.c_str()};
        const char* outNames[] = {model->outputName.c_str()};
        outputs = model->session.Run(Ort::RunOptions{nullptr},
                                     inNames, &inputTensor, 1, outNames, 1);
    } catch (const Ort::Exception&) {
        return out;
    } catch (...) {
        return out;
    }

    if (outputs.empty())
        return out;
    const std::vector<int64_t> oShape =
        outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (oShape.size() != 3)
        return out; // expect [1, T, C]
    const int T = static_cast<int>(oShape[1]);
    const int C = static_cast<int>(oShape[2]);
    if (T <= 0 || C <= 0)
        return out;
    const float* logits = outputs[0].GetTensorData<float>();

    // --- 3. Greedy CTC decode + per-char column. ----------------------------
    // Keep a token iff (it differs from the previous raw token) AND (not blank,
    // index 0) -- identical to PaddleOCR/RapidOCR CTCLabelDecode.
    struct Emit { QString ch; int col; };
    std::vector<Emit> emits;
    emits.reserve(T);

    // Mean winning-class probability over the emitted timesteps, which is how
    // PP-OCR derives its per-line score. Blank and repeat timesteps are skipped
    // so a long silent tail cannot inflate or deflate the average.
    double probSum = 0.0;
    int probCount = 0;

    int prevRaw = -1;
    for (int t = 0; t < T; ++t) {
        const float* p = logits + static_cast<size_t>(t) * C;
        int best = 0;
        float bestVal = p[0];
        for (int c = 1; c < C; ++c) {
            if (p[c] > bestVal) { bestVal = p[c]; best = c; }
        }
        if (best != prevRaw && best != 0) {
            const QString ch = (best < model->charTable.size())
                                   ? model->charTable[best]
                                   : QString();
            if (!ch.isEmpty()) {
                emits.push_back({ch, t});

                // Softmax of the winner, shifted by the max (which is bestVal)
                // for stability: p_best = 1 / sum(exp(p[c] - bestVal)).
                double denom = 0.0;
                for (int c = 0; c < C; ++c)
                    denom += std::exp(static_cast<double>(p[c] - bestVal));
                if (denom > 0.0) {
                    probSum += 1.0 / denom;
                    probCount += 1;
                }
            }
        }
        prevRaw = best;
    }

    if (emits.empty())
        return out;

    if (probCount > 0)
        out.confidence = static_cast<float>(probSum / probCount);

    // --- 4. Text + approximate per-char boxes in received-strip pixels. -----
    // CTC gives a meaningful column (good X); Y is weak so each box spans the
    // full strip height (QA Q11.2). Box edges are midpoints between adjacent
    // character centers, mapped from feature columns to strip width.
    QString text;
    QVector<QRectF> boxes;
    bool charBoxesValid = true;

    const int n = static_cast<int>(emits.size());
    std::vector<double> centers(n);
    for (int i = 0; i < n; ++i)
        centers[i] = (emits[i].col + 0.5) / static_cast<double>(T) * stripW;

    for (int i = 0; i < n; ++i) {
        const double left  = (i == 0)      ? 0.0    : (centers[i - 1] + centers[i]) / 2.0;
        const double right = (i == n - 1)  ? stripW : (centers[i] + centers[i + 1]) / 2.0;
        const QRectF box(left, 0.0, std::max(0.0, right - left), stripH);

        const QString& ch = emits[i].ch;
        text += ch;
        for (int k = 0; k < ch.length(); ++k)
            boxes.append(box);
        if (ch.length() != 1)
            charBoxesValid = false; // multi-codepoint token -> degrade to word rects
    }

    out.text = text;
    if (charBoxesValid && boxes.size() == text.length())
        out.charBoxesImage = boxes; // else base falls back to the word rect
    return out;
}

#endif // SPEEDYNOTE_HAS_PADDLE_OCR
