#pragma once

class QAction;
class QMenu;
class QPoint;

/**
 * @brief Shows a menu at a global position and returns the action chosen, if any.
 *
 * Everywhere except HarmonyOS this is QMenu::exec(). There it also works around the
 * platform dropping the geometry of a window that has been created but not yet shown,
 * which otherwise leaves a menu built on the spot as a sliver in the top-left corner
 * that dismisses itself; MenuPopup.cpp has the details.
 *
 * Prefer this to QMenu::exec() for any menu that is constructed where it is used, which
 * is nearly all of them: a menu kept in a member variable is only affected the first
 * time it opens, but that is still once per run.
 */
QAction* execMenuAt(QMenu& menu, const QPoint& globalPos);

/**
 * @brief Lifts a submenu's actions into the menu that owns it, on platforms where a submenu
 *        cannot be opened at all.
 *
 * Does nothing anywhere but HarmonyOS. There, the QPA closes every visible popup window the
 * moment a second one appears, so opening a submenu takes down the menu it was opened from and
 * itself along with it -- the submenus in the launcher's notebook menus could not be reached by
 * any means. It is a platform response to a window appearing, with no app-side lever on it, so
 * flattening is the only way those actions become reachable.
 *
 * The item that used to open the submenu stays where it was, disabled, as a heading for what
 * was behind it. That way the actions keep the labels they were written with -- "To PDF..."
 * still has an "Export" above it -- and the group stays where the menu was designed to have it.
 *
 * Call this once the submenu has been filled in, and pass the menu it was added to.
 */
void flattenSubmenu(QMenu& parent, QMenu* submenu);
