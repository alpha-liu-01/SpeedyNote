#include "MacMenuBar.h"

#ifdef Q_OS_MACOS

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QUrl>
#include <QtDebug>

#include "AboutDialog.h"
#include "../ControlPanelDialog.h"
#include "../MainWindow.h"
#include "../core/Document.h"
#include "../core/DocumentViewport.h"
#include "../core/ShortcutManager.h"

// ============================================================================
// Singleton
// ============================================================================

MacMenuBar* MacMenuBar::s_instance = nullptr;

MacMenuBar* MacMenuBar::instance()
{
    if (!s_instance) {
        s_instance = new MacMenuBar();
    }
    return s_instance;
}

// ============================================================================
// Construction
// ============================================================================

MacMenuBar::MacMenuBar(QObject* parent) : QObject(parent)
{
    // Parent-less QMenuBar: Qt promotes it to the macOS system menu bar that
    // is shared by every window in the app and stays visible even when no
    // window is open (per QA Q6.5.3).
    //
    // Lifecycle: both this MacMenuBar and m_menuBar are intentionally
    // parent-less. The singleton lives for the entire QApplication lifetime
    // and is reclaimed by the OS at process exit. Do NOT add a destructor
    // that deletes m_menuBar — Qt requires the parent-less QMenuBar to
    // outlive the application's last window for the system menu bar to
    // remain visible (Qt 6 docs: "You can create a custom default menu bar
    // by creating a parentless QMenuBar.").
    m_menuBar = new QMenuBar(nullptr);
    m_menuBar->setNativeMenuBar(true);  // explicit (default true on macOS)

    // Top-level menus, display order. The first menu is the "App menu" —
    // Qt auto-titles it from CFBundleName (packaged build via compile-mac.sh)
    // or applicationName() (dev build). Adding it with an empty title lets
    // Qt manage the title; the actions inside are what we author.
    m_appMenu      = m_menuBar->addMenu(QString());
    m_fileMenu     = m_menuBar->addMenu(tr("&File"));
    m_editMenu     = m_menuBar->addMenu(tr("&Edit"));
    m_viewMenu     = m_menuBar->addMenu(tr("&View"));
    m_documentMenu = m_menuBar->addMenu(tr("&Document"));
    m_toolsMenu    = m_menuBar->addMenu(tr("&Tools"));
    m_ocrMenu      = m_menuBar->addMenu(tr("&OCR"));
    m_windowMenu   = m_menuBar->addMenu(tr("&Window"));
    m_helpMenu     = m_menuBar->addMenu(tr("&Help"));

    buildAppMenu();
    populateFileMenu();      // MAC.3
    populateHelpMenu();      // MAC.3
    populateEditMenu();      // MAC.4
    populateDocumentMenu();  // MAC.4
    populateViewMenu();      // MAC.5
    populateToolsMenu();     // MAC.7 (called between View and OCR so the
                             // top-level menu bar order matches the QA
                             // layout: App / File / Edit / View / Document
                             // / Tools / OCR / Window / Help)
    populateOcrMenu();       // MAC.6
    populateWindowMenu();    // MAC.6
}

// ============================================================================
// App menu — About + Settings (Quit/Hide/Services auto-provided by Qt)
// ============================================================================

void MacMenuBar::buildAppMenu()
{
    // About SpeedyNote — opens the standalone AboutDialog. Qt's AboutRole
    // moves this to the top of the App menu regardless of insertion order.
    QAction* aboutAction = new QAction(tr("About SpeedyNote"), this);
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, []() {
        AboutDialog dlg(MainWindow::activeMainWindow());
        dlg.exec();
    });
    m_appMenu->addAction(aboutAction);

    // Settings… — reuses the registry's app.settings QAction. The Cmd+,
    // shortcut comes from ShortcutManager::setMacosDefault (MAC.1). Qt's
    // PreferencesRole moves the item into the App menu and renames it
    // "Settings…".
    //
    // MAC.3: the triggered() handler is wired centrally in
    // MainWindow::wireQActionDispatchers() (with activeMainWindow() dispatch).
    // We just set MenuRole and add the action to the menu here — no connect.
    QAction* settingsAction = ShortcutManager::instance()->action("app.settings");
    if (!settingsAction) {
        // Loud warning rather than silent skip: a missing app.settings would
        // mean the registry was tampered with (the action is unconditionally
        // registered in ShortcutManager::registerDefaults). Surfacing this in
        // the log is much easier to debug than a quietly-missing menu item.
        qWarning() << "[MacMenuBar] app.settings action missing from registry; "
                      "Settings menu item will not be added.";
        return;
    }
    settingsAction->setMenuRole(QAction::PreferencesRole);
    m_appMenu->addAction(settingsAction);

    // Quit / Hide SpeedyNote / Hide Others / Show All / Services submenu are
    // contributed automatically by Qt on macOS. Per QA Q3.3, About Qt is
    // intentionally NOT exposed.
}

