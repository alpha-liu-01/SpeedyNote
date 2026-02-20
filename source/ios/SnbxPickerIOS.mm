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
    fprintf(stderr, "[SnbxPickerIOS] delegate: didPickDocumentsAtURLs (count=%lu)\n",
            (unsigned long)urls.count);

    NSFileManager *fm = [NSFileManager defaultManager];
    NSMutableArray<NSString *> *results = [NSMutableArray array];

    for (NSURL *url in urls) {
        NSString *fileName = url.lastPathComponent;

        if (![fileName.pathExtension.lowercaseString isEqualToString:@"snbx"]) {
            fprintf(stderr, "[SnbxPickerIOS] skipping non-.snbx: %s\n", fileName.UTF8String);
            continue;
        }

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
            fprintf(stderr, "[SnbxPickerIOS] imported: %s\n", destPath.UTF8String);
            [results addObject:destPath];
        } else {
            fprintf(stderr, "[SnbxPickerIOS] copy failed for %s: %s\n",
                    fileName.UTF8String, error.localizedDescription.UTF8String);
        }
    }

    [self finish:results];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    fprintf(stderr, "[SnbxPickerIOS] delegate: cancelled\n");
    [self finish:@[]];
}

- (void)finish:(NSArray<NSString *> *)paths
{
    self.pickerWindow.hidden = YES;
    self.pickerWindow = nil;

    if (self.completionBlock)
        self.completionBlock(paths);
}

- (void)dealloc
{
    fprintf(stderr, "[SnbxPickerIOS] delegate: dealloc\n");
}

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
    fprintf(stderr, "[SnbxPickerIOS] pickSnbxFiles called (active=%d)\n", s_active);

    if (s_active) {
        if (completion) completion({});
        return;
    }
    s_active = true;
    QDir().mkpath(destDir);

    UIWindowScene *scene = activeWindowScene();
    if (!scene) {
        fprintf(stderr, "[SnbxPickerIOS] ERROR: no active window scene\n");
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

    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeItem]];
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

    fprintf(stderr, "[SnbxPickerIOS] presenting picker from dedicated window (delegate=%p)\n",
            (void *)s_delegate);
    [pickerWindow.rootViewController presentViewController:picker animated:YES completion:^{
        fprintf(stderr, "[SnbxPickerIOS] presentation complete\n");
    }];
}

} // namespace SnbxPickerIOS

#endif // Q_OS_IOS
