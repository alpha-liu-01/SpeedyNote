#include "MainWindow.h"

#include "core/DocumentViewport.h"  // Phase 3.1: New viewport architecture
#include "core/Document.h"          // Phase 3.1: Document class
#include "core/Page.h"              // Phase P.4.6: For thumbnail rendering
#include "layers/VectorLayer.h"     // Phase P.4.6: For thumbnail rendering
#include <QPainter>                 // Phase P.4.6: For thumbnail rendering
#include "ui/sidebars/LayerPanel.h" // Phase S1: Moved to sidebars folder
#include "ui/sidebars/OutlinePanel.h" // Phase E.2: PDF outline panel
#include "ui/sidebars/LeftSidebarContainer.h" // Phase S3: Left sidebar container
#include "ui/sidebars/PagePanel.h" // Page Panel: Task 5.1
#include "ui/DebugOverlay.h"        // Debug overlay (toggle with D key)
#include "ui/StyleLoader.h"         // QSS stylesheet loader
// Phase D: Subtoolbar includes
#include "ui/subtoolbars/SubToolbarContainer.h"
#include "ui/subtoolbars/PenSubToolbar.h"
#include "ui/subtoolbars/MarkerSubToolbar.h"
#include "ui/subtoolbars/HighlighterSubToolbar.h"
#include "ui/subtoolbars/ObjectSelectSubToolbar.h"
#include "ui/actionbars/ActionBarContainer.h"
#include "ui/actionbars/LassoActionBar.h"
#include "ui/actionbars/ObjectSelectActionBar.h"
#include "ui/actionbars/TextSelectionActionBar.h"
#include "ui/actionbars/ClipboardActionBar.h"
#include "ui/actionbars/PagePanelActionBar.h"
#include "objects/LinkObject.h"  // For LinkSlot slot state access
#include "core/MarkdownNote.h"   // Phase M.3: For loading markdown notes
#include "core/NotebookLibrary.h" // Phase P.4.6: For saving thumbnails
#include "pdf/PdfRelinkDialog.h" // Phase R.4: For PDF relink dialog
#include "sharing/NotebookExporter.h" // Phase 1: Export notebooks as .snbx
#include "sharing/ExportDialog.h"     // Phase 1: Export dialog UI
#include "ui/widgets/PdfSearchBar.h"  // PDF text search bar
#include "pdf/PdfSearchEngine.h"      // PDF text search engine
#include "ui/dialogs/PdfExportDialog.h"  // Phase 8: PDF export dialog
#include "pdf/MuPdfExporter.h"           // Phase 8: PDF export engine
#include <QClipboard>  // For clipboard signal connection
#include <algorithm>   // Phase M.4: For std::sort in searchMarkdownNotes
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QApplication>
#ifdef Q_OS_WIN
#include <windows.h>  // For MSG struct in nativeEvent (theme change detection)
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
#include <QInputDialog>
#include <QStandardPaths>
#include <QRegularExpression>  // BUG-A002: For filename sanitization on Android
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QInputMethod>
#include <QPropertyAnimation>  // Phase P.4.5: Smooth window transitions
#include <QInputMethodEvent>
#include <QSet>
#include <QWheelEvent>
#include <QTimer>
#include <QShortcut>  // Phase doc-1: Application-wide keyboard shortcuts
#include "core/ShortcutManager.h"  // Keyboard shortcut hub
#include <QInputDevice>  // MW5.8: For keyboard detection
#include <QColorDialog>  // Phase 3.1.8: For custom color picker
#include <QProcess>
#include <QLocalSocket>  // For single-instance server communication
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>  // Phase doc-1: JSON serialization
#include <QThread>

// Android JNI support for PDF file picking (BUG-A003)
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QEventLoop>
#include "ui/dialogs/SaveDocumentDialog.h"  // BUG-A002: Touch-friendly save dialog

// ============================================================================
// Android PDF File Picker (BUG-A003)
// ============================================================================
// Uses shared PdfPickerAndroid utility (source/android/PdfPickerAndroid.cpp)
// which wraps PdfFileHelper.java for proper SAF permission handling.
// ============================================================================

#include "android/PdfPickerAndroid.h"

#endif // Q_OS_ANDROID
// #include "HandwritingLineEdit.h"
#include "ControlPanelDialog.h"  // Phase CP.1: Re-enabled with cleaned up tabs
#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
#include "SDLControllerManager.h"
#endif
// #include "LauncherWindow.h" // Phase 3.1: Disconnected - LauncherWindow will be re-linked later

#include "DocumentConverter.h" // Added for PowerPoint conversion

// Linux-specific includes for signal handling
#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Static member definition for single instance
#ifndef Q_OS_ANDROID
QSharedMemory *MainWindow::sharedMemory = nullptr;
#endif
// Phase 3.1: LauncherWindow disconnected - will be re-linked later
// LauncherWindow *MainWindow::sharedLauncher = nullptr;

// REMOVED Phase 3.1: Static flag for viewport architecture mode
// Always using new architecture now
// bool MainWindow::s_useNewViewport = false;

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// Linux-specific signal handler for cleanup (not used on Android)
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

    setWindowTitle(tr("SpeedyNote 1.0.2"));
    
    // Phase 3.1: Always using new DocumentViewport architecture

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Setup signal handlers for proper cleanup on Linux (not Android)
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
        // Phase 6.1: Hide PDF search bar when switching tabs to prevent stale state
        if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
            hidePdfSearchBar();
        }
        
        // Save/restore left sidebar tab selection per document tab
        // IMPORTANT: Must be FIRST, before updatePagePanelForViewport() which modifies sidebar tabs
        int newIndex = m_tabManager->currentIndex();
        if (m_leftSidebar && newIndex != m_previousTabIndex) {
            // Save current sidebar tab for previous document tab
            if (m_previousTabIndex >= 0) {
                m_sidebarTabStates[m_previousTabIndex] = m_leftSidebar->currentIndex();
            }
        }
        
        // Task 7.2: Save PagePanel scroll position for previous document tab
        // MUST be before updatePagePanelForViewport() which resets scroll via setDocument()
        if (m_pagePanel && m_previousTabIndex >= 0 && newIndex != m_previousTabIndex) {
            m_pagePanel->saveTabState(m_previousTabIndex);
        }
        
        // Phase 3.3: Connect scroll signals from current viewport
        connectViewportScrollSignals(vp);
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        
        // Sync viewport dark mode with current theme
        if (vp) {
            vp->setDarkMode(isDarkMode());
        }
        
        // Phase 5.1 Task 4: Update LayerPanel when tab changes
        updateLayerPanelForViewport(vp);
        
        // Phase E.2: Update OutlinePanel for current document
        if (vp) {
            updateOutlinePanelForDocument(vp->document());
        }
        
        // Page Panel: Task 5.1: Update PagePanel when tab changes
        updatePagePanelForViewport(vp);
        
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
        
        // Restore left sidebar tab selection for new document tab
        // IMPORTANT: Must be AFTER updatePagePanelForViewport() which modifies sidebar tabs
        if (m_leftSidebar && newIndex != m_previousTabIndex) {
            if (m_sidebarTabStates.contains(newIndex)) {
                m_leftSidebar->setCurrentIndex(m_sidebarTabStates[newIndex]);
            }
        }
        
        // Task 7.2: Restore PagePanel scroll position for new document tab
        // MUST be after updatePagePanelForViewport() which sets the new document
        if (m_pagePanel && newIndex != m_previousTabIndex) {
            m_pagePanel->restoreTabState(newIndex);
        }
    });

    // ML-1 FIX: Connect tabCloseRequested to clean up Document when tab closes
    // TabManager::closeTab() emits this signal before deleting the viewport
    connect(m_tabManager, &TabManager::tabCloseRequested, this, [this](int index, DocumentViewport* vp) {
        // Phase 6.2: Cancel search if the document being closed has an active search
        if (vp && m_searchEngine && vp == currentViewport()) {
            if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
                hidePdfSearchBar();  // This also cancels and clears the cache
            }
        }
        
        // Clean up subtoolbar per-tab state to prevent memory leak
        if (m_subtoolbarContainer) {
            m_subtoolbarContainer->clearTabState(index);
        }
        
        // Task 7.2: Clean up PagePanel scroll state for closed tab
        if (m_pagePanel) {
            m_pagePanel->clearTabState(index);
        }
        
        // Clean up sidebar tab state for closed tab
        m_sidebarTabStates.remove(index);
        
        if (vp && m_documentManager) {
            Document* doc = vp->document();
            if (doc) {
                // Phase P.4.6: Save page-0 thumbnail to NotebookLibrary before closing
                // Only for paged documents that have been saved (have a bundle path)
                QString bundlePath = m_documentManager->documentPath(doc);
                if (!bundlePath.isEmpty() && !doc->isEdgeless() && doc->pageCount() > 0) {
                    // Try to get cached thumbnail from PagePanel first
                    QPixmap thumbnail;
                    if (m_pagePanel && m_pagePanel->document() == doc) {
                        thumbnail = m_pagePanel->thumbnailForPage(0);
                    }
                    
                    // If no cached thumbnail, render one synchronously
                    if (thumbnail.isNull()) {
                        thumbnail = renderPage0Thumbnail(doc);
                    }
                    
                    // Save to NotebookLibrary
                    if (!thumbnail.isNull()) {
                        NotebookLibrary::instance()->saveThumbnail(bundlePath, thumbnail);
                    }
                }
                
                // CR-L8: Clear LayerPanel's document pointer BEFORE deleting Document
                // to prevent dangling pointer if any code accesses LayerPanel during cleanup
                if (m_layerPanel && m_layerPanel->edgelessDocument() == doc) {
                    m_layerPanel->setCurrentPage(nullptr);
                }
                
                // Phase P.4.6 FIX: Clear PagePanel's document pointer BEFORE deleting Document
                // This cancels any async thumbnail renders to prevent use-after-free.
                // ThumbnailRenderer::cancelAll() blocks until all active renders complete.
                if (m_pagePanel && m_pagePanel->document() == doc) {
                    m_pagePanel->setDocument(nullptr);
                }
                
                // MEMORY FIX: Clear viewport's document pointer BEFORE deleting Document
                // This triggers cleanup of PDF cache, undo stacks, and other document-related
                // data structures while the document is still valid. Also prevents any
                // dangling pointer access during viewport destruction.
                vp->setDocument(nullptr);
                
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
        
        // FEATURE-DOC-001: Update lastAccessedPage for paged documents
        // This ensures the page position is saved even if no other edits were made
        bool isUsingTemp = m_documentManager->isUsingTempBundle(doc);
        bool positionChanged = false;
        
        if (!doc->isEdgeless()) {
            int currentPage = vp->currentPageIndex();
            if (doc->lastAccessedPage != currentPage) {
                doc->lastAccessedPage = currentPage;
                positionChanged = true;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "tabCloseAttempted: lastAccessedPage changed to" << currentPage;
#endif
            }
        } else {
            // Phase 4: Sync edgeless position before closing tab
            vp->syncPositionToDocument();
            positionChanged = true;  // Always consider position changed for edgeless
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "tabCloseAttempted: Synced edgeless position";
#endif
        }
        
        // FEATURE-DOC-001: Auto-save if only position changed (no content changes)
        // This is a silent save - no prompt needed for just navigation
        if (positionChanged && !isUsingTemp && !doc->modified) {
            QString existingPath = m_documentManager->documentPath(doc);
            if (!existingPath.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "tabCloseAttempted: Auto-saving to persist position";
#endif
                m_documentManager->saveDocument(doc);
                // Don't show error dialog - this is a best-effort save for position only
            }
        }
        
        // Check if this document has unsaved changes
        bool needsSavePrompt = false;
        
        if (doc->isEdgeless()) {
            // Edgeless: check if it has tiles and is in temp bundle
            bool hasContent = doc->tileCount() > 0 || doc->tileIndexCount() > 0;
            needsSavePrompt = isUsingTemp && hasContent;
        } else {
            // Paged: check if modified OR (in temp bundle with pages)
            bool hasContent = doc->pageCount() > 0;
            needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
        }
        
        if (needsSavePrompt) {
            // Prompt user to save
            QString docType = doc->isEdgeless() ? tr("canvas") : tr("document");
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Save Changes?"),
                tr("This %1 has unsaved changes. Do you want to save before closing?").arg(docType),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save
            );
            
            if (reply == QMessageBox::Cancel) {
                // User cancelled - don't close
                return;
            }
            
            if (reply == QMessageBox::Save) {
                // Note: lastAccessedPage was already updated above (before needsSavePrompt check)
                
                // Check if document already has a permanent save path
                QString existingPath = m_documentManager->documentPath(doc);
                bool canSaveInPlace = !existingPath.isEmpty() && !isUsingTemp;
                
                if (canSaveInPlace) {
                    // Save in-place to existing location
                    if (!m_documentManager->saveDocument(doc)) {
                        QMessageBox::critical(this, tr("Save Error"),
                            tr("Failed to save document to:\n%1").arg(existingPath));
                        return;  // Don't close if save failed
                    }
                } else {
                    // New document - use Android-aware save dialog
                    if (!saveNewDocumentWithDialog(doc)) {
                        return;  // User cancelled or save failed - don't close
                    }
                }
                
                // Update tab title and NavigationBar
                m_tabManager->setTabTitle(index, doc->displayName());
                m_tabManager->markTabModified(index, false);
                if (m_navigationBar) {
                    m_navigationBar->setFilename(doc->displayName());
                }
            }
            // If Discard, fall through to close
        }
        
        // Close the tab
        m_tabManager->closeTab(index);
    });
    // ===========================================================================
    
    setupUi();    // ✅ Move all UI setup here

#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
    controllerManager = new SDLControllerManager();
    controllerThread = new QThread(this);

    controllerManager->moveToThread(controllerThread);
    
    // MW2.2: Removed mouse dial control system
    connect(controllerThread, &QThread::started, controllerManager, &SDLControllerManager::start);
    connect(controllerThread, &QThread::finished, controllerManager, &SDLControllerManager::deleteLater);

    controllerThread->start();
#endif

    // toggleFullscreen(); // ✅ Toggle fullscreen to adjust layout

    
    loadUserSettings();


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
    
    // FIX: Start autohide timer if scrollbars are initially visible
    // Without this, scrollbars stay visible forever until user interacts with them
    if (scrollbarsVisible) {
        scrollbarHideTimer->start();
    }
    
    

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
    m_pagePanel = m_leftSidebar->pagePanel();    // Page Panel: Task 5.1
    
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
    
    // Phase M.3: Connect new signals for LinkObject-based markdown notes
    
    // Handle note content changes - save to file
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteContentSaved,
            this, [this](const QString& noteId, const QString& title, const QString& content) {
        DocumentViewport* vp = currentViewport();
        if (!vp || !vp->document()) return;
        
        QString notesDir = vp->document()->notesPath();
        if (notesDir.isEmpty()) return;
        
        QString filePath = notesDir + "/" + noteId + ".md";
        MarkdownNote note;
        note.id = noteId;
        note.title = title;
        note.content = content;
        note.saveToFile(filePath);
    });
    
    // Handle note deletion from sidebar - delete file and clear LinkSlot
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeletedWithLink,
            this, [this](const QString& noteId, const QString& linkObjectId) {
        DocumentViewport* vp = currentViewport();
        if (!vp || !vp->document()) return;
        
        Document* doc = vp->document();
        
        // Delete the note file
        doc->deleteNoteFile(noteId);
        
        // Find the LinkObject and clear the slot
        Page* page = doc->page(vp->currentPageIndex());
        if (page) {
            for (const auto& objPtr : page->objects) {
                LinkObject* link = dynamic_cast<LinkObject*>(objPtr.get());
                if (link && link->id == linkObjectId) {
                    for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                        if (link->linkSlots[i].type == LinkSlot::Type::Markdown &&
                            link->linkSlots[i].markdownNoteId == noteId) {
                            link->linkSlots[i].clear();
                            doc->markPageDirty(vp->currentPageIndex());
                            vp->update();
                            break;
                        }
                    }
                    break;
                }
            }
        }
        
        // Refresh sidebar
        markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
    });
    
    // Handle jump to LinkObject
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::linkObjectClicked,
            this, [this](const QString& linkObjectId) {
        navigateToLinkObject(linkObjectId);
    });
    
    // Phase M.4: Handle search requests
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::searchRequested,
            this, [this](const QString& query, int fromPage, int toPage) {
        QList<NoteDisplayData> results = searchMarkdownNotes(query, fromPage, toPage);
        markdownNotesSidebar->displaySearchResults(results);
    });
    
    // Connect reload request from sidebar (when exiting search mode)
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::reloadNotesRequested,
            this, [this]() {
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
        }
    });
    
    // Phase C.1.5: Removed old m_tabWidget configuration - now using m_tabBar + m_viewportStack
    // Corner widgets (launcher button, add tab button) are now in NavigationBar
    
    // Phase 3.1: Old tabBarContainer kept but hidden (for reference, will be removed later)
    tabBarContainer = new QWidget(this);
    tabBarContainer->setObjectName("tabBarContainer");
    tabBarContainer->setVisible(false);  // Hidden - using m_tabBar now


    overflowMenu = new QMenu(this);
    overflowMenu->setObjectName("overflowMenu");

    // Phase R.4: Relink PDF action (enabled only when document has PDF reference)
    m_relinkPdfAction = overflowMenu->addAction(tr("Relink PDF..."));
    m_relinkPdfAction->setEnabled(false);  // Initially disabled
    connect(m_relinkPdfAction, &QAction::triggered, this, [this]() {
        showPdfRelinkDialog(currentViewport());
    });
    
    // PDF Export action (Ctrl+P)
    m_exportPdfAction = overflowMenu->addAction(tr("Export to PDF..."));
    m_exportPdfAction->setShortcut(ShortcutManager::instance()->keySequenceForAction("file.export_pdf"));
    connect(m_exportPdfAction, &QAction::triggered, this, &MainWindow::showPdfExportDialog);
    
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
        // Phase CP.1: Open the cleaned-up Control Panel dialog
        ControlPanelDialog dialog(this, this);
        dialog.exec();
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
#ifdef SPEEDYNOTE_DEBUG
    m_debugOverlay->show();  // Show by default in debug builds
