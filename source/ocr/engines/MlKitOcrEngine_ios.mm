#include "MlKitOcrEngine.h"

#if defined(SPEEDYNOTE_HAS_MLKIT_INK) && defined(Q_OS_IOS)

#include <QDebug>
#include <limits>

#import <MLKitDigitalInkRecognition/MLKitDigitalInkRecognition.h>
#import <MLKitCommon/MLKitCommon.h>

static const NSTimeInterval kTimeoutSeconds = 30.0;

// ---------------------------------------------------------------------------
// invalidateNativeRecognizer
// ---------------------------------------------------------------------------

void MlKitOcrEngine::invalidateNativeRecognizer()
{
    if (m_iosRecognizer) {
        CFRelease(m_iosRecognizer);
        m_iosRecognizer = nullptr;
    }
}

static MLKDigitalInkRecognizer *cachedRecognizer(void *&slot, NSString *langTag)
{
    if (slot)
        return (__bridge MLKDigitalInkRecognizer *)slot;

    MLKDigitalInkRecognitionModelIdentifier *identifier =
        [MLKDigitalInkRecognitionModelIdentifier modelIdentifierForLanguageTag:langTag];
    if (!identifier)
        return nil;

    MLKDigitalInkRecognitionModel *model =
        [[MLKDigitalInkRecognitionModel alloc] initWithModelIdentifier:identifier];
    MLKDigitalInkRecognizerOptions *options =
        [[MLKDigitalInkRecognizerOptions alloc] initWithModel:model];
    MLKDigitalInkRecognizer *recognizer =
        [MLKDigitalInkRecognizer digitalInkRecognizerWithOptions:options];

    slot = (void *)CFBridgingRetain(recognizer);
    return recognizer;
}

// ---------------------------------------------------------------------------
// checkAvailabilityNative
// ---------------------------------------------------------------------------

bool MlKitOcrEngine::checkAvailabilityNative() const
{
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
            qWarning() << "MlKitOcrEngine: No model identifier for language tag:" << languageTag;
            return false;
        }

        MLKDigitalInkRecognitionModel *model =
            [[MLKDigitalInkRecognitionModel alloc] initWithModelIdentifier:identifier];
        MLKModelManager *manager = [MLKModelManager modelManager];

        if ([manager isModelDownloaded:model])
            return true;

        NSLog(@"MlKitOcrEngine: Downloading model for '%@'...", nsTag);

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block BOOL success = NO;

        NSString *targetTag = identifier.languageTag;

        id successObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:MLKModelDownloadDidSucceedNotification
                       object:nil
                        queue:[NSOperationQueue mainQueue]
                   usingBlock:^(NSNotification *note) {
            MLKRemoteModel *noteModel = note.userInfo[MLKModelDownloadUserInfoKeyRemoteModel];
            if ([noteModel isKindOfClass:[MLKDigitalInkRecognitionModel class]]) {
                MLKDigitalInkRecognitionModel *dlModel = (MLKDigitalInkRecognitionModel *)noteModel;
                if ([dlModel.modelIdentifier.languageTag isEqualToString:targetTag]) {
                    success = YES;
                    dispatch_semaphore_signal(sem);
                }
            }
        }];

        id failObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:MLKModelDownloadDidFailNotification
                       object:nil
                        queue:[NSOperationQueue mainQueue]
                   usingBlock:^(NSNotification *note) {
            MLKRemoteModel *noteModel = note.userInfo[MLKModelDownloadUserInfoKeyRemoteModel];
            if ([noteModel isKindOfClass:[MLKDigitalInkRecognitionModel class]]) {
                MLKDigitalInkRecognitionModel *dlModel = (MLKDigitalInkRecognitionModel *)noteModel;
                if ([dlModel.modelIdentifier.languageTag isEqualToString:targetTag]) {
                    NSError *error = note.userInfo[MLKModelDownloadUserInfoKeyError];
                    NSLog(@"MlKitOcrEngine: Model download failed for '%@': %@",
                          targetTag, error.localizedDescription);
                    dispatch_semaphore_signal(sem);
                }
            }
        }];

        MLKModelDownloadConditions *conditions =
            [[MLKModelDownloadConditions alloc] initWithAllowsCellularAccess:YES
                                                 allowsBackgroundDownloading:YES];
        [manager downloadModel:model conditions:conditions];

        long waitResult = dispatch_semaphore_wait(
            sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kTimeoutSeconds * NSEC_PER_SEC)));

        [[NSNotificationCenter defaultCenter] removeObserver:successObserver];
        [[NSNotificationCenter defaultCenter] removeObserver:failObserver];

        if (waitResult != 0) {
            NSLog(@"MlKitOcrEngine: Model download timed out for '%@'", nsTag);
            success = [manager isModelDownloaded:model];
        }

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
        NSString *langTag = m_languageTag.toNSString();

        if (!m_modelDownloaded) {
            qWarning() << "MlKitOcrEngine: Model not downloaded for" << m_languageTag;
            return {};
        }

        MLKDigitalInkRecognizer *recognizer = cachedRecognizer(m_iosRecognizer, langTag);
        if (!recognizer) {
            qWarning() << "MlKitOcrEngine: Could not create recognizer for" << m_languageTag;
            return {};
        }

        // Build MLKInk from VectorStroke data
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
                long t;

                if (needSyntheticTimestamps) {
                    t = static_cast<long>(globalPointIdx * 15);
                } else {
                    t = static_cast<long>(pt.timestamp - baseTimestamp);
                }

                [mlkPoints addObject:[[MLKStrokePoint alloc] initWithX:x y:y t:t]];
                ++globalPointIdx;
            }

            [mlkStrokes addObject:[[MLKStroke alloc] initWithPoints:mlkPoints]];
        }

        MLKInk *ink = [[MLKInk alloc] initWithStrokes:mlkStrokes];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSString *resultText = nil;

        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            [recognizer recognizeInk:ink
                          completion:^(MLKDigitalInkRecognitionResult *result, NSError *error) {
                if (error) {
                    NSLog(@"MlKitOcrEngine: Recognition failed: %@", error.localizedDescription);
                } else if (result.candidates.count > 0) {
                    resultText = [result.candidates[0].text copy];
                }
                dispatch_semaphore_signal(sem);
            }];
        });

        long waitResult = dispatch_semaphore_wait(
            sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kTimeoutSeconds * NSEC_PER_SEC)));

        if (waitResult != 0)
            NSLog(@"MlKitOcrEngine: Recognition timed out (%d points)", globalPointIdx);

        return resultText ? QString::fromNSString(resultText) : QString();
    }
}

#endif // SPEEDYNOTE_HAS_MLKIT_INK && Q_OS_IOS
