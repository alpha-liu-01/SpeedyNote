#include "MacPlatformHelper.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>

namespace MacPlatformHelper {

bool isDarkMode()
{
    // Can be reached before the NSApplication is up, in which case there is no
    // appearance to resolve yet.
    if (!NSApp) {
        return false;
    }

    // effectiveAppearance is the appearance the app resolved to, which already
    // accounts for the system setting and for any per-app override. Matching
    // against both names rather than comparing to DarkAqua directly keeps the
    // accessibility variants (increased contrast) classified correctly.
    NSAppearanceName name = [[NSApp effectiveAppearance]
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua,
                                             NSAppearanceNameDarkAqua ]];
    return [name isEqualToString:NSAppearanceNameDarkAqua];
}

} // namespace MacPlatformHelper

#endif // Q_OS_MACOS
