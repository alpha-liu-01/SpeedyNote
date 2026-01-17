package org.speedynote.app;

import android.content.Intent;
import android.content.res.Configuration;
import android.os.Build;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

import org.qtproject.qt.android.bindings.QtActivity;

/**
 * Custom Activity for SpeedyNote.
 * 
 * Extends QtActivity to:
 * 1. Handle Activity results from the PDF file picker (BUG-A003)
 * 2. Handle Activity results from the .snbx package importer (Phase 2)
 * 3. Enable high-rate stylus input via requestUnbufferedDispatch() (BUG-A004)
 * 4. Provide system dark mode detection for theme synchronization (BUG-A007)
 * 
 * This is necessary because:
 * - PDF picker: We need to process the file picker result while SAF permission is valid
 * - Package importer: Same SAF handling for .snbx files
 * - Stylus input: Android batches touch events at 60Hz by default; we want 240Hz
 * - Dark mode: Qt doesn't automatically detect Android's system theme setting
 */
public class SpeedyNoteActivity extends QtActivity {
    private static final String TAG = "SpeedyNoteActivity";
    
    // Singleton reference for JNI calls
    private static SpeedyNoteActivity sInstance;
    
    @Override
    public void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
    }
    
    @Override
    protected void onDestroy() {
        if (sInstance == this) {
            sInstance = null;
        }
        super.onDestroy();
    }
    
    /**
     * Check if the system is in dark mode.
     * Called from C++ via JNI to sync Qt's palette with Android's system theme.
     * 
     * @return true if dark mode is enabled, false otherwise
     */
    public static boolean isDarkMode() {
        if (sInstance == null) {
            Log.w(TAG, "isDarkMode: Activity not available, defaulting to light mode");
            return false;
        }
        
        Configuration config = sInstance.getResources().getConfiguration();
        int nightMode = config.uiMode & Configuration.UI_MODE_NIGHT_MASK;
        boolean isDark = (nightMode == Configuration.UI_MODE_NIGHT_YES);
        Log.d(TAG, "isDarkMode: " + isDark + " (uiMode=" + config.uiMode + ")");
        return isDark;
    }
    
    /**
     * Intercept all touch events to request unbuffered dispatch.
     * 
     * On API 31+, this tells Android to deliver touch/stylus events at the
     * hardware's native rate (e.g., 240Hz) instead of batching them at 60Hz.
     * This results in smoother, more responsive drawing.
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        // Request unbuffered dispatch for high-rate stylus input (API 31+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            View contentView = findViewById(android.R.id.content);
            if (contentView != null) {
                contentView.requestUnbufferedDispatch(event);
            }
        }
        return super.dispatchTouchEvent(event);
    }
    
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "onActivityResult: requestCode=" + requestCode + ", resultCode=" + resultCode);
        
        // First, let our PDF helper try to handle it
        if (PdfFileHelper.handleActivityResult(requestCode, resultCode, data)) {
            Log.d(TAG, "PdfFileHelper handled the result");
            return;
        }
        
        // Try the package import helper
        if (ImportHelper.handleActivityResult(requestCode, resultCode, data)) {
            Log.d(TAG, "ImportHelper handled the result");
            return;
        }
        
        // If not handled, pass to Qt's default handling
        super.onActivityResult(requestCode, resultCode, data);
    }
}

