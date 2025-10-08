#include "InkCanvas.h"
#include "ToolType.h"
#include "MarkdownWindowManager.h"
#include "MarkdownWindow.h" // Include the full definition
#include "PictureWindowManager.h"
#include "PictureWindow.h" // Include the full definition
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QColor>
#include <cmath>
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
// #include <QInputDevice>
#include <QTabletEvent>
#include <QTouchEvent>
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

#include <poppler-qt5.h>




InkCanvas::InkCanvas(QWidget *parent) 
    : QWidget(parent), drawing(false), penColor(Qt::black), penThickness(5.0), zoomFactor(100), panOffsetX(0), panOffsetY(0), currentTool(ToolType::Pen) {    
    
    // Set theme-aware default pen color
    MainWindow *mainWindow = qobject_cast<MainWindow*>(parent);
    if (mainWindow) {
        penColor = mainWindow->getDefaultPenColor();
    }    
    setAttribute(Qt::WA_StaticContents);
    setTabletTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);  // Enable touch events

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
    
    // Initialize markdown manager
    markdownManager = new MarkdownWindowManager(this, this);
    pictureManager = new PictureWindowManager(this, this);
    
    // Connect pan/zoom signals to update markdown window positions
    connect(this, &InkCanvas::panChanged, markdownManager, &MarkdownWindowManager::updateAllWindowPositions);
    connect(this, &InkCanvas::zoomChanged, markdownManager, &MarkdownWindowManager::updateAllWindowPositions);
    
    // Connect pan/zoom signals to update picture window positions
    connect(this, &InkCanvas::panChanged, pictureManager, &PictureWindowManager::updateAllWindowPositions);
    connect(this, &InkCanvas::zoomChanged, pictureManager, &PictureWindowManager::updateAllWindowPositions);
    
    // Initialize inertia scrolling timer (60 FPS for smooth animation)
    inertiaTimer = new QTimer(this);
    inertiaTimer->setInterval(16); // ~60 FPS
    connect(inertiaTimer, &QTimer::timeout, this, &InkCanvas::updateInertiaScroll);
}

InkCanvas::~InkCanvas() {
    // ✅ Auto-save if the canvas has been edited
    if (edited && !saveFolder.isEmpty()) {
        // Save the current page using existing logic
        saveToFile(lastActivePage);
        
        // Also save markdown windows if they exist
        if (markdownManager) {
            markdownManager->saveWindowsForPage(lastActivePage);
        }
        if (pictureManager) {
            pictureManager->saveWindowsForPage(lastActivePage);
        }
    }
    
    // ✅ Cleanup PDF resources
    if (pdfDocument) {
        pdfDocument.reset();
        pdfDocument = nullptr;
    }
    
    // ✅ Clear caches to free memory
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
    }
    {
        QMutexLocker locker(&noteCacheMutex);
        noteCache.clear();
    }
    
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
    
    // ✅ Clear inertia scrolling resources
    cachedFrame = QPixmap(); // Release cached frame memory
    recentVelocities.clear(); // Clear velocity history
    
    // ✅ Cancel and clean up any active PDF watchers
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher && !watcher->isFinished()) {
            watcher->cancel();
        }
        watcher->deleteLater();
    }
    activePdfWatchers.clear();
    
    // ✅ Cancel and clean up any active note cache watchers
    for (QFutureWatcher<void>* watcher : activeNoteWatchers) {
        if (watcher && !watcher->isFinished()) {
            watcher->cancel();
        }
        watcher->deleteLater();
    }
    activeNoteWatchers.clear();
    
    // ✅ Clear PDF text boxes - CRITICAL: Clear selectedTextBoxes first to prevent crashes
    selectedTextBoxes.clear();  // Must clear before deleting currentPdfTextBoxes
    qDeleteAll(currentPdfTextBoxes);
    currentPdfTextBoxes.clear();
    
    // ✅ Explicitly clean up window managers to prevent memory leaks
    if (markdownManager) {
        markdownManager->deleteLater();
        markdownManager = nullptr;
    }
    if (pictureManager) {
        pictureManager->deleteLater();
        pictureManager = nullptr;
    }
    
    // ✅ Sync .spn package and cleanup temp directory
    if (isSpnPackage) {
        syncSpnPackage();
        SpnPackageManager::cleanupTempDir(tempWorkingDir);
    }
}



void InkCanvas::initializeBuffer() {
    QScreen *screen = QGuiApplication::primaryScreen();
    qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    // Get logical screen size
    QSize logicalSize = screen ? screen->size() : QSize(1440, 900);
    QSize pixelSize = logicalSize * dpr;

    pixelSize.setHeight(pixelSize.height() * 2);

    buffer = QPixmap(pixelSize);
    buffer.fill(Qt::transparent);

    setMaximumSize(pixelSize); // 🔥 KEY LINE to make full canvas drawable
}

