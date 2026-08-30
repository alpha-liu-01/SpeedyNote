#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
// #include "InkCanvas.h"  // Phase 3.1.7: Disconnected - using DocumentViewport
#include "ui/MarkdownNotesSidebar.h"
#include "core/Page.h"  // Phase 3.1.8: For Page::BackgroundType
#include "core/MarkdownNote.h"  // Phase M.3: For loading markdown notes
// #include "RecentNotebooksManager.h"  // TODO G.6: Re-enable after LauncherWindow remake

#include <QTimer>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QHash>
#include <set>
#include <QSpinBox>
#include "pdf/PdfSearchEngine.h"  // SBS2: PdfSearchMatch complete type for scan aggregate
#include <QResizeEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QCloseEvent>
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#endif
#include <QShortcut>  // For m_escapeShortcut + the macOS Cmd+K Settings alternate
#include <memory>     // For std::unique_ptr
// Note: ControlPanelDialog is included in MainWindow.cpp (Phase CP.1)
#include <QLocalServer>
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QSharedMemory>
#endif
// Phase SV: Tab bars and viewport stacks managed by SplitViewManager
#include <QStackedWidget>
#include <QSplitter>

// Phase 3.1: New architecture includes
#include "ui/TabManager.h"
#include "core/DocumentManager.h"
#include "core/ToolType.h"

// Toolbar extraction includes
#include "ui/NavigationBar.h"
#include "ui/Toolbar.h"
#include "ui/sidebars/LeftSidebarContainer.h"  // Phase S3: Left sidebar container

class QThread;

// PDF Search
class PdfSearchBar;
class PdfSearchEngine;
struct PdfSearchMatch;
struct PdfSearchState;
// PdfOutlineItem must be a COMPLETE type here (not just forward-declared): a
// private slot takes const QVector<PdfOutlineItem>&, and stricter moc/QMetaType
// toolchains (Qt on Linux / GCC 15 / Flatpak) require the full definition to
// build QMetaType<QList<PdfOutlineItem>>. PdfProvider.h is lightweight.
#include "pdf/PdfProvider.h"

// OCR
class OcrWorker;
class OcrSubToolbar;
struct OcrSnapParams;

#include "ocr/OcrTextBlock.h"

// Action Bar includes
class ActionBarContainer;
class LassoActionBar;
class ObjectSelectActionBar;
class TextSelectionActionBar;
class PagePanelActionBar;

// Phase 3.1.8: TouchGestureMode - extracted from InkCanvas.h for palm rejection
// Will be reimplemented in Phase 3.3 if needed
// Guard to prevent redefinition if InkCanvas.h is also included
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,     // Touch gestures completely off
    YAxisOnly,    // Only Y-axis panning allowed (X-axis and zoom locked)
    Full          // Full touch gestures (panning and zoom)
};
#endif


// Forward declarations
class QTreeWidgetItem;
class QProgressDialog;
class DocumentViewport;
class LayerPanel;
class PagePanel;
class DebugOverlay;
namespace Poppler { 
    class Document; 
    class OutlineItem;
}

// Forward declarations
class LauncherWindow;
class SplitViewManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Construct MainWindow.
     * @param parent Parent widget.
     * 
     * Phase 3.1: Always uses new DocumentViewport architecture.
     * Legacy InkCanvas support has been removed.
     */
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow();
    
    // int getCurrentPageForCanvas(InkCanvas *canvas);  // MW1.4: Stub - returns 0

    TouchGestureMode touchGestureMode = TouchGestureMode::Full;
    TouchGestureMode previousTouchGestureMode = TouchGestureMode::Full; // Store state before text selection
    TouchGestureMode getTouchGestureMode() const;
    void setTouchGestureMode(TouchGestureMode mode);
    void cycleTouchGestureMode(); // Cycle through: Disabled -> YAxisOnly -> Full -> Disabled

#ifdef Q_OS_LINUX
    // Palm rejection (Linux only - Windows/macOS/Android have built-in palm rejection)
    bool isPalmRejectionEnabled() const;
    void setPalmRejectionEnabled(bool enabled);
    int getPalmRejectionDelay() const;
    void setPalmRejectionDelay(int delayMs);
