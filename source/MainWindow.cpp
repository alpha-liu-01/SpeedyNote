#include "MainWindow.h"
// #include "InkCanvas.h"  // Phase 3.1.7: Disconnected - using DocumentViewport
// #include "VectorCanvas.h"  // REMOVED Phase 3.1.3 - Features migrated to DocumentViewport
#include "core/DocumentViewport.h"  // Phase 3.1: New viewport architecture
#include "core/Document.h"          // Phase 3.1: Document class
#include "ui/sidebars/LayerPanel.h" // Phase S1: Moved to sidebars folder
#include "ui/DebugOverlay.h"        // Debug overlay (toggle with D key)
#include "ui/StyleLoader.h"         // QSS stylesheet loader
// Phase D: Subtoolbar includes
#include "ui/subtoolbars/SubToolbarContainer.h"
#include "ui/subtoolbars/PenSubToolbar.h"
#include "ui/subtoolbars/MarkerSubToolbar.h"
#include "ui/subtoolbars/HighlighterSubToolbar.h"
#include "ui/subtoolbars/ObjectSelectSubToolbar.h"
#include "objects/LinkObject.h"  // For LinkSlot slot state access
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
#include <QInputDevice>  // MW5.8: For keyboard detection
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
    : QMainWindow(parent), localServer(nullptr) {

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
        // Clean up subtoolbar per-tab state to prevent memory leak
        if (m_subtoolbarContainer) {
            m_subtoolbarContainer->clearTabState(index);
        }
        
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
    
    setupUi();    // ✅ Move all UI setup here

    controllerManager = new SDLControllerManager();
    controllerThread = new QThread(this);

    controllerManager->moveToThread(controllerThread);
    
    // MW2.2: Removed mouse dial control system
    connect(controllerThread, &QThread::started, controllerManager, &SDLControllerManager::start);
    connect(controllerThread, &QThread::finished, controllerManager, &SDLControllerManager::deleteLater);

    controllerThread->start();

    // toggleFullscreen(); // ✅ Toggle fullscreen to adjust layout

    
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
    
    // MW5.8: Keyboard detection and auto-hide scrollbars
    panXSlider->setMouseTracking(true);
    panYSlider->setMouseTracking(true);
    
    // Detect keyboard and set initial visibility
    m_hasKeyboard = hasPhysicalKeyboard();
    scrollbarsVisible = m_hasKeyboard;
    panXSlider->setVisible(scrollbarsVisible);
    panYSlider->setVisible(scrollbarsVisible);
    
    // Create timer for auto-hiding (3 seconds of inactivity)
    scrollbarHideTimer = new QTimer(this);
    scrollbarHideTimer->setSingleShot(true);
    scrollbarHideTimer->setInterval(3000);  // 3 seconds
    connect(scrollbarHideTimer, &QTimer::timeout, this, &MainWindow::hideScrollbars);
    
    // Trackpad mode timer: maintains trackpad state across rapid events
    trackpadModeTimer = new QTimer(this);
    trackpadModeTimer->setSingleShot(true);
    trackpadModeTimer->setInterval(350);
    connect(trackpadModeTimer, &QTimer::timeout, this, [this]() {
        trackpadModeActive = false;
    });
    

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
    
    QAction *openControlPanelAction = overflowMenu->addAction(tr("Settings"));
    connect(openControlPanelAction, &QAction::triggered, this, [this]() {
        // REMOVED: openControlPanelButton removed - use direct action
        QMessageBox::information(this, tr("Control Panel"), tr("Control Panel is being redesigned. Coming soon!"));
    });
    
    // MW7.8: overflowMenuButton deleted - menu now shown via NavigationBar menuRequested signal



    // Create a container for the viewport stack and scrollbars with relative positioning
    m_canvasContainer = new QWidget;
    QWidget *canvasContainer = m_canvasContainer;  // Local alias for existing code
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
        // REMOVED: updateToolButtonStates call removed - tool button state functionality deleted
        qDebug() << "Toolbar: Tool selected:" << static_cast<int>(tool);
    });
    connect(m_toolbar, &Toolbar::straightLineToggled, this, [this](bool enabled) {
        // Straight line mode toggle
        if (DocumentViewport* vp = currentViewport()) {
            vp->setStraightLineMode(enabled);
        }
        qDebug() << "Toolbar: Straight line mode" << (enabled ? "enabled" : "disabled");
    });
    connect(m_toolbar, &Toolbar::objectInsertClicked, this, [this]() {
        // Stub - will show object insert menu in future
        qDebug() << "Toolbar: Object insert clicked (stub)";
    });
    // Note: m_textButton now emits toolSelected(ToolType::Highlighter) directly
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
    
    // Phase D: Setup subtoolbars
    setupSubToolbars();

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
        // REMOVED: updateTabSizes call removed - tab sizing functionality deleted
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
    connect(openPdfShortcut, &QShortcut::activated, this, [this]() { openPdfDocument(); });
    
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
    
    // Use static cleanup method for consistent cleanup
    cleanupSharedResources();
}

