#include "InkCanvas.h"
#include "ToolType.h"
#include "PictureWindowManager.h"
#include "PictureWindow.h" // Include the full definition
#include "PictureSourceDialog.h"
#include "ExistingPictureDialog.h"
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QColor>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
// #include <QInputDevice>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QCache>
#include "MainWindow.h"
#include <QInputDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QNativeGestureEvent>
#include <QApplication>
#include <QtMath>
#include <QPainterPath>
#include <QTimer>
#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QBuffer>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUuid>
#include <QSettings>

#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QUuid>
#include <QFutureWatcher>

#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QMessageBox>

#include <poppler-qt6.h>




InkCanvas::InkCanvas(QWidget *parent) 
    : QWidget(parent), drawing(false), penColor(Qt::black), penThickness(5.0), currentTool(ToolType::Pen), zoomFactor(100), panOffsetX(0), panOffsetY(0) {    
    
    // Set theme-aware default pen color
    MainWindow *mainWindow = qobject_cast<MainWindow*>(parent);
    if (mainWindow) {
        penColor = mainWindow->getDefaultPenColor();
    }    
    setAttribute(Qt::WA_StaticContents);
    setTabletTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);  // Enable touch events
    
    // Enable pinch gesture recognition (for Linux/KDE trackpad pinch-to-zoom)
    grabGesture(Qt::PinchGesture);

    // Enable immediate updates for smoother animation
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    // Detect screen resolution and set canvas size
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize logicalSize = screen->availableGeometry().size() * 0.89;
        setMaximumSize(logicalSize); // Optional
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    } else {
        setFixedSize(1920, 1080); // Fallback size
    }
    
    initializeBuffer();
    pdfCache.setMaxCost(6);  // ✅ Ensures the cache holds at most 6 pages
    // No need to set auto-delete, QCache will handle deletion automatically
    
    // Initialize PDF text selection throttling timer (60 FPS = ~16.67ms)
    pdfTextSelectionTimer = new QTimer(this);
    pdfTextSelectionTimer->setSingleShot(true);
    pdfTextSelectionTimer->setInterval(16); // ~60 FPS
    connect(pdfTextSelectionTimer, &QTimer::timeout, this, &InkCanvas::processPendingTextSelection);
    
    // Initialize PDF cache timer (will be created on-demand)
    pdfCacheTimer = nullptr;
    currentCachedPage = -1;
    
    // Initialize note page cache system
    noteCache.setMaxCost(6); // Cache up to 6 note pages
    noteCacheTimer = nullptr;
    currentCachedNotePage = -1;
    
    // Initialize picture manager
    pictureManager = new PictureWindowManager(this, this);
    
    // Connect pan/zoom signals to update picture window positions
    connect(this, &InkCanvas::panChanged, pictureManager, &PictureWindowManager::updateAllWindowPositions);
    connect(this, &InkCanvas::zoomChanged, pictureManager, &PictureWindowManager::updateAllWindowPositions);
    
    // Initialize inertia scrolling timer (60 FPS for smooth animation)
    inertiaTimer = new QTimer(this);
    inertiaTimer->setInterval(16); // ~60 FPS
    connect(inertiaTimer, &QTimer::timeout, this, &InkCanvas::updateInertiaScroll);
    
    // Initialize touch panning timeout timer (Linux fix for stuck isTouchPanning state)
    // If no touch events arrive within 200ms, assume touch gesture ended abnormally
    touchPanningTimeoutTimer = new QTimer(this);
    touchPanningTimeoutTimer->setSingleShot(true);
    touchPanningTimeoutTimer->setInterval(200); // 200ms timeout
    connect(touchPanningTimeoutTimer, &QTimer::timeout, this, &InkCanvas::resetTouchPanningState);

    // Initialize trackpad scroll timeout timer (detects end of trackpad scroll gesture)
    // Windows doesn't provide scroll phases, so we detect gesture end via timeout
    trackpadScrollTimeoutTimer = new QTimer(this);
    trackpadScrollTimeoutTimer->setSingleShot(true);
    trackpadScrollTimeoutTimer->setInterval(300); // 300ms timeout - long enough to capture Windows trackpad momentum
    connect(trackpadScrollTimeoutTimer, &QTimer::timeout, this, &InkCanvas::resetTrackpadScrollingState);

    // Initialize trackpad pinch-zoom timeout timer (detects end of pinch-zoom gesture)
    trackpadPinchZoomTimeoutTimer = new QTimer(this);
    trackpadPinchZoomTimeoutTimer->setSingleShot(true);
    trackpadPinchZoomTimeoutTimer->setInterval(150); // 150ms timeout
    connect(trackpadPinchZoomTimeoutTimer, &QTimer::timeout, this, &InkCanvas::resetTrackpadPinchZoomState);

    // Initialize trackpad zoom animation timer (smooth 60fps interpolation like touchscreen)
    trackpadZoomAnimationTimer = new QTimer(this);
    trackpadZoomAnimationTimer->setInterval(16); // ~60 FPS
    connect(trackpadZoomAnimationTimer, &QTimer::timeout, this, &InkCanvas::updateTrackpadZoomAnimation);

    // Initialize auto-save timer (single-shot, triggered after first edit)
    autoSaveTimer = new QTimer(this);
    autoSaveTimer->setSingleShot(true);
    autoSaveTimer->setInterval(autoSaveInterval);
    connect(autoSaveTimer, &QTimer::timeout, this, &InkCanvas::onAutoSaveTimeout);
    
    // Initialize metadata save timer (single-shot, debounces rapid page switches)
    metadataSaveTimer = new QTimer(this);
    metadataSaveTimer->setSingleShot(true);
    metadataSaveTimer->setInterval(1000); // 1 second debounce delay
    connect(metadataSaveTimer, &QTimer::timeout, this, &InkCanvas::onMetadataSaveTimeout);
    
    // ✅ OPTIMIZATION: Initialize .spn sync timer (longer delay, expensive operation)
    spnSyncTimer = new QTimer(this);
    spnSyncTimer->setSingleShot(true);
    spnSyncTimer->setInterval(3000); // 3 second delay - .spn packing is expensive
    connect(spnSyncTimer, &QTimer::timeout, this, &InkCanvas::onSpnSyncTimeout);
}

InkCanvas::~InkCanvas() {
    // ✅ Auto-save if the canvas has been edited
    if (edited && !saveFolder.isEmpty()) {
        // Save the current page using existing logic
        saveToFile(lastActivePage);
    }
    
    // ✅ Cleanup PDF resources
    if (pdfDocument) {
        pdfDocument.reset();
        pdfDocument = nullptr;
    }
    
    // ✅ Clear text box references (pointers owned by cache, don't delete here)
    currentPdfTextBoxes.clear();
    currentPdfTextBoxPageNumbers.clear();
    
    // ✅ Clear page size cache and trackers
    pdfPageSizeCache.clear();
    currentTextPageNumber = -1;
    currentTextPageNumberSecond = -1;
    currentTextBoxesLoadedForPage = -1;
    
    // ✅ MEMORY LEAK FIX: Clear text box cache (will delete all TextBox* via entry destructors)
    qDeleteAll(pdfTextBoxCache);
    pdfTextBoxCache.clear();
    pdfTextBoxCacheAccessOrder.clear();
    
    // ✅ Clear caches to free memory
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
        pdfCacheAccessOrder.clear();
    }
    {
        QMutexLocker locker(&noteCacheMutex);
        noteCache.clear();
        noteCacheAccessOrder.clear();
    }
    
    // ✅ CRITICAL: Wait for and clean up any active PDF watchers before destruction
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
                watcher->cancel();
                watcher->waitForFinished(); // Wait for thread to complete
            }
            delete watcher; // Delete immediately
        }
    }
    activePdfWatchers.clear();
    
    // ✅ Stop and clean up timers
    if (pdfCacheTimer) {
        pdfCacheTimer->stop();
        // Timer will be deleted automatically as child of this
    }
    if (noteCacheTimer) {
        noteCacheTimer->stop();
        // Timer will be deleted automatically as child of this
    }
    if (pdfTextSelectionTimer) {
        pdfTextSelectionTimer->stop();
        // Timer will be deleted automatically as child of this
    }
    if (inertiaTimer) {
        inertiaTimer->stop();
        // Timer will be deleted automatically as child of this
    }
    if (autoSaveTimer) {
        autoSaveTimer->stop();
        // Timer will be deleted automatically as child of this
    }
    if (metadataSaveTimer) {
        metadataSaveTimer->stop();
        // Flush any pending metadata save before destruction
        if (metadataSavePending) {
            saveNotebookMetadata();
            metadataSavePending = false;
        }
        // Timer will be deleted automatically as child of this
    }
    
    // ✅ OPTIMIZATION: Flush pending .spn sync before destruction
    if (spnSyncTimer) {
        spnSyncTimer->stop();
        if (spnSyncPending) {
            syncSpnPackage();
            spnSyncPending = false;
        }
        // Timer will be deleted automatically as child of this
    }
    
    // ✅ Clear inertia scrolling resources
    cachedFrame = QPixmap(); // Release cached frame memory
    recentVelocities.clear(); // Clear velocity history
    
    // ✅ MEMORY LEAK FIX: Clear rope selection buffer to release memory
    selectionBuffer = QPixmap();
    selectionRect = QRect();
    exactSelectionRectF = QRectF();
    lassoPathPoints.clear();
    selectionMaskPath = QPainterPath();

    // ✅ MEMORY LEAK FIX: Clear main canvas buffer to release memory
    buffer = QPixmap();
    backgroundImage = QPixmap();
    picturePreviewRect = QRect();
    
    // ✅ CRITICAL: Wait for and clean up any active PDF watchers
    // Must wait for futures to complete to prevent memory leaks from thread-local Poppler::Documents
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
            watcher->cancel();
                watcher->waitForFinished(); // Block until thread completes and releases resources
        }
            delete watcher; // Delete immediately, not later
        }
    }
    activePdfWatchers.clear();
    
    // ✅ Wait for and clean up any active note cache watchers
    for (QFutureWatcher<void>* watcher : activeNoteWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
            watcher->cancel();
                watcher->waitForFinished(); // Block until thread completes
        }
            delete watcher; // Delete immediately, not later
        }
    }
    activeNoteWatchers.clear();
    
    // ✅ Clear PDF text boxes - CRITICAL: Clear selectedTextBoxes first to prevent crashes
    selectedTextBoxes.clear();  // References to cached text boxes, don't delete
    
    // ✅ Explicitly clean up window managers to prevent memory leaks
    if (pictureManager) {
        pictureManager->clearAllCachedWindows();
        pictureManager->deleteLater();
        pictureManager = nullptr;
    }
    
    // ✅ Sync .spn package and cleanup temp directory
    if (isSpnPackage) {
        syncSpnPackage();
        SpnPackageManager::cleanupTempDir(tempWorkingDir);
    }
}



// Helper function to get correct DPI scale factor (Wayland-aware)
qreal InkCanvas::getEffectiveDpiScale(QScreen *screen) const {
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    
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

void InkCanvas::initializeBuffer() {
    QScreen *screen = QGuiApplication::primaryScreen();

    // Get logical screen size
    QSize logicalSize = screen ? screen->size() : QSize(1440, 900);
    
    // Get effective DPI scale (Wayland-aware)
    qreal effectiveScale = getEffectiveDpiScale(screen);
    
    QSize pixelSize = logicalSize * effectiveScale;

    
    // ✅ Double the vertical height for pseudo smooth scrolling system
    // This allows combining two pages vertically for seamless scrolling
    pixelSize.setHeight(pixelSize.height() * 2);

    buffer = QPixmap(pixelSize);
    buffer.fill(Qt::transparent);

    setMaximumSize(pixelSize); // 🔥 KEY LINE to make full canvas drawable
}

// Helper function to invert PDF colors for dark mode
static QImage invertPdfImage(const QImage &original) {
    if (original.isNull()) {
        return original;
    }
    
    QImage inverted = original.copy();
    inverted.invertPixels(QImage::InvertRgb);  // Invert RGB, keep alpha
    return inverted;
}

// Helper function to calculate contrasting text color for highlights
// For normal mode: returns white or black based on highlight luminance
// For inverted PDF mode: returns the inverted highlight color (original color)
[[maybe_unused]] static QColor getContrastingTextColor(const QColor &highlightColor, bool isPdfInverted) {
    if (isPdfInverted) {
        // For inverted PDFs, use the original (non-inverted) color
        // This means inverting the highlight color back
        return QColor(255 - highlightColor.red(),
                     255 - highlightColor.green(),
                     255 - highlightColor.blue());
    } else {
        // For normal mode, calculate a contrasting color based on luminance
        // Use standard luminance calculation (ITU-R BT.601)
        int luminance = (highlightColor.red() * 299 + 
                        highlightColor.green() * 587 + 
                        highlightColor.blue() * 114) / 1000;
        
        // If highlight is dark, use white text; if light, use black text
        return (luminance < 128) ? Qt::white : Qt::black;
    }
}


void InkCanvas::setPdfInversionEnabled(bool enabled) {
    if (pdfInversionEnabled != enabled) {
        pdfInversionEnabled = enabled;
        
        // Clear PDF cache to force re-rendering with new inversion setting
        clearPdfCache();
        
        // Save to metadata
        if (!saveFolder.isEmpty()) {
            saveNotebookMetadata();
        }
    }
}

void InkCanvas::loadPdf(const QString &pdfPath) {
    // ✅ Clear existing PDF cache before loading new PDF to prevent old pages from showing
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
        pdfCacheAccessOrder.clear();
    }
    currentCachedPage = -1;
    
    // Cancel any active PDF caching operations
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // ✅ CRITICAL: Wait for and clean up any active PDF watchers from previous PDF
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
            watcher->cancel();
                watcher->waitForFinished(); // Wait for thread to complete
        }
            delete watcher; // Delete immediately
        }
    }
    activePdfWatchers.clear();
    
    pdfDocument = Poppler::Document::load(pdfPath);
    if (pdfDocument && !pdfDocument->isLocked()) {
        // Enable anti-aliasing rendering hints for better text quality
        pdfDocument->setRenderHint(Poppler::Document::Antialiasing, true);
        pdfDocument->setRenderHint(Poppler::Document::TextAntialiasing, true);
        pdfDocument->setRenderHint(Poppler::Document::TextHinting, true);
        pdfDocument->setRenderHint(Poppler::Document::TextSlightHinting, true);
        
        totalPdfPages = pdfDocument->numPages();
        isPdfLoaded = true;
        // ✅ Don't automatically load page 0 - let MainWindow handle initial page loading
        
        // ✅ Save the PDF path in the unified JSON metadata
        if (!saveFolder.isEmpty()) {
            this->pdfPath = pdfPath; // Store in member variable
            saveNotebookMetadata(); // Save to JSON
        }
        
        // Emit signal that PDF was loaded
        emit pdfLoaded();
        // update();
    }
}

void InkCanvas::clearPdf() {
    pdfDocument.reset();
    pdfDocument = nullptr;
    isPdfLoaded = false;
    totalPdfPages = 0;
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
        pdfCacheAccessOrder.clear();
    }
    
    // ✅ Clear text box references (pointers owned by cache, don't delete here)
    currentPdfTextBoxes.clear();
    currentPdfTextBoxPageNumbers.clear();
    selectedTextBoxes.clear();
    
    // ✅ Clear page size cache and trackers
    pdfPageSizeCache.clear();
    currentTextPageNumber = -1;
    currentTextPageNumberSecond = -1;
    currentTextBoxesLoadedForPage = -1;
    
    // ✅ MEMORY LEAK FIX: Clear text box cache (will delete all TextBox* via entry destructors)
    qDeleteAll(pdfTextBoxCache);
    pdfTextBoxCache.clear();
    pdfTextBoxCacheAccessOrder.clear();

    // ✅ Clear the background image immediately to remove PDF from display
    backgroundImage = QPixmap();
    
    // Reset cache system state
    currentCachedPage = -1;
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // ✅ CRITICAL: Wait for and clean up any active PDF watchers
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
            watcher->cancel();
                watcher->waitForFinished(); // Wait for thread to complete
        }
            delete watcher; // Delete immediately
        }
    }
    activePdfWatchers.clear();

    // ✅ Clear the PDF path from JSON metadata when clearing the PDF
    if (!saveFolder.isEmpty()) {
        this->pdfPath.clear();
        saveNotebookMetadata();
    }
}

void InkCanvas::clearPdfNoDelete() {
    pdfDocument.reset();
    pdfDocument = nullptr;
    isPdfLoaded = false;
    totalPdfPages = 0;
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
    }
    
    // ✅ Clear text box references (pointers owned by cache, don't delete here)
    currentPdfTextBoxes.clear();
    currentPdfTextBoxPageNumbers.clear();
    selectedTextBoxes.clear();
    
    // ✅ Clear page size cache and trackers
    pdfPageSizeCache.clear();
    currentTextPageNumber = -1;
    currentTextPageNumberSecond = -1;
    currentTextBoxesLoadedForPage = -1;
    
    // ✅ MEMORY LEAK FIX: Clear text box cache
    qDeleteAll(pdfTextBoxCache);
    pdfTextBoxCache.clear();
    pdfTextBoxCacheAccessOrder.clear();
    
    // Reset cache system state
    currentCachedPage = -1;
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // ✅ CRITICAL: Wait for and clean up any active PDF watchers
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher) {
            if (!watcher->isFinished()) {
            watcher->cancel();
                watcher->waitForFinished(); // Wait for thread to complete
        }
            delete watcher; // Delete immediately
        }
    }
    activePdfWatchers.clear();
}

void InkCanvas::loadPdfPage(int pageNumber) {
    if (!pdfDocument) return;

    // Update current page tracker
    currentCachedPage = pageNumber;

    // Check if target page is already cached (thread-safe)
    bool isCached = false;
    {
        QMutexLocker locker(&pdfCacheMutex);
        if (pdfCache.contains(pageNumber)) {
            // Display the cached page immediately
            backgroundImage = *pdfCache.object(pageNumber);
            isCached = true;

            // ✅ LRU: Mark this page as recently accessed by moving to end of access order
            pdfCacheAccessOrder.removeAll(pageNumber);
            pdfCacheAccessOrder.append(pageNumber);
        }
    }
    
    if (isCached) {
        loadPage(pageNumber);  // Load annotations
        // ✅ MEMORY OPTIMIZATION: Don't load text boxes until user attempts selection
        // loadPdfTextBoxes(pageNumber); // Load text boxes for PDF text selection
        update();
        
        // Check and cache adjacent pages after delay
        checkAndCacheAdjacentPages(pageNumber);
        return;
    }

    // Target page not in cache - render it immediately
    renderPdfPageToCache(pageNumber);
    
    // Display the newly rendered page (thread-safe)
    {
        QMutexLocker locker(&pdfCacheMutex);
        if (pdfCache.contains(pageNumber)) {
            backgroundImage = *pdfCache.object(pageNumber);
        } else {
            backgroundImage = QPixmap();  // Clear background if rendering failed
        }
    }
    
    loadPage(pageNumber);  // Load existing canvas annotations
    // ✅ MEMORY OPTIMIZATION: Don't load text boxes until user attempts selection
    // loadPdfTextBoxes(pageNumber); // Load text boxes for PDF text selection
    update();
    
    // Cache adjacent pages after delay
    checkAndCacheAdjacentPages(pageNumber);
}


void InkCanvas::loadPdfPreviewAsync(int pageNumber) {
    if (!pdfDocument || pageNumber < 0 || pageNumber >= pdfDocument->numPages()) return;

    QFutureWatcher<QPixmap> *watcher = new QFutureWatcher<QPixmap>(this);

    connect(watcher, &QFutureWatcher<QPixmap>::finished, this, [this, watcher]() {
        QPixmap result = watcher->result();
        if (!result.isNull()) {
            backgroundImage = result;
            update();  // trigger repaint
        }
        watcher->deleteLater();  // Clean up
    });

    QFuture<QPixmap> future = QtConcurrent::run([=]() -> QPixmap {
        // Render current page
        std::unique_ptr<Poppler::Page> currentPage(pdfDocument->page(pageNumber));
        if (!currentPage) return QPixmap();

        QImage currentPageImage = currentPage->renderToImage(96, 96);
        if (currentPageImage.isNull()) return QPixmap();
        
        // Apply PDF inversion if enabled
        if (pdfInversionEnabled) {
            currentPageImage = invertPdfImage(currentPageImage);
        }

        // Try to render next page for combination
        QImage nextPageImage;
        int nextPageNumber = pageNumber + 1;
        if (nextPageNumber < pdfDocument->numPages()) {
            std::unique_ptr<Poppler::Page> nextPage(pdfDocument->page(nextPageNumber));
            if (nextPage) {
                nextPageImage = nextPage->renderToImage(96, 96);
                
                // Apply PDF inversion if enabled
                if (pdfInversionEnabled) {
                    nextPageImage = invertPdfImage(nextPageImage);
                }
            }
        }

        // Create combined image
        QImage combinedImage;
        if (!nextPageImage.isNull()) {
            // Both pages available - create combined image
            int combinedHeight = currentPageImage.height() + nextPageImage.height();
            int combinedWidth = qMax(currentPageImage.width(), nextPageImage.width());
            
            combinedImage = QImage(combinedWidth, combinedHeight, QImage::Format_ARGB32);
            combinedImage.fill(Qt::white);
            
            QPainter painter(&combinedImage);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            
            // Draw current page at top
            painter.drawImage(0, 0, currentPageImage);
            
            // Draw next page below current page
            painter.drawImage(0, currentPageImage.height(), nextPageImage);
            
            painter.end();
        } else {
            // Only current page available (last page or next page failed to render)
            // Create double-height image with current page at top and white space below
            int combinedHeight = currentPageImage.height() * 2;
            int combinedWidth = currentPageImage.width();
            
            combinedImage = QImage(combinedWidth, combinedHeight, QImage::Format_ARGB32);
            combinedImage.fill(Qt::white);
            
            QPainter painter(&combinedImage);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            
            // Draw current page at top
            painter.drawImage(0, 0, currentPageImage);
            
            painter.end();
        }

        if (combinedImage.isNull()) return QPixmap();

        // Scale to match the desired DPI
        QImage upscaled = combinedImage.scaled(combinedImage.width() * (pdfRenderDPI / 96),
                                              combinedImage.height() * (pdfRenderDPI / 96),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);

        return QPixmap::fromImage(upscaled);
    });

    watcher->setFuture(future);
}


void InkCanvas::startBenchmark() {
    benchmarking = true;
    processedTimestamps.clear();
    benchmarkTimer.start();
}

void InkCanvas::stopBenchmark() {
    benchmarking = false;
}

int InkCanvas::getProcessedRate() {
    qint64 now = benchmarkTimer.elapsed();
    while (!processedTimestamps.empty() && now - processedTimestamps.front() > 1000) {
        processedTimestamps.pop_front();
    }
    return static_cast<int>(processedTimestamps.size());
}


void InkCanvas::resizeEvent(QResizeEvent *event) {
    // Don't resize the buffer when the widget resizes
    // The buffer size should be determined by the PDF/document content, not the widget size
    // The paintEvent will handle centering the buffer content within the widget

    QWidget::resizeEvent(event);
}

