#include "SnbxPickerIOS.h"

#ifdef Q_OS_IOS

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ============================================================================
// Delegate for UIDocumentPickerViewController (.snbx multi-select)
// ============================================================================

@interface SNSnbxPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *destDir;
@property (nonatomic, copy) void (^completionBlock)(NSArray<NSString *> *localPaths);
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
            fprintf(stderr, "[SnbxPickerIOS] move failed for %s: %s\n",
                    fileName.UTF8String, error.localizedDescription.UTF8String);
        }
    }

    if (self.completionBlock) self.completionBlock(results);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    Q_UNUSED(controller);
    fprintf(stderr, "[SnbxPickerIOS] delegate: cancelled\n");
    if (self.completionBlock) self.completionBlock(@[]);
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

    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeItem]];
    picker.allowsMultipleSelection = YES;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;

    s_delegate = [[SNSnbxPickerDelegate alloc] init];
    s_delegate.destDir = destDir.toNSString();

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

    UIViewController *vc = topmostViewController();
    if (!vc) {
        fprintf(stderr, "[SnbxPickerIOS] ERROR: no topmost VC\n");
        s_delegate = nil;
        s_active = false;
        if (*shared) (*shared)({});
        return;
    }

    fprintf(stderr, "[SnbxPickerIOS] presenting picker from %s (delegate=%p)\n",
            NSStringFromClass([vc class]).UTF8String, (void *)s_delegate);
    [vc presentViewController:picker animated:YES completion:^{
        fprintf(stderr, "[SnbxPickerIOS] presentation complete\n");
    }];
}

} // namespace SnbxPickerIOS

#endif // Q_OS_IOS
