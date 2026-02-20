#include "IOSPlatformHelper.h"

#ifdef Q_OS_IOS

#include <QApplication>
#include <QDebug>

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

namespace IOSPlatformHelper {

bool isDarkMode()
{
    // TODO Phase 4: Query UITraitCollection.currentTraitCollection.userInterfaceStyle
    // Return true if UIUserInterfaceStyleDark
    qDebug() << "IOSPlatformHelper::isDarkMode: Not yet implemented (stub), returning false";
    return false;
}

void applyPalette(QApplication& app)
{
    Q_UNUSED(app);
    // TODO Phase 4: Detect dark/light mode and set appropriate QPalette
    // Mirror the logic in applyAndroidPalette() from Main.cpp
    qDebug() << "IOSPlatformHelper::applyPalette: Not yet implemented (stub)";
}

void applyFonts(QApplication& app)
{
    Q_UNUSED(app);
    // TODO Phase 4: Set San Francisco system font with appropriate sizing
    // Mirror the logic in applyAndroidFonts() from Main.cpp
    qDebug() << "IOSPlatformHelper::applyFonts: Not yet implemented (stub)";
}

static void removeRecognizersRecursive(UIView *view, Class targetClass)
{
    for (UIGestureRecognizer *gr in [view.gestureRecognizers copy]) {
        if ([gr isKindOfClass:targetClass]) {
            [view removeGestureRecognizer:gr];
            fprintf(stderr, "[IOSPlatformHelper] removed %s from %s\n",
                    class_getName(targetClass),
                    class_getName([view class]));
        }
    }
    for (UIView *sub in view.subviews) {
        removeRecognizersRecursive(sub, targetClass);
    }
}

void disableEditMenuOverlay()
{
    Class tapRecognizerClass = NSClassFromString(@"QIOSTapRecognizer");
    if (!tapRecognizerClass) {
        fprintf(stderr, "[IOSPlatformHelper] QIOSTapRecognizer class not found, skipping\n");
        return;
    }

    for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]])
            continue;
        for (UIWindow *window in scene.windows) {
            removeRecognizersRecursive(window, tapRecognizerClass);
        }
    }
}

} // namespace IOSPlatformHelper

#endif // Q_OS_IOS
