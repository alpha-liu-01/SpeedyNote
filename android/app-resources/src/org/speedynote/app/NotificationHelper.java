package org.speedynote.app;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

/**
 * Helper class for showing system notifications on Android.
 * 
 * Part of Phase 3: Batch Operations - System Notifications.
 * 
 * Called from C++ via JNI to show notifications when exports/imports complete,
 * especially useful when the app is in the background.
 * 
 * @see docs/private/BATCH_OPERATIONS.md Step 3.11
 */
public class NotificationHelper {
    private static final String TAG = "NotificationHelper";
    
    // Notification channel ID (required for Android 8.0+)
    private static final String CHANNEL_ID = "speedynote_export_import";
    private static final String CHANNEL_NAME = "Export & Import";
    private static final String CHANNEL_DESCRIPTION = "Notifications for export and import operations";
    
    // Notification IDs
    private static final int NOTIFICATION_ID_EXPORT = 1001;
    private static final int NOTIFICATION_ID_IMPORT = 1002;
    
    private static boolean channelCreated = false;
    
    /**
     * Create the notification channel (required for Android 8.0+).
     * Call this once during app initialization.
     * 
     * @param context Application or Activity context
     */
    public static void createNotificationChannel(Context context) {
        if (channelCreated) {
            return;
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_DEFAULT
            );
            channel.setDescription(CHANNEL_DESCRIPTION);
            
            // Don't vibrate or make sound for export/import notifications
            channel.enableVibration(false);
            channel.setSound(null, null);
            
            NotificationManager notificationManager = 
                context.getSystemService(NotificationManager.class);
            if (notificationManager != null) {
                notificationManager.createNotificationChannel(channel);
                Log.d(TAG, "Notification channel created");
            }
        }
        
        channelCreated = true;
    }
    
    /**
     * Show a notification for export completion.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param title Notification title (e.g., "Export Complete")
     * @param message Notification message (e.g., "3 notebooks exported successfully")
     * @param success Whether the operation was successful (affects icon)
     */
    public static void showExportNotification(Activity activity, String title, 
                                              String message, boolean success) {
        showNotification(activity, title, message, success, NOTIFICATION_ID_EXPORT);
    }
    
    /**
     * Show a notification for import completion.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param title Notification title (e.g., "Import Complete")
     * @param message Notification message (e.g., "2 notebooks imported")
     * @param success Whether the operation was successful
     */
    public static void showImportNotification(Activity activity, String title, 
                                              String message, boolean success) {
        showNotification(activity, title, message, success, NOTIFICATION_ID_IMPORT);
    }
    
    /**
     * Show a generic notification.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param title Notification title
     * @param message Notification message
     * @param success Whether this is a success or error notification
     * @param notificationId Unique ID for this notification (allows update/cancel)
     */
    public static void showNotification(Activity activity, String title, 
                                        String message, boolean success, int notificationId) {
        if (activity == null) {
            Log.e(TAG, "showNotification: Activity is null");
            return;
        }
        
        try {
            // Ensure notification channel exists
            createNotificationChannel(activity);
            
            // Create intent to open the app when notification is tapped
            Intent intent = new Intent(activity, activity.getClass());
            intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            
            PendingIntent pendingIntent = PendingIntent.getActivity(
                activity, 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
            );
            
            // Build the notification
            // Use Android's built-in icons for broad compatibility
            int iconRes = success 
                ? android.R.drawable.stat_sys_download_done  // Checkmark icon
                : android.R.drawable.stat_notify_error;      // Error icon
            
            NotificationCompat.Builder builder = new NotificationCompat.Builder(activity, CHANNEL_ID)
                .setSmallIcon(iconRes)
                .setContentTitle(title != null ? title : "SpeedyNote")
                .setContentText(message != null ? message : "")
                .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                .setContentIntent(pendingIntent)
                .setAutoCancel(true);  // Dismiss when tapped
            
            // Show the notification
            NotificationManager notificationManager = 
                (NotificationManager) activity.getSystemService(Context.NOTIFICATION_SERVICE);
            if (notificationManager != null) {
                notificationManager.notify(notificationId, builder.build());
                Log.d(TAG, "Notification shown: " + title);
            }
            
        } catch (Exception e) {
            Log.e(TAG, "showNotification: Error showing notification: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * Cancel/dismiss a notification by ID.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param notificationId ID of notification to cancel
     */
    public static void cancelNotification(Activity activity, int notificationId) {
        if (activity == null) {
            return;
        }
        
        try {
            NotificationManager notificationManager = 
                (NotificationManager) activity.getSystemService(Context.NOTIFICATION_SERVICE);
            if (notificationManager != null) {
                notificationManager.cancel(notificationId);
            }
        } catch (Exception e) {
            Log.e(TAG, "cancelNotification: Error: " + e.getMessage());
        }
    }
    
    /**
     * Check if app has notification permission (Android 13+).
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @return true if notifications are allowed, false otherwise
     */
    public static boolean hasNotificationPermission(Activity activity) {
        if (activity == null) {
            return false;
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // Android 13+ requires explicit permission
            return ContextCompat.checkSelfPermission(activity, 
                Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED;
        }
        
        // Before Android 13, notifications are enabled by default
        return true;
    }
    
    /**
     * Request notification permission (Android 13+).
     * Called from C++ via JNI.
     * 
     * On Android 13+, this shows the system permission dialog.
     * On earlier versions, this is a no-op (permission is granted by default).
     * 
     * @param activity The current Activity
     * @param requestCode Request code for permission result callback
     */
    public static void requestNotificationPermission(Activity activity, int requestCode) {
        if (activity == null) {
            Log.e(TAG, "requestNotificationPermission: Activity is null");
            return;
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // Check if we already have permission
            if (ContextCompat.checkSelfPermission(activity, 
                    Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                Log.d(TAG, "Requesting POST_NOTIFICATIONS permission");
                ActivityCompat.requestPermissions(activity,
                    new String[] { Manifest.permission.POST_NOTIFICATIONS },
                    requestCode);
            } else {
                Log.d(TAG, "POST_NOTIFICATIONS permission already granted");
            }
        } else {
            Log.d(TAG, "POST_NOTIFICATIONS not required on API < 33");
        }
    }
    
    /**
     * Check if permission request should show rationale.
     * Called from C++ via JNI.
     * 
     * Returns true if the user has previously denied the permission and
     * we should explain why we need it before requesting again.
     * 
     * @param activity The current Activity
     * @return true if rationale should be shown
     */
    public static boolean shouldShowPermissionRationale(Activity activity) {
        if (activity == null) {
            return false;
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return ActivityCompat.shouldShowRequestPermissionRationale(activity,
                Manifest.permission.POST_NOTIFICATIONS);
        }
        
        return false;
    }
}
