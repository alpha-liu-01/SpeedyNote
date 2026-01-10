#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
// #include "InkCanvas.h"  // Phase 3.1.7: Disconnected - using DocumentViewport
#include "MarkdownNotesSidebar.h"
#include "core/Page.h"  // Phase 3.1.8: For Page::BackgroundType

// Phase 3.1.7: Forward declaration for legacy method signatures (will be removed)
// class InkCanvas;
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QFuture>
#include <QLineEdit>
#include <QSlider>
#include <QScrollBar>
#include <QComboBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QDialog>
#include <QFileDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QDial>
#include "SimpleAudio.h"
#include <QFont>
#include <QQueue>
#include "SDLControllerManager.h"
#include "ButtonMappingTypes.h"
#include "RecentNotebooksManager.h"
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QMenu>
#include <QCloseEvent>
// #include "ControlPanelDialog.h"  // Phase 3.1.8: Disabled - depends on InkCanvas
#include "PictureWindowManager.h"
#include "SpnPackageManager.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
// Phase C.1.5: QTabWidget removed - using QTabBar + QStackedWidget
// Phase C.2: Using custom TabBar class
#include "ui/TabBar.h"
#include <QStackedWidget>

// Phase 3.1: New architecture includes
#include "ui/TabManager.h"
#include "core/DocumentManager.h"
#include "core/ToolType.h"
#include <QElapsedTimer>

// Toolbar extraction includes
#include "ui/NavigationBar.h"
#include "ui/Toolbar.h"
#include "ui/sidebars/LeftSidebarContainer.h"  // Phase S3: Left sidebar container

// Phase D: Subtoolbar includes
class SubToolbarContainer;
class PenSubToolbar;
class MarkerSubToolbar;
class HighlighterSubToolbar;
class ObjectSelectSubToolbar;

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
class DebugOverlay;
namespace Poppler { 
    class Document; 
    class OutlineItem;
}

// Forward declarations
class LauncherWindow;

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

    // Theme settings
    QColor customAccentColor;
    bool useCustomAccentColor = false;
    
    QColor getAccentColor() const;
    QColor getCustomAccentColor() const { return customAccentColor; }
    void setCustomAccentColor(const QColor &color);
    bool isUsingCustomAccentColor() const { return useCustomAccentColor; }
    void setUseCustomAccentColor(bool use);


    QColor getDefaultPenColor();

    SDLControllerManager *controllerManager = nullptr;
    QThread *controllerThread = nullptr;


    
    // Single instance functionality
    static bool isInstanceRunning();
    static bool sendToExistingInstance(const QString &filePath);
    void setupSingleInstanceServer();
    
    // Theme/palette management
    static void updateApplicationPalette(); // Update Qt application palette based on dark mode
    void openFileInNewTab(const QString &filePath); // ✅ Open .spn package directly


    
    void saveThemeSettings();
    void loadThemeSettings();
    void updateTheme(); // Apply current theme settings
    // REMOVED: updateTabSizes removed - tab sizing functionality deleted
    
    // REMOVED MW7.6: migrateOldButtonMappings and migrateOldActionString removed - old mapping system deleted

    // InkCanvas* currentCanvas();  // MW1.4: Stub - returns nullptr, use currentViewport()
    DocumentViewport* currentViewport() const; // Phase 3.1.4: New accessor for DocumentViewport

    void switchPage(int pageNumber); // Made public for RecentNotebooksDialog
    // REMOVED MW7.7: switchPageWithDirection stub removed - replaced with switchPage calls

    QSpinBox *pageInput = nullptr; // Made public for RecentNotebooksDialog

    
    void updateTabLabel(); // Made public for RecentNotebooksDialog    
    // New: Keyboard mapping methods (made public for ControlPanelDialog)
    // REMOVED MW7.6: addKeyboardMapping, removeKeyboardMapping, and getKeyboardMappings removed - old mapping system deleted
    
    // Controller access
    SDLControllerManager* getControllerManager() const { return controllerManager; }
    void reconnectControllerSignals(); // Reconnect controller signals after reconnection

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


    // Phase 3.1: sharedLauncher disconnected - will be re-linked later
    // static LauncherWindow *sharedLauncher;

    static QSharedMemory *sharedMemory;
    
    // Static cleanup method for signal handlers
    static void cleanupSharedResources();



protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;  // New: Handle keyboard shortcuts
    void keyReleaseEvent(QKeyEvent *event) override; // Track Ctrl key release for trackpad pinch-zoom detection
    // REMOVED: tabletEvent removed - tablet event handling deleted

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override; // Handle Windows theme changes
#endif
    void closeEvent(QCloseEvent *event) override; // ✅ Add auto-save on program close
    
    // IME support for multi-language input
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private slots:

    void onNewConnection(); // Handle new instance connections

    void updatePanX(int value);
    void updatePanY(int value);


    void forceUIRefresh();

    void switchTab(int index);

    void removeTabAt(int index);
    // void toggleThicknessSlider(); // Added function to toggle thickness slider
    void toggleFullscreen();
    void showJumpToPageDialog();
    void goToPreviousPage(); // Go to previous page
    void goToNextPage();     // Go to next page
    void onPageInputChanged(int newPage); // Handle spinbox page changes with direction tracking


    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    void connectViewportScrollSignals(DocumentViewport* viewport);  // Phase 3.3
    void centerViewportContent(int tabIndex);  // Phase 3.3: One-time horizontal centering
    void updateLayerPanelForViewport(DocumentViewport* viewport);  // Phase 5.1: Update LayerPanel
    void updateLinkSlotButtons(DocumentViewport* viewport);  // Phase D: Update subtoolbar slot buttons
    void applySubToolbarValuesToViewport(ToolType tool);  // Phase D: Apply subtoolbar presets to viewport (via signals)
    void applyAllSubToolbarValuesToViewport(DocumentViewport* viewport);  // Phase D: Apply ALL tool presets directly
    
    // Phase doc-1: Document operations
    void saveDocument();          // doc-1.1: Save document to JSON file (Ctrl+S)
    void loadDocument();          // doc-1.2: Load document from JSON file (Ctrl+O)
    void addPageToDocument();     // doc-1.0: Add page at end of document (Ctrl+Shift+A)
    void insertPageInDocument();  // Phase 3: Insert page after current (Ctrl+Shift+I)
    void deletePageInDocument();  // Phase 3B: Delete current page (Ctrl+Shift+D)
    void openPdfDocument(const QString &filePath = QString());       // doc-1.4: Open PDF file (Ctrl+Shift+O)
    qreal getDevicePixelRatio(); 
    bool isDarkMode();
 
