#pragma once

// ============================================================================
// ActionBarContainerTests - Unit tests for ActionBarContainer visibility
// ============================================================================
// The container's job is a routing decision: given the current tool and the
// selection flags, which action bar should be on screen. Every tool with a bar
// now routes to just one of them, so what the tests guard is which buttons that
// one bar offers - state combinations that are tedious to reach by hand.
//
// Current tests:
// - which bar each (tool, selection) combination routes to
// - the Add/Select toggle is ObjectSelect-only
// - a bare text selection leaves Copy as the only button
// ============================================================================

#include "ActionBarContainer.h"
#include "ObjectSelectActionBar.h"

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

    Fixture()
    {
        container = new ActionBarContainer(&host);
        container->setAnimationEnabled(false);
        objectBar = new ObjectSelectActionBar();
        container->setActionBar(QStringLiteral("objectSelect"), objectBar);
    }

    /// A button on the object bar by its position in setupButtons() order.
    QWidget* objectBarButton(int index) const
    {
        QLayoutItem* item = objectBar->layout() ? objectBar->layout()->itemAt(index)
                                                : nullptr;
        return item ? item->widget() : nullptr;
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

    // The Highlighter is the interesting one: the bar appears for either kind of
    // selection, and for nothing when nothing is selected.
    {
        Fixture f;
        f.container->onToolChanged(ToolType::Highlighter);
        check("Highlighter idle", f.container->currentActionBar(), nullptr);

        f.container->onTextSelectionChanged(true);
        check("Highlighter with text selected", f.container->currentActionBar(),
              f.objectBar);

        // Tap-to-select: the annotation becomes the subject, and the text
        // selection it replaced is dropped.
        f.container->onTextSelectionChanged(false);
        f.container->onObjectSelectionChanged(true);
        check("Highlighter with an annotation selected",
              f.container->currentActionBar(), f.objectBar);

        // Deselecting the annotation while text is still selected keeps the bar,
        // now in its text-selection shape.
        f.container->onTextSelectionChanged(true);
        f.container->onObjectSelectionChanged(false);
        check("Highlighter after the annotation is deselected",
              f.container->currentActionBar(), f.objectBar);

        // Dropping both is what actually hides it.
        f.container->onTextSelectionChanged(false);
        check("Highlighter with nothing selected again",
              f.container->currentActionBar(), nullptr);
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
    QWidget* modeButton = f.objectBarButton(0);
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

/**
 * @brief With only text selected, the object bar reduces to a Copy button.
 *
 * This is what replaced the one-button TextSelectionActionBar, so the check is
 * that Copy survives with no object selected and that Paste - an object action
 * that would drop an object and leave the user in the Highlighter - does not.
 */
inline bool testTextSelectionShape()
{
    qDebug() << "=== Test: object bar shape for a bare text selection ===";
    bool success = true;

    Fixture f;

    // setupButtons() order: mode, aspect lock, OCR lock, OCR convert, Copy, Paste.
    QWidget* copyButton = f.objectBarButton(4);
    QWidget* pasteButton = f.objectBarButton(5);
    if (!copyButton || !pasteButton) {
        qDebug() << "FAIL: could not reach the Copy / Paste buttons";
        return false;
    }

    f.container->onToolChanged(ToolType::Highlighter);
    f.container->onObjectClipboardChanged(true);
    f.container->onTextSelectionChanged(true);

    if (copyButton->isHidden()) {
        qDebug() << "FAIL: Copy hidden with text selected";
        success = false;
    } else {
        qDebug() << "  - Copy visible for a text selection: OK";
    }
    if (!pasteButton->isHidden()) {
        qDebug() << "FAIL: Paste shown under the Highlighter";
        success = false;
    } else {
        qDebug() << "  - Paste hidden under the Highlighter: OK";
    }

    // Under ObjectSelect the clipboard makes Paste available again.
    f.container->onToolChanged(ToolType::ObjectSelect);
    if (pasteButton->isHidden()) {
        qDebug() << "FAIL: Paste not restored under ObjectSelect";
        success = false;
    } else {
        qDebug() << "  - Paste restored under ObjectSelect: OK";
    }

    // Copy needs a selection of some kind; ObjectSelect idle has neither.
    f.container->onTextSelectionChanged(false);
    if (!copyButton->isHidden()) {
        qDebug() << "FAIL: Copy shown with nothing selected";
        success = false;
    } else {
        qDebug() << "  - Copy hidden with nothing selected: OK";
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
    run(testTextSelectionShape());

    qDebug() << "=== Results:" << passed << "passed," << failed << "failed ===";
    return failed == 0;
}

} // namespace ActionBarContainerTests
