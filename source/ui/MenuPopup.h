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