#else
    m_debugOverlay->hide();  // Hidden in release builds
#endif

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
    connect(m_navigationBar, &NavigationBar::launcherClicked, this, &MainWindow::toggleLauncher);
    connect(m_navigationBar, &NavigationBar::leftSidebarToggled, this, [this](bool checked) {
        // Phase S3: Toggle left sidebar container
        if (m_leftSidebar) {
            m_leftSidebar->setVisible(checked);
            // Phase P.4: Update action bar visibility when sidebar visibility changes
            updatePagePanelActionBarVisibility();
        }
    });
    connect(m_navigationBar, &NavigationBar::saveClicked, this, &MainWindow::saveDocument);
    connect(m_navigationBar, &NavigationBar::addClicked, this, [this]() {
        // Phase P.4.3: Show dropdown menu for new document options
        showAddMenu();
    });
    connect(m_navigationBar, &NavigationBar::filenameClicked, this, [this]() {
        // Toggle tab bar visibility
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "NavigationBar: Filename clicked - toggle tabs";
#endif
        if (m_tabBar) {
            m_tabBar->setVisible(!m_tabBar->isVisible());
        }
    });
    connect(m_navigationBar, &NavigationBar::fullscreenToggled, this, [this](bool checked) {
        Q_UNUSED(checked);
        toggleFullscreen();
    });
    connect(m_navigationBar, &NavigationBar::shareClicked, this, [this]() {
        // Phase 1: Export notebook as .snbx package
        DocumentViewport* vp = currentViewport();
        Document* doc = vp ? vp->document() : nullptr;
        if (!doc) {
            QMessageBox::warning(this, tr("Export Failed"), 
                tr("No document is currently open."));
            return;
        }
        
        // Ensure document is saved before exporting
        if (doc->bundlePath().isEmpty()) {
            QMessageBox::warning(this, tr("Export Failed"),
                tr("Please save the document before exporting."));
            return;
        }
        
        // Show export dialog
        ExportDialog dialog(doc, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        
#ifdef Q_OS_ANDROID
        // Android: Export to cache, then share via share sheet
        QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QString exportDir = cachePath + "/exports";
        QDir exportDirObj(exportDir);
        
        // Clean up old exports to prevent disk space leaks
        // (User may cancel share sheet or export multiple times)
        if (exportDirObj.exists()) {
            QStringList oldFiles = exportDirObj.entryList(QStringList() << "*.snbx", QDir::Files);
            for (const QString& oldFile : oldFiles) {
                exportDirObj.remove(oldFile);
            }
        } else {
            exportDirObj.mkpath(".");
        }
        
        // Sanitize filename for Android
        QString safeName = doc->name;
        safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        QString exportPath = exportDir + "/" + safeName + ".snbx";
        
        NotebookExporter::ExportOptions options;
        options.includePdf = dialog.includePdf();
        options.destPath = exportPath;
        
        auto result = NotebookExporter::exportPackage(doc, options);
        if (result.success) {
            // Call ShareHelper.shareFile via JNI
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            QJniObject::callStaticMethod<void>(
                "org/speedynote/app/ShareHelper",
                "shareFile",
                "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V",
                activity.object<jobject>(),
                QJniObject::fromString(exportPath).object<jstring>(),
                QJniObject::fromString("application/octet-stream").object<jstring>()
            );
        } else {
            QMessageBox::warning(this, tr("Export Failed"), result.errorMessage);
        }
#else
        // Desktop: Show save dialog
        QString defaultPath = QDir::homePath() + "/" + doc->name + ".snbx";
        QString destPath = QFileDialog::getSaveFileName(this,
            tr("Export Notebook"),
            defaultPath,
            tr("SpeedyNote Package (*.snbx)"));
        
        if (destPath.isEmpty()) {
            return;  // User cancelled
        }
        
        NotebookExporter::ExportOptions options;
        options.includePdf = dialog.includePdf();
        options.destPath = destPath;
        
        auto result = NotebookExporter::exportPackage(doc, options);
        if (result.success) {
            // Show success message with file size
            QString sizeStr;
            if (result.fileSize < 1024) {
                sizeStr = tr("%1 bytes").arg(result.fileSize);
            } else if (result.fileSize < 1024 * 1024) {
                sizeStr = tr("%1 KB").arg(result.fileSize / 1024);
            } else {
                double sizeMB = static_cast<double>(result.fileSize) / (1024.0 * 1024.0);
                sizeStr = tr("%1 MB").arg(sizeMB, 0, 'f', 1);
            }
            
            QMessageBox::information(this, tr("Export Complete"),
                tr("Notebook exported successfully.\n\nFile: %1\nSize: %2")
                    .arg(QFileInfo(destPath).fileName())
                    .arg(sizeStr));
        } else {
            QMessageBox::warning(this, tr("Export Failed"), result.errorMessage);
        }
#endif
    });
    connect(m_navigationBar, &NavigationBar::rightSidebarToggled, this, [this](bool checked) {
        // Toggle markdown notes sidebar
        if (markdownNotesSidebar) {
            markdownNotesSidebar->setVisible(checked);
            markdownNotesSidebarVisible = checked;
            
            // Load notes when sidebar becomes visible
            if (checked) {
                markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
            }
            
            // Force layout update and reposition action bar
            if (centralWidget() && centralWidget()->layout()) {
                centralWidget()->layout()->invalidate();
                centralWidget()->layout()->activate();
            }
            QApplication::processEvents();
            updateActionBarPosition();
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
        // qDebug() << "Toolbar: Tool selected:" << static_cast<int>(tool);
    });
    connect(m_toolbar, &Toolbar::straightLineToggled, this, [this](bool enabled) {
        // Straight line mode toggle
        if (DocumentViewport* vp = currentViewport()) {
            vp->setStraightLineMode(enabled);
        }
        // qDebug() << "Toolbar: Straight line mode" << (enabled ? "enabled" : "disabled");
    });
    connect(m_toolbar, &Toolbar::objectInsertClicked, this, [this]() {
        // Stub - will show object insert menu in future
        // qDebug() << "Toolbar: Object insert clicked (stub)";
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
        // Convert int to TouchGestureMode enum and apply
        TouchGestureMode gestureMode;
        switch (mode) {
            case 0: gestureMode = TouchGestureMode::Disabled; break;
            case 1: gestureMode = TouchGestureMode::YAxisOnly; break;
            case 2: gestureMode = TouchGestureMode::Full; break;
            default: gestureMode = TouchGestureMode::Full; break;
        }
        setTouchGestureMode(gestureMode);
        // qDebug() << "Toolbar: Touch gesture mode changed to" << mode;
    });
    // ------------------ End of Toolbar signal connections ------------------
    
    // Phase D: Setup subtoolbars
    setupSubToolbars();
    
    // Setup action bars
    setupActionBars();
    
    // PDF Search: Setup search bar
    setupPdfSearch();
    
    // Phase E.2: Setup outline panel connections
    setupOutlinePanelConnections();
    
    // Page Panel: Task 5.2: Setup page panel connections
    setupPagePanelConnections();

    // Add components in vertical order
    // Phase C.1.5: tabBarContainer hidden - buttons now in NavigationBar
    // mainLayout->addWidget(tabBarContainer);   // Old tab bar - now hidden
    // REMOVED MW5.1: controlBar layout removed - replaced by NavigationBar and Toolbar
    
    // Content area with sidebars and canvas
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    

    
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

    // NOTE: Do NOT call addNewTab() here!
    // When launched from Launcher, the FAB actions (createNewPaged, createNewEdgeless, etc.)
    // explicitly call the appropriate method to create a tab.
    // When launched with a file argument, openFileInNewTab() creates the tab.
    // Auto-creating a tab here would result in an unwanted extra tab.

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
        
        // Page Panel: Task 5.1: Initialize PagePanel for the first tab
        updatePagePanelForViewport(currentViewport());
        
        // Initialize DebugOverlay with the first viewport
        if (m_debugOverlay) {
            m_debugOverlay->setViewport(currentViewport());
        }
    });
    
    // =========================================================================
    // Keyboard Shortcut Hub: Setup managed shortcuts
    // All shortcuts now go through ShortcutManager for customization support
    // =========================================================================
    setupManagedShortcuts();
}

// ============================================================================
// Keyboard Shortcut Hub: Setup and Management
// ============================================================================

void MainWindow::setupManagedShortcuts()
{
    auto* sm = ShortcutManager::instance();
    
    // Helper lambda to create and register a managed shortcut
    auto createShortcut = [this, sm](const QString& actionId, 
                                      std::function<void()> callback,
                                      Qt::ShortcutContext context = Qt::ApplicationShortcut) {
        QKeySequence seq = sm->keySequenceForAction(actionId);
        QShortcut* shortcut = new QShortcut(seq, this);
        shortcut->setContext(context);
        connect(shortcut, &QShortcut::activated, this, callback);
        m_managedShortcuts.insert(actionId, shortcut);
    };
    
    // ===== File Operations =====
    createShortcut("file.save", [this]() { saveDocument(); });
    createShortcut("file.new_paged", [this]() { addNewTab(); });
    createShortcut("file.new_edgeless", [this]() { addNewEdgelessTab(); });
    createShortcut("file.open_pdf", [this]() { openPdfDocument(); });
    createShortcut("file.open_notebook", [this]() { loadFolderDocument(); });
    // file.close_tab - TODO: implement closeCurrentTab()
    // file.export - TODO: implement export action
    
    // ===== Document/Page Operations =====
    createShortcut("document.add_page", [this]() { addPageToDocument(); });
    createShortcut("document.insert_page", [this]() { insertPageInDocument(); });
    createShortcut("document.delete_page", [this]() { deletePageInDocument(); });
    
    // ===== Navigation =====
    createShortcut("navigation.launcher", [this]() { toggleLauncher(); });
    createShortcut("navigation.escape", [this]() {
        // Only process if no modal dialog is open
        if (QApplication::activeModalWidget()) {
            return;
        }
        
        // First, close PDF search bar if it's open
        if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
            hidePdfSearchBar();
            return;
        }
        
        // Next, let the current viewport try to handle Escape
        // (cancel lasso selection, deselect objects, cancel text selection)
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->handleEscapeKey()) {
                // Viewport handled Escape (cancelled something)
                return;
            }
        }
        
        // Nothing to cancel in viewport - toggle to launcher
            toggleLauncher();
    }, Qt::WindowShortcut);  // WindowShortcut for Escape
    createShortcut("navigation.go_to_page", [this]() { showJumpToPageDialog(); });
    // navigation.next_tab, navigation.prev_tab - TODO: implement tab switching
    // navigation.prev_page, navigation.next_page - handled in DocumentViewport
    
    // ===== View =====
    createShortcut("view.debug_overlay", [this]() { toggleDebugOverlay(); });
    createShortcut("view.auto_layout", [this]() { toggleAutoLayout(); });
    createShortcut("view.fullscreen", [this]() { toggleFullscreen(); });
    createShortcut("view.left_sidebar", [this]() {
        if (m_leftSidebar && m_navigationBar) {
            bool newState = !m_leftSidebar->isVisible();
            m_leftSidebar->setVisible(newState);
            m_navigationBar->setLeftSidebarChecked(newState);
        }
    });
    createShortcut("view.right_sidebar", [this]() {
        if (markdownNotesSidebar && m_navigationBar) {
            bool newState = !markdownNotesSidebar->isVisible();
            markdownNotesSidebar->setVisible(newState);
            markdownNotesSidebarVisible = newState;
            m_navigationBar->setRightSidebarChecked(newState);
        }
    });
    
    // ===== Application =====
    createShortcut("app.settings", [this]() { 
        // Show control panel dialog
        ControlPanelDialog dialog(this, this);
        dialog.exec();
    });
    createShortcut("app.keyboard_shortcuts", [this]() {
        // Show control panel dialog and switch to Keyboard Shortcuts tab
        ControlPanelDialog dialog(this, this);
        dialog.switchToKeyboardShortcutsTab();
        dialog.exec();
    });
    createShortcut("app.find", [this]() {
        // Show PDF search bar (only works for PDF documents)
        showPdfSearchBar();
    });
    createShortcut("app.find_next", [this]() {
        // F3: Find next (only works when search bar is visible)
        if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
            QString text = m_pdfSearchBar->searchText();
            if (!text.isEmpty()) {
                emit m_pdfSearchBar->searchNextRequested(text, m_pdfSearchBar->caseSensitive(), m_pdfSearchBar->wholeWord());
            }
        }
    });
    createShortcut("app.find_prev", [this]() {
        // Shift+F3: Find previous (only works when search bar is visible)
        if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
            QString text = m_pdfSearchBar->searchText();
            if (!text.isEmpty()) {
                emit m_pdfSearchBar->searchPrevRequested(text, m_pdfSearchBar->caseSensitive(), m_pdfSearchBar->wholeWord());
            }
        }
    });
    
    // ===== Export/Share =====
    createShortcut("file.export", [this]() {
        // Trigger the share/export action (same as NavigationBar share button)
        if (m_navigationBar) {
            emit m_navigationBar->shareClicked();
        }
    });
    createShortcut("file.export_pdf", [this]() {
        showPdfExportDialog();
    });
    
    // ===== Tools (delegated to viewport) =====
    // These need to check if text input is active before firing
    auto createToolShortcut = [this, sm](const QString& actionId, ToolType tool) {
        QKeySequence seq = sm->keySequenceForAction(actionId);
        QShortcut* shortcut = new QShortcut(seq, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, tool]() {
            // Skip if text input widget has focus (single-key shortcuts conflict with typing)
            QWidget* focused = QApplication::focusWidget();
            if (qobject_cast<QLineEdit*>(focused) ||
                qobject_cast<QTextEdit*>(focused) ||
                qobject_cast<QPlainTextEdit*>(focused)) {
                return;
            }
            
            if (DocumentViewport* vp = currentViewport()) {
                vp->setCurrentTool(tool);
            }
        });
        m_managedShortcuts.insert(actionId, shortcut);
    };
    
    createToolShortcut("tool.pen", ToolType::Pen);
    createToolShortcut("tool.eraser", ToolType::Eraser);
    createToolShortcut("tool.lasso", ToolType::Lasso);
    createToolShortcut("tool.highlighter", ToolType::Highlighter);
    createToolShortcut("tool.marker", ToolType::Marker);
    createToolShortcut("tool.object_select", ToolType::ObjectSelect);
    
    // ===== Edit (delegated to viewport) =====
    createShortcut("edit.undo", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->undo();
        }
    });
    createShortcut("edit.redo", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->redo();
        }
    });
    createShortcut("edit.redo_alt", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->redo();
        }
    });
    
    // ===== Home Key (context-dependent: edgeless origin OR first page) =====
    // Note: edgeless.home and navigation.first_page share the same "Home" key
    // We only create ONE QShortcut to avoid Qt ambiguity, and dispatch based on document type
    createShortcut("edgeless.home", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->document()) {
                if (vp->document()->isEdgeless()) {
                    vp->returnToOrigin();
                } else {
                    // Paged document: Home = first page
                    vp->scrollToPage(0);
                }
            }
        }
    });
    // Note: navigation.first_page is NOT created separately - handled by edgeless.home above
    
    createShortcut("edgeless.go_back", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->document() && vp->document()->isEdgeless()) {
                // Edgeless: Backspace navigates back in position history
                vp->goBackPosition();
            } else {
                // Paged: Backspace acts as delete (same as Delete key)
                vp->handleDeleteAction();
            }
        }
    });
    
    // ===== Page Navigation (paged documents only) =====
    createShortcut("navigation.prev_page", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->document() && !vp->document()->isEdgeless()) {
                int current = vp->currentPageIndex();
                if (current > 0) {
                    vp->scrollToPage(current - 1);
                }
            }
        }
    });
    createShortcut("navigation.next_page", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->document() && !vp->document()->isEdgeless()) {
                int current = vp->currentPageIndex();
                int lastPage = vp->document()->pageCount() - 1;
                if (current < lastPage) {
                    vp->scrollToPage(current + 1);
                }
            }
        }
    });
    // navigation.first_page is handled by edgeless.home (same "Home" key, context-dependent)
    
    createShortcut("navigation.last_page", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->document() && !vp->document()->isEdgeless()) {
                int lastPage = vp->document()->pageCount() - 1;
                vp->scrollToPage(lastPage);
            }
        }
    });
    
    // ===== Tab Navigation =====
    createShortcut("navigation.next_tab", [this]() {
        if (m_tabManager) {
            m_tabManager->switchToNextTab();
        }
    });
    createShortcut("navigation.prev_tab", [this]() {
        if (m_tabManager) {
            m_tabManager->switchToPrevTab();
        }
    });
    createShortcut("file.close_tab", [this]() {
        // Use tabCloseAttempted signal flow to properly handle unsaved changes
        if (m_tabManager && m_tabManager->tabCount() > 0) {
            int currentIndex = m_tabManager->currentIndex();
            DocumentViewport* vp = m_tabManager->currentViewport();
            if (vp) {
                emit m_tabManager->tabCloseAttempted(currentIndex, vp);
            }
        }
    });
    
    // ===== Zoom Shortcuts =====
    createShortcut("zoom.in", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomIn();
        }
    });
    createShortcut("zoom.in_alt", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomIn();
        }
    });
    createShortcut("zoom.out", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomOut();
        }
    });
    createShortcut("zoom.fit", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomToFit();
        }
    });
    createShortcut("zoom.100", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomToActualSize();
        }
    });
    createShortcut("zoom.fit_width", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->zoomToWidth();
        }
    });
    
    // ===== Layer Operations =====
    createShortcut("layer.new", [this]() {
        if (m_layerPanel) {
            m_layerPanel->addNewLayerAction();
        }
    });
    createShortcut("layer.toggle_visibility", [this]() {
        if (m_layerPanel) {
            m_layerPanel->toggleActiveLayerVisibility();
        }
    });
    createShortcut("layer.select_all", [this]() {
        if (m_layerPanel) {
            m_layerPanel->toggleSelectAllLayers();
        }
    });
    createShortcut("layer.select_top", [this]() {
        if (m_layerPanel) {
            m_layerPanel->selectTopLayer();
        }
    });
    createShortcut("layer.select_bottom", [this]() {
        if (m_layerPanel) {
            m_layerPanel->selectBottomLayer();
        }
    });
    createShortcut("layer.merge", [this]() {
        if (m_layerPanel) {
            m_layerPanel->mergeSelectedLayers();
        }
    });
    
    // ===== Context-Dependent Edit Operations (delegated to viewport) =====
    // These behave differently based on current tool and selection
    createShortcut("edit.copy", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->handleCopyAction();
        }
    });
    createShortcut("edit.cut", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->handleCutAction();
        }
    });
    createShortcut("edit.paste", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->handlePasteAction();
        }
    });
    createShortcut("edit.delete", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->handleDeleteAction();
        }
    });
    
    // ===== Object Manipulation (delegated to viewport, ObjectSelect tool) =====
    // Z-Order
    createShortcut("object.bring_front", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->bringSelectedToFront();
            }
        }
    });
    createShortcut("object.bring_forward", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->bringSelectedForward();
            }
        }
    });
    createShortcut("object.send_backward", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->sendSelectedBackward();
            }
        }
    });
    createShortcut("object.send_back", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->sendSelectedToBack();
            }
        }
    });
    
    // Affinity
    createShortcut("object.affinity_up", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->increaseSelectedAffinity();
            }
        }
    });
    createShortcut("object.affinity_down", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->decreaseSelectedAffinity();
            }
        }
    });
    createShortcut("object.affinity_background", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect && vp->hasSelectedObjects()) {
                vp->sendSelectedToBackground();
            }
        }
    });
    
    // Object Mode Switching
    createShortcut("object.mode_image", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
            }
        }
    });
    createShortcut("object.mode_link", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->setObjectInsertMode(DocumentViewport::ObjectInsertMode::Link);
            }
        }
    });
    createShortcut("object.mode_create", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->setObjectActionMode(DocumentViewport::ObjectActionMode::Create);
            }
        }
    });
    createShortcut("object.mode_select", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->setObjectActionMode(DocumentViewport::ObjectActionMode::Select);
            }
        }
    });
    
    // ===== Link Slots (delegated to viewport) =====
    createShortcut("link.slot_1", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->activateLinkSlot(0);
            }
        }
    });
    createShortcut("link.slot_2", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->activateLinkSlot(1);
            }
        }
    });
    createShortcut("link.slot_3", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->activateLinkSlot(2);
            }
        }
    });
    
    // ===== PDF/Highlighter Features =====
    createShortcut("pdf.auto_highlight", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::Highlighter) {
                vp->setAutoHighlightEnabled(!vp->isAutoHighlightEnabled());
            }
        }
    });
    
    // Connect to ShortcutManager's change signal for dynamic updates
    connect(sm, &ShortcutManager::shortcutChanged,
            this, &MainWindow::onShortcutChanged);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MainWindow] Registered" << m_managedShortcuts.size() << "managed shortcuts";
