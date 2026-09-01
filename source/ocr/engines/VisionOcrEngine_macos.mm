#include "VisionOcrEngine.h"

#if defined(SPEEDYNOTE_HAS_VISION_OCR)

#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// isAvailable
// ---------------------------------------------------------------------------
// macOS 12+ (the deployment target) always ships Vision text recognition; the
// runtime class check is a cheap safety net for unusual environments.
bool VisionOcrEngine::isAvailable() const
{
    return NSClassFromString(@"VNRecognizeTextRequest") != nil;
}

// ---------------------------------------------------------------------------
// availableLanguages
// ---------------------------------------------------------------------------
QStringList VisionOcrEngine::availableLanguages() const
{
    if (!m_cachedLanguages.isEmpty())
        return m_cachedLanguages;

    @autoreleasepool {
        VNRecognizeTextRequest *req = [[VNRecognizeTextRequest alloc] init];
        req.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        NSError *err = nil;
        NSArray<NSString *> *langs = [req supportedRecognitionLanguagesAndReturnError:&err];
        if (err) {
            NSLog(@"VisionOcrEngine: supportedRecognitionLanguages error: %@",
                  err.localizedDescription);
        }
        for (NSString *l in langs)
            m_cachedLanguages.append(QString::fromNSString(l));
    }
    return m_cachedLanguages;
}

// ---------------------------------------------------------------------------
// recognizeImage
// ---------------------------------------------------------------------------
RasterOcrEngine::ImageRecognition
VisionOcrEngine::recognizeImage(const QImage& strip, const QString& languageTag)
{
    ImageRecognition out;
    if (strip.isNull())
        return out;

    @autoreleasepool {
        // 1. Normalized strip (single-channel, dark-on-white) -> grayscale
        //    CGImage. This matches the rasterizer's output exactly: no channel
        //    expansion, half the memory of RGBA, and no alpha/byte-order
        //    ambiguity. The convertToFormat copy is kept alive until
        //    performRequests: returns (synchronous), so the data provider never
        //    dangles.
        const QImage img = strip.convertToFormat(QImage::Format_Grayscale8);
        const int W = img.width();
        const int H = img.height();
        if (W <= 0 || H <= 0)
            return out;

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr, img.bits(), static_cast<size_t>(img.sizeInBytes()), nullptr);
        CGImageRef cg = CGImageCreate(
            static_cast<size_t>(W), static_cast<size_t>(H), 8, 8,
            static_cast<size_t>(img.bytesPerLine()), cs,
            kCGImageAlphaNone, provider, nullptr, false, kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(cs);
        if (!cg)
            return out;

        // 2. Configure the request (QA Q6.1).
        VNRecognizeTextRequest *req = [[VNRecognizeTextRequest alloc] init];
        req.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        req.usesLanguageCorrection = YES;
        if (!languageTag.isEmpty())
            req.recognitionLanguages = @[ languageTag.toNSString() ];

        VNImageRequestHandler *handler =
            [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
        NSError *err = nil;
        const BOOL ok = [handler performRequests:@[ req ] error:&err];
        CGImageRelease(cg);

        if (!ok || err || req.results.count == 0) {
            if (err)
                NSLog(@"VisionOcrEngine: performRequests error: %@", err.localizedDescription);
            return out;
        }

        // 3. Walk observations left-to-right, accumulating one box per character
        //    (including a synthetic box for the space we insert between separate
        //    observations) so that charBoxesImage.size() == text.length() always
        //    holds. Vision boxes are normalized with a bottom-left origin, so we
        //    flip Y and denormalize to strip-image pixels (QA Q3.1/Q3.4).
        //
        //    NOTE: Vision does NOT expose per-glyph geometry. -boundingBoxForRange:
        //    returns the box of the WHOLE candidate string for every sub-range
        //    (verified on macOS: all single-char ranges of "Hello" returned one
        //    identical box). Consuming those directly makes per-character overlay
        //    glyphs and text selection collapse onto each other. We therefore
        //    subdivide each observation's box uniformly by character, yielding
        //    monotonic, non-overlapping per-char boxes. The word-level union
        //    (WordSegment::boundingRect) is unchanged, so word placement/copy is
        //    unaffected; only intra-word per-char positions become approximate.
        NSArray<VNRecognizedTextObservation *> *obs = [req.results
            sortedArrayUsingComparator:^NSComparisonResult(VNRecognizedTextObservation *a,
                                                           VNRecognizedTextObservation *b) {
                const CGFloat ax = a.boundingBox.origin.x;
                const CGFloat bx = b.boundingBox.origin.x;
                if (ax < bx) return NSOrderedAscending;
                if (ax > bx) return NSOrderedDescending;
                return NSOrderedSame;
            }];

        QString text;
        QVector<QRectF> boxes;

        // Character-weighted mean of the per-observation scores: one strip can
        // hold several observations, and a long one should not be outvoted by a
        // stray single glyph.
        double confSum = 0.0;
        int confChars = 0;

        for (VNRecognizedTextObservation *o in obs) {
            VNRecognizedText *cand = [[o topCandidates:1] firstObject];
            if (!cand)
                continue;
            NSString *s = cand.string;
            if (s.length == 0)
                continue;

            confSum += static_cast<double>(cand.confidence) * s.length;
            confChars += static_cast<int>(s.length);

            if (!text.isEmpty()) {
                text.append(QLatin1Char(' '));
                boxes.append(QRectF()); // null box keeps the size invariant
            }

            // Observation box in strip-image pixels (flip Y, denormalize).
            const CGRect bb = o.boundingBox;
            const qreal obsX = bb.origin.x * W;
            const qreal obsY = (1.0 - bb.origin.y - bb.size.height) * H;
            const qreal obsW = bb.size.width * W;
            const qreal obsH = bb.size.height * H;
            const int n = static_cast<int>(s.length);
            const qreal charW = n > 0 ? obsW / n : obsW;

            for (NSUInteger i = 0; i < s.length; ++i) {
                const NSRange range = NSMakeRange(i, 1);
                text.append(QString::fromNSString([s substringWithRange:range]));
                boxes.append(QRectF(obsX + static_cast<qreal>(i) * charW,
                                    obsY, charW, obsH));
            }
        }

        out.text = text;
        if (!text.isEmpty() && boxes.size() == text.length())
            out.charBoxesImage = boxes;
        if (confChars > 0)
            out.confidence = static_cast<float>(confSum / confChars);
    }

    return out;
}

#endif // SPEEDYNOTE_HAS_VISION_OCR
