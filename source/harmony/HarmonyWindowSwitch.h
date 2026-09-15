#pragma once

// ============================================================================
// Launcher <-> MainWindow switching
// ============================================================================
// SpeedyNote switches between the Launcher and a MainWindow by showing one
// top-level window and hiding the other. That is an ordinary z-order change
// everywhere except HarmonyOS, where a top-level window is an *ability instance*
// and the platform gives Qt no way to bring a backgrounded one forward:
//
//   - Qt backs hide()/show() of a top-level with the platform's minimize() and
//     restore(). minimize() works; restore() is a PC/2-in-1-only call that a
//     tablet refuses outright ("Restore: This is not PC or PcAppInPad, not
//     supported"), so the pair is a one-way door. Hiding either window made the
//     app look like it had vanished, which was the original bug. Declaring only
//     "tablet;2in1" does not help -- the check is about the hardware, not the
//     manifest -- and crashed the app on a tablet besides.
//   - hideAbility()/showAbility(), which Qt prefers when available, fail too
//     (error 16000067), so there is nothing to fall back to.
//   - Raising does not help either: QWidget::raise() reaches showWindow(), which
//     orders windows *within* an instance that is already foreground.
//
// So the app keeps a single ability instance. The Launcher is the one that owns
// it -- it is the app's first window and outlives every MainWindow -- and each
// MainWindow is tagged as a *sub-window* of it (adoptAsLauncherSubWindow) rather
// than being allowed to become an instance of its own. Sub-windows live in their
// parent's stage, so both directions of the switch are ordinary z-order changes
// again: raise() reaches raiseToAppTop(), and hide() is a real hide instead of a
// minimise. The task switcher shows one entry, and the ability manager is not
// involved in a switch at all.
//
// The Launcher itself must still never be hidden, because hiding *it* would
// minimise the instance -- hence mustStayResident().
//
// This lives under harmony/ because HarmonyOS is the only reason it exists, but
// it is built on every platform: the show/raise/geometry sequence is shared, and
// only the hide is conditional.
// ============================================================================

#include <QRect>
#include <QtGlobal>

class QWidget;

namespace HarmonyWindowSwitch {

enum class OutgoingPolicy {
    // Hide `outgoing` as soon as `incoming` is up. Off HarmonyOS this is the
    // immediate, un-animated hide the toggle has always done.
    Hide,
    // The caller dismisses `outgoing` itself, because it wants its own fade-out
    // (Launcher::hideWithAnimation) rather than an immediate hide.
    LeaveToCaller,
};

// Bring `incoming` to the front, adopting `outgoing`'s window state and
// geometry, and fade it in. `outgoing` is then hidden, unless the policy leaves
// that to the caller or mustStayResident() forbids it. A null `outgoing` skips
// both the geometry adoption and the hide.
void switchTo(QWidget* incoming, QWidget* outgoing,
              OutgoingPolicy policy = OutgoingPolicy::Hide, int fadeDurationMs = 150);

// The geometry `incoming` should take on so that a switch does not move the app
// around the screen: normally `reference`'s, `reference` being the window being
// switched away from. On HarmonyOS a MainWindow is a sub-window and gets the
// screen's safe area instead, because only the Launcher's own window has the
// platform's status-bar insets applied to its content -- copying the Launcher's
// geometry would slide a MainWindow's toolbar up underneath the status bar.
// Returns an invalid rect when there is nothing to go on, meaning "leave as is".
QRect targetGeometry(const QWidget* incoming, const QWidget* reference);

// HarmonyOS: make `window` a sub-window of the Launcher's ability instead of
// letting it become an ability instance of its own. Call it on every MainWindow
// before the window is first shown -- once the platform window exists the view
// type is already decided. A no-op on every other platform, and on HarmonyOS if
// there is no Launcher yet, which is only true of the Launcher itself.
void adoptAsLauncherSubWindow(QWidget* window);

// True when a window has to be left visible to move it out of the way, because
// hiding it would minimise an ability instance nothing can restore. Only ever
// true of the Launcher, and only on HarmonyOS.
bool mustStayResident(const QWidget* window);

// True when the Launcher, rather than a MainWindow, is the front window.
// Off HarmonyOS this is just `launcher->isVisible()`. On HarmonyOS the Launcher
// stays visible while it sits behind a MainWindow, so visibility cannot answer
// the question and the direction recorded by switchTo() is used instead.
bool launcherInFront(const QWidget* launcher);

// For the HarmonyOS paths that show or dismiss a window without going through
// switchTo(): cold start, and Launcher::hideWithAnimation().
void setLauncherInFront(bool inFront);

} // namespace HarmonyWindowSwitch