#endif

    // Scroll-bar placement settings (Plan SB4); delegate to SplitViewManager.
    // Page-axis (vertical) bar: false = left edge, true = right edge.
    bool scrollBarVerticalOnRight() const;
    void setScrollBarVerticalOnRight(bool onRight);
    // Cross-axis (horizontal) bar: false = top edge, true = bottom edge.
    bool scrollBarHorizontalOnBottom() const;
    void setScrollBarHorizontalOnBottom(bool onBottom);
    // Keep bars pinned (visible; disables proximity auto-hide).
    bool scrollBarsPinned() const;
    void setScrollBarsPinned(bool pinned);

    // OCR language query (for ControlPanelDialog and per-document dialog)
    QStringList ocrAvailableLanguages() const { return m_ocrAvailableLanguages; }
    // Subset already present locally; the rest need an on-demand download.
    QStringList ocrDownloadedLanguages() const { return m_ocrDownloadedLanguages; }

    // Apply a per-document OCR recognizer language override (empty = global
    // fallback). Marks the document modified and refreshes the OCR worker.
    // Shared by showOcrLanguageDialog() and DocumentSettingsDialog.
    void applyDocumentOcrLanguage(Document* doc, const QString& lang);

    // Theme settings
    QColor customAccentColor;
    bool useCustomAccentColor = false;
    
    QColor getAccentColor() const;
    QColor getCustomAccentColor() const { return customAccentColor; }
    void setCustomAccentColor(const QColor &color);
    bool isUsingCustomAccentColor() const { return useCustomAccentColor; }
    void setUseCustomAccentColor(bool use);

    /**
     * @brief Apply background settings to the current document and viewport.
     * @param type Background type (None, Grid, Lines)
     * @param bgColor Background color
     * @param gridColor Grid/line color
     * @param gridSpacing Grid spacing in pixels
     * @param lineSpacing Line spacing in pixels
     */
    void applyBackgroundSettings(Page::BackgroundType type, const QColor& bgColor,
                                 const QColor& gridColor, int gridSpacing, int lineSpacing);

    QColor getDefaultPenColor();
    void setPdfDarkModeEnabled(bool enabled);
    void setSkipImageMasking(bool skip);

    // Per-document PDF display overrides (PDF-backed documents only). Resolve to
    // the document's tri-state override when set (>= 0), else the global
    // QSettings value. refreshPdfDisplaySettingsForDocument() re-applies the
    // resolved values to every viewport currently showing the document.
    bool resolvePdfDarkMode(Document* doc) const;
    bool resolvePdfInvertIncludeImages(Document* doc) const;
    void refreshPdfDisplaySettingsForDocument(Document* doc);


    
    // Single instance functionality
    static bool isInstanceRunning();
    static bool sendToExistingInstance(const QString &filePath);
    void setupSingleInstanceServer();
    
    /**
     * @brief Find an existing MainWindow among all top-level widgets.
     * @return Pointer to existing MainWindow, or nullptr if none exists.
     * 
     * Phase P.1: Extracted from LauncherWindow for reuse.
     */
    static MainWindow* findExistingMainWindow();
    
    /**
     * @brief Preserve window state when transitioning from another window.
     * @param sourceWindow The window whose state to preserve (size, position, maximized/fullscreen).
     * @param isExistingWindow If true, just show without changing size/position.
     * 
     * Phase P.1: Extracted from LauncherWindow for reuse.
     */
    void preserveWindowState(QWidget* sourceWindow, bool isExistingWindow = false);
    
    // Theme/palette management
    static void updateApplicationPalette(); // Update Qt application palette based on dark mode
    void openFileInNewTab(const QString &filePath); // Open file (PDF, .snb) in new tab via single-instance
    
    /**
     * @brief Close a document by its ID.
     * @param documentId The document's UUID.
     * @param discardChanges If true, close without saving (for delete operations).
     *                       If false (default), save first if modified.
     * @return True if the document was closed (or wasn't open), false if user cancelled save.
     * 
     * Used when a document needs to be closed before an external operation:
     * - Rename: discardChanges=false (save first, then rename)
     * - Delete: discardChanges=true (just close, file will be deleted anyway)
     */
    bool closeDocumentById(const QString& documentId, bool discardChanges = false);
    
    /**
     * @brief Show PDF open dialog and open selected PDF in a new tab.
     * 
     * Phase P.4: Made public for Launcher integration.
     * Routes through DocumentManager::loadDocument().
     */
    void showOpenPdfDialog();
    
    void saveThemeSettings();
    void loadThemeSettings();
    void updateTheme(); // Apply current theme settings
    // REMOVED: updateTabSizes removed - tab sizing functionality deleted
    
    // REMOVED MW7.6: migrateOldButtonMappings and migrateOldActionString removed - old mapping system deleted

    // InkCanvas* currentCanvas();  // MW1.4: Stub - returns nullptr, use currentViewport()
    DocumentViewport* currentViewport() const; // Phase 3.1.4: New accessor for DocumentViewport
    int tabCount() const;  // Returns number of open tabs (used by Launcher for Escape handling)

    /**
     * @brief Get the currently-active MainWindow instance (MAC.1).
     *
     * Tracks the most recently focused MainWindow. Used by ShortcutManager
     * QAction handlers (and the upcoming MacMenuBar in MAC.2+) to dispatch
     * actions to the correct window when multiple are open. Survives modal
     * dialogs (the dialog gets focus, but s_activeMainWindow continues to
     * point at the underlying MainWindow).
     *
     * Returns nullptr if no MainWindow has ever received focus or all have
     * been destroyed.
     */
    static MainWindow* activeMainWindow();
    void switchToTabIndex(int index);  // Switch to a specific tab by index
    void saveSessionTabs();  // Persist open tab paths to QSettings for session restore

    void switchPage(int pageNumber); // Made public for RecentNotebooksDialog
    // REMOVED MW7.7: switchPageWithDirection stub removed - replaced with switchPage calls

    
    // New: Keyboard mapping methods (made public for ControlPanelDialog)
    // REMOVED MW7.6: addKeyboardMapping, removeKeyboardMapping, and getKeyboardMappings removed - old mapping system deleted

    void addNewTab();
    
    /**
     * @brief Create a new tab with an edgeless (infinite canvas) document.
     * 
     * Edgeless mode provides an infinite canvas where tiles are created
     * on-demand as the user draws.
     */
    void addNewEdgelessTab();

    /**
     * TEMPORARY: Load edgeless document from .snb bundle directory.
     * Uses QFileDialog::getExistingDirectory() to select the .snb folder.
     * 
     * TODO: Replace with unified file picker that handles both files and bundles,
     * possibly by packaging .snb as a single file (zip/tar) in the future.
     */
    void loadFolderDocument();
    
    // ========== Phase P.4.2: Launcher Interface Methods ==========
    
    /**
     * @brief Check if any documents are currently open.
     * @return True if at least one tab/document is open.
     * 
     * Used by Launcher to determine if MainWindow should be reused.
     */
    bool hasOpenDocuments() const;
    
    /**
     * @brief Switch to an already-open document tab.
     * @param bundlePath Path to the .snb bundle to switch to.
     * @return True if document was found and switched to, false otherwise.
     * 
     * If the document is already open in a tab, switches to that tab
     * instead of opening a duplicate.
     */
    bool switchToDocument(const QString& bundlePath);
    
    /**
     * @brief Bring this window to the front.
     * 
     * Convenience method that calls show(), raise(), and activateWindow().
     * Used by Launcher when transitioning to MainWindow.
     */
    void bringToFront();

    // Phase 3.1: sharedLauncher disconnected - will be re-linked later
    // static LauncherWindow *sharedLauncher;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    static QSharedMemory *sharedMemory;
