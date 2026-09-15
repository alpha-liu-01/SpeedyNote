#include "MenuPopup.h"

#include <QMenu>
#include <QPoint>

#ifdef Q_OS_HARMONY
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QWindow>
#endif

#ifdef Q_OS_HARMONY
// Where the menu should end up, clamped the way QMenu::popup() would have clamped it:
// a menu that does not fit below or to the right of what it was opened from goes to the
// other side of it rather than off the edge of the screen.
static QRect harmonyMenuRect(const QMenu& menu, const QPoint& globalPos, const QSize& size)
{
    QRect rect(globalPos, size);

    const QScreen* screen = menu.screen();
    if (screen == nullptr) {
        return rect;
    }

    const QRect limit = screen->availableGeometry();
    if (rect.right() > limit.right()) {
        rect.moveRight(globalPos.x());
    }
    if (rect.bottom() > limit.bottom()) {
        rect.moveBottom(globalPos.y());
    }
    // A menu too large to fit either way keeps its top-left corner on screen, that being
    // the end it is read from.
    if (rect.right() > limit.right()) {
        rect.moveRight(limit.right());
    }
    if (rect.bottom() > limit.bottom()) {
        rect.moveBottom(limit.bottom());
    }
    if (rect.left() < limit.left()) {
        rect.moveLeft(limit.left());
    }
    if (rect.top() < limit.top()) {
        rect.moveTop(limit.top());
    }
    return rect;
}
#endif

#ifdef Q_OS_HARMONY
// QMenuPrivate::popup() creates the platform window before it has worked out where the
// menu goes, and on HarmonyOS a geometry submitted between create() and show() is dropped:
// the window appears at the rect it was created with. For a menu built where it is used
// that rect is QWidget's default for a widget with a parent, 0,0 100x30, so what shows up
// is a sliver in the top-left corner -- and it dismisses itself at once, the tap that
// opened it being nowhere near where it landed.
//
// Only the first popup of a given QMenu is affected. The second lands correctly, because by
// then the window exists and is being moved rather than shown, which is why the one menu
// this app keeps in a member variable looked like it worked while every menu rebuilt per
// use never did.
//
// So the position is submitted again through the window once the menu is up, the same way a
// dialog is placed on this platform. It has to be handed in from outside, because by the
// time the menu is visible nothing remembers where it was meant to go: Qt's own rect and
// the platform's have both been overwritten with the creation rect.
void placeHarmonyMenu(QMenu& menu, const QPoint& globalPos)
{
    menu.ensurePolished();
    const QRect target = harmonyMenuRect(menu, globalPos, menu.sizeHint());

    // Invisible until it has moved, or the corner is still where it first appears.
    menu.setWindowOpacity(0.0);

    QMenu* menuPtr = &menu;
    QTimer::singleShot(0, &menu, [menuPtr, target]() {
        if (QWindow* handle = menuPtr->windowHandle()) {
            handle->setGeometry(target);
        }
        menuPtr->setWindowOpacity(1.0);
    });
}
#endif

QAction* execMenuAt(QMenu& menu, const QPoint& globalPos)
{
#ifdef Q_OS_HARMONY
    placeHarmonyMenu(menu, globalPos);
#endif

    return menu.exec(globalPos);
}