// MW1.5: Kept as stubs - still called from many places
void MainWindow::switchPage(int pageIndex) {
    // Phase S4: Main page switching function - everything goes through here
    // pageIndex is 0-based internally
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    vp->scrollToPage(pageIndex);
    // pageInput update is handled by currentPageChanged signal connection
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
    // Phase D: Disconnect auto-highlight sync connection
    if (m_autoHighlightConn) {
        disconnect(m_autoHighlightConn);
        m_autoHighlightConn = {};
    }
    // Phase D: Disconnect object mode sync connections
    if (m_insertModeConn) {
        disconnect(m_insertModeConn);
        m_insertModeConn = {};
    }
    if (m_actionModeConn) {
        disconnect(m_actionModeConn);
        m_actionModeConn = {};
    }
    if (m_selectionChangedConn) {
        disconnect(m_selectionChangedConn);
        m_selectionChangedConn = {};
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
    
    // MW5.8: Connect scroll signals - show scrollbars on scroll, with auto-hide
    m_hScrollConn = connect(viewport, &DocumentViewport::horizontalScrollChanged, this, [this](qreal fraction) {
        showScrollbars();  // MW5.8: Show on scroll activity
        if (panXSlider) {
            panXSlider->blockSignals(true);
            panXSlider->setValue(qRound(fraction * 10000));
            panXSlider->blockSignals(false);
        }
    });
    
    m_vScrollConn = connect(viewport, &DocumentViewport::verticalScrollChanged, this, [this](qreal fraction) {
        showScrollbars();  // MW5.8: Show on scroll activity
        if (panYSlider) {
            panYSlider->blockSignals(true);
            panYSlider->setValue(qRound(fraction * 10000));
            panYSlider->blockSignals(false);
        }
    });
    
    // CR-2B: Connect tool/mode signals for keyboard shortcut sync
    m_toolChangedConn = connect(viewport, &DocumentViewport::toolChanged, this, [this](ToolType) {
        // REMOVED: updateToolButtonStates call removed - tool button state functionality deleted
        // REMOVED: updateThicknessSliderForCurrentTool removed - thicknessSlider deleted
    // updateThicknessSliderForCurrentTool();
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
    });
    
    // Phase D: Connect straight line mode sync (viewport → toolbar)
    // When straight line mode changes (e.g., auto-disabled when switching to Eraser/Lasso),
    // update the toolbar toggle button to match
    m_straightLineModeConn = connect(viewport, &DocumentViewport::straightLineModeChanged, 
                                     this, [this](bool enabled) {
        if (m_toolbar) {
            m_toolbar->setStraightLineMode(enabled);
        }
    });
    
    // Also sync the current straight line mode to the toolbar
    if (m_toolbar) {
        m_toolbar->setStraightLineMode(viewport->straightLineMode());
    }
    
    // Phase D: Connect auto-highlight state sync (viewport → subtoolbar)
    // When Ctrl+H changes the state, update the subtoolbar toggle to match
    m_autoHighlightConn = connect(viewport, &DocumentViewport::autoHighlightEnabledChanged, 
                                  this, [this](bool enabled) {
        if (m_highlighterSubToolbar) {
            m_highlighterSubToolbar->setAutoHighlightState(enabled);
        }
    });
    
    // Also sync the current auto-highlight state to the subtoolbar
    if (m_highlighterSubToolbar) {
        m_highlighterSubToolbar->setAutoHighlightState(viewport->isAutoHighlightEnabled());
    }
    
    // Phase D: Connect object mode state sync (viewport → subtoolbar)
    // When Ctrl+< / Ctrl+> / Ctrl+6 / Ctrl+7 changes the mode, update the subtoolbar
    m_insertModeConn = connect(viewport, &DocumentViewport::objectInsertModeChanged,
                               this, [this](DocumentViewport::ObjectInsertMode mode) {
        if (m_objectSelectSubToolbar) {
            m_objectSelectSubToolbar->setInsertModeState(mode);
        }
    });
    
    m_actionModeConn = connect(viewport, &DocumentViewport::objectActionModeChanged,
                               this, [this](DocumentViewport::ObjectActionMode mode) {
        if (m_objectSelectSubToolbar) {
            m_objectSelectSubToolbar->setActionModeState(mode);
        }
    });
    
    // Also sync the current object modes to the subtoolbar
    if (m_objectSelectSubToolbar) {
        m_objectSelectSubToolbar->setInsertModeState(viewport->objectInsertMode());
        m_objectSelectSubToolbar->setActionModeState(viewport->objectActionMode());
    }
    
    // Phase D: Connect object selection changed to update LinkSlot buttons
    m_selectionChangedConn = connect(viewport, &DocumentViewport::objectSelectionChanged,
                                     this, [this, viewport]() {
        updateLinkSlotButtons(viewport);
    });
    
    // Also sync the current selection state to the subtoolbar
    updateLinkSlotButtons(viewport);
}

void MainWindow::updateLinkSlotButtons(DocumentViewport* viewport)
{
    // Phase D: Update ObjectSelectSubToolbar slot buttons based on selected LinkObject
    if (!m_objectSelectSubToolbar || !viewport) {
        return;
    }
    
    const auto& selectedObjects = viewport->selectedObjects();
    
    // Check if exactly one LinkObject is selected
    if (selectedObjects.size() == 1) {
        LinkObject* link = dynamic_cast<LinkObject*>(selectedObjects.first());
        if (link) {
            // Convert LinkSlot::Type to LinkSlotState for each slot
            LinkSlotState states[3];
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                switch (link->linkSlots[i].type) {
                    case LinkSlot::Type::Empty:
                        states[i] = LinkSlotState::Empty;
                        break;
                    case LinkSlot::Type::Position:
                        states[i] = LinkSlotState::Position;
                        break;
                    case LinkSlot::Type::Url:
                        states[i] = LinkSlotState::Url;
                        break;
                    case LinkSlot::Type::Markdown:
                        states[i] = LinkSlotState::Markdown;
                        break;
                }
            }
            m_objectSelectSubToolbar->updateSlotStates(states);
            return;
        }
    }
    
    // No LinkObject selected (or multiple objects selected) - clear slots
    m_objectSelectSubToolbar->clearSlotStates();
}

