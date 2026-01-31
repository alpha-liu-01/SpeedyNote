#ifndef SYSTEMNOTIFICATION_H
#define SYSTEMNOTIFICATION_H

/**
 * @file SystemNotification.h
 * @brief Cross-platform system notification API.
 * 
 * Part of Phase 3: Batch Operations - System Notifications (Step 3.11).
 * 
 * Provides a unified interface for showing system notifications on:
 * - Android: Uses NotificationManager via JNI
 * - Linux: Uses DBus org.freedesktop.Notifications
 * - Windows/macOS: Placeholder (can be extended)
 * 
 * Primary use case: Notify user when export/import completes while app is backgrounded.
 * 
 * @see android/app-resources/src/org/speedynote/app/NotificationHelper.java
 * @see docs/private/BATCH_OPERATIONS.md Step 3.11
 */

#include <QString>

namespace SystemNotification {

/**
 * @brief Notification type for different operations.
 */
enum class Type {
    Export,     ///< Export operation completed
    Import,     ///< Import operation completed
    General     ///< General notification
};

/**
 * @brief Initialize the notification system.
 * 
 * Call this once during app startup.
 * On Android, creates the notification channel (required for Android 8.0+).
 * On desktop, initializes DBus connection if available.
 * 
 * @return true if initialization succeeded, false otherwise.
 */
bool initialize();

/**
 * @brief Check if system notifications are available.
 * 
 * @return true if notifications can be shown on this platform.
 */
bool isAvailable();

/**
 * @brief Check if the app has permission to show notifications.
 * 
 * On Android 13+, notification permission must be granted by user.
 * On desktop, usually always returns true.
 * 
 * @return true if notifications are permitted.
 */
bool hasPermission();

/**
 * @brief Request notification permission from the user.
 * 
 * On Android 13+, this shows the system permission dialog.
 * On desktop, this is a no-op (permissions not required).
 * 
 * Call this before showing notifications, or proactively at app startup.
 * The result is asynchronous - check hasPermission() later to see the result.
 */
void requestPermission();

/**
 * @brief Check if we should explain why notifications are needed.
 * 
 * On Android, returns true if user previously denied permission and
 * we should show an explanation before requesting again.
 * 
 * @return true if rationale should be shown to user.
 */
bool shouldShowRationale();

/**
 * @brief Show a notification for export completion.
 * 
 * @param title Notification title (e.g., "Export Complete")
 * @param message Notification body (e.g., "3 notebooks exported successfully")
 * @param success true for success notification, false for error
 */
void showExportNotification(const QString& title, const QString& message, bool success = true);

/**
 * @brief Show a notification for import completion.
 * 
 * @param title Notification title (e.g., "Import Complete")
 * @param message Notification body (e.g., "2 notebooks imported")
 * @param success true for success notification, false for error
 */
void showImportNotification(const QString& title, const QString& message, bool success = true);

/**
 * @brief Show a generic notification.
 * 
 * @param type Notification type (affects icon/category)
 * @param title Notification title
 * @param message Notification body
 * @param success true for success, false for error
 */
void show(Type type, const QString& title, const QString& message, bool success = true);

/**
 * @brief Dismiss/cancel any active export notification.
 */
void dismissExportNotification();

/**
 * @brief Dismiss/cancel any active import notification.
 */
void dismissImportNotification();

} // namespace SystemNotification

#endif // SYSTEMNOTIFICATION_H