void InkCanvas::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    
    // ⚡ SQUID-STYLE OPTIMIZATION: During touch panning, draw cached frame shifted by pan delta
    if (isTouchPanning && !cachedFrame.isNull()) {
        // PERFORMANCE FIX: Only fill and draw the necessary regions
        // Calculate where the cached canvas region will be drawn
        QRect cachedCanvasRect = cachedCanvasRegion.translated(cachedFrameOffset);
        
        // Get the actual update region that Qt wants us to repaint
        QRegion updateRegion = event->region();
        
        // Fill only the areas outside the canvas region (within update region only)
        QRegion outsideRegion = updateRegion;
        outsideRegion -= QRegion(cachedCanvasRect);
        
        if (!outsideRegion.isEmpty()) {
            painter.setClipRegion(outsideRegion);
            painter.fillRect(updateRegion.boundingRect(), palette().window().color());
            painter.setClipping(false);
        }
        
        // Draw only the canvas region at the offset position (no antialiasing for speed)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(cachedCanvasRect.topLeft(), cachedFrame);
        return; // Skip expensive rendering during gesture
    }
    
    // Save the painter state before transformations
    painter.save();
    
    // Calculate the scaled canvas size
    qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
    
    // Calculate centering offsets
    qreal centerOffsetX = 0;
    qreal centerOffsetY = 0;
    
    // Center horizontally if canvas is smaller than widget
    if (scaledCanvasWidth < width()) {
        centerOffsetX = (width() - scaledCanvasWidth) / 2.0;
    }
    
    // Center vertically if canvas is smaller than widget
    if (scaledCanvasHeight < height()) {
        centerOffsetY = (height() - scaledCanvasHeight) / 2.0;
    }
    
    // Apply centering offset first
    painter.translate(centerOffsetX, centerOffsetY);
    
    // Use internal zoom factor for smoother animation
    painter.scale(internalZoomFactor / 100.0, internalZoomFactor / 100.0);

    // Pan offset needs to be reversed because painter works in transformed coordinates
    painter.translate(-panOffsetX, -panOffsetY);

    // Set clipping rectangle to canvas bounds to prevent painting outside
    painter.setClipRect(0, 0, buffer.width(), buffer.height());

    // 🟨 Notebook-style background rendering
    if (backgroundImage.isNull()) {
        painter.save();
        
        // Always apply background color regardless of style
        painter.fillRect(QRectF(0, 0, buffer.width(), buffer.height()), backgroundColor);

        // Only draw grid/lines if not "None" style
        if (backgroundStyle != BackgroundStyle::None) {
        QPen linePen(QColor(100, 100, 100, 100));  // Subtle gray lines
        linePen.setWidthF(1.0);
        painter.setPen(linePen);

        qreal scaledDensity = backgroundDensity;
        qreal effectiveScale = getEffectiveDpiScale();

        if (effectiveScale > 1.0)
            scaledDensity *= effectiveScale;  // DPI handling (Wayland-aware)

        if (backgroundStyle == BackgroundStyle::Lines || backgroundStyle == BackgroundStyle::Grid) {
            for (int y = 0; y < buffer.height(); y += scaledDensity)
                painter.drawLine(0, y, buffer.width(), y);
        }
        if (backgroundStyle == BackgroundStyle::Grid) {
            for (int x = 0; x < buffer.width(); x += scaledDensity)
            painter.drawLine(x, 0, x, buffer.height());
            }
        }

        painter.restore();
    }

    // ✅ Draw loaded image or PDF background if available
    if (!backgroundImage.isNull()) {
        painter.drawPixmap(0, 0, backgroundImage);
    }

    // ✅ Draw pictures (above PDF, below user strokes) - only render pictures in update region
    if (pictureManager) {
        // Get the update region in canvas coordinates for efficient rendering
        QRegion updateRegion = event->region();
        if (!updateRegion.isEmpty()) {
            // Convert update region to canvas coordinates
            QRect updateRect = updateRegion.boundingRect();
            QRect canvasUpdateRect = mapWidgetToCanvas(updateRect);
            pictureManager->renderPicturesToCanvas(painter, canvasUpdateRect);
        } else {
            // Full repaint - render all pictures
            pictureManager->renderPicturesToCanvas(painter);
        }
    }
    
    // ✅ PERFORMANCE: Draw outline preview during picture movement (after user strokes)
    // This ensures the outline appears on top and doesn't interfere with background rendering

    // ✅ Draw user's strokes from the buffer (transparent overlay)
    painter.drawPixmap(0, 0, buffer);
    
    // Draw straight line preview if in straight line mode and drawing
    // Skip preview for eraser tool
    if (straightLineMode && drawing && currentTool != ToolType::Eraser) {
        // Save the current state before applying the eraser mode
        painter.save();
        
        // Store current pressure - ensure minimum is 0.5 for consistent preview
        qreal pressure = qMax(0.5, painter.device()->devicePixelRatioF() > 1.0 ? 0.8 : 1.0);
        
        // Set up the pen based on tool type
        if (currentTool == ToolType::Marker) {
            qreal thickness = penThickness * 8.0;
            QColor markerColor = penColor;
            // Increase alpha for better visibility in straight line mode
            // Using a higher value (80) instead of the regular 4 to compensate for single-pass drawing
            markerColor.setAlpha(80);
            QPen pen(markerColor, thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
        } else { // Default Pen
            // Match the exact same thickness calculation as in drawStroke
            qreal scaledThickness = penThickness * pressure;
            QPen pen(penColor, scaledThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
        }
        
        // Use the same coordinate transformation logic as in drawStroke
        // to ensure the preview line appears at the exact same position
        QPointF bufferStart, bufferEnd;
        
        // Convert screen coordinates to buffer coordinates using the same calculations as drawStroke
        qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
        qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
        qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
        qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
        
        QPointF adjustedStart = straightLineStartPoint - QPointF(centerOffsetX, centerOffsetY);
        QPointF adjustedEnd = lastPoint - QPointF(centerOffsetX, centerOffsetY);
        
        bufferStart = (adjustedStart / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
        bufferEnd = (adjustedEnd / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
        
        // Draw the preview line using the same coordinates that will be used for the final line
        painter.drawLine(bufferStart, bufferEnd);
        
        // Restore the original painter state
        painter.restore();
    }

    // Draw selection rectangle if in rope tool mode and selecting, moving, or has a selection
    if (ropeToolMode && (selectingWithRope || movingSelection || (!selectionBuffer.isNull() && !selectionRect.isEmpty()))) {
        painter.save(); // Save painter state for overlays
        painter.resetTransform(); // Reset transform to draw directly in logical widget coordinates
        
        if (selectingWithRope && !lassoPathPoints.isEmpty()) {
            QPen selectionPen(Qt::DashLine);
            selectionPen.setColor(Qt::blue);
            selectionPen.setWidthF(1.5); // Width in logical pixels
            painter.setPen(selectionPen);
            painter.drawPolygon(lassoPathPoints); // lassoPathPoints are logical widget coordinates
        } else if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
            // selectionRect is in logical widget coordinates.
            // selectionBuffer is in buffer coordinates, we need to handle scaling correctly
            QPixmap scaledBuffer = selectionBuffer;
            
            // Calculate the current zoom factor
            qreal currentZoom = internalZoomFactor / 100.0;
            
            // Scale the selection buffer to match the current zoom level
            if (currentZoom != 1.0) {
                QSize scaledSize = QSize(
                    qRound(scaledBuffer.width() * currentZoom),
                    qRound(scaledBuffer.height() * currentZoom)
                );
                scaledBuffer = scaledBuffer.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            
            // Draw it at the logical position
            // Use exactSelectionRectF for smoother movement if available
            QPointF topLeft = exactSelectionRectF.isEmpty() ? selectionRect.topLeft() : exactSelectionRectF.topLeft();
            
            painter.drawPixmap(topLeft, scaledBuffer);

            QPen selectionBorderPen(Qt::DashLine);
            selectionBorderPen.setColor(Qt::darkCyan);
            selectionBorderPen.setWidthF(1.5); // Width in logical pixels
            painter.setPen(selectionBorderPen);
            
            // Use exactSelectionRectF for drawing the selection border if available
            if (!exactSelectionRectF.isEmpty()) {
                painter.drawRect(exactSelectionRectF);
            } else {
                painter.drawRect(selectionRect);
            }
        }
        painter.restore(); // Restore painter state to what it was for drawing the main buffer
    }
    

    

    
    // Restore the painter state
    painter.restore();
    
    // Fill the area outside the canvas with the widget's background color
    QRect widgetRect = rect();
    QRectF canvasRect(
        centerOffsetX - panOffsetX * (internalZoomFactor / 100.0),
        centerOffsetY - panOffsetY * (internalZoomFactor / 100.0),
        buffer.width() * (internalZoomFactor / 100.0),
        buffer.height() * (internalZoomFactor / 100.0)
    );
    
    // Create regions for areas outside the canvas
    QRegion outsideRegion(widgetRect);
    outsideRegion -= QRegion(canvasRect.toRect());
    
    // Fill the outside region with the background color
    painter.setClipRegion(outsideRegion);
    painter.fillRect(widgetRect, palette().window().color());
    
    // Reset clipping for overlay elements that should appear on top
    painter.setClipping(false);
    
    // Draw PDF text selection overlay on top of everything
    if (pdfTextSelectionEnabled && isPdfLoaded) {
        painter.save(); // Save painter state for PDF text overlay
        painter.resetTransform(); // Reset transform to draw directly in logical widget coordinates
        
        // Note: We no longer draw the selection rectangle during dragging
        // since text selection is row-based, not area-based
        
        // Note: Persistent highlights are now drawn directly onto cached PDF images during rendering
        // for better performance. See drawHighlightsOnPageImage() and renderPdfPageToCacheThreadSafe()
        
        // Draw highlights for selected text boxes (temporary selection during text selection)
        if (!selectedTextBoxes.isEmpty()) {
            // Use current pen color for highlight with semi-transparency
            // The original PDF text will show through the transparent overlay
            QColor highlightColor = penColor;
            highlightColor.setAlpha(120); // Semi-transparent to let original text show through
            
            painter.setBrush(highlightColor);
            painter.setPen(Qt::NoPen);
            
            // Group text boxes by row for connected highlighting
            const qreal rowToleranceWidget = 10.0; // Widget coordinate pixels
            
            struct TextBoxWithCoords {
                QRectF widgetRect;
                qreal centerY;
            };
            
            QList<TextBoxWithCoords> textBoxCoords;
            
            // Convert all text boxes to widget coordinates
            for (int i = 0; i < selectedTextBoxes.size(); ++i) {
                const Poppler::TextBox* textBox = selectedTextBoxes[i];
                if (textBox) {
                    // Find the page number for this text box
                    int textBoxPageNumber = -1;
                    for (int j = 0; j < currentPdfTextBoxes.size(); ++j) {
                        if (currentPdfTextBoxes[j] == textBox) {
                            textBoxPageNumber = (j < currentPdfTextBoxPageNumbers.size()) ? 
                                               currentPdfTextBoxPageNumbers[j] : -1;
                            break;
                        }
                    }
                    
                    // Convert PDF coordinates to widget coordinates
                    QRectF pdfRect = textBox->boundingBox();
                    QPointF topLeft = mapPdfToWidgetCoordinates(pdfRect.topLeft(), textBoxPageNumber);
                    QPointF bottomRight = mapPdfToWidgetCoordinates(pdfRect.bottomRight(), textBoxPageNumber);
                    QRectF widgetRect(topLeft, bottomRight);
                    widgetRect = widgetRect.normalized();
                    
                    TextBoxWithCoords coords;
                    coords.widgetRect = widgetRect;
                    coords.centerY = (widgetRect.top() + widgetRect.bottom()) / 2.0;
                    textBoxCoords.append(coords);
                }
            }
            
            // Group by row
            QList<QList<QRectF>> rowGroups;
            for (const TextBoxWithCoords &coords : textBoxCoords) {
                bool addedToRow = false;
                
                // Try to add to existing row
                for (QList<QRectF> &row : rowGroups) {
                    if (!row.isEmpty()) {
                        qreal avgY = (row.first().top() + row.first().bottom()) / 2.0;
                        
                        if (qAbs(avgY - coords.centerY) <= rowToleranceWidget) {
                            row.append(coords.widgetRect);
                            addedToRow = true;
                            break;
                        }
                    }
                }
                
                // Create new row if not added
                if (!addedToRow) {
                    QList<QRectF> newRow;
                    newRow.append(coords.widgetRect);
                    rowGroups.append(newRow);
                }
            }
            
            // Draw continuous rectangle for each row
            for (const QList<QRectF> &row : rowGroups) {
                if (row.isEmpty()) continue;
                
                // Find leftmost and rightmost bounds
                qreal minX = row.first().left();
                qreal maxX = row.first().right();
                qreal minY = row.first().top();
                qreal maxY = row.first().bottom();
                
                for (const QRectF &rect : row) {
                    minX = qMin(minX, rect.left());
                    maxX = qMax(maxX, rect.right());
                    minY = qMin(minY, rect.top());
                    maxY = qMax(maxY, rect.bottom());
                }
                
                // Create continuous rectangle spanning the entire row
                QRectF continuousRect(minX, minY, maxX - minX, maxY - minY);
                painter.drawRect(continuousRect);
            }
        }
        
        painter.restore(); // Restore painter state
    }
    
    // ✅ PERFORMANCE: Draw outline preview during picture movement (on top of everything)
    if (!picturePreviewRect.isEmpty() && (pictureDragging || pictureResizing)) {
        // Reset transform to draw in canvas coordinates
        painter.resetTransform();
        painter.translate(centerOffsetX, centerOffsetY);
        painter.scale(internalZoomFactor / 100.0, internalZoomFactor / 100.0);
        painter.translate(-panOffsetX, -panOffsetY);
        
        painter.save();
        
        // ✅ HIGH CONTRAST OUTLINE: Use solid line with contrasting colors for better visibility
        QPen previewPen(QColor(255, 140, 0), 2, Qt::SolidLine); // Solid orange outline (thinner)
        painter.setPen(previewPen);
        painter.setBrush(Qt::NoBrush);
        
        // Enable antialiasing for smoother outline
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Draw the preview rectangle outline
        painter.drawRect(picturePreviewRect);
        
        // Draw corner handles for resize preview
        if (pictureResizing) {
            painter.setBrush(QBrush(QColor(255, 140, 0, 200))); // More opaque orange
            painter.setPen(QPen(QColor(255, 140, 0), 2, Qt::SolidLine));
            int handleSize = 12; // ✅ TOUCH UX: Larger visual handles to match touch areas
            
            // Draw resize handles at corners
            painter.drawEllipse(picturePreviewRect.topLeft() + QPoint(-handleSize/2, -handleSize/2), handleSize, handleSize);
            painter.drawEllipse(picturePreviewRect.topRight() + QPoint(-handleSize/2, -handleSize/2), handleSize, handleSize);
            painter.drawEllipse(picturePreviewRect.bottomLeft() + QPoint(-handleSize/2, -handleSize/2), handleSize, handleSize);
            painter.drawEllipse(picturePreviewRect.bottomRight() + QPoint(-handleSize/2, -handleSize/2), handleSize, handleSize);
        }
        
        painter.restore();
    }
}

void InkCanvas::tabletEvent(QTabletEvent *event) {
    // Skip tablet event handling when a picture window is in edit mode
    if (pictureWindowEditMode) {
        // qDebug() << "InkCanvas: Skipping tablet event due to picture window edit mode";
        event->ignore();
        return;
    }
    
    // ✅ PRIORITY: Handle PDF text selection with stylus when enabled
    // This redirects stylus input to text selection instead of drawing
    if (pdfTextSelectionEnabled && isPdfLoaded) {
        if (event->type() == QEvent::TabletPress) {
            // ✅ LAZY LOAD: Only load text boxes when user actually attempts selection
            // Check if text boxes need to be loaded or reloaded for current page
            if (currentCachedPage >= 0 && currentTextBoxesLoadedForPage != currentCachedPage) {
                loadPdfTextBoxes(currentCachedPage);
            }
            
            pdfTextSelecting = true;
            pdfSelectionStart = event->position();
            pdfSelectionEnd = pdfSelectionStart;
            
            // Clear any existing selected text boxes without resetting pdfTextSelecting
            selectedTextBoxes.clear();
            
            setCursor(Qt::IBeamCursor); // Ensure cursor is correct
            update(); // Refresh display
            event->accept();
            return;
        } else if (event->type() == QEvent::TabletMove && pdfTextSelecting) {
            pdfSelectionEnd = event->position();
            
            // Store pending selection for throttled processing (60 FPS throttling)
            pendingSelectionStart = pdfSelectionStart;
            pendingSelectionEnd = pdfSelectionEnd;
            hasPendingSelection = true;
            
            // Start timer if not already running (throttled to 60 FPS)
            if (!pdfTextSelectionTimer->isActive()) {
                pdfTextSelectionTimer->start();
            }
            
            // NOTE: No direct update() call here - let the timer handle updates at 60 FPS
            event->accept();
            return;
        } else if (event->type() == QEvent::TabletRelease && pdfTextSelecting) {
            pdfSelectionEnd = event->position();
            
            // Process any pending selection immediately on release
            if (pdfTextSelectionTimer && pdfTextSelectionTimer->isActive()) {
                pdfTextSelectionTimer->stop();
                if (hasPendingSelection) {
                    updatePdfTextSelection(pendingSelectionStart, pendingSelectionEnd, true); // Final selection
                    hasPendingSelection = false;
                }
            } else {
                // Update selection with final position
                updatePdfTextSelection(pdfSelectionStart, pdfSelectionEnd, true); // Final selection
            }
            
            pdfTextSelecting = false;
            
            // Check for link clicks if no text was selected
            QString selectedText = getSelectedPdfText();
            if (selectedText.isEmpty()) {
                handlePdfLinkClick(event->position());
            } else {
                // Show context menu for text selection
                QPoint globalPos = mapToGlobal(event->position().toPoint());
                showPdfTextSelectionMenu(globalPos);
            }
            
            event->accept();
            return;
        }
    }
    
    // ✅ NORMAL STYLUS BEHAVIOR: Only reached when PDF text selection is OFF
    // Hardware eraser detection
    static bool hardwareEraserActive = false;
    bool wasUsingHardwareEraser = false;
    
    // Track hardware eraser state
    if (event->pointerType() == QPointingDevice::PointerType::Eraser) {
        // Hardware eraser is being used
        wasUsingHardwareEraser = true;
        
        if (event->type() == QEvent::TabletPress) {
            // Start of eraser stroke - save current tool and switch to eraser
            hardwareEraserActive = true;
            previousTool = currentTool;
            currentTool = ToolType::Eraser;
            // ✅ Switch to eraser thickness when hardware eraser is activated
            penThickness = eraserToolThickness;
        }
    }
    
    // Maintain hardware eraser state across move events
    if (hardwareEraserActive && event->type() != QEvent::TabletRelease) {
        wasUsingHardwareEraser = true;
    }

    // Determine if we're in eraser mode (either hardware eraser or tool set to eraser)
    bool isErasing = (currentTool == ToolType::Eraser);

    if (event->type() == QEvent::TabletPress) {
        // Only start drawing if this is a pen tip press (LeftButton), not a side button press
        // Side buttons (MiddleButton, RightButton) can generate TabletPress while hovering
        // and should not initiate drawing
        Qt::MouseButton pressedButton = event->button();
        Qt::MouseButtons allButtons = event->buttons();
        
        // Check if this is a side button press (not pen tip touching surface)
        // Case 1: button() explicitly reports a side button
        // Case 2: button() is NoButton but buttons() shows side button without LeftButton
        //         (this happens on some drivers when side button pressed while hovering)
        bool isSideButtonOnly = (pressedButton != Qt::LeftButton && pressedButton != Qt::NoButton);
        bool isHoverWithSideButton = (pressedButton == Qt::NoButton && 
                                       (allButtons & (Qt::MiddleButton | Qt::RightButton)) &&
                                       !(allButtons & Qt::LeftButton));
        
        if (isSideButtonOnly || isHoverWithSideButton) {
#ifdef Q_OS_WIN
            // Windows-specific: On Windows, stylus side buttons only work while hovering
            // We need to allow drawing/tracking in hover mode for straight line and lasso
            if (straightLineMode || ropeToolMode) {
                windowsStylusHoverDrawing = true;
                windowsHoverWasStraightLine = straightLineMode;
                windowsHoverWasLasso = ropeToolMode;
                drawing = true;
                lastPoint = event->position();
                
                if (straightLineMode) {
                    straightLineStartPoint = lastPoint;
                }
                if (ropeToolMode) {
                    // Start new selection in hover mode
                    if (!selectionBuffer.isNull()) {
                        // Cancel existing selection
                        selectionBuffer = QPixmap();
                        selectionRect = QRect();
                        lassoPathPoints.clear();
                        movingSelection = false;
                        selectingWithRope = false;
                        selectionJustCopied = false;
                        selectionAreaCleared = false;
                        selectionMaskPath = QPainterPath();
                        selectionBufferRect = QRectF();
                        update();
                    }
                    selectingWithRope = true;
                    movingSelection = false;
                    selectionJustCopied = false;
                    selectionAreaCleared = false;
                    selectionMaskPath = QPainterPath();
                    selectionBufferRect = QRectF();
                    lassoPathPoints.clear();
                    lassoPathPoints << lastPoint;
                    selectionRect = QRect();
                    selectionBuffer = QPixmap();
                }
                
                event->accept();
                return;
            }
#endif
            // This is a side button press while hovering - don't start drawing
            event->accept();
            return;
        }
        
        drawing = true;
        lastPoint = event->position(); // Logical widget coordinates
        if (straightLineMode) {
            straightLineStartPoint = lastPoint;
        }
        if (ropeToolMode) {
            if (!selectionBuffer.isNull() && selectionRect.contains(lastPoint.toPoint())) {
                // Start moving an existing selection (or continue if already moving)
                movingSelection = true;
                selectingWithRope = false;
                lastMovePoint = lastPoint;
                // Initialize the exact floating-point rect if it's empty
                if (exactSelectionRectF.isEmpty()) {
                    exactSelectionRectF = QRectF(selectionRect);
                }
                
                // If this selection was just copied, DON'T clear the area - the original should remain
                // This is the key difference between copy and cut/move operations
                if (selectionJustCopied) {
                    selectionJustCopied = false; // Clear the flag, but don't clear the buffer area
                }
                
                // If the selection area hasn't been cleared from the buffer yet, clear it now
                if (!selectionAreaCleared && !selectionMaskPath.isEmpty()) {
                    QPainter painter(&buffer);
                    painter.setCompositionMode(QPainter::CompositionMode_Clear);
                    painter.fillPath(selectionMaskPath, Qt::transparent);
                    painter.end();
                    selectionAreaCleared = true;
                }
                // selectionBuffer already has the content.
                // The original area in 'buffer' was already cleared when selection was made.
            } else {
                // Start a new selection or cancel existing one
                if (!selectionBuffer.isNull()) { // If there's an active selection, a tap outside cancels it
                    selectionBuffer = QPixmap();
                    selectionRect = QRect();
                    lassoPathPoints.clear();
                    movingSelection = false;
                    selectingWithRope = false;
                    selectionJustCopied = false;
                    selectionAreaCleared = false;
                    selectionMaskPath = QPainterPath();
                    selectionBufferRect = QRectF();
#ifdef Q_OS_WIN
                    // Auto-disable lasso mode if this was a Windows stylus hover lasso
                    if (windowsLassoNeedsAutoDisable) {
                        ropeToolMode = false;
                        windowsLassoNeedsAutoDisable = false;
                    }
#endif
                    update(); // Full update to remove old selection visuals
                    drawing = false; // Consumed this press for cancel
                    return;
                }
                selectingWithRope = true;
                movingSelection = false;
                selectionJustCopied = false;
                selectionAreaCleared = false;
                selectionMaskPath = QPainterPath();
                selectionBufferRect = QRectF();
                lassoPathPoints.clear();
                lassoPathPoints << lastPoint; // Start the lasso path
                selectionRect = QRect();
                selectionBuffer = QPixmap();
            }
        }
    } else if (event->type() == QEvent::TabletMove && drawing) {
        if (ropeToolMode) {
            if (selectingWithRope) {
                QRectF oldPreviewBoundingRect = lassoPathPoints.boundingRect();
                lassoPathPoints << event->position();
                lastPoint = event->position();
                QRectF newPreviewBoundingRect = lassoPathPoints.boundingRect();
                // Update the area of the selection rectangle preview (logical widget coordinates)
                update(oldPreviewBoundingRect.united(newPreviewBoundingRect).toRect().adjusted(-5,-5,5,5));
            } else if (movingSelection) {
                QPointF delta = event->position() - lastMovePoint; // Delta in logical widget coordinates
                QRect oldWidgetSelectionRect = selectionRect;
                
                // Update the exact floating-point rectangle first
                exactSelectionRectF.translate(delta);
                
                // Convert back to integer rect, but only when the position actually changes
                QRect newRect = exactSelectionRectF.toRect();
                if (newRect != selectionRect) {
                    selectionRect = newRect;
                    // Update the combined area of the old and new selection positions (logical widget coordinates)
                    update(oldWidgetSelectionRect.united(selectionRect).adjusted(-2,-2,2,2));
                } else {
                    // Even if the integer position didn't change, we still need to update
                    // to make movement feel smoother, especially with slow movements
                    update(selectionRect.adjusted(-2,-2,2,2));
                }
                
                lastMovePoint = event->position();
                if (!edited){
                    edited = true;
                }
            }
        } else if (straightLineMode && !isErasing) {
            // For straight line mode with non-eraser tools, just update the last position
            // and trigger a repaint of only the affected area for preview
            static QElapsedTimer updateTimer;
            static bool timerInitialized = false;
            
            if (!timerInitialized) {
                updateTimer.start();
                timerInitialized = true;
            }
            
            // Throttle updates based on time for high CPU usage tools
            bool shouldUpdate = true;
            
            // Apply throttling for marker which can be CPU intensive
            if (currentTool == ToolType::Marker) {
                shouldUpdate = updateTimer.elapsed() > 16; // Only update every 16ms (approx 60fps)
            }
            
            if (shouldUpdate) {
                QPointF oldLastPoint = lastPoint;
                lastPoint = event->position();
                
                // Calculate affected rectangle that needs updating
                QRectF updateRect = calculatePreviewRect(straightLineStartPoint, oldLastPoint, lastPoint);
                update(updateRect.toRect());
                
                // Reset timer
                updateTimer.restart();
            } else {
                // Just update the last point without redrawing
                lastPoint = event->position();
            }
        } else if (straightLineMode && isErasing) {
            // For eraser in straight line mode, continuously erase from start to current point
            // This gives immediate visual feedback and smoother erasing experience
            
            // Store current point
            QPointF currentPoint = event->position();
            
            // Clear previous stroke by redrawing with transparency
            QRectF updateRect = QRectF(straightLineStartPoint, lastPoint).normalized().adjusted(-20, -20, 20, 20);
            update(updateRect.toRect());
            
            // Erase from start point to current position
            eraseStroke(straightLineStartPoint, currentPoint, event->pressure());
            
            // Update last point
            lastPoint = currentPoint;
            
            // Only track benchmarking when enabled
            if (benchmarking) {
                processedTimestamps.push_back(benchmarkTimer.elapsed());
            }
        } else {
            // Normal drawing mode OR eraser regardless of straight line mode
            if (isErasing) {
            eraseStroke(lastPoint, event->position(), event->pressure());
        } else {
            drawStroke(lastPoint, event->position(), event->pressure());
        }
        lastPoint = event->position();
            
            // Only track benchmarking when enabled
            if (benchmarking) {
        processedTimestamps.push_back(benchmarkTimer.elapsed());
            }
        }
    } else if (event->type() == QEvent::TabletRelease) {
        // Only process drawing completion if we were actually drawing (pen tip was on surface)
        // Side button releases should not trigger drawing completion
        Qt::MouseButton releasedButton = event->button();
        bool isTipRelease = (releasedButton == Qt::LeftButton || releasedButton == Qt::NoButton);
        
#ifdef Q_OS_WIN
        // Windows-specific: Handle side button release during hover drawing mode
        // On Windows, we need to finalize straight line and lasso when side button is released
        // Note: We use stored flags (windowsHoverWasStraightLine, windowsHoverWasLasso) because
        // MainWindow's eventFilter may have already disabled the modes before we get here
        bool isSideButtonRelease = (releasedButton == Qt::MiddleButton || releasedButton == Qt::RightButton);
        
        if (windowsStylusHoverDrawing && isSideButtonRelease) {
            if (windowsHoverWasStraightLine && !isErasing) {
                // Finalize straight line in hover mode
                qreal pressure = event->pressure();
                pressure = qMax(pressure, 0.5);
                
                // Only draw if there's meaningful distance
                QPointF delta = event->position() - straightLineStartPoint;
                if (delta.manhattanLength() > 5) {
                    drawStroke(straightLineStartPoint, event->position(), pressure);
                    if (!edited) {
                        edited = true;
                    }
                }
                
                update();
            } else if (windowsHoverWasStraightLine && isErasing) {
                // Finalize erasing line in hover mode
                qreal pressure = qMax(event->pressure(), 0.5);
                eraseStroke(straightLineStartPoint, event->position(), pressure);
                update();
                if (!edited) {
                    edited = true;
                }
            } else if (windowsHoverWasLasso && selectingWithRope) {
                // Finalize lasso selection in hover mode
                if (lassoPathPoints.size() > 2) {
                    lassoPathPoints << lassoPathPoints.first(); // Close the polygon
                    
                    if (!lassoPathPoints.boundingRect().isEmpty()) {
                        // Create a QPolygonF in buffer coordinates
                        QPolygonF bufferLassoPath;
                        for (const QPointF& p_widget_logical : std::as_const(lassoPathPoints)) {
                            bufferLassoPath << mapLogicalWidgetToPhysicalBuffer(p_widget_logical);
                        }

                        // Get the bounding box of this path on the buffer
                        QRectF bufferPathBoundingRect = bufferLassoPath.boundingRect();

                        // Copy that part of the main buffer
                        QPixmap originalPiece = buffer.copy(bufferPathBoundingRect.toRect());

                        // Create the selectionBuffer and fill transparent
                        selectionBuffer = QPixmap(originalPiece.size());
                        selectionBuffer.fill(Qt::transparent);
                        
                        // Create a mask from the lasso path
                        QPainterPath maskPath;
                        maskPath.addPolygon(bufferLassoPath.translated(-bufferPathBoundingRect.topLeft()));
                        
                        // Paint the originalPiece onto selectionBuffer using the mask
                        QPainter selectionPainter(&selectionBuffer);
                        selectionPainter.setClipPath(maskPath);
                        selectionPainter.drawPixmap(0, 0, originalPiece);
                        selectionPainter.end();

                        selectionAreaCleared = false;
                        selectionMaskPath = maskPath.translated(bufferPathBoundingRect.topLeft());
                        selectionBufferRect = bufferPathBoundingRect;
                        
                        // Calculate the correct selectionRect in logical widget coordinates
                        QRectF logicalSelectionRect = mapRectBufferToWidgetLogical(bufferPathBoundingRect);
                        selectionRect = logicalSelectionRect.toRect();
                        exactSelectionRectF = logicalSelectionRect;
                        
                        update(logicalSelectionRect.adjusted(-2, -2, 2, 2).toRect());
                        
                        // Re-enable ropeToolMode so user can move the selection
                        // MainWindow has already disabled it, but we need it enabled for interaction
                        ropeToolMode = true;
                        windowsLassoNeedsAutoDisable = true; // Mark for auto-disable after interaction
                        
                        // Emit signal for context menu
                        QPoint menuPosition = selectionRect.center();
                        QTimer::singleShot(500, this, [this, menuPosition]() {
                            if (!selectionBuffer.isNull() && !selectionRect.isEmpty() && !movingSelection) {
                                emit ropeSelectionCompleted(menuPosition);
                            }
                        });
                    }
                }
                lassoPathPoints.clear();
                selectingWithRope = false;
            }
            
            // Reset Windows hover mode flags
            windowsStylusHoverDrawing = false;
            windowsHoverWasStraightLine = false;
            windowsHoverWasLasso = false;
            drawing = false;
            
            // Start auto-save if edited
            if (edited && autoSaveTimer && !autoSaveTimer->isActive()) {
                autoSaveTimer->start();
            }
            
            event->accept();
            return; // Don't continue to regular release handling
        }
#endif
        
        if (isTipRelease && drawing && straightLineMode && !isErasing) {
            // Draw the final line on release with the current pressure
            qreal pressure = event->pressure();
            
            // Always use at least a minimum pressure
            pressure = qMax(pressure, 0.5);
            
            drawStroke(straightLineStartPoint, event->position(), pressure);
            
            // Only track benchmarking when enabled
            if (benchmarking) {
                processedTimestamps.push_back(benchmarkTimer.elapsed());
            }
            
            // Force repaint to clear the preview line
            update();
            if (!edited){
                edited = true;
            }
        } else if (isTipRelease && drawing && straightLineMode && isErasing) {
            // For erasing in straight line mode, most of the work is done during movement
            // Just ensure one final erasing pass from start to end point
            qreal pressure = qMax(event->pressure(), 0.5);
            
            // Final pass to ensure the entire line is erased
            eraseStroke(straightLineStartPoint, event->position(), pressure);
            
            // Force update to clear any remaining artifacts
            update();
            if (!edited){
                edited = true;
            }
        }
        
        // Only clear drawing flag on tip release, not side button release
        if (isTipRelease) {
            drawing = false;
        }
        
        // ✅ AUTO-SAVE: Start timer when stroke ends (debouncing pattern)
        // Timer will be reset if user starts drawing again before it fires
        if (edited && autoSaveTimer && !autoSaveTimer->isActive()) {
            autoSaveTimer->start();
        }
        
        // Reset tool state if we were using the hardware eraser
        if (wasUsingHardwareEraser) {
            currentTool = previousTool;
            hardwareEraserActive = false;  // Reset hardware eraser tracking
            
            // ✅ Restore the thickness for the previous tool
            switch (currentTool) {
                case ToolType::Pen:
                    penThickness = penToolThickness;
                    break;
                case ToolType::Marker:
                    penThickness = markerToolThickness;
                    break;
                case ToolType::Eraser:
                    penThickness = eraserToolThickness;
                    break;
            }
        }

        if (ropeToolMode) {
            if (selectingWithRope) {
                if (lassoPathPoints.size() > 2) { // Need at least 3 points for a polygon
                    lassoPathPoints << lassoPathPoints.first(); // Close the polygon
                    
                    if (!lassoPathPoints.boundingRect().isEmpty()) {
                        // 1. Create a QPolygonF in buffer coordinates using proper transformation
                        QPolygonF bufferLassoPath;
                        for (const QPointF& p_widget_logical : std::as_const(lassoPathPoints)) {
                            bufferLassoPath << mapLogicalWidgetToPhysicalBuffer(p_widget_logical);
                        }

                        // 2. Get the bounding box of this path on the buffer
                        QRectF bufferPathBoundingRect = bufferLassoPath.boundingRect();

                        // 3. Copy that part of the main buffer
                        QPixmap originalPiece = buffer.copy(bufferPathBoundingRect.toRect());

                        // 4. Create the selectionBuffer (same size as originalPiece) and fill transparent
                        selectionBuffer = QPixmap(originalPiece.size());
                        selectionBuffer.fill(Qt::transparent);
                        
                        // 5. Create a mask from the lasso path
                        QPainterPath maskPath;
                        // The lasso path for the mask needs to be relative to originalPiece.topLeft()
                        maskPath.addPolygon(bufferLassoPath.translated(-bufferPathBoundingRect.topLeft()));
                        
                        // 6. Paint the originalPiece onto selectionBuffer, using the mask
                        QPainter selectionPainter(&selectionBuffer);
                        selectionPainter.setClipPath(maskPath);
                        selectionPainter.drawPixmap(0,0, originalPiece);
                        selectionPainter.end();

                        // 7. DON'T clear the selected area from the main buffer yet
                        // The area will only be cleared when the user actually starts moving the selection
                        // This way, if the user cancels without moving, the original content remains
                        selectionAreaCleared = false;
                        
                        // Store the mask path and buffer rect for later clearing when movement starts
                        selectionMaskPath = maskPath.translated(bufferPathBoundingRect.topLeft());
                        selectionBufferRect = bufferPathBoundingRect;
                        
                        // 8. Calculate the correct selectionRect in logical widget coordinates
                        QRectF logicalSelectionRect = mapRectBufferToWidgetLogical(bufferPathBoundingRect);
                        selectionRect = logicalSelectionRect.toRect();
                        exactSelectionRectF = logicalSelectionRect;
                        
                        // Update the area of the selection on screen
                        update(logicalSelectionRect.adjusted(-2,-2,2,2).toRect());
                        
                        // Emit signal for context menu at the center of the selection with a delay
                        // This allows the user to immediately start moving the selection if desired
                        QPoint menuPosition = selectionRect.center();
                        QTimer::singleShot(500, this, [this, menuPosition]() {
                            // Only show menu if selection still exists and hasn't been moved
                            if (!selectionBuffer.isNull() && !selectionRect.isEmpty() && !movingSelection) {
                                emit ropeSelectionCompleted(menuPosition);
                            }
                        });
                    }
                }
                lassoPathPoints.clear(); // Ready for next selection, or move
                selectingWithRope = false;
                // Now, if the user presses inside selectionRect, movingSelection will become true.
            } else if (movingSelection) {
                if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
                    QPainter painter(&buffer);
                    // Explicitly set composition mode to draw on top of existing content
                    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                    // Use exact floating-point position if available for more precise placement
                    QPointF topLeft = exactSelectionRectF.isEmpty() ? selectionRect.topLeft() : exactSelectionRectF.topLeft();
                    // Use proper coordinate transformation to get buffer coordinates
                    QPointF bufferDest = mapLogicalWidgetToPhysicalBuffer(topLeft);
                    painter.drawPixmap(bufferDest.toPoint(), selectionBuffer);
                    painter.end();
                    
                    // Update the pasted area
                    QRectF bufferPasteRect(bufferDest, selectionBuffer.size());
                    update(mapRectBufferToWidgetLogical(bufferPasteRect).adjusted(-2,-2,2,2));
                    
                    // Clear selection after pasting, making it permanent
                    selectionBuffer = QPixmap();
                    selectionRect = QRect();
                    exactSelectionRectF = QRectF();
                    movingSelection = false;
                    selectionJustCopied = false;
                    selectionAreaCleared = false;
                    selectionMaskPath = QPainterPath();
                    selectionBufferRect = QRectF();
#ifdef Q_OS_WIN
                    // Auto-disable lasso mode if this was a Windows stylus hover lasso
                    if (windowsLassoNeedsAutoDisable) {
                        ropeToolMode = false;
                        windowsLassoNeedsAutoDisable = false;
                    }
#endif
                }
            }
        }
        

    }
    event->accept();
}

// Helper function to calculate area that needs updating for preview
QRectF InkCanvas::calculatePreviewRect(const QPointF &start, const QPointF &oldEnd, const QPointF &newEnd) {
    // Calculate centering offsets - use the same calculation as in paintEvent
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Calculate the buffer coordinates for our points (same as in drawStroke)
    QPointF adjustedStart = start - QPointF(centerOffsetX, centerOffsetY);
    QPointF adjustedOldEnd = oldEnd - QPointF(centerOffsetX, centerOffsetY);
    QPointF adjustedNewEnd = newEnd - QPointF(centerOffsetX, centerOffsetY);
    
    QPointF bufferStart = (adjustedStart / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    QPointF bufferOldEnd = (adjustedOldEnd / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    QPointF bufferNewEnd = (adjustedNewEnd / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    
    // Now convert from buffer coordinates to screen coordinates
    QPointF screenStart = (bufferStart - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY);
    QPointF screenOldEnd = (bufferOldEnd - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY);
    QPointF screenNewEnd = (bufferNewEnd - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY);
    
    // Create rectangles for both the old and new lines
    QRectF oldLineRect = QRectF(screenStart, screenOldEnd).normalized();
    QRectF newLineRect = QRectF(screenStart, screenNewEnd).normalized();
    
    // Calculate padding based on pen thickness and device pixel ratio
    qreal effectiveScale = getEffectiveDpiScale();
    qreal padding;
    
    if (currentTool == ToolType::Eraser) {
        padding = penThickness * 6.0 * effectiveScale;
    } else if (currentTool == ToolType::Marker) {
        padding = penThickness * 8.0 * effectiveScale;
    } else {
        padding = penThickness * effectiveScale;
    }
    
    // Ensure minimum padding
    padding = qMax(padding, 15.0);
    
    // Combine rectangles with appropriate padding
    QRectF combinedRect = oldLineRect.united(newLineRect);
    return combinedRect.adjusted(-padding, -padding, padding, padding);
}

void InkCanvas::mousePressEvent(QMouseEvent *event) {
    // Check if click is on a picture window - handle specific interactions
    if (pictureManager && event->button() == Qt::LeftButton) {
        // Convert widget coordinates to canvas coordinates
        QPointF widgetPos = event->position();
        QPointF canvasPos = mapWidgetToCanvas(widgetPos);
        
        // Check if click is on any picture window
        PictureWindow *hitWindow = pictureManager->hitTest(canvasPos.toPoint());
        if (hitWindow) {
            // Handle specific picture window interactions
            if (hitWindow->isClickOnDeleteButton(canvasPos.toPoint())) {
                // Delete button clicked
                // qDebug() << "Delete button clicked on picture window";
                hitWindow->onDeleteClicked();
                return;
            }
            
            PictureWindow::ResizeHandle resizeHandle = hitWindow->getResizeHandleAtCanvasPos(canvasPos.toPoint());
            if (resizeHandle != PictureWindow::None) {
                // Resize handle clicked - enter edit mode and start resize
                // qDebug() << "Resize handle clicked on picture window";
                if (!hitWindow->isInEditMode()) {
                    hitWindow->enterEditMode();
                }
                // Set up resize state in canvas
                activePictureWindow = hitWindow;
                pictureResizing = true;
                pictureResizeHandle = static_cast<int>(resizeHandle);
                // Store widget-local position for consistent coordinate system
                pictureInteractionStartPos = event->position().toPoint();
                pictureStartRect = hitWindow->getCanvasRect();
                picturePreviousRect = pictureStartRect; // Initialize previous rect
                
                // ✅ FAST MOTION FIX: Clear any existing outline artifacts before starting
                update(); // Full update to ensure clean starting state
                return;
            }
            
            if (hitWindow->isClickOnHeader(canvasPos.toPoint()) || hitWindow->isClickOnPictureBody(canvasPos.toPoint())) {
                // ✅ TOUCH UX: Header or picture body clicked - enter edit mode and start drag
                // qDebug() << "Header or picture body clicked on picture window";
                if (!hitWindow->isInEditMode()) {
                    hitWindow->enterEditMode();
                }
                // Set up drag state in canvas
                activePictureWindow = hitWindow;
                pictureDragging = true;
                // Store widget-local position for consistent coordinate system
                pictureInteractionStartPos = event->position().toPoint();
                pictureStartRect = hitWindow->getCanvasRect();
                picturePreviousRect = pictureStartRect; // Initialize previous rect
                
                // ✅ FAST MOTION FIX: Clear any existing outline artifacts before starting
                update(); // Full update to ensure clean starting state
                return;
            }
            
            // Click on picture body - handle long press for edit mode or short tap for exit
            if (!hitWindow->isInEditMode()) {
                // Start long press timer for edit mode by forwarding to the picture window
                QPoint localPos = canvasPos.toPoint() - hitWindow->getCanvasRect().topLeft();
                QMouseEvent *forwardedEvent = new QMouseEvent(
                    event->type(),
                    localPos,
                    event->globalPosition(),
                    event->button(),
                    event->buttons(),
                    event->modifiers()
                );
                QApplication::postEvent(hitWindow, forwardedEvent);
                return;
            } else {
                // Already in edit mode - check for short tap to exit
                QPoint localPos = canvasPos.toPoint() - hitWindow->getCanvasRect().topLeft();
                QMouseEvent *forwardedEvent = new QMouseEvent(
                    event->type(),
                    localPos,
                    event->globalPosition(),
                    event->button(),
                    event->buttons(),
                    event->modifiers()
                );
                QApplication::postEvent(hitWindow, forwardedEvent);
                return;
            }
        } else {
            // Click was on empty canvas, exit all edit modes
            pictureManager->exitAllEditModes();
        }
    }
    
    // Handle picture selection when enabled
    if (pictureSelectionMode && event->button() == Qt::LeftButton) {
        pictureSelecting = true;
        pictureSelectionStart = event->pos();
        
        // ✅ NEW: Show choice dialog instead of directly opening file dialog
        PictureSourceDialog sourceDialog(this);
        if (sourceDialog.exec() != QDialog::Accepted) {
            // User cancelled
            setPictureSelectionMode(false);
            MainWindow *mainWindow = findMainWindow();
            if (mainWindow) {
                mainWindow->updatePictureButtonState();
            }
            event->accept();
            return;
        }
        
        QString imagePath;
        int currentPage = getLastActivePage();
        
        if (sourceDialog.getResult() == PictureSourceDialog::LoadFromDisk) {
            // Traditional file dialog
            imagePath = QFileDialog::getOpenFileName(this, tr("Select Image"), "", 
                tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.tiff *.webp)"));
            
            if (!imagePath.isEmpty() && pictureManager) {
                // Copy image to notebook (this increments reference count)
                imagePath = pictureManager->copyImageToNotebook(imagePath, currentPage);
            }
        } else if (sourceDialog.getResult() == PictureSourceDialog::UseExisting) {
            // Show existing picture picker
            ExistingPictureDialog existingDialog(this, currentPage, this);
            if (existingDialog.exec() == QDialog::Accepted) {
                QString selectedPath = existingDialog.getSelectedImagePath();
                
                if (!selectedPath.isEmpty()) {
                    // ✅ For existing pictures, just increment reference count and reuse the path
                    // DON'T call copyImageToNotebook - it would create a duplicate file!
                    QString imageHash = extractImageHashFromPath(selectedPath);
                    if (!imageHash.isEmpty()) {
                        incrementImageReference(imageHash);
                        imagePath = selectedPath; // Reuse the existing image file
                    }
                }
            }
        }
        
        // Create picture window if we have an image
        if (!imagePath.isEmpty() && pictureManager) {
            // Use widget position - PictureWindowManager::createPictureWindow converts to canvas coords
            QRect initialRect(pictureSelectionStart, QSize(200, 150));
            
            PictureWindow* window = pictureManager->createPictureWindow(initialRect, imagePath);
            Q_UNUSED(window);
            
            // Save with combined-aware handling
            saveCombinedWindowsForPage(currentPage);
            
            // Mark canvas as edited
            setEdited(true);
        }
        
        // Exit picture selection mode after placing a picture
        setPictureSelectionMode(false);
        
        // Update the main window button state
        MainWindow *mainWindow = findMainWindow();
        if (mainWindow) {
            mainWindow->updatePictureButtonState();
        }
        
        event->accept();
        return;
    }
    
    // ✅ NEW FEATURE: Handle right-click for clipboard paste when picture selection mode is active
    if (pictureSelectionMode && event->button() == Qt::RightButton) {
        // qDebug() << "InkCanvas::mousePressEvent() - Right-click in picture selection mode!";
        
        pictureSelecting = true;
        pictureSelectionStart = event->pos();
        
        // Try to paste image from clipboard
        QString clipboardImagePath = pasteImageFromClipboard();
        
        if (!clipboardImagePath.isEmpty()) {
            // Create picture window with clipboard image
            if (pictureManager) {
                int currentPage = getLastActivePage();
                
                // Create picture window at clicked position
                QRect initialRect(pictureSelectionStart, QSize(200, 150));
                
                PictureWindow* window = pictureManager->createPictureWindow(initialRect, clipboardImagePath);
                
                if (window) {
                    // ✅ COMBINED MODE FIX: Use combined-aware save to handle Y-coordinate adjustment
                    saveCombinedWindowsForPage(currentPage);
                    
                    // Disable picture selection mode after successful paste
                    setPictureSelectionMode(false);
                    
                    // Update the main window button state
                    MainWindow *mainWindow = qobject_cast<MainWindow*>(parent());
                    if (!mainWindow) {
                        // Try to find MainWindow through the widget hierarchy
                        QWidget *current = this;
                        while (current && !mainWindow) {
                            current = current->parentWidget();
                            mainWindow = qobject_cast<MainWindow*>(current);
                        }
                    }
                    
                    if (mainWindow) {
                        mainWindow->updatePictureButtonState();
                    }
                    
                    // Show brief success message
                    QMessageBox::information(this, tr("Image Pasted"), 
                        tr("Image from clipboard pasted successfully."));
                }
            }
        }
        
        event->accept();
        return;
    }
    
    // Handle PDF text selection when enabled (mouse/touch fallback - stylus handled in tabletEvent)
    if (pdfTextSelectionEnabled && isPdfLoaded) {
        if (event->button() == Qt::LeftButton) {
            // ✅ LAZY LOAD: Only load text boxes when user actually attempts selection
            // Check if text boxes need to be loaded or reloaded for current page
            if (currentCachedPage >= 0 && currentTextBoxesLoadedForPage != currentCachedPage) {
                loadPdfTextBoxes(currentCachedPage);
            }
            
            pdfTextSelecting = true;
            pdfSelectionStart = event->position();
            pdfSelectionEnd = pdfSelectionStart;
            
            // Clear any existing selected text boxes without resetting pdfTextSelecting
            selectedTextBoxes.clear();
            
            setCursor(Qt::IBeamCursor); // Ensure cursor is correct
            update(); // Refresh display
            event->accept();
            return;
        }
    }
    
    // Ignore mouse/touch input on canvas for drawing (drawing only works with tablet/stylus)
    event->ignore();
}

void InkCanvas::mouseMoveEvent(QMouseEvent *event) {
    // Handle picture window drag/resize
    if (pictureDragging || pictureResizing) {
        handlePictureMouseMove(event);
        return;
    }
    
    // Handle PDF text selection when enabled (mouse/touch fallback - stylus handled in tabletEvent)
    if (pdfTextSelectionEnabled && isPdfLoaded && pdfTextSelecting) {
        pdfSelectionEnd = event->position();
        
        // Store pending selection for throttled processing
        pendingSelectionStart = pdfSelectionStart;
        pendingSelectionEnd = pdfSelectionEnd;
        hasPendingSelection = true;
        
        // Start timer if not already running (throttled to 60 FPS)
        if (!pdfTextSelectionTimer->isActive()) {
            pdfTextSelectionTimer->start();
        }
        
        event->accept();
        return;
    }
    
    // Update cursor based on mode when not actively selecting
    if (pdfTextSelectionEnabled && isPdfLoaded && !pdfTextSelecting) {
        setCursor(Qt::IBeamCursor);
    }
    
    event->ignore();
}

void InkCanvas::mouseReleaseEvent(QMouseEvent *event) {
    // Handle picture window drag/resize
    if ((pictureDragging || pictureResizing) && event->button() == Qt::LeftButton) {
        handlePictureMouseRelease(event);
        return;
    }
    
    // Handle PDF text selection when enabled (mouse/touch fallback - stylus handled in tabletEvent)
    if (pdfTextSelectionEnabled && isPdfLoaded && pdfTextSelecting) {
        if (event->button() == Qt::LeftButton) {
            pdfSelectionEnd = event->position();
            
            // Process any pending selection immediately on release
            if (pdfTextSelectionTimer && pdfTextSelectionTimer->isActive()) {
                pdfTextSelectionTimer->stop();
                if (hasPendingSelection) {
                    updatePdfTextSelection(pendingSelectionStart, pendingSelectionEnd, true); // Final selection
                    hasPendingSelection = false;
                }
            } else {
                // Update selection with final position
                updatePdfTextSelection(pdfSelectionStart, pdfSelectionEnd, true); // Final selection
            }
            
            pdfTextSelecting = false;
            
            // Check for link clicks if no text was selected
            QString selectedText = getSelectedPdfText();
            if (selectedText.isEmpty()) {
                handlePdfLinkClick(event->position());
            } else {
                // Show context menu for text selection
                QPoint globalPos = mapToGlobal(event->position().toPoint());
                showPdfTextSelectionMenu(globalPos);
            }
            
            event->accept();
            return;
        }
    }
    

    
    event->ignore();
}

void InkCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
    // Check if double-click is in text selection mode and on a highlight
    if (pdfTextSelectionEnabled && isPdfLoaded && event->button() == Qt::LeftButton) {
        QPointF clickPos = event->position();
        
        // Convert widget coordinates to PDF coordinates
        QPointF pdfCoords = mapWidgetToPdfCoordinates(clickPos);
        
        // Get current page(s) to check
        QList<int> pagesToCheck;
        pagesToCheck.append(currentTextPageNumber);
        if (currentTextPageNumberSecond >= 0) {
            pagesToCheck.append(currentTextPageNumberSecond);
        }
        
        // Check if click position intersects with any highlight on visible page(s)
        for (int pageNum : pagesToCheck) {
            if (pageNum < 0) continue; // Skip invalid pages
            
            // Check all highlights on this page
            for (const TextHighlight &highlight : persistentHighlights) {
                if (highlight.pageNumber == pageNum && highlight.boundingBox.contains(pdfCoords)) {
                    // Found a highlight at click position!
                    // If it has a linked markdown note, emit signal to open it
                    if (!highlight.markdownWindowId.isEmpty()) {
                        emit highlightDoubleClicked(highlight.id);
                        event->accept();
                        return;
                    }
                }
            }
        }
    }
    
    // Default handling
    QWidget::mouseDoubleClickEvent(event);
}

void InkCanvas::drawStroke(const QPointF &start, const QPointF &end, qreal pressure) {
    if (buffer.isNull()) {
        initializeBuffer();
    }

    // Track if this is the first edit
    if (!edited){
        edited = true;
    }
    
    // ✅ AUTO-SAVE: Reset timer on new stroke to prevent saving during active drawing
    // Timer will start when stroke ends (debouncing pattern)
    if (autoSaveTimer && autoSaveTimer->isActive()) {
        autoSaveTimer->stop();
    }

    QPainter painter(&buffer);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal thickness = penThickness;

    qreal updatePadding = (currentTool == ToolType::Marker) ? thickness * 4.0 : 10;

    if (currentTool == ToolType::Marker) {
        thickness *= 8.0;
        QColor markerColor = penColor;
        // Adjust alpha based on whether we're in straight line mode
        if (straightLineMode) {
            // For straight line mode, use higher alpha to make it more visible
            markerColor.setAlpha(40);
        } else {
            // For regular drawing, use lower alpha for the usual marker effect
            markerColor.setAlpha(4);
        }
        QPen pen(markerColor, thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
    } else { // Default Pen
        qreal scaledThickness = thickness * pressure;  // **Linear pressure scaling**
        QPen pen(penColor, scaledThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
    }

    // Calculate centering offsets
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert screen position to buffer position, accounting for centering
    QPointF adjustedStart = start - QPointF(centerOffsetX, centerOffsetY);
    QPointF adjustedEnd = end - QPointF(centerOffsetX, centerOffsetY);
    
    QPointF bufferStart = (adjustedStart / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    QPointF bufferEnd = (adjustedEnd / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);

    painter.drawLine(bufferStart, bufferEnd);

    QRectF updateRect = QRectF(bufferStart, bufferEnd)
                        .normalized()
                        .adjusted(-updatePadding, -updatePadding, updatePadding, updatePadding);

    QRect scaledUpdateRect = QRect(
        ((updateRect.topLeft() - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY)).toPoint(),
        ((updateRect.bottomRight() - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY)).toPoint()
    );
    update(scaledUpdateRect);
}

void InkCanvas::eraseStroke(const QPointF &start, const QPointF &end, qreal pressure) {
    if (buffer.isNull()) {
        initializeBuffer();
    }

    // Track if this is the first edit
    if (!edited){
        edited = true;
    }
    
    // ✅ AUTO-SAVE: Reset timer on new stroke to prevent saving during active drawing
    // Timer will start when stroke ends (debouncing pattern)
    if (autoSaveTimer && autoSaveTimer->isActive()) {
        autoSaveTimer->stop();
    }

    QPainter painter(&buffer);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);

    qreal eraserThickness = penThickness * 6.0;
    QPen eraserPen(Qt::transparent, eraserThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(eraserPen);

    // Calculate centering offsets
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert screen position to buffer position, accounting for centering
    QPointF adjustedStart = start - QPointF(centerOffsetX, centerOffsetY);
    QPointF adjustedEnd = end - QPointF(centerOffsetX, centerOffsetY);
    
    QPointF bufferStart = (adjustedStart / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    QPointF bufferEnd = (adjustedEnd / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);

    painter.drawLine(bufferStart, bufferEnd);

    qreal updatePadding = eraserThickness / 2.0 + 5.0; // Half the eraser thickness plus some extra padding
    QRectF updateRect = QRectF(bufferStart, bufferEnd)
                        .normalized()
                        .adjusted(-updatePadding, -updatePadding, updatePadding, updatePadding);

    QRect scaledUpdateRect = QRect(
        ((updateRect.topLeft() - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY)).toPoint(),
        ((updateRect.bottomRight() - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY)).toPoint()
    );
    update(scaledUpdateRect);
}

void InkCanvas::setPenColor(const QColor &color) {
    penColor = color;
}

void InkCanvas::setPenThickness(qreal thickness) {
    // Set thickness for the current tool
    switch (currentTool) {
        case ToolType::Pen:
            penToolThickness = thickness;
            break;
        case ToolType::Marker:
            markerToolThickness = thickness;
            break;
        case ToolType::Eraser:
            eraserToolThickness = thickness;
            break;
    }
    
    // Update the current thickness for efficient drawing
    penThickness = thickness;
}

void InkCanvas::adjustAllToolThicknesses(qreal zoomRatio) {
    // Adjust thickness for all tools to maintain visual consistency
    penToolThickness *= zoomRatio;
    markerToolThickness *= zoomRatio;
    eraserToolThickness *= zoomRatio;
    
    // Update the current thickness based on the current tool
    switch (currentTool) {
        case ToolType::Pen:
            penThickness = penToolThickness;
            break;
        case ToolType::Marker:
            penThickness = markerToolThickness;
            break;
        case ToolType::Eraser:
            penThickness = eraserToolThickness;
            break;
    }
}

void InkCanvas::setTool(ToolType tool) {
    currentTool = tool;
    
    // Switch to the thickness for the new tool
    switch (currentTool) {
        case ToolType::Pen:
            penThickness = penToolThickness;
            break;
        case ToolType::Marker:
            penThickness = markerToolThickness;
            break;
        case ToolType::Eraser:
            penThickness = eraserToolThickness;
            break;
    }
}

void InkCanvas::resetStraightLineStartPoint() {
    // If currently drawing (pen tip on surface), update the start point to current position
    // This is used when straight line mode is enabled via stylus button while already drawing
    if (drawing) {
        straightLineStartPoint = lastPoint;
    }
}

void InkCanvas::setSaveFolder(const QString &folderPath) {
    // ✅ Handle .spn packages by extracting to temporary directory
    if (SpnPackageManager::isSpnPackage(folderPath)) {
        // Clean up previous temp directory if exists
        if (!tempWorkingDir.isEmpty()) {
            SpnPackageManager::cleanupTempDir(tempWorkingDir);
        }
        
        // Extract .spn package to temporary directory
        tempWorkingDir = SpnPackageManager::extractSpnToTemp(folderPath);
        if (tempWorkingDir.isEmpty()) {
            qWarning() << "Failed to extract .spn package:" << folderPath;
        return;
    }

        saveFolder = tempWorkingDir;
        actualPackagePath = folderPath; // Store the .spn package path for display
        isSpnPackage = true;
    } else {
        saveFolder = folderPath;
        actualPackagePath.clear();
        tempWorkingDir.clear();
        isSpnPackage = false;
    }
    
    clearPdfNoDelete(); 

    if (!saveFolder.isEmpty()) {
        QDir().mkpath(saveFolder);
        loadNotebookMetadata();  // ✅ Load unified JSON metadata when save folder is set
    }
}

void InkCanvas::saveToFile(int pageNumber) {
    if (saveFolder.isEmpty()) {
        return;
    }
    
    if (!edited) {
        return;
    }

    // Check if this is a combined canvas (double height due to combined pages)
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    // If we have a background image (PDF) and buffer is roughly double its height, it's combined
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2; // Each PDF page in the combined image
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (isCombinedCanvas) {
        // Split the combined canvas and save both halves
        int bufferWidth = buffer.width();
        
        // Save current page (top half)
        QString currentFilePath = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
        QPixmap currentPageBuffer = buffer.copy(0, 0, bufferWidth, singlePageHeight);
        QImage currentImage(currentPageBuffer.size(), QImage::Format_ARGB32);
        currentImage.fill(Qt::transparent);
        {
            QPainter painter(&currentImage);
            painter.drawPixmap(0, 0, currentPageBuffer);
        }
        currentImage.save(currentFilePath, "PNG");
        
        // Save next page (bottom half) - DIRECT SAVE like top half
        // ✅ FIX: Don't merge, just save directly to properly handle erasure/deletion
        // The buffer already contains the complete state of both pages after loading
        int nextPageNumber = pageNumber + 1;
        QString nextFilePath = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(nextPageNumber, 5, 10, QChar('0'));
        QPixmap nextPageBuffer = buffer.copy(0, singlePageHeight, bufferWidth, singlePageHeight);
        QImage nextImage(nextPageBuffer.size(), QImage::Format_ARGB32);
        nextImage.fill(Qt::transparent);
        {
            QPainter painter(&nextImage);
            painter.drawPixmap(0, 0, nextPageBuffer);
        }
        nextImage.save(nextFilePath, "PNG");
        
        // ✅ OPTIMIZATION: Populate cache with split pages to avoid reload from disk
        // This eliminates the save-invalidate-reload cycle for smooth page switching
        insertPageIntoCache(pageNumber, currentPageBuffer);
        insertPageIntoCache(nextPageNumber, nextPageBuffer);
        
    } else {
        // Standard single page save
        QString filePath = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
        QImage image(buffer.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.drawPixmap(0, 0, buffer);
        image.save(filePath, "PNG");
        
        // ✅ OPTIMIZATION: Populate cache with saved page
        insertPageIntoCache(pageNumber, buffer);
    }
    
    edited = false;
    
    // ✅ AUTO-SAVE: Stop timer after manual save (prevents redundant auto-save)
    if (autoSaveTimer && autoSaveTimer->isActive()) {
        autoSaveTimer->stop();
    }
    
    // Save windows with appropriate handling for combined canvas
    saveCombinedWindowsForPage(pageNumber);
    
    // ✅ Sync changes to .spn package if needed
    syncSpnPackage();
}

void InkCanvas::loadPage(int pageNumber) {
    if (saveFolder.isEmpty()) return;

    // Hide any picture windows from the previous page BEFORE loading the new page content.
    // This ensures the correct repaint area and stops the transparency timer.
    if (pictureManager) {
        pictureManager->hideAllWindows();
    }

    // Update current note page tracker
    currentCachedNotePage = pageNumber;

    // ✅ NEW APPROACH: Cache single pages, combine on-the-fly
    // Load individual pages into cache (will skip if already cached)
    loadSingleNotePageToCache(pageNumber);
    loadSingleNotePageToCache(pageNumber + 1);

    // Combine the two pages on-the-fly to create the display buffer
    QPixmap currentPageCanvas;
    QPixmap nextPageCanvas;
    bool currentExists = false;
    bool nextExists = false;
    bool loadedFromCache = false; // Track if we loaded anything from cache
    
    // Use the newly cached page or initialize buffer if loading failed
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.contains(pageNumber)) {
            currentPageCanvas = *noteCache.object(pageNumber);
            currentExists = true;
            loadedFromCache = true;
            // ✅ LRU: Mark as recently accessed
            noteCacheAccessOrder.removeAll(pageNumber);
            noteCacheAccessOrder.append(pageNumber);
        }
        if (noteCache.contains(pageNumber + 1)) {
            nextPageCanvas = *noteCache.object(pageNumber + 1);
            nextExists = true;
            loadedFromCache = true;
            // ✅ LRU: Mark as recently accessed
            noteCacheAccessOrder.removeAll(pageNumber + 1);
            noteCacheAccessOrder.append(pageNumber + 1);
        }
    }
    // Combine the two pages into the display buffer
    if (currentExists || nextExists) {
        int combinedWidth = qMax(currentExists ? currentPageCanvas.width() : 0, 
                                 nextExists ? nextPageCanvas.width() : 0);
        int combinedHeight = (currentExists ? currentPageCanvas.height() : 0) + 
                            (nextExists ? nextPageCanvas.height() : 0);

        // If only one page exists, double the height
        if (currentExists && !nextExists) {
            combinedHeight = currentPageCanvas.height() * 2;
            combinedWidth = currentPageCanvas.width();
        } else if (!currentExists && nextExists) {
            combinedHeight = nextPageCanvas.height() * 2;
            combinedWidth = nextPageCanvas.width();
        }

        buffer = QPixmap(combinedWidth, combinedHeight);
        buffer.fill(Qt::transparent);

        QPainter painter(&buffer);
        if (currentExists) {
            painter.drawPixmap(0, 0, currentPageCanvas);
        }
        if (nextExists) {
            int yOffset = currentExists ? currentPageCanvas.height() : nextPageCanvas.height();
            painter.drawPixmap(0, yOffset, nextPageCanvas);
        }
        painter.end();
    } else {
        // No pages exist - initialize empty buffer
        initializeBuffer();
        loadedFromCache = false;
    }
    
    // Reset edited state when loading a new page
    edited = false;
    
    // ✅ AUTO-SAVE: Stop timer when switching pages (new page gets fresh auto-save cycle)
    if (autoSaveTimer && autoSaveTimer->isActive()) {
        autoSaveTimer->stop();
    }
    
    // Handle background image loading (PDF or custom background)
    if (isPdfLoaded && pdfDocument && pageNumber >= 0 && pageNumber < pdfDocument->numPages()) {
        // Use PDF as background (should already be cached by loadPdfPage) - thread-safe
        {
            QMutexLocker locker(&pdfCacheMutex);
            if (pdfCache.contains(pageNumber)) {
                backgroundImage = *pdfCache.object(pageNumber);
            
            // Resize canvas buffer to match PDF page size if needed
            // BUT: Don't resize if we have a combined canvas (double height)
            bool isCombinedCanvas = false;
                if (buffer.height() >= backgroundImage.height() * 1.8) {
                    isCombinedCanvas = true;
                }
                
                if (!isCombinedCanvas && backgroundImage.size() != buffer.size()) {
                QPixmap newBuffer(backgroundImage.size());
                newBuffer.fill(Qt::transparent);

                // Copy existing drawings
                QPainter painter(&newBuffer);
                painter.drawPixmap(0, 0, buffer);

                buffer = newBuffer;
                // Don't constrain widget size - let it expand to fill available space
                // The paintEvent will center the PDF content within the widget
            
                // ❌ REMOVED: Don't cache the combined buffer! Cache should only have single pages from disk
            }
        }
        } // Close pdfCacheMutex scope
    } else {
        // Handle custom background images
        QString bgFileName = saveFolder + QString("/bg_%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
        QString metadataFile = saveFolder + QString("/.%1_bgsize_%2.txt").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));

        int bgWidth = 0, bgHeight = 0;
        if (QFile::exists(metadataFile)) {
            QFile file(metadataFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                in >> bgWidth >> bgHeight;
                file.close();
            }
        }

        if (QFile::exists(bgFileName)) {
            QImage bgImage(bgFileName);
            backgroundImage = QPixmap::fromImage(bgImage);

            // Resize canvas **only if background resolution is different**
            if (bgWidth > 0 && bgHeight > 0 && (bgWidth != width() || bgHeight != height())) {
                // Create a new buffer
                QPixmap newBuffer(bgWidth, bgHeight);
                newBuffer.fill(Qt::transparent);

                // Copy existing drawings to the new buffer
                QPainter painter(&newBuffer);
                painter.drawPixmap(0, 0, buffer);

                // Assign the new buffer
                buffer = newBuffer;
                setMaximumSize(bgWidth, bgHeight);
                
                // ❌ REMOVED: Don't cache the combined buffer! Cache should only have single pages from disk
            }
        } else {
            backgroundImage = QPixmap(); // No background for this page
            // Only apply device pixel ratio fix if buffer was NOT loaded from cache
            // This prevents resizing cached buffers that might already be correctly sized
            if (!loadedFromCache) {
                QScreen *screen = QGuiApplication::primaryScreen();
                QSize logicalSize = screen ? screen->size() : QSize(1440, 900);
                qreal effectiveScale = getEffectiveDpiScale(screen);
                QSize expectedPixelSize = logicalSize * effectiveScale;
                
                // ✅ Double the vertical height for pseudo smooth scrolling system
                expectedPixelSize.setHeight(expectedPixelSize.height() * 2);
                
                if (buffer.size() != expectedPixelSize) {
                    // Buffer is wrong size, need to resize it properly
                    QPixmap newBuffer(expectedPixelSize);
                    newBuffer.fill(Qt::transparent);
                    
                    // Copy existing drawings if buffer was smaller
                    if (!buffer.isNull()) {
                        QPainter painter(&newBuffer);
                        painter.drawPixmap(0, 0, buffer);
                    }
                    
                    buffer = newBuffer;
                    setMaximumSize(expectedPixelSize);
                }
            }
        }
    }

    // Cache adjacent note pages after delay for faster navigation
    checkAndCacheAdjacentNotePages(pageNumber);
    
    // ✅ Handle touch gesture continuation after page load (for active dragging/inertia across page boundary)
    if (continueTouchGestureAfterPageLoad) {
        // Restore pan offsets to continue the gesture seamlessly
        panOffsetX = pendingContinuationPanX;
        panOffsetY = pendingContinuationPanY;
        
        // Reset touch pan start positions for correct delta calculation
        touchPanStartX = panOffsetX;
        touchPanStartY = panOffsetY;
        
        // ✅ CRITICAL: Update inertia pan positions to match the new page
        // This ensures inertia scrolling continues from the correct position
        inertiaPanX = panOffsetX;
        inertiaPanY = panOffsetY;
        
        // Emit pan change to update scrollbars
        emit panChanged(panOffsetX, panOffsetY);
        
        // Clear old cached frame immediately to prevent memory leak
        cachedFrame = QPixmap();
        cachedFrameOffset = QPoint(0, 0);
        
        // Schedule cached frame rebuild after the widget has been rendered
        // Use a zero-delay timer to ensure rendering happens first, without blocking
        QTimer::singleShot(0, this, [this]() {
            if (isTouchPanning) {
                // Only rebuild if still touch panning (prevents stale timer callbacks)
                // PERFORMANCE FIX: Recalculate and grab only canvas region
                qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
                qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
                
                cachedCanvasRegion = QRect(
                    qRound(centerOffsetX - panOffsetX * (internalZoomFactor / 100.0)),
                    qRound(centerOffsetY - panOffsetY * (internalZoomFactor / 100.0)),
                    qRound(scaledCanvasWidth),
                    qRound(scaledCanvasHeight)
                );
                
                cachedCanvasRegion = cachedCanvasRegion.intersected(rect());
                cachedFrame = grab(cachedCanvasRegion);
                cachedFrameOffset = QPoint(0, 0);
            }
        });
        
        // Reset continuation flag
        continueTouchGestureAfterPageLoad = false;
        pendingContinuationPanX = 0;
        pendingContinuationPanY = 0;
        
        // Reset page switch progress to allow detection of the next page boundary
        // Use a short timer to ensure the page is fully loaded before allowing next switch
        QTimer::singleShot(50, this, [this]() {
            pageSwitchInProgress = false;
        });
    }

    update();
    adjustSize();            // Force the widget to update its size
    parentWidget()->update();
    
    // Load markdown windows AFTER canvas is fully rendered and sized
    // Use a single-shot timer to ensure the canvas is fully updated
    QTimer::singleShot(0, this, [this, pageNumber]() {
        loadCombinedWindowsForPage(pageNumber);
    });
}

void InkCanvas::deletePage(int pageNumber) {
    if (saveFolder.isEmpty()) {
        return;
    }
    QString fileName = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
    QString bgFileName = saveFolder + QString("/bg_%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
    QString metadataFileName = saveFolder + QString("/.%1_bgsize_%2.txt").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
    
    #ifdef Q_OS_WIN
    // Remove hidden attribute before deleting in Windows
    SetFileAttributesW(reinterpret_cast<const wchar_t *>(metadataFileName.utf16()), FILE_ATTRIBUTE_NORMAL);
    #endif
    
    QFile::remove(fileName);
    QFile::remove(bgFileName);
    QFile::remove(metadataFileName);

    // Remove deleted page from note cache
    {
        QMutexLocker locker(&noteCacheMutex);
    noteCache.remove(pageNumber);
        noteCacheAccessOrder.removeAll(pageNumber);
    }

    // Delete picture windows for this page
    if (pictureManager) {
        pictureManager->deleteWindowsForPage(pageNumber);
    }

    if (pdfDocument){
        loadPdfPage(pageNumber);
    }
    else{
        loadPage(pageNumber);
    }

}

void InkCanvas::clearCurrentPage() {
    int pageNumber = lastActivePage;
    
    // Clear the drawing buffer - only the current page (upper half in combined mode)
    if (buffer.isNull()) {
        initializeBuffer();
    } else {
        if (isCombinedCanvasMode()) {
            // Combined canvas mode: only clear the upper half (current page)
            int singlePageHeight = getSinglePageHeight();
            QPainter painter(&buffer);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(0, 0, buffer.width(), singlePageHeight, Qt::transparent);
            painter.end();
        } else {
            // Single page mode: clear entire buffer
            buffer.fill(Qt::transparent);
        }
    }
    
    // Clear all picture windows from current page (already deletes files permanently)
    if (pictureManager) {
        pictureManager->clearCurrentPageWindows();
    }
    
    // Clear highlights for current page
    clearHighlightsForPage(pageNumber);
    
    // Clear markdown notes for current page
    clearMarkdownNotesForPage(pageNumber);
    
    // Mark as edited and update display
    edited = true;
    update();
    
    // ✅ AUTO-SAVE: Start timer after clearing page
    if (edited && autoSaveTimer && !autoSaveTimer->isActive()) {
        autoSaveTimer->start();
    }
    
    // Auto-save the cleared page
    if (!saveFolder.isEmpty()) {
        saveToFile(pageNumber);
    }
    
    // NOTE: Do NOT emit markdownNotesUpdated() here - that signal auto-opens the sidebar
    // (intended for note creation). MainWindow::deleteCurrentPage() handles sidebar refresh.
}

void InkCanvas::setBackground(const QString &filePath, int pageNumber) {
    if (saveFolder.isEmpty()) {
        return; // No save folder set
    }

    // Construct full path inside save folder
    QString bgFileName = saveFolder + QString("/bg_%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));

    // Copy the file to the save folder
    QFile::copy(filePath, bgFileName);

    // Load the background image
    QImage bgImage(bgFileName);

    if (!bgImage.isNull()) {
        // Save background resolution in metadata file
        QString metadataFile = saveFolder + QString("/.%1_bgsize_%2.txt")
                                            .arg(notebookId)
                                            .arg(pageNumber, 5, 10, QChar('0'));
        QFile file(metadataFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << bgImage.width() << " " << bgImage.height();
            file.close();
        }

        #ifdef Q_OS_WIN
        // Set hidden attribute for metadata on Windows
        SetFileAttributesW(reinterpret_cast<const wchar_t *>(metadataFile.utf16()), FILE_ATTRIBUTE_HIDDEN);
        #endif    

        // Only resize if the background size is different
        if (bgImage.width() != width() || bgImage.height() != height()) {
            // Create a new buffer with the new size
            QPixmap newBuffer(bgImage.width(), bgImage.height());
            newBuffer.fill(Qt::transparent);

            // Copy existing drawings
            QPainter painter(&newBuffer);
            painter.drawPixmap(0, 0, buffer);

            // Assign new buffer and update canvas size
            buffer = newBuffer;
            setMaximumSize(bgImage.width(), bgImage.height());
        }

        backgroundImage = QPixmap::fromImage(bgImage);
        
        update();
        adjustSize();
        parentWidget()->update();
    }

    update();  // Refresh canvas
}

void InkCanvas::setZoom(int zoomLevel) {
    int newZoom = qMax(10, qMin(zoomLevel, 400)); // Limit zoom to 10%-400%
    if (zoomFactor != newZoom) {
        zoomFactor = newZoom;
        internalZoomFactor = zoomFactor; // Sync internal zoom
        
        // Clear cached frame when zoom changes to avoid mismatched zoom levels
        cachedFrame = QPixmap();
        cachedFrameOffset = QPoint(0, 0);
        
        update();
        emit zoomChanged(zoomFactor);
    }
}

void InkCanvas::updatePanOffsets(int xOffset, int yOffset) {
    panOffsetX = xOffset;
    panOffsetY = yOffset;
    update();
}

int InkCanvas::getPanOffsetX() const {
    return panOffsetX;
}

int InkCanvas::getPanOffsetY() const {
    return panOffsetY;
}

int InkCanvas::getZoom() const {
    return zoomFactor;
}

void InkCanvas::setPanX(int value) {
    if (panOffsetX != value) {
    panOffsetX = value;
    update();
        emit panChanged(panOffsetX, panOffsetY);
    }
}

void InkCanvas::setPanY(int value) {
    if (panOffsetY != value) {
        int oldPanOffsetY = panOffsetY;
        panOffsetY = value;
        update();
        emit panChanged(panOffsetX, panOffsetY);
        checkAutoscrollThreshold(oldPanOffsetY, value);
    }
}

void InkCanvas::setPanWithTouchScroll(int xOffset, int yOffset) {
    // Squid-style efficient panning: draw cached frame shifted by pan delta
    // This method is ONLY called during active touch panning gestures
    
    if (panOffsetX == xOffset && panOffsetY == yOffset) {
        return; // No change
    }
    
    int oldPanOffsetY = panOffsetY;
    
    // Calculate pixel delta BEFORE updating pan offsets (important for first pan after zoom)
    int deltaX = -(xOffset - touchPanStartX) * (internalZoomFactor / 100.0);
    int deltaY = -(yOffset - touchPanStartY) * (internalZoomFactor / 100.0);
    
    panOffsetX = xOffset;
    panOffsetY = yOffset;
    
    // Update the cached frame offset for the next paint
    cachedFrameOffset = QPoint(deltaX, deltaY);
    
    // PERFORMANCE FIX: Only update the region that actually changes
    // Calculate the union of old and new canvas positions to minimize GPU work
    QRect oldCanvasRect = cachedCanvasRegion;
    QRect newCanvasRect = cachedCanvasRegion.translated(cachedFrameOffset);
    QRect updateRect = oldCanvasRect.united(newCanvasRect);
    
    // Clip update rect to widget bounds to avoid updating outside the widget
    updateRect = updateRect.intersected(rect());
    
    // Use update(rect) for non-blocking updates to reduce touch input lag
    // Only invalidate the region that actually changes, not the entire widget
    // This is critical for performance on low-end GPUs (Mali, Panfrost, etc.)
    update(updateRect);
    
    // Emit signal for MainWindow to update scrollbars
    // MainWindow checks isTouchPanningActive() and skips expensive operations
    emit panChanged(panOffsetX, panOffsetY);
    
    // Check autoscroll threshold for page flipping
    checkAutoscrollThreshold(oldPanOffsetY, yOffset);
}

bool InkCanvas::isPdfLoadedFunc() const {
    return isPdfLoaded;
}

int InkCanvas::getTotalPdfPages() const {
    return totalPdfPages;
}

Poppler::Document* InkCanvas::getPdfDocument() const {
    return pdfDocument.get();
}

void InkCanvas::saveCurrentPage() {
    MainWindow *mainWin = qobject_cast<MainWindow*>(parentWidget());  // ✅ Get main window
    if (!mainWin) return;
    
    int currentPage = mainWin->getCurrentPageForCanvas(this);  // ✅ Get correct page
    saveToFile(currentPage);
}

QColor InkCanvas::getPenColor(){
    return penColor;
}

qreal InkCanvas::getPenThickness(){
    return penThickness;
}

ToolType InkCanvas::getCurrentTool() {
    return currentTool;
}


// for background
void InkCanvas::setBackgroundStyle(BackgroundStyle style) {
    backgroundStyle = style;
    update();  // Trigger repaint
}

void InkCanvas::setBackgroundColor(const QColor &color) {
    backgroundColor = color;
    update();
}

void InkCanvas::setBackgroundDensity(int density) {
    backgroundDensity = density;
    update();
}

// saveBackgroundMetadata implementation moved to unified JSON system below








void InkCanvas::loadNotebookId() {
    QString idFile = saveFolder + "/.notebook_id.txt";
    if (QFile::exists(idFile)) {
        QFile file(idFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            notebookId = in.readLine().trimmed();
        }
    } else {
        // No ID file → create new random ID
        notebookId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "");
        saveNotebookId();
    }
}

void InkCanvas::saveNotebookId() {
    QString idFile = saveFolder + "/.notebook_id.txt";
    QFile file(idFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << notebookId;
    }
}




bool InkCanvas::event(QEvent *event) {
    // Handle native gestures (macOS/Linux trackpad)
    if (event->type() == QEvent::NativeGesture) {
        QNativeGestureEvent *nge = static_cast<QNativeGestureEvent*>(event);
        
        // Handle zoom gesture
        if (nge->gestureType() == Qt::ZoomNativeGesture) {
            if (touchGestureMode == TouchGestureMode::Disabled || 
                touchGestureMode == TouchGestureMode::YAxisOnly) {
                event->accept();
                return true;
            }
            
            // Initialize on first zoom event
            if (!isTrackpadPinchZooming) {
                isTrackpadPinchZooming = true;
                targetZoomFactor = internalZoomFactor;
            }
            
            // Apply zoom - value is the scale delta (e.g., 0.1 for 10% zoom)
            qreal scaleFactor = 1.0 + nge->value();
            targetZoomFactor *= scaleFactor;
            targetZoomFactor = qBound(10.0, targetZoomFactor, 400.0);
            
            trackpadZoomCenterPoint = nge->position();
            
            if (!trackpadZoomAnimationTimer->isActive()) {
                trackpadZoomAnimationTimer->start();
            }
            
            // Reset timeout
            trackpadPinchZoomTimeoutTimer->start();
            
            event->accept();
            return true;
        }
    }
    
    // Handle native pinch gestures (Linux/KDE trackpad pinch-to-zoom)
    // On Linux, pinch-to-zoom comes as QPinchGesture, not Ctrl+Wheel like Windows
    if (event->type() == QEvent::Gesture) {
        QGestureEvent *gestureEvent = static_cast<QGestureEvent*>(event);
        if (QPinchGesture *pinch = static_cast<QPinchGesture*>(gestureEvent->gesture(Qt::PinchGesture))) {
            // DISABLED MODE: Block all gestures
            if (touchGestureMode == TouchGestureMode::Disabled) {
                event->accept();
                return true;
            }
            
            // Y-AXIS ONLY MODE: Disable pinch-zoom
            if (touchGestureMode == TouchGestureMode::YAxisOnly) {
                event->accept();
                return true;
            }
            
            if (pinch->state() == Qt::GestureStarted) {
                // Initialize zoom at gesture start
                internalZoomFactor = zoomFactor;
                targetZoomFactor = zoomFactor;
                isTrackpadPinchZooming = true;
            }
            
            if (pinch->state() == Qt::GestureUpdated || pinch->state() == Qt::GestureStarted) {
                // Get scale factor from gesture
                qreal scaleFactor = pinch->scaleFactor();
                QPointF centerPoint = pinch->centerPoint();
                
                // Update target zoom (accumulative)
                targetZoomFactor *= scaleFactor;
                targetZoomFactor = qBound(10.0, targetZoomFactor, 400.0);
                
                // Store zoom center point
                trackpadZoomCenterPoint = mapFromGlobal(centerPoint.toPoint());
                
                // Start/continue smooth zoom animation
                if (!trackpadZoomAnimationTimer->isActive()) {
                    trackpadZoomAnimationTimer->start();
                }
            }
            
            if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled) {
                isTrackpadPinchZooming = false;
                // Let animation finish naturally
            }
            
            event->accept();
            return true;
        }
    }
    
    // ========================================================================
    // WHEEL EVENT HANDLING: Trackpad Gestures
    // ========================================================================
    // MainWindow routes trackpad events here; mouse wheel is handled there.
    // This handles: trackpad scrolling and trackpad pinch-to-zoom (Ctrl+Wheel)
    // ========================================================================
    if (event->type() == QEvent::Wheel) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
        
        // --- Extract event properties ---
        const QInputDevice *device = wheelEvent->device();
        const bool isFromTouchpad = device && (device->type() == QInputDevice::DeviceType::TouchPad);
        const bool hasCtrlModifier = wheelEvent->modifiers() & Qt::ControlModifier;
        const bool hasPixelDelta = !wheelEvent->pixelDelta().isNull();
        const bool hasScrollPhase = wheelEvent->phase() != Qt::NoScrollPhase;
        const int angleX = qAbs(wheelEvent->angleDelta().x());
        const int angleY = qAbs(wheelEvent->angleDelta().y());
        const bool hasSmallAngle = (angleY > 0 && angleY < 120) || (angleX > 0 && angleX < 120);
        
        // --- Disabled mode: block trackpad, allow mouse wheel to propagate ---
        if (touchGestureMode == TouchGestureMode::Disabled) {
            const bool hasExactWheelStep = (angleY == 120 && angleX == 0) || (angleX == 120 && angleY == 0);
            if (hasExactWheelStep && !hasPixelDelta && !hasScrollPhase) {
                return QWidget::event(event); // Let mouse wheel propagate to MainWindow
            }
            event->accept();
            return true; // Block trackpad
        }
        
        // ====================================================================
        // PINCH-TO-ZOOM (Ctrl+Wheel from trackpad)
        // ====================================================================
        // Windows: Trackpad pinch generates synthetic Ctrl + wheel events
        // Linux: Trackpad may be reported as TouchPad device, or Ctrl never pressed
        // ====================================================================
        bool isPinchZoom = false;
        
        if (hasCtrlModifier) {
            if (isTrackpadPinchZooming) {
                // Continue existing pinch-zoom gesture
                isPinchZoom = true;
            } else {
                // Detect start of pinch-zoom gesture
                const qint64 ctrlAge = getCtrlKeyPressAge();
                const bool isSyntheticCtrl = (ctrlAge >= 0 && ctrlAge < 100); // Ctrl pressed within 100ms
                const bool ctrlNeverPressed = (ctrlAge < 0); // Ctrl was never physically pressed
                
                if (isFromTouchpad || isSyntheticCtrl || ctrlNeverPressed) {
                    isPinchZoom = true;
                    isTrackpadPinchZooming = true;
                    targetZoomFactor = internalZoomFactor;
                }
            }
        }
        
        if (isPinchZoom) {
            trackpadPinchZoomTimeoutTimer->start();
            
            // Smooth zoom animation: accumulate target, interpolate at 60fps
            const qreal delta = wheelEvent->angleDelta().y();
            const qreal scaleFactor = 1.0 + (delta / 1500.0); // ~8% zoom per 120 units
            targetZoomFactor = qBound(10.0, targetZoomFactor * scaleFactor, 400.0);
            trackpadZoomCenterPoint = wheelEvent->position();
            
            if (!trackpadZoomAnimationTimer->isActive()) {
                trackpadZoomAnimationTimer->start();
            }
            
            event->accept();
            return true;
        }
        
        // ====================================================================
        // TRACKPAD SCROLLING
        // ====================================================================
        // Detection: pixelDelta, scrollPhase, small angleDelta, or active gesture
        // ====================================================================
        const bool isTrackpad = isTrackpadScrolling || isFromTouchpad || hasPixelDelta || 
                                hasScrollPhase || (hasSmallAngle && !hasCtrlModifier);
        
        if (isTrackpad) {
            // Calculate scroll delta
            qreal deltaX, deltaY;
            if (hasPixelDelta) {
                deltaX = wheelEvent->pixelDelta().x();
                deltaY = wheelEvent->pixelDelta().y();
            } else {
                deltaX = wheelEvent->angleDelta().x() / 4.0;
                deltaY = wheelEvent->angleDelta().y() / 4.0;
            }
            
            // Determine gesture phase (Windows lacks scroll phases)
            bool gestureStart = false;
            bool gestureEnd = false;
            
            switch (wheelEvent->phase()) {
                case Qt::ScrollBegin:
                    gestureStart = true;
                    break;
                case Qt::ScrollEnd:
                    gestureEnd = true;
                    break;
                case Qt::NoScrollPhase:
                    gestureStart = !isTrackpadScrolling;
                    trackpadScrollTimeoutTimer->start();
                    break;
                default:
                    trackpadScrollTimeoutTimer->start();
                    break;
            }
            
            handleTrackpadScroll(deltaX, deltaY, gestureStart, gestureEnd);
            event->accept();
            return true;
        }
        
        // Not a trackpad event - let MainWindow handle as mouse wheel
        return QWidget::event(event);
    }
    
    // Touch gesture handling - only when not disabled
    if (touchGestureMode == TouchGestureMode::Disabled) {
        return QWidget::event(event);
    }
    
    // Skip touch handling when a picture window is in edit mode
    if (pictureWindowEditMode && (event->type() == QEvent::TouchBegin || 
                                 event->type() == QEvent::TouchUpdate || 
                                 event->type() == QEvent::TouchEnd)) {
        // qDebug() << "InkCanvas: Skipping touch event due to picture window edit mode";
        return QWidget::event(event);
    }

    if (event->type() == QEvent::TouchBegin || 
        event->type() == QEvent::TouchUpdate || 
        event->type() == QEvent::TouchEnd) {
        
        // ✅ LINUX FIX: Restart timeout timer on every touch event
        // This ensures we detect when touch events stop arriving unexpectedly
        if (touchPanningTimeoutTimer && event->type() != QEvent::TouchEnd) {
            touchPanningTimeoutTimer->start(); // Restart the 200ms timeout
        }
        
        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        const QList<QTouchEvent::TouchPoint> touchPoints = touchEvent->points();
        
        activeTouchPoints = touchPoints.count();

        if (activeTouchPoints == 1) {
            // Single finger pan
            const QTouchEvent::TouchPoint &touchPoint = touchPoints.first();
            
            if (event->type() == QEvent::TouchBegin) {
                // Stop any ongoing inertia
                if (inertiaTimer->isActive()) {
                    inertiaTimer->stop();
                    cachedFrame = QPixmap(); // Clear old cache
                }
                
                // Stop any ongoing trackpad scrolling (touch takes priority)
                if (isTrackpadScrolling) {
                    isTrackpadScrolling = false;
                    trackpadScrollTimeoutTimer->stop();
                }
                
                // Reset page switch cooldown
                pageSwitchInProgress = false;
                
                isPanning = true;
                isTouchPanning = true;  // Enable efficient scrolling mode
                emit touchPanningChanged(true);  // Notify that touch panning has started
                lastTouchPos = touchPoint.position();
                
                // Store starting pan position
                touchPanStartX = panOffsetX;
                touchPanStartY = panOffsetY;
                
                // Initialize velocity tracking
                velocityTimer.start();
                recentVelocities.clear();
                lastTouchVelocity = QPointF(0, 0);
                
                // Capture current frame for efficient panning
                // PERFORMANCE FIX: Only grab the canvas region (not blank areas)
                qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
                qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
                
                cachedCanvasRegion = QRect(
                    qRound(centerOffsetX - panOffsetX * (internalZoomFactor / 100.0)),
                    qRound(centerOffsetY - panOffsetY * (internalZoomFactor / 100.0)),
                    qRound(scaledCanvasWidth),
                    qRound(scaledCanvasHeight)
                );
                
                // Clip to widget bounds to avoid grabbing outside
                cachedCanvasRegion = cachedCanvasRegion.intersected(rect());
                
                // DEBUG: Print cached region information
                /*
                // Get effective DPI scale (Wayland-aware, respects manual override)
                qreal dpr = getEffectiveDpiScale();
                qDebug() << "=== TOUCH GESTURE START - CACHE INFO ===";
                qDebug() << "Effective DPI Scale (Wayland-aware):" << dpr;
                qDebug() << "Widget size (logical):" << width() << "x" << height() << "=" << (width() * height()) << "pixels";
                qDebug() << "Widget size (physical):" << qRound(width() * dpr) << "x" << qRound(height() * dpr) 
                         << "=" << qRound(width() * dpr * height() * dpr) << "pixels";
                qDebug() << "Canvas buffer:" << buffer.width() << "x" << buffer.height();
                qDebug() << "Zoom factor:" << internalZoomFactor << "%";
                qDebug() << "Cached region (logical):" << cachedCanvasRegion;
                qDebug() << "Cached region size (logical):" << cachedCanvasRegion.width() << "x" << cachedCanvasRegion.height() 
                         << "=" << (cachedCanvasRegion.width() * cachedCanvasRegion.height()) << "pixels";
                */
                // Grab the canvas region - grab() returns physical pixels
                cachedFrame = grab(cachedCanvasRegion);
                cachedFrameOffset = QPoint(0, 0);
                /*
                qDebug() << "CachedFrame actual size (physical):" << cachedFrame.width() << "x" << cachedFrame.height()
                         << "=" << (cachedFrame.width() * cachedFrame.height()) << "pixels";
                qDebug() << "Expected physical size:" << qRound(cachedCanvasRegion.width() * dpr) << "x" 
                         << qRound(cachedCanvasRegion.height() * dpr);
                
                         
                // Calculate savings based on physical pixels (what actually matters for GPU)
                int widgetPhysicalPixels = qRound(width() * dpr * height() * dpr);
                int cachedPhysicalPixels = cachedFrame.width() * cachedFrame.height();
                qDebug() << "Physical pixel savings:" << (widgetPhysicalPixels - cachedPhysicalPixels) << "pixels"
                         << "(" << (100.0 * (1.0 - (qreal)cachedPhysicalPixels / widgetPhysicalPixels)) << "% reduction)";
                qDebug() << "=========================================";
                */
                // Pre-calculate for optimization
                touchPanStartX = panOffsetX;
                touchPanStartY = panOffsetY;
            } else if (event->type() == QEvent::TouchUpdate && isPanning) {
                // ✅ SAFETY: If we're in single-finger panning but suddenly see multiple touch points,
                // this is likely spurious (finger count shouldn't change without TouchBegin/End)
                if (activeTouchPoints != 1) {
                    // Touch count changed mid-gesture - this is invalid, ignore it
                    event->accept();
                    return true;
                }
                QPointF delta = touchPoint.position() - lastTouchPos;
                qint64 elapsed = velocityTimer.elapsed();
                
                // Track velocity for inertia - but only every other frame to reduce overhead
                static int velocitySampleCounter = 0;
                velocitySampleCounter++;
                if (elapsed > 0 && velocitySampleCounter % 2 == 0) {
                    // ✅ Y-AXIS ONLY MODE: Only track Y-axis velocity
                    qreal velocityX = (touchGestureMode == TouchGestureMode::YAxisOnly) ? 0 : delta.x() / elapsed;
                    qreal velocityY = delta.y() / elapsed;
                    QPointF velocity(velocityX, velocityY);
                    recentVelocities.append(qMakePair(velocity, elapsed));
                    
                    // Keep only last 5 velocity samples for smoothing
                    if (recentVelocities.size() > 5) {
                        recentVelocities.removeFirst();
                    }
                }
                velocityTimer.restart();
                
                // Make panning more responsive by using floating-point calculations
                qreal scaledDeltaX = delta.x() / (internalZoomFactor / 100.0);
                qreal scaledDeltaY = delta.y() / (internalZoomFactor / 100.0);
                
                // Calculate new pan positions with sub-pixel precision
                qreal newPanX = panOffsetX - scaledDeltaX;
                qreal newPanY = panOffsetY - scaledDeltaY;
                
                // ✅ Y-AXIS ONLY MODE: Lock X-axis panning
                if (touchGestureMode == TouchGestureMode::YAxisOnly) {
                    newPanX = panOffsetX; // Don't allow X-axis movement
                }
                
                // Clamp pan X to valid range (pan Y is not clamped for pseudo smooth scrolling)
                qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                // If canvas is smaller than widget, lock pan to 0 (centered)
                if (scaledCanvasWidth <= width()) {
                    newPanX = 0;
                } else {
                    // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
                    qreal maxPanX = scaledCanvasWidth - width();
                    newPanX = qBound(0.0, newPanX, maxPanX);
                }
                
                // Pan Y is intentionally NOT clamped here to allow pseudo smooth scrolling
                // (scrolling beyond bounds triggers page switching)
                if (scaledCanvasHeight < height()) {
                    newPanY = 0;
                }
                
                // Use efficient scrolling during touch gesture
                int roundedPanX = qRound(newPanX);
                int roundedPanY = qRound(newPanY);
                setPanWithTouchScroll(roundedPanX, roundedPanY);
                
                lastTouchPos = touchPoint.position();
            }
        } else if (activeTouchPoints == 2) {
            // ✅ Y-AXIS ONLY MODE: Disable pinch-zoom
            if (touchGestureMode == TouchGestureMode::YAxisOnly) {
                event->accept();
                return true;
            }
            
            // ✅ SAFETY: Only block spurious 2-finger events during active page switch recovery
            // This prevents false zoom triggers during fast page switches without blocking legitimate pinch-zoom
            
            // During page switch recovery window (50ms), require TouchBegin for 2-finger gestures
            // This blocks spurious 2-finger detections that happen during page transitions
            // but allows normal pinch-zoom gestures at other times
            if (pageSwitchRecoveryTimer.isValid() && pageSwitchRecoveryTimer.elapsed() < 50 
                && event->type() == QEvent::TouchUpdate) {
                // We're in the critical page switch recovery period - ignore spurious 2-finger events
                // (Reduced to 50ms to minimize interference with legitimate gestures)
                event->accept();
                return true;
            }
            
            // Two finger pinch zoom (legitimate)
            isPanning = false;
            if (isTouchPanning) {
            isTouchPanning = false;  // Disable efficient scrolling during pinch-zoom
                emit touchPanningChanged(false);  // Notify that touch panning has ended
            }
            
            const QTouchEvent::TouchPoint &touch1 = touchPoints[0];
            const QTouchEvent::TouchPoint &touch2 = touchPoints[1];
            
            // Calculate distance between touch points with higher precision
            qreal currentDist = QLineF(touch1.position(), touch2.position()).length();
            qreal startDist = QLineF(touch1.pressPosition(), touch2.pressPosition()).length();
            
            if (event->type() == QEvent::TouchBegin) {
                lastPinchScale = 1.0;
                // Store the starting internal zoom
                internalZoomFactor = zoomFactor;
            } else if (event->type() == QEvent::TouchUpdate && startDist > 0) {
                qreal scale = currentDist / startDist;
                
                // Use exponential scaling for more natural feel
                qreal scaleChange = scale / lastPinchScale;
                
                // Apply scale with higher sensitivity
                internalZoomFactor *= scaleChange;
                internalZoomFactor = qBound(10.0, internalZoomFactor, 400.0);
                
                // Calculate zoom center (midpoint between two fingers)
                QPointF center = (touch1.position() + touch2.position()) / 2.0;
                
                // Account for centering offset
                qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
                qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
                qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
                qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
                
                // Adjust center point for centering offset
                QPointF adjustedCenter = center - QPointF(centerOffsetX, centerOffsetY);
                
                // Always update zoom for smooth animation
                int newZoom = qRound(internalZoomFactor);
                
                // Calculate pan adjustment to keep the zoom centered at pinch point
                QPointF bufferCenter = adjustedCenter / (zoomFactor / 100.0) + QPointF(panOffsetX, panOffsetY);
                
                // Update zoom factor before emitting
                zoomFactor = newZoom;
                
                // Emit zoom change even for small changes
                emit zoomChanged(newZoom);
                
                // Clear cached frame when zoom changes during pinch gesture
                cachedFrame = QPixmap();
                cachedFrameOffset = QPoint(0, 0);
                
                // Adjust pan to keep center point fixed with sub-pixel precision
                qreal newPanX = bufferCenter.x() - adjustedCenter.x() / (internalZoomFactor / 100.0);
                qreal newPanY = bufferCenter.y() - adjustedCenter.y() / (internalZoomFactor / 100.0);
                
                // After zoom, clamp pan X to valid range (pan Y is not clamped for pseudo smooth scrolling)
                qreal newScaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal newScaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                if (newScaledCanvasWidth <= width()) {
                    newPanX = 0;
                } else {
                    // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
                    qreal maxPanX = newScaledCanvasWidth - width();
                    newPanX = qBound(0.0, newPanX, maxPanX);
                }
                
                // Pan Y is intentionally NOT clamped here to allow pseudo smooth scrolling
                // (scrolling beyond bounds triggers page switching)
                if (newScaledCanvasHeight < height()) {
                    newPanY = 0;
                }
                
                emit panChanged(qRound(newPanX), qRound(newPanY));
                
                lastPinchScale = scale;
                
                // Request update only for visible area
                update();
            }
        } else {
            // More than 2 fingers - reset everything to prevent corrupted state
            isPanning = false;
            if (isTouchPanning) {
            isTouchPanning = false;  // Disable efficient scrolling
                emit touchPanningChanged(false);  // Notify that touch panning has ended
            }
            
            // Clear any page switch recovery state - something went wrong
            pageSwitchRecoveryTimer.invalidate();
            continueTouchGestureAfterPageLoad = false;
            
            // Clear cached frame to force full redraw
            cachedFrame = QPixmap();
            cachedFrameOffset = QPoint(0, 0);
        }
        
        if (event->type() == QEvent::TouchEnd) {
            // ✅ LINUX FIX: Stop timeout timer - TouchEnd received properly
            if (touchPanningTimeoutTimer && touchPanningTimeoutTimer->isActive()) {
                touchPanningTimeoutTimer->stop();
            }
            
            isPanning = false;
            lastPinchScale = 1.0;
            activeTouchPoints = 0;
            // Sync internal zoom with actual zoom
            internalZoomFactor = zoomFactor;
            
            // Start inertia scrolling if there's sufficient velocity
            if (isTouchPanning && !recentVelocities.isEmpty()) {
                // Calculate average velocity from recent samples (weighted by time)
                QPointF avgVelocity(0, 0);
                qreal totalWeight = 0;
                for (const auto &sample : recentVelocities) {
                    qreal weight = sample.second; // More recent samples have similar weight
                    avgVelocity += sample.first * weight;
                    totalWeight += weight;
                }
                if (totalWeight > 0) {
                    avgVelocity /= totalWeight;
                }
                
                // Convert velocity from pixels/ms to canvas units/ms (accounting for zoom)
                avgVelocity /= (internalZoomFactor / 100.0);
                
                // Minimum velocity threshold for inertia (canvas units per ms)
                const qreal minVelocity = 0.1; // Adjust for sensitivity
                qreal velocityMagnitude = std::sqrt(avgVelocity.x() * avgVelocity.x() + 
                                                     avgVelocity.y() * avgVelocity.y());
                
                if (velocityMagnitude > minVelocity) {
                    // Start inertia with the calculated velocity
                    inertiaVelocityX = avgVelocity.x();
                    inertiaVelocityY = avgVelocity.y();
                    
                    // ✅ Y-AXIS ONLY MODE: Zero out X-axis inertia velocity
                    if (touchGestureMode == TouchGestureMode::YAxisOnly) {
                        inertiaVelocityX = 0;
                    }
                    
                    inertiaPanX = panOffsetX;
                    inertiaPanY = panOffsetY;
                    
                    // Keep the cached frame for inertia animation
                    // ✅ IMPORTANT: Don't clear isTouchPanning yet - inertia will use it
                    // This keeps markdown/picture windows in frame-only mode during inertia
                    // for optimal performance (touchPanningChanged signal stays true)
                    inertiaTimer->start();
                } else {
                    // No significant velocity, end immediately
                    isTouchPanning = false;
                    emit touchPanningChanged(false);  // Notify that touch panning has ended
                    cachedFrame = QPixmap();
                    cachedFrameOffset = QPoint(0, 0);
                    update();
                }
            } else {
                // No inertia, end immediately
                if (isTouchPanning) {
                    isTouchPanning = false;
                    emit touchPanningChanged(false);  // Notify that touch panning has ended
                    cachedFrame = QPixmap();
                    cachedFrameOffset = QPoint(0, 0);
                    update();
                }
            }
            
            // ✅ Invalidate recovery timer when gesture ends cleanly
            // This prevents stale page switch recovery state from affecting the next gesture
            pageSwitchRecoveryTimer.invalidate();
            
            // Emit signal that touch gesture has ended
            emit touchGestureEnded();
        }
        
        event->accept();
        return true;
    }
    
    return QWidget::event(event);
}

void InkCanvas::updateInertiaScroll() {
    // Deceleration factor (friction) - higher value = faster slowdown
    const qreal friction = 0.92; // Retain 92% of velocity each frame (smooth deceleration)
    const qreal minVelocity = 0.05; // Stop when velocity is very small
    
    // Apply friction
    inertiaVelocityX *= friction;
    inertiaVelocityY *= friction;
    
    // Check if velocity is too small to continue
    qreal velocityMagnitude = std::sqrt(inertiaVelocityX * inertiaVelocityX + 
                                         inertiaVelocityY * inertiaVelocityY);
    
    if (velocityMagnitude < minVelocity) {
        // Stop inertia
        inertiaTimer->stop();
        isTouchPanning = false;
        emit touchPanningChanged(false);  // Notify that touch panning has ended
        pageSwitchInProgress = false; // Reset page switch cooldown
        cachedFrame = QPixmap();
        cachedFrameOffset = QPoint(0, 0);
        update(); // Final full redraw
        return;
    }
    
    // Update pan position (velocity is in canvas units per ms, timer is ~16ms)
    inertiaPanX -= inertiaVelocityX * 16.0; // 16ms per frame at 60fps
    inertiaPanY -= inertiaVelocityY * 16.0;
    
    // Clamp pan X to valid range (pan Y is not clamped for pseudo smooth scrolling)
    qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
    
    if (scaledCanvasWidth <= width()) {
        inertiaPanX = 0;
    } else {
        // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
        qreal maxPanX = scaledCanvasWidth - width();
        inertiaPanX = qBound(0.0, inertiaPanX, maxPanX);
    }
    
    // Pan Y is intentionally NOT clamped here to allow pseudo smooth scrolling
    // (scrolling beyond bounds triggers page switching)
    if (scaledCanvasHeight < height()) {
        inertiaPanY = 0;
    }
    
    // Stop inertia X velocity if we hit a boundary
    if ((inertiaPanX == 0 && inertiaVelocityX < 0) || 
        (inertiaPanX >= scaledCanvasWidth - width() && inertiaVelocityX > 0)) {
        inertiaVelocityX = 0;
    }
    
    // Update pan offsets and trigger repaint
    int newPanX = qRound(inertiaPanX);
    int newPanY = qRound(inertiaPanY);
    
    // Use setPanWithTouchScroll for efficient rendering during inertia
    setPanWithTouchScroll(newPanX, newPanY);
}

void InkCanvas::resetTouchPanningState() {
    // ✅ LINUX FIX: Reset touch panning state if no touch events arrived within timeout
    // This handles cases where TouchEnd event is lost or delayed on Linux
    if (isTouchPanning && !inertiaTimer->isActive()) {
        // Only reset if not in inertia scroll (inertia should complete naturally)
        // qDebug() << "Touch panning timeout - resetting stuck isTouchPanning state";
        
        isTouchPanning = false;
        isPanning = false;
        activeTouchPoints = 0;
        
        // Clear cached frame to force full redraw
        cachedFrame = QPixmap();
        cachedFrameOffset = QPoint(0, 0);
        
        // Notify MainWindow that touch panning ended
        emit touchPanningChanged(false);
        
        // Force a full repaint to show any pending changes
        update();
    }
}

void InkCanvas::resetTrackpadScrollingState() {
    // Called when trackpad scroll timeout fires (no wheel events for ~120ms)
    // This signals the end of a trackpad scroll gesture
    if (isTrackpadScrolling) {
        isTrackpadScrolling = false;
        isPanning = false;
        
        // DO NOT apply our own inertia for trackpad scrolling!
        // Windows Precision Touchpad drivers handle momentum by continuing to send
        // wheel events after the finger lifts. If we start our own inertia, it
        // conflicts with the driver's momentum events, causing back-and-forth skipping.
        // (This is different from touchscreen, which stops sending events when finger lifts)
        
        // End touch panning state
        isTouchPanning = false;
        emit touchPanningChanged(false);
        
        // Clear cached frame to force full redraw
        cachedFrame = QPixmap();
        cachedFrameOffset = QPoint(0, 0);
        
        // Clear velocity tracking
        trackpadRecentVelocities.clear();
        
        // Emit signal that gesture has ended
        emit touchGestureEnded();
        
        update();
    }
}

void InkCanvas::resetTrackpadPinchZoomState() {
    // Called when trackpad pinch-zoom timeout fires (no wheel events for ~150ms)
    // This signals the end of a trackpad pinch-zoom gesture
    if (isTrackpadPinchZooming) {
        isTrackpadPinchZooming = false;
        
        // Stop zoom animation (let it finish naturally to target)
        // Animation will stop itself when it reaches target
        
        // Sync target with current for next gesture
        targetZoomFactor = internalZoomFactor;
        
        // Emit signal that gesture has ended
        emit touchGestureEnded();
    }
}

void InkCanvas::updateTrackpadZoomAnimation() {
    // Smoothly interpolate internalZoomFactor toward targetZoomFactor
    // This creates touchscreen-like smooth zooming from discrete wheel events
    
    const qreal interpolationSpeed = 0.25; // 25% of the way each frame (smooth but responsive)
    const qreal epsilon = 0.5; // Stop when within 0.5% of target
    
    qreal diff = targetZoomFactor - internalZoomFactor;
    
    if (qAbs(diff) < epsilon) {
        // Close enough to target - snap to it and stop
        internalZoomFactor = targetZoomFactor;
        trackpadZoomAnimationTimer->stop();
        
        // Final update
        zoomFactor = qRound(internalZoomFactor);
        emit zoomChanged(zoomFactor);
        update();
        return;
    }
    
    // Interpolate toward target
    internalZoomFactor += diff * interpolationSpeed;
    internalZoomFactor = qBound(10.0, internalZoomFactor, 400.0);
    
    // Apply zoom with center point adjustment (same math as touchscreen pinch)
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Adjust center point for centering offset
    QPointF adjustedCenter = trackpadZoomCenterPoint - QPointF(centerOffsetX, centerOffsetY);
    
    // Calculate pan adjustment to keep the zoom centered at the gesture center
    QPointF bufferCenter = adjustedCenter / (zoomFactor / 100.0) + QPointF(panOffsetX, panOffsetY);
    
    // Update zoom factor
    int newZoom = qRound(internalZoomFactor);
    if (newZoom != zoomFactor) {
        zoomFactor = newZoom;
        emit zoomChanged(newZoom);
    }
    
    // Clear cached frame when zoom changes
    cachedFrame = QPixmap();
    cachedFrameOffset = QPoint(0, 0);
    
    // Adjust pan to keep center point fixed
    qreal newPanX = bufferCenter.x() - adjustedCenter.x() / (internalZoomFactor / 100.0);
    qreal newPanY = bufferCenter.y() - adjustedCenter.y() / (internalZoomFactor / 100.0);
    
    // Clamp pan X to valid range
    qreal newScaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
    qreal newScaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
    
    if (newScaledCanvasWidth <= width()) {
        newPanX = 0;
    } else {
        qreal maxPanX = newScaledCanvasWidth - width();
        newPanX = qBound(0.0, newPanX, maxPanX);
    }
    
    if (newScaledCanvasHeight < height()) {
        newPanY = 0;
    }
    
    // Update pan
    panOffsetX = qRound(newPanX);
    panOffsetY = qRound(newPanY);
    emit panChanged(panOffsetX, panOffsetY);
    
    // Request repaint
    update();
}

void InkCanvas::handleTrackpadScroll(qreal pixelDeltaX, qreal pixelDeltaY, bool gestureStart, bool gestureEnd) {
    // Bridge trackpad two-finger scroll to touch gesture pan system
    // This reuses all the touch gesture infrastructure (cached frame, inertia, autoscroll, etc.)
    
    if (touchGestureMode == TouchGestureMode::Disabled) {
        return;
    }
    
    if (gestureStart && !isTrackpadScrolling) {
        // Don't start trackpad scrolling if touch panning is already active
        if (isTouchPanning && activeTouchPoints > 0) {
            return; // Touch gesture has priority
        }
        
        // Start of trackpad scroll gesture - same as TouchBegin for single finger pan
        
        // Stop any ongoing inertia
        if (inertiaTimer->isActive()) {
            inertiaTimer->stop();
            cachedFrame = QPixmap();
        }
        
        // Reset page switch cooldown
        pageSwitchInProgress = false;
        
        isPanning = true;
        isTouchPanning = true;
        isTrackpadScrolling = true;
        emit touchPanningChanged(true);
        
        // Store starting pan position
        touchPanStartX = panOffsetX;
        touchPanStartY = panOffsetY;
        
        // Initialize velocity tracking
        trackpadVelocityTimer.start();
        trackpadRecentVelocities.clear();
        
        // Capture current frame for efficient panning (same as touch gesture)
        qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
        qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
        
        qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
        qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
        
        cachedCanvasRegion = QRect(
            qRound(centerOffsetX - panOffsetX * (internalZoomFactor / 100.0)),
            qRound(centerOffsetY - panOffsetY * (internalZoomFactor / 100.0)),
            qRound(scaledCanvasWidth),
            qRound(scaledCanvasHeight)
        );
        
        // Clip to widget bounds
        cachedCanvasRegion = cachedCanvasRegion.intersected(rect());
        
        // Grab the canvas region for efficient panning
        cachedFrame = grab(cachedCanvasRegion);
        cachedFrameOffset = QPoint(0, 0);
    }
    
    if (isTrackpadScrolling && !gestureEnd) {
        // Update phase - apply delta to pan (same as TouchUpdate)
        qint64 elapsed = trackpadVelocityTimer.elapsed();
        
        // Track velocity for inertia
        if (elapsed > 0) {
            // Y-AXIS ONLY MODE: Only track Y-axis velocity
            qreal velocityX = (touchGestureMode == TouchGestureMode::YAxisOnly) ? 0 : pixelDeltaX / elapsed;
            qreal velocityY = pixelDeltaY / elapsed;
            QPointF velocity(velocityX, velocityY);
            trackpadRecentVelocities.append(qMakePair(velocity, elapsed));
            
            // Keep only last 5 velocity samples
            if (trackpadRecentVelocities.size() > 5) {
                trackpadRecentVelocities.removeFirst();
            }
        }
        trackpadVelocityTimer.restart();
        
        // Convert pixel delta to pan delta (accounting for zoom)
        // Note: pixelDelta is in screen pixels, need to convert to canvas units
        qreal scaledDeltaX = pixelDeltaX / (internalZoomFactor / 100.0);
        qreal scaledDeltaY = pixelDeltaY / (internalZoomFactor / 100.0);
        
        // Calculate new pan positions
        qreal newPanX = panOffsetX - scaledDeltaX;
        qreal newPanY = panOffsetY - scaledDeltaY;
        
        // Y-AXIS ONLY MODE: Lock X-axis panning
        if (touchGestureMode == TouchGestureMode::YAxisOnly) {
            newPanX = panOffsetX;
        }
        
        // Clamp pan X to valid range (pan Y is not clamped for pseudo smooth scrolling)
        qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
        qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
        
        if (scaledCanvasWidth <= width()) {
            newPanX = 0;
        } else {
            qreal maxPanX = scaledCanvasWidth - width();
            newPanX = qBound(0.0, newPanX, maxPanX);
        }
        
        if (scaledCanvasHeight < height()) {
            newPanY = 0;
        }
        
        // Use efficient scrolling during trackpad gesture
        int roundedPanX = qRound(newPanX);
        int roundedPanY = qRound(newPanY);
        setPanWithTouchScroll(roundedPanX, roundedPanY);
    }
    
    if (gestureEnd && isTrackpadScrolling) {
        // End of gesture - trigger inertia calculation via timeout handler
        resetTrackpadScrollingState();
    }
}

void InkCanvas::handleTrackpadPinchZoom(qreal scaleFactor, QPointF centerPoint) {
    // Bridge trackpad pinch-to-zoom to touch gesture zoom system
    // Trackpad pinch-zoom ALWAYS works regardless of touchGestureMode
    // (touchGestureMode controls touch screen gestures, not trackpad)

    // Y-AXIS ONLY MODE: Disable pinch-zoom (user explicitly chose to disable zoom)
    if (touchGestureMode == TouchGestureMode::YAxisOnly) {
        return;
    }
    
    // Stop any panning during zoom
    if (isPanning || isTrackpadScrolling) {
        isPanning = false;
        isTrackpadScrolling = false;
        if (trackpadScrollTimeoutTimer->isActive()) {
            trackpadScrollTimeoutTimer->stop();
        }
    }
    
    if (isTouchPanning) {
        isTouchPanning = false;
        emit touchPanningChanged(false);
    }
    
    // Apply scale factor to internal zoom
    internalZoomFactor *= scaleFactor;
    internalZoomFactor = qBound(10.0, internalZoomFactor, 400.0);
    
    // Calculate zoom center adjustment (same math as touch pinch zoom)
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Adjust center point for centering offset
    QPointF adjustedCenter = centerPoint - QPointF(centerOffsetX, centerOffsetY);
    
    // Calculate pan adjustment to keep the zoom centered at the gesture center
    QPointF bufferCenter = adjustedCenter / (zoomFactor / 100.0) + QPointF(panOffsetX, panOffsetY);
    
    // Update zoom factor
    int newZoom = qRound(internalZoomFactor);
    zoomFactor = newZoom;
    
    // Emit zoom change
    emit zoomChanged(newZoom);
    
    // Clear cached frame when zoom changes
    cachedFrame = QPixmap();
    cachedFrameOffset = QPoint(0, 0);
    
    // Adjust pan to keep center point fixed
    qreal newPanX = bufferCenter.x() - adjustedCenter.x() / (internalZoomFactor / 100.0);
    qreal newPanY = bufferCenter.y() - adjustedCenter.y() / (internalZoomFactor / 100.0);
    
    // Clamp pan X to valid range
    qreal newScaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
    qreal newScaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
    
    if (newScaledCanvasWidth <= width()) {
        newPanX = 0;
    } else {
        qreal maxPanX = newScaledCanvasWidth - width();
        newPanX = qBound(0.0, newPanX, maxPanX);
    }
    
    if (newScaledCanvasHeight < height()) {
        newPanY = 0;
    }
    
    emit panChanged(qRound(newPanX), qRound(newPanY));
    
    // Request update
    update();
}

void InkCanvas::onAutoSaveTimeout() {
    // ✅ Perform incremental auto-save to reduce page-switch burden
    if (edited && !saveFolder.isEmpty()) {
        saveToFile(lastActivePage);
        saveCombinedWindowsForPage(lastActivePage);
        
        // ✅ OPTIMIZATION: No need to invalidate cache - saveToFile() now populates cache
        // with fresh data directly, eliminating the save-invalidate-reload cycle
        
        edited = false;
        
        // Note: Timer is single-shot, so it won't fire again until the next stroke
        // This prevents continuous disk writes while idle
    }
    
    // ✅ PERFORMANCE FIX: Also flush dirty picture windows to disk
    // This allows page switches to skip disk writes while still ensuring autosave works
    if (!saveFolder.isEmpty()) {
        if (pictureManager) {
            pictureManager->flushDirtyPagesToDisk();
        }
    }
}

// Helper function to map LOGICAL widget coordinates to PHYSICAL buffer coordinates
QPointF InkCanvas::mapLogicalWidgetToPhysicalBuffer(const QPointF& logicalWidgetPoint) {
    // Use the same coordinate transformation logic as drawStroke for consistency
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert widget position to buffer position, accounting for centering
    QPointF adjustedPoint = logicalWidgetPoint - QPointF(centerOffsetX, centerOffsetY);
    QPointF bufferPoint = (adjustedPoint / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    
    return bufferPoint;
}

// Helper function to map a PHYSICAL buffer RECT to LOGICAL widget RECT for updates
QRect InkCanvas::mapRectBufferToWidgetLogical(const QRectF& physicalBufferRect) {
    // Use the same coordinate transformation logic as drawStroke for consistency
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert buffer coordinates back to widget coordinates
    QRectF widgetRect = QRectF(
        (physicalBufferRect.topLeft() - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY),
        physicalBufferRect.size() * (zoomFactor / 100.0)
    );
    
    return widgetRect.toRect();
}

// Rope tool selection actions
void InkCanvas::deleteRopeSelection() {
    if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
        // If the selection area hasn't been cleared from the buffer yet, clear it now for deletion
        if (!selectionAreaCleared && !selectionMaskPath.isEmpty()) {
            QPainter painter(&buffer);
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.fillPath(selectionMaskPath, Qt::transparent);
            painter.end();
        }
        
        // Clear the selection state
        selectionBuffer = QPixmap();
        selectionRect = QRect();
        exactSelectionRectF = QRectF();
        movingSelection = false;
        selectingWithRope = false;
        selectionJustCopied = false;
        selectionAreaCleared = false;
        selectionMaskPath = QPainterPath();
        selectionBufferRect = QRectF();
        
#ifdef Q_OS_WIN
        // Auto-disable lasso mode if this was a Windows stylus hover lasso
        if (windowsLassoNeedsAutoDisable) {
            ropeToolMode = false;
            windowsLassoNeedsAutoDisable = false;
        }
#endif
        
        // Mark as edited since we deleted content
        if (!edited) {
            edited = true;
        }
        
        // ✅ AUTO-SAVE: Start timer after rope selection operation
        if (edited && autoSaveTimer && !autoSaveTimer->isActive()) {
            autoSaveTimer->start();
        }
        
        // Update the entire canvas to remove selection visuals
        update();
    }
}

void InkCanvas::cancelRopeSelection() {
    if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
        // Paste the selection back to its current location (where user moved it)
        QPainter painter(&buffer);
        // Explicitly set composition mode to draw on top of existing content
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // Use the current selection position
        QPointF currentTopLeft = exactSelectionRectF.isEmpty() ? selectionRect.topLeft() : exactSelectionRectF.topLeft();
        QPointF bufferDest = mapLogicalWidgetToPhysicalBuffer(currentTopLeft);
        painter.drawPixmap(bufferDest.toPoint(), selectionBuffer);
        painter.end();
        
        // Store selection buffer size for update calculation before clearing it
        QSize selectionSize = selectionBuffer.size();
        QRect updateRect = QRect(currentTopLeft.toPoint(), selectionSize);
        
        // Clear the selection state
        selectionBuffer = QPixmap();
        selectionRect = QRect();
        exactSelectionRectF = QRectF();
        movingSelection = false;
        selectingWithRope = false;
        selectionJustCopied = false;
        selectionAreaCleared = false;
        selectionMaskPath = QPainterPath();
        selectionBufferRect = QRectF();
        
#ifdef Q_OS_WIN
        // Auto-disable lasso mode if this was a Windows stylus hover lasso
        if (windowsLassoNeedsAutoDisable) {
            ropeToolMode = false;
            windowsLassoNeedsAutoDisable = false;
        }
#endif
        
        // Update the restored area
        update(updateRect.adjusted(-5, -5, 5, 5));
    }
}

void InkCanvas::clearInProgressLasso() {
    // Clear any in-progress lasso drawing state without completing the selection
    // This is used when disabling lasso mode via stylus button
    if (selectingWithRope || !lassoPathPoints.isEmpty()) {
        QRectF updateRect = lassoPathPoints.boundingRect();
        lassoPathPoints.clear();
        selectingWithRope = false;
        movingSelection = false;
        drawing = false;
        // Refresh the area where the lasso path was being drawn
        if (!updateRect.isEmpty()) {
            update(updateRect.toRect().adjusted(-10, -10, 10, 10));
        }
    }
}

void InkCanvas::copyRopeSelection() {
    if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
        // Get current selection position
        QPointF currentTopLeft = exactSelectionRectF.isEmpty() ? selectionRect.topLeft() : exactSelectionRectF.topLeft();
        
        // Calculate new position (right next to the original), but ensure it stays within canvas bounds
        QPointF newTopLeft = currentTopLeft + QPointF(selectionRect.width() + 5, 0); // 5 pixels gap
        
        // Check if the new position would extend beyond buffer boundaries and adjust if needed
        QPointF currentBufferDest = mapLogicalWidgetToPhysicalBuffer(currentTopLeft);
        QPointF newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
        
        // Ensure the copy stays within buffer bounds
        if (newBufferDest.x() + selectionBuffer.width() > buffer.width()) {
            // If it would extend beyond right edge, place it to the left of the original
            newTopLeft = currentTopLeft - QPointF(selectionRect.width() + 5, 0);
            newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
            
            // If it still doesn't fit on the left, place it below the original
            if (newBufferDest.x() < 0) {
                newTopLeft = currentTopLeft + QPointF(0, selectionRect.height() + 5);
                newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
                
                // If it doesn't fit below either, place it above
                if (newBufferDest.y() + selectionBuffer.height() > buffer.height()) {
                    newTopLeft = currentTopLeft - QPointF(0, selectionRect.height() + 5);
                    newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
                    
                    // If none of the positions work, just offset slightly and let it extend
                    if (newBufferDest.y() < 0) {
                        newTopLeft = currentTopLeft + QPointF(10, 10);
                        newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
                    }
                }
            }
        }
        if (newBufferDest.y() + selectionBuffer.height() > buffer.height()) {
            // If it would extend beyond bottom edge, place it above the original
            newTopLeft = currentTopLeft - QPointF(0, selectionRect.height() + 5);
            newBufferDest = mapLogicalWidgetToPhysicalBuffer(newTopLeft);
        }
        
        // First, permanently commit the original selection to the buffer
        QPainter painter(&buffer);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawPixmap(currentBufferDest.toPoint(), selectionBuffer);
        painter.end();
        
        // Clear the original selection's mask path so it won't be cleared later
        // This makes the original permanently part of the buffer
        selectionMaskPath = QPainterPath();
        selectionAreaCleared = true; // Mark as cleared so it won't be cleared again
        
        // Now create a new independent copy selection at the new location
        // Keep the same selectionBuffer content since it's the copied content
        selectionRect = QRect(newTopLeft.toPoint(), selectionRect.size());
        exactSelectionRectF = QRectF(newTopLeft, selectionRect.size());
        selectionJustCopied = true; // Mark that this selection was just copied
        
        // Mark as edited since we added content
        if (!edited) {
            edited = true;
        }
        
        // ✅ AUTO-SAVE: Start timer after rope selection operation
        if (edited && autoSaveTimer && !autoSaveTimer->isActive()) {
            autoSaveTimer->start();
        }
        
        // Update the entire affected area (original + copy + gap)
        QRect updateArea = QRect(currentTopLeft.toPoint(), selectionRect.size())
                          .united(selectionRect)
                          .adjusted(-10, -10, 10, 10);
        update(updateArea);
    }
}

void InkCanvas::copyRopeSelectionToClipboard() {
    if (!selectionBuffer.isNull() && !selectionRect.isEmpty()) {
        // Get the clipboard
        QClipboard *clipboard = QApplication::clipboard();
        if (!clipboard) {
            qWarning() << "Failed to access clipboard";
            QMessageBox::warning(this, tr("Clipboard Error"), 
                tr("Failed to access clipboard for copying rope selection."));
        return;
    }

        // Copy the selection buffer (which contains the selected area) to clipboard
        clipboard->setPixmap(selectionBuffer);
        
        // Optional: Show a brief confirmation message
        QMessageBox::information(this, tr("Copied to Clipboard"), 
            tr("Selected area has been copied to clipboard.\n\nYou can now paste it on other pages or tabs using the picture paste feature."));
        
        // Keep the selection active so user can still move it or perform other operations
        // Don't clear the selection state like we do in copyRopeSelection()
    }
}

// PDF text selection implementation
void InkCanvas::clearPdfTextSelection() {
    // Clear selection state
    selectedTextBoxes.clear();
    pdfTextSelecting = false;
    
    // Cancel any pending throttled updates
    if (pdfTextSelectionTimer && pdfTextSelectionTimer->isActive()) {
        pdfTextSelectionTimer->stop();
    }
    hasPendingSelection = false;
    
    // Refresh display
    update();
    
    // Notify that text selection was cleared (for stylus button delayed disable)
    emit pdfTextSelectionCleared();
}

QString InkCanvas::getSelectedPdfText() const {
    if (selectedTextBoxes.isEmpty()) {
        return QString();
    }
    
    // Pre-allocate string with estimated size for efficiency
    QString selectedText;
    selectedText.reserve(selectedTextBoxes.size() * 20); // Estimate ~20 chars per text box
    
    // Build text with space separators
    for (const Poppler::TextBox* textBox : selectedTextBoxes) {
        if (textBox && !textBox->text().isEmpty()) {
            if (!selectedText.isEmpty()) {
                selectedText += " ";
            }
            selectedText += textBox->text();
        }
    }
    
    return selectedText;
}

void InkCanvas::loadPdfTextBoxes(int pageNumber) {
    // Clear existing text boxes and page number tracking
    // CRITICAL: Clear selectedTextBoxes first to prevent crashes in paintEvent
    selectedTextBoxes.clear();  // Must clear before deleting currentPdfTextBoxes
    
    // ✅ NO LONGER DELETE: currentPdfTextBoxes will reference cached data
    // qDeleteAll(currentPdfTextBoxes); // ❌ DON'T DELETE - cached pointers!
    currentPdfTextBoxes.clear();
    currentPdfTextBoxPageNumbers.clear();
    
    // ✅ Reset page trackers
    currentTextPageNumber = -1;
    currentTextPageNumberSecond = -1;
    currentTextBoxesLoadedForPage = -1;
    
    if (!pdfDocument || pageNumber < 0 || pageNumber >= pdfDocument->numPages()) {
        return;
    }

    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }

    // ✅ CACHE KEY: For combined canvas, use negative page number to differentiate
    int cacheKey = isCombinedCanvas ? -(pageNumber + 1) : pageNumber;
    
    // ✅ CHECK CACHE FIRST
    if (pdfTextBoxCache.contains(cacheKey)) {
        TextBoxCacheEntry* entry = pdfTextBoxCache[cacheKey];
        currentPdfTextBoxes = entry->textBoxes;
        currentPdfTextBoxPageNumbers = entry->pageNumbers;
        currentTextBoxesLoadedForPage = pageNumber; // Track which page these text boxes belong to
        
        // Restore page number trackers for coordinate mapping (critical for selection overlay)
        if (isCombinedCanvas) {
            currentTextPageNumber = pageNumber;
            currentTextPageNumberSecond = pageNumber + 1;
        } else {
            currentTextPageNumber = pageNumber;
            currentTextPageNumberSecond = -1;
        }
        
        // Update LRU access order
        pdfTextBoxCacheAccessOrder.removeAll(cacheKey);
        pdfTextBoxCacheAccessOrder.append(cacheKey);
        return;
    }
    
    // ✅ NOT IN CACHE: Allocate new text boxes
    TextBoxCacheEntry* newEntry = new TextBoxCacheEntry();

    if (isCombinedCanvas) {
        loadPdfTextBoxesForCombinedCanvas(pageNumber, singlePageHeight, newEntry);
    } else {
        loadPdfTextBoxesForSinglePage(pageNumber, newEntry);
    }
    
    // ✅ CACHE THE RESULT
    pdfTextBoxCache[cacheKey] = newEntry;
    pdfTextBoxCacheAccessOrder.append(cacheKey);
    
    // ✅ USE THE CACHED DATA
    currentPdfTextBoxes = newEntry->textBoxes;
    currentPdfTextBoxPageNumbers = newEntry->pageNumbers;
    currentTextBoxesLoadedForPage = pageNumber; // Track which page these text boxes belong to
    
    // ✅ LRU EVICTION: Keep only last 5 pages in cache
    while (pdfTextBoxCacheAccessOrder.size() > 5) {
        int oldestKey = pdfTextBoxCacheAccessOrder.takeFirst();
        TextBoxCacheEntry* oldEntry = pdfTextBoxCache.take(oldestKey);
        delete oldEntry; // Destructor will qDeleteAll(textBoxes)
    }
    
    // ✅ CRITICAL: Also evict old page sizes from cache to prevent unbounded growth
    // Keep page sizes synchronized with text box cache
    // Collect which page numbers are currently in use from cache keys
    QSet<int> pagesInUse;
    for (int key : pdfTextBoxCacheAccessOrder) {
        int absKey = qAbs(key);
        // For combined canvas: key is -(pageNumber+1), so page is |key|-1
        // For single page: key is pageNumber
        if (key < 0) {
            // Combined canvas: pages are (|key|-1) and |key|
            int basePage = absKey - 1;
            pagesInUse.insert(basePage);
            pagesInUse.insert(basePage + 1);
        } else {
            // Single page
            pagesInUse.insert(key);
        }
    }
    
    // Remove page sizes not in use
    auto it = pdfPageSizeCache.begin();
    while (it != pdfPageSizeCache.end()) {
        if (!pagesInUse.contains(it.key())) {
            it = pdfPageSizeCache.erase(it);
        } else {
            ++it;
        }
    }
}

void InkCanvas::loadPdfTextBoxesForSinglePage(int pageNumber, TextBoxCacheEntry* entry) {
    // ✅ Create a temporary local page object (will be destroyed at end of function)
    std::unique_ptr<Poppler::Page> tempPage(pdfDocument->page(pageNumber));
    if (!tempPage) {
        return;
    }

    // ✅ Cache the page size for coordinate mapping (lightweight)
    pdfPageSizeCache[pageNumber] = tempPage->pageSizeF();
    currentTextPageNumber = pageNumber;

    // Load text boxes for the single page and populate cache entry
    auto textBoxVector = tempPage->textList();
    for (auto& textBox : textBoxVector) {
        entry->textBoxes.append(textBox.release()); // Transfer ownership to cache entry
        entry->pageNumbers.append(pageNumber); // Track page number for each text box
    }
    // tempPage automatically destroyed here, releasing Poppler::Page resources
}

void InkCanvas::loadPdfTextBoxesForCombinedCanvas(int pageNumber, int singlePageHeight, TextBoxCacheEntry* entry) {
    // For combined canvas showing pages N and N+1:
    // - Top half shows page N (coordinates as-is)
    // - Bottom half shows page N+1 (coordinates will be adjusted in mapping functions)
    
    // Load text boxes for top half (page N) - use temporary local page object
    {
        std::unique_ptr<Poppler::Page> tempPage(pdfDocument->page(pageNumber));
        if (tempPage) {
            // ✅ Cache the page size for coordinate mapping (lightweight)
            pdfPageSizeCache[pageNumber] = tempPage->pageSizeF();
            currentTextPageNumber = pageNumber;
            
            auto topTextBoxVector = tempPage->textList();
        for (auto& textBox : topTextBoxVector) {
            // Text boxes for top half keep their original coordinates
                entry->textBoxes.append(textBox.release());
                entry->pageNumbers.append(pageNumber); // Track as page N
        }
        }
        // tempPage automatically destroyed here
    }
    
    // Load text boxes for bottom half (page N+1) if it exists
    int nextPageNumber = pageNumber + 1;
    if (nextPageNumber < pdfDocument->numPages()) {
        std::unique_ptr<Poppler::Page> tempPageSecond(pdfDocument->page(nextPageNumber));
        if (tempPageSecond) {
            // ✅ Cache the page size for coordinate mapping (lightweight)
            pdfPageSizeCache[nextPageNumber] = tempPageSecond->pageSizeF();
            currentTextPageNumberSecond = nextPageNumber;
            
            auto bottomTextBoxVector = tempPageSecond->textList();
            for (auto& textBox : bottomTextBoxVector) {
                // Text boxes for bottom half - coordinates will be adjusted in mapping functions
                entry->textBoxes.append(textBox.release());
                entry->pageNumbers.append(nextPageNumber); // Track as page N+1
            }
        }
        // tempPageSecond automatically destroyed here
    }
}

QPointF InkCanvas::mapWidgetToPdfCoordinates(const QPointF &widgetPoint) {
    if (backgroundImage.isNull() || currentTextPageNumber < 0) {
        return QPointF();
    }
    
    // Use the same zoom factor as in paintEvent (internalZoomFactor for smooth animations)
    qreal zoom = internalZoomFactor / 100.0;
    
    // Calculate the scaled canvas size (same as in paintEvent)
    qreal scaledCanvasWidth = buffer.width() * zoom;
    qreal scaledCanvasHeight = buffer.height() * zoom;
    
    // Calculate centering offsets (same as in paintEvent)
    qreal centerOffsetX = 0;
    qreal centerOffsetY = 0;
    
    // Center horizontally if canvas is smaller than widget
    if (scaledCanvasWidth < width()) {
        centerOffsetX = (width() - scaledCanvasWidth) / 2.0;
    }
    
    // Center vertically if canvas is smaller than widget
    if (scaledCanvasHeight < height()) {
        centerOffsetY = (height() - scaledCanvasHeight) / 2.0;
    }
    
    // Reverse the transformations applied in paintEvent
    QPointF adjustedPoint = widgetPoint;
    adjustedPoint -= QPointF(centerOffsetX, centerOffsetY);
    adjustedPoint /= zoom;
    adjustedPoint += QPointF(panOffsetX, panOffsetY);
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    // Determine which page and adjust coordinates for combined canvas
    int targetPageNumber = currentTextPageNumber;
    QPointF finalAdjustedPoint = adjustedPoint;
    
    if (isCombinedCanvas && currentTextPageNumberSecond >= 0) {
        // Check if the point is in the bottom half (page N+1)
        if (adjustedPoint.y() >= singlePageHeight) {
            // Bottom half - use second page and adjust coordinates
            targetPageNumber = currentTextPageNumberSecond;
            finalAdjustedPoint.setY(adjustedPoint.y() - singlePageHeight); // Shift back to page coordinate system
        }
        // Top half - use first page with original coordinates
    }
    
    // Get cached page size
    if (!pdfPageSizeCache.contains(targetPageNumber)) {
        return QPointF(); // Page size not cached
    }
    QSizeF pdfPageSize = pdfPageSizeCache[targetPageNumber];
    QSizeF imageSize = isCombinedCanvas ? QSizeF(backgroundImage.width(), singlePageHeight * 2) : backgroundImage.size();
    
    // For combined canvas, we need to scale based on single page height
    qreal effectiveImageHeight = isCombinedCanvas ? singlePageHeight : imageSize.height();
    
    // Scale from image coordinates to PDF coordinates
    qreal scaleX = pdfPageSize.width() / imageSize.width();
    qreal scaleY = pdfPageSize.height() / effectiveImageHeight;
    
    QPointF pdfPoint;
    pdfPoint.setX(finalAdjustedPoint.x() * scaleX);
    pdfPoint.setY(finalAdjustedPoint.y() * scaleY);
    
    return pdfPoint;
}

QPointF InkCanvas::mapPdfToWidgetCoordinates(const QPointF &pdfPoint, int pageNumber) {
    if (backgroundImage.isNull() || currentTextPageNumber < 0) {
        return QPointF();
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    // Determine which page to use and coordinate adjustments
    int sourcePageNumber = currentTextPageNumber;
    qreal yOffset = 0;
    
    if (isCombinedCanvas && pageNumber != -1) {
        // Find the base page number (first page of the combined canvas)
        int basePage = -1;
        if (!currentPdfTextBoxPageNumbers.empty()) {
            basePage = currentPdfTextBoxPageNumbers[0];
        }
        
        if (pageNumber > basePage && currentTextPageNumberSecond >= 0) {
            // This text box belongs to the second page (bottom half)
            sourcePageNumber = currentTextPageNumberSecond;
            yOffset = singlePageHeight; // Shift down to bottom half
        }
        // Otherwise, use first page (top half) with no offset
    }
    
    // Get cached page size
    if (!pdfPageSizeCache.contains(sourcePageNumber)) {
        return QPointF(); // Page size not cached
    }
    QSizeF pdfPageSize = pdfPageSizeCache[sourcePageNumber];
    
    // Convert from PDF coordinates to image coordinates
    QSizeF imageSize = backgroundImage.size();
    
    // For combined canvas, scale based on single page height
    qreal effectiveImageHeight = isCombinedCanvas ? singlePageHeight : imageSize.height();
    
    // Scale from PDF coordinates to image coordinates
    qreal scaleX = imageSize.width() / pdfPageSize.width();
    qreal scaleY = effectiveImageHeight / pdfPageSize.height();
    
    QPointF imagePoint;
    imagePoint.setX(pdfPoint.x() * scaleX);
    imagePoint.setY(pdfPoint.y() * scaleY + yOffset); // Add offset for bottom half
    
    // Use the same zoom factor as in paintEvent (internalZoomFactor for smooth animations)
    qreal zoom = internalZoomFactor / 100.0;
    
    // Apply the same transformations as in paintEvent
    QPointF widgetPoint = imagePoint;
    widgetPoint -= QPointF(panOffsetX, panOffsetY);
    widgetPoint *= zoom;
    
    // Calculate the scaled canvas size (same as in paintEvent)
    qreal scaledCanvasWidth = buffer.width() * zoom;
    qreal scaledCanvasHeight = buffer.height() * zoom;
    
    // Calculate centering offsets (same as in paintEvent)
    qreal centerOffsetX = 0;
    qreal centerOffsetY = 0;
    
    // Center horizontally if canvas is smaller than widget
    if (scaledCanvasWidth < width()) {
        centerOffsetX = (width() - scaledCanvasWidth) / 2.0;
    }
    
    // Center vertically if canvas is smaller than widget
    if (scaledCanvasHeight < height()) {
        centerOffsetY = (height() - scaledCanvasHeight) / 2.0;
    }
    
    widgetPoint += QPointF(centerOffsetX, centerOffsetY);
    
    return widgetPoint;
}

void InkCanvas::updatePdfTextSelection(const QPointF &start, const QPointF &end, bool isFinal) {
    // Early return if PDF is not loaded or no text boxes available
    if (!isPdfLoaded || currentPdfTextBoxes.isEmpty()) {
        return;
    }

    // Check if this is a drag or just a single click
    // Minimum distance threshold to distinguish between click and drag (in widget coordinates)
    const qreal minDragDistance = 5.0; // pixels
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal dragDistance = std::sqrt(dx * dx + dy * dy);
    
    // If distance is too small, treat it as a single click and don't select anything
    if (dragDistance < minDragDistance) {
        selectedTextBoxes.clear();
        update();
        return;
    }

    // Clear previous selection efficiently
    selectedTextBoxes.clear();

    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = 0;
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height();
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }

    // Convert widget coordinates to PDF coordinates for start and end points
    QPointF startBuffer = mapWidgetToCanvas(start);
    QPointF endBuffer = mapWidgetToCanvas(end);
    
    // Determine base page number
    int basePage = currentTextPageNumber;
    if (basePage < 0) basePage = currentCachedPage;
    
    // Structure to hold text box info with position for sorting
    struct TextBoxInfo {
        Poppler::TextBox* textBox;
        int pageNumber;
        QRectF boundingBox; // In PDF coordinates
        qreal centerY;      // For row-based sorting
        qreal centerX;      // For left-to-right sorting within rows
        int originalIndex;  // To preserve order when needed
    };
    
    QList<TextBoxInfo> textBoxInfoList;
    textBoxInfoList.reserve(currentPdfTextBoxes.size());
    
    // Build list of text boxes with position info
    for (int i = 0; i < currentPdfTextBoxes.size(); ++i) {
        const Poppler::TextBox* textBox = currentPdfTextBoxes[i];
        if (!textBox) continue;
        
        int textBoxPage = (i < currentPdfTextBoxPageNumbers.size()) ? currentPdfTextBoxPageNumbers[i] : -1;
        if (textBoxPage < 0) continue;
        
        TextBoxInfo info;
        info.textBox = const_cast<Poppler::TextBox*>(textBox);
        info.pageNumber = textBoxPage;
        info.boundingBox = textBox->boundingBox();
        info.originalIndex = i;
        
        // Calculate position for sorting (in buffer coordinate system for consistency)
        // For combined canvas, offset bottom page text boxes
        qreal yOffset = 0;
        if (isCombinedCanvas && textBoxPage == basePage + 1) {
            yOffset = singlePageHeight;
        }
        
        // Get page size for coordinate conversion
        QSizeF pageSize = pdfPageSizeCache.value(textBoxPage, QSizeF());
        if (pageSize.isEmpty()) continue;
        
        // Convert PDF box to buffer coordinates for consistent sorting
        qreal bufferWidth = isCombinedCanvas ? buffer.width() : buffer.width();
        qreal bufferHeight = isCombinedCanvas ? singlePageHeight : buffer.height();
        qreal scaleX = bufferWidth / pageSize.width();
        qreal scaleY = bufferHeight / pageSize.height();
        
        qreal boxCenterX = (info.boundingBox.left() + info.boundingBox.right()) / 2.0 * scaleX;
        qreal boxCenterY = (info.boundingBox.top() + info.boundingBox.bottom()) / 2.0 * scaleY + yOffset;
        
        info.centerX = boxCenterX;
        info.centerY = boxCenterY;
        
        textBoxInfoList.append(info);
    }
    
    if (textBoxInfoList.isEmpty()) {
        update();
        return;
    }
    
    // Sort text boxes by reading order: top-to-bottom, then left-to-right
    // Use a tolerance for "same row" detection (~10 pixels in buffer coordinates)
    const qreal rowTolerance = 10.0;
    std::sort(textBoxInfoList.begin(), textBoxInfoList.end(), 
              [rowTolerance](const TextBoxInfo &a, const TextBoxInfo &b) {
        // First sort by page number
        if (a.pageNumber != b.pageNumber) {
            return a.pageNumber < b.pageNumber;
        }
        // Then by row (Y position with tolerance)
        if (std::abs(a.centerY - b.centerY) > rowTolerance) {
            return a.centerY < b.centerY;
        }
        // Same row: sort left to right
        return a.centerX < b.centerX;
    });
    
    // Helper lambda to find the closest text box to a point
    // This checks if the point is inside a text box first, then finds the nearest one
    auto findClosestTextBox = [&textBoxInfoList, singlePageHeight, isCombinedCanvas, basePage, this]
                               (const QPointF &point) -> int {
        int closestIndex = -1;
        qreal minDist = std::numeric_limits<qreal>::max();
        
        for (int i = 0; i < textBoxInfoList.size(); ++i) {
            const TextBoxInfo &info = textBoxInfoList[i];
            
            // Reconstruct the text box bounds in buffer coordinates for accurate checking
            int textBoxPage = info.pageNumber;
            QSizeF pageSize = pdfPageSizeCache.value(textBoxPage, QSizeF());
            if (pageSize.isEmpty()) continue;
            
            qreal yOffset = 0;
            if (isCombinedCanvas && textBoxPage == basePage + 1) {
                yOffset = singlePageHeight;
            }
            
            qreal bufferWidth = isCombinedCanvas ? buffer.width() : buffer.width();
            qreal bufferHeight = isCombinedCanvas ? singlePageHeight : buffer.height();
            qreal scaleX = bufferWidth / pageSize.width();
            qreal scaleY = bufferHeight / pageSize.height();
            
            // Convert PDF bounding box to buffer coordinates
            QRectF boxInBuffer(
                info.boundingBox.left() * scaleX,
                info.boundingBox.top() * scaleY + yOffset,
                info.boundingBox.width() * scaleX,
                info.boundingBox.height() * scaleY
            );
            
            qreal dist;
            
            // Check if point is inside the text box
            if (boxInBuffer.contains(point)) {
                // Point is inside - prioritize this with very small distance
                // Use horizontal distance within the box for sub-selection priority
                qreal horizontalPos = (point.x() - boxInBuffer.left()) / boxInBuffer.width();
                dist = horizontalPos * 0.1; // Very small distance, ordered by horizontal position
            } else {
                // Point is outside - calculate distance to nearest edge/corner of the box
                qreal dx = 0;
                qreal dy = 0;
                
                if (point.x() < boxInBuffer.left()) {
                    dx = boxInBuffer.left() - point.x();
                } else if (point.x() > boxInBuffer.right()) {
                    dx = point.x() - boxInBuffer.right();
                }
                
                if (point.y() < boxInBuffer.top()) {
                    dy = boxInBuffer.top() - point.y();
                } else if (point.y() > boxInBuffer.bottom()) {
                    dy = point.y() - boxInBuffer.bottom();
                }
                
                dist = std::sqrt(dx * dx + dy * dy);
            }
            
            if (dist < minDist) {
                minDist = dist;
                closestIndex = i;
            }
        }
        
        return closestIndex;
    };
    
    // Find the text box closest to start point
    int startIndex = findClosestTextBox(startBuffer);
    
    // Find the text box closest to end point
    int endIndex = findClosestTextBox(endBuffer);
    
    // Select all text boxes between start and end (inclusive) in reading order
    if (startIndex >= 0 && endIndex >= 0) {
        // Ensure start comes before end in reading order
        if (startIndex > endIndex) {
            std::swap(startIndex, endIndex);
        }
        
        // Add all text boxes in the range
        for (int i = startIndex; i <= endIndex; ++i) {
            selectedTextBoxes.append(textBoxInfoList[i].textBox);
        }
    }
    
    // Only emit signal and update if we have selected text
    if (!selectedTextBoxes.isEmpty()) {
        QString selectedText = getSelectedPdfText();
        if (!selectedText.isEmpty()) {
            emit pdfTextSelected(selectedText);
        }
    }
    
    // Trigger repaint to show selection
    update();
}

QList<Poppler::TextBox*> InkCanvas::getTextBoxesInSelection(const QPointF &start, const QPointF &end) {
    QList<Poppler::TextBox*> selectedBoxes;
    
    if (currentTextPageNumber < 0) {
        return selectedBoxes;
    }
    
    // For combined canvas, we need to handle coordinate spaces per-page
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (!isCombinedCanvas) {
        // Single page mode - use original logic
        QPointF pdfStart = mapWidgetToPdfCoordinates(start);
        QPointF pdfEnd = mapWidgetToPdfCoordinates(end);
        
        QRectF selectionRect(pdfStart, pdfEnd);
        selectionRect = selectionRect.normalized();
        
        // Check all text boxes
        for (Poppler::TextBox* textBox : currentPdfTextBoxes) {
            if (textBox) {
                QRectF textBoxRect = textBox->boundingBox();
                if (textBoxRect.intersects(selectionRect)) {
                    selectedBoxes.append(textBox);
                }
            }
        }
    } else {
        // Combined canvas mode - handle each page separately
        if (currentPdfTextBoxPageNumbers.isEmpty()) {
            return selectedBoxes;
        }
        
        int basePage = currentPdfTextBoxPageNumbers.first();
        
        // Determine which page(s) the selection spans
        bool selectsTopHalf = (start.y() < singlePageHeight || end.y() < singlePageHeight);
        bool selectsBottomHalf = (start.y() >= singlePageHeight || end.y() >= singlePageHeight);
        
        // Process each text box
        for (int i = 0; i < currentPdfTextBoxes.size(); ++i) {
            Poppler::TextBox* textBox = currentPdfTextBoxes[i];
            if (!textBox || i >= currentPdfTextBoxPageNumbers.size()) {
                continue;
            }
            
            int textBoxPage = currentPdfTextBoxPageNumbers[i];
            bool isTopPage = (textBoxPage == basePage);
            bool isBottomPage = (textBoxPage == basePage + 1);
            
            // Skip text boxes on pages we're not selecting
            if (isTopPage && !selectsTopHalf) {
                continue;
            }
            if (isBottomPage && !selectsBottomHalf) {
                continue;
            }
            
            // Create selection rectangle in widget space for this page's region
            QRectF widgetSelectionRect(start, end);
            widgetSelectionRect = widgetSelectionRect.normalized();
            
            // Clip selection to this page's region in widget space
            QRectF pageRegionWidget;
            if (isTopPage) {
                // Top half: Y from 0 to singlePageHeight
                pageRegionWidget = QRectF(0, 0, buffer.width(), singlePageHeight);
            } else {
                // Bottom half: Y from singlePageHeight to buffer.height()
                pageRegionWidget = QRectF(0, singlePageHeight, buffer.width(), singlePageHeight);
            }
            
            QRectF clippedWidgetRect = widgetSelectionRect.intersected(pageRegionWidget);
            
            if (clippedWidgetRect.isEmpty()) {
                continue; // Selection doesn't touch this page
            }
            
            // Map corners to PDF coordinates
            QPointF pdfTopLeft = mapWidgetToPdfCoordinates(clippedWidgetRect.topLeft());
            QPointF pdfBottomRight = mapWidgetToPdfCoordinates(clippedWidgetRect.bottomRight());
            
            QRectF selectionRect(pdfTopLeft, pdfBottomRight);
            selectionRect = selectionRect.normalized();
            
            // Check intersection
            QRectF textBoxRect = textBox->boundingBox();
            
            if (textBoxRect.intersects(selectionRect)) {
                selectedBoxes.append(textBox);
            }
        }
    }
    
    return selectedBoxes;
}

void InkCanvas::handlePdfLinkClick(const QPointF &position) {
    if (!isPdfLoaded || currentTextPageNumber < 0) {
        return;
    }

    // Convert widget coordinates to PDF coordinates
    QPointF pdfPoint = mapWidgetToPdfCoordinates(position);
    
    // Get PDF page size from cache
    if (!pdfPageSizeCache.contains(currentTextPageNumber)) {
        return;
    }
    QSizeF pdfPageSize = pdfPageSizeCache[currentTextPageNumber];
    
    // Convert to normalized coordinates (0.0 to 1.0) to match Poppler's link coordinate system
    QPointF normalizedPoint(pdfPoint.x() / pdfPageSize.width(), pdfPoint.y() / pdfPageSize.height());
    
    // ✅ Create temporary page to get links (will be destroyed at end of function)
    std::unique_ptr<Poppler::Page> tempPage(pdfDocument->page(currentTextPageNumber));
    if (!tempPage) {
        return;
    }
    
    // Get links for the current page
    auto links = tempPage->links();
    
    for (const auto& link : links) {
        QRectF linkArea = link->linkArea();
        
        // Normalize the rectangle to handle negative width/height
        QRectF normalizedLinkArea = linkArea.normalized();
        
        // Check if the normalized rectangle contains the normalized point
        if (normalizedLinkArea.contains(normalizedPoint)) {
            // Handle different types of links
            if (link->linkType() == Poppler::Link::Goto) {
                Poppler::LinkGoto* gotoLink = static_cast<Poppler::LinkGoto*>(link.get());
                if (gotoLink && gotoLink->destination().pageNumber() >= 0) {
                    int targetPage = gotoLink->destination().pageNumber() - 1; // Convert to 0-based
                    emit pdfLinkClicked(targetPage);
                    return;
                }
            }
            // Add other link types as needed (URI, etc.)
        }
    }
}

void InkCanvas::showPdfTextSelectionMenu(const QPoint &position) {
    QString selectedText = getSelectedPdfText();
    if (selectedText.isEmpty()) {
        return; // No text selected, don't show menu
    }
    
    // Create context menu
    QMenu *contextMenu = new QMenu(this);
    contextMenu->setAttribute(Qt::WA_DeleteOnClose);
    
    // Add Copy action
    QAction *copyAction = contextMenu->addAction(tr("Copy"));
    copyAction->setIcon(QIcon(":/resources/icons/copy.png"));
    connect(copyAction, &QAction::triggered, this, [selectedText]() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(selectedText);
    });
    
    // Add separator
    contextMenu->addSeparator();
    
    // Check if current selection is already highlighted
    bool alreadyHighlighted = isSelectionHighlighted();
    
    if (alreadyHighlighted) {
        // Already highlighted - show "Remove Highlight" and "Add Notes"
        QAction *removeHighlightAction = contextMenu->addAction(tr("Remove Highlight"));
        removeHighlightAction->setIcon(QIcon(":/resources/icons/cross.png"));
        connect(removeHighlightAction, &QAction::triggered, this, [this]() {
            removeHighlightAtSelection();
            clearPdfTextSelection();
        });
        
        // Add "Add Notes" option (links to existing highlight)
        QAction *addNotesAction = contextMenu->addAction(tr("Add Notes"));
        addNotesAction->setIcon(QIcon(":/resources/icons/edit.png"));
        connect(addNotesAction, &QAction::triggered, this, [this]() {
            QString noteId = addMarkdownNoteFromSelection();
            clearPdfTextSelection();
            if (!noteId.isEmpty()) {
                // Note was created - emit signal to update UI and auto-open sidebar
                emit markdownNotesUpdated();
                // qDebug() << "Markdown note linked to existing highlight:" << noteId;
            } else {
                // qDebug() << "Failed to create markdown note - no highlight found?";
            }
        });
    } else {
        // Not highlighted yet - show combined "Highlight and Add Notes"
        QAction *highlightAndNotesAction = contextMenu->addAction(tr("Highlight and Add Notes"));
        highlightAndNotesAction->setIcon(QIcon(":/resources/icons/marker.png"));
        connect(highlightAndNotesAction, &QAction::triggered, this, [this]() {
            QString noteId = addMarkdownNoteFromSelection();
            clearPdfTextSelection();
            if (!noteId.isEmpty()) {
                // Note was created - emit signal to update UI and auto-open sidebar
                emit markdownNotesUpdated();
                // qDebug() << "Highlight and note created:" << noteId;
            } else {
                // qDebug() << "Failed to create highlight and note - no selection?";
            }
        });
        
        // Also keep "Add Highlight" option for users who just want to highlight
        QAction *addHighlightAction = contextMenu->addAction(tr("Add Highlight"));
        addHighlightAction->setIcon(QIcon(":/resources/icons/marker.png"));
        connect(addHighlightAction, &QAction::triggered, this, [this]() {
            addHighlightFromSelection();
            clearPdfTextSelection();
        });
    }
    
    // Add separator
    contextMenu->addSeparator();
    
    // Add Cancel action
    QAction *cancelAction = contextMenu->addAction(tr("Cancel"));
    cancelAction->setIcon(QIcon(":/resources/icons/cross.png"));
    connect(cancelAction, &QAction::triggered, this, [this]() {
        clearPdfTextSelection();
    });
    
    // When menu is about to hide, clear selection if it still exists
    // (Actions that handle selection already call clearPdfTextSelection,
    // but if user clicks outside to dismiss, we need to clear it here)
    connect(contextMenu, &QMenu::aboutToHide, this, [this]() {
        // Use a short delay to allow action handlers to run first
        QTimer::singleShot(0, this, [this]() {
            // Only clear if there's still a selection (wasn't already cleared by an action)
            if (!selectedTextBoxes.isEmpty()) {
                clearPdfTextSelection();
            }
        });
    });
    
    // Show the menu at the specified position
    contextMenu->popup(position);
}

// Add a persistent highlight from the current text selection
void InkCanvas::addHighlightFromSelection() {
    if (selectedTextBoxes.isEmpty()) {
        return; // No selection to highlight
    }
    
    // Create storage for individual text box rectangles and combined bounding box
    QRectF combinedBoundingBox;
    QList<QRectF> individualRects;
    QString combinedText;
    int highlightPage = -1;
    
    for (int i = 0; i < selectedTextBoxes.size(); ++i) {
        const Poppler::TextBox* textBox = selectedTextBoxes[i];
        if (textBox) {
            // Get page number for this text box
            int pageNumber = -1;
            for (int j = 0; j < currentPdfTextBoxes.size(); ++j) {
                if (currentPdfTextBoxes[j] == textBox) {
                    pageNumber = (j < currentPdfTextBoxPageNumbers.size()) ? 
                                currentPdfTextBoxPageNumbers[j] : -1;
                    break;
                }
            }
            
            if (highlightPage == -1) {
                highlightPage = pageNumber;
            }
            
            // Store individual text box rectangle
            QRectF bbox = textBox->boundingBox();
            individualRects.append(bbox);
            
            // Also create combined bounding box for intersection checks
            if (combinedBoundingBox.isNull()) {
                combinedBoundingBox = bbox;
            } else {
                combinedBoundingBox = combinedBoundingBox.united(bbox);
            }
            
            // Combine text
            QString text = textBox->text();
            if (!text.isEmpty()) {
                if (!combinedText.isEmpty()) {
                    combinedText += " ";
                }
                combinedText += text;
            }
        }
    }
    
    if (highlightPage == -1 || combinedBoundingBox.isNull() || individualRects.isEmpty()) {
        return; // Invalid highlight
    }
    
    // Create the highlight
    TextHighlight highlight;
    highlight.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    highlight.pageNumber = highlightPage;
    highlight.boundingBox = combinedBoundingBox; // For intersection checks
    highlight.textBoxRects = individualRects; // For precise rendering
    highlight.text = combinedText;
    highlight.color = penColor; // Use current pen color
    highlight.markdownWindowId = ""; // No markdown window yet (for Step 3)
    
    // Add to persistent highlights
    persistentHighlights.append(highlight);
    
    // Save to metadata
    saveHighlightsToMetadata();
    
    // Mark as edited
    setEdited(true);
    
    // ✅ OPTIMIZATION: Only invalidate the specific page(s) affected by the highlight
    // Instead of clearPdfCache() which clears ALL pages, causing re-render on next page switch
    invalidatePdfPageCache(highlightPage);
    refreshCurrentPdfPage();
}

// Remove highlight(s) that overlap with the current selection
void InkCanvas::removeHighlightAtSelection() {
    if (selectedTextBoxes.isEmpty()) {
        return; // No selection
    }
    
    // Build the list of individual text box rects from the current selection
    QList<QRectF> selectionRects;
    int selectionPage = -1;
    
    for (int i = 0; i < selectedTextBoxes.size(); ++i) {
        const Poppler::TextBox* textBox = selectedTextBoxes[i];
        if (textBox) {
            // Get page number for this text box
            int pageNumber = -1;
            for (int j = 0; j < currentPdfTextBoxes.size(); ++j) {
                if (currentPdfTextBoxes[j] == textBox) {
                    pageNumber = (j < currentPdfTextBoxPageNumbers.size()) ? 
                                currentPdfTextBoxPageNumbers[j] : -1;
                    break;
                }
            }
            
            if (selectionPage == -1) {
                selectionPage = pageNumber;
            }
            
            // Store the individual text box rect
            selectionRects.append(textBox->boundingBox());
        }
    }
    
    if (selectionPage == -1 || selectionRects.isEmpty()) {
        return;
    }
    
    // Remove highlights whose actual text box rects intersect with the selection's rects
    bool removed = false;
    QStringList removedHighlightIds; // Track IDs for cascade deletion of notes
    
    for (int i = persistentHighlights.size() - 1; i >= 0; --i) {
        const TextHighlight &highlight = persistentHighlights[i];
        if (highlight.pageNumber == selectionPage && 
            doTextBoxRectsOverlap(highlight.textBoxRects, selectionRects)) {
            // Track this highlight's ID for note cleanup
            if (!highlight.markdownWindowId.isEmpty()) {
                removedHighlightIds.append(highlight.id);
            }
            persistentHighlights.removeAt(i);
            removed = true;
        }
    }
    
    // CASCADE DELETE: Remove notes linked to deleted highlights
    for (const QString &highlightId : removedHighlightIds) {
        for (int i = markdownNotes.size() - 1; i >= 0; --i) {
            if (markdownNotes[i].highlightId == highlightId) {
                // qDebug() << "Cascade deleting orphaned note:" << markdownNotes[i].id;
                markdownNotes.removeAt(i);
            }
        }
    }
    
    if (removed) {
        // ✅ OPTIMIZATION: Use deferred save - data is in memory
        saveNotebookMetadataDeferred();
        
        // Mark as edited
        setEdited(true);
        
        // ✅ OPTIMIZATION: Only invalidate the specific page(s) affected
        // Instead of clearPdfCache() which clears ALL pages
        invalidatePdfPageCache(selectionPage);
        refreshCurrentPdfPage();
        
        // Emit signal to update UI (note list needs to refresh)
        if (!removedHighlightIds.isEmpty()) {
            emit markdownNotesUpdated();
        }
    }
}

// Helper to check if any text box rects refer to the SAME text box (not just geometric overlap)
// This prevents false positives when different rows have geometrically overlapping bounding boxes
// due to text box heights being larger than line spacing.
bool InkCanvas::doTextBoxRectsOverlap(const QList<QRectF> &rects1, const QList<QRectF> &rects2) const {
    // Tolerance for floating point comparison (text boxes from the same source should match exactly or very closely)
    const qreal tolerance = 2.0;
    
    // Check if any rect from rects1 is approximately equal to any rect from rects2
    // (i.e., they represent the SAME text box, not just geometrically overlapping ones)
    for (const QRectF &rect1 : rects1) {
        for (const QRectF &rect2 : rects2) {
            // Check if rectangles are approximately the same (same text box)
            if (qAbs(rect1.x() - rect2.x()) < tolerance &&
                qAbs(rect1.y() - rect2.y()) < tolerance &&
                qAbs(rect1.width() - rect2.width()) < tolerance &&
                qAbs(rect1.height() - rect2.height()) < tolerance) {
                return true;
            }
        }
    }
    return false;
}

// Check if the current selection overlaps with any persistent highlight
bool InkCanvas::isSelectionHighlighted() const {
    if (selectedTextBoxes.isEmpty()) {
        return false;
    }
    
    // Build the list of individual text box rects from the current selection
    QList<QRectF> selectionRects;
    int selectionPage = -1;
    
    for (int i = 0; i < selectedTextBoxes.size(); ++i) {
        const Poppler::TextBox* textBox = selectedTextBoxes[i];
        if (textBox) {
            // Get page number for this text box
            int pageNumber = -1;
            for (int j = 0; j < currentPdfTextBoxes.size(); ++j) {
                if (currentPdfTextBoxes[j] == textBox) {
                    pageNumber = (j < currentPdfTextBoxPageNumbers.size()) ? 
                                currentPdfTextBoxPageNumbers[j] : -1;
                    break;
                }
            }
            
            if (selectionPage == -1) {
                selectionPage = pageNumber;
            }
            
            // Store the individual text box rect
            selectionRects.append(textBox->boundingBox());
        }
    }
    
    if (selectionPage == -1 || selectionRects.isEmpty()) {
        return false;
    }
    
    // Check if any highlight's actual text box rects match the selection's rects
    for (const TextHighlight &highlight : persistentHighlights) {
        if (highlight.pageNumber == selectionPage && 
            doTextBoxRectsOverlap(highlight.textBoxRects, selectionRects)) {
            return true;
        }
    }
    
    return false;
}

// Get all highlights for a specific page
QList<TextHighlight> InkCanvas::getHighlightsForPage(int pageNumber) const {
    QList<TextHighlight> pageHighlights;
    for (const TextHighlight &highlight : persistentHighlights) {
        if (highlight.pageNumber == pageNumber) {
            pageHighlights.append(highlight);
        }
    }
    return pageHighlights;
}

// Save highlights to metadata (standalone method for Step 3)
void InkCanvas::saveHighlightsToMetadata() {
    // ✅ OPTIMIZATION: Use deferred save - highlights are in memory
    saveNotebookMetadataDeferred();
}

// Load highlights from metadata (standalone method for Step 3)
void InkCanvas::loadHighlightsFromMetadata() {
    // Just call the main load function which now includes highlights
    loadNotebookMetadata();
}

// ============================================================================
// Markdown Notes Management
// ============================================================================

// Add a markdown note from current text selection
QString InkCanvas::addMarkdownNoteFromSelection() {
    if (selectedTextBoxes.isEmpty()) {
        return QString(); // No selection
    }
    
    // Check if selection is already highlighted
    TextHighlight *existingHighlight = nullptr;
    
    // Build the list of individual text box rects from the current selection
    QList<QRectF> selectionRects;
    int selectionPage = -1;
    
    for (int i = 0; i < selectedTextBoxes.size(); ++i) {
        const Poppler::TextBox* textBox = selectedTextBoxes[i];
        if (textBox) {
            // Get page number for this text box
            int pageNumber = -1;
            for (int j = 0; j < currentPdfTextBoxes.size(); ++j) {
                if (currentPdfTextBoxes[j] == textBox) {
                    pageNumber = (j < currentPdfTextBoxPageNumbers.size()) ? 
                                currentPdfTextBoxPageNumbers[j] : -1;
                    break;
                }
            }
            
            if (selectionPage == -1) {
                selectionPage = pageNumber;
            }
            
            // Store the individual text box rect
            selectionRects.append(textBox->boundingBox());
        }
    }
    
    // Find existing highlight whose actual text box rects intersect with the selection's rects
    if (selectionPage != -1 && !selectionRects.isEmpty()) {
        for (int i = 0; i < persistentHighlights.size(); ++i) {
            if (persistentHighlights[i].pageNumber == selectionPage && 
                doTextBoxRectsOverlap(persistentHighlights[i].textBoxRects, selectionRects)) {
                existingHighlight = &persistentHighlights[i];
                break;
            }
        }
    }
    
    // If no existing highlight, create a new one
    if (!existingHighlight) {
        addHighlightFromSelection();
        
        // Get the just-created highlight (it will be the last one added)
        if (persistentHighlights.isEmpty()) {
            return QString();
        }
        
        existingHighlight = &persistentHighlights.last();
    }
    
    // Create a new markdown note linked to the highlight (existing or new)
    MarkdownNoteData note;
    note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    note.highlightId = existingHighlight->id;
    note.pageNumber = existingHighlight->pageNumber;
    note.title = tr("Note for: ") + existingHighlight->text.left(30) + (existingHighlight->text.length() > 30 ? "..." : "");
    note.content = "";
    note.color = existingHighlight->color; // Use highlight's color, not current pen color
    
    // Link the highlight to this note
    existingHighlight->markdownWindowId = note.id;
    
    // Add the note
    markdownNotes.append(note);
    
    // ✅ OPTIMIZATION: Use deferred save - data is in memory, no need to block UI
    saveNotebookMetadataDeferred();
    setEdited(true);
    
    // Emit signal
    emit markdownNotesUpdated();
    
    return note.id;
}

// Add a markdown note directly
void InkCanvas::addMarkdownNote(const MarkdownNoteData &note) {
    // Check if note already exists
    for (int i = 0; i < markdownNotes.size(); ++i) {
        if (markdownNotes[i].id == note.id) {
            // Update existing note
            markdownNotes[i] = note;
            // ✅ OPTIMIZATION: Use deferred save
            saveNotebookMetadataDeferred();
            setEdited(true);
            emit markdownNotesUpdated();
            return;
        }
    }
    
    // Add new note
    markdownNotes.append(note);
    // ✅ OPTIMIZATION: Use deferred save
    saveNotebookMetadataDeferred();
    setEdited(true);
    emit markdownNotesUpdated();
}

// Update an existing markdown note
void InkCanvas::updateMarkdownNote(const MarkdownNoteData &note) {
    for (int i = 0; i < markdownNotes.size(); ++i) {
        if (markdownNotes[i].id == note.id) {
            markdownNotes[i] = note;
            // ✅ OPTIMIZATION: Use deferred save instead of immediate save
            // This prevents blocking the main thread on every keystroke while typing.
            // The data is safely updated in memory, and will be persisted:
            // - After 1 second of no changes (debounced)
            // - Immediately on page switch (flushPendingMetadataSave)
            // - Immediately on app close
            saveNotebookMetadataDeferred();
            setEdited(true);
            // DON'T emit markdownNotesUpdated() here - that causes a feedback loop
            // that reloads all notes and resets the editor on every keystroke!
            // The signal is only needed when notes are added/removed, not updated.
            return;
        }
    }
}

// Remove a markdown note
void InkCanvas::removeMarkdownNote(const QString &noteId) {
    for (int i = 0; i < markdownNotes.size(); ++i) {
        if (markdownNotes[i].id == noteId) {
            QString highlightId = markdownNotes[i].highlightId;
            markdownNotes.removeAt(i);
            
            // Unlink from highlight if linked
            if (!highlightId.isEmpty()) {
                TextHighlight *highlight = findHighlightById(highlightId);
                if (highlight) {
                    highlight->markdownWindowId.clear();
                }
            }
            
            // ✅ OPTIMIZATION: Use deferred save
            saveNotebookMetadataDeferred();
            setEdited(true);
            emit markdownNotesUpdated();
            return;
        }
    }
}

// Clear all highlights for a specific page
void InkCanvas::clearHighlightsForPage(int pageNumber) {
    // Collect IDs of highlights to remove (for cascade deletion of linked notes)
    QStringList removedHighlightIds;
    
    // Remove all highlights for this page
    for (int i = persistentHighlights.size() - 1; i >= 0; --i) {
        if (persistentHighlights[i].pageNumber == pageNumber) {
            removedHighlightIds.append(persistentHighlights[i].id);
            persistentHighlights.removeAt(i);
        }
    }
    
    // Cascade delete any markdown notes linked to removed highlights
    for (const QString &highlightId : removedHighlightIds) {
        for (int i = markdownNotes.size() - 1; i >= 0; --i) {
            if (markdownNotes[i].highlightId == highlightId) {
                markdownNotes.removeAt(i);
            }
        }
    }
    
    // Save changes and invalidate cache
    if (!removedHighlightIds.isEmpty()) {
        saveNotebookMetadataDeferred();
        setEdited(true);
        
        // ✅ CRITICAL: Invalidate PDF cache for this page so highlights re-render without the deleted ones
        // Highlights are baked into cached PDF images, so we must clear the cache
        invalidatePdfPageCache(pageNumber);
        
        // ✅ INSTANT UPDATE: If this page is currently displayed, force immediate re-render
        // Note: We do NOT call loadPdfPage() here because that calls loadPage() which would
        // reload the ink buffer from cache, potentially undoing a clearCurrentPage() operation.
        // Instead, we just re-render the PDF background directly.
        if (pdfDocument && (pageNumber == lastActivePage || pageNumber == currentCachedPage)) {
            // Re-render PDF page to cache (without highlights since they're now deleted)
            renderPdfPageToCache(pageNumber);
            
            // Update backgroundImage directly from the newly cached image
            {
                QMutexLocker locker(&pdfCacheMutex);
                if (pdfCache.contains(pageNumber)) {
                    backgroundImage = *pdfCache.object(pageNumber);
                }
            }
            
            // Trigger repaint to show updated background
            update();
        }
    }
}

// Clear all markdown notes for a specific page (including orphaned ones not linked to highlights)
void InkCanvas::clearMarkdownNotesForPage(int pageNumber) {
    bool anyRemoved = false;
    
    // Remove all markdown notes for this page
    for (int i = markdownNotes.size() - 1; i >= 0; --i) {
        if (markdownNotes[i].pageNumber == pageNumber) {
            // Unlink from highlight if linked
            QString highlightId = markdownNotes[i].highlightId;
            if (!highlightId.isEmpty()) {
                TextHighlight *highlight = findHighlightById(highlightId);
                if (highlight) {
                    highlight->markdownWindowId.clear();
                }
            }
            markdownNotes.removeAt(i);
            anyRemoved = true;
        }
    }
    
    // Save changes
    if (anyRemoved) {
        saveNotebookMetadataDeferred();
        setEdited(true);
    }
}

// Find a markdown note by ID
MarkdownNoteData* InkCanvas::findMarkdownNote(const QString &noteId) {
    for (int i = 0; i < markdownNotes.size(); ++i) {
        if (markdownNotes[i].id == noteId) {
            return &markdownNotes[i];
        }
    }
    return nullptr;
}

// Get markdown notes for specific page(s) - supports combined canvas
QList<MarkdownNoteData> InkCanvas::getMarkdownNotesForPages(int page1, int page2) const {
    QList<MarkdownNoteData> pageNotes;
    for (const MarkdownNoteData &note : markdownNotes) {
        if (note.pageNumber == page1 || (page2 >= 0 && note.pageNumber == page2)) {
            pageNotes.append(note);
        }
    }
    return pageNotes;
}

QList<MarkdownNoteData> InkCanvas::getAllMarkdownNotes() const {
    return markdownNotes;
}

// Link a highlight to a markdown note
void InkCanvas::linkHighlightToNote(const QString &highlightId, const QString &noteId) {
    TextHighlight *highlight = findHighlightById(highlightId);
    if (highlight) {
        highlight->markdownWindowId = noteId;
        // ✅ OPTIMIZATION: Use deferred save
        saveNotebookMetadataDeferred();
        setEdited(true);
    }
}

// Find a highlight by ID
TextHighlight* InkCanvas::findHighlightById(const QString &highlightId) {
    for (int i = 0; i < persistentHighlights.size(); ++i) {
        if (persistentHighlights[i].id == highlightId) {
            return &persistentHighlights[i];
        }
    }
    return nullptr;
}

// Handle double-click on a highlight
void InkCanvas::handleHighlightDoubleClick(const QString &highlightId) {
    emit highlightDoubleClicked(highlightId);
}

void InkCanvas::processPendingTextSelection() {
    if (!hasPendingSelection) {
        return;
    }

    // Process the pending selection update
    updatePdfTextSelection(pendingSelectionStart, pendingSelectionEnd);
    hasPendingSelection = false;
}

// Intelligent PDF Cache System Implementation

bool InkCanvas::isValidPageNumber(int pageNumber) const {
    return (pageNumber >= 0 && pageNumber < totalPdfPages);
}

// Helper method to draw highlights onto a PDF page image during rendering/caching
void InkCanvas::drawHighlightsOnPageImage(QImage &pageImage, int pageNumber, Poppler::Document* pdfDoc) {
    if (pageImage.isNull() || !pdfDoc || persistentHighlights.isEmpty()) {
        return;
    }
    
    // Get page size in PDF coordinates
    std::unique_ptr<Poppler::Page> pdfPage(pdfDoc->page(pageNumber));
    if (!pdfPage) {
        return;
    }
    QSizeF pdfPageSize = pdfPage->pageSizeF();
    
    // Find all highlights for this page
    QList<TextHighlight> pageHighlights;
    for (const TextHighlight &highlight : persistentHighlights) {
        if (highlight.pageNumber == pageNumber) {
            pageHighlights.append(highlight);
        }
    }
    
    if (pageHighlights.isEmpty()) {
        return; // No highlights for this page
    }
    
    // Create painter for the image
    QPainter painter(&pageImage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Calculate scale from PDF coordinates to image coordinates
    qreal scaleX = pageImage.width() / pdfPageSize.width();
    qreal scaleY = pageImage.height() / pdfPageSize.height();
    
    // Draw each highlight
    for (const TextHighlight &highlight : pageHighlights) {
        // Use the stored highlight color with semi-transparency
        QColor highlightColor = highlight.color;
        highlightColor.setAlpha(120); // Semi-transparent
        
        painter.setBrush(highlightColor);
        painter.setPen(Qt::NoPen);
        
        // Draw connected rectangles row by row
        if (!highlight.textBoxRects.isEmpty()) {
            // Group text boxes by row (using Y-coordinate tolerance)
            const qreal rowTolerance = 5.0; // PDF coordinate units
            
            QList<QList<QRectF>> rowGroups;
            for (const QRectF &rect : highlight.textBoxRects) {
                bool addedToRow = false;
                
                // Try to add to existing row
                for (QList<QRectF> &row : rowGroups) {
                    if (!row.isEmpty()) {
                        qreal avgY = (row.first().top() + row.first().bottom()) / 2.0;
                        qreal rectY = (rect.top() + rect.bottom()) / 2.0;
                        
                        if (qAbs(avgY - rectY) <= rowTolerance) {
                            row.append(rect);
                            addedToRow = true;
                            break;
                        }
                    }
                }
                
                // Create new row if not added
                if (!addedToRow) {
                    QList<QRectF> newRow;
                    newRow.append(rect);
                    rowGroups.append(newRow);
                }
            }
            
            // Draw continuous rectangle for each row
            for (const QList<QRectF> &row : rowGroups) {
                if (row.isEmpty()) continue;
                
                // Find leftmost and rightmost bounds
                qreal minX = row.first().left();
                qreal maxX = row.first().right();
                qreal minY = row.first().top();
                qreal maxY = row.first().bottom();
                
                for (const QRectF &rect : row) {
                    minX = qMin(minX, rect.left());
                    maxX = qMax(maxX, rect.right());
                    minY = qMin(minY, rect.top());
                    maxY = qMax(maxY, rect.bottom());
                }
                
                // Create continuous rectangle spanning the entire row
                QRectF pdfRowRect(minX, minY, maxX - minX, maxY - minY);
                QRectF imageRect(
                    pdfRowRect.x() * scaleX,
                    pdfRowRect.y() * scaleY,
                    pdfRowRect.width() * scaleX,
                    pdfRowRect.height() * scaleY
                );
                painter.drawRect(imageRect);
            }
        } else {
            // Legacy format: draw combined bounding box (for backward compatibility)
            QRectF pdfRect = highlight.boundingBox;
            QRectF imageRect(
                pdfRect.x() * scaleX,
                pdfRect.y() * scaleY,
                pdfRect.width() * scaleX,
                pdfRect.height() * scaleY
            );
            painter.drawRect(imageRect);
        }
    }
    
    painter.end();
}

// Refresh the currently displayed PDF page (for highlight updates)
// This re-renders and displays the current page without navigation
void InkCanvas::refreshCurrentPdfPage() {
    if (!pdfDocument || currentCachedPage < 0) {
        return;
    }
    
    // Re-render the current page (which is a combined canvas of current + next page)
    renderPdfPageToCache(currentCachedPage);
    
    // Update the backgroundImage with the newly rendered cached page
    {
        QMutexLocker locker(&pdfCacheMutex);
        if (pdfCache.contains(currentCachedPage)) {
            backgroundImage = *pdfCache.object(currentCachedPage);
        }
    }
    
    // Refresh display
    update();
    
    // ✅ OPTIMIZATION: Trigger immediate async caching of adjacent pages
    // When highlights are added/removed, invalidatePdfPageCache() clears pages N-1 and N.
    // refreshCurrentPdfPage() only re-renders page N (current).
    // Without this, page N-1 remains empty in cache, causing a freeze when user scrolls backward.
    // Use QTimer::singleShot(0) to run after current event processing completes (non-blocking).
    QTimer::singleShot(0, this, [this]() {
        cacheAdjacentPagesImmediately();
    });
}

void InkCanvas::renderPdfPageToCache(int pageNumber) {
    renderPdfPageToCacheThreadSafe(pageNumber, pdfDocument.get());
}

void InkCanvas::renderPdfPageToCacheThreadSafe(int pageNumber, Poppler::Document* sharedDocument) {
    if (!sharedDocument || !isValidPageNumber(pageNumber)) {
        return;
    }

    // Check if already cached and manage cache size (thread-safe)
    {
        QMutexLocker locker(&pdfCacheMutex);
        if (pdfCache.contains(pageNumber)) {
            // ✅ LRU: Mark as recently accessed
            pdfCacheAccessOrder.removeAll(pageNumber);
            pdfCacheAccessOrder.append(pageNumber);
            return;
        }
        
        // Ensure the cache holds only 6 pages max
        // ✅ BUG FIX: Only evict ONE page, not two!
        if (pdfCache.count() >= 6) {
            // ✅ LRU: Evict the LEAST recently used page (front of the list)
            if (!pdfCacheAccessOrder.isEmpty()) {
                int pageToEvict = pdfCacheAccessOrder.takeFirst();
                pdfCache.remove(pageToEvict);
            } else {
                // Fallback if access order is somehow empty
                auto keys = pdfCache.keys();
                if (!keys.isEmpty()) {
                    pdfCache.remove(keys.first());
                }
            }
        }
    }
    
    // Render current page using the shared document pointer
    std::unique_ptr<Poppler::Page> currentPage(sharedDocument->page(pageNumber));
    if (!currentPage) {
        return;
    }
    
    QImage currentPageImage = currentPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
    if (currentPageImage.isNull()) {
        return;
    }
    
    // Apply PDF inversion if enabled
    if (pdfInversionEnabled) {
        currentPageImage = invertPdfImage(currentPageImage);
    }
    
    // Draw highlights on the current page image
    drawHighlightsOnPageImage(currentPageImage, pageNumber, sharedDocument);
    
    // Try to render next page for combination
    QImage nextPageImage;
    int nextPageNumber = pageNumber + 1;
    if (isValidPageNumber(nextPageNumber)) {
        std::unique_ptr<Poppler::Page> nextPage(sharedDocument->page(nextPageNumber));
        if (nextPage) {
            nextPageImage = nextPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
            
            // Apply PDF inversion if enabled
            if (pdfInversionEnabled) {
                nextPageImage = invertPdfImage(nextPageImage);
            }
            
            // Draw highlights on the next page image
            drawHighlightsOnPageImage(nextPageImage, nextPageNumber, sharedDocument);
        }
    }
    
    // Create combined image
    QImage combinedImage;
    if (!nextPageImage.isNull()) {
        // Both pages available - create combined image
        int combinedHeight = currentPageImage.height() + nextPageImage.height();
        int combinedWidth = qMax(currentPageImage.width(), nextPageImage.width());
        
        combinedImage = QImage(combinedWidth, combinedHeight, QImage::Format_ARGB32);
        combinedImage.fill(Qt::white);
        
        QPainter painter(&combinedImage);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        // Draw current page at top
        painter.drawImage(0, 0, currentPageImage);
        
        // Draw next page below current page
        painter.drawImage(0, currentPageImage.height(), nextPageImage);
        
        painter.end();
    } else {
        // Only current page available (last page or next page failed to render)
        // Create double-height image with current page at top and white space below
        int combinedHeight = currentPageImage.height() * 2;
        int combinedWidth = currentPageImage.width();
        
        combinedImage = QImage(combinedWidth, combinedHeight, QImage::Format_ARGB32);
        combinedImage.fill(Qt::white);
        
        QPainter painter(&combinedImage);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        // Draw current page at top
        painter.drawImage(0, 0, currentPageImage);
        
        painter.end();
    }
    
    // Cache the combined image (thread-safe)
    if (!combinedImage.isNull()) {
        // Create QPixmap directly from QImage without intermediate copy
        {
            QMutexLocker locker(&pdfCacheMutex);
            pdfCache.insert(pageNumber, new QPixmap(QPixmap::fromImage(combinedImage)));
            // ✅ LRU: Add newly cached page to end of access order (most recent)
            pdfCacheAccessOrder.removeAll(pageNumber); // Remove if already present
            pdfCacheAccessOrder.append(pageNumber);
        }
        
        // ✅ Explicitly release large intermediate QImage objects to prevent accumulation
        // These can accumulate during rapid page traversal before garbage collection
        currentPageImage = QImage();
        nextPageImage = QImage();
        combinedImage = QImage();
    }
}

void InkCanvas::checkAndCacheAdjacentPages(int targetPage) {
    if (!pdfDocument || !isValidPageNumber(targetPage)) {
        return;
    }
    
    // Calculate adjacent pages - with pseudo smooth scrolling, we need 2 pages ahead
    int prevPage = targetPage - 1;
    int nextPage = targetPage + 1;
    int nextNextPage = targetPage + 2; // Added for pseudo smooth scrolling
    
    // Check what needs to be cached (thread-safe)
    bool needPrevPage, needCurrentPage, needNextPage, needNextNextPage;
    {
        QMutexLocker locker(&pdfCacheMutex);
        needPrevPage = isValidPageNumber(prevPage) && !pdfCache.contains(prevPage);
        needCurrentPage = !pdfCache.contains(targetPage);
        needNextPage = isValidPageNumber(nextPage) && !pdfCache.contains(nextPage);
        needNextNextPage = isValidPageNumber(nextNextPage) && !pdfCache.contains(nextNextPage);
    }
    
    // If all pages are cached, nothing to do
    if (!needPrevPage && !needCurrentPage && !needNextPage && !needNextNextPage) {
        return;
    }

    // Stop any existing timer
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // Create timer if it doesn't exist
    if (!pdfCacheTimer) {
        pdfCacheTimer = new QTimer(this);
        pdfCacheTimer->setSingleShot(true);
        connect(pdfCacheTimer, &QTimer::timeout, this, &InkCanvas::cacheAdjacentPages);
    }
    
    // Store the target page for validation when timer fires
    pendingCacheTargetPage = targetPage;
    
    // Start 1-second delay timer
    pdfCacheTimer->start(1000);
}

void InkCanvas::cacheAdjacentPages() {
    if (!pdfDocument || currentCachedPage < 0) {
            return;
        }
    
    // Check if the user has moved to a different page since the timer was started
    // If so, skip caching adjacent pages as they're no longer relevant
    if (pendingCacheTargetPage != currentCachedPage) {
        return; // User switched pages, don't cache adjacent pages for old page
    }
    
    int targetPage = currentCachedPage;
    int prevPage = targetPage - 1;
    int nextPage = targetPage + 1;
    int nextNextPage = targetPage + 2; // Added for pseudo smooth scrolling
    
    // Create list of pages to cache asynchronously
    QList<int> pagesToCache;
    
    // Add pages that need caching (thread-safe check)
    {
        QMutexLocker locker(&pdfCacheMutex);
        // Add previous page if needed
        if (isValidPageNumber(prevPage) && !pdfCache.contains(prevPage)) {
            pagesToCache.append(prevPage);
        }
        
        // Add next page if needed
        if (isValidPageNumber(nextPage) && !pdfCache.contains(nextPage)) {
            pagesToCache.append(nextPage);
        }
        
        // Add next-next page if needed (for pseudo smooth scrolling)
        if (isValidPageNumber(nextNextPage) && !pdfCache.contains(nextNextPage)) {
            pagesToCache.append(nextNextPage);
        }
    }
    
    // ✅ PERFORMANCE: Get PDF path once for all threads to avoid race conditions
    QString pdfFilePath = this->pdfPath;
    if (pdfFilePath.isEmpty()) {
        return; // No PDF path available
    }
    
    // ✅ MULTITHREADED OPTIMIZATION: Cache pages truly in parallel with independent document instances
    // Each thread loads its own Poppler::Document to avoid contention on shared resources
    for (int pageNum : pagesToCache) {
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
        
        // Track the watcher for cleanup
        activePdfWatchers.append(watcher);
        
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            // Remove from active list and delete
            activePdfWatchers.removeOne(watcher);
            watcher->deleteLater();
        });
        
        // ✅ Each thread gets its own document instance for true parallel rendering
        QFuture<void> future = QtConcurrent::run([this, pageNum, pdfFilePath]() {
            // Load a fresh document instance in this thread (thread-safe, no shared state)
            std::unique_ptr<Poppler::Document> threadDocument = Poppler::Document::load(pdfFilePath);
            if (!threadDocument || threadDocument->isLocked()) {
                return;
            }
            
            // Apply same render hints as main document for consistency
            threadDocument->setRenderHint(Poppler::Document::Antialiasing, true);
            threadDocument->setRenderHint(Poppler::Document::TextAntialiasing, true);
            threadDocument->setRenderHint(Poppler::Document::TextHinting, true);
            threadDocument->setRenderHint(Poppler::Document::TextSlightHinting, true);
            
            // Render using the thread-local document instance
            renderPdfPageToCacheThreadSafe(pageNum, threadDocument.get());
        });
        
        watcher->setFuture(future);
    }
}