#endif
}

void MainWindow::onShortcutChanged(const QString& actionId, const QString& newShortcut)
{
    // Update the QShortcut if we manage this action
    auto it = m_managedShortcuts.find(actionId);
    if (it != m_managedShortcuts.end()) {
        QShortcut* shortcut = it.value();
        QKeySequence newSeq(newShortcut);
        shortcut->setKey(newSeq);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MainWindow] Updated shortcut:" << actionId << "->" << newShortcut;
#endif
    }
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
    
#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
    // ✅ CRITICAL: Stop controller thread before destruction
    // Qt will abort if a QThread is destroyed while still running
    if (controllerThread && controllerThread->isRunning()) {
        controllerThread->quit();
        controllerThread->wait();  // Wait for thread to finish
    }
#endif
    
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
    // Action Bar: Disconnect selection state connections
    if (m_lassoSelectionConn) {
        disconnect(m_lassoSelectionConn);
        m_lassoSelectionConn = {};
    }
    if (m_objectSelectionForActionBarConn) {
        disconnect(m_objectSelectionForActionBarConn);
        m_objectSelectionForActionBarConn = {};
    }
    if (m_textSelectionConn) {
        disconnect(m_textSelectionConn);
        m_textSelectionConn = {};
    }
    if (m_strokeClipboardConn) {
        disconnect(m_strokeClipboardConn);
        m_strokeClipboardConn = {};
    }
    if (m_objectClipboardConn) {
        disconnect(m_objectClipboardConn);
        m_objectClipboardConn = {};
    }
    // Phase E.2: Disconnect outline page tracking connection
    if (m_outlinePageConn) {
        disconnect(m_outlinePageConn);
        m_outlinePageConn = {};
    }
    // Page Panel: Task 5.2: Disconnect page panel connections
    if (m_pagePanelPageConn) {
        disconnect(m_pagePanelPageConn);
        m_pagePanelPageConn = {};
    }
    if (m_pagePanelContentConn) {
        disconnect(m_pagePanelContentConn);
        m_pagePanelContentConn = {};
    }
    if (m_pagePanelActionBarConn) {
        disconnect(m_pagePanelActionBarConn);
        m_pagePanelActionBarConn = {};
    }
    // BUG FIX: Disconnect documentModified connection
    if (m_documentModifiedConn) {
        disconnect(m_documentModifiedConn);
        m_documentModifiedConn = {};
    }
    if (m_markdownNotesPageConn) {
        disconnect(m_markdownNotesPageConn);
        m_markdownNotesPageConn = {};
    }
    if (m_markdownNoteOpenConn) {
        disconnect(m_markdownNoteOpenConn);
        m_markdownNoteOpenConn = {};
    }
    // M.7.3: Disconnect linkObjectList connection
    if (m_linkObjectListConn) {
        disconnect(m_linkObjectListConn);
        m_linkObjectListConn = {};
    }
    // Phase R.4: Disconnect PDF relink connection
    if (m_pdfRelinkConn) {
        disconnect(m_pdfRelinkConn);
        m_pdfRelinkConn = {};
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
    // When tool is changed via keyboard shortcuts or programmatically,
    // update the toolbar button and subtoolbar to match
    m_toolChangedConn = connect(viewport, &DocumentViewport::toolChanged, this, [this](ToolType tool) {
        // Update toolbar to show correct button selected
        if (m_toolbar) {
            m_toolbar->setCurrentTool(tool);
        }
        // Update subtoolbar container to show correct subtoolbar
        if (m_subtoolbarContainer) {
            m_subtoolbarContainer->onToolChanged(tool);
        }
        // Update action bar container for tool context
        if (m_actionBarContainer) {
            m_actionBarContainer->onToolChanged(tool);
        }
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
    
    // =========================================================================
    // Action Bar: Connect selection state signals to ActionBarContainer
    // =========================================================================
    
    // Lasso selection changed (shows/hides LassoActionBar)
    m_lassoSelectionConn = connect(viewport, &DocumentViewport::lassoSelectionChanged,
                                   m_actionBarContainer, &ActionBarContainer::onLassoSelectionChanged);
    
    // Object selection changed (shows/hides ObjectSelectActionBar)
    // Note: objectSelectionChanged has no bool parameter, so we wrap it
    m_objectSelectionForActionBarConn = connect(viewport, &DocumentViewport::objectSelectionChanged,
                                                this, [this, viewport]() {
        if (m_actionBarContainer) {
            bool hasSelection = !viewport->selectedObjects().isEmpty();
            m_actionBarContainer->onObjectSelectionChanged(hasSelection);
        }
    });
    
    // Text selection changed (shows/hides TextSelectionActionBar)
    m_textSelectionConn = connect(viewport, &DocumentViewport::textSelectionChanged,
                                  m_actionBarContainer, &ActionBarContainer::onTextSelectionChanged);
    
    // Stroke clipboard changed (shows/hides Paste button in LassoActionBar)
    m_strokeClipboardConn = connect(viewport, &DocumentViewport::strokeClipboardChanged,
                                    m_actionBarContainer, &ActionBarContainer::onStrokeClipboardChanged);
    
    // Object clipboard changed (shows/hides Paste button in ObjectSelectActionBar)
    m_objectClipboardConn = connect(viewport, &DocumentViewport::objectClipboardChanged,
                                    m_actionBarContainer, &ActionBarContainer::onObjectClipboardChanged);
    
    // Sync initial action bar state from viewport
    // CR-AB-2 FIX: Sync ALL context states to prevent stale state from previous tab
    if (m_actionBarContainer) {
        // Trigger tool change to evaluate initial visibility
        m_actionBarContainer->onToolChanged(viewport->currentTool());
        
        // Sync all selection/clipboard states
        m_actionBarContainer->onLassoSelectionChanged(viewport->hasLassoSelection());
        m_actionBarContainer->onObjectSelectionChanged(viewport->hasSelectedObjects());
        m_actionBarContainer->onTextSelectionChanged(viewport->hasTextSelection());
        m_actionBarContainer->onStrokeClipboardChanged(viewport->hasStrokesInClipboard());
        m_actionBarContainer->onObjectClipboardChanged(viewport->hasObjectsInClipboard());
    }
    
    // =========================================================================
    // Phase E.2: Connect page change to OutlinePanel for section highlighting
    // =========================================================================
    
    if (m_leftSidebar) {
        OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
        if (outlinePanel) {
            // Connect viewport's currentPageChanged to outline highlighting
            m_outlinePageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                                        outlinePanel, &OutlinePanel::highlightPage);
            
            // Sync current page state immediately
            outlinePanel->highlightPage(viewport->currentPageIndex());
        }
    }
    
    // =========================================================================
    // Page Panel: Task 5.2: Connect viewport ↔ PagePanel
    // =========================================================================
    
    if (m_pagePanel) {
        // Connect viewport's currentPageChanged to PagePanel
        m_pagePanelPageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                                      m_pagePanel, &PagePanel::onCurrentPageChanged);
        
        // Connect documentModified to invalidate current page's thumbnail
        // This ensures thumbnails update when user draws/erases/pastes
        m_pagePanelContentConn = connect(viewport, &DocumentViewport::documentModified, 
                                         this, [this, viewport]() {
            if (m_pagePanel && viewport) {
                // Invalidate the current page's thumbnail (most likely the one being edited)
                m_pagePanel->invalidateThumbnail(viewport->currentPageIndex());
            }
        });
        
        // Sync current page state immediately
        m_pagePanel->onCurrentPageChanged(viewport->currentPageIndex());
    }
    
    // =========================================================================
    // BUG FIX: Connect documentModified to mark document and tab as modified
    // This was missing, causing the save prompt to never show when closing tabs
    // =========================================================================
    if (viewport && m_tabManager) {
        m_documentModifiedConn = connect(viewport, &DocumentViewport::documentModified,
                                          this, [this, viewport]() {
            if (!viewport || !m_tabManager) return;
            
            Document* doc = viewport->document();
            if (doc) {
                // Mark document as modified (sets doc->modified = true)
                doc->markModified();
                
                // Find the tab index for this viewport
                int tabIndex = -1;
                for (int i = 0; i < m_tabManager->tabCount(); ++i) {
                    if (m_tabManager->viewportAt(i) == viewport) {
                        tabIndex = i;
                        break;
                    }
                }
                
                // Mark the tab as modified (shows * in title)
                if (tabIndex >= 0) {
                    m_tabManager->markTabModified(tabIndex, true);
                }
            }
        });
    }
    
    // Page Panel: Task 5.3: Sync PagePanelActionBar with viewport
    if (m_pagePanelActionBar) {
        // Connect viewport's currentPageChanged to PagePanelActionBar (tracked connection)
        m_pagePanelActionBarConn = connect(viewport, &DocumentViewport::currentPageChanged,
                this, [this](int pageIndex) {
            if (m_pagePanelActionBar) {
                m_pagePanelActionBar->setCurrentPage(pageIndex);
            }
        });
        
        // Sync current state immediately
        if (Document* doc = viewport->document()) {
            m_pagePanelActionBar->setPageCount(doc->pageCount());
            m_pagePanelActionBar->setCurrentPage(viewport->currentPageIndex());
            m_pagePanelActionBar->setAutoLayoutEnabled(viewport->autoLayoutEnabled());
        }
    }
    
    // Phase M.3: Refresh markdown notes sidebar when page changes
    if (markdownNotesSidebar) {
        // Set edgeless mode (hides page range controls for edgeless documents)
        Document* doc = viewport->document();
        markdownNotesSidebar->setEdgelessMode(doc && doc->isEdgeless());
        
        // Set initial page info for search range defaults
        if (doc && !doc->isEdgeless()) {
            markdownNotesSidebar->setCurrentPageInfo(viewport->currentPageIndex(), doc->pageCount());
        }
        
        m_markdownNotesPageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                this, [this](int pageIndex) {
            if (!markdownNotesSidebar) return;
            
            // Update page info for search range defaults
            DocumentViewport* vp = currentViewport();
            if (vp && vp->document() && !vp->document()->isEdgeless()) {
                markdownNotesSidebar->setCurrentPageInfo(pageIndex, vp->document()->pageCount());
            }
            
            // Refresh notes display
            if (markdownNotesSidebar->isVisible()) {
                markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
            }
        });
        
        // Load notes for current page if sidebar is visible
        if (markdownNotesSidebar->isVisible()) {
            markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
        }
        
        // Phase M.5: Handle requestOpenMarkdownNote signal (create/open note)
        m_markdownNoteOpenConn = connect(viewport, &DocumentViewport::requestOpenMarkdownNote,
                this, [this](const QString& noteId, const QString& /*linkObjectId*/) {
            if (!markdownNotesSidebar) return;
            
            // Show the markdown notes sidebar if hidden
            if (!markdownNotesSidebar->isVisible()) {
                toggleMarkdownNotesSidebar();
            }
            
            // Reload notes to include the new/opened note
            markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
            
            // Scroll to the note and set it to edit mode
            markdownNotesSidebar->scrollToNote(noteId);
            markdownNotesSidebar->setNoteEditMode(noteId, true);
        });
        
        // M.7.3: Handle linkObjectListMayHaveChanged signal (objects add/remove, tile eviction)
        m_linkObjectListConn = connect(viewport, &DocumentViewport::linkObjectListMayHaveChanged,
                this, [this]() {
            if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
                markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
            }
        });
    }
    
    // =========================================================================
    // Phase R.4: PDF Relink - Connect signal and check for missing PDF
    // =========================================================================
    
    m_pdfRelinkConn = connect(viewport, &DocumentViewport::requestPdfRelink,
            this, [this, viewport]() {
        showPdfRelinkDialog(viewport);
    });
    
    // Check if PDF is missing and show banner
    Document* doc = viewport->document();
    if (doc && doc->hasPdfReference() && !doc->isPdfLoaded()) {
        QFileInfo pdfInfo(doc->pdfPath());
        viewport->showMissingPdfBanner(pdfInfo.fileName());
    } else if (doc) {
        // PDF exists or no PDF reference - ensure banner is hidden
        viewport->hideMissingPdfBanner();
    }
    
    // Update Relink PDF menu action enabled state
    if (m_relinkPdfAction) {
        m_relinkPdfAction->setEnabled(doc && doc->hasPdfReference());
    }
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
            
            // Show LinkObject color button
            m_objectSelectSubToolbar->setLinkObjectColor(link->iconColor, true);
            
            // Show LinkObject description editor
            m_objectSelectSubToolbar->setLinkObjectDescription(link->description, true);
            return;
        }
    }
    
    // No LinkObject selected (or multiple objects selected) - clear slots and hide controls
    m_objectSelectSubToolbar->clearSlotStates();
    m_objectSelectSubToolbar->setLinkObjectColor(Qt::transparent, false);
    m_objectSelectSubToolbar->setLinkObjectDescription(QString(), false);
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
    
    // Apply marker settings
    if (m_markerSubToolbar) {
        viewport->setMarkerColor(m_markerSubToolbar->currentColor());
        viewport->setMarkerThickness(m_markerSubToolbar->currentThickness());
    }
    
    // Apply highlighter color (uses separate m_highlighterColor in viewport)
    // Note: Highlighter and Marker share the same color PRESETS (QSettings),
    // but the Highlighter tool uses a separate color variable in DocumentViewport
    if (m_highlighterSubToolbar) {
        viewport->setHighlighterColor(m_highlighterSubToolbar->currentColor());
    }
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
        
    }
}