// ============================================================================
// MAC.3: File menu — New/Open/Save/Save As/Relink PDF/Export/Close
// ============================================================================

void MacMenuBar::populateFileMenu()
{
    auto* sm = ShortcutManager::instance();

    // Skip-on-null helper: avoids cascading qWarnings if a registry id is
    // mistyped (sm->action() warns once; QWidget::insertAction would warn
    // again on null). Kept local so the populate methods stay self-contained.
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Group 1: New
    add(m_fileMenu, "file.new_paged");
    add(m_fileMenu, "file.new_edgeless");
    m_fileMenu->addSeparator();

    // Group 2: Open (Open Recent submenu deferred — see QA Q4.1)
    add(m_fileMenu, "file.open_pdf");
    add(m_fileMenu, "file.open_notebook");
    m_fileMenu->addSeparator();

    // Group 3: Save / Save As
    add(m_fileMenu, "file.save");
    add(m_fileMenu, "file.save_as");
    m_fileMenu->addSeparator();

    // Group 4: inspect or repair the active document's PDF source registry.
    QAction* pdfSources = m_fileMenu->addAction(tr("PDF Sources..."));
    connect(m_fileMenu, &QMenu::aboutToShow, pdfSources, [pdfSources]() {
        auto* mw = MainWindow::activeMainWindow();
        DocumentViewport* viewport = mw ? mw->currentViewport() : nullptr;
        Document* doc = viewport ? viewport->document() : nullptr;
        const QVector<PdfSourceHealth> health =
            doc ? doc->pdfSourceHealthSnapshot() : QVector<PdfSourceHealth>();
        int repairCount = 0;
        for (const PdfSourceHealth& source : health) {
            if (source.requiresRepair()) ++repairCount;
        }
        pdfSources->setEnabled(!health.isEmpty());
        pdfSources->setText(repairCount > 0
            ? QCoreApplication::translate("MainWindow", "Repair PDF Sources... (%1)")
                  .arg(repairCount)
            : QCoreApplication::translate("MainWindow", "PDF Sources..."));
    });
    connect(pdfSources, &QAction::triggered, this, []() {
        if (auto* mw = MainWindow::activeMainWindow()) {
            QMetaObject::invokeMethod(mw, "showPdfSourcesDialog",
                                      Qt::DirectConnection,
                                      Q_ARG(DocumentViewport*, mw->currentViewport()));
        }
    });
    m_fileMenu->addSeparator();

    // Group 5: Export / Share
    add(m_fileMenu, "file.export_pdf");
    add(m_fileMenu, "file.export");
    m_fileMenu->addSeparator();

    // Group 6: Close Tab (Quit auto-provided by Qt in App menu via QuitRole)
    add(m_fileMenu, "file.close_tab");
}

// ============================================================================
// MAC.3: Help menu — Keyboard Shortcuts + GitHub links
// ============================================================================

void MacMenuBar::populateHelpMenu()
{
    if (auto* a = ShortcutManager::instance()->action("app.keyboard_shortcuts")) {
        m_helpMenu->addAction(a);
    }
    m_helpMenu->addSeparator();

    QAction* visit = m_helpMenu->addAction(tr("Visit GitHub"));
    connect(visit, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/alpha-liu-01/SpeedyNote"));
    });

    QAction* report = m_helpMenu->addAction(tr("Report a Bug..."));
    connect(report, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/alpha-liu-01/SpeedyNote/issues/new"));
    });

    // The macOS system Help-menu search field is auto-injected at the top by
    // Qt + AppKit once the Help menu has at least one item. It enables
    // Cmd+Shift+/ to fuzzy-find any menu item app-wide.
}

