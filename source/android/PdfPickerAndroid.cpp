#include "PdfPickerAndroid.h"

#ifdef Q_OS_ANDROID

#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <jni.h>

// ============================================================================
// Android PDF File Picker (shared utility)
// ============================================================================
// Uses PdfFileHelper.java to pick PDFs with proper SAF permission handling.
// The Java code copies the file to local storage while permission is valid,
// then calls back to C++ with the local file path.
//
// Originally in MainWindow.cpp (BUG-A003), moved here for reuse by:
// - MainWindow::openPdfDocument()
// - PdfRelinkDialog::onLocatePdf()
// ============================================================================

namespace {
    // Static variables for async file picker result
    // Only one picker can be active at a time
    static QString s_pickedPdfPath;
    static bool s_pdfPickerCancelled = false;
    static QEventLoop* s_pdfPickerLoop = nullptr;
    static bool s_pickerActive = false;  // Reentrancy guard
}

// JNI callback: Called from Java when a PDF file is successfully picked and copied
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_PdfFileHelper_onPdfFilePicked(JNIEnv *env, jclass /*clazz*/, jstring localPath)
{
    const char* pathChars = env->GetStringUTFChars(localPath, nullptr);
    s_pickedPdfPath = QString::fromUtf8(pathChars);
    env->ReleaseStringUTFChars(localPath, pathChars);
    
    s_pdfPickerCancelled = false;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "PdfPickerAndroid: PDF picked -" << s_pickedPdfPath;
#endif
    
    if (s_pdfPickerLoop) {
        s_pdfPickerLoop->quit();
    }
}

// JNI callback: Called from Java when PDF picking is cancelled or fails
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_PdfFileHelper_onPdfPickCancelled(JNIEnv * /*env*/, jclass /*clazz*/)
{
    s_pickedPdfPath.clear();
    s_pdfPickerCancelled = true;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "PdfPickerAndroid: PDF pick cancelled";
#endif
    
    if (s_pdfPickerLoop) {
        s_pdfPickerLoop->quit();
    }
}

namespace PdfPickerAndroid {

QString pickPdfFile()
{
    // Default destination: app-private /pdfs/ directory
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
    return pickPdfFile(destDir);
}

QString pickPdfFile(const QString& destDir)
{
    // Reentrancy guard - only one picker can be active at a time
    if (s_pickerActive) {
        qWarning() << "PdfPickerAndroid::pickPdfFile: Picker already active, ignoring request";
        return QString();
    }
    s_pickerActive = true;
    
    // Reset state
    s_pickedPdfPath.clear();
    s_pdfPickerCancelled = false;
    
    // Ensure destination directory exists
    QDir().mkpath(destDir);
    
    // Get the Activity
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "PdfPickerAndroid::pickPdfFile: Failed to get Android context";
        s_pickerActive = false;
        return QString();
    }
    
    // Call Java helper to open the file picker
    QJniObject destDirJni = QJniObject::fromString(destDir);
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/PdfFileHelper",
        "pickPdfFile",
        "(Landroid/app/Activity;Ljava/lang/String;)V",
        activity.object(),
        destDirJni.object<jstring>());
    
    QJniEnvironment env;
    if (env.checkAndClearExceptions()) {
        qWarning() << "PdfPickerAndroid::pickPdfFile: Exception calling pickPdfFile";
        s_pickerActive = false;
        return QString();
    }
    
    // Wait for the callback using a local event loop
    QEventLoop loop;
    s_pdfPickerLoop = &loop;
    
    // Timeout after 2 minutes (user should have picked a file by then)
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    
    loop.exec();
    s_pdfPickerLoop = nullptr;
    s_pickerActive = false;
    
    if (s_pdfPickerCancelled || s_pickedPdfPath.isEmpty()) {
        return QString();
    }
    
    return s_pickedPdfPath;
}

} // namespace PdfPickerAndroid

#endif // Q_OS_ANDROID