// ============================================================================
// Phase R.4: Unified PDF Relink Handler
// ============================================================================

void MainWindow::showPdfRelinkDialog(DocumentViewport* viewport)
{
    if (!viewport) return;
    
    Document* doc = viewport->document();
    if (!doc || !doc->hasPdfReference()) return;
    
    // Open PdfRelinkDialog with hash verification
    PdfRelinkDialog dialog(doc->pdfPath(), doc->pdfHash(), doc->pdfSize(), this);
    if (dialog.exec() == QDialog::Accepted) {
        PdfRelinkDialog::Result result = dialog.getResult();
        
        if (result == PdfRelinkDialog::RelinkPdf) {
            QString newPath = dialog.getNewPdfPath();
            if (!newPath.isEmpty() && doc->relinkPdf(newPath)) {
                // Hide the banner
                viewport->hideMissingPdfBanner();
                
                // Refresh viewport to show PDF backgrounds
                viewport->update();
                
                // Refresh OutlinePanel - PDF may have outline now
                updateOutlinePanelForDocument(doc);
                
                // Refresh PagePanel thumbnails to show PDF background
                if (m_pagePanel) {
                    m_pagePanel->invalidateAllThumbnails();
                }
            }
        } else if (result == PdfRelinkDialog::ContinueWithoutPdf) {
            // User chose to continue without PDF - hide the banner
            viewport->hideMissingPdfBanner();
        }
        // Cancel: do nothing, banner remains visible
    }
}

// ============================================================================
// Phase 8: PDF Export Dialog
// ============================================================================

void MainWindow::showPdfExportDialog()
{
    DocumentViewport* viewport = currentViewport();
    if (!viewport) {
        QMessageBox::warning(this, tr("Export to PDF"), 
                             tr("No document is currently open."));
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        QMessageBox::warning(this, tr("Export to PDF"),
                             tr("No document is currently open."));
        return;
    }
    
    // Check if document is paged (PDF export only makes sense for paged documents)
    if (doc->isEdgeless()) {
        QMessageBox::warning(this, tr("Export to PDF"),
                             tr("PDF export is only available for paged documents.\n"
                                "Edgeless canvas export is not yet supported."));
        return;
    }
    
    // Check for unsaved changes - require saving first
    if (doc->modified) {
        QMessageBox::StandardButton result = QMessageBox::question(
            this, tr("Save Document First"),
            tr("The document has unsaved changes.\n"
               "Please save the document before exporting to PDF.\n\n"
               "Would you like to save now?"),
            QMessageBox::Save | QMessageBox::Cancel);
        
        if (result == QMessageBox::Save) {
            saveDocument();
            // If still modified after save attempt, user cancelled or save failed
            if (doc->modified) {
                return;
            }
        } else {
            return;
        }
    }
    
    // Show the export dialog
    PdfExportDialog dialog(doc, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Get export options from dialog
        PdfExportOptions options;
        options.outputPath = dialog.outputPath();
        options.pageRange = dialog.pageRange();
        options.dpi = dialog.dpi();
        options.preserveMetadata = true;
        options.preserveOutline = true;
        
        // Create exporter and export
        MuPdfExporter exporter;
        exporter.setDocument(doc);
        
        // TODO: Phase 9 - Show progress dialog instead of blocking
        // For now, just do a blocking export with a wait cursor
        QApplication::setOverrideCursor(Qt::WaitCursor);
        PdfExportResult result = exporter.exportPdf(options);
        QApplication::restoreOverrideCursor();
        
        if (result.success) {
            QMessageBox::information(this, tr("Export Complete"),
                                     tr("PDF exported successfully!\n\n"
                                        "Pages exported: %1\n"
                                        "File size: %2 KB")
                                     .arg(result.pagesExported)
                                     .arg(result.fileSizeBytes / 1024));
        } else {
            QMessageBox::warning(this, tr("Export Failed"),
                                 tr("Failed to export PDF:\n%1").arg(result.errorMessage));
        }
    }
}

// ============================================================================
// Phase E.2: OutlinePanel Update for Document
// ============================================================================

void MainWindow::updateOutlinePanelForDocument(Document* doc)
{
    if (!m_leftSidebar) {
        return;
    }
    
    OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
    if (!outlinePanel) {
        return;
    }
    
    // Case 1: No document or not a PDF document
    if (!doc || !doc->isPdfLoaded()) {
        m_leftSidebar->showOutlineTab(false);
        outlinePanel->clearOutline();
        return;
    }
    
    // Case 2: PDF document but no outline
    const PdfProvider* pdf = doc->pdfProvider();
    if (!pdf || !pdf->hasOutline()) {
        m_leftSidebar->showOutlineTab(false);
        outlinePanel->clearOutline();
        return;
    }
    
    // Case 3: PDF with outline - show tab and load data
    QVector<PdfOutlineItem> outline = pdf->outline();
    outlinePanel->setOutline(outline);
    m_leftSidebar->showOutlineTab(true);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Phase E.2: Loaded outline with" << outline.size() << "top-level items";
#endif
}

// ============================================================================
// Page Panel: Task 5.1: Update PagePanel for Viewport
// ============================================================================

void MainWindow::updatePagePanelForViewport(DocumentViewport* viewport)
{
    if (!m_leftSidebar) {
        return;
    }
    
    PagePanel* pagePanel = m_leftSidebar->pagePanel();
    if (!pagePanel) {
        return;
    }
    
    // Case 1: No viewport or no document
    if (!viewport || !viewport->document()) {
        m_leftSidebar->showPagesTab(false);
        pagePanel->setDocument(nullptr);
        updatePagePanelActionBarVisibility();  // Task 5.4: Hide action bar
        return;
    }
    
    Document* doc = viewport->document();
    
    // Case 2: Edgeless document - hide Pages tab
    if (doc->isEdgeless()) {
        m_leftSidebar->showPagesTab(false);
        pagePanel->setDocument(nullptr);
        updatePagePanelActionBarVisibility();  // Task 5.4: Hide action bar
        return;
    }
    
    // Case 3: Paged document - show Pages tab
    pagePanel->setDocument(doc);
    pagePanel->setCurrentPageIndex(viewport->currentPageIndex());
    m_leftSidebar->showPagesTab(true);
    
    // Task 5.4: Update action bar visibility when viewport changes
    updatePagePanelActionBarVisibility();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Page Panel: Updated for document with" << doc->pageCount() << "pages";
#endif
}

// ============================================================================
// Helper: Notify PagePanel and ActionBar after page structure change
// ============================================================================

void MainWindow::notifyPageStructureChanged(Document* doc, int currentPage)
{
    // Update PagePanel thumbnail model
    if (m_pagePanel) {
        m_pagePanel->onPageCountChanged();
    }
    
    // Update action bar page count and optionally current page
    if (m_pagePanelActionBar && doc) {
        m_pagePanelActionBar->setPageCount(doc->pageCount());
        if (currentPage >= 0) {
            m_pagePanelActionBar->setCurrentPage(currentPage);
        }
    }
}

// ============================================================================
// Helper: Save new document with dialog prompt (Android-aware)
// ============================================================================

bool MainWindow::saveNewDocumentWithDialog(Document* doc)
{
    // Single source of truth for "Save As" functionality
    // Works correctly on both Android (app-private storage) and desktop (file dialog)
    
    if (!doc || !m_documentManager) {
        return false;
    }
    
    bool isEdgeless = doc->isEdgeless();
    QString defaultName = doc->name.isEmpty() 
        ? (isEdgeless ? tr("Untitled Canvas") : tr("Untitled Document"))
        : doc->name;
    
    QString filePath;
    
#ifdef Q_OS_ANDROID
    // Android: Save to app-private storage using touch-friendly dialog
    QString notebooksDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
    QDir().mkpath(notebooksDir);
    
    bool ok;
    QString dialogTitle = isEdgeless ? tr("Save Canvas") : tr("Save Document");
    QString docName = SaveDocumentDialog::getDocumentName(this, dialogTitle, defaultName, &ok);
    
    if (!ok || docName.isEmpty()) {
        return false; // User cancelled
    }
    
    filePath = notebooksDir + "/" + docName + ".snb";
    
    // Check if file exists and ask for overwrite confirmation
    if (QDir(filePath).exists()) {
        if (QMessageBox::question(this, tr("Overwrite?"),
                tr("A document named '%1' already exists.\nDo you want to replace it?").arg(docName),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return false;
        }
    }
#else
    // Desktop: Use standard file dialog
    QString defaultPath = QDir::homePath() + "/" + defaultName + ".snb";
    
    filePath = QFileDialog::getSaveFileName(
        this,
        isEdgeless ? tr("Save Canvas") : tr("Save Document"),
        defaultPath,
        tr("SpeedyNote Bundle (*.snb)")
    );
    
    if (filePath.isEmpty()) {
        return false; // User cancelled
    }
#endif
    
    // Ensure .snb extension
    if (!filePath.endsWith(".snb", Qt::CaseInsensitive)) {
        filePath += ".snb";
    }
    
    // Update document name from file name
    QFileInfo fileInfo(filePath);
    doc->name = fileInfo.baseName();
    
    // Save using DocumentManager
    if (!m_documentManager->saveDocumentAs(doc, filePath)) {
        QMessageBox::critical(this, tr("Save Error"),
            tr("Failed to save document to:\n%1").arg(filePath));
        return false;
    }
    
    // Phase P.4.6: Save thumbnail to NotebookLibrary
    if (!isEdgeless && doc->pageCount() > 0) {
        QPixmap thumbnail = m_pagePanel ? m_pagePanel->thumbnailForPage(0) : QPixmap();
        if (thumbnail.isNull()) {
            thumbnail = renderPage0Thumbnail(doc);
        }
        if (!thumbnail.isNull()) {
            NotebookLibrary::instance()->saveThumbnail(filePath, thumbnail);
        }
    }
    
    // Register with NotebookLibrary
    NotebookLibrary::instance()->addToRecent(filePath);
    
#ifdef SPEEDYNOTE_DEBUG
    if (isEdgeless) {
        qDebug() << "saveNewDocumentWithDialog: Saved edgeless canvas to" << filePath;
    } else {
        qDebug() << "saveNewDocumentWithDialog: Saved" << doc->pageCount() << "pages to" << filePath;
    }
#endif
    
    return true;
}

// ============================================================================
// Phase doc-1: Document Operations
// ============================================================================

void MainWindow::saveDocument()
{
    // Phase doc-1.1: Save current document to file
    // Uses DocumentManager for proper document handling
    // All documents (paged and edgeless) are saved as .snb bundles
    // - If document has existing path: save in-place (no dialog)
    // - If new document: show Save As dialog
    
    if (!m_documentManager || !m_tabManager) {
        #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "saveDocument: DocumentManager or TabManager not initialized";
        #endif
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
            
    // Update lastAccessedPage before saving (for restoring position on reload)
    if (!doc->isEdgeless()) {
        doc->lastAccessedPage = viewport->currentPageIndex();
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "saveDocument: Setting lastAccessedPage to" << doc->lastAccessedPage;
#endif
    } else {
        // Phase 4: Sync edgeless position history to document before saving
        viewport->syncPositionToDocument();
    }
            
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
        
        // Phase P.4.6: Save thumbnail to NotebookLibrary
        if (!isEdgeless && doc->pageCount() > 0) {
            QPixmap thumbnail = m_pagePanel ? m_pagePanel->thumbnailForPage(0) : QPixmap();
            if (thumbnail.isNull()) {
                thumbnail = renderPage0Thumbnail(doc);
            }
            if (!thumbnail.isNull()) {
                NotebookLibrary::instance()->saveThumbnail(existingPath, thumbnail);
            }
        }
        
        if (isEdgeless) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "saveDocument: Saved edgeless canvas with" 
                     << doc->tileIndexCount() << "tiles to" << existingPath;
#endif
        } else {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "saveDocument: Saved" << doc->pageCount() << "pages to" << existingPath;
#endif
        }
                return;
            }
            
    // ✅ New document or temp bundle - use unified save dialog
    if (!saveNewDocumentWithDialog(doc)) {
        return;  // User cancelled or save failed
    }
    
    // Update tab title and NavigationBar
    int currentIndex = m_tabManager->currentIndex();
    if (currentIndex >= 0) {
        m_tabManager->setTabTitle(currentIndex, doc->name);
        m_tabManager->markTabModified(currentIndex, false);
    }
    if (m_navigationBar) {
        m_navigationBar->setFilename(doc->name);
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
    
    QString filePath;
    
#ifdef Q_OS_ANDROID
    // BUG-A002 Fix: On Android, show list of saved documents from app-private storage.
    // QFileDialog returns content:// URIs which don't work for .snb bundles (directories).
    QString notebooksDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
    QDir dir(notebooksDir);
    
    // Get list of .snb bundles (they are directories)
    QStringList notebooks = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    // Filter to only include .snb directories
    QStringList snbNotebooks;
    for (const QString& name : notebooks) {
        if (name.endsWith(".snb", Qt::CaseInsensitive)) {
            snbNotebooks << name;
        }
    }
    
    if (snbNotebooks.isEmpty()) {
        QMessageBox::information(this, tr("No Documents"),
            tr("No saved documents found.\n\nDocuments are saved to:\n%1").arg(notebooksDir));
        return;
    }
    
    // Show selection dialog
    bool ok;
    QString selected = QInputDialog::getItem(this, tr("Open Document"),
        tr("Select a document:"), snbNotebooks, 0, false, &ok);
    
    if (!ok || selected.isEmpty()) {
        return; // User cancelled
    }
    
    filePath = notebooksDir + "/" + selected;
#else
    // Open file dialog for file selection
    // Phase O1.7.6: Unified .snb bundle format
    QString filter = tr("SpeedyNote Files (*.snb *.pdf);;SpeedyNote Bundle (*.snb);;PDF Documents (*.pdf);;All Files (*)");
    filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Document"),
        QDir::homePath(),
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
        return;
    }
#endif
    
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
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "loadDocument: Loaded" << doc->pageCount() << "pages from" << filePath;
#endif
    }
}

void MainWindow::addPageToDocument()
{
    // Phase doc-1.0: Add new page at end of document
    // Required for multi-page save/load testing
    
    if (!m_tabManager) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addPageToDocument: No tab manager";
#endif
        return;
    }
    
    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addPageToDocument: No current viewport";
#endif
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addPageToDocument: No document in viewport";
#endif
        return;
    }
    
    // Add page at end
    Page* newPage = doc->addPage();
    if (newPage) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addPageToDocument: Added page" << doc->pageCount() 
                 << "to document" << doc->name;
#endif
    
        // CRITICAL: Notify viewport that document structure changed
        // This invalidates layout cache and triggers repaint
        viewport->notifyDocumentStructureChanged();
        
        // Mark tab as modified
        int currentIndex = m_tabManager->currentIndex();
        if (currentIndex >= 0) {
            m_tabManager->markTabModified(currentIndex, true);
        }
        
        // Update PagePanel and action bar
        notifyPageStructureChanged(doc);
    }
}

void MainWindow::insertPageInDocument()
{
    // Phase 3: Insert new page after current page
    // Works for both PDF and non-PDF documents (inserted page has no PDF background)
    
    if (!m_tabManager) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertPageInDocument: No tab manager";
#endif
        return;
        }
    
    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertPageInDocument: No current viewport";
#endif
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertPageInDocument: No document in viewport";
#endif
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
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertPageInDocument: Inserted page at" << insertIndex
                 << "in document" << doc->name << "(now" << doc->pageCount() << "pages)";
#endif
        
        // Notify viewport that document structure changed
        viewport->notifyDocumentStructureChanged();
        
        // Mark tab as modified
        int tabIndex = m_tabManager->currentIndex();
        if (tabIndex >= 0) {
            m_tabManager->markTabModified(tabIndex, true);
        }
        
        // Update PagePanel and action bar
        notifyPageStructureChanged(doc);
    }
}

