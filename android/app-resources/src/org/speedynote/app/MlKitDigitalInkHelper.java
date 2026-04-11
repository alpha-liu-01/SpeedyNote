package org.speedynote.app;

import android.content.Context;
import android.util.Log;

import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.tasks.Tasks;
import com.google.mlkit.common.model.DownloadConditions;
import com.google.mlkit.common.model.RemoteModelManager;
import com.google.mlkit.vision.digitalink.common.RecognitionResult;
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognition;
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognitionModel;
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognitionModelIdentifier;
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognizer;
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognizerOptions;
import com.google.mlkit.vision.digitalink.recognition.Ink;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * Java bridge for ML Kit Digital Ink Recognition.
 * All methods are static and blocking (called from OcrWorker's background thread via JNI).
 */
public class MlKitDigitalInkHelper {
    private static final String TAG = "MlKitDigitalInk";
    private static final long TIMEOUT_SECONDS = 30;

    /**
     * Check if Google Play Services (required by ML Kit) are available.
     */
    public static boolean isAvailable(Context context) {
        try {
            int result = GoogleApiAvailability.getInstance()
                    .isGooglePlayServicesAvailable(context);
            return result == ConnectionResult.SUCCESS;
        } catch (Exception e) {
            Log.w(TAG, "GMS availability check failed, assuming available: " + e.getMessage());
            return true;
        }
    }

    /**
     * Returns BCP 47 language tags for all supported recognition models,
     * excluding gesture classifiers.
     */
    public static String[] getAvailableLanguages() {
        try {
            List<String> tags = new ArrayList<>();
            for (DigitalInkRecognitionModelIdentifier id :
                    DigitalInkRecognitionModelIdentifier.allModelIdentifiers()) {
                String tag = id.getLanguageTag();
                if (tag != null && !tag.contains("GESTURE")) {
                    tags.add(tag);
                }
            }
            return tags.toArray(new String[0]);
        } catch (Exception e) {
            Log.e(TAG, "Failed to enumerate languages: " + e.getMessage());
            return new String[0];
        }
    }

    /**
     * Ensure the model for the given language tag is downloaded.
     * Blocks until download completes or times out.
     *
     * @return true if the model is ready, false on failure.
     */
    public static boolean ensureModelDownloaded(String languageTag) {
        try {
            DigitalInkRecognitionModelIdentifier modelId =
                    DigitalInkRecognitionModelIdentifier.fromLanguageTag(languageTag);
            if (modelId == null) {
                Log.e(TAG, "No model found for language tag: " + languageTag);
                return false;
            }

            DigitalInkRecognitionModel model =
                    DigitalInkRecognitionModel.builder(modelId).build();
            RemoteModelManager manager = RemoteModelManager.getInstance();

            Boolean downloaded = Tasks.await(
                    manager.isModelDownloaded(model), TIMEOUT_SECONDS, TimeUnit.SECONDS);
            if (Boolean.TRUE.equals(downloaded)) {
                return true;
            }

            Tasks.await(
                    manager.download(model, new DownloadConditions.Builder().build()),
                    TIMEOUT_SECONDS, TimeUnit.SECONDS);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Model download failed for " + languageTag + ": " + e.getMessage());
            return false;
        }
    }

    /**
     * Recognize handwritten strokes.
     *
     * @param points        Flat array: [x0, y0, t0, x1, y1, t1, ...] (triples)
     * @param strokeLengths Number of points per stroke
     * @param languageTag   BCP 47 tag, e.g. "en-US"
     * @return Top recognition candidate text, or empty string on failure
     */
    public static String recognize(float[] points, int[] strokeLengths, String languageTag) {
        DigitalInkRecognizer recognizer = null;
        try {
            // Build Ink from flat arrays
            Ink.Builder inkBuilder = Ink.builder();
            int offset = 0;
            for (int strokeLen : strokeLengths) {
                Ink.Stroke.Builder strokeBuilder = Ink.Stroke.builder();
                for (int j = 0; j < strokeLen; j++) {
                    float x = points[offset];
                    float y = points[offset + 1];
                    long t = (long) points[offset + 2];
                    strokeBuilder.addPoint(Ink.Point.create(x, y, t));
                    offset += 3;
                }
                inkBuilder.addStroke(strokeBuilder.build());
            }
            Ink ink = inkBuilder.build();

            // Build recognizer
            DigitalInkRecognitionModelIdentifier modelId =
                    DigitalInkRecognitionModelIdentifier.fromLanguageTag(languageTag);
            if (modelId == null) {
                Log.e(TAG, "No model for language tag: " + languageTag);
                return "";
            }

            DigitalInkRecognitionModel model =
                    DigitalInkRecognitionModel.builder(modelId).build();
            recognizer = DigitalInkRecognition.getClient(
                    DigitalInkRecognizerOptions.builder(model).build());

            RecognitionResult result = Tasks.await(
                    recognizer.recognize(ink), TIMEOUT_SECONDS, TimeUnit.SECONDS);

            if (result.getCandidates().isEmpty()) {
                return "";
            }
            return result.getCandidates().get(0).getText();
        } catch (Exception e) {
            Log.e(TAG, "Recognition failed: " + e.getMessage());
            return "";
        } finally {
            if (recognizer != null) {
                try {
                    recognizer.close();
                } catch (Exception e) {
                    Log.w(TAG, "Error closing recognizer: " + e.getMessage());
                }
            }
        }
    }
}