#endif

    /// MAC.1: most recently focused MainWindow. QPointer auto-clears on
    /// destruction so activeMainWindow() returns nullptr safely.
    static QPointer<MainWindow> s_activeMainWindow;
    
    // Static cleanup method for signal handlers
    static void cleanupSharedResources();



protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;  // New: Handle keyboard shortcuts
    void keyReleaseEvent(QKeyEvent *event) override; // Track Ctrl key release for trackpad pinch-zoom detection
    void focusInEvent(QFocusEvent *event) override;  // MAC.1: track active MainWindow
    void changeEvent(QEvent *event) override;        // Sync nav bar fullscreen button when window state changes (e.g. macOS green button)
    // REMOVED: tabletEvent removed - tablet event handling deleted

#ifdef Q_OS_WIN
#  if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#  else
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#  endif
#endif
    void closeEvent(QCloseEvent *event) override;

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
#endif

    // IME support for multi-language input
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private slots:

    void onNewConnection(); // Handle new instance connections
    
    // Keyboard Shortcut Hub: Handle shortcut changes from ShortcutManager
    void onShortcutChanged(const QString& actionId, const QString& newShortcut);

    void forceUIRefresh();


    void removeTabAt(int index);
    // void toggleThicknessSlider(); // Added function to toggle thickness slider
    void toggleFullscreen();
    void showJumpToPageDialog();
    void goToPreviousPage(); // Go to previous page
    void goToNextPage();     // Go to next page

    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    void connectViewportScrollSignals(DocumentViewport* viewport);  // Phase 3.3
    void centerViewportContent(int tabIndex);  // Phase 3.3: One-time horizontal centering
    void updateLayerPanelForViewport(DocumentViewport* viewport);  // Phase 5.1: Update LayerPanel
    void updateOutlinePanelForDocument(Document* doc);  // Phase E.2: Update OutlinePanel for document
    QSet<QString> computeUnavailableOutlinePages(Document* doc) const;  // OUT1: keyFor(sourceId, originalPage) of absent outline targets
    // OUT1: same as above but reuses an already-aggregated outline to avoid a
    // second (expensive, uncached) TOC parse when the caller already has one.
    QSet<QString> computeUnavailableOutlinePages(Document* doc, const QVector<PdfOutlineItem>& outline) const;
    void refreshOutlineAvailability(Document* doc);  // Plan A2: re-grey outline entries after structure change
    void updatePagePanelForViewport(DocumentViewport* viewport);  // Page Panel: Task 5.1: Update PagePanel
    void notifyPageStructureChanged(Document* doc, int currentPage = -1);  // Helper: Update PagePanel after page add/remove
    void showPdfSourcesDialog(DocumentViewport* viewport);
    void updatePdfSourceUi(DocumentViewport* viewport);
    void showExportDialog();
    void applySubToolbarValuesToViewport(ToolType tool);  // Phase D: Apply subtoolbar presets to viewport (via signals)
    void applyAllSubToolbarValuesToViewport(DocumentViewport* viewport);  // Phase D: Apply ALL tool presets directly
    
    // Phase doc-1: Document operations
    void saveDocument();          // doc-1.1: Save document to JSON file (Ctrl+S)
    void saveDocumentAs();        // MAC.3: Always prompt for new path (Ctrl+Shift+S)
    void loadDocument();          // doc-1.2: Load document from JSON file (Ctrl+O)
    void addPageToDocument();     // doc-1.0: Add page at end of document (Ctrl+Shift+A)
    void insertPageInDocument();  // Phase 3: Insert page after current (Ctrl+Shift+I)
    void deletePageInDocument();  // Phase 3B: Delete current page (Ctrl+Shift+D)
