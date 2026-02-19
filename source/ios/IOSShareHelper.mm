#include "IOSShareHelper.h"

#ifdef Q_OS_IOS

#include <QDebug>
#include <QFile>

#import <UIKit/UIKit.h>

static UIWindowScene *activeWindowScene()
{
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState == UISceneActivationStateForegroundActive &&
            [scene isKindOfClass:[UIWindowScene class]]) {
            return (UIWindowScene *)scene;
        }
    }
    return nil;
}

// Present a UIActivityViewController from a dedicated window so that Qt's
// QIOSView touch handling does not interfere with the share sheet on iPad.
static void presentShareSheet(UIActivityViewController *avc)
{
    UIWindowScene *scene = activeWindowScene();
    if (!scene) {
        qWarning() << "IOSShareHelper: No active UIWindowScene";
        return;
    }

    UIWindow *shareWindow = [[UIWindow alloc] initWithWindowScene:scene];
    UIViewController *hostVC = [[UIViewController alloc] init];
    hostVC.view.backgroundColor = [UIColor clearColor];
    shareWindow.rootViewController = hostVC;
    shareWindow.windowLevel = UIWindowLevelAlert;
    shareWindow.backgroundColor = [UIColor clearColor];
    [shareWindow makeKeyAndVisible];

    if (avc.popoverPresentationController) {
        avc.popoverPresentationController.sourceView = hostVC.view;
        avc.popoverPresentationController.sourceRect = CGRectMake(
            hostVC.view.bounds.size.width / 2, hostVC.view.bounds.size.height / 2, 0, 0);
        avc.popoverPresentationController.permittedArrowDirections = 0;
    }

    // Tear down the dedicated window after the share sheet is dismissed.
    avc.completionWithItemsHandler = ^(UIActivityType, BOOL, NSArray *, NSError *) {
        shareWindow.hidden = YES;
    };

    [hostVC presentViewController:avc animated:YES completion:nil];
}

namespace IOSShareHelper {

void shareFile(const QString& filePath, const QString& mimeType, const QString& title)
{
    Q_UNUSED(mimeType);
    Q_UNUSED(title);

    if (!QFile::exists(filePath)) {
        qWarning() << "IOSShareHelper::shareFile: File does not exist:" << filePath;
        return;
    }

    NSURL *fileURL = [NSURL fileURLWithPath:filePath.toNSString()];
    UIActivityViewController *avc = [[UIActivityViewController alloc]
        initWithActivityItems:@[fileURL]
        applicationActivities:nil];

    presentShareSheet(avc);
}

void shareMultipleFiles(const QStringList& filePaths, const QString& mimeType, const QString& title)
{
    Q_UNUSED(mimeType);
    Q_UNUSED(title);

    NSMutableArray *items = [NSMutableArray array];
    for (const QString& path : filePaths) {
        if (QFile::exists(path)) {
            [items addObject:[NSURL fileURLWithPath:path.toNSString()]];
        }
    }

    if (items.count == 0) {
        qWarning() << "IOSShareHelper::shareMultipleFiles: No valid files";
        return;
    }

    UIActivityViewController *avc = [[UIActivityViewController alloc]
        initWithActivityItems:items
        applicationActivities:nil];

    presentShareSheet(avc);
}

bool isAvailable()
{
    return true;
}

} // namespace IOSShareHelper

#else // !Q_OS_IOS

namespace IOSShareHelper {

void shareFile(const QString& /*filePath*/, const QString& /*mimeType*/, const QString& /*title*/) {}
void shareMultipleFiles(const QStringList& /*filePaths*/, const QString& /*mimeType*/, const QString& /*title*/) {}
bool isAvailable() { return false; }

} // namespace IOSShareHelper

#endif // Q_OS_IOS
