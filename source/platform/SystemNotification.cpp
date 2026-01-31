#include "SystemNotification.h"

#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif
#endif

namespace SystemNotification {

// Notification IDs (must match NotificationHelper.java)
static const int NOTIFICATION_ID_EXPORT = 1001;
static const int NOTIFICATION_ID_IMPORT = 1002;
static const int NOTIFICATION_ID_GENERAL = 1003;

static bool s_initialized = false;

#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
// Desktop Linux DBus notification support
static bool s_dbusAvailable = false;
// Track notification IDs per type for proper dismiss functionality
static uint32_t s_exportNotificationId = 0;
static uint32_t s_importNotificationId = 0;
static uint32_t s_generalNotificationId = 0;

static bool initDBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "SystemNotification: DBus session bus not available";
        return false;
    }
    
    // Check if notification service is available
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         bus);
    
    if (!iface.isValid()) {
        qDebug() << "SystemNotification: org.freedesktop.Notifications not available";
        return false;
    }
    
    s_dbusAvailable = true;
    qDebug() << "SystemNotification: DBus notifications initialized";
    return true;
}

static uint32_t showDBusNotification(const QString& title, const QString& message, 
                                      const QString& icon, uint32_t replaceId = 0)
{
    if (!s_dbusAvailable) {
        return 0;
    }
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         bus);
    
    if (!iface.isValid()) {
        return 0;
    }
    
    // Notification parameters:
    // - app_name: Application name
    // - replaces_id: ID of notification to replace (0 for new)
    // - app_icon: Icon name or path
    // - summary: Title
    // - body: Message
    // - actions: Array of action strings (empty)
    // - hints: Dictionary of hints (empty)
    // - expire_timeout: Timeout in ms (-1 for default)
    
    QVariantMap hints;
    hints["urgency"] = QVariant::fromValue(static_cast<uchar>(1)); // Normal urgency
    
    QDBusReply<uint> reply = iface.call(
        "Notify",
        "SpeedyNote",           // app_name
        replaceId,              // replaces_id
        icon,                   // app_icon
        title,                  // summary
        message,                // body
        QStringList(),          // actions
        hints,                  // hints
        5000                    // expire_timeout (5 seconds)
    );
    
    if (reply.isValid()) {
        return reply.value();
    }
    return 0;
}

static void closeDBusNotification(uint32_t id)
{
    if (!s_dbusAvailable || id == 0) {
        return;
    }
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         bus);
    
    if (iface.isValid()) {
        iface.call("CloseNotification", id);
    }
}
#endif // not Q_OS_ANDROID
#endif // Q_OS_LINUX

// ============================================================================
// Public API Implementation
// ============================================================================

bool initialize()
{
    if (s_initialized) {
        return true;
    }
    
#ifdef Q_OS_ANDROID
    // Create notification channel on Android
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject::callStaticMethod<void>(
            "org/speedynote/app/NotificationHelper",
            "createNotificationChannel",
            "(Landroid/content/Context;)V",
            activity.object<jobject>()
        );
        s_initialized = true;
        qDebug() << "SystemNotification: Android notification channel created";
    }
#elif defined(Q_OS_LINUX)
    s_initialized = initDBus();
#else
    // Windows/macOS: placeholder
    s_initialized = true;
    qDebug() << "SystemNotification: Platform notifications not fully implemented";
#endif
    
    return s_initialized;
}

bool isAvailable()
{
#ifdef Q_OS_ANDROID
    return true;
#elif defined(Q_OS_LINUX)
    return s_dbusAvailable;
#else
    // Windows/macOS: not yet implemented
    return false;
#endif
}

bool hasPermission()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        return false;
    }
    
    jboolean result = QJniObject::callStaticMethod<jboolean>(
        "org/speedynote/app/NotificationHelper",
        "hasNotificationPermission",
        "(Landroid/app/Activity;)Z",
        activity.object<jobject>()
    );
    
    return result;
#else
    // Desktop platforms generally always allow notifications
    return isAvailable();
#endif
}

