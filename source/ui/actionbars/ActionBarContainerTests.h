#pragma once

// ============================================================================
// ActionBarContainerTests - Unit tests for ActionBarContainer visibility
// ============================================================================
// The container's job is a routing decision: given the current tool and the
// selection flags, which action bar should be on screen. That matrix grew a
// priority rule when the Highlighter gained tap-to-select (an annotation
// selected there shows the object bar, not the text bar), which is exactly the
// kind of state combination that is tedious to reach by hand.
//
// Current tests:
// - which bar each (tool, selection) combination routes to
// - the Add/Select toggle is ObjectSelect-only
// ============================================================================

#include "ActionBarContainer.h"
#include "ObjectSelectActionBar.h"
#include "TextSelectionActionBar.h"

#include <QDebug>
#include <QLayout>
#include <QWidget>

namespace ActionBarContainerTests {

/**
 * @brief A container wired up the way MainWindow wires it, minus the window.
 *
 * The container is parented to a host that is never shown, so show() calls
 * inside showActionBar() do not flash a real window during the test run.
 * Visibility of the container itself is therefore never true here; the tests
 * assert on currentActionBar(), which tracks the routing decision regardless.
 */
struct Fixture {
    QWidget host;
    ActionBarContainer* container = nullptr;
    ObjectSelectActionBar* objectBar = nullptr;
    TextSelectionActionBar* textBar = nullptr;

    Fixture()
    {
        container = new ActionBarContainer(&host);
        container->setAnimationEnabled(false);
        objectBar = new ObjectSelectActionBar();
        textBar = new TextSelectionActionBar();
        container->setActionBar(QStringLiteral("objectSelect"), objectBar);
        container->setActionBar(QStringLiteral("textSelection"), textBar);
    }
};

/**
 * @brief The (tool, selection) -> action bar routing matrix.
 */
inline bool testVisibilityRouting()
{
    qDebug() << "=== Test: action bar routing ===";
    bool success = true;

    auto check = [&success](const char* what, const ActionBar* got,
                            const ActionBar* want) {
        if (got != want) {
            qDebug() << "FAIL:" << what << "routed to the wrong bar";
            success = false;
        } else {
            qDebug() << "  -" << what << ": OK";
        }
    };

    // A tool with no action bar of its own shows nothing, selection or not.
    {
        Fixture f;
        f.container->onToolChanged(ToolType::Pen);
        f.container->onObjectSelectionChanged(true);
        check("Pen with an object selected", f.container->currentActionBar(), nullptr);
    }

    // ObjectSelect shows its bar unconditionally: the Add/Select toggle is the
    // tool's entry point and has to be reachable from the idle state.
    {
        Fixture f;
        f.container->onToolChanged(ToolType::ObjectSelect);
        check("ObjectSelect idle", f.container->currentActionBar(), f.objectBar);

        f.container->onObjectSelectionChanged(true);
        check("ObjectSelect with a selection", f.container->currentActionBar(),
              f.objectBar);
    }

    // The Highlighter is the interesting one: it hosts either bar depending on
    // what is selected, and nothing when nothing is.
    {
        Fixture f;
        f.container->onToolChanged(ToolType::Highlighter);
        check("Highlighter idle", f.container->currentActionBar(), nullptr);

        f.container->onTextSelectionChanged(true);
        check("Highlighter with text selected", f.container->currentActionBar(),
              f.textBar);

        // Tap-to-select: the annotation becomes the subject, and the text
        // selection it replaced is dropped.
        f.container->onTextSelectionChanged(false);
        f.container->onObjectSelectionChanged(true);
        check("Highlighter with an annotation selected",
              f.container->currentActionBar(), f.objectBar);

        // Both live at once happens during an Adjust session, where the
        // annotation still wins.
        f.container->onTextSelectionChanged(true);
        check("Highlighter adjusting (both selections live)",
              f.container->currentActionBar(), f.objectBar);

        // Starting a fresh drag deselects the annotation and hands the text
        // bar back.
        f.container->onObjectSelectionChanged(false);
        check("Highlighter after the annotation is deselected",
              f.container->currentActionBar(), f.textBar);
    }

    // Leaving the Highlighter with an annotation still selected hides the bar,
    // and coming back restores it.
    {
        Fixture f;
        f.container->onToolChanged(ToolType::Highlighter);
        f.container->onObjectSelectionChanged(true);
        f.container->onToolChanged(ToolType::Pen);
        check("Pen after leaving the Highlighter", f.container->currentActionBar(),
              nullptr);

        f.container->onToolChanged(ToolType::Highlighter);
        check("returning to the Highlighter", f.container->currentActionBar(),
              f.objectBar);
    }

    return success;
}

/**
 * @brief The Add/Select toggle shows only while ObjectSelect is the tool.
 *
 * It switches ObjectSelect's own sub-mode and no other tool reads it, so under
 * the Highlighter it would arm a mode with no visible effect.
 */
inline bool testActionModeToggleScope()
{
    qDebug() << "=== Test: Add/Select toggle scope ===";
    bool success = true;

    Fixture f;

    // setupButtons() adds the mode toggle first, so it heads the layout.
    QLayoutItem* first = f.objectBar->layout() ? f.objectBar->layout()->itemAt(0)
                                               : nullptr;
    QWidget* modeButton = first ? first->widget() : nullptr;
    if (!modeButton) {
        qDebug() << "FAIL: could not reach the Add/Select toggle";
        return false;
    }

    f.container->onToolChanged(ToolType::ObjectSelect);
    if (modeButton->isHidden()) {
        qDebug() << "FAIL: toggle hidden under ObjectSelect";
        success = false;
    } else {
        qDebug() << "  - visible under ObjectSelect: OK";
    }

    f.container->onToolChanged(ToolType::Highlighter);
    f.container->onObjectSelectionChanged(true);
    if (!modeButton->isHidden()) {
        qDebug() << "FAIL: toggle still shown under the Highlighter";
        success = false;
    } else {
        qDebug() << "  - hidden under the Highlighter: OK";
    }

    f.container->onToolChanged(ToolType::ObjectSelect);
    if (modeButton->isHidden()) {
        qDebug() << "FAIL: toggle not restored on return to ObjectSelect";
        success = false;
    } else {
        qDebug() << "  - restored on return to ObjectSelect: OK";
    }

    return success;
}

inline bool runAllTests()
{
    qDebug() << "";
    qDebug() << "########################################";
    qDebug() << "# ActionBarContainer Tests";
    qDebug() << "########################################";
    qDebug() << "";

    int passed = 0;
    int failed = 0;

    auto run = [&passed, &failed](bool result) {
        if (result) ++passed; else ++failed;
        qDebug() << "";
    };

    run(testVisibilityRouting());
    run(testActionModeToggleScope());

    qDebug() << "=== Results:" << passed << "passed," << failed << "failed ===";
    return failed == 0;
}

} // namespace ActionBarContainerTests
