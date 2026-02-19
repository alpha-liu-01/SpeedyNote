#include "PdfPickerIOS.h"

#ifdef Q_OS_IOS

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ============================================================================
// Delegate for UIDocumentPickerViewController
// ============================================================================

@interface SNPdfPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *destDir;
@property (nonatomic, copy) void (^completionBlock)(NSString *localPath);
@end

@implementation SNPdfPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    Q_UNUSED(controller);
    fprintf(stderr, "[PdfPickerIOS] delegate: didPickDocumentsAtURLs (count=%lu)\n",
            (unsigned long)urls.count);

    if (urls.count == 0) {
        if (self.completionBlock) self.completionBlock(nil);
        return;
    }

    NSURL *url = urls.firstObject;
    fprintf(stderr, "[PdfPickerIOS] picked URL: %s\n", url.absoluteString.UTF8String);

    BOOL accessed = [url startAccessingSecurityScopedResource];
    NSString *fileName = url.lastPathComponent;
    NSString *destPath = [self.destDir stringByAppendingPathComponent:fileName];

    NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:destPath]) {
        NSString *baseName = [fileName stringByDeletingPathExtension];
        NSString *ext = [fileName pathExtension];
        int counter = 1;
        do {
            NSString *newName = [NSString stringWithFormat:@"%@_%d.%@",
                baseName, counter++, ext];
            destPath = [self.destDir stringByAppendingPathComponent:newName];
        } while ([fm fileExistsAtPath:destPath]);
    }

    NSError *error = nil;
    BOOL ok = [fm copyItemAtURL:url
                          toURL:[NSURL fileURLWithPath:destPath]
                          error:&error];
    if (accessed) {
        [url stopAccessingSecurityScopedResource];
    }

    if (ok) {
        fprintf(stderr, "[PdfPickerIOS] copied to %s\n", destPath.UTF8String);
        if (self.completionBlock) self.completionBlock(destPath);
    } else {
        fprintf(stderr, "[PdfPickerIOS] copy failed: %s\n",
                error.localizedDescription.UTF8String);
        if (self.completionBlock) self.completionBlock(nil);
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    fprintf(stderr, "[PdfPickerIOS] delegate: cancelled\n");
    if (self.completionBlock) self.completionBlock(nil);
}

- (void)dealloc
{
    fprintf(stderr, "[PdfPickerIOS] delegate: dealloc\n");
}

@end

// ============================================================================
// Static state
// ============================================================================

namespace {
    static bool s_pickerActive = false;
    // Strong reference keeps the delegate alive (picker.delegate is weak).
    static SNPdfPickerDelegate *s_delegate = nil;
}

static UIViewController *topmostViewController()
{
    UIWindowScene *scene = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes) {
        if (s.activationState == UISceneActivationStateForegroundActive &&
            [s isKindOfClass:[UIWindowScene class]]) {
            scene = (UIWindowScene *)s;
            break;
        }
    }
    if (!scene) return nil;

    UIViewController *vc = scene.windows.firstObject.rootViewController;
    while (vc.presentedViewController) {
        vc = vc.presentedViewController;
    }
    return vc;
}

namespace PdfPickerIOS {

void pickPdfFile(std::function<void(const QString&)> completion)
{
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
    pickPdfFile(destDir, std::move(completion));
}

void pickPdfFile(const QString& destDir, std::function<void(const QString&)> completion)
{
    fprintf(stderr, "[PdfPickerIOS] pickPdfFile called (active=%d)\n", s_pickerActive);

    if (s_pickerActive) {
        if (completion) completion(QString());
        return;
    }
    s_pickerActive = true;
    QDir().mkpath(destDir);

    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypePDF]];
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;

    s_delegate = [[SNPdfPickerDelegate alloc] init];
    s_delegate.destDir = destDir.toNSString();

    auto shared = std::make_shared<std::function<void(const QString&)>>(std::move(completion));
    s_delegate.completionBlock = ^(NSString *localPath) {
        QString result = localPath ? QString::fromNSString(localPath) : QString();
        s_delegate = nil;
        s_pickerActive = false;
        if (*shared) (*shared)(result);
    };

    picker.delegate = s_delegate;

    UIViewController *vc = topmostViewController();
    if (!vc) {
        fprintf(stderr, "[PdfPickerIOS] ERROR: no topmost VC\n");
        s_delegate = nil;
        s_pickerActive = false;
        if (*shared) (*shared)(QString());
        return;
    }

    fprintf(stderr, "[PdfPickerIOS] presenting picker from %s (delegate=%p)\n",
            NSStringFromClass([vc class]).UTF8String, (void *)s_delegate);
    [vc presentViewController:picker animated:YES completion:^{
        fprintf(stderr, "[PdfPickerIOS] presentation complete\n");
    }];
}

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
