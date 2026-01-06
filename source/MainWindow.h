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

enum class ControllerAction {
    None,
    ToggleFullscreen,
    // REMOVED MW7.2: ToggleDial removed - dial functionality deleted
    Zoom50,
    ZoomOut,
    Zoom200,
    AddPreset,
    DeletePage,
    FastForward,
    OpenControlPanel,
    RedColor,
    BlueColor,
    YellowColor,
    GreenColor,
    BlackColor,
    WhiteColor,
    CustomColor,
    ToggleSidebar,
    Save,
    StraightLineTool,
    RopeTool,
    SetPenTool,
    SetMarkerTool,
    SetEraserTool,
    TogglePdfTextSelection,
    // REMOVED MW7.5: ToggleOutline removed - outline sidebar deleted
    // REMOVED MW7.4: ToggleBookmarks and AddBookmark removed - bookmark implementation deleted
    ToggleTouchGestures,
    PreviousPage,
    NextPage
};

// Stylus button actions (hold-to-enable style)
enum class StylusButtonAction {
    None,
    HoldStraightLine,    // Enable straight line mode while held
    HoldLasso,           // Enable lasso/rope tool while held
    HoldEraser,          // Enable eraser while held
    HoldTextSelection    // Enable PDF text selection while held
};

[[maybe_unused]] static QString actionToString(ControllerAction action) {
    switch (action) {
        case ControllerAction::ToggleFullscreen: return "Toggle Fullscreen";
        // REMOVED MW7.2: ToggleDial case removed - dial functionality deleted
        case ControllerAction::Zoom50: return "Zoom 50%";
        case ControllerAction::ZoomOut: return "Zoom Out";
        case ControllerAction::Zoom200: return "Zoom 200%";
        case ControllerAction::AddPreset: return "Add Preset";
        case ControllerAction::DeletePage: return "Delete Page";
        case ControllerAction::FastForward: return "Fast Forward";
        case ControllerAction::OpenControlPanel: return "Open Control Panel";
        case ControllerAction::RedColor: return "Red";
        case ControllerAction::BlueColor: return "Blue";
        case ControllerAction::YellowColor: return "Yellow";
        case ControllerAction::GreenColor: return "Green";
        case ControllerAction::BlackColor: return "Black";
        case ControllerAction::WhiteColor: return "White";
        case ControllerAction::CustomColor: return "Custom Color";
        case ControllerAction::ToggleSidebar: return "Toggle Sidebar";
        case ControllerAction::Save: return "Save";
        case ControllerAction::StraightLineTool: return "Straight Line Tool";
        case ControllerAction::RopeTool: return "Rope Tool";
        case ControllerAction::SetPenTool: return "Set Pen Tool";
        case ControllerAction::SetMarkerTool: return "Set Marker Tool";
        case ControllerAction::SetEraserTool: return "Set Eraser Tool";
        case ControllerAction::TogglePdfTextSelection: return "Toggle PDF Text Selection";
        // REMOVED MW7.5: ToggleOutline case removed - outline sidebar deleted
        // REMOVED MW7.4: ToggleBookmarks and AddBookmark cases removed - bookmark implementation deleted
        case ControllerAction::ToggleTouchGestures: return "Toggle Touch Gestures";
        case ControllerAction::PreviousPage: return "Previous Page";
        case ControllerAction::NextPage: return "Next Page";
        default: return "None";
    }
}