#ifdef SPEEDYNOTE_DEBUG
    void importPagesFromOtherDocDebug();  // Plan B temp: import pages from another open doc
#endif
    // Plan D1: copy the given selected pages (0-based indices in the active
    // document) into another open document chosen via a dialog.
    void copyPagesToOtherDocument(const QList<int>& srcRows);

    // Plan D2: cross-document page-transfer drag-and-drop.
    // Connect a viewport's pageTransferDropped signal (idempotent).
    void connectViewportTransferSignal(DocumentViewport* vp);
    // Handle a drop: resolve srcToken -> live Document, copy + refresh.
    void handlePageTransferDrop(const QString& srcToken, const QStringList& srcUuids, int destIndex);
    // Shared post-import refresh of a (possibly non-active) destination viewport.
    void refreshDestinationAfterImport(DocumentViewport* destVp, int destIndex);
    void openPdfDocument(const QString &filePath = QString());       // doc-1.4: Open PDF file (Ctrl+Shift+O)
    void addPagesFromPdf(const QString& filePath = QString(),
                         DocumentViewport* targetViewport = nullptr);
    bool isDarkMode();

    // MAC.6: promoted from private: to private slots: so MacMenuBar can
    // dispatch to them via QMetaObject::invokeMethod. lockAllOcrText() is a slot factored
    // out of the inline lambda previously living in setupUi()'s overflow
    // menu; the overflow now calls it directly so the macOS OCR menu and
    // the overflow menu share one implementation.
    void showOcrLanguageDialog();
    void lockAllOcrText();

    // MAC.7: Sync the 7 object Z-order + affinity QActions' enabled state to
    // the active viewport's currentTool() == ObjectSelect && hasSelectedObjects().
    // Called from connectViewportScrollSignals (extends the existing
    // m_toolChangedConn / m_selectionChangedConn lambdas), from changeEvent's
    // ActivationChange branch, and once after binding in setupManagedShortcuts.
    void updateObjectActionsEnabled();

    // MAC.6 review fix: Push this window's OcrSubToolbar button states onto
    // the 3 checkable OCR QActions. Called from connectViewportScrollSignals
    // after a tab switch (toolbar's restoreTabState uses blockSignals so the
    // user-driven sync edge in setupConnections doesn't fire) and from
    // changeEvent's ActivationChange branch (multi-window: the QActions are
    // app-global, so switching focus between two MainWindows whose toolbars
    // disagree must re-seed the menu checkmarks from the now-active window).
    void syncOcrCheckActions();

    // Keep the macOS Object Mode menu checkmarks aligned with the active
    // viewport's per-tab insert and action modes.
    void syncObjectModeCheckActions();

    /**
     * @brief Refresh the OS window title and NavigationBar filename label.
     *
     * Single source of truth for both. Reads the active pane's current tab
     * (display name + modified flag) and applies a platform-appropriate
     * format:
     *   - macOS: "<doc>[*]" (Apple HIG; bullet + close-button "edited" dot
     *     come from setWindowModified). Also calls setWindowFilePath() so
     *     the title bar gets the document proxy icon.
     *   - Other:  "<doc>[*] \xE2\x80\x94 SpeedyNote".
     *   - No doc: just "SpeedyNote", with setWindowModified(false).
     *
     * Connected to SplitViewManager::activeViewportChanged (active pane /
     * tab switches) and TabManager::currentTabDisplayChanged (rename,
     * markTabModified flips on the current tab).
     */
    void updateWindowTitle();

