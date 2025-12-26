#include "MainWindow.h"
// #include "InkCanvas.h"  // Phase 3.1.7: Disconnected - using DocumentViewport
// #include "VectorCanvas.h"  // REMOVED Phase 3.1.3 - Features migrated to DocumentViewport
#include "core/DocumentViewport.h"  // Phase 3.1: New viewport architecture
#include "core/Document.h"          // Phase 3.1: Document class
#include "ButtonMappingTypes.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif 
#include <QGuiApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include "core/ToolType.h" // Include the header file where ToolType is defined
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QSpinBox>
#include <QTextStream>
#include <QInputDialog>
#include <QDial>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QToolTip> // For manual tooltip display
#include <QWindow> // For tablet event safety checks
#include <QtConcurrent/QtConcurrentRun> // For concurrent saving
#include <QFuture>
#include <QFutureWatcher>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QSet>
#include <QWheelEvent>
#include <QTimer>
#include <QShortcut>  // Phase doc-1: Application-wide keyboard shortcuts
#include <QColorDialog>  // Phase 3.1.8: For custom color picker
#include <QPdfWriter>
#include <QProgressDialog>
#include <QProcess>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>  // Phase doc-1: JSON serialization
#include <QThread>
#include <QPointer>
// #include "HandwritingLineEdit.h"
// #include "ControlPanelDialog.h"  // Phase 3.1.8: Disabled - depends on InkCanvas
#include "SDLControllerManager.h"
// #include "LauncherWindow.h" // Phase 3.1: Disconnected - LauncherWindow will be re-linked later
#include "PdfOpenDialog.h" // Added for PDF file association
#include "DocumentConverter.h" // Added for PowerPoint conversion
#include <poppler-qt6.h> // For PDF outline parsing
#include <memory> // For std::shared_ptr

// Linux-specific includes for signal handling
#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <QProcess>
#endif

// Static member definition for single instance
QSharedMemory *MainWindow::sharedMemory = nullptr;
// Phase 3.1: LauncherWindow disconnected - will be re-linked later
// LauncherWindow *MainWindow::sharedLauncher = nullptr;

// REMOVED Phase 3.1: Static flag for viewport architecture mode
// Always using new architecture now
// bool MainWindow::s_useNewViewport = false;

#ifdef Q_OS_LINUX
// Linux-specific signal handler for cleanup
void linuxSignalHandler(int signal) {
    Q_UNUSED(signal);
    
    // Only do minimal cleanup in signal handler to avoid Qt conflicts
    // The main cleanup will happen in the destructor
    if (MainWindow::sharedMemory && MainWindow::sharedMemory->isAttached()) {
        MainWindow::sharedMemory->detach();
    }
    
    // Remove local server
    QLocalServer::removeServer("SpeedyNote_SingleInstance");
    
    // Exit immediately - don't call QApplication::quit() from signal handler
    // as it can interfere with Qt's event system
    _exit(0);
}

// Function to setup Linux signal handlers
void setupLinuxSignalHandlers() {
    // Only handle SIGTERM and SIGINT, avoid SIGHUP as it can interfere with Qt
    signal(SIGTERM, linuxSignalHandler);
    signal(SIGINT, linuxSignalHandler);
}
#endif

MainWindow::MainWindow(QWidget *parent) 
    : QMainWindow(parent), benchmarking(false), localServer(nullptr) {

    setWindowTitle(tr("SpeedyNote Beta 0.12.2"));
    
    // Phase 3.1: Always using new DocumentViewport architecture
    qDebug() << "MainWindow: Using DocumentViewport architecture (Phase 3.1+)";

#ifdef Q_OS_LINUX
    // Setup signal handlers for proper cleanup on Linux
    setupLinuxSignalHandlers();
#endif

    // Enable IME support for multi-language input
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);
    
    // Initialize DPR early
    initialDpr = getDevicePixelRatio();

    // Initialize tooltip timer for pen hover throttling
    tooltipTimer = new QTimer(this);
    tooltipTimer->setSingleShot(true);
    tooltipTimer->setInterval(100); // 100ms delay
    lastHoveredWidget = nullptr;
    connect(tooltipTimer, &QTimer::timeout, this, &MainWindow::showPendingTooltip);

    // QString iconPath = QCoreApplication::applicationDirPath() + "/icon.ico";
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    

    // ✅ Get screen size & adjust window size
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize logicalSize = screen->availableGeometry().size() * 0.89;
        resize(logicalSize);
    }
    // Phase 3.1.1: Create QTabWidget to hold DocumentViewports
    // Replaces old canvasStack (QStackedWidget)
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    // Note: m_tabWidget will be positioned in setupUi(), not set as central widget directly
    
    // Phase 3.1.1: Initialize DocumentManager and TabManager
    m_documentManager = new DocumentManager(this);
    m_tabManager = new TabManager(m_tabWidget, this);
    
    // Connect TabManager signals
    connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
        // Phase 3.3: Connect scroll signals from current viewport
        connectViewportScrollSignals(vp);
        updateDialDisplay();
    });
    QSettings settings("SpeedyNote", "App");
    pdfRenderDPI = settings.value("pdfRenderDPI", 192).toInt();
    setPdfDPI(pdfRenderDPI);
    
    setupUi();    // ✅ Move all UI setup here

    controllerManager = new SDLControllerManager();
    controllerThread = new QThread(this);

    controllerManager->moveToThread(controllerThread);
    
    // ✅ Initialize mouse dial control system
    mouseDialTimer = new QTimer(this);
    mouseDialTimer->setSingleShot(true);
    mouseDialTimer->setInterval(500); // 0.5 seconds
    connect(mouseDialTimer, &QTimer::timeout, this, [this]() {
        if (!pressedMouseButtons.isEmpty()) {
            startMouseDialMode(mouseButtonCombinationToString(pressedMouseButtons));
        }
    });
    connect(controllerThread, &QThread::started, controllerManager, &SDLControllerManager::start);
    connect(controllerThread, &QThread::finished, controllerManager, &SDLControllerManager::deleteLater);

    controllerThread->start();

    
    updateZoom(); // ✅ Keep this for initial zoom adjustment
    updatePanRange(); // Set initial slider range  HERE IS THE PROBLEM!!
    // toggleFullscreen(); // ✅ Toggle fullscreen to adjust layout
    // toggleDial(); // ✅ Toggle dial to adjust layout
   
    // zoomSlider->setValue(100 / initialDpr); // Set initial zoom level based on DPR
    // setColorButtonsVisible(false); // ✅ Show color buttons by default
    
    loadUserSettings();

    setBenchmarkControlsVisible(false);
    
    recentNotebooksManager = RecentNotebooksManager::getInstance(this); // Use singleton instance

    // Show dial by default after UI is fully initialized
    QTimer::singleShot(200, this, [this]() {
        if (!dialContainer) {
            toggleDial(); // This will create and show the dial
        }
    });
    
    // Force IME activation after a short delay to ensure proper initialization
    QTimer::singleShot(500, this, [this]() {
        QInputMethod *inputMethod = QGuiApplication::inputMethod();
        if (inputMethod) {
            inputMethod->show();
            inputMethod->reset();
        }
    });

    // Disable tablet tracking for now to prevent crashes
    // TODO: Find a safer way to implement hover tooltips without tablet tracking
    // QTimer::singleShot(100, this, [this]() {
    //     try {
    //         if (windowHandle() && windowHandle()->screen()) {
    //             setAttribute(Qt::WA_TabletTracking, true);
    //         }
    //     } catch (...) {
    //         // Silently ignore tablet tracking errors
    //     }
    // });
}


void MainWindow::setupUi() {
    
    // Ensure IME is properly enabled for the application
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    if (inputMethod) {
        inputMethod->show();
        inputMethod->reset();
    }
    
    // Create theme-aware button style
    bool darkMode = isDarkMode();
    QString buttonStyle = createButtonStyle(darkMode);


    loadPdfButton = new QPushButton(this);
    clearPdfButton = new QPushButton(this);
    loadPdfButton->setFixedSize(26, 30);
    clearPdfButton->setFixedSize(26, 30);
    QIcon pdfIcon(loadThemedIcon("pdf"));  // Path to your icon in resources
    QIcon pdfDeleteIcon(loadThemedIcon("pdfdelete"));  // Path to your icon in resources
    loadPdfButton->setIcon(pdfIcon);
    clearPdfButton->setIcon(pdfDeleteIcon);
    loadPdfButton->setStyleSheet(buttonStyle);
    clearPdfButton->setStyleSheet(buttonStyle);
    loadPdfButton->setToolTip(tr("Import/Clear Document"));
    clearPdfButton->setToolTip(tr("Clear PDF"));
    clearPdfButton->setVisible(false); // ✅ Hide clearPdfButton to save space
    connect(loadPdfButton, &QPushButton::clicked, this, &MainWindow::handleSmartPdfButton);
    connect(clearPdfButton, &QPushButton::clicked, this, &MainWindow::clearPdf);

    pdfTextSelectButton = new QPushButton(this);
    pdfTextSelectButton->setFixedSize(36, 36);
    pdfTextSelectButton->setStyleSheet(buttonStyle);
    pdfTextSelectButton->setToolTip(tr("Toggle PDF Text Selection"));
    pdfTextSelectButton->setProperty("selected", false); // Initially disabled
    updateButtonIcon(pdfTextSelectButton, "ibeam");
    connect(pdfTextSelectButton, &QPushButton::clicked, this, [this]() {
        // Phase 3.1.4: PDF text selection stubbed - will be reimplemented for DocumentViewport
        // TODO Phase 3.4: Implement PDF text selection in DocumentViewport
        qDebug() << "PDF text select: Not implemented yet (Phase 3.4)";
    });



    benchmarkButton = new QPushButton(this);
    // QIcon benchmarkIcon(loadThemedIcon("benchmark"));  // Path to your icon in resources
    // benchmarkButton->setIcon(benchmarkIcon);
    benchmarkButton->setFixedSize(26, 30); // Make the benchmark button smaller
    benchmarkButton->setStyleSheet(buttonStyle);
    benchmarkButton->setToolTip(tr("Toggle Benchmark"));
    benchmarkLabel = new QLabel("PR:N/A", this);
    benchmarkLabel->setFixedHeight(30);  // Make the benchmark bar smaller
    updateButtonIcon(benchmarkButton, "benchmark");

    toggleTabBarButton = new QPushButton(this);
    toggleTabBarButton->setToolTip(tr("Show/Hide Tab Bar"));
    toggleTabBarButton->setFixedSize(36, 36);
    toggleTabBarButton->setStyleSheet(buttonStyle);
    toggleTabBarButton->setProperty("selected", true); // Initially visible
    
    // PDF Outline Toggle Button
    // PDF Outline Toggle - Floating tab on left side (created here, positioned later)
    toggleOutlineButton = new QPushButton(this);
    toggleOutlineButton->setObjectName("outlineSidebarTab");
    toggleOutlineButton->setToolTip(tr("Show/Hide PDF Outline"));
    toggleOutlineButton->setFixedSize(28, 80);
    toggleOutlineButton->setCursor(Qt::PointingHandCursor);
    toggleOutlineButton->setProperty("selected", false);
    toggleOutlineButton->setIcon(loadThemedIcon("outline"));
    toggleOutlineButton->setIconSize(QSize(18, 18));
    toggleOutlineButton->raise();
    
    // Bookmarks Toggle - Floating tab on left side (below outline tab)
    toggleBookmarksButton = new QPushButton(this);
    toggleBookmarksButton->setObjectName("bookmarksSidebarTab");
    toggleBookmarksButton->setToolTip(tr("Show/Hide Bookmarks"));
    toggleBookmarksButton->setFixedSize(28, 80);
    toggleBookmarksButton->setCursor(Qt::PointingHandCursor);
    toggleBookmarksButton->setProperty("selected", false);
    toggleBookmarksButton->setIcon(loadThemedIcon("bookmark"));
    toggleBookmarksButton->setIconSize(QSize(18, 18));
    toggleBookmarksButton->raise();
    
    // Apply floating tab styling for left sidebar tabs (using isDarkMode() to get current theme)
    {
        bool isDark = isDarkMode();
        QString tabBg = isDark ? "#3A3A3A" : "#EAEAEA";
        QString tabHover = isDark ? "#4A4A4A" : "#DADADA";
        QString tabBorder = isDark ? "#555555" : "#CCCCCC";
        
        QString outlineStyle = QString(
            "QPushButton#outlineSidebarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-left: none;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-right-radius: 0px;"
            "}"
            "QPushButton#outlineSidebarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#outlineSidebarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBg, tabBorder, tabHover);
        toggleOutlineButton->setStyleSheet(outlineStyle);
        
        QString bookmarksStyle = QString(
            "QPushButton#bookmarksSidebarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-left: none;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-right-radius: 0px;"
            "}"
            "QPushButton#bookmarksSidebarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#bookmarksSidebarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBg, tabBorder, tabHover);
        toggleBookmarksButton->setStyleSheet(bookmarksStyle);
    }
    
    // Add/Remove Bookmark Toggle Button
    toggleBookmarkButton = new QPushButton(this);
    toggleBookmarkButton->setToolTip(tr("Add/Remove Bookmark"));
    toggleBookmarkButton->setFixedSize(36, 36);
    toggleBookmarkButton->setStyleSheet(buttonStyle);
    toggleBookmarkButton->setProperty("selected", false); // For toggle state styling
    updateButtonIcon(toggleBookmarkButton, "star");
    
    // Markdown Notes Toggle Button
    toggleMarkdownNotesButton = new QPushButton(this);
    toggleMarkdownNotesButton->setToolTip(tr("Show/Hide Markdown Notes"));
    toggleMarkdownNotesButton->setFixedSize(36, 36);
    toggleMarkdownNotesButton->setStyleSheet(buttonStyle);
    toggleMarkdownNotesButton->setProperty("selected", false); // Initially hidden
    // Try "note" icon, fallback to text if icon doesn't exist
    updateButtonIcon(toggleMarkdownNotesButton, "markdown");


    // Touch Gestures Toggle Button
    touchGesturesButton = new QPushButton(this);
    touchGesturesButton->setToolTip(tr("Cycle Touch Gestures (Off/Y-Only/Full)"));
    touchGesturesButton->setFixedSize(36, 36);
    touchGesturesButton->setStyleSheet(buttonStyle);
    touchGesturesButton->setProperty("selected", touchGestureMode != TouchGestureMode::Disabled); // For toggle state styling
    touchGesturesButton->setProperty("yAxisOnly", touchGestureMode == TouchGestureMode::YAxisOnly); // For Y-only styling
    updateButtonIcon(touchGesturesButton, "hand");

    selectFolderButton = new QPushButton(this);
    selectFolderButton->setFixedSize(0, 0);
    QIcon folderIcon(loadThemedIcon("folder"));  // Path to your icon in resources
    selectFolderButton->setIcon(folderIcon);
    selectFolderButton->setStyleSheet(buttonStyle);
    selectFolderButton->setToolTip(tr("Select Save Folder"));
    selectFolderButton->setVisible(false); // ✅ Hide deprecated folder selection button
    connect(selectFolderButton, &QPushButton::clicked, this, &MainWindow::selectFolder);
    
    
    saveButton = new QPushButton(this);
    saveButton->setFixedSize(36, 36);
    QIcon saveIcon(loadThemedIcon("save"));  // Path to your icon in resources
    saveButton->setIcon(saveIcon);
    saveButton->setStyleSheet(buttonStyle);
    saveButton->setToolTip(tr("Save Notebook"));
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveCurrentPage);

    exportPdfButton = new QPushButton(this);
    exportPdfButton->setFixedSize(26, 30);
    QIcon exportPdfIcon(loadThemedIcon("export"));  // Using PDF icon for export
    exportPdfButton->setIcon(exportPdfIcon);
    exportPdfButton->setStyleSheet(buttonStyle);
    exportPdfButton->setToolTip(tr("Export Annotated PDF"));
    connect(exportPdfButton, &QPushButton::clicked, this, &MainWindow::exportAnnotatedPdf);

    fullscreenButton = new QPushButton(this);
    fullscreenButton->setIcon(loadThemedIcon("fullscreen"));  // Load from resources
    fullscreenButton->setFixedSize(36, 36);
    fullscreenButton->setToolTip(tr("Toggle Fullscreen"));
    fullscreenButton->setStyleSheet(buttonStyle);

    // ✅ Connect button click to toggleFullscreen() function
    connect(fullscreenButton, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    // Use the darkMode variable already declared at the beginning of setupUi()

    redButton = new QPushButton(this);
    redButton->setFixedSize(24, 36);  // Color button
    QString redIconPath = darkMode ? ":/resources/icons/pen_light_red.png" : ":/resources/icons/pen_dark_red.png";
    QIcon redIcon(redIconPath);
    redButton->setIcon(redIcon);
    redButton->setStyleSheet(buttonStyle);
    connect(redButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(getPaletteColor("red")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });
    
    blueButton = new QPushButton(this);
    blueButton->setFixedSize(24, 36);  // Color button
    QString blueIconPath = darkMode ? ":/resources/icons/pen_light_blue.png" : ":/resources/icons/pen_dark_blue.png";
    QIcon blueIcon(blueIconPath);
    blueButton->setIcon(blueIcon);
    blueButton->setStyleSheet(buttonStyle);
    connect(blueButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(getPaletteColor("blue")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });

    yellowButton = new QPushButton(this);
    yellowButton->setFixedSize(24, 36);  // Color button
    QString yellowIconPath = darkMode ? ":/resources/icons/pen_light_yellow.png" : ":/resources/icons/pen_dark_yellow.png";
    QIcon yellowIcon(yellowIconPath);
    yellowButton->setIcon(yellowIcon);
    yellowButton->setStyleSheet(buttonStyle);
    connect(yellowButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(getPaletteColor("yellow")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });

    greenButton = new QPushButton(this);
    greenButton->setFixedSize(24, 36);  // Color button
    QString greenIconPath = darkMode ? ":/resources/icons/pen_light_green.png" : ":/resources/icons/pen_dark_green.png";
    QIcon greenIcon(greenIconPath);
    greenButton->setIcon(greenIcon);
    greenButton->setStyleSheet(buttonStyle);
    connect(greenButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(getPaletteColor("green")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });

    blackButton = new QPushButton(this);
    blackButton->setFixedSize(24, 36);  // Color button
    QString blackIconPath = darkMode ? ":/resources/icons/pen_light_black.png" : ":/resources/icons/pen_dark_black.png";
    QIcon blackIcon(blackIconPath);
    blackButton->setIcon(blackIcon);
    blackButton->setStyleSheet(buttonStyle);
    connect(blackButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(QColor("#000000")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });

    whiteButton = new QPushButton(this);
    whiteButton->setFixedSize(24, 36);  // Color button
    QString whiteIconPath = darkMode ? ":/resources/icons/pen_light_white.png" : ":/resources/icons/pen_dark_white.png";
    QIcon whiteIcon(whiteIconPath);
    whiteButton->setIcon(whiteIcon);
    whiteButton->setStyleSheet(buttonStyle);
    connect(whiteButton, &QPushButton::clicked, [this]() { 
        // Phase 3.1.4: Use currentViewport()
        if (DocumentViewport* vp = currentViewport()) {
            handleColorButtonClick();
            vp->setPenColor(QColor("#FFFFFF")); 
            updateDialDisplay(); 
            updateColorButtonStates();
        }
    });
    
    customColorInput = new QLineEdit(this);
    customColorInput->setPlaceholderText("Custom HEX");
    customColorInput->setFixedSize(0, 0);
    
    // Enable IME support for multi-language input
    customColorInput->setAttribute(Qt::WA_InputMethodEnabled, true);
    customColorInput->setInputMethodHints(Qt::ImhNone); // Allow all input methods
    customColorInput->installEventFilter(this); // Install event filter for IME handling
    
    connect(customColorInput, &QLineEdit::returnPressed, this, &MainWindow::applyCustomColor);

    
    thicknessButton = new QPushButton(this);
    thicknessButton->setIcon(loadThemedIcon("thickness"));
    thicknessButton->setFixedSize(26, 30);
    thicknessButton->setStyleSheet(buttonStyle);
    connect(thicknessButton, &QPushButton::clicked, this, &MainWindow::toggleThicknessSlider);

    thicknessFrame = new QFrame(this);
    thicknessFrame->setFrameShape(QFrame::StyledPanel);
    thicknessFrame->setStyleSheet(R"(
        background-color: black;
        border: 1px solid black;
        padding: 5px;
    )");
    thicknessFrame->setVisible(false);
    thicknessFrame->setFixedSize(220, 40); // Adjust width/height as needed

    thicknessSlider = new QSlider(Qt::Horizontal, this);
    thicknessSlider->setRange(1, 50);
    thicknessSlider->setValue(5);
    thicknessSlider->setMaximumWidth(200);


    connect(thicknessSlider, &QSlider::valueChanged, this, &MainWindow::updateThickness);

    QVBoxLayout *popupLayoutThickness = new QVBoxLayout();
    popupLayoutThickness->setContentsMargins(10, 5, 10, 5);
    popupLayoutThickness->addWidget(thicknessSlider);
    thicknessFrame->setLayout(popupLayoutThickness);


    toolSelector = new QComboBox(this);
    toolSelector->addItem(loadThemedIcon("pen"), "");
    toolSelector->addItem(loadThemedIcon("marker"), "");
    toolSelector->addItem(loadThemedIcon("eraser"), "");
    toolSelector->setFixedWidth(43);
    toolSelector->setFixedHeight(30);
    connect(toolSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::changeTool);

    // Hide toolSelector since it's not used in the layout but needed for functionality
    toolSelector->hide();
    toolSelector->setFixedSize(0, 0); // Make it invisible by setting size to 0

    // ✅ Individual tool buttons
    penToolButton = new QPushButton(this);
    penToolButton->setFixedSize(36, 36);
    penToolButton->setStyleSheet(buttonStyle);
    penToolButton->setToolTip(tr("Pen Tool"));
    connect(penToolButton, &QPushButton::clicked, this, &MainWindow::setPenTool);

    markerToolButton = new QPushButton(this);
    markerToolButton->setFixedSize(36, 36);
    markerToolButton->setStyleSheet(buttonStyle);
    markerToolButton->setToolTip(tr("Marker Tool"));
    connect(markerToolButton, &QPushButton::clicked, this, &MainWindow::setMarkerTool);

    eraserToolButton = new QPushButton(this);
    eraserToolButton->setFixedSize(36, 36);
    eraserToolButton->setStyleSheet(buttonStyle);
    eraserToolButton->setToolTip(tr("Eraser Tool"));
    connect(eraserToolButton, &QPushButton::clicked, this, &MainWindow::setEraserTool);

    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    // Features migrated to DocumentViewport - all pens now use vector layers

    backgroundButton = new QPushButton(this);
    backgroundButton->setFixedSize(26, 30);
    QIcon bgIcon(loadThemedIcon("background"));  // Path to your icon in resources
    backgroundButton->setIcon(bgIcon);
    backgroundButton->setStyleSheet(buttonStyle);
    backgroundButton->setToolTip(tr("Set Background Pic"));
    connect(backgroundButton, &QPushButton::clicked, this, &MainWindow::selectBackground);

    // Initialize straight line toggle button
    straightLineToggleButton = new QPushButton(this);
    straightLineToggleButton->setFixedSize(36, 36);
    straightLineToggleButton->setStyleSheet(buttonStyle);
    straightLineToggleButton->setToolTip(tr("Toggle Straight Line Mode"));
    straightLineToggleButton->setProperty("selected", false); // Initially disabled
    updateButtonIcon(straightLineToggleButton, "straightLine");
    connect(straightLineToggleButton, &QPushButton::clicked, this, [this]() {
        // Phase 3.1.4: Straight line mode stubbed - will be reimplemented for DocumentViewport
        // TODO Phase 3.3: Implement straight line mode in DocumentViewport
        qDebug() << "Straight line toggle: Not implemented yet (Phase 3.3)";
    });
    
    ropeToolButton = new QPushButton(this);
    ropeToolButton->setFixedSize(36, 36);
    ropeToolButton->setStyleSheet(buttonStyle);
    ropeToolButton->setToolTip(tr("Toggle Rope Tool Mode"));
    ropeToolButton->setProperty("selected", false); // Initially disabled
    updateButtonIcon(ropeToolButton, "rope");
    connect(ropeToolButton, &QPushButton::clicked, this, [this]() {
        // Phase 3.1.4: Rope tool mode stubbed - will be reimplemented for DocumentViewport
        // TODO Phase 3.3: Implement rope/lasso tool in DocumentViewport
        qDebug() << "Rope tool toggle: Not implemented yet (Phase 3.3)";
        updateRopeToolButtonState();
    });
    
    // Insert Picture Button
    insertPictureButton = new QPushButton(this);
    insertPictureButton->setFixedSize(36, 36);
    insertPictureButton->setStyleSheet(buttonStyle);
    insertPictureButton->setToolTip(tr("Insert Picture"));
    insertPictureButton->setProperty("selected", false); // Initially disabled
    updateButtonIcon(insertPictureButton, "background");
    connect(insertPictureButton, &QPushButton::clicked, this, [this]() {
        // Phase 3.1.4: Picture insertion stubbed - will be reimplemented for DocumentViewport
        // TODO Phase 4: Implement picture insertion in DocumentViewport via InsertedObject
        qDebug() << "Insert picture: Not implemented yet (Phase 4)";
    });
    
    deletePageButton = new QPushButton(this);
    deletePageButton->setFixedSize(22, 30);
    QIcon trashIcon(loadThemedIcon("trash"));  // Path to your icon in resources
    deletePageButton->setIcon(trashIcon);
    deletePageButton->setStyleSheet(buttonStyle);
    deletePageButton->setToolTip(tr("Clear All Content"));
    connect(deletePageButton, &QPushButton::clicked, this, &MainWindow::deleteCurrentPage);

    zoomButton = new QPushButton(this);
    zoomButton->setIcon(loadThemedIcon("zoom"));
    zoomButton->setFixedSize(26, 30);
    zoomButton->setStyleSheet(buttonStyle);
    connect(zoomButton, &QPushButton::clicked, this, &MainWindow::toggleZoomSlider);

    // ✅ Create the floating frame (Initially Hidden)
    zoomFrame = new QFrame(this);
    zoomFrame->setFrameShape(QFrame::StyledPanel);
    zoomFrame->setStyleSheet(R"(
        background-color: black;
        border: 1px solid black;
        padding: 5px;
    )");
    zoomFrame->setVisible(false);
    zoomFrame->setFixedSize(440, 40); // Adjust width/height as needed

    zoomSlider = new QSlider(Qt::Horizontal, this);
    zoomSlider->setRange(10, 400);
    zoomSlider->setValue(100);
    zoomSlider->setMaximumWidth(405);

    connect(zoomSlider, &QSlider::valueChanged, this, &MainWindow::onZoomSliderChanged);

    QVBoxLayout *popupLayout = new QVBoxLayout();
    popupLayout->setContentsMargins(10, 5, 10, 5);
    popupLayout->addWidget(zoomSlider);
    zoomFrame->setLayout(popupLayout);
  

    zoom50Button = new QPushButton("0.5x", this);
    zoom50Button->setFixedSize(35, 30);
    zoom50Button->setStyleSheet(buttonStyle);
    zoom50Button->setToolTip(tr("Set Zoom to 50%"));
    connect(zoom50Button, &QPushButton::clicked, [this]() { zoomSlider->setValue(qRound(50.0 / initialDpr)); updateDialDisplay(); });

    dezoomButton = new QPushButton("1x", this);
    dezoomButton->setFixedSize(26, 30);
    dezoomButton->setStyleSheet(buttonStyle);
    dezoomButton->setToolTip(tr("Set Zoom to 100%"));
    connect(dezoomButton, &QPushButton::clicked, [this]() { zoomSlider->setValue(qRound(100.0 / initialDpr)); updateDialDisplay(); });

    zoom200Button = new QPushButton("2x", this);
    zoom200Button->setFixedSize(31, 30);
    zoom200Button->setStyleSheet(buttonStyle);
    zoom200Button->setToolTip(tr("Set Zoom to 200%"));
    connect(zoom200Button, &QPushButton::clicked, [this]() { zoomSlider->setValue(qRound(200.0 / initialDpr)); updateDialDisplay(); });

    panXSlider = new QScrollBar(Qt::Horizontal, this);
    panYSlider = new QScrollBar(Qt::Vertical, this);
    panYSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // Phase 3.3: Set fixed high-resolution range for scroll fraction (0.0-1.0 mapped to 0-10000)
    panXSlider->setRange(0, 10000);
    panYSlider->setRange(0, 10000);
    // Set page step to control handle size (10% of range = reasonable handle size)
    panXSlider->setPageStep(1000);
    panYSlider->setPageStep(1000);
    
    // Set scrollbar styling - semi-transparent overlay style
    QString scrollBarStyle = R"(
        QScrollBar {
            background: rgba(180, 180, 180, 120);
            border: none;
            margin: 0px;
        }
        QScrollBar:hover {
            background: rgba(180, 180, 180, 180);
        }
        QScrollBar:horizontal {
            height: 16px !important;
            max-height: 16px !important;
        }
        QScrollBar:vertical {
            width: 16px !important;
            max-width: 16px !important;
        }
        QScrollBar::handle {
            background: rgba(100, 100, 100, 180);
            border-radius: 3px;
            min-height: 40px;
            min-width: 40px;
        }
        QScrollBar::handle:hover {
            background: rgba(80, 80, 80, 220);
        }
        /* Hide scroll buttons */
        QScrollBar::add-line, 
        QScrollBar::sub-line {
            width: 0px;
            height: 0px;
            background: none;
            border: none;
        }
        /* Disable scroll page buttons */
        QScrollBar::add-page, 
        QScrollBar::sub-page {
            background: transparent;
        }
    )";
    
    panXSlider->setStyleSheet(scrollBarStyle);
    panYSlider->setStyleSheet(scrollBarStyle);
    
    // Force fixed dimensions programmatically
    panXSlider->setFixedHeight(16);
    panYSlider->setFixedWidth(16);
    
    // Phase 3.3: Sliders always visible (auto-hide deferred to Phase 3.4)
    panXSlider->setMouseTracking(true);
    panYSlider->setMouseTracking(true);
    panXSlider->setVisible(true);
    panYSlider->setVisible(true);
    
    // Create timer for auto-hiding (disabled for now - Phase 3.4)
    scrollbarHideTimer = new QTimer(this);
    scrollbarHideTimer->setSingleShot(true);
    scrollbarHideTimer->setInterval(200);
    // Timer not connected - sliders stay visible
    
    // Trackpad mode timer: maintains trackpad state across rapid events
    trackpadModeTimer = new QTimer(this);
    trackpadModeTimer->setSingleShot(true);
    trackpadModeTimer->setInterval(350);
    connect(trackpadModeTimer, &QTimer::timeout, this, [this]() {
        trackpadModeActive = false;
    });
    
#ifdef Q_OS_LINUX
    // Create timer for palm rejection restore delay
    palmRejectionTimer = new QTimer(this);
    palmRejectionTimer->setSingleShot(true);
    connect(palmRejectionTimer, &QTimer::timeout, this, &MainWindow::restoreTouchGestureMode);
#endif
    
    // panXSlider->setFixedHeight(30);
    // panYSlider->setFixedWidth(30);

    connect(panXSlider, &QScrollBar::valueChanged, this, &MainWindow::updatePanX);
    
    connect(panYSlider, &QScrollBar::valueChanged, this, &MainWindow::updatePanY);




    // 🌟 PDF Outline Sidebar
    outlineSidebar = new QWidget(this);
    outlineSidebar->setFixedWidth(250);
    outlineSidebar->setVisible(false); // Hidden by default
    
    QVBoxLayout *outlineLayout = new QVBoxLayout(outlineSidebar);
    outlineLayout->setContentsMargins(5, 5, 5, 5);
    
    QLabel *outlineLabel = new QLabel(tr("PDF Outline"), outlineSidebar);
    outlineLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    outlineLayout->addWidget(outlineLabel);
    
    outlineTree = new QTreeWidget(outlineSidebar);
    outlineTree->setHeaderHidden(true);
    outlineTree->setRootIsDecorated(true);
    outlineTree->setIndentation(15);
    outlineLayout->addWidget(outlineTree);
    
    // Connect outline tree item clicks
    connect(outlineTree, &QTreeWidget::itemClicked, this, &MainWindow::onOutlineItemClicked);
    
    // 🌟 Bookmarks Sidebar
    bookmarksSidebar = new QWidget(this);
    bookmarksSidebar->setFixedWidth(250);
    bookmarksSidebar->setVisible(false); // Hidden by default
    QVBoxLayout *bookmarksLayout = new QVBoxLayout(bookmarksSidebar);
    bookmarksLayout->setContentsMargins(5, 5, 5, 5);
    QLabel *bookmarksLabel = new QLabel(tr("Bookmarks"), bookmarksSidebar);
    bookmarksLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    bookmarksLayout->addWidget(bookmarksLabel);
    bookmarksTree = new QTreeWidget(bookmarksSidebar);
    bookmarksTree->setHeaderHidden(true);
    bookmarksTree->setRootIsDecorated(false);
    bookmarksTree->setIndentation(0);
    bookmarksLayout->addWidget(bookmarksTree);
    // Connect bookmarks tree item clicks
    connect(bookmarksTree, &QTreeWidget::itemClicked, this, &MainWindow::onBookmarkItemClicked);
    
    // 🌟 Markdown Notes Sidebar
    markdownNotesSidebar = new MarkdownNotesSidebar(this);
    markdownNotesSidebar->setFixedWidth(300);
    markdownNotesSidebar->setVisible(false); // Hidden by default
    
    // Connect markdown notes signals
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteContentChanged, this, &MainWindow::onMarkdownNoteContentChanged);
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeleted, this, &MainWindow::onMarkdownNoteDeleted);
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::highlightLinkClicked, this, &MainWindow::onHighlightLinkClicked);
    
    // Set up note provider for search functionality
    // Phase 3.1.8: Stubbed - markdown notes will use DocumentViewport in Phase 3.3
    markdownNotesSidebar->setNoteProvider([this]() -> QList<MarkdownNoteData> {
        // TODO Phase 3.3: Get notes from currentViewport()->document()
        return QList<MarkdownNoteData>();
    });
    
    // Phase 3.1.1: QTabWidget replaces old QListWidget tabList
    // m_tabWidget was created in constructor, configure it here
    m_tabWidget->setTabPosition(QTabWidget::North);
    m_tabWidget->setElideMode(Qt::ElideRight);
    // Stylesheet will be applied in updateTheme() to match dark/light mode

    // Connect tab changes to switchTab
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::switchTab);

    // Phase 3.1: Consolidate tab bars - use m_tabWidget's corner widgets instead of separate container
    // Create "Return to Launcher" button as left corner widget
    openRecentNotebooksButton = new QPushButton(this);
    openRecentNotebooksButton->setIcon(loadThemedIcon("recent"));
    openRecentNotebooksButton->setStyleSheet(buttonStyle);
    openRecentNotebooksButton->setToolTip(tr("Return to Launcher"));
    openRecentNotebooksButton->setFixedSize(30, 30);
    connect(openRecentNotebooksButton, &QPushButton::clicked, this, &MainWindow::returnToLauncher);
    m_tabWidget->setCornerWidget(openRecentNotebooksButton, Qt::TopLeftCorner);
    
    // Add Button for New Tab as right corner widget
    addTabButton = new QPushButton(this);
    QIcon addTab(loadThemedIcon("addtab"));
    addTabButton->setIcon(addTab);
    addTabButton->setFixedSize(30, 30);
    addTabButton->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(220, 220, 220, 0);
            border-radius: 0px;
            margin: 2px;
        }
        QPushButton:hover {
            background-color: rgba(200, 200, 200, 255);
        }
        QPushButton:pressed {
            background-color: rgba(180, 180, 180, 255);
        }
    )");
    addTabButton->setToolTip(tr("Add New Tab"));
    connect(addTabButton, &QPushButton::clicked, this, &MainWindow::addNewTab);
    m_tabWidget->setCornerWidget(addTabButton, Qt::TopRightCorner);
    
    // Phase 3.1: Old tabBarContainer kept but hidden (for reference, will be removed later)
    tabBarContainer = new QWidget(this);
    tabBarContainer->setObjectName("tabBarContainer");
    tabBarContainer->setVisible(false);  // Hidden - using m_tabWidget's tab bar instead
    
    // Phase 3.1: Tab scroll handled by QTabWidget internally
    // QTabWidget handles tab scrolling automatically

    connect(toggleTabBarButton, &QPushButton::clicked, this, [=]() {
        // Phase 3.1: Toggle the tab bar visibility via m_tabWidget
        QTabBar* tabBar = m_tabWidget->tabBar();
        bool isVisible = tabBar->isVisible();
        tabBar->setVisible(!isVisible);
        
        // Update button toggle state
        toggleTabBarButton->setProperty("selected", !isVisible);
        updateButtonIcon(toggleTabBarButton, "tabs");
        toggleTabBarButton->style()->unpolish(toggleTabBarButton);
        toggleTabBarButton->style()->polish(toggleTabBarButton);

        // Phase 3.1.8: Stubbed - DocumentViewport handles its own sizing
        QTimer::singleShot(0, this, [this]() {
            // TODO Phase 3.3: Handle viewport sizing if needed
        });
    });
    
    connect(toggleOutlineButton, &QPushButton::clicked, this, &MainWindow::toggleOutlineSidebar);
    connect(toggleBookmarksButton, &QPushButton::clicked, this, &MainWindow::toggleBookmarksSidebar);
    connect(toggleBookmarkButton, &QPushButton::clicked, this, &MainWindow::toggleCurrentPageBookmark);
    connect(toggleMarkdownNotesButton, &QPushButton::clicked, this, &MainWindow::toggleMarkdownNotesSidebar);
    connect(touchGesturesButton, &QPushButton::clicked, this, [this]() {
        cycleTouchGestureMode();
        touchGesturesButton->setProperty("selected", touchGestureMode != TouchGestureMode::Disabled);
        touchGesturesButton->setProperty("yAxisOnly", touchGestureMode == TouchGestureMode::YAxisOnly);
        updateButtonIcon(touchGesturesButton, "hand");
        touchGesturesButton->style()->unpolish(touchGesturesButton);
        touchGesturesButton->style()->polish(touchGesturesButton);
    });

    


    // Previous page button
    prevPageButton = new QPushButton(this);
    prevPageButton->setFixedSize(24, 30);
    prevPageButton->setText("◀");
    prevPageButton->setStyleSheet(buttonStyle);
    prevPageButton->setToolTip(tr("Previous Page"));
    connect(prevPageButton, &QPushButton::clicked, this, &MainWindow::goToPreviousPage);

    pageInput = new QSpinBox(this);
    pageInput->setFixedSize(36, 30);
    pageInput->setMinimum(1);
    pageInput->setMaximum(9999);
    pageInput->setValue(1);
    pageInput->setMaximumWidth(100);
    connect(pageInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onPageInputChanged);

    // Next page button
    nextPageButton = new QPushButton(this);
    nextPageButton->setFixedSize(24, 30);
    nextPageButton->setText("▶");
    nextPageButton->setStyleSheet(buttonStyle);
    nextPageButton->setToolTip(tr("Next Page"));
    connect(nextPageButton, &QPushButton::clicked, this, &MainWindow::goToNextPage);

    jumpToPageButton = new QPushButton(this);
    // QIcon jumpIcon(":/resources/icons/bookpage.png");  // Path to your icon in resources
    jumpToPageButton->setFixedSize(26, 30);
    jumpToPageButton->setStyleSheet(buttonStyle);
    jumpToPageButton->setIcon(loadThemedIcon("bookpage"));
    connect(jumpToPageButton, &QPushButton::clicked, this, &MainWindow::showJumpToPageDialog);

    // ✅ Dial Toggle Button
    dialToggleButton = new QPushButton(this);
    dialToggleButton->setFixedSize(26, 30);
    dialToggleButton->setToolTip(tr("Toggle Magic Dial"));
    dialToggleButton->setStyleSheet(buttonStyle);
    dialToggleButton->setProperty("selected", false); // Initially hidden
    updateButtonIcon(dialToggleButton, "dial");

    // ✅ Connect to toggle function
    connect(dialToggleButton, &QPushButton::clicked, this, &MainWindow::toggleDial);

    // toggleDial();

    

    fastForwardButton = new QPushButton(this);
    fastForwardButton->setFixedSize(26, 30);
    fastForwardButton->setToolTip(tr("Toggle Fast Forward 8x"));
    fastForwardButton->setStyleSheet(buttonStyle);
    fastForwardButton->setProperty("selected", false); // Initially disabled
    updateButtonIcon(fastForwardButton, "fastforward");

    // ✅ Toggle fast-forward mode
    connect(fastForwardButton, &QPushButton::clicked, [this]() {
        fastForwardMode = !fastForwardMode;
        updateFastForwardButtonState();
    });

    QComboBox *dialModeSelector = new QComboBox(this);
    dialModeSelector->addItem("Page Switch", PageSwitching);
    dialModeSelector->addItem("Zoom", ZoomControl);
    dialModeSelector->addItem("Thickness", ThicknessControl);

    dialModeSelector->addItem("Tool Switch", ToolSwitching);
    dialModeSelector->setFixedWidth(120);

    connect(dialModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int index) { changeDialMode(static_cast<DialMode>(index)); });

    // Hide the dialModeSelector since it's not used in the layout but needed for compilation
    dialModeSelector->hide();
    dialModeSelector->setFixedSize(0, 0); // Make it invisible by setting size to 0



    // Removed unused colorPreview widget that was causing UI artifacts

    btnPageSwitch = new QPushButton(loadThemedIcon("bookpage"), "", this);
    btnPageSwitch->setStyleSheet(buttonStyle);
    btnPageSwitch->setFixedSize(26, 30);
    btnPageSwitch->setToolTip(tr("Set Dial Mode to Page Switching"));
    btnZoom = new QPushButton(loadThemedIcon("zoom"), "", this);
    btnZoom->setStyleSheet(buttonStyle);
    btnZoom->setFixedSize(26, 30);
    btnZoom->setToolTip(tr("Set Dial Mode to Zoom Ctrl"));
    btnThickness = new QPushButton(loadThemedIcon("thickness"), "", this);
    btnThickness->setStyleSheet(buttonStyle);
    btnThickness->setFixedSize(26, 30);
    btnThickness->setToolTip(tr("Set Dial Mode to Pen Tip Thickness Ctrl"));

    btnTool = new QPushButton(loadThemedIcon("pen"), "", this);
    btnTool->setStyleSheet(buttonStyle);
    btnTool->setFixedSize(26, 30);
    btnTool->setToolTip(tr("Set Dial Mode to Tool Switching"));
    btnPresets = new QPushButton(loadThemedIcon("preset"), "", this);
    btnPresets->setStyleSheet(buttonStyle);
    btnPresets->setFixedSize(26, 30);
    btnPresets->setToolTip(tr("Set Dial Mode to Color Preset Selection"));
    btnPannScroll = new QPushButton(loadThemedIcon("scroll"), "", this);
    btnPannScroll->setStyleSheet(buttonStyle);
    btnPannScroll->setFixedSize(26, 30);
    btnPannScroll->setToolTip(tr("Slide and turn pages with the dial"));

    connect(btnPageSwitch, &QPushButton::clicked, this, [this]() { changeDialMode(PageSwitching); });
    connect(btnZoom, &QPushButton::clicked, this, [this]() { changeDialMode(ZoomControl); });
    connect(btnThickness, &QPushButton::clicked, this, [this]() { changeDialMode(ThicknessControl); });

    connect(btnTool, &QPushButton::clicked, this, [this]() { changeDialMode(ToolSwitching); });
    connect(btnPresets, &QPushButton::clicked, this, [this]() { changeDialMode(PresetSelection); }); 
    connect(btnPannScroll, &QPushButton::clicked, this, [this]() { changeDialMode(PanAndPageScroll); });


    // ✅ Initialize color presets based on palette mode (will be updated after UI setup)
    colorPresets.enqueue(getDefaultPenColor());
    colorPresets.enqueue(QColor("#AA0000"));  // Temporary - will be updated later
    colorPresets.enqueue(QColor("#997700"));
    colorPresets.enqueue(QColor("#0000AA"));
    colorPresets.enqueue(QColor("#007700"));
    colorPresets.enqueue(QColor("#000000"));
    colorPresets.enqueue(QColor("#FFFFFF"));

    // ✅ Button to add current color to presets
    addPresetButton = new QPushButton(loadThemedIcon("savepreset"), "", this);
    addPresetButton->setStyleSheet(buttonStyle);
    addPresetButton->setToolTip(tr("Add Current Color to Presets"));
    addPresetButton->setFixedSize(26, 30);
    connect(addPresetButton, &QPushButton::clicked, this, &MainWindow::addColorPreset);




    openControlPanelButton = new QPushButton(this);
    openControlPanelButton->setIcon(loadThemedIcon("settings"));  // Replace with your actual settings icon
    openControlPanelButton->setStyleSheet(buttonStyle);
    openControlPanelButton->setToolTip(tr("Open Control Panel"));
    openControlPanelButton->setFixedSize(26, 30);  // Adjust to match your other buttons

    connect(openControlPanelButton, &QPushButton::clicked, this, [=]() {
        // Phase 3.1.8: ControlPanelDialog disabled - depends on InkCanvas
        QMessageBox::information(const_cast<MainWindow*>(this), tr("Control Panel"), 
            tr("Control Panel is being redesigned. Coming soon!"));
        // TODO Phase 4.6: Reconnect ControlPanelDialog with DocumentViewport
    });

    // openRecentNotebooksButton created earlier and added to tab bar layout

    customColorButton = new QPushButton(this);
    customColorButton->setFixedSize(62, 30);
    QColor initialColor = getDefaultPenColor();  // Theme-aware default color
    customColorButton->setText(initialColor.name().toUpper());

    // Phase 3.1.4: Use currentViewport() for initial color
    if (DocumentViewport* vp = currentViewport()) {
        initialColor = vp->penColor();
    }

    updateCustomColorButtonStyle(initialColor);

    QTimer::singleShot(0, this, [=]() {
        connect(customColorButton, &QPushButton::clicked, this, [=]() {
            // Phase 3.1.4: Use currentViewport()
            DocumentViewport* vp = currentViewport();
            if (!vp) return;
            
            handleColorButtonClick();
            
            // Get the current custom color from the button text
            QString buttonText = customColorButton->text();
            QColor customColor(buttonText);
            
            // Check if the custom color is already the current pen color
            if (vp->penColor() == customColor) {
                // Second click - show color picker dialog
                QColor chosen = QColorDialog::getColor(vp->penColor(), this, "Select Pen Color");
                if (chosen.isValid()) {
                    vp->setPenColor(chosen);
                    updateCustomColorButtonStyle(chosen);
                    updateDialDisplay();
                    updateColorButtonStates();
                }
            } else {
                // First click - apply the custom color
                vp->setPenColor(customColor);
                updateDialDisplay();
                updateColorButtonStates();
            }
        });
    });

    // ========================================
    // Overflow Menu for infrequently used actions
    // ========================================
    overflowMenuButton = new QPushButton(this);
    overflowMenuButton->setObjectName("overflowMenuButton");
    overflowMenuButton->setFixedSize(30, 30);
    overflowMenuButton->setToolTip(tr("More Actions"));
    overflowMenuButton->setIcon(loadThemedIcon("menu"));  // Will use menu icon
    overflowMenuButton->setCursor(Qt::PointingHandCursor);
    overflowMenuButton->setStyleSheet(buttonStyle);
    
    overflowMenu = new QMenu(this);
    overflowMenu->setObjectName("overflowMenu");
    
    // Add actions to the overflow menu
    QAction *managePdfAction = overflowMenu->addAction(loadThemedIcon("pdf"), tr("Import/Clear Document"));
    connect(managePdfAction, &QAction::triggered, this, &MainWindow::handleSmartPdfButton);
    
    QAction *exportPdfAction = overflowMenu->addAction(loadThemedIcon("export"), tr("Export Annotated PDF"));
    connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportAnnotatedPdf);
    
    overflowMenu->addSeparator();
    
    QAction *zoom50Action = overflowMenu->addAction(tr("Zoom 50%"));
    connect(zoom50Action, &QAction::triggered, this, [this]() { zoom50Button->click(); });
    
    QAction *zoomResetAction = overflowMenu->addAction(tr("Zoom Reset"));
    connect(zoomResetAction, &QAction::triggered, this, [this]() { dezoomButton->click(); });
    
    QAction *zoom200Action = overflowMenu->addAction(tr("Zoom 200%"));
    connect(zoom200Action, &QAction::triggered, this, [this]() { zoom200Button->click(); });
    
    overflowMenu->addSeparator();
    
    QAction *jumpToPageAction = overflowMenu->addAction(tr("Jump to Page..."));
    connect(jumpToPageAction, &QAction::triggered, this, &MainWindow::showJumpToPageDialog);
    
    QAction *openControlPanelAction = overflowMenu->addAction(loadThemedIcon("settings"), tr("Settings"));
    connect(openControlPanelAction, &QAction::triggered, this, [this]() {
        openControlPanelButton->click();
    });
    
    // Connect button to show menu
    connect(overflowMenuButton, &QPushButton::clicked, this, [this]() {
        // Position menu below the button
        QPoint pos = overflowMenuButton->mapToGlobal(QPoint(0, overflowMenuButton->height()));
        overflowMenu->exec(pos);
    });

    QHBoxLayout *controlLayout = new QHBoxLayout;
    
    // Left stretch to center the main buttons
    controlLayout->addStretch();
    
    // Centered buttons - toggle and utility
    controlLayout->addWidget(toggleTabBarButton);
    controlLayout->addWidget(toggleMarkdownNotesButton);
    controlLayout->addWidget(touchGesturesButton);
    controlLayout->addWidget(pdfTextSelectButton);
    controlLayout->addWidget(saveButton);
    
    // Color buttons
    controlLayout->addWidget(redButton);
    controlLayout->addWidget(blueButton);
    controlLayout->addWidget(yellowButton);
    controlLayout->addWidget(greenButton);
    controlLayout->addWidget(blackButton);
    controlLayout->addWidget(whiteButton);
    controlLayout->addWidget(customColorButton);
    
    // Tool buttons
    controlLayout->addWidget(penToolButton);
    controlLayout->addWidget(markerToolButton);
    controlLayout->addWidget(eraserToolButton);
    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    controlLayout->addWidget(straightLineToggleButton);
    controlLayout->addWidget(ropeToolButton);
    controlLayout->addWidget(insertPictureButton);
    controlLayout->addWidget(fullscreenButton);
    
    // Right stretch to center the main buttons
    controlLayout->addStretch();
    
    // Page controls and overflow menu on the right (fixed position)
    controlLayout->addWidget(toggleBookmarkButton);
    controlLayout->addWidget(pageInput);
    controlLayout->addWidget(overflowMenuButton);
    controlLayout->addWidget(deletePageButton);
    
    // Benchmark controls (hidden by default, can be enabled in settings)
    controlLayout->addWidget(benchmarkButton);
    controlLayout->addWidget(benchmarkLabel);
    
    // Hide buttons that are now in overflow menu or obsolete (keep for functionality)
    thicknessButton->setVisible(false);
    loadPdfButton->setVisible(false);
    clearPdfButton->setVisible(false);
    exportPdfButton->setVisible(false);
    openControlPanelButton->setVisible(false);
    selectFolderButton->setVisible(false);
    jumpToPageButton->setVisible(false);
    zoom50Button->setVisible(false);
    dezoomButton->setVisible(false);
    zoom200Button->setVisible(false);
    openRecentNotebooksButton->setVisible(false);
    benchmarkButton->setVisible(false);  // Hidden by default, toggle via Settings > Features
    benchmarkLabel->setVisible(false);
    prevPageButton->setVisible(false);
    nextPageButton->setVisible(false);
    
    
    
    controlBar = new QWidget;  // Use member variable instead of local
    controlBar->setObjectName("controlBar");
    // controlBar->setLayout(controlLayout);  // Commented out - responsive layout will handle this
    controlBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    // Theme will be applied later in loadUserSettings -> updateTheme()
    controlBar->setStyleSheet("");

    
        

    // Phase 3.1.1: m_tabWidget was created in constructor, just configure size policy
    m_tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Create a container for the tab widget and scrollbars with relative positioning
    QWidget *canvasContainer = new QWidget;
    QVBoxLayout *canvasLayout = new QVBoxLayout(canvasContainer);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->addWidget(m_tabWidget);  // Phase 3.1.1: Use m_tabWidget instead of canvasStack

    // Enable context menu for the workaround
    canvasContainer->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Set up the scrollbars to overlay the canvas
    panXSlider->setParent(canvasContainer);
    panYSlider->setParent(canvasContainer);
    
    // Raise scrollbars to ensure they're visible above the canvas
    panXSlider->raise();
    panYSlider->raise();
    
    // Handle scrollbar intersection
    connect(canvasContainer, &QWidget::customContextMenuRequested, this, [this]() {
        // This connection is just to make sure the container exists
        // and can receive signals - a workaround for some Qt versions
    });
    
    // Position the scrollbars at the bottom and right edges
    canvasContainer->installEventFilter(this);
    
    // Update scrollbar positions initially
    QTimer::singleShot(0, this, [this, canvasContainer]() {
        updateScrollbarPositions();
    });

    // ========================================
    // Dial Mode Toolbar (retractable, vertical, right side)
    // ========================================
    // The toolbar panel - this is what takes up layout space
    dialToolbar = new QWidget(this);
    dialToolbar->setObjectName("dialToolbar");
    dialToolbar->setFixedWidth(50);
    
    QVBoxLayout *dialToolbarLayout = new QVBoxLayout(dialToolbar);
    dialToolbarLayout->setContentsMargins(4, 8, 4, 8);
    dialToolbarLayout->setSpacing(6);
    dialToolbarLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    
    // Add dial mode buttons to vertical toolbar with larger sizes for touch
    QSize dialBtnSize(42, 38);
    
    dialToggleButton->setFixedSize(dialBtnSize);
    fastForwardButton->setFixedSize(dialBtnSize);
    btnPannScroll->setFixedSize(dialBtnSize);
    btnPageSwitch->setFixedSize(dialBtnSize);
    btnZoom->setFixedSize(dialBtnSize);
    btnThickness->setFixedSize(dialBtnSize);
    btnTool->setFixedSize(dialBtnSize);
    btnPresets->setFixedSize(dialBtnSize);
    addPresetButton->setFixedSize(dialBtnSize);
    
    dialToolbarLayout->addWidget(dialToggleButton);
    dialToolbarLayout->addWidget(fastForwardButton);
    dialToolbarLayout->addWidget(btnPannScroll);
    dialToolbarLayout->addWidget(btnPageSwitch);
    dialToolbarLayout->addWidget(btnZoom);
    dialToolbarLayout->addWidget(btnThickness);
    dialToolbarLayout->addWidget(btnTool);
    dialToolbarLayout->addWidget(btnPresets);
    dialToolbarLayout->addWidget(addPresetButton);
    
    dialToolbarLayout->addStretch(); // Push buttons to top
    
    // Apply theme-aware styling to toolbar panel (solid background)
    {
        bool isDark = isDarkMode();
        QString panelBg = isDark ? "#2D2D2D" : "#F5F5F5";
        QString panelBorder = isDark ? "#555555" : "#CCCCCC";
        QString panelStyle = QString(
            "QWidget#dialToolbar {"
            "  background-color: %1;"
            "  border-left: 1px solid %2;"
            "}"
        ).arg(panelBg, panelBorder);
        dialToolbar->setStyleSheet(panelStyle);
    }
    
    // Dial toolbar floating tab - floats on top of the canvas, positioned absolutely
    // This is a child of MainWindow so it can overlay the canvas area
    dialToolbarToggle = new QPushButton(this);
    dialToolbarToggle->setObjectName("dialToolbarTab");
    dialToolbarToggle->setFixedSize(28, 80);
    dialToolbarToggle->setToolTip(tr("Toggle Dial Mode Toolbar"));
    dialToolbarToggle->setCursor(Qt::PointingHandCursor);
    dialToolbarToggle->setIcon(loadThemedIcon("dial"));
    dialToolbarToggle->setIconSize(QSize(18, 18));
    dialToolbarToggle->raise(); // Ensure it's on top
    
    // Apply floating tab styling (solid background, right side tabs have left-rounded corners)
    {
        bool isDark = isDarkMode();
        QString tabBg = isDark ? "#3A3A3A" : "#EAEAEA";
        QString tabHover = isDark ? "#4A4A4A" : "#DADADA";
        QString tabBorder = isDark ? "#555555" : "#CCCCCC";
        
        QString dialTabStyle = QString(
            "QPushButton#dialToolbarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-right: none;"
            "  border-top-left-radius: 0px;"
            "  border-bottom-left-radius: 0px;"
            "}"
            "QPushButton#dialToolbarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#dialToolbarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBg, tabBorder, tabHover);
        dialToolbarToggle->setStyleSheet(dialTabStyle);
    }
    
    // Connect toggle tab to show/hide toolbar panel
    connect(dialToolbarToggle, &QPushButton::clicked, this, [this]() {
        dialToolbarExpanded = !dialToolbarExpanded;
        
        // Show/hide the toolbar panel
        dialToolbar->setVisible(dialToolbarExpanded);
        
        // Icon already indicates state, no need for arrow text
        
        // Update toggle button state for styling
        dialToolbarToggle->setProperty("selected", dialToolbarExpanded);
        dialToolbarToggle->style()->unpolish(dialToolbarToggle);
        dialToolbarToggle->style()->polish(dialToolbarToggle);
        
        // Reposition the tab and dial container
        positionDialToolbarTab();
        if (dialContainer && dialContainer->isVisible()) {
            positionDialContainer();
        }
    });

    // Main layout: tab bar -> toolbar -> canvas (vertical stack)
    QWidget *container = new QWidget;
    container->setObjectName("container");
    QVBoxLayout *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);  // ✅ Remove extra margins
    mainLayout->setSpacing(0); // ✅ Remove spacing between components

    // Add components in vertical order
    // Phase 3.1: tabBarContainer hidden - buttons moved to m_tabWidget corner widgets
    // mainLayout->addWidget(tabBarContainer);   // Old tab bar - now hidden
    mainLayout->addWidget(controlBar);        // Toolbar at top
    
    // Content area with sidebars and canvas
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(outlineSidebar, 0); // Fixed width outline sidebar
    contentLayout->addWidget(bookmarksSidebar, 0); // Fixed width bookmarks sidebar
    contentLayout->addWidget(canvasContainer, 1); // Canvas takes remaining space
    contentLayout->addWidget(dialToolbar, 0); // Dial mode toolbar (before markdown sidebar)
    contentLayout->addWidget(markdownNotesSidebar, 0); // Fixed width markdown notes sidebar
    
    QWidget *contentWidget = new QWidget;
    contentWidget->setLayout(contentLayout);
    mainLayout->addWidget(contentWidget, 1);

    setCentralWidget(container);

    benchmarkTimer = new QTimer(this);
    connect(benchmarkButton, &QPushButton::clicked, this, &MainWindow::toggleBenchmark);
    connect(benchmarkTimer, &QTimer::timeout, this, &MainWindow::updateBenchmarkDisplay);

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/temp_session";
    QDir dir(tempDir);

    // Remove all contents (but keep the directory itself)
    if (dir.exists()) {
        dir.removeRecursively();  // Careful: this wipes everything inside
    }
    QDir().mkpath(tempDir);  // Recreate clean directory

    addNewTab();

    // Setup single instance server
    setupSingleInstanceServer();

    // Initialize responsive toolbar layout
    createSingleRowLayout();  // Start with single row layout
    
    // Now that all UI components are created, update the color palette
    updateColorPalette();
    
    // Position add tab button and floating sidebar tabs initially
    QTimer::singleShot(100, this, [this]() {
        updateTabSizes();
        positionLeftSidebarTabs();
        positionDialToolbarTab();
    });
    
    // =========================================================================
    // Phase doc-1: Application-wide keyboard shortcuts
    // Using QShortcut with ApplicationShortcut context for guaranteed behavior
    // regardless of which widget has focus.
    // =========================================================================
    
    // Save Document: Ctrl+S - save document to JSON file
    QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::ApplicationShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &MainWindow::saveDocument);
    
    // Load Document: Ctrl+O - load document from JSON file
    QShortcut* loadShortcut = new QShortcut(QKeySequence::Open, this);
    loadShortcut->setContext(Qt::ApplicationShortcut);
    connect(loadShortcut, &QShortcut::activated, this, &MainWindow::loadDocument);
    
    // Add Page: Ctrl+Shift+A - appends new page at end of document
    QShortcut* addPageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this);
    addPageShortcut->setContext(Qt::ApplicationShortcut);
    connect(addPageShortcut, &QShortcut::activated, this, &MainWindow::addPageToDocument);
    
    // Open PDF: Ctrl+Shift+O - open PDF file in new tab
    QShortcut* openPdfShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this);
    openPdfShortcut->setContext(Qt::ApplicationShortcut);
    connect(openPdfShortcut, &QShortcut::activated, this, &MainWindow::openPdfDocument);

}

MainWindow::~MainWindow() {
    // ✅ MEMORY SAFETY: Wait for any pending async save to complete
    // This prevents the async task from running after MainWindow is destroyed
    // (even though it captures data by value, this ensures clean shutdown)
    if (concurrentSaveFuture.isValid() && !concurrentSaveFuture.isFinished()) {
        concurrentSaveFuture.waitForFinished();
    }

    saveButtonMappings();  // ✅ Save on exit, as backup
    
    // ✅ FIX: Disconnect TabManager signals BEFORE Qt deletes children
    // This prevents "signal during destruction" crash where TabManager emits
    // currentViewportChanged during child deletion, triggering updateDialDisplay
    // on a partially-destroyed MainWindow.
    if (m_tabManager) {
        disconnect(m_tabManager, nullptr, this, nullptr);
    }
    
    // Phase 3.3: Clean up viewport scroll connections
    if (m_hScrollConn) disconnect(m_hScrollConn);
    if (m_vScrollConn) disconnect(m_vScrollConn);
    if (m_connectedViewport) {
        m_connectedViewport->removeEventFilter(this);
    }
    
    // Note: Do NOT manually delete canvas - it's a child of canvasStack
    // Qt will automatically delete all canvases when canvasStack is destroyed
    // Manual deletion here would cause double-delete and segfault!
    
    // ✅ CRITICAL: Stop controller thread before destruction
    // Qt will abort if a QThread is destroyed while still running
    if (controllerThread && controllerThread->isRunning()) {
        controllerThread->quit();
        controllerThread->wait();  // Wait for thread to finish
    }
    
    // Phase 3.1: LauncherWindow disconnected
    // if (sharedLauncher) {
    //     sharedLauncher->deleteLater();
    //     sharedLauncher = nullptr;
    // }
    
    // Cleanup single instance resources
    if (localServer) {
        localServer->close();
        localServer = nullptr;
    }
    
#ifdef Q_OS_LINUX
    // Stop palm rejection timer to prevent callback during destruction
    if (palmRejectionTimer) {
        palmRejectionTimer->stop();
        palmRejectionTimer->disconnect();
    }
#endif
    
    // Use static cleanup method for consistent cleanup
    cleanupSharedResources();
}

void MainWindow::toggleBenchmark() {
    // Phase 3.1.4: Use currentViewport() for benchmark
    benchmarking = !benchmarking;
    if (DocumentViewport* vp = currentViewport()) {
        if (benchmarking) {
            vp->startBenchmark();
            benchmarkTimer->start(1000);
        } else {
            vp->stopBenchmark();
            benchmarkTimer->stop();
            benchmarkLabel->setText(tr("PR:N/A"));
        }
    } else {
        benchmarkTimer->stop();
        benchmarkLabel->setText(tr("PR:N/A"));
    }
}

void MainWindow::updateBenchmarkDisplay() {
    // Phase 3.1.4: Use currentViewport() for benchmark display
    if (DocumentViewport* vp = currentViewport()) {
        int paintRate = vp->getPaintRate();
        benchmarkLabel->setText(QString(tr("PR:%1 Hz")).arg(paintRate));
    } else {
        benchmarkLabel->setText(tr("PR:N/A"));
    }
}

void MainWindow::applyCustomColor() {
    // Phase 3.1.4: Use currentViewport()
    if (DocumentViewport* vp = currentViewport()) {
        QString colorCode = customColorInput->text();
        if (!colorCode.startsWith("#")) {
            colorCode.prepend("#");
        }
        vp->setPenColor(QColor(colorCode));
        updateDialDisplay(); 
    }
}

void MainWindow::updateThickness(int value) {
    // Phase 3.1.4: Use currentViewport()
    if (DocumentViewport* vp = currentViewport()) {
        // Calculate thickness based on the slider value at 100% zoom
        qreal visualThickness = value;
        
        // Apply zoom scaling to maintain visual consistency
        qreal zoomPercent = vp->zoomLevel() * 100.0;  // zoomLevel() returns 1.0 for 100%
        qreal actualThickness = visualThickness * (100.0 / zoomPercent); 
        
        vp->setPenThickness(actualThickness);
    }
}

void MainWindow::adjustThicknessForZoom(int oldZoom, int newZoom) {
    // Adjust all tool thicknesses to maintain visual consistency when zoom changes
    // Phase 3.1.8: Stubbed - DocumentViewport handles zoom-adjusted thickness internally
    if (oldZoom == newZoom || oldZoom <= 0 || newZoom <= 0) return;
    
    // TODO Phase 3.3: If needed, adjust thickness on DocumentViewport
    // DocumentViewport already handles zoom-aware pen thickness
    updateThicknessSliderForCurrentTool();
    updateDialDisplay();
}


void MainWindow::changeTool(int index) {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        if (index == 0) {
            vp->setCurrentTool(ToolType::Pen);
        } else if (index == 1) {
            vp->setCurrentTool(ToolType::Marker);
        } else if (index == 2) {
            vp->setCurrentTool(ToolType::Eraser);
        }
    }
    updateToolButtonStates();
    updateThicknessSliderForCurrentTool();
    updateDialDisplay();
}

void MainWindow::setPenTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Pen);
    }
    updateToolButtonStates();
    updateThicknessSliderForCurrentTool();
    updateDialDisplay();
}

void MainWindow::setMarkerTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Marker);
    }
    updateToolButtonStates();
    updateThicknessSliderForCurrentTool();
    updateDialDisplay();
}

void MainWindow::setEraserTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Eraser);
    }
    updateToolButtonStates();
    updateThicknessSliderForCurrentTool();
    updateDialDisplay();
}

// REMOVED Phase 3.1.3: setVectorPenTool(), setVectorEraserTool(), vectorUndo()
// Features migrated to DocumentViewport

void MainWindow::updateToolButtonStates() {
    // Phase 3.1.4: Use currentViewport()
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    // Reset all tool buttons
    penToolButton->setProperty("selected", false);
    markerToolButton->setProperty("selected", false);
    eraserToolButton->setProperty("selected", false);
    
    // Update icons for unselected state
    updateButtonIcon(penToolButton, "pen");
    updateButtonIcon(markerToolButton, "marker");
    updateButtonIcon(eraserToolButton, "eraser");
    
    // Set the selected property for the current tool
    ToolType tool = vp->currentTool();
    switch (tool) {
        case ToolType::Pen:
            penToolButton->setProperty("selected", true);
            updateButtonIcon(penToolButton, "pen");
            break;
        case ToolType::Marker:
            markerToolButton->setProperty("selected", true);
            updateButtonIcon(markerToolButton, "marker");
            break;
        case ToolType::Eraser:
            eraserToolButton->setProperty("selected", true);
            updateButtonIcon(eraserToolButton, "eraser");
            break;
        case ToolType::Highlighter:
        case ToolType::Lasso:
            // Future tools - will be implemented in DocumentViewport (Phase 2B)
            break;
    }
    
    // Force style update
    penToolButton->style()->unpolish(penToolButton);
    penToolButton->style()->polish(penToolButton);
    markerToolButton->style()->unpolish(markerToolButton);
    markerToolButton->style()->polish(markerToolButton);
    eraserToolButton->style()->unpolish(eraserToolButton);
    eraserToolButton->style()->polish(eraserToolButton);
    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton style updates
}

void MainWindow::handleColorButtonClick() {
    // Phase 3.1.4: Use currentViewport()
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    ToolType tool = vp->currentTool();
    
    // If in eraser mode, switch back to pen mode
    if (tool == ToolType::Eraser) {
        vp->setCurrentTool(ToolType::Pen);
        updateToolButtonStates();
        updateThicknessSliderForCurrentTool();
    }
    
    // TODO Phase 3.3: Rope tool mode handling (if implemented)
    // For now, rope tool is InkCanvas-only and will be reimplemented later
}

void MainWindow::updateThicknessSliderForCurrentTool() {
    // Phase 3.1.4: Use currentViewport()
    DocumentViewport* vp = currentViewport();
    if (!vp || !thicknessSlider) return;
    
    // Block signals to prevent recursive calls
    thicknessSlider->blockSignals(true);
    
    // Update slider to reflect current tool's thickness
    qreal currentThickness = vp->penThickness();
    
    // Convert thickness back to slider value (reverse of updateThickness calculation)
    qreal zoomPercent = vp->zoomLevel() * 100.0;
    qreal visualThickness = currentThickness * (zoomPercent / 100.0);
    int sliderValue = qBound(1, static_cast<int>(qRound(visualThickness)), 50);
    
    thicknessSlider->setValue(sliderValue);
    thicknessSlider->blockSignals(false);
}

bool MainWindow::selectFolder() {
    // Phase 3.1.8: Stubbed - old .spn format is being replaced with .snx
    // TODO Phase 3.4: Implement save folder selection for new .snx format
    QMessageBox::information(this, tr("Save Location"), 
        tr("Saving to folder is being redesigned. Coming soon with .snx format!"));
    return false;
}

void MainWindow::saveCanvas() {
    // Phase 3.1.8: Stubbed - old .spn format is being replaced with .snx
    // TODO Phase 3.4: Implement document saving to new .snx format
    qDebug() << "saveCanvas(): Not implemented yet (Phase 3.4)";
}


void MainWindow::switchPage(int pageNumber) {
    // Phase 3.1.6: Stubbed - will use DocumentViewport::scrollToPage() in Phase 3.3
    Q_UNUSED(pageNumber);
    // TODO Phase 3.3.4: Use currentViewport()->scrollToPage()
    qDebug() << "switchPage(): Not implemented yet (Phase 3.3.4)";
}
void MainWindow::switchPageWithDirection(int pageNumber, int direction) {
    // Phase 3.1.6: Stubbed - will use DocumentViewport::scrollToPage() in Phase 3.3
    Q_UNUSED(pageNumber);
    Q_UNUSED(direction);
    // TODO Phase 3.3.4: Use currentViewport()->scrollToPage() with scroll direction
    qDebug() << "switchPageWithDirection(): Not implemented yet (Phase 3.3.4)";
}

void MainWindow::deleteCurrentPage() {
    // Phase 3.1.8: Stubbed - page deletion will use DocumentViewport
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    int displayPageNumber = vp->currentPageIndex() + 1;
    
    // Show confirmation dialog
    QMessageBox confirmBox(this);
    confirmBox.setWindowTitle(tr("Clear Page"));
    confirmBox.setIcon(QMessageBox::Warning);
    confirmBox.setText(tr("Are you sure you want to clear page %1?").arg(displayPageNumber));
    confirmBox.setInformativeText(tr("This will permanently delete all drawings on this page. This action cannot be undone."));
    confirmBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmBox.setDefaultButton(QMessageBox::No);
    
    if (confirmBox.exec() == QMessageBox::Yes) {
        // TODO Phase 3.3: Clear page via vp->document()->currentPage()->clearAll()
        qDebug() << "deleteCurrentPage(): Clear page not implemented yet";
    }
}

void MainWindow::saveCurrentPage() {
    // Phase 3.1.8: Stubbed - old .spn format is being replaced with .snx
    // TODO Phase 3.4: Implement page saving to new .snx format
    qDebug() << "saveCurrentPage(): Not implemented yet (Phase 3.4)";
}

void MainWindow::saveCurrentPageConcurrent() {
    // Phase 3.1.8: Stubbed - old .spn format is being replaced with .snx
    // TODO Phase 3.4: Implement concurrent page saving to new .snx format
    qDebug() << "saveCurrentPageConcurrent(): Not implemented yet (Phase 3.4)";
}

void MainWindow::selectBackground() {
    // Phase 3.1.8: Stubbed - background selection will use DocumentViewport
    // TODO Phase 3.3: Implement background selection for DocumentViewport
    QMessageBox::information(this, tr("Background"), 
        tr("Background selection is being redesigned. Coming soon!"));
}

// Helper function to show page range selection dialog
bool MainWindow::showPageRangeDialog(int totalPages, bool &exportWholeDocument, int &startPage, int &endPage) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Select Page Range to Export"));
    dialog.setMinimumWidth(400);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);
    
    // Info label
    QLabel *infoLabel = new QLabel(tr("Choose which pages to export:"));
    mainLayout->addWidget(infoLabel);
    
    mainLayout->addSpacing(10);
    
    // Radio button for whole document
    QRadioButton *wholeDocRadio = new QRadioButton(tr("Whole document (pages 1-%1)").arg(totalPages));
    wholeDocRadio->setChecked(true);
    mainLayout->addWidget(wholeDocRadio);
    
    mainLayout->addSpacing(5);
    
    // Radio button for page range
    QRadioButton *rangeRadio = new QRadioButton(tr("Page range:"));
    mainLayout->addWidget(rangeRadio);
    
    // Range input layout
    QHBoxLayout *rangeLayout = new QHBoxLayout();
    rangeLayout->addSpacing(30); // Indent
    
    QLabel *fromLabel = new QLabel(tr("From:"));
    rangeLayout->addWidget(fromLabel);
    
    QSpinBox *fromSpinBox = new QSpinBox();
    fromSpinBox->setMinimum(1);
    fromSpinBox->setMaximum(totalPages);
    fromSpinBox->setValue(1);
    fromSpinBox->setEnabled(false); // Initially disabled
    rangeLayout->addWidget(fromSpinBox);
    
    QLabel *toLabel = new QLabel(tr("To:"));
    rangeLayout->addWidget(toLabel);
    
    QSpinBox *toSpinBox = new QSpinBox();
    toSpinBox->setMinimum(1);
    toSpinBox->setMaximum(totalPages);
    toSpinBox->setValue(totalPages);
    toSpinBox->setEnabled(false); // Initially disabled
    rangeLayout->addWidget(toSpinBox);
    
    rangeLayout->addStretch();
    mainLayout->addLayout(rangeLayout);
    
    mainLayout->addSpacing(20);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));
    
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);
    
    // Connect radio button to enable/disable spin boxes
    connect(rangeRadio, &QRadioButton::toggled, fromSpinBox, &QSpinBox::setEnabled);
    connect(rangeRadio, &QRadioButton::toggled, toSpinBox, &QSpinBox::setEnabled);
    
    // Connect buttons
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    // Ensure 'from' <= 'to'
    connect(fromSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [=](int value) {
        if (value > toSpinBox->value()) {
            toSpinBox->setValue(value);
        }
    });
    connect(toSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [=](int value) {
        if (value < fromSpinBox->value()) {
            fromSpinBox->setValue(value);
        }
    });
    
    // Show dialog
    if (dialog.exec() == QDialog::Accepted) {
        exportWholeDocument = wholeDocRadio->isChecked();
        startPage = fromSpinBox->value() - 1; // Convert to 0-based
        endPage = toSpinBox->value() - 1; // Convert to 0-based
        return true;
    }
    
    return false; // User cancelled
}

void MainWindow::exportAnnotatedPdf() {
    // Phase 3.1.8: Stubbed - PDF export will be reimplemented for DocumentViewport
    QMessageBox::information(this, tr("Export PDF"), 
        tr("PDF export is being redesigned. Coming soon!"));
    return;

#if 0 // Phase 3.1.8: Old export code disabled - uses InkCanvas
    // Original code below - will be removed after Phase 3.4
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;

    QString saveFolder = canvas->getSaveFolder();
    QString notebookId = canvas->getNotebookId();

    if (saveFolder.isEmpty() || notebookId.isEmpty()) {
        QMessageBox::warning(this, tr("Export Failed"), 
            tr("Cannot export: notebook not properly initialized."));
        return;
    }

    // Check if a PDF is loaded
    bool hasPdf = canvas->isPdfLoadedFunc();
    
    if (!hasPdf) {
        // No PDF - export canvas-only notebook to PDF
        exportCanvasOnlyNotebook(saveFolder, notebookId);
        return;
    }

    // Get PDF document and notebook info
    Poppler::Document* pdfDoc = canvas->getPdfDocument();
    if (!pdfDoc) {
        QMessageBox::warning(this, tr("Export Failed"), 
            tr("Failed to access PDF document."));
        return;
    }

    int totalPages = canvas->getTotalPdfPages();
    QString originalPdfPath = canvas->getPdfPath();

    if (originalPdfPath.isEmpty()) {
        QMessageBox::warning(this, tr("Export Failed"), 
            tr("Cannot export: PDF path not found."));
        return;
    }
    
    // Show page range selection dialog
    bool exportWholeDocument = true;
    int userStartPage = 0;
    int userEndPage = totalPages - 1;
    
    if (!showPageRangeDialog(totalPages, exportWholeDocument, userStartPage, userEndPage)) {
        return; // User cancelled
    }
    
    // Apply page range filter
    int exportStartPage = exportWholeDocument ? 0 : userStartPage;
    int exportEndPage = exportWholeDocument ? totalPages - 1 : userEndPage;

    // Ask user where to save the exported PDF
    // Default to same location as .spn file with similar name
    QString defaultPath;
    QString displayPath = canvas->getDisplayPath();
    
    if (!displayPath.isEmpty() && displayPath.endsWith(".spn", Qt::CaseInsensitive)) {
        // It's a .spn file - use its directory and name
        QFileInfo spnInfo(displayPath);
        QString baseName = spnInfo.completeBaseName(); // filename without extension
        QString directory = spnInfo.absolutePath();
        defaultPath = directory + "/" + baseName + "_annotated.pdf";
    } else if (!displayPath.isEmpty()) {
        // It's a folder - use folder name
        QFileInfo folderInfo(displayPath);
        QString folderName = folderInfo.fileName();
        defaultPath = displayPath + "/" + folderName + "_annotated.pdf";
    } else {
        // Fallback
        defaultPath = "annotated_export.pdf";
    }
    
    QString exportPath = QFileDialog::getSaveFileName(this, 
        tr("Export Annotated PDF"), 
        defaultPath, 
        "PDF Files (*.pdf)");

    if (exportPath.isEmpty()) {
        return; // User cancelled
    }

    // Ensure .pdf extension
    if (!exportPath.toLower().endsWith(".pdf")) {
        exportPath += ".pdf";
    }

    // First pass: identify which pages have annotations (within selected range)
    QSet<int> annotatedPages;
    int scanPageCount = exportEndPage - exportStartPage + 1;
    QProgressDialog scanProgress(tr("Scanning for annotated pages..."), tr("Cancel"), 0, scanPageCount, this);
    scanProgress.setWindowModality(Qt::WindowModal);
    scanProgress.setMinimumDuration(500);
    
    int scanIdx = 0;
    for (int pageNum = exportStartPage; pageNum <= exportEndPage; ++pageNum) {
        if (scanProgress.wasCanceled()) {
            return;
        }
        
        scanProgress.setValue(scanIdx);
        QCoreApplication::processEvents();
        
        QString canvasFile = saveFolder + QString("/%1_%2.png")
                            .arg(notebookId)
                            .arg(pageNum, 5, 10, QChar('0'));
        
        if (QFile::exists(canvasFile)) {
            // Check if the file has actual content (not just an empty/transparent PNG)
            QImage canvasImage(canvasFile);
            if (!canvasImage.isNull() && canvasImage.width() > 0 && canvasImage.height() > 0) {
                annotatedPages.insert(pageNum);
            }
        }
        scanIdx++;
    }
    scanProgress.setValue(scanPageCount);

    if (annotatedPages.isEmpty()) {
        // No annotations found in selected page range
        // Check if pdftk is available for fast export (even with no annotations)
        QProcess testPdftk;
        testPdftk.start("pdftk", QStringList() << "--version");
        bool hasPdftk = testPdftk.waitForFinished(1000) && testPdftk.exitCode() == 0;
        
        if (exportWholeDocument) {
            // No annotations and exporting whole document
            QMessageBox::information(this, tr("No Annotations"), 
                tr("No annotated pages found. The output will be identical to the original PDF."));
            
            if (hasPdftk) {
                // Use pdftk to copy (preserves metadata and outline better than QFile::copy)
                // Run asynchronously to avoid UI freeze
                QProcess *copyProcess = new QProcess(this);
                QProgressDialog *copyProgress = new QProgressDialog(tr("Copying PDF..."), QString(), 0, 0, this);
                copyProgress->setWindowModality(Qt::WindowModal);
                copyProgress->setCancelButton(nullptr);
                copyProgress->setMinimumDuration(0);
                copyProgress->show();
                
                // Use QPointer to safely check if MainWindow still exists
                QPointer<MainWindow> mainWindowPtr(this);
                
                connect(copyProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
                        copyProgress->close();
                        copyProgress->deleteLater();
                        
                        // Only show message boxes if MainWindow still exists
                        if (!mainWindowPtr.isNull()) {
                            if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
                                QMessageBox::information(mainWindowPtr, tr("Export Complete"), 
                                    tr("PDF copied successfully (no annotations to add)."));
                            } else {
                                QMessageBox::critical(mainWindowPtr, tr("Export Failed"), 
                                    tr("Failed to copy PDF.\n\nError: %1")
                                    .arg(QString::fromUtf8(copyProcess->readAllStandardError())));
                            }
                        }
                        copyProcess->deleteLater();
                    });
                
                copyProcess->start("pdftk", QStringList() << originalPdfPath << "output" << exportPath);
            } else {
                // Fall back to simple file copy
                if (QFile::copy(originalPdfPath, exportPath)) {
                    QMessageBox::information(this, tr("Export Complete"), 
                        tr("PDF copied successfully (no annotations to add)."));
                } else {
                    QMessageBox::critical(this, tr("Export Failed"), 
                        tr("Failed to copy original PDF."));
                }
            }
            return;
        } else {
            // User selected a page range, but no annotations - extract that range using pdftk
            if (!hasPdftk) {
                QMessageBox::critical(this, tr("pdftk Required"), 
                    tr("pdftk is required to export page ranges.\n\n"
                       "Please install pdftk:\n"
                       "  MSYS2: pacman -S mingw-w64-clang-x86_64-pdftk\n"
                       "  Windows: Download from https://www.pdflabs.com/tools/pdftk-the-pdf-toolkit/"));
                return;
            }
            
            // Extract outline BEFORE starting the export (to avoid timeout in callback)
            QProgressDialog outlineProgress(tr("Reading PDF outline..."), QString(), 0, 0, this);
            outlineProgress.setWindowModality(Qt::WindowModal);
            outlineProgress.setCancelButton(nullptr);
            outlineProgress.setMinimumDuration(0);
            outlineProgress.show();
            QCoreApplication::processEvents();
            
            QString outlineData;
            QString filteredOutline;
            bool hasOutline = extractPdfOutlineData(originalPdfPath, outlineData);
            if (hasOutline) {
                filteredOutline = filterAndAdjustOutline(outlineData, exportStartPage, exportEndPage, 0);
            }
            
            outlineProgress.close();
            
            // Now run pdftk extraction asynchronously to avoid UI freeze
            QProcess *extractProcess = new QProcess(this);
            QProgressDialog *extractProgress = new QProgressDialog(
                tr("Extracting pages %1-%2...").arg(exportStartPage + 1).arg(exportEndPage + 1), 
                QString(), 0, 0, this);
            extractProgress->setWindowModality(Qt::WindowModal);
            extractProgress->setCancelButton(nullptr);
            extractProgress->setMinimumDuration(0);
            extractProgress->show();
            
            // Use QPointer to safely check if MainWindow still exists
            QPointer<MainWindow> mainWindowPtr(this);
            
            connect(extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
                    extractProgress->close();
                    extractProgress->deleteLater();
                    
                    // Only process results if MainWindow still exists
                    if (!mainWindowPtr.isNull()) {
                        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
                            // Apply the pre-extracted outline if we have one
                            if (!filteredOutline.isEmpty()) {
                                mainWindowPtr->applyOutlineToPdf(exportPath, filteredOutline);
                            }
                            
                            QMessageBox::information(mainWindowPtr, tr("Export Complete"), 
                                tr("Pages %1-%2 exported successfully (no annotations found in this range).")
                                .arg(exportStartPage + 1).arg(exportEndPage + 1));
                        } else {
                            QMessageBox::critical(mainWindowPtr, tr("Export Failed"), 
                                tr("Failed to extract page range from PDF.\n\nError: %1")
                                .arg(QString::fromUtf8(extractProcess->readAllStandardError())));
                        }
                    }
                    extractProcess->deleteLater();
                });
            
            QStringList args;
            args << originalPdfPath;
            args << "cat";
            args << QString("%1-%2").arg(exportStartPage + 1).arg(exportEndPage + 1); // pdftk uses 1-based indexing
            args << "output";
            args << exportPath;
            
            extractProcess->start("pdftk", args);
            return;
        }
    }

    // Try to use pdftk for efficient merging (only annotated pages need rendering)
    QString tempAnnotatedPdf = QDir::temp().filePath("speedynote_annotated_pages.pdf");
    
    // Strategy: Create a PDF with only annotated pages (using original page numbers),
    // then use pdftk to merge and extract the desired range in one operation
    // If pdftk isn't available, fall back to full re-render (but inform user)
    
    // Check if pdftk is available
    bool pdftkAvailable = false;
    QProcess testProcess;
    
    testProcess.start("pdftk", QStringList() << "--version");
    if (testProcess.waitForFinished(1000) && testProcess.exitCode() == 0) {
        pdftkAvailable = true;
    }

    if (!pdftkAvailable) {
        // pdftk not available - inform user and use slower method
        int pageCount = exportWholeDocument ? totalPages : (exportEndPage - exportStartPage + 1);
        QMessageBox::StandardButton reply = QMessageBox::question(this, 
            tr("Optimization Not Available"),
            tr("Found %1 annotated pages out of %2 total pages in selected range.\n\n"
               "For fast export, please install 'pdftk':\n"
               "  MSYS2: pacman -S mingw-w64-clang-x86_64-pdftk\n"
               "  Windows: Download from https://www.pdflabs.com/tools/pdftk-the-pdf-toolkit/\n\n"
               "Without pdftk, export requires re-rendering all %2 pages.\n"
               "On slow systems this may take over an hour.\n\n"
               "Continue with slow export anyway?")
            .arg(annotatedPages.size()).arg(pageCount),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::No) {
            return;
        }
        
        // Fall back to rendering all pages (or selected range)
        exportAnnotatedPdfFullRender(exportPath, annotatedPages, exportWholeDocument, exportStartPage, exportEndPage);
        return;
    }

    // Create temporary PDF with only annotated pages
    QProgressDialog progress(tr("Rendering annotated pages..."), tr("Cancel"), 0, annotatedPages.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    
    QList<int> sortedPages = annotatedPages.values();
    std::sort(sortedPages.begin(), sortedPages.end());
    
    // Create temporary annotated pages PDF (using original page numbers)
    if (!createAnnotatedPagesPdf(tempAnnotatedPdf, sortedPages, progress)) {
        QFile::remove(tempAnnotatedPdf);
        return;
    }

    // Close the rendering progress dialog
    progress.close();
    
    // Create new progress dialog for merge step
    QProgressDialog *mergeProgress = new QProgressDialog(
        tr("Merging %1 annotated pages with original PDF...\nThis may take a minute...").arg(annotatedPages.size()),
        tr("Cancel"), 0, 0, this);
    mergeProgress->setWindowModality(Qt::WindowModal);
    mergeProgress->setMinimumDuration(0);
    mergeProgress->setValue(0);
    mergeProgress->show();
    QCoreApplication::processEvents();
    
    // Run merge in background thread using QFutureWatcher for proper event-driven handling
    QFutureWatcher<bool> *mergeWatcher = new QFutureWatcher<bool>(this);
    QString *mergeError = new QString(); // Heap allocated so it survives the lambda
    
    // Set parent to ensure cleanup if window closes during export
    mergeProgress->setParent(this);
    
    // Use QPointer to safely check if MainWindow still exists when lambda runs
    QPointer<MainWindow> mainWindowPtr(this);
    
    // Connect finished signal before starting
    // Use mergeWatcher as context so cleanup always happens even if MainWindow is destroyed
    connect(mergeWatcher, &QFutureWatcher<bool>::finished, mergeWatcher, [=]() {
        bool mergeSuccess = mergeWatcher->result();
        
        // Close progress dialog
        if (mergeProgress) {
            mergeProgress->close();
            mergeProgress->deleteLater();
        }
        
        // Cleanup temp files - always happens
        QFile::remove(tempAnnotatedPdf);

        // Only show message boxes if MainWindow still exists
        if (!mainWindowPtr.isNull()) {
            if (mergeSuccess) {
                QFileInfo outputInfo(exportPath);
                QFileInfo originalInfo(originalPdfPath);
                
                int exportedPageCount = exportWholeDocument ? totalPages : (exportEndPage - exportStartPage + 1);
                
                QMessageBox::information(mainWindowPtr, tr("Export Complete"), 
                    tr("Annotated PDF exported successfully!\n\n"
                       "Annotated pages: %1 of %2\n"
                       "Original size: %3 MB\n"
                       "Output size: %4 MB\n"
                       "Saved to: %5")
                    .arg(annotatedPages.size())
                    .arg(exportedPageCount)
                    .arg(originalInfo.size() / 1024.0 / 1024.0, 0, 'f', 2)
                    .arg(outputInfo.size() / 1024.0 / 1024.0, 0, 'f', 2)
                    .arg(exportPath));
            } else {
                QString errorMsg = tr("Failed to merge annotated pages with original PDF.");
                if (!mergeError->isEmpty()) {
                    errorMsg += "\n\n" + *mergeError;
                }
                QMessageBox::critical(mainWindowPtr, tr("Export Failed"), errorMsg);
            }
        }
        
        // Cleanup - always happens even if MainWindow is destroyed
        delete mergeError;
        mergeWatcher->deleteLater();
    });
    
    // Handle cancel
    connect(mergeProgress, &QProgressDialog::canceled, mergeProgress, [=]() {
        // Note: Can't actually cancel pdftk once started
        if (mergeProgress) {
            mergeProgress->setLabelText(tr("Waiting for merge to complete..."));
        }
    });
    
    // Start the background task
    // Pass exportStartPage and exportEndPage to handle page range extraction during merge
    QFuture<bool> mergeFuture = QtConcurrent::run([=]() {
        // mergePdfWithPdftk will handle extracting the range if needed
        return mergePdfWithPdftk(originalPdfPath, tempAnnotatedPdf, exportPath, sortedPages, 
                                 mergeError, exportWholeDocument, exportStartPage, exportEndPage);
    });
    
    mergeWatcher->setFuture(mergeFuture);
#endif // Phase 3.1.8: Old export code disabled
}

// Phase 3.1.8: Entire function disabled - uses InkCanvas
#if 0
// Export canvas-only notebook (no underlying PDF) to PDF
void MainWindow::exportCanvasOnlyNotebook(const QString &saveFolder, const QString &notebookId) {
    // Scan for canvas PNG files
    QDir dir(saveFolder);
    QStringList filters;
    filters << QString("%1_*.png").arg(notebookId);
    QStringList pngFiles = dir.entryList(filters, QDir::Files, QDir::Name);
    
    if (pngFiles.isEmpty()) {
        QMessageBox::information(this, tr("No Pages Found"), 
            tr("No canvas pages found to export."));
        return;
    }
    
    // Parse page numbers from filenames
    QMap<int, QString> pageFiles; // pageNumber -> filePath
    for (const QString &fileName : pngFiles) {
        // Format: notebookId_XXXXX.png
        QString baseName = fileName;
        baseName.remove(0, notebookId.length() + 1); // Remove "notebookId_"
        baseName.chop(4); // Remove ".png"
        
        bool ok;
        int pageNum = baseName.toInt(&ok);
        if (ok) {
            pageFiles[pageNum] = dir.filePath(fileName);
        }
    }
    
    if (pageFiles.isEmpty()) {
        QMessageBox::information(this, tr("No Pages Found"), 
            tr("No valid canvas pages found to export."));
        return;
    }
    
    // Get sorted list of pages
    QList<int> sortedPages = pageFiles.keys();
    std::sort(sortedPages.begin(), sortedPages.end());
    
    int totalPages = sortedPages.size();
    
    // Show page range selection dialog
    bool exportWholeDocument = true;
    int userStartPage = 0;
    int userEndPage = totalPages - 1;
    
    if (!showPageRangeDialog(totalPages, exportWholeDocument, userStartPage, userEndPage)) {
        return; // User cancelled
    }
    
    // Apply page range filter
    if (!exportWholeDocument) {
        // Filter sortedPages to only include pages in the selected range
        QList<int> filteredPages;
        for (int i = 0; i < sortedPages.size(); ++i) {
            if (i >= userStartPage && i <= userEndPage) {
                filteredPages.append(sortedPages[i]);
            }
        }
        sortedPages = filteredPages;
    }
    
    if (sortedPages.isEmpty()) {
        QMessageBox::information(this, tr("No Pages to Export"), 
            tr("No pages found in the selected range."));
        return;
    }
    
    // Ask user where to save
    // Default to same location as .spn file with similar name
    QString defaultPath;
    InkCanvas *canvas = currentCanvas();
    if (canvas) {
        QString displayPath = canvas->getDisplayPath();
        
        if (!displayPath.isEmpty() && displayPath.endsWith(".spn", Qt::CaseInsensitive)) {
            // It's a .spn file - use its directory and name
            QFileInfo spnInfo(displayPath);
            QString baseName = spnInfo.completeBaseName(); // filename without extension
            QString directory = spnInfo.absolutePath();
            defaultPath = directory + "/" + baseName + ".pdf";
        } else if (!displayPath.isEmpty()) {
            // It's a folder - use folder name
            QFileInfo folderInfo(displayPath);
            QString folderName = folderInfo.fileName();
            defaultPath = displayPath + "/" + folderName + ".pdf";
        } else {
            // Fallback
            defaultPath = "canvas_export.pdf";
        }
    } else {
        defaultPath = "canvas_export.pdf";
    }
    
    QString exportPath = QFileDialog::getSaveFileName(this, 
        tr("Export Canvas Notebook to PDF"), 
        defaultPath, 
        "PDF Files (*.pdf)");
    
    if (exportPath.isEmpty()) {
        return; // User cancelled
    }
    
    // Ensure .pdf extension
    if (!exportPath.toLower().endsWith(".pdf")) {
        exportPath += ".pdf";
    }
    
    // Show progress dialog
    QProgressDialog progress(tr("Creating PDF from canvas pages..."), tr("Cancel"), 0, sortedPages.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    
    // Load first page to determine size
    QImage firstImage(pageFiles[sortedPages.first()]);
    if (firstImage.isNull()) {
        QMessageBox::critical(this, tr("Export Failed"), 
            tr("Failed to read canvas pages."));
        return;
    }
    
    // Create QPdfWriter
    QPdfWriter pdfWriter(exportPath);
    pdfWriter.setResolution(pdfRenderDPI);
    
    // Calculate page size in points (1/72 inch) from image size and DPI
    // imageWidth pixels / DPI * 72 = points
    qreal pageWidthPoints = (firstImage.width() * 72.0) / pdfRenderDPI;
    qreal pageHeightPoints = (firstImage.height() * 72.0) / pdfRenderDPI;
    
    pdfWriter.setPageSize(QPageSize(QSizeF(pageWidthPoints, pageHeightPoints), QPageSize::Point));
    pdfWriter.setPageMargins(QMarginsF(0, 0, 0, 0));
    
    QPainter painter;
    if (!painter.begin(&pdfWriter)) {
        QMessageBox::critical(this, tr("Export Failed"), 
            tr("Failed to create PDF file."));
        return;
    }
    
    // Export each page
    for (int i = 0; i < sortedPages.size(); ++i) {
        if (progress.wasCanceled()) {
            painter.end();
            QFile::remove(exportPath);
            return;
        }
        
        int pageNum = sortedPages[i];
        progress.setValue(i);
        progress.setLabelText(tr("Exporting page %1 of %2...").arg(pageNum + 1).arg(sortedPages.size()));
        QCoreApplication::processEvents();
        
        // Add new page if not the first
        if (i > 0) {
            pdfWriter.newPage();
        }
        
        // Load and draw canvas image
        QImage canvasImage(pageFiles[pageNum]);
        if (!canvasImage.isNull()) {
            QSizeF targetSize = pdfWriter.pageLayout().paintRectPixels(pdfRenderDPI).size();
            QRectF targetRect(0, 0, targetSize.width(), targetSize.height());
            painter.drawImage(targetRect, canvasImage);
        }
    }
    
    painter.end();
    progress.setValue(sortedPages.size());
    
    QFileInfo outputInfo(exportPath);
    QMessageBox::information(this, tr("Export Complete"), 
        tr("Canvas notebook exported successfully!\n\n"
           "Pages exported: %1\n"
           "Output size: %2 MB\n"
           "Saved to: %3")
        .arg(sortedPages.size())
        .arg(outputInfo.size() / 1024.0 / 1024.0, 0, 'f', 2)
        .arg(exportPath));
}
#endif // Phase 3.1.8: exportCanvasOnlyNotebook disabled

// Phase 3.1.8: Stub implementation - function disabled
void MainWindow::exportCanvasOnlyNotebook(const QString &saveFolder, const QString &notebookId) {
    Q_UNUSED(saveFolder);
    Q_UNUSED(notebookId);
    qDebug() << "exportCanvasOnlyNotebook(): Disabled in Phase 3.1.8";
}

// Phase 3.1.8: Entire function disabled - uses InkCanvas
#if 0
// Helper function for full render fallback
void MainWindow::exportAnnotatedPdfFullRender(const QString &exportPath, const QSet<int> &annotatedPages, 
                                               bool exportWholeDocument, int exportStartPage, int exportEndPage) {
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    Poppler::Document* pdfDoc = canvas->getPdfDocument();
    QString saveFolder = canvas->getSaveFolder();
    QString notebookId = canvas->getNotebookId();
    QString originalPdfPath = canvas->getPdfPath();
    int totalPages = canvas->getTotalPdfPages();
    
    // Determine the range to export
    int startPage = exportWholeDocument ? 0 : exportStartPage;
    int endPage = exportWholeDocument ? (totalPages - 1) : exportEndPage;
    int pageCount = endPage - startPage + 1;

    QProgressDialog progress(tr("Exporting annotated PDF..."), tr("Cancel"), 0, pageCount, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QPdfWriter pdfWriter(exportPath);
    pdfWriter.setResolution(pdfRenderDPI);
    
    std::unique_ptr<Poppler::Page> firstPage(pdfDoc->page(startPage));
    if (!firstPage) return;
    
    QSizeF pageSize = firstPage->pageSizeF();
    pdfWriter.setPageSize(QPageSize(pageSize, QPageSize::Point));
    pdfWriter.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter;
    if (!painter.begin(&pdfWriter)) {
        QMessageBox::critical(this, tr("Export Failed"), 
            tr("Failed to create PDF file."));
        return;
    }

    bool firstPageWritten = false;
    int exportedPageIndex = 0;
    for (int pageNum = startPage; pageNum <= endPage; ++pageNum) {
        if (progress.wasCanceled()) {
            painter.end();
            QFile::remove(exportPath);
            return;
        }

        progress.setValue(exportedPageIndex);
        if (annotatedPages.contains(pageNum)) {
            progress.setLabelText(tr("Rendering page %1 of %2 (annotated)...").arg(exportedPageIndex + 1).arg(pageCount));
        } else {
            progress.setLabelText(tr("Rendering page %1 of %2...").arg(exportedPageIndex + 1).arg(pageCount));
        }
        QCoreApplication::processEvents();

        if (firstPageWritten) {
            pdfWriter.newPage();
        }
        firstPageWritten = true;

        std::unique_ptr<Poppler::Page> pdfPage(pdfDoc->page(pageNum));
        if (!pdfPage) {
            exportedPageIndex++;
            continue;
        }

        QImage pdfImage = pdfPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
        if (pdfImage.isNull()) {
            exportedPageIndex++;
            continue;
        }

        QSizeF targetSize = pdfWriter.pageLayout().paintRectPixels(pdfRenderDPI).size();
        QRectF targetRect(0, 0, targetSize.width(), targetSize.height());

        painter.drawImage(targetRect, pdfImage);

        // Overlay annotations only if they exist
        if (annotatedPages.contains(pageNum)) {
            QString canvasFile = saveFolder + QString("/%1_%2.png")
                                .arg(notebookId)
                                .arg(pageNum, 5, 10, QChar('0'));

            if (QFile::exists(canvasFile)) {
                QImage canvasImage(canvasFile);
                if (!canvasImage.isNull()) {
                    painter.drawImage(targetRect, canvasImage);
                }
            }
        }
        
        exportedPageIndex++;
    }

    painter.end();
    progress.setValue(pageCount);

    QMessageBox::information(this, tr("Export Complete"), 
        tr("Annotated PDF exported to:\n%1").arg(exportPath));
}
#endif // Phase 3.1.8: exportAnnotatedPdfFullRender disabled

// Phase 3.1.8: Stub implementation - function disabled
void MainWindow::exportAnnotatedPdfFullRender(const QString &exportPath, const QSet<int> &annotatedPages, 
                                               bool exportWholeDocument, int exportStartPage, int exportEndPage) {
    Q_UNUSED(exportPath);
    Q_UNUSED(annotatedPages);
    Q_UNUSED(exportWholeDocument);
    Q_UNUSED(exportStartPage);
    Q_UNUSED(exportEndPage);
    qDebug() << "exportAnnotatedPdfFullRender(): Disabled in Phase 3.1.8";
}

// Phase 3.1.8: Entire function disabled - uses InkCanvas
#if 0
// Helper function to create PDF with only annotated pages
bool MainWindow::createAnnotatedPagesPdf(const QString &outputPath, const QList<int> &pages, QProgressDialog &progress) {
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return false;
    
    Poppler::Document* pdfDoc = canvas->getPdfDocument();
    QString saveFolder = canvas->getSaveFolder();
    QString notebookId = canvas->getNotebookId();

    QPdfWriter pdfWriter(outputPath);
    pdfWriter.setResolution(pdfRenderDPI);
    
    std::unique_ptr<Poppler::Page> firstPage(pdfDoc->page(pages.first()));
    if (!firstPage) return false;
    
    QSizeF pageSize = firstPage->pageSizeF();
    pdfWriter.setPageSize(QPageSize(pageSize, QPageSize::Point));
    pdfWriter.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter;
    if (!painter.begin(&pdfWriter)) return false;

    for (int i = 0; i < pages.size(); ++i) {
        if (progress.wasCanceled()) {
            painter.end();
            return false;
        }

        int pageNum = pages[i];
        progress.setValue(i);
        progress.setLabelText(tr("Rendering page %1...").arg(pageNum + 1));
        QCoreApplication::processEvents();

        if (i > 0) {
            pdfWriter.newPage();
        }

        std::unique_ptr<Poppler::Page> pdfPage(pdfDoc->page(pageNum));
        if (!pdfPage) continue;

        QImage pdfImage = pdfPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
        if (pdfImage.isNull()) continue;

        QSizeF targetSize = pdfWriter.pageLayout().paintRectPixels(pdfRenderDPI).size();
        QRectF targetRect(0, 0, targetSize.width(), targetSize.height());

        painter.drawImage(targetRect, pdfImage);

        QString canvasFile = saveFolder + QString("/%1_%2.png")
                            .arg(notebookId)
                            .arg(pageNum, 5, 10, QChar('0'));

        if (QFile::exists(canvasFile)) {
            QImage canvasImage(canvasFile);
            if (!canvasImage.isNull()) {
                painter.drawImage(targetRect, canvasImage);
            }
        }
    }

    painter.end();
    return true;
}
#endif // Phase 3.1.8: createAnnotatedPagesPdf disabled

// Phase 3.1.8: Stub implementation - function disabled
bool MainWindow::createAnnotatedPagesPdf(const QString &outputPath, const QList<int> &pages, QProgressDialog &progress) {
    Q_UNUSED(outputPath);
    Q_UNUSED(pages);
    Q_UNUSED(progress);
    qDebug() << "createAnnotatedPagesPdf(): Disabled in Phase 3.1.8";
    return false;
}

// Phase 3.1.8: Entire function disabled - uses InkCanvas
#if 0
// Helper function to merge using pdftk
bool MainWindow::mergePdfWithPdftk(const QString &originalPdf, const QString &annotatedPagesPdf, 
                                    const QString &outputPdf, const QList<int> &annotatedPageNumbers,
                                    QString *errorMsg, bool exportWholeDocument, int exportStartPage, int exportEndPage) {
    // Strategy: Use pdftk's "cat" command to assemble pages from original and annotated PDFs
    // This reads through the PDF only once and assembles pages in order
    // Note: pdftk is single-threaded by design, no way to parallelize
    
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return false;
    
    int totalPages = canvas->getTotalPdfPages();
    
    // Determine the range to export
    int startPage = exportWholeDocument ? 0 : exportStartPage;
    int endPage = exportWholeDocument ? (totalPages - 1) : exportEndPage;
    
    // Create a map for quick lookup
    QMap<int, int> annotatedPageMap; // originalPageNum -> indexInAnnotatedPdf
    for (int i = 0; i < annotatedPageNumbers.size(); ++i) {
        annotatedPageMap[annotatedPageNumbers[i]] = i;
    }
    
    // Build the page specification string (only for the selected range)
    QStringList pageSpecs;
    int lastProcessedPage = startPage - 1;
    
    for (int pageNum = startPage; pageNum <= endPage; ++pageNum) {
        if (annotatedPageMap.contains(pageNum)) {
            // Need to add pages from original up to (but not including) this page
            if (pageNum > lastProcessedPage + 1) {
                int rangeStart = lastProcessedPage + 2; // pdftk uses 1-based indexing
                int rangeEnd = pageNum; // This page will be from annotated
                if (rangeStart == rangeEnd) {
                    pageSpecs << QString("A%1").arg(rangeStart);
                } else {
                    pageSpecs << QString("A%1-%2").arg(rangeStart).arg(rangeEnd);
                }
            }
            
            // Add the annotated page
            int annotatedIndex = annotatedPageMap[pageNum];
            pageSpecs << QString("B%1").arg(annotatedIndex + 1); // 1-based
            lastProcessedPage = pageNum;
        }
    }
    
    // Add remaining pages from original if any (within the selected range)
    if (lastProcessedPage < endPage) {
        int rangeStart = lastProcessedPage + 2;
        int rangeEnd = endPage + 1; // 1-based
        if (rangeStart == rangeEnd) {
            pageSpecs << QString("A%1").arg(rangeStart);
        } else {
            pageSpecs << QString("A%1-%2").arg(rangeStart).arg(rangeEnd);
        }
    }
    
    // Build pdftk command with compression for faster output
    QProcess pdftkProcess;
    QStringList args;
    args << "A=" + originalPdf
         << "B=" + annotatedPagesPdf
         << "cat";
    args << pageSpecs;  // Add each page spec as separate argument
    args << "output" << outputPdf
         << "compress";  // Enable compression for faster writing
    
    pdftkProcess.start("pdftk", args);
    if (!pdftkProcess.waitForFinished(300000) || pdftkProcess.exitCode() != 0) { // 5 minute timeout
        if (errorMsg) {
            *errorMsg = QString("pdftk merge failed:\nStderr: %1\nExit code: %2")
                .arg(QString(pdftkProcess.readAllStandardError()))
                .arg(pdftkProcess.exitCode());
        }
        return false;
    }
    
    // Preserve PDF outline/bookmarks
    QString outlineData;
    if (extractPdfOutlineData(originalPdf, outlineData)) {
        // Filter and adjust outline for the exported page range
        QString filteredOutline = filterAndAdjustOutline(outlineData, startPage, endPage, 0);
        
        // Apply the filtered outline to the output PDF
        if (!filteredOutline.isEmpty()) {
            applyOutlineToPdf(outputPdf, filteredOutline);
            // Note: We don't treat outline application failure as a critical error
            // The PDF is still valid without the outline
        }
    }
    
    return true;
}
#endif // Phase 3.1.8: mergePdfWithPdftk disabled

// Phase 3.1.8: Stub implementation - function disabled
bool MainWindow::mergePdfWithPdftk(const QString &originalPdf, const QString &annotatedPagesPdf, 
                                    const QString &outputPdf, const QList<int> &annotatedPageNumbers,
                                    QString *errorMsg, bool exportWholeDocument, int exportStartPage, int exportEndPage) {
    Q_UNUSED(originalPdf);
    Q_UNUSED(annotatedPagesPdf);
    Q_UNUSED(outputPdf);
    Q_UNUSED(annotatedPageNumbers);
    Q_UNUSED(exportWholeDocument);
    Q_UNUSED(exportStartPage);
    Q_UNUSED(exportEndPage);
    if (errorMsg) *errorMsg = "Disabled in Phase 3.1.8";
    qDebug() << "mergePdfWithPdftk(): Disabled in Phase 3.1.8";
    return false;
}

// Extract PDF metadata including outline/bookmarks using pdftk
// Note: This function doesn't use InkCanvas, so it's not disabled
bool MainWindow::extractPdfOutlineData(const QString &pdfPath, QString &outlineData) {
    QProcess process;
    process.start("pdftk", QStringList() << pdfPath << "dump_data");
    
    // Increase timeout to 60 seconds for large PDFs
    if (!process.waitForFinished(60000)) {
        return false;
    }
    
    if (process.exitCode() != 0) {
        return false;
    }
    
    outlineData = QString::fromUtf8(process.readAllStandardOutput());
    return !outlineData.isEmpty();
}

// Filter outline entries to only include those pointing to pages in the export range,
// and adjust page numbers to match the new PDF (0-indexed)
QString MainWindow::filterAndAdjustOutline(const QString &metadataContent, int startPage, int endPage, int pageOffset) {
    QStringList lines = metadataContent.split('\n');
    QStringList filteredLines;
    
    // First, extract and preserve non-bookmark metadata
    QStringList metadataLines;
    bool inMetadata = true;
    
    for (const QString &line : lines) {
        if (line.startsWith("BookmarkBegin")) {
            inMetadata = false;
            break;
        }
        // Collect metadata but skip NumberOfPages (it will be wrong after extraction)
        if (inMetadata && !line.startsWith("NumberOfPages:")) {
            if (line.startsWith("InfoBegin") || line.startsWith("InfoKey:") || line.startsWith("InfoValue:") ||
                line.startsWith("PdfID0:") || line.startsWith("PdfID1:") || 
                line.trimmed().isEmpty()) {
                metadataLines.append(line);
            }
        }
    }
    
    // Add metadata to output
    filteredLines.append(metadataLines);
    
    // Now process bookmarks
    QStringList currentBookmark;
    int bookmarkPage = -1;
    bool inBookmark = false;
    
    // Lambda to process a completed bookmark
    auto processBookmark = [&]() {
        // Only include bookmarks that:
        // 1. Have a valid page number
        // 2. Point to a page within the export range
        if (bookmarkPage > 0 && bookmarkPage >= startPage + 1 && bookmarkPage <= endPage + 1) {
            // Adjust the page number (pdftk uses 1-based indexing)
            int newPageNumber = bookmarkPage - startPage;  // Subtract the offset
            
            // Write out the bookmark with adjusted page number
            for (const QString &bmLine : currentBookmark) {
                if (bmLine.startsWith("BookmarkPageNumber: ")) {
                    filteredLines.append(QString("BookmarkPageNumber: %1").arg(newPageNumber));
                } else {
                    filteredLines.append(bmLine);
                }
            }
        }
    };
    
    // Parse bookmark entries
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        
        // Detect start of a new bookmark
        if (line.startsWith("BookmarkBegin")) {
            // Process the previous bookmark if we were in one
            if (inBookmark) {
                processBookmark();
            }
            
            // Start a new bookmark
            inBookmark = true;
            currentBookmark.clear();
            currentBookmark.append(line);
            bookmarkPage = -1;
        }
        // If we're in a bookmark, collect its lines
        else if (inBookmark) {
            // Check for various bookmark fields
            if (line.startsWith("BookmarkTitle:") || 
                line.startsWith("BookmarkLevel:") || 
                line.startsWith("BookmarkPageNumber:")) {
                
                // Extract page number if this is the PageNumber line
                if (line.startsWith("BookmarkPageNumber: ")) {
                    QString pageStr = line.mid(20).trimmed();
                    bool ok = false;
                    bookmarkPage = pageStr.toInt(&ok);
                    if (!ok) {
                        bookmarkPage = -1; // Invalid page number
                    }
                }
                
                currentBookmark.append(line);
            }
            // Empty line might indicate end of bookmark (some PDFs format this way)
            else if (line.trimmed().isEmpty()) {
                // Don't add empty lines to bookmark, but don't end it yet either
                // We'll end on the next BookmarkBegin
                continue;
            }
        }
    }
    
    // Process the last bookmark if we ended while in one
    if (inBookmark) {
        processBookmark();
    }
    
    return filteredLines.join('\n');
}

// Apply outline metadata to an existing PDF using pdftk update_info
bool MainWindow::applyOutlineToPdf(const QString &pdfPath, const QString &outlineData) {
    if (outlineData.isEmpty()) {
        return true;  // Nothing to apply, not an error
    }
    
    // Create temporary file for metadata
    QString tempMetadataFile = QDir::temp().filePath("speedynote_outline_temp.txt");
    QFile metaFile(tempMetadataFile);
    if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream out(&metaFile);
    out << outlineData;
    metaFile.close();
    
    // Create temporary output file
    QString tempOutputFile = QDir::temp().filePath("speedynote_with_outline_temp.pdf");
    
    // Use pdftk to apply metadata
    QProcess process;
    process.start("pdftk", QStringList() << pdfPath << "update_info" << tempMetadataFile << "output" << tempOutputFile);
    
    bool success = false;
    if (process.waitForFinished(30000) && process.exitCode() == 0) {
        // Replace original with the one that has outline
        QFile::remove(pdfPath);
        if (QFile::copy(tempOutputFile, pdfPath)) {
            QFile::setPermissions(pdfPath, QFile::WriteOwner | QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
            success = true;
        }
    }
    
    // Cleanup
    QFile::remove(tempMetadataFile);
    QFile::remove(tempOutputFile);
    
    return success;
}


void MainWindow::updateZoom() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    if (vp && zoomSlider) {
        vp->setZoomLevel(zoomSlider->value() / 100.0); // Convert slider value to zoom factor
        updatePanRange();
    }
}

qreal MainWindow::getDevicePixelRatio(){
    QScreen *screen = QGuiApplication::primaryScreen();
    qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    QString platformName = QGuiApplication::platformName();
    
    if (platformName == "wayland") {
        // Check if user has set a manual override
        QSettings settings;
        qreal manualScale = settings.value("display/waylandDpiScale", 0.0).toReal();
        
        // If user set a manual value (anything > 0.0), use it
        // 0.0 = Auto, 1.00 = explicit 100%, etc.
        if (manualScale > 0.0) {
            return manualScale;
        }
        
        // Try to derive actual scale factor from physical vs logical DPI
        if (screen) {
            qreal physicalDpi = screen->physicalDotsPerInch();
            qreal logicalDpi = screen->logicalDotsPerInch();
            
            // Try calculating from physical DPI / logical DPI
            if (logicalDpi > 0 && physicalDpi > 0) {
                qreal calculatedScale = physicalDpi / logicalDpi;
                if (qAbs(calculatedScale - 1.0) >= 0.01) {
                    return calculatedScale;
                }
            }
            
            // If that doesn't work, try physical DPI / 96
            if (physicalDpi > 0) {
                qreal calculatedScale = physicalDpi / 96.0;
                if (qAbs(calculatedScale - 1.0) >= 0.01) {
                    return calculatedScale;
                }
            }
        }
        
        return dpr;
    }
    
    // X11/Windows: use standard device pixel ratio
    return dpr;
}

void MainWindow::updatePanRange() {
    // Phase 3.3: OBSOLETE - DocumentViewport handles pan range internally
    // Sliders use fixed range 0-10000 and connect via scroll fractions
    // This function is now a no-op (kept for compatibility with old call sites)
}

void MainWindow::updatePanX(int value) {
    // Phase 3.3: Convert slider value to fraction and apply to viewport
    if (DocumentViewport* vp = currentViewport()) {
        qreal fraction = value / 10000.0;
        vp->setHorizontalScrollFraction(fraction);
    }
}

void MainWindow::updatePanY(int value) {
    // Phase 3.3: Convert slider value to fraction and apply to viewport
    if (DocumentViewport* vp = currentViewport()) {
        qreal fraction = value / 10000.0;
        vp->setVerticalScrollFraction(fraction);
    }
}

void MainWindow::connectViewportScrollSignals(DocumentViewport* viewport) {
    // Phase 3.3: Connect viewport scroll signals to update pan sliders
    // This is called when the current viewport changes (tab switch)
    
    // Disconnect any previous viewport connections
    if (m_hScrollConn) {
        disconnect(m_hScrollConn);
        m_hScrollConn = {};
    }
    if (m_vScrollConn) {
        disconnect(m_vScrollConn);
        m_vScrollConn = {};
    }
    
    // Remove event filter from previous viewport (QPointer auto-nulls if deleted)
    if (m_connectedViewport) {
        m_connectedViewport->removeEventFilter(this);
    }
    m_connectedViewport = nullptr;
    
    if (!viewport) {
        return;
    }
    
    // Install event filter on the new viewport for wheel/tablet event handling
    viewport->installEventFilter(this);
    m_connectedViewport = viewport;  // QPointer tracks lifetime
    
    // Initialize slider values from current viewport state
    // Guard against division by zero (zoomLevel should never be 0, but be safe)
    qreal zoomLevel = viewport->zoomLevel();
    if (zoomLevel <= 0) {
        zoomLevel = 1.0;
    }
    
    QPointF panOffset = viewport->panOffset();
    QSizeF contentSize = viewport->totalContentSize();
    
    qreal viewWidth = viewport->width() / zoomLevel;
    qreal viewHeight = viewport->height() / zoomLevel;
    qreal scrollableWidth = contentSize.width() - viewWidth;
    qreal scrollableHeight = contentSize.height() - viewHeight;
    
    qreal hFraction = (scrollableWidth > 0) ? qBound(0.0, panOffset.x() / scrollableWidth, 1.0) : 0.0;
    qreal vFraction = (scrollableHeight > 0) ? qBound(0.0, panOffset.y() / scrollableHeight, 1.0) : 0.0;
    
    if (panXSlider) {
        panXSlider->blockSignals(true);
        panXSlider->setValue(qRound(hFraction * 10000));
        panXSlider->blockSignals(false);
    }
    if (panYSlider) {
        panYSlider->blockSignals(true);
        panYSlider->setValue(qRound(vFraction * 10000));
        panYSlider->blockSignals(false);
    }
    
    // Connect signals - sliders stay visible (auto-hide deferred to Phase 3.4)
    m_hScrollConn = connect(viewport, &DocumentViewport::horizontalScrollChanged, this, [this](qreal fraction) {
        if (panXSlider) {
            panXSlider->blockSignals(true);
            panXSlider->setValue(qRound(fraction * 10000));
            panXSlider->blockSignals(false);
        }
    });
    
    m_vScrollConn = connect(viewport, &DocumentViewport::verticalScrollChanged, this, [this](qreal fraction) {
        if (panYSlider) {
            panYSlider->blockSignals(true);
            panYSlider->setValue(qRound(fraction * 10000));
            panYSlider->blockSignals(false);
        }
    });
}

void MainWindow::centerViewportContent(int tabIndex) {
    // Phase 3.3: One-time horizontal centering for new tabs
    // Sets initial pan X to a negative value so content appears centered
    // when it's narrower than the viewport.
    //
    // This is called ONCE when a tab is created. User can then pan freely.
    // The DocumentViewport debug overlay will show negative pan X values.
    
    if (!m_tabManager) return;
    
    DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
    if (!viewport) return;
    
    // Get content and viewport dimensions in document units
    QSizeF contentSize = viewport->totalContentSize();
    qreal zoomLevel = viewport->zoomLevel();
    
    // Guard against zero zoom
    if (zoomLevel <= 0) zoomLevel = 1.0;
    
    qreal viewportWidth = viewport->width() / zoomLevel;
    
    // Only center if content is narrower than viewport
    if (contentSize.width() < viewportWidth) {
        // Calculate the offset needed to center content
        // Negative pan X shifts content to the right (toward center)
        qreal centeringOffset = (viewportWidth - contentSize.width()) / 2.0;
        
        // Set initial pan with negative X to center horizontally
        QPointF currentPan = viewport->panOffset();
        viewport->setPanOffset(QPointF(-centeringOffset, currentPan.y()));
        /*
        qDebug() << "centerViewportContent: tabIndex=" << tabIndex
                 << "contentWidth=" << contentSize.width()
                 << "viewportWidth=" << viewportWidth
                 << "centeringOffset=" << centeringOffset
                 << "newPanX=" << -centeringOffset;
        */
    }
}

// ============================================================================
// Phase doc-1: Document Operations
// ============================================================================

void MainWindow::saveDocument()
{
    // Phase doc-1.1: Save current document to JSON file
    // Uses DocumentManager for proper document handling
    // - If document has existing path: save in-place (no dialog)
    // - If new document: show Save As dialog
    
    if (!m_documentManager || !m_tabManager) {
        qWarning() << "saveDocument: DocumentManager or TabManager not initialized";
        return;
    }
    
    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
        QMessageBox::warning(this, tr("Save Document"), 
            tr("No document is open."));
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        QMessageBox::warning(this, tr("Save Document"), 
            tr("No document is open."));
        return;
    }
    
    // Check if document already has a path (previously saved)
    QString existingPath = m_documentManager->documentPath(doc);
    
    if (!existingPath.isEmpty()) {
        // ✅ Document was previously saved - save in-place, no dialog
        if (!m_documentManager->saveDocument(doc)) {
            QMessageBox::critical(this, tr("Save Error"),
                tr("Failed to save document to:\n%1").arg(existingPath));
            return;
        }
        
        // Update tab title (clear modified flag)
        int currentIndex = m_tabManager->currentIndex();
        if (currentIndex >= 0) {
            m_tabManager->markTabModified(currentIndex, false);
        }
        
        qDebug() << "saveDocument: Saved" << doc->pageCount() << "pages to" << existingPath;
        return;
    }
    
    // ✅ New document - show Save As dialog
    QString defaultName = doc->name.isEmpty() ? "Untitled" : doc->name;
    QString defaultPath = QDir::homePath() + "/" + defaultName + ".json";
    
    QString filter = tr("SpeedyNote JSON (*.json);;All Files (*)");
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Document"),
        defaultPath,
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    // Ensure .json extension
    if (!filePath.endsWith(".json", Qt::CaseInsensitive)) {
        filePath += ".json";
    }
    
    // Update document name from file name (without extension)
    QFileInfo fileInfo(filePath);
    doc->name = fileInfo.baseName();
    
    // Use DocumentManager to save (handles all the file I/O and state updates)
    if (!m_documentManager->saveDocumentAs(doc, filePath)) {
        QMessageBox::critical(this, tr("Save Error"),
            tr("Failed to save document to:\n%1").arg(filePath));
        return;
    }
    
    // Update tab title (remove * prefix)
    int currentIndex = m_tabManager->currentIndex();
    if (currentIndex >= 0) {
        m_tabManager->setTabTitle(currentIndex, doc->name);
        m_tabManager->markTabModified(currentIndex, false);
    }
    
    qDebug() << "saveDocument: Saved" << doc->pageCount() << "pages to" << filePath;
}

void MainWindow::loadDocument()
{
    // Phase doc-1.2: Load document from JSON file via file dialog
    // Uses DocumentManager for proper document ownership
    
    if (!m_documentManager || !m_tabManager) {
        qWarning() << "loadDocument: DocumentManager or TabManager not initialized";
        return;
    }
    
    // Open file dialog for file selection
    QString filter = tr("SpeedyNote Files (*.json *.snx);;All Files (*)");
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Document"),
        QDir::homePath(),
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    // Use DocumentManager to load the document (handles ownership, PDF reloading, etc.)
    Document* doc = m_documentManager->loadDocument(filePath);
    if (!doc) {
        QMessageBox::critical(this, tr("Load Error"),
            tr("Failed to load document from:\n%1").arg(filePath));
        return;
    }
    
    // Get document name from file if not set
    if (doc->name.isEmpty()) {
        QFileInfo fileInfo(filePath);
        doc->name = fileInfo.baseName();
    }
    
    // Create new tab with the loaded document
    int tabIndex = m_tabManager->createTab(doc, doc->displayName());
    
    if (tabIndex >= 0) {
        // Center the viewport content
        centerViewportContent(tabIndex);
        
        qDebug() << "loadDocument: Loaded" << doc->pageCount() << "pages from" << filePath;
    }
}

void MainWindow::addPageToDocument()
{
    // Phase doc-1.0: Add new page at end of document
    // Required for multi-page save/load testing
    
    if (!m_tabManager) {
        qDebug() << "addPageToDocument: No tab manager";
        return;
    }
    
    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
        qDebug() << "addPageToDocument: No current viewport";
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        qDebug() << "addPageToDocument: No document in viewport";
        return;
    }
    
    // Add page at end
    Page* newPage = doc->addPage();
    if (newPage) {
        qDebug() << "addPageToDocument: Added page" << doc->pageCount() 
                 << "to document" << doc->name;
        
        // Trigger viewport repaint to show new page in layout
        viewport->update();
        
        // Mark tab as modified
        int currentIndex = m_tabManager->currentIndex();
        if (currentIndex >= 0) {
            m_tabManager->markTabModified(currentIndex, true);
        }
        
        // Optionally scroll to the new page (user can do this manually for now)
        // viewport->scrollToPage(doc->pageCount() - 1);
    }
}

void MainWindow::openPdfDocument()
{
    // Phase doc-1.4: Open PDF file and create PDF-backed document
    // Uses DocumentManager for proper document ownership
    
    if (!m_documentManager || !m_tabManager) {
        qWarning() << "openPdfDocument: DocumentManager or TabManager not initialized";
        return;
    }
    
    // Open file dialog for PDF selection
    QString filter = tr("PDF Files (*.pdf);;All Files (*)");
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open PDF"),
        QDir::homePath(),
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    // Use DocumentManager to load the PDF
    // DocumentManager::loadDocument() handles .pdf extension:
    // - Calls Document::createForPdf(baseName, path)
    // - Takes ownership of the document
    // - Adds to recent documents
    Document* doc = m_documentManager->loadDocument(filePath);
    if (!doc) {
        QMessageBox::critical(this, tr("PDF Error"),
            tr("Failed to open PDF file:\n%1").arg(filePath));
        return;
    }
    
    // Create new tab with the PDF document
    int tabIndex = m_tabManager->createTab(doc, doc->displayName());
    
    if (tabIndex >= 0) {
        // Center the viewport content
        centerViewportContent(tabIndex);
        
        qDebug() << "openPdfDocument: Loaded PDF with" << doc->pageCount() 
                 << "pages from" << filePath;
    } else {
        qWarning() << "openPdfDocument: Failed to create tab for document";
    }
}

void MainWindow::applyZoom() {
    // Phase 3.1.8: Stubbed - DocumentViewport handles zoom via zoomSlider
    // TODO Phase 3.3: Connect to currentViewport()->setZoom() if needed
    qDebug() << "applyZoom(): Not implemented yet (Phase 3.3)";
}

void MainWindow::forceUIRefresh() {
    setWindowState(Qt::WindowNoState);  // Restore first
    setWindowState(Qt::WindowMaximized);  // Maximize again
}

void MainWindow::loadPdf() {
    // Phase doc-1.4: Redirect to openPdfDocument() for PDF loading
    // This stub exists for backward compatibility with menu items
    openPdfDocument();
}

void MainWindow::clearPdf() {
    // Phase 3.1.8: Stubbed - PDF clearing will use DocumentViewport
    // TODO Phase 3.4: Implement PDF clearing for DocumentViewport
    qDebug() << "clearPdf(): Not implemented yet (Phase 3.4)";
}

void MainWindow::handleSmartPdfButton() {
    // Phase 3.1.8: Stubbed - PDF management will use DocumentViewport
    // For now, show a message since loadPdf() is also stubbed
    QMessageBox::information(this, tr("PDF Management"), 
        tr("PDF import/management is being redesigned. Coming soon!"));
}


void MainWindow::switchTab(int index) {
    // Phase 3.1.1: Simplified switchTab using TabManager
    // Many InkCanvas-specific features are stubbed for now
    
    if (!m_tabWidget || !pageInput || !zoomSlider) {
        return;
    }

    if (index >= 0 && index < m_tabWidget->count()) {
        // QTabWidget handles the tab switch internally
        // m_tabWidget->setCurrentIndex(index) is called by QTabWidget's signal
        
        DocumentViewport *viewport = currentViewport();
        if (viewport) {
            // TODO Phase 3.3: Update page spinbox from viewport
            // int currentPage = viewport->currentPageIndex();
            // pageInput->blockSignals(true);
            // pageInput->setValue(currentPage + 1);
            // pageInput->blockSignals(false);
            
            // TODO Phase 3.3: Update zoom from viewport
            // zoomSlider->blockSignals(true);
            // zoomSlider->setValue(viewport->zoomLevel() * 100);
            // zoomSlider->blockSignals(false);
        }
        
        updateDialDisplay();
        // TODO Phase 3.3: Reconnect these update functions to work with DocumentViewport
        // updateColorButtonStates();
        // updateStraightLineButtonState();
        // updateRopeToolButtonState();
        // updatePictureButtonState();
        // updatePdfTextSelectButtonState();
        // updateBookmarkButtonState();
        updateDialButtonState();
        updateFastForwardButtonState();
        // updateToolButtonStates();
        // updateThicknessSliderForCurrentTool();
    }
}

int MainWindow::findTabWithNotebookId(const QString &notebookId) {
    // Phase 3.1.1: Stubbed - will be implemented with DocumentManager
    // TODO Phase 3.5: Use DocumentManager to find document by notebook ID
    if (!m_tabWidget || notebookId.isEmpty()) return -1;
    
    // For now, return -1 (not found) - will be implemented with new save/load system
    return -1;
}

bool MainWindow::switchToExistingNotebook(const QString &spnPath) {
    // Phase 3.1.1: Stubbed - will be implemented with DocumentManager in Phase 3.5
    Q_UNUSED(spnPath);
    // TODO Phase 3.5: Use DocumentManager to check if document is already open
    // For now, always return false (allow opening)
    return false;
}

void MainWindow::addNewTab() {
    // Phase 3.1.1: Simplified addNewTab using DocumentManager and TabManager
    if (!m_tabManager || !m_documentManager) {
        qWarning() << "addNewTab: TabManager or DocumentManager not initialized";
        return;
    }
    
    // Create a new blank document
    Document* doc = m_documentManager->createDocument();
    if (!doc) {
        qWarning() << "addNewTab: Failed to create document";
        return;
    }
    
    // Phase doc-1: Apply user's default background settings from QSettings
    // This ensures new pages use the user's preferred background style
    {
        Page::BackgroundType defaultStyle;
        QColor defaultBgColor;
        QColor defaultGridColor;
        int defaultDensity;
        loadDefaultBackgroundSettings(defaultStyle, defaultBgColor, defaultGridColor, defaultDensity);
        
        // Update document defaults for future pages
        doc->defaultBackgroundType = defaultStyle;
        doc->defaultBackgroundColor = defaultBgColor;
        doc->defaultGridColor = defaultGridColor;
        doc->defaultGridSpacing = defaultDensity;
        doc->defaultLineSpacing = defaultDensity;
        
        // Also apply to the first page (already created by Document::createNew)
        if (doc->pageCount() > 0) {
            Page* firstPage = doc->page(0);
            if (firstPage) {
                firstPage->backgroundType = defaultStyle;
                firstPage->backgroundColor = defaultBgColor;
                firstPage->gridColor = defaultGridColor;
                firstPage->gridSpacing = defaultDensity;
                firstPage->lineSpacing = defaultDensity;
            }
        }
    }
    
    // Create a new tab with DocumentViewport
    QString tabTitle = doc->displayName();
    int tabIndex = m_tabManager->createTab(doc, tabTitle);
    
    qDebug() << "Created new tab at index" << tabIndex << "with document:" << tabTitle;
    
    // Switch to the new tab
    if (m_tabWidget) {
        m_tabWidget->setCurrentIndex(tabIndex);
    }
    
    // Phase 3.3: Center content horizontally (one-time initial offset)
    // Defer to next event loop iteration so viewport has its final size
    QTimer::singleShot(0, this, [this, tabIndex]() {
        centerViewportContent(tabIndex);
    });
    
    updateDialDisplay();
    
    return;
    
    /* ========== OLD INKCANVAS CODE - KEPT FOR REFERENCE ==========
    // This code will be removed in Phase 3.1.7 when InkCanvas is fully disconnected
    
    if (!tabList || !canvasStack) return;  // Ensure tabList and canvasStack exist

    int newTabIndex = tabList->count();  // New tab index
    QWidget *tabWidget = new QWidget();  // Custom tab container
    tabWidget->setObjectName("tabWidget"); // Name the widget for easy retrieval later
    QHBoxLayout *tabLayout = new QHBoxLayout(tabWidget);
    tabLayout->setContentsMargins(5, 2, 5, 2);

    // ✅ Create the label (Tab Name) - adaptive width based on content
    QLabel *tabLabel = new QLabel(QString("Tab %1").arg(newTabIndex + 1), tabWidget);    
    tabLabel->setObjectName("tabLabel"); // ✅ Name the label for easy retrieval later
    tabLabel->setWordWrap(false); // ✅ No wrapping for horizontal tabs
    // Use expanding size policy so label grows with content
    tabLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tabLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // Left-align to show filename start
    tabLabel->setTextFormat(Qt::PlainText); // Ensure plain text for proper eliding
    // Tab label styling will be updated by theme

    // ✅ Create the close button (❌) - styled for browser-like tabs
    QPushButton *closeButton = new QPushButton(tabWidget);
    closeButton->setFixedSize(14, 14); // Smaller to fit narrower tabs
    closeButton->setIcon(loadThemedIcon("cross")); // Set themed icon
    closeButton->setStyleSheet(R"(
        QPushButton { 
            border: none; 
            background: transparent; 
            border-radius: 6px;
            padding: 1px;
        }
        QPushButton:hover { 
            background: rgba(255, 100, 100, 150); 
            border-radius: 6px;
        }
        QPushButton:pressed { 
            background: rgba(255, 50, 50, 200); 
            border-radius: 6px;
        }
    )"); // Themed styling with hover and press effects
    
    // ✅ Create new InkCanvas instance EARLIER so it can be captured by the lambda
    InkCanvas *newCanvas = new InkCanvas(this);
    
    // ✅ Handle tab closing when the button is clicked
    connect(closeButton, &QPushButton::clicked, this, [=]() { // newCanvas is now captured

        // ✅ Prevent multiple executions by disabling the button immediately
        closeButton->setEnabled(false);
        
        // ✅ Safety check: Ensure the canvas still exists and is in the stack
        if (!newCanvas || !canvasStack) {
            qWarning() << "Canvas or canvas stack is null during tab close";
            closeButton->setEnabled(true); // Re-enable on error
            return;
        }

        // ✅ Find the index by directly searching for the canvas in canvasStack
        // This is more reliable than trying to correlate tabList and canvasStack
        int indexToRemove = -1;
        for (int i = 0; i < canvasStack->count(); ++i) {
            if (canvasStack->widget(i) == newCanvas) {
                indexToRemove = i;
                break;
            }
        }

        if (indexToRemove == -1) {
            qWarning() << "Could not find canvas in canvasStack during tab close";
            closeButton->setEnabled(true); // Re-enable on error
            return; // Critical error, cannot proceed.
        }
        
        // ✅ Verify that tabList and canvasStack are in sync
        if (indexToRemove >= tabList->count()) {
            qWarning() << "Tab lists are out of sync! Canvas index:" << indexToRemove << "Tab count:" << tabList->count();
            closeButton->setEnabled(true); // Re-enable on error
                return;
            }
        
        // At this point, newCanvas is the InkCanvas instance for the tab being closed.
        // And indexToRemove is its index in tabList and canvasStack.

        // ✅ Auto-save the current page before closing the tab
        if (newCanvas && newCanvas->isEdited()) {
            int pageNumber = getCurrentPageForCanvas(newCanvas);
            newCanvas->saveToFile(pageNumber);
            
            // ✅ COMBINED MODE FIX: Use combined-aware save for markdown/picture windows
            newCanvas->saveCombinedWindowsForPage(pageNumber);
            
            // ✅ Mark as not edited to prevent double-saving in destructor
            newCanvas->setEdited(false);
        }
        
        // ✅ Save the last accessed page and bookmarks before closing tab
        if (newCanvas) {
            // ✅ Additional safety check before accessing canvas methods
            try {
                int currentPage = getCurrentPageForCanvas(newCanvas);
                newCanvas->setLastAccessedPage(currentPage);
                
                // ✅ Save current bookmarks to JSON metadata
                saveBookmarks();
            } catch (...) {
                qWarning() << "Exception occurred while saving last accessed page";
            }
        }

        // ✅ 1. PRIORITY: Handle saving first - user can cancel here
        if (!ensureTabHasUniqueSaveFolder(newCanvas)) {
            closeButton->setEnabled(true); // Re-enable on cancellation
            return; // User cancelled saving, don't close tab
        }

        // ✅ 2. ONLY AFTER SAVING: Check if it's the last remaining tab
        if (tabList->count() <= 1) {
            QMessageBox::information(this, tr("Notice"), tr("At least one tab must remain open."));
            closeButton->setEnabled(true); // Re-enable if can't close last tab
            return;
        }

        // 2. Get the final save folder path and update recent notebooks EARLY
        QString folderPath = newCanvas->getSaveFolder();
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/temp_session";

        // 3. EARLY: Update cover preview and recent list BEFORE any UI changes
        if (!folderPath.isEmpty() && folderPath != tempDir && recentNotebooksManager) {
            // Force canvas to update and render current state before thumbnail generation
            newCanvas->update();
            newCanvas->repaint();
            QApplication::processEvents(); // Ensure all pending updates are processed
            
            // Generate thumbnail IMMEDIATELY while canvas is guaranteed to be valid
            recentNotebooksManager->generateAndSaveCoverPreview(folderPath, newCanvas);
            // Add/update in recent list. This also moves it to the top.
            recentNotebooksManager->addRecentNotebook(folderPath, newCanvas);
            // Refresh shared launcher if it exists (but only if visible to avoid issues)
            if (sharedLauncher && sharedLauncher->isVisible()) {
                sharedLauncher->refreshRecentNotebooks();
            }
        }
        
        // 4. Update the tab's label directly as folderPath might have changed
        QLabel *label = tabWidget->findChild<QLabel*>("tabLabel");
        if (label) {
            QString tabNameText;
            if (!folderPath.isEmpty() && folderPath != tempDir) { // Only for permanent notebooks
                QString metadataFile = folderPath + "/.pdf_path.txt";
                if (QFile::exists(metadataFile)) {
                    QFile file(metadataFile);
                    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QTextStream in(&file);
                        QString pdfPath = in.readLine().trimmed();
                        file.close();
                        if (QFile::exists(pdfPath)) { // Check if PDF file actually exists
                            tabNameText = QFileInfo(pdfPath).fileName(); // Use full filename, adaptive width handles display
                        }
                    }
                }
                // Fallback to folder name if no PDF or PDF path invalid
                if (tabNameText.isEmpty()) {
                    tabNameText = QFileInfo(folderPath).fileName(); // Use full filename, adaptive width handles display
                }
            }
            // Only update the label if a new valid name was determined.
            // If it's still a temp folder, the original "Tab X" label remains appropriate.
            if (!tabNameText.isEmpty()) {
                label->setText(tabNameText);
            }
        }

        // 5. Remove the tab
        removeTabAt(indexToRemove);
    });


    // ✅ Add widgets to the tab layout
    tabLayout->addWidget(tabLabel);
    tabLayout->addWidget(closeButton);
    tabLayout->setStretch(0, 1);
    tabLayout->setStretch(1, 0);
    
    // ✅ Create the tab item and set widget (horizontal layout)
    QListWidgetItem *tabItem = new QListWidgetItem();
    tabItem->setSizeHint(QSize(135, 32)); // Initial size, will be updated adaptively
    tabList->addItem(tabItem);
    tabList->setItemWidget(tabItem, tabWidget);  // Attach tab layout
    
    // Update tab sizes to adapt to new tab
    QTimer::singleShot(0, this, [this]() {
        updateTabSizes();
    });

    canvasStack->addWidget(newCanvas);

    // ✅ Connect touch gesture signals
    connect(newCanvas, &InkCanvas::zoomChanged, this, &MainWindow::handleTouchZoomChange);
    connect(newCanvas, &InkCanvas::panChanged, this, &MainWindow::handleTouchPanChange);
    connect(newCanvas, &InkCanvas::touchGestureEnded, this, &MainWindow::handleTouchGestureEnd);
    connect(newCanvas, &InkCanvas::touchPanningChanged, this, &MainWindow::handleTouchPanningChanged);
    connect(newCanvas, &InkCanvas::ropeSelectionCompleted, this, &MainWindow::showRopeSelectionMenu);
    connect(newCanvas, &InkCanvas::pdfLinkClicked, this, [this](int targetPage) {
        // Navigate to the target page when a PDF link is clicked
        if (targetPage >= 0 && targetPage < 9999) {
            switchPageWithDirection(targetPage + 1, (targetPage + 1 > getCurrentPageForCanvas(currentCanvas()) + 1) ? 1 : -1);
            pageInput->setValue(targetPage + 1);
        }
    });
    connect(newCanvas, &InkCanvas::pdfLoaded, this, [this]() {
        // Refresh PDF outline if sidebar is visible
        if (outlineSidebarVisible) {
            loadPdfOutline();
        }
    });
    connect(newCanvas, &InkCanvas::autoScrollRequested, this, &MainWindow::onAutoScrollRequested);
    connect(newCanvas, &InkCanvas::earlySaveRequested, this, &MainWindow::onEarlySaveRequested);
    connect(newCanvas, &InkCanvas::markdownNotesUpdated, this, &MainWindow::onMarkdownNotesUpdated);
    connect(newCanvas, &InkCanvas::highlightDoubleClicked, this, &MainWindow::onHighlightDoubleClicked);
    connect(newCanvas, &InkCanvas::pdfTextSelectionCleared, this, &MainWindow::onPdfTextSelectionCleared);
    
    // Install event filter to detect mouse movement for scrollbar visibility
    newCanvas->setMouseTracking(true);
    newCanvas->installEventFilter(this);
    
    // Disable tablet tracking for canvases for now to prevent crashes
    // TODO: Find a safer way to implement hover tooltips without tablet tracking
    // QTimer::singleShot(50, this, [newCanvas]() {
    //     try {
    //         if (newCanvas && newCanvas->window() && newCanvas->window()->windowHandle()) {
    //             newCanvas->setAttribute(Qt::WA_TabletTracking, true);
    //         }
    //     } catch (...) {
    //         // Silently ignore tablet tracking errors
    //     }
    // });
    
    // ✅ Apply touch gesture setting
    newCanvas->setTouchGestureMode(touchGestureMode);

    pageMap[newCanvas] = 0;

    // ✅ Select the new tab
    tabList->setCurrentItem(tabItem);
    canvasStack->setCurrentWidget(newCanvas);

    zoomSlider->setValue(qRound(100.0 / initialDpr)); // Set initial zoom level based on DPR
    updateDialDisplay();
    updateStraightLineButtonState();  // Initialize straight line button state for the new tab
    updateRopeToolButtonState(); // Initialize rope tool button state for the new tab
    updatePdfTextSelectButtonState(); // Initialize PDF text selection button state for the new tab
    updateBookmarkButtonState(); // Initialize bookmark button state for the new tab
    updatePictureButtonState(); // Initialize picture button state for the new tab
    updateDialButtonState();     // Initialize dial button state for the new tab
    updateFastForwardButtonState(); // Initialize fast forward button state for the new tab
    updateToolButtonStates();   // Initialize tool button states for the new tab

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/temp_session";
    newCanvas->setSaveFolder(tempDir);
    
    // Load persistent background settings
    BackgroundStyle defaultStyle;
    QColor defaultColor;
    int defaultDensity;
    loadDefaultBackgroundSettings(defaultStyle, defaultColor, defaultDensity);
    
    newCanvas->setBackgroundStyle(defaultStyle);
    newCanvas->setBackgroundColor(defaultColor);
    newCanvas->setBackgroundDensity(defaultDensity);
    
    // ✅ New tabs start without PDFs, so disable scroll on top initially
    // It will be automatically enabled when a PDF is loaded
    setScrollOnTopEnabled(false);
    newCanvas->setPDFRenderDPI(getPdfDPI());
    
    // Update color button states for the new tab
    updateColorButtonStates();
    ========== END OLD INKCANVAS CODE ==========*/
}
void MainWindow::removeTabAt(int index) {
    // Phase 3.1.2: Use TabManager to remove tabs
    if (m_tabManager) {
        m_tabManager->closeTab(index);
    }
}

bool MainWindow::ensureTabHasUniqueSaveFolder(InkCanvas* canvas) {
    // Phase 3.1.2: Stubbed - will use new save logic in Phase 3.5
    Q_UNUSED(canvas);
    // TODO Phase 3.5: Implement save-before-close logic with DocumentManager
    return true; // Allow closure for now
}



InkCanvas* MainWindow::currentCanvas() {
    // WILL BE REMOVED Phase 3.1.7 - Use currentViewport() instead
    // For now, this returns nullptr since canvasStack is replaced with m_tabWidget
    // which contains DocumentViewports, not InkCanvases
    return nullptr;
}

// Phase 3.1.4: New accessor for DocumentViewport
DocumentViewport* MainWindow::currentViewport() const {
    if (m_tabManager) {
        return m_tabManager->currentViewport();
    }
    return nullptr;
}


void MainWindow::updateTabLabel() {
    // Phase 3.1.2: TabManager handles tab labels via QTabWidget
    // TODO Phase 3.3: Connect to Document displayName changes
    qDebug() << "updateTabLabel(): Using TabManager (Phase 3.3)";
}

int MainWindow::getCurrentPageForCanvas(InkCanvas *canvas) {
    // Phase 3.1.6: Stubbed - use DocumentViewport::currentPageIndex() instead
    Q_UNUSED(canvas);
    // TODO Phase 3.3: Remove this method entirely
    return 0;
}

void MainWindow::toggleZoomSlider() {
    if (zoomFrame->isVisible()) {
        zoomFrame->hide();
        return;
    }

    // ✅ Set as a standalone pop-up window so it can receive events
    zoomFrame->setWindowFlags(Qt::Popup);

    // ✅ Position it right below the button
    QPoint buttonPos = zoomButton->mapToGlobal(QPoint(0, zoomButton->height()));
    zoomFrame->move(buttonPos.x(), buttonPos.y() + 5);
    zoomFrame->show();
}

void MainWindow::toggleThicknessSlider() {
    if (thicknessFrame->isVisible()) {
        thicknessFrame->hide();
        return;
    }

    // ✅ Set as a standalone pop-up window so it can receive events
    thicknessFrame->setWindowFlags(Qt::Popup);

    // ✅ Position it right below the button
    QPoint buttonPos = thicknessButton->mapToGlobal(QPoint(0, thicknessButton->height()));
    thicknessFrame->move(buttonPos.x(), buttonPos.y() + 5);

    thicknessFrame->show();
}


void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();  // Exit fullscreen mode
    } else {
        showFullScreen();  // Enter fullscreen mode
    }
}

void MainWindow::showJumpToPageDialog() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
    
    bool ok;
    int newPage = QInputDialog::getInt(this, "Jump to Page", "Enter Page Number:", 
                                       currentPage, 1, 9999, 1, &ok);
    if (ok) {
        // ✅ Use direction-aware page switching for jump-to-page
        int direction = (newPage > currentPage) ? 1 : (newPage < currentPage) ? -1 : 0;
        if (direction != 0) {
            switchPageWithDirection(newPage, direction);
        } else {
            switchPage(newPage); // Same page, no direction needed
        }
        pageInput->setValue(newPage);
    }
}

void MainWindow::goToPreviousPage() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
    if (currentPage > 1) {
        int newPage = currentPage - 1;
        switchPageWithDirection(newPage, -1); // -1 indicates backward
        pageInput->blockSignals(true);
        pageInput->setValue(newPage);
        pageInput->blockSignals(false);
    }
}

void MainWindow::goToNextPage() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
    int newPage = currentPage + 1;
    switchPageWithDirection(newPage, 1); // 1 indicates forward
    pageInput->blockSignals(true);
    pageInput->setValue(newPage);
    pageInput->blockSignals(false);
}

void MainWindow::onPageInputChanged(int newPage) {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
    
    // ✅ Use direction-aware page switching for spinbox
    int direction = (newPage > currentPage) ? 1 : (newPage < currentPage) ? -1 : 0;
    if (direction != 0) {
        switchPageWithDirection(newPage, direction);
    } else {
        switchPage(newPage); // Same page, no direction needed
    }
}

void MainWindow::toggleDial() {
    if (!dialContainer) {  
        // ✅ Create floating container for the dial
        dialContainer = new QWidget(this);
        dialContainer->setObjectName("dialContainer");
        dialContainer->setFixedSize(140, 140);
        dialContainer->setAttribute(Qt::WA_TranslucentBackground);
        dialContainer->setAttribute(Qt::WA_NoSystemBackground);
        dialContainer->setAttribute(Qt::WA_OpaquePaintEvent);
        dialContainer->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        dialContainer->setStyleSheet("background: transparent; border-radius: 100px;");  // ✅ More transparent

        // ✅ Create dial
        pageDial = new QDial(dialContainer);
        pageDial->setFixedSize(140, 140);
        pageDial->setMinimum(0);
        pageDial->setMaximum(360);
        pageDial->setWrapping(true);  // ✅ Allow full-circle rotation
        
        // Apply theme color immediately when dial is created
        QColor accentColor = getAccentColor();
        pageDial->setStyleSheet(QString(R"(
        QDial {
            background-color: %1;
            }
        )").arg(accentColor.name()));

        /*

        modeDial = new QDial(dialContainer);
        modeDial->setFixedSize(150, 150);
        modeDial->setMinimum(0);
        modeDial->setMaximum(300);  // 6 modes, 60° each
        modeDial->setSingleStep(60);
        modeDial->setWrapping(true);
        modeDial->setStyleSheet("background:rgb(0, 76, 147);");
        modeDial->move(25, 25);
        
        */
        

        dialColorPreview = new QFrame(dialContainer);
        dialColorPreview->setFixedSize(30, 30);
        dialColorPreview->setStyleSheet("border-radius: 15px; border: 1px solid black;");
        dialColorPreview->move(55, 35); // Center of dial

        dialIconView = new QLabel(dialContainer);
        dialIconView->setFixedSize(30, 30);
        dialIconView->setStyleSheet("border-radius: 1px; border: 1px solid black;");
        dialIconView->move(55, 35); // Center of dial

        // ✅ Position dial near top-right corner initially
        positionDialContainer();

        dialDisplay = new QLabel(dialContainer);
        dialDisplay->setAlignment(Qt::AlignCenter);
        dialDisplay->setFixedSize(80, 80);
        dialDisplay->move(30, 30);  // Center position inside the dial
        

        int fontId = QFontDatabase::addApplicationFont(":/resources/fonts/Jersey20-Regular.ttf");
        // int chnFontId = QFontDatabase::addApplicationFont(":/resources/fonts/NotoSansSC-Medium.ttf");
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);

        if (!fontFamilies.isEmpty()) {
            QFont pixelFont(fontFamilies.at(0), 11);
            dialDisplay->setFont(pixelFont);
        }

        dialDisplay->setStyleSheet("background-color: black; color: white; font-size: 14px; border-radius: 4px;");

        dialHiddenButton = new QPushButton(dialContainer);
        dialHiddenButton->setFixedSize(80, 80);
        dialHiddenButton->move(30, 30); // Same position as dialDisplay
        dialHiddenButton->setStyleSheet("background: transparent; border: none;"); // ✅ Fully invisible
        dialHiddenButton->setFocusPolicy(Qt::NoFocus); // ✅ Prevents accidental focus issues
        dialHiddenButton->setEnabled(false);  // ✅ Disabled by default

        // ✅ Connection will be set in changeDialMode() based on current mode

        dialColorPreview->raise();  // ✅ Ensure it's on top
        dialIconView->raise();
        // ✅ Connect dial input and release
        // connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleDialInput);
        // connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onDialReleased);

        // connect(modeDial, &QDial::valueChanged, this, &MainWindow::handleModeSelection);
        changeDialMode(currentDialMode);  // ✅ Set initial mode

        // ✅ Enable drag detection
        dialContainer->installEventFilter(this);
    }

    // ✅ Ensure that `dialContainer` is always initialized before setting visibility
    if (dialContainer) {
        dialContainer->setVisible(!dialContainer->isVisible());
    }

    initializeDialSound();  // ✅ Ensure sound is loaded

    // Inside toggleDial():
    
    if (!dialDisplay) {
        dialDisplay = new QLabel(dialContainer);
    }
    updateDialDisplay(); // ✅ Ensure it's updated before showing

    if (controllerManager) {
        connect(controllerManager, &SDLControllerManager::buttonHeld, this, &MainWindow::handleButtonHeld);
        connect(controllerManager, &SDLControllerManager::buttonReleased, this, &MainWindow::handleButtonReleased);
        connect(controllerManager, &SDLControllerManager::leftStickAngleChanged, pageDial, &QDial::setValue);
        connect(controllerManager, &SDLControllerManager::leftStickReleased, pageDial, &QDial::sliderReleased);
        connect(controllerManager, &SDLControllerManager::buttonSinglePress, this, &MainWindow::handleControllerButton);
    }

    loadButtonMappings();  // ✅ Load button mappings for the controller
    loadMouseDialMappings(); // ✅ Load mouse dial mappings

    // Update button state to reflect dial visibility
    updateDialButtonState();
}

void MainWindow::positionDialContainer() {
    if (!dialContainer) return;
    
    // Get window dimensions
    int windowWidth = width();
    int windowHeight = height();
    
    // Get dial dimensions
    int dialWidth = dialContainer->width();   // 140px
    int dialHeight = dialContainer->height(); // 140px
    
    // Calculate heights based on current layout (tab bar is now above toolbar)
    int tabBarHeight = (tabBarContainer && tabBarContainer->isVisible()) ? 38 : 0; // Tab bar height
    int toolbarHeight = isToolbarTwoRows ? 80 : 50; // Toolbar height

    // Define margins from edges
    int rightMargin = 20;  // Distance from right edge
    int topMargin = 20;    // Distance from top edge (below tab bar and toolbar)
    
    // Calculate total width of visible right-side sidebars
    int rightSidebarWidth = 0;
    // Tab overlays canvas, so only count the panel width when expanded
    if (dialToolbarExpanded && dialToolbar && dialToolbar->isVisible()) {
        rightSidebarWidth += dialToolbar->width(); // 50px panel
    }
    if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
        rightSidebarWidth += markdownNotesSidebar->width();
    }
    
    // Calculate total width of visible left-side sidebars
    int leftSidebarWidth = 0;
    if (outlineSidebar && outlineSidebar->isVisible()) {
        leftSidebarWidth += outlineSidebar->width();
    }
    if (bookmarksSidebar && bookmarksSidebar->isVisible()) {
        leftSidebarWidth += bookmarksSidebar->width();
    }

    // Calculate ideal position (top-right corner with margins, accounting for sidebars)
    int idealX = windowWidth - dialWidth - rightMargin - rightSidebarWidth;
    int idealY = tabBarHeight + toolbarHeight + topMargin;
    
    // Ensure dial stays within window bounds with minimum margins
    int minMargin = 10;
    int maxX = windowWidth - dialWidth - minMargin - rightSidebarWidth;
    int maxY = windowHeight - dialHeight - minMargin;
    
    // Clamp position to stay within bounds (accounting for left sidebars too)
    int finalX = qBound(leftSidebarWidth + minMargin, idealX, maxX);
    int finalY = qBound(tabBarHeight + toolbarHeight + minMargin, idealY, maxY);
    
    // Move the dial to the calculated position
    dialContainer->move(finalX, finalY);
}

void MainWindow::positionDialToolbarTab() {
    if (!dialToolbarToggle) return;
    
    // Calculate position: tab should be at the left edge of dialToolbar (or right edge of canvas if collapsed)
    int windowWidth = width();
    int tabWidth = dialToolbarToggle->width();  // 20px
    
    // Calculate heights for vertical positioning
    int tabBarHeight = (tabBarContainer && tabBarContainer->isVisible()) ? 38 : 0;
    int toolbarHeight = isToolbarTwoRows ? 80 : 50;
    int topOffset = tabBarHeight + toolbarHeight + 60; // 60px margin from top of content area
    
    // Calculate x position based on visible right sidebars
    int rightOffset = 0;
    if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
        rightOffset += markdownNotesSidebar->width();
    }
    if (dialToolbarExpanded && dialToolbar && dialToolbar->isVisible()) {
        rightOffset += dialToolbar->width(); // 50px
    }
    
    // Position the tab: right edge minus sidebars minus tab width
    int tabX = windowWidth - rightOffset - tabWidth;
    int tabY = topOffset;
    
    dialToolbarToggle->move(tabX, tabY);
    dialToolbarToggle->raise(); // Ensure it stays on top
}

void MainWindow::positionLeftSidebarTabs() {
    // Calculate heights for vertical positioning
    int tabBarHeight = (tabBarContainer && tabBarContainer->isVisible()) ? 38 : 0;
    int toolbarHeight = isToolbarTwoRows ? 80 : 50;
    int topOffset = tabBarHeight + toolbarHeight + 60; // 60px margin from top of content area
    int tabSpacing = 10; // Space between tabs
    
    // Calculate x position based on visible left sidebars
    int leftOffset = 0;
    if (outlineSidebarVisible && outlineSidebar && outlineSidebar->isVisible()) {
        leftOffset += outlineSidebar->width();
    }
    if (bookmarksSidebarVisible && bookmarksSidebar && bookmarksSidebar->isVisible()) {
        leftOffset += bookmarksSidebar->width();
    }
    
    // Position outline tab
    if (toggleOutlineButton) {
        int tabX = leftOffset; // At the right edge of visible sidebars
        int tabY = topOffset;
        toggleOutlineButton->move(tabX, tabY);
        toggleOutlineButton->raise();
    }
    
    // Position bookmarks tab below outline tab
    if (toggleBookmarksButton) {
        int tabX = leftOffset;
        int tabY = topOffset + 80 + tabSpacing; // Below outline tab
        toggleBookmarksButton->move(tabX, tabY);
        toggleBookmarksButton->raise();
    }
}

void MainWindow::updateDialDisplay() {
    if (!dialDisplay) return;
    if (!dialColorPreview) return;
    if (!dialIconView) return;
    
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    if (!vp) {
        dialDisplay->setText(tr("\n\nNo Canvas"));
        return;
    }
    
    dialIconView->show();
    switch (currentDialMode) {
        case DialMode::PageSwitching:
            if (fastForwardMode){
                dialDisplay->setText(QString(tr("\n\nPage\n%1").arg(vp->currentPageIndex() + 1 + tempClicks * 8)));
            }
            else {
                dialDisplay->setText(QString(tr("\n\nPage\n%1").arg(vp->currentPageIndex() + 1 + tempClicks)));
            }
            dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/bookpage_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            break;
        case DialMode::ThicknessControl:
            {
                QString toolName;
                switch (vp->currentTool()) {
                    case ToolType::Pen:
                        toolName = tr("Pen");
            break;
                    case ToolType::Marker:
                        toolName = tr("Marker");
            break;
                    case ToolType::Eraser:
                        toolName = tr("Eraser");
                    break;
                    case ToolType::Highlighter:
                        toolName = tr("Highlighter");
                        break;
                    case ToolType::Lasso:
                        toolName = tr("Lasso");
                        break;
            }
                dialDisplay->setText(QString(tr("\n\n%1\n%2").arg(toolName).arg(QString::number(vp->penThickness(), 'f', 1))));
            dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/thickness_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            break;  
        case DialMode::ZoomControl:
            dialDisplay->setText(QString(tr("\n\nZoom\n%1%").arg(vp ? qRound(vp->zoomLevel() * 100.0 * initialDpr) : qRound(zoomSlider->value() * initialDpr))));
            dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/zoom_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            break;
  
        case DialMode::ToolSwitching:
            // ✅ Convert ToolType to QString for display
            switch (vp->currentTool()) {
                case ToolType::Pen:
                    dialDisplay->setText(tr("\n\n\nPen"));
                    dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/pen_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
                case ToolType::Marker:
                    dialDisplay->setText(tr("\n\n\nMarker"));
                    dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/marker_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
                case ToolType::Eraser:
                    dialDisplay->setText(tr("\n\n\nEraser"));
                    dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/eraser_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
                case ToolType::Highlighter:
                    dialDisplay->setText(tr("\n\n\nHighlighter"));
                    dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/marker_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
                case ToolType::Lasso:
                    dialDisplay->setText(tr("\n\n\nLasso"));
                    dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/pen_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
            }
            break;
        case PresetSelection:
            dialColorPreview->show();
            dialIconView->hide();
            dialColorPreview->setStyleSheet(QString("background-color: %1; border-radius: 15px; border: 1px solid black;")
                                            .arg(colorPresets[currentPresetIndex].name()));
            dialDisplay->setText(QString(tr("\n\nPreset %1\n#%2"))
                                            .arg(currentPresetIndex + 1)  // ✅ Display preset index (1-based)
                                            .arg(colorPresets[currentPresetIndex].name().remove("#"))); // ✅ Display hex color
            // dialIconView->setPixmap(QPixmap(":/resources/reversed_icons/preset_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            break;
        case DialMode::PanAndPageScroll:
            {
                dialIconView->setPixmap(QPixmap(":/resources/icons/scroll_reversed.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                QString fullscreenStatus = controlBarVisible ? tr("Etr") : tr("Exit");
                int pageNum = vp ? vp->currentPageIndex() + 1 : 1;
                dialDisplay->setText(QString(tr("\n\nPage %1\n%2 FulScr")).arg(pageNum).arg(fullscreenStatus));
            }
            break;
        case None:
            // No dial mode active, do nothing
            break;
    }
}

/*

void MainWindow::handleModeSelection(int angle) {
    static int lastModeIndex = -1;  // ✅ Store last mode index

    // ✅ Snap to closest fixed 60° step
    int snappedAngle = (angle + 30) / 60 * 60;  // Round to nearest 60°
    int modeIndex = snappedAngle / 60;  // Convert to mode index

    if (modeIndex >= 6) modeIndex = 0;  // ✅ Wrap around (if 360°, reset to 0° mode)

    if (modeIndex != lastModeIndex) {  // ✅ Only switch mode if it's different
        changeDialMode(static_cast<DialMode>(modeIndex));

        // ✅ Play click sound when snapping to new mode
        if (dialClickSound) {
            dialClickSound->play();
        }

        lastModeIndex = modeIndex;  // ✅ Update last mode
    }
}

*/



void MainWindow::handleDialInput(int angle) {
    if (!tracking) {
        startAngle = angle;  // ✅ Set initial position
        accumulatedRotation = 0;  // ✅ Reset tracking
        tracking = true;
        lastAngle = angle;
        return;
    }

    int delta = angle - lastAngle;

    // ✅ Handle 360-degree wrapping
    if (delta > 180) delta -= 360;  // Example: 350° → 10° should be -20° instead of +340°
    if (delta < -180) delta += 360; // Example: 10° → 350° should be +20° instead of -340°

    accumulatedRotation += delta;  // ✅ Accumulate movement

    // ✅ Detect crossing a 45-degree boundary
    int currentClicks = accumulatedRotation / 45; // Total number of "clicks" crossed
    int previousClicks = (accumulatedRotation - delta) / 45; // Previous click count

    if (currentClicks != previousClicks) {  // ✅ Play sound if a new boundary is crossed
        
        if (dialClickSound) {
            dialClickSound->play();
    
            // ✅ Vibrate controller
            SDL_Joystick *joystick = controllerManager->getJoystick();
            if (joystick) {
                // Note: SDL_JoystickRumble requires SDL 2.0.9+
                // For older versions, this will be a no-op
                #if SDL_VERSION_ATLEAST(2, 0, 9)
                SDL_JoystickRumble(joystick, 0xA000, 0xF000, 10);  // Vibrate shortly
                #endif
            }
    
            grossTotalClicks += 1;
            tempClicks = currentClicks;
            updateDialDisplay();
    
            // Phase 3.1.8: PDF preview stubbed - DocumentViewport handles its own rendering
            // TODO Phase 3.4: Implement low-res preview for DocumentViewport if needed
        }
    }

    lastAngle = angle;  // ✅ Store last position
}



void MainWindow::onDialReleased() {
    if (!tracking) return;  // ✅ Ignore if no tracking

    int pagesToAdvance = fastForwardMode ? 8 : 1;
    int totalClicks = accumulatedRotation / 45;  // ✅ Convert degrees to pages

    /*
    int leftOver = accumulatedRotation % 45;  // ✅ Track remaining rotation
    if (leftOver > 22 && totalClicks >= 0) {
        totalClicks += 1;  // ✅ Round up if more than halfway
    } 
    else if (leftOver <= -22 && totalClicks >= 0) {
        totalClicks -= 1;  // ✅ Round down if more than halfway
    }
    */
    

    if (totalClicks != 0 || grossTotalClicks != 0) {  // ✅ Only switch pages if movement happened
        // Phase 3.1.8: Autosave stubbed - DocumentViewport handles undo/redo, not file saving
        
        DocumentViewport* vp = currentViewport();
        int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
        int newPage = qBound(1, currentPage + totalClicks * pagesToAdvance, 99999);
        
        // ✅ Use direction-aware page switching for dial
        int direction = (totalClicks * pagesToAdvance > 0) ? 1 : -1;
        switchPageWithDirection(newPage, direction);
        pageInput->setValue(newPage);
        tempClicks = 0;
        updateDialDisplay(); 
        /*
        if (dialClickSound) {
            dialClickSound->play();
        }
        */
    }

    accumulatedRotation = 0;  // ✅ Reset tracking
    grossTotalClicks = 0;
    tracking = false;
}


void MainWindow::handleToolSelection(int angle) {
    static int lastToolIndex = -1;  // ✅ Track last tool index

    // ✅ Snap to closest fixed 120° step
    int snappedAngle = (angle + 60) / 120 * 120;  // Round to nearest 120°
    int toolIndex = snappedAngle / 120;  // Convert to tool index (0, 1, 2)

    if (toolIndex >= 3) toolIndex = 0;  // ✅ Wrap around at 360° → Back to Pen (0)

    if (toolIndex != lastToolIndex) {  // ✅ Only switch if tool actually changes
        toolSelector->setCurrentIndex(toolIndex);  // ✅ Change tool
        lastToolIndex = toolIndex;  // ✅ Update last selected tool

        // ✅ Play click sound when tool changes
        if (dialClickSound) {
            dialClickSound->play();
        }

        SDL_Joystick *joystick = controllerManager->getJoystick();

        if (joystick) {
            #if SDL_VERSION_ATLEAST(2, 0, 9)
            SDL_JoystickRumble(joystick, 0xA000, 0xF000, 20);  // ✅ Vibrate controller
            #endif
        }

        updateToolButtonStates();  // ✅ Update tool button states
        updateDialDisplay();  // ✅ Update dial display]
    }
}

void MainWindow::onToolReleased() {
    
}
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    static bool dragging = false;
    static QPoint lastMousePos;
    static QTimer *longPressTimer = nullptr;

    // Handle IME focus events for text input widgets
    QLineEdit *lineEdit = qobject_cast<QLineEdit*>(obj);
    if (lineEdit) {
        if (event->type() == QEvent::FocusIn) {
            // Ensure IME is enabled when text field gets focus
            lineEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
            QInputMethod *inputMethod = QGuiApplication::inputMethod();
            if (inputMethod) {
                inputMethod->show();
            }
        }
        else if (event->type() == QEvent::FocusOut) {
            // Keep IME available but reset state
            QInputMethod *inputMethod = QGuiApplication::inputMethod();
            if (inputMethod) {
                inputMethod->reset();
            }
        }
    }

    // Handle resize events for canvas container
    // Phase 3.1: Use m_tabWidget instead of canvasStack
    QWidget *container = m_tabWidget ? m_tabWidget->parentWidget() : nullptr;
    if (obj == container && event->type() == QEvent::Resize) {
        updateScrollbarPositions();
        return false; // Let the event propagate
    }

    // Handle scrollbar visibility
    if (obj == panXSlider || obj == panYSlider) {
        if (event->type() == QEvent::Enter) {
            // Mouse entered scrollbar area
            if (scrollbarHideTimer->isActive()) {
                scrollbarHideTimer->stop();
            }
            return false;
        } 
        else if (event->type() == QEvent::Leave) {
            // Mouse left scrollbar area - start timer to hide
            if (!scrollbarHideTimer->isActive()) {
                scrollbarHideTimer->start();
            }
            return false;
        }
    }

    // Phase 3.1.8: InkCanvas event filtering disabled - DocumentViewport handles its own events
    // Check if this is a viewport event for scrollbar handling
    DocumentViewport* viewport = qobject_cast<DocumentViewport*>(obj);
    if (viewport) {
        // Handle mouse movement for scrollbar visibility
        if (event->type() == QEvent::MouseMove) {
            // QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            // TODO Phase 3.3: Implement edge proximity for scrollbar visibility
        }
        // Handle tablet events for stylus hover (safely)
        else if (event->type() == QEvent::TabletMove) {
            // TODO Phase 3.3: Implement tablet hover handling
        }
        // Handle tablet press/release for stylus button mapping
        else if (event->type() == QEvent::TabletPress) {
            QTabletEvent* tabletEvent = static_cast<QTabletEvent*>(event);
#ifdef Q_OS_LINUX
            onStylusProximityEnter(); // Treat press as "stylus is active" for palm rejection
#endif
            // Handle stylus side button press (not just tip)
            Qt::MouseButtons buttons = tabletEvent->buttons();
            if ((buttons & Qt::MiddleButton) || (buttons & Qt::RightButton)) {
                handleStylusButtonPress(buttons);
            }
        }
        else if (event->type() == QEvent::TabletRelease) {
            QTabletEvent* tabletEvent = static_cast<QTabletEvent*>(event);
#ifdef Q_OS_LINUX
            onStylusProximityLeave(); // Treat release as "stylus may be leaving" for palm rejection
#endif
            // Handle stylus side button release
            Qt::MouseButton releasedButton = tabletEvent->button();
            Qt::MouseButtons remainingButtons = tabletEvent->buttons();
            if (releasedButton == Qt::MiddleButton || releasedButton == Qt::RightButton ||
                stylusButtonAActive || stylusButtonBActive) {
                handleStylusButtonRelease(remainingButtons, releasedButton);
            }
        }
        // Handle mouse button press events for forward/backward navigation
            // ✅ Don't handle BackButton/ForwardButton here anymore - handled by mouse dial system
            // They will handle short press page navigation and long press dial mode
            /*
        else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            
            // Mouse button 4 (Back button) - Previous page
            if (mouseEvent->button() == Qt::BackButton) {
                if (prevPageButton) {
                    prevPageButton->click();
                }
                return true; // Consume the event
            }
            // Mouse button 5 (Forward button) - Next page
            else if (mouseEvent->button() == Qt::ForwardButton) {
                if (nextPageButton) {
                    nextPageButton->click();
                }
                return true; // Consume the event
            }
            // ✅ Don't handle ExtraButton1/ExtraButton2 here anymore - handled by mouse dial system
        }
        */
        // Handle wheel events for scrolling
        // ========================================================================
        // WHEEL EVENT ROUTING: Mouse Wheel vs Trackpad
        // ========================================================================
        // Mouse wheel events are handled here with stepped scrolling.
        // Trackpad events are forwarded to InkCanvas for smooth gesture handling.
        //
        // Detection strategy:
        // - Mouse wheel: exactly 120 units, no pixelDelta, no scrollPhase, > 5ms gap
        // - Everything else: treated as trackpad (safer default)
        // ========================================================================
        else if (event->type() == QEvent::Wheel) {
            if (mouseDialModeActive) {
                return false;
            }

            // Phase 3.1.8: Use touchGestureMode member directly instead of InkCanvas
            
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            
            // --- Timing-based detection ---
            qint64 timeSinceLastEvent = lastWheelEventTimer.isValid() ? lastWheelEventTimer.elapsed() : -1;
            lastWheelEventTimer.restart();
            
            // --- Extract wheel event properties ---
            const int angleX = qAbs(wheelEvent->angleDelta().x());
            const int angleY = qAbs(wheelEvent->angleDelta().y());
            const bool hasPixelDelta = !wheelEvent->pixelDelta().isNull();
            const bool hasScrollPhase = wheelEvent->phase() != Qt::NoScrollPhase;
            const bool hasCtrlModifier = wheelEvent->modifiers() & Qt::ControlModifier;
            
            // --- Mouse wheel detection ---
            // A real mouse wheel produces exactly 120 units per step with no trackpad signals
            const bool hasExactWheelStep = (angleY == 120 && angleX == 0) || (angleX == 120 && angleY == 0);
            const bool looksLikeMouseWheel = hasExactWheelStep && !hasPixelDelta && !hasScrollPhase && !hasCtrlModifier;
            const bool isMouseWheel = looksLikeMouseWheel && timeSinceLastEvent > 5;
            
            // --- Route event ---
            // Phase 3.1.8: Use member variable touchGestureMode directly
            if (touchGestureMode == TouchGestureMode::Disabled) {
                // Disabled mode: mouse wheel works, trackpad blocked
                if (!isMouseWheel) {
                    return true; // Block trackpad
                }
            } else if (!isMouseWheel) {
                // Normal mode: send trackpad events to DocumentViewport
                trackpadModeActive = true;
                trackpadModeTimer->start();
                return false; // Let DocumentViewport handle
            }
            
            // --- Exit trackpad mode if this is definitely a mouse wheel event ---
            if (trackpadModeActive) {
                trackpadModeActive = false;
                trackpadModeTimer->stop();
            }
            
            // --- Let DocumentViewport handle mouse wheel scrolling natively ---
            // Phase 3.3: DocumentViewport's wheelEvent() handles all scroll/zoom
            // with proper pan overshoot support. The sliders are updated via
            // the scroll fraction signals (horizontalScrollChanged/verticalScrollChanged).
            // 
            // Previously this code handled mouse wheel via sliders which clamped
            // to 0-10000 (no overshoot). Now we let DocumentViewport handle it.
            return false;
        }
    }

    // Handle dial container drag events
    if (obj == dialContainer) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            lastMousePos = mouseEvent->globalPosition().toPoint();
            dragging = false;

            if (!longPressTimer) {
                longPressTimer = new QTimer(this);
                longPressTimer->setSingleShot(true);
                connect(longPressTimer, &QTimer::timeout, [this]() {
                    dragging = true;  // ✅ Allow movement after long press
                });
            }
            longPressTimer->start(1500);  // ✅ Start long press timer (500ms)
            return true;
        }

        if (event->type() == QEvent::MouseMove && dragging) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint delta = mouseEvent->globalPosition().toPoint() - lastMousePos;
            dialContainer->move(dialContainer->pos() + delta);
            lastMousePos = mouseEvent->globalPosition().toPoint();
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            if (longPressTimer) longPressTimer->stop();
            dragging = false;  // ✅ Stop dragging on release
            return true;
        }
    }

    return QObject::eventFilter(obj, event);
}


void MainWindow::initializeDialSound() {
    if (!dialClickSound) {
        dialClickSound = new SimpleAudio();
        if (!dialClickSound->loadWavFile(":/resources/sounds/dial_click.wav")) {
            qWarning() << "Failed to load dial click sound - audio will be disabled";
        }
        dialClickSound->setVolume(0.8);  // ✅ Set volume (0.0 - 1.0)
        dialClickSound->setMinimumInterval(5); // ✅ DirectSound can handle much faster rates (5ms minimum)
    }
}

void MainWindow::changeDialMode(DialMode mode) {

    if (!dialContainer) return;  // ✅ Ensure dial container exists
    currentDialMode = mode; // ✅ Set new mode
    updateDialDisplay();

    // ✅ Enable dialHiddenButton for PanAndPageScroll and ZoomControl modes
    dialHiddenButton->setEnabled(currentDialMode == PanAndPageScroll || currentDialMode == ZoomControl);

    // ✅ Disconnect previous slots
    disconnect(pageDial, &QDial::valueChanged, nullptr, nullptr);
    disconnect(pageDial, &QDial::sliderReleased, nullptr, nullptr);
    
    // ✅ Disconnect dialHiddenButton to reconnect with appropriate function
    disconnect(dialHiddenButton, &QPushButton::clicked, nullptr, nullptr);
    
    // ✅ Connect dialHiddenButton to appropriate function based on mode
    if (currentDialMode == PanAndPageScroll) {
        connect(dialHiddenButton, &QPushButton::clicked, this, &MainWindow::toggleControlBar);
    } else if (currentDialMode == ZoomControl) {
        connect(dialHiddenButton, &QPushButton::clicked, this, &MainWindow::cycleZoomLevels);
    }

    dialColorPreview->hide();
    dialDisplay->setStyleSheet("background-color: black; color: white; font-size: 14px; border-radius: 40px;");

    // ✅ Connect the correct function set for the current mode
    switch (currentDialMode) {
        case PageSwitching:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleDialInput);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onDialReleased);
            break;
        case ZoomControl:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleDialZoom);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onZoomReleased);
            break;
        case ThicknessControl:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleDialThickness);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onThicknessReleased);
            break;

        case ToolSwitching:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleToolSelection);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onToolReleased);
            break;
        case PresetSelection:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handlePresetSelection);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onPresetReleased);
            break;
        case PanAndPageScroll:
            connect(pageDial, &QDial::valueChanged, this, &MainWindow::handleDialPanScroll);
            connect(pageDial, &QDial::sliderReleased, this, &MainWindow::onPanScrollReleased);
            break;
        case None:
            // No dial mode active, do nothing
            break;
    }
}

void MainWindow::handleDialZoom(int angle) {
    if (!tracking) {
        startAngle = angle;  
        accumulatedRotation = 0;  
        tracking = true;
        lastAngle = angle;
        return;
    }

    int delta = angle - lastAngle;

    // ✅ Handle 360-degree wrapping
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    accumulatedRotation += delta;

    if (abs(delta) < 4) {  
        return;  
    }

    // ✅ Apply zoom dynamically (instead of waiting for release)
    int oldZoom = zoomSlider->value();
    int newZoom = qBound(10, oldZoom + (delta / 4), 400);  
    zoomSlider->setValue(newZoom);
    updateZoom();  // ✅ Ensure zoom updates immediately
    updateDialDisplay(); 

    lastAngle = angle;
}

void MainWindow::onZoomReleased() {
    accumulatedRotation = 0;
    tracking = false;
}

// New variable (add to MainWindow.h near accumulatedRotation)
int accumulatedRotationAfterLimit = 0;

void MainWindow::handleDialPanScroll(int angle) {
    if (!tracking) {
        startAngle = angle;
        accumulatedRotation = 0;
        accumulatedRotationAfterLimit = 0;
        tracking = true;
        lastAngle = angle;
        pendingPageFlip = 0;
        return;
    }

    int delta = angle - lastAngle;

    // Handle 360 wrap
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    accumulatedRotation += delta;

    // Pan scroll
    int panDelta = delta * 4;  // Adjust scroll sensitivity here
    int currentPan = panYSlider->value();
    int newPan = currentPan + panDelta;

    // Clamp pan slider
    newPan = qBound(panYSlider->minimum(), newPan, panYSlider->maximum());
    panYSlider->setValue(newPan);

    // ✅ NEW → if slider reached top/bottom, accumulate AFTER LIMIT
    if (newPan == panYSlider->maximum()) {
        accumulatedRotationAfterLimit += delta;

        if (accumulatedRotationAfterLimit >= 120) {
            pendingPageFlip = +1;  // Flip next when released
        }
    } 
    else if (newPan == panYSlider->minimum()) {
        accumulatedRotationAfterLimit += delta;

        if (accumulatedRotationAfterLimit <= -120) {
            pendingPageFlip = -1;  // Flip previous when released
        }
    } 
    else {
        // Reset after limit accumulator when not at limit
        accumulatedRotationAfterLimit = 0;
        pendingPageFlip = 0;
    }

    lastAngle = angle;
}

void MainWindow::onPanScrollReleased() {
    // ✅ Perform page flip only when dial released and flip is pending
    if (pendingPageFlip != 0) {
        // Phase 3.1.8: Autosave stubbed - DocumentViewport handles undo/redo

        DocumentViewport* vp = currentViewport();
        int currentPage = vp ? vp->currentPageIndex() : 0;
        int newPage = qBound(1, currentPage + pendingPageFlip + 1, 99999);
        
        // ✅ Use direction-aware page switching for pan-and-scroll dial
        switchPageWithDirection(newPage, pendingPageFlip);
        pageInput->setValue(newPage);
        updateDialDisplay();

        SDL_Joystick *joystick = controllerManager->getJoystick();
        if (joystick) {
            #if SDL_VERSION_ATLEAST(2, 0, 9)
            SDL_JoystickRumble(joystick, 0xA000, 0xF000, 25);  // Vibrate shortly
            #endif
        }
    }

    // Reset states
    pendingPageFlip = 0;
    accumulatedRotation = 0;
    accumulatedRotationAfterLimit = 0;
    tracking = false;
}



void MainWindow::handleDialThickness(int angle) {
    if (!tracking) {
        startAngle = angle;
        tracking = true;
        lastAngle = angle;
        return;
    }

    int delta = angle - lastAngle;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    int thicknessStep = fastForwardMode ? 5 : 1;
    vp->setPenThickness(qBound<qreal>(1.0, vp->penThickness() + (delta / 10.0) * thicknessStep, 50.0));

    updateDialDisplay();
    lastAngle = angle;
}

void MainWindow::onThicknessReleased() {
    accumulatedRotation = 0;
    tracking = false;
}

void MainWindow::handlePresetSelection(int angle) {
    static int lastAngle = angle;
    int delta = angle - lastAngle;

    // ✅ Handle 360-degree wrapping
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    if (abs(delta) >= 60) {  // ✅ Change preset every 60° (6 presets)
        lastAngle = angle;
        currentPresetIndex = (currentPresetIndex + (delta > 0 ? 1 : -1) + colorPresets.size()) % colorPresets.size();
        
        QColor selectedColor = colorPresets[currentPresetIndex];
        // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
        if (DocumentViewport* vp = currentViewport()) {
            vp->setPenColor(selectedColor);
        }
        updateCustomColorButtonStyle(selectedColor);
        updateDialDisplay();
        updateColorButtonStates();  // Update button states when preset is selected
        
        if (dialClickSound) dialClickSound->play();  // ✅ Provide feedback
        SDL_Joystick *joystick = controllerManager->getJoystick();
            if (joystick) {
                #if SDL_VERSION_ATLEAST(2, 0, 9)
                SDL_JoystickRumble(joystick, 0xA000, 0xF000, 25);  // Vibrate shortly
                #endif
            }
    }
}

void MainWindow::onPresetReleased() {
    accumulatedRotation = 0;
    tracking = false;
}






void MainWindow::addColorPreset() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    QColor currentColor = vp->penColor();

    // ✅ Prevent duplicates
    if (!colorPresets.contains(currentColor)) {
        if (colorPresets.size() >= 6) {
            colorPresets.dequeue();  // ✅ Remove oldest color
        }
        colorPresets.enqueue(currentColor);
    }
}

// Static method to update Qt application palette based on Windows dark mode
void MainWindow::updateApplicationPalette() {
#ifdef Q_OS_WIN
    // Detect if Windows is in dark mode
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                       QSettings::NativeFormat);
    int appsUseLightTheme = settings.value("AppsUseLightTheme", 1).toInt();
    bool isDarkMode = (appsUseLightTheme == 0);
    
    if (isDarkMode) {
        // Switch to Fusion style on Windows for proper dark mode support
        // The default Windows style doesn't respect custom palettes properly
        QApplication::setStyle("Fusion");
        
        // Create a comprehensive dark palette for Qt widgets
        QPalette darkPalette;
        
        // Base colors
        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor black(25, 25, 25);
        QColor blue(42, 130, 218);
        QColor lightGray(180, 180, 180);
        
        // Window colors (main background)
        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        
        // Base (text input background) colors
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);
        
        // Tooltip colors
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        
        // Button colors (critical for dialogs)
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        
        // 3D effects and borders (critical for proper widget rendering)
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
        
        // Bright text
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        
        // Link colors
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());
        
        // Highlight colors (selection)
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        
        // Placeholder text (for line edits, spin boxes, etc.)
        darkPalette.setColor(QPalette::PlaceholderText, gray);
        
        // Disabled colors (all color groups)
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
        
        QApplication::setPalette(darkPalette);
    } else {
        // Use default Windows style and palette for light mode
        QApplication::setStyle("windowsvista");
        QApplication::setPalette(QPalette());
    }
#endif
    // On Linux, don't override palette - desktop environment handles it
}

// to support dark mode icon switching.
bool MainWindow::isDarkMode() {
#ifdef Q_OS_WIN
    // On Windows, read the registry to detect dark mode
    // This works on Windows 10 1809+ and Windows 11
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                       QSettings::NativeFormat);
    
    // AppsUseLightTheme: 0 = dark mode, 1 = light mode
    // If the key doesn't exist (older Windows), default to light mode
    int appsUseLightTheme = settings.value("AppsUseLightTheme", 1).toInt();
    return (appsUseLightTheme == 0);
#else
    // On Linux and other platforms, use palette-based detection
    QColor bg = palette().color(QPalette::Window);
    return bg.lightness() < 128;  // Lightness scale: 0 (black) - 255 (white)
#endif
}

QColor MainWindow::getDefaultPenColor() {
    return isDarkMode() ? Qt::white : Qt::black;
}

QIcon MainWindow::loadThemedIcon(const QString& baseName) {
    QString path = isDarkMode()
        ? QString(":/resources/icons/%1_reversed.png").arg(baseName)
        : QString(":/resources/icons/%1.png").arg(baseName);
    return QIcon(path);
}

QIcon MainWindow::loadThemedIconReversed(const QString& baseName) {
    // Load the opposite of what loadThemedIcon would load
    // In dark mode: load normal icons (better visibility when selected)
    // In light mode: load reversed icons (better visibility when selected)
    QString path = isDarkMode()
        ? QString(":/resources/icons/%1.png").arg(baseName)
        : QString(":/resources/icons/%1_reversed.png").arg(baseName);
    return QIcon(path);
}

void MainWindow::updateButtonIcon(QPushButton* button, const QString& iconName) {
    if (!button) return;
    
    bool isSelected = button->property("selected").toBool();
    
    if (isSelected) {
        // When selected, use reversed icon for better contrast
        button->setIcon(loadThemedIconReversed(iconName));
    } else {
        // When not selected, use normal themed icon
        button->setIcon(loadThemedIcon(iconName));
    }
}

QString MainWindow::createButtonStyle(bool darkMode) {
    if (darkMode) {
        // Dark mode: Keep current white highlights (good contrast)
        return R"(
            QPushButton {
                background: transparent;
                border: none;
                padding: 6px;
            }
            QPushButton:hover {
                background: rgba(255, 255, 255, 50);
            }
            QPushButton:pressed {
                background: rgba(0, 0, 0, 50);
            }
            QPushButton[selected="true"] {
                background: rgba(255, 255, 255, 100);
                /*border: 1px solid rgba(255, 255, 255, 150);*/
                padding: 4px;
                border-radius: 0px;
            }
            QPushButton[selected="true"]:hover {
                background: rgba(255, 255, 255, 120);
            }
            QPushButton[selected="true"]:pressed {
                background: rgba(0, 0, 0, 50);
            }
            QPushButton[yAxisOnly="true"] {
                background: rgba(255, 100, 100, 120);
                /*border: 1px solid rgba(255, 100, 100, 180);*/
                padding: 4px;
                border-radius: 0px;
            }
            QPushButton[yAxisOnly="true"]:hover {
                background: rgba(255, 120, 120, 140);
            }
            QPushButton[yAxisOnly="true"]:pressed {
                background: rgba(200, 50, 50, 100);
            }
        )";
    } else {
        // Light mode: Use darker colors for better visibility
        return R"(
            QPushButton {
                background: transparent;
                border: none;
                padding: 6px;
            }
            QPushButton:hover {
                background: rgba(0, 0, 0, 30);
            }
            QPushButton:pressed {
                background: rgba(0, 0, 0, 60);
            }
            QPushButton[selected="true"] {
                background: rgba(0, 0, 0, 80);
                /*border: 2px solid rgba(0, 0, 0, 120);*/
                padding: 4px;
                border-radius: 0px;
            }
            QPushButton[selected="true"]:hover {
                background: rgba(0, 0, 0, 100);
            }
            QPushButton[selected="true"]:pressed {
                background: rgba(0, 0, 0, 140);
            }
            QPushButton[yAxisOnly="true"] {
                background: rgba(255, 60, 60, 100);
                /*border: 2px solid rgba(255, 60, 60, 150);*/
                padding: 4px;
                border-radius: 0px;
            }
            QPushButton[yAxisOnly="true"]:hover {
                background: rgba(255, 80, 80, 120);
            }
            QPushButton[yAxisOnly="true"]:pressed {
                background: rgba(200, 40, 40, 140);
            }
        )";
    }
}



QColor MainWindow::getAccentColor() const {
    if (useCustomAccentColor && customAccentColor.isValid()) {
        return customAccentColor;
    }
    
    // Return system accent color
    QPalette palette = QGuiApplication::palette();
    return palette.highlight().color();
}

void MainWindow::setCustomAccentColor(const QColor &color) {
    if (customAccentColor != color) {
        customAccentColor = color;
        saveThemeSettings();
        // Always update theme if custom accent color is enabled
        if (useCustomAccentColor) {
            updateTheme();
        }
    }
}

void MainWindow::setUseCustomAccentColor(bool use) {
    if (useCustomAccentColor != use) {
        useCustomAccentColor = use;
        updateTheme();
        saveThemeSettings();
    }
}
void MainWindow::updateTheme() {
    // Update control bar background color to match tab list brightness
    QColor accentColor = getAccentColor();
    bool darkMode = isDarkMode();
    
    if (controlBar) {
        // Use same background as unselected tab color
        QString toolbarBgColor = darkMode ? "rgba(80, 80, 80, 255)" : "rgba(220, 220, 220, 255)";
        controlBar->setStyleSheet(QString(R"(
        QWidget#controlBar {
            background-color: %1;
            }
        )").arg(toolbarBgColor));
    }
    
    // Common floating tab styling colors (solid, not transparent)
    QString tabBgColor = darkMode ? "#3A3A3A" : "#EAEAEA";
    QString tabHoverColor = darkMode ? "#4A4A4A" : "#DADADA";
    QString tabBorderColor = darkMode ? "#555555" : "#CCCCCC";
    
    // Update dial toolbar tab styling (right side, rounded left)
    if (dialToolbarToggle) {
        QString dialTabStyle = QString(
            "QPushButton#dialToolbarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-right: none;"
            "  border-top-left-radius: 0px;"
            "  border-bottom-left-radius: 0px;"
            "}"
            "QPushButton#dialToolbarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#dialToolbarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBgColor, tabBorderColor, tabHoverColor);
        dialToolbarToggle->setStyleSheet(dialTabStyle);
        dialToolbarToggle->setIcon(loadThemedIcon("dial"));
    }
    
    if (dialToolbar) {
        QString panelBg = darkMode ? "#2D2D2D" : "#F5F5F5";
        QString panelStyle = QString(
            "QWidget#dialToolbar {"
            "  background-color: %1;"
            "  border-left: 1px solid %2;"
            "}"
        ).arg(panelBg, tabBorderColor);
        dialToolbar->setStyleSheet(panelStyle);
    }
    
    // Update left sidebar tabs styling (left side, rounded right)
    if (toggleOutlineButton) {
        QString outlineStyle = QString(
            "QPushButton#outlineSidebarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-left: none;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-right-radius: 0px;"
            "}"
            "QPushButton#outlineSidebarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#outlineSidebarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBgColor, tabBorderColor, tabHoverColor);
        toggleOutlineButton->setStyleSheet(outlineStyle);
        toggleOutlineButton->setIcon(loadThemedIcon("outline"));
    }
    
    if (toggleBookmarksButton) {
        QString bookmarksStyle = QString(
            "QPushButton#bookmarksSidebarTab {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-left: none;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-right-radius: 0px;"
            "}"
            "QPushButton#bookmarksSidebarTab:hover {"
            "  background-color: %3;"
            "}"
            "QPushButton#bookmarksSidebarTab:pressed {"
            "  background-color: %1;"
            "}"
        ).arg(tabBgColor, tabBorderColor, tabHoverColor);
        toggleBookmarksButton->setStyleSheet(bookmarksStyle);
        toggleBookmarksButton->setIcon(loadThemedIcon("bookmark"));
    }
    
    // Update dial background color
    if (pageDial) {
        pageDial->setStyleSheet(QString(R"(
        QDial {
            background-color: %1;
            }
        )").arg(accentColor.name()));
    }
    
    // Update add tab button styling
    if (addTabButton) {
        bool darkMode = isDarkMode();
        QString buttonBgColor = darkMode ? "rgba(80, 80, 80, 0)" : "rgba(220, 220, 220, 0)";
        QString buttonHoverColor = darkMode ? "rgba(90, 90, 90, 255)" : "rgba(200, 200, 200, 255)";
        QString buttonPressColor = darkMode ? "rgba(70, 70, 70, 255)" : "rgba(180, 180, 180, 255)";
        QString borderColor = darkMode ? "rgba(100, 100, 100, 255)" : "rgba(180, 180, 180, 255)";
        
        addTabButton->setStyleSheet(QString(R"(
            QPushButton {
                background-color: %1;
                /*border: 1px solid %2;*/
                border-radius: 0px;
                margin: 2px;
            }
            QPushButton:hover {
                background-color: %3;
            }
            QPushButton:pressed {
                background-color: %4;
            }
        )").arg(buttonBgColor).arg(borderColor).arg(buttonHoverColor).arg(buttonPressColor));
    }
    
    // Update PDF outline sidebar styling
    if (outlineSidebar && outlineTree) {
        bool darkMode = isDarkMode();
        QString bgColor = darkMode ? "rgba(45, 45, 45, 255)" : "rgba(250, 250, 250, 255)";
        QString borderColor = darkMode ? "rgba(80, 80, 80, 255)" : "rgba(200, 200, 200, 255)";
        QString textColor = darkMode ? "#E0E0E0" : "#333";
        QString hoverColor = darkMode ? "rgba(60, 60, 60, 255)" : "rgba(240, 240, 240, 255)";
        QString selectedColor = QString("rgba(%1, %2, %3, 100)").arg(accentColor.red()).arg(accentColor.green()).arg(accentColor.blue());
        
        outlineSidebar->setStyleSheet(QString(R"(
            QWidget {
                background-color: %1;
                border-right: 1px solid %2;
            }
            QLabel {
                color: %3;
                background: transparent;
            }
        )").arg(bgColor).arg(borderColor).arg(textColor));
        
        outlineTree->setStyleSheet(QString(R"(
            QTreeWidget {
                background-color: %1;
                border: none;
                color: %2;
                outline: none;
            }
            QTreeWidget::item {
                padding: 4px;
                border: none;
            }
            QTreeWidget::item:hover {
                background-color: %3;
            }
            QTreeWidget::item:selected {
                background-color: %4;
                color: %2;
            }
            QTreeWidget::branch {
                background: transparent;
            }
            QTreeWidget::branch:has-children:!has-siblings:closed,
            QTreeWidget::branch:closed:has-children:has-siblings {
                border-image: none;
                image: url(:/resources/icons/down_arrow.png);
            }
            QTreeWidget::branch:open:has-children:!has-siblings,
            QTreeWidget::branch:open:has-children:has-siblings {
                border-image: none;
                image: url(:/resources/icons/up_arrow.png);
            }
            QScrollBar:vertical {
                background: rgba(200, 200, 200, 80);
                border: none;
                margin: 0px;
                width: 16px !important;
                max-width: 16px !important;
            }
            QScrollBar:vertical:hover {
                background: rgba(200, 200, 200, 120);
            }
            QScrollBar::handle:vertical {
                background: rgba(100, 100, 100, 150);
                border-radius: 2px;
                min-height: 120px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(80, 80, 80, 210);
            }
            QScrollBar::add-line:vertical, 
            QScrollBar::sub-line:vertical {
                width: 0px;
                height: 0px;
                background: none;
                border: none;
            }
            QScrollBar::add-page:vertical, 
            QScrollBar::sub-page:vertical {
                background: transparent;
            }
        )").arg(bgColor).arg(textColor).arg(hoverColor).arg(selectedColor));
        
        // Apply same styling to bookmarks tree
        bookmarksTree->setStyleSheet(QString(R"(
            QTreeWidget {
                background-color: %1;
                border: none;
                color: %2;
                outline: none;
            }
            QTreeWidget::item {
                padding: 2px;
                border: none;
                min-height: 26px;
            }
            QTreeWidget::item:hover {
                background-color: %3;
            }
            QTreeWidget::item:selected {
                background-color: %4;
                color: %2;
            }
            QScrollBar:vertical {
                background: rgba(200, 200, 200, 80);
                border: none;
                margin: 0px;
                width: 16px !important;
                max-width: 16px !important;
            }
            QScrollBar:vertical:hover {
                background: rgba(200, 200, 200, 120);
            }
            QScrollBar::handle:vertical {
                background: rgba(100, 100, 100, 150);
                border-radius: 2px;
                min-height: 120px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(80, 80, 80, 210);
            }
            QScrollBar::add-line:vertical, 
            QScrollBar::sub-line:vertical {
                width: 0px;
                height: 0px;
                background: none;
                border: none;
            }
            QScrollBar::add-page:vertical, 
            QScrollBar::sub-page:vertical {
                background: transparent;
            }
        )").arg(bgColor).arg(textColor).arg(hoverColor).arg(selectedColor));
    }
    
    // Phase 3.1: Tab bar styling now uses QTabWidget
    if (m_tabWidget) {
        // Style the tab bar with accent color
        m_tabWidget->setStyleSheet(QString(R"(
        QTabBar {
            background-color: %1;
        }
        QTabBar::tab {
            background-color: %1;
            color: %2;
            padding: 8px 16px;
            border: none;
        }
        QTabBar::tab:selected {
            background-color: %3;
        }
        QTabBar::tab:hover:!selected {
            background-color: %4;
        }
        )").arg(accentColor.name())
           .arg(darkMode ? "#ffffff" : "#000000")
           .arg(accentColor.darker(110).name())
           .arg(accentColor.lighter(110).name()));
    }
    

    
    // Force icon reload for all buttons that use themed icons
    // Use updateButtonIcon for buttons with selectable states, direct setIcon for others
    if (loadPdfButton) loadPdfButton->setIcon(loadThemedIcon("pdf"));
    if (clearPdfButton) clearPdfButton->setIcon(loadThemedIcon("pdfdelete"));
    updateButtonIcon(pdfTextSelectButton, "ibeam");

    updateButtonIcon(benchmarkButton, "benchmark");
    updateButtonIcon(toggleTabBarButton, "tabs");
    updateButtonIcon(toggleOutlineButton, "outline");
    updateButtonIcon(toggleBookmarksButton, "bookmark");
    updateButtonIcon(toggleBookmarkButton, "star");
    if (selectFolderButton) selectFolderButton->setIcon(loadThemedIcon("folder"));
    if (saveButton) saveButton->setIcon(loadThemedIcon("save"));
    if (exportPdfButton) exportPdfButton->setIcon(loadThemedIcon("export"));
    if (fullscreenButton) fullscreenButton->setIcon(loadThemedIcon("fullscreen"));
    // if (backgroundButton) backgroundButton->setIcon(loadThemedIcon("background"));
    updateButtonIcon(straightLineToggleButton, "straightLine");
    updateButtonIcon(ropeToolButton, "rope");
    if (deletePageButton) deletePageButton->setIcon(loadThemedIcon("trash"));
    if (zoomButton) zoomButton->setIcon(loadThemedIcon("zoom"));
    updateButtonIcon(dialToggleButton, "dial");
    updateButtonIcon(fastForwardButton, "fastforward");
    if (jumpToPageButton) jumpToPageButton->setIcon(loadThemedIcon("bookpage"));
    if (thicknessButton) thicknessButton->setIcon(loadThemedIcon("thickness"));
    if (btnPageSwitch) btnPageSwitch->setIcon(loadThemedIcon("bookpage"));
    if (btnZoom) btnZoom->setIcon(loadThemedIcon("zoom"));
    if (btnThickness) btnThickness->setIcon(loadThemedIcon("thickness"));
    if (btnTool) btnTool->setIcon(loadThemedIcon("pen"));
    if (btnPresets) btnPresets->setIcon(loadThemedIcon("preset"));
    if (btnPannScroll) btnPannScroll->setIcon(loadThemedIcon("scroll"));
    if (addPresetButton) addPresetButton->setIcon(loadThemedIcon("savepreset"));
    if (openControlPanelButton) openControlPanelButton->setIcon(loadThemedIcon("settings"));
    if (openRecentNotebooksButton) {
        openRecentNotebooksButton->setIcon(loadThemedIcon("recent"));
        // Update button style for theme
        QString buttonStyle = createButtonStyle(darkMode);
        openRecentNotebooksButton->setStyleSheet(buttonStyle);
    }
    updateButtonIcon(penToolButton, "pen");
    updateButtonIcon(markerToolButton, "marker");
    updateButtonIcon(eraserToolButton, "eraser");
    updateButtonIcon(insertPictureButton, "background");
    updateButtonIcon(touchGesturesButton, "hand");
    
    // Update button styles with new theme (darkMode already declared at top of function)
    QString newButtonStyle = createButtonStyle(darkMode);
    
    // Update all buttons that use the buttonStyle
    if (loadPdfButton) loadPdfButton->setStyleSheet(newButtonStyle);
    if (clearPdfButton) clearPdfButton->setStyleSheet(newButtonStyle);
    if (pdfTextSelectButton) pdfTextSelectButton->setStyleSheet(newButtonStyle);

    if (benchmarkButton) benchmarkButton->setStyleSheet(newButtonStyle);
    if (toggleTabBarButton) toggleTabBarButton->setStyleSheet(newButtonStyle);
    // toggleOutlineButton and toggleBookmarksButton use custom floating tab styles, not buttonStyle
    if (toggleBookmarkButton) toggleBookmarkButton->setStyleSheet(newButtonStyle);
    if (selectFolderButton) selectFolderButton->setStyleSheet(newButtonStyle);
    if (saveButton) saveButton->setStyleSheet(newButtonStyle);
    if (fullscreenButton) fullscreenButton->setStyleSheet(newButtonStyle);
    if (redButton) redButton->setStyleSheet(newButtonStyle);
    if (blueButton) blueButton->setStyleSheet(newButtonStyle);
    if (yellowButton) yellowButton->setStyleSheet(newButtonStyle);
    if (greenButton) greenButton->setStyleSheet(newButtonStyle);
    if (blackButton) blackButton->setStyleSheet(newButtonStyle);
    if (whiteButton) whiteButton->setStyleSheet(newButtonStyle);
    if (thicknessButton) thicknessButton->setStyleSheet(newButtonStyle);
    if (penToolButton) penToolButton->setStyleSheet(newButtonStyle);
    if (markerToolButton) markerToolButton->setStyleSheet(newButtonStyle);
    if (eraserToolButton) eraserToolButton->setStyleSheet(newButtonStyle);
    // if (backgroundButton) backgroundButton->setStyleSheet(newButtonStyle);
    if (straightLineToggleButton) straightLineToggleButton->setStyleSheet(newButtonStyle);
    if (ropeToolButton) ropeToolButton->setStyleSheet(newButtonStyle);
    if (insertPictureButton) insertPictureButton->setStyleSheet(newButtonStyle);
    if (deletePageButton) deletePageButton->setStyleSheet(newButtonStyle);
    if (overflowMenuButton) overflowMenuButton->setStyleSheet(newButtonStyle);
    if (zoomButton) zoomButton->setStyleSheet(newButtonStyle);
    if (dialToggleButton) dialToggleButton->setStyleSheet(newButtonStyle);
    if (fastForwardButton) fastForwardButton->setStyleSheet(newButtonStyle);
    if (jumpToPageButton) jumpToPageButton->setStyleSheet(newButtonStyle);
    if (btnPageSwitch) btnPageSwitch->setStyleSheet(newButtonStyle);
    if (btnZoom) btnZoom->setStyleSheet(newButtonStyle);
    if (btnThickness) btnThickness->setStyleSheet(newButtonStyle);
    if (btnTool) btnTool->setStyleSheet(newButtonStyle);
    if (btnPresets) btnPresets->setStyleSheet(newButtonStyle);
    if (btnPannScroll) btnPannScroll->setStyleSheet(newButtonStyle);
    if (addPresetButton) addPresetButton->setStyleSheet(newButtonStyle);
    if (openControlPanelButton) openControlPanelButton->setStyleSheet(newButtonStyle);
    if (openRecentNotebooksButton) openRecentNotebooksButton->setStyleSheet(newButtonStyle);
    if (zoom50Button) zoom50Button->setStyleSheet(newButtonStyle);
    if (dezoomButton) dezoomButton->setStyleSheet(newButtonStyle);
    if (zoom200Button) zoom200Button->setStyleSheet(newButtonStyle);
    if (prevPageButton) prevPageButton->setStyleSheet(newButtonStyle);
    if (nextPageButton) nextPageButton->setStyleSheet(newButtonStyle);
    
    // Update color buttons with palette-based icons
    // Removed colorPreview widget - no longer needed

    if (redButton) {
        QString redIconPath = useBrighterPalette ? ":/resources/icons/pen_light_red.png" : ":/resources/icons/pen_dark_red.png";
        redButton->setIcon(QIcon(redIconPath));
    }
    if (blueButton) {
        QString blueIconPath = useBrighterPalette ? ":/resources/icons/pen_light_blue.png" : ":/resources/icons/pen_dark_blue.png";
        blueButton->setIcon(QIcon(blueIconPath));
    }
    if (yellowButton) {
        QString yellowIconPath = useBrighterPalette ? ":/resources/icons/pen_light_yellow.png" : ":/resources/icons/pen_dark_yellow.png";
        yellowButton->setIcon(QIcon(yellowIconPath));
    }
    if (greenButton) {
        QString greenIconPath = useBrighterPalette ? ":/resources/icons/pen_light_green.png" : ":/resources/icons/pen_dark_green.png";
        greenButton->setIcon(QIcon(greenIconPath));
    }
    if (blackButton) {
        QString blackIconPath = darkMode ? ":/resources/icons/pen_light_black.png" : ":/resources/icons/pen_dark_black.png";
        blackButton->setIcon(QIcon(blackIconPath));
    }
    if (whiteButton) {
        QString whiteIconPath = darkMode ? ":/resources/icons/pen_light_white.png" : ":/resources/icons/pen_dark_white.png";
        whiteButton->setIcon(QIcon(whiteIconPath));
    }
    
    // Phase 3.1: Tab styling now uses QTabWidget
    // QTabWidget provides built-in close buttons via setTabsClosable(true)
    // TODO Phase 3.3: Apply custom styling to m_tabWidget tabs if needed
    
    // Update dial display
    updateDialDisplay();
}

void MainWindow::saveThemeSettings() {
    QSettings settings("SpeedyNote", "App");
    settings.setValue("useCustomAccentColor", useCustomAccentColor);
    if (customAccentColor.isValid()) {
        settings.setValue("customAccentColor", customAccentColor.name());
    }
    settings.setValue("useBrighterPalette", useBrighterPalette);
}

void MainWindow::loadThemeSettings() {
    QSettings settings("SpeedyNote", "App");
    useCustomAccentColor = settings.value("useCustomAccentColor", false).toBool();
    QString colorName = settings.value("customAccentColor", "#0078D4").toString();
    customAccentColor = QColor(colorName);
    useBrighterPalette = settings.value("useBrighterPalette", false).toBool();
    
    // Ensure valid values
    if (!customAccentColor.isValid()) {
        customAccentColor = QColor("#0078D4"); // Default blue
    }
    
    // Apply theme immediately after loading
    updateTheme();
}

void MainWindow::updateTabSizes() {
    // Phase 3.1: QTabWidget handles its own tab sizing
    // This function is now a stub - QTabWidget with setElideMode() handles overflow
    // TODO Phase 3.3: Consider custom tab styling via QTabBar::setTabButton if needed
}

// performance optimizations
void MainWindow::setLowResPreviewEnabled(bool enabled) {
    lowResPreviewEnabled = enabled;

    QSettings settings("SpeedyNote", "App");
    settings.setValue("lowResPreviewEnabled", enabled);
}

bool MainWindow::isLowResPreviewEnabled() const {
    return lowResPreviewEnabled;
}

// ui optimizations

bool MainWindow::areBenchmarkControlsVisible() const {
    return benchmarkButton->isVisible() && benchmarkLabel->isVisible();
}

void MainWindow::setBenchmarkControlsVisible(bool visible) {
    benchmarkButton->setVisible(visible);
    benchmarkLabel->setVisible(visible);
}

bool MainWindow::areZoomButtonsVisible() const {
    return zoomButtonsVisible;
}

void MainWindow::setZoomButtonsVisible(bool visible) {
    zoom50Button->setVisible(visible);
    dezoomButton->setVisible(visible);
    zoom200Button->setVisible(visible);

    QSettings settings("SpeedyNote", "App");
    settings.setValue("zoomButtonsVisible", visible);
    
    // Update zoomButtonsVisible flag and trigger layout update
    zoomButtonsVisible = visible;
    
    // Trigger layout update to adjust responsive thresholds
    if (layoutUpdateTimer) {
        layoutUpdateTimer->stop();
        layoutUpdateTimer->start(50); // Quick update for settings change
    } else {
        updateToolbarLayout(); // Direct update if no timer exists yet
    }
}



bool MainWindow::isScrollOnTopEnabled() const {
    return scrollOnTopEnabled;
}

void MainWindow::setScrollOnTopEnabled(bool enabled) {
    scrollOnTopEnabled = enabled;

    QSettings settings("SpeedyNote", "App");
    settings.setValue("scrollOnTopEnabled", enabled);
}

TouchGestureMode MainWindow::getTouchGestureMode() const {
    return touchGestureMode;
}

void MainWindow::setTouchGestureMode(TouchGestureMode mode) {
    touchGestureMode = mode;
    
    // Phase 3.1: DocumentViewport handles touch gestures differently
    // TODO Phase 3.3: Apply touch gesture mode to all DocumentViewports via TabManager
    // if (m_tabManager) {
    //     m_tabManager->applyToAllViewports([mode](DocumentViewport* vp) {
    //         vp->setTouchGestureMode(mode);
    //     });
    // }
    
    QSettings settings("SpeedyNote", "App");
    settings.setValue("touchGestureMode", static_cast<int>(mode));
}

void MainWindow::cycleTouchGestureMode() {
    // Cycle: Disabled -> YAxisOnly -> Full -> Disabled
    switch (touchGestureMode) {
        case TouchGestureMode::Disabled:
            setTouchGestureMode(TouchGestureMode::YAxisOnly);
            break;
        case TouchGestureMode::YAxisOnly:
            setTouchGestureMode(TouchGestureMode::Full);
            break;
        case TouchGestureMode::Full:
            setTouchGestureMode(TouchGestureMode::Disabled);
            break;
    }
}

#ifdef Q_OS_LINUX
void MainWindow::setPalmRejectionEnabled(bool enabled) {
    palmRejectionEnabled = enabled;
    QSettings settings("SpeedyNote", "App");
    settings.setValue("palmRejectionEnabled", enabled);
    
    // If disabling while active, restore the original mode immediately
    if (!enabled && palmRejectionActive) {
        if (palmRejectionTimer && palmRejectionTimer->isActive()) {
            palmRejectionTimer->stop();
        }
        restoreTouchGestureMode();
    }
}

void MainWindow::setPalmRejectionDelay(int delayMs) {
    palmRejectionDelayMs = qBound(0, delayMs, 5000); // Clamp to 0-5000ms
    QSettings settings("SpeedyNote", "App");
    settings.setValue("palmRejectionDelayMs", palmRejectionDelayMs);
}

void MainWindow::onStylusProximityEnter() {
    // Phase 3.1: Palm rejection stubbed - will use DocumentViewport
    if (!palmRejectionEnabled) {
        return;
    }
    
    // Stop any pending restore timer
    if (palmRejectionTimer && palmRejectionTimer->isActive()) {
        palmRejectionTimer->stop();
    }
    
    // If not already suppressing, save current mode and disable touch gestures
    if (!palmRejectionActive) {
        if (touchGestureMode == TouchGestureMode::Disabled) {
            return;
        }
        
        palmRejectionOriginalMode = touchGestureMode;
        palmRejectionActive = true;
        
        // TODO Phase 3.3: Apply to DocumentViewports via TabManager
    }
}

void MainWindow::onStylusProximityLeave() {
    if (!palmRejectionEnabled || !palmRejectionActive) {
        return;
    }
    
    // Start timer to restore touch gestures after delay
    if (palmRejectionTimer) {
        palmRejectionTimer->setInterval(palmRejectionDelayMs);
        palmRejectionTimer->start();
    }
}

void MainWindow::restoreTouchGestureMode() {
    // Phase 3.1: Palm rejection stubbed - will use DocumentViewport
    if (!palmRejectionActive) {
        return;
    }
    
    palmRejectionActive = false;
    
    // TODO Phase 3.3: Apply to DocumentViewports via TabManager
}

bool MainWindow::event(QEvent *event) {
    // Handle tablet proximity events for palm rejection
    if (event->type() == QEvent::TabletEnterProximity) {
        onStylusProximityEnter();
    } else if (event->type() == QEvent::TabletLeaveProximity) {
        onStylusProximityLeave();
    }
    
    return QMainWindow::event(event);
}
#endif

// ==================== Stylus Button Mapping ====================

void MainWindow::setStylusButtonAAction(StylusButtonAction action) {
    stylusButtonAAction = action;
    saveStylusButtonSettings();
}

void MainWindow::setStylusButtonBAction(StylusButtonAction action) {
    stylusButtonBAction = action;
    saveStylusButtonSettings();
}

void MainWindow::setStylusButtonAQt(Qt::MouseButton button) {
    stylusButtonAQt = button;
    saveStylusButtonSettings();
}

void MainWindow::setStylusButtonBQt(Qt::MouseButton button) {
    stylusButtonBQt = button;
    saveStylusButtonSettings();
}

void MainWindow::saveStylusButtonSettings() {
    QSettings settings("SpeedyNote", "App");
    settings.setValue("stylusButtonAAction", static_cast<int>(stylusButtonAAction));
    settings.setValue("stylusButtonBAction", static_cast<int>(stylusButtonBAction));
    settings.setValue("stylusButtonAQt", static_cast<int>(stylusButtonAQt));
    settings.setValue("stylusButtonBQt", static_cast<int>(stylusButtonBQt));
}

void MainWindow::loadStylusButtonSettings() {
    QSettings settings("SpeedyNote", "App");
    stylusButtonAAction = static_cast<StylusButtonAction>(
        settings.value("stylusButtonAAction", static_cast<int>(StylusButtonAction::None)).toInt());
    stylusButtonBAction = static_cast<StylusButtonAction>(
        settings.value("stylusButtonBAction", static_cast<int>(StylusButtonAction::None)).toInt());
    stylusButtonAQt = static_cast<Qt::MouseButton>(
        settings.value("stylusButtonAQt", static_cast<int>(Qt::MiddleButton)).toInt());
    stylusButtonBQt = static_cast<Qt::MouseButton>(
        settings.value("stylusButtonBQt", static_cast<int>(Qt::RightButton)).toInt());
}

void MainWindow::enableStylusButtonMode(Qt::MouseButton button) {
    // Phase 3.1.8: Stubbed - stylus button modes will use DocumentViewport
    Q_UNUSED(button);
    // TODO Phase 3.3: Implement stylus button modes with DocumentViewport
    qDebug() << "enableStylusButtonMode(): Not implemented yet (Phase 3.3)";
    return;

#if 0 // Phase 3.1.8: Old stylus button code disabled
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    StylusButtonAction action = StylusButtonAction::None;
    bool *activeFlag = nullptr;
    ToolType *previousTool = nullptr;
    bool *previousStraightLine = nullptr;
    bool *previousRopeTool = nullptr;
    bool *previousTextSelection = nullptr;
    
    // Determine which button and its settings
    if (button == stylusButtonAQt) {
        action = stylusButtonAAction;
        activeFlag = &stylusButtonAActive;
        previousTool = &previousToolBeforeStylusA;
        previousStraightLine = &previousStraightLineModeA;
        previousRopeTool = &previousRopeToolModeA;
        previousTextSelection = &previousTextSelectionModeA;
    } else if (button == stylusButtonBQt) {
        action = stylusButtonBAction;
        activeFlag = &stylusButtonBActive;
        previousTool = &previousToolBeforeStylusB;
        previousStraightLine = &previousStraightLineModeB;
        previousRopeTool = &previousRopeToolModeB;
        previousTextSelection = &previousTextSelectionModeB;
    } else {
        return; // Unknown button
    }
    
    if (action == StylusButtonAction::None || *activeFlag) {
        return; // No action configured or already active
    }
    
    // Save current state before enabling
    *previousTool = canvas->getCurrentTool();
    *previousStraightLine = canvas->isStraightLineMode();
    *previousRopeTool = canvas->isRopeToolMode();
    *previousTextSelection = canvas->isPdfTextSelectionEnabled();
    
    *activeFlag = true;
    
    // Enable the requested mode
    switch (action) {
        case StylusButtonAction::HoldStraightLine:
            // Disable rope tool if active (they're mutually exclusive)
            if (canvas->isRopeToolMode()) {
                canvas->setRopeToolMode(false);
                updateRopeToolButtonState();
            }
            canvas->setStraightLineMode(true);
            // Reset start point to current position if pen is already on surface
            // This fixes race condition when stylus button and pen tip touch happen simultaneously
            canvas->resetStraightLineStartPoint();
            updateStraightLineButtonState();
            break;
            
        case StylusButtonAction::HoldLasso:
            // Disable straight line if active (they're mutually exclusive)
            if (canvas->isStraightLineMode()) {
                canvas->setStraightLineMode(false);
                updateStraightLineButtonState();
            }
            canvas->setRopeToolMode(true);
            updateRopeToolButtonState();
            break;
            
        case StylusButtonAction::HoldEraser:
            canvas->setTool(ToolType::Eraser);
            updateToolButtonStates();
            break;
            
        case StylusButtonAction::HoldTextSelection:
            // Clear any pending disable from previous interaction
            textSelectionPendingDisable = false;
            canvas->setPdfTextSelectionEnabled(true);
            updatePdfTextSelectButtonState();
            break;
            
        default:
            break;
    }
#endif // Phase 3.1.8: enableStylusButtonMode disabled
}

void MainWindow::disableStylusButtonMode(Qt::MouseButton button) {
    // Phase 3.1.8: Stubbed - stylus button modes will use DocumentViewport
    Q_UNUSED(button);
    // TODO Phase 3.3: Implement stylus button modes with DocumentViewport
    qDebug() << "disableStylusButtonMode(): Not implemented yet (Phase 3.3)";
    return;

#if 0 // Phase 3.1.8: Old stylus button code disabled
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    StylusButtonAction action = StylusButtonAction::None;
    bool *activeFlag = nullptr;
    ToolType *previousTool = nullptr;
    bool *previousStraightLine = nullptr;
    bool *previousRopeTool = nullptr;
    bool *previousTextSelection = nullptr;
    
    // Determine which button and its settings
    if (button == stylusButtonAQt) {
        action = stylusButtonAAction;
        activeFlag = &stylusButtonAActive;
        previousTool = &previousToolBeforeStylusA;
        previousStraightLine = &previousStraightLineModeA;
        previousRopeTool = &previousRopeToolModeA;
        previousTextSelection = &previousTextSelectionModeA;
    } else if (button == stylusButtonBQt) {
        action = stylusButtonBAction;
        activeFlag = &stylusButtonBActive;
        previousTool = &previousToolBeforeStylusB;
        previousStraightLine = &previousStraightLineModeB;
        previousRopeTool = &previousRopeToolModeB;
        previousTextSelection = &previousTextSelectionModeB;
    } else {
        return; // Unknown button
    }
    
    if (!*activeFlag) {
        return; // Was not active
    }
    
    *activeFlag = false;
    
    // Restore previous state
    switch (action) {
        case StylusButtonAction::HoldStraightLine:
            canvas->setStraightLineMode(*previousStraightLine);
            updateStraightLineButtonState();
            // Restore rope tool if it was active before
            if (*previousRopeTool) {
                canvas->setRopeToolMode(true);
                updateRopeToolButtonState();
            }
            break;
            
        case StylusButtonAction::HoldLasso:
#ifndef Q_OS_WIN
            // Clear any in-progress lasso selection before disabling (non-Windows only)
            // On Windows, InkCanvas handles lasso finalization in hover mode
            canvas->clearInProgressLasso();
#endif
            canvas->setRopeToolMode(*previousRopeTool);
            updateRopeToolButtonState();
            // Restore straight line if it was active before
            if (*previousStraightLine) {
                canvas->setStraightLineMode(true);
                updateStraightLineButtonState();
            }
            break;
            
        case StylusButtonAction::HoldEraser:
            canvas->setTool(*previousTool);
            updateToolButtonStates();
            break;
            
        case StylusButtonAction::HoldTextSelection:
            // Check if there's an active text selection that needs interaction
            if (canvas->hasSelectedPdfText()) {
                // Delay the disable - keep text selection mode on until user completes interaction
                textSelectionPendingDisable = true;
                textSelectionWasButtonA = (button == stylusButtonAQt);
                // Don't disable yet, don't clear activeFlag - wait for onPdfTextSelectionCleared
                return; // Exit without disabling
            }
            canvas->setPdfTextSelectionEnabled(*previousTextSelection);
            updatePdfTextSelectButtonState();
            break;
            
        default:
            break;
    }
#endif // Phase 3.1.8: disableStylusButtonMode disabled
}

void MainWindow::onPdfTextSelectionCleared() {
    // Phase 3.1.8: Stubbed - text selection will use DocumentViewport
    // TODO Phase 3.4: Implement text selection with DocumentViewport
    textSelectionPendingDisable = false;
    return;

#if 0 // Phase 3.1.8: Old text selection code disabled
    // Called when text selection is cleared (after menu action or tap outside)
    // If we were waiting to disable text selection mode, do it now
    if (!textSelectionPendingDisable) {
        return; // Not waiting for disable
    }
    
    textSelectionPendingDisable = false;
    
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    // Determine which button's settings to use
    bool *previousTextSelection = textSelectionWasButtonA ? 
        &previousTextSelectionModeA : &previousTextSelectionModeB;
    bool *activeFlag = textSelectionWasButtonA ?
        &stylusButtonAActive : &stylusButtonBActive;
    
    // Now complete the disable
    *activeFlag = false;
    canvas->setPdfTextSelectionEnabled(*previousTextSelection);
    updatePdfTextSelectButtonState();
#endif // Phase 3.1.8: onPdfTextSelectionCleared disabled
}

void MainWindow::handleStylusButtonPress(Qt::MouseButtons buttons) {
    // Check if any configured stylus button is now pressed
    if ((buttons & stylusButtonAQt) && stylusButtonAAction != StylusButtonAction::None) {
        enableStylusButtonMode(stylusButtonAQt);
    }
    if ((buttons & stylusButtonBQt) && stylusButtonBAction != StylusButtonAction::None) {
        enableStylusButtonMode(stylusButtonBQt);
    }
}

void MainWindow::handleStylusButtonRelease(Qt::MouseButtons buttons, Qt::MouseButton releasedButton) {
    // Check if a configured stylus button was released
    if (releasedButton == stylusButtonAQt || !(buttons & stylusButtonAQt)) {
        if (stylusButtonAActive) {
            disableStylusButtonMode(stylusButtonAQt);
        }
    }
    if (releasedButton == stylusButtonBQt || !(buttons & stylusButtonBQt)) {
        if (stylusButtonBActive) {
            disableStylusButtonMode(stylusButtonBQt);
        }
    }
}

void MainWindow::setTemporaryDialMode(DialMode mode) {
    if (temporaryDialMode == None) {
        temporaryDialMode = currentDialMode;
    }
    changeDialMode(mode);
}

void MainWindow::clearTemporaryDialMode() {
    if (temporaryDialMode != None) {
        changeDialMode(temporaryDialMode);
        temporaryDialMode = None;
    }
}



void MainWindow::handleButtonHeld(const QString &buttonName) {
    QString mode = buttonHoldMapping.value(buttonName, "None");
    if (mode != "None") {
        setTemporaryDialMode(dialModeFromString(mode));
        return;
    }
}

void MainWindow::handleButtonReleased(const QString &buttonName) {
    QString mode = buttonHoldMapping.value(buttonName, "None");
    if (mode != "None") {
        clearTemporaryDialMode();
    }
}

void MainWindow::setHoldMapping(const QString &buttonName, const QString &dialMode) {
    buttonHoldMapping[buttonName] = dialMode;
}

void MainWindow::setPressMapping(const QString &buttonName, const QString &action) {
    buttonPressMapping[buttonName] = action;
    buttonPressActionMapping[buttonName] = stringToAction(action);  // ✅ THIS LINE WAS MISSING
}


DialMode MainWindow::dialModeFromString(const QString &mode) {
    // Convert internal key to our existing DialMode enum
    InternalDialMode internalMode = ButtonMappingHelper::internalKeyToDialMode(mode);
    
    switch (internalMode) {
        case InternalDialMode::None: return PageSwitching; // Default fallback
        case InternalDialMode::PageSwitching: return PageSwitching;
        case InternalDialMode::ZoomControl: return ZoomControl;
        case InternalDialMode::ThicknessControl: return ThicknessControl;

        case InternalDialMode::ToolSwitching: return ToolSwitching;
        case InternalDialMode::PresetSelection: return PresetSelection;
        case InternalDialMode::PanAndPageScroll: return PanAndPageScroll;
    }
    return PanAndPageScroll;  // Default fallback
}

// MainWindow.cpp

QString MainWindow::getHoldMapping(const QString &buttonName) {
    return buttonHoldMapping.value(buttonName, "None");
}

QString MainWindow::getPressMapping(const QString &buttonName) {
    return buttonPressMapping.value(buttonName, "None");
}

void MainWindow::saveButtonMappings() {
    QSettings settings("SpeedyNote", "App");

    settings.beginGroup("ButtonHoldMappings");
    for (auto it = buttonHoldMapping.begin(); it != buttonHoldMapping.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();

    settings.beginGroup("ButtonPressMappings");
    for (auto it = buttonPressMapping.begin(); it != buttonPressMapping.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void MainWindow::loadButtonMappings() {
    QSettings settings("SpeedyNote", "App");

    // First, check if we need to migrate old settings
    migrateOldButtonMappings();

    settings.beginGroup("ButtonHoldMappings");
    QStringList holdKeys = settings.allKeys();
    for (const QString &key : holdKeys) {
        buttonHoldMapping[key] = settings.value(key, "none").toString();
    }
    settings.endGroup();

    settings.beginGroup("ButtonPressMappings");
    QStringList pressKeys = settings.allKeys();
    for (const QString &key : pressKeys) {
        QString value = settings.value(key, "none").toString();
        buttonPressMapping[key] = value;

        // ✅ Convert internal key to action enum
        buttonPressActionMapping[key] = stringToAction(value);
    }
    settings.endGroup();
}

void MainWindow::migrateOldButtonMappings() {
    QSettings settings("SpeedyNote", "App");
    
    // Check if migration is needed by looking for old format strings
    settings.beginGroup("ButtonHoldMappings");
    QStringList holdKeys = settings.allKeys();
    bool needsMigration = false;
    
    for (const QString &key : holdKeys) {
        QString value = settings.value(key).toString();
        // If we find old English strings, we need to migrate
        if (value == "PageSwitching" || value == "ZoomControl" || value == "ThicknessControl" ||
            value == "ToolSwitching" || value == "PresetSelection" ||
            value == "PanAndPageScroll") {
            needsMigration = true;
            break;
        }
    }
    settings.endGroup();
    
    if (!needsMigration) {
        settings.beginGroup("ButtonPressMappings");
        QStringList pressKeys = settings.allKeys();
        for (const QString &key : pressKeys) {
            QString value = settings.value(key).toString();
            // Check for old English action strings
            if (value == "Toggle Fullscreen" || value == "Toggle Dial" || value == "Zoom 50%" ||
                value == "Add Preset" || value == "Delete Page" || value == "Fast Forward" ||
                value == "Open Control Panel" || value == "Custom Color") {
                needsMigration = true;
                break;
            }
        }
        settings.endGroup();
    }
    
    if (!needsMigration) return;
    
    // Perform migration
    // qDebug() << "Migrating old button mappings to new format...";
    
    // Migrate hold mappings
    settings.beginGroup("ButtonHoldMappings");
    holdKeys = settings.allKeys();
    for (const QString &key : holdKeys) {
        QString oldValue = settings.value(key).toString();
        QString newValue = migrateOldDialModeString(oldValue);
        if (newValue != oldValue) {
            settings.setValue(key, newValue);
        }
    }
    settings.endGroup();
    
    // Migrate press mappings
    settings.beginGroup("ButtonPressMappings");
    QStringList pressKeys = settings.allKeys();
    for (const QString &key : pressKeys) {
        QString oldValue = settings.value(key).toString();
        QString newValue = migrateOldActionString(oldValue);
        if (newValue != oldValue) {
            settings.setValue(key, newValue);
        }
    }
    settings.endGroup();
    
   // qDebug() << "Button mapping migration completed.";
}

QString MainWindow::migrateOldDialModeString(const QString &oldString) {
    // Convert old English strings to new internal keys
    if (oldString == "None") return "none";
    if (oldString == "PageSwitching") return "page_switching";
    if (oldString == "ZoomControl") return "zoom_control";
    if (oldString == "ThicknessControl") return "thickness_control";

    if (oldString == "ToolSwitching") return "tool_switching";
    if (oldString == "PresetSelection") return "preset_selection";
    if (oldString == "PanAndPageScroll") return "pan_and_page_scroll";
    return oldString; // Return as-is if not found (might already be new format)
}

QString MainWindow::migrateOldActionString(const QString &oldString) {
    // Convert old English strings to new internal keys
    if (oldString == "None") return "none";
    if (oldString == "Toggle Fullscreen") return "toggle_fullscreen";
    if (oldString == "Toggle Dial") return "toggle_dial";
    if (oldString == "Zoom 50%") return "zoom_50";
    if (oldString == "Zoom Out") return "zoom_out";
    if (oldString == "Zoom 200%") return "zoom_200";
    if (oldString == "Add Preset") return "add_preset";
    if (oldString == "Delete Page") return "delete_page";
    if (oldString == "Fast Forward") return "fast_forward";
    if (oldString == "Open Control Panel") return "open_control_panel";
    if (oldString == "Red") return "red_color";
    if (oldString == "Blue") return "blue_color";
    if (oldString == "Yellow") return "yellow_color";
    if (oldString == "Green") return "green_color";
    if (oldString == "Black") return "black_color";
    if (oldString == "White") return "white_color";
    if (oldString == "Custom Color") return "custom_color";
    if (oldString == "Toggle Sidebar") return "toggle_sidebar";
    if (oldString == "Save") return "save";
    if (oldString == "Straight Line Tool") return "straight_line_tool";
    if (oldString == "Rope Tool") return "rope_tool";
    if (oldString == "Set Pen Tool") return "set_pen_tool";
    if (oldString == "Set Marker Tool") return "set_marker_tool";
    if (oldString == "Set Eraser Tool") return "set_eraser_tool";
    if (oldString == "Toggle PDF Text Selection") return "toggle_pdf_text_selection";
    return oldString; // Return as-is if not found (might already be new format)
}
void MainWindow::handleControllerButton(const QString &buttonName) {  // This is for single press functions
    ControllerAction action = buttonPressActionMapping.value(buttonName, ControllerAction::None);

    switch (action) {
        case ControllerAction::ToggleFullscreen:
            fullscreenButton->click();
            break;
        case ControllerAction::ToggleDial:
            toggleDial();
            break;
        case ControllerAction::Zoom50:
            zoom50Button->click();
            break;
        case ControllerAction::ZoomOut:
            dezoomButton->click();
            break;
        case ControllerAction::Zoom200:
            zoom200Button->click();
            break;
        case ControllerAction::AddPreset:
            addPresetButton->click();
            break;
        case ControllerAction::DeletePage:
            deletePageButton->click();  // assuming you have this
            break;
        case ControllerAction::FastForward:
            fastForwardButton->click();  // assuming you have this
            break;
        case ControllerAction::OpenControlPanel:
            openControlPanelButton->click();
            break;
        case ControllerAction::RedColor:
            redButton->click();
            break;
        case ControllerAction::BlueColor:
            blueButton->click();
            break;
        case ControllerAction::YellowColor:
            yellowButton->click();
            break;
        case ControllerAction::GreenColor:
            greenButton->click();
            break;
        case ControllerAction::BlackColor:
            blackButton->click();
            break;
        case ControllerAction::WhiteColor:
            whiteButton->click();
            break;
        case ControllerAction::CustomColor:
            customColorButton->click();
            break;
        case ControllerAction::ToggleSidebar:
            toggleTabBarButton->click();
            break;
        case ControllerAction::Save:
            saveButton->click();
            break;
        case ControllerAction::StraightLineTool:
            straightLineToggleButton->click();
            break;
        case ControllerAction::RopeTool:
            ropeToolButton->click();
            break;
        case ControllerAction::SetPenTool:
            setPenTool();
            break;
        case ControllerAction::SetMarkerTool:
            setMarkerTool();
            break;
        case ControllerAction::SetEraserTool:
            setEraserTool();
            break;
        case ControllerAction::TogglePdfTextSelection:
            pdfTextSelectButton->click();
            break;
        case ControllerAction::ToggleOutline:
            toggleOutlineButton->click();
            break;
        case ControllerAction::ToggleBookmarks:
            toggleBookmarksButton->click();
            break;
        case ControllerAction::AddBookmark:
            toggleBookmarkButton->click();
            break;
        case ControllerAction::ToggleTouchGestures:
            touchGesturesButton->click();
            break;
        case ControllerAction::PreviousPage:
            goToPreviousPage();
            break;
        case ControllerAction::NextPage:
            goToNextPage();
            break;
        default:
            break;
    }
}




void MainWindow::openPdfFile(const QString &pdfPath) {
    // Phase 3.1.8: Stubbed - PDF opening will use DocumentViewport
    Q_UNUSED(pdfPath);
    QMessageBox::information(this, tr("Open PDF"), 
        tr("PDF opening from file association is being redesigned. Coming soon!"));
    return;

#if 0 // Phase 3.1.8: Old PDF opening code disabled
    // Check if the PDF file exists
    if (!QFile::exists(pdfPath)) {
        QMessageBox::warning(this, tr("File Not Found"), tr("The PDF file could not be found:\n%1").arg(pdfPath));
        return;
    }

    // First, check if there's already a valid notebook folder for this PDF
    QString existingFolderPath;
    if (PdfOpenDialog::hasValidNotebookFolder(pdfPath, existingFolderPath)) {
        // Found a valid notebook folder, open it directly without showing dialog
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;

        // Save current work if edited
        if (canvas->isEdited()) {
            saveCurrentPage();
        }
        
        // Set the existing folder as save folder
        canvas->setSaveFolder(existingFolderPath);
        
        // Load the PDF
        canvas->loadPdf(pdfPath);
        
        // ✅ Automatically enable scroll on top when PDF is loaded (required for pseudo smooth scrolling)
        setScrollOnTopEnabled(true);
        
        // Update tab label
        updateTabLabel();
        updateBookmarkButtonState(); // ✅ Update bookmark button state after loading notebook
        
        // ✅ Show last accessed page dialog if available
        if (!showLastAccessedPageDialog(canvas)) {
            // No last accessed page, start from page 1
            switchPageWithDirection(1, 1);
            pageInput->setValue(1);
        } else {
            // Dialog handled page switching, update page input
            pageInput->setValue(getCurrentPageForCanvas(canvas) + 1);
        }
        updateZoom();
        updatePanRange();
        
        // ✅ Add to recent notebooks AFTER PDF is loaded to ensure proper thumbnail generation
        if (recentNotebooksManager) {
            // Use QPointer to safely handle canvas deletion
            QPointer<InkCanvas> canvasPtr(canvas);
            
            // Phase 3.1: recentNotebooksManager and sharedLauncher disconnected
            // if (canvasPtr && canvasPtr->isPdfLoadedFunc()) {
            //     recentNotebooksManager->addRecentNotebook(existingFolderPath, canvasPtr.data());
            //     if (sharedLauncher && sharedLauncher->isVisible()) {
            //         sharedLauncher->refreshRecentNotebooks();
            //     }
            // }
        }
        
        return; // Exit early, no need to show dialog
    }
    
    // No valid notebook folder found, show the dialog with options
    PdfOpenDialog dialog(pdfPath, this);
    dialog.exec();
    
    PdfOpenDialog::Result result = dialog.getResult();
    QString selectedFolder = dialog.getSelectedFolder();
    
    if (result == PdfOpenDialog::Cancel) {
        return; // User cancelled, do nothing
    }
    
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    // Save current work if edited
    if (canvas->isEdited()) {
        saveCurrentPage();
    }
    
    if (result == PdfOpenDialog::CreateNewFolder || result == PdfOpenDialog::CreateNewFolderCustomLocation) {
        // Set the new folder as save folder
        canvas->setSaveFolder(selectedFolder);
        
        // ✅ Apply default background settings to new PDF notebook
        applyDefaultBackgroundToCanvas(canvas);
        
        // Load the PDF
        canvas->loadPdf(pdfPath);
        
        // ✅ Automatically enable scroll on top when PDF is loaded (required for pseudo smooth scrolling)
        setScrollOnTopEnabled(true);
        
        // Update tab label
        updateTabLabel();
        
        // Switch to page 1 for new folders (no last accessed page)
        switchPageWithDirection(1, 1);
        pageInput->setValue(1);
        updateZoom();
        updatePanRange();
        
        // Phase 3.1: recentNotebooksManager and sharedLauncher disconnected
        // if (recentNotebooksManager) {
        //     // Add to recent notebooks after PDF load
        // }
        
    } else if (result == PdfOpenDialog::UseExistingFolder) {
        // ✅ Check if the existing folder is linked to the same PDF using JSON metadata
        canvas->setSaveFolder(selectedFolder); // Load metadata first
        QString existingPdfPath = canvas->getPdfPath();
        bool isLinkedToSamePdf = false;
        
        if (!existingPdfPath.isEmpty()) {
            // Compare absolute paths
            QFileInfo existingInfo(existingPdfPath);
            QFileInfo newInfo(pdfPath);
            isLinkedToSamePdf = (existingInfo.absoluteFilePath() == newInfo.absoluteFilePath());
        }
        
        if (!isLinkedToSamePdf && !existingPdfPath.isEmpty()) {
            // Folder is linked to a different PDF, ask user what to do
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Different PDF Linked"),
                tr("This notebook folder is already linked to a different PDF file.\n\nDo you want to replace the link with the new PDF?"),
                QMessageBox::Yes | QMessageBox::No
            );
            
            if (reply == QMessageBox::No) {
                return; // User chose not to replace
            }
        }
        
        // Set the existing folder as save folder
        canvas->setSaveFolder(selectedFolder);
        
        // ✅ Handle missing PDF file if it's a .spn package
        if (SpnPackageManager::isSpnPackage(selectedFolder)) {
            if (!canvas->handleMissingPdf(this)) {
                // User cancelled PDF relinking, don't continue
                return;
            }
            // ✅ Update scroll behavior based on PDF loading state after relinking
            setScrollOnTopEnabled(canvas->isPdfLoadedFunc());
        } else {
            // Load the PDF for regular folders
            canvas->loadPdf(pdfPath);
            // ✅ Automatically enable scroll on top when PDF is loaded (required for pseudo smooth scrolling)
            setScrollOnTopEnabled(true);
        }
        
        // Update tab label
        updateTabLabel();
        updateBookmarkButtonState(); // ✅ Update bookmark button state after loading notebook
        
        // ✅ Show last accessed page dialog if available for existing folders
        if (!showLastAccessedPageDialog(canvas)) {
            // No last accessed page or user chose page 1
            switchPageWithDirection(1, 1);
            pageInput->setValue(1);
        } else {
            // Dialog handled page switching, update page input
            pageInput->setValue(getCurrentPageForCanvas(canvas) + 1);
        }
        updateZoom();
        updatePanRange();
        
        // Phase 3.1: recentNotebooksManager and sharedLauncher disconnected
        // if (recentNotebooksManager) {
        //     // Add to recent notebooks after PDF load
        // }
    }
#endif // Phase 3.1.8: openPdfFile disabled
}

void MainWindow::setPdfDPI(int dpi) {
    if (dpi != pdfRenderDPI) {
        pdfRenderDPI = dpi;
        savePdfDPI(dpi);

        // Phase 3.1.8: Stubbed - PDF DPI will apply to DocumentViewport when PDF support is added
        // TODO Phase 3.4: Apply DPI setting to currentViewport() when PDF rendering is implemented
    }
}

void MainWindow::savePdfDPI(int dpi) {
    QSettings settings("SpeedyNote", "App");
    settings.setValue("pdfRenderDPI", dpi);
}

void MainWindow::loadUserSettings() {
    QSettings settings("SpeedyNote", "App");

    // Load low-res toggle
    lowResPreviewEnabled = settings.value("lowResPreviewEnabled", true).toBool();
    setLowResPreviewEnabled(lowResPreviewEnabled);

    
    zoomButtonsVisible = settings.value("zoomButtonsVisible", true).toBool();
    setZoomButtonsVisible(zoomButtonsVisible);

    scrollOnTopEnabled = settings.value("scrollOnTopEnabled", true).toBool();
    setScrollOnTopEnabled(scrollOnTopEnabled);

    // Load touch gesture mode (default to Full for backwards compatibility)
    int savedMode = settings.value("touchGestureMode", static_cast<int>(TouchGestureMode::Full)).toInt();
    touchGestureMode = static_cast<TouchGestureMode>(savedMode);
    setTouchGestureMode(touchGestureMode);
    
    // Update button visual state to match loaded setting
    touchGesturesButton->setProperty("selected", touchGestureMode != TouchGestureMode::Disabled);
    touchGesturesButton->setProperty("yAxisOnly", touchGestureMode == TouchGestureMode::YAxisOnly);
    updateButtonIcon(touchGesturesButton, "hand");
    touchGesturesButton->style()->unpolish(touchGesturesButton);
    touchGesturesButton->style()->polish(touchGesturesButton);
    
#ifdef Q_OS_LINUX
    // Load palm rejection settings (Linux only)
    palmRejectionEnabled = settings.value("palmRejectionEnabled", false).toBool();
    palmRejectionDelayMs = settings.value("palmRejectionDelayMs", 500).toInt();
#endif
    
    // Load stylus button settings
    loadStylusButtonSettings();
    
    // Phase doc-1: Migrate from old BackgroundStyle enum to Page::BackgroundType
    // Old enum: None=0, Grid=1, Lines=2
    // New enum: None=0, PDF=1, Custom=2, Grid=3, Lines=4
    // Using new key "defaultBgType" to avoid loading stale values from old enum
    if (!settings.contains("defaultBgType")) {
        // Clear old key if it exists (from pre-migration)
        if (settings.contains("defaultBackgroundStyle")) {
            settings.remove("defaultBackgroundStyle");
        }
        saveDefaultBackgroundSettings(Page::BackgroundType::Grid, Qt::white, QColor(200, 200, 200), 30);
    }
    
    // Load keyboard mappings
    loadKeyboardMappings();
    
    // Load theme settings
    loadThemeSettings();
}

void MainWindow::toggleControlBar() {
    // Proper fullscreen toggle: handle both sidebar and control bar
    
    if (controlBarVisible) {
        // Going into fullscreen mode
        
        // First, remember current tab bar state
        QTabBar* tabBar = m_tabWidget->tabBar();
        sidebarWasVisibleBeforeFullscreen = tabBar->isVisible();
        
        // Hide tab bar if it's visible
        if (tabBar->isVisible()) {
            tabBar->setVisible(false);
        }
        
        // Hide control bar
        controlBarVisible = false;
        controlBar->setVisible(false);
        
        // Hide floating popup widgets when control bar is hidden to prevent stacking
        if (zoomFrame && zoomFrame->isVisible()) zoomFrame->hide();
        if (thicknessFrame && thicknessFrame->isVisible()) thicknessFrame->hide();
        
        // Hide orphaned widgets that are not added to any layout
        // Removed colorPreview widget - no longer needed
        // thicknessButton and jumpToPageButton are now properly in the layout

        // toolSelector is now properly hidden with size 0x0
        if (zoomButton) zoomButton->hide();
        if (customColorInput) customColorInput->hide();
        
        // Find and hide local widgets that might be orphaned
        QList<QComboBox*> comboBoxes = findChildren<QComboBox*>();
        for (QComboBox* combo : comboBoxes) {
            if (combo->parent() == this && !combo->isVisible()) {
                // Already hidden, keep it hidden
            } else if (combo->parent() == this) {
                // This might be other orphaned combo boxes
                combo->hide();
            }
        }
    } else {
        // Coming out of fullscreen mode
        
        // Restore control bar
        controlBarVisible = true;
        controlBar->setVisible(true);
        
        // Restore tab bar to its previous state
        m_tabWidget->tabBar()->setVisible(sidebarWasVisibleBeforeFullscreen);
        
        // Show widgets that are now properly in the layout
        // thicknessButton and jumpToPageButton are now in the layout so they'll be visible automatically
    }
    
    // Update dial display to reflect new status
    updateDialDisplay();
    
    // Phase 3.1.8: Canvas size management stubbed - DocumentViewport handles its own size
    // Force layout update to recalculate space
    // TODO Phase 3.3: Implement viewport size management if needed
}

void MainWindow::cycleZoomLevels() {
    if (!zoomSlider) return;
    
    int currentZoom = zoomSlider->value();
    int targetZoom;
    
    // Calculate the scaled zoom levels based on initial DPR
    int zoom50 = qRound(50.0 / initialDpr);
    int zoom100 = qRound(100.0 / initialDpr);
    int zoom200 = qRound(200.0 / initialDpr);
    
    // Cycle through 0.5x -> 1x -> 2x -> 0.5x...
    if (currentZoom <= zoom50 + 5) { // Close to 0.5x (with small tolerance)
        targetZoom = zoom100; // Go to 1x
    } else if (currentZoom <= zoom100 + 5) { // Close to 1x
        targetZoom = zoom200; // Go to 2x
    } else { // Any other zoom level or close to 2x
        targetZoom = zoom50; // Go to 0.5x
    }
    
    zoomSlider->setValue(targetZoom);
    updateZoom();
    updateDialDisplay();
}

void MainWindow::handleTouchZoomChange(int newZoom) {
    // Phase 3.1.5: Stubbed - will connect to DocumentViewport in Phase 3.3
    Q_UNUSED(newZoom);
    // TODO Phase 3.3: Connect to DocumentViewport zoom handling
}

void MainWindow::handleTouchPanChange(int panX, int panY) {
    // Phase 3.1.5: Stubbed - will connect to DocumentViewport in Phase 3.3
    Q_UNUSED(panX);
    Q_UNUSED(panY);
    // TODO Phase 3.3: Connect to DocumentViewport pan handling
}

void MainWindow::handleTouchGestureEnd() {
    // Phase 3.1.5: Stubbed - will connect to DocumentViewport in Phase 3.3
    // TODO Phase 3.3: Hide scrollbars after gesture ends
}

void MainWindow::handleTouchPanningChanged(bool active) {
    // Phase 3.1.5: Stubbed - InkCanvas-specific PictureWindowManager optimization
    Q_UNUSED(active);
    // TODO Phase 4: Reimplement for DocumentViewport if picture windows are added
}

void MainWindow::updateColorButtonStates() {
    // Phase 3.1.4: Use currentViewport()
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    // Get current pen color
    QColor currentColor = vp->penColor();
    
    // Determine if we're in dark mode to match the correct colors
    bool darkMode = isDarkMode();
    
    // Reset all color buttons to original style
    redButton->setProperty("selected", false);
    blueButton->setProperty("selected", false);
    yellowButton->setProperty("selected", false);
    greenButton->setProperty("selected", false);
    blackButton->setProperty("selected", false);
    whiteButton->setProperty("selected", false);
    
    // Update all color button icons to unselected state
    QString redIconName = darkMode ? "pen_light_red" : "pen_dark_red";
    QString blueIconName = darkMode ? "pen_light_blue" : "pen_dark_blue";
    QString yellowIconName = darkMode ? "pen_light_yellow" : "pen_dark_yellow";
    QString greenIconName = darkMode ? "pen_light_green" : "pen_dark_green";
    QString blackIconName = darkMode ? "pen_light_black" : "pen_dark_black";
    QString whiteIconName = darkMode ? "pen_light_white" : "pen_dark_white";
    
    // Set the selected property for the matching color button based on current palette
    QColor redColor = getPaletteColor("red");
    QColor blueColor = getPaletteColor("blue");
    QColor yellowColor = getPaletteColor("yellow");
    QColor greenColor = getPaletteColor("green");
    
    if (currentColor == redColor) {
        redButton->setProperty("selected", true);
        // For color buttons, we don't reverse the icon - the colored pen icon should stay
    } else if (currentColor == blueColor) {
        blueButton->setProperty("selected", true);
    } else if (currentColor == yellowColor) {
        yellowButton->setProperty("selected", true);
    } else if (currentColor == greenColor) {
        greenButton->setProperty("selected", true);
    } else if (currentColor == QColor("#000000")) {
        blackButton->setProperty("selected", true);
    } else if (currentColor == QColor("#FFFFFF")) {
        whiteButton->setProperty("selected", true);
    }
    
    // Force style update
    redButton->style()->unpolish(redButton);
    redButton->style()->polish(redButton);
    blueButton->style()->unpolish(blueButton);
    blueButton->style()->polish(blueButton);
    yellowButton->style()->unpolish(yellowButton);
    yellowButton->style()->polish(yellowButton);
    greenButton->style()->unpolish(greenButton);
    greenButton->style()->polish(greenButton);
    blackButton->style()->unpolish(blackButton);
    blackButton->style()->polish(blackButton);
    whiteButton->style()->unpolish(whiteButton);
    whiteButton->style()->polish(whiteButton);
}

void MainWindow::selectColorButton(QPushButton* selectedButton) {
    updateColorButtonStates();
}

QColor MainWindow::getContrastingTextColor(const QColor &backgroundColor) {
    // Calculate relative luminance using the formula from WCAG 2.0
    double r = backgroundColor.redF();
    double g = backgroundColor.greenF();
    double b = backgroundColor.blueF();
    
    // Gamma correction
    r = (r <= 0.03928) ? r/12.92 : pow((r + 0.055)/1.055, 2.4);
    g = (g <= 0.03928) ? g/12.92 : pow((g + 0.055)/1.055, 2.4);
    b = (b <= 0.03928) ? b/12.92 : pow((b + 0.055)/1.055, 2.4);
    
    // Calculate luminance
    double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    
    // Use white text for darker backgrounds
    return (luminance < 0.5) ? Qt::white : Qt::black;
}

void MainWindow::updateCustomColorButtonStyle(const QColor &color) {
    QColor textColor = getContrastingTextColor(color);
    customColorButton->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 0px;")
        .arg(color.name())
        .arg(textColor.name()));
    customColorButton->setText(QString("%1").arg(color.name()).toUpper());
}

void MainWindow::updateStraightLineButtonState() {
    // Phase 3.1.4: Stubbed - straight line mode not implemented yet
    if (straightLineToggleButton) {
        straightLineToggleButton->setProperty("selected", false);
        updateButtonIcon(straightLineToggleButton, "straightLine");
        straightLineToggleButton->style()->unpolish(straightLineToggleButton);
        straightLineToggleButton->style()->polish(straightLineToggleButton);
    }
}

void MainWindow::updateRopeToolButtonState() {
    // Phase 3.1.4: Stubbed - rope tool mode not implemented yet
    if (ropeToolButton) {
        ropeToolButton->setProperty("selected", false);
        updateButtonIcon(ropeToolButton, "rope");
        ropeToolButton->style()->unpolish(ropeToolButton);
        ropeToolButton->style()->polish(ropeToolButton);
    }
}

void MainWindow::updatePictureButtonState() {
    // Phase 3.1.4: Stubbed - picture insertion not implemented yet
    bool isEnabled = false;

    // Set visual indicator that the button is active/inactive
    if (insertPictureButton) {
        insertPictureButton->setProperty("selected", isEnabled);
        updateButtonIcon(insertPictureButton, "background");

        // Force style update
        insertPictureButton->style()->unpolish(insertPictureButton);
        insertPictureButton->style()->polish(insertPictureButton);
    }
}

void MainWindow::updateDialButtonState() {
    // Check if dial is visible
    bool isDialVisible = dialContainer && dialContainer->isVisible();
    
    if (dialToggleButton) {
        dialToggleButton->setProperty("selected", isDialVisible);
        updateButtonIcon(dialToggleButton, "dial");
        
        // Force style update
        dialToggleButton->style()->unpolish(dialToggleButton);
        dialToggleButton->style()->polish(dialToggleButton);
    }
}

void MainWindow::updateFastForwardButtonState() {
    if (fastForwardButton) {
        fastForwardButton->setProperty("selected", fastForwardMode);
        updateButtonIcon(fastForwardButton, "fastforward");
        
        // Force style update
        fastForwardButton->style()->unpolish(fastForwardButton);
        fastForwardButton->style()->polish(fastForwardButton);
    }
}

// Add this new method
void MainWindow::updateScrollbarPositions() {
    // Phase 3.1: Use m_tabWidget instead of canvasStack
    QWidget *container = m_tabWidget ? m_tabWidget->parentWidget() : nullptr;
    if (!container || !panXSlider || !panYSlider || !m_tabWidget) return;
    
    // Get tab bar height to offset positions (sliders should be below tab bar)
    int tabBarHeight = m_tabWidget->tabBar()->isVisible() ? m_tabWidget->tabBar()->height() : 0;
    
    // Add small margins for better visibility
    const int margin = 3;
    
    // Get scrollbar dimensions - use fixed values since setFixedHeight/Width was called
    const int scrollbarWidth = 16;  // panYSlider fixed width
    const int scrollbarHeight = 16; // panXSlider fixed height
    
    // Calculate sizes based on container
    int containerWidth = container->width();
    int containerHeight = container->height();
    
    // Leave a bit of space for the corner
    int cornerOffset = 15;
    
    // Position horizontal scrollbar at top (BELOW tab bar)
    panXSlider->setGeometry(
        cornerOffset + margin,  // Leave space at left corner
        tabBarHeight + margin,  // Below tab bar
        containerWidth - cornerOffset - margin*2,  // Full width minus corner and right margin
        scrollbarHeight
    );
    
    // Position vertical scrollbar at left (starting below tab bar)
    panYSlider->setGeometry(
        margin,
        tabBarHeight + cornerOffset + margin,  // Below tab bar + corner offset
        scrollbarWidth,
        containerHeight - tabBarHeight - cornerOffset - margin*2  // Height minus tab bar
    );
    
    // Ensure sliders are raised above content
    panXSlider->raise();
    panYSlider->raise();
}
void MainWindow::handleEdgeProximity(InkCanvas* canvas, const QPoint& pos) {
    // Phase 3.1.7: Stubbed - InkCanvas-specific edge detection
    Q_UNUSED(canvas);
    Q_UNUSED(pos);
    // TODO Phase 3.3: Implement for DocumentViewport if needed
}

void MainWindow::returnToLauncher() {
    // Phase 3.1: LauncherWindow disconnected - will be re-linked later
    // TODO Phase 3.5: Re-implement launcher return functionality
    QMessageBox::information(this, tr("Return to Launcher"), 
        tr("Launcher is being redesigned. This feature will return soon!"));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    
    // Use a timer to delay layout updates during resize to prevent excessive switching
    if (!layoutUpdateTimer) {
        layoutUpdateTimer = new QTimer(this);
        layoutUpdateTimer->setSingleShot(true);
        connect(layoutUpdateTimer, &QTimer::timeout, this, [this]() {
            updateToolbarLayout();
            updateTabSizes(); // Update tab widths when window resizes
            // Reposition floating sidebar tabs
            positionLeftSidebarTabs();
            positionDialToolbarTab();
            // Also reposition dial after resize finishes
            if (dialContainer && dialContainer->isVisible()) {
                positionDialContainer();
            }
        });
    }
    
    layoutUpdateTimer->stop();
    layoutUpdateTimer->start(100); // Wait 100ms after resize stops
}

void MainWindow::updateToolbarLayout() {
    int windowWidth = width();
    
    // Thresholds:
    // >= 1090: Single row with centered buttons (left spacer compensates for right buttons)
    // < 1090 and >= 1020: Single row without centering (left spacer removed, buttons can use full width)
    // < 1020: Two-row layout
    const int centeringThreshold = 1090;
    const int twoRowThreshold = 1020;
    
    bool shouldBeTwoRows = windowWidth < twoRowThreshold;
    bool shouldBeCentered = windowWidth >= centeringThreshold;
    
    // Track if we need to recreate the layout
    static bool wasCentered = true;  // Start with centered assumption
    
    if (shouldBeTwoRows != isToolbarTwoRows) {
        isToolbarTwoRows = shouldBeTwoRows;
        
        if (isToolbarTwoRows) {
            createTwoRowLayout();
        } else {
            createSingleRowLayout(shouldBeCentered);
        }
        wasCentered = shouldBeCentered;
    } else if (!isToolbarTwoRows && shouldBeCentered != wasCentered) {
        // Still single row, but centering mode changed
        createSingleRowLayout(shouldBeCentered);
        wasCentered = shouldBeCentered;
    }
}

void MainWindow::createSingleRowLayout(bool centered) {
    // Delete separator line if it exists (from previous 2-row layout)
    if (separatorLine) {
        delete separatorLine;
        separatorLine = nullptr;
    }
    
    // Create new single row layout
    QHBoxLayout *newLayout = new QHBoxLayout;
    
    // When centered mode is enabled (wide window), add a left spacer to compensate
    // for the right-aligned buttons, making the center buttons truly centered.
    // When not centered (narrower window), skip the spacer so buttons can use full width.
    if (centered) {
        // Right buttons: toggleBookmarkButton(36) + pageInput(36) + overflowMenuButton(30) + deletePageButton(22) + spacing
        const int rightButtonsWidth = 130;
        QSpacerItem *leftSpacer = new QSpacerItem(rightButtonsWidth, 0, QSizePolicy::Preferred, QSizePolicy::Minimum);
        newLayout->addSpacerItem(leftSpacer);
    }
    
    // Left stretch to center the main buttons
    newLayout->addStretch();
    
    // Centered buttons - toggle and utility
    newLayout->addWidget(toggleTabBarButton);
        newLayout->addWidget(toggleMarkdownNotesButton);
        newLayout->addWidget(touchGesturesButton);
    newLayout->addWidget(pdfTextSelectButton);
    newLayout->addWidget(saveButton);
    
    // Color buttons
    newLayout->addWidget(redButton);
    newLayout->addWidget(blueButton);
    newLayout->addWidget(yellowButton);
    newLayout->addWidget(greenButton);
    newLayout->addWidget(blackButton);
    newLayout->addWidget(whiteButton);
    newLayout->addWidget(customColorButton);
    
    // Tool buttons
    newLayout->addWidget(penToolButton);
    newLayout->addWidget(markerToolButton);
    newLayout->addWidget(eraserToolButton);
    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    newLayout->addWidget(straightLineToggleButton);
    newLayout->addWidget(ropeToolButton);
    newLayout->addWidget(insertPictureButton);
    newLayout->addWidget(fullscreenButton);
    
    // Right stretch to center the main buttons
    newLayout->addStretch();
    
    // Page controls and overflow menu on the right (fixed position)
    newLayout->addWidget(toggleBookmarkButton);
    newLayout->addWidget(pageInput);
    newLayout->addWidget(overflowMenuButton);
    newLayout->addWidget(deletePageButton);
    
    // Benchmark controls (visibility controlled by settings)
    newLayout->addWidget(benchmarkButton);
    newLayout->addWidget(benchmarkLabel);
    
    // Safely replace the layout
    QLayout* oldLayout = controlBar->layout();
    if (oldLayout) {
        // Remove all items from old layout (but don't delete widgets)
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            // Just removing, not deleting widgets
        }
        delete oldLayout;
    }
    
    // Set the new layout
    controlBar->setLayout(newLayout);
    controlLayoutSingle = newLayout;
    
    // Clean up other layout pointers
    controlLayoutVertical = nullptr;
    controlLayoutFirstRow = nullptr;
    controlLayoutSecondRow = nullptr;
    
    // Update pan range after layout change
    updatePanRange();
}

void MainWindow::createTwoRowLayout() {
    // Two-row layout for narrow windows
    
    // Create new layouts
    QVBoxLayout *newVerticalLayout = new QVBoxLayout;
    QHBoxLayout *newFirstRowLayout = new QHBoxLayout;
    QHBoxLayout *newSecondRowLayout = new QHBoxLayout;
    
    // Add comfortable spacing and margins
    newFirstRowLayout->setContentsMargins(8, 8, 8, 6);
    newFirstRowLayout->setSpacing(3);
    newSecondRowLayout->setContentsMargins(8, 6, 8, 8);
    newSecondRowLayout->setSpacing(3);
    
    // First row: toggle buttons and colors (centered - no right buttons, so no compensation needed)
    newFirstRowLayout->addStretch();
    newFirstRowLayout->addWidget(toggleTabBarButton);
        newFirstRowLayout->addWidget(toggleMarkdownNotesButton);
        newFirstRowLayout->addWidget(touchGesturesButton);
    newFirstRowLayout->addWidget(pdfTextSelectButton);
    newFirstRowLayout->addWidget(saveButton);
    newFirstRowLayout->addWidget(redButton);
    newFirstRowLayout->addWidget(blueButton);
    newFirstRowLayout->addWidget(yellowButton);
    newFirstRowLayout->addWidget(greenButton);
    newFirstRowLayout->addWidget(blackButton);
    newFirstRowLayout->addWidget(whiteButton);
    newFirstRowLayout->addWidget(customColorButton);
    newFirstRowLayout->addStretch();
    
    // Create a separator line
    if (!separatorLine) {
        separatorLine = new QFrame();
        separatorLine->setFrameShape(QFrame::HLine);
        separatorLine->setFrameShadow(QFrame::Sunken);
        separatorLine->setLineWidth(1);
        separatorLine->setStyleSheet("QFrame { color: rgba(255, 255, 255, 255); }");
    }
    
    // Calculate the width of right-aligned buttons to create a compensating left spacer
    // Right buttons: toggleBookmarkButton(36) + pageInput(36) + overflowMenuButton(30) + deletePageButton(22) + spacing
    const int rightButtonsWidth = 130;
    
    // Second row: tool buttons (centered) and page controls (right)
    // Left spacer to compensate for right-aligned buttons (can shrink when window is narrow)
    QSpacerItem *leftSpacer = new QSpacerItem(rightButtonsWidth, 0, QSizePolicy::Preferred, QSizePolicy::Minimum);
    newSecondRowLayout->addSpacerItem(leftSpacer);
    
    newSecondRowLayout->addStretch();
    newSecondRowLayout->addWidget(penToolButton);
    newSecondRowLayout->addWidget(markerToolButton);
    newSecondRowLayout->addWidget(eraserToolButton);
    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    newSecondRowLayout->addWidget(straightLineToggleButton);
    newSecondRowLayout->addWidget(ropeToolButton);
    newSecondRowLayout->addWidget(insertPictureButton);
    newSecondRowLayout->addWidget(fullscreenButton);
    
    newSecondRowLayout->addStretch();
    
    newSecondRowLayout->addWidget(toggleBookmarkButton);
    newSecondRowLayout->addWidget(pageInput);
    newSecondRowLayout->addWidget(overflowMenuButton);
    newSecondRowLayout->addWidget(deletePageButton);
    
    // Benchmark controls (visibility controlled by settings)
    newSecondRowLayout->addWidget(benchmarkButton);
    newSecondRowLayout->addWidget(benchmarkLabel);
    
    // Add layouts to vertical layout with separator
    newVerticalLayout->addLayout(newFirstRowLayout);
    newVerticalLayout->addWidget(separatorLine);
    newVerticalLayout->addLayout(newSecondRowLayout);
    newVerticalLayout->setContentsMargins(0, 0, 0, 0);
    newVerticalLayout->setSpacing(0);
    
    // Safely replace the layout
    QLayout* oldLayout = controlBar->layout();
    if (oldLayout) {
        // Remove all items from old layout (but don't delete widgets)
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            // Just removing, not deleting widgets
        }
        delete oldLayout;
    }
    
    // Set the new layout
    controlBar->setLayout(newVerticalLayout);
    controlLayoutVertical = newVerticalLayout;
    controlLayoutFirstRow = newFirstRowLayout;
    controlLayoutSecondRow = newSecondRowLayout;
    
    // Clean up other layout pointer
    controlLayoutSingle = nullptr;
    
    // Update pan range after layout change
    updatePanRange();
}

// New: Keyboard mapping implementation
void MainWindow::handleKeyboardShortcut(const QString &keySequence) {
    ControllerAction action = keyboardActionMapping.value(keySequence, ControllerAction::None);
    
    // Use the same handler as Joy-Con buttons
    switch (action) {
        case ControllerAction::ToggleFullscreen:
            fullscreenButton->click();
            break;
        case ControllerAction::ToggleDial:
            toggleDial();
            break;
        case ControllerAction::Zoom50:
            zoom50Button->click();
            break;
        case ControllerAction::ZoomOut:
            dezoomButton->click();
            break;
        case ControllerAction::Zoom200:
            zoom200Button->click();
            break;
        case ControllerAction::AddPreset:
            addPresetButton->click();
            break;
        case ControllerAction::DeletePage:
            deletePageButton->click();
            break;
        case ControllerAction::FastForward:
            fastForwardButton->click();
            break;
        case ControllerAction::OpenControlPanel:
            openControlPanelButton->click();
            break;
        case ControllerAction::RedColor:
            redButton->click();
            break;
        case ControllerAction::BlueColor:
            blueButton->click();
            break;
        case ControllerAction::YellowColor:
            yellowButton->click();
            break;
        case ControllerAction::GreenColor:
            greenButton->click();
            break;
        case ControllerAction::BlackColor:
            blackButton->click();
            break;
        case ControllerAction::WhiteColor:
            whiteButton->click();
            break;
        case ControllerAction::CustomColor:
            customColorButton->click();
            break;
        case ControllerAction::ToggleSidebar:
            toggleTabBarButton->click();
            break;
        case ControllerAction::Save:
            saveButton->click();
            break;
        case ControllerAction::StraightLineTool:
            straightLineToggleButton->click();
            break;
        case ControllerAction::RopeTool:
            ropeToolButton->click();
            break;
        case ControllerAction::SetPenTool:
            setPenTool();
            break;
        case ControllerAction::SetMarkerTool:
            setMarkerTool();
            break;
        case ControllerAction::SetEraserTool:
            setEraserTool();
            break;
        case ControllerAction::TogglePdfTextSelection:
            pdfTextSelectButton->click();
            break;
        case ControllerAction::ToggleOutline:
            toggleOutlineButton->click();
            break;
        case ControllerAction::ToggleBookmarks:
            toggleBookmarksButton->click();
            break;
        case ControllerAction::AddBookmark:
            toggleBookmarkButton->click();
            break;
        case ControllerAction::ToggleTouchGestures:
            touchGesturesButton->click();
            break;
        case ControllerAction::PreviousPage:
            goToPreviousPage();
            break;
        case ControllerAction::NextPage:
            goToNextPage();
            break;
        default:
            break;
    }
}

void MainWindow::addKeyboardMapping(const QString &keySequence, const QString &action) {
    // List of IME-related shortcuts that should not be intercepted
    QStringList imeShortcuts = {
        "Ctrl+Space",      // Primary IME toggle
        "Ctrl+Shift",      // Language switching
        "Ctrl+Alt",        // IME functions
        "Shift+Alt",       // Alternative language switching
        "Alt+Shift"        // Alternative language switching (reversed)
    };
    
    // Don't allow mapping of IME-related shortcuts
    if (imeShortcuts.contains(keySequence)) {
        qWarning() << "Cannot map IME-related shortcut:" << keySequence;
        return;
    }
    
    keyboardMappings[keySequence] = action;
    keyboardActionMapping[keySequence] = stringToAction(action);
    saveKeyboardMappings();
}

void MainWindow::removeKeyboardMapping(const QString &keySequence) {
    keyboardMappings.remove(keySequence);
    keyboardActionMapping.remove(keySequence);
    saveKeyboardMappings();
}

void MainWindow::saveKeyboardMappings() {
    QSettings settings("SpeedyNote", "App");
    settings.beginGroup("KeyboardMappings");
    for (auto it = keyboardMappings.begin(); it != keyboardMappings.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void MainWindow::loadKeyboardMappings() {
    QSettings settings("SpeedyNote", "App");
    settings.beginGroup("KeyboardMappings");
    QStringList keys = settings.allKeys();
    
    // List of IME-related shortcuts that should not be intercepted
    QStringList imeShortcuts = {
        "Ctrl+Space",      // Primary IME toggle
        "Ctrl+Shift",      // Language switching
        "Ctrl+Alt",        // IME functions
        "Shift+Alt",       // Alternative language switching
        "Alt+Shift"        // Alternative language switching (reversed)
    };
    
    for (const QString &key : keys) {
        // Skip IME-related shortcuts
        if (imeShortcuts.contains(key)) {
            // Remove from settings if it exists
            settings.remove(key);
            continue;
        }
        
        QString value = settings.value(key).toString();
        keyboardMappings[key] = value;
        keyboardActionMapping[key] = stringToAction(value);
    }
    settings.endGroup();
    
    // Save settings to persist the removal of IME shortcuts
    settings.sync();
}

QMap<QString, QString> MainWindow::getKeyboardMappings() const {
    return keyboardMappings;
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Phase 3.1.8: Ctrl tracking for trackpad zoom stubbed
    // Track Ctrl key state for trackpad pinch-zoom detection
    // Windows sends pinch-zoom as Ctrl+Wheel, so we need to distinguish from real Ctrl+Wheel
    // TODO Phase 3.3: Track Ctrl state in DocumentViewport if needed
    
    // Don't intercept keyboard events when text input widgets have focus
    // This prevents conflicts with Windows TextInputFramework
    QWidget *focusWidget = QApplication::focusWidget();
    if (focusWidget) {
        bool isTextInputWidget = qobject_cast<QLineEdit*>(focusWidget) || 
                               qobject_cast<QSpinBox*>(focusWidget) || 
                               qobject_cast<QTextEdit*>(focusWidget) ||
                               qobject_cast<QPlainTextEdit*>(focusWidget) ||
                               qobject_cast<QComboBox*>(focusWidget);
        
        if (isTextInputWidget) {
            // Let text input widgets handle their own keyboard events
            QMainWindow::keyPressEvent(event);
            return;
        }
    }
    
    // Don't intercept IME-related keyboard shortcuts
    // These are reserved for Windows Input Method Editor
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_Space ||           // Ctrl+Space (IME toggle)
            event->key() == Qt::Key_Shift ||           // Ctrl+Shift (language switch)
            event->key() == Qt::Key_Alt) {             // Ctrl+Alt (IME functions)
            // Let Windows handle IME shortcuts
            QMainWindow::keyPressEvent(event);
            return;
        }
    }
    
    // Don't intercept Shift+Alt (another common IME shortcut)
    if ((event->modifiers() & Qt::ShiftModifier) && (event->modifiers() & Qt::AltModifier)) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    
    // Build key sequence string
    QStringList modifiers;
    
    if (event->modifiers() & Qt::ControlModifier) modifiers << "Ctrl";
    if (event->modifiers() & Qt::ShiftModifier) modifiers << "Shift";
    if (event->modifiers() & Qt::AltModifier) modifiers << "Alt";
    if (event->modifiers() & Qt::MetaModifier) modifiers << "Meta";
    
    QString keyString = QKeySequence(event->key()).toString();
    
    QString fullSequence;
    if (!modifiers.isEmpty()) {
        fullSequence = modifiers.join("+") + "+" + keyString;
    } else {
        fullSequence = keyString;
    }
    
    // Check if this sequence is mapped
    if (keyboardMappings.contains(fullSequence)) {
        handleKeyboardShortcut(fullSequence);
        event->accept();
        return;
    }
    
    // If not handled, pass to parent
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    // Phase 3.1.8: Ctrl tracking for trackpad zoom stubbed
    // TODO Phase 3.3: Track Ctrl state in DocumentViewport if needed
    
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::tabletEvent(QTabletEvent *event) {
    // Since tablet tracking is disabled to prevent crashes, we now only handle
    // basic tablet events that come through when stylus is touching the surface
    if (!event) {
        return;
    }
    
    // Just pass tablet events to parent safely without custom hover handling
    // (hover tooltips will work through normal mouse events instead)
    try {
        QMainWindow::tabletEvent(event);
    } catch (...) {
        // Catch any exceptions and just accept the event
        event->accept();
    }
}

void MainWindow::showPendingTooltip() {
    // This function is now unused since we disabled tablet tracking
    // Tooltips will work through normal mouse hover events instead
    // Keeping the function for potential future use
}

void MainWindow::onZoomSliderChanged(int value) {
    // Phase 3.1.4: Use currentViewport() for zoom
    DocumentViewport* vp = currentViewport();
    int oldZoom = vp ? qRound(vp->zoomLevel() * 100.0) : 100;
    int newZoom = value;
    
    updateZoom();
    adjustThicknessForZoom(oldZoom, newZoom); // Maintain visual thickness consistency
}

void MainWindow::saveDefaultBackgroundSettings(Page::BackgroundType style, QColor bgColor, QColor gridColor, int density) {
    // Phase doc-1: Using new key "defaultBgType" to avoid stale values from old BackgroundStyle enum
    QSettings settings("SpeedyNote", "App");
    settings.setValue("defaultBgType", static_cast<int>(style));
    settings.setValue("defaultBackgroundColor", bgColor.name());
    settings.setValue("defaultGridColor", gridColor.name());  // Phase doc-1: Added grid color
    settings.setValue("defaultBackgroundDensity", density);
}

// PDF Outline functionality
void MainWindow::toggleOutlineSidebar() {
    outlineSidebarVisible = !outlineSidebarVisible;
    
    // Hide bookmarks sidebar if it's visible when opening outline
    if (outlineSidebarVisible && bookmarksSidebar && bookmarksSidebar->isVisible()) {
        bookmarksSidebar->setVisible(false);
        bookmarksSidebarVisible = false;
        // Update bookmarks button state
        if (toggleBookmarksButton) {
            toggleBookmarksButton->setProperty("selected", false);
            updateButtonIcon(toggleBookmarksButton, "bookmark");
            toggleBookmarksButton->style()->unpolish(toggleBookmarksButton);
            toggleBookmarksButton->style()->polish(toggleBookmarksButton);
        }
    }
    
    outlineSidebar->setVisible(outlineSidebarVisible);
    
    // Update button toggle state
    if (toggleOutlineButton) {
        toggleOutlineButton->setProperty("selected", outlineSidebarVisible);
        updateButtonIcon(toggleOutlineButton, "outline");
        toggleOutlineButton->style()->unpolish(toggleOutlineButton);
        toggleOutlineButton->style()->polish(toggleOutlineButton);
    }
    
    // Load PDF outline when showing sidebar for the first time
    if (outlineSidebarVisible) {
        loadPdfOutline();
        
        // Phase 3.1.8: Use currentViewport() for page tracking
        DocumentViewport* viewport = currentViewport();
        if (viewport) {
            int currentPage = viewport->currentPageIndex() + 1; // Convert to 1-based
            updateOutlineSelection(currentPage);
        }
    }
    
    // Force layout update and reposition floating tabs after sidebar visibility change
    if (centralWidget() && centralWidget()->layout()) {
        centralWidget()->layout()->invalidate();
        centralWidget()->layout()->activate();
    }
    QTimer::singleShot(0, this, [this]() {
        positionLeftSidebarTabs();
        positionDialToolbarTab();
        if (dialContainer && dialContainer->isVisible()) {
            positionDialContainer();
        }
    });
}

void MainWindow::onOutlineItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    
    if (!item) return;
    
    // Get the page number stored in the item data
    QVariant pageData = item->data(0, Qt::UserRole);
    if (pageData.isValid()) {
        int pageNumber = pageData.toInt();
        if (pageNumber >= 0) {
            // Switch to the selected page (pageNumber is already 1-based from PDF outline)
            switchPage(pageNumber);
            pageInput->setValue(pageNumber);
        }
    }
}

void MainWindow::loadPdfOutline() {
    if (!outlineTree) return;
    
    outlineTree->clear();
    
    // Get current PDF document
    Poppler::Document* pdfDoc = getPdfDocument();
    if (!pdfDoc) return;
    
    // Get the outline from the PDF document
    QVector<Poppler::OutlineItem> outlineItems = pdfDoc->outline();
    
    if (outlineItems.isEmpty()) {
        // If no outline exists, show page numbers as fallback
        int pageCount = pdfDoc->numPages();
        for (int i = 0; i < pageCount; ++i) {
            QTreeWidgetItem* item = new QTreeWidgetItem(outlineTree);
            item->setText(0, QString(tr("Page %1")).arg(i + 1));
            item->setData(0, Qt::UserRole, i + 1); // Store 1-based page index to match outline behavior
        }
    } else {
        // Process the actual PDF outline
        for (const Poppler::OutlineItem& outlineItem : outlineItems) {
            addOutlineItem(outlineItem, nullptr);
        }
    }
    
    // Expand the first level by default
    outlineTree->expandToDepth(0);
}

void MainWindow::addOutlineItem(const Poppler::OutlineItem& outlineItem, QTreeWidgetItem* parentItem) {
    if (outlineItem.isNull()) return;
    
    QTreeWidgetItem* item;
    if (parentItem) {
        item = new QTreeWidgetItem(parentItem);
    } else {
        item = new QTreeWidgetItem(outlineTree);
    }
    
    // Set the title
    item->setText(0, outlineItem.name());
    
    // Try to get the page number from the destination
    int pageNumber = -1;
    auto destination = outlineItem.destination();
    if (destination) {
        pageNumber = destination->pageNumber();
    }
    
    // Store the page number (already 1-based from PDF) in the item data
    if (pageNumber >= 0) {
        item->setData(0, Qt::UserRole, pageNumber); // pageNumber is already 1-based from PDF outline
    }
    
    // Add child items recursively
    if (outlineItem.hasChildren()) {
        QVector<Poppler::OutlineItem> children = outlineItem.children();
        for (const Poppler::OutlineItem& child : children) {
            addOutlineItem(child, item);
        }
    }
}

void MainWindow::updateOutlineSelection(int pageNumber) {
    // ✅ EFFICIENCY: Only update if outline sidebar is visible
    if (!outlineSidebarVisible || !outlineTree) return;
    
    // ✅ MEMORY SAFETY: Use QTreeWidgetItemIterator for safe tree traversal
    QTreeWidgetItem* bestMatch = nullptr;
    int bestMatchPage = -1;
    
    // Iterate through all items in the tree to find the best match
    QTreeWidgetItemIterator it(outlineTree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        QVariant pageData = item->data(0, Qt::UserRole);
        
        if (pageData.isValid()) {
            int itemPage = pageData.toInt();
            
            // Find the item with the highest page number that's <= current page
            if (itemPage <= pageNumber && itemPage > bestMatchPage) {
                bestMatch = item;
                bestMatchPage = itemPage;
            }
        }
        
        ++it;
    }
    
    // ✅ Update selection if we found a match
    if (bestMatch) {
        // Block signals to prevent triggering navigation when programmatically selecting
        outlineTree->blockSignals(true);
        
        // Clear previous selection and select the new item
        outlineTree->clearSelection();
        bestMatch->setSelected(true);
        
        // Ensure the item is visible by scrolling to it
        outlineTree->scrollToItem(bestMatch, QAbstractItemView::EnsureVisible);
        
        // ✅ Expand parent items to make the selected item visible
        QTreeWidgetItem* parent = bestMatch->parent();
        while (parent) {
            parent->setExpanded(true);
            parent = parent->parent();
        }
        
        // Re-enable signals
        outlineTree->blockSignals(false);
    }
}

Poppler::Document* MainWindow::getPdfDocument() {
    // Phase 3.1.8: Stubbed - PDF document access will use DocumentViewport
    // TODO Phase 3.4: Implement PDF access through DocumentViewport
    return nullptr;
}

void MainWindow::loadDefaultBackgroundSettings(Page::BackgroundType &style, QColor &bgColor, QColor &gridColor, int &density) {
    // Phase doc-1: Using new key "defaultBgType" to avoid stale values from old BackgroundStyle enum
    QSettings settings("SpeedyNote", "App");
    style = static_cast<Page::BackgroundType>(settings.value("defaultBgType", static_cast<int>(Page::BackgroundType::Grid)).toInt());
    bgColor = QColor(settings.value("defaultBackgroundColor", "#FFFFFF").toString());
    gridColor = QColor(settings.value("defaultGridColor", "#C8C8C8").toString());  // Phase doc-1: Added grid color (gray 200,200,200)
    density = settings.value("defaultBackgroundDensity", 30).toInt();
    
    // Ensure valid values
    if (!bgColor.isValid()) bgColor = Qt::white;
    if (!gridColor.isValid()) gridColor = QColor(200, 200, 200);
    if (density < 10) density = 10;
    if (density > 200) density = 200;
    
    // Validate enum range (0-4 for Page::BackgroundType)
    int styleInt = static_cast<int>(style);
    if (styleInt < 0 || styleInt > 4) {
        style = Page::BackgroundType::Grid;  // Default to Grid if invalid
    }
}

void MainWindow::applyDefaultBackgroundToCanvas(InkCanvas *canvas) {
    // Phase 3.1.7: Stubbed - InkCanvas-specific background handling
    Q_UNUSED(canvas);
    // TODO Phase 3.3: Implement for DocumentViewport via Page background settings
}

void MainWindow::showRopeSelectionMenu(const QPoint &position) {
    // Phase 3.1.5: Stubbed - Lasso/rope tool will be reimplemented in Phase 2B
    Q_UNUSED(position);
    // TODO Phase 2B: Reimplement rope selection menu for DocumentViewport
}
void MainWindow::updatePdfTextSelectButtonState() {
    // Phase 3.1.5: Stubbed - PDF text selection not yet implemented in DocumentViewport
    if (pdfTextSelectButton) {
        pdfTextSelectButton->setProperty("selected", false);
        updateButtonIcon(pdfTextSelectButton, "ibeam");
        pdfTextSelectButton->style()->unpolish(pdfTextSelectButton);
        pdfTextSelectButton->style()->polish(pdfTextSelectButton);
    }
}

QString MainWindow::elideTabText(const QString &text, int maxWidth) {
    // Create a font metrics object using the default font
    QFontMetrics fontMetrics(QApplication::font());
    
    // Elide the text from the right (showing the beginning)
    return fontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
}

// Bookmark functionality implementation
void MainWindow::toggleBookmarksSidebar() {
    if (!bookmarksSidebar) return;
    
    bool isVisible = bookmarksSidebar->isVisible();
    
    // Hide outline sidebar if it's visible
    if (!isVisible && outlineSidebar && outlineSidebar->isVisible()) {
        outlineSidebar->setVisible(false);
        outlineSidebarVisible = false;
        // Update outline button state
        if (toggleOutlineButton) {
            toggleOutlineButton->setProperty("selected", false);
            updateButtonIcon(toggleOutlineButton, "outline");
            toggleOutlineButton->style()->unpolish(toggleOutlineButton);
            toggleOutlineButton->style()->polish(toggleOutlineButton);
        }
    }
    
    bookmarksSidebar->setVisible(!isVisible);
    bookmarksSidebarVisible = !isVisible;
    
    // Update button toggle state
    if (toggleBookmarksButton) {
        toggleBookmarksButton->setProperty("selected", bookmarksSidebarVisible);
        updateButtonIcon(toggleBookmarksButton, "bookmark");
        toggleBookmarksButton->style()->unpolish(toggleBookmarksButton);
        toggleBookmarksButton->style()->polish(toggleBookmarksButton);
    }
    
    if (bookmarksSidebarVisible) {
        loadBookmarks(); // Refresh bookmarks when opening
    }
    
    // Force layout update and reposition floating tabs after sidebar visibility change
    if (centralWidget() && centralWidget()->layout()) {
        centralWidget()->layout()->invalidate();
        centralWidget()->layout()->activate();
    }
    QTimer::singleShot(0, this, [this]() {
        positionLeftSidebarTabs();
        positionDialToolbarTab();
        if (dialContainer && dialContainer->isVisible()) {
            positionDialContainer();
        }
    });
}

void MainWindow::onBookmarkItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    if (!item) return;
    
    // Get the page number from the item data
    bool ok;
    int pageNumber = item->data(0, Qt::UserRole).toInt(&ok);
    if (ok && pageNumber > 0) {
        // Phase 3.1.9: Use currentViewport() for page navigation
        DocumentViewport* vp = currentViewport();
        int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
        switchPageWithDirection(pageNumber, (pageNumber > currentPage) ? 1 : -1);
        pageInput->setValue(pageNumber);
    }
}

void MainWindow::loadBookmarks() {
    // Phase 3.1.9: Stubbed - bookmarks will use Document in Phase 3.4
    if (!bookmarksTree) return;
    
    bookmarksTree->clear();
    bookmarks.clear();
    
    // TODO Phase 3.4: Load bookmarks from currentViewport()->document()
    
    updateBookmarkButtonState();
}

// Markdown Notes Sidebar functionality
void MainWindow::toggleMarkdownNotesSidebar() {
    if (!markdownNotesSidebar) return;
    
    bool isVisible = markdownNotesSidebar->isVisible();
    
    // Note: Markdown notes sidebar (right side) is independent of 
    // outline/bookmarks sidebars (left side), so we don't hide them here.
    // The left sidebars are mutually exclusive with each other, but not with markdown notes.
    
    markdownNotesSidebar->setVisible(!isVisible);
    markdownNotesSidebarVisible = !isVisible;
    
    // Update button toggle state
    if (toggleMarkdownNotesButton) {
        toggleMarkdownNotesButton->setProperty("selected", markdownNotesSidebarVisible);
        updateButtonIcon(toggleMarkdownNotesButton, "markdown");
        toggleMarkdownNotesButton->style()->unpolish(toggleMarkdownNotesButton);
        toggleMarkdownNotesButton->style()->polish(toggleMarkdownNotesButton);
    }
    
    if (markdownNotesSidebarVisible) {
        loadMarkdownNotesForCurrentPage(); // Load notes when opening
    }
    
    // Force immediate layout update so canvas repositions correctly
    if (centralWidget() && centralWidget()->layout()) {
        centralWidget()->layout()->invalidate();
        centralWidget()->layout()->activate();
    }
    QApplication::processEvents(); // Process layout changes immediately
    
    // Update canvas position and scrollbars
    updatePanRange();
    // Phase 3.1.9: Stubbed - DocumentViewport auto-updates
    if (DocumentViewport* vp = currentViewport()) {
        vp->update();
    }
    
    // Reposition dial toolbar tab and dial container after layout settles
    // Use a short delay to ensure the layout has fully completed
    QTimer::singleShot(0, this, [this]() {
        positionDialToolbarTab();
        positionLeftSidebarTabs();  // Also reposition left tabs for consistency
    if (dialContainer && dialContainer->isVisible()) {
        positionDialContainer();
    }
    });
}

void MainWindow::onMarkdownNotesUpdated() {
    // Phase 3.1.9: Stubbed - markdown notes will use Document in Phase 3.4
    // This was triggered by InkCanvas::markdownNotesUpdated signal (now disconnected)
    qDebug() << "onMarkdownNotesUpdated(): Not implemented yet (Phase 3.4)";
    
    // Still handle sidebar visibility for future use
    if (markdownNotesSidebar && !markdownNotesSidebarVisible) {
        // Don't auto-open sidebar when note is created (will be re-enabled in Phase 3.4)
    }
}

void MainWindow::onMarkdownNoteContentChanged(const QString &noteId, const MarkdownNoteData &data) {
    // Phase 3.1.9: Stubbed - markdown notes will use Document in Phase 3.4
    Q_UNUSED(noteId);
    Q_UNUSED(data);
    qDebug() << "onMarkdownNoteContentChanged(): Not implemented yet (Phase 3.4)";
}

void MainWindow::onMarkdownNoteDeleted(const QString &noteId) {
    // Phase 3.1.9: Stubbed - markdown notes will use Document in Phase 3.4
    Q_UNUSED(noteId);
    qDebug() << "onMarkdownNoteDeleted(): Not implemented yet (Phase 3.4)";
}

void MainWindow::onHighlightLinkClicked(const QString &highlightId) {
    // Phase 3.1.9: Stubbed - text highlights will use Document in Phase 3.4
    Q_UNUSED(highlightId);
    qDebug() << "onHighlightLinkClicked(): Not implemented yet (Phase 3.4)";
}

void MainWindow::onHighlightDoubleClicked(const QString &highlightId) {
    // Phase 3.1.9: Stubbed - text highlights will use Document in Phase 3.4
    Q_UNUSED(highlightId);
    qDebug() << "onHighlightDoubleClicked(): Not implemented yet (Phase 3.4)";
}

void MainWindow::loadMarkdownNotesForCurrentPage() {
    // Phase 3.1.9: Stubbed - markdown notes will use Document in Phase 3.4
    if (!markdownNotesSidebar) return;
    
    // Clear sidebar since we don't have InkCanvas notes anymore
    if (markdownNotesSidebar->isInSearchMode()) {
        markdownNotesSidebar->exitSearchMode();
    }
    markdownNotesSidebar->clearNotes();
    
    DocumentViewport* vp = currentViewport();
    if (vp) {
        // TODO Phase 3.4: Get notes from vp->document()->currentPage()
        markdownNotesSidebar->setCurrentPageInfo(vp->currentPageIndex(), 1);
    }
}

void MainWindow::saveBookmarks() {
    // Phase 3.1.9: Stubbed - bookmarks will use Document in Phase 3.4
    qDebug() << "saveBookmarks(): Not implemented yet (Phase 3.4)";
}

void MainWindow::toggleCurrentPageBookmark() {
    // Phase 3.1.9: Stubbed - bookmarks will use Document in Phase 3.4
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    int currentPage = vp->currentPageIndex() + 1; // Convert to 1-based
    
    if (bookmarks.contains(currentPage)) {
        // Remove bookmark
        bookmarks.remove(currentPage);
    } else {
        // Add bookmark with default title
        QString defaultTitle = QString(tr("Bookmark %1")).arg(currentPage);
        bookmarks[currentPage] = defaultTitle;
    }
    
    // TODO Phase 3.4: Save bookmarks to Document
    updateBookmarkButtonState();
    
    // Refresh bookmarks view if visible
    if (bookmarksSidebarVisible) {
        loadBookmarks();
    }
}

void MainWindow::updateBookmarkButtonState() {
    // Phase 3.1.9: Use currentViewport() instead of currentCanvas()
    if (!toggleBookmarkButton) return;
    
    DocumentViewport* vp = currentViewport();
    int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
    bool isBookmarked = bookmarks.contains(currentPage);
    
    toggleBookmarkButton->setProperty("selected", isBookmarked);
    updateButtonIcon(toggleBookmarkButton, "star");
    
    // Update tooltip
    if (isBookmarked) {
        toggleBookmarkButton->setToolTip(tr("Remove Bookmark"));
    } else {
        toggleBookmarkButton->setToolTip(tr("Add Bookmark"));
    }
    
    // Force style update
    toggleBookmarkButton->style()->unpolish(toggleBookmarkButton);
    toggleBookmarkButton->style()->polish(toggleBookmarkButton);
}

// IME support for multi-language input
void MainWindow::inputMethodEvent(QInputMethodEvent *event) {
    // Forward IME events to the focused widget
    QWidget *focusWidget = QApplication::focusWidget();
    if (focusWidget && focusWidget != this) {
        QApplication::sendEvent(focusWidget, event);
        event->accept();
        return;
    }
    
    // Default handling
    QMainWindow::inputMethodEvent(event);
}

QVariant MainWindow::inputMethodQuery(Qt::InputMethodQuery query) const {
    // Forward IME queries to the focused widget
    QWidget *focusWidget = QApplication::focusWidget();
    if (focusWidget && focusWidget != this) {
        return focusWidget->inputMethodQuery(query);
    }
    
    // Default handling
    return QMainWindow::inputMethodQuery(query);
}

// Color palette management
void MainWindow::setUseBrighterPalette(bool use) {
    if (useBrighterPalette != use) {
        useBrighterPalette = use;
        
        // Update all colors - call updateColorPalette which handles null checks
        updateColorPalette();
        
        // Save preference
        QSettings settings("SpeedyNote", "App");
        settings.setValue("useBrighterPalette", useBrighterPalette);
    }
}

void MainWindow::updateColorPalette() {
    // Clear existing presets
    colorPresets.clear();
    currentPresetIndex = 0;
    
    // Add default pen color (theme-aware)
    colorPresets.enqueue(getDefaultPenColor());
    
    // Add palette colors
    colorPresets.enqueue(getPaletteColor("red"));
    colorPresets.enqueue(getPaletteColor("yellow"));
    colorPresets.enqueue(getPaletteColor("blue"));
    colorPresets.enqueue(getPaletteColor("green"));
    colorPresets.enqueue(QColor("#000000")); // Black (always same)
    colorPresets.enqueue(QColor("#FFFFFF")); // White (always same)
    
    // Only update UI elements if they exist
    if (redButton && blueButton && yellowButton && greenButton) {
        // Update color button icons based on current palette (not theme)
        QString redIconPath = useBrighterPalette ? ":/resources/icons/pen_light_red.png" : ":/resources/icons/pen_dark_red.png";
        QString blueIconPath = useBrighterPalette ? ":/resources/icons/pen_light_blue.png" : ":/resources/icons/pen_dark_blue.png";
        QString yellowIconPath = useBrighterPalette ? ":/resources/icons/pen_light_yellow.png" : ":/resources/icons/pen_dark_yellow.png";
        QString greenIconPath = useBrighterPalette ? ":/resources/icons/pen_light_green.png" : ":/resources/icons/pen_dark_green.png";
        
        redButton->setIcon(QIcon(redIconPath));
        blueButton->setIcon(QIcon(blueIconPath));
        yellowButton->setIcon(QIcon(yellowIconPath));
        greenButton->setIcon(QIcon(greenIconPath));
        
        // Update color button states
        updateColorButtonStates();
    }
}

QColor MainWindow::getPaletteColor(const QString &colorName) {
    if (useBrighterPalette) {
        // Brighter colors (good for dark backgrounds)
        if (colorName == "red") return QColor("#FF7755");
        if (colorName == "yellow") return QColor("#EECC00");
        if (colorName == "blue") return QColor("#66CCFF");
        if (colorName == "green") return QColor("#55FF77");
    } else {
        // Darker colors (good for light backgrounds)
        if (colorName == "red") return QColor("#AA0000");
        if (colorName == "yellow") return QColor("#997700");
        if (colorName == "blue") return QColor("#0000AA");
        if (colorName == "green") return QColor("#007700");
    }
    
    // Fallback colors
    if (colorName == "black") return QColor("#000000");
    if (colorName == "white") return QColor("#FFFFFF");
    
    return QColor("#000000"); // Default fallback
}

void MainWindow::reconnectControllerSignals() {
    if (!controllerManager || !pageDial) {
        return;
    }
    
    // Reset internal dial state
    tracking = false;
    accumulatedRotation = 0;
    grossTotalClicks = 0;
    tempClicks = 0;
    lastAngle = 0;
    startAngle = 0;
    pendingPageFlip = 0;
    accumulatedRotationAfterLimit = 0;
    
    // Disconnect all existing connections to avoid duplicates
    disconnect(controllerManager, nullptr, this, nullptr);
    disconnect(controllerManager, nullptr, pageDial, nullptr);
    
    // Reconnect all controller signals
    connect(controllerManager, &SDLControllerManager::buttonHeld, this, &MainWindow::handleButtonHeld);
    connect(controllerManager, &SDLControllerManager::buttonReleased, this, &MainWindow::handleButtonReleased);
    connect(controllerManager, &SDLControllerManager::leftStickAngleChanged, pageDial, &QDial::setValue);
    connect(controllerManager, &SDLControllerManager::leftStickReleased, pageDial, &QDial::sliderReleased);
    connect(controllerManager, &SDLControllerManager::buttonSinglePress, this, &MainWindow::handleControllerButton);
    
    // Re-establish dial mode connections by changing to current mode
    DialMode currentMode = currentDialMode;
    changeDialMode(currentMode);
    
    // Update dial display to reflect current state
    updateDialDisplay();
    
    // qDebug() << "Controller signals reconnected successfully";
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    // Detect Windows theme changes at runtime
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        
        // WM_SETTINGCHANGE (0x001A) is sent when system settings change
        if (msg->message == 0x001A) {
            // Check if this is a theme-related setting change
            if (msg->lParam != 0) {
                const wchar_t *lparam = reinterpret_cast<const wchar_t *>(msg->lParam);
                if (lparam && wcscmp(lparam, L"ImmersiveColorSet") == 0) {
                    // Windows theme changed - update Qt palette and our UI
                    // Use a small delay to ensure registry has been updated
                    QTimer::singleShot(100, this, [this]() {
                        MainWindow::updateApplicationPalette(); // Update Qt's global palette
                        updateTheme(); // Update our custom theme
                    });
                }
            }
        }
    }
    
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::closeEvent(QCloseEvent *event) {
    // ✅ OPTIMIZATION: Wait for any pending async save to complete before closing
    // This ensures data saved during page switching is fully written to disk
    if (concurrentSaveFuture.isValid() && !concurrentSaveFuture.isFinished()) {
        concurrentSaveFuture.waitForFinished();
    }
    
    // Temp folder path for comparison
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/temp_session";
    
    // Phase 3.1: Auto-save via DocumentManager and TabManager
    // TODO Phase 3.5: Implement auto-save for DocumentViewports
    // if (m_tabManager && m_documentManager) {
    //     m_tabManager->saveAllTabs();
    // }
    
    // ✅ Save current bookmarks before closing
    saveBookmarks();
    
    // Accept the close event to allow the program to close
    event->accept();
}

bool MainWindow::showLastAccessedPageDialog(InkCanvas *canvas) {
    // Phase 3.1.7: Stubbed - InkCanvas-specific
    Q_UNUSED(canvas);
    return false;
}

void MainWindow::openSpnPackage(const QString &spnPath)
{
    // Phase 3.1.8: Stubbed - .spn format is being replaced with .snx
    Q_UNUSED(spnPath);
    QMessageBox::information(this, tr("Open Notebook"), 
        tr("Opening .spn packages is being redesigned. Coming soon with .snx format!"));
    return;

#if 0 // Phase 3.1.8: Old .spn opening code disabled
    if (!SpnPackageManager::isValidSpnPackage(spnPath)) {
        QMessageBox::warning(this, tr("Invalid Package"), 
            tr("The selected file is not a valid SpeedyNote package."));
        return;
    }
    
    // Check if this notebook is already open in another tab
    if (switchToExistingNotebook(spnPath)) {
        return; // Switched to existing tab, don't open again
    }
    
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    // Save current work if edited
    if (canvas->isEdited()) {
        saveCurrentPage();
    }
    
    // Set the .spn package as save folder
    canvas->setSaveFolder(spnPath);
    
    // ✅ Handle missing PDF file
    if (!canvas->handleMissingPdf(this)) {
        // User cancelled PDF relinking, don't open the package
        return;
    }
    
    // ✅ Update scroll behavior based on PDF loading state after relinking
    setScrollOnTopEnabled(canvas->isPdfLoadedFunc());
    
    // Update tab label
    updateTabLabel();
    updateBookmarkButtonState(); // ✅ Update bookmark button state after loading notebook
    
    // ✅ Show last accessed page dialog if available
    if (!showLastAccessedPageDialog(canvas)) {
        // No last accessed page, start from page 1
        switchPageWithDirection(1, 1);
        pageInput->setValue(1);
    } else {
        // Dialog handled page switching, update page input
        pageInput->setValue(getCurrentPageForCanvas(canvas) + 1);
    }
    updateZoom();
    updatePanRange();
    
    // Phase 3.1: recentNotebooksManager and sharedLauncher disconnected
    // if (recentNotebooksManager) {
    //     recentNotebooksManager->addRecentNotebook(spnPath, canvas);
    //     if (sharedLauncher && sharedLauncher->isVisible()) {
    //         sharedLauncher->refreshRecentNotebooks();
    //     }
    // }
#endif // Phase 3.1.8: openSpnPackage disabled
}

void MainWindow::createNewSpnPackage(const QString &spnPath)
{
    // Phase 3.1.8: Stubbed - .spn format is being replaced with .snx
    Q_UNUSED(spnPath);
    QMessageBox::information(this, tr("Create Notebook"), 
        tr("Creating .spn packages is being redesigned. Coming soon with .snx format!"));
    return;

#if 0 // Phase 3.1.8: Old .spn creation code disabled
    // Check if file already exists
    if (QFile::exists(spnPath)) {
        QMessageBox::warning(this, tr("File Exists"), 
            tr("A file with this name already exists. Please choose a different name."));
        return;
    }
    
    // Get the base name for the notebook (without .spn extension)
    QFileInfo fileInfo(spnPath);
    QString notebookName = fileInfo.baseName();
    
    // Create the new .spn package
    if (!SpnPackageManager::createSpnPackage(spnPath, notebookName)) {
        QMessageBox::critical(this, tr("Creation Failed"), 
            tr("Failed to create the SpeedyNote package. Please check file permissions."));
        return;
    }
    
    // Get current canvas and save any existing work
    InkCanvas *canvas = currentCanvas();
    if (!canvas) return;
    
    if (canvas->isEdited()) {
        saveCurrentPage();
    }
    
    // Open the newly created package
    canvas->setSaveFolder(spnPath);
    
    // ✅ Apply default background settings to new package
    applyDefaultBackgroundToCanvas(canvas);
    
    // Update UI
    updateTabLabel();
    updateBookmarkButtonState();
    
    // Start from page 1 (no last accessed page for new packages)
    switchPageWithDirection(1, 1);
    pageInput->setValue(1);
    updateZoom();
    updatePanRange();
    
    // Phase 3.1: recentNotebooksManager and sharedLauncher disconnected
    // if (recentNotebooksManager) {
    //     recentNotebooksManager->addRecentNotebook(spnPath, canvas);
    //     if (sharedLauncher && sharedLauncher->isVisible()) {
    //         sharedLauncher->refreshRecentNotebooks();
    //     }
    // }
    
    // Show success message
    QMessageBox::information(this, tr("Package Created"), 
        tr("New SpeedyNote package '%1' has been created successfully!").arg(notebookName));
#endif // Phase 3.1.8: createNewSpnPackage disabled
}

// ========================================
// Single Instance Implementation
// ========================================

bool MainWindow::isInstanceRunning()
{
    if (!sharedMemory) {
        sharedMemory = new QSharedMemory("SpeedyNote_SingleInstance");
    }
    
    // First, try to create shared memory segment
    if (sharedMemory->create(1)) {
        // Successfully created, we're the first instance
        return false;
    }
    
    // Creation failed, check why
    QSharedMemory::SharedMemoryError error = sharedMemory->error();
    
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // On Linux and macOS, handle stale shared memory by checking if server is actually responding
    if (error == QSharedMemory::AlreadyExists) {
        // Try to connect to the local server to see if instance is actually running
        QLocalSocket testSocket;
        testSocket.connectToServer("SpeedyNote_SingleInstance");
        
        // Wait briefly for connection - reduced timeout for faster response
        if (!testSocket.waitForConnected(500)) {
            // No server responding, definitely stale shared memory
            #ifdef Q_OS_MACOS
            // qDebug() << "Detected stale shared memory on macOS, attempting cleanup...";
            #else
            // qDebug() << "Detected stale shared memory on Linux, attempting cleanup...";
            #endif
            
            // Delete current shared memory object and create a fresh one
            delete sharedMemory;
            sharedMemory = new QSharedMemory("SpeedyNote_SingleInstance");
            
            // Try to attach to the existing segment and then detach to clean it up
            if (sharedMemory->attach()) {
                sharedMemory->detach();
                
                // Create a new shared memory object again after cleanup
                delete sharedMemory;
                sharedMemory = new QSharedMemory("SpeedyNote_SingleInstance");
                
                // Now try to create again
                if (sharedMemory->create(1)) {
                    // qDebug() << "Successfully cleaned up stale shared memory";
                    return false; // We're now the first instance
                }
            }
            
            #ifdef Q_OS_LINUX
            // If attach failed on Linux, try more aggressive cleanup
            // This handles the case where the segment exists but is corrupted
            delete sharedMemory;
            sharedMemory = nullptr;
            
            // Use system command to remove stale shared memory (last resort)
            // Run this asynchronously to avoid blocking the startup
            QProcess *cleanupProcess = new QProcess();
            cleanupProcess->start("sh", QStringList() << "-c" << "ipcs -m | grep $(whoami) | awk '/SpeedyNote/{print $2}' | xargs -r ipcrm -m");
            
            // Clean up the process when it finishes
            QObject::connect(cleanupProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                           cleanupProcess, &QProcess::deleteLater);
            
            // Create fresh shared memory object
            sharedMemory = new QSharedMemory("SpeedyNote_SingleInstance");
            if (sharedMemory->create(1)) {
                // qDebug() << "Cleaned up stale shared memory using system command";
                return false;
            }
            
            // If we still can't create, log the issue
            qWarning() << "Failed to clean up stale shared memory on Linux. Manual cleanup may be required.";
            #endif
            
            #ifdef Q_OS_MACOS
            // On macOS, if attach/detach didn't work, the memory is truly stale
            // Just force create by using a new instance
            delete sharedMemory;
            sharedMemory = new QSharedMemory("SpeedyNote_SingleInstance");
            if (sharedMemory->create(1)) {
                return false;
            }
            // If still failing, log but allow app to run anyway (better than locking out)
            qWarning() << "Failed to clean up stale shared memory on macOS";
            // Force it to work by assuming we're the only instance
            return false;
            #endif
        } else {
            // Server is responding, there's actually another instance running
            testSocket.disconnectFromServer();
        }
    }
#endif
    
    // Another instance is running (or cleanup failed)
    return true;
}

bool MainWindow::sendToExistingInstance(const QString &filePath)
{
    QLocalSocket socket;
    socket.connectToServer("SpeedyNote_SingleInstance");
    
    if (!socket.waitForConnected(3000)) {
        return false; // Failed to connect to existing instance
    }
    
    // Send the file path to the existing instance
    QByteArray data = filePath.toUtf8();
    socket.write(data);
    socket.waitForBytesWritten(3000);
    socket.disconnectFromServer();
    
    return true;
}

void MainWindow::setupSingleInstanceServer()
{
    localServer = new QLocalServer(this);
    
    // Remove any existing server (in case of improper shutdown)
    QLocalServer::removeServer("SpeedyNote_SingleInstance");
    
    // Start listening for new connections
    if (!localServer->listen("SpeedyNote_SingleInstance")) {
        qWarning() << "Failed to start single instance server:" << localServer->errorString();
        return;
    }
    
    // Connect to handle new connections
    connect(localServer, &QLocalServer::newConnection, this, &MainWindow::onNewConnection);
}

void MainWindow::onNewConnection()
{
    QLocalSocket *clientSocket = localServer->nextPendingConnection();
    if (!clientSocket) return;
    
    // Set up the socket to auto-delete when disconnected
    clientSocket->setParent(this); // Ensure proper cleanup
    
    // Use QPointer for safe access in lambdas
    QPointer<QLocalSocket> socketPtr(clientSocket);
    
    // Handle data reception with improved error handling
    connect(clientSocket, &QLocalSocket::readyRead, this, [this, socketPtr]() {
        if (!socketPtr || socketPtr->state() != QLocalSocket::ConnectedState) {
            return; // Socket was deleted or disconnected
        }
        
        QByteArray data = socketPtr->readAll();
        QString command = QString::fromUtf8(data);
        
        if (!command.isEmpty()) {
            // Use QTimer::singleShot to defer processing to avoid signal/slot conflicts
            QTimer::singleShot(0, this, [this, command]() {
                // Bring window to front and focus (already on main thread)
                raise();
                activateWindow();
                
                // Parse command
                if (command.startsWith("--create-new|")) {
                    // Handle create new package command
                    QString filePath = command.mid(13); // Remove "--create-new|" prefix
                    createNewSpnPackage(filePath);
                } else {
                    // Regular file opening
                    openFileInNewTab(command);
                }
            });
        }
        
        // Close the connection after processing with a small delay
        QTimer::singleShot(10, this, [socketPtr]() {
            if (socketPtr && socketPtr->state() == QLocalSocket::ConnectedState) {
                socketPtr->disconnectFromServer();
            }
        });
    });
    
    // Handle connection errors
    connect(clientSocket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
            this, [socketPtr](QLocalSocket::LocalSocketError error) {
        Q_UNUSED(error);
        if (socketPtr) {
            socketPtr->disconnectFromServer();
        }
    });
    
    // Clean up when disconnected
    connect(clientSocket, &QLocalSocket::disconnected, clientSocket, &QLocalSocket::deleteLater);
    
    // Set a reasonable timeout (3 seconds) with safe pointer
    QTimer::singleShot(3000, this, [socketPtr]() {
        if (socketPtr && socketPtr->state() != QLocalSocket::UnconnectedState) {
            socketPtr->disconnectFromServer();
        }
    });
}

// Static cleanup method for signal handlers and emergency cleanup
void MainWindow::cleanupSharedResources()
{
    // Minimal cleanup to avoid Qt conflicts
    if (sharedMemory) {
        if (sharedMemory->isAttached()) {
            sharedMemory->detach();
        }
        delete sharedMemory;
        sharedMemory = nullptr;
    }
    
    // Remove local server
    QLocalServer::removeServer("SpeedyNote_SingleInstance");
    
#ifdef Q_OS_LINUX
    // On Linux, try to clean up stale shared memory segments
    // Use system() instead of QProcess to avoid Qt dependencies in cleanup
    int ret = system("ipcs -m | grep $(whoami) | awk '/SpeedyNote/{print $2}' | xargs -r ipcrm -m 2>/dev/null");
    (void)ret; // Explicitly ignore return value
#endif

#ifdef Q_OS_MACOS
    // On macOS, QSharedMemory uses POSIX shared memory which should auto-cleanup
    // but we can force removal of the underlying file just to be sure
    // QSharedMemory on macOS creates files in /var/tmp or similar
    // The removeServer above should handle the local socket cleanup
#endif
}

void MainWindow::openFileInNewTab(const QString &filePath)
{
    // Check for duplicate .spn notebooks before creating a new tab
    if (filePath.toLower().endsWith(".spn")) {
        if (switchToExistingNotebook(filePath)) {
            return; // Notebook already open, switched to existing tab
        }
    }
    
    // Create a new tab first
    addNewTab();
    
    // Open the file in the new tab
    if (filePath.toLower().endsWith(".pdf")) {
        openPdfFile(filePath);
    } else if (filePath.toLower().endsWith(".spn")) {
        openSpnPackage(filePath);
    }
}

// ✅ MOUSE DIAL CONTROL IMPLEMENTATION

void MainWindow::mousePressEvent(QMouseEvent *event) {
    // Only track side buttons and right button for dial combinations
    if (event->button() == Qt::RightButton || 
        event->button() == Qt::BackButton || 
        event->button() == Qt::ForwardButton) {
        
        pressedMouseButtons.insert(event->button());
        
        // Start timer for long press detection
        if (!mouseDialTimer->isActive()) {
            mouseDialTimer->start();
        }
    }
    
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (pressedMouseButtons.contains(event->button())) {
        // Check if this was a short press (timer still running) for page navigation
        bool wasShortPress = mouseDialTimer->isActive();
        // Check if this button was part of a combination (more than one button pressed)
        bool wasPartOfCombination = pressedMouseButtons.size() > 1;
        
        pressedMouseButtons.remove(event->button());
        
        // If this was the last button released and we're in dial mode, stop it
        if (pressedMouseButtons.isEmpty()) {
            mouseDialTimer->stop();
            if (mouseDialModeActive) {
                stopMouseDialMode();
            } else if (wasShortPress && !wasPartOfCombination) {
                // Only handle short press if it was NOT part of a combination
                if (event->button() == Qt::BackButton) {
                    goToPreviousPage();
                } else if (event->button() == Qt::ForwardButton) {
                    goToNextPage();
                }
            }
        }
    }
    
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::wheelEvent(QWheelEvent *event) {
    // Only handle wheel events if mouse dial mode is active
    if (mouseDialModeActive) {
        handleMouseWheelDial(event->angleDelta().y());
        event->accept();
        return;
    }
    
    QMainWindow::wheelEvent(event);
}
QString MainWindow::mouseButtonCombinationToString(const QSet<Qt::MouseButton> &buttons) const {
    QStringList buttonNames;
    
    if (buttons.contains(Qt::RightButton)) {
        buttonNames << "Right";
    }
    if (buttons.contains(Qt::BackButton)) {
        buttonNames << "Side1";
    }
    if (buttons.contains(Qt::ForwardButton)) {
        buttonNames << "Side2";
    }
    
    // Sort to ensure consistent combination strings
    buttonNames.sort();
    return buttonNames.join("+");
}

void MainWindow::startMouseDialMode(const QString &combination) {
    if (mouseDialMappings.contains(combination)) {
        QString dialModeKey = mouseDialMappings[combination];
        DialMode mode = dialModeFromString(dialModeKey);
        
        mouseDialModeActive = true;
        currentMouseDialCombination = combination;
        setTemporaryDialMode(mode);
        
        // Show brief tooltip to indicate mode activation
        QToolTip::showText(QCursor::pos(), 
            tr("Mouse Dial: %1").arg(ButtonMappingHelper::internalKeyToDisplay(dialModeKey, true)),
            this, QRect(), 1500);
    }
}

void MainWindow::stopMouseDialMode() {
    if (mouseDialModeActive) {
        // ✅ Trigger the appropriate dial release function before stopping
        if (pageDial) {
            // Emit the sliderReleased signal to trigger the current mode's release function
            emit pageDial->sliderReleased();
        }
        
        mouseDialModeActive = false;
        currentMouseDialCombination.clear();
        clearTemporaryDialMode();
    }
}

void MainWindow::handleMouseWheelDial(int delta) {
    if (!mouseDialModeActive || !dialContainer) return;
    
    // Calculate step size based on current dial mode
    int stepDegrees = 15; // Default step
    
    switch (currentDialMode) {
        case PageSwitching:
            stepDegrees = 45; // 45 degrees per page (8 pages per full rotation)
            break;
        case PresetSelection:
            stepDegrees = 60; // 60 degrees per preset (6 presets per full rotation)
            break;
        case ZoomControl:
            stepDegrees = 30; // 30 degrees per zoom step (12 steps per rotation)
            break;
        case ThicknessControl:
            stepDegrees = 20; // 20 degrees per thickness step (18 steps per rotation)
            break;
        case ToolSwitching:
            stepDegrees = 120; // 120 degrees per tool (3 tools per rotation)
            break;
        case PanAndPageScroll:
            stepDegrees = 15; // 15 degrees per pan step (24 steps per rotation)
            break;
        default:
            stepDegrees = 15;
            break;
    }
    
    // Convert wheel delta to dial angle change (reversed: down = increase, up = decrease)
    int angleChange = (delta > 0) ? -stepDegrees : stepDegrees;
    
    // Apply the angle change to the dial
    int currentAngle = pageDial->value();
    int newAngle = (currentAngle + angleChange + 360) % 360;
    
    pageDial->setValue(newAngle);
    
    // Trigger the dial input handling
    handleDialInput(newAngle);
}

void MainWindow::setMouseDialMapping(const QString &combination, const QString &dialMode) {
    mouseDialMappings[combination] = dialMode;
    saveMouseDialMappings();
}

QString MainWindow::getMouseDialMapping(const QString &combination) const {
    return mouseDialMappings.value(combination, "none");
}

QMap<QString, QString> MainWindow::getMouseDialMappings() const {
    return mouseDialMappings;
}

void MainWindow::saveMouseDialMappings() {
    QSettings settings("SpeedyNote", "App");
    settings.beginGroup("MouseDialMappings");
    
    for (auto it = mouseDialMappings.begin(); it != mouseDialMappings.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    
    settings.endGroup();
}

void MainWindow::loadMouseDialMappings() {
    QSettings settings("SpeedyNote", "App");
    settings.beginGroup("MouseDialMappings");
    
    QStringList keys = settings.allKeys();
    
    if (keys.isEmpty()) {
        // Set default mappings
        mouseDialMappings["Right"] = "page_switching";
        mouseDialMappings["Side1"] = "zoom_control";
        mouseDialMappings["Side2"] = "thickness_control";
        mouseDialMappings["Right+Side1"] = "tool_switching";
        mouseDialMappings["Right+Side2"] = "preset_selection";
        mouseDialMappings["Side1+Side2"] = "pan_and_page_scroll"; // ✅ Added 6th combination
        
        saveMouseDialMappings(); // Save defaults
    } else {
        // Load saved mappings
        for (const QString &key : keys) {
            mouseDialMappings[key] = settings.value(key).toString();
        }
    }
    
    settings.endGroup();
}

void MainWindow::onAutoScrollRequested(int direction)
{
    // Phase 3.1.5: Stubbed - not needed with DocumentViewport's continuous scrolling
    Q_UNUSED(direction);
    // DocumentViewport handles infinite scrolling internally
}

void MainWindow::onEarlySaveRequested()
{
    // Phase 3.1.5: Stubbed - will be reimplemented in Phase 3.5 (file operations)
    // TODO Phase 3.5: Connect to DocumentManager save operations
}