private:

    void returnToLauncher(); // Return to launcher window

    // Markdown notes sidebar functionality
    void toggleMarkdownNotesSidebar();  // Toggle markdown notes sidebar

    QMenu *overflowMenu;
    QScrollBar *panXSlider;
    QScrollBar *panYSlider;


    // QListWidget *tabList;          // Horizontal tab bar
    // QStackedWidget *canvasStack;   // Holds multiple InkCanvas instances
    
    // Phase C.1.5: New tab system (QTabBar + QStackedWidget via TabManager)
    // Phase C.2: Using custom TabBar class for theming
    TabManager *m_tabManager = nullptr;     // Manages tabs and DocumentViewports
    DocumentManager *m_documentManager = nullptr;  // Manages Document lifecycle
    TabBar *m_tabBar = nullptr;            // Custom tab bar with built-in theming
    QStackedWidget *m_viewportStack = nullptr;  // Viewport container
    
    // Toolbar extraction: NavigationBar (Phase A)
    NavigationBar *m_navigationBar = nullptr;
    
    // Toolbar extraction: Toolbar (Phase B)
    Toolbar *m_toolbar = nullptr;
    
    // Phase D: Subtoolbar system
    SubToolbarContainer *m_subtoolbarContainer = nullptr;
    PenSubToolbar *m_penSubToolbar = nullptr;
    MarkerSubToolbar *m_markerSubToolbar = nullptr;
    HighlighterSubToolbar *m_highlighterSubToolbar = nullptr;
    ObjectSelectSubToolbar *m_objectSelectSubToolbar = nullptr;
    QWidget *m_canvasContainer = nullptr;  // Stored for subtoolbar positioning
    int m_previousTabIndex = -1;  // Track previous tab for per-tab state management
    
    // Phase C.1.5: addTabButton removed - functionality now in NavigationBar
    QWidget *tabBarContainer;      // Container for horizontal tab bar (legacy, hidden)
    
    // PDF Outline Sidebar
    // REMOVED MW7.5: Outline sidebar variables removed - outline sidebar deleted
    
    // REMOVED MW7.4: Bookmarks Sidebar removed - bookmark implementation deleted
    
    // Phase S3: Left Sidebar Container (replaces floating tabs)
    LeftSidebarContainer *m_leftSidebar = nullptr;  // Tabbed container for left panels
    LayerPanel *m_layerPanel = nullptr;             // Reference to LayerPanel in container
    // QWidget *m_leftSideContainer = nullptr;       // Container for sidebars + layer panel
    // QPushButton *toggleLayerPanelButton = nullptr; // Floating tab button to toggle layer panel
    bool layerPanelVisible = true;                   // Layer panel visible by default
    
    // void positionLeftSidebarTabs();  // Position the floating tabs for left sidebars
    
    // Debug Overlay (development tool - easily disabled for production)
    class DebugOverlay* m_debugOverlay = nullptr;  // Floating debug info panel
    void toggleDebugOverlay();                      // Toggle debug overlay visibility
    
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
    
 

    RecentNotebooksManager *recentNotebooksManager; // Added manager instance


    
    // Single instance support
    QLocalServer *localServer;
    

    void setupUi();

    void loadUserSettings();

    bool scrollbarsVisible = false;
    QTimer *scrollbarHideTimer = nullptr;
    bool m_hasKeyboard = false;  // MW5.8: Cached keyboard detection result
    
    // MW5.8: Keyboard detection and scrollbar visibility
    bool hasPhysicalKeyboard();   // Check if physical keyboard is connected
    void showScrollbars();        // Show scrollbars and reset hide timer
    void hideScrollbars();        // Hide scrollbars
    
    // Phase 3.3: Viewport scroll signal connections (for proper cleanup)
    QMetaObject::Connection m_hScrollConn;
    QMetaObject::Connection m_vScrollConn;
    QPointer<DocumentViewport> m_connectedViewport;  // QPointer for safe dangling check
    
    // CR-2B: Tool/mode signal connections (for keyboard shortcut sync)
    QMetaObject::Connection m_toolChangedConn;
    QMetaObject::Connection m_straightLineModeConn;
    
    // Phase D: Auto-highlight sync connection (subtoolbar ↔ viewport)
    QMetaObject::Connection m_autoHighlightConn;
    
    // Phase D: Object mode sync connections (subtoolbar ↔ viewport)
    QMetaObject::Connection m_insertModeConn;
    QMetaObject::Connection m_actionModeConn;
    QMetaObject::Connection m_selectionChangedConn;
    
    // Phase 5.1: LayerPanel page change connection
    QMetaObject::Connection m_layerPanelPageConn;
    
    // Trackpad vs mouse wheel routing (see eventFilter wheel handling)
    bool trackpadModeActive = false;
    QTimer *trackpadModeTimer = nullptr;
    QElapsedTimer lastWheelEventTimer;
    

  
    
    // Event filter for scrollbar hover detection and dial container drag
    bool eventFilter(QObject *obj, QEvent *event) override;
    
    // Update scrollbar positions based on container size
    void updateScrollbarPositions();
    
    // Phase D: Subtoolbar setup and positioning
    void setupSubToolbars();           // Create and connect subtoolbars
    void updateSubToolbarPosition();   // Update position on viewport resize
    
    // Responsive toolbar management - REMOVED MW4.3: All layout functions and variables removed
    
    // Helper function for tab text eliding
    QString elideTabText(const QString &text, int maxWidth);
    
    // Layout timers and separators - REMOVED MW4.3: No longer needed
};

#endif // MAINWINDOW_H