private:

    /**
     * @brief Convenience accessor for the active pane's TabManager.
     *
     * Most MainWindow code that used m_tabManager now uses this instead.
     * For operations that span both panes (iteration, search), use
     * m_splitViewManager->forEachTabManager() or leftTabManager()/rightTabManager().
     */
    inline TabManager* tabManager() const;

    // Note: returnToLauncher() removed - obsolete, replaced by toggleLauncher()
    
    /**
     * @brief Sync a document's viewport position to the document model.
     * @param doc The document to sync.
     * @param vp The viewport showing the document.
     * @return true if the document's stored position/history actually changed,
     *         false if it was already in sync (no-op).
     *
     * For paged documents: updates lastAccessedPage from viewport's current page.
     * For edgeless documents: defers to DocumentViewport::syncPositionToDocument,
     * which compares against the doc's stored values to report an honest result.
     *
     * NOTE: This does NOT mark the document as modified. Callers decide:
     * - Save operations: don't mark (the save itself persists the position).
     * - Desktop close (tab/app): use autosavePositionOnlyChange, which silently
     *   saves on a true return without ever flipping doc->modified.
     * - Mobile suspend: use syncAllDocumentPositions, which markModifies on
     *   true so autoSaveModifiedDocuments picks the doc up.
     */
    bool syncDocumentPosition(Document* doc, DocumentViewport* vp);
    
    /**
     * @brief Sync positions for all open documents AND mark them modified.
     * 
     * Iterates all tabs, syncs position, and marks modified if position changed.
     * Used before auto-save (Android) and before closeEvent checks.
     */
    void syncAllDocumentPositions();

    /**
     * @brief Commit any open inline text edit across every tab.
     *
     * In-progress inline text lives only in the object model until the session
     * commits, and an uncommitted page is never dirty, so autosave would write
     * the document without it. Explicit Save already commits first.
     */
    void commitAllInlineTextEdits();

    /**
     * @brief Sync position and silently auto-save if the only change is the position.
     *
     * Used by both tab-close and app-close to persist ephemeral view state
     * (edgeless last_position / paged lastAccessedPage) without flipping
     * doc->modified. After this call, doc->modified reflects only real edits,
     * so callers can use it directly to decide whether to prompt the user.
     *
     * Auto-saves only when: position actually changed, the doc is NOT a temp
     * bundle, the doc was not already modified, and a permanent path exists.
     * Mobile autosave continues to use syncAllDocumentPositions() above
     * because it relies on markModified to drive autoSaveModifiedDocuments.
     */
    void autosavePositionOnlyChange(Document* doc, DocumentViewport* vp);
    
    /**
     * @brief Save a new document with dialog prompt (Android-aware).
     * @param doc The document to save.
     * @return true if saved successfully, false if cancelled or failed.
     * 
     * On Android: Uses SaveDocumentDialog (touch-friendly) and saves to app-private storage.
     * On Desktop: Uses QFileDialog for standard save dialog.
     * 
     * This is the single source of truth for "Save As" functionality.
     * Used by saveDocument(), tab close handlers, and window close event.
     */
    bool saveNewDocumentWithDialog(Document* doc);
    
    /**
     * @brief Phase P.4.6: Render a thumbnail for page 0 of a document.
     * @param doc The document to render from.
     * @return The rendered thumbnail, or null pixmap on failure.
     * 
     * Used to save thumbnails to NotebookLibrary when closing documents.
     * Renders synchronously at a reasonable size for launcher display.
     */
    QPixmap renderPage0Thumbnail(Document* doc);
    
    /**
     * @brief Render a thumbnail for an edgeless (infinite canvas) document.
     * @param doc The edgeless document to render.
     * @return The rendered thumbnail, or null pixmap on failure.
     * 
     * Renders tiles near the document's last_position (the area the user
     * was last viewing). Falls back to the first indexed tile if no tiles
     * exist near last_position, or a default background if the canvas is empty.
     */
    QPixmap renderEdgelessThumbnail(Document* doc);
    
    /**
     * @brief Phase P.4.4: Toggle the launcher visibility.
     * 
     * If launcher is visible, hides it and brings MainWindow to front.
     * If launcher is hidden, shows it.
     * Connected to Ctrl+H shortcut and launcher button in NavigationBar.
     */
    void toggleLauncher();
    
    /**
     * @brief Phase P.4.3: Show the "+" button dropdown menu.
     * 
     * Displays a menu with options:
     * - New Edgeless Canvas (Ctrl+Shift+N)
     * - New Paged Notebook (Ctrl+N)
     * - ───────────────
     * - Open PDF... (Ctrl+Shift+O)
     * - Open Notebook... (Ctrl+Shift+L)
     */
    void showAddMenu();

    // Markdown notes sidebar functionality
    void toggleMarkdownNotesSidebar();  // Toggle markdown notes sidebar
    
    /**
     * @brief Phase M.8: Rebuild the right-sidebar outline tree from the
     *        current document (no .md file I/O).
     *
     * Obtains a fresh `LinkOutlineEntry` vector via
     * `Document::enumerateLinkOutline()` and forwards it to the sidebar.
     * Also updates edgeless-mode / hidden-tiles warning / notes directory.
     * Cheap enough to call from every `linkObjectListMayHaveChanged`
     * signal path.
     */
    void refreshNotesOutline();
    
    /**
     * @brief Phase M.3: Navigate to and select a LinkObject on the current page.
     * @param linkObjectId The UUID of the LinkObject to navigate to.
     * 
     * Scrolls the viewport to center the LinkObject and selects it.
     * Implementation in Task M.3.6.
     */
    void navigateToLinkObject(const QString& linkObjectId);
    
    /**
     * @brief Phase M.4: Search markdown notes across pages.
     * @param query Search query string.
     * @param fromPage Start page index (0-based).
     * @param toPage End page index (0-based).
     * @return List of matching notes with display data.
     * 
     * Searches LinkObject.description (100 pts), note title (75 pts), 
     * and note content (50 pts). Results sorted by score descending.
     */
    QList<NoteDisplayData> searchMarkdownNotes(const QString& query, int fromPage, int toPage);

    QMenu *overflowMenu;
    QAction* m_pdfSourcesAction = nullptr;

    // QListWidget *tabList;          // Horizontal tab bar
    // QStackedWidget *canvasStack;   // Holds multiple InkCanvas instances
    
    // Phase C.1.5: New tab system (QTabBar + QStackedWidget via TabManager)
    SplitViewManager *m_splitViewManager = nullptr;  // Manages split-view panes, tab bars, and viewports
    DocumentManager *m_documentManager = nullptr;  // Manages Document lifecycle
    
    // Toolbar extraction: NavigationBar (Phase A)
    NavigationBar *m_navigationBar = nullptr;
    
    // Toolbar extraction: Toolbar (Phase B)
    Toolbar *m_toolbar = nullptr;
    
    QWidget *m_canvasContainer = nullptr;
    int m_previousTabId = -1;  // Track previous tab ID for per-tab state management
    int m_previousPaneId = 0;  // Track previous active pane (0=Left, 1=Right)
    QHash<int, int> m_sidebarTabStates;  // Per-tab sidebar tab index, keyed by unique tab ID
    
    // Action Bar system
    ActionBarContainer *m_actionBarContainer = nullptr;
    LassoActionBar *m_lassoActionBar = nullptr;
    ObjectSelectActionBar *m_objectSelectActionBar = nullptr;
    TextSelectionActionBar *m_textSelectionActionBar = nullptr;
    PagePanelActionBar *m_pagePanelActionBar = nullptr;
    
    // PDF Search
    PdfSearchBar *m_pdfSearchBar = nullptr;
    PdfSearchEngine *m_searchEngine = nullptr;
    std::unique_ptr<PdfSearchState> m_searchState;

    // SBS2: whole-document streaming scan (live match count + SBS3 marker store)
    QTimer *m_searchScanDebounce = nullptr;
    QHash<int, QVector<PdfSearchMatch>> m_searchResultsByPage;
    int m_searchTotalMatches = 0;
    // SBS3: coalesces scroll-bar search-marker refreshes during streaming
    QTimer *m_searchMarkerRefresh = nullptr;
    
    // OCR
    QThread *m_ocrThread = nullptr;
    OcrWorker *m_ocrWorker = nullptr;
    QTimer *m_ocrDebounceTimer = nullptr;
    bool m_autoOcrEnabled = false;
    QStringList m_ocrAvailableLanguages;
    QStringList m_ocrDownloadedLanguages;
    std::set<std::pair<int,int>> m_ocrTempLoadedTiles;
    Document* m_ocrTempLoadedDoc = nullptr;
    
    // Page Panel: Task 5.3: Pending delete state for undo support
    int m_pendingDeletePageIndex = -1;
    
    // Phase C.1.5: addTabButton removed - functionality now in NavigationBar
    QWidget *tabBarContainer;      // Container for horizontal tab bar (legacy, hidden)
    
    // PDF Outline Sidebar
    // REMOVED MW7.5: Outline sidebar variables removed - outline sidebar deleted
    
    // REMOVED MW7.4: Bookmarks Sidebar removed - bookmark implementation deleted
    
    // Phase S3: Left Sidebar Container (replaces floating tabs)
    LeftSidebarContainer *m_leftSidebar = nullptr;  // Tabbed container for left panels
    QSplitter *m_contentSplitter = nullptr;         // Splitter for resizable sidebar
    QTimer *m_sidebarWidthSaveTimer = nullptr;      // Debounce timer for persisting width
    LayerPanel *m_layerPanel = nullptr;             // Reference to LayerPanel in container
    PagePanel *m_pagePanel = nullptr;               // Reference to PagePanel in container
    // QWidget *m_leftSideContainer = nullptr;       // Container for sidebars + layer panel
    // QPushButton *toggleLayerPanelButton = nullptr; // Floating tab button to toggle layer panel
    bool layerPanelVisible = true;                   // Layer panel visible by default
    
    // void positionLeftSidebarTabs();  // Position the floating tabs for left sidebars
    
    // Debug Overlay (development tool - easily disabled for production)
    class DebugOverlay* m_debugOverlay = nullptr;  // Floating debug info panel
    void toggleDebugOverlay();                      // Toggle debug overlay visibility
    
    // Paint performance HUD (available in release builds; see ViewportPerfMonitor)
    bool m_perfHudEnabled = false;                  // Whether instrumentation is on
    void togglePerfHud();                           // Toggle paint instrumentation
    void applyPerfHudToViewport(DocumentViewport* viewport);  // Carry state across tabs
    
    // Two-column layout toggle (Ctrl+2)
    void toggleAutoLayout();                        // Toggle auto 1/2 column layout mode
    // REMOVED MW7.4: bookmarks QMap removed - bookmark implementation deleted
    // QPushButton *jumpToPageButton; // Button to jump to a specific page
    
    // Markdown Notes Sidebar
    MarkdownNotesSidebar *markdownNotesSidebar;  // Sidebar for markdown notes
    // QPushButton *toggleMarkdownNotesButton; // Button to toggle markdown notes sidebar
    bool markdownNotesSidebarVisible = false;


    QWidget *sidebarContainer;  // Container for sidebar

    
    // ✅ Mouse dial controls
    
    // ✅ Override mouse events for dial control
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;


    QTimer *tooltipTimer;
    QWidget *lastHoveredWidget;
    QPoint pendingTooltipPos;
    
 

    // TODO G.6: Re-enable after LauncherWindow remake
    // RecentNotebooksManager *recentNotebooksManager;


    
    // Single instance support
    QLocalServer *localServer;
    

    void setupUi();

    void loadUserSettings();

    QPointer<DocumentViewport> m_connectedViewport;  // QPointer for safe dangling check
    
    // CR-2B: Tool/mode signal connections (for keyboard shortcut sync)
    QMetaObject::Connection m_toolChangedConn;
    QMetaObject::Connection m_straightLineModeConn;
    
    // Phase D: Auto-highlight sync connection (subtoolbar ↔ viewport)
    QMetaObject::Connection m_autoHighlightConn;

    // Highlighter selection-source sync connection (viewport -> subtoolbar)
    QMetaObject::Connection m_highlighterModeConn;

    // Phase D: Object mode sync connections (subtoolbar ↔ viewport)
    QMetaObject::Connection m_insertModeConn;
    QMetaObject::Connection m_actionModeConn;
    QMetaObject::Connection m_selectionChangedConn;
    
    // Action Bar: Selection state connections (viewport → action bar container)
    QMetaObject::Connection m_lassoSelectionConn;
    /// Lasso swatch sync: re-seeds LassoActionBar's recolor swatch from the
    /// current pen color whenever a fresh selection appears on the active
    /// viewport. Tracked separately from m_lassoSelectionConn so it can be
    /// disconnected on viewport switch (otherwise duplicates accumulate per
    /// switch and stale viewports keep firing the lambda).
    QMetaObject::Connection m_lassoSwatchSyncConn;
    QMetaObject::Connection m_objectSelectionForActionBarConn;
    QMetaObject::Connection m_textSelectionConn;
    QMetaObject::Connection m_strokeClipboardConn;
    QMetaObject::Connection m_objectClipboardConn;
    
    // Smart tool auto-switch: clipboard signals → tool override connections
    QMetaObject::Connection m_strokeClipboardOverrideConn;
    QMetaObject::Connection m_objectClipboardOverrideConn;
    
    // Smart tool auto-switch state (split-view copy/paste assistance)
    QPointer<DocumentViewport> m_toolOverrideViewport;
    ToolType m_toolOverrideSavedTool = ToolType::Pen;
    void applyToolOverrideForClipboard(ToolType requiredTool);
    void clearToolOverride(bool revert);
    
    // Phase 5.1: LayerPanel page change connection
    QMetaObject::Connection m_layerPanelPageConn;
    
    // Phase E.2: OutlinePanel page change connection (for section highlighting)
    QMetaObject::Connection m_outlinePageConn;
    
    // Page Panel: Task 5.2: PagePanel page change connection
    QMetaObject::Connection m_pagePanelPageConn;
    QMetaObject::Connection m_pagePanelContentConn;  // For documentModified → thumbnail invalidation
    QMetaObject::Connection m_pagePanelPageModConn;  // For pageModified → targeted thumbnail invalidation
    QMetaObject::Connection m_pagePanelActionBarConn;  // For currentPageChanged → action bar sync
    QMetaObject::Connection m_pageStructureUndoConn;   // Plan A2: PageDelete undo/redo → refresh page panel
    QMetaObject::Connection m_documentModifiedConn;    // BUG FIX: documentModified → mark doc/tab modified
    QMetaObject::Connection m_markdownNotesPageConn;  // Phase M.3: For page change → notes reload
    QMetaObject::Connection m_markdownNoteOpenConn;   // Phase M.5: For requestOpenMarkdownNote
    QMetaObject::Connection m_userWarningConn;        // For viewport userWarning → QMessageBox
    QMetaObject::Connection m_linkObjectListConn;     // M.7.3: For linkObjectListMayHaveChanged
    QMetaObject::Connection m_linkAppearanceConn;     // For linkObjectAppearanceChanged
    QMetaObject::Connection m_pdfSourcesConn;
    QMetaObject::Connection m_strokesChangedConn;      // OCR: For strokesChanged → debounce
    QMetaObject::Connection m_ocrConvertConn;          // For convertOcrTextRequested
    QMetaObject::Connection m_textBoxLayoutConn;       // Text-box commit → search invalidation
    
    // Pan tool hold (H key spring-loaded activation)
    bool m_panHoldActive = false;
    ToolType m_toolBeforePanHold = ToolType::Pen;
    int m_panHoldKey = 0;
    
    // Event filter for scrollbar hover detection
    bool eventFilter(QObject *obj, QEvent *event) override;
    
    // Update scrollbar positions based on container size
    void updateScrollbarPositions();
    
    void connectSubToolbarSignals();   // Wire subtoolbar signals to viewports
    
    // Action Bar setup and positioning
    void setupActionBars();            // Create and connect action bars
    void updateActionBarPosition();    // Update position on viewport resize
    void setupPagePanelActionBar();    // Page Panel: Task 5.3: Create and connect PagePanelActionBar
    void updatePagePanelActionBarVisibility();  // Page Panel: Task 5.4: Update visibility based on tab and document
    
    // Phase E.2: PDF Outline Panel connections
    void setupOutlinePanelConnections();  // Connect outline panel signals
    
    // Page Panel: Task 5.2: Page Panel connections
    void setupPagePanelConnections();  // Connect page panel signals
    
    // PDF Search
    void setupPdfSearch();             // Create search bar and engine
    void updatePdfSearchBarPosition(); // Update position on viewport resize
    void showPdfSearchBar();           // Show and focus search bar (Ctrl+F)
    void hidePdfSearchBar();           // Hide search bar and clear highlights
    void onSearchNext(const QString& text, bool caseSensitive, bool wholeWord);
    void onSearchPrev(const QString& text, bool caseSensitive, bool wholeWord);
    void onSearchMatchFound(const PdfSearchMatch& match, const QVector<PdfSearchMatch>& pageMatches);
    void onSearchNotFound(bool wrapped);
    // SBS2: live whole-document scan (debounced) + streamed aggregation
    void onSearchTextChanged(const QString& text);
    void onSearchScanPage(int pageIndex, const QVector<PdfSearchMatch>& matches);
    void onSearchScanComplete(int totalMatches);
    void updateSearchCountStatus();    // Reflect m_searchTotalMatches in the bar
    // SBS3: scroll-bar search-hit ticks
    void refreshSearchMarkers();       // Push current aggregate to the active bar
    void onSearchMarkerActivated(DocumentViewport* vp, int pageIndex, qreal normY, int matchIndex);
    
    // OCR
    void setupOcr();
    void triggerOcrForCurrentPage();
    void triggerOcrForAllPages();
    void onDebounceTimeout();
    void onOcrResultsReady(const QString& pageId, const QVector<OcrTextBlock>& blocks);
    void onOcrBatchFinished(int pagesScanned, int pagesWithText);
    void onOcrError(const QString& pageId, const QString& message);
    QVector<VectorStroke> collectPageStrokes(const Page* page) const;
    void syncOcrTextObjects(Document* owner, Page* page,
                            const QVector<OcrTextBlock>& blocks);
    /**
     * @brief Drop viewport references to objects about to be destroyed.
     *
     * Selection and hover hold raw pointers, so any code that frees objects
     * directly on a Page must notify every viewport showing @p owner first.
     */
    void forgetObjectsInViewports(Document* owner,
                                  const QVector<QString>& objectIds);
    void setOcrTextVisibility(bool visible);
    void setOcrConfidenceVisibility(bool enabled);
    // MAC.6: showOcrLanguageDialog() promoted to private slots: above.
    QString resolveOcrLanguage(Document* doc) const;
    OcrSnapParams buildOcrSnapParams(Document* doc, Page* page) const;
    
    /// Confirms with the user, then asks the viewport to replace a recognized
    /// OCR block with an editable text box.
    void convertOcrTextToTextBox(InsertedObject* obj);
    
    // Responsive toolbar management - REMOVED MW4.3: All layout functions and variables removed
    
    // Helper function for tab text eliding
    QString elideTabText(const QString &text, int maxWidth);
    
    // Layout timers and separators - REMOVED MW4.3: No longer needed
    
    // Keyboard Shortcut Hub
    //
    // Almost every keyboard shortcut now lives on a registry QAction (owned
    // by ShortcutManager, dispatched via wireQActionDispatchers). m_escapeShortcut
    // is the lone exception — it's a per-window QShortcut because Escape
    // dismissal walks a per-window priority list (modal -> search bar ->
    // viewport -> launcher) that doesn't fit the activeMainWindow() dispatch
    // model and needs Qt::WindowShortcut scope.
    // setupManagedShortcuts() also installs an unnamed Cmd+K alternate for
    // Settings on macOS (parent-owned by `this`, no separate handle needed).
    QShortcut* m_escapeShortcut = nullptr;
    void setupManagedShortcuts();  // Wires every registry QAction to this window plus the two non-registry cases above.

    // MAC.3: One-time, app-wide wiring of QAction triggered() signals to their
    // handlers via MainWindow::activeMainWindow() dispatch. Static so it runs
    // exactly once per process regardless of how many MainWindows are created;
    // each MainWindow still calls addAction() per migrated id so the QAction's
    // shortcut is registered in that window's shortcut map.
    static void wireQActionDispatchers();

#ifdef Q_OS_LINUX
    // Palm rejection state (Linux only)
    // Temporarily disables touch gestures while the stylus is in proximity.
    // Restores the user's configured mode after a delay when the stylus leaves.
    bool m_palmRejectionEnabled = false;
    int m_palmRejectionDelayMs = 500;
    QTimer* m_palmRejectionTimer = nullptr;
    bool m_palmRejectionActive = false;  ///< Whether currently suppressing touch gestures
    void onStylusProximityEnter();
    void onStylusProximityLeave();
#endif
};

#endif // MAINWINDOW_H