// ============================================================================
// MAC.4: Edit menu — Undo/Redo + Cut/Copy/Paste/Delete + Find
// ============================================================================

void MacMenuBar::populateEditMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Group 1: Undo / Redo
    // edit.redo_alt (Cmd+Y) is intentionally omitted — it is the alternate
    // Redo binding only and is wired via the dispatcher so the keyboard
    // shortcut still fires; surfacing two Redo items in the menu would be
    // redundant and confusing.
    add(m_editMenu, "edit.undo");
    add(m_editMenu, "edit.redo");
    m_editMenu->addSeparator();

    // Group 2: Cut / Copy / Paste / Delete
    add(m_editMenu, "edit.cut");
    add(m_editMenu, "edit.copy");
    add(m_editMenu, "edit.paste");
    add(m_editMenu, "edit.delete");
    m_editMenu->addSeparator();

    // Group 3: Find
    // edit.select_all and edit.deselect are registered in ShortcutManager but
    // have no handlers anywhere today; intentionally not surfaced here. Add
    // them when the underlying feature lands.
    add(m_editMenu, "app.find");
    add(m_editMenu, "app.find_next");
    add(m_editMenu, "app.find_prev");

    // Qt + macOS may auto-inject "Start Dictation…" / "Emoji & Symbols" near
    // the bottom of the Edit menu once it has items; that is intended OS
    // behavior, not something we control.
}

// ============================================================================
// MAC.4: Document menu — Add / Insert / Delete page (PagedOnly)
// ============================================================================

void MacMenuBar::populateDocumentMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // All three are PagedOnly. ShortcutManager::setActiveDocumentScope()
    // (plumbed in MAC.1; called from MainWindow's tab/viewport-change paths)
    // automatically toggles QAction::setEnabled() on each PagedOnly action so
    // the Document menu greys out atomically when the active tab is edgeless.
    add(m_documentMenu, "document.add_page");
    add(m_documentMenu, "document.insert_page");
    add(m_documentMenu, "document.delete_page");

    // Per QA Q4.4: navigation.go_to_page lives in the View menu (MAC.5),
    // not duplicated here.
}

// ============================================================================
// MAC.5: View menu — Zoom + page nav + edgeless nav + layout + panes + fullscreen
// ============================================================================

void MacMenuBar::populateViewMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Group 1: Zoom
    // zoom.in_alt (Cmd+=) is intentionally omitted — it is the convenience
    // alternate binding only; the menu surfaces zoom.in (displays as Cmd+
    // / Cmd+Shift+=). Same convention as MAC.4 omitted edit.redo_alt.
    add(m_viewMenu, "zoom.in");
    add(m_viewMenu, "zoom.out");
    add(m_viewMenu, "zoom.fit");
    add(m_viewMenu, "zoom.100");
    add(m_viewMenu, "zoom.fit_width");
    m_viewMenu->addSeparator();

    // Group 2: Page navigation (PagedOnly — auto-greys on edgeless tab via
    // ShortcutManager::setActiveDocumentScope())
    add(m_viewMenu, "navigation.prev_page");
    add(m_viewMenu, "navigation.next_page");
    add(m_viewMenu, "navigation.first_page");
    add(m_viewMenu, "navigation.last_page");
    add(m_viewMenu, "navigation.go_to_page");
    m_viewMenu->addSeparator();

    // Group 3: Edgeless navigation (EdgelessOnly — auto-greys on paged tab)
    // 'Return to Origin' (Home) and 'Go Back' (Backspace). Note that the Home
    // key is shared with navigation.first_page above, but scope enforcement
    // means only one of the two QActions is enabled at any given moment, so
    // Qt's shortcut router never reports an ambiguous overload.
    add(m_viewMenu, "edgeless.home");
    add(m_viewMenu, "edgeless.go_back");
    m_viewMenu->addSeparator();

    // Group 4: Layout / sidebars / launcher
    add(m_viewMenu, "navigation.launcher");
    add(m_viewMenu, "view.left_sidebar");
    add(m_viewMenu, "view.right_sidebar");
    add(m_viewMenu, "view.auto_layout");
    m_viewMenu->addSeparator();

    // Group 5: Pane management
    add(m_viewMenu, "view.split_right");
    add(m_viewMenu, "view.merge_panes");
    add(m_viewMenu, "view.focus_left_pane");
    add(m_viewMenu, "view.focus_right_pane");
    m_viewMenu->addSeparator();

    // Group 6: Fullscreen
    // The macOS-specific Ctrl+Cmd+F default for view.fullscreen was registered
    // in MAC.1 via ShortcutManager::setMacosDefault; nothing platform-specific
    // to do here. The action's text follows the registry's displayName
    // ("Toggle Fullscreen"); macOS conventions normally label this 'Enter Full
    // Screen' / 'Exit Full Screen' depending on state, but tracking that
    // toggle live would require a state-sync wire and isn't in MAC.5 scope.
    add(m_viewMenu, "view.fullscreen");

