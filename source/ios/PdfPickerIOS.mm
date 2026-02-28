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

    if (urls.count == 0) {
        [self finish:nil];
        return;
    }

    NSURL *url = urls.firstObject;

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
        [self finish:destPath];
    } else {
        [self finish:nil];
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    [self finish:nil];
}

- (void)finish:(NSString *)localPath
{
    self.pickerWindow.hidden = YES;
    self.pickerWindow = nil;

    if (self.completionBlock)
        self.completionBlock(localPath);
}

// ARC manages [super dealloc] automatically; suppress the false-positive warning.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-missing-super-calls"
- (void)dealloc
{
    if (self.pickerWindow) {
        self.pickerWindow.hidden = YES;
        self.pickerWindow.windowScene = nil;
        self.pickerWindow = nil;
    }
}
#pragma clang diagnostic pop

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
    if (s_pickerActive) {
        if (completion) completion(QString());
        return;
    }
    s_pickerActive = true;
    QDir().mkpath(destDir);

    UIWindowScene *scene = activeWindowScene();
    if (!scene) {
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

    [pickerWindow.rootViewController presentViewController:picker animated:YES completion:nil];
}

} // namespace PdfPickerIOS

#endif // Q_OS_IOS
