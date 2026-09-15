#include "HarmonyWindowSwitch.h"

#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_HARMONY
#include <QApplication>
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#include <QtHarmonyExtras/private/qohosabilitycontext_p.h>
#include <QtHarmonyExtras/private/qohosappcontext_p.h>
#include <QtHarmonyExtras/private/qohosoperationstatus_p.h>
#include <QtHarmonyExtras/private/qohoswant_p.h>
#include <QtHarmonyExtras/private/qohoswantinfo_p.h>
#endif

namespace {

// Cold start lands on the Launcher on every platform.
bool s_launcherInFront = true;

// Recognised by class name so that this file, which every platform compiles,
// does not have to depend on the Launcher's header.
bool isLauncher(const QWidget* widget)
{
    return widget != nullptr && widget->inherits("Launcher");
}

#ifdef Q_OS_HARMONY
QWidget* findLauncherOrNull()
{
    const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
    for (QWidget* candidate : topLevels) {
        if (isLauncher(candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

// Raising a window cannot move an ability instance from the background to the
// foreground. QWidget::raise() reaches window.showWindow(), which reorders
// windows within an instance that is already foreground; which instance is
// foreground is the ability manager's decision, and the only way to ask it to
// change its mind is to start the ability again.
//
// QAbility is declared launchType "specified", so rather than creating an
// instance the system asks Qt for an instance key (QAbilityStage.onAcceptWant)
// and routes the request to the live instance holding that key. Qt reads the key
// from the private "io.qt.private.abilityInstanceId" want parameter and, for a
// want carrying none, falls back to the key of the app's first instance -- the
// one the Launcher was created in. A want naming nothing but this ability
// therefore resolves to the Launcher and brings it forward, which is the same
// request the system makes when the user taps the app icon.
//
// This works for the Launcher only. Qt keeps the keys of the instances it starts
// later to itself, so there is no want we can build that names a MainWindow.
bool requestLauncherAbilityForeground()
{
    const std::shared_ptr<QtHarmonyExtras::WantInfo> launchWantInfo =
        QtHarmonyExtras::AppContext::appLaunchWantInfo();
    if (!launchWantInfo) {
        return false;
    }

    // Name this ability by echoing the launch want rather than hardcoding the
    // bundle and module, which the build can rename.
    const QtHarmonyExtras::Want launchWant = launchWantInfo->want();

    QtHarmonyExtras::Want want;
    want.bundleName  = launchWant.bundleName;
    want.moduleName  = launchWant.moduleName;
    want.abilityName = launchWant.abilityName;

    const std::shared_ptr<QtHarmonyExtras::OperationStatus> status =
        QtHarmonyExtras::startAbility(want);
    return status && status->success();
}
#endif

// QWidget::setWindowState() on a hidden widget only updates the internal flag --
// it skips the platform update because isVisible() is false. Applying the state
// to the QWindow as well forces the native window to drop stale fullscreen or
// maximised styling, which would otherwise survive into the next show() and
// produce a frameless or full-screen window.
void clearStaleWindowState(QWidget* widget)
{
    widget->setWindowState(Qt::WindowNoState);
    if (QWindow* handle = widget->windowHandle()) {
        handle->setWindowState(Qt::WindowNoState);
    }
}

} // namespace

namespace HarmonyWindowSwitch {

QRect targetGeometry(const QWidget* incoming, const QWidget* reference)
{
#ifdef Q_OS_HARMONY
    if (!isLauncher(incoming) && reference != nullptr) {
        // Match the Launcher's client rect. A sub-window is placed where we ask,
        // in screen coordinates, and the screen's availableGeometry() is no help
        // here: the platform reports the whole display and no safe-area margins,
        // so the Launcher's own laid-out rect is the only thing that knows where
        // the status bar ends.
        return reference->geometry();
    }
#endif
    return reference != nullptr ? QRect(reference->pos(), reference->size()) : QRect();
}

void adoptAsLauncherSubWindow(QWidget* window)
{
#ifdef Q_OS_HARMONY
    if (window == nullptr || isLauncher(window)) {
        return;
    }

    if (window->windowHandle() != nullptr) {
        // Qt decides the view type when it creates the platform window and reads
        // the tag as part of that, so tagging afterwards would not take effect.
        qWarning("HarmonyWindowSwitch: too late to adopt an already-realised window");
        return;
    }

    QWidget* launcher = findLauncherOrNull();
    if (launcher == nullptr) {
        return;
    }

    // The tag holds a QWindow, so the Launcher has to be realised before we can
    // point at it. It is shown during cold start, ahead of any MainWindow, and it
    // outlives them all, so the pointer stays good for as long as the tag is read.
    launcher->createWinId();
    QWindow* launcherWindow = launcher->windowHandle();
    if (launcherWindow == nullptr) {
        return;
    }

    QPlatformNativeInterface* nativeInterface = QGuiApplication::platformNativeInterface();
    using TagAsSubWindowOf = void (*)(QObject*, QWindow*);
    auto tagAsSubWindowOf = nativeInterface != nullptr
        ? reinterpret_cast<TagAsSubWindowOf>(
              nativeInterface->platformFunction("tagWindowOrWidgetAsSubWindowOf"))
        : nullptr;
    if (tagAsSubWindowOf == nullptr) {
        // Leaves `window` to become an ability instance of its own, which still
        // switches one way -- see requestLauncherAbilityForeground().
        qWarning("HarmonyWindowSwitch: no tagWindowOrWidgetAsSubWindowOf in this Qt");
        return;
    }

    // Tagging the widget rather than its QWindow is what lets this run before the
    // window exists: Qt carries the property over when it creates it.
    tagAsSubWindowOf(window, launcherWindow);
#else
    Q_UNUSED(window);
#endif
}

bool mustStayResident(const QWidget* window)
{
#ifdef Q_OS_HARMONY
    // The Launcher owns the app's only ability instance, and hiding it would
    // minimise that instance. Everything else is a sub-window inside it and hides
    // the way it does anywhere else.
    return isLauncher(window);
#else
    Q_UNUSED(window);
    return false;
#endif
}

void switchTo(QWidget* incoming, QWidget* outgoing, OutgoingPolicy policy, int fadeDurationMs) {
    if (!incoming) {
        return;
    }

    // Read the outgoing window's state before anything below resets it.
    const bool outgoingMaximized  = outgoing && outgoing->isMaximized();
    const bool outgoingFullScreen = outgoing && outgoing->isFullScreen();
    const QRect incomingGeometry = targetGeometry(incoming, outgoing);

    clearStaleWindowState(incoming);
    incoming->setWindowOpacity(0.0);

    if (outgoingMaximized) {
        incoming->showMaximized();
    } else if (outgoingFullScreen) {
        incoming->showFullScreen();
    } else {
        incoming->showNormal();
        if (incomingGeometry.isValid()) {
            // Geometry after show, not before: on Windows ShowWindow() can adjust
            // the position from stale placement data, so a move()/resize() ahead
            // of the show does not survive it. The window is at opacity 0, so the
            // intermediate position is never seen.
            incoming->move(incomingGeometry.topLeft());
            incoming->resize(incomingGeometry.size());
        }
    }

    incoming->raise();
    incoming->activateWindow();

    const bool incomingIsLauncher = isLauncher(incoming);

#ifdef Q_OS_HARMONY
    if (const QScreen* screen = incoming->screen()) {
        const QMargins launcherSafe = outgoing && outgoing->windowHandle()
            ? outgoing->windowHandle()->safeAreaMargins() : QMargins();
        const QMargins incomingSafe = incoming->windowHandle()
            ? incoming->windowHandle()->safeAreaMargins() : QMargins();
        auto fmt = [](const QRect& r) {
            return QString("%1x%2+%3+%4").arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
        };
        qWarning("PROBE incomingIsLauncher=%d outGeom=%s outFrame=%s inGeom=%s inFrame=%s outSafe=%d,%d,%d,%d inSafe=%d,%d,%d,%d",
                 int(incomingIsLauncher),
                 qPrintable(outgoing ? fmt(outgoing->geometry()) : QString("none")),
                 qPrintable(outgoing ? fmt(outgoing->frameGeometry()) : QString("none")),
                 qPrintable(fmt(incoming->geometry())),
                 qPrintable(fmt(incoming->frameGeometry())),
                 launcherSafe.left(), launcherSafe.top(), launcherSafe.right(), launcherSafe.bottom(),
                 incomingSafe.left(), incomingSafe.top(), incomingSafe.right(), incomingSafe.bottom());
    }
#endif

#ifdef Q_OS_HARMONY
    // Normally redundant: with the Launcher owning the only ability instance, that
    // instance is always foreground and the raise above is the whole switch. It
    // matters when adoptAsLauncherSubWindow() could not tag a MainWindow and the
    // MainWindow became an instance of its own, in which case showing it pushed
    // the Launcher's instance to the background and only this can bring it back.
    if (incomingIsLauncher && !requestLauncherAbilityForeground()) {
        qWarning("HarmonyWindowSwitch: the system refused to foreground the Launcher's ability");
    }
#endif

    if (outgoing && policy == OutgoingPolicy::Hide && !mustStayResident(outgoing)) {
        // Leave fullscreen/maximised while the window is still visible (behind
        // `incoming`, at opacity 0). Doing it after hide() would not reach the
        // platform, and the stale styling would come back on the next show().
        outgoing->setWindowOpacity(0.0);
        clearStaleWindowState(outgoing);
        outgoing->hide();
        outgoing->setWindowOpacity(1.0);
    }

    auto* fadeIn = new QPropertyAnimation(incoming, "windowOpacity");
    fadeIn->setDuration(fadeDurationMs);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    s_launcherInFront = incomingIsLauncher;
}

bool launcherInFront(const QWidget* launcher) {
#ifdef Q_OS_HARMONY
    Q_UNUSED(launcher);
    return s_launcherInFront;
#else
    return launcher && launcher->isVisible();
#endif
}

void setLauncherInFront(bool inFront) {
    s_launcherInFront = inFront;
}

} // namespace HarmonyWindowSwitch
