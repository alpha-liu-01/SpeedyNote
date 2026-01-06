#include "MainWindow.h"
// #include "InkCanvas.h"  // Phase 3.1.7: Disconnected - using DocumentViewport
// #include "VectorCanvas.h"  // REMOVED Phase 3.1.3 - Features migrated to DocumentViewport
#include "core/DocumentViewport.h"  // Phase 3.1: New viewport architecture
#include "core/Document.h"          // Phase 3.1: Document class
#include "ui/sidebars/LayerPanel.h" // Phase S1: Moved to sidebars folder
#include "ui/DebugOverlay.h"        // Debug overlay (toggle with D key)
#include "ui/StyleLoader.h"         // QSS stylesheet loader
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
// REMOVED MW7.2: QDial include removed - dial functionality deleted
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
    // Phase C.1.1: Create new tab system (QTabBar + QStackedWidget)
    // Phase C.2: Using custom TabBar class (handles configuration and initial styling)
    m_tabBar = new TabBar(this);
    
    m_viewportStack = new QStackedWidget(this);
    m_viewportStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Phase 3.1.1: Initialize DocumentManager and TabManager
    m_documentManager = new DocumentManager(this);
    // Phase C.1.2: TabManager now uses QTabBar + QStackedWidget
    m_tabManager = new TabManager(m_tabBar, m_viewportStack, this);
    
    // Connect TabManager signals
    connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
        // Phase 3.3: Connect scroll signals from current viewport
        connectViewportScrollSignals(vp);
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        
        // Phase 5.1 Task 4: Update LayerPanel when tab changes
        updateLayerPanelForViewport(vp);
        
        // Update DebugOverlay with current viewport
        if (m_debugOverlay) {
            m_debugOverlay->setViewport(vp);
        }
        
        // REMOVED E.1: straightLineToggleButton moved to Toolbar - no longer need to sync button state
        
        // TG.6: Apply touch gesture mode to new viewport
        if (vp) {
            vp->setTouchGestureMode(touchGestureMode);
        }
        
        // Phase C.1.6: Update NavigationBar with current document's filename
        if (m_navigationBar) {
            QString filename = tr("Untitled");
            if (vp && vp->document()) {
                filename = vp->document()->displayName();
            }
            m_navigationBar->setFilename(filename);
        }
    });

    // ML-1 FIX: Connect tabCloseRequested to clean up Document when tab closes
    // TabManager::closeTab() emits this signal before deleting the viewport
    connect(m_tabManager, &TabManager::tabCloseRequested, this, [this](int index, DocumentViewport* vp) {
        Q_UNUSED(index);
        if (vp && m_documentManager) {
            Document* doc = vp->document();
            if (doc) {
                // CR-L8: Clear LayerPanel's document pointer BEFORE deleting Document
                // to prevent dangling pointer if any code accesses LayerPanel during cleanup
                if (m_layerPanel && m_layerPanel->edgelessDocument() == doc) {
                    m_layerPanel->setCurrentPage(nullptr);
                }
                
                m_documentManager->closeDocument(doc);
            }
        }
    });
    
    // ========== EDGELESS SAVE PROMPT (A2: Prompt save before closing) ==========
    // Connect tabCloseAttempted to check for unsaved edgeless documents.
    // The tab is NOT automatically closed - we must call closeTab() explicitly.
    connect(m_tabManager, &TabManager::tabCloseAttempted, this, [this](int index, DocumentViewport* vp) {
        if (!vp || !m_documentManager || !m_tabManager) {
            return;
        }
        
        // Prevent closing the last tab (same behavior as old InkCanvas)
        if (m_tabManager->tabCount() <= 1) {
            QMessageBox::information(this, tr("Notice"), 
                tr("At least one tab must remain open."));
            return;
        }
        
        Document* doc = vp->document();
        if (!doc) {
            // No document, just close
            m_tabManager->closeTab(index);
            return;
        }
        
        // Check if this is an unsaved edgeless document with content
        bool isUnsavedEdgeless = doc->isEdgeless() && 
                                  m_documentManager->isUsingTempBundle(doc) &&
                                  (doc->tileCount() > 0 || doc->tileIndexCount() > 0);
        
        if (isUnsavedEdgeless) {
            // Prompt user to save
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Save Canvas?"),
                tr("This canvas has unsaved changes. Do you want to save before closing?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save
            );
            
            if (reply == QMessageBox::Cancel) {
                // User cancelled - don't close
                return;
            }
            
            if (reply == QMessageBox::Save) {
                // Show save dialog
                QString defaultName = doc->name.isEmpty() ? "Untitled Canvas" : doc->name;
                QString defaultPath = QDir::homePath() + "/" + defaultName + ".snb";
                
                QString savePath = QFileDialog::getSaveFileName(
                    this,
                    tr("Save Edgeless Canvas"),
                    defaultPath,
                    tr("SpeedyNote Bundle (*.snb)")
                );
                
                if (savePath.isEmpty()) {
                    // User cancelled save dialog - don't close
                    return;
                }
                
                // Ensure .snb extension
                if (!savePath.endsWith(".snb", Qt::CaseInsensitive)) {
                    savePath += ".snb";
                }
                
                // Save to the chosen location
                if (!m_documentManager->saveDocumentAs(doc, savePath)) {
                    QMessageBox::critical(this, tr("Save Error"),
                        tr("Failed to save canvas to:\n%1").arg(savePath));
                    return;  // Don't close if save failed
                }
                
                // Update tab title
                m_tabManager->setTabTitle(index, QFileInfo(savePath).baseName());
                m_tabManager->markTabModified(index, false);
            }
            // If Discard, fall through to close
        }
        
        // Close the tab
        m_tabManager->closeTab(index);
    });
    // ===========================================================================
    
    QSettings settings("SpeedyNote", "App");
    pdfRenderDPI = settings.value("pdfRenderDPI", 192).toInt();
    setPdfDPI(pdfRenderDPI);
    
    setupUi();    // ✅ Move all UI setup here

    controllerManager = new SDLControllerManager();
    controllerThread = new QThread(this);

    controllerManager->moveToThread(controllerThread);
    
    // MW2.2: Removed mouse dial control system
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

    // REMOVED: Benchmark controls removed - buttons deleted
    // setBenchmarkControlsVisible(false);
    
    recentNotebooksManager = RecentNotebooksManager::getInstance(this); // Use singleton instance

    // MW2.2: Removed dial initialization
    
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
    // QString buttonStyle = createButtonStyle(darkMode);


    // REMOVED: PDF buttons removed - sizes (26, 30) and (36, 36)



    // REMOVED: benchmarkButton removed - size (26, 30)
    // benchmarkLabel = new QLabel("PR:N/A", this);
    // benchmarkLabel->setFixedHeight(30);  // Make the benchmark bar smaller

    // REMOVED E.1: toggleTabBarButton moved to NavigationBar
    // toggleTabBarButton = new QPushButton(this);
    // toggleTabBarButton->setToolTip(tr("Show/Hide Tab Bar"));
    // toggleTabBarButton->setFixedSize(36, 36);
    // toggleTabBarButton->setStyleSheet(buttonStyle);
    // toggleTabBarButton->setProperty("selected", true); // Initially visible
    
    // Phase S3: Floating toggle buttons removed - replaced by LeftSidebarContainer
    // toggleOutlineButton, toggleBookmarksButton, toggleLayerPanelButton are now tabs in m_leftSidebar
    
    // Phase S3: Floating tab styling removed - LeftSidebarContainer uses QTabWidget styling
    
    // Add/Remove Bookmark Toggle Button
    // REMOVED: toggleBookmarkButton removed - size (36, 36)
    
    // REMOVED E.1: toggleMarkdownNotesButton moved to NavigationBar
    // toggleMarkdownNotesButton = new QPushButton(this);
    // toggleMarkdownNotesButton->setToolTip(tr("Show/Hide Markdown Notes"));
    // toggleMarkdownNotesButton->setFixedSize(36, 36);
    // toggleMarkdownNotesButton->setStyleSheet(buttonStyle);
    // toggleMarkdownNotesButton->setProperty("selected", false); // Initially hidden
    // // Try "note" icon, fallback to text if icon doesn't exist
    // updateButtonIcon(toggleMarkdownNotesButton, "markdown");


    // REMOVED E.1: touchGesturesButton moved to Toolbar
    // touchGesturesButton = new QPushButton(this);
    // touchGesturesButton->setToolTip(tr("Cycle Touch Gestures (Off/Y-Only/Full)"));
    // touchGesturesButton->setFixedSize(36, 36);
    // touchGesturesButton->setStyleSheet(buttonStyle);
    // touchGesturesButton->setProperty("selected", touchGestureMode != TouchGestureMode::Disabled); // For toggle state styling
    // touchGesturesButton->setProperty("yAxisOnly", touchGestureMode == TouchGestureMode::YAxisOnly); // For Y-only styling
    // updateButtonIcon(touchGesturesButton, "hand");

    // REMOVED MW5.6+: selectFolderButton creation removed - .spn format deprecated
    
    
    // REMOVED E.1: saveButton moved to NavigationBar
    // saveButton = new QPushButton(this);
    // saveButton->setFixedSize(36, 36);
    // QIcon saveIcon(loadThemedIcon("save"));  // Path to your icon in resources
    // saveButton->setIcon(saveIcon);
    // saveButton->setStyleSheet(buttonStyle);
    // saveButton->setToolTip(tr("Save Notebook"));
    // connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveCurrentPage);

    // REMOVED: exportPdfButton removed - size (26, 30)

    // REMOVED E.1: fullscreenButton moved to NavigationBar
    // fullscreenButton = new QPushButton(this);
    // fullscreenButton->setIcon(loadThemedIcon("fullscreen"));  // Load from resources
    // fullscreenButton->setFixedSize(36, 36);
    // fullscreenButton->setToolTip(tr("Toggle Fullscreen"));
    // fullscreenButton->setStyleSheet(buttonStyle);
    //
    // // ✅ Connect button click to toggleFullscreen() function
    // connect(fullscreenButton, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    // Use the darkMode variable already declared at the beginning of setupUi()

    // REMOVED: Color buttons removed - size (24, 36)
    
    customColorInput = new QLineEdit(this);
    customColorInput->setPlaceholderText("Custom HEX");
    customColorInput->setFixedSize(0, 0);
    
    // Enable IME support for multi-language input
    customColorInput->setAttribute(Qt::WA_InputMethodEnabled, true);
    customColorInput->setInputMethodHints(Qt::ImhNone); // Allow all input methods
    customColorInput->installEventFilter(this); // Install event filter for IME handling
    
    connect(customColorInput, &QLineEdit::returnPressed, this, &MainWindow::applyCustomColor);

    
    // REMOVED: thicknessButton and related UI removed - size (26, 30)


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

    // REMOVED E.1: Tool buttons moved to Toolbar
    // penToolButton = new QPushButton(this);
    // penToolButton->setFixedSize(36, 36);
    // penToolButton->setStyleSheet(buttonStyle);
    // penToolButton->setToolTip(tr("Pen Tool"));
    // connect(penToolButton, &QPushButton::clicked, this, &MainWindow::setPenTool);
    //
    // markerToolButton = new QPushButton(this);
    // markerToolButton->setFixedSize(36, 36);
    // markerToolButton->setStyleSheet(buttonStyle);
    // markerToolButton->setToolTip(tr("Marker Tool"));
    // connect(markerToolButton, &QPushButton::clicked, this, &MainWindow::setMarkerTool);
    //
    // eraserToolButton = new QPushButton(this);
    // eraserToolButton->setFixedSize(36, 36);
    // eraserToolButton->setStyleSheet(buttonStyle);
    // eraserToolButton->setToolTip(tr("Eraser Tool"));
    // connect(eraserToolButton, &QPushButton::clicked, this, &MainWindow::setEraserTool);

    // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    // Features migrated to DocumentViewport - all pens now use vector layers

    // REMOVED MW1.2: backgroundButton - feature was dropped

    // REMOVED E.1: straightLineToggleButton moved to Toolbar, ropeToolButton deprecated
    // straightLineToggleButton = new QPushButton(this);
    // straightLineToggleButton->setFixedSize(36, 36);
    // straightLineToggleButton->setStyleSheet(buttonStyle);
    // straightLineToggleButton->setToolTip(tr("Toggle Straight Line Mode"));
    // straightLineToggleButton->setProperty("selected", false); // Initially disabled
    // updateButtonIcon(straightLineToggleButton, "straightLine");
    // connect(straightLineToggleButton, &QPushButton::clicked, this, [this]() {
    //     // Task 2.9.5: Toggle straight line mode on current viewport
    //     if (DocumentViewport* vp = currentViewport()) {
    //         bool newState = !vp->straightLineMode();
    //         vp->setStraightLineMode(newState);
    //
    //         // Update button visual state
    //         straightLineToggleButton->setProperty("selected", newState);
    //         straightLineToggleButton->style()->unpolish(straightLineToggleButton);
    //         straightLineToggleButton->style()->polish(straightLineToggleButton);
    //     }
    // });
    //
    // ropeToolButton = new QPushButton(this);
    // ropeToolButton->setFixedSize(36, 36);
    // ropeToolButton->setStyleSheet(buttonStyle);
    // ropeToolButton->setToolTip(tr("Toggle Rope Tool Mode"));
    // ropeToolButton->setProperty("selected", false); // Initially disabled
    // updateButtonIcon(ropeToolButton, "rope");
    // connect(ropeToolButton, &QPushButton::clicked, this, [this]() {
    //     // Task 2.10.9: Connect lasso/rope tool to DocumentViewport
    //     if (DocumentViewport* vp = currentViewport()) {
    //         // Toggle between Lasso and Pen - if already lasso, switch back to pen
    //         if (vp->currentTool() == ToolType::Lasso) {
    //             vp->setCurrentTool(ToolType::Pen);
    //         } else {
    //             vp->setCurrentTool(ToolType::Lasso);
    //         }
    //     }
    //     updateToolButtonStates();
    // });
    
    // Insert Picture Button (Phase O2.9: repurposed as Object Select Tool)
    // REMOVED: insertPictureButton removed - size (36, 36)
    
    deletePageButton = new QPushButton(this);
    deletePageButton->setFixedSize(22, 30);
    QIcon trashIcon(loadThemedIcon("trash"));  // Path to your icon in resources
    deletePageButton->setIcon(trashIcon);
    // deletePageButton->setStyleSheet(buttonStyle);
    deletePageButton->setToolTip(tr("Clear All Content"));
    connect(deletePageButton, &QPushButton::clicked, this, &MainWindow::deleteCurrentPage);

    // REMOVED MW5.2+: Zoom buttons and frame moved to NavigationBar/Toolbar
  

    // REMOVED MW5.2+: Zoom buttons moved to NavigationBar/Toolbar

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




    // REMOVED MW7.5: PDF Outline Sidebar creation removed - outline sidebar deleted
    
    // REMOVED MW7.4: Bookmarks Sidebar creation removed - bookmark implementation deleted
    
    // 🌟 Phase S3: Left Sidebar Container (replaces floating tabs)
    // ---------------------------------------------------------------------------------------------------------
    m_leftSidebar = new LeftSidebarContainer(this);
    m_leftSidebar->setFixedWidth(250);  // Match sidebar width
    m_leftSidebar->setVisible(false);   // Hidden by default, toggled via NavigationBar
    m_layerPanel = m_leftSidebar->layerPanel();  // Get reference for signal connections
    
    // =========================================================================
    // Phase 5.6.8: Simplified LayerPanel Signal Handlers
    // =========================================================================
    // LayerPanel now directly updates Document's manifest (for edgeless mode)
    // or Page (for paged mode). Document methods sync changes to all loaded tiles.
    // MainWindow just needs to handle viewport updates.
    
    // Visibility change → repaint viewport
    connect(m_layerPanel, &LayerPanel::layerVisibilityChanged, this, [this](int /*layerIndex*/, bool /*visible*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            vp->update();
        }
    });
    
    // Active layer change → update drawing target for edgeless mode
    connect(m_layerPanel, &LayerPanel::activeLayerChanged, this, [this](int layerIndex) {
        if (DocumentViewport* vp = currentViewport()) {
            Document* doc = vp->document();
            if (doc && doc->isEdgeless()) {
                // LayerPanel already updated manifest, sync to viewport
                vp->setEdgelessActiveLayerIndex(layerIndex);
            }
            // Paged mode: Page::activeLayerIndex already updated by LayerPanel
        }
    });
    
    // Layer structural changes → mark modified and repaint
    connect(m_layerPanel, &LayerPanel::layerAdded, this, [this](int /*layerIndex*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
            vp->update();
        }
    });
    
    connect(m_layerPanel, &LayerPanel::layerRemoved, this, [this](int /*layerIndex*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
            vp->update();
        }
    });
    
    connect(m_layerPanel, &LayerPanel::layerMoved, this, [this](int /*fromIndex*/, int /*toIndex*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
            vp->update();
        }
    });
    
    // Layer rename → mark modified (no repaint needed, name doesn't affect rendering)
    connect(m_layerPanel, &LayerPanel::layerRenamed, this, [this](int /*layerIndex*/, const QString& /*newName*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
        }
    });
    
    // Phase 5.4: Layer merge → mark modified and repaint
    connect(m_layerPanel, &LayerPanel::layersMerged, this, [this](int /*targetIndex*/, QVector<int> /*mergedIndices*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
            vp->update();
        }
    });
    
    // Phase 5.5: Layer duplicate → mark modified and repaint
    connect(m_layerPanel, &LayerPanel::layerDuplicated, this, [this](int /*originalIndex*/, int /*newIndex*/) {
        if (DocumentViewport* vp = currentViewport()) {
            // LayerPanel already updated manifest/page, Document synced to tiles
            emit vp->documentModified();
            vp->update();
        }
    });
    
    // 🌟 Markdown Notes Sidebar
    markdownNotesSidebar = new MarkdownNotesSidebar(this);
    markdownNotesSidebar->setFixedWidth(300);
    markdownNotesSidebar->setVisible(false); // Hidden by default
    
    // MW1.5: Markdown notes signals disabled - will be reimplemented
    // connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteContentChanged, this, &MainWindow::onMarkdownNoteContentChanged);
    // connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeleted, this, &MainWindow::onMarkdownNoteDeleted);
    // connect(markdownNotesSidebar, &MarkdownNotesSidebar::highlightLinkClicked, this, &MainWindow::onHighlightLinkClicked);
    
    // Set up note provider for search functionality
    // Phase 3.1.8: Stubbed - markdown notes will use DocumentViewport in Phase 3.3
    markdownNotesSidebar->setNoteProvider([this]() -> QList<MarkdownNoteData> {
        // TODO Phase 3.3: Get notes from currentViewport()->document()
        return QList<MarkdownNoteData>();
    });
    
    // Phase C.1.5: Removed old m_tabWidget configuration - now using m_tabBar + m_viewportStack
    // Corner widgets (launcher button, add tab button) are now in NavigationBar
    
    // Phase 3.1: Old tabBarContainer kept but hidden (for reference, will be removed later)
    tabBarContainer = new QWidget(this);
    tabBarContainer->setObjectName("tabBarContainer");
    tabBarContainer->setVisible(false);  // Hidden - using m_tabBar now

    // REMOVED E.1: toggleTabBarButton moved to NavigationBar
    // Phase S3: Floating toggle buttons removed - sidebars now in LeftSidebarContainer tabs
    // connect(toggleOutlineButton, &QPushButton::clicked, this, &MainWindow::toggleOutlineSidebar);
    // connect(toggleBookmarksButton, &QPushButton::clicked, this, &MainWindow::toggleBookmarksSidebar);
    // connect(toggleLayerPanelButton, &QPushButton::clicked, this, &MainWindow::toggleLayerPanel);
    // REMOVED E.1: toggleMarkdownNotesButton and touchGesturesButton moved to new components

    


    // REMOVED MW1.3: prevPageButton - feature was dropped (already hidden)

    pageInput = new QSpinBox(this);
    pageInput->setFixedSize(36, 30);
    pageInput->setMinimum(1);
    pageInput->setMaximum(9999);
    pageInput->setValue(1);
    pageInput->setMaximumWidth(100);
    connect(pageInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onPageInputChanged);

    // REMOVED MW1.3: nextPageButton - feature was dropped (already hidden)

    // REMOVED: jumpToPageButton removed - size (26, 30)

    // MW2.2: Removed dial toggle button
    // MW2.2: Removed fast forward button - keeping only essential controls

    // MW2.2: Removed fast forward button connection

    // MW2.2: Removed dialModeSelector



    // Removed unused colorPreview widget that was causing UI artifacts

    // MW2.2: Removed mode selection buttons


    // REMOVED MW5.5: colorPresets initialization removed - color presets no longer used

    // REMOVED MW5.5: addPresetButton creation removed - color presets no longer used




    // REMOVED: openControlPanelButton removed - size (26, 30)

    // Phase C.1.5: openRecentNotebooksButton removed - functionality now in NavigationBar

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
            
            // REMOVED: handleColorButtonClick removed - color buttons deleted
            
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
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
                    // REMOVED: Color button updates removed - buttons deleted
    // updateColorButtonStates();
                }
            } else {
                // First click - apply the custom color
                vp->setPenColor(customColor);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
                // REMOVED: Color button updates removed - buttons deleted
    // updateColorButtonStates();
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
    // overflowMenuButton->setStyleSheet(buttonStyle);
    
    overflowMenu = new QMenu(this);
    overflowMenu->setObjectName("overflowMenu");
    
    // MW1.5: PDF menu actions disabled - will be reimplemented
    // QAction *managePdfAction = overflowMenu->addAction(loadThemedIcon("pdf"), tr("Import/Clear Document"));
    // connect(managePdfAction, &QAction::triggered, this, &MainWindow::handleSmartPdfButton);
    // QAction *exportPdfAction = overflowMenu->addAction(loadThemedIcon("export"), tr("Export Annotated PDF"));
    // connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportAnnotatedPdf);
    
    overflowMenu->addSeparator();
    
    QAction *zoom50Action = overflowMenu->addAction(tr("Zoom 50%"));
    connect(zoom50Action, &QAction::triggered, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setZoomLevel(0.5);
            // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        }
    });
    
    QAction *zoomResetAction = overflowMenu->addAction(tr("Zoom Reset"));
    connect(zoomResetAction, &QAction::triggered, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setZoomLevel(1.0);
            // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        }
    });
    
    QAction *zoom200Action = overflowMenu->addAction(tr("Zoom 200%"));
    connect(zoom200Action, &QAction::triggered, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setZoomLevel(2.0);
            // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        }
    });
    
    overflowMenu->addSeparator();
    
    QAction *jumpToPageAction = overflowMenu->addAction(tr("Jump to Page..."));
    connect(jumpToPageAction, &QAction::triggered, this, &MainWindow::showJumpToPageDialog);
    
    QAction *openControlPanelAction = overflowMenu->addAction(loadThemedIcon("settings"), tr("Settings"));
    connect(openControlPanelAction, &QAction::triggered, this, [this]() {
        // REMOVED: openControlPanelButton removed - use direct action
        QMessageBox::information(this, tr("Control Panel"), tr("Control Panel is being redesigned. Coming soon!"));
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
    // REMOVED E.1: toggleTabBarButton, toggleMarkdownNotesButton, touchGesturesButton, saveButton moved to new components
    // controlLayout->addWidget(toggleTabBarButton);
    // controlLayout->addWidget(toggleMarkdownNotesButton);
    // controlLayout->addWidget(touchGesturesButton);
    // controlLayout->addWidget(pdfTextSelectButton);
    // controlLayout->addWidget(saveButton);
    
    // REMOVED: Color buttons removed - sizes (24, 36)
    // controlLayout->addWidget(redButton);
    // controlLayout->addWidget(blueButton);
    // controlLayout->addWidget(yellowButton);
    // controlLayout->addWidget(greenButton);
    // controlLayout->addWidget(blackButton);
    // controlLayout->addWidget(whiteButton);
    controlLayout->addWidget(customColorButton);
    
    // Tool buttons
    // REMOVED E.1: Tool buttons moved to Toolbar
    // controlLayout->addWidget(penToolButton);
    // controlLayout->addWidget(markerToolButton);
    // controlLayout->addWidget(eraserToolButton);
    // // REMOVED Phase 3.1.3: vectorPenButton, vectorEraserButton, vectorUndoButton
    // controlLayout->addWidget(straightLineToggleButton);
    // controlLayout->addWidget(ropeToolButton);
    // controlLayout->addWidget(insertPictureButton);
    // controlLayout->addWidget(fullscreenButton);
    
    // Right stretch to center the main buttons
    controlLayout->addStretch();
    
    // Page controls and overflow menu on the right (fixed position)
    // REMOVED: toggleBookmarkButton removed - size (36, 36)
    // controlLayout->addWidget(toggleBookmarkButton);
    controlLayout->addWidget(pageInput);
    controlLayout->addWidget(overflowMenuButton);
    controlLayout->addWidget(deletePageButton);
    
    // REMOVED: Benchmark controls removed - button size (26, 30)
    // controlLayout->addWidget(benchmarkButton);
    // controlLayout->addWidget(benchmarkLabel);
    
    // REMOVED: Button visibility settings removed - buttons deleted
    // thicknessButton->setVisible(false);
    // loadPdfButton->setVisible(false);
    // clearPdfButton->setVisible(false);
    // exportPdfButton->setVisible(false);
    // openControlPanelButton->setVisible(false);
    // REMOVED MW5.6+: selectFolderButton removed - .spn format deprecated
    // jumpToPageButton->setVisible(false);
    // REMOVED MW5.2+: zoom buttons moved to NavigationBar/Toolbar
    // Phase C.1.5: openRecentNotebooksButton removed - functionality now in NavigationBar
    // benchmarkButton->setVisible(false);  // Hidden by default, toggle via Settings > Features
    // benchmarkLabel->setVisible(false);
    // REMOVED MW1.3: prevPageButton->setVisible(false), nextPageButton->setVisible(false)
    
    // REMOVED MW5.1: controlBar creation removed - replaced by NavigationBar and Toolbar

    // Phase C.1.5: Removed m_tabWidget - now using m_tabBar + m_viewportStack

    // Create a container for the viewport stack and scrollbars with relative positioning
    QWidget *canvasContainer = new QWidget;
    QVBoxLayout *canvasLayout = new QVBoxLayout(canvasContainer);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    
    // Phase C.1.2: Use m_viewportStack instead of m_tabWidget
    // m_viewportStack was created in constructor, just add to layout here
    canvasLayout->addWidget(m_viewportStack);
    // ------------------ End of viewport stack layout ------------------

    // ========================================
    // Debug Overlay (development tool)
    // ========================================
    // Create the debug overlay as a child of canvasContainer so it floats above the viewport.
    // Toggle with 'D' key (defined in shortcuts below). Hidden by default in production.
    m_debugOverlay = new DebugOverlay(canvasContainer);
    m_debugOverlay->move(10, 10);  // Position at top-left
// #ifdef SPEEDYNOTE_DEBUG
    m_debugOverlay->show();  // Show by default in debug builds
// #else
    // m_debugOverlay->hide();  // Hidden in release builds
// #endif

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

    // MW2.2: Removed dial mode toolbar
    
    // MW2.2: Removed dial toolbar toggle

    // Main layout: navigation bar -> tab bar -> toolbar -> canvas (vertical stack)
    QWidget *container = new QWidget;
    container->setObjectName("container");
    QVBoxLayout *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);  // ✅ Remove extra margins
    mainLayout->setSpacing(0); // ✅ Remove spacing between components

    // =========================================================================
    // Phase A: NavigationBar (Toolbar Extraction)
    // =========================================================================
    m_navigationBar = new NavigationBar(this);
    m_navigationBar->setFilename(tr("Untitled"));
    mainLayout->addWidget(m_navigationBar);
    
    // Connect NavigationBar signals
    connect(m_navigationBar, &NavigationBar::launcherClicked, this, [this]() {
        // Stub - will show launcher in future
        qDebug() << "NavigationBar: Launcher clicked (stub)";
    });
    connect(m_navigationBar, &NavigationBar::leftSidebarToggled, this, [this](bool checked) {
        // Phase S3: Toggle left sidebar container
        if (m_leftSidebar) {
            m_leftSidebar->setVisible(checked);
        }
    });
    connect(m_navigationBar, &NavigationBar::saveClicked, this, &MainWindow::saveDocument);
    connect(m_navigationBar, &NavigationBar::addClicked, this, [this]() {
        // Stub - will show add menu in future
        qDebug() << "NavigationBar: Add clicked (stub)";
        addNewTab();  // For now, just add a new tab
    });
    connect(m_navigationBar, &NavigationBar::filenameClicked, this, [this]() {
        // Toggle tab bar visibility
        qDebug() << "NavigationBar: Filename clicked - toggle tabs";
        if (m_tabBar) {
            m_tabBar->setVisible(!m_tabBar->isVisible());
        }
    });
    connect(m_navigationBar, &NavigationBar::fullscreenToggled, this, [this](bool checked) {
        Q_UNUSED(checked);
        toggleFullscreen();
    });
    connect(m_navigationBar, &NavigationBar::shareClicked, this, []() {
        // Stub - placeholder, does nothing
        qDebug() << "NavigationBar: Share clicked (stub - not implemented)";
    });
    connect(m_navigationBar, &NavigationBar::rightSidebarToggled, this, [this](bool checked) {
        // Toggle markdown notes sidebar
        if (markdownNotesSidebar) {
            markdownNotesSidebar->setVisible(checked);
        }
    });
    connect(m_navigationBar, &NavigationBar::menuRequested, this, [this]() {
        // Show overflow menu at menu button position
        if (overflowMenu && m_navigationBar) {
            overflowMenu->popup(m_navigationBar->mapToGlobal(
                QPoint(m_navigationBar->width() - 10, m_navigationBar->height())));
        }
    });
    // ------------------ End of NavigationBar signal connections ------------------

    // =========================================================================
    // Phase C: TabBar (Toolbar Extraction)
    // =========================================================================
    // m_tabBar was created in constructor, just add to layout here
    mainLayout->addWidget(m_tabBar);
    // Note: TabBar signals are connected via TabManager (created in constructor)
    // ------------------ End of TabBar setup ------------------

    // =========================================================================
    // Phase B: Toolbar (Toolbar Extraction)
    // =========================================================================
    m_toolbar = new Toolbar(this);
    mainLayout->addWidget(m_toolbar);
    
    // Connect Toolbar signals
    connect(m_toolbar, &Toolbar::toolSelected, this, [this](ToolType tool) {
        // Set tool on current viewport
        if (DocumentViewport* vp = currentViewport()) {
            vp->setCurrentTool(tool);
        }
        updateToolButtonStates();
        qDebug() << "Toolbar: Tool selected:" << static_cast<int>(tool);
    });
    connect(m_toolbar, &Toolbar::shapeClicked, this, [this]() {
        // Shape tool → straight line mode for now
        if (DocumentViewport* vp = currentViewport()) {
            vp->setStraightLineMode(true);
            // REMOVED E.1: straightLineToggleButton moved to Toolbar
        }
        qDebug() << "Toolbar: Shape clicked → straight line mode";
    });
    connect(m_toolbar, &Toolbar::objectInsertClicked, this, [this]() {
        // Stub - will show object insert menu in future
        qDebug() << "Toolbar: Object insert clicked (stub)";
    });
    connect(m_toolbar, &Toolbar::textClicked, this, [this]() {
        // Stub - will activate text tool in future
        qDebug() << "Toolbar: Text clicked (stub)";
    });
    connect(m_toolbar, &Toolbar::undoClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->undo();
        }
    });
    connect(m_toolbar, &Toolbar::redoClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->redo();
        }
    });
    connect(m_toolbar, &Toolbar::touchGestureModeChanged, this, [this](int mode) {
        // Touch gesture mode: 0=off, 1=y-axis, 2=full
        qDebug() << "Toolbar: Touch gesture mode changed to" << mode;
        // TODO: Connect to TouchGestureHandler when ready
    });
    // ------------------ End of Toolbar signal connections ------------------

    // Add components in vertical order
    // Phase C.1.5: tabBarContainer hidden - buttons now in NavigationBar
    // mainLayout->addWidget(tabBarContainer);   // Old tab bar - now hidden
    // REMOVED MW5.1: controlBar layout removed - replaced by NavigationBar and Toolbar
    
    // Content area with sidebars and canvas
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    
    // Phase 5: Left side container holds sidebars on top, layer panel at bottom
    // Phase S3: Old sidebar container removed - outline/bookmarks will be moved to LeftSidebarContainer
    // m_leftSideContainer = new QWidget(this);
    // QHBoxLayout *leftSidebarsLayout = new QHBoxLayout(m_leftSideContainer);
    // leftSidebarsLayout->setContentsMargins(0, 0, 0, 0);
    // leftSidebarsLayout->setSpacing(0);
    // leftSidebarsLayout->addWidget(outlineSidebar);
    // leftSidebarsLayout->addWidget(bookmarksSidebar);
    
    // Phase S3: Left sidebar container (replaces separate sidebars and floating tabs)
    contentLayout->addWidget(m_leftSidebar, 0);
    // Note: m_leftSideContainer kept for now (outline/bookmarks) but hidden
    // contentLayout->addWidget(m_leftSideContainer, 0);  // Old outline/bookmarks - to be removed
    contentLayout->addWidget(canvasContainer, 1); // Canvas takes remaining space
    // MW2.2: Removed dialToolbar from layout
    contentLayout->addWidget(markdownNotesSidebar, 0); // Fixed width markdown notes sidebar
    
    QWidget *contentWidget = new QWidget;
    contentWidget->setLayout(contentLayout);
    mainLayout->addWidget(contentWidget, 1);

    setCentralWidget(container);

    benchmarkTimer = new QTimer(this);
    // REMOVED: benchmarkButton connection removed - button deleted
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

    // REMOVED E.1: Layout functions removed - new components handle their own layout
    
    // Now that all UI components are created, update the color palette
    // REMOVED: updateColorPalette removed - color buttons deleted
    
    // Position add tab button and floating sidebar tabs initially
    QTimer::singleShot(100, this, [this]() {
        updateTabSizes();
        // Phase S3: positionLeftSidebarTabs() removed - using LeftSidebarContainer
        // MW2.2: Removed positionDialToolbarTab()
        
        // Phase 5.1: Initialize LayerPanel for the first tab
        // currentViewportChanged may have been emitted before m_layerPanel was ready
        updateLayerPanelForViewport(currentViewport());
        
        // Initialize DebugOverlay with the first viewport
        if (m_debugOverlay) {
            m_debugOverlay->setViewport(currentViewport());
        }
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
    
    // Insert Page: Ctrl+Shift+I - inserts new page after current page
    QShortcut* insertPageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this);
    insertPageShortcut->setContext(Qt::ApplicationShortcut);
    connect(insertPageShortcut, &QShortcut::activated, this, &MainWindow::insertPageInDocument);
    
    // Delete Page: Ctrl+Shift+D - delete current page
    QShortcut* deletePageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this);
    deletePageShortcut->setContext(Qt::ApplicationShortcut);
    connect(deletePageShortcut, &QShortcut::activated, this, &MainWindow::deletePageInDocument);
    
    // New Edgeless Canvas: Ctrl+Shift+N - creates infinite canvas document
    QShortcut* newEdgelessShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this);
    newEdgelessShortcut->setContext(Qt::ApplicationShortcut);
    connect(newEdgelessShortcut, &QShortcut::activated, this, &MainWindow::addNewEdgelessTab);
    
    // TEMPORARY: Load Bundle (.snb folder): Ctrl+Shift+L
    // Phase O1.7.6: Now handles BOTH paged and edgeless bundles
    // TODO: Replace with unified file picker when .snb becomes a single file (QDataStream packaging)
    QShortcut* loadBundleShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), this);
    loadBundleShortcut->setContext(Qt::ApplicationShortcut);
    connect(loadBundleShortcut, &QShortcut::activated, this, &MainWindow::loadFolderDocument);
    
    // Open PDF: Ctrl+Shift+O - open PDF file in new tab
    QShortcut* openPdfShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this);
    openPdfShortcut->setContext(Qt::ApplicationShortcut);
    connect(openPdfShortcut, &QShortcut::activated, this, &MainWindow::openPdfDocument);
    
    // Debug Overlay toggle (F12) - developer tools style, like browser devtools
    // Note: Ctrl+Shift+D is already used for Delete Page
    QShortcut* debugOverlayShortcut = new QShortcut(QKeySequence(Qt::Key_F12), this);
    debugOverlayShortcut->setContext(Qt::ApplicationShortcut);
    connect(debugOverlayShortcut, &QShortcut::activated, this, &MainWindow::toggleDebugOverlay);
    
    // Two-column auto layout toggle (Ctrl+2) - toggle between 1-column only and auto 1/2 column
    // Only applies to paged documents (not edgeless)
    QShortcut* autoLayoutShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_2), this);
    autoLayoutShortcut->setContext(Qt::ApplicationShortcut);
    connect(autoLayoutShortcut, &QShortcut::activated, this, &MainWindow::toggleAutoLayout);

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
    // CR-2B: Cleanup tool/mode signal connections
    if (m_toolChangedConn) disconnect(m_toolChangedConn);
    if (m_straightLineModeConn) disconnect(m_straightLineModeConn);
    
    // Phase 5.1: Clean up LayerPanel page connection
    if (m_layerPanelPageConn) disconnect(m_layerPanelPageConn);
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
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted 
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
    // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
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
    // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

