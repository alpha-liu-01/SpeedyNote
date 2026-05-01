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
#include "ui/subtoolbars/PenSubToolbar.h"
#include "ui/subtoolbars/MarkerSubToolbar.h"
#include "ui/subtoolbars/HighlighterSubToolbar.h"
#include "ui/subtoolbars/ObjectSelectSubToolbar.h"
#include "ui/subtoolbars/EraserSubToolbar.h"
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
#include "ui/widgets/PdfSearchBar.h"  // PDF text search bar
#include "pdf/PdfSearchEngine.h"      // PDF text search engine
#include "ocr/OcrWorker.h"            // OCR background worker
#include "objects/OcrTextObject.h"     // OCR text objects (Phase 1D)
#include "ui/subtoolbars/OcrSubToolbar.h"  // OCR subtoolbar
#include "ui/panels/FloatingTextEditor.h"  // Phase 2B: Floating text editor
#include "objects/TextBoxObject.h"         // Phase 2B: TextBoxObject for editor
#include <QMetaObject>                 // OCR queued invocations
#include "ui/dialogs/BatchPdfExportDialog.h"   // Phase 3: Unified PDF export dialog
#include "ui/dialogs/BatchSnbxExportDialog.h"  // Phase 3: Unified SNBX export dialog
#include "pdf/MuPdfExporter.h"                 // Phase 8: PDF export engine
#include <QClipboard>  // For clipboard signal connection
#include <algorithm>   // Phase M.4: For std::sort in searchMarkdownNotes
#include <cmath>       // For std::floor in renderEdgelessThumbnail
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScreen>
#include <QApplication>
#ifdef Q_OS_WIN
#include <windows.h>  // For MSG struct in nativeEvent (theme change detection)
#endif
#include <QGuiApplication>
#include <QLineEdit>
#include <QTextBrowser>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include "core/ToolType.h" // Include the header file where ToolType is defined
#include "ui/SplitViewManager.h"
#include "ui/TabManager.h"
#include "ui/TabBar.h"
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
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDialog>
#include <QDebug>
#include <QInputMethod>
#include <QPropertyAnimation>  // Phase P.4.5: Smooth window transitions
#include <QWindow>             // For windowHandle()->setWindowState() in transitions
#include <QInputMethodEvent>
#include <QLocale>
#include <QSet>
#include <QWheelEvent>
#include <QTimer>
#include <QSplitter>
#include <QShortcut>  // Phase doc-1: Application-wide keyboard shortcuts
#include "core/ShortcutManager.h"  // Keyboard shortcut hub
#include "compat/qt_compat.h"  // Qt5/Qt6 input device shims
#include <QColorDialog>  // Phase 3.1.8: For custom color picker
#include <QProcess>
#include <QLocalSocket>  // For single-instance server communication
#include <QFileInfo>
#include <QFile>
#include <QMimeData>
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

#elif defined(Q_OS_IOS)
#include "ui/dialogs/SaveDocumentDialog.h"
#include "ios/PdfPickerIOS.h"
#include "ios/IOSShareHelper.h"
#include "ios/IOSPlatformHelper.h"

#endif // Q_OS_ANDROID / Q_OS_IOS
// #include "HandwritingLineEdit.h"
#include "ControlPanelDialog.h"  // Phase CP.1: Re-enabled with cleaned up tabs
#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
#include "SDLControllerManager.h"
#endif
// #include "LauncherWindow.h" // Phase 3.1: Disconnected - LauncherWindow will be re-linked later

// #include "DocumentConverter.h" // Added for PowerPoint conversion

// Linux-specific includes for signal handling
#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Static member definition for single instance
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
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