// ✅ OPTIMIZATION: Immediately cache adjacent pages after highlight changes
// Unlike cacheAdjacentPages() which has a 1-second delay, this runs immediately
// to ensure pages are cached before user can scroll to them.
void InkCanvas::cacheAdjacentPagesImmediately() {
    if (!pdfDocument || currentCachedPage < 0) {
        return;
    }
    
    int targetPage = currentCachedPage;
    int prevPage = targetPage - 1;
    int nextPage = targetPage + 1;
    
    // Create list of pages to cache asynchronously
    QList<int> pagesToCache;
    
    // Add pages that need caching (thread-safe check)
    {
        QMutexLocker locker(&pdfCacheMutex);
        // Priority: previous page (for backward scroll after highlight creation)
        if (isValidPageNumber(prevPage) && !pdfCache.contains(prevPage)) {
            pagesToCache.append(prevPage);
        }
        
        // Also cache next page for forward scroll
        if (isValidPageNumber(nextPage) && !pdfCache.contains(nextPage)) {
            pagesToCache.append(nextPage);
        }
    }
    
    if (pagesToCache.isEmpty()) {
        return; // All adjacent pages already cached
    }
    
    // Get PDF path for async rendering
    QString pdfFilePath = this->pdfPath;
    if (pdfFilePath.isEmpty()) {
        return;
    }
    
    // Cache pages asynchronously using QtConcurrent
    for (int pageNum : pagesToCache) {
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
        
        // Track the watcher for cleanup
        activePdfWatchers.append(watcher);
        
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            activePdfWatchers.removeOne(watcher);
            watcher->deleteLater();
        });
        
        // Each thread gets its own document instance for true parallel rendering
        QFuture<void> future = QtConcurrent::run([this, pageNum, pdfFilePath]() {
            std::unique_ptr<Poppler::Document> threadDocument = Poppler::Document::load(pdfFilePath);
            if (!threadDocument || threadDocument->isLocked()) {
                return;
            }
            
            // Apply same render hints as main document
            threadDocument->setRenderHint(Poppler::Document::Antialiasing, true);
            threadDocument->setRenderHint(Poppler::Document::TextAntialiasing, true);
            threadDocument->setRenderHint(Poppler::Document::TextHinting, true);
            threadDocument->setRenderHint(Poppler::Document::TextSlightHinting, true);
            
            // Render using the thread-local document instance
            renderPdfPageToCacheThreadSafe(pageNum, threadDocument.get());
        });
        
        watcher->setFuture(future);
    }
}

