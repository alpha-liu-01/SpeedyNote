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
// Desktop Linux: XDG Desktop Portal notification support
// Uses org.freedesktop.portal.Notification (works in Flatpak and on modern desktops)
static bool s_portalAvailable = false;

// String IDs for portal notifications (portal uses string IDs, not uint32)
static const QString PORTAL_ID_EXPORT = QStringLiteral("speedynote-export");
static const QString PORTAL_ID_IMPORT = QStringLiteral("speedynote-import");
static const QString PORTAL_ID_GENERAL = QStringLiteral("speedynote-general");

static bool initPortal()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "SystemNotification: DBus session bus not available";
        return false;
    }
    
    // Check if the XDG Desktop Portal is available
    QDBusInterface iface("org.freedesktop.portal.Desktop",
                         "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Notification",
                         bus);
    
    if (!iface.isValid()) {
        qDebug() << "SystemNotification: XDG Desktop Portal notifications not available";
        return false;
    }
    
    s_portalAvailable = true;
    qDebug() << "SystemNotification: Portal notifications initialized";
    return true;
}

static void showPortalNotification(const QString& id, const QString& title,
                                    const QString& message, const QString& priority)
{
    if (!s_portalAvailable) {
        return;
    }
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("org.freedesktop.portal.Desktop",
                         "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Notification",
                         bus);
    
    if (!iface.isValid()) {
        return;
    }
    
    // Build notification dict: a{sv} with title, body, priority
    QVariantMap notification;
    notification["title"] = title;
    notification["body"] = message;
    notification["priority"] = priority; // "low", "normal", "high", "urgent"
    
    // AddNotification(id: s, notification: a{sv})
    // Using the same string ID automatically replaces any previous notification of that type
    iface.call("AddNotification", id, notification);
}

static void removePortalNotification(const QString& id)
{
    if (!s_portalAvailable) {
        return;
    }
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("org.freedesktop.portal.Desktop",
                         "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Notification",
                         bus);
    
    if (iface.isValid()) {
        iface.call("RemoveNotification", id);
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
#elif defined(Q_OS_IOS)
    // TODO Phase 4: iOS UNUserNotificationCenter initialization
    s_initialized = true;
#elif defined(Q_OS_LINUX)
    s_initialized = initPortal();
#else
    // Windows/macOS: System notifications not implemented
    // Mark as initialized but isAvailable() will return false
    s_initialized = true;
#endif
    
    return s_initialized;
}

bool isAvailable()
{
#ifdef Q_OS_ANDROID
    return true;
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Check UNUserNotificationCenter availability
    return false;
#elif defined(Q_OS_LINUX)
    return s_portalAvailable;
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
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Check iOS notification permission via UNUserNotificationCenter
    return false;
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
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Request iOS notification permission via UNUserNotificationCenter
#else
    // Desktop Linux uses XDG portal (no permission needed)
    // Windows/macOS: notifications not implemented, silently ignore
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
#elif defined(Q_OS_IOS)
    // iOS does not have a "show rationale" concept like Android
    return false;
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
    
    // Early return if notifications not available (Windows/macOS)
    if (!isAvailable()) {
        return;
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
    
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Show iOS notification via UNUserNotificationCenter
    Q_UNUSED(type);
    Q_UNUSED(title);
    Q_UNUSED(message);
    Q_UNUSED(success);
    
#elif defined(Q_OS_LINUX)
    // Desktop Linux: Use XDG Desktop Portal notifications
    QString priority = success ? "normal" : "urgent";
    
    QString portalId;
    switch (type) {
        case Type::Export:
            portalId = PORTAL_ID_EXPORT;
            break;
        case Type::Import:
            portalId = PORTAL_ID_IMPORT;
            break;
        default:
            portalId = PORTAL_ID_GENERAL;
            break;
    }
    
    showPortalNotification(portalId, title, message, priority);
    
#endif
    // Note: Windows/macOS return early via isAvailable() check above
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
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Dismiss iOS export notification
#elif defined(Q_OS_LINUX)
    removePortalNotification(PORTAL_ID_EXPORT);
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
#elif defined(Q_OS_IOS)
    // TODO Phase 4: Dismiss iOS import notification
#elif defined(Q_OS_LINUX)
    removePortalNotification(PORTAL_ID_IMPORT);
#endif
}

} // namespace SystemNotification