void MainWindow::setPenTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Pen);
    }
    updateToolButtonStates();
    // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

void MainWindow::setMarkerTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Marker);
    }
    updateToolButtonStates();
    // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

void MainWindow::setEraserTool() {
    // Phase 3.1.4: Use currentViewport() instead of currentCanvas()
    if (DocumentViewport* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Eraser);
    }
    updateToolButtonStates();
    // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

// REMOVED Phase 3.1.3: setVectorPenTool(), setVectorEraserTool(), vectorUndo()
// Features migrated to DocumentViewport

void MainWindow::updateToolButtonStates() {
    // REMOVED E.1: All tool buttons moved to Toolbar component
    // Toolbar component handles its own visual state updates
}

// REMOVED: handleColorButtonClick function removed - color buttons deleted
// void MainWindow::handleColorButtonClick() {
//     // Phase 3.1.4: Use currentViewport()
//     DocumentViewport* vp = currentViewport();
//     if (!vp) return;
//
//     ToolType tool = vp->currentTool();
//
//     // If in eraser mode, switch back to pen mode
//     if (tool == ToolType::Eraser) {
//         vp->setCurrentTool(ToolType::Pen);
//         updateToolButtonStates();
//         // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
//     }
//
//     // TODO Phase 3.3: Rope tool mode handling (if implemented)
//     // For now, rope tool is InkCanvas-only and will be reimplemented later
// }

