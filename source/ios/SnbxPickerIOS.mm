#include "SnbxPickerIOS.h"

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

@interface SNSnbxPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *destDir;
@property (nonatomic, copy) void (^completionBlock)(NSArray<NSString *> *localPaths);
@property (nonatomic, strong) UIWindow *pickerWindow;
@end

@implementation SNSnbxPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    Q_UNUSED(controller);

    NSFileManager *fm = [NSFileManager defaultManager];
    NSMutableArray<NSString *> *results = [NSMutableArray array];

    for (NSURL *url in urls) {
        NSString *fileName = url.lastPathComponent;

        if (![fileName.pathExtension.lowercaseString isEqualToString:@"snbx"])
            continue;

        BOOL accessed = [url startAccessingSecurityScopedResource];
        NSString *destPath = [self.destDir stringByAppendingPathComponent:fileName];

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
            [results addObject:destPath];
        }
    }

    [self finish:results];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    [self finish:@[]];
}

- (void)finish:(NSArray<NSString *> *)paths
{
    self.pickerWindow.hidden = YES;
    self.pickerWindow = nil;

    if (self.completionBlock)
        self.completionBlock(paths);
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
    static bool s_active = false;
    static SNSnbxPickerDelegate *s_delegate = nil;
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

namespace SnbxPickerIOS {

void pickSnbxFiles(std::function<void(const QStringList&)> completion)
{
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/imports";
    pickSnbxFiles(destDir, std::move(completion));
}

void pickSnbxFiles(const QString& destDir, std::function<void(const QStringList&)> completion)
{
    if (s_active) {
        if (completion) completion({});
        return;
    }
    s_active = true;
    QDir().mkpath(destDir);

    UIWindowScene *scene = activeWindowScene();
    if (!scene) {
        s_active = false;
        if (completion) completion({});
        return;
    }

    // Dedicated UIWindow above Qt's windows — Qt cannot intercept touches here
    UIWindow *pickerWindow = [[UIWindow alloc] initWithWindowScene:scene];
    pickerWindow.windowLevel = UIWindowLevelAlert + 1;
    pickerWindow.rootViewController = [[UIViewController alloc] init];
    pickerWindow.rootViewController.view.backgroundColor = [UIColor clearColor];
    [pickerWindow makeKeyAndVisible];

    UTType *snbxType = [UTType typeWithIdentifier:@"org.speedynote.snbx"];
    if (!snbxType)
        snbxType = UTTypeItem;

    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[snbxType]];
    picker.allowsMultipleSelection = YES;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;

    s_delegate = [[SNSnbxPickerDelegate alloc] init];
    s_delegate.destDir = destDir.toNSString();
    s_delegate.pickerWindow = pickerWindow;

    auto shared = std::make_shared<std::function<void(const QStringList&)>>(std::move(completion));
    s_delegate.completionBlock = ^(NSArray<NSString *> *localPaths) {
        QStringList result;
        for (NSString *path in localPaths) {
            result.append(QString::fromNSString(path));
        }
        s_delegate = nil;
        s_active = false;
        if (*shared) (*shared)(result);
    };

    picker.delegate = s_delegate;

    [pickerWindow.rootViewController presentViewController:picker animated:YES completion:nil];
}

} // namespace SnbxPickerIOS

#endif // Q_OS_IOS
