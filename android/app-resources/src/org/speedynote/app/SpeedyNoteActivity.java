package org.speedynote.app;

import android.content.Intent;
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
 * 2. Enable high-rate stylus input via requestUnbufferedDispatch() (BUG-A004)
 * 
 * This is necessary because:
 * - PDF picker: We need to process the file picker result while SAF permission is valid
 * - Stylus input: Android batches touch events at 60Hz by default; we want 240Hz
 */
public class SpeedyNoteActivity extends QtActivity {
    private static final String TAG = "SpeedyNoteActivity";
    
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
        
        // If not handled, pass to Qt's default handling
        super.onActivityResult(requestCode, resultCode, data);
    }
}

