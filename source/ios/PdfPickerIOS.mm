#include "PdfPickerIOS.h"

#ifdef Q_OS_IOS

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ============================================================================
// Dedicated picker window — isolates the UIDocumentPickerViewController from
// Qt's UIView hierarchy so that Qt does not intercept touch events destined
// for the remote view controller.
// ============================================================================

@interface SNPdfPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *destDir;
@property (nonatomic, copy) void (^completionBlock)(NSString *localPath);
@property (nonatomic, strong) UIWindow *pickerWindow;
@end

@implementation SNPdfPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    Q_UNUSED(controller);
    fprintf(stderr, "[PdfPickerIOS] delegate: didPickDocumentsAtURLs (count=%lu)\n",
            (unsigned long)urls.count);

    if (urls.count == 0) {
        [self finish:nil];
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
        [self finish:destPath];
    } else {
        fprintf(stderr, "[PdfPickerIOS] copy failed: %s\n",
                error.localizedDescription.UTF8String);
        [self finish:nil];
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    fprintf(stderr, "[PdfPickerIOS] delegate: cancelled\n");
    [self finish:nil];
}

- (void)finish:(NSString *)localPath
{
    self.pickerWindow.hidden = YES;
    self.pickerWindow = nil;

    if (self.completionBlock)
        self.completionBlock(localPath);
}

- (void)dealloc
{
    if (self.pickerWindow) {
        self.pickerWindow.hidden = YES;
        self.pickerWindow.windowScene = nil;
        self.pickerWindow = nil;
    }
    fprintf(stderr, "[PdfPickerIOS] delegate: dealloc\n");
}

@end

// ============================================================================
// Static state
// ============================================================================

namespace {
    static bool s_pickerActive = false;
    static SNPdfPickerDelegate *s_delegate = nil;
}

static UIWindowScene *activeWindowScene()
{
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes) {
        if (s.activationState == UISceneActivationStateForegroundActive &&
            [s isKindOfClass:[UIWindowScene class]]) {
            return (UIWindowScene *)s;
        }
    }
    return nil;
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

    UIWindowScene *scene = activeWindowScene();
    if (!scene) {
        fprintf(stderr, "[PdfPickerIOS] ERROR: no active window scene\n");
        s_pickerActive = false;
        if (completion) completion(QString());
        return;
    }

    // Dedicated UIWindow above Qt's windows — Qt cannot intercept touches here
    UIWindow *pickerWindow = [[UIWindow alloc] initWithWindowScene:scene];
    pickerWindow.windowLevel = UIWindowLevelAlert + 1;
    pickerWindow.rootViewController = [[UIViewController alloc] init];
    pickerWindow.rootViewController.view.backgroundColor = [UIColor clearColor];
    [pickerWindow makeKeyAndVisible];

    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypePDF]];
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;

    s_delegate = [[SNPdfPickerDelegate alloc] init];
    s_delegate.destDir = destDir.toNSString();
    s_delegate.pickerWindow = pickerWindow;

    auto shared = std::make_shared<std::function<void(const QString&)>>(std::move(completion));
    s_delegate.completionBlock = ^(NSString *localPath) {
        QString result = localPath ? QString::fromNSString(localPath) : QString();
        s_delegate = nil;
        s_pickerActive = false;
        if (*shared) (*shared)(result);
    };

    picker.delegate = s_delegate;

    fprintf(stderr, "[PdfPickerIOS] presenting picker from dedicated window (delegate=%p)\n",
            (void *)s_delegate);
    [pickerWindow.rootViewController presentViewController:picker animated:YES completion:^{
        fprintf(stderr, "[PdfPickerIOS] presentation complete\n");
    }];
}

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