// ----------------------------------------------------------------------------
// CJK detection for the OCR language tag stored on the document / global setting
//
// Why this is non-trivial: different OCR backends report languages in different
// string shapes.
//
//   * ML Kit (Android, macOS) reports BCP-47 tags: "zh-Hani-CN", "ja", "ko-KR", ...
//   * Windows Ink reports the localized display name of each InkRecognizer, e.g.
//       "Microsoft Chinese (Simplified) Handwriting Recognizer"   (EN Windows)
//       "Microsoft 中文（简体）手写识别器"                         (ZH Windows)
//       "Microsoft Japanese Handwriting Recognizer"
//       "Microsoft 日本語手書き認識エンジン"                       (JP Windows)
//       "Microsoft 日语手写识别器"                                 (JP on ZH Windows)
//       "Microsoft 한국어 필기 인식기"                             (KO Windows)
//
// The original check only looked for BCP-47 prefixes ("zh"/"ja"/"ko"), which
// meant the CJK grid-mode override never fired on Windows: a tag like
// "Microsoft 中文（简体）手写识别器" starts with "Microsoft", not "zh".
//
// This helper handles both formats by combining:
//   1. A BCP-47 pattern "^(zh|ja|ko)([-_].*)?$" (word-boundary on the subtag).
//   2. A set of CJK marker substrings that appear in localized recognizer names
//      across the common Windows UI languages (EN / ZH / JA / KO).
// ----------------------------------------------------------------------------
static bool isCjkOcrLanguage(const QString& lang)
{
    // Empty or explicit "auto" = unknown. Preserve the user's global toggle
    // (the caller has already verified ocrCjkGridMode is enabled), so return
    // true here to keep the previous behaviour for system-default languages.
    if (lang.isEmpty() || lang.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0)
        return true;

    // BCP-47: "zh", "ja", "ko", optionally followed by script/region subtags.
    static const QRegularExpression kBcp47Cjk(
        QStringLiteral("^(zh|ja|ko)(?:[-_].*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (kBcp47Cjk.match(lang).hasMatch())
        return true;

    // Localized InkRecognizer display names. Covers the common display-language
    // permutations; the English tokens cover EN Windows, the CJK tokens cover
    // ZH / JA / KO Windows regardless of which recognizer's name we're reading.
    static const QString kCjkMarkers[] = {
        // English tokens (EN Windows UI)
        QStringLiteral("Chinese"),
        QStringLiteral("Japanese"),
        QStringLiteral("Korean"),
        // Chinese tokens
        QStringLiteral("中文"),    // Chinese (ZH name)
        QStringLiteral("中国"),    // China / Chinese
        QStringLiteral("日语"),    // Japanese (ZH name)
        QStringLiteral("韩国"),    // Korea (ZH name, simplified)
        QStringLiteral("韓国"),    // Korea (JP / traditional)
        // Japanese / Korean tokens
        QStringLiteral("日本"),    // Japan / Japanese (JP & ZH)
        QStringLiteral("한국"),    // Korea / Korean (KO)
        QStringLiteral("한글"),    // Hangul script (KO)
        QStringLiteral("조선"),    // Korean (alt.)
    };
    for (const auto& marker : kCjkMarkers) {
        if (lang.contains(marker, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

MainWindow::MainWindow(QWidget *parent) 
    : QMainWindow(parent), localServer(nullptr) {

    setWindowTitle(tr("SpeedyNote %1").arg(APP_VERSION));
    
    // Phase 3.1: Always using new DocumentViewport architecture

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Setup signal handlers for proper cleanup on Linux (not Android)
    setupLinuxSignalHandlers();
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // On Android/iOS, auto-save all modified documents when app goes to background
    // This is critical because the app may be killed without closeEvent()
    // when user swipes from recents. Without this:
    // 1. Unsaved changes would be lost
    // 2. New documents wouldn't appear in Launcher
    // 
    // Note: This connect is set up early in constructor, but m_documentManager
    // is initialized just after. The lambda captures 'this' and checks for null.
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, [this](Qt::ApplicationState state) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MainWindow] Application state changed to:" 
                 << (state == Qt::ApplicationActive ? "Active" :
                     state == Qt::ApplicationSuspended ? "Suspended" :
                     state == Qt::ApplicationInactive ? "Inactive" : "Hidden");
#endif
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationInactive) {
            // Sync positions for all documents before auto-save
            // This ensures lastAccessedPage/edgeless position is saved
            syncAllDocumentPositions();
            
            if (m_documentManager) {
                // autoSaveModifiedDocuments() also saves NotebookLibrary internally
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[MainWindow] Triggering auto-save, document count:" << m_documentManager->documentCount();
#endif
                int saved = m_documentManager->autoSaveModifiedDocuments();
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[MainWindow] Auto-saved" << saved << "documents";
#endif
            } else {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[MainWindow] m_documentManager is null, only saving NotebookLibrary";
#endif
                // Fallback: save NotebookLibrary directly if DocumentManager not ready
                NotebookLibrary::instance()->save();
            }
        }
    });
#endif

    // Enable IME support for multi-language input
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    setAcceptDrops(true);
#endif

    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    

    // ✅ Get screen size & adjust window size
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize logicalSize = screen->availableGeometry().size() * 0.89;
        resize(logicalSize);
    }
    // Phase SV: SplitViewManager owns tab bars, viewport stacks, and TabManagers
    m_splitViewManager = new SplitViewManager(this);

    // Phase 3.1.1: Initialize DocumentManager
    m_documentManager = new DocumentManager(this);
    
    // Connect SplitViewManager signals (routes through active pane)
    connect(m_splitViewManager, &SplitViewManager::activeViewportChanged, this, [this](DocumentViewport* vp) {
        // Smart tool auto-switch: consume override when user activates the overridden viewport
        if (m_toolOverrideViewport == vp) {
            m_toolOverrideViewport = nullptr;
        }
        
        // Clear pan hold when viewport changes - revert old viewport's tool
        if (m_panHoldActive) {
            if (m_connectedViewport) {
                m_connectedViewport->setCurrentTool(m_toolBeforePanHold);
            }
            m_panHoldActive = false;
        }
        
        // Phase 6.1: Hide PDF search bar when switching tabs to prevent stale state
        if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
            hidePdfSearchBar();
        }
        
        // Save/restore left sidebar tab selection per document tab
        // IMPORTANT: Must be FIRST, before updatePagePanelForViewport() which modifies sidebar tabs
        int newTabId = tabManager() ? tabManager()->currentTabId() : -1;
        if (m_leftSidebar && newTabId != m_previousTabId) {
            // Save current sidebar tab for previous document tab
            if (m_previousTabId >= 0) {
                m_sidebarTabStates[m_previousTabId] = m_leftSidebar->currentIndex();
            }
        }
        
        // Task 7.2: Save PagePanel scroll position for previous document tab
        // MUST be before updatePagePanelForViewport() which resets scroll via setDocument()
        if (m_pagePanel && m_previousTabId >= 0 && newTabId != m_previousTabId) {
            m_pagePanel->saveTabState(m_previousTabId);
        }
        
        // Phase 3.3: Connect scroll signals from current viewport
        connectViewportScrollSignals(vp);
        // REMOVED MW7.2: updateDialDisplay removed - dial functionality deleted
        
        // Sync viewport dark mode with current theme
        if (vp) {
            vp->setDarkMode(isDarkMode());
            QSettings s("SpeedyNote", "App");
            vp->setPdfDarkModeEnabled(s.value("display/pdfDarkMode", true).toBool());
            vp->setSkipImageMasking(s.value("display/skipImageMasking", false).toBool());
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
            TouchGestureMode effectiveMode = touchGestureMode;
#ifdef Q_OS_LINUX
            // If palm rejection is currently active, keep touch disabled on new viewport
            if (m_palmRejectionActive && effectiveMode != TouchGestureMode::Disabled) {
                effectiveMode = TouchGestureMode::Disabled;
            }
#endif
            vp->setTouchGestureMode(effectiveMode);
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
        if (m_leftSidebar && newTabId != m_previousTabId) {
            if (m_sidebarTabStates.contains(newTabId)) {
                m_leftSidebar->setCurrentIndex(m_sidebarTabStates[newTabId]);
            }
        }
        
        // Task 7.2: Restore PagePanel scroll position for new document tab
        // MUST be after updatePagePanelForViewport() which sets the new document
        if (m_pagePanel && newTabId != m_previousTabId) {
            m_pagePanel->restoreTabState(newTabId);
        }
    });

    // Smart tool auto-switch: clear override when split view closes
    connect(m_splitViewManager, &SplitViewManager::splitStateChanged, this, [this](bool isSplit) {
        if (!isSplit) clearToolOverride(false);
    });

    // Auto-hide the tab bar container when only one notebook is open.
    // The filename click in NavigationBar still toggles visibility as a
    // manual override (last interaction wins until the next count transition).
    if (QWidget* tbc = m_splitViewManager->tabBarContainer()) {
        tbc->setVisible(false);
    }
    connect(m_splitViewManager, &SplitViewManager::totalTabCountChanged, this, [this](int total) {
        if (QWidget* tbc = m_splitViewManager ? m_splitViewManager->tabBarContainer() : nullptr) {
            tbc->setVisible(total >= 2);
        }
    });

    // ML-1 FIX: Connect tabCloseRequested to clean up Document when tab closes
    // SplitViewManager forwards this from both panes with the unique tab ID
    connect(m_splitViewManager, &SplitViewManager::tabCloseRequested, this, [this](int tabId, DocumentViewport* vp, SplitViewManager::Pane) {
        // Phase 6.2: Cancel search if the document being closed has an active search
        if (vp && m_searchEngine && vp == currentViewport()) {
            if (m_pdfSearchBar && m_pdfSearchBar->isVisible()) {
                hidePdfSearchBar();  // This also cancels and clears the cache
            }
        }

        // Clean up subtoolbar per-tab state to prevent memory leak
        if (m_toolbar && tabId >= 0) {
            m_toolbar->clearTabState(tabId);
        }
        
        // Task 7.2: Clean up PagePanel scroll state for closed tab
        if (m_pagePanel && tabId >= 0) {
            m_pagePanel->clearTabState(tabId);
        }
        
        // Clean up sidebar tab state for closed tab
        if (tabId >= 0) {
            m_sidebarTabStates.remove(tabId);
        }
        
        if (vp && m_documentManager) {
            Document* doc = vp->document();
            if (doc) {
                // Phase P.4.6: Save page-0 thumbnail to NotebookLibrary before closing
                // Only for paged documents that have been saved (have a bundle path)
                QString bundlePath = m_documentManager->documentPath(doc);
                if (!bundlePath.isEmpty()) {
                    QPixmap thumbnail;
                    if (doc->isEdgeless()) {
                        // Render edgeless thumbnail from last-viewed position
                        thumbnail = renderEdgelessThumbnail(doc);
                    } else if (doc->pageCount() > 0) {
                        // Try to get cached thumbnail from PagePanel first
                        if (m_pagePanel && m_pagePanel->document() == doc) {
                            thumbnail = m_pagePanel->thumbnailForPage(0);
                        }
                        // If no cached thumbnail, render one synchronously
                        if (thumbnail.isNull()) {
                            // THREAD SAFETY FIX: Cancel any background thumbnail rendering before
                            // accessing Document::page() directly. Background renders also call
                            // Document::page() which modifies m_loadedPages without synchronization.
                            if (m_pagePanel) {
                                m_pagePanel->cancelPendingRenders();
                            }
                            thumbnail = renderPage0Thumbnail(doc);
                        }
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
                
                // THREAD SAFETY: Cancel and wait for all background PDF render threads
                // before destroying the Document. The finished-signal handlers capture
                // the viewport pointer and access its members, so they must complete
                // before we clear the document.
                vp->cancelAndWaitForBackgroundThreads();
                
                // Clear viewport's document pointer BEFORE deleting Document.
                // This triggers cleanup of undo stacks and other document-related
                // data structures while the document is still valid.
                vp->setDocument(nullptr);
                
                m_documentManager->closeDocument(doc);
            }
        }
    });
    
    // ========== EDGELESS SAVE PROMPT (A2: Prompt save before closing) ==========
    // Connect tabCloseAttempted to check for unsaved edgeless documents.
    // The tab is NOT automatically closed - we must call closeTab() explicitly.
    connect(m_splitViewManager, &SplitViewManager::tabCloseAttempted, this, [this](int tabId, DocumentViewport* vp, SplitViewManager::Pane pane) {
        Q_UNUSED(tabId);
        if (!vp || !m_documentManager) {
            return;
        }

        // Determine which TabManager owns this tab
        TabManager* tm = (pane == SplitViewManager::Left)
            ? m_splitViewManager->leftTabManager()
            : m_splitViewManager->rightTabManager();
        if (!tm) return;

        // Find the index of this viewport in its pane
        int index = -1;
        for (int i = 0; i < tm->tabCount(); ++i) {
            if (tm->viewportAt(i) == vp) { index = i; break; }
        }
        if (index < 0) return;
        
        // Prevent closing the last tab across all panes
        if (m_splitViewManager->totalTabCount() <= 1) {
            QMessageBox::information(this, tr("Notice"), 
                tr("At least one tab must remain open."));
            return;
        }
        
        Document* doc = vp->document();
        if (!doc) {
            tm->closeTab(index);
            return;
        }
        
        bool isUsingTemp = m_documentManager->isUsingTempBundle(doc);
        bool positionChanged = syncDocumentPosition(doc, vp);
        
        if (positionChanged && !isUsingTemp && !doc->modified) {
            QString existingPath = m_documentManager->documentPath(doc);
            if (!existingPath.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "tabCloseAttempted: Auto-saving to persist position";
#endif
                m_documentManager->saveDocument(doc);
            }
        }
        
        bool needsSavePrompt = false;
        
        if (doc->isEdgeless()) {
            bool hasContent = doc->tileCount() > 0 || doc->tileIndexCount() > 0;
            needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
        } else {
            bool hasContent = doc->pageCount() > 0;
            needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
        }
        
        if (needsSavePrompt) {
            QString docType = doc->isEdgeless() ? tr("canvas") : tr("document");
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Save Changes?"),
                tr("This %1 has unsaved changes. Do you want to save before closing?").arg(docType),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save
            );
            
            if (reply == QMessageBox::Cancel) {
                return;
            }
            
            if (reply == QMessageBox::Save) {
                QString existingPath = m_documentManager->documentPath(doc);
                bool canSaveInPlace = !existingPath.isEmpty() && !isUsingTemp;
                
                if (canSaveInPlace) {
                    if (!m_documentManager->saveDocument(doc)) {
                        QMessageBox::critical(this, tr("Save Error"),
                            tr("Failed to save document to:\n%1").arg(existingPath));
                        return;
                    }
                } else {
                    if (!saveNewDocumentWithDialog(doc)) {
                        return;
                    }
                }
                
                tm->setTabTitle(index, doc->displayName());
                tm->markTabModified(index, false);
                if (m_navigationBar) {
                    m_navigationBar->setFilename(doc->displayName());
                }
            }
        }
        
        tm->closeTab(index);

        // If split is active and either pane is now empty, collapse back
        // to a single-pane layout (avoids a blank left/right pane).
        // mergePanes() moves any remaining right-pane tabs to the left and
        // destroys the right pane, so it works for both "right empty" and
        // "left empty" cases.
        if (m_splitViewManager->isSplit()) {
            TabManager* leftTm  = m_splitViewManager->leftTabManager();
            TabManager* rightTm = m_splitViewManager->rightTabManager();
            const bool leftEmpty  = leftTm  && leftTm->tabCount()  == 0;
            const bool rightEmpty = rightTm && rightTm->tabCount() == 0;
            if (leftEmpty || rightEmpty) {
                m_splitViewManager->mergePanes();
            }
        }
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

#ifdef Q_OS_LINUX
    // Palm rejection: install application-wide event filter to catch tablet proximity events.
    // This intercepts TabletEnterProximity/TabletLeaveProximity before any widget processes them.
    m_palmRejectionTimer = new QTimer(this);
    m_palmRejectionTimer->setSingleShot(true);
    connect(m_palmRejectionTimer, &QTimer::timeout, this, [this]() {
        if (m_palmRejectionActive) {
            m_palmRejectionActive = false;
            // Restore user's configured touch gesture mode
            if (DocumentViewport* vp = currentViewport()) {
                vp->setTouchGestureMode(touchGestureMode);
            }
        }
    });
#endif
    
    qApp->installEventFilter(this);
    
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
    m_leftSidebar->setMinimumWidth(180);
    m_leftSidebar->setMaximumWidth(500);
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
    markdownNotesSidebar->setMinimumWidth(220);
    markdownNotesSidebar->setMaximumWidth(600);
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
    
    // Handle note deletion from sidebar - delete file and clear LinkSlot.
    // Phase M.8: search across ALL pages/tiles (not just the current page) so
    // edgeless mode and focused-L3 deletion both find the target LinkObject.
    connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeletedWithLink,
            this, [this](const QString& noteId, const QString& linkObjectId) {
        DocumentViewport* vp = currentViewport();
        if (!vp || !vp->document()) return;

        Document* doc = vp->document();
        doc->deleteNoteFile(noteId);

        auto tryClearSlotIn = [&](Page* page, int pageIdxForDirty) -> bool {
            if (!page) return false;
            for (const auto& objPtr : page->objects) {
                LinkObject* link = dynamic_cast<LinkObject*>(objPtr.get());
                if (!link || link->id != linkObjectId) continue;
                for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                    if (link->linkSlots[i].type == LinkSlot::Type::Markdown &&
                        link->linkSlots[i].markdownNoteId == noteId) {
                        link->linkSlots[i].clear();
                        if (pageIdxForDirty >= 0) {
                            doc->markPageDirty(pageIdxForDirty);
                        }
                        return true;
                    }
                }
                return false;  // matched link but no slot — stop searching this container
            }
            return false;
        };

        // Phase M.9: resolve the owning container from the outline
        // cache first — this works even when the tile is currently
        // evicted, whereas the old "iterate allLoadedTileCoords()" loop
        // silently leaked the slot pointer on evicted tiles.
        bool found = false;
        int  foundPageIndex = -1;
        Document::TileCoord foundTileCoord{0, 0};
        bool isTile = false;
        {
            const QVector<LinkOutlineEntry> outline = doc->enumerateLinkOutline();
            for (const auto& entry : outline) {
                if (entry.linkObjectId != linkObjectId) continue;
                if (doc->isEdgeless()) {
                    foundTileCoord = Document::TileCoord(entry.tileX, entry.tileY);
                    isTile = true;
                } else {
                    foundPageIndex = entry.pageIndex;
                }
                break;
            }
        }

        if (isTile) {
            Page* tile = doc->getTile(foundTileCoord.first, foundTileCoord.second);
            if (tile && tryClearSlotIn(tile, /*pageIdxForDirty=*/-1)) {
                doc->markTileDirty(foundTileCoord);
                doc->refreshLinkOutlineFor(foundTileCoord);
                found = true;
            }
        } else if (foundPageIndex >= 0) {
            if (tryClearSlotIn(doc->page(foundPageIndex), foundPageIndex)) {
                doc->refreshLinkOutlineFor(foundPageIndex);
                found = true;
            }
        } else {
            // Cache miss (race or out-of-sync): fall back to the legacy
            // linear scan so we don't silently leak the slot.
            if (doc->isEdgeless()) {
                for (const auto& coord : doc->allLoadedTileCoords()) {
                    Page* tile = doc->getTile(coord.first, coord.second);
                    if (tryClearSlotIn(tile, /*pageIdxForDirty=*/-1)) {
                        doc->markTileDirty(coord);
                        doc->refreshLinkOutlineFor(coord);
                        found = true;
                        break;
                    }
                }
            } else {
                const int count = doc->pageCount();
                for (int i = 0; i < count; ++i) {
                    if (tryClearSlotIn(doc->page(i), i)) {
                        doc->refreshLinkOutlineFor(i);
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found) vp->update();
        refreshNotesOutline();
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
            refreshNotesOutline();
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

    QAction *ocrLanguageAction = overflowMenu->addAction(tr("OCR Language..."));
    connect(ocrLanguageAction, &QAction::triggered, this, [this]() {
        showOcrLanguageDialog();
    });

    QAction *lockAllOcrAction = overflowMenu->addAction(tr("Lock All OCR Text"));
    connect(lockAllOcrAction, &QAction::triggered, this, [this]() {
        DocumentViewport* vp = currentViewport();
        if (!vp || !vp->document()) return;

        auto lockOnPage = [](Page* page) -> QVector<QString> {
            QVector<QString> ids;
            if (!page) return ids;
            for (const auto& obj : page->objects) {
                if (obj->type() == QStringLiteral("ocr_text")) {
                    auto* ocr = static_cast<OcrTextObject*>(obj.get());
                    if (!ocr->ocrLocked) {
                        ocr->ocrLocked = true;
                        ids.append(ocr->id);
                    }
                }
            }
            return ids;
        };

        QVector<QString> lockedIds;
        Document* doc = vp->document();
        if (doc->isEdgeless()) {
            for (auto coord : doc->allLoadedTileCoords()) {
                Page* tile = doc->getTile(coord.first, coord.second);
                lockedIds += lockOnPage(tile);
            }
        } else {
            Page* page = doc->page(vp->currentPageIndex());
            lockedIds = lockOnPage(page);
        }

        if (!lockedIds.isEmpty()) {
            vp->pushOcrLockUndo(lockedIds, true);
            vp->update();
        }
    });

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

    // Phase SV: Use SplitViewManager's splitter instead of single viewport stack
    canvasLayout->addWidget(m_splitViewManager->viewportSplitter());
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
            
            // Force layout update so canvas container resizes before we
            // recalculate action bar position (same pattern as right sidebar)
            if (centralWidget() && centralWidget()->layout()) {
                centralWidget()->layout()->invalidate();
                centralWidget()->layout()->activate();
            }
            QApplication::processEvents();
            updateActionBarPosition();
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
        if (m_splitViewManager && m_splitViewManager->tabBarContainer()) {
            QWidget* tbc = m_splitViewManager->tabBarContainer();
            tbc->setVisible(!tbc->isVisible());
        }
    });
    connect(m_navigationBar, &NavigationBar::fullscreenToggled, this, [this]() {
        toggleFullscreen();
    });
    connect(m_navigationBar, &NavigationBar::shareClicked, this, [this]() {
        // Phase 3: Export notebook as .snbx package using unified dialog
        DocumentViewport* vp = currentViewport();
        Document* doc = vp ? vp->document() : nullptr;
        if (!doc) {
            QMessageBox::warning(this, tr("Export Failed"), 
                tr("No document is currently open."));
            return;
        }
        
        // Ensure document is saved before exporting
        QString bundlePath = doc->bundlePath();
        if (bundlePath.isEmpty()) {
            QMessageBox::warning(this, tr("Export Failed"),
                tr("Please save the document before exporting."));
            return;
        }
        
        // Show unified SNBX export dialog with current notebook
        BatchSnbxExportDialog dialog(QStringList{bundlePath}, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        
        // Single-file export: use direct export for immediate feedback
        // (ExportQueueManager is for batch exports from Launcher)
        QString outputDir = dialog.outputDirectory();
        QString outputPath = outputDir + "/" + doc->name + ".snbx";
        
        // Auto-rename if file exists (with safety limit to prevent infinite loop)
        if (QFile::exists(outputPath)) {
            int counter = 1;
            QString baseName = doc->name;
            const int maxAttempts = 1000;  // Safety limit
            while (QFile::exists(outputPath) && counter <= maxAttempts) {
                outputPath = outputDir + "/" + baseName + QString(" (%1).snbx").arg(counter++);
            }
            if (counter > maxAttempts) {
                QMessageBox::warning(this, tr("Export Failed"),
                    tr("Could not find a unique filename. Please choose a different location."));
                return;
            }
        }
        
        NotebookExporter::ExportOptions options;
        options.includePdf = dialog.includePdf();
        options.destPath = outputPath;
        
        QApplication::setOverrideCursor(Qt::WaitCursor);
        auto result = NotebookExporter::exportPackage(doc, options);
        QApplication::restoreOverrideCursor();
        
        if (result.success) {
#ifdef Q_OS_ANDROID
            // Android: Share the exported file via share sheet
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            QJniObject::callStaticMethod<void>(
                "org/speedynote/app/ShareHelper",
                "shareFile",
                "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V",
                activity.object<jobject>(),
                QJniObject::fromString(outputPath).object<jstring>(),
                QJniObject::fromString("application/octet-stream").object<jstring>()
            );
#elif defined(Q_OS_IOS)
            IOSShareHelper::shareFile(outputPath, "application/octet-stream", tr("Share Notebook Package"));
#else
            // Desktop: Show success message
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
                    .arg(QFileInfo(outputPath).fileName())
                    .arg(sizeStr));
#endif
        } else {
            QMessageBox::warning(this, tr("Export Failed"), result.errorMessage);
        }
    });
    connect(m_navigationBar, &NavigationBar::rightSidebarToggled, this, [this](bool checked) {
        // Toggle markdown notes sidebar
        if (markdownNotesSidebar) {
            markdownNotesSidebar->setVisible(checked);
            markdownNotesSidebarVisible = checked;
            
            // Load notes when sidebar becomes visible
            if (checked) {
                refreshNotesOutline();
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
    mainLayout->addWidget(m_splitViewManager->tabBarContainer());

    // =========================================================================
    // Phase B: Toolbar (Toolbar Extraction)
    // =========================================================================
    m_toolbar = new Toolbar(this);
    mainLayout->addWidget(m_toolbar);
    
    // Connect Toolbar signals
    connect(m_toolbar, &Toolbar::toolSelected, this, [this](ToolType tool) {
        if (m_panHoldActive) m_panHoldActive = false;
        if (DocumentViewport* vp = currentViewport()) {
            if (m_toolOverrideViewport == vp)
                m_toolOverrideViewport = nullptr;
            vp->setCurrentTool(tool);
        }
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
            closeFloatingTextEditor();
            vp->undo();
        }
    });
    connect(m_toolbar, &Toolbar::redoClicked, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            closeFloatingTextEditor();
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
    connectSubToolbarSignals();
    
    // Setup action bars
    setupActionBars();
    
    // PDF Search: Setup search bar
    setupPdfSearch();
    
    // OCR: Setup worker thread and engine
    setupOcr();
    
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
    
    // QSplitter for resizable left + right sidebars (markdownNotesSidebar is
    // the 3rd child so the same splitter gives us handles on both sides).
    m_contentSplitter = new QSplitter(Qt::Horizontal);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(3);
    m_contentSplitter->addWidget(m_leftSidebar);
    m_contentSplitter->addWidget(canvasContainer);
    m_contentSplitter->addWidget(markdownNotesSidebar);
    m_contentSplitter->setStretchFactor(0, 0);  // Left sidebar: fixed
    m_contentSplitter->setStretchFactor(1, 1);  // Canvas: stretches
    m_contentSplitter->setStretchFactor(2, 0);  // Right sidebar: fixed

    // Restore persisted sidebar widths
    {
        QSettings s("SpeedyNote", "App");
        int leftW  = qBound(180, s.value("ui/leftSidebarWidth",  250).toInt(), 500);
        int rightW = qBound(220, s.value("ui/rightSidebarWidth", 300).toInt(), 600);
        m_contentSplitter->setSizes({leftW, /*canvas=*/1, rightW});
    }

    // Debounce timer for saving sidebar widths (shared for both sides)
    m_sidebarWidthSaveTimer = new QTimer(this);
    m_sidebarWidthSaveTimer->setSingleShot(true);
    m_sidebarWidthSaveTimer->setInterval(300);
    connect(m_sidebarWidthSaveTimer, &QTimer::timeout, this, [this]() {
        QSettings s("SpeedyNote", "App");
        // Guard against persisting a stale 0 when either panel is hidden
        // (QSplitter reports width 0 for hidden children).  With two splitter
        // handles, dragging the right handle also fires splitterMoved, which
        // would otherwise clobber the left width with 0 when it's hidden.
        if (m_leftSidebar && m_leftSidebar->isVisible()) {
            s.setValue("ui/leftSidebarWidth", m_leftSidebar->width());
        }
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            s.setValue("ui/rightSidebarWidth", markdownNotesSidebar->width());
        }
    });

    connect(m_contentSplitter, &QSplitter::splitterMoved, this, [this]() {
        m_sidebarWidthSaveTimer->start();
    });

    contentLayout->addWidget(m_contentSplitter, 1);
    
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

        // Phase 2B: Close floating text editor if open
        if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
            m_floatingTextEditor->closeEditor();
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
            updatePagePanelActionBarVisibility();
            
            // Force layout update so canvas container resizes before we
            // recalculate action bar position
            if (centralWidget() && centralWidget()->layout()) {
                centralWidget()->layout()->invalidate();
                centralWidget()->layout()->activate();
            }
            QApplication::processEvents();
            updateActionBarPosition();
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
    createShortcut("view.split_right", [this]() {
        if (m_splitViewManager && tabManager() && tabManager()->tabCount() > 1) {
            m_splitViewManager->splitTab(tabManager()->currentIndex(),
                                         m_splitViewManager->activePane());
        }
    });
    createShortcut("view.merge_panes", [this]() {
        if (m_splitViewManager && m_splitViewManager->isSplit()) {
            m_splitViewManager->mergePanes();
        }
    });
    createShortcut("view.focus_left_pane", [this]() {
        if (m_splitViewManager) m_splitViewManager->setActivePane(SplitViewManager::Left);
    });
    createShortcut("view.focus_right_pane", [this]() {
        if (m_splitViewManager && m_splitViewManager->isSplit())
            m_splitViewManager->setActivePane(SplitViewManager::Right);
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
            
            if (m_panHoldActive) m_panHoldActive = false;
            if (DocumentViewport* vp = currentViewport()) {
                if (m_toolOverrideViewport == vp)
                    m_toolOverrideViewport = nullptr;
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
    
    // Pan tool: H key hold handled via event filter, not QShortcut (need release detection)
    {
        QKeySequence panSeq = sm->keySequenceForAction("tool.pan");
        if (!panSeq.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            m_panHoldKey = panSeq[0].key();
#else
            m_panHoldKey = panSeq[0] & ~Qt::KeyboardModifierMask;
#endif
        }
    }
    
    // ===== Edit (delegated to viewport) =====
    createShortcut("edit.undo", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            closeFloatingTextEditor();
            vp->undo();
        }
    });
    createShortcut("edit.redo", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            closeFloatingTextEditor();
            vp->redo();
        }
    });
    createShortcut("edit.redo_alt", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            closeFloatingTextEditor();
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
        if (tabManager()) {
            tabManager()->switchToNextTab();
        }
    });
    createShortcut("navigation.prev_tab", [this]() {
        if (tabManager()) {
            tabManager()->switchToPrevTab();
        }
    });
    createShortcut("file.close_tab", [this]() {
        // Use tabCloseAttempted signal flow to properly handle unsaved changes
        if (tabManager() && tabManager()->tabCount() > 0) {
            int currentIndex = tabManager()->currentIndex();
            DocumentViewport* vp = tabManager()->currentViewport();
            if (vp) {
                emit tabManager()->tabCloseAttempted(currentIndex, vp);
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
        if (auto *tb = qobject_cast<QTextBrowser *>(QApplication::focusWidget())) {
            if (tb->textCursor().hasSelection()) {
                tb->copy();
                return;
            }
        }
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
            if (m_toolOverrideViewport && m_toolOverrideViewport != vp)
                clearToolOverride(true);
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
    createShortcut("object.mode_text", [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            if (vp->currentTool() == ToolType::ObjectSelect) {
                vp->setObjectInsertMode(DocumentViewport::ObjectInsertMode::Text);
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
    
    // ===== OCR subtoolbar =====
    // Each shortcut delegates to a public trigger method on OcrSubToolbar,
    // which forwards to the same button-click path the UI already uses. This
    // keeps one source of truth for button state / settings / signal emission.
    createShortcut("ocr.scan_page", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->ocrSubToolbar() : nullptr)
            st->triggerScanPage();
    });
    createShortcut("ocr.scan_all", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->ocrSubToolbar() : nullptr)
            st->triggerScanAll();
    });
    createShortcut("ocr.auto_ocr", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->ocrSubToolbar() : nullptr)
            st->toggleAutoOcr();
    });
    createShortcut("ocr.show_text", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->ocrSubToolbar() : nullptr)
            st->toggleShowText();
    });
    createShortcut("ocr.snap_grid", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->ocrSubToolbar() : nullptr)
            st->toggleSnapToGrid();
    });

    // ===== Highlighter subtoolbar =====
    // Style shortcuts drive the dropdown's QAction::trigger() path so the
    // existing onAutoHighlightStyleTriggered() slot handles persistence,
    // check-state, icon refresh, and autoHighlightStyleChanged emission.
    // No current-tool gate: these set the globally persisted style, so users
    // can pre-configure before switching to the Highlighter tool.
    using HS = HighlighterSubToolbar::HighlightStyle;
    createShortcut("highlighter.style_none", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->highlighterSubToolbar() : nullptr)
            st->selectAutoHighlightStyleFromShortcut(HS::None);
    });
    createShortcut("highlighter.style_cover", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->highlighterSubToolbar() : nullptr)
            st->selectAutoHighlightStyleFromShortcut(HS::Cover);
    });
    createShortcut("highlighter.style_underline", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->highlighterSubToolbar() : nullptr)
            st->selectAutoHighlightStyleFromShortcut(HS::Underline);
    });
    createShortcut("highlighter.style_dotted", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->highlighterSubToolbar() : nullptr)
            st->selectAutoHighlightStyleFromShortcut(HS::DottedUnderline);
    });
    createShortcut("highlighter.toggle_source", [this]() {
        if (auto* st = m_toolbar ? m_toolbar->highlighterSubToolbar() : nullptr)
            st->toggleSelectionSourceFromShortcut();
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
    // Pan tool hold key is managed separately (not a QShortcut)
    if (actionId == "tool.pan") {
        QKeySequence seq(newShortcut);
        if (seq.isEmpty()) {
            m_panHoldKey = 0;
        } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            m_panHoldKey = seq[0].key();
#else
            m_panHoldKey = seq[0] & ~Qt::KeyboardModifierMask;
#endif
        }
        return;
    }
    
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
    if (m_splitViewManager) {
        disconnect(m_splitViewManager, nullptr, this, nullptr);
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
    
    if (m_ocrThread && m_ocrThread->isRunning()) {
        m_ocrThread->quit();
        m_ocrThread->wait();
    }

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
    // Disconnect highlighter-mode (PDF/OCR) sync connection
    if (m_highlighterModeConn) {
        disconnect(m_highlighterModeConn);
        m_highlighterModeConn = {};
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
    if (m_strokeClipboardOverrideConn) {
        disconnect(m_strokeClipboardOverrideConn);
        m_strokeClipboardOverrideConn = {};
    }
    if (m_objectClipboardOverrideConn) {
        disconnect(m_objectClipboardOverrideConn);
        m_objectClipboardOverrideConn = {};
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
    if (m_pagePanelPageModConn) {
        disconnect(m_pagePanelPageModConn);
        m_pagePanelPageModConn = {};
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
    if (m_userWarningConn) {
        disconnect(m_userWarningConn);
        m_userWarningConn = {};
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
    // OCR: Disconnect strokesChanged connection
    if (m_strokesChangedConn) {
        disconnect(m_strokesChangedConn);
        m_strokesChangedConn = {};
    }
    if (m_textEditorConn) {
        disconnect(m_textEditorConn);
        m_textEditorConn = {};
    }

    // Close floating text editor when switching viewports (target may belong to old viewport)
    closeFloatingTextEditor();

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
    
    // Phase D: Connect auto-highlight style sync (viewport → subtoolbar)
    // When Ctrl+H or a tab switch changes the style, update the dropdown to match.
    m_autoHighlightConn = connect(viewport, &DocumentViewport::autoHighlightStyleChanged,
                                  this, [this](DocumentViewport::HighlightStyle style) {
        if (m_toolbar->highlighterSubToolbar()) {
            m_toolbar->highlighterSubToolbar()->setAutoHighlightStyle(
                static_cast<HighlighterSubToolbar::HighlightStyle>(style));
        }
    });

    // Also sync the current auto-highlight style to the subtoolbar
    if (m_toolbar->highlighterSubToolbar()) {
        m_toolbar->highlighterSubToolbar()->setAutoHighlightStyle(
            static_cast<HighlighterSubToolbar::HighlightStyle>(viewport->autoHighlightStyle()));
    }

    // Connect highlighter-mode (PDF/OCR) sync (viewport -> subtoolbar)
    m_highlighterModeConn = connect(viewport, &DocumentViewport::highlighterModeChanged,
                                    this, [this](DocumentViewport::HighlighterMode mode) {
        if (m_toolbar && m_toolbar->highlighterSubToolbar()) {
            auto src = (mode == DocumentViewport::HighlighterMode::Ocr)
                           ? HighlighterSubToolbar::SelectionSource::Ocr
                           : HighlighterSubToolbar::SelectionSource::Pdf;
            m_toolbar->highlighterSubToolbar()->setSelectionSourceState(src);
        }
    });

    // Also sync the current highlighter mode to the subtoolbar
    if (m_toolbar->highlighterSubToolbar()) {
        auto src = (viewport->highlighterMode() == DocumentViewport::HighlighterMode::Ocr)
                       ? HighlighterSubToolbar::SelectionSource::Ocr
                       : HighlighterSubToolbar::SelectionSource::Pdf;
        m_toolbar->highlighterSubToolbar()->setSelectionSourceState(src);
    }

    // Phase D: Connect object mode state sync (viewport → subtoolbar)
    // When Ctrl+< / Ctrl+> / Ctrl+6 / Ctrl+7 changes the mode, update the subtoolbar
    m_insertModeConn = connect(viewport, &DocumentViewport::objectInsertModeChanged,
                               this, [this](DocumentViewport::ObjectInsertMode mode) {
        if (m_toolbar->objectSelectSubToolbar()) {
            m_toolbar->objectSelectSubToolbar()->setInsertModeState(mode);
        }
    });
    
    m_actionModeConn = connect(viewport, &DocumentViewport::objectActionModeChanged,
                               this, [this](DocumentViewport::ObjectActionMode mode) {
        if (m_toolbar->objectSelectSubToolbar()) {
            m_toolbar->objectSelectSubToolbar()->setActionModeState(mode);
        }
    });
    
    // Also sync the current object modes to the subtoolbar
    if (m_toolbar->objectSelectSubToolbar()) {
        m_toolbar->objectSelectSubToolbar()->setInsertModeState(viewport->objectInsertMode());
        m_toolbar->objectSelectSubToolbar()->setActionModeState(viewport->objectActionMode());
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
        // Update image-specific state BEFORE notifying the container,
        // so button visibility is correct when the container computes its size.
        if (m_objectSelectActionBar) {
            const auto& sel = viewport->selectedObjects();
            if (!sel.isEmpty() && sel.size() == 1 && sel.first()->type() == "image") {
                auto* img = dynamic_cast<ImageObject*>(sel.first());
                m_objectSelectActionBar->updateImageSelection(true, img ? img->maintainAspectRatio : true);
            } else {
                m_objectSelectActionBar->updateImageSelection(false, false);
            }
            if (!sel.isEmpty() && sel.size() == 1 && sel.first()->type() == QStringLiteral("ocr_text")) {
                auto* ocr = dynamic_cast<OcrTextObject*>(sel.first());
                m_objectSelectActionBar->updateOcrLockSelection(true, ocr ? ocr->ocrLocked : false);
            } else {
                m_objectSelectActionBar->updateOcrLockSelection(false, false);
            }
        }
        if (m_actionBarContainer) {
            bool hasSelection = !viewport->selectedObjects().isEmpty();
            m_actionBarContainer->onObjectSelectionChanged(hasSelection);
        }
        // Phase 2B: Close floating editor if target was deselected/deleted
        if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
            auto* tgt = m_floatingTextEditor->target();
            if (tgt && !viewport->selectedObjects().contains(tgt)) {
                m_floatingTextEditor->closeEditor();
            }
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
    
    // Smart tool auto-switch: override inactive viewport's tool on copy/cut
    m_strokeClipboardOverrideConn = connect(viewport, &DocumentViewport::strokeClipboardChanged,
                                            this, [this](bool has) {
        if (has) applyToolOverrideForClipboard(ToolType::Lasso);
        else clearToolOverride(true);
    });
    m_objectClipboardOverrideConn = connect(viewport, &DocumentViewport::objectClipboardChanged,
                                            this, [this](bool has) {
        if (has) applyToolOverrideForClipboard(ToolType::ObjectSelect);
        else clearToolOverride(true);
    });

    // Phase 2B: Floating text editor
    m_textEditorConn = connect(viewport, &DocumentViewport::openTextEditorRequested,
                               this, &MainWindow::openFloatingTextEditor);

    // Sync initial action bar state from viewport
    // CR-AB-2 FIX: Sync ALL context states to prevent stale state from previous tab
    if (m_actionBarContainer) {
        // Trigger tool change to evaluate initial visibility
        m_actionBarContainer->onToolChanged(viewport->currentTool());
        
        // Sync all selection/clipboard states
        m_actionBarContainer->onLassoSelectionChanged(viewport->hasLassoSelection());

        // Sync image/OCR-specific state before object selection so button count is correct
        if (m_objectSelectActionBar) {
            const auto& sel = viewport->selectedObjects();
            if (!sel.isEmpty() && sel.size() == 1 && sel.first()->type() == "image") {
                auto* img = dynamic_cast<ImageObject*>(sel.first());
                m_objectSelectActionBar->updateImageSelection(true, img ? img->maintainAspectRatio : true);
            } else {
                m_objectSelectActionBar->updateImageSelection(false, false);
            }
            if (!sel.isEmpty() && sel.size() == 1 && sel.first()->type() == QStringLiteral("ocr_text")) {
                auto* ocr = dynamic_cast<OcrTextObject*>(sel.first());
                m_objectSelectActionBar->updateOcrLockSelection(true, ocr ? ocr->ocrLocked : false);
            } else {
                m_objectSelectActionBar->updateOcrLockSelection(false, false);
            }
        }
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
            // Note: Outline stores PDF page indices, so we convert notebook page → PDF page
            m_outlinePageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                                        this, [outlinePanel, viewport]() {
                Document* doc = viewport->document();
                if (!doc) return;
                
                int notebookPage = viewport->currentPageIndex();
                int pdfPage = doc->pdfPageIndexForNotebookPage(notebookPage);
                
                // Only highlight if current page is a PDF page
                // For inserted blank pages, keep the previous highlight
                if (pdfPage >= 0) {
                    outlinePanel->highlightPage(pdfPage);
                }
            });
            
            // Sync current page state immediately
            Document* doc = viewport->document();
            if (doc) {
                int pdfPage = doc->pdfPageIndexForNotebookPage(viewport->currentPageIndex());
                if (pdfPage >= 0) {
                    outlinePanel->highlightPage(pdfPage);
                }
            }
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
            if (m_pagePanel && viewport && !viewport->hasSelectedObjects()) {
                m_pagePanel->invalidateThumbnail(viewport->currentPageIndex());
            }
        });

        m_pagePanelPageModConn = connect(viewport, &DocumentViewport::pageModified,
                                         m_pagePanel, &PagePanel::invalidateThumbnail);
        
        // Sync current page state immediately
        m_pagePanel->onCurrentPageChanged(viewport->currentPageIndex());
    }
    
    // =========================================================================
    // BUG FIX: Connect documentModified to mark document and tab as modified
    // This was missing, causing the save prompt to never show when closing tabs
    // =========================================================================
    if (viewport && m_splitViewManager) {
        m_documentModifiedConn = connect(viewport, &DocumentViewport::documentModified,
                                          this, [this, viewport]() {
            if (!viewport || !m_splitViewManager) return;
            
            Document* doc = viewport->document();
            if (doc) {
                doc->markModified();
                
                // Find which TabManager owns this viewport and mark tab modified
                m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
                    for (int i = 0; i < tm->tabCount(); ++i) {
                        if (tm->viewportAt(i) == viewport) {
                            tm->markTabModified(i, true);
                            return;
                        }
                    }
                });
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
    
    // Phase M.3 / M.8: Refresh markdown notes sidebar when page changes
    if (markdownNotesSidebar) {
        Document* doc = viewport->document();
        markdownNotesSidebar->setEdgelessMode(doc && doc->isEdgeless());

        if (doc && !doc->isEdgeless()) {
            markdownNotesSidebar->setCurrentPageInfo(viewport->currentPageIndex(),
                                                     doc->pageCount());
        }

        // Page change no longer reloads the outline (loading is O(#links), and
        // the outline is document-wide); it just moves the L1 highlight.
        m_markdownNotesPageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                this, [this](int pageIndex) {
            if (!markdownNotesSidebar) return;
            DocumentViewport* vp = currentViewport();
            if (vp && vp->document() && !vp->document()->isEdgeless()) {
                markdownNotesSidebar->setCurrentPageInfo(pageIndex,
                                                         vp->document()->pageCount());
                markdownNotesSidebar->highlightPage(pageIndex);
            }
        });

        if (markdownNotesSidebar->isVisible()) {
            refreshNotesOutline();
        }

        // Phase M.5 / M.8: open-note request — expand chain + inflate editor.
        m_markdownNoteOpenConn = connect(viewport, &DocumentViewport::requestOpenMarkdownNote,
                this, [this](const QString& noteId, const QString& linkObjectId) {
            if (!markdownNotesSidebar) return;
            if (!markdownNotesSidebar->isVisible()) {
                toggleMarkdownNotesSidebar();  // also triggers refreshNotesOutline
            } else {
                refreshNotesOutline();
            }
            markdownNotesSidebar->openNoteById(linkObjectId, noteId);
        });
        
        m_userWarningConn = connect(viewport, &DocumentViewport::userWarning,
                this, [this](const QString& message) {
            QMessageBox::warning(this, tr("Warning"), message);
        });

        // M.7.3: Handle linkObjectListMayHaveChanged signal (objects add/remove, tile eviction)
        m_linkObjectListConn = connect(viewport, &DocumentViewport::linkObjectListMayHaveChanged,
                this, [this]() {
            if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
                refreshNotesOutline();
            }
        });

        // OCR: Restart debounce timer when strokes change
        m_strokesChangedConn = connect(viewport, &DocumentViewport::strokesChanged, this, [this]() {
            if (m_autoOcrEnabled && m_ocrDebounceTimer)
                m_ocrDebounceTimer->start();
        });

        // OCR: Sync text visibility with the Show Text toggle for the new viewport
        setOcrTextVisibility(m_toolbar->ocrSubToolbar()->isShowTextEnabled());
        setOcrConfidenceVisibility(m_toolbar->ocrSubToolbar()->isConfidenceEnabled());

        // OCR: Restore snap toggle from document's persisted state
        if (Document* doc = viewport->document()) {
            OcrSubToolbar* ocrST = m_toolbar->ocrSubToolbar();
            ocrST->blockSignals(true);
            ocrST->setSnapToGridChecked(doc->ocrSnapToBackground);
            ocrST->blockSignals(false);
        }

        // OCR: Sync language for the new document
        if (m_ocrWorker) {
            Document* doc = viewport->document();
            QMetaObject::invokeMethod(m_ocrWorker, "setLanguage", Qt::QueuedConnection,
                Q_ARG(QString, resolveOcrLanguage(doc)));
        }
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
    
    // Update Link/Relink PDF menu action
    if (m_relinkPdfAction) {
        m_relinkPdfAction->setEnabled(doc != nullptr);
        if (doc && doc->hasPdfReference()) {
            m_relinkPdfAction->setText(tr("Relink PDF..."));
        } else {
            m_relinkPdfAction->setText(tr("Link PDF..."));
        }
    }
}

void MainWindow::updateLinkSlotButtons(DocumentViewport* viewport)
{
    // Phase D: Update ObjectSelectSubToolbar slot buttons based on selected LinkObject
    if (!m_toolbar->objectSelectSubToolbar() || !viewport) {
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
            m_toolbar->objectSelectSubToolbar()->updateSlotStates(states);
            
            // Show LinkObject color button
            m_toolbar->objectSelectSubToolbar()->setLinkObjectColor(link->iconColor, true);
            
            // Show LinkObject description editor
            m_toolbar->objectSelectSubToolbar()->setLinkObjectDescription(link->description, true);
            return;
        }
    }
    
    // No LinkObject selected (or multiple objects selected) - clear slots and hide controls
    m_toolbar->objectSelectSubToolbar()->clearSlotStates();
    m_toolbar->objectSelectSubToolbar()->setLinkObjectColor(Qt::transparent, false);
    m_toolbar->objectSelectSubToolbar()->setLinkObjectDescription(QString(), false);
}

void MainWindow::applySubToolbarValuesToViewport(ToolType tool)
{
    // Phase D: Apply subtoolbar's current preset values to the viewport (via signals)
    // This is used when the current tool changes and we want to emit signals
    // For new viewports, use applyAllSubToolbarValuesToViewport() instead
    
    switch (tool) {
        case ToolType::Pen:
            if (m_toolbar->penSubToolbar()) {
                m_toolbar->penSubToolbar()->emitCurrentValues();
            }
            break;
        case ToolType::Marker:
            if (m_toolbar->markerSubToolbar()) {
                m_toolbar->markerSubToolbar()->emitCurrentValues();
            }
            break;
        case ToolType::Highlighter:
            if (m_toolbar->highlighterSubToolbar()) {
                m_toolbar->highlighterSubToolbar()->emitCurrentValues();
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
    if (m_toolbar->penSubToolbar()) {
        viewport->setPenColor(m_toolbar->penSubToolbar()->currentColor());
        viewport->setPenThickness(m_toolbar->penSubToolbar()->currentThickness());
        viewport->setPenMinStrokeWidth(m_toolbar->penSubToolbar()->currentMinStrokeWidth());
    }
    
    // Apply marker settings
    if (m_toolbar->markerSubToolbar()) {
        viewport->setMarkerColor(m_toolbar->markerSubToolbar()->currentColor());
        viewport->setMarkerThickness(m_toolbar->markerSubToolbar()->currentThickness());
    }
    
    // Apply highlighter color (uses separate m_highlighterColor in viewport)
    // Note: Highlighter and Marker share the same color PRESETS (QSettings),
    // but the Highlighter tool uses a separate color variable in DocumentViewport
    if (m_toolbar->highlighterSubToolbar()) {
        viewport->setHighlighterColor(m_toolbar->highlighterSubToolbar()->currentColor());
    }
    
    // Apply eraser size and mode
    if (m_toolbar->eraserSubToolbar()) {
        viewport->setEraserSize(m_toolbar->eraserSubToolbar()->currentSize());
        viewport->setEraserMode(
            static_cast<DocumentViewport::EraserMode>(
                m_toolbar->eraserSubToolbar()->currentModeIndex()));
    }
}

void MainWindow::centerViewportContent(int tabIndex) {
    // Phase 3.3: One-time horizontal centering for new tabs
    // Sets initial pan X to a negative value so content appears centered
    // when it's narrower than the viewport.
    //
    // This is called ONCE when a tab is created. User can then pan freely.
    // The DocumentViewport debug overlay will show negative pan X values.
    
    if (!tabManager()) return;
    
    DocumentViewport* viewport = tabManager()->viewportAt(tabIndex);
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
    if (!doc) return;
    
    // Open PdfRelinkDialog with hash verification
    PdfRelinkDialog dialog(doc->pdfPath(), doc->pdfHash(), doc->pdfSize(),
                           doc->isPdfLoaded(), this);
    if (dialog.exec() == QDialog::Accepted) {
        PdfRelinkDialog::Result result = dialog.getResult();
        
        if (result == PdfRelinkDialog::RelinkPdf) {
            QString newPath = dialog.getNewPdfPath();
            if (!newPath.isEmpty() && doc->relinkPdf(newPath)) {
                viewport->hideMissingPdfBanner();
                viewport->notifyPdfChanged();
            }
        } else if (result == PdfRelinkDialog::ContinueWithoutPdf) {
            doc->clearPdfReference();
            viewport->hideMissingPdfBanner();
            viewport->notifyPdfChanged();
        }

        if (result == PdfRelinkDialog::RelinkPdf ||
            result == PdfRelinkDialog::ContinueWithoutPdf) {
            updateOutlinePanelForDocument(doc);
            if (m_pagePanel) {
                m_pagePanel->invalidateAllThumbnails();
            }
            if (m_relinkPdfAction) {
                if (doc->hasPdfReference()) {
                    m_relinkPdfAction->setText(tr("Relink PDF..."));
                } else {
                    m_relinkPdfAction->setText(tr("Link PDF..."));
                }
            }
        }
        // Cancel: do nothing, banner remains visible
    }
}

// ============================================================================
// Phase 8: PDF Export Dialog
// ============================================================================

void MainWindow::showPdfExportDialog()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString dialogTitle = tr("Share as PDF");
#else
    QString dialogTitle = tr("Export to PDF");
#endif
    
    DocumentViewport* viewport = currentViewport();
    if (!viewport) {
        QMessageBox::warning(this, dialogTitle, 
                             tr("No document is currently open."));
        return;
    }
    
    Document* doc = viewport->document();
    if (!doc) {
        QMessageBox::warning(this, dialogTitle,
                             tr("No document is currently open."));
        return;
    }
    
    // Get bundle path - document must be saved
    QString bundlePath = doc->bundlePath();
    if (bundlePath.isEmpty()) {
        QMessageBox::warning(this, dialogTitle,
                             tr("Please save the document before exporting."));
        return;
    }
    
    // Check if document is paged (PDF export only makes sense for paged documents)
    // Note: BatchPdfExportDialog also detects edgeless, but we check here for better UX
    if (doc->isEdgeless()) {
        QMessageBox::warning(this, dialogTitle,
                             tr("PDF export is only available for paged documents.\n"
                                "Edgeless canvas export is not yet supported."));
        return;
    }
    
    // Check for unsaved changes - require saving first
    if (doc->modified) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        QString savePrompt = tr("The document has unsaved changes.\n"
                                "Please save the document before sharing as PDF.\n\n"
                                "Would you like to save now?");
#else
        QString savePrompt = tr("The document has unsaved changes.\n"
                                "Please save the document before exporting to PDF.\n\n"
                                "Would you like to save now?");
#endif
        QMessageBox::StandardButton result = QMessageBox::question(
            this, tr("Save Document First"),
            savePrompt,
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
    
    // Show the unified PDF export dialog with current notebook
    BatchPdfExportDialog dialog(QStringList{bundlePath}, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Get valid bundles (dialog filters out edgeless)
        QStringList validBundles = dialog.validBundles();
        if (validBundles.isEmpty()) {
            // This shouldn't happen since we checked isEdgeless above,
            // but handle it gracefully
            return;
        }
        
        // Single-file export: use direct export for immediate feedback
        // (ExportQueueManager is for batch exports from Launcher)
        QString outputDir = dialog.outputDirectory();
        QString outputPath = outputDir + "/" + doc->name + ".pdf";
        
        // Auto-rename if file exists (with safety limit to prevent infinite loop)
        if (QFile::exists(outputPath)) {
            int counter = 1;
            QString baseName = doc->name;
            const int maxAttempts = 1000;  // Safety limit
            while (QFile::exists(outputPath) && counter <= maxAttempts) {
                outputPath = outputDir + "/" + baseName + QString(" (%1).pdf").arg(counter++);
            }
            if (counter > maxAttempts) {
                QMessageBox::warning(this, dialogTitle,
                    tr("Could not find a unique filename. Please choose a different location."));
                return;
            }
        }
        
        // Build PDF export options
        PdfExportOptions options;
        options.outputPath = outputPath;
        options.pageRange = dialog.pageRange();
        options.dpi = dialog.dpi();
        options.preserveMetadata = dialog.includeMetadata();
        options.preserveOutline = dialog.includeOutline();
        options.annotationsOnly = dialog.annotationsOnly();
        options.darkModeBackground = dialog.darkModeBackground();
        options.darkenStrokes = dialog.darkenStrokes();
        options.skipImageMasking = QSettings("SpeedyNote", "App")
            .value("display/skipImageMasking", false).toBool();
        
        // Create exporter and export
        MuPdfExporter exporter;
        exporter.setDocument(doc);
        
        QApplication::setOverrideCursor(Qt::WaitCursor);
        PdfExportResult result = exporter.exportPdf(options);
        QApplication::restoreOverrideCursor();
        
        if (result.success) {
#ifdef Q_OS_ANDROID
            // Android: Share the exported PDF via share sheet
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            QJniObject::callStaticMethod<void>(
                "org/speedynote/app/ShareHelper",
                "shareFileWithTitle",
                "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                activity.object<jobject>(),
                QJniObject::fromString(outputPath).object<jstring>(),
                QJniObject::fromString("application/pdf").object<jstring>(),
                QJniObject::fromString(tr("Share PDF")).object<jstring>()
            );
#elif defined(Q_OS_IOS)
            IOSShareHelper::shareFile(outputPath, "application/pdf", tr("Share PDF"));
#else
            // Desktop: Show success message
            QMessageBox::information(this, tr("Export Complete"),
                                     tr("PDF exported successfully!\n\n"
                                        "Pages exported: %1\n"
                                        "File size: %2 KB")
                                     .arg(result.pagesExported)
                                     .arg(result.fileSizeBytes / 1024));
#endif
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
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Android/iOS: Save to app-private storage using touch-friendly dialog
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
    QSettings saveSettings("SpeedyNote", "App");
    QString lastSaveDir = saveSettings.value("FileDialogs/lastSaveDirectory").toString();
    if (lastSaveDir.isEmpty() || !QDir(lastSaveDir).exists()) {
        lastSaveDir = QDir::homePath();
    }
    QString defaultPath = lastSaveDir + "/" + defaultName + ".snb";
    
    filePath = QFileDialog::getSaveFileName(
        this,
        isEdgeless ? tr("Save Canvas") : tr("Save Document"),
        defaultPath,
        tr("SpeedyNote Bundle (*.snb)")
    );
    
    if (filePath.isEmpty()) {
        return false; // User cancelled
    }
    
    saveSettings.setValue("FileDialogs/lastSaveDirectory", QFileInfo(filePath).absolutePath());
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
    {
        QPixmap thumbnail;
        if (isEdgeless) {
            thumbnail = renderEdgelessThumbnail(doc);
        } else if (doc->pageCount() > 0) {
            thumbnail = m_pagePanel ? m_pagePanel->thumbnailForPage(0) : QPixmap();
            if (thumbnail.isNull()) {
                // THREAD SAFETY FIX: Cancel any background thumbnail rendering before
                // accessing Document::page() directly. Background renders also call
                // Document::page() which modifies m_loadedPages without synchronization.
                if (m_pagePanel) {
                    m_pagePanel->cancelPendingRenders();
                }
                thumbnail = renderPage0Thumbnail(doc);
            }
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
    
    if (!m_documentManager || !tabManager()) {
        #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "saveDocument: DocumentManager or TabManager not initialized";
        #endif
        return;
    }

    DocumentViewport* viewport = tabManager()->currentViewport();
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
            
    // Sync position before saving (for restoring position on reload)
    syncDocumentPosition(doc, viewport);
            
    if (!existingPath.isEmpty() && !isUsingTemp) {
        // ✅ Document was previously saved to permanent location - save in-place
        if (!m_documentManager->saveDocument(doc)) {
            QMessageBox::critical(this, tr("Save Error"),
                tr("Failed to save document to:\n%1").arg(existingPath));
        return;
    }

        // Update tab title (clear modified flag)
        int currentIndex = tabManager()->currentIndex();
        if (currentIndex >= 0) {
            tabManager()->markTabModified(currentIndex, false);
        }
        
        // Phase P.4.6: Save thumbnail to NotebookLibrary
        {
            QPixmap thumbnail;
            if (isEdgeless) {
                thumbnail = renderEdgelessThumbnail(doc);
            } else if (doc->pageCount() > 0) {
                thumbnail = m_pagePanel ? m_pagePanel->thumbnailForPage(0) : QPixmap();
                if (thumbnail.isNull()) {
                    // THREAD SAFETY FIX: Cancel any background thumbnail rendering before
                    // accessing Document::page() directly. Background renders also call
                    // Document::page() which modifies m_loadedPages without synchronization.
                    if (m_pagePanel) {
                        m_pagePanel->cancelPendingRenders();
                    }
                    thumbnail = renderPage0Thumbnail(doc);
                }
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
    int currentIndex = tabManager()->currentIndex();
    if (currentIndex >= 0) {
        tabManager()->setTabTitle(currentIndex, doc->name);
        tabManager()->markTabModified(currentIndex, false);
    }
    if (m_navigationBar) {
        m_navigationBar->setFilename(doc->name);
    }
}

void MainWindow::loadDocument()
{
    // Phase doc-1.2: Load document from JSON file via file dialog
    // Uses DocumentManager for proper document ownership
    
    if (!m_documentManager || !tabManager()) {
        qWarning() << "loadDocument: DocumentManager or TabManager not initialized";
                return;
            }
    
    QString filePath;
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // BUG-A002 Fix: On Android/iOS, show list of saved documents from app-private storage.
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
    QSettings openSettings("SpeedyNote", "App");
    QString lastOpenDir = openSettings.value("FileDialogs/lastOpenDirectory").toString();
    if (lastOpenDir.isEmpty() || !QDir(lastOpenDir).exists()) {
        lastOpenDir = QDir::homePath();
    }
    
    QString filter = tr("SpeedyNote Files (*.snb *.pdf);;SpeedyNote Bundle (*.snb);;PDF Documents (*.pdf);;All Files (*)");
    filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Document"),
        lastOpenDir,
        filter
    );
    
    if (filePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    openSettings.setValue("FileDialogs/lastOpenDirectory", QFileInfo(filePath).absolutePath());
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
    int tabIndex = tabManager()->createTab(doc, doc->displayName());
    
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
    
    if (!tabManager()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addPageToDocument: No tab manager";
#endif
        return;
    }
    
    DocumentViewport* viewport = tabManager()->currentViewport();
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
        int currentIndex = tabManager()->currentIndex();
        if (currentIndex >= 0) {
            tabManager()->markTabModified(currentIndex, true);
        }
        
        // Update PagePanel and action bar
        notifyPageStructureChanged(doc);
    }
}

void MainWindow::insertPageInDocument()
{
    // Phase 3: Insert new page after current page
    // Works for both PDF and non-PDF documents (inserted page has no PDF background)
    
    if (!tabManager()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertPageInDocument: No tab manager";
#endif
        return;
        }
    
    DocumentViewport* viewport = tabManager()->currentViewport();
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
        int tabIndex = tabManager()->currentIndex();
        if (tabIndex >= 0) {
            tabManager()->markTabModified(tabIndex, true);
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
    
    if (!tabManager()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deletePageInDocument: No tab manager";
#endif
        return;
    }

    DocumentViewport* viewport = tabManager()->currentViewport();
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
    int tabIndex = tabManager()->currentIndex();
    if (tabIndex >= 0) {
        tabManager()->markTabModified(tabIndex, true);
    }
    
    // Update PagePanel and action bar
    notifyPageStructureChanged(doc, newPage);
}

void MainWindow::openPdfDocument(const QString &filePath)
{
    // Phase doc-1.4: Open PDF file and create PDF-backed document
    // Uses DocumentManager for proper document ownership

    if (!m_documentManager || !tabManager()) {
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
#elif defined(Q_OS_IOS)
        // Async: UIDocumentPickerViewController is a remote VC whose result
        // is delivered via XPC — cannot be received in a nested QEventLoop.
        // Re-call openPdfDocument(path) once the user has picked a file.
        PdfPickerIOS::pickPdfFile([this](const QString& picked) {
            if (!picked.isEmpty()) {
                openPdfDocument(picked);
            }
        });
        return;
#else
        QSettings pdfSettings("SpeedyNote", "App");
        QString lastPdfDir = pdfSettings.value("FileDialogs/lastOpenDirectory").toString();
        if (lastPdfDir.isEmpty() || !QDir(lastPdfDir).exists()) {
            lastPdfDir = QDir::homePath();
        }
        
        QString filter = tr("PDF Files (*.pdf);;All Files (*)");
        pdfPath = QFileDialog::getOpenFileName(
            this,
            tr("Open PDF"),
            lastPdfDir,
            filter
        );

        if (pdfPath.isEmpty()) {
            // User cancelled
            return;
        }
        
        pdfSettings.setValue("FileDialogs/lastOpenDirectory", QFileInfo(pdfPath).absolutePath());
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
    int tabIndex = tabManager()->createTab(doc, doc->displayName());
    
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
    if (!tabManager() || !m_documentManager) {
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
        
        // Load background settings (dark-mode-aware defaults)
        // Default: Grid with 32px spacing (32 divides evenly into 1024px tiles)
        bool dark = isDarkMode();
        Page::BackgroundType defaultStyle = static_cast<Page::BackgroundType>(
            settings.value("background/type", static_cast<int>(Page::BackgroundType::Grid)).toInt());
        QColor defaultBgColor = QColor(settings.value("background/color", dark ? "#2b2b2b" : "#ffffff").toString());
        QColor defaultGridColor = QColor(settings.value("background/gridColor", dark ? "#404040" : "#c8c8c8").toString());
        int defaultGridSpacing = settings.value("background/gridSpacing", 32).toInt();
        int defaultLineSpacing = settings.value("background/lineSpacing", 32).toInt();
        
        // Update document defaults for future pages
        doc->defaultPageSize = defaultPageSize;
        doc->defaultBackgroundType = defaultStyle;
        doc->defaultBackgroundColor = defaultBgColor;
        doc->defaultGridColor = defaultGridColor;
        doc->defaultGridSpacing = defaultGridSpacing;
        doc->defaultLineSpacing = defaultLineSpacing;
        
        // Also apply to the first page (already created by Document::createNew).
        // Use setPageSize() so both the Page object AND the layout metadata
        // (used by pageSizeAt() / viewport layout) are updated together.
        if (doc->pageCount() > 0) {
            doc->setPageSize(0, defaultPageSize);
            Page* firstPage = doc->page(0);
            if (firstPage) {
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
    int tabIndex = tabManager()->createTab(doc, tabTitle);
    
    // Note: zoomToWidth() is called automatically by DocumentViewport::setDocument()
    // for new paged documents, which also handles horizontal centering.
}

void MainWindow::addNewEdgelessTab()
{
    // Phase E7: Create a new edgeless (infinite canvas) document
    if (!tabManager() || !m_documentManager) {
        qWarning() << "addNewEdgelessTab: TabManager or DocumentManager not initialized";
        return;
    }
    
    // Create a new edgeless document
    Document* doc = m_documentManager->createEdgelessDocument();
    if (!doc) {
        qWarning() << "addNewEdgelessTab: Failed to create edgeless document";
        return;
    }
    
    // Apply default background settings from user preferences (dark-mode-aware defaults)
    // Default: Grid with 32px spacing (32 divides evenly into 1024px tiles)
    {
        QSettings settings("SpeedyNote", "App");
        bool dark = isDarkMode();
        Page::BackgroundType defaultStyle = static_cast<Page::BackgroundType>(
            settings.value("background/type", static_cast<int>(Page::BackgroundType::Grid)).toInt());
        QColor defaultBgColor = QColor(settings.value("background/color", dark ? "#2b2b2b" : "#ffffff").toString());
        QColor defaultGridColor = QColor(settings.value("background/gridColor", dark ? "#404040" : "#c8c8c8").toString());
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
    int tabIndex = tabManager()->createTab(doc, tabTitle);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Created new edgeless tab at index" << tabIndex << "with document:" << tabTitle;
#endif
    
    // For edgeless, center on origin (0,0)
    QTimer::singleShot(0, this, [this, tabIndex]() {
        if (tabManager()) {
            DocumentViewport* viewport = tabManager()->viewportAt(tabIndex);
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
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // On Android/iOS, just use the regular loadDocument() which shows a list dialog
    loadDocument();
    return;
#endif
    
    // Show directory dialog to select .snb bundle folder
    QSettings bundleSettings("SpeedyNote", "App");
    QString lastBundleDir = bundleSettings.value("FileDialogs/lastOpenDirectory").toString();
    if (lastBundleDir.isEmpty() || !QDir(lastBundleDir).exists()) {
        lastBundleDir = QDir::homePath();
    }
    
    QString bundlePath = QFileDialog::getExistingDirectory(
        this,
        tr("Open SpeedyNote Bundle (.snb folder)"),
        lastBundleDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (bundlePath.isEmpty()) {
        // User cancelled
        return;
    }
    
    bundleSettings.setValue("FileDialogs/lastOpenDirectory", QFileInfo(bundlePath).absolutePath());
    
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
    if (tabManager()) {
        tabManager()->closeTab(index);
    }
}

// Phase 3.1.4: New accessor for DocumentViewport
DocumentViewport* MainWindow::currentViewport() const {
    return m_splitViewManager ? m_splitViewManager->activeViewport() : nullptr;
}

int MainWindow::tabCount() const {
    return m_splitViewManager ? m_splitViewManager->totalTabCount() : 0;
}

void MainWindow::switchToTabIndex(int index) {
    if (tabManager() && index >= 0 && index < tabManager()->tabCount()) {
        TabBar* bar = (m_splitViewManager->activePane() == SplitViewManager::Left)
            ? m_splitViewManager->leftTabBar()
            : m_splitViewManager->rightTabBar();
        if (bar) bar->setCurrentIndex(index);
    }
}


void MainWindow::toggleFullscreen() {
    bool goingFullscreen = !isFullScreen();
    if (goingFullscreen) {
        showFullScreen();
    } else {
        showNormal();
    }
    if (m_navigationBar) {
        m_navigationBar->setFullscreenChecked(goingFullscreen);
    }
}

void MainWindow::showJumpToPageDialog() {
    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) return;
    
    // Edgeless documents have only one infinite canvas - no pages to jump to
    if (vp->document()->isEdgeless()) {
        return;
    }
    
    int currentPage = vp->currentPageIndex() + 1;
    int maxPage = vp->document()->pageCount();
    
    bool ok;
    int newPage = QInputDialog::getInt(this, tr("Jump to Page"), tr("Enter Page Number:"), 
                                       currentPage, 1, maxPage, 1, &ok);
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
#ifdef Q_OS_LINUX
    // Palm rejection: catch tablet proximity events at application level.
    // These fire once when stylus enters/leaves the tablet's detection range.
    if (event->type() == QEvent::TabletEnterProximity) {
        onStylusProximityEnter();
        return false;  // Don't consume - let DocumentViewport handle it too
    }
    if (event->type() == QEvent::TabletLeaveProximity) {
        onStylusProximityLeave();
        return false;  // Don't consume
    }
#endif

    // Pan tool hold: cancel if application loses focus (KeyRelease won't arrive)
    if (m_panHoldActive && event->type() == QEvent::ApplicationDeactivate) {
        if (auto* vp = currentViewport()) {
            vp->setCurrentTool(m_toolBeforePanHold);
        }
        m_panHoldActive = false;
    }
    
    // Pan tool hold: H key spring-loaded activation
    // setCurrentTool emits toolChanged which updates the toolbar automatically
    if (m_panHoldKey && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == m_panHoldKey && !ke->isAutoRepeat() && !m_panHoldActive) {
            QWidget* focused = QApplication::focusWidget();
            if (!qobject_cast<QLineEdit*>(focused) && !qobject_cast<QTextEdit*>(focused)
                && !qobject_cast<QPlainTextEdit*>(focused)) {
                if (auto* vp = currentViewport()) {
                    if (vp->currentTool() != ToolType::Pan) {
                        m_toolBeforePanHold = vp->currentTool();
                        m_panHoldActive = true;
                        vp->setCurrentTool(ToolType::Pan);
                    }
                }
            }
        }
    }
    if (m_panHoldActive && event->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == m_panHoldKey && !ke->isAutoRepeat()) {
            if (auto* vp = currentViewport()) {
                vp->setCurrentTool(m_toolBeforePanHold);
            }
            m_panHoldActive = false;
        }
    }

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
#elif defined(Q_OS_IOS)
    return IOSPlatformHelper::isDarkMode();
#else
    // On Linux and other platforms, use palette-based detection
    QColor bg = palette().color(QPalette::Window);
    return bg.lightness() < 128;  // Lightness scale: 0 (black) - 255 (white)
#endif
}

QColor MainWindow::getDefaultPenColor() {
    return isDarkMode() ? Qt::white : Qt::black;
}

void MainWindow::setPdfDarkModeEnabled(bool enabled) {
    if (m_splitViewManager) {
        m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
            for (int i = 0; i < tm->tabCount(); ++i) {
                if (DocumentViewport* vp = tm->viewportAt(i))
                    vp->setPdfDarkModeEnabled(enabled);
            }
        });
    }
}

void MainWindow::setSkipImageMasking(bool skip) {
    if (m_splitViewManager) {
        m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
            for (int i = 0; i < tm->tabCount(); ++i) {
                if (DocumentViewport* vp = tm->viewportAt(i))
                    vp->setSkipImageMasking(skip);
            }
        });
    }
}

QColor MainWindow::getAccentColor() const {
    if (useCustomAccentColor && customAccentColor.isValid()) {
        return customAccentColor;
    }
    
    QPalette palette = QGuiApplication::palette();
    QColor systemHighlight = palette.highlight().color();
    
#if defined(Q_OS_ANDROID) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
    // Qt's generic fallback Highlight is #0078d7 (Windows 10's blue), used when
    // the platform provides no real accent color (lightweight Linux DEs, Android).
    // Replace with SpeedyNote's own light/dark defaults.
    static const QColor qtFallbackBlue(0, 120, 215);
    if (systemHighlight == qtFallbackBlue) {
        QColor bg = palette.color(QPalette::Window);
        return (bg.lightness() < 128) ? QColor("#316882") : QColor("#cffff5");
    }
#endif
    
    return systemHighlight;
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

    // Refresh page panel thumbnails so they reflect the new colours
    if (m_pagePanel) {
        m_pagePanel->invalidateAllThumbnails();
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
    
    // Phase SV: SplitViewManager handles theming for all tab bars (current + future)
    if (m_splitViewManager) {
        m_splitViewManager->updateTheme(darkMode, accentColor);
    }
    
    // Update all DocumentViewports across both panes
    if (m_splitViewManager) {
        QSettings s("SpeedyNote", "App");
        bool pdfDarkMode = s.value("display/pdfDarkMode", true).toBool();
        bool skipMasking = s.value("display/skipImageMasking", false).toBool();
        m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
            for (int i = 0; i < tm->tabCount(); ++i) {
                if (DocumentViewport* vp = tm->viewportAt(i)) {
                    vp->setDarkMode(darkMode);
                    vp->setPdfDarkModeEnabled(pdfDarkMode);
                    vp->setSkipImageMasking(skipMasking);
                }
            }
        });
    }
    
    // Update OCR text object backgrounds for the new theme
    {
        DocumentViewport* vp = currentViewport();
        Document* doc = vp ? vp->document() : nullptr;
        if (doc) doc->setOcrDarkMode(darkMode);
    }
    if (m_toolbar && m_toolbar->ocrSubToolbar()->isShowTextEnabled()) {
        setOcrTextVisibility(true);
    }

    // Phase 2B: Sync floating text editor theme
    if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
        m_floatingTextEditor->setDarkMode(darkMode);
    }

    // Sync thumbnail renderer dark mode state
    if (m_pagePanel) {
        QSettings s("SpeedyNote", "App");
        bool pdfDarkMode = darkMode && s.value("display/pdfDarkMode", true).toBool();
        m_pagePanel->setPdfDarkMode(pdfDarkMode);
    }
    
    // REMOVED MW5.1: controlBar styling removed - replaced by NavigationBar and Toolbar
    
    // Unified gray colors: dark #2a2e32/#3a3e42/#4d4d4d, light #F5F5F5/#E8E8E8/#D0D0D0
    QString tabBgColor = darkMode ? "#2a2e32" : "#F5F5F5";
    QString tabHoverColor = darkMode ? "#3a3e42" : "#E8E8E8";
    QString tabBorderColor = darkMode ? "#4d4d4d" : "#D0D0D0";
    
    // MW2.2: Removed dial toolbar styling
    
    // Phase S3: Floating sidebar tab styling removed - using LeftSidebarContainer
    // Update left sidebar container theme
    if (m_leftSidebar) {
        m_leftSidebar->updateTheme(darkMode);
    }
    
    // Style the resizable sidebar splitter handle
    if (m_contentSplitter) {
        QString handleColor = darkMode ? "#4d4d4d" : "#D0D0D0";
        m_contentSplitter->setStyleSheet(QString(
            "QSplitter::handle:horizontal {"
            "  background-color: %1;"
            "  width: 3px;"
            "}"
        ).arg(handleColor));
    }
    
    // Update ActionBarContainer theme
    if (m_actionBarContainer) {
        m_actionBarContainer->setDarkMode(darkMode);
    }

    // Phase M.8: propagate theme into the right-hand markdown notes sidebar
    // (and through it to NotesTreePanel + NotesTreeDelegate).  Done last so
    // any stylesheet that depends on the global palette sees the final state.
    if (markdownNotesSidebar) {
        markdownNotesSidebar->setDarkMode(darkMode);
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
    
#ifdef Q_OS_LINUX
    // If user explicitly sets Disabled, palm rejection override is no longer needed.
    // If user changes to a non-Disabled mode while palm rejection is active (stylus in
    // proximity), save the preference but keep viewport at Disabled - the restore timer
    // will apply the new preference when the stylus leaves.
    if (m_palmRejectionActive) {
        if (mode == TouchGestureMode::Disabled) {
            // User disabled touch manually - clear palm rejection state entirely
            m_palmRejectionTimer->stop();
            m_palmRejectionActive = false;
        }
        // For non-Disabled modes: save preference (done below), but skip viewport
        // update to keep palm rejection override in effect.
    }
#endif
    
    // TG.6: Apply touch gesture mode to current DocumentViewport
    if (DocumentViewport* vp = currentViewport()) {
#ifdef Q_OS_LINUX
        // Don't override palm rejection - viewport stays Disabled until stylus leaves
        if (m_palmRejectionActive) {
            vp->setTouchGestureMode(TouchGestureMode::Disabled);
        } else
#endif
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
    
#ifdef Q_OS_LINUX
    // Load palm rejection settings (Linux only)
    m_palmRejectionEnabled = settings.value("palmRejection/enabled", false).toBool();
    m_palmRejectionDelayMs = settings.value("palmRejection/delayMs", 500).toInt();
#endif
    
    // Load theme settings
    loadThemeSettings();
}

// ==================== Palm Rejection (Linux Only) ====================

#ifdef Q_OS_LINUX
bool MainWindow::isPalmRejectionEnabled() const {
    return m_palmRejectionEnabled;
}

void MainWindow::setPalmRejectionEnabled(bool enabled) {
    m_palmRejectionEnabled = enabled;
    
    // If disabling while palm rejection is actively suppressing touch, restore immediately
    if (!enabled && m_palmRejectionActive) {
        m_palmRejectionTimer->stop();
        m_palmRejectionActive = false;
        if (DocumentViewport* vp = currentViewport()) {
            vp->setTouchGestureMode(touchGestureMode);
        }
    }
    
    QSettings settings("SpeedyNote", "App");
    settings.setValue("palmRejection/enabled", enabled);
}

int MainWindow::getPalmRejectionDelay() const {
    return m_palmRejectionDelayMs;
}

void MainWindow::setPalmRejectionDelay(int delayMs) {
    m_palmRejectionDelayMs = delayMs;
    
    QSettings settings("SpeedyNote", "App");
    settings.setValue("palmRejection/delayMs", delayMs);
}

void MainWindow::onStylusProximityEnter() {
    if (!m_palmRejectionEnabled) return;
    
    // Only affect active touch gesture modes (YAxisOnly and Full)
    if (touchGestureMode == TouchGestureMode::Disabled) return;
    
    // Cancel any pending restore (stylus came back before delay elapsed)
    m_palmRejectionTimer->stop();
    m_palmRejectionActive = true;
    
    // Directly disable touch on current viewport without changing the user's setting.
    // touchGestureMode remains unchanged so toolbar/settings stay correct.
    if (DocumentViewport* vp = currentViewport()) {
        vp->setTouchGestureMode(TouchGestureMode::Disabled);
    }
}

void MainWindow::onStylusProximityLeave() {
    if (!m_palmRejectionEnabled || !m_palmRejectionActive) return;
    
    // Start delay timer - touch gestures will be restored when it fires.
    // This delay prevents accidental palm touches immediately after lifting the stylus.
    m_palmRejectionTimer->start(m_palmRejectionDelayMs);
}
#endif

void MainWindow::wheelEvent(QWheelEvent *event) {
    // MW2.2: Forward to base class - dial wheel handling removed
    QMainWindow::wheelEvent(event);
}

// ==================== MW5.8: Pan Slider Management ====================

bool MainWindow::hasPhysicalKeyboard() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6: enumerate input devices and look for a keyboard
    const auto devices = QInputDevice::devices();
    for (const QInputDevice *device : devices) {
        if (device->type() == QInputDevice::DeviceType::Keyboard) {
            return true;
        }
    }
    return false;
#else
    // Qt5: QInputDevice does not exist; assume a physical keyboard is present
    // (desktop / Win32 target always has a keyboard)
    return true;
#endif
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
    QWidget *container = m_splitViewManager ? m_splitViewManager->viewportSplitter() : nullptr;
    if (!container || !panXSlider || !panYSlider || !m_splitViewManager) return;
    
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
    
    // Update action bar position
    updateActionBarPosition();
    
    // Update PDF search bar position
    updatePdfSearchBarPosition();
}

// =========================================================================
// Subtoolbar Signal Wiring
// =========================================================================

void MainWindow::connectSubToolbarSignals()
{
    // Subtoolbars are now owned by the Toolbar (via ExpandableToolButtons).
    // This method connects subtoolbar signals to viewport actions.

    auto* penST = m_toolbar->penSubToolbar();
    auto* markerST = m_toolbar->markerSubToolbar();
    auto* highlighterST = m_toolbar->highlighterSubToolbar();
    auto* objectST = m_toolbar->objectSelectSubToolbar();
    auto* eraserST = m_toolbar->eraserSubToolbar();

    // Pen
    connect(penST, &PenSubToolbar::penColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) vp->setPenColor(color);
    });
    connect(penST, &PenSubToolbar::penThicknessChanged, this, [this](qreal thickness) {
        if (DocumentViewport* vp = currentViewport()) vp->setPenThickness(thickness);
    });
    connect(penST, &PenSubToolbar::penMinStrokeWidthChanged, this, [this](qreal minWidth) {
        if (DocumentViewport* vp = currentViewport()) vp->setPenMinStrokeWidth(minWidth);
    });

    // Marker
    connect(markerST, &MarkerSubToolbar::markerColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) vp->setMarkerColor(color);
    });
    connect(markerST, &MarkerSubToolbar::markerThicknessChanged, this, [this](qreal thickness) {
        if (DocumentViewport* vp = currentViewport()) vp->setMarkerThickness(thickness);
    });

    // Highlighter
    connect(highlighterST, &HighlighterSubToolbar::highlighterColorChanged, this, [this](const QColor& color) {
        if (DocumentViewport* vp = currentViewport()) vp->setHighlighterColor(color);
    });
    connect(highlighterST, &HighlighterSubToolbar::autoHighlightStyleChanged, this,
            [this](HighlighterSubToolbar::HighlightStyle style) {
        if (DocumentViewport* vp = currentViewport())
            vp->setAutoHighlightStyle(static_cast<DocumentViewport::HighlightStyle>(style));
    });
    connect(highlighterST, &HighlighterSubToolbar::selectionSourceChanged, this,
            [this](HighlighterSubToolbar::SelectionSource src) {
        if (DocumentViewport* vp = currentViewport()) {
            auto mode = (src == HighlighterSubToolbar::SelectionSource::Ocr)
                            ? DocumentViewport::HighlighterMode::Ocr
                            : DocumentViewport::HighlighterMode::Pdf;
            vp->setHighlighterMode(mode);
        }
    });

    // ObjectSelect
    connect(objectST, &ObjectSelectSubToolbar::insertModeChanged, this,
            [this](DocumentViewport::ObjectInsertMode mode) {
        if (DocumentViewport* vp = currentViewport()) vp->setObjectInsertMode(mode);
    });
    connect(objectST, &ObjectSelectSubToolbar::actionModeChanged, this,
            [this](DocumentViewport::ObjectActionMode mode) {
        if (DocumentViewport* vp = currentViewport()) vp->setObjectActionMode(mode);
    });
    connect(objectST, &ObjectSelectSubToolbar::slotActivated, this, [this](int index) {
        if (DocumentViewport* vp = currentViewport()) vp->activateLinkSlot(index);
    });
    connect(objectST, &ObjectSelectSubToolbar::slotCleared, this, [this](int index) {
        if (DocumentViewport* vp = currentViewport()) vp->clearLinkSlot(index);
    });

    // Eraser
    connect(eraserST, &EraserSubToolbar::eraserSizeChanged, this, [this](qreal size) {
        if (DocumentViewport* vp = currentViewport()) vp->setEraserSize(size);
    });
    connect(eraserST, &EraserSubToolbar::eraserModeChanged, this, [this](int mode) {
        if (DocumentViewport* vp = currentViewport())
            vp->setEraserMode(static_cast<DocumentViewport::EraserMode>(mode));
    });

    // LinkObject color
    connect(objectST, &ObjectSelectSubToolbar::linkObjectColorChanged,
            this, [this](const QColor& color) {
        DocumentViewport* vp = currentViewport();
        if (!vp) return;
        const auto& selectedObjects = vp->selectedObjects();
        if (selectedObjects.size() != 1) return;
        LinkObject* link = dynamic_cast<LinkObject*>(selectedObjects.first());
        if (!link) return;
        link->iconColor = color;
        if (Document* doc = vp->document()) {
            Page* page = doc->page(vp->currentPageIndex());
            if (page) {
                int pageIndex = doc->pageIndexByUuid(page->uuid);
                if (pageIndex >= 0) doc->markPageDirty(pageIndex);
            }
        }
        vp->update();
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            // Phase M.8: update just this LinkObject in place (no preview reload,
            // no collapse of expanded subtrees, no focus loss).
            markdownNotesSidebar->updateLinkObject(link->id, link->description, color);
        }
    });

    // LinkObject description
    connect(objectST, &ObjectSelectSubToolbar::linkObjectDescriptionChanged,
            this, [this](const QString& description) {
        DocumentViewport* vp = currentViewport();
        if (!vp) return;
        const auto& selectedObjects = vp->selectedObjects();
        if (selectedObjects.size() != 1) return;
        LinkObject* link = dynamic_cast<LinkObject*>(selectedObjects.first());
        if (!link) return;
        link->description = description;
        if (Document* doc = vp->document()) {
            Page* page = doc->page(vp->currentPageIndex());
            if (page) {
                int pageIndex = doc->pageIndexByUuid(page->uuid);
                if (pageIndex >= 0) doc->markPageDirty(pageIndex);
            }
        }
        vp->update();
        if (markdownNotesSidebar && markdownNotesSidebar->isVisible()) {
            markdownNotesSidebar->updateLinkObject(link->id, description, link->iconColor);
        }
    });

    // Tab changes: per-tab state management via Toolbar (keyed by unique tab IDs).
    // Also force refresh when the active pane changes (safety net in case tab IDs
    // ever collide, though globally unique IDs from Fix 3 prevent that).
    connect(m_splitViewManager, &SplitViewManager::activeViewportChanged, this, [this](DocumentViewport* vp) {
        int newTabId = tabManager() ? tabManager()->currentTabId() : -1;
        int newPaneId = m_splitViewManager ? static_cast<int>(m_splitViewManager->activePane()) : 0;
        bool paneChanged = (newPaneId != m_previousPaneId);

        if (newTabId != m_previousTabId || paneChanged) {
            m_toolbar->onTabChanged(newTabId, m_previousTabId);

            if (vp) {
                ToolType currentTool = vp->currentTool();
                m_toolbar->setCurrentTool(currentTool);
                applyAllSubToolbarValuesToViewport(vp);
            }
            m_previousTabId = newTabId;
            m_previousPaneId = newPaneId;
        }
    });

    // Apply initial preset values to first viewport
    QTimer::singleShot(0, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            applyAllSubToolbarValuesToViewport(vp);
        }
    });

    // OCR subtoolbar
    auto* ocrST = m_toolbar->ocrSubToolbar();
    connect(ocrST, &OcrSubToolbar::scanPageClicked, this, &MainWindow::triggerOcrForCurrentPage);
    connect(ocrST, &OcrSubToolbar::scanAllClicked, this, &MainWindow::triggerOcrForAllPages);
    connect(ocrST, &OcrSubToolbar::autoOcrToggled, this, [this](bool enabled) {
        m_autoOcrEnabled = enabled;
        if (!enabled && m_ocrDebounceTimer)
            m_ocrDebounceTimer->stop();
    });
    connect(ocrST, &OcrSubToolbar::showTextToggled, this, [this](bool enabled) {
        setOcrTextVisibility(enabled);
    });
    connect(ocrST, &OcrSubToolbar::confidenceToggled, this, [this](bool enabled) {
        setOcrConfidenceVisibility(enabled);
    });
    connect(ocrST, &OcrSubToolbar::snapToGridToggled, this, [this](bool enabled) {
        DocumentViewport* vp = currentViewport();
        Document* doc = vp ? vp->document() : nullptr;
        if (doc) {
            doc->ocrSnapToBackground = enabled;
            doc->modified = true;
        }
    });
}

// ============================================================================
// Smart Tool Auto-Switch (split-view copy/paste assistance)
// ============================================================================

void MainWindow::applyToolOverrideForClipboard(ToolType requiredTool)
{
    if (!m_splitViewManager || !m_splitViewManager->isSplit())
        return;

    DocumentViewport* inactiveVp = m_splitViewManager->inactiveViewport();
    if (!inactiveVp)
        return;

    if (m_toolOverrideViewport == inactiveVp) {
        if (inactiveVp->currentTool() == requiredTool)
            return;
        inactiveVp->setCurrentTool(requiredTool);
        return;
    }

    if (m_toolOverrideViewport)
        clearToolOverride(true);

    if (inactiveVp->currentTool() == requiredTool)
        return;

    m_toolOverrideSavedTool = inactiveVp->currentTool();
    m_toolOverrideViewport = inactiveVp;
    inactiveVp->setCurrentTool(requiredTool);
}

void MainWindow::clearToolOverride(bool revert)
{
    if (!m_toolOverrideViewport)
        return;

    if (revert)
        m_toolOverrideViewport->setCurrentTool(m_toolOverrideSavedTool);

    m_toolOverrideViewport = nullptr;
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
            if (m_toolOverrideViewport && m_toolOverrideViewport != vp)
                clearToolOverride(true);
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
            if (m_toolOverrideViewport && m_toolOverrideViewport != vp)
                clearToolOverride(true);
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
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::aspectRatioLockRequested, this, [this]() {
        if (DocumentViewport* vp = currentViewport()) {
            vp->toggleImageAspectRatioLock();
        }
    });
    connect(m_objectSelectActionBar, &ObjectSelectActionBar::ocrLockToggleRequested, this, [this]() {
        DocumentViewport* vp = currentViewport();
        if (!vp) return;
        const auto& sel = vp->selectedObjects();
        if (sel.size() != 1) return;
        auto* ocr = dynamic_cast<OcrTextObject*>(sel.first());
        if (!ocr) return;
        bool newState = !ocr->ocrLocked;
        ocr->ocrLocked = newState;
        vp->pushOcrLockUndo(QVector<QString>{ocr->id}, newState);
        m_objectSelectActionBar->updateOcrLockSelection(true, newState);
        vp->update();
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
            if (m_toolOverrideViewport && m_toolOverrideViewport != vp)
                clearToolOverride(true);
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
    
    Document *doc = vp->document();
    if (!doc) {
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
    if (!doc) {
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
        startPage = doc->isEdgeless() ? 0 : vp->currentPageIndex();
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
    if (!doc) {
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
        startPage = doc->isEdgeless() ? 0 : vp->currentPageIndex();
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
    
    Document* doc = vp->document();
    if (!doc) {
        return;
    }
    
    // Update search state
    m_searchState->currentPageIndex = match.pageIndex;
    m_searchState->currentMatchIndex = match.matchIndex;
    m_searchState->currentPageMatches = pageMatches;
    
    // Find the index of current match within pageMatches
    int currentIdx = -1;
    for (int i = 0; i < pageMatches.size(); ++i) {
        if (pageMatches[i].matchIndex == match.matchIndex) {
            currentIdx = i;
            break;
        }
    }

    if (match.source == PdfSearchMatch::OcrTextTile) {
        // Edgeless OCR match: pan viewport to the tile containing the match
        int tileSize = Document::EDGELESS_TILE_SIZE;
        QPointF docCenter(
            static_cast<qreal>(match.tileX) * tileSize + match.boundingRect.center().x(),
            static_cast<qreal>(match.tileY) * tileSize + match.boundingRect.center().y());
        vp->navigateToEdgelessPosition(match.tileX, match.tileY, docCenter);
        vp->setSearchMatches(pageMatches, currentIdx, match.pageIndex);

    } else {
        // Paged match (PdfText or OcrText): pageIndex is a notebook page index
        vp->scrollToPage(match.pageIndex);
        vp->setSearchMatches(pageMatches, currentIdx, match.pageIndex);
    }
    
    // Clear any previous "not found" status
    m_pdfSearchBar->clearStatus();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MainWindow] Search match found: source=" << static_cast<int>(match.source)
             << "pageIndex=" << match.pageIndex 
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
// OCR: Setup, scan actions, debounce, and result handlers
// =========================================================================

void MainWindow::setupOcr()
{
    qRegisterMetaType<QVector<VectorStroke>>("QVector<VectorStroke>");
    qRegisterMetaType<QVector<QString>>("QVector<QString>");
    qRegisterMetaType<QSet<QString>>("QSet<QString>");
    qRegisterMetaType<QVector<QVector<VectorStroke>>>("QVector<QVector<VectorStroke>>");
    qRegisterMetaType<QVector<QSet<QString>>>("QVector<QSet<QString>>");
    qRegisterMetaType<QVector<OcrTextBlock>>("QVector<OcrTextBlock>");
    qRegisterMetaType<OcrSnapParams>("OcrSnapParams");
    qRegisterMetaType<QVector<OcrSnapParams>>("QVector<OcrSnapParams>");

    // Start with OCR disabled; the worker thread will report availability after init.
    // (Avoids creating WinRT COM objects on the main thread.)
    m_toolbar->setOcrAvailable(false);

    m_ocrWorker = new OcrWorker();
    m_ocrThread = new QThread(this);
    m_ocrWorker->moveToThread(m_ocrThread);
    connect(m_ocrThread, &QThread::finished, m_ocrWorker, &QObject::deleteLater);

    connect(m_ocrThread, &QThread::started, m_ocrWorker, &OcrWorker::initEngine);
    connect(m_ocrWorker, &OcrWorker::engineReady, this, [this](bool available) {
        m_toolbar->setOcrAvailable(available);
    }, Qt::QueuedConnection);
    connect(m_ocrWorker, &OcrWorker::languagesAvailable, this, [this](const QStringList& langs) {
        m_ocrAvailableLanguages = langs;
    }, Qt::QueuedConnection);

    m_ocrThread->start();

    connect(m_ocrWorker, &OcrWorker::resultsReady,
            this, &MainWindow::onOcrResultsReady, Qt::QueuedConnection);
    connect(m_ocrWorker, &OcrWorker::batchFinished,
            this, &MainWindow::onOcrBatchFinished, Qt::QueuedConnection);
    connect(m_ocrWorker, &OcrWorker::error,
            this, &MainWindow::onOcrError, Qt::QueuedConnection);

    m_ocrDebounceTimer = new QTimer(this);
    m_ocrDebounceTimer->setSingleShot(true);
    m_ocrDebounceTimer->setInterval(5000);
    connect(m_ocrDebounceTimer, &QTimer::timeout, this, &MainWindow::onDebounceTimeout);
}

void MainWindow::triggerOcrForCurrentPage()
{
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc || !m_ocrWorker) return;

    QMetaObject::invokeMethod(m_ocrWorker, "setLanguage", Qt::QueuedConnection,
        Q_ARG(QString, resolveOcrLanguage(doc)));

    m_toolbar->ocrSubToolbar()->setStatusText(tr("Scanning..."));

    const QString docTag = doc->sessionId();

    if (doc->isEdgeless()) {
        for (auto coord : doc->allLoadedTileCoords()) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            QVector<VectorStroke> strokes = collectPageStrokes(tile);
            if (!strokes.isEmpty())
                QMetaObject::invokeMethod(m_ocrWorker, "processPage", Qt::QueuedConnection,
                    Q_ARG(QString, docTag + QStringLiteral("|") +
                                   QString("%1,%2").arg(coord.first).arg(coord.second)),
                    Q_ARG(QVector<VectorStroke>, strokes),
                    Q_ARG(QSet<QString>, tile->suppressedStrokeIds),
                    Q_ARG(OcrSnapParams, buildOcrSnapParams(doc, tile)));
        }
    } else {
        int pageIndex = vp->currentPageIndex();
        Page* page = doc->page(pageIndex);
        if (!page) return;
        QVector<VectorStroke> strokes = collectPageStrokes(page);
        QMetaObject::invokeMethod(m_ocrWorker, "processPage", Qt::QueuedConnection,
            Q_ARG(QString, docTag + QStringLiteral("|") + page->uuid),
            Q_ARG(QVector<VectorStroke>, strokes),
            Q_ARG(QSet<QString>, page->suppressedStrokeIds),
            Q_ARG(OcrSnapParams, buildOcrSnapParams(doc, page)));
    }
}

void MainWindow::triggerOcrForAllPages()
{
    Document* doc = currentViewport() ? currentViewport()->document() : nullptr;
    if (!doc || !m_ocrWorker) return;

    // Evict any leftover temp tiles from a previous scan-all that may not have finished
    if (!m_ocrTempLoadedTiles.empty() && m_ocrTempLoadedDoc &&
        m_ocrTempLoadedDoc->isEdgeless()) {
        for (const auto& coord : m_ocrTempLoadedTiles)
            m_ocrTempLoadedDoc->evictTile(coord);
    }
    m_ocrTempLoadedTiles.clear();
    m_ocrTempLoadedDoc = nullptr;

    QMetaObject::invokeMethod(m_ocrWorker, "setLanguage", Qt::QueuedConnection,
        Q_ARG(QString, resolveOcrLanguage(doc)));

    m_toolbar->ocrSubToolbar()->setStatusText(tr("Scanning all pages..."));

    QVector<QString> pageIds;
    QVector<QVector<VectorStroke>> strokeSets;
    QVector<QSet<QString>> suppressedSets;
    QVector<OcrSnapParams> snapParamsVec;

    const QString docTag = doc->sessionId();

    if (doc->isEdgeless()) {
        auto allCoords = doc->allKnownTileCoords();
        auto loadedBefore = doc->allLoadedTileCoords();
        std::set<Document::TileCoord> loadedSet(loadedBefore.begin(), loadedBefore.end());

        for (const auto& coord : allCoords) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            QVector<VectorStroke> strokes = collectPageStrokes(tile);
            if (strokes.isEmpty()) {
                if (loadedSet.count(coord) == 0)
                    doc->evictTile(coord);
                continue;
            }
            if (loadedSet.count(coord) == 0)
                m_ocrTempLoadedTiles.insert(coord);
            pageIds.append(docTag + QStringLiteral("|") +
                           QString("%1,%2").arg(coord.first).arg(coord.second));
            strokeSets.append(strokes);
            suppressedSets.append(tile->suppressedStrokeIds);
            snapParamsVec.append(buildOcrSnapParams(doc, tile));
        }

        if (!m_ocrTempLoadedTiles.empty())
            m_ocrTempLoadedDoc = doc;
    } else {
        for (int i = 0; i < doc->pageCount(); ++i) {
            Page* page = doc->page(i);
            if (!page) continue;
            pageIds.append(docTag + QStringLiteral("|") + page->uuid);
            strokeSets.append(collectPageStrokes(page));
            suppressedSets.append(page->suppressedStrokeIds);
            snapParamsVec.append(buildOcrSnapParams(doc, page));
        }
    }

    if (!pageIds.isEmpty())
        QMetaObject::invokeMethod(m_ocrWorker, "processBatch", Qt::QueuedConnection,
            Q_ARG(QVector<QString>, pageIds),
            Q_ARG(QVector<QVector<VectorStroke>>, strokeSets),
            Q_ARG(QVector<QSet<QString>>, suppressedSets),
            Q_ARG(QVector<OcrSnapParams>, snapParamsVec));
}

void MainWindow::onDebounceTimeout()
{
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc || !m_ocrWorker) return;

    QMetaObject::invokeMethod(m_ocrWorker, "setLanguage", Qt::QueuedConnection,
        Q_ARG(QString, resolveOcrLanguage(doc)));

    const QString docTag = doc->sessionId();

    if (doc->isEdgeless()) {
        auto dirtyTiles = vp->takeOcrDirtyTiles();
        if (dirtyTiles.empty()) return;

        bool anyQueued = false;
        for (const auto& coord : dirtyTiles) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            QVector<VectorStroke> strokes = collectPageStrokes(tile);
            if (strokes.isEmpty()) continue;
            if (!anyQueued) {
                m_toolbar->ocrSubToolbar()->setStatusText(tr("Auto-scanning..."));
                anyQueued = true;
            }
            QMetaObject::invokeMethod(m_ocrWorker, "processPageIncremental", Qt::QueuedConnection,
                Q_ARG(QString, docTag + QStringLiteral("|") +
                               QString("%1,%2").arg(coord.first).arg(coord.second)),
                Q_ARG(QVector<VectorStroke>, strokes),
                Q_ARG(QSet<QString>, tile->suppressedStrokeIds),
                Q_ARG(OcrSnapParams, buildOcrSnapParams(doc, tile)));
        }
    } else {
        int idx = vp->currentPageIndex();
        Page* page = doc->page(idx);
        if (!page) return;

        QVector<VectorStroke> strokes = collectPageStrokes(page);
        if (strokes.isEmpty()) return;

        m_toolbar->ocrSubToolbar()->setStatusText(tr("Auto-scanning..."));
        QMetaObject::invokeMethod(m_ocrWorker, "processPageIncremental", Qt::QueuedConnection,
            Q_ARG(QString, docTag + QStringLiteral("|") + page->uuid),
            Q_ARG(QVector<VectorStroke>, strokes),
            Q_ARG(QSet<QString>, page->suppressedStrokeIds),
            Q_ARG(OcrSnapParams, buildOcrSnapParams(doc, page)));
    }
}

void MainWindow::onOcrResultsReady(const QString& pageId, const QVector<OcrTextBlock>& blocks)
{
    Page* page = nullptr;
    Document* doc = nullptr;
    Document::TileCoord tileCoord{0, 0};
    bool tileCoordResolved = false;

    // Split the tag prefix (docSessionId|localId). The docTag ties this result
    // back to the exact Document that queued it, so sibling edgeless notebooks
    // with the same tile coords can never receive each other's OCR output.
    QString docTag;
    QString localId = pageId;
    const int sep = pageId.indexOf(QLatin1Char('|'));
    if (sep > 0) {
        docTag  = pageId.left(sep);
        localId = pageId.mid(sep + 1);
    }

    Document* targetDoc = nullptr;
    if (!docTag.isEmpty()) {
        for (int d = 0; d < m_documentManager->documentCount(); ++d) {
            Document* candidate = m_documentManager->documentAt(d);
            if (candidate && candidate->sessionId() == docTag) {
                targetDoc = candidate;
                break;
            }
        }
        // Owning document was closed between queue and response -> drop.
        if (!targetDoc) return;
    }

    // Edgeless tile lookup ("tx,ty"). When tagged, restrict to targetDoc;
    // when untagged (legacy), fall back to the first match across all docs.
    QStringList parts = localId.split(',');
    if (parts.size() == 2) {
        bool okX, okY;
        int tx = parts[0].toInt(&okX);
        int ty = parts[1].toInt(&okY);
        if (okX && okY) {
            if (targetDoc) {
                if (targetDoc->isEdgeless()) {
                    if (Page* tile = targetDoc->getTile(tx, ty)) {
                        page = tile;
                        doc = targetDoc;
                        tileCoord = {tx, ty};
                        tileCoordResolved = true;
                    }
                }
            } else {
                for (int d = 0; d < m_documentManager->documentCount(); ++d) {
                    Document* candidate = m_documentManager->documentAt(d);
                    if (candidate && candidate->isEdgeless()) {
                        Page* tile = candidate->getTile(tx, ty);
                        if (tile) {
                            page = tile;
                            doc = candidate;
                            tileCoord = {tx, ty};
                            tileCoordResolved = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Fallback: UUID-based lookup (paged mode).
    if (!page) {
        if (targetDoc) {
            int idx = targetDoc->pageIndexByUuid(localId);
            if (idx >= 0) {
                page = targetDoc->page(idx);
                doc = targetDoc;
            }
        } else {
            for (int d = 0; d < m_documentManager->documentCount(); ++d) {
                Document* candidate = m_documentManager->documentAt(d);
                if (!candidate) continue;

                int idx = candidate->pageIndexByUuid(localId);
                if (idx >= 0) {
                    page = candidate->page(idx);
                    doc = candidate;
                    break;
                }
            }
        }
    }
    if (!page || !doc) return;

    page->ocrTextBlocks = blocks;
    page->ocrDirty = false;

    syncOcrTextObjects(page, blocks);

    // Invalidate the Highlighter's OCR-block cache on any viewport currently
    // showing this document, so a new drag will see the refreshed OCR data.
    // For tiles we can't cheaply compute a "page index" - invalidate wholesale.
    if (m_splitViewManager) {
        const int pageIdx = tileCoordResolved ? -1 : doc->pageIndexByUuid(localId);
        m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
            if (!tm) return;
            const int n = tm->tabCount();
            for (int i = 0; i < n; ++i) {
                DocumentViewport* vp = tm->viewportAt(i);
                if (vp && vp->document() == doc) {
                    vp->invalidateOcrBlockCache(pageIdx);
                }
            }
        });
    }

    if (tileCoordResolved)
        doc->saveTileOcr(tileCoord);
    else
        doc->savePageOcr(localId, page);

    int wordCount = 0;
    for (const auto& b : blocks)
        if (!b.text.isEmpty()) ++wordCount;

    m_toolbar->ocrSubToolbar()->setStatusText(
        tr("Done: %1 words").arg(wordCount));
    m_toolbar->ocrSubToolbar()->clearStatusAfterDelay(5000);

    if (m_toolbar->ocrSubToolbar()->isShowTextEnabled()) {
        if (DocumentViewport* vp = currentViewport())
            vp->update();
    }
}

void MainWindow::onOcrBatchFinished(int pagesScanned, int pagesWithText)
{
    m_toolbar->ocrSubToolbar()->setStatusText(
        tr("OCR complete: %1 pages scanned, %2 with text")
            .arg(pagesScanned).arg(pagesWithText));
    m_toolbar->ocrSubToolbar()->clearStatusAfterDelay(8000);

    if (!m_ocrTempLoadedTiles.empty() && m_ocrTempLoadedDoc) {
        for (const auto& coord : m_ocrTempLoadedTiles)
            m_ocrTempLoadedDoc->evictTile(coord);
    }
    m_ocrTempLoadedTiles.clear();
    m_ocrTempLoadedDoc = nullptr;
}

void MainWindow::onOcrError(const QString& pageId, const QString& message)
{
    // Drop errors whose owning Document has been closed, and silence errors
    // from a non-current document so user doesn't see A's failure on B's
    // toolbar after switching tabs. Legacy untagged pageIds (no '|') fall
    // through and always surface (backwards-compat safety net).
    const int sep = pageId.indexOf(QLatin1Char('|'));
    if (sep > 0) {
        const QString docTag = pageId.left(sep);
        Document* owningDoc = nullptr;
        for (int d = 0; d < m_documentManager->documentCount(); ++d) {
            Document* candidate = m_documentManager->documentAt(d);
            if (candidate && candidate->sessionId() == docTag) {
                owningDoc = candidate;
                break;
            }
        }
        if (!owningDoc)
            return;

        DocumentViewport* vp = currentViewport();
        if (!vp || vp->document() != owningDoc)
            return;
    }

    m_toolbar->ocrSubToolbar()->setStatusText(tr("OCR error: %1").arg(message));
    m_toolbar->ocrSubToolbar()->clearStatusAfterDelay(8000);
}

QVector<VectorStroke> MainWindow::collectPageStrokes(const Page* page) const
{
    QVector<VectorStroke> allStrokes;
    for (const auto& layer : page->vectorLayers) {
        if (layer && layer->visible)
            allStrokes.append(layer->strokes());
    }
    return allStrokes;
}

void MainWindow::syncOcrTextObjects(Page* page, const QVector<OcrTextBlock>& blocks)
{
    if (!page) return;

    // Close floating editor if its target is an unlocked OCR text on this page
    if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
        if (auto* tgt = m_floatingTextEditor->target()) {
            if (tgt->type() == QLatin1String("ocr_text") && page->objectById(tgt->id)) {
                auto* ocrTgt = static_cast<OcrTextObject*>(tgt);
                if (!ocrTgt->ocrLocked)
                    m_floatingTextEditor->closeEditor();
            }
        }
    }

    // Collect locked stroke IDs to suppress, and remove only unlocked OCR objects
    QSet<QString> suppressedStrokeIds;
    QVector<QString> toRemove;
    for (const auto& obj : page->objects) {
        if (obj && obj->type() == QStringLiteral("ocr_text")) {
            auto* ocr = static_cast<OcrTextObject*>(obj.get());
            if (ocr->ocrLocked) {
                for (const auto& sid : ocr->sourceStrokeIds)
                    suppressedStrokeIds.insert(sid);
            } else {
                toRemove.append(obj->id);
            }
        }
    }
    for (const auto& oid : toRemove)
        page->removeObject(oid);

    bool showText = m_toolbar->ocrSubToolbar()->isShowTextEnabled();
    bool showConf = m_toolbar->ocrSubToolbar()->isConfidenceEnabled();
    bool dark = isDarkMode();

    // Pre-compute snap rendering state once for all blocks
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;
    bool isGrid = (page->backgroundType == Page::BackgroundType::Grid);
    bool isLines = (page->backgroundType == Page::BackgroundType::Lines);
    bool pageSnap = doc && doc->ocrSnapToBackground && (isGrid || isLines);
    int snapSpacing = isGrid ? page->gridSpacing : page->lineSpacing;
    bool pageCjk = false;
    if (pageSnap && isGrid) {
        QSettings settings("SpeedyNote", "App");
        if (settings.value("ocrCjkGridMode", false).toBool()) {
            pageCjk = isCjkOcrLanguage(resolveOcrLanguage(doc));
        }
    }

    for (const auto& block : blocks) {
        if (block.dirty || block.text.isEmpty())
            continue;

        // Skip blocks whose strokes are claimed by locked objects
        bool suppressed = false;
        for (const auto& sid : block.sourceStrokeIds) {
            if (suppressedStrokeIds.contains(sid)) {
                suppressed = true;
                break;
            }
        }
        if (suppressed) continue;

        QColor color = OcrTextObject::dominantStrokeColor(page, block.sourceStrokeIds);
        auto obj = OcrTextObject::createFromBlock(block, color, dark);
        obj->visible = showText;
        obj->showConfidence = showConf;
        obj->layerAffinity = OcrTextObject::resolveLayerAffinity(page, block.sourceStrokeIds);
        obj->ocrSnapEnabled = pageSnap;
        obj->ocrGridSpacing = snapSpacing;
        obj->ocrCjkGridMode = pageCjk;

        page->addObject(std::move(obj));
    }
}

void MainWindow::setOcrTextVisibility(bool visible)
{
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc) return;

    bool dark = isDarkMode();
    doc->setOcrTextVisible(visible);
    doc->setOcrDarkMode(dark);
    QColor bg = dark ? QColor(40, 40, 40, 180) : QColor(255, 255, 255, 180);

    bool snapEnabled = doc->ocrSnapToBackground;
    bool cjkGlobal = false;
    if (snapEnabled) {
        QSettings snapSettings("SpeedyNote", "App");
        cjkGlobal = snapSettings.value("ocrCjkGridMode", false).toBool();
    }

    auto setOnPage = [this, visible, bg, doc, snapEnabled, cjkGlobal](Page* page) {
        if (!page) return;

        bool isGrid = (page->backgroundType == Page::BackgroundType::Grid);
        bool isLines = (page->backgroundType == Page::BackgroundType::Lines);
        bool pageSnap = snapEnabled && (isGrid || isLines);
        int spacing = isGrid ? page->gridSpacing : page->lineSpacing;
        bool pageCjk = false;
        if (pageSnap && cjkGlobal && isGrid) {
            pageCjk = isCjkOcrLanguage(resolveOcrLanguage(doc));
        }

        for (const auto& obj : page->objects) {
            if (obj && obj->type() == QStringLiteral("ocr_text")) {
                obj->visible = visible;
                auto* ocr = static_cast<OcrTextObject*>(obj.get());
                ocr->backgroundColor = bg;
                ocr->ocrSnapEnabled = pageSnap;
                ocr->ocrGridSpacing = spacing;
                ocr->ocrCjkGridMode = pageCjk;
            }
        }
    };

    if (doc->isEdgeless()) {
        for (auto coord : doc->allLoadedTileCoords()) {
            Page* tile = doc->getTile(coord.first, coord.second);
            setOnPage(tile);
        }
    } else {
        for (int i : doc->loadedPageIndices()) {
            setOnPage(doc->page(i));
        }
    }

    vp->update();
}

void MainWindow::setOcrConfidenceVisibility(bool enabled)
{
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;
    if (!doc) return;

    doc->setOcrShowConfidence(enabled);

    auto setOnPage = [enabled](Page* page) {
        if (!page) return;
        for (const auto& obj : page->objects) {
            if (obj && obj->type() == QStringLiteral("ocr_text")) {
                static_cast<OcrTextObject*>(obj.get())->showConfidence = enabled;
            }
        }
    };

    if (doc->isEdgeless()) {
        for (auto coord : doc->allLoadedTileCoords()) {
            Page* tile = doc->getTile(coord.first, coord.second);
            setOnPage(tile);
        }
    } else {
        for (int i : doc->loadedPageIndices()) {
            setOnPage(doc->page(i));
        }
    }

    vp->update();
}

QString MainWindow::resolveOcrLanguage(Document* doc) const
{
    if (doc && !doc->ocrLanguage.isEmpty())
        return doc->ocrLanguage;
    QSettings settings("SpeedyNote", "App");
    return settings.value("ocrLanguage").toString();
}

OcrSnapParams MainWindow::buildOcrSnapParams(Document* doc, Page* page) const
{
    OcrSnapParams snap;
    if (!doc || !page) return snap;

    snap.enabled = doc->ocrSnapToBackground;
    QSettings settings("SpeedyNote", "App");
    snap.cjkGridMode = settings.value("ocrCjkGridMode", false).toBool();
    snap.gridSpacing = page->gridSpacing;
    snap.lineSpacing = page->lineSpacing;
    snap.backgroundIsGrid = (page->backgroundType == Page::BackgroundType::Grid);
    snap.backgroundIsLines = (page->backgroundType == Page::BackgroundType::Lines);
    return snap;
}

void MainWindow::showOcrLanguageDialog()
{
    DocumentViewport* vp = currentViewport();
    Document* doc = vp ? vp->document() : nullptr;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("OCR Language"));
    dlg.setMinimumWidth(380);

    auto* layout = new QVBoxLayout(&dlg);

    if (doc) {
        auto* docLabel = new QLabel(tr("Document: %1").arg(doc->name), &dlg);
        docLabel->setStyleSheet("font-weight: bold;");
        layout->addWidget(docLabel);
        layout->addSpacing(8);
    }

    auto* label = new QLabel(tr("Handwriting recognition language for this document:"), &dlg);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* combo = new QComboBox(&dlg);
    combo->addItem(tr("Use global setting"), QStringLiteral(""));
    combo->addItem(tr("Auto-detect (system default)"), QStringLiteral("auto"));

    // Partition languages: common first, then the rest sorted by display name
    static const QStringList commonTags = {
        QStringLiteral("en-US"), QStringLiteral("en-GB"),
        QStringLiteral("zh-Hani-CN"), QStringLiteral("zh-Hani-TW"),
        QStringLiteral("ja"), QStringLiteral("ko"),
        QStringLiteral("es-ES"), QStringLiteral("fr-FR"),
        QStringLiteral("de-DE"), QStringLiteral("pt-BR"),
        QStringLiteral("it-IT"), QStringLiteral("ru"), QStringLiteral("ar"),
    };

    auto displayNameForTag = [](const QString& tag) -> QString {
        QLocale locale(QString(tag).replace(QLatin1Char('-'), QLatin1Char('_')));
        QString name = locale.nativeLanguageName();
        if (name.isEmpty() || name == QLatin1String("C"))
            return tag;
        return QStringLiteral("%1 (%2)").arg(name, tag);
    };

    QStringList common, rest;
    for (const auto& lang : m_ocrAvailableLanguages) {
        if (commonTags.contains(lang))
            common.append(lang);
        else
            rest.append(lang);
    }

    // Sort rest alphabetically by display name
    std::sort(rest.begin(), rest.end(), [&](const QString& a, const QString& b) {
        return displayNameForTag(a).toLower() < displayNameForTag(b).toLower();
    });

    // Preserve the order of commonTags for the common section
    for (const auto& tag : commonTags) {
        if (common.contains(tag))
            combo->addItem(displayNameForTag(tag), tag);
    }
    if (!common.isEmpty() && !rest.isEmpty())
        combo->insertSeparator(combo->count());
    for (const auto& tag : rest)
        combo->addItem(displayNameForTag(tag), tag);

    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    if (combo->completer())
        combo->completer()->setFilterMode(Qt::MatchContains);

    layout->addWidget(combo);

    // Pre-select current value
    if (doc) {
        int idx = combo->findData(doc->ocrLanguage);
        if (idx >= 0) combo->setCurrentIndex(idx);
    }

    layout->addSpacing(8);

    auto* globalNote = new QLabel(
        tr("\"Use global setting\" inherits from Settings > Language > Handwriting Recognition Language."),
        &dlg);
    globalNote->setWordWrap(true);
    globalNote->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(globalNote);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QString chosen = combo->currentData().toString();

    if (doc) {
        doc->ocrLanguage = chosen;
        doc->modified = true;
    }

    QString effective = resolveOcrLanguage(doc);
    if (m_ocrWorker) {
        QMetaObject::invokeMethod(m_ocrWorker, "setLanguage", Qt::QueuedConnection,
            Q_ARG(QString, effective));
    }
}

// =========================================================================
// Phase 2B: Floating Text Editor
// =========================================================================

void MainWindow::openFloatingTextEditor(InsertedObject* obj)
{
    auto* textBox = dynamic_cast<TextBoxObject*>(obj);
    if (!textBox) return;

    // If target is an unlocked OcrTextObject, ask user to lock before editing
    auto* ocrObj = dynamic_cast<OcrTextObject*>(textBox);
    if (ocrObj && !ocrObj->ocrLocked) {
        auto result = QMessageBox::question(this, tr("Lock OCR Text"),
            tr("Lock this OCR text? It will no longer be updated by automatic scanning.\n\nProceed to lock and edit?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (result != QMessageBox::Yes) return;
        ocrObj->ocrLocked = true;
        if (DocumentViewport* vp = currentViewport())
            vp->pushOcrLockUndo(QVector<QString>{ocrObj->id}, true);
    }

    // Close existing session first (pushes undo if changed)
    if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
        m_floatingTextEditor->closeEditor();
    }

    // Lazy creation
    if (!m_floatingTextEditor) {
        m_floatingTextEditor = new FloatingTextEditor(this);
        connect(m_floatingTextEditor, &FloatingTextEditor::repaintRequested,
                this, [this]() {
            if (DocumentViewport* vp = currentViewport())
                vp->update();
        });
        connect(m_floatingTextEditor, &FloatingTextEditor::editorClosed,
                this, [this](const QString& objectId,
                             const QString& oldText, const QString& newText,
                             int oldAlign, int newAlign,
                             int oldOpacity, int newOpacity,
                             const QColor& oldFontColor, const QColor& newFontColor) {
            DocumentViewport* vp = currentViewport();
            if (!vp) return;
            InsertedObject* obj = nullptr;
            if (Document* doc = vp->document()) {
                if (doc->isEdgeless()) {
                    for (const auto& coord : doc->allLoadedTileCoords()) {
                        Page* tile = doc->getTile(coord.first, coord.second);
                        if (tile) { obj = tile->objectById(objectId); if (obj) break; }
                    }
                } else {
                    for (int i = 0; i < doc->pageCount(); ++i) {
                        Page* page = doc->page(i);
                        if (page) { obj = page->objectById(objectId); if (obj) break; }
                    }
                }
            }
            if (obj) {
                vp->pushObjectTextEditUndo(obj, oldText, newText,
                                           oldAlign, newAlign,
                                           oldOpacity, newOpacity,
                                           oldFontColor, newFontColor);
            }
        });
    }

    m_floatingTextEditor->setDarkMode(isDarkMode());
    m_floatingTextEditor->setTarget(textBox);

    // Position near the text box
    if (DocumentViewport* vp = currentViewport()) {
        QPointF docPos = textBox->position;

        if (Document* doc = vp->document()) {
            if (doc->isEdgeless()) {
                for (const auto& coord : doc->allLoadedTileCoords()) {
                    Page* tile = doc->getTile(coord.first, coord.second);
                    if (tile && tile->objectById(textBox->id)) {
                        docPos = QPointF(coord.first * Document::EDGELESS_TILE_SIZE,
                                         coord.second * Document::EDGELESS_TILE_SIZE)
                                 + textBox->position;
                        break;
                    }
                }
            } else {
                for (int i = 0; i < doc->pageCount(); ++i) {
                    Page* page = doc->page(i);
                    if (page && page->objectById(textBox->id)) {
                        docPos = vp->pageToDocument(i, textBox->position);
                        break;
                    }
                }
            }
        }

        QPointF vpPt = vp->documentToViewport(docPos);
        QPoint globalPt = vp->mapToGlobal(vpPt.toPoint());
        QPoint localPt = mapFromGlobal(globalPt);

        int editorW = m_floatingTextEditor->width();
        int editorH = m_floatingTextEditor->height();

        // Place to the right of the object, or fall back to below
        int x = localPt.x() + static_cast<int>(textBox->size.width() * vp->zoomLevel()) + 10;
        int y = localPt.y();

        if (x + editorW > width())
            x = qMax(0, localPt.x() - editorW - 10);
        if (y + editorH > height())
            y = qMax(0, height() - editorH);

        m_floatingTextEditor->move(x, y);
    }

    m_floatingTextEditor->show();
    m_floatingTextEditor->raise();
}

void MainWindow::closeFloatingTextEditor()
{
    if (m_floatingTextEditor && m_floatingTextEditor->isVisible()) {
        m_floatingTextEditor->closeEditor();
    }
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
    
    // Search: Toggle the PDF search bar (Ctrl+F)
    connect(m_pagePanelActionBar, &PagePanelActionBar::searchClicked, this, [this]() {
        showPdfSearchBar();
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
            int tabIndex = tabManager()->currentIndex();
            if (tabIndex >= 0) {
                tabManager()->markTabModified(tabIndex, true);
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
    
    // Check if the current document supports pages (independent of UI state)
    bool documentHasPages = false;
    if (DocumentViewport* vp = currentViewport()) {
        if (Document* doc = vp->document()) {
            documentHasPages = !doc->isEdgeless();
        }
    }
    m_actionBarContainer->setPagePanelDocumentSupported(documentHasPages);
    
    // Check if the page panel UI is active (sidebar visible + pages tab selected)
    bool panelActive = false;
    if (documentHasPages && m_leftSidebar && m_leftSidebar->isVisible()
        && m_leftSidebar->hasPagesTab()) {
        int pagesTabIndex = m_leftSidebar->indexOf(m_leftSidebar->pagePanel());
        panelActive = (m_leftSidebar->currentIndex() == pagesTabIndex);
    }
    m_actionBarContainer->setPagePanelVisible(panelActive);
    
    // Update action bar position after visibility change to ensure correct placement
    updateActionBarPosition();
    
    // Sync action bar state when bar will be shown
    if (documentHasPages && (panelActive || m_pagePanelActionBar->isLocked())) {
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
    // Note: pageIndex from outline is a PDF page index, not notebook page index
    // When pages are inserted between PDF pages, these differ
    connect(outlinePanel, &OutlinePanel::navigationRequested,
            this, [this](int pdfPageIndex, QPointF position) {
        if (DocumentViewport* vp = currentViewport()) {
            Document* doc = vp->document();
            if (!doc) return;
            
            // Convert PDF page index to notebook page index
            int notebookPageIndex = doc->notebookPageIndexForPdfPage(pdfPageIndex);
            if (notebookPageIndex < 0) {
                qWarning() << "Outline navigation: PDF page" << pdfPageIndex 
                           << "not found in notebook";
                return;
            }
            
            // Position values of -1 mean "not specified"
            if (position.x() >= 0 || position.y() >= 0) {
                // Scroll to exact position within the page (PDF provides normalized coords)
                vp->scrollToPositionOnPage(notebookPageIndex, position);
            } else {
                // No position specified - just scroll to the page top
                vp->scrollToPage(notebookPageIndex);
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
                    int tabIndex = tabManager()->currentIndex();
                    if (tabIndex >= 0) {
                        tabManager()->markTabModified(tabIndex, true);
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

TabManager* MainWindow::tabManager() const
{
    return m_splitViewManager ? m_splitViewManager->activeTabManager() : nullptr;
}

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

// ============================================================================
// Document Position Sync Helpers
// ============================================================================

bool MainWindow::syncDocumentPosition(Document* doc, DocumentViewport* vp)
{
    // Syncs viewport position to document WITHOUT marking modified.
    // Returns true if position was updated.
    // Caller decides whether to mark modified based on context.
    
    if (!doc || !vp) {
        return false;
    }
    
    if (doc->isEdgeless()) {
        // Edgeless: sync canvas position/zoom to document
        // Note: syncPositionToDocument() updates internal state but doesn't mark modified
        vp->syncPositionToDocument();
        return true;  // Position always "changes" for edgeless (can't easily detect)
    } else {
        // Paged: update lastAccessedPage if changed
        int currentPage = vp->currentPageIndex();
        if (doc->lastAccessedPage != currentPage) {
            doc->lastAccessedPage = currentPage;
            return true;  // Position actually changed
        }
        return false;  // Position unchanged
    }
}

void MainWindow::syncAllDocumentPositions()
{
    if (!m_splitViewManager || !m_documentManager)
        return;
    
    m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
        for (int i = 0; i < tm->tabCount(); ++i) {
            Document* doc = tm->documentAt(i);
            if (!doc) continue;
            DocumentViewport* vp = tm->viewportAt(i);
            if (!vp) continue;
            if (syncDocumentPosition(doc, vp)) {
                doc->markModified();
            }
        }
    });
}

// ============================================================================

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

QPixmap MainWindow::renderEdgelessThumbnail(Document* doc)
{
    if (!doc || !doc->isEdgeless()) {
        return QPixmap();
    }
    
    // Target thumbnail size (same as paged thumbnails)
    static constexpr int THUMBNAIL_WIDTH = 180;
    static constexpr int THUMBNAIL_HEIGHT = 180;  // Square for edgeless (no page aspect ratio)
    static constexpr qreal MAX_DPR = 2.0;
    
    qreal dpr = qMin(devicePixelRatioF(), MAX_DPR);
    int physicalWidth = static_cast<int>(THUMBNAIL_WIDTH * dpr);
    int physicalHeight = static_cast<int>(THUMBNAIL_HEIGHT * dpr);
    
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Determine the center point for the thumbnail viewport.
    // Primary: last_position (where the user was last looking).
    // Fallback: center of the first indexed tile if last_position has no nearby tiles.
    QPointF center = doc->edgelessLastPosition();
    
    // Define a virtual viewport: 1 tile centered on the position.
    // A single tile keeps content legible at small thumbnail sizes.
    qreal viewExtent = tileSize * 1.0;
    QRectF viewRect(center.x() - viewExtent / 2.0, center.y() - viewExtent / 2.0,
                    viewExtent, viewExtent);
    
    // Find tiles that intersect this viewport (with margin for strokes at edges).
    // tilesInRect() returns ALL coordinate positions in the range, even empty ones,
    // so we filter to keep only coordinates where an actual tile exists.
    static constexpr int STROKE_MARGIN = 100;
    QRectF marginRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN,
                                          STROKE_MARGIN, STROKE_MARGIN);
    QVector<Document::TileCoord> allTiles;
    {
        QVector<Document::TileCoord> candidates = doc->tilesInRect(marginRect);
        for (const auto& coord : candidates) {
            if (doc->getTile(coord.first, coord.second)) {
                allTiles.append(coord);
            }
        }
    }
    
    // Fallback: if no tiles with content near last_position, try the first loaded tile
    if (allTiles.isEmpty()) {
        QVector<Document::TileCoord> allCoords = doc->allTileCoords();
        if (!allCoords.isEmpty()) {
            // Use first tile and re-center viewport on it
            auto firstCoord = allCoords.first();
            center = QPointF(firstCoord.first * tileSize + tileSize / 2.0,
                             firstCoord.second * tileSize + tileSize / 2.0);
            viewRect = QRectF(center.x() - viewExtent / 2.0, center.y() - viewExtent / 2.0,
                              viewExtent, viewExtent);
            marginRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN,
                                           STROKE_MARGIN, STROKE_MARGIN);
            QVector<Document::TileCoord> fallbackCandidates = doc->tilesInRect(marginRect);
            for (const auto& coord : fallbackCandidates) {
                if (doc->getTile(coord.first, coord.second)) {
                    allTiles.append(coord);
                }
            }
        }
    }
    
    // Create the thumbnail pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(doc->defaultBackgroundColor);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale: map viewExtent doc units to THUMBNAIL_WIDTH logical pixels
    qreal scale = static_cast<qreal>(THUMBNAIL_WIDTH) / viewExtent;
    
    // Transform: translate so viewRect.topLeft maps to (0,0), then scale
    painter.scale(scale, scale);
    painter.translate(-viewRect.left(), -viewRect.top());
    
    // Pre-calculate visible tile range for background pass
    int minVisibleTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxVisibleTx = static_cast<int>(std::floor(viewRect.right() / tileSize));
    int minVisibleTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxVisibleTy = static_cast<int>(std::floor(viewRect.bottom() / tileSize));
    
    // ===== PASS 1: Render backgrounds for visible tiles =====
    // Render all tile positions in the view, even empty ones (they get default background)
    for (int tx = minVisibleTx; tx <= maxVisibleTx; ++tx) {
        for (int ty = minVisibleTy; ty <= maxVisibleTy; ++ty) {
            QRectF tileRect(tx * tileSize, ty * tileSize, tileSize, tileSize);
            Page* tile = doc->getTile(tx, ty);
            
            if (tile) {
                Page::renderBackgroundPattern(
                    painter, tileRect,
                    tile->backgroundColor, tile->backgroundType,
                    tile->gridColor, tile->gridSpacing, tile->lineSpacing,
                    1.0  // No zoom compensation needed for static thumbnail
                );
            } else {
                Page::renderBackgroundPattern(
                    painter, tileRect,
                    doc->defaultBackgroundColor, doc->defaultBackgroundType,
                    doc->defaultGridColor, doc->defaultGridSpacing, doc->defaultLineSpacing,
                    1.0
                );
            }
        }
    }
    
    // If no tiles with content exist, we're done (thumbnail shows just the background)
    if (allTiles.isEmpty()) {
        painter.end();
        return thumbnail;
    }
    
    // ===== PASS 2: Render objects with default affinity (-1) =====
    // These render below all stroke layers
    const auto& layers = doc->edgelessLayers();
    auto renderObjectsWithAffinity = [&](int affinity) {
        // Check if the tied layer is visible (affinity K ties to Layer K+1)
        int tiedLayerIndex = affinity + 1;
        if (tiedLayerIndex >= 0 && tiedLayerIndex < static_cast<int>(layers.size())) {
            if (!layers[tiedLayerIndex].visible) return;
        }
        
        for (const auto& coord : allTiles) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            auto it = tile->objectsByAffinity.find(affinity);
            if (it == tile->objectsByAffinity.end() || it->second.empty()) continue;
            
            QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
            
            // Sort by z-order
            std::vector<InsertedObject*> objs = it->second;
            std::sort(objs.begin(), objs.end(),
                      [](InsertedObject* a, InsertedObject* b) {
                          return a->zOrder < b->zOrder;
                      });
            
            for (InsertedObject* obj : objs) {
                if (!obj->visible) continue;
                painter.save();
                painter.translate(tileOrigin);
                obj->render(painter, 1.0);
                painter.restore();
            }
        }
    };
    
    renderObjectsWithAffinity(-1);
    
    // ===== PASS 3: Interleaved layer strokes and objects =====
    int maxLayerCount = 0;
    for (const auto& coord : allTiles) {
        Page* tile = doc->getTile(coord.first, coord.second);
        if (tile) {
            maxLayerCount = qMax(maxLayerCount, tile->layerCount());
        }
    }
    
    for (int layerIdx = 0; layerIdx < maxLayerCount; ++layerIdx) {
        // PASS 3a: Render this layer's strokes from all tiles
        for (const auto& coord : allTiles) {
            Page* tile = doc->getTile(coord.first, coord.second);
            if (!tile) continue;
            if (layerIdx >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(layerIdx);
            if (!layer || !layer->visible) continue;
            
            QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
            painter.save();
            painter.translate(tileOrigin);
            layer->render(painter);
            painter.restore();
        }
        
        // PASS 3b: Render objects with affinity = layerIdx
        renderObjectsWithAffinity(layerIdx);
    }
    
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
        // Read Launcher state BEFORE we reset it below.
        const bool launcherMaximized  = launcher->isMaximized();
        const bool launcherFullScreen = launcher->isFullScreen();
        const QPoint launcherPos  = launcher->pos();
        const QSize  launcherSize = launcher->size();
        
        // Show MainWindow in the Launcher's window state.
        // Reset stale native state on the hidden MainWindow (same reasoning
        // as the Launcher reset in the other branch — QWidget skips the
        // platform update for hidden widgets, but QWindow does not).
        setWindowState(Qt::WindowNoState);
        if (QWindow* win = windowHandle()) {
            win->setWindowState(Qt::WindowNoState);
        }
        setWindowOpacity(0.0);
        if (launcherMaximized) {
            showMaximized();
        } else if (launcherFullScreen) {
            showFullScreen();
        } else {
            showNormal();
            // Set geometry AFTER show so our move()/resize() has the final
            // word.  On Windows, ShowWindow(SW_SHOWNORMAL) can adjust the
            // position using stale placement data; applying geometry after
            // the show overrides that.  The window is at opacity 0, so the
            // brief intermediate position is invisible.
            move(launcherPos);
            resize(launcherSize);
        }
        raise();
        activateWindow();
        
        // Sync fullscreen button with the actual window state
        if (m_navigationBar) {
            m_navigationBar->setFullscreenChecked(isFullScreen());
        }
        
        // Restore Launcher to normal windowed state BEFORE hiding.
        // On Windows, hide() preserves the native window's fullscreen styling
        // (no decorations, full-screen geometry).  Qt's setWindowState() on a
        // *hidden* widget only updates the internal state variable — it skips
        // the platform-level update because isVisible() is false.  That means
        // a later showNormal() finds the native window still carrying stale
        // fullscreen styles, producing a frameless or full-screen window.
        // Transitioning while visible (behind MainWindow, opacity 0) forces the
        // window manager to properly restore the window frame.
        launcher->setWindowOpacity(0.0);
        if (launcher->windowState() != Qt::WindowNoState) {
            launcher->setWindowState(Qt::WindowNoState);
        }
        launcher->hide();
        launcher->setWindowOpacity(1.0);  // Reset for next time
        
        // Fade MainWindow in
        auto* fadeIn = new QPropertyAnimation(this, "windowOpacity");
        fadeIn->setDuration(fadeDuration);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
        
    } else {
        // ========== MAINWINDOW → LAUNCHER ==========
        const bool srcMaximized  = isMaximized();
        const bool srcFullScreen = isFullScreen();
        const QPoint srcPos  = pos();
        const QSize  srcSize = size();
        
        // Show Launcher in MainWindow's window state.
        // Reset stale fullscreen/maximized state at BOTH the QWidget and
        // QWindow level.  QWidget::setWindowState() on a hidden widget only
        // updates the internal flag — it skips the platform update because
        // isVisible() is false.  QWindow::setWindowState() has no such guard,
        // so calling it on windowHandle() forces the native window to restore
        // normal styling (decorations, geometry) even while hidden.
        launcher->setWindowState(Qt::WindowNoState);
        if (QWindow* win = launcher->windowHandle()) {
            win->setWindowState(Qt::WindowNoState);
        }
        launcher->setWindowOpacity(0.0);
        if (srcMaximized) {
            launcher->showMaximized();
        } else if (srcFullScreen) {
            launcher->showFullScreen();
        } else {
            launcher->show();
            launcher->move(srcPos);
            launcher->resize(srcSize);
        }
        launcher->raise();
        launcher->activateWindow();
        
        // Hide MainWindow immediately (no flicker since launcher is now on top)
        hide();
        setWindowOpacity(1.0);  // Reset for next time
        
        // Fade launcher in
        auto* fadeIn = new QPropertyAnimation(launcher, "windowOpacity");
        fadeIn->setDuration(fadeDuration);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
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
    updateActionBarPosition();
    updatePdfSearchBarPosition();
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
#ifdef Q_OS_IOS
    QTimer::singleShot(0, []{ IOSPlatformHelper::disableEditMenuOverlay(); });
#endif
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
    bool makeVisible = !isVisible;

    // Note: Markdown notes sidebar (right side) is independent of 
    // outline/bookmarks sidebars (left side), so we don't hide them here.
    // The left sidebars are mutually exclusive with each other, but not with markdown notes.

    // When transitioning from hidden → visible, re-apply the persisted
    // width.  QSplitter would otherwise reopen the panel at whatever
    // size the splitter last computed for it (often the minimum, if the
    // canvas got narrow while the panel was hidden).
    if (makeVisible && m_contentSplitter) {
        QSettings s("SpeedyNote", "App");
        int rightW = qBound(220, s.value("ui/rightSidebarWidth", 300).toInt(), 600);
        QList<int> sizes = m_contentSplitter->sizes();
        if (sizes.size() == 3) {
            const int total   = sizes[0] + sizes[1] + sizes[2];
            const int leftW   = sizes[0];
            const int canvasW = qMax(1, total - leftW - rightW);
            m_contentSplitter->setSizes({leftW, canvasW, rightW});
        }
    }

    markdownNotesSidebar->setVisible(makeVisible);
    markdownNotesSidebarVisible = makeVisible;
    
    // Sync NavigationBar button state when sidebar is toggled programmatically
    if (m_navigationBar) {
        m_navigationBar->setRightSidebarChecked(markdownNotesSidebarVisible);
    }
    
    // Phase M.3: Load notes when sidebar becomes visible
    if (markdownNotesSidebarVisible) {
        refreshNotesOutline();
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

// Phase M.8: Rebuild right-sidebar outline tree (no .md file I/O).
// The sidebar stores a compact LinkOutlineEntry per markdown-bearing link;
// previews and full bodies are loaded lazily by NotesTreePanel.
void MainWindow::refreshNotesOutline()
{
    if (!markdownNotesSidebar) return;

    DocumentViewport* vp = currentViewport();
    if (!vp || !vp->document()) {
        markdownNotesSidebar->setNotesDir(QString());
        markdownNotesSidebar->setOutline({}, /*edgeless=*/false);
        return;
    }

    // Phase M.9: enumerateLinkOutline() is now a flat snapshot of the
    // persistent link-outline cache; no force-load of pages/tiles.  The
    // outline survives tile eviction in edgeless mode — no hidden-tiles
    // warning is needed because the user never sees an incomplete view.
    Document* doc = vp->document();
    markdownNotesSidebar->setNotesDir(doc->notesPath());
    markdownNotesSidebar->setEdgelessMode(doc->isEdgeless());
    markdownNotesSidebar->setOutline(doc->enumerateLinkOutline(),
                                     doc->isEdgeless());
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
        // Phase M.9: Resolve owning tile from the persistent outline
        // cache — works even when the tile is not currently resident.
        int foundTileX = 0;
        int foundTileY = 0;
        QPointF cachedDocPos;
        bool gotFromCache = false;

        const QVector<LinkOutlineEntry> outline = doc->enumerateLinkOutline();
        for (const auto& entry : outline) {
            if (entry.linkObjectId == linkObjectId) {
                foundTileX   = entry.tileX;
                foundTileY   = entry.tileY;
                cachedDocPos = entry.docPos;
                gotFromCache = true;
                break;
            }
        }

        if (!gotFromCache) {
            qWarning() << "navigateToLinkObject: LinkObject not in outline cache:" << linkObjectId;
            return;
        }

        // Lazy-load the owning tile (getTile materializes it if needed).
        Page* tile = doc->getTile(foundTileX, foundTileY);
        if (tile) {
            for (const auto& objPtr : tile->objects) {
                if (objPtr->id == linkObjectId) {
                    foundObject = objPtr.get();
                    break;
                }
            }
        }

        // Fallback target: cache position if the tile / object can't
        // be resolved (e.g. disk file moved).  Navigation still works;
        // only object selection is skipped.
        QPointF objectCenter;
        if (foundObject) {
            const QPointF tileOrigin(foundTileX * Document::EDGELESS_TILE_SIZE,
                                      foundTileY * Document::EDGELESS_TILE_SIZE);
            objectCenter = tileOrigin + foundObject->position +
                QPointF(foundObject->size.width() / 2.0,
                        foundObject->size.height() / 2.0);
        } else {
            objectCenter = cachedDocPos +
                QPointF(LinkObject::ICON_SIZE / 2.0,
                        LinkObject::ICON_SIZE / 2.0);
        }

        vp->navigateToEdgelessPosition(foundTileX, foundTileY, objectCenter);
        if (foundObject) vp->selectObject(foundObject);

    } else {
        // Paged mode: resolve owning page from the outline cache, then
        // lazy-load only that page (Phase M.9).  Falls back to a full
        // linear scan if the cache miss ever happens.
        int currentPage = vp->currentPageIndex();
        int foundPageIndex = -1;

        const QVector<LinkOutlineEntry> outline = doc->enumerateLinkOutline();
        for (const auto& entry : outline) {
            if (entry.linkObjectId == linkObjectId) {
                foundPageIndex = entry.pageIndex;
                break;
            }
        }

        if (foundPageIndex >= 0) {
            Page* page = doc->page(foundPageIndex);
            if (page) {
                for (const auto& objPtr : page->objects) {
                    if (objPtr->id == linkObjectId) {
                        foundObject = objPtr.get();
                        break;
                    }
                }
            }
        }

        // Legacy fallback: outline cache missing this link for some
        // reason.  Scan loaded pages + current page so we don't crash.
        if (!foundObject) {
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
            if (!searchPage(currentPage)) {
                for (int pageIdx = 0; pageIdx < doc->pageCount(); ++pageIdx) {
                    if (pageIdx == currentPage) continue;
                    if (searchPage(pageIdx)) break;
                }
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#else
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result) {
#endif
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

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)

static bool isSupportedDropFile(const QString& path)
{
    if (path.endsWith(".pdf", Qt::CaseInsensitive)) return true;
    if (path.endsWith(".snbx", Qt::CaseInsensitive)) return true;
    if (path.endsWith(".snb", Qt::CaseInsensitive) && QFileInfo(path).isDir()) return true;
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile() && isSupportedDropFile(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile() && isSupportedDropFile(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    bool accepted = false;
    const QList<QUrl> urls = event->mimeData()->urls();

    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        QString filePath = url.toLocalFile();
        if (!isSupportedDropFile(filePath)) continue;

        accepted = true;
        openFileInNewTab(filePath);
    }

    if (accepted)
        event->acceptProposedAction();
    else
        event->ignore();
}

#endif // !Q_OS_ANDROID && !Q_OS_IOS

void MainWindow::saveSessionTabs()
{
    QSettings settings("SpeedyNote", "App");

    if (!m_splitViewManager || !m_documentManager || m_splitViewManager->totalTabCount() == 0) {
        settings.remove("session/lastOpenTabs");
        settings.remove("session/activeTabIndex");
        return;
    }

    QStringList paths;
    m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
        for (int i = 0; i < tm->tabCount(); ++i) {
            Document* doc = tm->documentAt(i);
            if (!doc) continue;

            QString docPath = m_documentManager->documentPath(doc);
            if (!docPath.isEmpty() && !m_documentManager->isUsingTempBundle(doc)) {
                paths.append(QFileInfo(docPath).absoluteFilePath());
            } else if (!doc->pdfPath().isEmpty()) {
                paths.append(QFileInfo(doc->pdfPath()).absoluteFilePath());
            }
        }
    });

    if (paths.isEmpty()) {
        settings.remove("session/lastOpenTabs");
        settings.remove("session/activeTabIndex");
    } else {
        settings.setValue("session/lastOpenTabs", paths);

        int globalActiveIndex = 0;
        if (m_splitViewManager->activePane() == SplitViewManager::Right
            && m_splitViewManager->rightTabManager()) {
            globalActiveIndex = m_splitViewManager->leftTabManager()->tabCount()
                              + m_splitViewManager->rightTabManager()->currentIndex();
        } else if (m_splitViewManager->leftTabManager()) {
            globalActiveIndex = m_splitViewManager->leftTabManager()->currentIndex();
        }
        settings.setValue("session/activeTabIndex", globalActiveIndex);
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // ========== UPDATE POSITIONS FOR ALL DOCUMENTS ==========
    // Before checking for unsaved changes, update positions for all documents
    // This ensures the position is saved even if the document was saved earlier in the session
    syncAllDocumentPositions();
    
    // ========== CHECK FOR UNSAVED DOCUMENTS ==========
    // Iterate through all tabs across both panes and prompt for unsaved documents
    // Returns false if user cancelled (abort quit)
    if (m_splitViewManager && m_documentManager) {
        auto checkPane = [&](TabManager* tm, TabBar* bar) -> bool {
            if (!tm) return true;
            for (int i = 0; i < tm->tabCount(); ++i) {
                Document* doc = tm->documentAt(i);
                if (!doc) continue;
                
                bool needsSavePrompt = false;
                bool isUsingTemp = m_documentManager->isUsingTempBundle(doc);
                
                if (doc->isEdgeless()) {
                    bool hasContent = doc->tileCount() > 0 || doc->tileIndexCount() > 0;
                    needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
                } else {
                    bool hasContent = doc->pageCount() > 0;
                    needsSavePrompt = doc->modified || (isUsingTemp && hasContent);
                }
                
                if (needsSavePrompt) {
                    if (bar) bar->setCurrentIndex(i);

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
                        return false;
                    }
                    
                    if (reply == QMessageBox::Save) {
                        QString existingPath = m_documentManager->documentPath(doc);
                        bool canSaveInPlace = !existingPath.isEmpty() && !isUsingTemp;
                        
                        if (canSaveInPlace) {
                            if (!m_documentManager->saveDocument(doc)) {
                                QMessageBox::critical(this, tr("Save Error"),
                                    tr("Failed to save document to:\n%1\n\nQuit anyway?").arg(existingPath));
                            }
                        } else {
                            if (!saveNewDocumentWithDialog(doc)) {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };

        if (!checkPane(m_splitViewManager->leftTabManager(), m_splitViewManager->leftTabBar())) {
            event->ignore();
            return;
        }
        if (!checkPane(m_splitViewManager->rightTabManager(), m_splitViewManager->rightTabBar())) {
            event->ignore();
            return;
        }
    }
    // ===========================================================
        
        // REMOVED MW7.4: Save bookmarks removed - bookmark implementation deleted
        // saveBookmarks();
    
    // Save session tabs for restore on next launch
    saveSessionTabs();

    // Flush NotebookLibrary to disk before exiting
    // This ensures any pending addToRecent() calls are persisted, even if
    // the debounced save timer hasn't fired yet. Critical for new documents
    // saved during closeEvent - without this, they won't appear in the Launcher.
    NotebookLibrary::instance()->save();
    
    // Accept the close event to allow the program to close
    event->accept();
}

// ========================================
// Single Instance Implementation
// ========================================

bool MainWindow::isInstanceRunning()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Android/iOS handle app lifecycle differently - always return false
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return;
#else
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
#endif
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
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
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

bool MainWindow::closeDocumentById(const QString& documentId, bool discardChanges)
{
    if (!m_splitViewManager) return true;

    bool result = true;
    bool found = false;
    m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane) {
        if (found) return;
        for (int i = 0; i < tm->tabCount(); ++i) {
            Document* doc = tm->documentAt(i);
            if (doc && doc->id == documentId) {
                found = true;
                if (!discardChanges) {
                    if (m_documentManager && m_documentManager->hasUnsavedChanges(doc)) {
                        QString existingPath = m_documentManager->documentPath(doc);
                        if (!existingPath.isEmpty()) {
                            if (!m_documentManager->saveDocument(doc)) {
                                QMessageBox::critical(this, tr("Save Error"),
                                    tr("Failed to save document before closing."));
                                result = false;
                                return;
                            }
                        } else {
                            if (!saveNewDocumentWithDialog(doc)) {
                                result = false;
                                return;
                            }
                        }
                    }
                }
                // Close the tab via the owning TabManager
                tm->closeTab(i);
                return;
            }
        }
    });
    
    return result;
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
    
    if (!m_documentManager || !tabManager()) {
        qWarning() << "openFileInNewTab: DocumentManager or TabManager not initialized";
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, tr("File Not Found"),
            tr("The file does not exist:\n%1").arg(filePath));
        return;
    }
    
    // Step 0: Check for duplicate documents (by ID, not path) across both panes
    QString suffix = fileInfo.suffix().toLower();
    if (suffix == "snb" || fileInfo.isDir()) {
        QString docId = Document::peekBundleId(filePath);
        if (!docId.isEmpty() && m_splitViewManager) {
            bool found = false;
            m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane pane) {
                if (found) return;
                for (int i = 0; i < tm->tabCount(); ++i) {
                    Document* existingDoc = tm->documentAt(i);
                    if (existingDoc && existingDoc->id == docId) {
                        TabBar* bar = (pane == SplitViewManager::Left)
                            ? m_splitViewManager->leftTabBar()
                            : m_splitViewManager->rightTabBar();
                        if (bar) bar->setCurrentIndex(i);
                        m_splitViewManager->setActivePane(pane);
                        m_documentManager->setDocumentPath(existingDoc, filePath);
                        found = true;
                        return;
                    }
                }
            });
            if (found) return;
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
    int tabIndex = tabManager()->createTab(doc, doc->displayName());
    
    if (tabIndex < 0) {
        QMessageBox::critical(this, tr("Open Error"),
            tr("Failed to create tab for:\n%1").arg(filePath));
        return;
    }
    
    // Step 4: createTab already switches to the new tab
    
    // Step 5: Mode-specific initial positioning
    // Use QTimer::singleShot(0) to ensure viewport geometry is ready
    bool isEdgeless = doc->isEdgeless();
    if (isEdgeless) {
        // Edgeless: Only set default position if document has no saved position
        // Documents with saved positions will have their position restored by DocumentViewport
        if (doc->edgelessLastPosition().isNull()) {
            QTimer::singleShot(0, this, [this, tabIndex]() {
                if (tabManager()) {
                    DocumentViewport* viewport = tabManager()->viewportAt(tabIndex);
                    if (viewport) {
                        // New document: center on origin (offset by a small margin)
                        viewport->setPanOffset(QPointF(-100, -100));
                    }
                }
            });
        }
        // else: Document has saved position - DocumentViewport::showEvent/resizeEvent will restore it
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
    return m_splitViewManager && m_splitViewManager->totalTabCount() > 0;
}

bool MainWindow::switchToDocument(const QString& bundlePath)
{
    if (bundlePath.isEmpty() || !m_splitViewManager || !m_documentManager) {
        return false;
    }
    
    QString normalizedPath = QFileInfo(bundlePath).absoluteFilePath();
    
    // Search through all open tabs across both panes
    bool found = false;
    m_splitViewManager->forEachTabManager([&](TabManager* tm, SplitViewManager::Pane pane) {
        if (found) return;
        for (int i = 0; i < tm->tabCount(); ++i) {
            Document* doc = tm->documentAt(i);
            if (!doc) continue;
            
            QString docPath = m_documentManager->documentPath(doc);
            if (docPath.isEmpty()) continue;
            
            if (QFileInfo(docPath).absoluteFilePath() == normalizedPath) {
                TabBar* bar = (pane == SplitViewManager::Left)
                    ? m_splitViewManager->leftTabBar()
                    : m_splitViewManager->rightTabBar();
                if (bar) bar->setCurrentIndex(i);
                m_splitViewManager->setActivePane(pane);
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "MainWindow::switchToDocument: Switched to existing tab for" << bundlePath;
#endif
                found = true;
                return;
            }
        }
    });
    
    return found;
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
    
    if (m_navigationBar) {
        m_navigationBar->setFullscreenChecked(isFullScreen());
    }
    
    if (wasHidden) {
        auto* fadeIn = new QPropertyAnimation(this, "windowOpacity");
        fadeIn->setDuration(150);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
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