// REMOVED: updateThicknessSliderForCurrentTool function removed - thicknessSlider deleted
// void MainWindow::updateThicknessSliderForCurrentTool() {
//     // Phase 3.1.4: Use currentViewport()
//     DocumentViewport* vp = currentViewport();
//     if (!vp || !thicknessSlider) return;
//
//     // Block signals to prevent recursive calls
//     thicknessSlider->blockSignals(true);
//
//     // Update slider to reflect current tool's thickness
//     qreal currentThickness = vp->penThickness();
//
//     // Convert thickness back to slider value (reverse of updateThickness calculation)
//     qreal zoomPercent = vp->zoomLevel() * 100.0;
//     qreal visualThickness = currentThickness * (zoomPercent / 100.0);
//     int sliderValue = qBound(1, static_cast<int>(qRound(visualThickness)), 50);
//
//     thicknessSlider->setValue(sliderValue);
//     thicknessSlider->blockSignals(false);
// }

// REMOVED MW5.6: selectFolder function removed - .spn format deprecated

// MW1.5: Kept as stubs - still called from many places
void MainWindow::switchPage(int pageIndex) {
    // Phase S4: Main page switching function - everything goes through here
    // pageIndex is 0-based internally
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    vp->scrollToPage(pageIndex);
    // pageInput update is handled by currentPageChanged signal connection
}

