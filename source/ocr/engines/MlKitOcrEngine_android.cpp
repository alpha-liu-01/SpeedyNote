#include "MlKitOcrEngine.h"

#if defined(SPEEDYNOTE_HAS_MLKIT_INK) && defined(Q_OS_ANDROID)

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <jni.h>

static const char* HELPER_CLASS = "org/speedynote/app/MlKitDigitalInkHelper";

// ---------------------------------------------------------------------------
// checkAvailabilityNative
// ---------------------------------------------------------------------------

bool MlKitOcrEngine::checkAvailabilityNative() const
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return false;

    return QJniObject::callStaticMethod<jboolean>(
        HELPER_CLASS, "isAvailable", "(Landroid/content/Context;)Z",
        context.object());
}

// ---------------------------------------------------------------------------
// queryLanguagesNative
// ---------------------------------------------------------------------------

QStringList MlKitOcrEngine::queryLanguagesNative() const
{
    QJniEnvironment env;
    QJniObject result = QJniObject::callStaticObjectMethod(
        HELPER_CLASS, "getAvailableLanguages", "()[Ljava/lang/String;");

    if (!result.isValid())
        return {};

    jobjectArray array = result.object<jobjectArray>();
    const int len = env->GetArrayLength(array);

    QStringList languages;
    languages.reserve(len);
    for (int i = 0; i < len; ++i) {
        jstring jstr = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        const char* chars = env->GetStringUTFChars(jstr, nullptr);
        languages.append(QString::fromUtf8(chars));
        env->ReleaseStringUTFChars(jstr, chars);
        env->DeleteLocalRef(jstr);
    }

    return languages;
}

// ---------------------------------------------------------------------------
// ensureModelDownloadedNative
// ---------------------------------------------------------------------------

bool MlKitOcrEngine::ensureModelDownloadedNative(const QString& languageTag)
{
    QJniObject tag = QJniObject::fromString(languageTag);
    return QJniObject::callStaticMethod<jboolean>(
        HELPER_CLASS, "ensureModelDownloaded", "(Ljava/lang/String;)Z",
        tag.object<jstring>());
}

// ---------------------------------------------------------------------------
// recognizeStrokesNative
// ---------------------------------------------------------------------------

QString MlKitOcrEngine::recognizeStrokesNative(const QVector<VectorStroke>& strokes)
{
    if (strokes.isEmpty())
        return {};

    // Count total points
    int totalPoints = 0;
    for (const auto& stroke : strokes)
        totalPoints += stroke.points.size();

    if (totalPoints == 0)
        return {};

    // Flatten strokes into float[totalPoints * 3] and int[numStrokes]
    QVector<float> flatPoints;
    flatPoints.reserve(totalPoints * 3);
    QVector<jint> strokeLengths;
    strokeLengths.reserve(strokes.size());

    // For legacy strokes (timestamp == 0), synthesize monotonically increasing timestamps
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int globalPointIdx = 0;

    for (const auto& stroke : strokes) {
        strokeLengths.append(stroke.points.size());

        bool needSyntheticTimestamps = false;
        for (const auto& pt : stroke.points) {
            if (pt.timestamp == 0) {
                needSyntheticTimestamps = true;
                break;
            }
        }

        for (int j = 0; j < stroke.points.size(); ++j) {
            const auto& pt = stroke.points[j];
            flatPoints.append(static_cast<float>(pt.pos.x()));
            flatPoints.append(static_cast<float>(pt.pos.y()));

            if (needSyntheticTimestamps) {
                flatPoints.append(static_cast<float>(now - (totalPoints - globalPointIdx) * 15));
            } else {
                flatPoints.append(static_cast<float>(pt.timestamp));
            }
            ++globalPointIdx;
        }
    }

    // Create JNI arrays and call Java
    QJniEnvironment env;

    jfloatArray jPoints = env->NewFloatArray(flatPoints.size());
    env->SetFloatArrayRegion(jPoints, 0, flatPoints.size(), flatPoints.constData());

    jintArray jLengths = env->NewIntArray(strokeLengths.size());
    env->SetIntArrayRegion(jLengths, 0, strokeLengths.size(), strokeLengths.constData());

    QJniObject langTag = QJniObject::fromString(m_languageTag);

    QJniObject result = QJniObject::callStaticObjectMethod(
        HELPER_CLASS, "recognize",
        "([F[ILjava/lang/String;)Ljava/lang/String;",
        jPoints, jLengths, langTag.object<jstring>());

    env->DeleteLocalRef(jPoints);
    env->DeleteLocalRef(jLengths);

    if (env.checkAndClearExceptions()) {
        qWarning() << "MlKitOcrEngine: JNI exception during recognize()";
        return {};
    }

    if (!result.isValid())
        return {};

    return result.toString();
}

#endif // SPEEDYNOTE_HAS_MLKIT_INK && Q_OS_ANDROID