#ifdef SPEEDYNOTE_DEBUG
    // Per QA Q4.3.a: debug overlay is hidden from the menu in release builds.
    // The keyboard shortcut (F12) is wired unconditionally in
    // wireQActionDispatchers() so it works in any build that ships the id.
    m_viewMenu->addSeparator();
    add(m_viewMenu, "view.debug_overlay");
#endif
}

// ============================================================================
// MAC.7: Tools menu — 6 direct tool items + 5 submenus per QA Q4.5
// ============================================================================

void MacMenuBar::populateToolsMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Direct items: tool selection. tool.pan (H, hold-to-activate) is
    // intentionally omitted per QA Q4.5 — it is not menu-friendly. The
    // existing event-filter path at MainWindow::eventFilter handles the
    // hold-and-release semantics.
    add(m_toolsMenu, "tool.pen");
    add(m_toolsMenu, "tool.marker");
    add(m_toolsMenu, "tool.highlighter");
    add(m_toolsMenu, "tool.eraser");
    add(m_toolsMenu, "tool.lasso");
    add(m_toolsMenu, "tool.object_select");
    m_toolsMenu->addSeparator();

    // Submenu 1: Highlighter Style — 4 mutually-exclusive auto-style items
    // followed by a separator and the PDF/OCR source toggle. The styles are
    // not made checkable here (toolbar dropdown shows the active style; QA
    // Q4.5 doesn't request checkmarks). Could be added as a QActionGroup in
    // a follow-up if desired.
    QMenu* hl = m_toolsMenu->addMenu(tr("Highlighter Style"));
    add(hl, "highlighter.style_none");
    add(hl, "highlighter.style_cover");
    add(hl, "highlighter.style_underline");
    add(hl, "highlighter.style_dotted");
    hl->addSeparator();
    add(hl, "highlighter.toggle_source");

    // Submenu 2: Object Mode — the three visible object-tool subtypes plus
    // their shared Add/Select interaction mode. Handlers activate ObjectSelect
    // before applying the requested mode, matching the main toolbar.
    QMenu* ins = m_toolsMenu->addMenu(tr("Object Mode"));
    add(ins, "object.mode_image");
    add(ins, "object.mode_link");
    add(ins, "object.mode_text");
    add(ins, "object.mode_create");
    add(ins, "object.mode_select");

    // Submenu 3: Object — Z-order (4 items) + separator + Affinity (3 items).
    // All 7 grey out via MainWindow::updateObjectActionsEnabled() when the
    // active viewport's tool is not ObjectSelect or no objects are selected.
    QMenu* obj = m_toolsMenu->addMenu(tr("Object"));
    add(obj, "object.bring_front");
    add(obj, "object.bring_forward");
    add(obj, "object.send_backward");
    add(obj, "object.send_back");
    obj->addSeparator();
    add(obj, "object.affinity_up");
    add(obj, "object.affinity_down");
    add(obj, "object.affinity_background");

    // Submenu 4: Layers — 6 items. layer.toggle_visibility's macOS shortcut
    // (Cmd+;) was rebound from the cross-platform Cmd+, in MAC.1 to avoid
    // colliding with the Settings shortcut, so the menu displays Cmd+;
    // automatically via the registry.
    QMenu* lay = m_toolsMenu->addMenu(tr("Layers"));
    add(lay, "layer.new");
    add(lay, "layer.toggle_visibility");
    add(lay, "layer.select_all");
    add(lay, "layer.select_top");
    add(lay, "layer.select_bottom");
    add(lay, "layer.merge");

    // Submenu 5: Links — 3 link slot activations. Inline gate ensures these
    // only fire / take effect when the Object Select tool is active.
    QMenu* lnk = m_toolsMenu->addMenu(tr("Links"));
    add(lnk, "link.slot_1");
    add(lnk, "link.slot_2");
    add(lnk, "link.slot_3");
}