// Intelligent Note Cache System Implementation

QString InkCanvas::getNotePageFilePath(int pageNumber) const {
    if (saveFolder.isEmpty() || notebookId.isEmpty()) {
        return QString();
    }
    return saveFolder + QString("/%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
}

void InkCanvas::loadSingleNotePageToCache(int pageNumber) {
    // Check if already cached (thread-safe)
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.contains(pageNumber)) {
            // ✅ LRU: Mark as recently accessed
            noteCacheAccessOrder.removeAll(pageNumber);
            noteCacheAccessOrder.append(pageNumber);
            return;
        }
    }
    
    QString filePath = getNotePageFilePath(pageNumber);
    if (filePath.isEmpty()) {
        return;
    }

    // Only cache if the file actually exists
    if (!QFile::exists(filePath)) {
        return;
    }
    
    // Ensure the cache doesn't exceed its limit (thread-safe)
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.count() >= 6) {
            // ✅ LRU: Evict the LEAST recently used page (front of the list)
            if (!noteCacheAccessOrder.isEmpty()) {
                int pageToEvict = noteCacheAccessOrder.takeFirst();
                noteCache.remove(pageToEvict);
            } else {
                // Fallback if access order is somehow empty
            auto keys = noteCache.keys();
            if (!keys.isEmpty()) {
                noteCache.remove(keys.first());
            }
        }
        }
    }
    
    // Load single page from disk
    QPixmap singlePageCanvas;
    if (singlePageCanvas.load(filePath)) {
        // Cache the single page (thread-safe)
            QMutexLocker locker(&noteCacheMutex);
        noteCache.insert(pageNumber, new QPixmap(singlePageCanvas));
        // ✅ LRU: Add newly cached page to end of access order (most recent)
        noteCacheAccessOrder.removeAll(pageNumber); // Remove if already present
        noteCacheAccessOrder.append(pageNumber);
        }
}

