package org.speedynote.app;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;

/**
 * Helper class for sharing files on Android.
 * 
 * This class uses Android's share sheet (ACTION_SEND) to share files
 * with other apps. It uses FileProvider to securely grant temporary
 * read permission to the receiving app.
 * 
 * Part of the Share/Import feature (Phase 1).
 * 
 * Called from C++ via JNI to share exported .snbx packages.
 */
public class ShareHelper {
    private static final String TAG = "ShareHelper";
    
    // FileProvider authority - must match AndroidManifest.xml
    private static final String FILE_PROVIDER_AUTHORITY = "org.speedynote.app.fileprovider";
    
    /**
     * Share a file using Android's native share sheet.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity (for starting the share intent)
     * @param filePath Absolute path to the file to share
     * @param mimeType MIME type of the file (e.g., "application/octet-stream" for .snbx)
     */
    public static void shareFile(Activity activity, String filePath, String mimeType) {
        if (activity == null) {
            Log.e(TAG, "shareFile: Activity is null");
            return;
        }
        
        if (filePath == null || filePath.isEmpty()) {
            Log.e(TAG, "shareFile: File path is null or empty");
            return;
        }
        
        File file = new File(filePath);
        if (!file.exists()) {
            Log.e(TAG, "shareFile: File does not exist: " + filePath);
            return;
        }
        
        try {
            // Get a content:// URI for the file using FileProvider
            // This grants temporary read permission to the receiving app
            Uri uri = FileProvider.getUriForFile(activity, FILE_PROVIDER_AUTHORITY, file);
            
            Log.d(TAG, "shareFile: Sharing " + filePath + " as " + uri.toString());
            
            // Create the share intent
            Intent shareIntent = new Intent(Intent.ACTION_SEND);
            shareIntent.setType(mimeType);
            shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
            
            // Grant temporary read permission to the receiving app
            shareIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            
            // Start the share sheet
            activity.startActivity(Intent.createChooser(shareIntent, "Share Notebook"));
            
            Log.d(TAG, "shareFile: Share sheet launched successfully");
            
        } catch (IllegalArgumentException e) {
            // FileProvider couldn't generate a URI for this file
            // This usually means the file is outside the paths defined in file_paths.xml
            Log.e(TAG, "shareFile: FileProvider error - file may be outside allowed paths: " + e.getMessage());
            e.printStackTrace();
        } catch (Exception e) {
            Log.e(TAG, "shareFile: Error sharing file: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    /**
     * Share a file with a custom chooser title.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param filePath Absolute path to the file to share
     * @param mimeType MIME type of the file
     * @param chooserTitle Title for the share sheet chooser dialog
     */
    public static void shareFileWithTitle(Activity activity, String filePath, 
                                           String mimeType, String chooserTitle) {
        if (activity == null) {
            Log.e(TAG, "shareFileWithTitle: Activity is null");
            return;
        }
        
        if (filePath == null || filePath.isEmpty()) {
            Log.e(TAG, "shareFileWithTitle: File path is null or empty");
            return;
        }
        
        File file = new File(filePath);
        if (!file.exists()) {
            Log.e(TAG, "shareFileWithTitle: File does not exist: " + filePath);
            return;
        }
        
        try {
            Uri uri = FileProvider.getUriForFile(activity, FILE_PROVIDER_AUTHORITY, file);
            
            Log.d(TAG, "shareFileWithTitle: Sharing " + filePath + " as " + uri.toString());
            
            Intent shareIntent = new Intent(Intent.ACTION_SEND);
            shareIntent.setType(mimeType);
            shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
            shareIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            
            // Use custom title if provided, otherwise default
            String title = (chooserTitle != null && !chooserTitle.isEmpty()) 
                           ? chooserTitle 
                           : "Share Notebook";
            
            activity.startActivity(Intent.createChooser(shareIntent, title));
            
            Log.d(TAG, "shareFileWithTitle: Share sheet launched successfully");
            
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "shareFileWithTitle: FileProvider error: " + e.getMessage());
            e.printStackTrace();
        } catch (Exception e) {
            Log.e(TAG, "shareFileWithTitle: Error sharing file: " + e.getMessage());
            e.printStackTrace();
        }
    }
}