void MainWindow::deletePageInDocument()
{
    // Phase 3B: Delete current page
    // - Non-PDF pages: delete entirely
    // - PDF pages: blocked (use external tool to modify PDF)
    
    if (!m_tabManager) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: No tab manager";
#endif
        return;
    }

    DocumentViewport* viewport = m_tabManager->currentViewport();
    if (!viewport) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: No current viewport";
#endif
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: No document in viewport";
#endif
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
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: Invalid page index" << currentPageIndex;
#endif
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
    if (!doc->removePage(currentPageIndex)) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: Failed to delete page" << currentPageIndex;
#endif
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "deletePageInDocument: Deleted page at" << currentPageIndex
             << "in document" << doc->name << "(now" << doc->pageCount() << "pages)";
#endif
    
    // Notify viewport that document structure changed
    viewport->notifyDocumentStructureChanged();
    
    // Navigate to appropriate page (stay at same index or go to last page)
    int newPage = qMin(currentPageIndex, doc->pageCount() - 1);
    viewport->scrollToPage(newPage);
        
    // Mark tab as modified
    int tabIndex = m_tabManager->currentIndex();
    if (tabIndex >= 0) {
        m_tabManager->markTabModified(tabIndex, true);
    }
    
    // Update PagePanel and action bar
    notifyPageStructureChanged(doc, newPage);
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
#ifdef Q_OS_ANDROID
        // BUG-A003: Use shared Android PDF picker that handles SAF permissions properly.
        // See source/android/PdfPickerAndroid.cpp for implementation.
        pdfPath = PdfPickerAndroid::pickPdfFile();
        
        if (pdfPath.isEmpty()) {
            // User cancelled or error
            return;
        }
#else
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
#endif
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
        // Note: zoomToWidth() is called automatically by DocumentViewport::setDocument()
        // for new paged documents, which also handles horizontal centering.
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "openPdfDocument: Loaded PDF with" << doc->pageCount() 
                 << "pages from" << filePath;
#endif
    } else {
        qWarning() << "openPdfDocument: Failed to create tab for document";
    }
}

        
void MainWindow::forceUIRefresh() {
    setWindowState(Qt::WindowNoState);  // Restore first
    setWindowState(Qt::WindowMaximized);  // Maximize again
}

// REMOVED MW7.3: loadPdf function removed - old PDF loading function




    
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
    
    // Apply default page size and background settings from user preferences
    {
        QSettings settings("SpeedyNote", "App");
        
        // Load page size (default: US Letter at 96 DPI)
        qreal pageWidth = settings.value("page/width", 816).toReal();
        qreal pageHeight = settings.value("page/height", 1056).toReal();
        QSizeF defaultPageSize(pageWidth, pageHeight);
        
        // Load background settings
        // Default: Grid with 32px spacing (32 divides evenly into 1024px tiles)
        Page::BackgroundType defaultStyle = static_cast<Page::BackgroundType>(
            settings.value("background/type", static_cast<int>(Page::BackgroundType::Grid)).toInt());
        QColor defaultBgColor = QColor(settings.value("background/color", "#ffffff").toString());
        QColor defaultGridColor = QColor(settings.value("background/gridColor", "#c8c8c8").toString());
        int defaultGridSpacing = settings.value("background/gridSpacing", 32).toInt();
        int defaultLineSpacing = settings.value("background/lineSpacing", 32).toInt();
        
        // Update document defaults for future pages
        doc->defaultPageSize = defaultPageSize;
        doc->defaultBackgroundType = defaultStyle;
        doc->defaultBackgroundColor = defaultBgColor;
        doc->defaultGridColor = defaultGridColor;
        doc->defaultGridSpacing = defaultGridSpacing;
        doc->defaultLineSpacing = defaultLineSpacing;
        
        // Also apply to the first page (already created by Document::createNew)
        if (doc->pageCount() > 0) {
            Page* firstPage = doc->page(0);
            if (firstPage) {
                firstPage->size = defaultPageSize;
                firstPage->backgroundType = defaultStyle;
                firstPage->backgroundColor = defaultBgColor;
                firstPage->gridColor = defaultGridColor;
                firstPage->gridSpacing = defaultGridSpacing;
                firstPage->lineSpacing = defaultLineSpacing;
            }
        }
    }
    
    // Create a new tab with DocumentViewport
    QString tabTitle = doc->displayName();
    int tabIndex = m_tabManager->createTab(doc, tabTitle);
    
    // Switch to the new tab (TabManager::createTab already does this, but ensure it's set)
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(tabIndex);
    }
    
    // Note: zoomToWidth() is called automatically by DocumentViewport::setDocument()
    // for new paged documents, which also handles horizontal centering.
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
    
    // Apply default background settings from user preferences
    // Default: Grid with 32px spacing (32 divides evenly into 1024px tiles)
    {
        QSettings settings("SpeedyNote", "App");
        Page::BackgroundType defaultStyle = static_cast<Page::BackgroundType>(
            settings.value("background/type", static_cast<int>(Page::BackgroundType::Grid)).toInt());
        QColor defaultBgColor = QColor(settings.value("background/color", "#ffffff").toString());
        QColor defaultGridColor = QColor(settings.value("background/gridColor", "#c8c8c8").toString());
        int defaultGridSpacing = settings.value("background/gridSpacing", 32).toInt();
        int defaultLineSpacing = settings.value("background/lineSpacing", 32).toInt();
        
        // Update document defaults for tiles
        doc->defaultBackgroundType = defaultStyle;
        doc->defaultBackgroundColor = defaultBgColor;
        doc->defaultGridColor = defaultGridColor;
        doc->defaultGridSpacing = defaultGridSpacing;
        doc->defaultLineSpacing = defaultLineSpacing;
    }
    
    // Create a new tab with DocumentViewport
    QString tabTitle = doc->displayName();
    int tabIndex = m_tabManager->createTab(doc, tabTitle);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Created new edgeless tab at index" << tabIndex << "with document:" << tabTitle;
#endif
    
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
    // ==========================================================================
    // UI ENTRY POINT: Shows directory dialog, then delegates to openFileInNewTab
    // ==========================================================================
    // This function ONLY handles the UI dialog. All actual document loading
    // and setup is done by openFileInNewTab() - the single source of truth.
    //
    // Uses directory selection because .snb is a folder, not a single file.
    // TODO: Replace with unified file picker when .snb becomes a single file.
    // ==========================================================================
    
#ifdef Q_OS_ANDROID
    // On Android, just use the regular loadDocument() which shows a list dialog
    loadDocument();
    return;
#endif
    
    // Show directory dialog to select .snb bundle folder
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
    // This validation is specific to directory-based bundles
    QString manifestPath = bundlePath + "/document.json";
    if (!QFile::exists(manifestPath)) {
        QMessageBox::critical(this, tr("Load Error"),
            tr("Selected folder is not a valid SpeedyNote bundle.\n"
               "Missing document.json manifest.\n\n%1").arg(bundlePath));
        return;
    }
    
    // Delegate to the single implementation
    openFileInNewTab(bundlePath);
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

int MainWindow::tabCount() const {
    if (m_tabBar) {
        return m_tabBar->count();
    }
    return 0;
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
    int newPage = QInputDialog::getInt(this, tr("Jump to Page"), tr("Enter Page Number:"), 
                                       currentPage, 1, 9999, 1, &ok);
    if (ok) {
        // Convert 1-based user input to 0-based index for switchPage()
        switchPage(newPage - 1);
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
    // BUG-AB-001/UI-001 FIX: Use m_canvasContainer directly instead of m_viewportStack->parentWidget()
    // The event filter was installed on m_canvasContainer, so compare with that directly
    if (obj == m_canvasContainer && event->type() == QEvent::Resize) {
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
        // Wheel events are now handled entirely by DocumentViewport::wheelEvent()
        // including trackpad blocking when TouchGestureMode::Disabled
    }

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
        QColor blue("#316882");  // SpeedyNote default teal accent
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
#elif defined(Q_OS_ANDROID)
    // On Android, query the system via JNI
    // Calls SpeedyNoteActivity.isDarkMode() which checks Configuration.UI_MODE_NIGHT_MASK
    // callStaticMethod<jboolean> returns the primitive directly, not a QJniObject
    return QJniObject::callStaticMethod<jboolean>(
        "org/speedynote/app/SpeedyNoteActivity",
        "isDarkMode",
        "()Z"
    );
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

void MainWindow::applyBackgroundSettings(Page::BackgroundType type, const QColor& bgColor,
                                         const QColor& gridColor, int gridSpacing, int lineSpacing) {
    // Apply to current document
    DocumentViewport* viewport = currentViewport();
    if (!viewport) {
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        return;
    }
    
    // Update document defaults for future pages
    doc->defaultBackgroundType = type;
    doc->defaultBackgroundColor = bgColor;
    doc->defaultGridColor = gridColor;
    doc->defaultGridSpacing = gridSpacing;
    doc->defaultLineSpacing = lineSpacing;
    
    // Apply to all existing pages in the document
    // IMPORTANT: Skip pages with PDF backgrounds - they should never be overwritten
    for (int i = 0; i < doc->pageCount(); ++i) {
        Page* page = doc->page(i);
        if (page) {
            // Preserve PDF backgrounds - only apply settings to non-PDF pages
            if (page->backgroundType != Page::BackgroundType::PDF) {
                page->backgroundType = type;
            }
            // Always update colors and spacing (these affect the rendering even for PDF pages)
            page->backgroundColor = bgColor;
            page->gridColor = gridColor;
            page->gridSpacing = gridSpacing;
            page->lineSpacing = lineSpacing;
        }
    }
    
    // For edgeless documents, also update tiles
    if (doc->mode == Document::Mode::Edgeless) {
        QVector<Document::TileCoord> tileCoords = doc->allTileCoords();
        for (const auto& coord : tileCoords) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (tile) {
                // Preserve PDF backgrounds - only apply settings to non-PDF tiles
                if (tile->backgroundType != Page::BackgroundType::PDF) {
                    tile->backgroundType = type;
                }
                tile->backgroundColor = bgColor;
                tile->gridColor = gridColor;
                tile->gridSpacing = gridSpacing;
                tile->lineSpacing = lineSpacing;
            }
        }
    }
    
    // Mark document as modified and trigger redraw
    doc->markModified();
    viewport->update();
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
    
    // Update all DocumentViewports
    if (m_tabManager) {
        for (int i = 0; i < m_tabManager->tabCount(); ++i) {
            if (DocumentViewport* vp = m_tabManager->viewportAt(i)) {
                vp->setDarkMode(darkMode);
            }
        }
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
    
    // Update ActionBarContainer theme
    if (m_actionBarContainer) {
        m_actionBarContainer->setDarkMode(darkMode);
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
    QString colorName = settings.value("customAccentColor", "#316882").toString();
    customAccentColor = QColor(colorName);
    
    // Ensure valid values
    if (!customAccentColor.isValid()) {
        customAccentColor = QColor("#316882"); // Default teal accent
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
    
    // Sync toolbar button state (prevents button from being out of sync after settings load)
    if (m_toolbar) {
        m_toolbar->setTouchGestureMode(static_cast<int>(mode));
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
    // MW5.8: Position sliders relative to their parent container (canvasContainer)
    // Note: Sliders are children of canvasContainer, NOT the main window, so their
    // coordinates are relative to canvasContainer. The left sidebar is a sibling of
    // canvasContainer in the layout, so we should NOT add sidebar offset here.
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
    
    // Leave a bit of space for the corner where panX and panY would intersect
    int cornerOffset = 15;
    
    // Position horizontal scrollbar at top
    // Pan X: Full width of container minus corner space for panY slider
    panXSlider->setGeometry(
        cornerOffset + margin,  // After corner space for panY
        margin,  // At top of container
        containerWidth - cornerOffset - margin * 2,  // Width minus corner and margins
        scrollbarHeight
    );
    
    // Position vertical scrollbar at left
    // Pan Y: On the LEFT side to avoid arm/wrist interference (for right-handed users)
    panYSlider->setGeometry(
        margin,  // At left edge of container
        cornerOffset + margin,  // Below corner offset (for panX)
        scrollbarWidth,
        containerHeight - cornerOffset - margin * 2  // Full height minus corners
    );
    
    // Ensure sliders are raised above content
    panXSlider->raise();
    panYSlider->raise();
    
    // Phase D: Also update subtoolbar position
    updateSubToolbarPosition();
    
    // Update action bar position
    updateActionBarPosition();
    
    // Update PDF search bar position
    updatePdfSearchBarPosition();
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
            vp->setHighlighterColor(color);
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
    
    // Connect LinkObject color change from subtoolbar
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::linkObjectColorChanged,
            this, [this](const QColor& color) {
        DocumentViewport* vp = currentViewport();
        if (!vp) return;
        
        const auto& selectedObjects = vp->selectedObjects();
        if (selectedObjects.size() != 1) return;
        
        LinkObject* link = dynamic_cast<LinkObject*>(selectedObjects.first());
        if (!link) return;
        
        // Update LinkObject color
        link->iconColor = color;
        
        // Mark document as modified
        if (Document* doc = vp->document()) {
            Page* page = doc->page(vp->currentPageIndex());
            if (page) {
                int pageIndex = doc->pageIndexByUuid(page->uuid);
                if (pageIndex >= 0) {
                    doc->markPageDirty(pageIndex);
                }
            }
        }
        
        vp->update();
        
        // Refresh markdown notes sidebar to update colors
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
        }
    });
    
    // Connect LinkObject description change from subtoolbar
    connect(m_objectSelectSubToolbar, &ObjectSelectSubToolbar::linkObjectDescriptionChanged,
            this, [this](const QString& description) {
        DocumentViewport* vp = currentViewport();
        if (!vp) return;
        
        const auto& selectedObjects = vp->selectedObjects();
        if (selectedObjects.size() != 1) return;
        
        LinkObject* link = dynamic_cast<LinkObject*>(selectedObjects.first());
        if (!link) return;
        
        // Update LinkObject description
        link->description = description;
        
        // Mark document as modified
        if (Document* doc = vp->document()) {
            Page* page = doc->page(vp->currentPageIndex());
            if (page) {
                int pageIndex = doc->pageIndexByUuid(page->uuid);
                if (pageIndex >= 0) {
                    doc->markPageDirty(pageIndex);
                }
            }
        }
        
        vp->update();
        
        // Refresh markdown notes sidebar to update descriptions
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
        }
    });
    
    // Connect tab changes to subtoolbar container and toolbar
    // Handles per-tab state for both toolbar tool selection and subtoolbar presets
    connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
        int newIndex = m_tabManager->currentIndex();
        
        if (newIndex != m_previousTabIndex) {
            // Update subtoolbar per-tab state (save old, restore new)
            m_subtoolbarContainer->onTabChanged(newIndex, m_previousTabIndex);
            
            // Note: Sidebar tab state save/restore is handled in the first currentViewportChanged
            // handler (above) because it must happen BEFORE/AFTER updatePagePanelForViewport()
            
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
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Tab changed: index" << newIndex 
                     << "tool" << (vp ? static_cast<int>(vp->currentTool()) : -1);
#endif
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
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Phase D: Subtoolbars initialized";
#endif
}

void MainWindow::updateSubToolbarPosition()
{
    if (!m_subtoolbarContainer || !m_canvasContainer) {
        return;
    }
    
    // Get canvas container geometry (the viewport area)
    // Note: SubToolbarContainer is a child of m_canvasContainer, so coordinates
    // are relative to canvasContainer. The left sidebar is a sibling of
    // canvasContainer in the layout, so we should NOT add sidebar offset here.
    QRect viewportRect = m_canvasContainer->rect();
    
    // Update subtoolbar container position
    m_subtoolbarContainer->updatePosition(viewportRect);
    
    // Ensure it's raised above viewport content
    m_subtoolbarContainer->raise();
}

void MainWindow::setupActionBars()
{
    if (!m_canvasContainer) {
        qWarning() << "setupActionBars: canvasContainer not yet created";
        return;
    }
    
    // Create action bar container as child of canvas container (floats over viewport)
    m_actionBarContainer = new ActionBarContainer(m_canvasContainer);
    
    // Create individual action bars
    m_lassoActionBar = new LassoActionBar();
    m_objectSelectActionBar = new ObjectSelectActionBar();
    m_textSelectionActionBar = new TextSelectionActionBar();
    m_clipboardActionBar = new ClipboardActionBar();
    
    // Register action bars with container
    m_actionBarContainer->setActionBar("lasso", m_lassoActionBar);
    m_actionBarContainer->setActionBar("objectSelect", m_objectSelectActionBar);
    m_actionBarContainer->setActionBar("textSelection", m_textSelectionActionBar);
    m_actionBarContainer->setActionBar("clipboard", m_clipboardActionBar);
    
    // Connect tool changes from Toolbar to ActionBarContainer
    connect(m_toolbar, &Toolbar::toolSelected, 
            m_actionBarContainer, &ActionBarContainer::onToolChanged);
    
    // Connect clipboard changes from system clipboard
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            m_actionBarContainer, &ActionBarContainer::onClipboardChanged);
    
    // BUG-AB-001 FIX: Connect position update request signal
    // This ensures the container gets a fresh viewport rect before becoming visible
    connect(m_actionBarContainer, &ActionBarContainer::positionUpdateRequested,
            this, &MainWindow::updateActionBarPosition);
    
    // Connect LassoActionBar signals to viewport
    connect(m_lassoActionBar, &LassoActionBar::copyRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->copyLassoSelection();
        }
    });
    connect(m_lassoActionBar, &LassoActionBar::cutRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->cutLassoSelection();
        }
    });
    connect(m_lassoActionBar, &LassoActionBar::pasteRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->pasteLassoSelection();
        }
    });
    connect(m_lassoActionBar, &LassoActionBar::deleteRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->deleteLassoSelection();
        }
    });
    
    // Connect ObjectSelectActionBar signals to viewport
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::copyRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->copySelectedObjects();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::pasteRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->pasteForObjectSelect();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::deleteRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->deleteSelectedObjects();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::bringForwardRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->bringSelectedForward();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::sendBackwardRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->sendSelectedBackward();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::increaseAffinityRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->increaseSelectedAffinity();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::decreaseAffinityRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->decreaseSelectedAffinity();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::cancelRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->cancelObjectSelectAction();
        }
    });
    
    // Connect TextSelectionActionBar signals to viewport
    connect(m_textSelectionActionBar, &TextSelectionActionBar::copyRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->copyTextSelection();
        }
    });
    
    // Connect ClipboardActionBar signals to viewport
    connect(m_clipboardActionBar, &ClipboardActionBar::pasteRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->pasteForObjectSelect();
        }
    });
    
    // Initial position update
    QTimer::singleShot(0, this, &MainWindow::updateActionBarPosition);
    
    // Page Panel: Task 5.3: Setup PagePanelActionBar
    setupPagePanelActionBar();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Action bars initialized";
