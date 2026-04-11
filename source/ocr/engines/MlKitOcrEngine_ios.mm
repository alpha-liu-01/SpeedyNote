#include "MlKitOcrEngine.h"

#if defined(SPEEDYNOTE_HAS_MLKIT_INK) && defined(Q_OS_IOS)

#include <QDebug>
#include <limits>

#import <MLKitDigitalInkRecognition/MLKitDigitalInkRecognition.h>

static const NSTimeInterval kTimeoutSeconds = 30.0;

// ---------------------------------------------------------------------------
// checkAvailabilityNative
// ---------------------------------------------------------------------------

bool MlKitOcrEngine::checkAvailabilityNative() const
{
    // ML Kit frameworks are statically linked on iOS; if we got here, they're available
    return true;
}

// ---------------------------------------------------------------------------
// queryLanguagesNative
// ---------------------------------------------------------------------------

QStringList MlKitOcrEngine::queryLanguagesNative() const
{
    QStringList languages;
    @autoreleasepool {
        for (MLKDigitalInkRecognitionModelIdentifier *identifier in
             [MLKDigitalInkRecognitionModelIdentifier allModelIdentifiers]) {
            NSString *tag = identifier.languageTag;
            if (tag && ![tag containsString:@"GESTURE"]) {
                languages.append(QString::fromNSString(tag));
            }
        }
    }
    return languages;
}

// ---------------------------------------------------------------------------
// ensureModelDownloadedNative
// ---------------------------------------------------------------------------

bool MlKitOcrEngine::ensureModelDownloadedNative(const QString& languageTag)
{
    @autoreleasepool {
        NSString *nsTag = languageTag.toNSString();
        MLKDigitalInkRecognitionModelIdentifier *identifier =
            [MLKDigitalInkRecognitionModelIdentifier modelIdentifierForLanguageTag:nsTag];
        if (!identifier) {
            qWarning() << "MlKitOcrEngine: No model for language tag:" << languageTag;
            return false;
        }

        MLKDigitalInkRecognitionModel *model =
            [[MLKDigitalInkRecognitionModel alloc] initWithModelIdentifier:identifier];
        MLKModelManager *manager = [MLKModelManager modelManager];

        if ([manager isModelDownloaded:model]) {
            return true;
        }

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block BOOL success = NO;

        MLKModelDownloadConditions *conditions =
            [[MLKModelDownloadConditions alloc] initWithAllowsCellularAccess:YES
                                                 allowsBackgroundDownloading:YES];
        [manager downloadModel:model
                    conditions:conditions
                    completion:^(NSError *error) {
            success = (error == nil);
            if (error) {
                NSLog(@"MlKitOcrEngine: Model download failed: %@", error.localizedDescription);
            }
            dispatch_semaphore_signal(sem);
        }];

        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                                   (int64_t)(kTimeoutSeconds * NSEC_PER_SEC)));
        return success;
    }
}

// ---------------------------------------------------------------------------
// recognizeStrokesNative
// ---------------------------------------------------------------------------

QString MlKitOcrEngine::recognizeStrokesNative(const QVector<VectorStroke>& strokes)
{
    if (strokes.isEmpty())
        return {};

    @autoreleasepool {
        // Determine whether timestamps need synthesizing and find the
        // baseline for normalization (keeps values small & precise).
        bool needSyntheticTimestamps = false;
        qint64 baseTimestamp = std::numeric_limits<qint64>::max();
        for (const auto& stroke : strokes) {
            for (const auto& pt : stroke.points) {
                if (pt.timestamp == 0) {
                    needSyntheticTimestamps = true;
                    break;
                }
                baseTimestamp = qMin(baseTimestamp, pt.timestamp);
            }
            if (needSyntheticTimestamps)
                break;
        }

        int globalPointIdx = 0;

        NSMutableArray<MLKStroke *> *mlkStrokes =
            [NSMutableArray arrayWithCapacity:strokes.size()];

        for (const auto& stroke : strokes) {
            NSMutableArray<MLKStrokePoint *> *mlkPoints =
                [NSMutableArray arrayWithCapacity:stroke.points.size()];

            for (int j = 0; j < stroke.points.size(); ++j) {
                const auto& pt = stroke.points[j];
                float x = static_cast<float>(pt.pos.x());
                float y = static_cast<float>(pt.pos.y());
                NSTimeInterval t;

                if (needSyntheticTimestamps) {
                    t = static_cast<NSTimeInterval>(globalPointIdx * 15);
                } else {
                    t = static_cast<NSTimeInterval>(pt.timestamp - baseTimestamp);
                }

                [mlkPoints addObject:[[MLKStrokePoint alloc] initWithX:x y:y t:t]];
                ++globalPointIdx;
            }

            [mlkStrokes addObject:[[MLKStroke alloc] initWithPoints:mlkPoints]];
        }

        MLKInk *ink = [[MLKInk alloc] initWithStrokes:mlkStrokes];

        // Build recognizer
        NSString *langTag = m_languageTag.toNSString();
        MLKDigitalInkRecognitionModelIdentifier *identifier =
            [MLKDigitalInkRecognitionModelIdentifier modelIdentifierForLanguageTag:langTag];
        if (!identifier) {
            qWarning() << "MlKitOcrEngine: No model for language tag:" << m_languageTag;
            return {};
        }

        MLKDigitalInkRecognitionModel *model =
            [[MLKDigitalInkRecognitionModel alloc] initWithModelIdentifier:identifier];
        MLKDigitalInkRecognizerOptions *options =
            [[MLKDigitalInkRecognizerOptions alloc] initWithModel:model];
        MLKDigitalInkRecognizer *recognizer =
            [MLKDigitalInkRecognizer digitalInkRecognizerWithOptions:options];

        // Recognize with semaphore blocking
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSString *resultText = nil;

        [recognizer recognizeInk:ink
                      completion:^(MLKDigitalInkRecognitionResult *result, NSError *error) {
            if (error) {
                NSLog(@"MlKitOcrEngine: Recognition failed: %@", error.localizedDescription);
            } else if (result.candidates.count > 0) {
                resultText = result.candidates[0].text;
            }
            dispatch_semaphore_signal(sem);
        }];

        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                                   (int64_t)(kTimeoutSeconds * NSEC_PER_SEC)));

        return resultText ? QString::fromNSString(resultText) : QString();
    }
}

#endif // SPEEDYNOTE_HAS_MLKIT_INK && Q_OS_IOS