void InkCanvas::checkAndCacheAdjacentNotePages(int targetPage) {
    if (saveFolder.isEmpty()) {
        return;
    }
    
    // Calculate adjacent pages - with pseudo smooth scrolling, we need 2 pages ahead
    int prevPage = targetPage - 1;
    int nextPage = targetPage + 1;
    int nextNextPage = targetPage + 2; // Added for pseudo smooth scrolling
    
    // Check what needs to be cached (we don't have a max page limit for notes) - thread-safe
    bool needPrevPage, needCurrentPage, needNextPage, needNextNextPage;
    {
        QMutexLocker locker(&noteCacheMutex);
        needPrevPage = (prevPage >= 0) && !noteCache.contains(prevPage);
        needCurrentPage = !noteCache.contains(targetPage);
        needNextPage = !noteCache.contains(nextPage); // No upper limit check for notes
        needNextNextPage = !noteCache.contains(nextNextPage); // No upper limit check for notes
    }
    
    // If all nearby pages are cached, nothing to do
    if (!needPrevPage && !needCurrentPage && !needNextPage && !needNextNextPage) {
        return;
    }
    
    // Stop any existing timer
    if (noteCacheTimer && noteCacheTimer->isActive()) {
        noteCacheTimer->stop();
    }
    
    // Create timer if it doesn't exist
    if (!noteCacheTimer) {
        noteCacheTimer = new QTimer(this);
        noteCacheTimer->setSingleShot(true);
        connect(noteCacheTimer, &QTimer::timeout, this, &InkCanvas::cacheAdjacentNotePages);
    }
    
    // Store the target page for validation when timer fires
    pendingNoteCacheTargetPage = targetPage;
    
    // Start 1-second delay timer
    noteCacheTimer->start(1000);
}

