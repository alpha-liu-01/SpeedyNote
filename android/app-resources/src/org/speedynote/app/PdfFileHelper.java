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
 * Helper class for picking and importing PDF files on Android.
 * 
 * This class handles the Storage Access Framework (SAF) properly by:
 * 1. Opening the file picker with proper Intent flags
 * 2. Copying the file to local storage while the temporary permission is valid
 * 3. Returning the local file path to C++
 * 
 * BUG-A003: Qt's QFileDialog returns content:// URIs, but the temporary
 * permission expires before our JNI code can use it. This class handles
 * the file copying inside the Activity result callback where permission is valid.
 */
public class PdfFileHelper {
    private static final String TAG = "PdfFileHelper";
    private static final int REQUEST_CODE_PICK_PDF = 9001;
    
    private static Activity sActivity;
    private static String sPendingDestDir;
    private static native void onPdfFilePicked(String localPath);
    private static native void onPdfPickCancelled();
    
    /**
     * Opens the system file picker to select a PDF.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param destDir Directory to copy the PDF to
     */
    public static void pickPdfFile(Activity activity, String destDir) {
        sActivity = activity;
        sPendingDestDir = destDir;
        
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/pdf");
        
        // These flags grant read permission that persists until the Activity result is processed
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        
        Log.d(TAG, "Opening file picker, destDir=" + destDir);
        activity.startActivityForResult(intent, REQUEST_CODE_PICK_PDF);
    }
    
    /**
     * Called from QtActivity's onActivityResult.
     * Must be called from the UI thread.
     */
    public static boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_CODE_PICK_PDF) {
            return false; // Not our request
        }
        
        if (resultCode != Activity.RESULT_OK || data == null) {
            Log.d(TAG, "File pick cancelled");
            onPdfPickCancelled();
            return true;
        }
        
        Uri uri = data.getData();
        if (uri == null) {
            Log.e(TAG, "No URI in result");
            onPdfPickCancelled();
            return true;
        }
        
        Log.d(TAG, "Got URI: " + uri.toString());
        
        // Copy file to local storage while permission is still valid
        String localPath = copyUriToLocal(sActivity, uri, sPendingDestDir);
        
        if (localPath != null) {
            Log.d(TAG, "Successfully copied to: " + localPath);
            onPdfFilePicked(localPath);
        } else {
            Log.e(TAG, "Failed to copy file");
            onPdfPickCancelled();
        }
        
        // Clear static references to avoid memory leaks
        sActivity = null;
        sPendingDestDir = null;
        
        return true;
    }
    
    /**
     * Copies a content:// URI to local storage.
     * This MUST be called while the SAF permission is still valid.
     * 
     * Implements deduplication: if a file with the same name and size already exists,
     * returns the existing file path instead of copying again.
     */
    private static String copyUriToLocal(Context context, Uri uri, String destDir) {
        if (context == null || uri == null || destDir == null) {
            return null;
        }
        
        // Get the original filename
        String filename = getFileName(context, uri);
        if (filename == null || filename.isEmpty()) {
            filename = "imported_" + System.currentTimeMillis() + ".pdf";
        }
        
        // Get the file size for deduplication
        long fileSize = getFileSize(context, uri);
        
        // Ensure destination directory exists
        File destDirFile = new File(destDir);
        if (!destDirFile.exists()) {
            destDirFile.mkdirs();
        }
        
        File destFile = new File(destDir, filename);
        
        // Deduplication check: if file with same name and size exists, reuse it
        if (destFile.exists() && fileSize > 0 && destFile.length() == fileSize) {
            Log.d(TAG, "Deduplication: Reusing existing file " + destFile.getAbsolutePath() + 
                       " (size: " + fileSize + " bytes)");
            return destFile.getAbsolutePath();
        }
        
        // If file exists but different size, generate unique name
        if (destFile.exists()) {
            String baseName = filename;
            String extension = "";
            int dotIndex = filename.lastIndexOf('.');
            if (dotIndex > 0) {
                baseName = filename.substring(0, dotIndex);
                extension = filename.substring(dotIndex);
            }
            
            int counter = 1;
            while (destFile.exists()) {
                filename = baseName + "_" + counter + extension;
                destFile = new File(destDir, filename);
                counter++;
            }
            Log.d(TAG, "File exists with different size, using unique name: " + filename);
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
            
            byte[] buffer = new byte[65536];
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
     * Gets the file size of a content:// URI.
     * Returns -1 if size cannot be determined.
     */
    private static long getFileSize(Context context, Uri uri) {
        if (uri == null || !uri.getScheme().equals("content")) {
            return -1;
        }
        
        Cursor cursor = context.getContentResolver().query(uri, null, null, null, null);
        try {
            if (cursor != null && cursor.moveToFirst()) {
                int sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (sizeIndex >= 0 && !cursor.isNull(sizeIndex)) {
                    return cursor.getLong(sizeIndex);
                }
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return -1;
    }
    
    /**
     * Gets the display name of a content:// URI.
     */
    private static String getFileName(Context context, Uri uri) {
        String result = null;
        
        if (uri.getScheme().equals("content")) {
            Cursor cursor = context.getContentResolver().query(uri, null, null, null, null);
            try {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        result = cursor.getString(index);
                    }
                }
            } finally {
                if (cursor != null) {
                    cursor.close();
                }
            }
        }
        
        if (result == null) {
            result = uri.getLastPathSegment();
        }
        
        // Ensure it ends with .pdf
        if (result != null && !result.toLowerCase().endsWith(".pdf")) {
            result = result + ".pdf";
        }
        
        return result;
    }
}