#endif
}

void MainWindow::updateActionBarPosition()
{
    if (!m_actionBarContainer || !m_canvasContainer) {
        return;
    }
    
    // Get canvas container geometry (the viewport area)
    // Note: ActionBarContainer is a child of m_canvasContainer, so coordinates
    // are relative to canvasContainer. The sidebars are siblings of
    // canvasContainer in the layout, so we should NOT add sidebar offset here.
    QRect viewportRect = m_canvasContainer->rect();
    
    // Update action bar container position
    m_actionBarContainer->updatePosition(viewportRect);
    
    // Ensure it's raised above viewport content
    m_actionBarContainer->raise();
}

// =========================================================================
// PDF Search Bar Setup and Positioning
// =========================================================================

void MainWindow::setupPdfSearch()
{
    if (!m_canvasContainer) {
        qWarning() << "setupPdfSearch: canvasContainer not yet created";
        return;
    }
    
    // Create search bar as child of canvas container (floats over viewport)
    m_pdfSearchBar = new PdfSearchBar(m_canvasContainer);
    m_pdfSearchBar->hide();  // Hidden by default
    
    // Initialize search state
    m_searchState = std::make_unique<PdfSearchState>();
    
    // Create search engine
    m_searchEngine = new PdfSearchEngine(this);
    
    // Connect search bar signals to trigger search
    connect(m_pdfSearchBar, &PdfSearchBar::searchNextRequested, this, [this](const QString& text, bool caseSensitive, bool wholeWord) {
        onSearchNext(text, caseSensitive, wholeWord);
    });
    
    connect(m_pdfSearchBar, &PdfSearchBar::searchPrevRequested, this, [this](const QString& text, bool caseSensitive, bool wholeWord) {
        onSearchPrev(text, caseSensitive, wholeWord);
    });
    
    connect(m_pdfSearchBar, &PdfSearchBar::closed, this, [this]() {
        hidePdfSearchBar();
    });
    
    // Connect search engine signals
    connect(m_searchEngine, &PdfSearchEngine::matchFound, this, 
            &MainWindow::onSearchMatchFound);
    connect(m_searchEngine, &PdfSearchEngine::notFound, this,
            &MainWindow::onSearchNotFound);
    
    // Position at bottom of viewport
    updatePdfSearchBarPosition();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "PDF search bar initialized";
#endif
}

void MainWindow::updatePdfSearchBarPosition()
{
    if (!m_pdfSearchBar || !m_canvasContainer) {
        return;
    }
    
    // Position at the bottom of the canvas container
    QRect viewportRect = m_canvasContainer->rect();
    
    // Calculate search bar geometry: full width, at bottom
    int barHeight = m_pdfSearchBar->height();
    int y = viewportRect.height() - barHeight;
    
    m_pdfSearchBar->setGeometry(0, y, viewportRect.width(), barHeight);
    
    // Ensure it's raised above viewport content
    m_pdfSearchBar->raise();
}

void MainWindow::showPdfSearchBar()
{
    DocumentViewport *vp = currentViewport();
    if (!vp || !m_pdfSearchBar) {
        return;
    }
    
    // Only show for PDF documents
    Document *doc = vp->document();
    if (!doc || !doc->isPdfLoaded()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MainWindow] Ctrl+F ignored: not a PDF document";
#endif
        return;
    }
    
    // Update position before showing
    updatePdfSearchBarPosition();
    
    // Show and focus the search bar
    m_pdfSearchBar->showAndFocus();
    
    // Sync dark mode
    m_pdfSearchBar->setDarkMode(isDarkMode());
}

void MainWindow::hidePdfSearchBar()
{
    if (!m_pdfSearchBar) {
        return;
    }
    
    // Cancel any ongoing search and clear cache to free memory
    if (m_searchEngine) {
        m_searchEngine->cancel();
        m_searchEngine->clearCache();
    }
    
    m_pdfSearchBar->hide();
    m_pdfSearchBar->clearStatus();
    
    // Clear search highlights from viewport
    if (DocumentViewport *vp = currentViewport()) {
        vp->clearSearchMatches();
    }
    
    // Reset search state
    if (m_searchState) {
        m_searchState->clear();
    }
    
    // Return focus to viewport
    if (DocumentViewport *vp = currentViewport()) {
        vp->setFocus();
    }
}

void MainWindow::onSearchNext(const QString& text, bool caseSensitive, bool wholeWord)
{
    DocumentViewport *vp = currentViewport();
    if (!vp || !m_searchEngine || !m_searchState) {
        return;
    }
    
    Document *doc = vp->document();
    if (!doc || !doc->isPdfLoaded()) {
        return;
    }
    
    // Set the document on the engine
    m_searchEngine->setDocument(doc);
    
    // Clear status before searching
    m_pdfSearchBar->clearStatus();
    
    // Determine start position
    int startPage = 0;
    int startMatchIndex = -1;
    
    if (m_searchState->hasCurrentMatch() && m_searchState->searchText == text) {
        // Continue from current match
        startPage = m_searchState->currentPageIndex;
        startMatchIndex = m_searchState->currentMatchIndex;
    } else {
        // New search or text changed - start from current visible page
        startPage = vp->currentPageIndex();
        startMatchIndex = -1;
        
        // Reset search state for new search
        m_searchState->clear();
    }
    
    // Update search state
    m_searchState->searchText = text;
    m_searchState->caseSensitive = caseSensitive;
    m_searchState->wholeWord = wholeWord;
    
    // Trigger search
    m_searchEngine->findNext(text, caseSensitive, wholeWord, startPage, startMatchIndex);
}

void MainWindow::onSearchPrev(const QString& text, bool caseSensitive, bool wholeWord)
{
    DocumentViewport *vp = currentViewport();
    if (!vp || !m_searchEngine || !m_searchState) {
        return;
    }
    
    Document *doc = vp->document();
    if (!doc || !doc->isPdfLoaded()) {
        return;
    }
    
    // Set the document on the engine
    m_searchEngine->setDocument(doc);
    
    // Clear status before searching
    m_pdfSearchBar->clearStatus();
    
    // Determine start position
    int startPage = 0;
    int startMatchIndex = -1;
    
    if (m_searchState->hasCurrentMatch() && m_searchState->searchText == text) {
        // Continue from current match
        startPage = m_searchState->currentPageIndex;
        startMatchIndex = m_searchState->currentMatchIndex;
    } else {
        // New search or text changed - start from current visible page
        startPage = vp->currentPageIndex();
        startMatchIndex = -1;
        
        // Reset search state for new search
        m_searchState->clear();
    }
    
    // Update search state
    m_searchState->searchText = text;
    m_searchState->caseSensitive = caseSensitive;
    m_searchState->wholeWord = wholeWord;
    
    // Trigger search
    m_searchEngine->findPrev(text, caseSensitive, wholeWord, startPage, startMatchIndex);
}

void MainWindow::onSearchMatchFound(const PdfSearchMatch& match, 
                                     const QVector<PdfSearchMatch>& pageMatches)
{
    DocumentViewport *vp = currentViewport();
    if (!vp || !m_searchState) {
        return;
    }
    
    // Update search state
    m_searchState->currentPageIndex = match.pageIndex;
    m_searchState->currentMatchIndex = match.matchIndex;
    m_searchState->currentPageMatches = pageMatches;
    
    // Navigate to the page with the match
    vp->scrollToPage(match.pageIndex);
    
    // Update viewport highlights
    // Find the index of current match within pageMatches
    int currentIdx = -1;
    for (int i = 0; i < pageMatches.size(); ++i) {
        if (pageMatches[i].matchIndex == match.matchIndex) {
            currentIdx = i;
            break;
        }
    }
    
    vp->setSearchMatches(pageMatches, currentIdx, match.pageIndex);
    
    // Clear any previous "not found" status
    m_pdfSearchBar->clearStatus();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MainWindow] Search match found on page" << match.pageIndex 
             << "match" << match.matchIndex << "of" << pageMatches.size();
#endif
}

void MainWindow::onSearchNotFound(bool wrapped)
{
    Q_UNUSED(wrapped)
    
    if (m_pdfSearchBar) {
        m_pdfSearchBar->setStatus(tr("No results found"));
    }
    
    // Clear any existing highlights
    if (DocumentViewport *vp = currentViewport()) {
        vp->clearSearchMatches();
    }
    
    // Reset match state but keep search text
    if (m_searchState) {
        m_searchState->resetMatch();
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MainWindow] Search not found, wrapped:" << wrapped;
#endif
}

// =========================================================================
// Page Panel: Task 5.3: PagePanelActionBar Setup and Connections
// =========================================================================

void MainWindow::setupPagePanelActionBar()
{
    if (!m_actionBarContainer) {
        qWarning() << "setupPagePanelActionBar: ActionBarContainer not yet created";
        return;
    }
    
    // Create the PagePanelActionBar
    m_pagePanelActionBar = new PagePanelActionBar(m_actionBarContainer);
    m_actionBarContainer->setPagePanelActionBar(m_pagePanelActionBar);
    
    // -------------------------------------------------------------------------
    // Navigation signals
    // -------------------------------------------------------------------------
    
    // Page Up: Go to previous page
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageUpClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            int currentPage = vp->currentPageIndex();
            if (currentPage > 0) {
                vp->scrollToPage(currentPage - 1);
            }
        }
    });
    
    // Page Down: Go to next page
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageDownClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            int currentPage = vp->currentPageIndex();
            if (Document* doc = vp->document()) {
                if (currentPage < doc->pageCount() - 1) {
                    vp->scrollToPage(currentPage + 1);
                }
            }
        }
    });
    
    // Wheel picker page selection: Navigate directly to page
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageSelected, this, [this](int page) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->scrollToPage(page);
        }
    });
    
    // Layout toggle: Switch between 1-column and auto 1/2 column mode
    connect(m_pagePanelActionBar, &PagePanelActionBar::layoutToggleClicked, this, [this]() {
        toggleAutoLayout();
        // Update the button state to reflect the new mode
        if (DocumentViewport* vp = currentViewport()) {
            m_pagePanelActionBar->setAutoLayoutEnabled(vp->autoLayoutEnabled());
        }
    });
    
    // -------------------------------------------------------------------------
    // Page management signals
    // -------------------------------------------------------------------------
    
    // Add Page: Add a new page at the end
    connect(m_pagePanelActionBar, &PagePanelActionBar::addPageClicked, this, [this]() {
        addPageToDocument();
        // Scroll to the newly added page (at end)
        if (DocumentViewport* vp = currentViewport()) {
            if (Document* doc = vp->document()) {
                vp->scrollToPage(doc->pageCount() - 1);
            }
        }
    });
    
    // Insert Page: Insert a new page after the current page
    connect(m_pagePanelActionBar, &PagePanelActionBar::insertPageClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            int targetPage = vp->currentPageIndex() + 1;
            insertPageInDocument();
            // Scroll to the newly inserted page
            vp->scrollToPage(targetPage);
        }
    });
    
    // Delete Page (first click): Store index, wait for confirmation
    // BUG-PG-002 FIX: Defer deletion until 5-second timer expires
    // This allows the user to undo by clicking the button again
    connect(m_pagePanelActionBar, &PagePanelActionBar::deletePageClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (Document* doc = vp->document()) {
                // Can't delete the last page
                if (doc->pageCount() <= 1) {
                    m_pagePanelActionBar->resetDeleteButton();
                    return;
                }
                
                int pageIndex = vp->currentPageIndex();
                
                // BUG-PG-001 FIX: Can't delete PDF background pages
                Page* page = doc->page(pageIndex);
                if (page && page->backgroundType == Page::BackgroundType::PDF) {
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Page Panel: Cannot delete PDF page" << pageIndex;
#endif
                    m_pagePanelActionBar->resetDeleteButton();
                    return;
                }
                
                // Store page index for deferred deletion
                // Actual deletion happens in deleteConfirmed handler
                m_pendingDeletePageIndex = pageIndex;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Page Panel: Page" << pageIndex << "marked for deletion (5 sec to undo)";
#endif
            }
        }
    });
    
    // Delete confirmed (timeout elapsed): Actually perform the deletion
    connect(m_pagePanelActionBar, &PagePanelActionBar::deleteConfirmed, this, [this]() {
        if (m_pendingDeletePageIndex < 0) {
            return;  // No pending delete
        }
        
        DocumentViewport* vp = currentViewport();
        if (!vp) {
            m_pendingDeletePageIndex = -1;
            return;
        }
        
        Document* doc = vp->document();
        if (!doc) {
            m_pendingDeletePageIndex = -1;
            return;
        }
        
        // Verify the page still exists and is still valid to delete
        if (m_pendingDeletePageIndex >= doc->pageCount()) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Pending delete index" << m_pendingDeletePageIndex << "no longer valid";
#endif
            m_pendingDeletePageIndex = -1;
            return;
        }
        
        // Double-check PDF protection (page may have changed)
        Page* page = doc->page(m_pendingDeletePageIndex);
        if (page && page->backgroundType == Page::BackgroundType::PDF) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Cannot delete PDF page" << m_pendingDeletePageIndex;
#endif
            m_pendingDeletePageIndex = -1;
            return;
        }
        
        // Can't delete the last page
        if (doc->pageCount() <= 1) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Cannot delete last page";
#endif
            m_pendingDeletePageIndex = -1;
            return;
        }
                
                // Actually delete the page
        int deleteIndex = m_pendingDeletePageIndex;
        if (doc->removePage(deleteIndex)) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Page" << deleteIndex << "permanently deleted";
#endif
            vp->notifyDocumentStructureChanged();
            
            // Navigate to appropriate page
            int newPage = qMin(deleteIndex, doc->pageCount() - 1);
            vp->scrollToPage(newPage);
            
            // Update UI
            notifyPageStructureChanged(doc, newPage);
            
            // Mark tab as modified (page deleted)
            int tabIndex = m_tabManager->currentIndex();
            if (tabIndex >= 0) {
                m_tabManager->markTabModified(tabIndex, true);
            }
        } else {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Delete failed for page" << deleteIndex;
#endif
        }
        
        m_pendingDeletePageIndex = -1;
    });
    
    // Undo delete clicked: Cancel the pending deletion
    connect(m_pagePanelActionBar, &PagePanelActionBar::undoDeleteClicked, this, [this]() {
        if (m_pendingDeletePageIndex >= 0) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page Panel: Delete cancelled for page" << m_pendingDeletePageIndex;
#endif
        m_pendingDeletePageIndex = -1;
        }
    });
    
    // -------------------------------------------------------------------------
    // Visibility: Show only when Pages tab is selected
    // -------------------------------------------------------------------------
    
    // Connect to left sidebar tab changes
    if (m_leftSidebar) {
        connect(m_leftSidebar, &QTabWidget::currentChanged, this, [this](int) {
            // Task 5.4: Use helper function for consistent visibility logic
            updatePagePanelActionBarVisibility();
        });
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Page Panel: PagePanelActionBar connections initialized";
#endif
}

// =========================================================================
// Page Panel: Task 5.4: Action Bar Visibility Logic
// =========================================================================