void MainWindow::applySubToolbarValuesToViewport(ToolType tool)
{
    // Phase D: Apply subtoolbar's current preset values to the viewport (via signals)
    // This is used when the current tool changes and we want to emit signals
    // For new viewports, use applyAllSubToolbarValuesToViewport() instead
    
    switch (tool) {
        case ToolType::Pen:
            if (m_penSubToolbar) {
                m_penSubToolbar->emitCurrentValues();
            }
            break;
        case ToolType::Marker:
            if (m_markerSubToolbar) {
                m_markerSubToolbar->emitCurrentValues();
            }
            break;
        case ToolType::Highlighter:
            if (m_highlighterSubToolbar) {
                m_highlighterSubToolbar->emitCurrentValues();
            }
            break;
        default:
            // Other tools don't have color/thickness presets
            break;
    }
}

void MainWindow::applyAllSubToolbarValuesToViewport(DocumentViewport* viewport)
{
    // Phase D: Apply ALL subtoolbar preset values DIRECTLY to a specific viewport
    // This is called when a new tab is created or when switching tabs
    // It bypasses signals and applies values directly to avoid timing issues
    
    if (!viewport) {
        return;
    }
    
    // Apply pen settings
    if (m_penSubToolbar) {
        viewport->setPenColor(m_penSubToolbar->currentColor());
        viewport->setPenThickness(m_penSubToolbar->currentThickness());
    }
    
    // Apply marker settings (marker and highlighter share colors)
    if (m_markerSubToolbar) {
        viewport->setMarkerColor(m_markerSubToolbar->currentColor());
        viewport->setMarkerThickness(m_markerSubToolbar->currentThickness());
    }
    
    // Note: Highlighter uses marker color, so no separate setter needed
    // (HighlighterSubToolbar shares color presets with MarkerSubToolbar)
    
    qDebug() << "Applied all subtoolbar values to viewport:"
             << "penColor=" << (m_penSubToolbar ? m_penSubToolbar->currentColor().name() : "N/A")
             << "penThickness=" << (m_penSubToolbar ? m_penSubToolbar->currentThickness() : 0)
             << "markerColor=" << (m_markerSubToolbar ? m_markerSubToolbar->currentColor().name() : "N/A")
             << "markerThickness=" << (m_markerSubToolbar ? m_markerSubToolbar->currentThickness() : 0);
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

void MainWindow::openPdfDocument(const QString &filePath)
{
    // Phase doc-1.4: Open PDF file and create PDF-backed document
    // Uses DocumentManager for proper document ownership

    if (!m_documentManager || !m_tabManager) {
        qWarning() << "openPdfDocument: DocumentManager or TabManager not initialized";
        return;
    }

    QString pdfPath = filePath;

    // If no file path provided, open file dialog for PDF selection
    if (pdfPath.isEmpty()) {
        QString filter = tr("PDF Files (*.pdf);;All Files (*)");
        pdfPath = QFileDialog::getOpenFileName(
            this,
            tr("Open PDF"),
            QDir::homePath(),
            filter
        );

        if (pdfPath.isEmpty()) {
            // User cancelled
            return;
        }
    }
    
    // Use DocumentManager to load the PDF
    // DocumentManager::loadDocument() handles .pdf extension:
    // - Calls Document::createForPdf(baseName, path)
    // - Takes ownership of the document
    // - Adds to recent documents
    Document* doc = m_documentManager->loadDocument(pdfPath);
    if (!doc) {
        QMessageBox::critical(this, tr("PDF Error"),
            tr("Failed to open PDF file:\n%1").arg(pdfPath));
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
    
    // Apply default background settings (hardcoded defaults)
    {
        Page::BackgroundType defaultStyle = Page::BackgroundType::Grid;
        QColor defaultBgColor = Qt::white;
        QColor defaultGridColor = QColor(200, 200, 200);
        int defaultDensity = 30;
        
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
    
    // Apply default background settings (hardcoded defaults)
    {
        Page::BackgroundType defaultStyle = Page::BackgroundType::Grid;
        QColor defaultBgColor = Qt::white;
        QColor defaultGridColor = QColor(200, 200, 200);
        int defaultDensity = 30;
        
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



void MainWindow::removeTabAt(int index) {
    // Phase 3.1.2: Use TabManager to remove tabs
    // Note: Document cleanup happens via tabCloseRequested signal handler (ML-1 fix)
    if (m_tabManager) {
        m_tabManager->closeTab(index);
    }
}

// Phase 3.1.4: New accessor for DocumentViewport
DocumentViewport* MainWindow::currentViewport() const {
    if (m_tabManager) {
        return m_tabManager->currentViewport();
    }
    return nullptr;
}


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
            switchPage(newPage);
        } else {
            switchPage(newPage); // Same page, no direction needed
        }
        if (pageInput) {
        pageInput->setValue(newPage);
        }
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

    // MW5.8: Handle scrollbar visibility with auto-hide
    if (obj == panXSlider || obj == panYSlider) {
        if (event->type() == QEvent::Enter) {
            // Mouse entered scrollbar area - keep visible
            showScrollbars();
            if (scrollbarHideTimer && scrollbarHideTimer->isActive()) {
                scrollbarHideTimer->stop();  // Don't hide while hovering
            }
            return false;
        } 
        else if (event->type() == QEvent::Leave) {
            // Mouse left scrollbar area - start hide timer
            if (scrollbarHideTimer && scrollbarsVisible) {
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
        // ========================================================================
        // Wheel event handling
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
    
    // Phase D: Update SubToolbarContainer theme for icon switching
    if (m_subtoolbarContainer) {
        m_subtoolbarContainer->setDarkMode(darkMode);
    }
}
    
void MainWindow::saveThemeSettings() {
    QSettings settings("SpeedyNote", "App");
    settings.setValue("useCustomAccentColor", useCustomAccentColor);
    if (customAccentColor.isValid()) {
        settings.setValue("customAccentColor", customAccentColor.name());
    }
}

void MainWindow::loadThemeSettings() {
    QSettings settings("SpeedyNote", "App");
    useCustomAccentColor = settings.value("useCustomAccentColor", false).toBool();
    QString colorName = settings.value("customAccentColor", "#0078D4").toString();
    customAccentColor = QColor(colorName);
    
    // Ensure valid values
    if (!customAccentColor.isValid()) {
        customAccentColor = QColor("#0078D4"); // Default blue
    }
    
    // Apply theme immediately after loading
    updateTheme();
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

void MainWindow::loadUserSettings() {
    QSettings settings("SpeedyNote", "App");

    // Load touch gesture mode (default to Full for backwards compatibility)
    int savedMode = settings.value("touchGestureMode", static_cast<int>(TouchGestureMode::Full)).toInt();
    touchGestureMode = static_cast<TouchGestureMode>(savedMode);
    setTouchGestureMode(touchGestureMode);
    
    // Load theme settings
    loadThemeSettings();
}

void MainWindow::wheelEvent(QWheelEvent *event) {
    // MW2.2: Forward to base class - dial wheel handling removed
    QMainWindow::wheelEvent(event);
}

// ==================== MW5.8: Pan Slider Management ====================

bool MainWindow::hasPhysicalKeyboard() {
    // Check if any keyboard device is connected using Qt 6.4+ API
    // On desktop systems, this typically returns true
    // On tablets without attached keyboards, this may return false
    const auto devices = QInputDevice::devices();
    for (const QInputDevice *device : devices) {
        if (device->type() == QInputDevice::DeviceType::Keyboard) {
            return true;
        }
    }
    return false;
}

void MainWindow::showScrollbars() {
    // Only show if keyboard is connected
    if (!m_hasKeyboard) {
        m_hasKeyboard = hasPhysicalKeyboard();  // Re-check in case keyboard was plugged in
        if (!m_hasKeyboard) return;
    }
    
    if (!scrollbarsVisible) {
        scrollbarsVisible = true;
        if (panXSlider) panXSlider->setVisible(true);
        if (panYSlider) panYSlider->setVisible(true);
        updateScrollbarPositions();
    }
    
    // Reset the hide timer
    if (scrollbarHideTimer) {
        scrollbarHideTimer->stop();
        scrollbarHideTimer->start();
    }
}

void MainWindow::hideScrollbars() {
    if (scrollbarsVisible) {
        scrollbarsVisible = false;
        if (panXSlider) panXSlider->setVisible(false);
        if (panYSlider) panYSlider->setVisible(false);
    }
}

void MainWindow::updateScrollbarPositions() {
    // MW5.8: Position sliders relative to Toolbar and LeftSidebarContainer
    QWidget *container = m_viewportStack ? m_viewportStack->parentWidget() : nullptr;
    if (!container || !panXSlider || !panYSlider || !m_viewportStack) return;
    
    // Don't position if not visible
    if (!scrollbarsVisible) return;
    
    // Add small margins for better visibility
    const int margin = 3;
    
    // Get scrollbar dimensions - use fixed values since setFixedHeight/Width was called
    const int scrollbarWidth = 16;  // panYSlider fixed width
    const int scrollbarHeight = 16; // panXSlider fixed height
    
    // Calculate container dimensions
    int containerWidth = container->width();
    int containerHeight = container->height();
    
    // Calculate left offset based on LeftSidebarContainer
    int leftOffset = 0;
    if (m_leftSidebar && m_leftSidebar->isVisible()) {
        leftOffset = m_leftSidebar->width();
    }
    
    // Leave a bit of space for the corner
    int cornerOffset = 15;
    
    // Position horizontal scrollbar at top (below Toolbar, which is handled by layout)
    // Pan X: Full width of viewport area, starting after left sidebar
    panXSlider->setGeometry(
        leftOffset + cornerOffset + margin,  // After left sidebar + corner space
        margin,  // At top of container (Toolbar is above in main layout)
        containerWidth - leftOffset - cornerOffset - margin*2,  // Width minus sidebar and margins
        scrollbarHeight
    );
    
    // Position vertical scrollbar at left (to the right of LeftSidebarContainer)
    // Pan Y: On the LEFT side to avoid arm/wrist interference
    panYSlider->setGeometry(
        leftOffset + margin,  // Right after left sidebar
        cornerOffset + margin,  // Below corner offset
        scrollbarWidth,
        containerHeight - cornerOffset - margin*2  // Full height minus corners
    );
    
    // Ensure sliders are raised above content
    panXSlider->raise();
    panYSlider->raise();
    
    // Phase D: Also update subtoolbar position
    updateSubToolbarPosition();
}

// =========================================================================
// Phase D: Subtoolbar Setup and Positioning
// =========================================================================

void MainWindow::setupSubToolbars()
{
    if (!m_canvasContainer) {
        qWarning() << "setupSubToolbars: canvasContainer not yet created";
        return;
    }
    
    // Create subtoolbar container as child of canvas container (floats over viewport)
    m_subtoolbarContainer = new SubToolbarContainer(m_canvasContainer);
    
    // Create individual subtoolbars
    m_penSubToolbar = new PenSubToolbar();
    m_markerSubToolbar = new MarkerSubToolbar();
    m_highlighterSubToolbar = new HighlighterSubToolbar();
    m_objectSelectSubToolbar = new ObjectSelectSubToolbar();
    
    // Register subtoolbars with container
    m_subtoolbarContainer->setSubToolbar(ToolType::Pen, m_penSubToolbar);
    m_subtoolbarContainer->setSubToolbar(ToolType::Marker, m_markerSubToolbar);
    m_subtoolbarContainer->setSubToolbar(ToolType::Highlighter, m_highlighterSubToolbar);
    m_subtoolbarContainer->setSubToolbar(ToolType::ObjectSelect, m_objectSelectSubToolbar);
    // Eraser, Lasso - no subtoolbar (nullptr by default)
    
    // Connect tool changes from Toolbar to SubToolbarContainer
    connect(m_toolbar, &Toolbar::toolSelected, 
            m_subtoolbarContainer, &SubToolbarContainer::onToolChanged);
    
    // Connect PenSubToolbar signals to viewport
    connect(m_penSubToolbar, &PenSubToolbar::penColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setPenColor(color);
        }
    });
    connect(m_penSubToolbar, &PenSubToolbar::penThicknessChanged, this, [this](qreal thickness) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setPenThickness(thickness);
        }
    });
    
    // Connect MarkerSubToolbar signals to viewport
    connect(m_markerSubToolbar, &MarkerSubToolbar::markerColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setMarkerColor(color);
        }
    });
    connect(m_markerSubToolbar, &MarkerSubToolbar::markerThicknessChanged, this, [this](qreal thickness) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setMarkerThickness(thickness);
        }
    });
    
    // Connect HighlighterSubToolbar signals to viewport
    connect(m_highlighterSubToolbar, &HighlighterSubToolbar::highlighterColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) {
            // Highlighter uses marker color for now (shared presets)
            vp->setMarkerColor(color);
        }
    });
    connect(m_highlighterSubToolbar, &HighlighterSubToolbar::autoHighlightChanged, this, [this](bool enabled) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setAutoHighlightEnabled(enabled);
        }
    });
    
    // Connect ObjectSelectSubToolbar signals to viewport
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::insertModeChanged, this, 
            [this](DocumentViewport::ObjectInsertMode mode) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setObjectInsertMode(mode);
        }
    });
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::actionModeChanged, this, 
            [this](DocumentViewport::ObjectActionMode mode) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->setObjectActionMode(mode);
        }
    });
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::slotActivated, this, [this](int index) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->activateLinkSlot(index);
        }
    });
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::slotCleared, this, [this](int index) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->clearLinkSlot(index);
        }
    });
    
    // Connect tab changes to subtoolbar container and toolbar
    // Handles per-tab state for both toolbar tool selection and subtoolbar presets
    connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
        int newIndex = m_tabManager->currentIndex();
        
        if (newIndex != m_previousTabIndex) {
            // Update subtoolbar per-tab state (save old, restore new)
            m_subtoolbarContainer->onTabChanged(newIndex, m_previousTabIndex);
            
            // Sync toolbar and subtoolbar to the new viewport's current tool
            if (vp) {
                ToolType currentTool = vp->currentTool();
                
                // Update toolbar button selection (without emitting signals)
                m_toolbar->setCurrentTool(currentTool);
                
                // Update subtoolbar to show the correct one for this tool
                m_subtoolbarContainer->showForTool(currentTool);
                
                // Apply ALL subtoolbar preset values DIRECTLY to the new viewport
                // This ensures the viewport's colors/thicknesses match what's selected in UI
                // Uses direct setter calls to avoid timing issues with signals
                applyAllSubToolbarValuesToViewport(vp);
            }
            
            m_previousTabIndex = newIndex;
            
            qDebug() << "Tab changed: index" << newIndex 
                     << "tool" << (vp ? static_cast<int>(vp->currentTool()) : -1);
        }
    });
    
    // Initial position update
    QTimer::singleShot(0, this, &MainWindow::updateSubToolbarPosition);
    
    // Show for default tool (Pen)
    m_subtoolbarContainer->showForTool(ToolType::Pen);
    
    // Apply initial preset values to first viewport on startup
    // Use QTimer to ensure the first tab is fully created
    QTimer::singleShot(0, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            applyAllSubToolbarValuesToViewport(vp);
        }
    });
    
    qDebug() << "Phase D: Subtoolbars initialized";
}