void InkCanvas::loadPdf(const QString &pdfPath) {
    // ✅ Clear existing PDF cache before loading new PDF to prevent old pages from showing
    {
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear();
    }
    currentCachedPage = -1;
    
    // Cancel any active PDF caching operations
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // Cancel and clean up any active PDF watchers from previous PDF
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher && !watcher->isFinished()) {
            watcher->cancel();
        }
        watcher->deleteLater();
    }
    activePdfWatchers.clear();
    
    pdfDocument = std::unique_ptr<Poppler::Document>(Poppler::Document::load(pdfPath));
    if (pdfDocument && !pdfDocument->isLocked()) {
        // Enable anti-aliasing rendering hints for better text quality
        pdfDocument->setRenderHint(Poppler::Document::Antialiasing, true);
        pdfDocument->setRenderHint(Poppler::Document::TextAntialiasing, true);
        pdfDocument->setRenderHint(Poppler::Document::TextHinting, true);
        pdfDocument->setRenderHint(Poppler::Document::TextSlightHinting, true);
        
        totalPdfPages = pdfDocument->numPages();
        isPdfLoaded = true;
        totalPdfPages = pdfDocument->numPages();
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
    }

    // ✅ Clear the background image immediately to remove PDF from display
    backgroundImage = QPixmap();
    
    // Reset cache system state
    currentCachedPage = -1;
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // Cancel and clean up any active PDF watchers
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher && !watcher->isFinished()) {
            watcher->cancel();
        }
        watcher->deleteLater();
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
    
    // Reset cache system state
    currentCachedPage = -1;
    if (pdfCacheTimer && pdfCacheTimer->isActive()) {
        pdfCacheTimer->stop();
    }
    
    // Cancel and clean up any active PDF watchers
    for (QFutureWatcher<void>* watcher : activePdfWatchers) {
        if (watcher && !watcher->isFinished()) {
            watcher->cancel();
        }
        watcher->deleteLater();
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
        }
    }
    
    if (isCached) {
        loadPage(pageNumber);  // Load annotations
        loadPdfTextBoxes(pageNumber); // Load text boxes for PDF text selection
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
    loadPdfTextBoxes(pageNumber); // Load text boxes for PDF text selection
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

        // Try to render next page for combination
        QImage nextPageImage;
        int nextPageNumber = pageNumber + 1;
        if (nextPageNumber < pdfDocument->numPages()) {
            std::unique_ptr<Poppler::Page> nextPage(pdfDocument->page(nextPageNumber));
            if (nextPage) {
                nextPageImage = nextPage->renderToImage(96, 96);
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
        // Fill entire background (simpler and avoids visual artifacts)
        painter.fillRect(rect(), palette().window().color());
        
        // Draw the cached frame at the offset position (no antialiasing for speed)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(cachedFrameOffset, cachedFrame);
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

        if (devicePixelRatioF() > 1.0)
            scaledDensity *= devicePixelRatioF();  // Optional DPI handling

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
    
    // Draw markdown selection overlay
    if (markdownSelectionMode && markdownSelecting) {
        painter.save();
        QPen selectionPen(Qt::DashLine);
        selectionPen.setColor(Qt::green);
        selectionPen.setWidthF(2.0);
        painter.setPen(selectionPen);
        
        QBrush selectionBrush(QColor(0, 255, 0, 30)); // Semi-transparent green
        painter.setBrush(selectionBrush);
        
        QRect selectionRect = QRect(markdownSelectionStart, markdownSelectionEnd).normalized();
        painter.drawRect(selectionRect);
        painter.restore();
    }
    
    // Draw PDF text selection overlay on top of everything
    if (pdfTextSelectionEnabled && isPdfLoaded) {
        painter.save(); // Save painter state for PDF text overlay
        painter.resetTransform(); // Reset transform to draw directly in logical widget coordinates
        
        // Draw selection rectangle if actively selecting
        if (pdfTextSelecting) {
            QPen selectionPen(Qt::DashLine);
            selectionPen.setColor(QColor(0, 120, 215)); // Blue selection rectangle
            selectionPen.setWidthF(2.0); // Make it more visible
            painter.setPen(selectionPen);
            
            QBrush selectionBrush(QColor(0, 120, 215, 30)); // Semi-transparent blue fill
            painter.setBrush(selectionBrush);
            
            QRectF selectionRect(pdfSelectionStart, pdfSelectionEnd);
            selectionRect = selectionRect.normalized();
            painter.drawRect(selectionRect);
        }
        
        // Draw highlights for selected text boxes
        if (!selectedTextBoxes.isEmpty()) {
            QColor highlightColor = QColor(255, 255, 0, 100); // Semi-transparent yellow
            painter.setBrush(highlightColor);
            painter.setPen(Qt::NoPen);
            
            // Draw highlight rectangles for selected text boxes
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
                    
                    painter.drawRect(widgetRect);
                }
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
            pdfTextSelecting = true;
            pdfSelectionStart = event->pos();
            pdfSelectionEnd = pdfSelectionStart;
            
            // Clear any existing selected text boxes without resetting pdfTextSelecting
            selectedTextBoxes.clear();
            
            setCursor(Qt::IBeamCursor); // Ensure cursor is correct
            update(); // Refresh display
            event->accept();
            return;
        } else if (event->type() == QEvent::TabletMove && pdfTextSelecting) {
            pdfSelectionEnd = event->pos();
            
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
            pdfSelectionEnd = event->pos();
            
            // Process any pending selection immediately on release
            if (pdfTextSelectionTimer && pdfTextSelectionTimer->isActive()) {
                pdfTextSelectionTimer->stop();
                if (hasPendingSelection) {
                    updatePdfTextSelection(pendingSelectionStart, pendingSelectionEnd);
                    hasPendingSelection = false;
                }
            } else {
                // Update selection with final position
                updatePdfTextSelection(pdfSelectionStart, pdfSelectionEnd);
            }
            
            pdfTextSelecting = false;
            
            // Check for link clicks if no text was selected
            QString selectedText = getSelectedPdfText();
            if (selectedText.isEmpty()) {
                handlePdfLinkClick(event->pos());
            } else {
                // Show context menu for text selection
                QPoint globalPos = mapToGlobal(event->pos()); // Qt5 pos() already returns QPoint
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
    if (event->pointerType() == QTabletEvent::Eraser) {
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
        drawing = true;
        lastPoint = event->posF(); // Logical widget coordinates
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
                lassoPathPoints << event->posF();
                lastPoint = event->posF();
                QRectF newPreviewBoundingRect = lassoPathPoints.boundingRect();
                // Update the area of the selection rectangle preview (logical widget coordinates)
                update(oldPreviewBoundingRect.united(newPreviewBoundingRect).toRect().adjusted(-5,-5,5,5));
            } else if (movingSelection) {
                QPointF delta = event->posF() - lastMovePoint; // Delta in logical widget coordinates
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
                
                lastMovePoint = event->posF();
                if (!edited){
                    edited = true;
                    // Invalidate cache for current page since it's been modified
                    invalidateCurrentPageCache();
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
                lastPoint = event->posF();
                
                // Calculate affected rectangle that needs updating
                QRectF updateRect = calculatePreviewRect(straightLineStartPoint, oldLastPoint, lastPoint);
                update(updateRect.toRect());
                
                // Reset timer
                updateTimer.restart();
            } else {
                // Just update the last point without redrawing
                lastPoint = event->posF();
            }
        } else if (straightLineMode && isErasing) {
            // For eraser in straight line mode, continuously erase from start to current point
            // This gives immediate visual feedback and smoother erasing experience
            
            // Store current point
            QPointF currentPoint = event->posF();
            
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
            eraseStroke(lastPoint, event->posF(), event->pressure());
        } else {
            drawStroke(lastPoint, event->posF(), event->pressure());
        }
        lastPoint = event->posF();
            
            // Only track benchmarking when enabled
            if (benchmarking) {
        processedTimestamps.push_back(benchmarkTimer.elapsed());
            }
        }
    } else if (event->type() == QEvent::TabletRelease) {
        if (straightLineMode && !isErasing) {
            // Draw the final line on release with the current pressure
            qreal pressure = event->pressure();
            
            // Always use at least a minimum pressure
            pressure = qMax(pressure, 0.5);
            
            drawStroke(straightLineStartPoint, event->posF(), pressure);
            
            // Only track benchmarking when enabled
            if (benchmarking) {
                processedTimestamps.push_back(benchmarkTimer.elapsed());
            }
            
            // Force repaint to clear the preview line
            update();
            if (!edited){
                edited = true;
                // Invalidate cache for current page since it's been modified
                invalidateCurrentPageCache();
            }
        } else if (straightLineMode && isErasing) {
            // For erasing in straight line mode, most of the work is done during movement
            // Just ensure one final erasing pass from start to end point
            qreal pressure = qMax(event->pressure(), 0.5);
            
            // Final pass to ensure the entire line is erased
            eraseStroke(straightLineStartPoint, event->posF(), pressure);
            
            // Force update to clear any remaining artifacts
            update();
            if (!edited){
                edited = true;
                // Invalidate cache for current page since it's been modified
                invalidateCurrentPageCache();
            }
        }
        
        drawing = false;
        
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
                        for (const QPointF& p_widget_logical : qAsConst(lassoPathPoints)) {
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
    qreal dpr = devicePixelRatioF();
    qreal padding;
    
    if (currentTool == ToolType::Eraser) {
        padding = penThickness * 6.0 * dpr;
    } else if (currentTool == ToolType::Marker) {
        padding = penThickness * 8.0 * dpr;
    } else {
        padding = penThickness * dpr;
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
        QPointF widgetPos = event->pos();
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
                pictureInteractionStartPos = event->pos(); // Qt5 pos() already returns QPoint
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
                pictureInteractionStartPos = event->pos(); // Qt5 pos() already returns QPoint
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
                     event->globalPos(),
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
                     event->globalPos(),
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
    
    // Handle markdown selection when enabled
    if (markdownSelectionMode && event->button() == Qt::LeftButton) {
        markdownSelecting = true;
        markdownSelectionStart = event->pos();
        markdownSelectionEnd = markdownSelectionStart;
        event->accept();
        return;
    }
    
    // Handle picture selection when enabled
    if (pictureSelectionMode && event->button() == Qt::LeftButton) {
        // qDebug() << "InkCanvas::mousePressEvent() - Picture selection mode active!";
        //qDebug() << "  Click position:" << event->pos();
        
        pictureSelecting = true;
        pictureSelectionStart = event->pos();
        
        // Show file dialog to select image
        //qDebug() << "  Opening file dialog...";
        QString imagePath = QFileDialog::getOpenFileName(this, tr("Select Image"), "", 
            tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.tiff *.webp)"));
        
        //qDebug() << "  Selected image path:" << imagePath;
        
        if (!imagePath.isEmpty()) {
            // Copy image to notebook and create picture window
            MainWindow *mainWindow = qobject_cast<MainWindow*>(parent());
            if (!mainWindow) {
                // Try to find MainWindow through the widget hierarchy
                QWidget *current = this;
                while (current && !mainWindow) {
                    current = current->parentWidget();
                    mainWindow = qobject_cast<MainWindow*>(current);
                }
            }
            //qDebug() << "  MainWindow found:" << (mainWindow != nullptr);
            //qDebug() << "  PictureManager found:" << (pictureManager != nullptr);
            
            if (pictureManager) {
                // Use InkCanvas's own method to get current page
                int currentPage = getLastActivePage();
                //qDebug() << "  Current page:" << currentPage;
                
                QString copiedImagePath = pictureManager->copyImageToNotebook(imagePath, currentPage);
                //qDebug() << "  Copied image path:" << copiedImagePath;
                
                if (!copiedImagePath.isEmpty()) {
                    // Create picture window at clicked position
                    QRect initialRect(pictureSelectionStart, QSize(200, 150));
                    //qDebug() << "  Creating picture window with rect:" << initialRect;
                    
                    PictureWindow* window = pictureManager->createPictureWindow(initialRect, copiedImagePath);
                    //qDebug() << "  Picture window created:" << (window != nullptr);
                    
                    // Save pictures for current page
                    pictureManager->saveWindowsForPage(currentPage);
                    //qDebug() << "  Pictures saved for page";
                    
                    // Mark canvas as edited
                    setEdited(true);
                    
                    // ✅ CACHE FIX: Invalidate note cache when pictures are created
                    // This ensures that when switching pages and coming back, the new picture is shown
                    invalidateCurrentPageCache();
                    
                    //qDebug() << "  Canvas marked as edited";
                } else {
                    //qDebug() << "  ERROR: Failed to copy image to notebook!";
                }
            } else {
                //qDebug() << "  ERROR: PictureManager is null!";
            }
        } else {
            //qDebug() << "  User cancelled file dialog or no image selected";
        }
        
        // Exit picture selection mode after placing a picture
        setPictureSelectionMode(false);
        // qDebug() << "  Picture selection mode disabled";
        
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
                    // Save pictures for current page
                    pictureManager->saveWindowsForPage(currentPage);
                    
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
                    
                    // ✅ CACHE FIX: Invalidate note cache when pictures are added
                    invalidateCurrentPageCache();
                    
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
            pdfTextSelecting = true;
            pdfSelectionStart = event->pos();
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
    
    // Handle markdown selection when enabled
    if (markdownSelectionMode && markdownSelecting) {
        markdownSelectionEnd = event->pos();
        update(); // Refresh to show selection rectangle
        event->accept();
        return;
    }
    
    // Handle PDF text selection when enabled (mouse/touch fallback - stylus handled in tabletEvent)
    if (pdfTextSelectionEnabled && isPdfLoaded && pdfTextSelecting) {
        pdfSelectionEnd = event->pos();
        
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
    
    // Handle markdown selection when enabled
    if (markdownSelectionMode && markdownSelecting && event->button() == Qt::LeftButton) {
        markdownSelecting = false;
        
        // Create markdown window if selection is valid
        QRect selectionRect = QRect(markdownSelectionStart, markdownSelectionEnd).normalized();
        if (selectionRect.width() > 50 && selectionRect.height() > 50 && markdownManager) {
            markdownManager->createMarkdownWindow(selectionRect);
        }
        
        // Exit selection mode
        setMarkdownSelectionMode(false);
        
        // Force screen update to clear the green selection overlay
        update();
        
        event->accept();
        return;
    }
    
    // Handle PDF text selection when enabled (mouse/touch fallback - stylus handled in tabletEvent)
    if (pdfTextSelectionEnabled && isPdfLoaded && pdfTextSelecting) {
        if (event->button() == Qt::LeftButton) {
            pdfSelectionEnd = event->pos();
            
            // Process any pending selection immediately on release
            if (pdfTextSelectionTimer && pdfTextSelectionTimer->isActive()) {
                pdfTextSelectionTimer->stop();
                if (hasPendingSelection) {
                    updatePdfTextSelection(pendingSelectionStart, pendingSelectionEnd);
                    hasPendingSelection = false;
                }
            } else {
                // Update selection with final position
                updatePdfTextSelection(pdfSelectionStart, pdfSelectionEnd);
            }
            
            pdfTextSelecting = false;
            
            // Check for link clicks if no text was selected
            QString selectedText = getSelectedPdfText();
            if (selectedText.isEmpty()) {
                handlePdfLinkClick(event->pos());
            } else {
                // Show context menu for text selection
                QPoint globalPos = mapToGlobal(event->pos()); // Qt5 pos() already returns QPoint
                showPdfTextSelectionMenu(globalPos);
            }
            
            event->accept();
            return;
        }
    }
    

    
    event->ignore();
}

void InkCanvas::drawStroke(const QPointF &start, const QPointF &end, qreal pressure) {
    if (buffer.isNull()) {
        initializeBuffer();
    }

    if (!edited){
        edited = true;
        // Invalidate cache for current page since it's been modified
        invalidateCurrentPageCache();
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

    if (!edited){
        edited = true;
        // Invalidate cache for current page since it's been modified
        invalidateCurrentPageCache();
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
    } else if (buffer.height() > 1400) { // Fallback heuristic for very tall buffers
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
        
        // Save next page (bottom half) by MERGING with existing content
        int nextPageNumber = pageNumber + 1;
        QString nextFilePath = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(nextPageNumber, 5, 10, QChar('0'));
        QPixmap nextPageBuffer = buffer.copy(0, singlePageHeight, bufferWidth, singlePageHeight);
        
        // Check if bottom half has any non-transparent content
        QImage nextCheck = nextPageBuffer.toImage();
        bool hasNewContent = false;
        for (int y = 0; y < nextCheck.height() && !hasNewContent; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb*>(nextCheck.scanLine(y));
            for (int x = 0; x < nextCheck.width() && !hasNewContent; ++x) {
                if (qAlpha(row[x]) != 0) {
                    hasNewContent = true;
                }
            }
        }
        
        if (hasNewContent) {
            // Load existing next page content and merge with new content
            QPixmap existingNextPage;
            if (QFile::exists(nextFilePath)) {
                existingNextPage.load(nextFilePath);
            }
            
            // Create merged image
            QImage mergedNextImage(nextPageBuffer.size(), QImage::Format_ARGB32);
            mergedNextImage.fill(Qt::transparent);
            {
                QPainter painter(&mergedNextImage);
                
                // Draw existing content first
                if (!existingNextPage.isNull()) {
                    painter.drawPixmap(0, 0, existingNextPage);
                }
                
                // Draw new content on top
                painter.drawPixmap(0, 0, nextPageBuffer);
            }
            
            mergedNextImage.save(nextFilePath, "PNG");
            
        // Update cache for next page with merged content
        {
            QMutexLocker locker(&noteCacheMutex);
            noteCache.insert(nextPageNumber, new QPixmap(QPixmap::fromImage(mergedNextImage)));
        }
            
            // Note: Cache invalidation is now handled during page switching for better timing
        }
        
        // Update cache for current page
        {
            QMutexLocker locker(&noteCacheMutex);
            noteCache.insert(pageNumber, new QPixmap(currentPageBuffer));
        }
        
    } else {
        // Standard single page save
        QString filePath = saveFolder + QString("/%1_%2.png").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
        QImage image(buffer.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.drawPixmap(0, 0, buffer);
        image.save(filePath, "PNG");
        
        // Update note cache with the saved buffer
        {
            QMutexLocker locker(&noteCacheMutex);
            noteCache.insert(pageNumber, new QPixmap(buffer));
        }
    }
    
    edited = false;
    
    // Save windows with appropriate handling for combined canvas
    saveCombinedWindowsForPage(pageNumber);
    
    // ✅ Sync changes to .spn package if needed
    syncSpnPackage();
}

void InkCanvas::saveAnnotated(int pageNumber) {
    if (saveFolder.isEmpty()) return;

    // ✅ Determine the target directory for the annotated image
    QString targetDir;
    QString displayPath = getDisplayPath(); // Gets .spn path or folder path
    
    if (isSpnPackage && !actualPackagePath.isEmpty()) {
        // For .spn packages, save in the same directory as the .spn file
        QFileInfo spnInfo(actualPackagePath);
        targetDir = spnInfo.absolutePath();
    } else {
        // For regular folders, save in the folder itself
        targetDir = saveFolder;
    }
    
    // ✅ Get notebook ID from JSON metadata
    loadNotebookMetadata(); // Ensure metadata is loaded
    QString currentNotebookId = getNotebookId();
    if (currentNotebookId.isEmpty()) {
        currentNotebookId = "unknown"; // Fallback if no ID found
    }
    
    // ✅ Create filename with proper notebook ID
    QString fileName = QString("annotated_%1_page_%2.png")
                       .arg(currentNotebookId)
                       .arg(pageNumber + 1, 3, 10, QChar('0')); // 1-based page numbering
    QString filePath = targetDir + "/" + fileName;

    // Use the buffer size to ensure correct resolution
    QImage image(buffer.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    
    // ✅ Draw background (PDF or notebook background)
    if (!backgroundImage.isNull()) {
        // Draw PDF background if available
        painter.drawPixmap(0, 0, backgroundImage.scaled(buffer.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        // Draw notebook background (grid/lines/color) if no PDF
        painter.fillRect(image.rect(), backgroundColor);
        
        if (backgroundStyle != BackgroundStyle::None) {
            QPen linePen(QColor(100, 100, 100, 100));
            linePen.setWidthF(1.0);
            painter.setPen(linePen);

            qreal scaledDensity = backgroundDensity;
            if (devicePixelRatioF() > 1.0) {
                scaledDensity *= devicePixelRatioF();
            }

            if (backgroundStyle == BackgroundStyle::Lines || backgroundStyle == BackgroundStyle::Grid) {
                for (int y = 0; y < image.height(); y += scaledDensity) {
                    painter.drawLine(0, y, image.width(), y);
                }
            }
            if (backgroundStyle == BackgroundStyle::Grid) {
                for (int x = 0; x < image.width(); x += scaledDensity) {
                    painter.drawLine(x, 0, x, image.height());
                }
            }
        }
    }
    
    // ✅ Draw user strokes on top
    painter.drawPixmap(0, 0, buffer);
    
    // ✅ Save the image
    if (image.save(filePath, "PNG")) {
        // ✅ Emit signal to notify MainWindow for user message
        emit annotatedImageSaved(filePath);
    }
}


void InkCanvas::loadPage(int pageNumber) {
    if (saveFolder.isEmpty()) return;

    // Hide any markdown windows from the previous page BEFORE loading the new page content.
    // This ensures the correct repaint area and stops the transparency timer.
    if (markdownManager) {
        markdownManager->hideAllWindows();
    }
    if (pictureManager) {
        pictureManager->hideAllWindows();
    }

    // Update current note page tracker
    currentCachedNotePage = pageNumber;

    // CRITICAL FIX: Always invalidate cache for combined pages when switching
    // This ensures we get fresh content that might have been updated by other views
    {
        QMutexLocker locker(&noteCacheMutex);
        if (pageNumber > 0) {
            noteCache.remove(pageNumber - 1); // Remove "page (n-1),n" cache
        }
    }
    {
        QMutexLocker locker(&noteCacheMutex);
        noteCache.remove(pageNumber);         // Remove "page n,(n+1)" cache
        noteCache.remove(pageNumber + 1);     // Remove "page (n+1),(n+2)" cache
    }

    // Now load fresh from disk (no cache check since we just invalidated)
    loadNotePageToCache(pageNumber);
    
    // Use the newly cached page or initialize buffer if loading failed
    bool loadedFromCache = false;
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.contains(pageNumber)) {
            buffer = *noteCache.object(pageNumber);
            loadedFromCache = true;
        }
    }
    if (!loadedFromCache) {
        initializeBuffer(); // Clear the canvas if no file exists
        loadedFromCache = false;
    }
    
    // Reset edited state when loading a new page
    edited = false;
    
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
            
                // Update cache with resized buffer
                {
                    QMutexLocker locker(&noteCacheMutex);
                    noteCache.insert(pageNumber, new QPixmap(buffer));
                }
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
                
                // Update cache with resized buffer
                {
                    QMutexLocker locker(&noteCacheMutex);
                    noteCache.insert(pageNumber, new QPixmap(buffer));
                }
            }
        } else {
            backgroundImage = QPixmap(); // No background for this page
            // Only apply device pixel ratio fix if buffer was NOT loaded from cache
            // This prevents resizing cached buffers that might already be correctly sized
            if (!loadedFromCache) {
                QScreen *screen = QGuiApplication::primaryScreen();
                qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
                QSize logicalSize = screen ? screen->size() : QSize(1440, 900);
                QSize expectedPixelSize = logicalSize * dpr;

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
    noteCache.remove(pageNumber);

    // Delete markdown windows for this page
    if (markdownManager) {
        markdownManager->deleteWindowsForPage(pageNumber);
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
    // Clear the drawing buffer
    if (buffer.isNull()) {
        initializeBuffer();
    } else {
        buffer.fill(Qt::transparent);
    }
    
    // ✅ PERMANENTLY delete markdown windows for current page
    if (markdownManager) {
        markdownManager->clearCurrentPagePermanently(lastActivePage);
    }
    
    // Clear all picture windows from current page (already deletes files permanently)
    if (pictureManager) {
        pictureManager->clearCurrentPageWindows();
    }
    
    // Mark as edited and update display
    edited = true;
    invalidateCurrentPageCache();
    update();
    
    // Auto-save the cleared page
    if (!saveFolder.isEmpty()) {
        saveToFile(lastActivePage);
    }
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
    
    // Use update() for non-blocking updates to reduce touch input lag
    // This allows the touch event to return quickly, improving responsiveness
    // Qt will coalesce multiple update() calls automatically for efficiency
    update();
    
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
    if (!touchGesturesEnabled) {
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
        
        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        const QList<QTouchEvent::TouchPoint> touchPoints = touchEvent->touchPoints();
        
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
                
                // Reset page switch cooldown
                pageSwitchInProgress = false;
                
                isPanning = true;
                isTouchPanning = true;  // Enable efficient scrolling mode
                emit touchPanningChanged(true);
                lastTouchPos = touchPoint.pos();
                
                // Store starting pan position
                touchPanStartX = panOffsetX;
                touchPanStartY = panOffsetY;
                
                // Initialize velocity tracking
                velocityTimer.start();
                recentVelocities.clear();
                lastTouchVelocity = QPointF(0, 0);
                
                // Capture current frame for efficient panning
                // Use devicePixelRatio-aware grab for better performance on low-spec devices
                cachedFrame = grab();  // Grab widget contents as pixmap
                cachedFrameOffset = QPoint(0, 0);  // Start with no offset
                
                // Pre-calculate for optimization
                touchPanStartX = panOffsetX;
                touchPanStartY = panOffsetY;
            } else if (event->type() == QEvent::TouchUpdate && isPanning) {
                QPointF delta = touchPoint.pos() - lastTouchPos;
                qint64 elapsed = velocityTimer.elapsed();
                
                // Track velocity for inertia - but only every other frame to reduce overhead
                static int velocitySampleCounter = 0;
                velocitySampleCounter++;
                if (elapsed > 0 && velocitySampleCounter % 2 == 0) {
                    QPointF velocity(delta.x() / elapsed, delta.y() / elapsed);
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
                
                // Clamp pan values when canvas is smaller than viewport
                qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                // If canvas is smaller than widget, lock pan to 0 (centered)
                if (scaledCanvasWidth < width()) {
                    newPanX = 0;
                } else {
                    // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
                    qreal maxPanX = scaledCanvasWidth - width();
                    newPanX = qBound(0.0, newPanX, maxPanX);
                }
                if (scaledCanvasHeight < height()) {
                    newPanY = 0;
                }
                
                // Use efficient scrolling during touch gesture
                int roundedPanX = qRound(newPanX);
                int roundedPanY = qRound(newPanY);
                setPanWithTouchScroll(roundedPanX, roundedPanY);
                
                lastTouchPos = touchPoint.pos();
            }
        } else if (activeTouchPoints == 2) {
            // Two finger pinch zoom
            isPanning = false;
            if (isTouchPanning) {
                isTouchPanning = false;  // Disable efficient scrolling during pinch-zoom
                emit touchPanningChanged(false);  // Notify that touch panning has ended
            } // Disable efficient scrolling during pinch-zoom
            
            const QTouchEvent::TouchPoint &touch1 = touchPoints[0];
            const QTouchEvent::TouchPoint &touch2 = touchPoints[1];
            
            // Calculate distance between touch points with higher precision
            qreal currentDist = QLineF(touch1.pos(), touch2.pos()).length();
            qreal startDist = QLineF(touch1.startPos(), touch2.startPos()).length();
            
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
                QPointF center = (touch1.pos() + touch2.pos()) / 2.0;
                
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
                qreal oldZoomFactor = zoomFactor;
                zoomFactor = newZoom;
                
                // Emit zoom change even for small changes
                emit zoomChanged(newZoom);
                
                // Clear cached frame when zoom changes during pinch gesture
                cachedFrame = QPixmap();
                cachedFrameOffset = QPoint(0, 0);
                
                // Adjust pan to keep center point fixed with sub-pixel precision
                qreal newPanX = bufferCenter.x() - adjustedCenter.x() / (internalZoomFactor / 100.0);
                qreal newPanY = bufferCenter.y() - adjustedCenter.y() / (internalZoomFactor / 100.0);
                
                // After zoom, check if we need to center
                qreal newScaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
                qreal newScaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
                
                if (newScaledCanvasWidth <= width()) {
                    newPanX = 0;
                } else {
                    // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
                    qreal maxPanX = newScaledCanvasWidth - width();
                    newPanX = qBound(0.0, newPanX, maxPanX);
                }
                if (newScaledCanvasHeight < height()) {
                    newPanY = 0;
                }
                
                emit panChanged(qRound(newPanX), qRound(newPanY));
                
                lastPinchScale = scale;
                
                // Request update only for visible area
                update();
            }
        } else {
            // More than 2 fingers - ignore
            isPanning = false;
            if (isTouchPanning) {
                isTouchPanning = false;  // Disable efficient scrolling
                emit touchPanningChanged(false);  // Notify that touch panning has ended
            }  // Disable efficient scrolling
        }
        
        if (event->type() == QEvent::TouchEnd) {
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
                    inertiaPanX = panOffsetX;
                    inertiaPanY = panOffsetY;
                    
                    // Keep the cached frame for inertia animation
                    // Don't clear isTouchPanning yet - inertia will use it
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
    
    // Clamp pan values
    qreal scaledCanvasWidth = buffer.width() * (internalZoomFactor / 100.0);
    qreal scaledCanvasHeight = buffer.height() * (internalZoomFactor / 100.0);
    
    if (scaledCanvasWidth <= width()) {
        inertiaPanX = 0;
    } else {
        // ✅ Enforce pan X range: [0, scaledCanvasWidth - width()]
        qreal maxPanX = scaledCanvasWidth - width();
        inertiaPanX = qBound(0.0, inertiaPanX, maxPanX);
    }
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
        
        // Mark as edited since we deleted content
        if (!edited) {
            edited = true;
            // Invalidate cache for current page since it's been modified
            invalidateCurrentPageCache();
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
        
        // Update the restored area
        update(updateRect.adjusted(-5, -5, 5, 5));
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
            // Invalidate cache for current page since it's been modified
            invalidateCurrentPageCache();
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
    qDeleteAll(currentPdfTextBoxes);
    currentPdfTextBoxes.clear();
    currentPdfTextBoxPageNumbers.clear();
    
    if (!pdfDocument || pageNumber < 0 || pageNumber >= pdfDocument->numPages()) {
        return;
    }

    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }

    if (isCombinedCanvas) {
        // For combined canvas, load text boxes for both pages
        loadPdfTextBoxesForCombinedCanvas(pageNumber, singlePageHeight);
    } else {
        // For single page canvas, load normally
        loadPdfTextBoxesForSinglePage(pageNumber);
    }
}

void InkCanvas::loadPdfTextBoxesForSinglePage(int pageNumber) {
    // Get the page for text operations
    currentPdfPageForText = std::unique_ptr<Poppler::Page>(pdfDocument->page(pageNumber));
    if (!currentPdfPageForText) {
        return;
    }

    // Load text boxes for the single page
    auto textBoxVector = currentPdfPageForText->textList();
    for (auto textBox : textBoxVector) {
        currentPdfTextBoxes.append(textBox); // Qt5 returns raw pointers
        currentPdfTextBoxPageNumbers.append(pageNumber); // Track page number for each text box
    }
}

void InkCanvas::loadPdfTextBoxesForCombinedCanvas(int pageNumber, int singlePageHeight) {
    // For combined canvas showing pages N and N+1:
    // - Top half shows page N (coordinates as-is)
    // - Bottom half shows page N+1 (coordinates will be adjusted in mapping functions)
    
    // Load text boxes for top half (page N)
    currentPdfPageForText = std::unique_ptr<Poppler::Page>(pdfDocument->page(pageNumber));
    if (currentPdfPageForText) {
        auto topTextBoxVector = currentPdfPageForText->textList();
        for (auto textBox : topTextBoxVector) {
            // Text boxes for top half keep their original coordinates
            currentPdfTextBoxes.append(textBox); // Qt5 returns raw pointers
            currentPdfTextBoxPageNumbers.append(pageNumber); // Track as page N
        }
    }
    
    // Load text boxes for bottom half (page N+1) if it exists
    int nextPageNumber = pageNumber + 1;
    if (nextPageNumber < pdfDocument->numPages()) {
        currentPdfPageForTextSecond = std::unique_ptr<Poppler::Page>(pdfDocument->page(nextPageNumber));
        if (currentPdfPageForTextSecond) {
            auto bottomTextBoxVector = currentPdfPageForTextSecond->textList();
            for (auto textBox : bottomTextBoxVector) {
                // Text boxes for bottom half - coordinates will be adjusted in mapping functions
                currentPdfTextBoxes.append(textBox); // Qt5 returns raw pointers
                currentPdfTextBoxPageNumbers.append(nextPageNumber); // Track as page N+1
            }
        }
    }
}

QPointF InkCanvas::mapWidgetToPdfCoordinates(const QPointF &widgetPoint) {
    if (!currentPdfPageForText || backgroundImage.isNull()) {
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
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    // Determine which page and adjust coordinates for combined canvas
    std::unique_ptr<Poppler::Page>* targetPage = &currentPdfPageForText;
    QPointF finalAdjustedPoint = adjustedPoint;
    
    if (isCombinedCanvas && currentPdfPageForTextSecond) {
        // Check if the point is in the bottom half (page N+1)
        if (adjustedPoint.y() >= singlePageHeight) {
            // Bottom half - use second page and adjust coordinates
            targetPage = &currentPdfPageForTextSecond;
            finalAdjustedPoint.setY(adjustedPoint.y() - singlePageHeight); // Shift back to page coordinate system
        }
        // Top half - use first page with original coordinates
    }
    
    // Convert to PDF coordinates
    QSizeF pdfPageSize = (*targetPage)->pageSizeF();
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
    if (!currentPdfPageForText || backgroundImage.isNull()) {
        return QPointF();
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    // Determine which page to use and coordinate adjustments
    std::unique_ptr<Poppler::Page>* sourcePage = &currentPdfPageForText;
    qreal yOffset = 0;
    
    if (isCombinedCanvas && pageNumber != -1) {
        // Find the base page number (first page of the combined canvas)
        int basePage = -1;
        if (!currentPdfTextBoxPageNumbers.empty()) {
            basePage = currentPdfTextBoxPageNumbers[0];
        }
        
        if (pageNumber > basePage && currentPdfPageForTextSecond) {
            // This text box belongs to the second page (bottom half)
            sourcePage = &currentPdfPageForTextSecond;
            yOffset = singlePageHeight; // Shift down to bottom half
        }
        // Otherwise, use first page (top half) with no offset
    }
    
    // Convert from PDF coordinates to image coordinates
    QSizeF pdfPageSize = (*sourcePage)->pageSizeF();
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

void InkCanvas::updatePdfTextSelection(const QPointF &start, const QPointF &end) {
    // Early return if PDF is not loaded or no text boxes available
    if (!isPdfLoaded || currentPdfTextBoxes.isEmpty()) {
        return;
    }

    // Clear previous selection efficiently
    selectedTextBoxes.clear();
    
    // Create normalized selection rectangle in widget coordinates
    QRectF widgetSelectionRect(start, end);
    widgetSelectionRect = widgetSelectionRect.normalized();
    
    // Convert to PDF coordinate space
    QPointF pdfTopLeft = mapWidgetToPdfCoordinates(widgetSelectionRect.topLeft());
    QPointF pdfBottomRight = mapWidgetToPdfCoordinates(widgetSelectionRect.bottomRight());
    QRectF pdfSelectionRect(pdfTopLeft, pdfBottomRight);
    pdfSelectionRect = pdfSelectionRect.normalized();
    
    // Reserve space for efficiency if we expect many selections
    selectedTextBoxes.reserve(qMin(currentPdfTextBoxes.size(), 50));
    
    // Find intersecting text boxes with optimized loop
    for (const Poppler::TextBox* textBox : currentPdfTextBoxes) {
        if (textBox && textBox->boundingBox().intersects(pdfSelectionRect)) {
            selectedTextBoxes.append(const_cast<Poppler::TextBox*>(textBox));
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
    
    if (!currentPdfPageForText) {
        // qDebug() << "PDF text selection: No current page for text";
        return selectedBoxes;
    }
    
    // Convert widget coordinates to PDF coordinates
    QPointF pdfStart = mapWidgetToPdfCoordinates(start);
    QPointF pdfEnd = mapWidgetToPdfCoordinates(end);
    
    // qDebug() << "PDF text selection: Widget coords" << start << "to" << end;
    // qDebug() << "PDF text selection: PDF coords" << pdfStart << "to" << pdfEnd;
    
    // Create selection rectangle in PDF coordinates
    QRectF selectionRect(pdfStart, pdfEnd);
    selectionRect = selectionRect.normalized();
    
    // qDebug() << "PDF text selection: Selection rect in PDF coords:" << selectionRect;
    
    // Find text boxes that intersect with the selection
    int intersectionCount = 0;
    for (Poppler::TextBox* textBox : currentPdfTextBoxes) {
        if (textBox) {
            QRectF textBoxRect = textBox->boundingBox();
            bool intersects = textBoxRect.intersects(selectionRect);
            
            if (intersects) {
                selectedBoxes.append(textBox);
                intersectionCount++;
                // qDebug() << "PDF text selection: Text box intersects:" << textBox->text() 
                //          << "at" << textBoxRect;
            }
        }
    }
    
    // qDebug() << "PDF text selection: Found" << intersectionCount << "intersecting text boxes";
    
    return selectedBoxes;
}

void InkCanvas::handlePdfLinkClick(const QPointF &position) {
    if (!isPdfLoaded || !currentPdfPageForText) {
        return;
    }

    // Convert widget coordinates to PDF coordinates
    QPointF pdfPoint = mapWidgetToPdfCoordinates(position);
    
    // Get PDF page size for reference
    QSizeF pdfPageSize = currentPdfPageForText->pageSizeF();
    
    // Convert to normalized coordinates (0.0 to 1.0) to match Poppler's link coordinate system
    QPointF normalizedPoint(pdfPoint.x() / pdfPageSize.width(), pdfPoint.y() / pdfPageSize.height());
    
    // Get links for the current page
    auto links = currentPdfPageForText->links();
    
    for (const auto& link : links) {
        QRectF linkArea = link->linkArea();
        
        // Normalize the rectangle to handle negative width/height
        QRectF normalizedLinkArea = linkArea.normalized();
        
        // Check if the normalized rectangle contains the normalized point
        if (normalizedLinkArea.contains(normalizedPoint)) {
            // Handle different types of links
            if (link->linkType() == Poppler::Link::Goto) {
                Poppler::LinkGoto* gotoLink = static_cast<Poppler::LinkGoto*>(link); // Qt5 uses raw pointers
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
    copyAction->setIcon(QIcon(":/resources/icons/copy.png")); // You may need to add this icon
    connect(copyAction, &QAction::triggered, this, [selectedText]() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(selectedText);
    });
    
    // Add separator
    contextMenu->addSeparator();
    
    // Add Cancel action
    QAction *cancelAction = contextMenu->addAction(tr("Cancel"));
    cancelAction->setIcon(QIcon(":/resources/icons/cross.png"));
    connect(cancelAction, &QAction::triggered, this, [this]() {
        clearPdfTextSelection();
    });
    
    // Show the menu at the specified position
    contextMenu->popup(position);
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

void InkCanvas::renderPdfPageToCache(int pageNumber) {
    if (!pdfDocument || !isValidPageNumber(pageNumber)) {
        return;
    }

    // Check if already cached and manage cache size (thread-safe)
    {
        QMutexLocker locker(&pdfCacheMutex);
        if (pdfCache.contains(pageNumber)) {
            return;
        }
        
        // Ensure the cache holds only 6 pages max
        if (pdfCache.count() >= 6) {
            auto oldestKey = pdfCache.keys().first();
            pdfCache.remove(oldestKey);
        }
    }
    
    // Render current page
    std::unique_ptr<Poppler::Page> currentPage(pdfDocument->page(pageNumber));
    if (!currentPage) {
        return;
    }
    
    QImage currentPageImage = currentPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
    if (currentPageImage.isNull()) {
        return;
    }
    
    // Try to render next page for combination
    QImage nextPageImage;
    int nextPageNumber = pageNumber + 1;
    if (isValidPageNumber(nextPageNumber)) {
        std::unique_ptr<Poppler::Page> nextPage(pdfDocument->page(nextPageNumber));
        if (nextPage) {
            nextPageImage = nextPage->renderToImage(pdfRenderDPI, pdfRenderDPI);
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
        QPixmap cachedPixmap = QPixmap::fromImage(combinedImage);
        {
            QMutexLocker locker(&pdfCacheMutex);
            pdfCache.insert(pageNumber, new QPixmap(cachedPixmap));
        }
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
    
    // Cache pages asynchronously
    for (int pageNum : pagesToCache) {
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
        
        // Track the watcher for cleanup
        activePdfWatchers.append(watcher);
        
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            // Remove from active list and delete
            activePdfWatchers.removeOne(watcher);
            watcher->deleteLater();
        });
        
        // Capture pageNum by value to avoid issues with lambda capture
        QFuture<void> future = QtConcurrent::run([this, pageNum]() {
            renderPdfPageToCache(pageNum);
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

void InkCanvas::loadNotePageToCache(int pageNumber) {
    // Check if already cached (thread-safe)
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.contains(pageNumber)) {
            return;
        }
    }
    
    QString currentFilePath = getNotePageFilePath(pageNumber);
    if (currentFilePath.isEmpty()) {
        return;
    }
    
    // Ensure the cache doesn't exceed its limit (thread-safe)
    {
        QMutexLocker locker(&noteCacheMutex);
        if (noteCache.count() >= 6) {
            // QCache will automatically remove least recently used items
            // but we can be explicit about it
            auto keys = noteCache.keys();
            if (!keys.isEmpty()) {
                noteCache.remove(keys.first());
            }
        }
    }
    
    // Load current page canvas
    QPixmap currentPageCanvas;
    bool currentExists = false;
    if (QFile::exists(currentFilePath)) {
        if (currentPageCanvas.load(currentFilePath)) {
            currentExists = true;
        }
    }
    
    // Load next page canvas for combination
    QPixmap nextPageCanvas;
    bool nextExists = false;
    int nextPageNumber = pageNumber + 1;
    QString nextFilePath = getNotePageFilePath(nextPageNumber);
    if (!nextFilePath.isEmpty() && QFile::exists(nextFilePath)) {
        if (nextPageCanvas.load(nextFilePath)) {
            nextExists = true;
        }
    }
    
    // Create combined canvas
    QPixmap combinedCanvas;
    if (currentExists || nextExists) {
        // Determine the size for the combined canvas
        int combinedWidth = 0;
        int combinedHeight = 0;
        
        if (currentExists && nextExists) {
            // Both pages exist - combine them vertically
            combinedWidth = qMax(currentPageCanvas.width(), nextPageCanvas.width());
            combinedHeight = currentPageCanvas.height() + nextPageCanvas.height();
        } else if (currentExists) {
            // Only current page exists - create double height with current page on top
            combinedWidth = currentPageCanvas.width();
            combinedHeight = currentPageCanvas.height() * 2;
        } else {
            // Only next page exists - create double height with empty space on top
            combinedWidth = nextPageCanvas.width();
            combinedHeight = nextPageCanvas.height() * 2;
        }
        
        // Create the combined canvas
        combinedCanvas = QPixmap(combinedWidth, combinedHeight);
        combinedCanvas.fill(Qt::transparent);
        
        QPainter painter(&combinedCanvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        // Draw current page at the top
        if (currentExists) {
            painter.drawPixmap(0, 0, currentPageCanvas);
        }
        
        // Draw next page below current page
        if (nextExists) {
            int yOffset = currentExists ? currentPageCanvas.height() : nextPageCanvas.height();
            painter.drawPixmap(0, yOffset, nextPageCanvas);
        }
        
        painter.end();
        
        // Cache the combined canvas (thread-safe)
        {
            QMutexLocker locker(&noteCacheMutex);
            noteCache.insert(pageNumber, new QPixmap(combinedCanvas));
        }
    }
    // If neither file exists, we don't cache anything - loadPage will handle initialization
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
            loadNotePageToCache(pageNum);
        });
        
        watcher->setFuture(future);
    }
}

void InkCanvas::invalidateCurrentPageCache() {
    if (currentCachedNotePage >= 0) {
        QMutexLocker locker(&noteCacheMutex);
        noteCache.remove(currentCachedNotePage);
    }
}

QList<MarkdownWindow*> InkCanvas::loadMarkdownWindowsForPage(int pageNumber) {
    if (!markdownManager) {
        return QList<MarkdownWindow*>();
    }
    
    // Load windows for the specified page without affecting current windows
    return markdownManager->loadWindowsForPageSeparately(pageNumber);
}

QList<PictureWindow*> InkCanvas::loadPictureWindowsForPage(int pageNumber) {
    if (!pictureManager) {
        return QList<PictureWindow*>();
    }
    
    // Load windows for the specified page without affecting current windows
    return pictureManager->loadWindowsForPageSeparately(pageNumber);
}

void InkCanvas::loadCombinedWindowsForPage(int pageNumber) {
    if (!markdownManager && !pictureManager) {
        return;
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (isCombinedCanvas) {
        // For combined canvas, we need to load and merge windows from both current and next page
        int nextPageNumber = pageNumber + 1;
        
        
        // For combined canvas showing pages N and N+1:
        // - Top half shows page N
        // - Bottom half shows page N+1
        
        // CORRECTED LOGIC: For page "N-(N+1)" view, we need:
        // - Top half: Page N windows (no adjustment needed - they were saved with top-half coordinates)  
        // - Bottom half: Page N+1 windows (adjust by +singlePageHeight)
        
        // Load page N windows for top half using separate method to avoid interference
        QList<MarkdownWindow*> topHalfMarkdownWindows;
        QList<PictureWindow*> topHalfPictureWindows;
        
        if (markdownManager) {
            topHalfMarkdownWindows = loadMarkdownWindowsForPage(pageNumber);
            // These windows were saved with their original coordinates and appear in top half as-is
        }
        
        if (pictureManager) {
            topHalfPictureWindows = loadPictureWindowsForPage(pageNumber);
            // These windows were saved with their original coordinates and appear in top half as-is
        }
        
        // Load page N+1 windows for bottom half and adjust their coordinates
        QList<MarkdownWindow*> bottomHalfMarkdownWindows;
        QList<PictureWindow*> bottomHalfPictureWindows;
        
        if (markdownManager) {
            bottomHalfMarkdownWindows = loadMarkdownWindowsForPage(nextPageNumber);
            // Move page N+1 windows to bottom half by adding singlePageHeight
            for (MarkdownWindow* window : bottomHalfMarkdownWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() + singlePageHeight);
                window->setCanvasRect(rect);
            }
        }
        
        if (pictureManager) {
            bottomHalfPictureWindows = loadPictureWindowsForPage(nextPageNumber);
            // Move page N+1 windows to bottom half by adding singlePageHeight
            for (PictureWindow* window : bottomHalfPictureWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() + singlePageHeight);
                window->setCanvasRect(rect);
            }
        }
        
        // Combine both sets of windows and set as current
        if (markdownManager) {
            QList<MarkdownWindow*> combinedMarkdownWindows = topHalfMarkdownWindows + bottomHalfMarkdownWindows;
            markdownManager->setCombinedWindows(combinedMarkdownWindows);
        }
        
        if (pictureManager) {
            QList<PictureWindow*> combinedPictureWindows = topHalfPictureWindows + bottomHalfPictureWindows;
            pictureManager->setCombinedWindows(combinedPictureWindows);
        }
        
    } else {
        // Standard single page window loading
        if (markdownManager) {
            markdownManager->loadWindowsForPage(pageNumber);
        }
        if (pictureManager) {
            pictureManager->loadWindowsForPage(pageNumber);
        }
    }
}

void InkCanvas::saveCombinedWindowsForPage(int pageNumber) {
    if (!markdownManager && !pictureManager) {
        return;
    }
    
    // Check if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = buffer.height();
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) {
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (isCombinedCanvas) {
        // For combined canvas, we need to save windows based on their positions
        // Windows in the top half belong to current page, bottom half to next page
        
        // Get all current windows
        QList<MarkdownWindow*> allMarkdownWindows = markdownManager ? markdownManager->getCurrentPageWindows() : QList<MarkdownWindow*>();
        QList<PictureWindow*> allPictureWindows = pictureManager ? pictureManager->getCurrentPageWindows() : QList<PictureWindow*>();
        
        // Separate windows by position
        QList<MarkdownWindow*> currentPageMarkdownWindows;
        QList<MarkdownWindow*> nextPageMarkdownWindows;
        QList<PictureWindow*> currentPagePictureWindows;
        QList<PictureWindow*> nextPagePictureWindows;
        
        // Separate markdown windows by position
        for (MarkdownWindow* window : allMarkdownWindows) {
            QRect rect = window->getCanvasRect();
            if (rect.top() < singlePageHeight) {
                // Window starts in top half (current page)
                currentPageMarkdownWindows.append(window);
            } else {
                // Window starts in bottom half (next page)
                nextPageMarkdownWindows.append(window);
            }
        }
        
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
        if (markdownManager) {
            markdownManager->saveWindowsForPageSeparately(pageNumber, currentPageMarkdownWindows);
        }
        if (pictureManager) {
            pictureManager->saveWindowsForPageSeparately(pageNumber, currentPagePictureWindows);
        }
        
        // Save next page windows (bottom half, with adjusted coordinates)
        if (!nextPageMarkdownWindows.isEmpty() && markdownManager) {
            // Temporarily adjust coordinates for saving
            for (MarkdownWindow* window : nextPageMarkdownWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() - singlePageHeight); // Move to top half coordinates
                window->setCanvasRect(rect);
            }
            markdownManager->saveWindowsForPageSeparately(pageNumber + 1, nextPageMarkdownWindows);
            // Restore original coordinates
            for (MarkdownWindow* window : nextPageMarkdownWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() + singlePageHeight); // Move back to bottom half
                window->setCanvasRect(rect);
            }
        }
        
        if (!nextPagePictureWindows.isEmpty() && pictureManager) {
            // Temporarily adjust coordinates for saving
            for (PictureWindow* window : nextPagePictureWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() - singlePageHeight); // Move to top half coordinates
                window->setCanvasRect(rect);
            }
            pictureManager->saveWindowsForPageSeparately(pageNumber + 1, nextPagePictureWindows);
            // Restore original coordinates
            for (PictureWindow* window : nextPagePictureWindows) {
                QRect rect = window->getCanvasRect();
                rect.moveTop(rect.y() + singlePageHeight); // Move back to bottom half
                window->setCanvasRect(rect);
            }
        }
        
    } else {
        // Standard single page window saving
        if (markdownManager) {
            markdownManager->saveWindowsForPage(pageNumber);
        }
        if (pictureManager) {
            pictureManager->saveWindowsForPage(pageNumber);
        }
    }
}

// Markdown integration methods
void InkCanvas::setMarkdownSelectionMode(bool enabled) {
    markdownSelectionMode = enabled;
    
    if (markdownManager) {
        markdownManager->setSelectionMode(enabled);
    }
    
    if (!enabled) {
        markdownSelecting = false;
    }
    
    // Update cursor
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    
    // Notify signal
    emit markdownSelectionModeChanged(enabled);
}

bool InkCanvas::isMarkdownSelectionMode() const {
    return markdownSelectionMode;
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
    
    // Load bookmarks
    bookmarks.clear();
    QJsonArray bookmarkArray = obj["bookmarks"].toArray();
    for (const QJsonValue &value : bookmarkArray) {
        bookmarks.append(value.toString());
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
    
    // Save bookmarks
    QJsonArray bookmarkArray;
    for (const QString &bookmark : bookmarks) {
        bookmarkArray.append(bookmark);
    }
    obj["bookmarks"] = bookmarkArray;
    
    QJsonDocument doc(obj);
    
    QString metadataFile = saveFolder + "/.speedynote_metadata.json";
    QFile file(metadataFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson());
        file.close();
    }
    
    // ✅ Sync changes to .spn package if needed
    syncSpnPackage();
}

void InkCanvas::setLastAccessedPage(int pageNumber) {
    lastAccessedPage = pageNumber;
    saveNotebookMetadata(); // Auto-save when page changes
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

void InkCanvas::handlePictureMouseMove(QMouseEvent *event) {
    if (!activePictureWindow) return;
    
    // Convert current mouse position to canvas coordinates
    QPointF currentWidgetPos = event->pos();
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
    
    // ✅ CACHE FIX: Invalidate note cache when pictures are moved/resized
    // This ensures that when switching pages and coming back, the updated picture positions are shown
    invalidateCurrentPageCache();
    
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
    } else if (buffer.height() > 1400) { 
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (!isCombinedCanvas || singlePageHeight == 0) {
        return; 
    }
    
    // During inertia scrolling, enforce cooldown to prevent rapid page switches
    if (isTouchPanning && inertiaTimer && inertiaTimer->isActive()) {
        // Check if we're in cooldown period (500ms after last page switch)
        if (pageSwitchInProgress && pageSwitchCooldown.isValid() && pageSwitchCooldown.elapsed() < 500) {
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
        emit autoScrollRequested(1); // 1 for forward
        
        // Stop inertia during page switch to allow proper pan reset
        if (isTouchPanning && inertiaTimer && inertiaTimer->isActive()) {
            inertiaTimer->stop();
            isTouchPanning = false;
            emit touchPanningChanged(false);  // Notify that touch panning has ended
            pageSwitchInProgress = true;
            pageSwitchCooldown.start();
            
            // Clear cached frame - page switch will load new content
            cachedFrame = QPixmap();
            cachedFrameOffset = QPoint(0, 0);
        }
    }
    
    // Check for backward autoscroll (scrolling up past -300 for delayed switching)
    // This gives more time for the save operation to complete on slower devices
    if (oldPanY > backwardSwitchThreshold && newPanY <= backwardSwitchThreshold) {
        emit autoScrollRequested(-1); // -1 for backward (delayed until -300)
        
        // Stop inertia during page switch to allow proper pan reset
        if (isTouchPanning && inertiaTimer && inertiaTimer->isActive()) {
            inertiaTimer->stop();
            isTouchPanning = false;
            emit touchPanningChanged(false);  // Notify that touch panning has ended
            pageSwitchInProgress = true;
            pageSwitchCooldown.start();
            
            // Clear cached frame - page switch will load new content
            cachedFrame = QPixmap();
            cachedFrameOffset = QPoint(0, 0);
        }
    }
}

int InkCanvas::getAutoscrollThreshold() const {
    // Detect if this is a combined canvas
    bool isCombinedCanvas = false;
    int singlePageHeight = 0;
    
    if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8 && backgroundImage.height() > 0) {
        isCombinedCanvas = true;
        singlePageHeight = backgroundImage.height() / 2;
    } else if (buffer.height() > 1400) { 
        isCombinedCanvas = true;
        singlePageHeight = buffer.height() / 2;
    }
    
    if (!isCombinedCanvas || singlePageHeight == 0) {
        return 0; // Return 0 for non-combined canvases
    }
    
    return singlePageHeight;
}