void requestPermission()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qDebug() << "SystemNotification: Cannot request permission - no activity";
        return;
    }
    
    // Use a fixed request code (the result is handled by Android automatically)
    static const int REQUEST_CODE_NOTIFICATIONS = 1001;
    
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/NotificationHelper",
        "requestNotificationPermission",
        "(Landroid/app/Activity;I)V",
        activity.object<jobject>(),
        static_cast<jint>(REQUEST_CODE_NOTIFICATIONS)
    );
    
    qDebug() << "SystemNotification: Permission request initiated";
#else
    // Desktop platforms don't need permission request
    qDebug() << "SystemNotification: Permission request not needed on this platform";
#endif
}

bool shouldShowRationale()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        return false;
    }
    
    jboolean result = QJniObject::callStaticMethod<jboolean>(
        "org/speedynote/app/NotificationHelper",
        "shouldShowPermissionRationale",
        "(Landroid/app/Activity;)Z",
        activity.object<jobject>()
    );
    
    return result;
#else
    return false;
#endif
}

void showExportNotification(const QString& title, const QString& message, bool success)
{
    show(Type::Export, title, message, success);
}

void showImportNotification(const QString& title, const QString& message, bool success)
{
    show(Type::Import, title, message, success);
}

void show(Type type, const QString& title, const QString& message, bool success)
{
    // Ensure initialized
    if (!s_initialized) {
        initialize();
    }
    
#ifdef Q_OS_ANDROID
    // Check if we have permission before trying to show notification
    if (!hasPermission()) {
        qDebug() << "SystemNotification: Permission not granted, skipping notification";
        return;
    }
    
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qDebug() << "SystemNotification: Android activity not available";
        return;
    }
    
    // Determine notification ID based on type
    int notificationId;
    switch (type) {
        case Type::Export:
            notificationId = NOTIFICATION_ID_EXPORT;
            break;
        case Type::Import:
            notificationId = NOTIFICATION_ID_IMPORT;
            break;
        default:
            notificationId = NOTIFICATION_ID_GENERAL;
            break;
    }
    
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/NotificationHelper",
        "showNotification",
        "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;ZI)V",
        activity.object<jobject>(),
        QJniObject::fromString(title).object<jstring>(),
        QJniObject::fromString(message).object<jstring>(),
        static_cast<jboolean>(success),
        static_cast<jint>(notificationId)
    );
    
#elif defined(Q_OS_LINUX)
    // Desktop Linux: Use DBus notifications
    QString icon;
    if (success) {
        // Use standard FreeDesktop icon names
        icon = "dialog-information";
    } else {
        icon = "dialog-error";
    }
    
    // Store notification ID per type for proper dismiss
    uint32_t notifId = showDBusNotification(title, message, icon);
    switch (type) {
        case Type::Export:
            s_exportNotificationId = notifId;
            break;
        case Type::Import:
            s_importNotificationId = notifId;
            break;
        default:
            s_generalNotificationId = notifId;
            break;
    }
    
#else
    // Windows/macOS: Log for now (can be extended)
    Q_UNUSED(type);
    qDebug() << "SystemNotification:" << (success ? "[SUCCESS]" : "[ERROR]")
             << title << "-" << message;
#endif
}

void dismissExportNotification()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject::callStaticMethod<void>(
            "org/speedynote/app/NotificationHelper",
            "cancelNotification",
            "(Landroid/app/Activity;I)V",
            activity.object<jobject>(),
            static_cast<jint>(NOTIFICATION_ID_EXPORT)
        );
    }
#elif defined(Q_OS_LINUX)
    closeDBusNotification(s_exportNotificationId);
    s_exportNotificationId = 0;
#endif
}

void dismissImportNotification()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject::callStaticMethod<void>(
            "org/speedynote/app/NotificationHelper",
            "cancelNotification",
            "(Landroid/app/Activity;I)V",
            activity.object<jobject>(),
            static_cast<jint>(NOTIFICATION_ID_IMPORT)
        );
    }
#elif defined(Q_OS_LINUX)
    closeDBusNotification(s_importNotificationId);
    s_importNotificationId = 0;
#endif
}

} // namespace SystemNotification