void MainWindow::updateSubToolbarPosition()
{
    if (!m_subtoolbarContainer || !m_canvasContainer) {
        return;
    }
    
    // Get canvas container geometry (the viewport area)
    QRect viewportRect = m_canvasContainer->rect();
    
    // Account for left sidebar if visible
    int leftOffset = 0;
    if (m_leftSidebar && m_leftSidebar->isVisible()) {
        leftOffset = m_leftSidebar->width();
    }
    
    // Adjust viewport rect to exclude left sidebar area
    viewportRect.setLeft(viewportRect.left() + leftOffset);
    
    // Update subtoolbar container position
    m_subtoolbarContainer->updatePosition(viewportRect);
    
    // Ensure it's raised above viewport content
    m_subtoolbarContainer->raise();
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
    
    // REMOVED MW7.6: Keyboard mapping system deleted - pass all events to parent
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    // Phase 3.1.8: Ctrl tracking for trackpad zoom stubbed
    // TODO Phase 3.3: Track Ctrl state in DocumentViewport if needed
    
    QMainWindow::keyReleaseEvent(event);
}



QString MainWindow::elideTabText(const QString &text, int maxWidth) {
    // Create a font metrics object using the default font
    QFontMetrics fontMetrics(QApplication::font());
    
    // Elide the text from the right (showing the beginning)
    return fontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
}


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



// MW2.2: reconnectControllerSignals simplified - dial system removed
void MainWindow::reconnectControllerSignals() {
    if (!controllerManager) {
        return;
    }
    
    // Disconnect all existing connections to avoid duplicates
    disconnect(controllerManager, nullptr, this, nullptr);
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
// REMOVED MW7.7: openPdfFile stub removed - replaced with openPdfDocument(filePath)

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
        openPdfDocument(filePath);
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