void MainWindow::updatePagePanelActionBarVisibility()
{
    if (!m_pagePanelActionBar || !m_actionBarContainer) {
        return;
    }
    bool shouldShow = false;
    
    // Condition 1: Left sidebar must be visible
    // Condition 2: Pages tab must exist and be selected
    // Condition 3: Must be a paged document (not edgeless)
    if (m_leftSidebar && m_leftSidebar->isVisible() && m_leftSidebar->hasPagesTab()) {
        int pagesTabIndex = m_leftSidebar->indexOf(m_leftSidebar->pagePanel());
        if (m_leftSidebar->currentIndex() == pagesTabIndex) {
            if (DocumentViewport* vp = currentViewport()) {
                if (Document* doc = vp->document()) {
                    if (!doc->isEdgeless()) {
                        shouldShow = true;
                    }
                }
            }
        }
    }
    
    m_actionBarContainer->setPagePanelVisible(shouldShow);
    
    // Update action bar position after visibility change to ensure correct placement
    updateActionBarPosition();
    
    // Update action bar state when becoming visible
    if (shouldShow) {
        if (DocumentViewport* vp = currentViewport()) {
            if (Document* doc = vp->document()) {
                m_pagePanelActionBar->setPageCount(doc->pageCount());
                m_pagePanelActionBar->setCurrentPage(vp->currentPageIndex());
                m_pagePanelActionBar->setAutoLayoutEnabled(vp->autoLayoutEnabled());
            }
        }
    }
}

// =========================================================================
// Phase E.2: PDF Outline Panel Connections
// =========================================================================

void MainWindow::setupOutlinePanelConnections()
{
    if (!m_leftSidebar) {
        qWarning() << "setupOutlinePanelConnections: m_leftSidebar not yet created";
        return;
    }
    
    OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
    if (!outlinePanel) {
        qWarning() << "setupOutlinePanelConnections: OutlinePanel not available";
        return;
    }
    
    // Navigation: OutlinePanel → DocumentViewport
    connect(outlinePanel, &OutlinePanel::navigationRequested,
            this, [this](int pageIndex, QPointF position) {
        if (DocumentViewport* vp = currentViewport()) {
            // Position values of -1 mean "not specified"
            if (position.x() >= 0 || position.y() >= 0) {
                // Scroll to exact position within the page (PDF provides normalized coords)
                vp->scrollToPositionOnPage(pageIndex, position);
            } else {
                // No position specified - just scroll to the page top
                vp->scrollToPage(pageIndex);
            }
        }
    });
    
}

// =========================================================================
// Page Panel: Task 5.2: Page Panel Connections
// =========================================================================

void MainWindow::setupPagePanelConnections()
{
    if (!m_leftSidebar) {
        qWarning() << "setupPagePanelConnections: m_leftSidebar not yet created";
        return;
    }
    
    PagePanel* pagePanel = m_leftSidebar->pagePanel();
    if (!pagePanel) {
        qWarning() << "setupPagePanelConnections: PagePanel not available";
        return;
    }
    
    // Navigation: PagePanel → DocumentViewport
    // When user clicks a page thumbnail, navigate to that page
    connect(pagePanel, &PagePanel::pageClicked, this, [this](int pageIndex) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->scrollToPage(pageIndex);
        }
    });
    
    // Drag-and-Drop: PagePanel → Document
    // When user drops a page to reorder, call Document::movePage()
    connect(pagePanel, &PagePanel::pageDropped, this, [this](int fromIndex, int toIndex) {
        if (DocumentViewport* vp = currentViewport()) {
            if (Document* doc = vp->document()) {
                if (doc->movePage(fromIndex, toIndex)) {
                    // Refresh the viewport after page reorder
                    vp->update();
                    
                    // Update page panel to reflect new order
                    if (m_pagePanel) {
                        m_pagePanel->invalidateAllThumbnails();
                    }
                    
                    // Mark tab as modified (page order changed)
                    int tabIndex = m_tabManager->currentIndex();
                    if (tabIndex >= 0) {
                        m_tabManager->markTabModified(tabIndex, true);
                    }
                    
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Page Panel: Moved page" << fromIndex << "to" << toIndex;
#endif
                }
            }
        }
    });
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Page Panel: Connections initialized";
#endif
}

// REMOVED MW1.4: handleEdgeProximity(InkCanvas*, QPoint&) - InkCanvas obsolete

// Phase P.1: Extracted from LauncherWindow
MainWindow* MainWindow::findExistingMainWindow()
{
    // Find existing MainWindow among all top-level widgets
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        MainWindow *mainWindow = qobject_cast<MainWindow*>(widget);
        if (mainWindow) {
            return mainWindow;
        }
    }
    return nullptr;
}

// Phase P.1: Extracted from LauncherWindow
void MainWindow::preserveWindowState(QWidget* sourceWindow, bool isExistingWindow)
{
    if (!sourceWindow) return;
    
    if (isExistingWindow) {
        // For existing windows, just show without changing size/position
        if (isMaximized()) {
            showMaximized();
        } else if (isFullScreen()) {
            showFullScreen();
        } else {
            show();
        }
    } else {
        // For new windows, apply source window's state
        if (sourceWindow->isMaximized()) {
            showMaximized();
        } else if (sourceWindow->isFullScreen()) {
            showFullScreen();
        } else {
            resize(sourceWindow->size());
            move(sourceWindow->pos());
            show();
        }
    }
}

// BUG-MISC-001 FIX: returnToLauncher() removed - obsolete placeholder
// The active implementation is toggleLauncher() which handles smooth fade transitions
// between MainWindow and Launcher. See line ~4052.

QPixmap MainWindow::renderPage0Thumbnail(Document* doc)
{
    // Phase P.4.6: Render page-0 thumbnail for saving to NotebookLibrary
    if (!doc || doc->isEdgeless() || doc->pageCount() == 0) {
        return QPixmap();
    }
    
    // Target thumbnail size for launcher display
    static constexpr int THUMBNAIL_WIDTH = 180;
    static constexpr qreal MAX_DPR = 2.0;  // Cap at 2x for reasonable file size
    
    // Get page size from metadata
    QSizeF pageSize = doc->pageSizeAt(0);
    if (pageSize.isEmpty()) {
        pageSize = QSizeF(612, 792);  // Default US Letter
    }
    
    // Calculate dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailHeight = static_cast<int>(THUMBNAIL_WIDTH * aspectRatio);
    qreal dpr = qMin(devicePixelRatioF(), MAX_DPR);
    
    int physicalWidth = static_cast<int>(THUMBNAIL_WIDTH * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    // Create pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale factor
    qreal scale = static_cast<qreal>(THUMBNAIL_WIDTH) / pageSize.width();
    painter.scale(scale, scale);
    
    // Get the page (may trigger lazy load)
    Page* page = doc->page(0);
    if (!page) {
        qWarning() << "renderPage0Thumbnail: page(0) returned nullptr";
        painter.end();
        return thumbnail;  // Return white placeholder
    }
    
    // Defensive check: verify page has layers (should always have at least 1)
    int layerCount = page->layerCount();
    if (layerCount <= 0) {
        qWarning() << "renderPage0Thumbnail: page has no layers, skipping layer rendering";
    }
    
    // Render PDF background if available
    QPixmap pdfBackground;
    if (doc->isPdfLoaded() && page->pdfPageNumber >= 0) {
        qreal pdfDpi = (THUMBNAIL_WIDTH * dpr) / (pageSize.width() / 72.0);
        pdfDpi = qMin(pdfDpi, 150.0);  // Cap at 150 DPI
        
        QImage pdfImage = doc->renderPdfPageToImage(page->pdfPageNumber, pdfDpi);
        if (!pdfImage.isNull()) {
            pdfBackground = QPixmap::fromImage(pdfImage);
        }
    }
    
    // Render background
    page->renderBackground(painter, pdfBackground.isNull() ? nullptr : &pdfBackground, 1.0);
    
    // Render vector layers (with bounds check)
    for (int layerIdx = 0; layerIdx < layerCount; ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer && layer->visible) {
            layer->render(painter);
        }
    }
    
    // Render inserted objects
    page->renderObjects(painter, 1.0);
    
    painter.end();
    return thumbnail;
}

void MainWindow::toggleLauncher() {
    // Phase P.4.4: Toggle launcher visibility
    // Phase P.4.5: Smooth transition with fade animation
    
    // Find existing Launcher among top-level widgets
    QWidget* launcher = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("Launcher")) {
            launcher = widget;
            break;
        }
    }
    
    if (!launcher) {
        // No launcher exists - can't toggle
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "MainWindow::toggleLauncher: No launcher window found";
#endif
        return;
    }
    
    // Animation duration in milliseconds
    const int fadeDuration = 150;
    
    if (launcher->isVisible()) {
        // ========== LAUNCHER → MAINWINDOW ==========
        // Copy window geometry from launcher to this window BEFORE showing
        if (launcher->isMaximized()) {
            // Set geometry first, then show maximized
            setGeometry(launcher->geometry());
        } else if (launcher->isFullScreen()) {
            setGeometry(launcher->geometry());
        } else {
            setGeometry(launcher->geometry());
        }
        
        // Start MainWindow at opacity 0, show it, then fade in
        setWindowOpacity(0.0);
        if (launcher->isMaximized()) {
            showMaximized();
        } else if (launcher->isFullScreen()) {
            showFullScreen();
        } else {
            showNormal();
        }
        raise();
        activateWindow();
        
        // Hide launcher immediately (no flicker since MainWindow is now on top)
        launcher->hide();
        launcher->setWindowOpacity(1.0);  // Reset for next time
        
        // Fade MainWindow in
        QPropertyAnimation* fadeIn = new QPropertyAnimation(this, "windowOpacity");
        fadeIn->setDuration(fadeDuration);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        connect(fadeIn, &QPropertyAnimation::finished, fadeIn, &QObject::deleteLater);
        fadeIn->start();
        
    } else {
        // ========== MAINWINDOW → LAUNCHER ==========
        // Copy window geometry from this window to launcher BEFORE showing
        if (isMaximized()) {
            launcher->setGeometry(geometry());
        } else if (isFullScreen()) {
            launcher->setGeometry(geometry());
        } else {
            launcher->setGeometry(geometry());
        }
        
        // Start launcher at opacity 0, show it, then fade in
        launcher->setWindowOpacity(0.0);
        if (isMaximized()) {
            launcher->showMaximized();
        } else if (isFullScreen()) {
            launcher->showFullScreen();
        } else {
            launcher->showNormal();
        }
        launcher->raise();
        launcher->activateWindow();
        
        // Hide MainWindow immediately (no flicker since launcher is now on top)
        hide();
        setWindowOpacity(1.0);  // Reset for next time
        
        // Fade launcher in
        QPropertyAnimation* fadeIn = new QPropertyAnimation(launcher, "windowOpacity");
        fadeIn->setDuration(fadeDuration);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        connect(fadeIn, &QPropertyAnimation::finished, fadeIn, &QObject::deleteLater);
        fadeIn->start();
    }
}

void MainWindow::showAddMenu() {
    // Phase P.4.3: Show dropdown menu for new document options
    if (!m_navigationBar) {
        return;
    }
    
    QMenu menu(this);
    
    // New Edgeless Canvas
    QAction* newEdgelessAction = menu.addAction(tr("New Edgeless Canvas"));
    newEdgelessAction->setShortcut(ShortcutManager::instance()->keySequenceForAction("file.new_edgeless"));
    connect(newEdgelessAction, &QAction::triggered, this, &MainWindow::addNewEdgelessTab);
    
    // New Paged Notebook
    QAction* newPagedAction = menu.addAction(tr("New Paged Notebook"));
    newPagedAction->setShortcut(ShortcutManager::instance()->keySequenceForAction("file.new_paged"));
    connect(newPagedAction, &QAction::triggered, this, &MainWindow::addNewTab);
    
    // Separator
    menu.addSeparator();
    
    // Open PDF...
    QAction* openPdfAction = menu.addAction(tr("Open PDF..."));
    openPdfAction->setShortcut(ShortcutManager::instance()->keySequenceForAction("file.open_pdf"));
    connect(openPdfAction, &QAction::triggered, this, &MainWindow::showOpenPdfDialog);
    
    // Open Notebook...
    QAction* openNotebookAction = menu.addAction(tr("Open Notebook..."));
    openNotebookAction->setShortcut(ShortcutManager::instance()->keySequenceForAction("file.open_notebook"));
    connect(openNotebookAction, &QAction::triggered, this, &MainWindow::loadFolderDocument);
    
    // Position menu below the add button
    QWidget* addButton = m_navigationBar->addButton();
    if (addButton) {
        QPoint buttonPos = addButton->mapToGlobal(QPoint(0, addButton->height()));
        menu.exec(buttonPos);
    } else {
        menu.exec(QCursor::pos());
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    
    // BUG-AB-001/UI-001 FIX: Update toolbar positions on window resize
    // This catches maximize/restore events that might not trigger canvas container resize
    updateSubToolbarPosition();
    updateActionBarPosition();
    updatePdfSearchBarPosition();
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
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Auto layout not available for edgeless canvas";
#endif
        return;
    }
    
    bool newState = !viewport->autoLayoutEnabled();
    viewport->setAutoLayoutEnabled(newState);
    
    // Show status feedback via debug console
#ifdef SPEEDYNOTE_DEBUG
    if (newState) {
        qDebug() << "Auto layout enabled (1/2 columns)";
    } else {
        qDebug() << "Single column layout";
    }
#endif
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
    
    // Sync NavigationBar button state when sidebar is toggled programmatically
    if (m_navigationBar) {
        m_navigationBar->setRightSidebarChecked(markdownNotesSidebarVisible);
    }
    
    // Phase M.3: Load notes when sidebar becomes visible
    if (markdownNotesSidebarVisible) {
        markdownNotesSidebar->loadNotesForPage(loadNotesForCurrentPage());
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
    
    // Update action bar position after sidebar visibility change
    updateActionBarPosition();
    
    // Reposition floating tabs after layout settles
    QTimer::singleShot(0, this, [this]() {
        // REMOVED S1: positionLeftSidebarTabs() removed - floating tabs replaced by LeftSidebarContainer
        // MW2.2: Removed dial container positioning
    });
}

// Phase M.3: Load markdown notes for current page from LinkObjects
QList<NoteDisplayData> MainWindow::loadNotesForCurrentPage()
{
    QList<NoteDisplayData> results;
    
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return results;
    
    Document* doc = vp->document();
    QString notesDir = doc->notesPath();
    if (notesDir.isEmpty()) return results;
    
    // Helper lambda to extract notes from a page/tile
    auto extractNotesFromPage = [&](Page* page) {
        if (!page) return;
        
        for (const auto& objPtr : page->objects) {
            LinkObject* link = dynamic_cast<LinkObject*>(objPtr.get());
            if (!link) continue;
            
            // Check each slot for markdown type
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                const LinkSlot& slot = link->linkSlots[i];
                if (slot.type != LinkSlot::Type::Markdown) continue;
                
                // Load the note file
                QString filePath = notesDir + "/" + slot.markdownNoteId + ".md";
                MarkdownNote note = MarkdownNote::loadFromFile(filePath);
                
                if (!note.isValid()) continue;  // File not found
                
                // Build display data
                NoteDisplayData displayData;
                displayData.noteId = note.id;
                displayData.title = note.title;
                displayData.content = note.content;
                displayData.linkObjectId = link->id;
                displayData.color = link->iconColor;
                displayData.description = link->description;
                
                results.append(displayData);
            }
        }
    };
    
    if (doc->isEdgeless()) {
        // Edgeless mode: iterate through all loaded tiles
        for (const auto& coord : doc->allLoadedTileCoords()) {
            Page* tile = doc->getTile(coord.first, coord.second);
            extractNotesFromPage(tile);
        }
        
        // M.7.2: Update hidden tiles warning
        int loadedCount = doc->tileCount();
        int totalCount = doc->tileIndexCount();
        if (markdownNotesSidebar) {
            markdownNotesSidebar->setHiddenTilesWarning(loadedCount < totalCount, loadedCount, totalCount);
        }
    } else {
        // Paged mode: use current page
        int pageIndex = vp->currentPageIndex();
        Page* page = doc->page(pageIndex);
        extractNotesFromPage(page);
        
        // M.7.2: Hide warning for paged mode
        if (markdownNotesSidebar) {
            markdownNotesSidebar->setHiddenTilesWarning(false, 0, 0);
        }
    }
    
    return results;
}