void InkCanvas::cacheAdjacentNotePages() {
    if (saveFolder.isEmpty() || currentCachedNotePage < 0) {
        return;
    }
    
    // Check if the user has moved to a different page since the timer was started
    // If so, skip caching adjacent pages as they're no longer relevant
    if (pendingNoteCacheTargetPage != currentCachedNotePage) {
        return; // User switched pages, don't cache adjacent pages for old page
    }
    
    int targetPage = currentCachedNotePage;
    int prevPage = targetPage - 1;
    int nextPage = targetPage + 1;
    int nextNextPage = targetPage + 2; // Added for pseudo smooth scrolling
    
    // Create list of note pages to cache asynchronously
    QList<int> notePagesToCache;
    
    // Add pages that need caching (thread-safe check)
    {
        QMutexLocker locker(&noteCacheMutex);
        // Add previous page if needed (check for >= 0 since notes can start from page 0)
        if (prevPage >= 0 && !noteCache.contains(prevPage)) {
            notePagesToCache.append(prevPage);
        }
        
        // Add next page if needed (no upper limit check for notes)
        if (!noteCache.contains(nextPage)) {
            notePagesToCache.append(nextPage);
        }
        
        // Add next-next page if needed (for pseudo smooth scrolling)
        if (!noteCache.contains(nextNextPage)) {
            notePagesToCache.append(nextNextPage);
        }
    }
    
    // Cache note pages asynchronously
    for (int pageNum : notePagesToCache) {
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
        
        // Track the watcher for cleanup
        activeNoteWatchers.append(watcher);
        
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            // Remove from active list and delete
            activeNoteWatchers.removeOne(watcher);
            watcher->deleteLater();
        });
        
        // Capture pageNum by value to avoid issues with lambda capture
        QFuture<void> future = QtConcurrent::run([this, pageNum]() {
            loadSingleNotePageToCache(pageNum);
        });
        
        watcher->setFuture(future);
    }
}

