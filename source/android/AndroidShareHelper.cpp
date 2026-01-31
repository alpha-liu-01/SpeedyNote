#include "AndroidShareHelper.h"

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QDebug>

namespace AndroidShareHelper {

void shareFile(const QString& filePath, const QString& mimeType, const QString& chooserTitle)
{
    if (filePath.isEmpty()) {
        qWarning() << "AndroidShareHelper::shareFile: Empty file path";
        return;
    }
    
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "AndroidShareHelper::shareFile: Failed to get Android context";
        return;
    }
    
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/ShareHelper",
        "shareFileWithTitle",
        "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        activity.object<jobject>(),
        QJniObject::fromString(filePath).object<jstring>(),
        QJniObject::fromString(mimeType).object<jstring>(),
        QJniObject::fromString(chooserTitle).object<jstring>()
    );
    
    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidShareHelper::shareFile: JNI exception occurred";
    }
}

void shareMultipleFiles(const QStringList& filePaths, const QString& mimeType, const QString& chooserTitle)
{
    if (filePaths.isEmpty()) {
        qWarning() << "AndroidShareHelper::shareMultipleFiles: Empty file list";
        return;
    }
    
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "AndroidShareHelper::shareMultipleFiles: Failed to get Android context";
        return;
    }
    
    // Create Java String array
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        qWarning() << "AndroidShareHelper::shareMultipleFiles: Failed to find String class";
        return;
    }
    
    jobjectArray jFilePaths = env->NewObjectArray(filePaths.size(), stringClass, nullptr);
    
    for (int i = 0; i < filePaths.size(); ++i) {
        jstring jPath = env->NewStringUTF(filePaths.at(i).toUtf8().constData());
        env->SetObjectArrayElement(jFilePaths, i, jPath);
        env->DeleteLocalRef(jPath);
    }
    
    // Call Java method
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/ShareHelper",
        "shareMultipleFiles",
        "(Landroid/app/Activity;[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        activity.object<jobject>(),
        jFilePaths,
        QJniObject::fromString(mimeType).object<jstring>(),
        QJniObject::fromString(chooserTitle).object<jstring>()
    );
    
    // Clean up JNI local references
    env->DeleteLocalRef(jFilePaths);
    env->DeleteLocalRef(stringClass);
    
    if (env.checkAndClearExceptions()) {
        qWarning() << "AndroidShareHelper::shareMultipleFiles: JNI exception occurred";
    }
}

bool isAvailable()
{
    return true;
}

} // namespace AndroidShareHelper

#else // !Q_OS_ANDROID

// Stub implementations for non-Android platforms

namespace AndroidShareHelper {

void shareFile(const QString& /*filePath*/, const QString& /*mimeType*/, const QString& /*chooserTitle*/)
{
    // No-op on non-Android
}

void shareMultipleFiles(const QStringList& /*filePaths*/, const QString& /*mimeType*/, const QString& /*chooserTitle*/)
{
    // No-op on non-Android
}

bool isAvailable()
{
    return false;
}

} // namespace AndroidShareHelper

#endif // Q_OS_ANDROID