// Phase M.3: Navigate to and select a LinkObject
// Phase M.7.1: Added edgeless mode support
void MainWindow::navigateToLinkObject(const QString& linkObjectId)
{
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return;
    
    Document* doc = vp->document();
    InsertedObject* foundObject = nullptr;
    
    if (doc->isEdgeless()) {
        // Edgeless mode: search through loaded tiles
        int foundTileX = 0;
        int foundTileY = 0;
        
        for (const auto& coord : doc->allLoadedTileCoords()) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            for (const auto& objPtr : tile->objects) {
                if (objPtr->id == linkObjectId) {
                    foundObject = objPtr.get();
                    foundTileX = coord.first;
                    foundTileY = coord.second;
                    break;
                }
            }
            if (foundObject) break;
        }
        
        if (!foundObject) {
            qWarning() << "navigateToLinkObject: LinkObject not found in loaded tiles:" << linkObjectId;
            return;
        }
        
        // Calculate document-global position (tile origin + object position)
        QPointF tileOrigin(foundTileX * Document::EDGELESS_TILE_SIZE,
                           foundTileY * Document::EDGELESS_TILE_SIZE);
        QPointF objectCenter = tileOrigin + foundObject->position + 
            QPointF(foundObject->size.width() / 2.0, foundObject->size.height() / 2.0);
        
        // Navigate to the object position (reuses back-link navigation)
        vp->navigateToEdgelessPosition(foundTileX, foundTileY, objectCenter);
        
        // Select the object
        vp->selectObject(foundObject);
        
    } else {
        // Paged mode: search through pages
        int currentPage = vp->currentPageIndex();
        int foundPageIndex = -1;
        
        // Helper lambda to search a page
        auto searchPage = [&](int pageIdx) -> bool {
            Page* page = doc->page(pageIdx);
            if (!page) return false;
            
            for (const auto& objPtr : page->objects) {
                if (objPtr->id == linkObjectId) {
                    foundObject = objPtr.get();
                    foundPageIndex = pageIdx;
                    return true;
                }
            }
            return false;
        };
        
        // Search current page first
        if (!searchPage(currentPage)) {
            // Not on current page - search all pages
            for (int pageIdx = 0; pageIdx < doc->pageCount(); ++pageIdx) {
                if (pageIdx == currentPage) continue;  // Already checked
                if (searchPage(pageIdx)) break;
            }
        }
        
        if (!foundObject) {
            qWarning() << "navigateToLinkObject: LinkObject not found:" << linkObjectId;
            return;
        }
        
        // Navigate to page if needed
        if (foundPageIndex != currentPage) {
            vp->scrollToPage(foundPageIndex);
        }
        
        // Calculate object center and convert to normalized coordinates for scrolling
        QSizeF pageSize = doc->pageSizeAt(foundPageIndex);
        if (pageSize.width() > 0 && pageSize.height() > 0) {
            QPointF objectCenter = foundObject->position + 
                QPointF(foundObject->size.width() / 2.0, foundObject->size.height() / 2.0);
            QPointF normalizedPos(
                objectCenter.x() / pageSize.width(),
                objectCenter.y() / pageSize.height()
            );
            vp->scrollToPositionOnPage(foundPageIndex, normalizedPos);
        }
        
        // Select the object (this will show slot buttons in subtoolbar)
        vp->selectObject(foundObject);
    }
}

// Phase M.4: Search markdown notes across pages
// Optimizations applied:
//   A. Two-tier search: check description first (in memory), load file only if needed
//   B. Result limiting: stop after MAX_SEARCH_RESULTS
//   C. (Future) Cancel flag for long searches
//   D. (Connected below) Background thread via QtConcurrent

static const int MAX_SEARCH_RESULTS = 100;  // Optimization B: Cap results

QList<NoteDisplayData> MainWindow::searchMarkdownNotes(const QString& query, int fromPage, int toPage)
{
    struct ScoredNote {
        NoteDisplayData data;
        int score;
    };
    
    QList<ScoredNote> results;
    
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return {};
    
    Document* doc = vp->document();
    QString notesDir = doc->notesPath();
    if (notesDir.isEmpty()) return {};
    
    bool reachedLimit = false;
    int tilesSearched = 0;
    
    // Helper lambda to search a page/tile for notes matching query
    auto searchPage = [&](Page* page) {
        if (!page || reachedLimit) return;
        
        for (const auto& objPtr : page->objects) {
            if (reachedLimit) break;
            
            LinkObject* link = dynamic_cast<LinkObject*>(objPtr.get());
            if (!link) continue;
            
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                const LinkSlot& slot = link->linkSlots[i];
                if (slot.type != LinkSlot::Type::Markdown) continue;
                
                // Optimization A: Two-tier search
                // Tier 1: Check description first (already in memory - no file I/O)
                int score = 0;
                bool descriptionMatch = link->description.contains(query, Qt::CaseInsensitive);
                if (descriptionMatch) {
                    score += 100;  // Description match highest priority
                }
                
                // Tier 2: Load file for title/content matching
                QString filePath = notesDir + "/" + slot.markdownNoteId + ".md";
                MarkdownNote note = MarkdownNote::loadFromFile(filePath);
                if (!note.isValid()) continue;
                
                // Check title and content
                if (note.title.contains(query, Qt::CaseInsensitive)) {
                    score += 75;   // Title match
                }
                if (note.content.contains(query, Qt::CaseInsensitive)) {
                    score += 50;   // Content match
                }
                
                if (score > 0) {
                    NoteDisplayData displayData;
                    displayData.noteId = note.id;
                    displayData.title = note.title;
                    displayData.content = note.content;
                    displayData.linkObjectId = link->id;
                    displayData.color = link->iconColor;
                    displayData.description = link->description;
                    
                    results.append({displayData, score});
                    
                    // Optimization B: Stop after reaching limit
                    if (results.size() >= MAX_SEARCH_RESULTS) {
                        reachedLimit = true;
                        break;
                    }
                }
            }
        }
    };
    
    if (doc->isEdgeless()) {
        // Edgeless mode: search all loaded tiles (page range is ignored)
        for (const auto& coord : doc->allLoadedTileCoords()) {
            if (reachedLimit) break;
            
            // Optimization D: Process events periodically
            if (++tilesSearched % 10 == 0) {
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            
            Page* tile = doc->getTile(coord.first, coord.second);
            searchPage(tile);
        }
    } else {
        // Paged mode: search within page range
        fromPage = qMax(0, fromPage);
        toPage = qMin(toPage, doc->pageCount() - 1);
        
        for (int pageIdx = fromPage; pageIdx <= toPage && !reachedLimit; ++pageIdx) {
            // Optimization D: Process events periodically
            if (++tilesSearched % 10 == 0) {
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            
            Page* page = doc->page(pageIdx);
            searchPage(page);
        }
    }
    
    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const ScoredNote& a, const ScoredNote& b) {
                  return a.score > b.score;
              });
    
    // Extract sorted data
    QList<NoteDisplayData> output;
    output.reserve(results.size());
    for (const ScoredNote& item : results) {
        output.append(item.data);
    }
    return output;
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



#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
// MW2.2: reconnectControllerSignals simplified - dial system removed
void MainWindow::reconnectControllerSignals() {
    if (!controllerManager) {
        return;
    }
    
    // Disconnect all existing connections to avoid duplicates
    disconnect(controllerManager, nullptr, this, nullptr);
}
#endif // SPEEDYNOTE_CONTROLLER_SUPPORT

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
    // ========== UPDATE POSITIONS FOR ALL DOCUMENTS ==========
    // Before checking for unsaved changes, update positions for all documents
    // This ensures the position is saved even if the document was saved earlier in the session
    if (m_tabManager && m_documentManager) {
        for (int i = 0; i < m_tabManager->tabCount(); ++i) {
            Document* doc = m_tabManager->documentAt(i);
            if (!doc) continue;
            
            DocumentViewport* vp = m_tabManager->viewportAt(i);
            if (!vp) continue;
            
            if (doc->isEdgeless()) {
                // Phase 4: Sync edgeless position before app exit
                vp->syncPositionToDocument();
                doc->markModified();
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "closeEvent: Synced edgeless position for" << doc->displayName();
#endif
            } else {
                // Paged: update lastAccessedPage
                int currentPage = vp->currentPageIndex();
                if (doc->lastAccessedPage != currentPage) {
                    doc->lastAccessedPage = currentPage;
                    // Mark as needing save for this metadata update
                    doc->markModified();
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "closeEvent: Updated lastAccessedPage to" << currentPage << "for" << doc->displayName();
#endif
                }
            }
        }
    }
    
    // ========== CHECK FOR UNSAVED DOCUMENTS ==========
    // Iterate through all tabs and prompt for unsaved documents
    if (m_tabManager && m_documentManager) {
        for (int i = 0; i < m_tabManager->tabCount(); ++i) {
            Document* doc = m_tabManager->documentAt(i);
            if (!doc) continue;
            
            // Check if this document has unsaved changes
            bool needsSavePrompt = false;
            bool isUsingTemp = m_documentManager->isUsingTempBundle(doc);
            
            if (doc->isEdgeless()) {
                // Edgeless: check if it has tiles and is in temp bundle
                bool hasContent = doc->tileCount() > 0 || doc->tileIndexCount() > 0;
                needsSavePrompt = isUsingTemp && hasContent;
            } else {
                // Paged: check if modified OR (in temp bundle with pages)
                bool hasContent = doc->pageCount() > 0;
                needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
            }
            
            if (needsSavePrompt) {
                // Switch to this tab so user knows which document we're asking about
                if (m_tabBar) {
                    m_tabBar->setCurrentIndex(i);
                }
                
                QString docType = doc->isEdgeless() ? tr("canvas") : tr("document");
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    tr("Save Changes?"),
                    tr("The %1 \"%2\" has unsaved changes. Do you want to save before quitting?")
                        .arg(docType)
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
                    // Note: lastAccessedPage was already updated in the loop at the start of closeEvent()
                    
                    // Check if document already has a permanent save path
                    QString existingPath = m_documentManager->documentPath(doc);
                    bool canSaveInPlace = !existingPath.isEmpty() && !isUsingTemp;
                    
                    if (canSaveInPlace) {
                        // Save in-place to existing location
                        if (!m_documentManager->saveDocument(doc)) {
                            QMessageBox::critical(this, tr("Save Error"),
                                tr("Failed to save document to:\n%1\n\nQuit anyway?").arg(existingPath));
                            // Don't abort - let them quit without saving if save failed
                        }
                    } else {
                        // New document - use Android-aware save dialog
                        if (!saveNewDocumentWithDialog(doc)) {
                            // User cancelled save dialog - abort quit
                            event->ignore();
                            return;
                        }
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

// ========================================
// Single Instance Implementation
// ========================================

bool MainWindow::isInstanceRunning()
{
#ifdef Q_OS_ANDROID
    // Android handles app lifecycle differently - always return false
    return false;
#else
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
#endif // !Q_OS_ANDROID
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
#ifndef Q_OS_ANDROID
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
#endif
    
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

bool MainWindow::closeDocumentById(const QString& documentId)
{
    // Find the document by ID among open tabs
    if (!m_tabManager) {
        return true;  // No tabs, nothing to close
    }
    
    for (int i = 0; i < m_tabManager->tabCount(); ++i) {
        Document* doc = m_tabManager->documentAt(i);
        if (doc && doc->id == documentId) {
            // Found the document - save if modified, then close
            if (m_documentManager && m_documentManager->hasUnsavedChanges(doc)) {
                QString existingPath = m_documentManager->documentPath(doc);
                if (!existingPath.isEmpty()) {
                    // Has existing path - save in place
                    if (!m_documentManager->saveDocument(doc)) {
                        QMessageBox::critical(this, tr("Save Error"),
                            tr("Failed to save document before closing."));
                        return false;
                    }
                } else {
                    // No path - use Android-aware save dialog
                    if (!saveNewDocumentWithDialog(doc)) {
                        return false;  // User cancelled or save failed
                    }
                }
            }
            
            // Close the tab
            removeTabAt(i);
            return true;
        }
    }
    
    return true;  // Document not found = nothing to close = success
}

void MainWindow::openFileInNewTab(const QString &filePath)
{
    // ==========================================================================
    // SINGLE SOURCE OF TRUTH for opening documents
    // ==========================================================================
    // This is THE implementation for opening any document type into a new tab.
    // All entry points (Launcher, "+" menu, shortcuts, command line) should
    // call this function to ensure consistent behavior.
    //
    // Handles: PDFs, .snb bundles
    // Performs: Load → Create Tab → Switch → Position (mode-specific)
    // ==========================================================================
    
    if (filePath.isEmpty()) {
        return;
    }
    
    if (!m_documentManager || !m_tabManager) {
        qWarning() << "openFileInNewTab: DocumentManager or TabManager not initialized";
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, tr("File Not Found"),
            tr("The file does not exist:\n%1").arg(filePath));
        return;
    }
    
    // Step 0: Check for duplicate documents (by ID, not path)
    // This handles the case where a document was renamed in the Launcher
    // but is still open in a tab. Without this check, we'd open a second tab.
    QString suffix = fileInfo.suffix().toLower();
    if (suffix == "snb" || fileInfo.isDir()) {
        QString docId = Document::peekBundleId(filePath);
        if (!docId.isEmpty()) {
            for (int i = 0; i < m_tabManager->tabCount(); ++i) {
                Document* existingDoc = m_tabManager->documentAt(i);
                if (existingDoc && existingDoc->id == docId) {
                    // Document is already open - switch to that tab
                    if (m_tabBar) {
                        m_tabBar->setCurrentIndex(i);
                    }
                    // Update the document path in case it was renamed
                    // This keeps DocumentManager's path tracking in sync
                    m_documentManager->setDocumentPath(existingDoc, filePath);
                    return;
                }
            }
        }
    }
    
    // Step 1: Load document via DocumentManager
    // DocumentManager handles all file types and manages document lifecycle
    Document* doc = m_documentManager->loadDocument(filePath);
    if (!doc) {
        QMessageBox::critical(this, tr("Open Error"),
            tr("Failed to open file:\n%1").arg(filePath));
        return;
    }
    
    // Step 2: Set document name from file/folder if not already set
    if (doc->name.isEmpty()) {
        doc->name = fileInfo.baseName();
        // Remove .snb suffix if present
        if (doc->name.endsWith(".snb", Qt::CaseInsensitive)) {
            doc->name.chop(4);
        }
    }
    
    // Step 3: Create new tab (TabManager creates DocumentViewport internally)
    int tabIndex = m_tabManager->createTab(doc, doc->displayName());
    
    if (tabIndex < 0) {
        QMessageBox::critical(this, tr("Open Error"),
            tr("Failed to create tab for:\n%1").arg(filePath));
        return;
    }
    
    // Step 4: Switch to the new tab
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(tabIndex);
    }
    
    // Step 5: Mode-specific initial positioning
    // Use QTimer::singleShot(0) to ensure viewport geometry is ready
    bool isEdgeless = doc->isEdgeless();
    if (isEdgeless) {
        // Edgeless: Center on origin (offset by a small margin)
        QTimer::singleShot(0, this, [this, tabIndex]() {
            if (m_tabManager) {
                DocumentViewport* viewport = m_tabManager->viewportAt(tabIndex);
                if (viewport) {
                    viewport->setPanOffset(QPointF(-100, -100));
                }
            }
        });
    } else {
        // Paged: Center content horizontally within the viewport
        centerViewportContent(tabIndex);
    }
    /*
    // Step 6: Log success
    if (isEdgeless) {
        qDebug() << "openFileInNewTab: Opened edgeless canvas with" 
                 << doc->tileIndexCount() << "tiles indexed from" << filePath;
    } else {
        qDebug() << "openFileInNewTab: Opened paged document with" 
                 << doc->pageCount() << "pages from" << filePath;
    }
    */
}

void MainWindow::showOpenPdfDialog()
{
    // Phase P.4: Public wrapper for opening PDF via file dialog
    // Calls the internal openPdfDocument() which shows a file dialog
    openPdfDocument();
}

// ========== Phase P.4.2: Launcher Interface Methods ==========

bool MainWindow::hasOpenDocuments() const
{
    if (!m_tabManager) {
        return false;
    }
    return m_tabManager->tabCount() > 0;
}

bool MainWindow::switchToDocument(const QString& bundlePath)
{
    if (bundlePath.isEmpty() || !m_tabManager || !m_documentManager) {
        return false;
    }
    
    // Normalize path for comparison
    QString normalizedPath = QFileInfo(bundlePath).absoluteFilePath();
    
    // Search through all open tabs for a matching document path
    int tabCount = m_tabManager->tabCount();
    for (int i = 0; i < tabCount; ++i) {
        Document* doc = m_tabManager->documentAt(i);
        if (!doc) continue;
        
        QString docPath = m_documentManager->documentPath(doc);
        if (docPath.isEmpty()) continue;
        
        // Normalize and compare
        QString normalizedDocPath = QFileInfo(docPath).absoluteFilePath();
        if (normalizedDocPath == normalizedPath) {
            // Found it - switch to this tab
            if (m_tabBar) {
                m_tabBar->setCurrentIndex(i);
            }
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "MainWindow::switchToDocument: Switched to existing tab for" << bundlePath;
#endif
            return true;
        }
    }
    
    return false;
}

void MainWindow::bringToFront()
{
    // Phase P.4.5: Fade in if window was hidden
    bool wasHidden = !isVisible();
    
    if (wasHidden) {
        // Start with opacity 0 and animate to 1
        setWindowOpacity(0.0);
    }
    
    show();
    raise();
    activateWindow();
    
    if (wasHidden) {
        // Fade in animation
        QPropertyAnimation* fadeIn = new QPropertyAnimation(this, "windowOpacity");
        fadeIn->setDuration(150);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        connect(fadeIn, &QPropertyAnimation::finished, fadeIn, &QObject::deleteLater);
        fadeIn->start();
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