void InkCanvas::invalidateBothPagesCache(int pageNumber) {
    // Invalidate both pages of a combined canvas to ensure cache doesn't have unsplit combined buffers
        QMutexLocker locker(&noteCacheMutex);
    
    // ✅ FIX: Also invalidate PREVIOUS combined page (pageNumber - 1)
    // because it displays the current page on its BOTTOM HALF
    // Example: If editing page 100 on combined (100,101):
    //   - Page (99,100) shows page 100 on bottom half → needs invalidation
    //   - Page (100,101) shows page 100 on top half → needs invalidation
    //   - Page (101,102) shows page 101 on bottom half → needs invalidation
    if (pageNumber > 0) {
        noteCache.remove(pageNumber - 1);
        noteCacheAccessOrder.removeAll(pageNumber - 1);
    }
    
    noteCache.remove(pageNumber);
    noteCacheAccessOrder.removeAll(pageNumber);
    
    noteCache.remove(pageNumber + 1);
    noteCacheAccessOrder.removeAll(pageNumber + 1);
}

void InkCanvas::insertPageIntoCache(int pageNumber, const QPixmap &pixmap) {
    // ✅ OPTIMIZATION: Insert a page directly into cache for smooth page switching
    // This avoids the save-invalidate-reload cycle by keeping fresh data in memory
    QMutexLocker locker(&noteCacheMutex);
    
    // ✅ MEMORY SAFETY: First remove this page from access order to prevent self-eviction
    // This also ensures the access order stays in sync with cache state
    noteCacheAccessOrder.removeAll(pageNumber);
    
    // Ensure cache doesn't exceed limit (evict LRU if needed)
    if (noteCache.count() >= 6 && !noteCache.contains(pageNumber)) {
        // LRU eviction: remove the least recently used page
        bool evicted = false;
        while (!noteCacheAccessOrder.isEmpty() && !evicted) {
            int pageToEvict = noteCacheAccessOrder.takeFirst();
            // ✅ SAFETY: Only evict if the page is actually in cache
            // (handles edge case where access order is out of sync)
            if (noteCache.contains(pageToEvict)) {
                noteCache.remove(pageToEvict);
                evicted = true;
            }
        }
        
        // Fallback if access order was empty or out of sync
        if (!evicted && noteCache.count() >= 6) {
            auto keys = noteCache.keys();
            if (!keys.isEmpty()) {
                noteCache.remove(keys.first());
            }
        }
    }
    
    // Insert or update the page in cache
    // QCache takes ownership of the pointer and deletes old value if key exists
    noteCache.insert(pageNumber, new QPixmap(pixmap));
    
    // Add to end of access order (most recently used)
    noteCacheAccessOrder.append(pageNumber);
}

QList<PictureWindow*> InkCanvas::loadPictureWindowsForPage(int pageNumber) {
    if (!pictureManager) {
        return QList<PictureWindow*>();
    }
    
    // Load windows for the specified page without affecting current windows
    return pictureManager->loadWindowsForPageSeparately(pageNumber);
}

// Load picture windows for combined canvas (markdown windows removed)
void InkCanvas::loadCombinedWindowsForPage(int pageNumber) {
    if (!pictureManager) {
        return;
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (isCombinedCanvas) {
        // For combined canvas, we need to load and merge windows from both current and next page
        int nextPageNumber = pageNumber + 1;
        
        // Load page N windows for top half
        QList<PictureWindow*> topHalfPictureWindows = loadPictureWindowsForPage(pageNumber);
        
        // Load page N+1 windows for bottom half and adjust their coordinates
        QList<PictureWindow*> bottomHalfPictureWindows = loadPictureWindowsForPage(nextPageNumber);
        
        // Move page N+1 windows to bottom half by adding singlePageHeight
        for (PictureWindow* window : bottomHalfPictureWindows) {
            QRect rect = window->getCanvasRect();
            rect.moveTop(rect.y() + singlePageHeight);
            window->setCanvasRect(rect);
        }
        
        // Combine both sets of windows and set as current
        QList<PictureWindow*> combinedPictureWindows = topHalfPictureWindows + bottomHalfPictureWindows;
        pictureManager->setCombinedWindows(combinedPictureWindows);
    } else {
        // Standard single page window loading
        pictureManager->loadWindowsForPage(pageNumber);
    }
}

// Save picture windows for combined canvas (markdown windows removed)
void InkCanvas::saveCombinedWindowsForPage(int pageNumber) {
    if (!pictureManager) {
        return;
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (isCombinedCanvas) {
        // For combined canvas, we need to save windows based on their positions
        // Windows in the top half belong to current page, bottom half to next page
        
        // Get all current windows
        QList<PictureWindow*> allPictureWindows = pictureManager->getCurrentPageWindows();
        
        // Separate windows by position
        QList<PictureWindow*> currentPagePictureWindows;
        QList<PictureWindow*> nextPagePictureWindows;
        
        // Separate picture windows by position  
        for (PictureWindow* window : allPictureWindows) {
            QRect rect = window->getCanvasRect();
            if (rect.top() < singlePageHeight) {
                // Window starts in top half (current page)
                currentPagePictureWindows.append(window);
            } else {
                // Window starts in bottom half (next page)
                nextPagePictureWindows.append(window);
            }
        }
        
        // Save current page windows (top half)
        // ✅ ALWAYS save even if empty - clears stale cache entries
        pictureManager->saveWindowsForPageSeparately(pageNumber, currentPagePictureWindows);
        
        // Save next page windows (bottom half, with adjusted coordinates)
        // ✅ CROSS-PAGE MOVE FIX: ALWAYS save to both pages, even if list is empty
        // When a window moves from bottom→top, nextPagePictureWindows becomes empty
        // We MUST still call saveWindowsForPageSeparately to clear the stale cache entry
        // Otherwise the old clone remains and causes duplication
        if (!nextPagePictureWindows.isEmpty()) {
            // Temporarily adjust coordinates for saving
            for (PictureWindow* window : nextPagePictureWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() - singlePageHeight); // Move to top half coordinates
                window->setCanvasRect(rect);
            }
        }
        // Save page N+1 (even if empty - to clear stale cache)
        pictureManager->saveWindowsForPageSeparately(pageNumber + 1, nextPagePictureWindows);
        // Restore original coordinates if we adjusted them
        if (!nextPagePictureWindows.isEmpty()) {
            for (PictureWindow* window : nextPagePictureWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() + singlePageHeight); // Move back to bottom half
                window->setCanvasRect(rect);
            }
        }
    } else {
        // Standard single page window saving
        pictureManager->saveWindowsForPage(pageNumber);
    }
}

void InkCanvas::setPictureSelectionMode(bool enabled) {
    // qDebug() << "InkCanvas::setPictureSelectionMode() called with enabled:" << enabled;
    
    pictureSelectionMode = enabled;
    
    if (pictureManager) {
        // qDebug() << "  Setting picture manager selection mode to:" << enabled;
        pictureManager->setSelectionMode(enabled);
    } else {
        // qDebug() << "  WARNING: Picture manager is null!";
    }
    
    if (!enabled) {
        pictureSelecting = false;
    }
    
    // Update cursor
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    // qDebug() << "  Cursor updated to:" << (enabled ? "CrossCursor" : "ArrowCursor");
}

bool InkCanvas::isPictureSelectionMode() const {
    return pictureSelectionMode;
}

void InkCanvas::setPictureWindowEditMode(bool enabled) {
    pictureWindowEditMode = enabled;
    //qDebug() << "InkCanvas::setPictureWindowEditMode() called with enabled:" << enabled;
    
    // When a picture window enters edit mode, disable pan for touch/stylus interactions
    // This allows the picture window to handle touch events without interference
}

// ✅ Combined canvas helpers for picture window page determination
bool InkCanvas::isCombinedCanvasMode() const {
    // Combined mode if buffer is roughly double a single page height
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        return true;
    }
    // Fallback heuristic for tall buffers without PDF
    if (buffer.height() > 1400) {
        return true;
    }
    return false;
}

int InkCanvas::getSinglePageHeight() const {
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        return backgroundImage.height() / 2;
    }
    if (buffer.height() > 1400) {
        return buffer.height() / 2;
    }
    // Not in combined mode - return full buffer height
    return buffer.height();
}

int InkCanvas::getPageNumberForCanvasY(int canvasY) const {
    int primaryPage = lastActivePage;
    
    if (!isCombinedCanvasMode()) {
        return primaryPage;  // Single page mode - always the current page
    }
    
    int singlePageHeight = getSinglePageHeight();
    
    // If Y is in bottom half, it belongs to the next page
    if (canvasY >= singlePageHeight) {
        return primaryPage + 1;
    }
    
    return primaryPage;
}

// Canvas coordinate conversion methods
QPointF InkCanvas::mapWidgetToCanvas(const QPointF &widgetPoint) const {
    // Use the same coordinate transformation logic as mapLogicalWidgetToPhysicalBuffer
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert widget position to canvas position
    QPointF adjustedPoint = widgetPoint - QPointF(centerOffsetX, centerOffsetY);
    QPointF canvasPoint = (adjustedPoint / (zoomFactor / 100.0)) + QPointF(panOffsetX, panOffsetY);
    
    return canvasPoint;
}

