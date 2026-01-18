package org.speedynote.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Helper class for picking and importing .snbx package files on Android.
 * 
 * This class handles the Storage Access Framework (SAF) properly by:
 * 1. Opening the file picker for .snbx files
 * 2. Copying the file to local storage while the temporary permission is valid
 * 3. Returning the local file path to C++
 * 
 * Part of the Share/Import feature (Phase 2).
 * 
 * Similar pattern to PdfFileHelper, but for notebook packages.
 */
public class ImportHelper {
    private static final String TAG = "ImportHelper";
    private static final int REQUEST_CODE_PICK_SNBX = 9002;
    
    private static Activity sActivity;
    private static String sPendingDestDir;
    
    // Native callbacks to C++
    private static native void onPackageFilePicked(String localPath);
    private static native void onPackagePickCancelled();
    
    /**
     * Opens the system file picker to select a .snbx package.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param destDir Directory to copy the .snbx file to (for extraction)
     */
    public static void pickPackageFile(Activity activity, String destDir) {
        sActivity = activity;
        sPendingDestDir = destDir;
        
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        
        // Accept any file type since .snbx is a custom extension
        // Android doesn't have a registered MIME type for .snbx
        intent.setType("*/*");
        
        // These flags grant read permission that persists until the Activity result is processed
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        
        Log.d(TAG, "Opening file picker for .snbx packages, destDir=" + destDir);
        activity.startActivityForResult(intent, REQUEST_CODE_PICK_SNBX);
    }
    
    /**
     * Called from SpeedyNoteActivity's onActivityResult.
     * Must be called from the UI thread.
     * 
     * @return true if this was our request and we handled it, false otherwise
     */
    public static boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_CODE_PICK_SNBX) {
            return false; // Not our request
        }
        
        if (resultCode != Activity.RESULT_OK || data == null) {
            Log.d(TAG, "Package pick cancelled");
            onPackagePickCancelled();
            clearStaticReferences();
            return true;
        }
        
        Uri uri = data.getData();
        if (uri == null) {
            Log.e(TAG, "No URI in result");
            onPackagePickCancelled();
            clearStaticReferences();
            return true;
        }
        
        Log.d(TAG, "Got URI: " + uri.toString());
        
        // Validate that the file looks like a .snbx package
        String filename = getFileName(sActivity, uri);
        if (filename == null || !filename.toLowerCase().endsWith(".snbx")) {
            Log.w(TAG, "Selected file is not a .snbx package: " + filename);
            // Still try to import - user might have renamed it
        }
        
        // Copy file to local storage while permission is still valid
        String localPath = copyUriToLocal(sActivity, uri, sPendingDestDir);
        
        if (localPath != null) {
            Log.d(TAG, "Successfully copied to: " + localPath);
            onPackageFilePicked(localPath);
        } else {
            Log.e(TAG, "Failed to copy package file");
            onPackagePickCancelled();
        }
        
        clearStaticReferences();
        return true;
    }
    
    /**
     * Clear static references to avoid memory leaks.
     */
    private static void clearStaticReferences() {
        sActivity = null;
        sPendingDestDir = null;
    }
    
    /**
     * Copies a content:// URI to local storage.
     * This MUST be called while the SAF permission is still valid.
     */
    private static String copyUriToLocal(Context context, Uri uri, String destDir) {
        if (context == null || uri == null || destDir == null) {
            return null;
        }
        
        // Get the original filename
        String filename = getFileName(context, uri);
        if (filename == null || filename.isEmpty()) {
            filename = "imported_" + System.currentTimeMillis() + ".snbx";
        }
        
        // Ensure it ends with .snbx
        if (!filename.toLowerCase().endsWith(".snbx")) {
            filename = filename + ".snbx";
        }
        
        // Ensure destination directory exists
        File destDirFile = new File(destDir);
        if (!destDirFile.exists()) {
            if (!destDirFile.mkdirs()) {
                Log.e(TAG, "Failed to create destination directory: " + destDir);
                return null;
            }
        }
        
        // Generate unique filename if needed
        File destFile = new File(destDir, filename);
        if (destFile.exists()) {
            String baseName = filename.substring(0, filename.length() - 5); // Remove .snbx
            int counter = 1;
            while (destFile.exists()) {
                filename = baseName + "_" + counter + ".snbx";
                destFile = new File(destDir, filename);
                counter++;
                
                // Safety limit
                if (counter > 1000) {
                    Log.e(TAG, "Too many duplicate filenames");
                    return null;
                }
            }
            Log.d(TAG, "Using unique filename: " + filename);
        }
        
        // Copy the file
        ContentResolver resolver = context.getContentResolver();
        InputStream inputStream = null;
        OutputStream outputStream = null;
        
        try {
            inputStream = resolver.openInputStream(uri);
            if (inputStream == null) {
                Log.e(TAG, "Failed to open input stream");
                return null;
            }
            
            outputStream = new FileOutputStream(destFile);
            
            byte[] buffer = new byte[65536]; // 64KB buffer
            int bytesRead;
            long totalBytes = 0;
            
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
                totalBytes += bytesRead;
            }
            
            Log.d(TAG, "Copied " + totalBytes + " bytes to " + destFile.getAbsolutePath());
            return destFile.getAbsolutePath();
            
        } catch (Exception e) {
            Log.e(TAG, "Error copying file: " + e.getMessage());
            e.printStackTrace();
            // Clean up partial file
            if (destFile.exists()) {
                destFile.delete();
            }
            return null;
        } finally {
            try {
                if (inputStream != null) inputStream.close();
                if (outputStream != null) outputStream.close();
            } catch (Exception e) {
                // Ignore close errors
            }
        }
    }
    
    /**
     * Gets the display name of a content:// URI.
     */
    private static String getFileName(Context context, Uri uri) {
        String result = null;
        
        if ("content".equals(uri.getScheme())) {
            Cursor cursor = null;
            try {
                cursor = context.getContentResolver().query(uri, null, null, null, null);
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        result = cursor.getString(index);
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Error getting filename: " + e.getMessage());
            } finally {
                if (cursor != null) {
                    cursor.close();
                }
            }
        }
        
        if (result == null) {
            result = uri.getLastPathSegment();
        }
        
        return result;
    }
}