void MainWindow::switchPageWithDirection(int pageIndex, int direction) {
    // Phase S4: direction parameter no longer used (was for magicdial animation)
    Q_UNUSED(direction);
    switchPage(pageIndex);
}

void MainWindow::saveCurrentPage() {
    qDebug() << "saveCurrentPage(): Stub (Phase 3.4)";
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


// REMOVED MW1.2: selectBackground() - feature was dropped

// REMOVED MW7.3: showPageRangeDialog function removed - export dialog function


// Phase 3.1.8: Stub implementation - function disabled
void MainWindow::exportCanvasOnlyNotebook(const QString &saveFolder, const QString &notebookId) {
    Q_UNUSED(saveFolder);
    Q_UNUSED(notebookId);
    qDebug() << "exportCanvasOnlyNotebook(): Disabled in Phase 3.1.8";
}

// Phase 3.1.8: Entire function disabled - uses InkCanvas


// Phase 3.1.8: Stub implementation - function disabled
// REMOVED MW7.3: exportAnnotatedPdfFullRender function removed - pdftk export function

// Phase 3.1.8: Entire function disabled - uses InkCanvas


// REMOVED MW7.3: createAnnotatedPagesPdf function removed - temp PDF creation function

// Phase 3.1.8: Entire function disabled - uses InkCanvas
// Phase 3.1.8: mergePdfWithPdftk disabled

// REMOVED MW7.3: mergePdfWithPdftk function removed - PDF merging function

// Extract PDF metadata including outline/bookmarks using pdftk
// Note: This function doesn't use InkCanvas, so it's not disabled
// REMOVED MW5.3: extractPdfOutlineData function removed - PDF outline extraction now handled by Document/DocumentViewport

// REMOVED MW7.3: filterAndAdjustOutline function removed - outline manipulation function

// REMOVED MW7.3: applyOutlineToPdf function removed - outline application function


void MainWindow::updateZoom() {
    // REMOVED MW5.2: zoomSlider reference removed - zoom now controlled by NavigationBar/Toolbar
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
    DocumentViewport* vp = currentViewport();
    if (vp) {
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
    // CR-2B: Disconnect tool/mode signal connections
    if (m_toolChangedConn) {
        disconnect(m_toolChangedConn);
        m_toolChangedConn = {};
    }
    if (m_straightLineModeConn) {
        disconnect(m_straightLineModeConn);
        m_straightLineModeConn = {};
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
    
    // CR-2B: Connect tool/mode signals for keyboard shortcut sync
    m_toolChangedConn = connect(viewport, &DocumentViewport::toolChanged, this, [this](ToolType) {
        updateToolButtonStates();
        // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    });
    
    // REMOVED E.1: straightLineToggleButton moved to Toolbar - no need to sync button state
    // m_straightLineModeConn = connect(viewport, &DocumentViewport::straightLineModeChanged, this, [this](bool enabled) {
    //     // Button state sync removed
    // });
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
// Phase 5.1: LayerPanel Integration
// ============================================================================

void MainWindow::updateLayerPanelForViewport(DocumentViewport* viewport) {
    // Disconnect previous page change connection
    if (m_layerPanelPageConn) {
        disconnect(m_layerPanelPageConn);
        m_layerPanelPageConn = {};
    }
    
    if (!m_layerPanel) return;
    
    if (!viewport) {
        m_layerPanel->setCurrentPage(nullptr);
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        m_layerPanel->setCurrentPage(nullptr);
        return;
    }
    
    // Phase 5.6.8: Use setEdgelessDocument for edgeless mode
    if (doc->isEdgeless()) {
        // Edgeless mode: LayerPanel reads from document's manifest
        m_layerPanel->setEdgelessDocument(doc);
        // No page change connection needed - manifest is global
    } else {
        // Paged mode: LayerPanel reads from current page
        int pageIndex = viewport->currentPageIndex();
        Page* page = doc->page(pageIndex);
        m_layerPanel->setCurrentPage(page);
        
        // Task 5: Connect viewport's currentPageChanged to update LayerPanel
        m_layerPanelPageConn = connect(viewport, &DocumentViewport::currentPageChanged, 
                                        this, [this, viewport](int pageIndex) {
            if (!m_layerPanel || !viewport) return;
            Document* doc = viewport->document();
            if (!doc || doc->isEdgeless()) return;
            
            Page* page = doc->page(pageIndex);
            
            // Task 9: Clamp activeLayerIndex if new page has fewer layers
            if (page) {
                int layerCount = page->layerCount();
                if (page->activeLayerIndex >= layerCount) {
                    page->activeLayerIndex = qMax(0, layerCount - 1);
                }
            }
            
            m_layerPanel->setCurrentPage(page);
        });
        
        // Phase S4: Connect viewport's currentPageChanged to update pageInput spinbox
        connect(viewport, &DocumentViewport::currentPageChanged, 
                this, [this](int pageIndex) {
            if (pageInput) {
                pageInput->blockSignals(true);
                pageInput->setValue(pageIndex + 1);  // Convert 0-based to 1-based for display
                pageInput->blockSignals(false);
            }
        });
    }
}

// ============================================================================
// Phase doc-1: Document Operations
// ============================================================================

void MainWindow::saveDocument()
{
    // Phase doc-1.1: Save current document to file
    // Uses DocumentManager for proper document handling
    // - Edgeless documents: saved as .snb bundles
    // - Paged documents: saved as .json files
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
            
    bool isEdgeless = doc->isEdgeless();
    
    // Check if document already has a permanent path (not temp bundle)
    QString existingPath = m_documentManager->documentPath(doc);
    bool isUsingTemp = m_documentManager->isUsingTempBundle(doc);
            
    if (!existingPath.isEmpty() && !isUsingTemp) {
        // ✅ Document was previously saved to permanent location - save in-place
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
        
        if (isEdgeless) {
            qDebug() << "saveDocument: Saved edgeless canvas with" 
                     << doc->tileIndexCount() << "tiles to" << existingPath;
        } else {
            qDebug() << "saveDocument: Saved" << doc->pageCount() << "pages to" << existingPath;
        }
                return;
            }
            
    // ✅ New document or temp bundle - show Save As dialog
    // Phase O1.7.6: All documents now use unified .snb bundle format
    QString defaultName = doc->name.isEmpty() ? 
        (isEdgeless ? "Untitled Canvas" : "Untitled") : doc->name;
    
    QString defaultPath = QDir::homePath() + "/" + defaultName + ".snb";
    
    QString filter = tr("SpeedyNote Bundle (*.snb);;All Files (*)");
    QString dialogTitle = isEdgeless ? tr("Save Canvas") : tr("Save Document");
    
    QString filePath = QFileDialog::getSaveFileName(
        this,
        dialogTitle,
        defaultPath,
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
                return;
            }
            
    // Ensure .snb extension (Phase O1.7.6: unified bundle format)
    if (!filePath.endsWith(".snb", Qt::CaseInsensitive)) {
        filePath += ".snb";
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
    
    if (isEdgeless) {
        qDebug() << "saveDocument: Saved edgeless canvas with"
                 << doc->tileIndexCount() << "tiles to" << filePath;
                } else {
        qDebug() << "saveDocument: Saved" << doc->pageCount() << "pages to" << filePath;
    }
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
    // Phase O1.7.6: Include .snb bundles in the filter (unified format)
    QString filter = tr("SpeedyNote Files (*.snb *.json *.snx);;SpeedyNote Bundle (*.snb);;Legacy JSON (*.json *.snx);;All Files (*)");
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
    
        // CRITICAL: Notify viewport that document structure changed
        // This invalidates layout cache and triggers repaint
        viewport->notifyDocumentStructureChanged();
        
        // Mark tab as modified
        int currentIndex = m_tabManager->currentIndex();
        if (currentIndex >= 0) {
            m_tabManager->markTabModified(currentIndex, true);
        }
        
        // Optionally scroll to the new page (user can do this manually for now)
        // viewport->scrollToPage(doc->pageCount() - 1);
    }
}

void MainWindow::insertPageInDocument()
{
    // Phase 3: Insert new page after current page
    // Works for both PDF and non-PDF documents (inserted page has no PDF background)
    
    if (!m_tabManager) {
        qDebug() << "insertPageInDocument: No tab manager";
        return;
        }
    
    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
        qDebug() << "insertPageInDocument: No current viewport";
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        qDebug() << "insertPageInDocument: No document in viewport";
        return;
    }

    // Get current page index and insert after it
    int currentPageIndex = viewport->currentPageIndex();
    int insertIndex = currentPageIndex + 1;
    
    // Clear undo/redo for pages >= insertIndex (they're shifting)
    // This must be done BEFORE the insert to avoid stale undo applying to wrong pages
    viewport->clearUndoStacksFrom(insertIndex);
    
    // Insert page after current
    Page* newPage = doc->insertPage(insertIndex);
    if (newPage) {
        qDebug() << "insertPageInDocument: Inserted page at" << insertIndex
                 << "in document" << doc->name << "(now" << doc->pageCount() << "pages)";
        
        // Notify viewport that document structure changed
        viewport->notifyDocumentStructureChanged();
        
        // Mark tab as modified
        int tabIndex = m_tabManager->currentIndex();
        if (tabIndex >= 0) {
            m_tabManager->markTabModified(tabIndex, true);
        }
        
        // Note: Don't auto-scroll - let user scroll manually if needed
        // (matches addPageToDocument behavior)
    }
}

void MainWindow::deletePageInDocument()
{
    // Phase 3B: Delete current page
    // - Non-PDF pages: delete entirely
    // - PDF pages: blocked (use external tool to modify PDF)
    
    if (!m_tabManager) {
        qDebug() << "deletePageInDocument: No tab manager";
        return;
    }

    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
        qDebug() << "deletePageInDocument: No current viewport";
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        qDebug() << "deletePageInDocument: No document in viewport";
        return;
                }
    
    // Guard 1: Cannot delete the last page
    if (doc->pageCount() <= 1) {
        QMessageBox::information(this, tr("Cannot Delete"),
            tr("Cannot delete the last remaining page."));
        return;
    }
    
    int currentPageIndex = viewport->currentPageIndex();
    Page* page = doc->page(currentPageIndex);
    if (!page) {
        qDebug() << "deletePageInDocument: Invalid page index" << currentPageIndex;
        return;
    }
    
    // Guard 2: Cannot delete PDF pages
    if (page->backgroundType == Page::BackgroundType::PDF) {
        QMessageBox::information(this, tr("Cannot Delete"),
            tr("Cannot delete PDF pages. Use an external tool to modify the PDF."));
        return;
            }
    
    // Clear undo/redo for pages >= currentPageIndex (they're shifting or being deleted)
    viewport->clearUndoStacksFrom(currentPageIndex);
    
    // Delete the page
    doc->removePage(currentPageIndex);
    
    qDebug() << "deletePageInDocument: Deleted page at" << currentPageIndex
             << "in document" << doc->name << "(now" << doc->pageCount() << "pages)";
    
    // Notify viewport that document structure changed
    viewport->notifyDocumentStructureChanged();
        
    // Mark tab as modified
    int tabIndex = m_tabManager->currentIndex();
    if (tabIndex >= 0) {
        m_tabManager->markTabModified(tabIndex, true);
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

        
void MainWindow::forceUIRefresh() {
    setWindowState(Qt::WindowNoState);  // Restore first
    setWindowState(Qt::WindowMaximized);  // Maximize again
}

// REMOVED MW7.3: loadPdf function removed - old PDF loading function


            

void MainWindow::switchTab(int index) {
    // Phase C.1.5: Updated to use m_tabManager instead of m_tabWidget
    // Many InkCanvas-specific features are stubbed for now
    
    if (!m_tabManager || !pageInput) {
        return;
    }

    if (index >= 0 && index < m_tabManager->tabCount()) {
        // QTabBar handles the tab switch internally via TabManager
        
        DocumentViewport *viewport = currentViewport();
        if (viewport) {
            // TODO Phase 3.3: Update page spinbox from viewport
            // int currentPage = viewport->currentPageIndex();
            // pageInput->blockSignals(true);
            // pageInput->setValue(currentPage + 1);
            // pageInput->blockSignals(false);
            
            // REMOVED MW5.2: zoomSlider removed - zoom now controlled by NavigationBar/Toolbar
        }
        
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        // TODO Phase 3.3: Reconnect these update functions to work with DocumentViewport
        // updateColorButtonStates();
        // updateStraightLineButtonState();
        // updateRopeToolButtonState();
        // updatePictureButtonState();
        // updatePdfTextSelectButtonState();
        // updateBookmarkButtonState();
        // MW2.2: Removed dial button state updates
        // updateToolButtonStates();
        // updateThicknessSliderForCurrentTool();
    }
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
    
    // Switch to the new tab (TabManager::createTab already does this, but ensure it's set)
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(tabIndex);
    }
    
    // Phase 3.3: Center content horizontally (one-time initial offset)
    // Defer to next event loop iteration so viewport has its final size
    QTimer::singleShot(0, this, [this, tabIndex]() {
        centerViewportContent(tabIndex);
    });
    
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    
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
    ========== END OLD INKCANVAS CODE ========== */
}

void MainWindow::addNewEdgelessTab()
{
    // Phase E7: Create a new edgeless (infinite canvas) document
    if (!m_tabManager || !m_documentManager) {
        qWarning() << "addNewEdgelessTab: TabManager or DocumentManager not initialized";
        return;
    }
    
    // Create a new edgeless document
    Document* doc = m_documentManager->createEdgelessDocument();
    if (!doc) {
        qWarning() << "addNewEdgelessTab: Failed to create edgeless document";
        return;
    }
    
    // Apply user's default background settings from QSettings
    {
        Page::BackgroundType defaultStyle;
        QColor defaultBgColor;
        QColor defaultGridColor;
        int defaultDensity;
        loadDefaultBackgroundSettings(defaultStyle, defaultBgColor, defaultGridColor, defaultDensity);
        
        // Update document defaults for tiles
        doc->defaultBackgroundType = defaultStyle;
        doc->defaultBackgroundColor = defaultBgColor;
        doc->defaultGridColor = defaultGridColor;
        doc->defaultGridSpacing = defaultDensity;
        doc->defaultLineSpacing = defaultDensity;
    }
    
    // Create a new tab with DocumentViewport
    QString tabTitle = doc->displayName();
    int tabIndex = m_tabManager->createTab(doc, tabTitle);
    
    qDebug() << "Created new edgeless tab at index" << tabIndex << "with document:" << tabTitle;
    
    // Switch to the new tab (TabManager::createTab already does this, but ensure it's set)
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(tabIndex);
    }
    
    // For edgeless, center on origin (0,0)
    QTimer::singleShot(0, this, [this, tabIndex]() {
        if (m_tabManager) {
            DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
            if (viewport) {
                // Center on origin - start with a small negative pan so origin is visible
                viewport->setPanOffset(QPointF(-100, -100));
            }
        }
    });
    
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

void MainWindow::loadFolderDocument()
{
    // TEMPORARY: Load .snb bundle document from directory
    // Uses directory selection because .snb is a folder, not a single file.
    // 
    // Phase O1.7.6: This now handles BOTH paged and edgeless bundles.
    // The mode is detected from the manifest and handled appropriately.
    //
    // TODO: Replace with unified file handling after implementing:
    // 1. Single-file packaging (.snb as zip/tar), OR
    // 2. Unified file picker that handles both files and folders
    
    if (!m_documentManager || !m_tabManager) {
        qWarning() << "loadBundleDocument: DocumentManager or TabManager not initialized";
        return;
    }
    
    // Use directory dialog to select .snb bundle folder
    QString bundlePath = QFileDialog::getExistingDirectory(
        this,
        tr("Open SpeedyNote Bundle (.snb folder)"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (bundlePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    // Validate that it's a .snb bundle (has document.json)
    QString manifestPath = bundlePath + "/document.json";
    if (!QFile::exists(manifestPath)) {
        QMessageBox::critical(this, tr("Load Error"),
            tr("Selected folder is not a valid SpeedyNote bundle.\n"
               "Missing document.json manifest.\n\n%1").arg(bundlePath));
        return;
    }
    
    // Load the bundle via DocumentManager (handles both paged and edgeless)
    Document* doc = m_documentManager->loadDocument(bundlePath);
    if (!doc) {
        QMessageBox::critical(this, tr("Load Error"),
            tr("Failed to load document from:\n%1").arg(bundlePath));
        return;
    }
    
    // Get document name from folder if not set
    if (doc->name.isEmpty()) {
        QFileInfo folderInfo(bundlePath);
        doc->name = folderInfo.baseName();
        // Remove .snb suffix if present
        if (doc->name.endsWith(".snb", Qt::CaseInsensitive)) {
            doc->name.chop(4);
        }
    }
    
    // Create new tab with the loaded document
    int tabIndex = m_tabManager->createTab(doc, doc->displayName());
    
    if (tabIndex >= 0) {
        // Switch to the new tab (TabManager::createTab already does this, but ensure it's set)
        if (m_tabBar) {
            m_tabBar->setCurrentIndex(tabIndex);
        }
        
        // Mode-specific setup
        bool isEdgeless = doc->isEdgeless();
        
        // Mode-specific initial positioning
        if (isEdgeless) {
            // Edgeless: Center on origin (use timer to ensure viewport is ready)
            QTimer::singleShot(0, this, [this, tabIndex]() {
                if (m_tabManager) {
                    DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
                    if (viewport) {
                        viewport->setPanOffset(QPointF(-100, -100));
                    }
                }
            });
        } else {
            // Paged: Center content horizontally (same as loadDocument)
            centerViewportContent(tabIndex);
        }
        
        if (isEdgeless) {
            qDebug() << "loadBundleDocument: Loaded edgeless canvas with" 
                     << doc->tileIndexCount() << "tiles indexed (lazy load) from" << bundlePath;
        } else {
            qDebug() << "loadBundleDocument: Loaded paged document with" 
                     << doc->pageCount() << "pages (lazy load) from" << bundlePath;
        }
    }
    
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}

// ========== LEGACY CODE BELOW ==========

    /* ========== OLD INKCANVAS CODE - KEPT FOR REFERENCE ==========
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

        // REMOVED: InkCanvas-related saving removed - InkCanvas being deleted
        // // ✅ Auto-save the current page before closing the tab
        // if (newCanvas && newCanvas->isEdited()) {
        //     int pageNumber = getCurrentPageForCanvas(newCanvas);
        //     newCanvas->saveToFile(pageNumber);
        //
        //     // ✅ COMBINED MODE FIX: Use combined-aware save for markdown/picture windows
        //     newCanvas->saveCombinedWindowsForPage(pageNumber);
        //
        //     // ✅ Mark as not edited to prevent double-saving in destructor
        //     newCanvas->setEdited(false);
        // }
        //
        // // ✅ Save the last accessed page and bookmarks before closing tab
        // if (newCanvas) {
        //     // ✅ Additional safety check before accessing canvas methods
        //     try {
        //         int currentPage = getCurrentPageForCanvas(newCanvas);
        //         newCanvas->setLastAccessedPage(currentPage);
        //
        //         // ✅ Save current bookmarks to JSON metadata
        //         saveBookmarks();
        //     } catch (...) {
        //         qWarning() << "Exception occurred while saving last accessed page";
        //     }
        // }

        // REMOVED: InkCanvas-related folder check removed - InkCanvas being deleted
        // // ✅ 1. PRIORITY: Handle saving first - user can cancel here
        // if (!ensureTabHasUniqueSaveFolder(newCanvas)) {
        //     closeButton->setEnabled(true); // Re-enable on cancellation
        //     return; // User cancelled saving, don't close tab
        // }

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
            // REMOVED: InkCanvas page navigation removed - use DocumentViewport instead
            switchPageWithDirection(targetPage + 1, 1); // Default to forward direction
            pageInput->setValue(targetPage + 1);
        }
    });
    // REMOVED MW5.3: PDF outline loading removed - now handled by Document/DocumentViewport
    // connect(newCanvas, &InkCanvas::pdfLoaded, this, [this]() {
    //     // Refresh PDF outline if sidebar is visible
    //     if (outlineSidebarVisible) {
    //         loadPdfOutline();
    //     }
    // });
    // REMOVED MW5.7: autoScroll signal connection removed - auto-scroll feature deprecated
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

    // REMOVED MW5.2: zoomSlider removed - zoom now controlled by NavigationBar/Toolbar
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    // REMOVED E.1: Button state functions no longer needed - Toolbar handles its own state
    // updateStraightLineButtonState();  // Initialize straight line button state for the new tab
    // updateRopeToolButtonState(); // Initialize rope tool button state for the new tab
    // REMOVED: updatePdfTextSelectButtonState removed - pdfTextSelectButton deleted
    // REMOVED: updateBookmarkButtonState removed - toggleBookmarkButton deleted
    // REMOVED: updatePictureButtonState removed - insertPictureButton deleted
    // MW2.2: Removed dial button state updates
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
    // REMOVED: Color button updates removed - buttons deleted
    // updateColorButtonStates();
    ========== END OLD INKCANVAS CODE ==========*/

void MainWindow::removeTabAt(int index) {
    // Phase 3.1.2: Use TabManager to remove tabs
    // Note: Document cleanup happens via tabCloseRequested signal handler (ML-1 fix)
    if (m_tabManager) {
        m_tabManager->closeTab(index);
    }
}

// MW1.4: Kept as stubs - still called from legacy InkCanvas code paths
// REMOVED: InkCanvas-related functions removed - InkCanvas being deleted
// bool MainWindow::ensureTabHasUniqueSaveFolder(InkCanvas* canvas) {
//     Q_UNUSED(canvas);
//     return true; // Allow closure
// }
//
// InkCanvas* MainWindow::currentCanvas() {
//     return nullptr; // Use currentViewport() instead
// }

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

// REMOVED: getCurrentPageForCanvas removed - InkCanvas being deleted
// // MW1.4: Kept as stub - still called from legacy InkCanvas code paths
// int MainWindow::getCurrentPageForCanvas(InkCanvas *canvas) {
//     Q_UNUSED(canvas);
//     return 0; // Use DocumentViewport::currentPageIndex() instead
// }

// REMOVED MW5.2+: toggleZoomSlider function removed - zoom buttons moved to NavigationBar/Toolbar

// REMOVED: toggleThicknessSlider function removed - thicknessFrame and thicknessButton deleted
// void MainWindow::toggleThicknessSlider() {
//     if (thicknessFrame->isVisible()) {
//         thicknessFrame->hide();
//         return;
//     }
//
//     // ✅ Set as a standalone pop-up window so it can receive events
//     thicknessFrame->setWindowFlags(Qt::Popup);
//
//     // ✅ Position it right below the button
//     QPoint buttonPos = thicknessButton->mapToGlobal(QPoint(0, thicknessButton->height()));
//     thicknessFrame->move(buttonPos.x(), buttonPos.y() + 5);
//
//     thicknessFrame->show();
// }


void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();  // Exit fullscreen mode
    } else {
        showFullScreen();  // Enter fullscreen mode
    }
}

void MainWindow::showJumpToPageDialog() {
    // Phase 3.1.8: Use currentViewport() instead of currentCanvas().
    // This one doesn't work yet. 
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
    // Phase S4: Thin wrapper - go to previous page (0-based)
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    switchPage(vp->currentPageIndex() - 1);
}

void MainWindow::goToNextPage() {
    // Phase S4: Thin wrapper - go to next page (0-based)
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    switchPage(vp->currentPageIndex() + 1);
}

void MainWindow::onPageInputChanged(int newPage) {
    // Phase S4: newPage is 1-based (from spinbox), convert to 0-based for switchPage
    switchPage(newPage - 1);
}

// MW2.2: toggleDial() removed - dial system deleted

// Phase S3: positionLeftSidebarTabs() removed - floating tabs replaced by LeftSidebarContainer
// void MainWindow::positionLeftSidebarTabs() { ... }

// REMOVED MW7.2: updateDialDisplay function removed - dial functionality deleted
// void MainWindow::updateDialDisplay() {
//     // MW2.2: Simplified updateDialDisplay - dial system removed
//     if (!dialDisplay) return;
//
//     // Phase 3.1.8: Use currentViewport() instead of currentCanvas()
//     DocumentViewport* vp = currentViewport();
//     if (!vp) {
//         dialDisplay->setText(tr("\n\nNo Canvas"));
//         return;
//     }
//
//     // MW2.2: Removed dial mode switching - keeping basic display
//     dialDisplay->setText(QString(tr("\n\nPage\n%1")).arg(vp->currentPageIndex() + 1));
// }

// MW2.2: handleModeSelection() removed - dial system deleted








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
    // Phase C.1.5: Use m_viewportStack instead of m_tabWidget
    QWidget *container = m_viewportStack ? m_viewportStack->parentWidget() : nullptr;
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
            // MW2.2: Removed mouseDialModeActive check

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

    // MW2.2: dialContainer drag handling removed - dial system deleted

    return QObject::eventFilter(obj, event);
}


// MW2.2: initializeDialSound function removed - dial system deleted



// MW2.2: Removed accumulatedRotationAfterLimit variable











// REMOVED MW5.5: addColorPreset function removed - color presets no longer used

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

// REMOVED: createButtonStyle function removed - button styling no longer needed in MainWindow



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
    
    // Phase A: Update NavigationBar theme
    if (m_navigationBar) {
        m_navigationBar->updateTheme(darkMode, accentColor);
    }
    
    // Phase B: Update Toolbar theme
    if (m_toolbar) {
        m_toolbar->updateTheme(darkMode);
    }
    
    // Phase C.2: TabBar handles its own theming
    if (m_tabBar) {
        m_tabBar->updateTheme(darkMode, accentColor);
    }
    
    // REMOVED MW5.1: controlBar styling removed - replaced by NavigationBar and Toolbar
    
    // Common floating tab styling colors (solid, not transparent)
    QString tabBgColor = darkMode ? "#3A3A3A" : "#EAEAEA";
    QString tabHoverColor = darkMode ? "#4A4A4A" : "#DADADA";
    QString tabBorderColor = darkMode ? "#555555" : "#CCCCCC";
    
    // MW2.2: Removed dial toolbar styling
    
    // Phase S3: Floating sidebar tab styling removed - using LeftSidebarContainer
    // Update left sidebar container theme
    if (m_leftSidebar) {
        m_leftSidebar->updateTheme(darkMode);
    }
    
    // MW2.2: Removed dial styling - dial system deleted
    // Phase C.1.5: Removed addTabButton styling - functionality now in NavigationBar
    
    // REMOVED MW7.5: PDF outline sidebar styling removed - outline sidebar deleted
    // // if (outlineSidebar && outlineTree) {
    //     bool darkMode = isDarkMode();
    //     QString bgColor = darkMode ? "rgba(45, 45, 45, 255)" : "rgba(250, 250, 250, 255)";
    //     QString borderColor = darkMode ? "rgba(80, 80, 80, 255)" : "rgba(200, 200, 200, 255)";
    //     QString textColor = darkMode ? "#E0E0E0" : "#333";
    //     QString hoverColor = darkMode ? "rgba(60, 60, 60, 255)" : "rgba(240, 240, 240, 255)";
    //     QString selectedColor = QString("rgba(%1, %2, %3, 100)").arg(accentColor.red()).arg(accentColor.green()).arg(accentColor.blue());
    //
    //     outlineSidebar->setStyleSheet(QString(R"(
    //         QWidget {
    //             background-color: %1;
    //             border-right: 1px solid %2;
    //         }
    //         QLabel {
    //             color: %3;
    //             background: transparent;
    //         }
    //     )").arg(bgColor).arg(borderColor).arg(textColor));
    //
    //     outlineTree->setStyleSheet(QString(R"(
    //         QTreeWidget {
    //             background-color: %1;
    //             border: none;
    //             color: %2;
    //             outline: none;
    //         }
    //         QTreeWidget::item {
    //             padding: 4px;
    //             border: none;
    //         }
    //         QTreeWidget::item:hover {
    //             background-color: %3;
    //         }
    //         QTreeWidget::item:selected {
    //             background-color: %4;
    //             color: %2;
    //         }
    //         QTreeWidget::branch {
    //             background: transparent;
    //         }
    //         QTreeWidget::branch:has-children:!has-siblings:closed,
    //         QTreeWidget::branch:closed:has-children:has-siblings {
    //             border-image: none;
    //             image: url(:/resources/icons/down_arrow.png);
    //         }
    //         QTreeWidget::branch:open:has-children:!has-siblings,
    //         QTreeWidget::branch:open:has-children:has-siblings {
    //             border-image: none;
    //             image: url(:/resources/icons/up_arrow.png);
    //         }
    //         QScrollBar:vertical {
    //             background: rgba(200, 200, 200, 80);
    //             border: none;
    //             margin: 0px;
    //             width: 16px !important;
    //             max-width: 16px !important;
    //         }
    //         QScrollBar:vertical:hover {
    //             background: rgba(200, 200, 200, 120);
    //         }
    //         QScrollBar::handle:vertical {
    //             background: rgba(100, 100, 100, 150);
    //             border-radius: 2px;
    //             min-height: 120px;
    //         }
    //         QScrollBar::handle:vertical:hover {
    //             background: rgba(80, 80, 80, 210);
    //         }
    //         QScrollBar::add-line:vertical,
    //         QScrollBar::sub-line:vertical {
    //             width: 0px;
    //             height: 0px;
    //             background: none;
    //             border: none;
    //         }
    //         QScrollBar::add-page:vertical,
    //         QScrollBar::sub-page:vertical {
    //             background: transparent;
    //         }
    //     )").arg(bgColor).arg(textColor).arg(hoverColor).arg(selectedColor));
    //  }
        // REMOVED MW7.4: Bookmarks tree styling removed - bookmark implementation deleted
    }
    
    // Phase C.1.5: Removed old m_tabWidget styling - now using m_tabBar with StyleLoader

    
    // REMOVED: Button icon updates removed - buttons deleted
    // // Force icon reload for all buttons that use themed icons
    // // Use updateButtonIcon for buttons with selectable states, direct setIcon for others
    // if (loadPdfButton) loadPdfButton->setIcon(loadThemedIcon("pdf"));
    // if (clearPdfButton) clearPdfButton->setIcon(loadThemedIcon("pdfdelete"));
    // updateButtonIcon(pdfTextSelectButton, "ibeam");
    //
    // updateButtonIcon(benchmarkButton, "benchmark");
    // // REMOVED E.1: toggleTabBarButton moved to NavigationBar
    // // REMOVED S1: Floating sidebar buttons moved to LeftSidebarContainer
    // updateButtonIcon(toggleBookmarkButton, "star");
    // // REMOVED MW5.6+: selectFolderButton removed - .spn format deprecated
    // // REMOVED E.1: saveButton moved to NavigationBar
    // if (exportPdfButton) exportPdfButton->setIcon(loadThemedIcon("export"));
    // // REMOVED E.1: fullscreenButton moved to NavigationBar
    // // REMOVED E.1: straightLineToggleButton and ropeToolButton moved to Toolbar
    // if (deletePageButton) deletePageButton->setIcon(loadThemedIcon("trash"));
    // // REMOVED MW5.2+: zoomButton moved to NavigationBar/Toolbar
    // // MW2.2: Removed dialToggleButton icon update
    // // MW2.2: Removed fastForwardButton icon update
    // if (jumpToPageButton) jumpToPageButton->setIcon(loadThemedIcon("bookpage"));
    // if (thicknessButton) thicknessButton->setIcon(loadThemedIcon("thickness"));
    // // REMOVED MW5.5: addPresetButton removed - color presets no longer used
    // if (openControlPanelButton) openControlPanelButton->setIcon(loadThemedIcon("settings"));
    // Phase C.1.5: Removed openRecentNotebooksButton - functionality now in NavigationBar
    // REMOVED E.1: Tool buttons moved to Toolbar
    // updateButtonIcon(penToolButton, "pen");
    // updateButtonIcon(markerToolButton, "marker");
    // REMOVED: All button styling removed from MainWindow - buttons moved to components


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

// REMOVED: areBenchmarkControlsVisible function removed - benchmarkButton deleted
// bool MainWindow::areBenchmarkControlsVisible() const {
//     return benchmarkButton->isVisible() && benchmarkLabel->isVisible();
// }

// REMOVED: setBenchmarkControlsVisible function removed - benchmark controls deleted
// void MainWindow::setBenchmarkControlsVisible(bool visible) {
//     benchmarkButton->setVisible(visible);
//     benchmarkLabel->setVisible(visible);
// }

// REMOVED MW5.2+: areZoomButtonsVisible function removed - zoom buttons moved to NavigationBar/Toolbar

// REMOVED MW5.2+: setZoomButtonsVisible function removed - zoom buttons moved to NavigationBar/Toolbar



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
    
    // TG.6: Apply touch gesture mode to current DocumentViewport
    if (DocumentViewport* vp = currentViewport()) {
        vp->setTouchGestureMode(mode);
        }
    
    // TODO: Apply to all viewports when TabManager supports iteration
    // For now, each new viewport gets the mode applied in openDocumentInNewTab()
    
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

// MW1.5: Kept as stubs - still called from stylus button handlers
void MainWindow::enableStylusButtonMode(Qt::MouseButton button) {
    Q_UNUSED(button);
    qDebug() << "enableStylusButtonMode(): Stub (Phase 3.3)";
}

void MainWindow::disableStylusButtonMode(Qt::MouseButton button) {
    Q_UNUSED(button);
    qDebug() << "disableStylusButtonMode(): Stub (Phase 3.3)";
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

// MW2.2: setTemporaryDialMode and clearTemporaryDialMode functions removed - dial system deleted



// MW2.2: handleButtonHeld simplified - dial system removed
void MainWindow::handleButtonHeld(const QString &buttonName) {
    // MW2.2: Removed dial mode switching
}

// MW2.2: handleButtonReleased simplified - dial system removed
void MainWindow::handleButtonReleased(const QString &buttonName) {
    // MW2.2: Removed dial mode switching
}



// MainWindow.cpp

QString MainWindow::getPressMapping(const QString &buttonName) {
    return buttonPressMapping.value(buttonName, "None");
}

void MainWindow::saveButtonMappings() {
    QSettings settings("SpeedyNote", "App");

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
    
    // Check if migration is needed by looking for old format strings in press mappings
        settings.beginGroup("ButtonPressMappings");
        QStringList pressKeys = settings.allKeys();
    bool needsMigration = false;
    
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
    
    if (!needsMigration) return;
    
    // Migrate press mappings
    settings.beginGroup("ButtonPressMappings");
    pressKeys = settings.allKeys();
    for (const QString &key : pressKeys) {
        QString oldValue = settings.value(key).toString();
        QString newValue = migrateOldActionString(oldValue);
        if (newValue != oldValue) {
            settings.setValue(key, newValue);
        }
    }
    settings.endGroup();
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
            toggleFullscreen();
            break;
        // REMOVED MW7.2: ToggleDial case removed - dial functionality deleted
        case ControllerAction::Zoom50:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(0.5);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        case ControllerAction::ZoomOut:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(1.0);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        case ControllerAction::Zoom200:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(2.0);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        // REMOVED MW5.5: AddPreset action removed - color presets no longer used
        case ControllerAction::DeletePage:
            deletePageButton->click();  // assuming you have this
            break;
        case ControllerAction::FastForward:
            // MW2.2: Removed fastForwardButton click - dial system deleted
            break;
        case ControllerAction::OpenControlPanel:
            // REMOVED: openControlPanelButton removed - show message instead
QMessageBox::information(this, tr("Control Panel"), tr("Control Panel is being redesigned. Coming soon!"));
            break;
        case ControllerAction::RedColor:
            // REMOVED: redButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("red"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::BlueColor:
            // REMOVED: blueButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("blue"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::YellowColor:
            // REMOVED: yellowButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("yellow"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::GreenColor:
            // REMOVED: greenButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("green"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::BlackColor:
            // REMOVED: blackButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(QColor("#000000"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::WhiteColor:
            // REMOVED: whiteButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(QColor("#FFFFFF"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::CustomColor:
            customColorButton->click();
            break;
        case ControllerAction::ToggleSidebar:
            // REMOVED E.1: toggleTabBarButton moved to NavigationBar
            if (m_tabBar) {
                bool isVisible = m_tabBar->isVisible();
                m_tabBar->setVisible(!isVisible);
            }
            break;
        case ControllerAction::Save:
            saveCurrentPage();
            break;
        case ControllerAction::StraightLineTool:
            // REMOVED E.1: straightLineToggleButton moved to Toolbar
            if (DocumentViewport* vp = currentViewport()) {
                vp->setStraightLineMode(true);
            }
            break;
        case ControllerAction::RopeTool:
            // REMOVED E.1: ropeToolButton moved to Toolbar
            if (DocumentViewport* vp = currentViewport()) {
                vp->setCurrentTool(ToolType::Lasso);
            }
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
            // REMOVED: pdfTextSelectButton removed - PDF text selection stubbed
            break;
        // REMOVED MW7.5: ToggleOutline case removed - outline sidebar deleted
        // REMOVED MW7.4: ToggleBookmarks and AddBookmark cases removed - bookmark implementation deleted
        case ControllerAction::ToggleTouchGestures:
            cycleTouchGestureMode();
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

    // REMOVED MW5.2+: zoomButtonsVisible moved to NavigationBar/Toolbar

    scrollOnTopEnabled = settings.value("scrollOnTopEnabled", true).toBool();
    setScrollOnTopEnabled(scrollOnTopEnabled);

    // Load touch gesture mode (default to Full for backwards compatibility)
    int savedMode = settings.value("touchGestureMode", static_cast<int>(TouchGestureMode::Full)).toInt();
    touchGestureMode = static_cast<TouchGestureMode>(savedMode);
    setTouchGestureMode(touchGestureMode);
    
    // REMOVED E.1: touchGesturesButton moved to Toolbar - no need to update button state
    
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

// REMOVED MW5.1: toggleControlBar function removed - controlBar deleted and fullscreen handled by NavigationBar

// REMOVED MW5.2+: cycleZoomLevels function removed - zoom controls moved to NavigationBar/Toolbar


// REMOVED: updateColorButtonStates function removed - color buttons deleted
// void MainWindow::updateColorButtonStates() {
//     // Phase 3.1.4: Use currentViewport()
//     DocumentViewport* vp = currentViewport();
//     if (!vp) return;
//
//     // Get current pen color
//     QColor currentColor = vp->penColor();
//
//     // Determine if we're in dark mode to match the correct colors
//     bool darkMode = isDarkMode();
//
//     // Reset all color buttons to original style
//     redButton->setProperty("selected", false);
//     blueButton->setProperty("selected", false);
//     yellowButton->setProperty("selected", false);
//     greenButton->setProperty("selected", false);
//     blackButton->setProperty("selected", false);
//     whiteButton->setProperty("selected", false);
//
//     // Update all color button icons to unselected state
//     QString redIconName = darkMode ? "pen_light_red" : "pen_dark_red";
//     QString blueIconName = darkMode ? "pen_light_blue" : "pen_dark_blue";
//     QString yellowIconName = darkMode ? "pen_light_yellow" : "pen_dark_yellow";
//     QString greenIconName = darkMode ? "pen_light_green" : "pen_dark_green";
//     QString blackIconName = darkMode ? "pen_light_black" : "pen_dark_black";
//     QString whiteIconName = darkMode ? "pen_light_white" : "pen_dark_white";
//
//     // Set the selected property for the matching color button based on current palette
//     QColor redColor = getPaletteColor("red");
//     QColor blueColor = getPaletteColor("blue");
//     QColor yellowColor = getPaletteColor("yellow");
//     QColor greenColor = getPaletteColor("green");
//
//     if (currentColor == redColor) {
//         redButton->setProperty("selected", true);
//         // For color buttons, we don't reverse the icon - the colored pen icon should stay
//     } else if (currentColor == blueColor) {
//         blueButton->setProperty("selected", true);
//     } else if (currentColor == yellowColor) {
//         yellowButton->setProperty("selected", true);
//     } else if (currentColor == greenColor) {
//         greenButton->setProperty("selected", true);
//     } else if (currentColor == QColor("#000000")) {
//         blackButton->setProperty("selected", true);
//     } else if (currentColor == QColor("#FFFFFF")) {
//         whiteButton->setProperty("selected", true);
//     }
//
//     // Force style update
//     redButton->style()->unpolish(redButton);
//     redButton->style()->polish(redButton);
//     blueButton->style()->unpolish(blueButton);
//     blueButton->style()->polish(blueButton);
//     yellowButton->style()->unpolish(yellowButton);
//     yellowButton->style()->polish(yellowButton);
//     greenButton->style()->unpolish(greenButton);
//     greenButton->style()->polish(greenButton);
//     blackButton->style()->unpolish(blackButton);
//     blackButton->style()->polish(blackButton);
//     whiteButton->style()->unpolish(whiteButton);
//     whiteButton->style()->polish(whiteButton);
// }

void MainWindow::selectColorButton(QPushButton* selectedButton) {
    // REMOVED: Color button updates removed - buttons deleted
    // updateColorButtonStates();
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
    // REMOVED E.1: straightLineToggleButton moved to Toolbar
    // Function kept as stub for compatibility
}

void MainWindow::updateRopeToolButtonState() {
    // REMOVED E.1: ropeToolButton moved to Toolbar
    // Function kept as stub for compatibility
}

// REMOVED: updatePictureButtonState function removed - insertPictureButton deleted
// void MainWindow::updatePictureButtonState() {
//     // Phase O2.9: Track ObjectSelect tool state
//     bool isActive = false;
//
//     if (DocumentViewport* vp = currentViewport()) {
//         isActive = (vp->currentTool() == ToolType::ObjectSelect);
//     }
//
//     // Set visual indicator that the button is active/inactive
//     if (insertPictureButton) {
//         insertPictureButton->setProperty("selected", isActive);
//         updateButtonIcon(insertPictureButton, "background");
//
//         // Force style update
//         insertPictureButton->style()->unpolish(insertPictureButton);
//         insertPictureButton->style()->polish(insertPictureButton);
//     }
// }

// REMOVED MW7.2: Dial system functions removed - dial functionality deleted
// void MainWindow::updateDialButtonState() { }
// void MainWindow::updateFastForwardButtonState() { }
// void MainWindow::toggleDial() { }
// void MainWindow::positionDialContainer() { }
// void MainWindow::initializeDialSound() { }

void MainWindow::wheelEvent(QWheelEvent *event) {
    // MW2.2: Forward to base class - dial wheel handling removed
    QMainWindow::wheelEvent(event);
}

// Add this new method
void MainWindow::updateScrollbarPositions() {
    // Phase C.1.5: Use m_viewportStack instead of m_tabWidget
    QWidget *container = m_viewportStack ? m_viewportStack->parentWidget() : nullptr;
    if (!container || !panXSlider || !panYSlider || !m_viewportStack) return;
    
    // Get tab bar height to offset positions (sliders should be below tab bar)
    int tabBarHeight = (m_tabBar && m_tabBar->isVisible()) ? m_tabBar->height() : 0;
    
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

// REMOVED MW1.4: handleEdgeProximity(InkCanvas*, QPoint&) - InkCanvas obsolete

void MainWindow::returnToLauncher() {
    // Phase 3.1: LauncherWindow disconnected - will be re-linked later
    // TODO Phase 3.5: Re-implement launcher return functionality
    QMessageBox::information(this, tr("Return to Launcher"), 
        tr("Launcher is being redesigned. This feature will return soon!"));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    
    // REMOVED E.1: layoutUpdateTimer no longer needed
    // // Use a timer to delay layout updates during resize to prevent excessive switching
    // if (!layoutUpdateTimer) {
    //     layoutUpdateTimer = new QTimer(this);
    //     layoutUpdateTimer->setSingleShot(true);
    //     connect(layoutUpdateTimer, &QTimer::timeout, this, [this]() {
    //         // REMOVED E.1: updateToolbarLayout no longer needed
    //         updateTabSizes(); // Update tab widths when window resizes
    //         // Reposition floating sidebar tabs
    //         // REMOVED S1: positionLeftSidebarTabs() removed - floating tabs replaced by LeftSidebarContainer
    //         // MW2.2: Removed dial container positioning
    //     });
    // }
    //
    // layoutUpdateTimer->stop();
    // layoutUpdateTimer->start(100); // Wait 100ms after resize stops
}

void MainWindow::handleKeyboardShortcut(const QString &keySequence) {
    ControllerAction action = keyboardActionMapping.value(keySequence, ControllerAction::None);
    
    // Use the same handler as Joy-Con buttons
    switch (action) {
        case ControllerAction::ToggleFullscreen:
            toggleFullscreen();
            break;
        // REMOVED MW7.2: ToggleDial case removed - dial functionality deleted
        case ControllerAction::Zoom50:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(0.5);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        case ControllerAction::ZoomOut:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(1.0);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        case ControllerAction::Zoom200:
            if (DocumentViewport* vp = currentViewport()) {
                vp->setZoomLevel(2.0);
                // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
            }
            break;
        // REMOVED MW5.5: AddPreset action removed - color presets no longer used
        case ControllerAction::DeletePage:
            deletePageButton->click();
            break;
        case ControllerAction::FastForward:
            // MW2.2: Removed fastForwardButton click - dial system deleted
            break;
        case ControllerAction::OpenControlPanel:
            // REMOVED: openControlPanelButton removed - show message instead
QMessageBox::information(this, tr("Control Panel"), tr("Control Panel is being redesigned. Coming soon!"));
            break;
        case ControllerAction::RedColor:
            // REMOVED: redButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("red"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::BlueColor:
            // REMOVED: blueButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("blue"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::YellowColor:
            // REMOVED: yellowButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("yellow"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::GreenColor:
            // REMOVED: greenButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(getPaletteColor("green"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::BlackColor:
            // REMOVED: blackButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(QColor("#000000"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::WhiteColor:
            // REMOVED: whiteButton removed - set color directly
if (DocumentViewport* vp = currentViewport()) {
    vp->setPenColor(QColor("#FFFFFF"));
    // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
}
            break;
        case ControllerAction::CustomColor:
            customColorButton->click();
            break;
        case ControllerAction::ToggleSidebar:
            // REMOVED E.1: toggleTabBarButton moved to NavigationBar
            if (m_tabBar) {
                bool isVisible = m_tabBar->isVisible();
                m_tabBar->setVisible(!isVisible);
            }
            break;
        case ControllerAction::Save:
            saveCurrentPage();
            break;
        case ControllerAction::StraightLineTool:
            // REMOVED E.1: straightLineToggleButton moved to Toolbar
            if (DocumentViewport* vp = currentViewport()) {
                vp->setStraightLineMode(true);
            }
            break;
        case ControllerAction::RopeTool:
            // REMOVED E.1: ropeToolButton moved to Toolbar
            if (DocumentViewport* vp = currentViewport()) {
                vp->setCurrentTool(ToolType::Lasso);
            }
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
            // REMOVED: pdfTextSelectButton removed - PDF text selection stubbed
            break;
        // REMOVED MW7.5: ToggleOutline case removed - outline sidebar deleted
        // REMOVED MW7.4: ToggleBookmarks and AddBookmark cases removed - bookmark implementation deleted
        case ControllerAction::ToggleTouchGestures:
            cycleTouchGestureMode();
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

// REMOVED MW5.2: onZoomSliderChanged function removed - zoomSlider deleted

void MainWindow::saveDefaultBackgroundSettings(Page::BackgroundType style, QColor bgColor, QColor gridColor, int density) {
    // Phase doc-1: Using new key "defaultBgType" to avoid stale values from old BackgroundStyle enum
    QSettings settings("SpeedyNote", "App");
    settings.setValue("defaultBgType", static_cast<int>(style));
    settings.setValue("defaultBackgroundColor", bgColor.name());
    settings.setValue("defaultGridColor", gridColor.name());  // Phase doc-1: Added grid color
    settings.setValue("defaultBackgroundDensity", density);
}

// PDF Outline functionality
// REMOVED MW7.5: toggleOutlineSidebar function removed - outline sidebar deleted

// REMOVED MW7.5: onOutlineItemClicked function removed - outline sidebar deleted

// REMOVED MW5.3: loadPdfOutline and addOutlineItem functions removed - PDF outline now handled by Document/DocumentViewport

// REMOVED MW7.5: updateOutlineSelection function removed - outline sidebar deleted

// REMOVED MW5.3: getPdfDocument function removed - PDF document access now handled by Document/DocumentViewport

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

// REMOVED MW1.4: applyDefaultBackgroundToCanvas(InkCanvas*) - use Page background settings

// REMOVED: updatePdfTextSelectButtonState function removed - pdfTextSelectButton deleted
// void MainWindow::updatePdfTextSelectButtonState() {
//     // Phase 3.1.5: Stubbed - PDF text selection not yet implemented in DocumentViewport
//     if (pdfTextSelectButton) {
//         pdfTextSelectButton->setProperty("selected", false);
//         updateButtonIcon(pdfTextSelectButton, "ibeam");
//         pdfTextSelectButton->style()->unpolish(pdfTextSelectButton);
//         pdfTextSelectButton->style()->polish(pdfTextSelectButton);
//     }
// }

QString MainWindow::elideTabText(const QString &text, int maxWidth) {
    // Create a font metrics object using the default font
    QFontMetrics fontMetrics(QApplication::font());
    
    // Elide the text from the right (showing the beginning)
    return fontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
}

// REMOVED MW7.4: toggleBookmarksSidebar function removed - bookmark implementation deleted

// REMOVED MW5.4: toggleLayerPanel function removed - layer panel now handled by LeftSidebarContainer

void MainWindow::toggleDebugOverlay() {
    if (!m_debugOverlay) return;
    
    m_debugOverlay->toggle();
        
    // Connect to current viewport if shown
    if (m_debugOverlay->isOverlayVisible()) {
        m_debugOverlay->setViewport(currentViewport());
    }
}

void MainWindow::toggleAutoLayout() {
    DocumentViewport* viewport = currentViewport();
    if (!viewport) return;
    
    Document* doc = viewport->document();
    if (!doc || doc->isEdgeless()) {
        // Auto layout only applies to paged documents
        qDebug() << "Auto layout not available for edgeless canvas";
        return;
    }
    
    bool newState = !viewport->autoLayoutEnabled();
    viewport->setAutoLayoutEnabled(newState);
    
    // Show status feedback via debug console
    if (newState) {
        qDebug() << "Auto layout enabled (1/2 columns)";
    } else {
        qDebug() << "Single column layout";
    }
}

// REMOVED MW7.4: onBookmarkItemClicked function removed - bookmark implementation deleted

// REMOVED MW7.4: loadBookmarks function removed - bookmark implementation deleted

// REMOVED MW7.4: saveBookmarks function removed - bookmark implementation deleted

// Markdown Notes Sidebar functionality
void MainWindow::toggleMarkdownNotesSidebar() {
    if (!markdownNotesSidebar) return;
    
    bool isVisible = markdownNotesSidebar->isVisible();
    
    // Note: Markdown notes sidebar (right side) is independent of 
    // outline/bookmarks sidebars (left side), so we don't hide them here.
    // The left sidebars are mutually exclusive with each other, but not with markdown notes.
    
    markdownNotesSidebar->setVisible(!isVisible);
    markdownNotesSidebarVisible = !isVisible;
    
    // REMOVED E.1: toggleMarkdownNotesButton moved to NavigationBar - no need to update button state
    
    if (markdownNotesSidebarVisible) {
        // MW1.5: loadMarkdownNotesForCurrentPage() removed - will be reimplemented
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
    
    // Reposition floating tabs after layout settles
    QTimer::singleShot(0, this, [this]() {
        // REMOVED S1: positionLeftSidebarTabs() removed - floating tabs replaced by LeftSidebarContainer
        // MW2.2: Removed dial container positioning
    });
}



// REMOVED: updateBookmarkButtonState function removed - toggleBookmarkButton deleted
// void MainWindow::updateBookmarkButtonState() {
//     // Phase 3.1.9: Use currentViewport() instead of currentCanvas()
//     if (!toggleBookmarkButton) return;
//
//     DocumentViewport* vp = currentViewport();
//     int currentPage = vp ? vp->currentPageIndex() + 1 : 1;
//     bool isBookmarked = bookmarks.contains(currentPage);
//
//     toggleBookmarkButton->setProperty("selected", isBookmarked);
//     updateButtonIcon(toggleBookmarkButton, "star");
//
//     // Update tooltip
//     if (isBookmarked) {
//         toggleBookmarkButton->setToolTip(tr("Remove Bookmark"));
//     } else {
//         toggleBookmarkButton->setToolTip(tr("Add Bookmark"));
//     }
//
//     // Force style update
//     toggleBookmarkButton->style()->unpolish(toggleBookmarkButton);
//     toggleBookmarkButton->style()->polish(toggleBookmarkButton);
// }

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
        // REMOVED: updateColorPalette removed - color buttons deleted
        
        // Save preference
        QSettings settings("SpeedyNote", "App");
        settings.setValue("useBrighterPalette", useBrighterPalette);
    }
}

// REMOVED: updateColorPalette function removed - color buttons deleted
// void MainWindow::updateColorPalette() {
//     // REMOVED MW5.5: colorPresets functionality removed - only keeping icon updates
//
//     // Only update UI elements if they exist
//     if (redButton && blueButton && yellowButton && greenButton) {
//         // Update color button icons based on current palette (not theme)
//         QString redIconPath = useBrighterPalette ? ":/resources/icons/pen_light_red.png" : ":/resources/icons/pen_dark_red.png";
//         QString blueIconPath = useBrighterPalette ? ":/resources/icons/pen_light_blue.png" : ":/resources/icons/pen_dark_blue.png";
//         QString yellowIconPath = useBrighterPalette ? ":/resources/icons/pen_light_yellow.png" : ":/resources/icons/pen_dark_yellow.png";
//         QString greenIconPath = useBrighterPalette ? ":/resources/icons/pen_light_green.png" : ":/resources/icons/pen_dark_green.png";
//
//         redButton->setIcon(QIcon(redIconPath));
//         blueButton->setIcon(QIcon(blueIconPath));
//         yellowButton->setIcon(QIcon(yellowIconPath));
//         greenButton->setIcon(QIcon(greenIconPath));
//
//         // Update color button states
//         // REMOVED: Color button updates removed - buttons deleted
//     // updateColorButtonStates();
//     }
// }

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

// MW2.2: reconnectControllerSignals simplified - dial system removed
void MainWindow::reconnectControllerSignals() {
    if (!controllerManager) {
        return;
    }
    
    // Disconnect all existing connections to avoid duplicates
    disconnect(controllerManager, nullptr, this, nullptr);
    
    // Reconnect controller signals (excluding dial-specific ones)
    connect(controllerManager, &SDLControllerManager::buttonHeld, this, &MainWindow::handleButtonHeld);
    connect(controllerManager, &SDLControllerManager::buttonReleased, this, &MainWindow::handleButtonReleased);
    connect(controllerManager, &SDLControllerManager::buttonSinglePress, this, &MainWindow::handleControllerButton);
    
    // MW2.2: Removed dial-related connections and state resets
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
    
    // ========== CHECK FOR UNSAVED EDGELESS DOCUMENTS ==========
    // Iterate through all tabs and prompt for unsaved edgeless documents
    if (m_tabManager && m_documentManager) {
        for (int i = 0; i < m_tabManager->tabCount(); ++i) {
            Document* doc = m_tabManager->documentAt(i);
            if (!doc || !doc->isEdgeless()) continue;
            
            // Check if this is an unsaved edgeless document with content
            bool hasContent = doc->tileCount() > 0 || doc->tileIndexCount() > 0;
            bool isUnsaved = m_documentManager->isUsingTempBundle(doc);
            
            if (hasContent && isUnsaved) {
                // Switch to this tab so user knows which canvas we're asking about
                if (m_tabBar) {
                    m_tabBar->setCurrentIndex(i);
                }
                
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    tr("Save Canvas?"),
                    tr("The canvas \"%1\" has unsaved changes. Do you want to save before quitting?")
                        .arg(doc->displayName()),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Save
                );
                
                if (reply == QMessageBox::Cancel) {
                    // User cancelled - abort quit
                    event->ignore();
                    return;
                }
                
                if (reply == QMessageBox::Save) {
                    // Show save dialog
                    QString defaultName = doc->name.isEmpty() ? "Untitled Canvas" : doc->name;
                    QString defaultPath = QDir::homePath() + "/" + defaultName + ".snb";
                    
                    QString savePath = QFileDialog::getSaveFileName(
                        this,
                        tr("Save Edgeless Canvas"),
                        defaultPath,
                        tr("SpeedyNote Bundle (*.snb)")
                    );
                    
                    if (savePath.isEmpty()) {
                        // User cancelled save dialog - abort quit
                        event->ignore();
                        return;
                    }
                    
                    // Ensure .snb extension
                    if (!savePath.endsWith(".snb", Qt::CaseInsensitive)) {
                        savePath += ".snb";
                    }
                    
                    // Save to the chosen location
                    if (!m_documentManager->saveDocumentAs(doc, savePath)) {
                        QMessageBox::critical(this, tr("Save Error"),
                            tr("Failed to save canvas to:\n%1\n\nQuit anyway?").arg(savePath));
                        // Don't abort - let them quit without saving if save failed
                    }
                }
                // If Discard, continue to next document
                }
            }
        }
    // ===========================================================
        
        // REMOVED MW7.4: Save bookmarks removed - bookmark implementation deleted
        // saveBookmarks();
    
    // Accept the close event to allow the program to close
    event->accept();
}

// REMOVED MW1.4: showLastAccessedPageDialog(InkCanvas*) - InkCanvas obsolete

// REMOVED MW5.6: openSpnPackage function removed - .spn format deprecated

// REMOVED MW5.6: createNewSpnPackage function removed - .spn format deprecated
    
// MW1.5: Kept as stubs - still called from openFileInNewTab
void MainWindow::openPdfFile(const QString &pdfPath) {
    Q_UNUSED(pdfPath);
    QMessageBox::information(this, tr("Open PDF"), 
        tr("PDF opening is being redesigned. Coming soon!"));
}

// REMOVED MW5.6: switchToExistingNotebook function removed - .spn format deprecated

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
                
                // REMOVED MW5.6: .spn format deprecated - only handle regular file opening
                    openFileInNewTab(command);
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
    // Create a new tab first
    addNewTab();
    
    // Open the file in the new tab
    // REMOVED MW5.6: .spn format deprecated - only PDF files supported now
    if (filePath.toLower().endsWith(".pdf")) {
        openPdfFile(filePath);
    }
}

// ✅ MOUSE DIAL CONTROL IMPLEMENTATION

// MW2.2: mousePressEvent simplified - dial system removed
void MainWindow::mousePressEvent(QMouseEvent *event) {
    // MW2.2: Removed mouse dial tracking
    QMainWindow::mousePressEvent(event);
}

// MW2.2: mouseReleaseEvent simplified - dial system removed
void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    // MW2.2: Removed mouse dial tracking - keeping only basic functionality
                if (event->button() == Qt::BackButton) {
                    goToPreviousPage();
                } else if (event->button() == Qt::ForwardButton) {
                    goToNextPage();
    }
    
    QMainWindow::mouseReleaseEvent(event);
}









// REMOVED MW5.7: onAutoScrollRequested function removed - auto-scroll feature deprecated

