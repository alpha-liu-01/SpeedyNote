#include "IOSPlatformHelper.h"

#ifdef Q_OS_IOS

#include <QApplication>
#include <QFont>
#include <QLocale>
#include <QPalette>

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

namespace IOSPlatformHelper {

bool isDarkMode()
{
    UITraitCollection *tc = [UITraitCollection currentTraitCollection];
    return tc.userInterfaceStyle == UIUserInterfaceStyleDark;
}

void applyPalette(QApplication& app)
{
    app.setStyle("Fusion");

    bool darkMode = isDarkMode();

    if (darkMode) {
        QPalette darkPalette;

        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor blue("#316882");

        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::PlaceholderText, gray);

        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));

        app.setPalette(darkPalette);
    } else {
        QPalette lightPalette;

        QColor lightGray(240, 240, 240);
        QColor gray(160, 160, 160);
        QColor blue(0, 120, 215);

        lightPalette.setColor(QPalette::Window, QColor(240, 240, 240));
        lightPalette.setColor(QPalette::WindowText, Qt::black);
        lightPalette.setColor(QPalette::Base, Qt::white);
        lightPalette.setColor(QPalette::AlternateBase, lightGray);
        lightPalette.setColor(QPalette::Text, Qt::black);
        lightPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
        lightPalette.setColor(QPalette::ToolTipText, Qt::black);
        lightPalette.setColor(QPalette::Button, lightGray);
        lightPalette.setColor(QPalette::ButtonText, Qt::black);
        lightPalette.setColor(QPalette::Light, Qt::white);
        lightPalette.setColor(QPalette::Midlight, QColor(227, 227, 227));
        lightPalette.setColor(QPalette::Dark, QColor(160, 160, 160));
        lightPalette.setColor(QPalette::Mid, QColor(200, 200, 200));
        lightPalette.setColor(QPalette::Shadow, QColor(105, 105, 105));
        lightPalette.setColor(QPalette::BrightText, Qt::red);
        lightPalette.setColor(QPalette::Link, blue);
        lightPalette.setColor(QPalette::LinkVisited, QColor(blue).darker());
        lightPalette.setColor(QPalette::Highlight, blue);
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);
        lightPalette.setColor(QPalette::PlaceholderText, gray);

        lightPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Base, lightGray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Button, lightGray);
        lightPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(180, 180, 180));

        app.setPalette(lightPalette);
    }
}

void applyFonts(QApplication& app)
{
    QString locale = QLocale::system().name();

    QFont font(".AppleSystemUIFont", 14);
    font.setStyleHint(QFont::SansSerif);

    if (locale.startsWith("zh_CN") || locale.startsWith("zh_Hans")) {
        font.setFamilies({".AppleSystemUIFont", "PingFang SC",
                          "Heiti SC", "STHeitiSC-Light"});
    } else if (locale.startsWith("zh_TW") || locale.startsWith("zh_HK") || locale.startsWith("zh_Hant")) {
        font.setFamilies({".AppleSystemUIFont", "PingFang TC",
                          "Heiti TC", "STHeitiTC-Light"});
    } else if (locale.startsWith("ja")) {
        font.setFamilies({".AppleSystemUIFont", "Hiragino Sans",
                          "Hiragino Kaku Gothic ProN"});
    } else if (locale.startsWith("ko")) {
        font.setFamilies({".AppleSystemUIFont", "Apple SD Gothic Neo"});
    } else {
        font.setFamilies({".AppleSystemUIFont", "PingFang SC"});
    }

    app.setFont(font);
}

static bool s_swizzled = false;

void disableEditMenuOverlay()
{
    if (s_swizzled)
        return;

    Class cls = NSClassFromString(@"QIOSTapRecognizer");
    if (!cls) {
        fprintf(stderr, "[IOSPlatformHelper] QIOSTapRecognizer class not found, skipping\n");
        return;
    }

    // Swizzle touchesEnded:withEvent: to a no-op. This is permanent —
    // no matter how many times Qt re-adds the recognizer to new views,
    // its dangerous async block (which captures a stale QPlatformWindow*)
    // will never be dispatched.
    SEL sel = @selector(touchesEnded:withEvent:);
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) {
        fprintf(stderr, "[IOSPlatformHelper] touchesEnded:withEvent: not found on QIOSTapRecognizer\n");
        return;
    }

    IMP noop = imp_implementationWithBlock(^(id, NSSet<UITouch *> *, UIEvent *) {});
    method_setImplementation(m, noop);
    s_swizzled = true;
    fprintf(stderr, "[IOSPlatformHelper] swizzled QIOSTapRecognizer touchesEnded:withEvent: to no-op\n");
}

} // namespace IOSPlatformHelper

#endif // Q_OS_IOS