[[maybe_unused]] static ControllerAction stringToAction(const QString &str) {
    // Convert internal key to ControllerAction enum
    InternalControllerAction internalAction = ButtonMappingHelper::internalKeyToAction(str);
    
    switch (internalAction) {
        case InternalControllerAction::None: return ControllerAction::None;
        case InternalControllerAction::ToggleFullscreen: return ControllerAction::ToggleFullscreen;
        // REMOVED MW7.2: InternalControllerAction ToggleDial removed - dial functionality deleted
        case InternalControllerAction::Zoom50: return ControllerAction::Zoom50;
        case InternalControllerAction::ZoomOut: return ControllerAction::ZoomOut;
        case InternalControllerAction::Zoom200: return ControllerAction::Zoom200;
        case InternalControllerAction::AddPreset: return ControllerAction::AddPreset;
        case InternalControllerAction::DeletePage: return ControllerAction::DeletePage;
        case InternalControllerAction::FastForward: return ControllerAction::FastForward;
        case InternalControllerAction::OpenControlPanel: return ControllerAction::OpenControlPanel;
        case InternalControllerAction::RedColor: return ControllerAction::RedColor;
        case InternalControllerAction::BlueColor: return ControllerAction::BlueColor;
        case InternalControllerAction::YellowColor: return ControllerAction::YellowColor;
        case InternalControllerAction::GreenColor: return ControllerAction::GreenColor;
        case InternalControllerAction::BlackColor: return ControllerAction::BlackColor;
        case InternalControllerAction::WhiteColor: return ControllerAction::WhiteColor;
        case InternalControllerAction::CustomColor: return ControllerAction::CustomColor;
        case InternalControllerAction::ToggleSidebar: return ControllerAction::ToggleSidebar;
        case InternalControllerAction::Save: return ControllerAction::Save;
        case InternalControllerAction::StraightLineTool: return ControllerAction::StraightLineTool;
        case InternalControllerAction::RopeTool: return ControllerAction::RopeTool;
        case InternalControllerAction::SetPenTool: return ControllerAction::SetPenTool;
        case InternalControllerAction::SetMarkerTool: return ControllerAction::SetMarkerTool;
        case InternalControllerAction::SetEraserTool: return ControllerAction::SetEraserTool;
        case InternalControllerAction::TogglePdfTextSelection: return ControllerAction::TogglePdfTextSelection;
        // REMOVED MW7.5: InternalControllerAction ToggleOutline removed - outline sidebar deleted
        // REMOVED MW7.4: InternalControllerAction ToggleBookmarks and AddBookmark removed - bookmark implementation deleted
        case InternalControllerAction::ToggleTouchGestures: return ControllerAction::ToggleTouchGestures;
        case InternalControllerAction::PreviousPage: return ControllerAction::PreviousPage;
        case InternalControllerAction::NextPage: return ControllerAction::NextPage;
    }
    return ControllerAction::None;
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

    bool lowResPreviewEnabled = true;

    void setLowResPreviewEnabled(bool enabled);
    bool isLowResPreviewEnabled() const;

    bool areBenchmarkControlsVisible() const;
    // void setBenchmarkControlsVisible(bool visible);

    // bool zoomButtonsVisible = true;
    bool areZoomButtonsVisible() const;
    void setZoomButtonsVisible(bool visible);

    bool scrollOnTopEnabled = false;
    bool isScrollOnTopEnabled() const;
    void setScrollOnTopEnabled(bool enabled);

    TouchGestureMode touchGestureMode = TouchGestureMode::Full;
    TouchGestureMode previousTouchGestureMode = TouchGestureMode::Full; // Store state before text selection
    TouchGestureMode getTouchGestureMode() const;
    void setTouchGestureMode(TouchGestureMode mode);
    void cycleTouchGestureMode(); // Cycle through: Disabled -> YAxisOnly -> Full -> Disabled

#ifdef Q_OS_LINUX
    // Palm rejection settings (Linux only - Windows has built-in palm rejection)
    bool palmRejectionEnabled = false;
    int palmRejectionDelayMs = 500; // Default 500ms delay before restoring touch gestures
    
    bool isPalmRejectionEnabled() const { return palmRejectionEnabled; }
    void setPalmRejectionEnabled(bool enabled);
    int getPalmRejectionDelay() const { return palmRejectionDelayMs; }
    void setPalmRejectionDelay(int delayMs);
#endif

    // Stylus side button mapping settings
    StylusButtonAction stylusButtonAAction = StylusButtonAction::None;
    StylusButtonAction stylusButtonBAction = StylusButtonAction::None;
    Qt::MouseButton stylusButtonAQt = Qt::MiddleButton; // Which Qt button maps to "Button A"
    Qt::MouseButton stylusButtonBQt = Qt::RightButton;  // Which Qt button maps to "Button B"
    
    StylusButtonAction getStylusButtonAAction() const { return stylusButtonAAction; }
    StylusButtonAction getStylusButtonBAction() const { return stylusButtonBAction; }
    Qt::MouseButton getStylusButtonAQt() const { return stylusButtonAQt; }
    Qt::MouseButton getStylusButtonBQt() const { return stylusButtonBQt; }
    void setStylusButtonAAction(StylusButtonAction action);
    void setStylusButtonBAction(StylusButtonAction action);
    void setStylusButtonAQt(Qt::MouseButton button);
    void setStylusButtonBQt(Qt::MouseButton button);
    void saveStylusButtonSettings();
    void loadStylusButtonSettings();

    // Theme settings
    QColor customAccentColor;
    bool useCustomAccentColor = false;
    
    QColor getAccentColor() const;
    QColor getCustomAccentColor() const { return customAccentColor; }
    void setCustomAccentColor(const QColor &color);
    bool isUsingCustomAccentColor() const { return useCustomAccentColor; }
    void setUseCustomAccentColor(bool use);
    
    // Color palette settings for control panel
    bool isUsingBrighterPalette() const { return useBrighterPalette; }
    void setUseBrighterPalette(bool use);
    
    QColor getDefaultPenColor();

    SDLControllerManager *controllerManager = nullptr;
    QThread *controllerThread = nullptr;

    // REMOVED MW7.6: getPressMapping removed - old mapping system deleted

    // REMOVED MW7.6: saveButtonMappings and loadButtonMappings removed - old mapping system deleted

    // REMOVED MW7.6: setPressMapping removed - old mapping system deleted

    
    // ✅ Mouse dial mapping management


    // REMOVED MW7.7: openPdfFile stub removed - replaced with openPdfDocument(filePath)
    
    // Single instance functionality
    static bool isInstanceRunning();
    static bool sendToExistingInstance(const QString &filePath);
    void setupSingleInstanceServer();
    
    // Theme/palette management
    static void updateApplicationPalette(); // Update Qt application palette based on dark mode
    void openFileInNewTab(const QString &filePath); // ✅ Open .spn package directly

    int getPdfDPI() const { return pdfRenderDPI; }
    // REMOVED: setPdfDPI removed - PDF DPI functionality deleted

    // void loadUserSettings();  // New
    void savePdfDPI(int dpi); // New

    // Background settings persistence
    // Phase 3.1.8: Migrated from BackgroundStyle to Page::BackgroundType
    void saveDefaultBackgroundSettings(Page::BackgroundType style, QColor bgColor, QColor gridColor, int density);
    void loadDefaultBackgroundSettings(Page::BackgroundType &style, QColor &bgColor, QColor &gridColor, int &density);
    
    void saveThemeSettings();
    void loadThemeSettings();
    void updateTheme(); // Apply current theme settings
    // REMOVED: updateTabSizes removed - tab sizing functionality deleted
    
    // REMOVED MW7.6: migrateOldButtonMappings and migrateOldActionString removed - old mapping system deleted

    // InkCanvas* currentCanvas();  // MW1.4: Stub - returns nullptr, use currentViewport()
    DocumentViewport* currentViewport() const; // Phase 3.1.4: New accessor for DocumentViewport
    void saveCurrentPage(); // Made public for RecentNotebooksDialog
    void saveCurrentPageConcurrent(); // Concurrent version for smooth page flipping
    void switchPage(int pageNumber); // Made public for RecentNotebooksDialog
    // REMOVED MW7.7: switchPageWithDirection stub removed - replaced with switchPage calls
    void updateTabLabel(); // Made public for RecentNotebooksDialog
    QSpinBox *pageInput; // Made public for RecentNotebooksDialog
    
    // New: Keyboard mapping methods (made public for ControlPanelDialog)
    // REMOVED MW7.6: addKeyboardMapping, removeKeyboardMapping, and getKeyboardMappings removed - old mapping system deleted
    
    // Controller access
    SDLControllerManager* getControllerManager() const { return controllerManager; }
    void reconnectControllerSignals(); // Reconnect controller signals after reconnection

    void updateDialButtonState();     // Update dial button state when switching tabs
    void updateFastForwardButtonState(); // Update fast forward button state when switching tabs
    // REMOVED: updateToolButtonStates removed - tool button state functionality deleted
    // void handleColorButtonClick();    // Handle tool switching when color buttons are clicked
    // void updateThicknessSliderForCurrentTool(); // Update thickness slider to reflect current tool's thickness
    void updatePdfTextSelectButtonState(); // Update PDF text selection button state when switching tabs
    // REMOVED MW7.4: updateBookmarkButtonState removed - bookmark implementation deleted

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

private slots:
    void toggleBenchmark();
    void updateBenchmarkDisplay();
    void onNewConnection(); // Handle new instance connections
    // REMOVED: applyCustomColor removed - custom color functionality deleted
    // REMOVED: updateThickness removed - thickness control functionality deleted
    // REMOVED: adjustThicknessForZoom removed - zoom thickness adjustment deleted
    // REMOVED: changeTool removed - tool changing functionality deleted
    void deleteCurrentPage();

    // REMOVED MW7.3: loadPdf removed - old PDF loading function
    // REMOVED: exportCanvasOnlyNotebook removed - canvas export functionality deleted
    // REMOVED MW7.3: exportAnnotatedPdfFullRender removed - pdftk export function
    // REMOVED MW7.3: createAnnotatedPagesPdf removed - temp PDF creation function
    // REMOVED MW7.3: mergePdfWithPdftk removed - PDF merging function
    
    // PDF outline preservation helpers
    // REMOVED MW7.3: filterAndAdjustOutline removed - outline manipulation function
    // REMOVED MW7.3: applyOutlineToPdf removed - outline application function
    
    // Helper function to show page range dialog (returns false if cancelled)
    // REMOVED MW7.3: showPageRangeDialog removed - export dialog function

    // REMOVED: updateZoom and updatePanRange removed - zoom/pan update functionality deleted
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

    // REMOVED MW7.2: toggleDial removed - dial functionality deleted
    // REMOVED MW7.2: positionDialContainer removed - dial functionality deleted
    // void processPageSwitch();
    // REMOVED MW7.2: initializeDialSound removed - dial functionality deleted

    // void updateCustomColor();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    void connectViewportScrollSignals(DocumentViewport* viewport);  // Phase 3.3
    void centerViewportContent(int tabIndex);  // Phase 3.3: One-time horizontal centering
    void updateLayerPanelForViewport(DocumentViewport* viewport);  // Phase 5.1: Update LayerPanel
    
    // Phase doc-1: Document operations
    void saveDocument();          // doc-1.1: Save document to JSON file (Ctrl+S)
    void loadDocument();          // doc-1.2: Load document from JSON file (Ctrl+O)
    void addPageToDocument();     // doc-1.0: Add page at end of document (Ctrl+Shift+A)
    void insertPageInDocument();  // Phase 3: Insert page after current (Ctrl+Shift+I)
    void deletePageInDocument();  // Phase 3B: Delete current page (Ctrl+Shift+D)
    void openPdfDocument(const QString &filePath = QString());       // doc-1.4: Open PDF file (Ctrl+Shift+O)
    
    // void handleModeSelection(int angle);



    // void updateColorPalette(); // Update colors based on current palette mode
    // REMOVED: getPaletteColor removed - palette color functionality deleted
    qreal getDevicePixelRatio(); 

    bool isDarkMode();
    // REMOVED: loadThemedIcon, loadThemedIconReversed, and updateButtonIcon removed - themed icon functionality deleted
    // REMOVED: createButtonStyle removed - button styling no longer needed in MainWindow


    
    // Color button state management
    // void updateColorButtonStates();
    // REMOVED: selectColorButton, updateStraightLineButtonState, and updateRopeToolButtonState removed - button state functionality deleted
    
    
    

public slots:
    // void updatePictureButtonState(); // Public slot for picture button state

private:
    void setPenTool();               // Set pen tool
    void setMarkerTool();            // Set marker tool
    void setEraserTool();            // Set eraser tool
    // Features migrated to DocumentViewport

    QColor getContrastingTextColor(const QColor &backgroundColor);
    void updateCustomColorButtonStyle(const QColor &color);
    
    void returnToLauncher(); // Return to launcher window
    
    // REMOVED: showPendingTooltip removed - tooltip functionality deleted
    
    
    // PDF Outline functionality
    // REMOVED MW7.5: Outline sidebar functions removed - outline sidebar deleted
    // REMOVED MW7.3: addOutlineItem removed - recursive outline builder function
    // REMOVED MW7.5: updateOutlineSelection removed - outline sidebar deleted
    
    // REMOVED MW7.4: Bookmark sidebar functionality removed - bookmark implementation deleted
    
    // Markdown notes sidebar functionality
    void toggleMarkdownNotesSidebar();  // Toggle markdown notes sidebar

private:
    // =========================================================================
    // bool m_useNewViewport = false;
    // =========================================================================
    
    // InkCanvas *canvas;  // using DocumentViewport via TabManager
    // QPushButton *benchmarkButton;
    QLabel *benchmarkLabel;
    QTimer *benchmarkTimer;
    bool benchmarking;



    // QPushButton *redButton;
    // QPushButton *blueButton;
    // QPushButton *yellowButton;
    // QPushButton *greenButton;
    // QPushButton *blackButton;
    // QPushButton *whiteButton;
    QLineEdit *customColorInput;
    QPushButton *customColorButton;
    // QPushButton *thicknessButton; // Added thickness button
    // QSlider *thicknessSlider; // Added thickness slider
    // QFrame *thicknessFrame; // Added thickness frame
    QComboBox *toolSelector;
    // QPushButton *penToolButton;    // Individual pen tool button
    // QPushButton *markerToolButton; // Individual marker tool button
    // QPushButton *eraserToolButton; // Individual eraser tool button
    // Features migrated to DocumentViewport - pen/eraser now use vector layers directly
    QPushButton *deletePageButton;
    // QPushButton *saveButton; // Button to save file
    // QPushButton *fullscreenButton;
    // QPushButton *openControlPanelButton;
    // Phase C.1.5: openRecentNotebooksButton removed - functionality now in NavigationBar

    // QPushButton *loadPdfButton;
    // QPushButton *clearPdfButton;
    // QPushButton *exportPdfButton; // Button to export annotated PDF
    // QPushButton *pdfTextSelectButton; // Button to toggle PDF text selection mode
    // QPushButton *toggleTabBarButton;
    
    // Overflow menu for infrequently used actions (shown via NavigationBar menu button)
    QMenu *overflowMenu;

    // Page tracking now done by DocumentViewport internally
    

    // QPushButton *straightLineToggleButton; // Button to toggle straight line mode
    // QPushButton *ropeToolButton; // Button to toggle rope tool mode
    // QPushButton *insertPictureButton; // Button to insert pictures

    // QPushButton *zoomButton;
    // QFrame *zoomFrame;
    // QPushButton *dezoomButton;
    // QPushButton *zoom50Button;
    // QPushButton *zoom200Button;
    QWidget *zoomContainer;
    QLineEdit *zoomInput;
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

    // Dial Mode Toolbar (vertical, right side)
    // MW2.2: Removed all dial-related variables

    // Removed unused colorPreview widget

    // REMOVED MW7.2: dialDisplay removed - dial functionality deleted

    // REMOVED MW7.2: dialColorPreview removed - dial functionality deleted
    // REMOVED MW7.2: dialIconView removed - dial functionality deleted
    QFont pixelFont; // ✅ Font for pixel effect
    // QLabel *modeIndicator; ✅ Indicator for mode selection
    // QLabel *dialNeedle;




    // Color palette mode (independent of UI theme)
    bool useBrighterPalette = false; // false = darker colors, true = brighter colors

    qreal initialDpr = 1.0; // Will be set in constructor

    QWidget *sidebarContainer;  // Container for sidebar

    
    // ✅ Mouse dial controls
    
    // ✅ Override mouse events for dial control
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    void cycleZoomLevels();         // Function to cycle through 0.5x, 1x, 2x zoom levels
    bool sidebarWasVisibleBeforeFullscreen = true;  // Track sidebar state before fullscreen


    // Add in MainWindow class:
    // REMOVED MW7.6: buttonPressMapping and buttonPressActionMapping removed - old mapping system deleted

    // New: Keyboard mapping support
    // REMOVED MW7.6: keyboardMappings and keyboardActionMapping removed - old mapping system deleted

    // Tooltip handling for pen input
    QTimer *tooltipTimer;
    QWidget *lastHoveredWidget;
    QPoint pendingTooltipPos;
    
    // Concurrent save handling for autoscroll synchronization
    QFuture<void> concurrentSaveFuture;

    // REMOVED MW7.6: handleButtonHeld and handleButtonReleased removed - old mapping system deleted

    // REMOVED MW7.6: handleControllerButton removed - old mapping system deleted
    
    // New: Keyboard mapping methods
    // REMOVED MW7.6: handleKeyboardShortcut removed - old mapping system deleted
    // REMOVED MW7.6: saveKeyboardMappings and loadKeyboardMappings removed - old mapping system deleted

    // bool ensureTabHasUniqueSaveFolder(InkCanvas* canvas);  // MW1.4: Stub - returns true

    RecentNotebooksManager *recentNotebooksManager; // Added manager instance

    int pdfRenderDPI = 192;  // Default to 288 DPI
    
    // Single instance support
    QLocalServer *localServer;
    

    void setupUi();

    void loadUserSettings();

    bool scrollbarsVisible = false;
    QTimer *scrollbarHideTimer = nullptr;
    
    // Phase 3.3: Viewport scroll signal connections (for proper cleanup)
    QMetaObject::Connection m_hScrollConn;
    QMetaObject::Connection m_vScrollConn;
    QPointer<DocumentViewport> m_connectedViewport;  // QPointer for safe dangling check
    
    // CR-2B: Tool/mode signal connections (for keyboard shortcut sync)
    QMetaObject::Connection m_toolChangedConn;
    QMetaObject::Connection m_straightLineModeConn;
    
    // Phase 5.1: LayerPanel page change connection
    QMetaObject::Connection m_layerPanelPageConn;
    
    // Trackpad vs mouse wheel routing (see eventFilter wheel handling)
    bool trackpadModeActive = false;
    QTimer *trackpadModeTimer = nullptr;
    QElapsedTimer lastWheelEventTimer;
    
#ifdef Q_OS_LINUX
    // Palm rejection internal state
    bool palmRejectionActive = false; // Whether we're currently suppressing touch gestures
    TouchGestureMode palmRejectionOriginalMode = TouchGestureMode::Full; // Original mode before suppression
    QTimer *palmRejectionTimer = nullptr; // Timer for delayed restore
    
    // REMOVED: onStylusProximityEnter, onStylusProximityLeave, and restoreTouchGestureMode removed - stylus proximity functionality deleted
#endif
    
    // Stylus button state tracking (hold-to-enable)
    bool stylusButtonAActive = false;
    bool stylusButtonBActive = false;
    ToolType previousToolBeforeStylusA = ToolType::Pen;
    ToolType previousToolBeforeStylusB = ToolType::Pen;
    bool previousStraightLineModeA = false;
    bool previousStraightLineModeB = false;
    bool previousRopeToolModeA = false;
    bool previousRopeToolModeB = false;
    bool previousTextSelectionModeA = false;
    bool previousTextSelectionModeB = false;
    
    // Text selection delayed disable (for stylus button hold)
    bool textSelectionPendingDisable = false; // True when waiting for text selection interaction to complete
    bool textSelectionWasButtonA = false; // Track which button enabled text selection
    
    // REMOVED MW7.7: enableStylusButtonMode and disableStylusButtonMode stubs removed
    void handleStylusButtonPress(Qt::MouseButtons buttons);
    void handleStylusButtonRelease(Qt::MouseButtons buttons, Qt::MouseButton releasedButton);
    
    // Event filter for scrollbar hover detection and dial container drag
    bool eventFilter(QObject *obj, QEvent *event) override;
    
    // Update scrollbar positions based on container size
    void updateScrollbarPositions();
    
    
    // Responsive toolbar management - REMOVED MW4.3: All layout functions and variables removed
    
    // Helper function for tab text eliding
    QString elideTabText(const QString &text, int maxWidth);
    
    // Layout timers and separators - REMOVED MW4.3: No longer needed
    
protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;  // New: Handle keyboard shortcuts
    void keyReleaseEvent(QKeyEvent *event) override; // Track Ctrl key release for trackpad pinch-zoom detection
    // REMOVED: tabletEvent removed - tablet event handling deleted
#ifdef Q_OS_LINUX
    // REMOVED: event removed - event handling functionality deleted
#endif
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override; // Handle Windows theme changes
#endif
    void closeEvent(QCloseEvent *event) override; // ✅ Add auto-save on program close
    
    // IME support for multi-language input
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
};

#endif // MAINWINDOW_H