QPointF InkCanvas::mapCanvasToWidget(const QPointF &canvasPoint) const {
    // Reverse the transformation from mapWidgetToCanvas
    qreal scaledCanvasWidth = buffer.width() * (zoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (zoomFactor / 100.0);
    qreal centerOffsetX = (scaledCanvasWidth < width()) ? (width() - scaledCanvasWidth) / 2.0 : 0;
    qreal centerOffsetY = (scaledCanvasHeight < height()) ? (height() - scaledCanvasHeight) / 2.0 : 0;
    
    // Convert canvas position to widget position
    QPointF widgetPoint = (canvasPoint - QPointF(panOffsetX, panOffsetY)) * (zoomFactor / 100.0) + QPointF(centerOffsetX, centerOffsetY);
    
    return widgetPoint;
}

QRect InkCanvas::mapWidgetToCanvas(const QRect &widgetRect) const {
    QPointF topLeft = mapWidgetToCanvas(widgetRect.topLeft());
    QPointF bottomRight = mapWidgetToCanvas(widgetRect.bottomRight());
    
    return QRect(topLeft.toPoint(), bottomRight.toPoint());
}

QRect InkCanvas::mapCanvasToWidget(const QRect &canvasRect) const {
    QPointF topLeft = mapCanvasToWidget(canvasRect.topLeft());
    QPointF bottomRight = mapCanvasToWidget(canvasRect.bottomRight());
    
    return QRect(topLeft.toPoint(), bottomRight.toPoint());
}

// ✅ New unified JSON metadata system
void InkCanvas::loadNotebookMetadata() {
    if (saveFolder.isEmpty()) return;
    
    QString metadataFile = saveFolder + "/.speedynote_metadata.json";
    
    // First, try to migrate from old files if JSON doesn't exist
    if (!QFile::exists(metadataFile)) {
        // Check if old metadata files exist
        if (QFile::exists(saveFolder + "/.notebook_id.txt") || 
            QFile::exists(saveFolder + "/.pdf_path.txt") || 
            QFile::exists(saveFolder + "/.background_config.txt") || 
            QFile::exists(saveFolder + "/.bookmarks.txt")) {
            // qDebug() << "Detected old metadata files, migrating to JSON format...";
            migrateOldMetadataFiles();
            return; // migrateOldMetadataFiles calls saveNotebookMetadata which creates the JSON
        } else {
            // No old files, this is a new notebook - generate new ID
            if (notebookId.isEmpty()) {
                notebookId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "");
                // qDebug() << "New notebook created with ID:" << notebookId;
            }
            return;
        }
    }
    
    QFile file(metadataFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse metadata JSON:" << error.errorString();
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // Load all metadata
    notebookId = obj["notebook_id"].toString();
    pdfPath = obj["pdf_path"].toString();
    lastAccessedPage = obj["last_accessed_page"].toInt(0);
    
    // Background settings
    QString bgStyleStr = obj["background_style"].toString("None");
    if (bgStyleStr == "Grid") backgroundStyle = BackgroundStyle::Grid;
    else if (bgStyleStr == "Lines") backgroundStyle = BackgroundStyle::Lines;
    else backgroundStyle = BackgroundStyle::None;
    
    backgroundColor = QColor(obj["background_color"].toString("#ffffff"));
    backgroundDensity = obj["background_density"].toInt(20);
    
    // PDF inversion setting (default to false for existing notebooks)
    pdfInversionEnabled = obj["pdf_inversion_enabled"].toBool(false);
    
    // Load bookmarks
    bookmarks.clear();
    QJsonArray bookmarkArray = obj["bookmarks"].toArray();
    for (const QJsonValue &value : bookmarkArray) {
        bookmarks.append(value.toString());
    }
    
    // Load persistent text highlights (backward compatible - defaults to empty array)
    persistentHighlights.clear();
    QJsonArray highlightsArray = obj["text_highlights"].toArray();
    for (const QJsonValue &value : highlightsArray) {
        if (value.isObject()) {
            TextHighlight highlight = TextHighlight::fromJson(value.toObject());
            persistentHighlights.append(highlight);
        }
    }
    
    // Load markdown notes (backward compatible - defaults to empty array)
    markdownNotes.clear();
    QJsonArray notesArray = obj["markdown_notes"].toArray();
    for (const QJsonValue &value : notesArray) {
        if (value.isObject()) {
            MarkdownNoteData note = MarkdownNoteData::fromJson(value.toObject());
            markdownNotes.append(note);
        }
    }
    
    // Load image references (for picture window file management)
    imageReferences.clear();
    if (obj.contains("image_references")) {
        QJsonObject imgRefObj = obj["image_references"].toObject();
        for (auto it = imgRefObj.begin(); it != imgRefObj.end(); ++it) {
            imageReferences[it.key()] = it.value().toInt();
        }
    } else {
        // No image_references field - this is an old notebook, rebuild from scratch
        rebuildImageReferences();
    }
    
    // If we have a PDF path, try to load it (missing PDF will be handled later)
    if (!pdfPath.isEmpty()) {
        if (QFile::exists(pdfPath)) {
            loadPdf(pdfPath);
        }
        // Note: Missing PDF handling is done in handleMissingPdf() when the notebook is opened
    }
}

void InkCanvas::saveNotebookMetadata() {
    if (saveFolder.isEmpty()) return;
    
    QJsonObject obj;
    
    // Save all metadata
    obj["notebook_id"] = notebookId;
    obj["pdf_path"] = pdfPath;
    obj["last_accessed_page"] = lastAccessedPage;
    obj["version"] = "1.0";
    obj["last_modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Background settings
    QString bgStyleStr = "None";
    if (backgroundStyle == BackgroundStyle::Grid) bgStyleStr = "Grid";
    else if (backgroundStyle == BackgroundStyle::Lines) bgStyleStr = "Lines";
    
    obj["background_style"] = bgStyleStr;
    obj["background_color"] = backgroundColor.name();
    obj["background_density"] = backgroundDensity;
    
    // PDF inversion setting
    obj["pdf_inversion_enabled"] = pdfInversionEnabled;
    
    // Save bookmarks
    QJsonArray bookmarkArray;
    for (const QString &bookmark : bookmarks) {
        bookmarkArray.append(bookmark);
    }
    obj["bookmarks"] = bookmarkArray;
    
    // Save persistent text highlights
    QJsonArray highlightsArray;
    for (const TextHighlight &highlight : persistentHighlights) {
        highlightsArray.append(highlight.toJson());
    }
    obj["text_highlights"] = highlightsArray;
    
    // Save markdown notes
    QJsonArray notesArray;
    for (const MarkdownNoteData &note : markdownNotes) {
        notesArray.append(note.toJson());
    }
    obj["markdown_notes"] = notesArray;
    
    // Save image references (for picture window file management)
    QJsonObject imgRefObj;
    for (auto it = imageReferences.begin(); it != imageReferences.end(); ++it) {
        if (it.value() > 0) { // Only save non-zero references
            imgRefObj[it.key()] = it.value();
        }
    }
    obj["image_references"] = imgRefObj;
    
    QJsonDocument doc(obj);
    
    QString metadataFile = saveFolder + "/.speedynote_metadata.json";
    QFile file(metadataFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson());
        file.close();
    }
    
    // ✅ OPTIMIZATION: Defer .spn package sync instead of immediate sync
    // Re-packing the entire directory is expensive (reads ALL files)
    // Use deferred sync to batch multiple saves together
    syncSpnPackageDeferred();
}

void InkCanvas::saveNotebookMetadataDeferred() {
    // ✅ OPTIMIZATION: Debounce rapid metadata saves during page switching
    // Instead of saving immediately, schedule a save after a short delay
    // If another save is requested before the timer fires, restart the timer
    if (!metadataSaveTimer) return;
    
    metadataSavePending = true;
    metadataSaveTimer->start(); // Restart timer (resets the 1 second delay)
}

void InkCanvas::flushPendingMetadataSave() {
    // Force immediate save if there's a pending deferred save
    if (metadataSavePending && metadataSaveTimer) {
        metadataSaveTimer->stop();
        saveNotebookMetadata();
        metadataSavePending = false;
    }
}

void InkCanvas::onMetadataSaveTimeout() {
    // Timer fired - perform the actual metadata save
    if (metadataSavePending) {
        saveNotebookMetadata();
        metadataSavePending = false;
    }
}

void InkCanvas::setLastAccessedPage(int pageNumber) {
    lastAccessedPage = pageNumber;
    // ✅ OPTIMIZATION: Use deferred save to avoid blocking on every page flip
    // The metadata will be saved after 1 second of no page changes
    saveNotebookMetadataDeferred();
}

int InkCanvas::getLastAccessedPage() const {
    return lastAccessedPage;
}

QString InkCanvas::getPdfPath() const {
    return pdfPath;
}

QString InkCanvas::getNotebookId() const {
    return notebookId;
}

// ✅ Image reference counting for picture windows
void InkCanvas::incrementImageReference(const QString &imageHash) {
    if (imageHash.isEmpty()) return;
    imageReferences[imageHash] = imageReferences.value(imageHash, 0) + 1;
    saveNotebookMetadataDeferred();
}

bool InkCanvas::decrementImageReference(const QString &imageHash) {
    if (imageHash.isEmpty()) return false;
    
    int currentCount = imageReferences.value(imageHash, 0);
    if (currentCount <= 0) {
        // No references to decrement - file should be deleted (orphaned)
        imageReferences.remove(imageHash);
        saveNotebookMetadataDeferred();
        return true;
    }
    
    currentCount--;
    if (currentCount <= 0) {
        // Last reference removed - file should be deleted
        imageReferences.remove(imageHash);
        saveNotebookMetadataDeferred();
        return true;
    } else {
        // Still has references - file should NOT be deleted
        imageReferences[imageHash] = currentCount;
        saveNotebookMetadataDeferred();
        return false;
    }
}

int InkCanvas::getImageReferenceCount(const QString &imageHash) const {
    return imageReferences.value(imageHash, 0);
}

void InkCanvas::rebuildImageReferences() {
    // Rebuild reference counts by scanning all picture metadata files
    // This is called when a notebook is opened that doesn't have image_references field
    if (saveFolder.isEmpty() || notebookId.isEmpty()) return;
    
    imageReferences.clear();
    
    // Scan all picture metadata files
    QDir dir(saveFolder);
    QString pattern = QString(".%1_pictures_*.json").arg(notebookId);
    QStringList filters;
    filters << pattern;
    
    // Get all matching files (need to list hidden files)
    QStringList metadataFiles;
    QFileInfoList allFiles = dir.entryInfoList(QDir::Files | QDir::Hidden);
    for (const QFileInfo &fileInfo : allFiles) {
        QString fileName = fileInfo.fileName();
        if (fileName.startsWith(QString(".%1_pictures_").arg(notebookId)) && fileName.endsWith(".json")) {
            metadataFiles.append(fileInfo.absoluteFilePath());
        }
    }
    
    // Parse each file and count image references
    for (const QString &filePath : metadataFiles) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) continue;
        
        QJsonArray windowsArray = doc.array();
        for (const QJsonValue &value : windowsArray) {
            if (!value.isObject()) continue;
            
            QJsonObject windowObj = value.toObject();
            QString imagePath = windowObj["image_path"].toString();
            
            // Extract hash from filename (format: {notebookId}_img_p{page}_{hash}.{ext})
            QString fileName = QFileInfo(imagePath).fileName();
            // Extract the hash portion (8 characters before the extension)
            int lastDot = fileName.lastIndexOf('.');
            int lastUnderscore = fileName.lastIndexOf('_');
            if (lastDot > 0 && lastUnderscore > 0 && lastUnderscore < lastDot) {
                QString hash = fileName.mid(lastUnderscore + 1, lastDot - lastUnderscore - 1);
                if (!hash.isEmpty()) {
                    imageReferences[hash] = imageReferences.value(hash, 0) + 1;
                }
            }
        }
    }
    
    // qDebug() << "Rebuilt image references:" << imageReferences;
}

MainWindow* InkCanvas::findMainWindow() {
    // Try to find MainWindow through the widget hierarchy
    MainWindow *mainWindow = qobject_cast<MainWindow*>(parent());
    if (!mainWindow) {
        QWidget *current = this;
        while (current && !mainWindow) {
            current = current->parentWidget();
            mainWindow = qobject_cast<MainWindow*>(current);
        }
    }
    return mainWindow;
}

QString InkCanvas::extractImageHashFromPath(const QString &imagePath) const {
    // Extract hash from filename (format: {notebookId}_img_p{page}_{hash}.{ext})
    QString fileName = QFileInfo(imagePath).fileName();
    int lastDot = fileName.lastIndexOf('.');
    int lastUnderscore = fileName.lastIndexOf('_');
    if (lastDot > 0 && lastUnderscore > 0 && lastUnderscore < lastDot) {
        return fileName.mid(lastUnderscore + 1, lastDot - lastUnderscore - 1);
    }
    return QString();
}

bool InkCanvas::handleMissingPdf(QWidget *parent) {
    if (pdfPath.isEmpty()) {
        return true; // No PDF was linked, continue normally
    }
    
    // Check if PDF file exists
    if (QFile::exists(pdfPath)) {
        return true; // PDF exists, continue normally
    }
    
    // PDF is missing, show relink dialog
    PdfRelinkDialog dialog(pdfPath, parent);
    dialog.exec();
    
    PdfRelinkDialog::Result result = dialog.getResult();
    
    if (result == PdfRelinkDialog::RelinkPdf) {
        QString newPdfPath = dialog.getNewPdfPath();
        if (!newPdfPath.isEmpty()) {
            // Update PDF path and reload
            pdfPath = newPdfPath;
            saveNotebookMetadata(); // Save the new path
            loadPdf(newPdfPath); // Load the new PDF
            return true;
        }
    } else if (result == PdfRelinkDialog::ContinueWithoutPdf) {
        // Clear PDF path and continue without PDF
        pdfPath.clear();
        saveNotebookMetadata(); // Save the cleared path
        clearPdf(); // Clear any loaded PDF data
        return true;
    }
    
    // User cancelled or other error
    return false;
}

void InkCanvas::migrateOldMetadataFiles() {
    if (saveFolder.isEmpty()) return;
    
    // qDebug() << "Migrating old metadata files for folder:" << saveFolder;
    
    // ✅ CRITICAL: Always load existing notebook ID first to preserve file naming consistency
    QString idFile = saveFolder + "/.notebook_id.txt";
    if (QFile::exists(idFile)) {
        QFile file(idFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString existingId = in.readLine().trimmed();
            file.close();
            if (!existingId.isEmpty()) {
                notebookId = existingId;
                // qDebug() << "Preserved existing notebook ID:" << notebookId;
            }
        }
    }
    
    // Only generate new ID if none exists
    if (notebookId.isEmpty()) {
        notebookId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "");
        // qDebug() << "Generated new notebook ID:" << notebookId;
    }
    
    // Migrate PDF path
    QString pdfPathFile = saveFolder + "/.pdf_path.txt";
    if (QFile::exists(pdfPathFile)) {
        QFile file(pdfPathFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            pdfPath = in.readLine().trimmed();
            file.close();
        }
    }
    
    // Migrate background config
    QString bgMetaFile = saveFolder + "/.background_config.txt";
    if (QFile::exists(bgMetaFile)) {
        QFile file(bgMetaFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("style=")) {
                    QString val = line.mid(6);
                    if (val == "Grid") backgroundStyle = BackgroundStyle::Grid;
                    else if (val == "Lines") backgroundStyle = BackgroundStyle::Lines;
                    else backgroundStyle = BackgroundStyle::None;
                } else if (line.startsWith("color=")) {
                    backgroundColor = QColor(line.mid(6));
                } else if (line.startsWith("density=")) {
                    backgroundDensity = line.mid(8).toInt();
                }
            }
            file.close();
        }
    }
    
    // Migrate bookmarks
    QString bookmarksFile = saveFolder + "/.bookmarks.txt";
    bookmarks.clear();
    if (QFile::exists(bookmarksFile)) {
        QFile file(bookmarksFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (!line.isEmpty()) {
                    bookmarks.append(line);
                }
            }
            file.close();
        }
    }
    
    // Set default last accessed page to 0
    lastAccessedPage = 0;
    
    // Save to new JSON format
    saveNotebookMetadata();
    
    // ✅ Verify migration was successful before cleanup
    QString jsonFile = saveFolder + "/.speedynote_metadata.json";
    if (QFile::exists(jsonFile)) {
        // Clean up old files only after successful JSON creation
        QFile::remove(saveFolder + "/.notebook_id.txt");
        QFile::remove(saveFolder + "/.pdf_path.txt");
        QFile::remove(saveFolder + "/.background_config.txt");
        QFile::remove(saveFolder + "/.bookmarks.txt");
        
        //qDebug() << "Successfully migrated metadata files to JSON for folder:" << saveFolder;
        //qDebug() << "Notebook ID preserved:" << notebookId;
        //qDebug() << "PDF path:" << pdfPath;
        //qDebug() << "Bookmarks count:" << bookmarks.size();
    } else {
        qWarning() << "Migration failed - JSON file not created. Keeping old files as backup.";
    }
}

// ✅ Modified existing methods to use JSON system
void InkCanvas::saveBackgroundMetadata() {
    // This method is called when background settings change
    // Now it just saves to the unified JSON
    saveNotebookMetadata();
}

// ✅ Bookmark management methods
void InkCanvas::addBookmark(const QString &bookmark) {
    if (!bookmarks.contains(bookmark)) {
        bookmarks.append(bookmark);
        saveNotebookMetadata();
    }
}

void InkCanvas::removeBookmark(const QString &bookmark) {
    if (bookmarks.removeAll(bookmark) > 0) {
        saveNotebookMetadata();
    }
}

QStringList InkCanvas::getBookmarks() const {
    return bookmarks;
}

void InkCanvas::setBookmarks(const QStringList &bookmarkList) {
    bookmarks = bookmarkList;
    saveNotebookMetadata();
}

QString InkCanvas::getDisplayPath() const {
    return actualPackagePath.isEmpty() ? saveFolder : actualPackagePath;
}

void InkCanvas::syncSpnPackage() {
    if (isSpnPackage && !actualPackagePath.isEmpty() && !tempWorkingDir.isEmpty()) {
        SpnPackageManager::updateSpnFromTemp(actualPackagePath, tempWorkingDir);
    }
}

void InkCanvas::syncSpnPackageDeferred() {
    // ✅ OPTIMIZATION: Defer .spn package sync to avoid blocking UI
    // Re-packing the entire directory is expensive (reads ALL files)
    // This debounces multiple saves into a single sync after 3 seconds of inactivity
    if (!isSpnPackage || actualPackagePath.isEmpty() || tempWorkingDir.isEmpty()) {
        return; // Not an .spn package, nothing to sync
    }
    
    spnSyncPending = true;
    if (spnSyncTimer) {
        spnSyncTimer->start(); // Restart timer (debounce)
    }
}

void InkCanvas::flushPendingSpnSync() {
    // Force immediate .spn sync if there's a pending deferred sync
    if (spnSyncPending && spnSyncTimer) {
        spnSyncTimer->stop();
        syncSpnPackage();
        spnSyncPending = false;
    }
}

void InkCanvas::onSpnSyncTimeout() {
    // Timer fired - perform the actual .spn package sync
    if (spnSyncPending) {
        syncSpnPackage();
        spnSyncPending = false;
    }
}

void InkCanvas::handlePictureMouseMove(QMouseEvent *event) {
    if (!activePictureWindow) return;
    
    // Convert current mouse position to canvas coordinates
    QPointF currentWidgetPos = event->position();
    QPointF currentCanvasPos = mapWidgetToCanvas(currentWidgetPos);
    
    // Convert start position to canvas coordinates for proper delta calculation
    QPointF startCanvasPos = mapWidgetToCanvas(QPointF(pictureInteractionStartPos));
    
    // Calculate delta in canvas coordinates (handles DPI scaling correctly)
    QPointF deltaCanvas = currentCanvasPos - startCanvasPos;
    QPoint delta = deltaCanvas.toPoint();
    
    QRect newRect = pictureStartRect;
    
    if (pictureResizing) {
        // Handle resize based on the stored handle (similar to MarkdownWindow)
        switch (static_cast<PictureWindow::ResizeHandle>(pictureResizeHandle)) {
            case PictureWindow::TopLeft:
                newRect.setTopLeft(newRect.topLeft() + delta);
                break;
            case PictureWindow::TopRight:
                newRect.setTopRight(newRect.topRight() + delta);
                break;
            case PictureWindow::BottomLeft:
                newRect.setBottomLeft(newRect.bottomLeft() + delta);
                break;
            case PictureWindow::BottomRight:
                newRect.setBottomRight(newRect.bottomRight() + delta);
                break;
            case PictureWindow::Top:
                newRect.setTop(newRect.top() + delta.y());
                break;
            case PictureWindow::Bottom:
                newRect.setBottom(newRect.bottom() + delta.y());
                break;
            case PictureWindow::Left:
                newRect.setLeft(newRect.left() + delta.x());
                break;
            case PictureWindow::Right:
                newRect.setRight(newRect.right() + delta.x());
                break;
            default:
                break;
        }
        
        // Maintain aspect ratio if enabled
        if (activePictureWindow->getMaintainAspectRatio()) {
            double aspectRatio = activePictureWindow->getAspectRatio();
            if (aspectRatio > 0) {
                QSize newSize = newRect.size();
                int headerHeight = 32; // Account for updated header height
                int availableHeight = newSize.height() - headerHeight;
                
                // Calculate new dimensions based on aspect ratio
                int newWidth = static_cast<int>(availableHeight * aspectRatio);
                int newHeight = static_cast<int>(newWidth / aspectRatio) + headerHeight;
                
                // Apply the constrained size
                newRect.setSize(QSize(newWidth, newHeight));
            }
        }
        
        // Enforce minimum size
        newRect.setSize(newRect.size().expandedTo(QSize(100, 80)));
        
    } else if (pictureDragging) {
        // Handle drag
        newRect.translate(delta);
    }
    
    // Keep within canvas bounds
    QRect canvasBounds = getCanvasRect();
    
    // Apply canvas bounds constraints
    int maxX = canvasBounds.width() - newRect.width();
    int maxY = canvasBounds.height() - newRect.height();
    
    newRect.setX(qMax(0, qMin(newRect.x(), maxX)));
    newRect.setY(qMax(0, qMin(newRect.y(), maxY)));
    
    // Also ensure the window doesn't get resized beyond canvas bounds
    if (newRect.right() > canvasBounds.width()) {
        newRect.setWidth(canvasBounds.width() - newRect.x());
    }
    if (newRect.bottom() > canvasBounds.height()) {
        newRect.setHeight(canvasBounds.height() - newRect.y());
    }
    
    // ✅ PERFORMANCE OPTIMIZATION: Store the preview rect but don't update the actual window yet
    picturePreviewRect = newRect;
    
    // ✅ PERFORMANCE: Adaptive throttling based on movement speed for fast motion
    static QElapsedTimer updateTimer;
    static bool timerInitialized = false;
    static QPoint lastUpdatePos;
    
    if (!timerInitialized) {
        updateTimer.start();
        timerInitialized = true;
        lastUpdatePos = newRect.center();
    }
    
    // Calculate movement distance since last update for adaptive throttling
    QPoint currentPos = newRect.center();
    int movementDistance = (currentPos - lastUpdatePos).manhattanLength();
    
    // Adaptive throttling: faster updates for fast movement, slower for slow movement
    int throttleInterval;
    if (movementDistance > 50) {
        throttleInterval = 8; // ~120 FPS for very fast movement
    } else if (movementDistance > 20) {
        throttleInterval = 12; // ~80 FPS for fast movement  
    } else {
        throttleInterval = 16; // ~60 FPS for normal movement
    }
    
    // Update based on adaptive interval
    if (updateTimer.elapsed() > throttleInterval) {
        // ✅ AGGRESSIVE CLEARING: Update larger combined area to eliminate all outline artifacts
        QRect widgetRect = mapCanvasToWidget(newRect);
        QRect previousWidgetRect = mapCanvasToWidget(picturePreviousRect);
        QRect combinedRect = widgetRect.united(previousWidgetRect);
        
        // ✅ FAST MOTION TRAIL CLEARING: For very fast movement, update a wider trail area
        if (movementDistance > 50) {
            // Create a trail rectangle that covers the entire movement path
            QPoint startPoint = previousWidgetRect.center();
            QPoint endPoint = widgetRect.center();
            
            // Calculate trail rectangle that encompasses the entire movement path
            int trailWidth = qMax(widgetRect.width(), previousWidgetRect.width()) + 50;
            int trailHeight = qMax(widgetRect.height(), previousWidgetRect.height()) + 50;
            
            QRect trailRect(
                qMin(startPoint.x(), endPoint.x()) - trailWidth/2,
                qMin(startPoint.y(), endPoint.y()) - trailHeight/2,
                qAbs(endPoint.x() - startPoint.x()) + trailWidth,
                qAbs(endPoint.y() - startPoint.y()) + trailHeight
            );
            
            // Use the larger of combined rect or trail rect
            combinedRect = combinedRect.united(trailRect);
        }
        
        // Larger padding to ensure complete clearing of fast-moving outlines
        update(combinedRect.adjusted(-30, -30, 30, 30)); // Even larger padding for fast motion
        
        updateTimer.restart();
        lastUpdatePos = currentPos;
    }
    
    // Update previous rect for next frame
    picturePreviousRect = newRect;
    
    // ✅ PERFORMANCE: Don't emit signals during movement - only on release
    // This prevents redundant saves and updates during dragging
}

// ✅ NEW FEATURE: Paste image from clipboard and save to notebook
QString InkCanvas::pasteImageFromClipboard() {
    // qDebug() << "InkCanvas::pasteImageFromClipboard() called";
    
    // Get the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        qWarning() << "Failed to access clipboard";
        QMessageBox::warning(this, tr("Clipboard Error"), 
            tr("Failed to access system clipboard."));
        return QString();
    }
    
    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData) {
        // qDebug() << "No mime data in clipboard";
        QMessageBox::information(this, tr("No Clipboard Data"), 
            tr("No data found in clipboard."));
        return QString();
    }
    
    // Check if clipboard contains image data
    QImage clipboardImage;
    
    if (mimeData->hasImage()) {
        // Direct image data
        QVariant imageData = mimeData->imageData();
        clipboardImage = qvariant_cast<QImage>(imageData);
        // qDebug() << "Found image data in clipboard, size:" << clipboardImage.size();
    } else if (mimeData->hasUrls()) {
        // Check if any URLs point to image files
        QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                QFileInfo fileInfo(filePath);
                
                // Check if it's an image file
                QStringList imageExtensions = {"png", "jpg", "jpeg", "bmp", "gif", "tiff", "webp"};
                if (imageExtensions.contains(fileInfo.suffix().toLower())) {
                    clipboardImage.load(filePath);
                    if (!clipboardImage.isNull()) {
                        // qDebug() << "Loaded image from URL:" << filePath;
                        break;
                    }
                }
            }
        }
    }
    
    // Check if we successfully got an image
    if (clipboardImage.isNull()) {
        // qDebug() << "No valid image found in clipboard";
        QMessageBox::information(this, tr("No Image in Clipboard"), 
            tr("No image data found in clipboard.\n\nPlease copy an image to the clipboard first."));
        return QString();
    }
    
    // Validate image size (prevent extremely large images)
    if (clipboardImage.width() > 8192 || clipboardImage.height() > 8192) {
        QMessageBox::warning(this, tr("Image Too Large"), 
            tr("The clipboard image is too large (max 8192x8192 pixels).\n\nPlease use a smaller image."));
        return QString();
    }
    
    // Get save folder from picture manager
    QString saveFolder;
    if (pictureManager) {
        saveFolder = pictureManager->getSaveFolder();
    }
    
    if (saveFolder.isEmpty()) {
        // qDebug() << "No save folder available";
        QMessageBox::warning(this, tr("No Notebook Open"), 
            tr("Please save your notebook as a SpeedyNote Package (.spn) file before pasting images."));
        return QString();
    }
    
    // Generate unique filename for clipboard image
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QString uniqueId = QUuid::createUuid().toString().mid(1, 8); // Remove braces and take first 8 chars
    int currentPage = getLastActivePage();
    
    // Get notebook ID for consistent naming
    QString notebookId = "notebook"; // Default fallback
    if (pictureManager) {
        QString managerId = pictureManager->getNotebookId();
        if (!managerId.isEmpty()) {
            notebookId = managerId;
        }
    }
    
    QString filename = QString("%1_clipboard_p%2_%3_%4.png")
                      .arg(notebookId)
                      .arg(currentPage, 5, 10, QChar('0'))
                      .arg(timestamp)
                      .arg(uniqueId);
    
    QString targetPath = saveFolder + "/" + filename;
    
    // qDebug() << "Saving clipboard image to:" << targetPath;
    
    // Save the image as PNG (lossless, good for clipboard content)
    bool saved = clipboardImage.save(targetPath, "PNG", 95); // High quality PNG
    
    if (!saved) {
        qWarning() << "Failed to save clipboard image to:" << targetPath;
        QMessageBox::critical(this, tr("Save Error"), 
            tr("Failed to save clipboard image to notebook.\n\nPath: %1").arg(targetPath));
        return QString();
    }
    
    // qDebug() << "Successfully saved clipboard image";
    
    #ifdef Q_OS_WIN
    // Set hidden attribute on Windows for consistency with other notebook files
    // SetFileAttributesW(reinterpret_cast<const wchar_t *>(targetPath.utf16()), FILE_ATTRIBUTE_HIDDEN);
    #endif
    
    return targetPath;
}

void InkCanvas::handlePictureMouseRelease(QMouseEvent *event) {
    if (!activePictureWindow) return;
    
    // ✅ PERFORMANCE: Apply the final position only on release for smooth interaction
    if (!picturePreviewRect.isEmpty()) {
        // Update the actual picture window position
        activePictureWindow->setCanvasRect(picturePreviewRect);
        
        // ✅ FULL SCREEN UPDATE: Clear all orange outline artifacts and render final picture
        update(); // Full canvas update to clear all accumulated outline frames
        
        // Clear the preview rect
        picturePreviewRect = QRect();
    }
    
    // ✅ Emit signals only on release to prevent redundant operations during movement
    if (pictureResizing) {
        emit activePictureWindow->windowResized(activePictureWindow);
    } else if (pictureDragging) {
        emit activePictureWindow->windowMoved(activePictureWindow);
    }
    
    // Reset interaction state
    activePictureWindow = nullptr;
    pictureDragging = false;
    pictureResizing = false;
    pictureResizeHandle = 0; // Reset to None (0)
    
    // Mark canvas as edited since picture was moved/resized
    setEdited(true);
    
    // Save current state
    if (pictureManager) {
        int currentPage = getLastActivePage();
        pictureManager->saveWindowsForPage(currentPage);
    }
}

void InkCanvas::checkAutoscrollThreshold(int oldPanY, int newPanY) {
    // Detect if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = 0;
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8 && backgroundImage.height() > 0) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (!isCombinedCanvas || singlePageHeight == 0) {
        return; 
    }
    
    // Enforce cooldown to prevent rapid page switches during touch panning (both active and inertia)
    // With smooth continuation, this prevents pages from flipping too quickly
    if (isTouchPanning) {
        // Check if we're in cooldown period (300ms after last page switch) 
        // Reduced from 500ms since smooth continuation makes it feel more natural
        if (pageSwitchInProgress && pageSwitchCooldown.isValid() && pageSwitchCooldown.elapsed() < 300) {
            return; // Skip autoscroll checks during cooldown
        }
    }
    
    // Threshold for autoscroll is the bottom of the first page in the combined view
    int threshold = singlePageHeight;
    
    // Define early save trigger zones (save when getting close to threshold)
    int forwardSaveZone = threshold - 300;  // Save 300 pixels before forward threshold
    int backwardSaveZone = -5;  // Save 5 pixels into negative territory for better timing on slower devices
    int backwardSwitchThreshold = -300;  // Delay backward page switch to -300 pixels for save completion
    
    // Safety check: ensure we have a reasonable threshold size for our offsets
    if (threshold < 600) {
        // For very small thresholds, use proportional offsets instead of fixed 300px
        forwardSaveZone = threshold - (threshold / 4);
        backwardSaveZone = -(threshold / 4);
        backwardSwitchThreshold = -(threshold / 4);
    }
    
    // Proactive save triggers - save before reaching the actual scroll threshold
    if (edited && oldPanY < forwardSaveZone && newPanY >= forwardSaveZone) {
        // Approaching forward threshold - trigger save early
        emit earlySaveRequested();
    }
    if (edited && oldPanY > backwardSaveZone && newPanY <= backwardSaveZone) {
        // Approaching backward threshold - trigger save early (much earlier now)
        emit earlySaveRequested();
    }
    
    // Check for forward autoscroll (scrolling down past the first page)
    if (oldPanY < threshold && newPanY >= threshold) {
        // Calculate relative pan offset beyond threshold for smooth continuation
        int excessPanY = newPanY - threshold;
        
        // Determine if this is inertia scrolling or active dragging BEFORE emitting signal
        bool isInertiaScroll = (isTouchPanning && inertiaTimer && inertiaTimer->isActive());
        bool isActiveDrag = (isTouchPanning && !isInertiaScroll);
        
        // Store continuation state BEFORE page switch happens for BOTH inertia and active drag
        if (isActiveDrag || isInertiaScroll) {
            pendingContinuationPanY = excessPanY;
            pendingContinuationPanX = panOffsetX;
            continueTouchGestureAfterPageLoad = true;
            pageSwitchInProgress = true;
            pageSwitchCooldown.start();
            
            if (isActiveDrag) {
                pageSwitchRecoveryTimer.start(); // Only start recovery timer for active dragging
            }
        }
        
        emit autoScrollRequested(1); // 1 for forward
        
        // For both inertia and active dragging, we keep touch panning active
        // and let loadPage() handle continuation for smooth scrolling
    }
    
    // Check for backward autoscroll (scrolling up past -300 for delayed switching)
    // This gives more time for the save operation to complete on slower devices
    if (oldPanY > backwardSwitchThreshold && newPanY <= backwardSwitchThreshold) {
        // Determine if this is inertia scrolling or active dragging BEFORE emitting signal
        bool isInertiaScroll = (isTouchPanning && inertiaTimer && inertiaTimer->isActive());
        bool isActiveDrag = (isTouchPanning && !isInertiaScroll);
        
        // Store continuation state BEFORE page switch happens for BOTH inertia and active drag
        if (isActiveDrag || isInertiaScroll) {
            // ✅ FIX: For backward scroll, use the FULL negative pan value, not just excess
            // When at -300, we should restore to threshold - 300 to maintain visual continuity
            // This prevents the sudden jump from negative pan to 0
            pendingContinuationPanY = threshold + newPanY;
            pendingContinuationPanX = panOffsetX;
            continueTouchGestureAfterPageLoad = true;
            pageSwitchInProgress = true;
            pageSwitchCooldown.start();
            
            if (isActiveDrag) {
                pageSwitchRecoveryTimer.start(); // Only start recovery timer for active dragging
            }
        }
        
        emit autoScrollRequested(-1); // -1 for backward (delayed until -300)
        
        // For both inertia and active dragging, we keep touch panning active
        // and let loadPage() handle continuation for smooth scrolling
    }
}

int InkCanvas::getAutoscrollThreshold() const {
    // Detect if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = 0;
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8 && backgroundImage.height() > 0) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers (handles 720p+)
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (!isCombinedCanvas || singlePageHeight == 0) {
        return 0; // Return 0 for non-combined canvases
    }
    
    return singlePageHeight;
}