// ============================================================================
// MAC.6: OCR menu — Scan + checkable toggles + standalone OCR Language / Lock
// ============================================================================

void MacMenuBar::populateOcrMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Group 1: Scan + Auto OCR.
    // ocr.auto_ocr is a checkable toggle (made checkable + state-synced in
    // MainWindow's setupConnections ocrST block per MAC.6) and lives in this
    // group rather than the toggle group because the QA structure pairs it
    // with the Scan items; it still renders a checkmark.
    add(m_ocrMenu, "ocr.scan_page");
    add(m_ocrMenu, "ocr.scan_all");
    add(m_ocrMenu, "ocr.auto_ocr");
    m_ocrMenu->addSeparator();

    // Group 2: Display toggles. Both checkable; menu-checkmark sync is wired
    // at the MainWindow side so it follows toolbar / shortcut / tab-switch
    // changes alike.
    add(m_ocrMenu, "ocr.show_text");
    add(m_ocrMenu, "ocr.snap_grid");
    m_ocrMenu->addSeparator();

    // Group 3: Standalone items (no shortcut, no ShortcutManager registration).
    // Per QA Q4.6 these mirror the legacy overflow-menu pair. Both dispatch
    // to private slots on the active MainWindow via QMetaObject::invokeMethod
    // (same pattern as the MAC.3 "Relink PDF..." item) so MacMenuBar stays
    // outside MainWindow's class boundary.
    QAction* langAction = m_ocrMenu->addAction(tr("OCR Language..."));
    connect(langAction, &QAction::triggered, this, []() {
        if (auto* mw = MainWindow::activeMainWindow()) {
            QMetaObject::invokeMethod(mw, "showOcrLanguageDialog",
                                      Qt::DirectConnection);
        }
    });

    QAction* lockAction = m_ocrMenu->addAction(tr("Lock All OCR Text"));
    connect(lockAction, &QAction::triggered, this, []() {
        if (auto* mw = MainWindow::activeMainWindow()) {
            QMetaObject::invokeMethod(mw, "lockAllOcrText",
                                      Qt::DirectConnection);
        }
    });
}

// ============================================================================
// MAC.6: Window menu — Next Tab / Previous Tab + Qt auto-contributed items
// ============================================================================

void MacMenuBar::populateWindowMenu()
{
    auto* sm = ShortcutManager::instance();
    auto add = [sm](QMenu* menu, const QString& id) {
        if (auto* a = sm->action(id)) menu->addAction(a);
    };

    // Qt 6 contributes Minimize (Cmd+M), Zoom, "Bring All to Front", and the
    // open-windows list to the macOS Window menu automatically when the
    // parent-less QMenuBar is installed as the system menu bar. Our manual
    // additions sit between Qt's defaults and (eventually) the auto-injected
    // open-windows list, separated by a leading separator for visual grouping
    // per QA Q4.7.
    //
    // If a future Qt version stops auto-providing these, add them manually
    // with QAction::WindowMenuRole.
    m_windowMenu->addSeparator();
    add(m_windowMenu, "navigation.next_tab");
    add(m_windowMenu, "navigation.prev_tab");
}

#endif // Q_OS_MACOS
