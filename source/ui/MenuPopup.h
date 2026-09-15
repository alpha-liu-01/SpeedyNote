#pragma once

#include <QtGlobal>

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

#ifdef Q_OS_HARMONY
/**
 * @brief Submits a menu's geometry again once its window is up, as HarmonyOS needs.
 *
 * This is what execMenuAt() does before showing a menu, exposed for submenus: those are
 * opened by QMenu itself rather than by any call site, so they are placed from the event
 * filter in Main.cpp instead.
 */
void placeHarmonyMenu(QMenu& menu, const QPoint& globalPos);
#endif
