#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "InkCanvas.h"
#include "MarkdownNotesSidebar.h"
#include <QPushButton>
#include <QLabel>
#include <QTimer>
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
#include "ControlPanelDialog.h"
#include "PictureWindowManager.h"
#include "SpnPackageManager.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>

// Forward declarations
class QTreeWidgetItem;
class QProgressDialog;
namespace Poppler { 
    class Document; 
    class OutlineItem;
}

// #include "HandwritingLineEdit.h"

enum DialMode {
    None,
    PageSwitching,
    ZoomControl,
    ThicknessControl,
    ToolSwitching,
    PresetSelection,
    PanAndPageScroll
};

enum class ControllerAction {
    None,
    ToggleFullscreen,
    ToggleDial,
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
    ToggleOutline,
    ToggleBookmarks,
    AddBookmark,
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

static QString actionToString(ControllerAction action) {
    switch (action) {
        case ControllerAction::ToggleFullscreen: return "Toggle Fullscreen";
        case ControllerAction::ToggleDial: return "Toggle Dial";
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
        case ControllerAction::ToggleOutline: return "Toggle PDF Outline";
        case ControllerAction::ToggleBookmarks: return "Toggle Bookmarks";
        case ControllerAction::AddBookmark: return "Add/Remove Bookmark";
        case ControllerAction::ToggleTouchGestures: return "Toggle Touch Gestures";
        case ControllerAction::PreviousPage: return "Previous Page";
        case ControllerAction::NextPage: return "Next Page";
        default: return "None";
    }
}

static ControllerAction stringToAction(const QString &str) {
    // Convert internal key to ControllerAction enum
    InternalControllerAction internalAction = ButtonMappingHelper::internalKeyToAction(str);
    
    switch (internalAction) {
        case InternalControllerAction::None: return ControllerAction::None;
        case InternalControllerAction::ToggleFullscreen: return ControllerAction::ToggleFullscreen;
        case InternalControllerAction::ToggleDial: return ControllerAction::ToggleDial;
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
        case InternalControllerAction::ToggleOutline: return ControllerAction::ToggleOutline;
        case InternalControllerAction::ToggleBookmarks: return ControllerAction::ToggleBookmarks;
        case InternalControllerAction::AddBookmark: return ControllerAction::AddBookmark;
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
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow();
    int getCurrentPageForCanvas(InkCanvas *canvas); 

    bool lowResPreviewEnabled = true;

    void setLowResPreviewEnabled(bool enabled);
    bool isLowResPreviewEnabled() const;

    bool areBenchmarkControlsVisible() const;
    void setBenchmarkControlsVisible(bool visible);

    bool zoomButtonsVisible = true;
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

    QString getHoldMapping(const QString &buttonName);
    QString getPressMapping(const QString &buttonName);

    void saveButtonMappings();
    void loadButtonMappings();

    void setHoldMapping(const QString &buttonName, const QString &dialMode);
    void setPressMapping(const QString &buttonName, const QString &action);
    DialMode dialModeFromString(const QString &mode);
    
    // ✅ Mouse dial mapping management
    void setMouseDialMapping(const QString &combination, const QString &dialMode);
    QString getMouseDialMapping(const QString &combination) const;
    QMap<QString, QString> getMouseDialMappings() const;
    void saveMouseDialMappings();
    void loadMouseDialMappings();


    void openPdfFile(const QString &pdfPath); // ✅ Open PDF file directly
    void openSpnPackage(const QString &spnPath);
    void createNewSpnPackage(const QString &spnPath); // ✅ Create new empty SPN package
    
    // Single instance functionality
    static bool isInstanceRunning();
    static bool sendToExistingInstance(const QString &filePath);
    void setupSingleInstanceServer();
    
    // Theme/palette management
    static void updateApplicationPalette(); // Update Qt application palette based on dark mode
    void openFileInNewTab(const QString &filePath); // ✅ Open .spn package directly
    bool showLastAccessedPageDialog(InkCanvas *canvas); // ✅ Show dialog for last accessed page

    int getPdfDPI() const { return pdfRenderDPI; }
    void setPdfDPI(int dpi);

    // void loadUserSettings();  // New
    void savePdfDPI(int dpi); // New

    // Background settings persistence
    void saveDefaultBackgroundSettings(BackgroundStyle style, QColor color, int density);
    void loadDefaultBackgroundSettings(BackgroundStyle &style, QColor &color, int &density);
    void applyDefaultBackgroundToCanvas(InkCanvas *canvas); // ✅ Helper to apply default background settings
    
    void saveThemeSettings();
    void loadThemeSettings();
    void updateTheme(); // Apply current theme settings
    void updateTabSizes(); // Update tab widths adaptively
    
    void migrateOldButtonMappings();
    QString migrateOldDialModeString(const QString &oldString);
    QString migrateOldActionString(const QString &oldString);

    InkCanvas* currentCanvas(); // Made public for RecentNotebooksDialog
    void saveCurrentPage(); // Made public for RecentNotebooksDialog
    void saveCurrentPageConcurrent(); // Concurrent version for smooth page flipping
    void switchPage(int pageNumber); // Made public for RecentNotebooksDialog
    void switchPageWithDirection(int pageNumber, int direction); // Enhanced page switching with direction tracking
    void updateTabLabel(); // Made public for RecentNotebooksDialog
    QSpinBox *pageInput; // Made public for RecentNotebooksDialog
    QPushButton *prevPageButton; // Previous page button
    QPushButton *nextPageButton; // Next page button
    
    // New: Keyboard mapping methods (made public for ControlPanelDialog)
    void addKeyboardMapping(const QString &keySequence, const QString &action);
    void removeKeyboardMapping(const QString &keySequence);
    QMap<QString, QString> getKeyboardMappings() const;
    
    // Controller access
    SDLControllerManager* getControllerManager() const { return controllerManager; }
    void reconnectControllerSignals(); // Reconnect controller signals after reconnection

    void updateDialButtonState();     // Update dial button state when switching tabs
    void updateFastForwardButtonState(); // Update fast forward button state when switching tabs
    void updateToolButtonStates();   // Update tool button states when switching tabs
    void handleColorButtonClick();    // Handle tool switching when color buttons are clicked
    void updateThicknessSliderForCurrentTool(); // Update thickness slider to reflect current tool's thickness
    void updatePdfTextSelectButtonState(); // Update PDF text selection button state when switching tabs
    void updateBookmarkButtonState(); // Update bookmark toggle button state
    bool selectFolder(); // Select save folder - moved to public for ControlPanelDialog access, returns true on success

    void addNewTab();

    // Find tab with given notebook ID, returns tab index or -1 if not found
    int findTabWithNotebookId(const QString &notebookId);
    
    // Check if notebook is already open, switch to it if so
    // Returns true if notebook was already open (and switched to), false otherwise
    bool switchToExistingNotebook(const QString &spnPath);

    // Shared launcher instance to prevent memory leaks (moved from private)
    static LauncherWindow *sharedLauncher;

    static QSharedMemory *sharedMemory;
    
    // Static cleanup method for signal handlers
    static void cleanupSharedResources();

private slots:
    void toggleBenchmark();
    void updateBenchmarkDisplay();
    void onNewConnection(); // Handle new instance connections
    void applyCustomColor(); // Added function for custom color input
    void updateThickness(int value); // New function for thickness control
    void adjustThicknessForZoom(int oldZoom, int newZoom); // Adjust thickness when zoom changes
    void changeTool(int index);
    void saveCanvas(); // Save canvas to file
    void deleteCurrentPage();

    void loadPdf();
    void clearPdf();
    void handleSmartPdfButton(); // ✅ Smart PDF button that handles all PDF operations
    void exportAnnotatedPdf(); // Export PDF with all annotations overlaid
    void exportCanvasOnlyNotebook(const QString &saveFolder, const QString &notebookId); // Export canvas-only notebook (no PDF)
    void exportAnnotatedPdfFullRender(const QString &exportPath, const QSet<int> &annotatedPages, bool exportWholeDocument = true, int exportStartPage = 0, int exportEndPage = -1); // Full render fallback
    bool createAnnotatedPagesPdf(const QString &outputPath, const QList<int> &pages, QProgressDialog &progress); // Create temp PDF
    bool mergePdfWithPdftk(const QString &originalPdf, const QString &annotatedPagesPdf, const QString &outputPdf, const QList<int> &annotatedPageNumbers, QString *errorMsg = nullptr, bool exportWholeDocument = true, int exportStartPage = 0, int exportEndPage = -1); // Merge using pdftk
    
    // PDF outline preservation helpers
    bool extractPdfOutlineData(const QString &pdfPath, QString &outlineData); // Extract PDF metadata including outline
    QString filterAndAdjustOutline(const QString &metadataContent, int startPage, int endPage, int pageOffset); // Filter and adjust outline for page range
    bool applyOutlineToPdf(const QString &pdfPath, const QString &outlineData); // Apply outline to PDF using pdftk
    
    // Helper function to show page range dialog (returns false if cancelled)
    bool showPageRangeDialog(int totalPages, bool &exportWholeDocument, int &startPage, int &endPage);

    void updateZoom();
    void onZoomSliderChanged(int value); // Handle manual zoom slider changes
    void applyZoom();
    void updatePanRange();
    void updatePanX(int value);
    void updatePanY(int value);

    void selectBackground(); // Added back

    void forceUIRefresh();

    void switchTab(int index);
    
    void removeTabAt(int index);
    void toggleZoomSlider();
    void toggleThicknessSlider(); // Added function to toggle thickness slider
    void toggleFullscreen();
    void showJumpToPageDialog();
    void goToPreviousPage(); // Go to previous page
    void goToNextPage();     // Go to next page
    void onPageInputChanged(int newPage); // Handle spinbox page changes with direction tracking

    void toggleDial();  // ✅ Show/Hide dial
    void positionDialContainer(); // ✅ Position dial container intelligently
    void handleDialInput(int angle);  // ✅ Handle touch input
    void onDialReleased();
    // void processPageSwitch();
    void initializeDialSound();

    void changeDialMode(DialMode mode);
    void handleDialZoom(int angle);
    void handleDialThickness(int angle); // Added function to handle thickness control
    void onZoomReleased();
    void onThicknessReleased(); // Added function to handle thickness control
    // void updateCustomColor();
    void updateDialDisplay();
    // void handleModeSelection(int angle);

    void handleToolSelection(int angle);
    void onToolReleased();


    void handlePresetSelection(int angle);
    void onPresetReleased();
    void addColorPreset();
    void updateColorPalette(); // Update colors based on current palette mode
    QColor getPaletteColor(const QString &colorName); // Get color based on current palette
    qreal getDevicePixelRatio(); 

    bool isDarkMode();
    QIcon loadThemedIcon(const QString& baseName);
    QIcon loadThemedIconReversed(const QString& baseName);
    void updateButtonIcon(QPushButton* button, const QString& iconName);
    QString createButtonStyle(bool darkMode);

    void handleDialPanScroll(int angle);  // Add missing function declaration
    void onPanScrollReleased();           // Add missing function declaration

    // Touch gesture handlers
    void handleTouchZoomChange(int newZoom);
    void handleTouchPanChange(int panX, int panY);
    void handleTouchGestureEnd(); // Add handler for touch gesture completion
    void handleTouchPanningChanged(bool active); // Handler for touch panning start/stop (window performance optimization)
    
    // Color button state management
    void updateColorButtonStates();
    void selectColorButton(QPushButton* selectedButton);
    void updateStraightLineButtonState();
    void updateRopeToolButtonState(); // New slot for rope tool button
    
    
    

public slots:
    void updatePictureButtonState(); // Public slot for picture button state
    void onAutoScrollRequested(int direction);
    void onEarlySaveRequested();

private:
    void setPenTool();               // Set pen tool
    void setMarkerTool();            // Set marker tool
    void setEraserTool();            // Set eraser tool

    QColor getContrastingTextColor(const QColor &backgroundColor);
    void updateCustomColorButtonStyle(const QColor &color);
    
    void returnToLauncher(); // Return to launcher window
    
    void showPendingTooltip(); // Show tooltip with throttling
    
    void showRopeSelectionMenu(const QPoint &position); // Show context menu for rope tool selection
    
    // PDF Outline functionality
    void toggleOutlineSidebar();     // Toggle PDF outline sidebar
    void onOutlineItemClicked(QTreeWidgetItem *item, int column); // Handle outline item clicks
    void loadPdfOutline();           // Load PDF outline/bookmarks
    void addOutlineItem(const Poppler::OutlineItem& outlineItem, QTreeWidgetItem* parentItem); // Add outline item recursively
    Poppler::Document* getPdfDocument(); // Get PDF document from current canvas
    void updateOutlineSelection(int pageNumber); // Update outline selection based on current page
    
    // Bookmark sidebar functionality
    void toggleBookmarksSidebar();   // Toggle bookmarks sidebar
    void onBookmarkItemClicked(QTreeWidgetItem *item, int column); // Handle bookmark item clicks
    void loadBookmarks();            // Load bookmarks from file
    void saveBookmarks();            // Save bookmarks to file
    void toggleCurrentPageBookmark(); // Add/remove current page from bookmarks
    
    // Markdown notes sidebar functionality
    void toggleMarkdownNotesSidebar();  // Toggle markdown notes sidebar
    void onMarkdownNotesUpdated();      // Handle markdown notes updates
    void onMarkdownNoteContentChanged(const QString &noteId, const MarkdownNoteData &data); // Handle note content changes
    void onMarkdownNoteDeleted(const QString &noteId); // Handle note deletion
    void onHighlightLinkClicked(const QString &highlightId); // Handle highlight link clicks
    void onHighlightDoubleClicked(const QString &highlightId); // Handle highlight double-clicks
    void loadMarkdownNotesForCurrentPage(); // Load notes for currently visible page(s)

private:
    InkCanvas *canvas;
    QPushButton *benchmarkButton;
    QLabel *benchmarkLabel;
    QTimer *benchmarkTimer;
    bool benchmarking;



    QPushButton *redButton;
    QPushButton *blueButton;
    QPushButton *yellowButton;
    QPushButton *greenButton;
    QPushButton *blackButton;
    QPushButton *whiteButton;
    QLineEdit *customColorInput;
    QPushButton *customColorButton;
    QPushButton *thicknessButton; // Added thickness button
    QSlider *thicknessSlider; // Added thickness slider
    QFrame *thicknessFrame; // Added thickness frame
    QComboBox *toolSelector;
    QPushButton *penToolButton;    // Individual pen tool button
    QPushButton *markerToolButton; // Individual marker tool button
    QPushButton *eraserToolButton; // Individual eraser tool button
    QPushButton *deletePageButton;
    QPushButton *selectFolderButton; // Button to select folder
    QPushButton *saveButton; // Button to save file
    QPushButton *fullscreenButton;
    QPushButton *openControlPanelButton;
    QPushButton *openRecentNotebooksButton; // Added button

    QPushButton *loadPdfButton;
    QPushButton *clearPdfButton;
    QPushButton *exportPdfButton; // Button to export annotated PDF
    QPushButton *pdfTextSelectButton; // Button to toggle PDF text selection mode
    QPushButton *toggleTabBarButton;
    
    // Overflow menu for infrequently used actions
    QPushButton *overflowMenuButton;
    QMenu *overflowMenu;

    QMap<InkCanvas*, int> pageMap;
    

    QPushButton *backgroundButton; // New button to set background
    QPushButton *straightLineToggleButton; // Button to toggle straight line mode
    QPushButton *ropeToolButton; // Button to toggle rope tool mode
    QPushButton *insertPictureButton; // Button to insert pictures

    QSlider *zoomSlider;
    QPushButton *zoomButton;
    QFrame *zoomFrame;
    QPushButton *dezoomButton;
    QPushButton *zoom50Button;
    QPushButton *zoom200Button;
    QWidget *zoomContainer;
    QLineEdit *zoomInput;
    QScrollBar *panXSlider;
    QScrollBar *panYSlider;


    QListWidget *tabList;          // Horizontal tab bar
    QStackedWidget *canvasStack;   // Holds multiple InkCanvas instances
    QPushButton *addTabButton;     // Button to add tabs
    QWidget *tabBarContainer;      // Container for horizontal tab bar
    
    // PDF Outline Sidebar
    QWidget *outlineSidebar;       // Container for PDF outline
    QTreeWidget *outlineTree;      // Tree widget for PDF bookmarks/outline
    QPushButton *toggleOutlineButton; // Floating tab button to toggle outline sidebar
    bool outlineSidebarVisible = false;
    
    // Bookmarks Sidebar
    QWidget *bookmarksSidebar;     // Container for bookmarks
    QTreeWidget *bookmarksTree;    // Tree widget for bookmarks
    QPushButton *toggleBookmarksButton; // Floating tab button to toggle bookmarks sidebar
    QPushButton *toggleBookmarkButton; // Button to add/remove current page bookmark
    QPushButton *touchGesturesButton; // Touch gestures toggle button
    bool bookmarksSidebarVisible = false;
    
    void positionLeftSidebarTabs();  // Position the floating tabs for left sidebars
    QMap<int, QString> bookmarks;  // Map of page number to bookmark title
    QPushButton *jumpToPageButton; // Button to jump to a specific page
    
    // Markdown Notes Sidebar
    MarkdownNotesSidebar *markdownNotesSidebar;  // Sidebar for markdown notes
    QPushButton *toggleMarkdownNotesButton; // Button to toggle markdown notes sidebar
    bool markdownNotesSidebarVisible = false;

    // Dial Mode Toolbar (vertical, right side)
    QWidget *dialToolbar = nullptr;           // Foldable vertical toolbar for dial mode buttons
    QPushButton *dialToolbarToggle = nullptr; // Floating tab button to fold/unfold the dial toolbar
    bool dialToolbarExpanded = true;          // Track expanded/collapsed state
    void positionDialToolbarTab();            // Position the floating tab at the edge of the toolbar

    QWidget *dialContainer = nullptr;  // ✅ Floating dial container
    QDial *pageDial = nullptr;  // ✅ The dial itself
    QDial *modeDial = nullptr;  // ✅ Mode dial
    QPushButton *dialToggleButton;  // ✅ Toggle button
    bool fastForwardMode = false;  // ✅ Toggle fast forward
    QPushButton *fastForwardButton;  // ✅ Fast forward button    
    int lastAngle = 0;
    int startAngle = 0;
    bool tracking = false;
    int accumulatedRotation = 0;
    SimpleAudio *dialClickSound = nullptr;

    int grossTotalClicks = 0;

    DialMode temporaryDialMode = None;
    
    // ✅ Mouse dial control system
    QTimer *mouseDialTimer;
    QSet<Qt::MouseButton> pressedMouseButtons;
    bool mouseDialModeActive = false;
    QString currentMouseDialCombination;
    QMap<QString, QString> mouseDialMappings; // combination -> dial mode

    QComboBox *dialModeSelector; // ✅ Mode selector

    DialMode currentDialMode = PanAndPageScroll; // ✅ Default mode

    // Removed unused colorPreview widget

    QLabel *dialDisplay = nullptr; // ✅ Display for dial mode

    QFrame *dialColorPreview;
    QLabel *dialIconView;
    QFont pixelFont; // ✅ Font for pixel effect
    // QLabel *modeIndicator; ✅ Indicator for mode selection
    // QLabel *dialNeedle;

    QPushButton *btnPageSwitch;
    QPushButton *btnZoom;
    QPushButton *btnThickness;

    QPushButton *btnTool;
    QPushButton *btnPresets;
    QPushButton *btnPannScroll;
    int tempClicks = 0;

    QPushButton *dialHiddenButton; // ✅ Invisible tap button over OLED display

    QQueue<QColor> colorPresets; // ✅ FIFO queue for color presets
    QPushButton *addPresetButton; // ✅ Button to add current color to queue
    int currentPresetIndex = 0; // ✅ Track selected preset

    // Color palette mode (independent of UI theme)
    bool useBrighterPalette = false; // false = darker colors, true = brighter colors

    qreal initialDpr = 1.0; // Will be set in constructor

    QWidget *sidebarContainer;  // Container for sidebar
    QWidget *controlBar;        // Control toolbar

    void setTemporaryDialMode(DialMode mode);
    void clearTemporaryDialMode();
    
    // ✅ Mouse dial controls
    void startMouseDialMode(const QString &combination);
    void stopMouseDialMode();
    void handleMouseWheelDial(int delta);
    QString mouseButtonCombinationToString(const QSet<Qt::MouseButton> &buttons) const;
    
    // ✅ Override mouse events for dial control
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    bool controlBarVisible = true;  // Track controlBar visibility state
    void toggleControlBar();        // Function to toggle controlBar visibility
    void cycleZoomLevels();         // Function to cycle through 0.5x, 1x, 2x zoom levels
    bool sidebarWasVisibleBeforeFullscreen = true;  // Track sidebar state before fullscreen

    int accumulatedRotationAfterLimit = 0; 

    int pendingPageFlip = 0;  // -1 for previous, +1 for next, 0 for no flip. This is used for mode PanAndPageScroll

    // Add in MainWindow class:
    QMap<QString, QString> buttonHoldMapping;
    QMap<QString, QString> buttonPressMapping;
    QMap<QString, ControllerAction> buttonPressActionMapping;

    // New: Keyboard mapping support
    QMap<QString, QString> keyboardMappings;  // keySequence -> action internal key
    QMap<QString, ControllerAction> keyboardActionMapping;  // keySequence -> action enum

    // Tooltip handling for pen input
    QTimer *tooltipTimer;
    QWidget *lastHoveredWidget;
    QPoint pendingTooltipPos;
    
    // Concurrent save handling for autoscroll synchronization
    QFuture<void> concurrentSaveFuture;

    void handleButtonHeld(const QString &buttonName);
    void handleButtonReleased(const QString &buttonName);

    void handleControllerButton(const QString &buttonName);
    
    // New: Keyboard mapping methods
    void handleKeyboardShortcut(const QString &keySequence);
    void saveKeyboardMappings();
    void loadKeyboardMappings();

    bool ensureTabHasUniqueSaveFolder(InkCanvas* canvas); // Returns true if tab can be closed

    RecentNotebooksManager *recentNotebooksManager; // Added manager instance

    int pdfRenderDPI = 192;  // Default to 288 DPI
    
    // Single instance support
    QLocalServer *localServer;
    

    void setupUi();

    void loadUserSettings();

    bool scrollbarsVisible = false;
    QTimer *scrollbarHideTimer = nullptr;
    
#ifdef Q_OS_LINUX
    // Palm rejection internal state
    bool palmRejectionActive = false; // Whether we're currently suppressing touch gestures
    TouchGestureMode palmRejectionOriginalMode = TouchGestureMode::Full; // Original mode before suppression
    QTimer *palmRejectionTimer = nullptr; // Timer for delayed restore
    
    void onStylusProximityEnter(); // Called when stylus enters proximity or touches
    void onStylusProximityLeave(); // Called when stylus leaves proximity or releases
    void restoreTouchGestureMode(); // Called by timer to restore original mode
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
    
    void enableStylusButtonMode(Qt::MouseButton button);
    void disableStylusButtonMode(Qt::MouseButton button);
    void handleStylusButtonPress(Qt::MouseButtons buttons);
    void handleStylusButtonRelease(Qt::MouseButtons buttons, Qt::MouseButton releasedButton);
    void onPdfTextSelectionCleared(); // Called when text selection is cleared, auto-disables if pending
    
    // Event filter for scrollbar hover detection and dial container drag
    bool eventFilter(QObject *obj, QEvent *event) override;
    
    // Update scrollbar positions based on container size
    void updateScrollbarPositions();
    
    // Handle edge proximity detection for scrollbar visibility
    void handleEdgeProximity(InkCanvas* canvas, const QPoint& pos);
    
    // Responsive toolbar management
    bool isToolbarTwoRows = false;
    QVBoxLayout *controlLayoutVertical = nullptr;
    QHBoxLayout *controlLayoutSingle = nullptr;
    QHBoxLayout *controlLayoutFirstRow = nullptr;
    QHBoxLayout *controlLayoutSecondRow = nullptr;
    void updateToolbarLayout();
    void createSingleRowLayout(bool centered = true);
    void createTwoRowLayout();
    
    // Helper function for tab text eliding
    QString elideTabText(const QString &text, int maxWidth);
    
    // Add timer for delayed layout updates
    QTimer *layoutUpdateTimer = nullptr;
    
    // Separator line for 2-row layout
    QFrame *separatorLine = nullptr;
    
protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;  // New: Handle keyboard shortcuts
    void tabletEvent(QTabletEvent *event) override; // Handle pen hover for tooltips
#ifdef Q_OS_LINUX
    bool event(QEvent *event) override; // Handle tablet proximity events for palm rejection
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
