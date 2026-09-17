#include "MenuPopup.h"

#include <QAction>
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

// QMenuPrivate::popup() creates the platform window before it has worked out where the
// menu goes, and on HarmonyOS a geometry submitted between create() and show() is dropped:
// the window appears at the rect it was created with. For a menu built where it is used
// that rect is QWidget's default for a widget with a parent, 0,0 100x30, so what shows up
// is a sliver in the top-left corner. It does not stay either: the QPA closes every visible
// popup as soon as a node reports a click on content outside one, and the tap that opened
// this menu is outside a menu stranded in the corner.
//
// Only the first popup of a given QMenu is affected. The second lands correctly, because by
// then the window exists and is being moved rather than shown, which is why the one menu
// this app keeps in a member variable looked like it worked while every menu rebuilt per
// use never did.
//
// So the position is submitted again through the window once the menu is up, the same way a
// dialog is placed on this platform -- through QWindow, which is the only route that reaches
// it, the QPA dropping any position Qt still considers automatic. It has to be worked out
// here, before the menu is shown, because afterwards nothing remembers where it was meant to
// go: Qt's own rect and the platform's have both been overwritten with the creation rect.
//
// A submenu is beyond this. It can be placed the same way, but opening its window counts as
// an interaction outside the parent menu, and the QPA answers that by closing every popup
// rather than the one that lost it, so the submenus in the launcher's notebook menus stay
// unusable on HarmonyOS.
static void placeHarmonyMenu(QMenu& menu, const QPoint& globalPos)
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

void flattenSubmenu(QMenu& parent, QMenu* submenu)
{
#ifdef Q_OS_HARMONY
    if (submenu == nullptr) {
        return;
    }

    // The action that opens a submenu is the item you see for it in the parent. Everything
    // here is positioned against it, so the flattened group ends up where the submenu was.
    QAction* opener = submenu->menuAction();

    QAction* heading = new QAction(opener->text(), &parent);
    heading->setEnabled(false);
    parent.insertAction(opener, heading);

    // A submenu can be disabled as a whole -- StarredView disables Export while nothing is
    // selected -- and the item carrying that is the one about to be removed, so it has to be
    // handed down to each action before it goes.
    const bool groupEnabled = opener->isEnabled();

    // The actions stay owned by the submenu, which stays owned by the parent menu, so nothing
    // here changes what is deleted with what. A QAction is not tied to one menu: listing it in
    // the parent as well is enough for it to be drawn and triggered there.
    const QList<QAction*> actions = submenu->actions();
    for (QAction* action : actions) {
        action->setEnabled(groupEnabled && action->isEnabled());
        parent.insertAction(opener, action);
    }

    parent.removeAction(opener);
#else
    Q_UNUSED(parent);
    Q_UNUSED(submenu);
#endif
}
