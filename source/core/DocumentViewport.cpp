// ============================================================================
// DocumentViewport - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.1)
// ============================================================================

#include "DocumentViewport.h"
#include "TouchGestureHandler.h"
// Note: ShortcutManager.h no longer needed here - all shortcuts handled by MainWindow
#include "MarkdownNote.h"           // Phase M.2: For markdown note creation
#include "../layers/VectorLayer.h"
#include "../pdf/PdfProvider.h"     // Use abstract interface, not concrete impl
#include "../objects/LinkObject.h"  // Phase C.2.3: For cloneWithBackLink
#include "../ui/banners/MissingPdfBanner.h"  // Phase R.3: Missing PDF notification

#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTouchEvent>
#include <QtMath>     // For qPow
#include <QtConcurrent>  // For async PDF rendering
#include <cmath>      // For std::floor, std::ceil
#include <algorithm>  // For std::remove_if
#include <limits>
#include <climits>    // For INT_MIN (Phase O3.5.5: affinity filtering)
#include <QDateTime>  // For timestamp
#include <QUuid>      // For stroke IDs
#include <QSet>       // For efficient ID lookup in eraseAt
#include <QClipboard>     // For clipboard access (O2.4)
#include <QGuiApplication> // For clipboard access (O2.4)
#include <QApplication>    // For focusWidget() - text input focus check
#include <QLineEdit>       // For text input focus check
#include <QTextEdit>       // For text input focus check
#include <QPlainTextEdit>  // For text input focus check
#include <QElapsedTimer>  // For double/triple click detection (Phase A)
#include <QMimeData>      // For clipboard content type check (O2.4)

#ifdef Q_OS_ANDROID
#include <QJniObject>       // BUG-A008: JNI for eraser tool type detection
#include <QJniEnvironment>  // BUG-A008: For cached JNI method calls
#endif
#include <QFileDialog>    // For insertImageFromDialog (Phase C.0.5)
#include <QDesktopServices>  // For opening URLs (Phase C.4.3)
#include <QUrl>              // For URL handling (Phase C.4.3)
#include <QMenu>             // For addLinkToSlot menu (Phase C.5.3 - temporary)
#include <QInputDialog>      // For URL input dialog (Phase C.5.3 - temporary)

// ===== Constants =====

// PDF uses 72 DPI, Page uses 96 DPI - scale factor for coordinate conversion
static constexpr qreal PDF_TO_PAGE_SCALE = 96.0 / 72.0;  // PDF coords → Page coords
static constexpr qreal PAGE_TO_PDF_SCALE = 72.0 / 96.0;  // Page coords → PDF coords

// Note: eventMatchesAction() helper was removed - all keyboard shortcuts
// are now handled by MainWindow's QShortcut system for focus-independent operation.

// ===== Constructor & Destructor =====

DocumentViewport::DocumentViewport(QWidget* parent)
    : QWidget(parent)
{
    // Enable mouse tracking for hover effects (future)
    setMouseTracking(true);
    
    // Accept tablet events
    setAttribute(Qt::WA_TabletTracking, true);
    
    // Enable touch events for touch gesture support (pan, zoom)
    // Note: Touch-synthesized mouse events are still rejected in mouse handlers
    // to prevent touch from triggering drawing (drawing is stylus/mouse only)
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    
    // Set focus policy for keyboard shortcuts
    setFocusPolicy(Qt::StrongFocus);
    
    // Set background color (will be painted over by pages)
    // CUSTOMIZABLE: Viewport background color (theme setting, visible in gaps between pages)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_backgroundColor);  // Uses cached background color
    setPalette(pal);
    
    // Benchmark display timer - triggers repaint to update paint rate counter
    // Note: Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
    connect(&m_benchmarkDisplayTimer, &QTimer::timeout, this, [this]() {
        if (m_benchmarking) {
            // DebugOverlay widget handles its own updates, but we may want
            // to trigger viewport repaints for accurate paint rate measurement
            // during benchmarking (disabled for now to avoid unnecessary repaints)
        }
    });
    
    // PDF preload timer - debounces preload requests during rapid scrolling
    m_pdfPreloadTimer = new QTimer(this);
    m_pdfPreloadTimer->setSingleShot(true);
    connect(m_pdfPreloadTimer, &QTimer::timeout, this, &DocumentViewport::doAsyncPdfPreload);
    
    // Gesture timeout timer - fallback for detecting gesture end (zoom or pan)
    m_gestureTimeoutTimer = new QTimer(this);
    m_gestureTimeoutTimer->setSingleShot(true);
    connect(m_gestureTimeoutTimer, &QTimer::timeout, this, &DocumentViewport::onGestureTimeout);
    
    // Touch gesture handler (encapsulates pan/zoom/tap logic)
    m_touchHandler = new TouchGestureHandler(this, this);
    
#ifdef Q_OS_ANDROID
    // Handle app suspend/resume (screen lock, home button, etc.)
    // Resets touch state when app returns to foreground to fix gesture reliability
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &DocumentViewport::onApplicationStateChanged);
#endif
    
    // Tablet hover timer - detects when stylus leaves viewport by timeout
    // When stylus hovers to another widget, we stop receiving TabletMove events.
    // This timer fires if no tablet hover event received within the interval.
    m_tabletHoverTimer = new QTimer(this);
    m_tabletHoverTimer->setSingleShot(true);
    m_tabletHoverTimer->setInterval(100);  // 100ms - short enough to feel responsive
    connect(m_tabletHoverTimer, &QTimer::timeout, this, [this]() {
        // No tablet hover event received - stylus must have left
        if (m_pointerInViewport && !m_pointerActive) {
            m_pointerInViewport = false;
            
            // Trigger repaint to hide eraser cursor
            // Use elliptical region to match circular cursor shape
            // Use toAlignedRect() to properly round floating-point to integer coords
            if (m_currentTool == ToolType::Eraser || m_hardwareEraserActive) {
                qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
                QRectF cursorRectF(m_lastPointerPos.x() - eraserRadius, m_lastPointerPos.y() - eraserRadius,
                                   eraserRadius * 2, eraserRadius * 2);
                update(QRegion(cursorRectF.toAlignedRect(), QRegion::Ellipse));
            }
        }
    });
    
    // Initialize PDF cache capacity based on default layout mode
    updatePdfCacheCapacity();
}

DocumentViewport::~DocumentViewport()
{
    // Cancel any pending preload requests
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->stop();
    }
    
    // Stop gesture timer
    if (m_gestureTimeoutTimer) {
        m_gestureTimeoutTimer->stop();
    }
    
    // Stop tablet hover timer (prevents lambda firing during destruction)
    if (m_tabletHoverTimer) {
        m_tabletHoverTimer->stop();
    }
    
    // Stop touch handler gestures (including inertia timer)
    // Must happen before m_gesture.reset() to avoid accessing stale gesture state
    if (m_touchHandler) {
        m_touchHandler->setMode(TouchGestureMode::Disabled);
    }
    
    // Clear gesture cached frame (releases memory)
    m_gesture.reset();
    
    // Wait for and clean up any active async PDF watchers
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_activePdfWatchers.clear();
    
    // ========== MEMORY FIX: Explicit cache cleanup ==========
    // While these should be cleaned up automatically by member destructors,
    // explicitly clearing them before destruction ensures:
    // 1. Qt's implicit sharing is broken before any other cleanup
    // 2. Large allocations are freed in a deterministic order
    // 3. Any circular references are broken
    
    // Clear PDF cache (can be several MB for multi-page documents)
    {
        QMutexLocker locker(&m_pdfCacheMutex);
        m_pdfCache.clear();
        m_pdfCache.squeeze();  // Release excess capacity
    }
    
    // Clear selection/drag snapshot caches (can be full viewport-sized pixmaps)
    m_selectionBackgroundSnapshot = QPixmap();
    m_objectDragBackgroundSnapshot = QPixmap();
    m_dragObjectRenderedCache = QPixmap();
    
    // Clear stroke rendering caches
    m_selectionStrokeCache = QPixmap();
    m_lassoPathCache = QPixmap();
    m_currentStrokeCache = QPixmap();
    
    // Clear text/link caches
    m_textBoxCache.clear();
    m_textBoxCache.squeeze();
    m_linkCache.clear();
    m_linkCache.squeeze();
    
    // Clear undo/redo stacks (can hold stroke data)
    m_undoStacks.clear();
    m_redoStacks.clear();
    m_edgelessUndoStack.clear();
    m_edgelessRedoStack.clear();
    
    // Clear page layout cache
    m_pageYCache.clear();
    m_pageYCache.squeeze();
    
    // Clear document pointer to prevent any dangling access
    m_document = nullptr;
}

// ===== Document Management =====

void DocumentViewport::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    // End any active gesture (cached frame is from old document)
    if (m_gesture.isActive()) {
        m_gesture.reset();
        m_gestureTimeoutTimer->stop();
    }
    m_backtickHeld = false;  // Reset key tracking for new document
    
    // Clear object selection (pointers refer to old document's objects)
    // Must be done BEFORE changing m_document to avoid dangling pointer access
    bool hadSelection = !m_selectedObjects.isEmpty();
    m_selectedObjects.clear();
    m_hoveredObject = nullptr;
    m_isDraggingObjects = false;
    m_isResizingObject = false;
    
    // Clear undo/redo stacks (actions refer to old document)
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    m_undoStacks.clear();
    m_redoStacks.clear();
    m_edgelessUndoStack.clear();
    m_edgelessRedoStack.clear();
    
    m_document = doc;
    
    // Emit selection changed signal after document change
    if (hadSelection) {
        emit objectSelectionChanged();
    }
    
    // Emit signals if undo/redo availability changed
    if (hadUndo) emit undoAvailableChanged(false);
    if (hadRedo) emit redoAvailableChanged(false);
    
    // Invalidate caches for new document
    invalidatePdfCache();
    invalidatePageLayoutCache();
    
    // Phase A: Clear text selection (refers to old document's text boxes)
    bool hadTextSelection = m_textSelection.isValid();
    m_textSelection.clear();
    if (hadTextSelection) {
        emit textSelectionChanged(false);
    }
    clearTextBoxCache();
    clearLinkCache();  // Phase D.1
    
    // Reset view state
    m_zoomLevel = 1.0;
    m_panOffset = QPointF(0, 0);
    m_currentPageIndex = 0;
    m_needsPositionRestore = false;  // Reset deferred restore flag for new document
    m_edgelessPositionHistory.clear();  // Clear old position history for new document
    
    // Track if we need to defer update for edgeless position restore
    bool deferUpdateForEdgeless = false;
    
    // If document exists, restore last accessed page/position or set initial view
    if (m_document) {
        if (m_document->isEdgeless()) {
            // Phase 4: Restore edgeless position from document
            QPointF lastPos = m_document->edgelessLastPosition();
            
            // If there's a saved position, defer update and restore in showEvent
            // This ensures the first paint uses the correct pan offset
            if (!lastPos.isNull()) {
                deferUpdateForEdgeless = true;
                // NOTE: We can't calculate the correct pan offset here because
                // width() and height() may not be valid yet. Just set the flag
                // and let showEvent do the proper restore.
            }
            
            // If widget is already visible with valid dimensions, restore now
            // Otherwise mark for restore in showEvent/resizeEvent
            if (isVisible() && width() > 0 && height() > 0) {
                // Widget is visible with valid dimensions - restore now
                applyRestoredEdgelessPosition();
                // Don't set flag - we already restored
            } else {
                // Widget not yet visible - restore in showEvent/resizeEvent
                m_needsPositionRestore = true;
            }
        } else if (m_document->lastAccessedPage > 0) {
            m_currentPageIndex = qMin(m_document->lastAccessedPage, 
                                       m_document->pageCount() - 1);
            
            // Defer scrollToPage to next event loop iteration
            // This ensures the widget has correct dimensions before calculating scroll position
            if (m_currentPageIndex > 0) {
                QTimer::singleShot(0, this, [this, pageToRestore = m_currentPageIndex]() {
                    if (m_document && pageToRestore < m_document->pageCount()) {
                        scrollToPage(pageToRestore);
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Restored last accessed page:" << pageToRestore;
#endif
                    }
                });
            }
        } else {
            // New paged document: zoom to fit page width
            // Deferred to ensure widget has correct dimensions
            QTimer::singleShot(0, this, [this]() {
                if (m_document && !m_document->isEdgeless()) {
                    zoomToWidth();
                }
            });
        }
    }
    
    // Trigger repaint (skip for edgeless with saved position - restore will trigger it)
    if (!deferUpdateForEdgeless) {
        update();
    }
    
    // Emit signals
    emit zoomChanged(m_zoomLevel);
    emit panChanged(m_panOffset);
    emit currentPageChanged(m_currentPageIndex);
    emitScrollFractions();
}

// ===== Missing PDF Banner (Phase R.3) =====

void DocumentViewport::showMissingPdfBanner(const QString& pdfName)
{
    if (!m_missingPdfBanner) {
        m_missingPdfBanner = new MissingPdfBanner(this);
        
        // Connect signals
        connect(m_missingPdfBanner, &MissingPdfBanner::locatePdfClicked,
                this, [this]() { emit requestPdfRelink(); });
        connect(m_missingPdfBanner, &MissingPdfBanner::dismissed,
                this, [this]() { /* Banner handles its own hide animation */ });
    }
    
    m_missingPdfBanner->setPdfName(pdfName);
    
    // Position at top of viewport
    m_missingPdfBanner->setFixedWidth(width());
    m_missingPdfBanner->move(0, 0);
    
    // Only animate if not already visible (avoid restart on redundant calls)
    if (!m_missingPdfBanner->isVisible()) {
        m_missingPdfBanner->showAnimated();
    }
}

void DocumentViewport::hideMissingPdfBanner()
{
    // Only hide if banner exists and is visible (avoid redundant animation)
    if (m_missingPdfBanner && m_missingPdfBanner->isVisible()) {
        m_missingPdfBanner->hideAnimated();
    }
}

// ===== Theme / Dark Mode =====

void DocumentViewport::setDarkMode(bool dark)
{
    if (m_isDarkMode == dark) {
        return;
    }
    
    m_isDarkMode = dark;
    
    // Cache background color to avoid recalculating on every paint
    // Dark mode: dark gray, Light mode: light gray
    // Unified gray colors: dark #4d4d4d (secondary), light #D0D0D0 (secondary)
    m_backgroundColor = dark ? QColor(0x4d, 0x4d, 0x4d) : QColor(0xD0, 0xD0, 0xD0);
    
    // Update palette for auto-fill background
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_backgroundColor);
    setPalette(pal);
    
    // Trigger repaint
    update();
}

// ===== Layout =====

void DocumentViewport::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == mode) {
        return;
    }
    
    // Before switching: get the page currently at viewport center
    int currentPage = m_currentPageIndex;
    qreal oldPageY = 0;
    if (m_document && !m_document->isEdgeless() && currentPage >= 0) {
        oldPageY = pagePosition(currentPage).y();
    }
    
    LayoutMode oldMode = m_layoutMode;
    m_layoutMode = mode;
    
    // Invalidate layout cache for new layout mode
    invalidatePageLayoutCache();
    
    // After switching: adjust vertical offset to keep same page visible
    if (m_document && !m_document->isEdgeless() && currentPage >= 0) {
        qreal newPageY = pagePosition(currentPage).y();
        
        // Adjust pan offset to compensate for page position change
        // Keep the same relative position within the viewport
        qreal yDelta = newPageY - oldPageY;
        m_panOffset.setY(m_panOffset.y() + yDelta);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Layout switch:" << (oldMode == LayoutMode::SingleColumn ? "1-col" : "2-col")
                 << "->" << (mode == LayoutMode::SingleColumn ? "1-col" : "2-col")
                 << "page" << currentPage << "yDelta" << yDelta;
#endif
    }
    
    // Update PDF cache capacity for new layout (Task 1.3.6)
    updatePdfCacheCapacity();
    
    // Recenter content horizontally for new layout width
    recenterHorizontally();
    
    // Recalculate layout and repaint
    clampPanOffset();
    update();
    emitScrollFractions();
}

void DocumentViewport::setPageGap(int gap)
{
    if (m_pageGap == gap) {
        return;
    }
    
    m_pageGap = qMax(0, gap);
    
    // Recalculate layout and repaint
    clampPanOffset();
    update();
    emitScrollFractions();
}

void DocumentViewport::setAutoLayoutEnabled(bool enabled)
{
    if (m_autoLayoutEnabled == enabled) {
        return;
    }
    
    m_autoLayoutEnabled = enabled;
    
    if (enabled) {
        // Immediately check if layout should change
        checkAutoLayout();
    } else {
        // When disabling auto mode, revert to single column
        setLayoutMode(LayoutMode::SingleColumn);
    }
}

void DocumentViewport::checkAutoLayout()
{
    // Only check if auto mode is enabled
    if (!m_autoLayoutEnabled) {
        return;
    }
    
    // Skip for edgeless documents (no pages)
    if (!m_document || m_document->isEdgeless()) {
        return;
    }
    
    // Skip if no pages
    if (m_document->pageCount() == 0) {
        return;
    }
    
    // Get typical page width from first page
    const Page* page = m_document->page(0);
    if (!page) {
        return;
    }
    
    // Calculate required width for 2-column layout (in viewport pixels)
    qreal pageWidth = page->size.width() * m_zoomLevel;
    qreal gapWidth = m_pageGap * m_zoomLevel;
    qreal requiredWidth = 2 * pageWidth + gapWidth;
    
    // Determine target layout mode
    LayoutMode targetMode = (width() >= requiredWidth) 
        ? LayoutMode::TwoColumn 
        : LayoutMode::SingleColumn;
    
    // Only switch if different (avoids redundant invalidation)
    if (targetMode != m_layoutMode) {
        setLayoutMode(targetMode);
    }
}

void DocumentViewport::recenterHorizontally()
{
    // Skip for edgeless documents
    if (!m_document || m_document->isEdgeless()) {
        return;
    }
    
    // Guard against zero zoom
    qreal zoomLevel = m_zoomLevel;
    if (zoomLevel <= 0) zoomLevel = 1.0;
    
    // Get content size in document coordinates
    QSizeF contentSize = totalContentSize();
    
    // Calculate viewport width in document coordinates
    qreal viewportWidth = width() / zoomLevel;
    
    if (contentSize.width() < viewportWidth) {
        // Case 1: Content is narrower than viewport - center it
        // Negative pan X shifts content to the right (toward center)
        qreal centeringOffset = (viewportWidth - contentSize.width()) / 2.0;
        m_panOffset.setX(-centeringOffset);
        emit panChanged(m_panOffset);
    } else {
        // Case 2: Viewport is narrower than content - clamp pan to valid range
        // This ensures we don't show empty space on one side while content
        // is still available on the other side
        
        // Minimum pan: 0 (left edge of content at left edge of viewport)
        // Maximum pan: content.width - viewport.width (right edge at right edge)
        qreal minX = 0.0;
        qreal maxX = contentSize.width() - viewportWidth;
        
        // Clamp current pan to this range (preserves user's horizontal scroll position
        // while preventing unnecessary empty space)
        qreal clampedX = qBound(minX, m_panOffset.x(), maxX);
        
        if (!qFuzzyCompare(m_panOffset.x(), clampedX)) {
            m_panOffset.setX(clampedX);
            emit panChanged(m_panOffset);
        }
    }
}

// ===== Document Change Notifications =====

void DocumentViewport::notifyDocumentStructureChanged()
{
    // Invalidate layout cache - page count or sizes changed
    invalidatePageLayoutCache();
    
    // Trigger repaint to show new/removed pages
    update();
    
    // Emit scroll signals (scroll range may have changed)
    emitScrollFractions();
}

// ===== Tool State Management (Task 2.1) =====

void DocumentViewport::setCurrentTool(ToolType tool)
{
    if (m_currentTool == tool) {
        return;
    }
    
    ToolType previousTool = m_currentTool;
    m_currentTool = tool;
    
    // CR-2B-1: Disable straight line mode when switching to Eraser or Lasso
    // (straight lines only work with Pen and Marker)
    if ((tool == ToolType::Eraser || tool == ToolType::Lasso) && m_straightLineMode) {
        m_straightLineMode = false;
        emit straightLineModeChanged(false);
    }
    
    // Task 2.10.9: Clear lasso selection when switching away from Lasso tool
    if (previousTool == ToolType::Lasso && tool != ToolType::Lasso) {
        // Apply any pending transform before switching
        if (m_lassoSelection.isValid() && m_lassoSelection.hasTransform()) {
            applySelectionTransform();
        } else {
            clearLassoSelection();
        }
    }
    
    // Phase A: Clear text selection when switching away from Highlighter
    if (previousTool == ToolType::Highlighter && tool != ToolType::Highlighter) {
        bool hadTextSelection = m_textSelection.isValid();
        m_textSelection.clear();
        if (hadTextSelection) {
            emit textSelectionChanged(false);
        }
        clearTextBoxCache();
        clearLinkCache();  // Phase D.1
    }
    
    // Update cursor based on tool and page type
    updateHighlighterCursor();
    
    // Repaint for tool-specific visuals (eraser cursor, etc.)
    update();
    
    emit toolChanged(tool);
}

void DocumentViewport::setPenColor(const QColor& color)
{
    if (m_penColor == color) {
        return;
    }
    
    m_penColor = color;
}

void DocumentViewport::setPenThickness(qreal thickness)
{
    // Clamp to reasonable range
    thickness = qBound(0.5, thickness, 100.0);
    
    if (qFuzzyCompare(m_penThickness, thickness)) {
        return;
    }
    
    m_penThickness = thickness;
}

void DocumentViewport::setEraserSize(qreal size)
{
    // Clamp to reasonable range
    size = qBound(5.0, size, 200.0);
    
    if (qFuzzyCompare(m_eraserSize, size)) {
        return;
    }
    
    m_eraserSize = size;
    
    // Repaint to update eraser cursor size
    if (m_currentTool == ToolType::Eraser) {
        update();
    }
}

// ===== Marker Tool (Task 2.8) =====

void DocumentViewport::setMarkerColor(const QColor& color)
{
    if (m_markerColor == color) {
        return;
    }
    m_markerColor = color;
}

void DocumentViewport::setMarkerThickness(qreal thickness)
{
    // Clamp to reasonable range (marker is typically wider than pen)
    thickness = qBound(1.0, thickness, 100.0);
    
    if (qFuzzyCompare(m_markerThickness, thickness)) {
        return;
    }
    m_markerThickness = thickness;
}

// ===== Straight Line Mode (Task 2.9) =====

void DocumentViewport::setStraightLineMode(bool enabled)
{
    if (m_straightLineMode == enabled) {
        return;
    }
    
    // If disabling while drawing, cancel the current straight line
    if (!enabled && m_isDrawingStraightLine) {
        m_isDrawingStraightLine = false;
        update();  // Clear the preview
    }
    
    // CR-2B-2: If enabling while on Eraser, switch to Pen first
    // (straight lines only work with Pen and Marker)
    if (enabled && m_currentTool == ToolType::Eraser) {
        m_currentTool = ToolType::Pen;
        emit toolChanged(ToolType::Pen);
    }
    
    m_straightLineMode = enabled;
    emit straightLineModeChanged(enabled);
}

// ===== Object Mode Setters (Phase D) =====

void DocumentViewport::setObjectInsertMode(ObjectInsertMode mode)
{
    if (m_objectInsertMode == mode) {
        return;
    }
    
    m_objectInsertMode = mode;
    emit objectInsertModeChanged(mode);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Object insert mode changed to:" << (mode == ObjectInsertMode::Image ? "Image" : "Link");
#endif
}

void DocumentViewport::setObjectActionMode(ObjectActionMode mode)
{
    if (m_objectActionMode == mode) {
        return;
    }
    
    m_objectActionMode = mode;
    emit objectActionModeChanged(mode);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Object action mode changed to:" << (mode == ObjectActionMode::Select ? "Select" : "Create");
#endif
}

// ===== View State Setters =====

void DocumentViewport::setZoomLevel(qreal zoom)
{
    // Apply mode-specific minimum zoom
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    
    // Clamp to valid range
    zoom = qBound(minZ, zoom, MAX_ZOOM);
    
    if (qFuzzyCompare(m_zoomLevel, zoom)) {
        return;
    }
    
    qreal oldDpi = effectivePdfDpi();
    m_zoomLevel = zoom;
    qreal newDpi = effectivePdfDpi();
    
    // Invalidate PDF cache if DPI changed significantly (Task 1.3.6)
    if (!qFuzzyCompare(oldDpi, newDpi)) {
        invalidatePdfCache();
    }
    
    // Note: Stroke caches are zoom-aware and will rebuild automatically
    // when ensureStrokeCacheValid() is called with the new zoom level.
    // No explicit invalidation needed - just lazy rebuild on next paint.
    
    // Clamp pan offset (bounds change with zoom)
    clampPanOffset();
    
    update();
    emit zoomChanged(m_zoomLevel);
    emitScrollFractions();
}

void DocumentViewport::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
    clampPanOffset();
    
    updateCurrentPageIndex();
    
    update();
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Preload PDF cache for adjacent pages after scroll (Task: PDF Performance Fix)
    // Safe here because scroll is user-initiated, not during rapid stroke drawing
    preloadPdfCache();
    
    // MEMORY FIX: Evict stroke caches for distant pages after scroll
    // This prevents unbounded memory growth when scrolling through large documents
    preloadStrokeCaches();
    
    // EDGELESS MEMORY FIX: Evict tiles that are far from visible area
    // This saves dirty tiles to disk and removes them from memory (Phase E5)
    evictDistantTiles();
}

void DocumentViewport::scrollToPage(int pageIndex)
{
    if (!m_document || m_document->pageCount() == 0) return;
    
    pageIndex = qBound(0, pageIndex, m_document->pageCount() - 1);
    
    // Get page position and scroll to show it at top of viewport
    QPointF pos = pagePosition(pageIndex);
    
    // Only change Y position (with margin), preserve X centering
    // This prevents the horizontal pan from resetting when navigating pages,
    // which would cause the page to shift when sidebars are toggled
    m_panOffset.setY(pos.y() - 10);
    
    // Re-center horizontally if content is narrower than viewport
    // If content is wider (user zoomed in), preserve their horizontal pan position
    recenterHorizontally();
    
    // Clamp to valid bounds and emit signal
    clampPanOffset();
    emit panChanged(m_panOffset);
    
    m_currentPageIndex = pageIndex;
    emit currentPageChanged(m_currentPageIndex);
    
    update();
}

void DocumentViewport::scrollToPositionOnPage(int pageIndex, QPointF normalizedPosition)
{
    // Phase E.2: Scroll to a specific position within a page using normalized coordinates
    // Used by OutlinePanel for PDF outline navigation
    //
    // Normalized coordinates: 0-1 range where:
    //   X: 0 = left edge, 1 = right edge
    //   Y: 0 = top edge, 1 = bottom edge (ALREADY converted from PDF coords by PopplerPdfProvider)
    //   Values < 0 mean "not specified"
    
    if (!m_document || m_document->pageCount() == 0) return;
    
    pageIndex = qBound(0, pageIndex, m_document->pageCount() - 1);
    
    // Get page size and position in document coordinates
    QSizeF pageSz = m_document->pageSizeAt(pageIndex);
    QPointF pagePos = pagePosition(pageIndex);
    
    // Calculate target Y position within the page
    // Only adjust Y if specified; X is handled by centering
    qreal targetY = pagePos.y();
    
    if (normalizedPosition.y() >= 0) {
        // Normalized Y is already in our coordinate system (0 = top, 1 = bottom)
        // Position near top of viewport, not centered, so user sees content below
        targetY += normalizedPosition.y() * pageSz.height();
        // Add small offset so the target line isn't at the very top edge
        targetY -= 20;  // 20px margin from top
    }
    
    // Set pan to show target Y position near top of viewport
    // For Y: we want targetY to be near the top of the viewport, not centered
    QPointF newPan(
        m_panOffset.x(),  // Keep current X (will recenter horizontally below)
        targetY
    );
    
    setPanOffset(newPan);
    
    // Re-center horizontally to keep pages properly centered
    // This ensures the document stays centered regardless of X position in outline
    recenterHorizontally();
    
    // Update current page index
    m_currentPageIndex = pageIndex;
    emit currentPageChanged(m_currentPageIndex);
    
    /*
    qDebug() << "scrollToPositionOnPage: page" << pageIndex 
             << "normalized" << normalizedPosition
             << "-> targetY" << targetY;
                 */
}

void DocumentViewport::navigateToPosition(QString pageUuid, QPointF position)
{
    // Phase C.5.1: Navigate to a specific page position (for LinkObject Position slots)
    if (!m_document || pageUuid.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "navigateToPosition: Invalid target";
#endif
        return;
    }
    
    int targetPageIndex = m_document->pageIndexByUuid(pageUuid);
    if (targetPageIndex < 0) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "navigateToPosition: Page not found for UUID" << pageUuid;
#endif
        // TODO: Show user message "Target page not found"
        return;
    }
    
    // First scroll to bring the page into view
    scrollToPage(targetPageIndex);
    
    // Convert page-local position to document coordinates
    QPointF targetDocPos = pageToDocument(targetPageIndex, position);
    
    // Calculate pan offset to center this position in viewport
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    QPointF targetViewportPos = documentToViewport(targetDocPos);
    QPointF panDelta = viewportCenter - targetViewportPos;
    
    setPanOffset(m_panOffset + panDelta);
    
    // Re-center horizontally to ensure proper alignment
    recenterHorizontally();
    
    update();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "navigateToPosition: Navigated to page" << targetPageIndex 
             << "position" << position;
#endif
}

void DocumentViewport::navigateToEdgelessPosition(int tileX, int tileY, QPointF docPosition)
{
    // Navigate to a specific position in an edgeless document
    if (!m_document || !m_document->isEdgeless()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "navigateToEdgelessPosition: Invalid target (not edgeless)";
#endif
        return;
    }
    
    // The tile coordinates are informational - we use docPosition directly
    Q_UNUSED(tileX);
    Q_UNUSED(tileY);
    
    // Calculate pan offset to center the target document position in viewport
    // Goal: documentToViewport(docPosition) should equal viewportCenter
    // documentToViewport(pos) = (pos - panOffset) * zoom
    // So: (docPosition - panOffset) * zoom = viewportCenter
    // Therefore: panOffset = docPosition - viewportCenter / zoom
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    QPointF newPanOffset = docPosition - viewportCenter / m_zoomLevel;
    
    // setPanOffset already calls update()
    setPanOffset(newPanOffset);
    
#ifdef SPEEDYNOTE_DEBUG
    // Verify: viewportCenter = (docCenter - panOffset) * zoom
    // So: docCenter = viewportCenter/zoom + panOffset
    QPointF actualCenter = viewportCenter / m_zoomLevel + m_panOffset;
    qDebug() << "navigateToEdgelessPosition: target docPosition =" << docPosition
             << "| new panOffset =" << m_panOffset
             << "| actual viewport center (doc coords) =" << actualCenter
             << "| difference =" << (actualCenter - docPosition);
#endif
}

// ============================================================================
// Edgeless Position History (Phase 4)
// ============================================================================

QPointF DocumentViewport::currentCenterPosition() const
{
    // Calculate the document position at the center of the viewport
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    return viewportCenter / m_zoomLevel + m_panOffset;
}

void DocumentViewport::pushPositionHistory()
{
    // Only applies to edgeless mode
    if (!m_document || !m_document->isEdgeless()) {
        return;
    }
    
    QPointF currentPos = currentCenterPosition();
    
    // Don't push if we're already at this position (avoid duplicates)
    if (!m_edgelessPositionHistory.isEmpty()) {
        QPointF lastPos = m_edgelessPositionHistory.top();
        // Consider positions within 10 pixels as "same"
        if ((currentPos - lastPos).manhattanLength() < 10.0) {
            return;
        }
    }
    
    // Trim history if at capacity - remove oldest (bottom) entry
    // QStack doesn't have removeFirst(), so we pop all except oldest, discard oldest, repush
    // O(n) but acceptable for small MAX_POSITION_HISTORY (20 items)
    if (m_edgelessPositionHistory.size() >= MAX_POSITION_HISTORY) {
        QStack<QPointF> temp;
        // Move all except the bottom (oldest) to temp
        while (m_edgelessPositionHistory.size() > 1) {
            temp.push(m_edgelessPositionHistory.pop());
        }
        // Discard the oldest (now the only item left)
        m_edgelessPositionHistory.pop();
        // Restore the rest
        while (!temp.isEmpty()) {
            m_edgelessPositionHistory.push(temp.pop());
        }
    }
    
    m_edgelessPositionHistory.push(currentPos);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Pushed position:" << currentPos 
             << "| History size:" << m_edgelessPositionHistory.size();
#endif
}

void DocumentViewport::returnToOrigin()
{
    // Only applies to edgeless mode
    if (!m_document || !m_document->isEdgeless()) {
        return;
    }
    
    // Save current position before jumping
    pushPositionHistory();
    
    // Navigate to origin (0, 0)
    QPointF origin(0.0, 0.0);
    
    // Use the existing navigation method with tile (0, 0)
    navigateToEdgelessPosition(0, 0, origin);
    
    // BUG FIX: Mark document as modified so position history is saved
    // This ensures the * indicator shows on the tab
    emit documentModified();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Returned to origin";
#endif
}

void DocumentViewport::goBackPosition()
{
    // Only applies to edgeless mode
    if (!m_document || !m_document->isEdgeless()) {
        return;
    }
    
    if (m_edgelessPositionHistory.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[PositionHistory] Go back: history empty";
#endif
        return;
    }
    
    QPointF previousPos = m_edgelessPositionHistory.pop();
    
    // Calculate tile coordinates from document position
    int tileX = static_cast<int>(std::floor(previousPos.x() / Document::EDGELESS_TILE_SIZE));
    int tileY = static_cast<int>(std::floor(previousPos.y() / Document::EDGELESS_TILE_SIZE));
    
    navigateToEdgelessPosition(tileX, tileY, previousPos);
    
    // BUG FIX: Mark document as modified so position history is saved
    // This ensures the * indicator shows on the tab
    emit documentModified();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Went back to:" << previousPos
             << "| tile:" << tileX << "," << tileY
             << "| Remaining history:" << m_edgelessPositionHistory.size();
#endif
}

bool DocumentViewport::hasPositionHistory() const
{
    return !m_edgelessPositionHistory.isEmpty();
}

void DocumentViewport::syncPositionToDocument()
{
    // Only applies to edgeless mode
    if (!m_document || !m_document->isEdgeless()) {
        return;
    }
    
    // Save current viewport center position
    QPointF currentPos = currentCenterPosition();
    m_document->setEdgelessLastPosition(currentPos);
    
    // Convert QStack to QVector for Document storage
    // QStack stores items in LIFO order, so we need to reverse to get oldest-to-newest
    QVector<QPointF> historyVec;
    historyVec.reserve(m_edgelessPositionHistory.size());
    
    // Copy stack to temp, then pop into vector (gives us correct order)
    QStack<QPointF> tempStack = m_edgelessPositionHistory;
    while (!tempStack.isEmpty()) {
        historyVec.append(tempStack.pop());  // O(1) append instead of O(n) prepend
    }
    // Reverse to get oldest-to-newest order
    std::reverse(historyVec.begin(), historyVec.end());
    
    m_document->setEdgelessPositionHistory(historyVec);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Synced to document: lastPos =" << currentPos
             << "| history size =" << historyVec.size();
#endif
}

bool DocumentViewport::applyRestoredEdgelessPosition()
{
    // Only applies to edgeless mode with valid dimensions
    if (!m_document || !m_document->isEdgeless()) {
        return false;
    }
    
    if (width() <= 0 || height() <= 0) {
        return false;  // Can't calculate pan offset without valid dimensions
    }
    
    // Restore position history from Document
    const QVector<QPointF>& savedHistory = m_document->edgelessPositionHistory();
    m_edgelessPositionHistory.clear();
    for (const QPointF& pos : savedHistory) {
        m_edgelessPositionHistory.push(pos);
    }
    
    // Calculate pan offset to center the saved position
    QPointF lastPos = m_document->edgelessLastPosition();
    if (lastPos.isNull()) {
        return false;  // No saved position
    }
    
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    m_panOffset = lastPos - viewportCenter / m_zoomLevel;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Applied restored position: lastPos =" << lastPos
             << "| panOffset =" << m_panOffset
             << "| history size =" << m_edgelessPositionHistory.size();
#endif
    
    return true;
}

void DocumentViewport::scrollBy(QPointF delta)
{
    setPanOffset(m_panOffset + delta);
}

void DocumentViewport::zoomToFit()
{
    if (!m_document || m_document->pageCount() == 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Get current page size
    const Page* page = m_document->page(m_currentPageIndex);
    if (!page) {
        setZoomLevel(1.0);
        return;
    }
    
    QSizeF pageSize = page->size;
    
    // Guard against zero-size pages
    if (pageSize.width() <= 0 || pageSize.height() <= 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Calculate zoom to fit page in viewport with some margin
    qreal marginFraction = 0.05;  // 5% margin on each side
    qreal availWidth = width() * (1.0 - 2 * marginFraction);
    qreal availHeight = height() * (1.0 - 2 * marginFraction);
    
    qreal zoomX = availWidth / pageSize.width();
    qreal zoomY = availHeight / pageSize.height();
    
    // Use the smaller zoom to fit both dimensions
    qreal newZoom = qMin(zoomX, zoomY);
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Set zoom and center on current page
    setZoomLevel(newZoom);
    
    // Center the page in viewport
    QPointF pagePos = pagePosition(m_currentPageIndex);
    QPointF pageCenter = pagePos + QPointF(pageSize.width() / 2, pageSize.height() / 2);
    
    // Calculate pan offset to center the page
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    m_panOffset = pageCenter - QPointF(viewWidth / 2, viewHeight / 2);
    
    clampPanOffset();
    update();
    emit panChanged(m_panOffset);
}

void DocumentViewport::zoomToWidth()
{
    if (!m_document || m_document->pageCount() == 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Get current page size
    const Page* page = m_document->page(m_currentPageIndex);
    if (!page) {
        setZoomLevel(1.0);
        return;
    }
    
    QSizeF pageSize = page->size;
    
    // Guard against zero-width pages
    if (pageSize.width() <= 0) {
        setZoomLevel(1.0);
        return;
    }
    
    // Calculate zoom to fit page width with some margin
    qreal marginFraction = 0.02;  // 2% margin on each side
    qreal availWidth = width() * (1.0 - 2 * marginFraction);
    
    qreal newZoom = availWidth / pageSize.width();
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Set zoom and adjust pan to keep current page visible
    setZoomLevel(newZoom);
    
    // Center horizontally on current page
    QPointF pagePos = pagePosition(m_currentPageIndex);
    qreal viewWidth = width() / m_zoomLevel;
    m_panOffset.setX(pagePos.x() + pageSize.width() / 2 - viewWidth / 2);
    
    clampPanOffset();
    update();
    emit panChanged(m_panOffset);
}

void DocumentViewport::zoomIn()
{
    // Zoom step factor (1.25x = 25% increase per step)
    static constexpr qreal ZOOM_STEP = 1.25;
    
    qreal newZoom = m_zoomLevel * ZOOM_STEP;
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    setZoomLevel(newZoom);
    
    // Recenter content for paged documents (no-op for edgeless)
    recenterHorizontally();
}

void DocumentViewport::zoomOut()
{
    // Zoom step factor (1/1.25 = 20% decrease per step)
    static constexpr qreal ZOOM_STEP = 1.25;
    
    qreal newZoom = m_zoomLevel / ZOOM_STEP;
    newZoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    setZoomLevel(newZoom);
    
    // Recenter content for paged documents (no-op for edgeless)
    recenterHorizontally();
}

void DocumentViewport::zoomToActualSize()
{
    setZoomLevel(1.0);
    
    // Recenter content for paged documents (no-op for edgeless)
    recenterHorizontally();
}

void DocumentViewport::scrollToHome()
{
    setPanOffset(QPointF(0, 0));
    m_currentPageIndex = 0;
    emit currentPageChanged(m_currentPageIndex);
}

void DocumentViewport::setHorizontalScrollFraction(qreal fraction)
{
    if (!m_document || m_document->pageCount() == 0) {
        return;
    }
    
    // Clamp fraction to valid range
    fraction = qBound(0.0, fraction, 1.0);
    
    // Calculate scrollable width
    QSizeF contentSize = totalContentSize();
    qreal viewportWidth = width() / m_zoomLevel;
    qreal scrollableWidth = contentSize.width() - viewportWidth;
    
    if (scrollableWidth <= 0) {
        // Content fits in viewport - no horizontal scroll needed
        return;
    }
    
    // Set pan offset based on fraction
    qreal newX = fraction * scrollableWidth;
    if (!qFuzzyCompare(m_panOffset.x(), newX)) {
        m_panOffset.setX(newX);
        clampPanOffset();
        emit panChanged(m_panOffset);
        update();
    }
}

void DocumentViewport::setVerticalScrollFraction(qreal fraction)
{
    if (!m_document || m_document->pageCount() == 0) {
        return;
    }
    
    // Clamp fraction to valid range
    fraction = qBound(0.0, fraction, 1.0);
    
    // Calculate scrollable height
    QSizeF contentSize = totalContentSize();
    qreal viewportHeight = height() / m_zoomLevel;
    qreal scrollableHeight = contentSize.height() - viewportHeight;
    
    if (scrollableHeight <= 0) {
        // Content fits in viewport - no vertical scroll needed
        return;
    }
    
    // Set pan offset based on fraction
    qreal newY = fraction * scrollableHeight;
    if (!qFuzzyCompare(m_panOffset.y(), newY)) {
        m_panOffset.setY(newY);
        clampPanOffset();
        updateCurrentPageIndex();
        emit panChanged(m_panOffset);
        update();
    }
}

// ===== Layout Engine (Task 1.3.2) =====

QPointF DocumentViewport::pagePosition(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QPointF(0, 0);
    }
    
    // For edgeless documents, there's only one page at origin
    if (m_document->isEdgeless()) {
        return QPointF(0, 0);
    }
    
    // Ensure cache is valid - O(n) rebuild only when dirty
    ensurePageLayoutCache();
    
    // O(1) lookup from cache
    qreal y = (pageIndex < m_pageYCache.size()) ? m_pageYCache[pageIndex] : 0;
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn:
            // X is always 0 for single column
            return QPointF(0, y);
        
        case LayoutMode::TwoColumn: {
            // Y comes from cache, just need to calculate X for right column
            int col = pageIndex % 2;
            qreal x = 0;
            
            if (col == 1) {
                // Right column - offset by left page width + gap
                // PERF FIX: Use pageSizeAt() to avoid triggering lazy loading
                int leftIdx = (pageIndex / 2) * 2;
                QSizeF leftSize = m_document->pageSizeAt(leftIdx);
                if (!leftSize.isEmpty()) {
                    x = leftSize.width() + m_pageGap;
                }
            }
            
            return QPointF(x, y);
        }
    }
    
    return QPointF(0, 0);
}

QRectF DocumentViewport::pageRect(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QRectF();
    }
    
    // PERF FIX: Use pageSizeAt() instead of page()->size to avoid
    // triggering lazy loading from disk. pageSizeAt() uses metadata
    // which is loaded upfront from the manifest.
    QSizeF pageSize = m_document->pageSizeAt(pageIndex);
    if (pageSize.isEmpty()) {
        return QRectF();
    }
    
    QPointF pos = pagePosition(pageIndex);
    return QRectF(pos, pageSize);
}

QSizeF DocumentViewport::totalContentSize() const
{
    if (!m_document || m_document->pageCount() == 0) {
        return QSizeF(0, 0);
    }
    
    // For edgeless documents, return the single page size
    // (it can grow dynamically, but we report current size)
    if (m_document->isEdgeless()) {
        const Page* page = m_document->edgelessPage();
        return page ? page->size : QSizeF(0, 0);
    }
    
    // PERF FIX: Use cached content size computed during layout pass.
    // ensurePageLayoutCache() computes both page Y positions AND total content size
    // in a single O(n) pass, avoiding repeated O(n) iterations on every scroll.
    ensurePageLayoutCache();
    return m_cachedContentSize;
}

int DocumentViewport::pageAtPoint(QPointF documentPt) const
{
    if (!m_document || m_document->pageCount() == 0) {
        return -1;
    }
    
    // For edgeless documents, the single page covers everything
    if (m_document->isEdgeless()) {
        const Page* page = m_document->edgelessPage();
        if (page) {
            return 0;
        }
        return -1;
    }
    
    // Ensure cache is valid for O(1) page position lookup
    ensurePageLayoutCache();
    
    int pageCount = m_document->pageCount();
    qreal y = documentPt.y();
    
    // For single column: use binary search on Y positions (O(log n))
    if (m_layoutMode == LayoutMode::SingleColumn && !m_pageYCache.isEmpty()) {
        // Binary search to find the page containing this Y coordinate
        int low = 0;
        int high = pageCount - 1;
        int candidate = -1;
        
        while (low <= high) {
            int mid = (low + high) / 2;
            qreal pageY = m_pageYCache[mid];
            
            if (y < pageY) {
                high = mid - 1;
            } else {
                candidate = mid;  // This page starts at or before our Y
                low = mid + 1;
            }
        }
        
        // Check if the point is actually within the candidate page
        if (candidate >= 0) {
            QRectF rect = pageRect(candidate);  // Now O(1)
            if (rect.contains(documentPt)) {
                return candidate;
            }
        }
        
        return -1;
    }
    
    // PERF FIX: For two-column, use binary search on Y cache to find the row
    // Then only check the two pages in that row instead of all 3600+ pages
    if (!m_pageYCache.isEmpty()) {
        qreal targetY = documentPt.y();
        int numRows = (pageCount + 1) / 2;
        
        // Binary search to find the row containing this Y coordinate
        int low = 0;
        int high = numRows - 1;
        int candidateRow = -1;
        
        while (low <= high) {
            int mid = (low + high) / 2;
            int pageIdx = mid * 2;  // First page of row
            qreal rowY = m_pageYCache[pageIdx];
            
            if (targetY < rowY) {
                high = mid - 1;
            } else {
                candidateRow = mid;  // This row or later
                low = mid + 1;
            }
        }
        
        // Check candidate row and neighbors (for edge cases)
        for (int row = qMax(0, candidateRow); row <= qMin(numRows - 1, candidateRow + 1); ++row) {
            int leftIdx = row * 2;
            
            // Check left page
            QRectF leftRect = pageRect(leftIdx);
            if (leftRect.contains(documentPt)) {
                return leftIdx;
            }
            
            // Check right page
            int rightIdx = leftIdx + 1;
            if (rightIdx < pageCount) {
                QRectF rightRect = pageRect(rightIdx);
                if (rightRect.contains(documentPt)) {
                    return rightIdx;
                }
            }
        }
        
        return -1;
    }
    
    // Fallback: linear search if cache not available
    for (int i = 0; i < pageCount; ++i) {
        QRectF rect = pageRect(i);
        if (rect.contains(documentPt)) {
            return i;
        }
    }
    
    return -1;
}

InsertedObject* DocumentViewport::objectAtPoint(const QPointF& docPoint) const
{
    if (!m_document) {
        return nullptr;
    }
    
    // Phase O3.5.5: Affinity filtering (Option A - Strict)
    // Only select objects where affinity == activeLayerIndex - 1
    // This ensures users can only select objects "tied to" the current layer.
    int affinityFilter = INT_MIN;  // Default: no filtering (for safety)
    
    if (m_document->isEdgeless()) {
        // Edgeless mode: use viewport-level active layer index
        affinityFilter = m_edgelessActiveLayerIndex - 1;
        
        // Edgeless mode: check all loaded tiles
        // Objects are stored with tile-local coordinates
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            // Convert document coords to tile-local coords
            QPointF tileLocal = docPoint - QPointF(
                coord.first * Document::EDGELESS_TILE_SIZE,
                coord.second * Document::EDGELESS_TILE_SIZE
            );
            
            // Check if point is within tile bounds (optimization)
            if (tileLocal.x() < 0 || tileLocal.y() < 0 ||
                tileLocal.x() > Document::EDGELESS_TILE_SIZE ||
                tileLocal.y() > Document::EDGELESS_TILE_SIZE) {
                // Point not in this tile, but object might extend beyond tile
                // Still check - Page::objectAtPoint handles this
            }
            
            if (InsertedObject* obj = tile->objectAtPoint(tileLocal, affinityFilter)) {
                return obj;
            }
        }
    } else {
        // Paged mode: check the page at the point
        int pageIdx = pageAtPoint(docPoint);
        if (pageIdx >= 0) {
            Page* page = m_document->page(pageIdx);
            if (page) {
                // Paged mode: use page-level active layer index
                affinityFilter = page->activeLayerIndex - 1;
                
                // Convert to page-local coordinates
                QPointF pageLocal = docPoint - pagePosition(pageIdx);
                return page->objectAtPoint(pageLocal, affinityFilter);
            }
        }
    }
    
    return nullptr;
}

// ===== Object Resize (Phase O3.1) =====

QRectF DocumentViewport::objectBoundsInViewport(InsertedObject* obj) const
{
    if (!obj || !m_document) {
        return QRectF();
    }
    
    // Get object's document-space position
    QPointF docPos;
    
    // PERF FIX: During drag/resize, use cached tile/page index instead of searching
    // This is called multiple times per frame during drag, so caching is critical
    bool useCachedLocation = (m_isDraggingObjects || m_isResizingObject) &&
                             m_selectedObjects.size() == 1 &&
                             m_selectedObjects.first() == obj;
    
    if (m_document->isEdgeless()) {
        if (useCachedLocation) {
            // Fast path: use cached tile coordinate
            QPointF tileOrigin(m_dragObjectTileCoord.first * Document::EDGELESS_TILE_SIZE,
                               m_dragObjectTileCoord.second * Document::EDGELESS_TILE_SIZE);
            docPos = tileOrigin + obj->position;
        } else {
            // Slow path: search all tiles (only when not dragging)
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                                       coord.second * Document::EDGELESS_TILE_SIZE);
                    docPos = tileOrigin + obj->position;
                    break;
                }
            }
        }
    } else {
        if (useCachedLocation && m_dragObjectPageIndex >= 0) {
            // Fast path: use cached page index
            docPos = pagePosition(m_dragObjectPageIndex) + obj->position;
        } else {
            // Slow path: search pages
            // PERF FIX: Only search loaded pages to avoid triggering lazy loading
            for (int i : m_document->loadedPageIndices()) {
                Page* page = m_document->page(i);  // Already loaded, no disk I/O
                if (page && page->objectById(obj->id)) {
                    docPos = pagePosition(i) + obj->position;
                    break;
                }
            }
        }
    }
    
    // Convert document position to viewport coordinates
    QPointF vpTopLeft = documentToViewport(docPos);
    QSizeF vpSize(obj->size.width() * m_zoomLevel, obj->size.height() * m_zoomLevel);
    
    return QRectF(vpTopLeft, vpSize);
}

DocumentViewport::HandleHit DocumentViewport::objectHandleAtPoint(const QPointF& viewportPos) const
{
    // Only works with single selection
    if (m_selectedObjects.size() != 1) {
        return HandleHit::None;
    }
    
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj) {
        return HandleHit::None;
    }
    
    // Get unrotated object bounds in viewport coordinates
    QRectF objRect = objectBoundsInViewport(obj);
    if (objRect.isEmpty()) {
        return HandleHit::None;
    }
    
    // Helper to rotate a point around center
    auto rotatePoint = [](const QPointF& pt, const QPointF& center, qreal angleDegrees) -> QPointF {
        if (qAbs(angleDegrees) < 0.01) return pt;
        qreal rad = qDegreesToRadians(angleDegrees);
        qreal cosA = qCos(rad);
        qreal sinA = qSin(rad);
        QPointF translated = pt - center;
        return QPointF(
            translated.x() * cosA - translated.y() * sinA + center.x(),
            translated.x() * sinA + translated.y() * cosA + center.y()
        );
    };
    
    QPointF vpCenter = objRect.center();
    
    // Calculate the 8 handle positions with rotation
    QPointF handles[8] = {
        rotatePoint(objRect.topLeft(), vpCenter, obj->rotation),                           // 0: TopLeft
        rotatePoint(QPointF(objRect.center().x(), objRect.top()), vpCenter, obj->rotation),// 1: Top
        rotatePoint(objRect.topRight(), vpCenter, obj->rotation),                          // 2: TopRight
        rotatePoint(QPointF(objRect.left(), objRect.center().y()), vpCenter, obj->rotation),  // 3: Left
        rotatePoint(QPointF(objRect.right(), objRect.center().y()), vpCenter, obj->rotation), // 4: Right
        rotatePoint(objRect.bottomLeft(), vpCenter, obj->rotation),                        // 5: BottomLeft
        rotatePoint(QPointF(objRect.center().x(), objRect.bottom()), vpCenter, obj->rotation),// 6: Bottom
        rotatePoint(objRect.bottomRight(), vpCenter, obj->rotation)                        // 7: BottomRight
    };
    
    // Rotation handle position (offset from top center in rotated direction)
    QPointF topCenter = handles[1];
    qreal rad = qDegreesToRadians(obj->rotation);
    QPointF rotateOffset(ROTATE_HANDLE_OFFSET * qSin(rad), -ROTATE_HANDLE_OFFSET * qCos(rad));
    QPointF rotatePos = topCenter + rotateOffset;
    
    // Use HANDLE_HIT_SIZE for hit testing (touch-friendly)
    qreal hitRadius = HANDLE_HIT_SIZE / 2.0;
    
    // Check rotation handle first (has priority)
    if (QLineF(viewportPos, rotatePos).length() <= hitRadius) {
        return HandleHit::Rotate;
    }
    
    // Check the 8 resize handles
    static const HandleHit handleTypes[8] = {
        HandleHit::TopLeft, HandleHit::Top, HandleHit::TopRight,
        HandleHit::Left, HandleHit::Right,
        HandleHit::BottomLeft, HandleHit::Bottom, HandleHit::BottomRight
    };
    
    for (int i = 0; i < 8; ++i) {
        if (QLineF(viewportPos, handles[i]).length() <= hitRadius) {
            return handleTypes[i];
        }
    }
    
    return HandleHit::None;
}

void DocumentViewport::updateObjectResize(const QPointF& currentViewport)
{
    // Phase O3.1.4: Resize logic implementation
    // BF-Rotation: Fixed to work correctly with rotated objects by converting
    // delta to local coordinates (same approach as lasso updateScaleFromHandle)
    
    if (m_selectedObjects.size() != 1) return;
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj) return;
    
    // Phase C.2.2: LinkObject doesn't resize - only move is allowed
    // LinkObject has fixed icon size (24x24), resize would distort it
    if (obj->type() == "link") {
        return;
    }
    
    // Convert positions to document coordinates
    QPointF currentDoc = viewportToDocument(currentViewport);
    
        // -----------------------------------------------------------------
        // Rotation (Phase O3.1.8.1): Rotate object around its center
        // -----------------------------------------------------------------
    if (m_objectResizeHandle == HandleHit::Rotate) {
        // BF: Use m_resizeObjectDocCenter (document-global) for consistent coordinates
        // with the pointer position from viewportToDocument()
            
            // Angle from center to current pointer (in document coords)
            // atan2 returns radians, with 0 pointing right (+X), positive going counterclockwise
            // We add 90° because the rotation handle starts above the object (at 12 o'clock)
            qreal angle = qRadiansToDegrees(
            qAtan2(currentDoc.y() - m_resizeObjectDocCenter.y(), 
                   currentDoc.x() - m_resizeObjectDocCenter.x())
            ) + 90.0;
            
            // Normalize to 0-360 range
            while (angle < 0) angle += 360.0;
            while (angle >= 360) angle -= 360.0;
            
            // Snap to 15° increments by default
            // TODO O3.1.8.1: Check Shift key for free rotation (no snap)
            angle = qRound(angle / 15.0) * 15.0;
            
            obj->rotation = angle;
            return;  // Don't apply resize logic below
        }
    
    // -----------------------------------------------------------------
    // Scale: Use same approach as lasso selection (updateScaleFromHandle)
    // Convert delta to local coordinates using inverse rotation
    // -----------------------------------------------------------------
    
    // BF: Use m_resizeObjectDocCenter (document-global) for scale factor calculation
    // because the pointer position from viewportToDocument() is document-global.
    // In edgeless mode, m_resizeOriginalPosition is tile-local but currentDoc is
    // document-global - this mismatch caused extreme scaling jumps!
    
    // Tile-local center (for final position calculation - obj->position is tile-local)
    QPointF center = m_resizeOriginalPosition + 
                     QPointF(m_resizeOriginalSize.width() / 2.0, 
                             m_resizeOriginalSize.height() / 2.0);
    
    // Original half-sizes (distances from center to edges in local space)
    qreal halfW = m_resizeOriginalSize.width() / 2.0;
    qreal halfH = m_resizeOriginalSize.height() / 2.0;
    
    // Get current pointer position relative to document-global center
    // (both values are now in document coordinates)
    qreal dx = currentDoc.x() - m_resizeObjectDocCenter.x();
    qreal dy = currentDoc.y() - m_resizeObjectDocCenter.y();
    
    // Convert to local coordinates using inverse rotation
    // (same math as lasso updateScaleFromHandle)
    qreal rotRad = qDegreesToRadians(m_resizeOriginalRotation);
    qreal cosR = qCos(-rotRad);  // Inverse rotation
    qreal sinR = qSin(-rotRad);
    qreal localX = dx * cosR - dy * sinR;
    qreal localY = dx * sinR + dy * cosR;
    
    // Calculate scale factors based on which handle is being dragged
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    
    // Determine which edges are being scaled
    // Positive half-size = right/bottom edge, negative = left/top edge
    switch (m_objectResizeHandle) {
        case HandleHit::TopLeft:
            if (halfW > 0.001) scaleX = -localX / halfW;  // Left edge: -halfW
            if (halfH > 0.001) scaleY = -localY / halfH;  // Top edge: -halfH
            break;
        case HandleHit::Top:
            if (halfH > 0.001) scaleY = -localY / halfH;
            break;
        case HandleHit::TopRight:
            if (halfW > 0.001) scaleX = localX / halfW;   // Right edge: +halfW
            if (halfH > 0.001) scaleY = -localY / halfH;
            break;
        case HandleHit::Left:
            if (halfW > 0.001) scaleX = -localX / halfW;
            break;
        case HandleHit::Right:
            if (halfW > 0.001) scaleX = localX / halfW;
            break;
        case HandleHit::BottomLeft:
            if (halfW > 0.001) scaleX = -localX / halfW;
            if (halfH > 0.001) scaleY = localY / halfH;   // Bottom edge: +halfH
            break;
        case HandleHit::Bottom:
            if (halfH > 0.001) scaleY = localY / halfH;
            break;
        case HandleHit::BottomRight:
            if (halfW > 0.001) scaleX = localX / halfW;
            if (halfH > 0.001) scaleY = localY / halfH;
            break;
        default:
            return;
    }
    
    // Clamp scale factors (prevent flip and ensure minimum size)
    const qreal MIN_SCALE = 0.1;
    const qreal MAX_SCALE = 10.0;
    scaleX = qBound(MIN_SCALE, scaleX, MAX_SCALE);
    scaleY = qBound(MIN_SCALE, scaleY, MAX_SCALE);
    
    // Calculate new size
    QSizeF newSize(m_resizeOriginalSize.width() * scaleX,
                   m_resizeOriginalSize.height() * scaleY);
    
    // Enforce minimum size
    const qreal MIN_SIZE = 10.0;
    if (newSize.width() < MIN_SIZE) newSize.setWidth(MIN_SIZE);
    if (newSize.height() < MIN_SIZE) newSize.setHeight(MIN_SIZE);
    
    // Calculate new position (keeping center fixed)
    // Position is top-left corner, which is center - half of new size
    QPointF newPos = center - QPointF(newSize.width() / 2.0, newSize.height() / 2.0);
    
    // Apply to object
    obj->position = newPos;
    obj->size = newSize;
}

QRectF DocumentViewport::visibleRect() const
{
    // Convert viewport bounds to document coordinates
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    return QRectF(m_panOffset, QSizeF(viewWidth, viewHeight));
}

QVector<int> DocumentViewport::visiblePages() const
{
    QVector<int> result;
    
    if (!m_document || m_document->pageCount() == 0) {
        return result;
    }
    
    // For edgeless documents, page 0 is always visible
    if (m_document->isEdgeless()) {
        result.append(0);
        return result;
    }
    
    // Ensure cache is valid for O(1) page position lookup
    ensurePageLayoutCache();
    
    QRectF viewRect = visibleRect();
    int pageCount = m_document->pageCount();
    
    // For single column: use binary search to find visible range (O(log n))
    if (m_layoutMode == LayoutMode::SingleColumn && !m_pageYCache.isEmpty()) {
        qreal viewTop = viewRect.top();
        qreal viewBottom = viewRect.bottom();
        
        // Binary search for first page that might be visible
        int low = 0;
        int high = pageCount - 1;
        int firstCandidate = pageCount;  // Beyond last page
        
        while (low <= high) {
            int mid = (low + high) / 2;
            qreal pageY = m_pageYCache[mid];
            // PERF FIX: Use pageSizeAt() to avoid triggering lazy loading in binary search
            QSizeF pageSize = m_document->pageSizeAt(mid);
            qreal pageBottom = pageY + pageSize.height();
            
            if (pageBottom < viewTop) {
                // Page is entirely above viewport
                low = mid + 1;
            } else {
                // Page might be visible
                firstCandidate = mid;
                high = mid - 1;
            }
        }
        
        // Now iterate from first candidate until pages are below viewport
        for (int i = firstCandidate; i < pageCount; ++i) {
            qreal pageY = m_pageYCache[i];
            if (pageY > viewBottom) {
                // This and all subsequent pages are below viewport
                break;
            }
            
            QRectF rect = pageRect(i);  // O(1) now
            if (rect.intersects(viewRect)) {
                result.append(i);
            }
        }
        
        return result;
    }
    
    // PERF FIX: For two-column, use binary search on Y cache to find visible rows
    // Then only check pages in those rows instead of all 3600+ pages
    if (!m_pageYCache.isEmpty()) {
        qreal viewTop = viewRect.top();
        qreal viewBottom = viewRect.bottom();
        
        // Binary search for first row that might be visible
        // In two-column mode, rows are at even indices (0, 2, 4, ...)
        int numRows = (pageCount + 1) / 2;
        int low = 0;
        int high = numRows - 1;
        int firstRow = numRows;  // Beyond last row
        
        while (low <= high) {
            int mid = (low + high) / 2;
            int pageIdx = mid * 2;  // First page of row
            qreal rowY = m_pageYCache[pageIdx];
            
            // Get row height (max of both pages in row)
            QSizeF leftSize = m_document->pageSizeAt(pageIdx);
            QSizeF rightSize = (pageIdx + 1 < pageCount) ? m_document->pageSizeAt(pageIdx + 1) : QSizeF();
            qreal rowHeight = qMax(leftSize.height(), rightSize.height());
            qreal rowBottom = rowY + rowHeight;
            
            if (rowBottom < viewTop) {
                // Row is entirely above viewport
                low = mid + 1;
            } else {
                // Row might be visible
                firstRow = mid;
                high = mid - 1;
            }
        }
        
        // Now iterate from first visible row until rows are below viewport
        for (int row = firstRow; row < numRows; ++row) {
            int leftIdx = row * 2;
            qreal rowY = m_pageYCache[leftIdx];
            
            if (rowY > viewBottom) {
                // This and all subsequent rows are below viewport
                break;
            }
            
            // Check both pages in row
            QRectF leftRect = pageRect(leftIdx);
            if (leftRect.intersects(viewRect)) {
                result.append(leftIdx);
            }
            
            int rightIdx = leftIdx + 1;
            if (rightIdx < pageCount) {
                QRectF rightRect = pageRect(rightIdx);
                if (rightRect.intersects(viewRect)) {
                    result.append(rightIdx);
                }
            }
        }
        
        return result;
    }
    
    // Fallback: linear search if cache not available
    for (int i = 0; i < pageCount; ++i) {
        QRectF rect = pageRect(i);
        if (rect.intersects(viewRect)) {
            result.append(i);
        }
    }
    
    return result;
}

// ===== Qt Event Overrides =====

void DocumentViewport::paintEvent(QPaintEvent* event)
{
    // Benchmark: track paint timestamps (Task 2.6)
    if (m_benchmarking) {
        m_paintTimestamps.push_back(m_benchmarkTimer.elapsed());
    }
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // ========== FAST PATH: Viewport Gesture (Zoom or Pan) ==========
    // During viewport gestures, draw transformed cached frame instead of re-rendering.
    // This provides 60+ FPS during rapid zoom/pan operations.
    if (m_gesture.isActive() && !m_gesture.cachedFrame.isNull() 
        && m_gesture.startZoom > 0) {  // Guard against division by zero
        
        // Fill background (for areas outside transformed frame)
        painter.fillRect(rect(), m_backgroundColor);
        
        // Calculate frame size in LOGICAL pixels (not physical)
        // grab() returns a pixmap at device pixel ratio, so we must divide by DPR
        // to get the logical size that matches the widget's coordinate system
        qreal dpr = m_gesture.frameDevicePixelRatio;
        QSizeF logicalSize(m_gesture.cachedFrame.width() / dpr,
                           m_gesture.cachedFrame.height() / dpr);
        
        // Draw based on gesture type
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);  // Speed over quality
        
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            // ZOOM + PAN: Scale the cached frame around zoom center, with pan offset
            qreal relativeScale = m_gesture.targetZoom / m_gesture.startZoom;
            QSizeF scaledSize = logicalSize * relativeScale;
            
            // The zoom center should remain fixed in viewport coords
            QPointF center = m_gesture.zoomCenter;
            QPointF scaledOrigin = center - (center * relativeScale);
            
            // Add pan offset from centroid movement (gallery-style 2-finger gesture)
            // Pan is in document coords, convert to viewport pixels at START zoom level
            // Then scale by relativeScale since the cached frame is being scaled
            if (m_gesture.initialCentroidSet) {
                QPointF panDeltaDoc = m_gesture.targetPan - m_gesture.startPan;
                // Convert to viewport pixels: doc coords * zoom = pixels
                // Use startZoom since we're transforming the original cached frame
                // Negate because pan offset increase = viewport content moves opposite
                QPointF panDeltaPixels = panDeltaDoc * m_gesture.startZoom * -1.0;
                // The pan needs to be applied at the scaled size
                scaledOrigin += panDeltaPixels * relativeScale;
            }
            
            painter.drawPixmap(QRectF(scaledOrigin, scaledSize), m_gesture.cachedFrame, 
                              m_gesture.cachedFrame.rect());
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            // PAN: Shift the cached frame by pan delta
            // Pan delta in document coords → convert to viewport pixels
            QPointF panDeltaDoc = m_gesture.targetPan - m_gesture.startPan;
            QPointF panDeltaPixels = panDeltaDoc * m_gesture.startZoom * -1.0;  // Negate: pan offset increase = viewport moves opposite
            
            painter.drawPixmap(panDeltaPixels, m_gesture.cachedFrame);
        }
        
        // Skip normal rendering during gesture
        return;
    }
    
    // ========== FAST PATH: Selection Transform ==========
    // During selection transform, draw cached background + transformed selection cache.
    // This avoids re-rendering all tiles/pages, providing smooth transform performance.
    if (m_isTransformingSelection && !m_selectionBackgroundSnapshot.isNull() 
        && m_lassoSelection.isValid() && !m_skipSelectionRendering) {
        
        // Draw the cached background (viewport without selection)
        qreal dpr = m_backgroundSnapshotDpr;
        QSizeF logicalSize(m_selectionBackgroundSnapshot.width() / dpr,
                           m_selectionBackgroundSnapshot.height() / dpr);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(QRectF(QPointF(0, 0), logicalSize), m_selectionBackgroundSnapshot,
                          m_selectionBackgroundSnapshot.rect());
        
        // Render the selection with its current transform (uses P3 cache)
        renderLassoSelection(painter);
        
        // Draw eraser cursor if needed
        drawEraserCursor(painter);
        
        // Skip normal rendering during transform
        return;
    }
    
    // ========== FAST PATH: Object Drag/Resize (Phase O4.1) ==========
    // During object drag/resize, draw cached background + objects at current position.
    // Same optimization pattern as lasso selection transform above.
    if ((m_isDraggingObjects || m_isResizingObject) 
        && !m_objectDragBackgroundSnapshot.isNull()
        && !m_skipSelectedObjectRendering) {
        
        // Draw the cached background (viewport without selected objects)
        qreal dpr = m_objectDragSnapshotDpr;
        QSizeF logicalSize(m_objectDragBackgroundSnapshot.width() / dpr,
                           m_objectDragBackgroundSnapshot.height() / dpr);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(QRectF(QPointF(0, 0), logicalSize), m_objectDragBackgroundSnapshot,
                          m_objectDragBackgroundSnapshot.rect());
        
        // Render only the selected objects at their current positions
        renderSelectedObjectsOnly(painter);
        
        // Skip normal rendering during drag/resize
        return;
    }
    
    // ========== OPTIMIZATION: Dirty Region Rendering ==========
    // Only repaint what's needed. During stroke drawing, the dirty region is small.
    QRect dirtyRect = event->rect();
    bool isPartialUpdate = (dirtyRect.width() < width() / 2 || dirtyRect.height() < height() / 2);
    
    // Fill background - only the dirty region for partial updates
    if (isPartialUpdate) {
        painter.fillRect(dirtyRect, m_backgroundColor);
    } else {
        painter.fillRect(rect(), m_backgroundColor);
    }
    
    if (!m_document) {
        // No document - draw placeholder
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, 
                         tr("No document loaded"));
        return;
    }
    
    // ========== EDGELESS MODE ==========
    // Edgeless uses tiled rendering instead of page-based rendering
    if (m_document->isEdgeless()) {
        renderEdgelessMode(painter);
        
        // Draw eraser cursor
        if (!m_isDrawing || !isPartialUpdate) {
            drawEraserCursor(painter);
        }
        
        // Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
        // Toggle with Ctrl+Shift+D
        
        return;  // Done with edgeless rendering
    }
    
    // ========== PAGED MODE ==========
    // Get visible pages to render
    QVector<int> visible = visiblePages();
    
    // Apply view transform
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // Render each visible page
    // For partial updates, only render pages that intersect the dirty region
    for (int pageIdx : visible) {
        Page* page = m_document->page(pageIdx);
        if (!page) continue;
        
        // Get page position once (O(1) with cache, but avoid redundant calls)
        QPointF pos = pagePosition(pageIdx);
        
        // Check if this page intersects the dirty region (optimization for partial updates)
        if (isPartialUpdate) {
            QRectF pageRectInViewport = QRectF(
                (pos.x() - m_panOffset.x()) * m_zoomLevel,
                (pos.y() - m_panOffset.y()) * m_zoomLevel,
                page->size.width() * m_zoomLevel,
                page->size.height() * m_zoomLevel
            );
            if (!pageRectInViewport.intersects(dirtyRect)) {
                continue;  // Skip this page - it doesn't intersect dirty region
            }
        }
        
        painter.save();
        painter.translate(pos);
        
        // Render the page (background + content)
        renderPage(painter, page, pageIdx);
        
        painter.restore();
    }
    
    painter.restore();
    
    // Render current stroke with incremental caching (Task 2.3)
    // This is done AFTER restoring the painter transform because the cache
    // is in viewport coordinates (not document coordinates)
    if (m_isDrawing && !m_currentStroke.points.isEmpty() && m_activeDrawingPage >= 0) {
        renderCurrentStrokeIncremental(painter);
    }
    
    // Task 2.9: Draw straight line preview
    if (m_isDrawingStraightLine) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Transform coordinates to viewport
        QPointF vpStart, vpEnd;
        if (m_document && m_document->isEdgeless()) {
            // Edgeless: coordinates are in document space
            vpStart = documentToViewport(m_straightLineStart);
            vpEnd = documentToViewport(m_straightLinePreviewEnd);
        } else {
            // Paged: coordinates are in page-local space
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            vpStart = documentToViewport(m_straightLineStart + pageOrigin);
            vpEnd = documentToViewport(m_straightLinePreviewEnd + pageOrigin);
        }
        
        // Use current tool's color and thickness
        QColor previewColor = (m_currentTool == ToolType::Marker) 
                              ? m_markerColor : m_penColor;
        qreal previewThickness = (m_currentTool == ToolType::Marker)
                                 ? m_markerThickness : m_penThickness;
        
        QPen pen(previewColor, previewThickness * m_zoomLevel, 
                 Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(vpStart, vpEnd);
        
        painter.restore();
    }
    
    // Task 2.10: Draw lasso selection path while drawing
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if (m_isDrawingLasso && m_lassoPath.size() > 1) {
        renderLassoPathIncremental(painter);
    }
    
    // Task 2.10.3: Draw lasso selection (selected strokes + bounding box)
    // P5: Skip during background snapshot capture
    if (m_lassoSelection.isValid() && !m_skipSelectionRendering) {
        renderLassoSelection(painter);
    }
    
    // Phase O2: Draw object selection (bounding boxes, handles, hover)
    // Phase O4.1: Skip during background snapshot capture
    if ((m_currentTool == ToolType::ObjectSelect || !m_selectedObjects.isEmpty()) 
        && !m_skipSelectedObjectRendering) {
        renderObjectSelection(painter);
    }
    
    // Draw eraser cursor (Task 2.4)
    // Skip during stroke drawing (partial updates for pen don't need eraser cursor)
    if (!m_isDrawing || !isPartialUpdate) {
        drawEraserCursor(painter);
    }
    
    // Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
    // Toggle with Ctrl+Shift+D
}

void DocumentViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    // End any gesture if active (cached frame size no longer matches)
    if (m_gesture.isActive()) {
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            endZoomGesture();
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
    }
    
    // Keep the same document point at viewport center after resize
    // This ensures content doesn't jump around during window resize or rotation
    
    if (!m_document || event->oldSize().isEmpty()) {
        // No document or first resize
        
        // BUG FIX: If edgeless position restore is pending (showEvent couldn't do it
        // because widget had zero dimensions), do it now that we have valid size
        if (m_document && m_document->isEdgeless() && m_needsPositionRestore) {
            if (applyRestoredEdgelessPosition()) {
                m_needsPositionRestore = false;
            }
        }
        
        clampPanOffset();
        update();
        emitScrollFractions();
        return;
    }
    
    // Calculate the document point that was at the center of the OLD viewport
    QPointF oldCenter(event->oldSize().width() / 2.0, event->oldSize().height() / 2.0);
    QPointF docPointAtOldCenter = oldCenter / m_zoomLevel + m_panOffset;
    
    // Calculate where the NEW center is in viewport coordinates
    QPointF newCenter(width() / 2.0, height() / 2.0);
    
    // Adjust pan offset so the same document point is at the NEW center
    // docPointAtOldCenter = newCenter / m_zoomLevel + m_panOffset
    // m_panOffset = docPointAtOldCenter - newCenter / m_zoomLevel
    m_panOffset = docPointAtOldCenter - newCenter / m_zoomLevel;
    
    // Clamp to valid bounds (content may now be smaller/larger relative to viewport)
    clampPanOffset();
    
    // Re-center horizontally if content is narrower than viewport
    // This fixes the issue where sidebar toggle causes page shift:
    // - Sidebar opens → viewport shrinks → page switch centers for narrow viewport
    // - Sidebar closes → viewport expands → we need to recenter for wider viewport
    // Only recenter when content is narrower than viewport (not when user has zoomed in)
    QSizeF contentSize = totalContentSize();
    qreal viewportWidth = width() / m_zoomLevel;
    if (contentSize.width() < viewportWidth) {
        recenterHorizontally();
    }
    
    // Update current page index (visible area changed)
    updateCurrentPageIndex();
    
    // Check if auto-layout should switch modes based on new viewport width
    checkAutoLayout();
    
    // Emit signals and repaint
    emit panChanged(m_panOffset);
    emitScrollFractions();
    update();
    
    // Update missing PDF banner width if visible
    if (m_missingPdfBanner && m_missingPdfBanner->isVisible()) {
        m_missingPdfBanner->setFixedWidth(width());
    }
}

void DocumentViewport::mousePressEvent(QMouseEvent* event)
{
    // Only handle left button for drawing
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    // CRITICAL: Reject touch-synthesized mouse events
    // Touch input should not draw - only stylus and real mouse
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active (avoid duplicate events)
    if (m_pointerActive && m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Press);
    handlePointerEvent(pe);
    event->accept();
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event)
{
    // CRITICAL: Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active
    if (m_pointerActive && m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    // Process move if we have an active pointer or for hover
    if (m_pointerActive || (event->buttons() & Qt::LeftButton)) {
        PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Move);
        handlePointerEvent(pe);
    } else {
        // Track position for eraser cursor even when not pressing (hover)
        QPointF oldPos = m_lastPointerPos;
        m_lastPointerPos = event->position();
        
        // Request repaint if eraser tool is active (to update cursor)
        // Use elliptical regions to match circular eraser cursor
        // Use toAlignedRect() to properly round floating-point to integer coords
        if (m_currentTool == ToolType::Eraser) {
            qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
            QRectF newRectF(m_lastPointerPos.x() - eraserRadius, m_lastPointerPos.y() - eraserRadius,
                            eraserRadius * 2, eraserRadius * 2);
            QRectF oldRectF(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                            eraserRadius * 2, eraserRadius * 2);
            QRegion dirtyRegion(oldRectF.toAlignedRect(), QRegion::Ellipse);
            dirtyRegion += QRegion(newRectF.toAlignedRect(), QRegion::Ellipse);
            update(dirtyRegion);
        }
        // Phase D.1: Update cursor for PDF link hover in Highlighter tool
        else if (m_currentTool == ToolType::Highlighter) {
            updateLinkCursor(m_lastPointerPos);
        }
    }
    event->accept();
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    // CRITICAL: Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore if tablet is active
    if (m_activeSource == PointerEvent::Stylus) {
        event->accept();
        return;
    }
    
    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Release);
    handlePointerEvent(pe);
    event->accept();
}

void DocumentViewport::wheelEvent(QWheelEvent* event)
{
    if (!m_document) {
        event->ignore();
        return;
    }
    
    // Get scroll delta (in degrees * 8, or pixels for high-res touchpads)
    QPoint pixelDelta = event->pixelDelta();
    QPoint angleDelta = event->angleDelta();
    
    // Check for Ctrl modifier → Zoom (deferred rendering)
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom at cursor position using deferred gesture API
        qreal zoomDelta = 0;
        
        if (!angleDelta.isNull()) {
            // Mouse wheel: 120 units = 15 degrees = one "step"
            zoomDelta = angleDelta.y() / 120.0;
        } else if (!pixelDelta.isNull()) {
            // Touchpad: use pixel delta scaled down
            zoomDelta = pixelDelta.y() / 50.0;
        }
        
        if (qFuzzyIsNull(zoomDelta)) {
            event->accept();
            return;
        }
        
        // Calculate zoom factor (multiplicative for consistent feel)
        qreal zoomFactor = qPow(1.1, zoomDelta);  // 10% per step
        
        // Use deferred zoom gesture API (will capture frame on first call)
        updateZoomGesture(zoomFactor, event->position());
        
        event->accept();
        return;
    }
    
    // Scroll with deferred rendering for Shift/backtick modifiers
    QPointF scrollDelta;
    
    if (!pixelDelta.isNull()) {
        // Touchpad: use pixel delta directly (in viewport pixels)
        // Convert to document units
        scrollDelta = QPointF(-pixelDelta.x(), -pixelDelta.y()) / m_zoomLevel;
    } else if (!angleDelta.isNull()) {
        // Mouse wheel: convert degrees to scroll distance
        // 120 units = one step, scroll by ~40 document units per step
        // CUSTOMIZABLE: Scroll speed (user preference, range: 10-100)
        qreal scrollSpeed = 40.0;  // TODO: Load from user settings
        scrollDelta.setX(-angleDelta.x() / 120.0 * scrollSpeed);
        scrollDelta.setY(-angleDelta.y() / 120.0 * scrollSpeed);
    }
    
    if (!scrollDelta.isNull()) {
        // Check for Shift modifier → Deferred horizontal pan
        if (event->modifiers() & Qt::ShiftModifier) {
            // Swap X and Y for horizontal scroll, then use deferred pan
            QPointF horizontalDelta(scrollDelta.y(), scrollDelta.x());
            updatePanGesture(horizontalDelta);
            event->accept();
            return;
        }
        
        // Check for backtick (`) key → Deferred vertical pan
        // Using custom key tracking since ` is not a modifier key
        if (m_backtickHeld) {
            // Vertical scroll with deferred rendering
            updatePanGesture(scrollDelta);
            event->accept();
            return;
        }
        
        // Plain wheel (no modifier) → Immediate scroll (unchanged behavior)
        scrollBy(scrollDelta);
    }
    
    event->accept();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event)
{
    // Track backtick key for deferred vertical pan
    if (event->key() == Qt::Key_QuoteLeft) {
        // Only set flag on initial press, ignore auto-repeat events
        if (!event->isAutoRepeat()) {
        m_backtickHeld = true;
        }
        // Always consume backtick events (initial and auto-repeat) to prevent spam
        event->accept();
        return;
    }
    
    // ===== Note: Most keyboard shortcuts moved to MainWindow =====
    // The following shortcuts are now handled by MainWindow's QShortcut system
    // so they work regardless of which widget has focus:
    // - Tool shortcuts (B, E, L, T, M, V)
    // - Edit shortcuts (Undo, Redo, Copy, Cut, Paste, Delete)
    // - Object manipulation (Z-order, Affinity, Mode switching, Link slots)
    // - Edgeless navigation (Home, Backspace)
    // - PDF/Highlighter features (Auto-highlight)
    //
    // Escape key handling is done via handleEscapeKey() called from MainWindow.
    
    // ===== Note: Tool/Edit/Edgeless shortcuts moved to MainWindow =====
    // Tool shortcuts (B, E, L, T, M, V), Undo/Redo, and Edgeless navigation
    // are now handled by MainWindow's QShortcut system so they work 
    // regardless of which widget has focus.
    
    // ===== Debug Shortcut (kept as hardcoded - development only) =====
#ifdef SPEEDYNOTE_DEBUG
    // F10 = Toggle benchmark (debug builds only, conflicts with tool.pen in release)
    if (event->key() == Qt::Key_F10) {
        if (m_benchmarking) {
            stopBenchmark();
        } else {
            startBenchmark();
        }
        update();
        event->accept();
        return;
    }
#endif
    
    // Pass unhandled keys to parent
    QWidget::keyPressEvent(event);
}

void DocumentViewport::keyReleaseEvent(QKeyEvent* event)
{
    // Ctrl release ends zoom gesture (if active)
    if (event->key() == Qt::Key_Control && m_gesture.activeType == ViewportGestureState::Zoom) {
        endZoomGesture();
        event->accept();
        return;
    }
    
    // Shift release ends pan gesture (if active)
    if (event->key() == Qt::Key_Shift && m_gesture.activeType == ViewportGestureState::Pan) {
        endPanGesture();
        event->accept();
        return;
    }
    
    // Backtick (`) release ends pan gesture (if active)
    // Ignore auto-repeat events - only handle actual key release
    if (event->key() == Qt::Key_QuoteLeft && !event->isAutoRepeat()) {
        m_backtickHeld = false;
        if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
        event->accept();
        return;
    }
    
    // Pass unhandled keys to parent
    QWidget::keyReleaseEvent(event);
}

void DocumentViewport::focusOutEvent(QFocusEvent* event)
{
    // Reset backtick tracking (user can't release key if we don't have focus)
    m_backtickHeld = false;
    
    // End any active gesture if window loses focus (user can't release modifier otherwise)
    if (m_gesture.isActive()) {
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            endZoomGesture();
        } else if (m_gesture.activeType == ViewportGestureState::Pan) {
            endPanGesture();
        }
    }
    
    QWidget::focusOutEvent(event);
}

void DocumentViewport::hideEvent(QHideEvent* event)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] hideEvent - clearing gesture state"
             << "wasActive:" << m_gesture.isActive();
#endif
    
    // BUG-A005 v4 FIX: Clear gesture state when viewport is hidden
    // When user goes to launcher and comes back, any stale gesture state
    // would block new gestures (beginZoomGesture returns early if isActive())
    if (m_gesture.isActive()) {
        m_gesture.reset();
        m_gestureTimeoutTimer->stop();
    }
    
    // Also reset touch handler state including inertia
    // This prevents inertia callbacks from accessing invalid widget state
    if (m_touchHandler) {
        m_touchHandler->reset();
    }
    
    QWidget::hideEvent(event);
}

void DocumentViewport::showEvent(QShowEvent* event)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] showEvent - starting touch cooldown";
#endif
    
    // Start touch cooldown period
    // After sleep/wake or tab switching, Android may send stale touch events
    // that can crash Qt's touch event processing. Reject all touch events
    // for a brief period to let the system stabilize.
    m_touchCooldownActive = true;
    m_touchCooldownTimer.start();
    
    // Also ensure touch handler is reset
    if (m_touchHandler) {
        m_touchHandler->reset();
    }
    
    // BUG FIX: For edgeless documents with saved position, set pan offset NOW
    // BEFORE the base class processes showEvent (which may trigger a paint).
    // This ensures the first paint uses the correct pan offset.
    if (m_document && m_document->isEdgeless() && m_needsPositionRestore) {
        if (applyRestoredEdgelessPosition()) {
            m_needsPositionRestore = false;
        }
        // If restore failed (invalid dimensions), resizeEvent will handle it
    }
    
    QWidget::showEvent(event);
}

#ifdef Q_OS_ANDROID
void DocumentViewport::onApplicationStateChanged(Qt::ApplicationState state)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] Application state changed to:" 
             << (state == Qt::ApplicationActive ? "Active" :
                 state == Qt::ApplicationSuspended ? "Suspended" :
                 state == Qt::ApplicationInactive ? "Inactive" : "Hidden");
#endif
    
    if (state == Qt::ApplicationActive) {
        // App returning to foreground - reset ALL touch state
        // This is critical for Android where Qt's touch tracking gets corrupted
        // after screen lock/unlock or app switching
        if (m_touchHandler) {
            m_touchHandler->reset();
        }
        if (m_gesture.isActive()) {
            m_gesture.reset();
            m_gestureTimeoutTimer->stop();
        }
        
        // Start touch cooldown - reject touches briefly to let system stabilize
        m_touchCooldownActive = true;
        m_touchCooldownTimer.start();
    }
}
#endif

void DocumentViewport::enterEvent(QEnterEvent* event)
{
    m_pointerInViewport = true;
    QWidget::enterEvent(event);
}

void DocumentViewport::leaveEvent(QEvent* event)
{
    m_pointerInViewport = false;
    
    // Trigger repaint to hide eraser cursor when pointer leaves viewport
    // Use elliptical region to match circular cursor shape
    // Use toAlignedRect() to properly round floating-point to integer coords
    if (m_currentTool == ToolType::Eraser || m_hardwareEraserActive) {
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        QRectF cursorRectF(m_lastPointerPos.x() - eraserRadius, m_lastPointerPos.y() - eraserRadius,
                           eraserRadius * 2, eraserRadius * 2);
        update(QRegion(cursorRectF.toAlignedRect(), QRegion::Ellipse));
    }
    
    QWidget::leaveEvent(event);
}

void DocumentViewport::tabletEvent(QTabletEvent* event)
{
    // Determine event type
    PointerEvent::Type peType;
    switch (event->type()) {
        case QEvent::TabletPress:
            peType = PointerEvent::Press;
            break;
        case QEvent::TabletMove:
            peType = PointerEvent::Move;
            break;
        case QEvent::TabletRelease:
            peType = PointerEvent::Release;
            break;
        default:
            event->ignore();
            return;
    }
    
    // ===== Tablet Hover Tracking for Eraser Cursor =====
    // TabletMove events arrive even when the pen is hovering (not pressed).
    // We need to track position for eraser cursor even during hover.
    // handlePointerEvent() returns early if m_pointerActive is false,
    // so we handle hover tracking separately here.
    if (event->type() == QEvent::TabletMove && !m_pointerActive) {
        QPointF newPos = event->position();
        
        // Check if stylus is within widget bounds
        // Unlike mouse, tablet doesn't trigger leaveEvent when stylus moves outside
        m_pointerInViewport = rect().contains(newPos.toPoint());
        
        // Restart hover timer - if no tablet event for 100ms, stylus left
        // This handles the case where stylus hovers to another widget
        // (we stop receiving events, timer fires, cursor hidden)
        if (m_tabletHoverTimer) {
            m_tabletHoverTimer->start();
        }
        
        // Check if eraser tool is active or this is hardware eraser
        bool isEraserHover = (m_currentTool == ToolType::Eraser) ||
                             (event->pointerType() == QPointingDevice::PointerType::Eraser);
        
        if (isEraserHover) {
            QPointF oldPos = m_lastPointerPos;
            m_lastPointerPos = newPos;
            
            // Trigger repaint for eraser cursor update
            // Use elliptical regions to match circular cursor shape
            // Use toAlignedRect() to properly round floating-point to integer coords
            qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
            QRectF oldRectF(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                            eraserRadius * 2, eraserRadius * 2);
            QRectF newRectF(newPos.x() - eraserRadius, newPos.y() - eraserRadius,
                            eraserRadius * 2, eraserRadius * 2);
            QRegion dirtyRegion(oldRectF.toAlignedRect(), QRegion::Ellipse);
            dirtyRegion += QRegion(newRectF.toAlignedRect(), QRegion::Ellipse);
            update(dirtyRegion);
        }
        
        event->accept();
        return;
    }
    
    PointerEvent pe = tabletToPointerEvent(event, peType);
    handlePointerEvent(pe);
    event->accept();
}

// ===== Coordinate Transforms (Task 1.3.5) =====

QPointF DocumentViewport::viewportToDocument(QPointF viewportPt) const
{
    // Viewport coordinates are in logical (widget) pixels
    // Document coordinates are in our custom unit system
    // 
    // The viewport shows a portion of the document:
    // - panOffset is the top-left corner of the viewport in document coords
    // - zoomLevel scales the document (zoom 2.0 = document appears twice as large)
    //
    // viewportPt = (docPt - panOffset) * zoomLevel
    // So: docPt = viewportPt / zoomLevel + panOffset
    
    return viewportPt / m_zoomLevel + m_panOffset;
}

QPointF DocumentViewport::documentToViewport(QPointF docPt) const
{
    // Inverse of viewportToDocument
    // viewportPt = (docPt - panOffset) * zoomLevel
    
    return (docPt - m_panOffset) * m_zoomLevel;
}

QPointF DocumentViewport::viewportCenterInDocument() const
{
    // Phase O2.4.3: Get center of viewport in document coordinates
    // Used for placing newly inserted objects at the center of the view
    QPointF viewportCenter(width() / 2.0, height() / 2.0);
    return viewportToDocument(viewportCenter);
}

int DocumentViewport::getNextZOrderForAffinity(Page* page, int affinity) const
{
    // Find the maximum zOrder among objects with the same affinity
    // New objects should get maxZOrder + 1 to appear on top
    if (!page) {
        return 0;
    }
    
    int maxZOrder = -1;  // Start below 0 so first object gets zOrder = 0
    for (const auto& obj : page->objects) {
        if (obj && obj->getLayerAffinity() == affinity) {
            maxZOrder = qMax(maxZOrder, obj->zOrder);
        }
    }
    
    return maxZOrder + 1;
}

PageHit DocumentViewport::viewportToPage(QPointF viewportPt) const
{
    // Convert viewport → document → page
    QPointF docPt = viewportToDocument(viewportPt);
    return documentToPage(docPt);
}

QPointF DocumentViewport::pageToViewport(int pageIndex, QPointF pagePt) const
{
    // Convert page → document → viewport
    QPointF docPt = pageToDocument(pageIndex, pagePt);
    return documentToViewport(docPt);
}

QPointF DocumentViewport::pageToDocument(int pageIndex, QPointF pagePt) const
{
    // Page-local coordinates are relative to the page's top-left corner
    // Document coordinates are absolute within the document
    //
    // docPt = pagePosition + pagePt
    
    QPointF pagePos = pagePosition(pageIndex);
    return pagePos + pagePt;
}

PageHit DocumentViewport::documentToPage(QPointF docPt) const
{
    PageHit hit;
    
    // Find which page contains this document point
    int pageIdx = pageAtPoint(docPt);
    if (pageIdx < 0) {
        // Point is not on any page (in the gaps or outside content)
        return hit;  // Invalid hit
    }
    
    // Convert document point to page-local coordinates
    QPointF pagePos = pagePosition(pageIdx);
    
    hit.pageIndex = pageIdx;
    hit.pagePoint = docPt - pagePos;
    
    return hit;
}

// ===== Pan & Zoom Helpers (Task 1.3.4) =====

QPointF DocumentViewport::viewportCenter() const
{
    // Get center of viewport in document coordinates
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    return m_panOffset + QPointF(viewWidth / 2, viewHeight / 2);
}

void DocumentViewport::zoomAtPoint(qreal newZoom, QPointF viewportPt)
{
    if (qFuzzyCompare(newZoom, m_zoomLevel)) {
        return;
    }
    
    // Convert viewport point to document coordinates at current zoom
    QPointF docPt = viewportPt / m_zoomLevel + m_panOffset;
    
    // Set new zoom
    qreal oldZoom = m_zoomLevel;
    m_zoomLevel = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    
    // Calculate new pan offset to keep docPt at the same viewport position
    // viewportPt = (docPt - m_panOffset) * m_zoomLevel
    // m_panOffset = docPt - viewportPt / m_zoomLevel
    m_panOffset = docPt - viewportPt / m_zoomLevel;
    
    clampPanOffset();
    updateCurrentPageIndex();
    
    // Check if auto-layout should switch modes (zoom level changed)
    checkAutoLayout();
    
    if (!qFuzzyCompare(oldZoom, m_zoomLevel)) {
        emit zoomChanged(m_zoomLevel);
    }
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    update();
}

// ===== Deferred Zoom Gesture (Task 2.3 - Zoom Optimization) =====

void DocumentViewport::beginZoomGesture(QPointF centerPoint)
{
    if (m_gesture.isActive()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[DocumentViewport] beginZoomGesture BLOCKED - already active!"
                 << "activeType:" << m_gesture.activeType;
#endif
        return;  // Already in gesture
    }
    
    // Safety check: don't start gesture if widget is not in a valid state
    if (!isVisible() || !isEnabled()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[DocumentViewport] beginZoomGesture BLOCKED - widget not visible/enabled";
#endif
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] beginZoomGesture STARTED";
#endif
    m_gesture.activeType = ViewportGestureState::Zoom;
    m_gesture.startZoom = m_zoomLevel;
    m_gesture.targetZoom = m_zoomLevel;
    m_gesture.zoomCenter = centerPoint;
    m_gesture.startPan = m_panOffset;
    m_gesture.targetPan = m_panOffset;
    
    // Track initial centroid for pan calculation during zoom gesture
    // This enables simultaneous pan+zoom (gallery-style 2-finger gestures)
    m_gesture.initialCentroid = centerPoint;
    m_gesture.initialCentroidSet = true;
    
    // Capture current viewport as cached frame for fast scaling
    m_gesture.cachedFrame = grab();
    // Store device pixel ratio for correct scaling on high-DPI displays
    m_gesture.frameDevicePixelRatio = m_gesture.cachedFrame.devicePixelRatio();
    
    // Grab keyboard focus to receive keyReleaseEvent when modifier is released
    setFocus(Qt::OtherFocusReason);
    
    // Start timeout timer (fallback for gesture end detection)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
}

void DocumentViewport::updateZoomGesture(qreal scaleFactor, QPointF centerPoint)
{
    // Auto-begin gesture if not already active
    if (!m_gesture.isActive()) {
        beginZoomGesture(centerPoint);
    }
    
#ifdef SPEEDYNOTE_DEBUG
    static int updateCount = 0;
    updateCount++;
    if (updateCount % 10 == 1) {  // Log every 10th update to avoid spam
        qDebug() << "[DocumentViewport] updateZoomGesture"
                 << "scale:" << scaleFactor
                 << "targetZoom:" << m_gesture.targetZoom * scaleFactor;
    }
#endif
    
    // Accumulate zoom (multiplicative for smooth feel)
    m_gesture.targetZoom *= scaleFactor;
    m_gesture.targetZoom = qBound(MIN_ZOOM, m_gesture.targetZoom, MAX_ZOOM);
    m_gesture.zoomCenter = centerPoint;
    
    // Calculate pan from centroid movement (for gallery-style 2-finger gestures)
    // The centroid movement in viewport pixels needs to be converted to document coords
    // using the START zoom level (since we're transforming the cached frame)
    if (m_gesture.initialCentroidSet) {
        QPointF centroidDelta = centerPoint - m_gesture.initialCentroid;
        // Convert viewport pixels to document coords (at start zoom level)
        // Negate because moving finger right should pan view left (reveal content on right)
        m_gesture.targetPan = m_gesture.startPan - centroidDelta / m_gesture.startZoom;
    }
    
    // Restart timeout timer (each event resets the timeout)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
    
    // Trigger repaint (will use fast cached frame scaling)
    update();
}

void DocumentViewport::endZoomGesture()
{
    if (m_gesture.activeType != ViewportGestureState::Zoom) {
        return;  // Not in zoom gesture
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] endZoomGesture"
             << "finalZoom:" << m_gesture.targetZoom;
#endif
    
    // Stop timeout timer
    m_gestureTimeoutTimer->stop();
    
    // Get final zoom level with mode-specific min zoom
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    qreal finalZoom = qBound(minZ, m_gesture.targetZoom, MAX_ZOOM);
    
    // Calculate new pan offset combining:
    // 1. Zoom center correction (keep center point fixed during zoom)
    // 2. Centroid movement pan (gallery-style 2-finger gesture)
    QPointF center = m_gesture.zoomCenter;
    QPointF docPtAtCenter = center / m_gesture.startZoom + m_gesture.startPan;
    QPointF zoomCorrectedPan = docPtAtCenter - center / finalZoom;
    
    // Add the centroid-based pan offset
    // targetPan already contains startPan + centroid delta, so we need to add
    // just the delta on top of the zoom-corrected pan
    QPointF centroidPanDelta = m_gesture.targetPan - m_gesture.startPan;
    QPointF newPan = zoomCorrectedPan + centroidPanDelta;
    
    // Clear gesture state BEFORE applying zoom (to avoid recursion in paintEvent)
    m_gesture.reset();
    
    // Apply final zoom and pan
    m_zoomLevel = finalZoom;
    m_panOffset = newPan;
    
    // Invalidate PDF cache (DPI changed)
    invalidatePdfCache();
    
    // Clamp and emit signals
    clampPanOffset();
    updateCurrentPageIndex();
    
    emit zoomChanged(m_zoomLevel);
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Trigger full re-render at new DPI
    update();
    
    // Check if auto-layout should switch modes (zoom level changed)
    checkAutoLayout();
    
    // Update PDF cache capacity (visible pages may have changed)
    updatePdfCacheCapacity();
    
    // Preload PDF cache for new zoom level
    preloadPdfCache();
}

void DocumentViewport::beginPanGesture()
{
    if (m_gesture.isActive()) {
        return;  // Already in gesture
    }
    
    // Safety check: don't start gesture if widget is not in a valid state
    if (!isVisible() || !isEnabled()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[DocumentViewport] beginPanGesture BLOCKED - widget not visible/enabled";
#endif
        return;
    }
    
    m_gesture.activeType = ViewportGestureState::Pan;
    m_gesture.startZoom = m_zoomLevel;
    m_gesture.targetZoom = m_zoomLevel;
    m_gesture.startPan = m_panOffset;
    m_gesture.targetPan = m_panOffset;
    
    // Capture current viewport as cached frame for fast shifting
    m_gesture.cachedFrame = grab();
    // Store device pixel ratio for correct positioning on high-DPI displays
    m_gesture.frameDevicePixelRatio = m_gesture.cachedFrame.devicePixelRatio();
    
    // Grab keyboard focus to receive keyReleaseEvent when modifier is released
    setFocus(Qt::OtherFocusReason);
    
    // Start timeout timer (fallback for gesture end detection)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
}

void DocumentViewport::updatePanGesture(QPointF panDelta)
{
    // Auto-begin gesture if not already active
    if (!m_gesture.isActive()) {
        beginPanGesture();
    }
    
    // Accumulate pan offset (additive)
    m_gesture.targetPan += panDelta;
    
    // Note: We don't clamp targetPan here - let endPanGesture handle clamping
    // This allows the visual feedback to show unclamped pan during the gesture
    
    // Restart timeout timer (each event resets the timeout)
    m_gestureTimeoutTimer->start(GESTURE_TIMEOUT_MS);
    
    // Trigger repaint (will use fast cached frame shifting)
    update();
}

void DocumentViewport::endPanGesture()
{
    if (m_gesture.activeType != ViewportGestureState::Pan) {
        return;  // Not in pan gesture
    }
    
    // Stop timeout timer
    m_gestureTimeoutTimer->stop();
    
    // Get final pan offset
    QPointF finalPan = m_gesture.targetPan;
    
    // Clear gesture state BEFORE applying pan (to avoid recursion in paintEvent)
    m_gesture.reset();
    
    // Apply final pan
    m_panOffset = finalPan;
    
    // Clamp and emit signals
    clampPanOffset();
    updateCurrentPageIndex();
    
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // Trigger full re-render
    update();
    
    // Update PDF cache capacity (visible pages may have changed)
    updatePdfCacheCapacity();
    
    // Preload PDF cache for new viewport position
    preloadPdfCache();
    
    // Evict distant tiles if in edgeless mode
    if (m_document && m_document->isEdgeless()) {
        evictDistantTiles();
    }
}

void DocumentViewport::onGestureTimeout()
{
    // Timeout reached - end the active gesture
    if (m_gesture.activeType == ViewportGestureState::Zoom) {
        endZoomGesture();  // This now calls checkAutoLayout() internally
    } else if (m_gesture.activeType == ViewportGestureState::Pan) {
        endPanGesture();   // No checkAutoLayout() needed - zoom unchanged
    }
}

// ===== Touch Gesture Mode (Task TG.1) =====

void DocumentViewport::setTouchGestureMode(TouchGestureMode mode)
{
    if (m_touchHandler) {
        m_touchHandler->setMode(mode);
    }
}

TouchGestureMode DocumentViewport::touchGestureMode() const
{
    if (m_touchHandler) {
        return m_touchHandler->mode();
    }
    return TouchGestureMode::Disabled;
}

bool DocumentViewport::event(QEvent* event)
{
    // ===== Tablet Proximity Events =====
    // These are sent when the stylus enters or leaves the detection range of the tablet.
    // Used to hide eraser cursor when pen is lifted away from the tablet surface.
    if (event->type() == QEvent::TabletEnterProximity) {
        m_pointerInViewport = true;
        return true;
    }
    
    if (event->type() == QEvent::TabletLeaveProximity) {
        m_pointerInViewport = false;
        
        // Stop hover timer - no need to wait for timeout, we know stylus left
        if (m_tabletHoverTimer) {
            m_tabletHoverTimer->stop();
        }
        
        // Trigger repaint to hide eraser cursor when pen leaves proximity
        // Use elliptical region to match circular cursor shape
        // Use toAlignedRect() to properly round floating-point to integer coords
        if (m_currentTool == ToolType::Eraser || m_hardwareEraserActive) {
            qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
            QRectF cursorRectF(m_lastPointerPos.x() - eraserRadius, m_lastPointerPos.y() - eraserRadius,
                               eraserRadius * 2, eraserRadius * 2);
            update(QRegion(cursorRectF.toAlignedRect(), QRegion::Ellipse));
        }
        return true;
    }
    
    // Forward touch events to handler
    if (event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd ||
        event->type() == QEvent::TouchCancel) {
        
        // Touch cooldown: reject all touch events briefly after becoming visible
        // This prevents crashes from stale touch state after sleep/wake on Android
        if (m_touchCooldownActive) {
            if (m_touchCooldownTimer.elapsed() < TOUCH_COOLDOWN_MS) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[DocumentViewport] Touch event rejected - cooldown active"
                         << "elapsed:" << m_touchCooldownTimer.elapsed() << "ms";
#endif
                event->accept();  // Accept but ignore
                return true;
            } else {
                // Cooldown expired
                m_touchCooldownActive = false;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "[DocumentViewport] Touch cooldown ended";
#endif
            }
        }
        
        QTouchEvent* touchEvent = static_cast<QTouchEvent*>(event);
        
        // Check if the touch started on a child widget (like MissingPdfBanner)
        // If so, let Qt's normal event propagation handle it instead of intercepting
        if (event->type() == QEvent::TouchBegin && !touchEvent->points().isEmpty()) {
            QPointF touchPos = touchEvent->points().first().position();
            QWidget* childWidget = childAt(touchPos.toPoint());
            
            // If touch is on a child widget (not directly on DocumentViewport),
            // let Qt handle normal event propagation to the child
            if (childWidget && childWidget != this) {
                // Don't intercept - let the event propagate to child widgets
                // This allows banner buttons, etc. to receive touch input
                return QWidget::event(event);
            }
        }
        
        if (m_touchHandler && m_touchHandler->handleTouchEvent(touchEvent)) {
            return true;
        }
    }
    
    return QWidget::event(event);
}

// ===== PDF Cache Helpers (Task 1.3.6) =====

QPixmap DocumentViewport::getCachedPdfPage(int pageIndex, qreal dpi)
{
    if (!m_document || !m_document->isPdfLoaded()) {
        return QPixmap();
    }
    
    // Thread-safe cache lookup
    QMutexLocker locker(&m_pdfCacheMutex);
    
    // Check if we have this page cached at the right DPI
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(pageIndex, dpi)) {
            return entry.pixmap;  // Cache hit - fast path
        }
    }
    
    // Cache miss - render synchronously (for visible pages that MUST be shown)
    // This should only happen on first paint of a new page
    locker.unlock();  // Release mutex during expensive render
    
#ifdef SPEEDYNOTE_DEBUG
    // Build cache contents string for debug
    QString cacheContents;
    {
        QMutexLocker debugLocker(&m_pdfCacheMutex);
        for (const PdfCacheEntry& e : m_pdfCache) {
            if (!cacheContents.isEmpty()) cacheContents += ",";
            cacheContents += QString::number(e.pageIndex);
        }
    }
    qDebug() << "PDF CACHE MISS: rendering page" << pageIndex 
             << "| cache has [" << cacheContents << "] capacity=" << m_pdfCacheCapacity;
#endif
    
    // Render the page (expensive operation - done outside mutex)
    QImage pdfImage = m_document->renderPdfPageToImage(pageIndex, dpi);
    if (pdfImage.isNull()) {
        return QPixmap();
    }
    
    QPixmap pixmap = QPixmap::fromImage(pdfImage);
    
    // Add to cache (thread-safe)
    locker.relock();
    
    // Double-check it wasn't added by another thread while we were rendering
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(pageIndex, dpi)) {
            return entry.pixmap;  // Another thread added it
        }
    }
    
    PdfCacheEntry entry;
    entry.pageIndex = pageIndex;
    entry.dpi = dpi;
    entry.pixmap = pixmap;
    
    // If cache is full, evict the page FURTHEST from current page (smart eviction)
    // This prevents evicting pages we're about to need (like the next visible page)
    if (m_pdfCache.size() >= m_pdfCacheCapacity) {
        int evictIndex = 0;
        int maxDistance = -1;
        for (int i = 0; i < m_pdfCache.size(); ++i) {
            int distance = qAbs(m_pdfCache[i].pageIndex - pageIndex);
            if (distance > maxDistance) {
                maxDistance = distance;
                evictIndex = i;
            }
        }
        m_pdfCache.removeAt(evictIndex);
    }
    
    m_pdfCache.append(entry);
    m_cachedDpi = dpi;
    
    return pixmap;
}

void DocumentViewport::preloadPdfCache()
{
    // Debounce: restart timer on each call
    // Actual preloading happens after user stops scrolling
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->start(PDF_PRELOAD_DELAY_MS);
    }
}

void DocumentViewport::doAsyncPdfPreload()
{
    if (!m_document || !m_document->isPdfLoaded()) {
        return;
    }
    
    QVector<int> visible = visiblePages();
    if (visible.isEmpty()) {
        return;
    }
    
    int first = visible.first();
    int last = visible.last();
    
    // Pre-load buffer depends on layout mode:
    // - Single column: ±1 page (above and below)
    // - Two column: ±2 pages (1 row above + 1 row below = 4 pages)
    int preloadBuffer = (m_layoutMode == LayoutMode::TwoColumn) ? 2 : 1;
    
    int preloadStart = qMax(0, first - preloadBuffer);
    int preloadEnd = qMin(m_document->pageCount() - 1, last + preloadBuffer);
    
    qreal dpi = effectivePdfDpi();
    QString pdfPath = m_document->pdfPath();
    
    if (pdfPath.isEmpty()) {
        return;  // No PDF path available
    }
    
    // Collect pages that need preloading
    QList<int> pagesToPreload;
    {
        QMutexLocker locker(&m_pdfCacheMutex);
        for (int i = preloadStart; i <= preloadEnd; ++i) {
            Page* page = m_document->page(i);
            if (page && page->backgroundType == Page::BackgroundType::PDF) {
                int pdfPageNum = page->pdfPageNumber;
                
                // Check if already cached
                bool alreadyCached = false;
                for (const PdfCacheEntry& entry : m_pdfCache) {
                    if (entry.matches(pdfPageNum, dpi)) {
                        alreadyCached = true;
                        break;
                    }
                }
                
                if (!alreadyCached) {
                    pagesToPreload.append(pdfPageNum);
                }
            }
        }
    }
    
    if (pagesToPreload.isEmpty()) {
        return;  // All pages already cached
    }
    
    // Launch async render for each page that needs caching
    for (int pdfPageNum : pagesToPreload) {
        QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
        
        // Track watcher for cleanup
        m_activePdfWatchers.append(watcher);
        
        // THREAD SAFETY FIX: QPixmap must only be created on the main thread.
        // The background thread returns QImage, and we convert to QPixmap here
        // in the finished handler which runs on the main thread.
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, pdfPageNum, dpi]() {
            // BUG-A006 FIX: Check if watcher was cancelled (e.g., by invalidatePdfCache)
            // This happens when document/page changes while render is in progress
            bool wasActive = m_activePdfWatchers.removeOne(watcher);
            watcher->deleteLater();
            
            if (!wasActive || watcher->isCanceled()) {
                // Watcher was removed by invalidatePdfCache - discard stale result
                return;
            }
            
            // Get the rendered image from the background task
            QImage pdfImage = watcher->result();
            
            // Check if rendering failed
            if (pdfImage.isNull()) {
                return;
            }
            
            // SAFE: QPixmap::fromImage on main thread
            QPixmap pixmap = QPixmap::fromImage(pdfImage);
            
            // Add to cache (thread-safe access to shared cache)
            QMutexLocker locker(&m_pdfCacheMutex);
            
            // Check if already added (race condition prevention)
            for (const PdfCacheEntry& entry : m_pdfCache) {
                if (entry.matches(pdfPageNum, dpi)) {
                    return;  // Already cached by another path
                }
            }
            
            PdfCacheEntry entry;
            entry.pageIndex = pdfPageNum;
            entry.dpi = dpi;
            entry.pixmap = pixmap;
            
            // Evict page FURTHEST from this page (smart eviction)
            if (m_pdfCache.size() >= m_pdfCacheCapacity) {
                int evictIndex = 0;
                int maxDistance = -1;
                for (int i = 0; i < m_pdfCache.size(); ++i) {
                    int distance = qAbs(m_pdfCache[i].pageIndex - pdfPageNum);
                    if (distance > maxDistance) {
                        maxDistance = distance;
                        evictIndex = i;
                    }
                }
                m_pdfCache.removeAt(evictIndex);
            }
            
            m_pdfCache.append(entry);
            m_cachedDpi = dpi;
            
            // Trigger repaint to show newly cached page
            update();
        });
        
        // Background thread: render PDF to QImage (thread-safe)
        // NOTE: QImage is explicitly documented as thread-safe for read operations
        // and can be safely passed between threads.
        QFuture<QImage> future = QtConcurrent::run([pdfPageNum, dpi, pdfPath]() -> QImage {
            // Create thread-local PDF provider (each thread loads its own copy)
            // Uses factory method to get platform-appropriate backend (Poppler/MuPDF)
            auto threadPdf = PdfProvider::create(pdfPath);
            if (!threadPdf || !threadPdf->isValid()) {
                return QImage();  // Return null image on failure
            }
            
            // Render page using thread-local provider
            // This is the expensive operation (50-200ms) that we're offloading
            return threadPdf->renderPageToImage(pdfPageNum, dpi);
        });
        
        watcher->setFuture(future);
    }
    /*
    if (!pagesToPreload.isEmpty()) {
        qDebug() << "PDF async preload: started" << pagesToPreload.size() 
                 << "background renders for pages" << pagesToPreload;
    }
    */
}

void DocumentViewport::invalidatePdfCache()
{
    // Cancel pending async preloads
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->stop();
    }
    
    // BUG-A006 FIX: Cancel active background PDF render threads
    // Without this, background threads can continue accessing stale PDF files
    // after document change, causing SIGBUS crashes on Android (memory alignment errors)
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
        // Don't waitForFinished() here - it would block the UI thread
        // The watcher will clean itself up when finished via deleteLater()
    }
    m_activePdfWatchers.clear();
    
    // Thread-safe cache clear
    QMutexLocker locker(&m_pdfCacheMutex);
#ifdef SPEEDYNOTE_DEBUG
    if (!m_pdfCache.isEmpty()) {
        qDebug() << "PDF CACHE INVALIDATED: cleared" << m_pdfCache.size() << "entries";
    }
#endif
    m_pdfCache.clear();
    m_cachedDpi = 0;
}

void DocumentViewport::invalidatePdfCachePage(int pageIndex)
{
    // Thread-safe page removal
    QMutexLocker locker(&m_pdfCacheMutex);
    m_pdfCache.erase(
        std::remove_if(m_pdfCache.begin(), m_pdfCache.end(),
                       [pageIndex](const PdfCacheEntry& entry) {
                           return entry.pageIndex == pageIndex;
                       }),
        m_pdfCache.end()
    );
}

void DocumentViewport::updatePdfCacheCapacity()
{
    // Calculate visible page count
    QVector<int> visible = visiblePages();
    int visibleCount = visible.size();
    
    // Buffer: 3 pages for 1-column (1 above + 2 below or vice versa)
    //         6 pages for 2-column (1 row above + 1 row below = 4, plus margin)
    int buffer = (m_layoutMode == LayoutMode::TwoColumn) ? 6 : 3;
    
    // New capacity with minimum of 4
    int newCapacity = qMax(4, visibleCount + buffer);
    
    // Thread-safe capacity update and eviction
    // Acquire mutex BEFORE updating capacity to prevent race conditions
    QMutexLocker locker(&m_pdfCacheMutex);
    
    // Only update if changed
    if (m_pdfCacheCapacity != newCapacity) {
        m_pdfCacheCapacity = newCapacity;
        
        // Immediately evict if over new capacity
        evictFurthestCacheEntries();
    }
}

void DocumentViewport::evictFurthestCacheEntries()
{
    // Must be called with m_pdfCacheMutex locked
    
    // Get reference page for distance calculation
    int centerPage = m_currentPageIndex;
    
    // Evict furthest entries until within capacity
    while (m_pdfCache.size() > m_pdfCacheCapacity) {
        int evictIdx = 0;
        int maxDistance = -1;
        
        for (int i = 0; i < m_pdfCache.size(); ++i) {
            int dist = qAbs(m_pdfCache[i].pageIndex - centerPage);
            if (dist > maxDistance) {
                maxDistance = dist;
                evictIdx = i;
            }
        }
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "PDF cache evict: page" << m_pdfCache[evictIdx].pageIndex 
                 << "distance" << maxDistance << "new size" << (m_pdfCache.size() - 1);
#endif
        m_pdfCache.removeAt(evictIdx);
    }
}

// ===== Page Layout Cache (Performance Optimization) =====

void DocumentViewport::ensurePageLayoutCache() const
{
    if (!m_pageLayoutDirty || !m_document) {
        return;
    }
    
    int pageCount = m_document->pageCount();
    m_pageYCache.resize(pageCount);
    
    if (m_document->isEdgeless() || pageCount == 0) {
        m_cachedContentSize = QSizeF(0, 0);
        m_pageLayoutDirty = false;
        return;
    }
    
    // Build cache based on layout mode
    // Phase O1.7.5: Use pageSizeAt() instead of page()->size to avoid loading full page content
    // This is critical for paged lazy loading - layout can be calculated from metadata alone
    // PERF: Also compute totalContentSize during this single O(n) pass
    qreal totalWidth = 0;
    qreal totalHeight = 0;
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn: {
            qreal y = 0;
            for (int i = 0; i < pageCount; ++i) {
                m_pageYCache[i] = y;
                QSizeF pageSize = m_document->pageSizeAt(i);
                if (!pageSize.isEmpty()) {
                    totalWidth = qMax(totalWidth, pageSize.width());
                    totalHeight = y + pageSize.height();  // Track total height
                    y += pageSize.height() + m_pageGap;
                }
            }
            break;
        }
        
        case LayoutMode::TwoColumn: {
            // For two-column, we store the Y of each row
            // Y position is same for both pages in a row
            qreal y = 0;
            for (int i = 0; i < pageCount; ++i) {
                QSizeF pageSize = m_document->pageSizeAt(i);
                
                if (i % 2 == 0) {
                    // First page of row - calculate and store Y
                    m_pageYCache[i] = y;
                } else {
                    // Second page of row - same Y as first
                    m_pageYCache[i] = m_pageYCache[i - 1];
                    
                    // After second page, advance Y using metadata sizes
                    qreal rowHeight = 0;
                    QSizeF leftSize = m_document->pageSizeAt(i - 1);
                    QSizeF rightSize = pageSize;
                    if (!leftSize.isEmpty()) rowHeight = qMax(rowHeight, leftSize.height());
                    if (!rightSize.isEmpty()) rowHeight = qMax(rowHeight, rightSize.height());
                    
                    // Track total width (both pages + gap)
                    qreal rowWidth = 0;
                    if (!leftSize.isEmpty()) rowWidth += leftSize.width();
                    if (!rightSize.isEmpty()) rowWidth += m_pageGap + rightSize.width();
                    totalWidth = qMax(totalWidth, rowWidth);
                    
                    totalHeight = y + rowHeight;  // Track total height
                    y += rowHeight + m_pageGap;
                }
            }
            // Handle odd page count (last page is alone)
            if (pageCount % 2 == 1 && pageCount > 0) {
                QSizeF lastSize = m_document->pageSizeAt(pageCount - 1);
                if (!lastSize.isEmpty()) {
                    totalWidth = qMax(totalWidth, lastSize.width());
                    totalHeight = m_pageYCache[pageCount - 1] + lastSize.height();
                }
            }
            break;
        }
    }
    
    m_cachedContentSize = QSizeF(totalWidth, totalHeight);
    m_pageLayoutDirty = false;
}

// ===== Stroke Cache Helpers (Task 1.3.7) =====

void DocumentViewport::preloadStrokeCaches()
{
    if (!m_document) {
        return;
    }
    
    // Skip for edgeless mode - uses tile-based loading
    if (m_document->isEdgeless()) {
        return;
    }
    
    QVector<int> visible = visiblePages();
    if (visible.isEmpty()) {
        return;
    }
    
    int first = visible.first();
    int last = visible.last();
    int pageCount = m_document->pageCount();
    
    // Pre-load ±1 pages beyond visible
    int preloadStart = qMax(0, first - 1);
    int preloadEnd = qMin(pageCount - 1, last + 1);
    
    // MEMORY OPTIMIZATION: Keep caches/pages for visible ±2 pages, evict everything else
    // This prevents unbounded memory growth when scrolling through large documents
    static constexpr int PAGE_BUFFER = 2;
    int keepStart = qMax(0, first - PAGE_BUFFER);
    int keepEnd = qMin(pageCount - 1, last + PAGE_BUFFER);
    
    // Phase O1.7.5: Evict pages far from visible area (lazy loading mode)
    // Only evict if lazy loading is enabled (bundle format)
    bool lazyLoadingEnabled = m_document->isLazyLoadEnabled();
    
    // PERF FIX: Only check pages that are actually loaded to avoid O(n) iterations
    // For documents with 3600 pages, iterating through all of them on every scroll is slow
            if (lazyLoadingEnabled) {
        // Get list of currently loaded page indices and evict those outside keep range
        QVector<int> loadedIndices = m_document->loadedPageIndices();
        for (int i : loadedIndices) {
            if (i < keepStart || i > keepEnd) {
                // CR-O1: Clear selection for objects on pages about to be evicted
                Page* page = m_document->page(i);  // Already loaded, no disk I/O
                if (page && !page->objects.empty()) {
                    bool selectionChanged = false;
                    for (const auto& obj : page->objects) {
                        if (m_hoveredObject == obj.get()) {
                            m_hoveredObject = nullptr;
                        }
                        if (m_selectedObjects.removeOne(obj.get())) {
                            selectionChanged = true;
                        }
                    }
                    if (selectionChanged) {
                        emit objectSelectionChanged();
                    }
                }
                
                // Evict entire page (saves if dirty, removes from memory)
                m_document->evictPage(i);
            }
        }
            } else {
        // Legacy mode: only evict stroke caches for pages outside keep range
        // Still need to iterate all pages, but page() access is cheap (already in memory)
        for (int i = 0; i < pageCount; ++i) {
            if (i < keepStart || i > keepEnd) {
                Page* page = m_document->page(i);
                if (page && page->hasLayerCachesAllocated()) {
                    page->releaseLayerCaches();
                }
            }
        }
    }
    
    // Get device pixel ratio for cache
    qreal dpr = devicePixelRatioF();
    
    // Phase O1.7.5: Preload nearby pages (triggers lazy loading if needed)
    // page() will automatically load from disk if not already in memory
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        Page* page = m_document->page(i);  // This triggers lazy load
        if (!page) continue;
        
        // Pre-generate zoom-aware stroke cache for all layers on this page
        for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
            VectorLayer* layer = page->layer(layerIdx);
            if (layer && layer->visible && !layer->isEmpty()) {
                // Build cache at current zoom level for sharp rendering
                layer->ensureStrokeCacheValid(page->size, m_zoomLevel, dpr);
            }
        }
    }
}

void DocumentViewport::evictDistantTiles()
{
    // Only applies to edgeless mode with lazy loading
    if (!m_document || !m_document->isEdgeless() || !m_document->isLazyLoadEnabled()) {
        return;
    }
    
    QRectF viewRect = visibleRect();
    
    // Keep tiles within 2 tiles of viewport, evict the rest
    constexpr int KEEP_MARGIN = 2;
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    QRectF keepRect = viewRect.adjusted(
        -KEEP_MARGIN * tileSize, -KEEP_MARGIN * tileSize,
        KEEP_MARGIN * tileSize, KEEP_MARGIN * tileSize);
    
    // Get all loaded tiles and check which to evict
    QVector<Document::TileCoord> loadedTiles = m_document->allLoadedTileCoords();
    
    int evictedCount = 0;
    bool selectionChanged = false;
    
    for (const auto& coord : loadedTiles) {
        // Phase 5.6.5: No longer need to protect origin tile - layer structure comes from manifest
        
        QRectF tileRect(coord.first * tileSize, coord.second * tileSize,
                        tileSize, tileSize);
        
        if (!keepRect.intersects(tileRect)) {
            // CR-O1: Clear selection for objects on tiles about to be evicted
            // This prevents dangling pointers in m_selectedObjects and m_hoveredObject
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && !tile->objects.empty()) {
                for (const auto& obj : tile->objects) {
                    if (m_hoveredObject == obj.get()) {
                        m_hoveredObject = nullptr;
                    }
                    if (m_selectedObjects.removeOne(obj.get())) {
                        selectionChanged = true;
                    }
                }
            }
            
            m_document->evictTile(coord);
            ++evictedCount;
        }
    }
    
    if (selectionChanged) {
        emit objectSelectionChanged();
    }
    
    // M.7.3: Notify that tiles were evicted (sidebar may need refresh)
    if (evictedCount > 0) {
        emit linkObjectListMayHaveChanged();
    }
    
#ifdef SPEEDYNOTE_DEBUG
    if (evictedCount > 0) {
        qDebug() << "Evicted" << evictedCount << "tiles, remaining:" << m_document->tileCount();
    }
#endif
}

// ===== Input Routing (Task 1.3.8) =====

PointerEvent DocumentViewport::mouseToPointerEvent(QMouseEvent* event, PointerEvent::Type type)
{
    PointerEvent pe;
    pe.type = type;
    pe.source = PointerEvent::Mouse;
    pe.viewportPos = event->position();
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Mouse has no pressure sensitivity
    pe.pressure = 1.0;
    pe.tiltX = 0;
    pe.tiltY = 0;
    pe.rotation = 0;
    
    // Hardware state
    pe.isEraser = false;
    pe.stylusButtons = 0;
    pe.buttons = event->buttons();
    pe.modifiers = event->modifiers();
    pe.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    return pe;
}

PointerEvent DocumentViewport::tabletToPointerEvent(QTabletEvent* event, PointerEvent::Type type)
{
    PointerEvent pe;
    pe.type = type;
    pe.source = PointerEvent::Stylus;
    pe.viewportPos = event->position();
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Tablet pressure and tilt
    pe.pressure = event->pressure();
    pe.tiltX = event->xTilt();
    pe.tiltY = event->yTilt();
    pe.rotation = event->rotation();
    
    // Check for eraser - either eraser end of stylus or eraser button
    // Qt6: pointerType() returns the type of pointing device
    // Also check deviceType() as a fallback - some drivers report eraser via device type
    pe.isEraser = (event->pointerType() == QPointingDevice::PointerType::Eraser);
    
    // Alternative detection: some tablets report eraser via deviceType() instead of pointerType()
    if (!pe.isEraser && event->deviceType() == QInputDevice::DeviceType::Stylus) {
        // Check if this might be an eraser based on the pointing device
        const QPointingDevice* device = event->pointingDevice();
        if (device && device->name().contains("eraser", Qt::CaseInsensitive)) {
            pe.isEraser = true;
        }
    }
    
#ifdef Q_OS_ANDROID
    // BUG-A008: Qt on Android doesn't properly translate Android's TOOL_TYPE_ERASER
    // to QPointingDevice::PointerType::Eraser. Query Android directly via JNI.
    // 
    // Performance: Cache the JNI class/method to avoid repeated lookups at 240Hz.
    // The static variables are initialized once on first call.
    if (!pe.isEraser) {
        static jclass activityClass = nullptr;
        static jmethodID isEraserMethod = nullptr;
        
        if (!activityClass) {
            QJniEnvironment env;
            jclass localClass = env->FindClass("org/speedynote/app/SpeedyNoteActivity");
            if (localClass) {
                activityClass = static_cast<jclass>(env->NewGlobalRef(localClass));
                isEraserMethod = env->GetStaticMethodID(activityClass, "isEraserToolActive", "()Z");
                env->DeleteLocalRef(localClass);
            }
        }
        
        if (activityClass && isEraserMethod) {
            QJniEnvironment env;
            pe.isEraser = static_cast<bool>(env->CallStaticBooleanMethod(activityClass, isEraserMethod));
        }
    }
#endif
    
    // Barrel buttons - Qt provides via buttons()
    // Common mappings: barrel button 1 = Qt::MiddleButton, barrel button 2 = Qt::RightButton
    pe.stylusButtons = static_cast<int>(event->buttons());
    pe.buttons = event->buttons();
    pe.modifiers = event->modifiers();
    pe.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    return pe;
}

void DocumentViewport::handlePointerEvent(const PointerEvent& pe)
{
    switch (pe.type) {
        case PointerEvent::Press:
            handlePointerPress(pe);
            break;
        case PointerEvent::Move:
            handlePointerMove(pe);
            break;
        case PointerEvent::Release:
            handlePointerRelease(pe);
            break;
    }
}

void DocumentViewport::handlePointerPress(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Ensure keyboard focus for shortcuts (stylus events don't auto-focus like mouse)
    if (!hasFocus()) {
        setFocus(Qt::OtherFocusReason);
    }
    
    // Set active state
    m_pointerActive = true;
    m_activeSource = pe.source;
    m_lastPointerPos = pe.viewportPos;
    
    // Track hardware eraser state for entire stroke
    // Initialize from the press event's eraser state
    m_hardwareEraserActive = pe.isEraser;
    
    // Determine which page to draw on
    if (pe.pageHit.valid()) {
        m_activeDrawingPage = pe.pageHit.pageIndex;
    } else {
        // Pointer is not on any page (in gap or outside content)
        m_activeDrawingPage = -1;
    }
    
    // Two-column UX: Update current page when touching a page with an editing tool
    // This ensures undo/redo operates on the page the user is actually editing,
    // not just the page at viewport center (which may be incorrect in 2-column mode)
    if (!m_document->isEdgeless() && pe.pageHit.valid()) {
        int touchedPage = pe.pageHit.pageIndex;
        if (touchedPage != m_currentPageIndex) {
            m_currentPageIndex = touchedPage;
            emit currentPageChanged(m_currentPageIndex);
            emit undoAvailableChanged(canUndo());
            emit redoAvailableChanged(canRedo());
        }
    }
    
    // Handle tool-specific actions
    // Hardware eraser (stylus eraser end) always erases, regardless of selected tool
    bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
    
    if (isErasing) {
        eraseAt(pe);
        // CRITICAL FIX: Always update cursor area on press to show the eraser cursor
        // eraseAt() only updates when strokes are removed, but we need to show cursor immediately
        // Use elliptical region to match the circular eraser cursor
        // Use toAlignedRect() to properly round floating-point to integer coords
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        QRectF cursorRectF(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                           eraserRadius * 2, eraserRadius * 2);
        update(QRegion(cursorRectF.toAlignedRect(), QRegion::Ellipse));
    } else if (m_currentTool == ToolType::Pen || m_currentTool == ToolType::Marker) {
        // Task 2.9: Straight line mode - record start point instead of normal stroke
        if (m_straightLineMode) {
            // Use document coords for edgeless, page coords for paged mode
            if (m_document->isEdgeless()) {
                m_straightLineStart = viewportToDocument(pe.viewportPos);
                m_straightLinePageIndex = -1;  // Not used in edgeless
            } else if (pe.pageHit.valid()) {
                m_straightLineStart = pe.pageHit.pagePoint;
                m_straightLinePageIndex = pe.pageHit.pageIndex;
            } else {
                return;  // No valid page hit in paged mode
            }
            m_straightLinePreviewEnd = m_straightLineStart;
            m_isDrawingStraightLine = true;
            m_pointerActive = true;  // Keep pointer active for move/release
            return;
        }
        
        startStroke(pe);
    } else if (m_currentTool == ToolType::Lasso) {
        // Task 2.10: Lasso selection tool
        handlePointerPress_Lasso(pe);
    } else if (m_currentTool == ToolType::ObjectSelect) {
        // Phase O2: Object selection tool
        handlePointerPress_ObjectSelect(pe);
    } else if (m_currentTool == ToolType::Highlighter) {
        // Phase A: Text selection / highlighter tool
        handlePointerPress_Highlighter(pe);
    }
}

void DocumentViewport::handlePointerMove(const PointerEvent& pe)
{
    if (!m_document || !m_pointerActive) return;
    
    // Store old position for cursor update
    QPointF oldPos = m_lastPointerPos;
    
    // Update last pointer position for cursor tracking
    m_lastPointerPos = pe.viewportPos;
    
    // CRITICAL: Some tablet drivers don't report eraser on Press but DO report it on Move.
    // If ANY event in the stroke has isEraser, treat the whole stroke as eraser.
    // This is the same pattern used in InkCanvas.
    if (pe.isEraser && !m_hardwareEraserActive) {
        m_hardwareEraserActive = true;
    }
    
    // Handle tool-specific actions
    // Hardware eraser: use m_hardwareEraserActive because some tablets
    // don't consistently report pointerType() == Eraser in every move event
    bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
    
    // Erasing works in edgeless mode even without a valid drawing page
    // (eraseAtEdgeless uses document coordinates, not page coordinates)
    if (isErasing) {
        eraseAt(pe);
        // CRITICAL FIX: eraseAt() only calls update() when strokes are removed!
        // We must ALWAYS update the cursor area to show cursor movement.
        // 
        // FIX: Use QRegion with two separate elliptical regions instead of
        // their bounding box union. This prevents the "square brush" visual
        // artifact where the entire bounding rectangle appears refreshed.
        // Use toAlignedRect() to properly round floating-point to integer coords.
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        
        // Create elliptical regions for old and new positions (approximates circles)
        QRectF oldRectF(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                        eraserRadius * 2, eraserRadius * 2);
        QRectF newRectF(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                        eraserRadius * 2, eraserRadius * 2);
        
        // Use elliptical regions for more accurate circular dirty areas
        QRegion dirtyRegion(oldRectF.toAlignedRect(), QRegion::Ellipse);
        dirtyRegion += QRegion(newRectF.toAlignedRect(), QRegion::Ellipse);
        update(dirtyRegion);
        return;  // Don't fall through to stroke continuation
    }
    
    // Task 2.9: Straight line mode - update preview end point
    if (m_isDrawingStraightLine) {
        // Use document coords for edgeless, page coords for paged mode
        if (m_document->isEdgeless()) {
            m_straightLinePreviewEnd = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_straightLinePageIndex) {
            m_straightLinePreviewEnd = pe.pageHit.pagePoint;
        } else {
            // Moved off the original page - extrapolate position
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            m_straightLinePreviewEnd = docPos - pageOrigin;
        }
        update();  // Trigger repaint for preview
        return;
    }
    
    // Task 2.10: Lasso tool - update lasso path OR handle transform
    // CR-2B-5: Must check m_isTransformingSelection too, not just m_isDrawingLasso
    if (m_isDrawingLasso || m_isTransformingSelection) {
        handlePointerMove_Lasso(pe);
        return;
    }
    
    // Phase O2: ObjectSelect tool - update hover or handle drag
    if (m_currentTool == ToolType::ObjectSelect) {
        handlePointerMove_ObjectSelect(pe);
        return;
    }
    
    // Phase A: Highlighter tool - update text selection
    if (m_currentTool == ToolType::Highlighter && m_textSelection.isSelecting) {
        handlePointerMove_Highlighter(pe);
        return;
    }
    
    // For stroke drawing, require an active drawing page
    if (m_activeDrawingPage < 0) {
        return;
    }
    
    if (m_isDrawing && (m_currentTool == ToolType::Pen || m_currentTool == ToolType::Marker)) {
        continueStroke(pe);
    }
}

void DocumentViewport::handlePointerRelease(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.9: Straight line mode - create the actual stroke
    if (m_isDrawingStraightLine) {
        // Get final end point
        QPointF endPoint;
        if (m_document->isEdgeless()) {
            endPoint = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_straightLinePageIndex) {
            endPoint = pe.pageHit.pagePoint;
        } else {
            // Moved off the original page - extrapolate position
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_straightLinePageIndex);
            endPoint = docPos - pageOrigin;
        }
        
        // Create the straight line stroke
        createStraightLineStroke(m_straightLineStart, endPoint);
        
        // Clear straight line state
        m_isDrawingStraightLine = false;
        m_straightLinePageIndex = -1;
        
        // Clear active state
        m_pointerActive = false;
        m_activeSource = PointerEvent::Unknown;
        m_hardwareEraserActive = false;
        
        update();
        preloadStrokeCaches();
        return;
    }
    
    // Task 2.10: Lasso tool - finalize lasso selection OR transform
    // CR-2B-5: Must check m_isTransformingSelection too, not just m_isDrawingLasso
    if (m_isDrawingLasso || m_isTransformingSelection) {
        handlePointerRelease_Lasso(pe);
        return;
    }
    
    // Phase O2: ObjectSelect tool - finalize drag
    if (m_currentTool == ToolType::ObjectSelect) {
        handlePointerRelease_ObjectSelect(pe);
        return;
    }
    
    // Phase A: Highlighter tool - finalize text selection
    if (m_currentTool == ToolType::Highlighter) {
        handlePointerRelease_Highlighter(pe);
        return;
    }
    
    Q_UNUSED(pe);
    
    // Finish stroke if we were drawing
    if (m_isDrawing) {
        finishStroke();
    }
    
    // Clear active state
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;  // Reset source
    m_activeDrawingPage = -1;
    m_hardwareEraserActive = false;  // Clear hardware eraser state
    // Note: Don't clear m_lastPointerPos - keep it for eraser cursor during hover
    
    // Pre-load stroke caches after interaction (but NOT PDF cache - it causes thrashing during rapid strokes)
    // PDF cache is preloaded during scroll/zoom, not during drawing
    preloadStrokeCaches();
    
    update();
}

// ===== Stroke Drawing (Task 2.2) =====

void DocumentViewport::startStroke(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Only drawing tools start strokes (Pen, Marker)
    if (m_currentTool != ToolType::Pen && m_currentTool != ToolType::Marker) {
        return;
    }
    
    // Determine stroke properties based on current tool (Task 2.8: Marker support)
    QColor strokeColor;
    qreal strokeThickness;
    bool useFixedPressure = false;  // Marker uses fixed thickness (ignores pressure)
    
    if (m_currentTool == ToolType::Marker) {
        strokeColor = m_markerColor;        // Includes alpha for opacity
        strokeThickness = m_markerThickness;
        useFixedPressure = true;            // Fixed thickness, no pressure variation
    } else {
        strokeColor = m_penColor;
        strokeThickness = m_penThickness;
        useFixedPressure = false;           // Pen uses pressure for thickness
    }
    
    // For edgeless mode, we don't require a page hit - we use document coordinates
    if (m_document->isEdgeless()) {
        m_isDrawing = true;
        // CR-4: m_activeDrawingPage = 0 is used for edgeless mode to satisfy
        // the m_activeDrawingPage >= 0 checks in renderCurrentStrokeIncremental().
        // The actual tile is tracked in m_edgelessDrawingTile.
        m_activeDrawingPage = 0;
        
        // Initialize new stroke
        m_currentStroke = VectorStroke();
        m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_currentStroke.color = strokeColor;
        m_currentStroke.baseThickness = strokeThickness;
        
        // Reset incremental rendering cache
        resetCurrentStrokeCache();
        
        // Get document coordinates for the first point
        QPointF docPt = viewportToDocument(pe.viewportPos);
        
        // Store the tile coordinate where stroke starts
        m_edgelessDrawingTile = m_document->tileCoordForPoint(docPt);
        
        // Add first point (stored in DOCUMENT coordinates for edgeless)
        // Marker uses fixed pressure (1.0) for consistent thickness
        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = useFixedPressure ? 1.0 : qBound(0.1, pe.pressure, 1.0);
        m_currentStroke.points.append(pt);
        return;
    }
    
    // Paged mode - require valid page hit
    if (!pe.pageHit.valid()) return;
    
    m_isDrawing = true;
    m_activeDrawingPage = pe.pageHit.pageIndex;
    
    // Initialize new stroke
    m_currentStroke = VectorStroke();
    m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentStroke.color = strokeColor;
    m_currentStroke.baseThickness = strokeThickness;
    
    // Reset incremental rendering cache (Task 2.3)
    resetCurrentStrokeCache();
    
    // Add first point (in page-local coordinates)
    // Marker uses fixed pressure (1.0) for consistent thickness
    qreal effectivePressure = useFixedPressure ? 1.0 : pe.pressure;
    addPointToStroke(pe.pageHit.pagePoint, effectivePressure);
}

void DocumentViewport::continueStroke(const PointerEvent& pe)
{
    if (!m_isDrawing || !m_document) return;
    
    // Task 2.8: Marker uses fixed pressure (1.0) for consistent thickness
    bool useFixedPressure = (m_currentTool == ToolType::Marker);
    qreal effectivePressure = useFixedPressure ? 1.0 : qBound(0.1, pe.pressure, 1.0);
    
    // For edgeless mode, use document coordinates directly
    if (m_document->isEdgeless()) {
        QPointF docPt = viewportToDocument(pe.viewportPos);
        
        // Point decimation (same logic as addPointToStroke but for document coords)
        if (!m_currentStroke.points.isEmpty()) {
            const QPointF& lastPos = m_currentStroke.points.last().pos;
            qreal dx = docPt.x() - lastPos.x();
            qreal dy = docPt.y() - lastPos.y();
            qreal distSq = dx * dx + dy * dy;
            
            if (distSq < MIN_DISTANCE_SQ) {
                // Point too close - but update pressure if higher (only for pen, not marker)
                if (!useFixedPressure && pe.pressure > m_currentStroke.points.last().pressure) {
                    m_currentStroke.points.last().pressure = pe.pressure;
                }
                return;
            }
        }
        
        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = effectivePressure;
        m_currentStroke.points.append(pt);
        
        // Dirty region update for edgeless (document coords → viewport coords)
        // Use current stroke thickness (may be pen or marker)
        qreal padding = m_currentStroke.baseThickness * 2 * m_zoomLevel;
        QPointF vpPos = documentToViewport(docPt);
        QRectF dirtyRect(vpPos.x() - padding, vpPos.y() - padding, padding * 2, padding * 2);
        
        if (m_currentStroke.points.size() > 1) {
            const auto& prevPt = m_currentStroke.points[m_currentStroke.points.size() - 2];
            QPointF prevVpPos = documentToViewport(prevPt.pos);
            dirtyRect = dirtyRect.united(QRectF(prevVpPos.x() - padding, prevVpPos.y() - padding, 
                                                 padding * 2, padding * 2));
        }
        
        update(dirtyRect.toAlignedRect());
        return;
    }
    
    // Paged mode
    if (m_activeDrawingPage < 0) return;
    
    // Get page-local coordinates
    // Note: Even if pointer moves off the active page, we continue drawing
    // to that page (don't switch pages mid-stroke)
    QPointF pagePos;
    if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_activeDrawingPage) {
        pagePos = pe.pageHit.pagePoint;
    } else {
        // Pointer moved off active page - extrapolate position
        QPointF docPos = viewportToDocument(pe.viewportPos);
        QPointF pageOrigin = pagePosition(m_activeDrawingPage);
        pagePos = docPos - pageOrigin;
    }
    
    // Use effective pressure (fixed 1.0 for marker, actual pressure for pen)
    addPointToStroke(pagePos, effectivePressure);
}

void DocumentViewport::finishStroke()
{
    if (!m_isDrawing) return;
    
    // Don't save empty strokes
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        m_currentStrokeCache = QPixmap();  // Release cache memory
        return;
    }
    
    // Finalize stroke
    m_currentStroke.updateBoundingBox();
    
    // Branch for edgeless mode
    if (m_document && m_document->isEdgeless()) {
        finishStrokeEdgeless();
        return;
    }
    
    // Paged mode: add to page's active layer
    Page* page = m_document ? m_document->page(m_activeDrawingPage) : nullptr;
    if (page) {
        VectorLayer* layer = page->activeLayer();
        if (layer) {
            layer->addStroke(m_currentStroke);
            
            // Mark page dirty for lazy save (BUG FIX: was missing, causing strokes to not save)
            m_document->markPageDirty(m_activeDrawingPage);
            
            // Push to undo stack
            pushUndoAction(m_activeDrawingPage, PageUndoAction::AddStroke, m_currentStroke);
        }
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;  // Reset incremental rendering state
    
    // MEMORY FIX: Release the incremental stroke cache
    // This cache is viewport-sized (~33MB at 4K) and should be freed after stroke completes.
    // It will be lazily reallocated on the next stroke start.
    m_currentStrokeCache = QPixmap();
    
    emit documentModified();
}

void DocumentViewport::finishStrokeEdgeless()
{
    // In edgeless mode, stroke points are in DOCUMENT coordinates.
    // We split the stroke at tile boundaries so each segment is stored in its home tile.
    // This allows the stroke cache to work per-tile while strokes can span multiple tiles.
    
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        m_currentStrokeCache = QPixmap();
        return;
    }
    
    // ========== STROKE SPLITTING AT TILE BOUNDARIES ==========
    // Strategy: Walk through all points, group consecutive points by tile.
    // Split stroke into tile segments using the common helper
    // (handles boundary crossings with overlapping points for visual continuity)
    QVector<TileSegment> segments = splitStrokeIntoTileSegments(m_currentStroke.points);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Edgeless: Stroke split into" << segments.size() << "segments";
#endif
    
    // ========== ADD EACH SEGMENT TO ITS TILE ==========
    QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;  // For undo
    
    for (const TileSegment& seg : segments) {
        // Get or create tile
        Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
        if (!tile) continue;
        
        // Ensure tile has enough layers
        while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        
        VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
        if (!layer) continue;
        
        // Create local stroke (convert from document coords to tile-local)
        VectorStroke localStroke = m_currentStroke;  // Copy base properties (color, width, etc.)
        localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New unique ID for each segment
        localStroke.points.clear();
        
        QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                           seg.coord.second * Document::EDGELESS_TILE_SIZE);
        
        for (const StrokePoint& pt : seg.points) {
            StrokePoint localPt = pt;
            localPt.pos -= tileOrigin;
            localStroke.points.append(localPt);
        }
        localStroke.updateBoundingBox();
        
        // Add to tile's layer
        layer->addStroke(localStroke);
        layer->invalidateStrokeCache();
        
        // Mark tile as dirty for persistence (Phase E5)
        m_document->markTileDirty(seg.coord);
        
        addedStrokes.append({seg.coord, localStroke});
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "  -> Tile" << seg.coord.first << "," << seg.coord.second
                 << "points:" << localStroke.points.size();
#endif
    }
    
    // ========== PUSH TO EDGELESS UNDO STACK (Phase E6) ==========
    // All segments from this stroke = one atomic undo action
    if (!addedStrokes.isEmpty()) {
        EdgelessUndoAction undoAction;
        undoAction.type = PageUndoAction::AddStroke;
        undoAction.layerIndex = m_edgelessActiveLayerIndex;
        for (const auto& pair : addedStrokes) {
            undoAction.segments.append({pair.first, pair.second});
        }
        pushEdgelessUndoAction(undoAction);
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;
    m_currentStrokeCache = QPixmap();
    
    // Trigger repaint
    update();
    
    emit documentModified();
}

QVector<QPair<Document::TileCoord, VectorStroke>> DocumentViewport::addStrokeToEdgelessTiles(
    const VectorStroke& stroke, int layerIndex)
{
    // ========== STROKE SPLITTING AT TILE BOUNDARIES ==========
    // This method is shared by finishStrokeEdgeless() and applySelectionTransform()
    // to ensure consistent behavior when strokes cross tile boundaries.
    //
    // Input: stroke with points in DOCUMENT coordinates
    // Output: multiple segments, each added to appropriate tile in tile-local coords
    
    QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;
    
    if (!m_document || stroke.points.isEmpty()) {
        return addedStrokes;
    }
    
    // Split stroke into tile segments using the common helper
    // (handles boundary crossings with overlapping points for visual continuity)
    QVector<TileSegment> segments = splitStrokeIntoTileSegments(stroke.points);
    
#ifdef SPEEDYNOTE_DEBUG
    if (segments.size() > 1) {
        qDebug() << "addStrokeToEdgelessTiles: stroke split into" << segments.size() << "segments";
    }
#endif
    
    // ========== ADD EACH SEGMENT TO ITS TILE ==========
    for (const TileSegment& seg : segments) {
        // Get or create tile
        Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
        if (!tile) continue;
        
        // Ensure tile has enough layers
        while (tile->layerCount() <= layerIndex) {
            tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
        }
        
        VectorLayer* layer = tile->layer(layerIndex);
        if (!layer) continue;
        
        // Create local stroke (convert from document coords to tile-local)
        VectorStroke localStroke = stroke;  // Copy base properties (color, width, etc.)
        localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New unique ID
        localStroke.points.clear();
        
        QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                           seg.coord.second * Document::EDGELESS_TILE_SIZE);
        
        for (const StrokePoint& pt : seg.points) {
            StrokePoint localPt = pt;
            localPt.pos -= tileOrigin;
            localStroke.points.append(localPt);
        }
        localStroke.updateBoundingBox();
        
        // Add to tile's layer
        layer->addStroke(localStroke);
        layer->invalidateStrokeCache();
        
        // Mark tile as dirty for persistence
        m_document->markTileDirty(seg.coord);
        
        addedStrokes.append({seg.coord, localStroke});
    }
    
    return addedStrokes;
}

// ===== Straight Line Mode (Task 2.9) =====

void DocumentViewport::createStraightLineStroke(const QPointF& start, const QPointF& end)
{
    if (!m_document) return;
    
    // Don't create zero-length lines
    if ((start - end).manhattanLength() < 1.0) {
        return;
    }
    
    // Determine color and thickness based on current tool
    QColor strokeColor;
    qreal strokeThickness;
    if (m_currentTool == ToolType::Marker) {
        strokeColor = m_markerColor;
        strokeThickness = m_markerThickness;
    } else {
        strokeColor = m_penColor;
        strokeThickness = m_penThickness;
    }
    
    // Create stroke with just two points (start and end)
    VectorStroke stroke;
    stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    stroke.color = strokeColor;
    stroke.baseThickness = strokeThickness;
    
    // Both points have pressure 1.0 (no pressure variation for straight lines)
    StrokePoint startPt;
    startPt.pos = start;
    startPt.pressure = 1.0;
    stroke.points.append(startPt);
    
    StrokePoint endPt;
    endPt.pos = end;
    endPt.pressure = 1.0;
    stroke.points.append(endPt);
    
    stroke.updateBoundingBox();
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE: Handle tile splitting ==========
        // A straight line may cross multiple tiles. We use a simplified approach:
        // Find all tiles the line passes through and add the appropriate segment.
        
        Document::TileCoord startTile = m_document->tileCoordForPoint(start);
        Document::TileCoord endTile = m_document->tileCoordForPoint(end);
        
        if (startTile == endTile) {
            // Simple case: line is within one tile
            Page* tile = m_document->getOrCreateTile(startTile.first, startTile.second);
            if (!tile) return;
            
            // Ensure tile has enough layers
            while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer) return;
            
            // Convert to tile-local coordinates
            QPointF tileOrigin(startTile.first * Document::EDGELESS_TILE_SIZE,
                               startTile.second * Document::EDGELESS_TILE_SIZE);
            VectorStroke localStroke = stroke;
            localStroke.points[0].pos -= tileOrigin;
            localStroke.points[1].pos -= tileOrigin;
            localStroke.updateBoundingBox();
            
            layer->addStroke(localStroke);
            layer->invalidateStrokeCache();
            m_document->markTileDirty(startTile);
            
            // Push to undo stack
            EdgelessUndoAction undoAction;
            undoAction.type = PageUndoAction::AddStroke;
            undoAction.layerIndex = m_edgelessActiveLayerIndex;
            undoAction.segments.append({startTile, localStroke});
            pushEdgelessUndoAction(undoAction);
        } else {
            // Line crosses tile boundaries - sample points along the line
            // and split at tile boundaries (same algorithm as freehand strokes)
            
            // Generate intermediate points along the line
            qreal lineLength = std::sqrt(std::pow(end.x() - start.x(), 2) + 
                                         std::pow(end.y() - start.y(), 2));
            int numPoints = qMax(2, static_cast<int>(lineLength / 10.0));  // ~10px spacing
            
            QVector<StrokePoint> linePoints;
            for (int i = 0; i <= numPoints; ++i) {
                qreal t = static_cast<qreal>(i) / numPoints;
                StrokePoint pt;
                pt.pos = start + t * (end - start);
                pt.pressure = 1.0;
                linePoints.append(pt);
            }
            
            // Split at tile boundaries using the common helper
            // (handles boundary crossings with overlapping points for visual continuity)
            QVector<TileSegment> segments = splitStrokeIntoTileSegments(linePoints);
            
            // Add each segment to its tile
            QVector<QPair<Document::TileCoord, VectorStroke>> addedStrokes;
            
            for (const TileSegment& seg : segments) {
                Page* tile = m_document->getOrCreateTile(seg.coord.first, seg.coord.second);
                if (!tile) continue;
                
                while (tile->layerCount() <= m_edgelessActiveLayerIndex) {
                    tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
                }
                
                VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
                if (!layer) continue;
                
                VectorStroke localStroke;
                localStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                localStroke.color = strokeColor;
                localStroke.baseThickness = strokeThickness;
                
                QPointF tileOrigin(seg.coord.first * Document::EDGELESS_TILE_SIZE,
                                   seg.coord.second * Document::EDGELESS_TILE_SIZE);
                
                for (const StrokePoint& pt : seg.points) {
                    StrokePoint localPt = pt;
                    localPt.pos -= tileOrigin;
                    localStroke.points.append(localPt);
                }
                localStroke.updateBoundingBox();
                
                layer->addStroke(localStroke);
                layer->invalidateStrokeCache();
                m_document->markTileDirty(seg.coord);
                
                addedStrokes.append({seg.coord, localStroke});
            }
            
            // Push to undo stack (all segments as one atomic action)
            if (!addedStrokes.isEmpty()) {
                EdgelessUndoAction undoAction;
                undoAction.type = PageUndoAction::AddStroke;
                undoAction.layerIndex = m_edgelessActiveLayerIndex;
                for (const auto& pair : addedStrokes) {
                    undoAction.segments.append({pair.first, pair.second});
                }
                pushEdgelessUndoAction(undoAction);
            }
        }
    } else {
        // ========== PAGED MODE: Add directly to page ==========
        if (m_straightLinePageIndex < 0 || m_straightLinePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_straightLinePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) return;
        
        layer->addStroke(stroke);
        layer->invalidateStrokeCache();
        
        // Mark page dirty for lazy save (BUG FIX: was missing)
        m_document->markPageDirty(m_straightLinePageIndex);
        
        // Push to undo stack (same pattern as finishStroke)
        pushUndoAction(m_straightLinePageIndex, PageUndoAction::AddStroke, stroke);
    }
    
    emit documentModified();
}

// ===== Lasso Selection Tool (Task 2.10) =====

// P1: Reset lasso path cache for new drawing session
void DocumentViewport::resetLassoPathCache()
{
    // Create cache at viewport size with device pixel ratio for high DPI
    qreal dpr = devicePixelRatioF();
    m_lassoPathCache = QPixmap(static_cast<int>(width() * dpr), 
                               static_cast<int>(height() * dpr));
    m_lassoPathCache.setDevicePixelRatio(dpr);
    m_lassoPathCache.fill(Qt::transparent);
    
    m_lastRenderedLassoIdx = 0;
    m_lassoPathCacheZoom = m_zoomLevel;
    m_lassoPathCachePan = m_panOffset;
    m_lassoPathLength = 0;
}

// P1: Incrementally render lasso path with consistent dash pattern
void DocumentViewport::renderLassoPathIncremental(QPainter& painter)
{
    if (m_lassoPath.size() < 2) return;
    
    // Check if cache needs reset (zoom/pan changed)
    if (m_lassoPathCache.isNull() ||
        !qFuzzyCompare(m_lassoPathCacheZoom, m_zoomLevel) ||
        m_lassoPathCachePan != m_panOffset) {
        // Zoom or pan changed - need to re-render everything
        resetLassoPathCache();
    }
    
    // Render new segments to cache
    if (m_lastRenderedLassoIdx < m_lassoPath.size() - 1) {
        QPainter cachePainter(&m_lassoPathCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Determine coordinate conversion based on mode
        bool isEdgeless = m_document && m_document->isEdgeless();
        QPointF pageOrigin;
        if (!isEdgeless && m_lassoSelection.sourcePageIndex >= 0) {
            pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        }
        
        // Render each new segment with proper dash offset
        for (int i = m_lastRenderedLassoIdx; i < m_lassoPath.size() - 1; ++i) {
            QPointF pt1 = m_lassoPath.at(i);
            QPointF pt2 = m_lassoPath.at(i + 1);
            
            // Convert to viewport coordinates
            QPointF vp1, vp2;
            if (isEdgeless) {
                vp1 = documentToViewport(pt1);
                vp2 = documentToViewport(pt2);
            } else {
                vp1 = documentToViewport(pt1 + pageOrigin);
                vp2 = documentToViewport(pt2 + pageOrigin);
            }
            
            // Calculate segment length in viewport coordinates
            qreal segLen = QLineF(vp1, vp2).length();
            
            // Create pen with dash offset for continuous pattern
            // Qt dash pattern: [dash, gap] - default DashLine is [4, 2] (in pen width units)
            // For 1.5px pen: [6, 3] pixel pattern
            QPen lassoPen(QColor(0, 120, 215), 1.5, Qt::DashLine);
            lassoPen.setCosmetic(true);  // Constant width regardless of transform
            lassoPen.setDashOffset(m_lassoPathLength / 1.5);  // Offset in pen-width units
            cachePainter.setPen(lassoPen);
            
            cachePainter.drawLine(vp1, vp2);
            
            // Accumulate path length for next segment's dash offset
            m_lassoPathLength += segLen;
        }
        
        m_lastRenderedLassoIdx = m_lassoPath.size() - 1;
    }
    
    // Blit cache to painter
    painter.drawPixmap(0, 0, m_lassoPathCache);
}

void DocumentViewport::handlePointerPress_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Check for handle/transform hit on existing selection
    if (m_lassoSelection.isValid()) {
        HandleHit hit = hitTestSelectionHandles(pe.viewportPos);
        
        if (hit != HandleHit::None) {
            // Start transform operation
            startSelectionTransform(hit, pe.viewportPos);
            m_pointerActive = true;
            return;
        }
        
        // Task 2.10.6: Click outside selection - apply transform (if any) and clear
        if (m_lassoSelection.hasTransform()) {
            applySelectionTransform();  // This also clears the selection
        } else {
            clearLassoSelection();
        }
    }
    
    // Start new lasso path
    m_lassoPath.clear();
    resetLassoPathCache();  // P1: Initialize cache for incremental rendering
    
    // Use appropriate coordinates based on mode
    QPointF pt;
    if (m_document->isEdgeless()) {
        pt = viewportToDocument(pe.viewportPos);
    } else if (pe.pageHit.valid()) {
        pt = pe.pageHit.pagePoint;
        m_lassoSelection.sourcePageIndex = pe.pageHit.pageIndex;
    } else {
        return;  // No valid page hit in paged mode
    }
    
    m_lassoPath << pt;
    m_isDrawingLasso = true;
    m_pointerActive = true;
    
    update();
}

void DocumentViewport::handlePointerMove_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Handle transform updates
    if (m_isTransformingSelection) {
        updateSelectionTransform(pe.viewportPos);
        return;
    }
    
    if (!m_isDrawingLasso) return;
    
    // Add point to lasso path
    QPointF pt;
    if (m_document->isEdgeless()) {
        pt = viewportToDocument(pe.viewportPos);
    } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_lassoSelection.sourcePageIndex) {
        pt = pe.pageHit.pagePoint;
    } else if (m_lassoSelection.sourcePageIndex >= 0) {
        // Pointer moved off page - extrapolate
        QPointF docPos = viewportToDocument(pe.viewportPos);
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        pt = docPos - pageOrigin;
    } else {
        return;
    }
    
    // Point decimation for lasso path (similar to stroke)
    QPointF lastPt;
    bool hasLastPoint = !m_lassoPath.isEmpty();
    if (hasLastPoint) {
        lastPt = m_lassoPath.last();
        qreal dx = pt.x() - lastPt.x();
        qreal dy = pt.y() - lastPt.y();
        if (dx * dx + dy * dy < 4.0) {  // 2px minimum distance
            return;  // Skip this point
        }
    }
    
    m_lassoPath << pt;
    
    // P2: Dirty region update - only repaint the new segment's bounding rect
    if (hasLastPoint) {
        // Convert both points to viewport coordinates
        QPointF vpLast, vpCurrent;
        if (m_document->isEdgeless()) {
            vpLast = documentToViewport(lastPt);
            vpCurrent = documentToViewport(pt);
        } else {
            QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
            vpLast = documentToViewport(lastPt + pageOrigin);
            vpCurrent = documentToViewport(pt + pageOrigin);
        }
        
        // Calculate dirty rect with padding for line width and antialiasing
        QRectF dirtyRect = QRectF(vpLast, vpCurrent).normalized();
        dirtyRect.adjust(-4, -4, 4, 4);  // Account for line width (1.5) + padding
        update(dirtyRect.toRect());
    } else {
        // First point - update a small region around it
        QPointF vpPt = m_document->isEdgeless() 
            ? documentToViewport(pt)
            : documentToViewport(pt + pagePosition(m_lassoSelection.sourcePageIndex));
        QRectF dirtyRect(vpPt.x() - 5, vpPt.y() - 5, 10, 10);
        update(dirtyRect.toRect());
    }
}

void DocumentViewport::handlePointerRelease_Lasso(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Task 2.10.5: Finalize transform if active
    if (m_isTransformingSelection) {
        finalizeSelectionTransform();
        m_pointerActive = false;
        return;
    }
    
    if (m_isDrawingLasso) {
        // Add final point
        QPointF pt;
        if (m_document->isEdgeless()) {
            pt = viewportToDocument(pe.viewportPos);
        } else if (pe.pageHit.valid()) {
            pt = pe.pageHit.pagePoint;
        } else if (m_lassoSelection.sourcePageIndex >= 0) {
            QPointF docPos = viewportToDocument(pe.viewportPos);
            QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
            pt = docPos - pageOrigin;
        }
        
        if (!pt.isNull()) {
            m_lassoPath << pt;
        }
        
        // Task 2.10.2: Find strokes within the lasso path
        finalizeLassoSelection();
        m_isDrawingLasso = false;
    }
    
    m_pointerActive = false;
    update();
}

// =============================================================================
// Object Selection Tool Handlers (Phase O2)
// =============================================================================

void DocumentViewport::handlePointerPress_ObjectSelect(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Phase C.4.4: Create mode - insert object at click position instead of selecting
    if (m_objectActionMode == ObjectActionMode::Create) {
        PageHit hit = viewportToPage(pe.viewportPos);
        if (hit.pageIndex < 0) {
            // Click not on any page - ignore in paged mode
            if (!m_document->isEdgeless()) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "handlePointerPress_ObjectSelect: Create mode click not on page";
#endif
                return;
            }
            // Edgeless: use document coordinates directly
            QPointF docPos = viewportToDocument(pe.viewportPos);
            auto coord = m_document->tileCoordForPoint(docPos);
            hit.pageIndex = 0;  // Placeholder - edgeless uses tiles
            hit.pagePoint = docPos - QPointF(coord.first * Document::EDGELESS_TILE_SIZE,
                                              coord.second * Document::EDGELESS_TILE_SIZE);
        }
        
        if (m_objectInsertMode == ObjectInsertMode::Image) {
            // Open file dialog and insert image
            // Note: insertImageFromDialog() positions at viewport center for now
            // TODO: Create insertImageAtPosition() for click-to-place
            insertImageFromDialog();
        } else {
            // Create empty LinkObject at position
            // Pass viewportPos so edgeless mode can determine correct tile
            createLinkObjectAtPosition(hit.pageIndex, hit.pagePoint, pe.viewportPos);
        }
        return;
    }
    
    // Phase O3.1.3: Check for resize handle click FIRST (single selection only)
    if (m_selectedObjects.size() == 1) {
        HandleHit handle = objectHandleAtPoint(pe.viewportPos);
        if (handle != HandleHit::None && handle != HandleHit::Inside) {
            InsertedObject* obj = m_selectedObjects.first();
            
            // Phase C.2.2: LinkObject doesn't resize - skip resize handle interaction
            // Allow the click to fall through to drag logic instead
            if (obj->type() != "link") {
                // Start resize operation (non-LinkObject only)
            m_isResizingObject = true;
            m_objectResizeHandle = handle;
            m_resizeStartViewport = pe.viewportPos;
            m_resizeOriginalSize = obj->size;
            m_resizeOriginalPosition = obj->position;  // Tile-local, for undo
            m_resizeOriginalRotation = obj->rotation;  // Phase O3.1.8.2
            m_pointerActive = true;
            
            // BF: Calculate document-global center for scale calculations
            // In edgeless mode, obj->position is tile-local, but pointer events
            // give document-global coordinates. Must use consistent coordinate system!
            QPointF docPos;
            if (m_document->isEdgeless()) {
                // Find tile containing this object and add tile origin
                for (const auto& coord : m_document->allLoadedTileCoords()) {
                    Page* tile = m_document->getTile(coord.first, coord.second);
                    if (tile && tile->objectById(obj->id)) {
                        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                                           coord.second * Document::EDGELESS_TILE_SIZE);
                        docPos = tileOrigin + obj->position;
                        break;
                    }
                }
            } else {
                // Paged: find page containing object
                // PERF FIX: Only search loaded pages to avoid triggering lazy loading
                for (int i : m_document->loadedPageIndices()) {
                    Page* page = m_document->page(i);  // Already loaded, no disk I/O
                    if (page && page->objectById(obj->id)) {
                        docPos = pagePosition(i) + obj->position;
                        break;
                    }
                }
            }
            m_resizeObjectDocCenter = docPos + QPointF(obj->size.width() / 2.0, 
                                                        obj->size.height() / 2.0);
            
            // Phase O4.1: Capture background for fast resize rendering
            captureObjectDragBackground();
            
            return;  // Don't start object drag
            }
            // LinkObject: fall through to handle as drag instead
        }
    }
    
    // Convert to document coordinates
    QPointF docPoint = viewportToDocument(pe.viewportPos);
    
    // Hit test for object
    InsertedObject* hitObject = objectAtPoint(docPoint);
    
    bool shiftHeld = (pe.modifiers & Qt::ShiftModifier);
    
    if (hitObject) {
        // Check if clicking on already-selected object (start drag)
        bool alreadySelected = m_selectedObjects.contains(hitObject);
        
        if (shiftHeld) {
            // Shift+click: toggle selection (uses API for signal emission)
            if (alreadySelected) {
                deselectObject(hitObject);
            } else {
                selectObject(hitObject, true);  // Add to selection
            }
        } else {
            // Regular click
            if (!alreadySelected) {
                // Replace selection with this object (uses API for signal emission)
                selectObject(hitObject, false);
            }
            // If already selected, keep selection (allows multi-drag)
        }
        
        // Start dragging if we have a selection
        if (!m_selectedObjects.isEmpty()) {
            m_isDraggingObjects = true;
            m_objectDragStartViewport = pe.viewportPos;
            m_objectDragStartDoc = docPoint;
            m_pointerActive = true;
            
            // O2.3.2: Store original positions for undo
            m_objectOriginalPositions.clear();
            for (InsertedObject* obj : m_selectedObjects) {
                if (obj) {
                    m_objectOriginalPositions[obj->id] = obj->position;
                }
            }
            
            // Phase O4.1: Capture background for fast drag rendering
            captureObjectDragBackground();
        }
    } else {
        // Clicked on empty space
        if (!shiftHeld) {
            // Deselect all (uses API for signal emission)
            deselectAllObjects();
        }
    }
}

void DocumentViewport::handlePointerMove_ObjectSelect(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Phase O3.1.3: Handle resize drag
    if (m_isResizingObject) {
        // Phase O4.1.3: Throttle ALL resize/rotate processing to ~60fps
        // This prevents excessive computation, not just excessive repaints
        if (m_dragUpdateTimer.isValid() && 
            m_dragUpdateTimer.elapsed() < DRAG_UPDATE_INTERVAL_MS) {
            return;  // Skip this event entirely - too soon since last update
        }
        m_dragUpdateTimer.restart();
        
        // Calculate new size based on handle being dragged
        updateObjectResize(pe.viewportPos);
        update();
        return;
    }
    
    QPointF docPoint = viewportToDocument(pe.viewportPos);
    
    if (m_isDraggingObjects && !m_selectedObjects.isEmpty()) {
        // Calculate delta in document coordinates
        QPointF delta = docPoint - m_objectDragStartDoc;
        
        // O2.3.3: Use moveSelectedObjects method
        moveSelectedObjects(delta);
        
        // Update drag start for next move
        m_objectDragStartDoc = docPoint;
    } else {
        // Not dragging - update hover state
        InsertedObject* newHover = objectAtPoint(docPoint);
        
        if (newHover != m_hoveredObject) {
            m_hoveredObject = newHover;
            update();  // Repaint for hover feedback
        }
    }
}

void DocumentViewport::handlePointerRelease_ObjectSelect(const PointerEvent& pe)
{
    Q_UNUSED(pe);
    
    // Phase O3.1.3: Finalize resize/rotate operation
    if (m_isResizingObject) {
        InsertedObject* obj = m_selectedObjects.isEmpty() ? nullptr : m_selectedObjects.first();
        // Check if any transform property changed (position, size, or rotation)
        bool changed = obj && (obj->size != m_resizeOriginalSize || 
                               obj->position != m_resizeOriginalPosition ||
                               obj->rotation != m_resizeOriginalRotation);  // O3.1.8.3
        if (changed) {
            // Phase O3.1.5/O3.1.8.3: Create undo entry for resize/rotate
            pushObjectResizeUndo(obj, m_resizeOriginalPosition, m_resizeOriginalSize, 
                                 m_resizeOriginalRotation);
            
            // Mark dirty
            if (m_document) {
                if (m_document->isEdgeless()) {
                    // May need to relocate to different tile if position changed
                    relocateObjectsToCorrectTiles();
                    // Mark tile dirty - use cached tile coord for efficiency
                    m_document->markTileDirty(m_dragObjectTileCoord);
                } else {
                    // Use cached page index for efficiency
                    int pageIdx = (m_dragObjectPageIndex >= 0) ? m_dragObjectPageIndex : m_currentPageIndex;
                    m_document->markPageDirty(pageIdx);
                }
            }
            
            emit documentModified();
        }
        
        m_isResizingObject = false;
        m_objectResizeHandle = HandleHit::None;
        m_pointerActive = false;
        
        // Phase O4.1: Clear background snapshot and object cache, trigger full re-render
        m_objectDragBackgroundSnapshot = QPixmap();
        m_dragObjectRenderedCache = QPixmap();
        update();
        return;
    }
    
    if (m_isDraggingObjects) {
        // O2.3.2: Finalize drag
        // Check if any object actually moved
        bool moved = false;
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            auto it = m_objectOriginalPositions.find(obj->id);
            if (it != m_objectOriginalPositions.end() && it.value() != obj->position) {
                moved = true;
                break;
            }
        }
        
        if (moved) {
            // Mark pages/tiles dirty and handle tile boundary crossing
            if (m_document) {
                if (m_document->isEdgeless()) {
                    // O2.3.4: Handle tile boundary crossing
                    // This will relocate objects to correct tiles and mark them dirty
                    int relocated = relocateObjectsToCorrectTiles();
                    
                    // Also mark tiles dirty for objects that didn't relocate
                    // (they still moved within their tile)
                    if (relocated < m_selectedObjects.size()) {
                        // PERF: For single selection, use cached tile coord
                        if (m_selectedObjects.size() == 1 && 
                            (m_dragObjectTileCoord.first != 0 || m_dragObjectTileCoord.second != 0 ||
                             m_document->getTile(0, 0))) {
                            m_document->markTileDirty(m_dragObjectTileCoord);
                        } else {
                            // Multi-selection: need to search for each object's tile
                            for (InsertedObject* obj : m_selectedObjects) {
                                if (!obj) continue;
                                for (const auto& coord : m_document->allLoadedTileCoords()) {
                                    Page* tile = m_document->getTile(coord.first, coord.second);
                                    if (tile && tile->objectById(obj->id)) {
                                        m_document->markTileDirty(coord);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Paged mode: use cached page index for efficiency
                    int pageIdx = (m_dragObjectPageIndex >= 0) ? m_dragObjectPageIndex : m_currentPageIndex;
                    m_document->markPageDirty(pageIdx);
                }
            }
            
            // O2.7/BF.8: Create undo entry for each moved object
            for (InsertedObject* obj : m_selectedObjects) {
                if (!obj) continue;
                
                auto it = m_objectOriginalPositions.find(obj->id);
                if (it == m_objectOriginalPositions.end()) continue;
                
                QPointF oldPos = it.value();
                
                // Only create undo if position actually changed
                if (oldPos != obj->position) {
                    // For edgeless mode, we need to track tile changes
                    Document::TileCoord oldTile = {0, 0};
                    Document::TileCoord newTile = {0, 0};
                    
                    if (m_document->isEdgeless()) {
                        // PERF: For single selection, use cached tile coord
                        if (m_selectedObjects.size() == 1) {
                            newTile = m_dragObjectTileCoord;
                        } else {
                            // Multi-selection: need to search for object's tile
                            for (const auto& coord : m_document->allLoadedTileCoords()) {
                                Page* tile = m_document->getTile(coord.first, coord.second);
                                if (tile && tile->objectById(obj->id)) {
                                    newTile = coord;
                                    break;
                                }
                            }
                        }
                        // Old tile is same as new if no relocation happened
                        // (tracking across tiles would require storing tile info in m_objectOriginalPositions)
                        oldTile = newTile;
                    }
                    
                    pushObjectMoveUndo(obj, oldPos, m_currentPageIndex, oldTile, newTile);
                }
            }
        }
        
        // Clear original positions
        m_objectOriginalPositions.clear();
        m_isDraggingObjects = false;
        
        // Phase O4.1: Clear background snapshot and object cache, trigger full re-render
        m_objectDragBackgroundSnapshot = QPixmap();
        m_dragObjectRenderedCache = QPixmap();
        update();
    }
    
    m_pointerActive = false;
}

void DocumentViewport::clearObjectSelection()
{
    bool hadSelection = !m_selectedObjects.isEmpty();
    m_selectedObjects.clear();
    m_hoveredObject = nullptr;
    m_isDraggingObjects = false;
    if (hadSelection) {
        emit objectSelectionChanged();
    }
    update();
}

int DocumentViewport::relocateObjectsToCorrectTiles()
{
    if (!m_document || !m_document->isEdgeless() || m_selectedObjects.isEmpty()) {
        return 0;
    }
    
    int relocatedCount = 0;
    const int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // We need to iterate carefully because we're modifying selection pointers
    // Build list of objects that need relocation first
    struct RelocationInfo {
        QString objectId;
        Document::TileCoord currentTile;
        Document::TileCoord targetTile;
        QPointF newLocalPos;
    };
    QVector<RelocationInfo> toRelocate;
    
    // Find which tile each object is currently in and where it should be
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        // Find current tile by searching loaded tiles
        Document::TileCoord currentTile = {0, 0};
        bool foundTile = false;
        
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                currentTile = coord;
                foundTile = true;
                break;
            }
        }
        
        if (!foundTile) continue;  // Object not in any loaded tile?
        
        // Calculate object's document position
        QPointF tileOrigin(currentTile.first * tileSize, currentTile.second * tileSize);
        QPointF docPos = tileOrigin + obj->position;
        
        // Determine which tile it should be in based on top-left corner
        Document::TileCoord targetTile = m_document->tileCoordForPoint(docPos);
        
        if (targetTile != currentTile) {
            // Needs relocation
            QPointF newTileOrigin(targetTile.first * tileSize, targetTile.second * tileSize);
            QPointF newLocalPos = docPos - newTileOrigin;
            
            toRelocate.append({obj->id, currentTile, targetTile, newLocalPos});
        }
    }
    
    // Now perform the relocations
    for (const auto& info : toRelocate) {
        Page* oldTile = m_document->getTile(info.currentTile.first, info.currentTile.second);
        if (!oldTile) continue;
        
        // Extract from old tile
        std::unique_ptr<InsertedObject> extracted = oldTile->extractObject(info.objectId);
        if (!extracted) continue;
        
        // Update position to new tile-local coordinates
        extracted->position = info.newLocalPos;
        
        // Get or create target tile
        Page* newTile = m_document->getOrCreateTile(info.targetTile.first, info.targetTile.second);
        if (!newTile) {
            // Failed to get/create tile, put object back
            oldTile->addObject(std::move(extracted));
            continue;
        }
        
        // Get raw pointer BEFORE std::move (for updating selection)
        InsertedObject* newPtr = extracted.get();
        Q_UNUSED(newPtr);  // Selection update not needed - see note below
        
        // Add to new tile (transfers ownership)
        newTile->addObject(std::move(extracted));
        
        // Note on m_selectedObjects: The raw pointer in m_selectedObjects remains valid
        // because unique_ptr::get() returns the same address before and after moving
        // the unique_ptr. The object itself doesn't move in memory - only ownership
        // is transferred from oldTile to newTile. So m_selectedObjects still points
        // to the same valid object, now owned by newTile.
        
        // Mark both tiles dirty
        m_document->markTileDirty(info.currentTile);
        m_document->markTileDirty(info.targetTile);
        
        relocatedCount++;
    }
    
    return relocatedCount;
}

void DocumentViewport::selectObject(InsertedObject* obj, bool addToSelection)
{
    if (!obj) return;
    
    bool changed = false;
    
    if (!addToSelection) {
        // Replace selection
        if (m_selectedObjects.size() != 1 || !m_selectedObjects.contains(obj)) {
            m_selectedObjects.clear();
            m_selectedObjects.append(obj);
            changed = true;
        }
    } else {
        // Add to selection
        if (!m_selectedObjects.contains(obj)) {
            m_selectedObjects.append(obj);
            changed = true;
        }
    }
    
    if (changed) {
        emit objectSelectionChanged();
        
        // Phase C.2.4: Auto-switch insert mode based on selected object type
        if (m_selectedObjects.size() == 1) {
            InsertedObject* selected = m_selectedObjects.first();
            ObjectInsertMode newMode = m_objectInsertMode;
            
            if (selected->type() == "image") {
                newMode = ObjectInsertMode::Image;
            } else if (selected->type() == "link") {
                newMode = ObjectInsertMode::Link;
            }
            
            if (newMode != m_objectInsertMode) {
                m_objectInsertMode = newMode;
                emit objectInsertModeChanged(m_objectInsertMode);
            }
        }
        
        update();
    }
}

void DocumentViewport::deselectObject(InsertedObject* obj)
{
    if (!obj) return;
    
    if (m_selectedObjects.removeOne(obj)) {
        emit objectSelectionChanged();
        update();
    }
}

void DocumentViewport::deselectAllObjects()
{
    if (m_selectedObjects.isEmpty()) return;
    
    m_selectedObjects.clear();
    emit objectSelectionChanged();
    update();
}

void DocumentViewport::cancelObjectSelectAction()
{
    // Step 1: If objects are selected, deselect them
    if (!m_selectedObjects.isEmpty()) {
        deselectAllObjects();
        return;
    }
    
    // Step 2: If no objects selected but clipboard has content, clear clipboard
    if (!m_objectClipboard.isEmpty()) {
        clearObjectClipboard();
    }
}

void DocumentViewport::clearObjectClipboard()
{
    if (m_objectClipboard.isEmpty()) return;
    
    m_objectClipboard.clear();
    emit objectClipboardChanged(false);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "clearObjectClipboard: Object clipboard cleared";
#endif
}

void DocumentViewport::deselectObjectById(const QString& objectId)
{
    for (int i = m_selectedObjects.size() - 1; i >= 0; --i) {
        if (m_selectedObjects[i] && m_selectedObjects[i]->id == objectId) {
            m_selectedObjects.removeAt(i);
            emit objectSelectionChanged();
            update();
            return;
        }
    }
}

void DocumentViewport::moveSelectedObjects(const QPointF& delta)
{
    if (m_selectedObjects.isEmpty() || delta.isNull()) {
        return;
    }
    
    // Move all selected objects
    for (InsertedObject* obj : m_selectedObjects) {
        if (obj) {
            obj->position += delta;
        }
    }
    
    // Note: Page/tile dirty marking is done on drag release (O2.3.2)
    // to avoid marking dirty on every micro-movement during drag.
    // Tile boundary crossing is handled in O2.3.4.
    
    // Phase O4.1.3: Throttle updates to ~60fps
    // High-DPI mice/tablets can send 100s of events per second.
    // Only trigger repaint if enough time has passed since last update.
    if (!m_dragUpdateTimer.isValid() || 
        m_dragUpdateTimer.elapsed() >= DRAG_UPDATE_INTERVAL_MS) {
        m_dragUpdateTimer.restart();
        update();
    }
    // If throttled, the final position will be rendered on pointer release.
}

void DocumentViewport::pasteForObjectSelect()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pasteForObjectSelect: Called, insertMode =" 
             << (m_objectInsertMode == ObjectInsertMode::Image ? "Image" : "Link");
#endif
    
    // Phase O2.4.2: Tool-aware paste for ObjectSelect tool
    // Paste priority depends on ObjectInsertMode:
    // - Image mode: System clipboard images take priority, then internal clipboard
    // - Link mode: Internal clipboard takes priority (ignore system clipboard images)
    
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard || !clipboard->mimeData()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect: No clipboard or mimeData";
#endif
        return;
    }
    
    const QMimeData* mimeData = clipboard->mimeData();
    
    // ===== Link mode: Internal clipboard takes priority =====
    // When user is in Link mode, they're focused on LinkObjects.
    // System clipboard images should NOT interrupt pasting copied LinkObjects.
    if (m_objectInsertMode == ObjectInsertMode::Link) {
        // Priority 1 (Link mode): Internal object clipboard
        if (!m_objectClipboard.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "pasteForObjectSelect (Link mode): Internal clipboard has" 
                     << m_objectClipboard.size() << "objects";
#endif
            pasteObjects();
            return;
        }
        
        // Priority 2 (Link mode): Fall through - no internal clipboard content
        // In Link mode, we don't paste system clipboard images.
        // User can switch to Image mode if they want to paste an image.
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Link mode): No internal clipboard content, skipping system clipboard";
#endif
        return;
    }
    
    // ===== Image mode: System clipboard takes priority =====
    // Priority 1 (Image mode): System clipboard has raw image data
    if (mimeData->hasImage()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): Clipboard has raw image";
#endif
        insertImageFromClipboard();
        return;
    }
    
    // Priority 2 (Image mode/BF.1): File URLs (e.g., copied from Windows File Explorer)
    if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): Clipboard has URLs:" << urls;
#endif
        
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "pasteForObjectSelect (Image mode): Checking file:" << filePath;
#endif
                
                // Check if it's an image file
                QString lower = filePath.toLower();
                if (lower.endsWith(".png") || lower.endsWith(".jpg") || 
                    lower.endsWith(".jpeg") || lower.endsWith(".bmp") ||
                    lower.endsWith(".gif") || lower.endsWith(".webp")) {
                    
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "pasteForObjectSelect (Image mode): Loading image from file:" << filePath;
#endif
                    insertImageFromFile(filePath);
                    return;  // Only insert first image
                }
            }
        }
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): No valid image files in URLs";
#endif
    }
    
    // Priority 3 (Image mode): Internal object clipboard
    // Even in Image mode, paste internal objects if no system clipboard image
    if (!m_objectClipboard.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): Internal clipboard has" 
                 << m_objectClipboard.size() << "objects";
#endif
        pasteObjects();
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pasteForObjectSelect: Nothing to paste";
#endif
}

void DocumentViewport::insertImageFromClipboard()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromClipboard: Called";
#endif
    
    // Phase O2.4.3: Insert image from clipboard as ImageObject
    if (!m_document) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromClipboard: No document!";
#endif
        return;
    }
    
    // 1. Get image from clipboard
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromClipboard: No clipboard!";
#endif
        return;
    }
    
    QImage image = clipboard->image();
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromClipboard: image.isNull() =" << image.isNull() 
             << "size =" << image.size();
#endif

    // CRITICAL: This check must be OUTSIDE debug block to prevent crash in release builds
    if (image.isNull()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromClipboard: No valid image in clipboard";
#endif
        return;
    }
    
    // 2. Create ImageObject with setPixmap()
    auto imgObj = std::make_unique<ImageObject>();
    imgObj->setPixmap(QPixmap::fromImage(image));
    // NOTE: id is auto-generated in InsertedObject constructor
    
    // Scale size for high DPI displays
    // The pixmap dimensions are in physical pixels, but document coordinates
    // are in logical pixels. Dividing by DPR ensures 1:1 pixel mapping on screen.
    qreal dpr = devicePixelRatioF();
    if (dpr > 1.0) {
        imgObj->size = QSizeF(imgObj->size.width() / dpr, imgObj->size.height() / dpr);
    }
    
    // 3. Position at viewport center
    QPointF center = viewportCenterInDocument();
    imgObj->position = center - QPointF(imgObj->size.width() / 2.0, imgObj->size.height() / 2.0);
    
    // Phase O3.5.1: Default affinity based on active layer
    // Formula: activeLayer - 1, so image appears BELOW active layer's strokes
    // This allows user to immediately annotate the image with the active layer
    int activeLayer = m_document->isEdgeless() 
        ? m_edgelessActiveLayerIndex 
        : (m_document->page(m_currentPageIndex) 
           ? m_document->page(m_currentPageIndex)->activeLayerIndex 
           : 0);
    int defaultAffinity = activeLayer - 1;  // -1 minimum (background)
    imgObj->setLayerAffinity(defaultAffinity);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromClipboard: activeLayer =" << activeLayer 
             << "defaultAffinity =" << defaultAffinity;
#endif
    
    // CRITICAL: Save raw pointer BEFORE std::move invalidates imgObj
    InsertedObject* rawPtr = imgObj.get();
    
    // Track tile coord for undo (edgeless mode)
    Document::TileCoord insertedTileCoord = {0, 0};
    
    // 4. Add to appropriate page/tile
    if (m_document->isEdgeless()) {
        // Edgeless mode: find tile for the center position
        auto coord = m_document->tileCoordForPoint(imgObj->position);
        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            qWarning() << "insertImageFromClipboard: Failed to get/create tile";
            return;
        }
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        imgObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromClipboard: assigned zOrder =" << imgObj->zOrder;
        #endif
        
        // Convert to tile-local coordinates
        imgObj->position = imgObj->position - QPointF(
            coord.first * Document::EDGELESS_TILE_SIZE,
            coord.second * Document::EDGELESS_TILE_SIZE
        );
        
        targetTile->addObject(std::move(imgObj));
        m_document->markTileDirty(coord);
        insertedTileCoord = coord;  // Save for undo
    } else {
        // Paged mode: add to current page
        Page* targetPage = m_document->page(m_currentPageIndex);
        if (!targetPage) {
            qWarning() << "insertImageFromClipboard: No page at index" << m_currentPageIndex;
            return;
        }
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        imgObj->zOrder = getNextZOrderForAffinity(targetPage, defaultAffinity);
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromClipboard: assigned zOrder =" << imgObj->zOrder;
        #endif
        
        // Adjust position to be page-local (subtract page origin)
        QPointF pageOrigin = pagePosition(m_currentPageIndex);
        imgObj->position = imgObj->position - pageOrigin;
        
        targetPage->addObject(std::move(imgObj));
        m_document->markPageDirty(m_currentPageIndex);
    }
    
    // 5. Update max object extent for extended tile loading
    m_document->updateMaxObjectExtent(rawPtr);
    
    // 6. Save to assets folder (hash-based deduplication) - Phase O2.C: type-agnostic
    if (!m_document->bundlePath().isEmpty()) {
        if (!rawPtr->saveAssets(m_document->bundlePath())) {
            qWarning() << "insertImageFromClipboard: Failed to save assets";
            // Continue anyway - data is in memory and will be saved on document save
        }
    }
    
    // 7. Create undo entry (BF.6)
    pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);
    
    // 8. Select the new object
    deselectAllObjects();
    selectObject(rawPtr, false);
    
    // 9. Auto-switch to Select mode after inserting
    if (m_objectActionMode == ObjectActionMode::Create) {
        m_objectActionMode = ObjectActionMode::Select;
        emit objectActionModeChanged(m_objectActionMode);
    }
    
    // 10. Emit modification signal
    emit documentModified();
    
    update();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromClipboard: Inserted image" << rawPtr->id 
             << "size" << rawPtr->size << "at" << rawPtr->position;
#endif
}

void DocumentViewport::insertImageFromFile(const QString& filePath)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromFile: Called with path:" << filePath;
#endif
    
    if (!m_document) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromFile: No document!";
#endif
        return;
    }
    
    // 1. Load image from file
    QImage image(filePath);
    if (image.isNull()) {
        qWarning() << "insertImageFromFile: Failed to load image from" << filePath;
        return;
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromFile: Loaded image, size =" << image.size();
#endif
    
    // 2. Create ImageObject with setPixmap()
    auto imgObj = std::make_unique<ImageObject>();
    imgObj->setPixmap(QPixmap::fromImage(image));
    
    // Scale size for high DPI displays
    // The pixmap dimensions are in physical pixels, but document coordinates
    // are in logical pixels. Dividing by DPR ensures 1:1 pixel mapping on screen.
    qreal dpr = devicePixelRatioF();
    if (dpr > 1.0) {
        imgObj->size = QSizeF(imgObj->size.width() / dpr, imgObj->size.height() / dpr);
    }
    
    // 3. Position at viewport center
    QPointF center = viewportCenterInDocument();
    imgObj->position = center - QPointF(imgObj->size.width() / 2.0, imgObj->size.height() / 2.0);
    
    // Phase O3.5.1: Default affinity based on active layer
    // Formula: activeLayer - 1, so image appears BELOW active layer's strokes
    // This allows user to immediately annotate the image with the active layer
    int activeLayer = m_document->isEdgeless() 
        ? m_edgelessActiveLayerIndex 
        : (m_document->page(m_currentPageIndex) 
           ? m_document->page(m_currentPageIndex)->activeLayerIndex 
           : 0);
    int defaultAffinity = activeLayer - 1;  // -1 minimum (background)
    imgObj->setLayerAffinity(defaultAffinity);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromFile: activeLayer =" << activeLayer 
             << "defaultAffinity =" << defaultAffinity;
#endif
    
    // Store raw pointer BEFORE std::move
    InsertedObject* rawPtr = imgObj.get();
    
    // Track tile coord for undo (edgeless mode)
    Document::TileCoord insertedTileCoord = {0, 0};
    
    // 4. Add to appropriate page/tile
    if (m_document->isEdgeless()) {
        auto coord = m_document->tileCoordForPoint(imgObj->position);
        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            qWarning() << "insertImageFromFile: Failed to get/create tile";
            return;
        }
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        imgObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromFile: assigned zOrder =" << imgObj->zOrder;
        #endif
        
        // Convert to tile-local coordinates
        imgObj->position = imgObj->position - QPointF(
            coord.first * Document::EDGELESS_TILE_SIZE,
            coord.second * Document::EDGELESS_TILE_SIZE
        );
        
        targetTile->addObject(std::move(imgObj));
        m_document->markTileDirty(coord);
        insertedTileCoord = coord;  // Save for undo
    } else {
        // Paged mode: add to current page
        Page* targetPage = m_document->page(m_currentPageIndex);
        if (!targetPage) {
            qWarning() << "insertImageFromFile: No page at index" << m_currentPageIndex;
            return;
        }
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        imgObj->zOrder = getNextZOrderForAffinity(targetPage, defaultAffinity);
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "insertImageFromFile: assigned zOrder =" << imgObj->zOrder;
        #endif
        
        // Adjust position to be page-local
        QPointF pageOrigin = pagePosition(m_currentPageIndex);
        imgObj->position = imgObj->position - pageOrigin;
        
        targetPage->addObject(std::move(imgObj));
        m_document->markPageDirty(m_currentPageIndex);
    }
    
    // 5. Update max object extent
    m_document->updateMaxObjectExtent(rawPtr);
    
    // 6. Save to assets folder - Phase O2.C: type-agnostic
    if (!m_document->bundlePath().isEmpty()) {
        if (!rawPtr->saveAssets(m_document->bundlePath())) {
            qWarning() << "insertImageFromFile: Failed to save assets";
        }
    }
    
    // 7. Create undo entry (BF.6)
    pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);
    
    // 8. Select the new object
    deselectAllObjects();
    selectObject(rawPtr, false);
    
    // 9. Auto-switch to Select mode after inserting
    if (m_objectActionMode == ObjectActionMode::Create) {
        m_objectActionMode = ObjectActionMode::Select;
        emit objectActionModeChanged(m_objectActionMode);
    }
    
    // 10. Emit modification signal
    emit documentModified();
    
    update();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "insertImageFromFile: Inserted image" << rawPtr->id 
             << "size" << rawPtr->size << "at" << rawPtr->position;
    #endif
}

void DocumentViewport::insertImageFromDialog()
{
    // Phase C.0.5: Open file dialog to select an image
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Insert Image"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)")
    );
    
    if (filePath.isEmpty()) {
        return;  // User cancelled
    }
    
    // Insert at viewport center (handled by insertImageFromFile)
    insertImageFromFile(filePath);
}

void DocumentViewport::deleteSelectedObjects()
{
    // Phase O2.5.2: Delete all selected objects
    if (!m_document || m_selectedObjects.isEmpty()) {
        return;
    }
    
    // Phase M.2: Cascade delete markdown notes linked to LinkObjects
    int noteCount = 0;
    for (InsertedObject* obj : m_selectedObjects) {
        if (LinkObject* link = dynamic_cast<LinkObject*>(obj)) {
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                    noteCount++;
                }
            }
        }
    }
    
    // TODO: Show confirmation dialog if notes will be deleted
    // "This will delete N linked note(s). Continue?"
    if (noteCount > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deleteSelectedObjects: Cascade deleting" << noteCount << "markdown note(s)";
        #endif
    }
    
    // Delete markdown note files before removing LinkObjects
    for (InsertedObject* obj : m_selectedObjects) {
        if (LinkObject* link = dynamic_cast<LinkObject*>(obj)) {
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                    QString noteId = link->linkSlots[i].markdownNoteId;
                    if (!noteId.isEmpty()) {
                        m_document->deleteNoteFile(noteId);
                    }
                }
            }
        }
    }
    
    int deletedCount = 0;
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Find which tile contains each object and remove it
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            
            // Find the tile containing this object
            bool found = false;
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    // Create undo entry BEFORE removing (object still valid)
                    pushObjectDeleteUndo(obj, -1, coord);
                    
                    // Remove object from tile
                    tile->removeObject(obj->id);
                    m_document->markTileDirty(coord);
                    deletedCount++;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                #ifdef SPEEDYNOTE_DEBUG
                qWarning() << "deleteSelectedObjects: Object" << obj->id << "not found in any tile";
                #endif
            }
        }
    } else {
        // ========== PAGED MODE ==========
        // Objects on current page (typically where selection was made)
        Page* currentPage = m_document->page(m_currentPageIndex);
        if (currentPage) {
            for (InsertedObject* obj : m_selectedObjects) {
                if (!obj) continue;
                
                // Check if object is on current page
                if (currentPage->objectById(obj->id)) {
                    // Create undo entry BEFORE removing (object still valid)
                    pushObjectDeleteUndo(obj, m_currentPageIndex, {});
                    
                    // Remove object from page
                    currentPage->removeObject(obj->id);
                    m_document->markPageDirty(m_currentPageIndex);
                    deletedCount++;
                } else {
                    // Object might be on a different page - search loaded pages only
                    // PERF FIX: Only search loaded pages to avoid triggering lazy loading
                    bool found = false;
                    for (int i : m_document->loadedPageIndices()) {
                        Page* page = m_document->page(i);  // Already loaded, no disk I/O
                        if (page && page->objectById(obj->id)) {
                            // Create undo entry BEFORE removing (object still valid)
                            pushObjectDeleteUndo(obj, i, {});
                            
                            page->removeObject(obj->id);
                            m_document->markPageDirty(i);
                            deletedCount++;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        qWarning() << "deleteSelectedObjects: Object" << obj->id << "not found on any loaded page";
                    }
                }
            }
        }
    }
    
    // Recalculate max object extent (removed object might have been largest)
    m_document->recalculateMaxObjectExtent();
    
    // Clear selection (objects are now deleted, pointers are invalid)
    m_selectedObjects.clear();
    m_hoveredObject = nullptr;
    emit objectSelectionChanged();
    
    // Emit modification signal
    if (deletedCount > 0) {
        emit documentModified();
        emit linkObjectListMayHaveChanged();  // M.7.3: Refresh sidebar
    }
    
    update();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "deleteSelectedObjects: Deleted" << deletedCount << "objects";
    #endif
}

void DocumentViewport::copySelectedObjects()
{
    // Phase O2.6.2: Copy selected objects to internal clipboard
    if (m_selectedObjects.isEmpty()) {
        return;
    }
    
    // Clear previous clipboard contents
    m_objectClipboard.clear();
    
    // Serialize each selected object to JSON
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        // Phase C.2.3: For LinkObject, use cloneWithBackLink to auto-fill slot 0
        // with a back-link to the original position
        if (auto* link = dynamic_cast<LinkObject*>(obj)) {
            if (m_document->isEdgeless()) {
                // Edgeless mode: find the tile containing this object
                // and create back-link with tile coordinates + document position
                bool foundTile = false;
                for (const auto& coord : m_document->allLoadedTileCoords()) {
                    Page* tile = m_document->getTile(coord.first, coord.second);
                    if (tile && tile->objectById(link->id)) {
                        // Found the tile - calculate document coordinates
                        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                                           coord.second * Document::EDGELESS_TILE_SIZE);
                        QPointF docPos = tileOrigin + link->position;
                        
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "copySelectedObjects (edgeless): link->id =" << link->id
                                 << "tile coord =" << coord.first << "," << coord.second
                                 << "tileOrigin =" << tileOrigin
                                 << "link->position (tile-local) =" << link->position
                                 << "docPos (calculated) =" << docPos;
#endif
                        
                        // Create clone with back-link to this position
                        auto clone = link->cloneWithBackLinkEdgeless(coord.first, coord.second, docPos);
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "  Back-link slot will store: tileX =" << coord.first 
                                 << "tileY =" << coord.second
                                 << "targetPosition =" << clone->linkSlots[0].targetPosition;
#endif
                        m_objectClipboard.append(clone->toJson());
                        foundTile = true;
                        break;
                    }
                }
                if (!foundTile) {
                    // Fallback: copy without back-link if tile not found
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "copySelectedObjects (edgeless): tile not found for link->id =" << link->id;
#endif
                    m_objectClipboard.append(link->toJson());
                }
            } else {
                // Paged mode: use page UUID
                QString sourcePageUuid;
                Page* currentPage = m_document->page(m_currentPageIndex);
                if (currentPage) {
                    sourcePageUuid = currentPage->uuid;
                }
                
                auto clone = link->cloneWithBackLink(sourcePageUuid);
                m_objectClipboard.append(clone->toJson());
            }
        } else {
            m_objectClipboard.append(obj->toJson());
        }
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "copySelectedObjects: Copied" << m_objectClipboard.size() << "objects to internal clipboard";
    #endif
    
    // Notify that object clipboard has content (for action bar paste button)
    emit objectClipboardChanged(!m_objectClipboard.isEmpty());
}

void DocumentViewport::pasteObjects()
{
    // Phase O2.6.3: Paste objects from internal clipboard
    if (!m_document || m_objectClipboard.isEmpty()) {
        return;
    }
    
    // Clear current selection - we'll select the pasted objects
    deselectAllObjects();
    
    // Track newly pasted objects for selection
    QList<InsertedObject*> pastedObjects;
    
    // Calculate paste position based on mouse cursor
    QPoint cursorViewport = mapFromGlobal(QCursor::pos());
    bool useCursorPosition = false;
    QPointF pastePagePos;
    
    if (rect().contains(cursorViewport)) {
        // Cursor is within the viewport - use its position
        if (m_document->isEdgeless()) {
            // Edgeless: convert to document coordinates
            pastePagePos = viewportToDocument(cursorViewport);
            useCursorPosition = true;
        } else {
            // Paged: convert to page-local coordinates using PageHit
            PageHit hit = viewportToPage(QPointF(cursorViewport));
            if (hit.valid() && hit.pageIndex == m_currentPageIndex) {
                // Cursor is on the current page - clamp to page bounds
                Page* page = m_document->page(hit.pageIndex);
                if (page) {
                    pastePagePos.setX(qBound(0.0, hit.pagePoint.x(), page->size.width() - 24.0));
                    pastePagePos.setY(qBound(0.0, hit.pagePoint.y(), page->size.height() - 24.0));
                    useCursorPosition = true;
                }
            }
        }
    }
    
    // Fallback: paste at top-left with offset
    constexpr qreal PASTE_OFFSET = 20.0;
    if (!useCursorPosition) {
        pastePagePos = QPointF(PASTE_OFFSET, PASTE_OFFSET);
    }
    
    for (const QJsonObject& jsonObj : m_objectClipboard) {
        // Deserialize object
        std::unique_ptr<InsertedObject> obj = InsertedObject::fromJson(jsonObj);
        if (!obj) {
            qWarning() << "pasteObjects: Failed to deserialize object from clipboard";
            continue;
        }
        
        // Assign new UUID (critical for uniqueness)
        obj->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        
        // Set position to paste location (cursor or fallback)
        obj->position = pastePagePos;
        
        // Phase O2.C: Load any external assets (type-agnostic)
        if (!m_document->bundlePath().isEmpty()) {
            if (!obj->loadAssets(m_document->bundlePath())) {
                qWarning() << "pasteObjects: Failed to load assets for pasted object";
                // Continue anyway - object will render as empty
            }
        }
        
        // Store raw pointer BEFORE std::move
        InsertedObject* rawPtr = obj.get();
        
        // Add to appropriate page/tile
        // Track tile coord for undo (edgeless mode)
        Document::TileCoord insertedTileCoord = {0, 0};
        
        if (m_document->isEdgeless()) {
            // Calculate which tile the object belongs to based on its position
            auto coord = m_document->tileCoordForPoint(obj->position);
            Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
            if (!targetTile) {
                qWarning() << "pasteObjects: Failed to get/create tile";
                continue;
            }
            
            // Set zOrder so pasted object appears on top of existing objects with same affinity
            int affinity = obj->getLayerAffinity();
            obj->zOrder = getNextZOrderForAffinity(targetTile, affinity);
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "pasteObjects: assigned zOrder =" << obj->zOrder << "for affinity =" << affinity;
            #endif
            
            // Convert to tile-local coordinates
            obj->position = obj->position - QPointF(
                coord.first * Document::EDGELESS_TILE_SIZE,
                coord.second * Document::EDGELESS_TILE_SIZE
            );
            
            targetTile->addObject(std::move(obj));
            m_document->markTileDirty(coord);
            insertedTileCoord = coord;
        } else {
            // Paged mode: add to current page
            Page* targetPage = m_document->page(m_currentPageIndex);
            if (!targetPage) {
                qWarning() << "pasteObjects: No page at index" << m_currentPageIndex;
                continue;
            }
            
            // Set zOrder so pasted object appears on top of existing objects with same affinity
            int affinity = obj->getLayerAffinity();
            obj->zOrder = getNextZOrderForAffinity(targetPage, affinity);
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "pasteObjects: assigned zOrder =" << obj->zOrder << "for affinity =" << affinity;
            #endif
            
            targetPage->addObject(std::move(obj));
            m_document->markPageDirty(m_currentPageIndex);
        }
        
        // Update max object extent
        m_document->updateMaxObjectExtent(rawPtr);
        
        // Create undo entry for this pasted object
        pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);
        
        // Track for selection
        pastedObjects.append(rawPtr);
    }
    
    // Select all pasted objects
    for (InsertedObject* obj : pastedObjects) {
        selectObject(obj, true);  // addToSelection = true
    }
    
    if (!pastedObjects.isEmpty()) {
        emit documentModified();
        emit linkObjectListMayHaveChanged();  // M.7.3: Refresh sidebar
    }
    
    update();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pasteObjects: Pasted" << pastedObjects.size() << "objects from internal clipboard";
    #endif
}

// ===== LinkObject Creation (Phase C.3.2 & C.4.5) =====

void DocumentViewport::createLinkObjectForHighlight(int pageIndex)
{
    // Phase C.3.2: Create LinkObject for text highlight
    if (!m_document || m_textSelection.highlightRects.isEmpty()) {
        return;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return;
    }
    
    // Create LinkObject
    auto linkObj = std::make_unique<LinkObject>();
    
    // Position to the LEFT of the first highlight rect (in the margin)
    // This avoids overlapping with the highlight strokes
    QRectF firstRect = m_textSelection.highlightRects[0];
    qreal firstRectX = firstRect.x() * PDF_TO_PAGE_SCALE;
    qreal firstRectY = firstRect.y() * PDF_TO_PAGE_SCALE;
    
    // Place icon to the left with padding, but clamp to avoid negative coords
    constexpr qreal MARGIN_PADDING = 4.0;
    qreal iconX = firstRectX - LinkObject::ICON_SIZE - MARGIN_PADDING;
    if (iconX < MARGIN_PADDING) {
        iconX = MARGIN_PADDING;  // Keep small margin from page edge
    }
    
    linkObj->position = QPointF(iconX, firstRectY);
    
    // Set description to extracted text
    linkObj->description = m_textSelection.selectedText;
    
    // Use a DARKER version of highlighter color for visibility on white pages
    // Light colors like yellow become hard to see, so we darken by ~50%
    QColor darkened = m_highlighterColor;
    darkened.setRed(darkened.red() * 0.5);
    darkened.setGreen(darkened.green() * 0.5);
    darkened.setBlue(darkened.blue() * 0.5);
    darkened.setAlpha(255);  // Full opacity for visibility
    linkObj->iconColor = darkened;
    
    // Set default affinity (activeLayer - 1, so it appears below strokes)
    int activeLayer = page->activeLayerIndex;
    int defaultAffinity = activeLayer - 1;
    linkObj->setLayerAffinity(defaultAffinity);
    
    // Set zOrder so new object appears on top of existing objects with same affinity
    linkObj->zOrder = getNextZOrderForAffinity(page, defaultAffinity);
    
    // Store raw pointer BEFORE std::move
    LinkObject* rawPtr = linkObj.get();
    
    // Add to page
    page->addObject(std::move(linkObj));
    
    // Mark page dirty for save
    m_document->markPageDirty(pageIndex);
    
    // Push undo action (empty tile coord for paged mode)
    pushObjectInsertUndo(rawPtr, pageIndex, {});
    
#ifdef QT_DEBUG
    qDebug() << "Created LinkObject for highlight on page" << pageIndex
             << "description:" << rawPtr->description.left(30);
#endif
}

void DocumentViewport::createLinkObjectAtPosition(int pageIndex, const QPointF& pagePos, const QPointF& viewportPos)
{
    // Phase C.4.5: Create empty LinkObject at specified position
    if (!m_document) return;
    
    auto linkObj = std::make_unique<LinkObject>();
    linkObj->position = pagePos;
    linkObj->description = QString();  // Empty for manual creation
    
    // Store raw pointer BEFORE std::move
    LinkObject* rawPtr = linkObj.get();
    
    // Track tile coord for undo (edgeless mode)
    Document::TileCoord insertedTileCoord = {0, 0};
    
    if (m_document->isEdgeless()) {
        // Edgeless mode: pagePos is already tile-local from handlePointerPress_ObjectSelect
        // BUG FIX: Use viewportPos from the input event to determine tile coordinate.
        // Previously used QCursor::pos() which gives wrong results for tablet/stylus input
        // (cursor position can differ from tablet event position, causing objects to be
        // placed on the wrong tile - typically 1 tile to the right on leftmost tiles).
        QPointF docPos = viewportToDocument(viewportPos);
        auto coord = m_document->tileCoordForPoint(docPos);
        
        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            qWarning() << "createLinkObjectAtPosition: Failed to get/create tile";
            return;
        }
        
        // Default affinity based on active layer
        int activeLayer = m_edgelessActiveLayerIndex;
        int defaultAffinity = activeLayer - 1;
        linkObj->setLayerAffinity(defaultAffinity);
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        linkObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);
        
        targetTile->addObject(std::move(linkObj));
        m_document->markTileDirty(coord);
        insertedTileCoord = coord;
    } else {
        // Paged mode
        Page* page = m_document->page(pageIndex);
        if (!page) {
            qWarning() << "createLinkObjectAtPosition: No page at index" << pageIndex;
            return;
        }
        
        // Default affinity based on active layer
        int activeLayer = page->activeLayerIndex;
        int defaultAffinity = activeLayer - 1;
        linkObj->setLayerAffinity(defaultAffinity);
        
        // Set zOrder so new object appears on top of existing objects with same affinity
        linkObj->zOrder = getNextZOrderForAffinity(page, defaultAffinity);
        
        page->addObject(std::move(linkObj));
        m_document->markPageDirty(pageIndex);
    }
    
    // Push undo action
    pushObjectInsertUndo(rawPtr, pageIndex, insertedTileCoord);
    
    // Select the new object
    deselectAllObjects();
    selectObject(rawPtr, false);
    
    // Auto-switch to Select mode after inserting
    if (m_objectActionMode == ObjectActionMode::Create) {
        m_objectActionMode = ObjectActionMode::Select;
        emit objectActionModeChanged(m_objectActionMode);
    }
    
    emit documentModified();
    update();
    
#ifdef SPEEDYNOTE_DEBUG
    if (m_document && m_document->isEdgeless()) {
        QPointF docPos = viewportToDocument(viewportPos);
        auto coord = m_document->tileCoordForPoint(docPos);
        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                           coord.second * Document::EDGELESS_TILE_SIZE);
        qDebug() << "createLinkObjectAtPosition (edgeless): "
                 << "pagePos (stored as position) =" << pagePos
                 << "tile =" << coord.first << "," << coord.second
                 << "docPos from viewportPos =" << docPos
                 << "tileOrigin =" << tileOrigin;
    } else {
    qDebug() << "createLinkObjectAtPosition: Created LinkObject at" << pagePos;
    }
#endif
}

// ===== Link Slot Activation (Phase C.4.3) =====

void DocumentViewport::activateLinkSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "activateLinkSlot: Invalid slot index" << slotIndex;
        #endif
        return;
    }
    
    // Must have exactly one LinkObject selected
    if (m_selectedObjects.size() != 1) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "activateLinkSlot: Need exactly one object selected";
        #endif
        return;
    }
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "activateLinkSlot: Selected object is not a LinkObject";
        #endif
        return;
    }
    
    const LinkSlot& slot = link->linkSlots[slotIndex];
    
    if (slot.isEmpty()) {
        // Empty slot - show menu to add link (Phase C.5.3)
        addLinkToSlot(slotIndex);
        return;
    }
    
    // Activate the slot based on type
    switch (slot.type) {
        case LinkSlot::Type::Position:
            // Navigate to position (paged or edgeless)
            if (slot.isEdgelessTarget) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "activateLinkSlot: Edgeless position link"
                         << "tileX =" << slot.edgelessTileX
                         << "tileY =" << slot.edgelessTileY
                         << "targetPosition =" << slot.targetPosition;
#endif
                // Save current position before jumping (Phase 4)
                pushPositionHistory();
                
                // Edgeless mode: navigate to tile + document position
                navigateToEdgelessPosition(slot.edgelessTileX, slot.edgelessTileY, slot.targetPosition);
            } else {
                // Paged mode: navigate to page UUID + page-local position
            navigateToPosition(slot.targetPageUuid, slot.targetPosition);
            }
            break;
            
        case LinkSlot::Type::Url:
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "activateLinkSlot: Opening URL" << slot.url;
#endif
            QDesktopServices::openUrl(QUrl(slot.url));
            break;
            
        case LinkSlot::Type::Markdown:
        {
            // Phase M.2: Open markdown note in sidebar
            QString noteId = slot.markdownNoteId;
            QString notePath = m_document->notesPath() + "/" + noteId + ".md";
            
            if (!QFile::exists(notePath)) {
                qWarning() << "activateLinkSlot: Markdown note file not found, clearing broken reference:" << notePath;
                link->linkSlots[slotIndex].clear();
                
                // Mark page dirty
                Page* page = findPageContainingObject(link);
                if (page) {
                    int pageIndex = m_document->pageIndexByUuid(page->uuid);
                    if (pageIndex >= 0) {
                        m_document->markPageDirty(pageIndex);
                    }
                }
                
                emit documentModified();
                update();
                // TODO: Notify user that note was missing
                return;
            }
            
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "activateLinkSlot: Opening markdown note" << noteId;
            #endif
            emit requestOpenMarkdownNote(noteId, link->id);
            break;
        }
            
        default:
            break;
    }
}

void DocumentViewport::addLinkToSlot(int slotIndex)
{
    // Phase C.5.3 (TEMPORARY): Simple menu UI for adding links to slots
    // This will be replaced with a proper subtoolbar in the future
    
    if (m_selectedObjects.size() != 1) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addLinkToSlot: Need exactly one object selected";
        #endif
        return;
    }
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addLinkToSlot: Selected object is not a LinkObject";
        #endif
        return;
    }
    
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addLinkToSlot: Invalid slot index" << slotIndex;
        #endif
        return;
    }
    
    // Simple context menu (TEMPORARY UI)
    QMenu menu;
    QAction* posAction = menu.addAction(tr("Add Position Link"));
    QAction* urlAction = menu.addAction(tr("Add URL Link"));
    QAction* mdAction = menu.addAction(tr("Add Markdown Note"));
    
    QAction* selected = menu.exec(QCursor::pos());
    
    if (selected == posAction) {
        // TODO: Enter "pick position" mode (requires additional UI work)
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "addLinkToSlot: Position link - TODO: implement pick position mode";
        #endif
    } else if (selected == urlAction) {
        QString url = QInputDialog::getText(this, tr("Add URL"), tr("Enter URL:"));
        if (!url.isEmpty()) {
            link->linkSlots[slotIndex].type = LinkSlot::Type::Url;
            link->linkSlots[slotIndex].url = url;
            
            // Mark page dirty - find which page contains this object
            Document::TileCoord tileCoord;
            Page* page = findPageContainingObject(link, &tileCoord);
            if (page && m_document) {
                if (m_document->isEdgeless()) {
                    m_document->markTileDirty(tileCoord);
                } else {
                    // Use cached UUID→index lookup (O(1) from Phase C.0.2)
                    int pageIndex = m_document->pageIndexByUuid(page->uuid);
                    if (pageIndex >= 0) {
                        m_document->markPageDirty(pageIndex);
                    }
                }
            }
            
            emit documentModified();
            update();
            
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "addLinkToSlot: Added URL link to slot" << slotIndex << ":" << url;
            #endif
        }
    } else if (selected == mdAction) {
        // Phase M.2: Create markdown note for this slot
        createMarkdownNoteForSlot(slotIndex);
    }
}

void DocumentViewport::clearLinkSlot(int slotIndex)
{
    // Phase D: Clear a LinkObject slot content (called from ObjectSelectSubToolbar)
    
    if (m_selectedObjects.size() != 1) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "clearLinkSlot: Need exactly one object selected";
        #endif
        return;
    }
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "clearLinkSlot: Selected object is not a LinkObject";
        #endif
        return;
    }
    
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "clearLinkSlot: Invalid slot index" << slotIndex;
        #endif
        return;
    }
    
    // Check if slot is already empty
    if (link->linkSlots[slotIndex].isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "clearLinkSlot: Slot" << slotIndex << "is already empty";
        #endif
        return;
    }
    
    LinkSlot& slot = link->linkSlots[slotIndex];
    LinkSlot::Type oldType = slot.type;
    
    // Phase M.2: If markdown slot, delete the note file
    if (slot.type == LinkSlot::Type::Markdown) {
        QString noteId = slot.markdownNoteId;
        if (!noteId.isEmpty()) {
            m_document->deleteNoteFile(noteId);
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "clearLinkSlot: Deleted markdown note file" << noteId;
            #endif
        }
    }
    
    // Clear the slot using LinkSlot::clear() which resets to default state
    slot.clear();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "clearLinkSlot: Cleared slot" << slotIndex 
             << "(was type" << static_cast<int>(oldType) << ")";
    #endif
    // Mark page dirty
    Page* page = findPageContainingObject(link);
    if (page) {
        int pageIndex = m_document->pageIndexByUuid(page->uuid);
        if (pageIndex >= 0) {
            m_document->markPageDirty(pageIndex);
        }
    }
    
    update();
}

void DocumentViewport::createMarkdownNoteForSlot(int slotIndex)
{
    // Phase M.2: Create a new markdown note for an empty LinkSlot
    
    // Validate selection - need exactly one LinkObject selected
    if (m_selectedObjects.size() != 1) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "createMarkdownNoteForSlot: Need exactly one object selected";
        #endif
        return;
    }
    
    LinkObject* link = dynamic_cast<LinkObject*>(m_selectedObjects[0]);
    if (!link) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "createMarkdownNoteForSlot: Selected object is not a LinkObject";
        #endif
        return;
    }
    
    // Validate slot index
    if (slotIndex < 0 || slotIndex >= LinkObject::SLOT_COUNT) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "createMarkdownNoteForSlot: Invalid slot index" << slotIndex;
        #endif
        return;
    }
    
    // Check slot is empty
    if (!link->linkSlots[slotIndex].isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "createMarkdownNoteForSlot: Slot" << slotIndex << "is not empty";
        #endif
        return;
    }
    
    // Check document is saved (needed for file path)
    QString notesDir = m_document->notesPath();
    if (notesDir.isEmpty()) {
        qWarning() << "createMarkdownNoteForSlot: Cannot create note - document not saved";
        // TODO: Show user message via a signal
        return;
    }
    
    // Generate note ID
    QString noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Create note with default title from LinkObject description
    MarkdownNote note;
    note.id = noteId;
    note.title = link->description.isEmpty() 
        ? tr("Untitled Note") 
        : link->description.left(50);
    note.content = "";
    
    // Save note file
    QString filePath = notesDir + "/" + noteId + ".md";
    if (!note.saveToFile(filePath)) {
        qWarning() << "createMarkdownNoteForSlot: Failed to create note file:" << filePath;
        return;
    }
    
    // Update slot
    link->linkSlots[slotIndex].type = LinkSlot::Type::Markdown;
    link->linkSlots[slotIndex].markdownNoteId = noteId;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "createMarkdownNoteForSlot: Created note" << noteId 
             << "for slot" << slotIndex << "title:" << note.title;
    #endif
    // Mark page dirty
    Page* page = findPageContainingObject(link);
    if (page) {
        int pageIndex = m_document->pageIndexByUuid(page->uuid);
        if (pageIndex >= 0) {
            m_document->markPageDirty(pageIndex);
        }
    }
    
    emit documentModified();
    emit requestOpenMarkdownNote(noteId, link->id);
    
    update();
}

// ===== Object Z-Order (Phase O2.8) =====

void DocumentViewport::bringSelectedToFront()
{
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "bringSelectedToFront: called, selectedObjects count =" << m_selectedObjects.size();
    #endif
    if (!m_document || m_selectedObjects.isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "bringSelectedToFront: early return - document:" << (m_document != nullptr) 
                 << "selectedObjects empty:" << m_selectedObjects.isEmpty();
        #endif
        return;
    }
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "bringSelectedToFront: processing obj" << obj->id 
                 << "current zOrder =" << obj->zOrder;
        #endif
        // Find the page/tile containing this object
        Page* page = nullptr;
        Document::TileCoord tileCoord = {0, 0};
        
        if (m_document->isEdgeless()) {
            // Search loaded tiles for this object
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    page = tile;
                    tileCoord = coord;
                    break;
                }
            }
        } else {
            page = m_document->page(m_currentPageIndex);
        }
        
        if (!page) {
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "bringSelectedToFront: page not found for obj" << obj->id;
            #endif
            continue;
        }
        
        // Find max zOrder among objects with same affinity
        int affinity = obj->getLayerAffinity();
        int maxZOrder = obj->zOrder;
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "bringSelectedToFront: obj affinity =" << affinity 
                 << "page has" << page->objects.size() << "objects";
        #endif
        for (const auto& otherObj : page->objects) {
            if (otherObj.get() != obj && otherObj->getLayerAffinity() == affinity) {
                #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "  other obj" << otherObj->id << "zOrder =" << otherObj->zOrder;
                #endif
                maxZOrder = qMax(maxZOrder, otherObj->zOrder);
            }
        }
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "bringSelectedToFront: maxZOrder found =" << maxZOrder;
        #endif
        
        // Set zOrder to max + 1
        if (obj->zOrder != maxZOrder + 1) {
            int oldZOrder = obj->zOrder;
            obj->zOrder = maxZOrder + 1;
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "bringSelectedToFront: changed zOrder from" << oldZOrder << "to" << obj->zOrder;
            #endif
            page->rebuildAffinityMap();  // Rebuild since zOrder changed
            
            if (m_document->isEdgeless()) {
                m_document->markTileDirty(tileCoord);
            } else {
                m_document->markPageDirty(m_currentPageIndex);
            }
        } else {
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "bringSelectedToFront: zOrder unchanged (already at max+1)";
            #endif
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::sendSelectedToBack()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        // Find the page/tile containing this object
        Page* page = nullptr;
        Document::TileCoord tileCoord = {0, 0};
        
        if (m_document->isEdgeless()) {
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    page = tile;
                    tileCoord = coord;
                    break;
                }
            }
        } else {
            page = m_document->page(m_currentPageIndex);
        }
        
        if (!page) continue;
        
        // Find min zOrder among objects with same affinity
        int affinity = obj->getLayerAffinity();
        int minZOrder = obj->zOrder;
        
        for (const auto& otherObj : page->objects) {
            if (otherObj.get() != obj && otherObj->getLayerAffinity() == affinity) {
                minZOrder = qMin(minZOrder, otherObj->zOrder);
            }
        }
        
        // Set zOrder to min - 1
        if (obj->zOrder != minZOrder - 1) {
            obj->zOrder = minZOrder - 1;
            page->rebuildAffinityMap();
            
            if (m_document->isEdgeless()) {
                m_document->markTileDirty(tileCoord);
            } else {
                m_document->markPageDirty(m_currentPageIndex);
            }
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::bringSelectedForward()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        // Find the page/tile containing this object
        Page* page = nullptr;
        Document::TileCoord tileCoord = {0, 0};
        
        if (m_document->isEdgeless()) {
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    page = tile;
                    tileCoord = coord;
                    break;
                }
            }
        } else {
            page = m_document->page(m_currentPageIndex);
        }
        
        if (!page) continue;
        
        // Find the object with the next higher zOrder in same affinity group
        int affinity = obj->getLayerAffinity();
        InsertedObject* nextHigher = nullptr;
        int nextHigherZOrder = INT_MAX;
        
        for (const auto& otherObj : page->objects) {
            if (otherObj.get() != obj && 
                otherObj->getLayerAffinity() == affinity &&
                otherObj->zOrder > obj->zOrder &&
                otherObj->zOrder < nextHigherZOrder) {
                nextHigher = otherObj.get();
                nextHigherZOrder = otherObj->zOrder;
            }
        }
        
        // Swap zOrders if found
        if (nextHigher) {
            int temp = obj->zOrder;
            obj->zOrder = nextHigher->zOrder;
            nextHigher->zOrder = temp;
            page->rebuildAffinityMap();
            
            if (m_document->isEdgeless()) {
                m_document->markTileDirty(tileCoord);
            } else {
                m_document->markPageDirty(m_currentPageIndex);
            }
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::sendSelectedBackward()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        // Find the page/tile containing this object
        Page* page = nullptr;
        Document::TileCoord tileCoord = {0, 0};
        
        if (m_document->isEdgeless()) {
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    page = tile;
                    tileCoord = coord;
                    break;
                }
            }
        } else {
            page = m_document->page(m_currentPageIndex);
        }
        
        if (!page) continue;
        
        // Find the object with the next lower zOrder in same affinity group
        int affinity = obj->getLayerAffinity();
        InsertedObject* nextLower = nullptr;
        int nextLowerZOrder = INT_MIN;
        
        for (const auto& otherObj : page->objects) {
            if (otherObj.get() != obj && 
                otherObj->getLayerAffinity() == affinity &&
                otherObj->zOrder < obj->zOrder &&
                otherObj->zOrder > nextLowerZOrder) {
                nextLower = otherObj.get();
                nextLowerZOrder = otherObj->zOrder;
            }
        }
        
        // Swap zOrders if found
        if (nextLower) {
            int temp = obj->zOrder;
            obj->zOrder = nextLower->zOrder;
            nextLower->zOrder = temp;
            page->rebuildAffinityMap();
            
            if (m_document->isEdgeless()) {
                m_document->markTileDirty(tileCoord);
            } else {
                m_document->markPageDirty(m_currentPageIndex);
            }
        }
    }
    
    emit documentModified();
    update();
}

// =============================================================================
// Layer Affinity Shortcuts (Phase O3.5.2)
// =============================================================================

void DocumentViewport::increaseSelectedAffinity()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    int maxAffinity = getMaxAffinity();
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "increaseSelectedAffinity: maxAffinity =" << maxAffinity;
    #endif
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        int currentAffinity = obj->getLayerAffinity();
        if (currentAffinity >= maxAffinity) {
            qDebug() << "  obj" << obj->id << "already at max affinity" << currentAffinity;
            continue;
        }
        
        Document::TileCoord tileCoord = {0, 0};
        Page* page = findPageContainingObject(obj, &tileCoord);
        if (!page) continue;
        
        int oldAffinity = currentAffinity;
        page->updateObjectAffinity(obj->id, currentAffinity + 1);
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "  obj" << obj->id << "affinity:" << oldAffinity 
                 << "->" << obj->getLayerAffinity();
        #endif
        
        // Phase O3.5.3: Push undo entry for affinity change
        pushObjectAffinityUndo(obj, oldAffinity);
        
        if (m_document->isEdgeless()) {
            m_document->markTileDirty(tileCoord);
        } else {
            m_document->markPageDirty(m_currentPageIndex);
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::decreaseSelectedAffinity()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    const int minAffinity = -1;  // Background
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "decreaseSelectedAffinity: minAffinity =" << minAffinity;
    #endif
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        int currentAffinity = obj->getLayerAffinity();
        if (currentAffinity <= minAffinity) {
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "  obj" << obj->id << "already at min affinity" << currentAffinity;
            #endif
            continue;
        }
        
        Document::TileCoord tileCoord = {0, 0};
        Page* page = findPageContainingObject(obj, &tileCoord);
        if (!page) continue;
        
        int oldAffinity = currentAffinity;
        page->updateObjectAffinity(obj->id, currentAffinity - 1);
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "  obj" << obj->id << "affinity:" << oldAffinity 
                 << "->" << obj->getLayerAffinity();
        #endif
        // Phase O3.5.3: Push undo entry for affinity change
        pushObjectAffinityUndo(obj, oldAffinity);
        
        if (m_document->isEdgeless()) {
            m_document->markTileDirty(tileCoord);
        } else {
            m_document->markPageDirty(m_currentPageIndex);
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::sendSelectedToBackground()
{
    if (!m_document || m_selectedObjects.isEmpty()) return;
    
    const int backgroundAffinity = -1;
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "sendSelectedToBackground: setting affinity to" << backgroundAffinity;
    #endif
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        int currentAffinity = obj->getLayerAffinity();
        if (currentAffinity == backgroundAffinity) {
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "  obj" << obj->id << "already at background";
            #endif
            continue;
        }
        
        Document::TileCoord tileCoord = {0, 0};
        Page* page = findPageContainingObject(obj, &tileCoord);
        if (!page) continue;
        
        int oldAffinity = currentAffinity;
        page->updateObjectAffinity(obj->id, backgroundAffinity);
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "  obj" << obj->id << "affinity:" << oldAffinity 
                 << "->" << backgroundAffinity;
        #endif
        // Phase O3.5.3: Push undo entry for affinity change
        pushObjectAffinityUndo(obj, oldAffinity);
        
        if (m_document->isEdgeless()) {
            m_document->markTileDirty(tileCoord);
        } else {
            m_document->markPageDirty(m_currentPageIndex);
        }
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::renderObjectSelection(QPainter& painter)
{
    if (!m_document) return;
    
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // BF.4: Helper to find the tile containing an object (edgeless mode).
    // Phase O4.1.2: Use cached tile coord during drag to avoid expensive search!
    auto findTileForObject = [&](InsertedObject* obj) -> Document::TileCoord {
        if (!obj) return {0, 0};
        
        // During drag/resize with single selection, use cached tile coord
        if ((m_isDraggingObjects || m_isResizingObject) && 
            m_selectedObjects.size() == 1 && m_selectedObjects.first() == obj) {
            return m_dragObjectTileCoord;
        }
        
        // Fallback: search all tiles (only when not dragging)
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                return coord;
            }
        }
        return {0, 0};
    };
    
    // Helper to rotate a point around a center
    auto rotatePoint = [](const QPointF& pt, const QPointF& center, qreal angleDegrees) -> QPointF {
        if (qAbs(angleDegrees) < 0.01) return pt;  // No rotation
        qreal rad = qDegreesToRadians(angleDegrees);
        qreal cosA = qCos(rad);
        qreal sinA = qSin(rad);
        QPointF translated = pt - center;
        return QPointF(
            translated.x() * cosA - translated.y() * sinA + center.x(),
            translated.x() * sinA + translated.y() * cosA + center.y()
        );
    };
    
    // Helper to convert object bounds to viewport coordinates (with rotation!)
    // Uses same approach as objectHandleAtPoint: get viewport rect, then rotate in viewport space
    auto objectToViewportRect = [&](InsertedObject* obj) -> QPolygonF {
        if (!obj) return QPolygonF();
        
        // Get axis-aligned bounding box in viewport coordinates (same as objectBoundsInViewport)
        QRectF vpRect = objectBoundsInViewport(obj);
        if (vpRect.isEmpty()) return QPolygonF();
        
        QPointF vpCenter = vpRect.center();
        
        // Rotate corners in viewport space (consistent with objectHandleAtPoint)
        QPolygonF vpCorners;
        vpCorners << rotatePoint(vpRect.topLeft(), vpCenter, obj->rotation)
                  << rotatePoint(vpRect.topRight(), vpCenter, obj->rotation)
                  << rotatePoint(vpRect.bottomRight(), vpCenter, obj->rotation)
                  << rotatePoint(vpRect.bottomLeft(), vpCenter, obj->rotation);
        
        return vpCorners;
    };
    
    // ===== Draw hover highlight =====
    if (m_hoveredObject && !m_selectedObjects.contains(m_hoveredObject)) {
        QPolygonF hoverPoly = objectToViewportRect(m_hoveredObject);
        if (!hoverPoly.isEmpty()) {
            // Light blue semi-transparent highlight
            painter.setPen(QPen(QColor(0, 120, 215), 2));
            painter.setBrush(QColor(0, 120, 215, 30));
            painter.drawPolygon(hoverPoly);
        }
    }
    
    // ===== Draw selection boxes =====
    if (m_selectedObjects.isEmpty()) {
        painter.restore();
        return;
    }
    
    // Static dash offset for marching ants effect
    static int dashOffset = 0;
    
    QPen blackPen(Qt::black, 1, Qt::DashLine);
    blackPen.setDashOffset(dashOffset);
    QPen whitePen(Qt::white, 1, Qt::DashLine);
    whitePen.setDashOffset(dashOffset + 4);
    
    // Draw bounding box for each selected object
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        
        QPolygonF vpPoly = objectToViewportRect(obj);
        if (vpPoly.isEmpty()) continue;
        
        // Draw white then black dashed outline for visibility on any background
        painter.setBrush(Qt::NoBrush);
        painter.setPen(whitePen);
        painter.drawPolygon(vpPoly);
        painter.setPen(blackPen);
        painter.drawPolygon(vpPoly);
    }
    
    // ===== Draw handles for single selection =====
    if (m_selectedObjects.size() == 1) {
        InsertedObject* obj = m_selectedObjects.first();
        if (obj) {
            // Get axis-aligned bounding box in viewport coordinates
            // (consistent with objectHandleAtPoint hit testing)
            QRectF vpRect = objectBoundsInViewport(obj);
            if (vpRect.isEmpty()) {
                painter.restore();
                return;
            }
            
            QPointF vpCenter = vpRect.center();
            
            // Handle positions (8 scale handles + 1 rotation) - rotate in viewport space
            QVector<QPointF> handles;
            handles << rotatePoint(vpRect.topLeft(), vpCenter, obj->rotation);                            // 0: TopLeft
            handles << rotatePoint(QPointF(vpRect.center().x(), vpRect.top()), vpCenter, obj->rotation);  // 1: Top
            handles << rotatePoint(vpRect.topRight(), vpCenter, obj->rotation);                           // 2: TopRight
            handles << rotatePoint(QPointF(vpRect.left(), vpRect.center().y()), vpCenter, obj->rotation); // 3: Left
            handles << rotatePoint(QPointF(vpRect.right(), vpRect.center().y()), vpCenter, obj->rotation);// 4: Right
            handles << rotatePoint(vpRect.bottomLeft(), vpCenter, obj->rotation);                         // 5: BottomLeft
            handles << rotatePoint(QPointF(vpRect.center().x(), vpRect.bottom()), vpCenter, obj->rotation);// 6: Bottom
            handles << rotatePoint(vpRect.bottomRight(), vpCenter, obj->rotation);                        // 7: BottomRight
            
            // Rotation handle: offset from top center in the rotated direction
            QPointF topCenter = handles[1];
            qreal rad = qDegreesToRadians(obj->rotation);
            QPointF rotateOffset(ROTATE_HANDLE_OFFSET * qSin(rad), 
                                -ROTATE_HANDLE_OFFSET * qCos(rad));
            QPointF rotatePos = topCenter + rotateOffset;
            handles << rotatePos;  // 8: Rotate
            
            // Draw scale handles (squares) - rotated with the object
            QPen handlePen(Qt::black, 1);
            painter.setPen(handlePen);
            painter.setBrush(Qt::white);
            
            qreal halfSize = HANDLE_VISUAL_SIZE / 2.0;
            for (int i = 0; i < 8; ++i) {
                // Draw rotated rectangles for handles
                painter.save();
                painter.translate(handles[i]);
                painter.rotate(obj->rotation);
                painter.drawRect(QRectF(-halfSize, -halfSize, HANDLE_VISUAL_SIZE, HANDLE_VISUAL_SIZE));
                painter.restore();
            }
            
            // Draw rotation handle (circle) with connecting line
            painter.drawLine(topCenter, rotatePos);
            painter.drawEllipse(rotatePos, halfSize, halfSize);
        }
    }
    
    painter.restore();
}

void DocumentViewport::finalizeLassoSelection()
{
    if (!m_document || m_lassoPath.size() < 3) {
        // Need at least 3 points to form a valid selection polygon
        m_lassoPath.clear();
        // P1: Reset cache state
        m_lastRenderedLassoIdx = 0;
        m_lassoPathLength = 0;
        return;
    }
    
    // BUG FIX: Save sourcePageIndex BEFORE clearing selection
    // (it was set during handlePointerPress_Lasso)
    int savedSourcePageIndex = m_lassoSelection.sourcePageIndex;
    
    // Clear any existing selection (but we saved the page index)
    m_lassoSelection.clear();
    
    // Restore the source page index for paged mode
    m_lassoSelection.sourcePageIndex = savedSourcePageIndex;
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Check strokes across all visible tiles
        // Lasso path is in document coordinates
        // Tile strokes are in tile-local coordinates
        
        m_lassoSelection.sourceLayerIndex = m_edgelessActiveLayerIndex;
        
        // Get all loaded tiles
        auto tiles = m_document->allLoadedTileCoords();
        
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer || layer->isEmpty()) continue;
            
            // Calculate tile origin in document coordinates
            QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                               coord.second * Document::EDGELESS_TILE_SIZE);
            
            const auto& strokes = layer->strokes();
            for (int i = 0; i < strokes.size(); ++i) {
                const VectorStroke& stroke = strokes[i];
                
                // Transform stroke to document coordinates for hit test
                // We create a temporary copy with document coords
                VectorStroke docStroke = stroke;
                for (auto& pt : docStroke.points) {
                    pt.pos += tileOrigin;
                }
                docStroke.updateBoundingBox();
                
                if (strokeIntersectsLasso(docStroke, m_lassoPath)) {
                    // Store the document-coordinate version for rendering
                    m_lassoSelection.selectedStrokes.append(docStroke);
                    m_lassoSelection.originalIndices.append(i);
                    // For edgeless, we store the tile coord; for simplicity,
                    // just store the first tile's coord (cross-tile selection is complex)
                    if (m_lassoSelection.sourceTileCoord == std::pair<int,int>(0,0) && 
                        m_lassoSelection.selectedStrokes.size() == 1) {
                        m_lassoSelection.sourceTileCoord = coord;
                    }
                }
            }
        }
    } else {
        // ========== PAGED MODE ==========
        // Check strokes on the active layer of the current page
        // Lasso path is in page-local coordinates
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            m_lassoPath.clear();
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) {
            m_lassoPath.clear();
            return;
        }
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) {
            m_lassoPath.clear();
            return;
        }
        
        m_lassoSelection.sourceLayerIndex = page->activeLayerIndex;
        
        const auto& strokes = layer->strokes();
        for (int i = 0; i < strokes.size(); ++i) {
            const VectorStroke& stroke = strokes[i];
            
            if (strokeIntersectsLasso(stroke, m_lassoPath)) {
                m_lassoSelection.selectedStrokes.append(stroke);
                m_lassoSelection.originalIndices.append(i);
            }
        }
    }
    
    // Calculate bounding box and transform origin if we have a selection
    if (m_lassoSelection.isValid()) {
        m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
        m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
        
        // P3: Invalidate selection cache so it rebuilds with new strokes
        invalidateSelectionCache();
        
        // P5: Clear background snapshot (new selection = new excluded strokes)
        m_selectionBackgroundSnapshot = QPixmap();
        
        // Action Bar: Notify that lasso selection now exists
        emit lassoSelectionChanged(true);
    }
    
    // Clear the lasso path now that selection is complete
    m_lassoPath.clear();
    
    // P1: Reset cache state (cache is no longer needed after selection)
    m_lastRenderedLassoIdx = 0;
    m_lassoPathLength = 0;
    
    update();
}

bool DocumentViewport::strokeIntersectsLasso(const VectorStroke& stroke, 
                                              const QPolygonF& lasso) const
{
    // Check if any point of the stroke is inside the lasso polygon
    for (const auto& pt : stroke.points) {
        if (lasso.containsPoint(pt.pos, Qt::OddEvenFill)) {
            return true;
        }
    }
    return false;
}

QRectF DocumentViewport::calculateSelectionBoundingBox() const
{
    if (m_lassoSelection.selectedStrokes.isEmpty()) {
        return QRectF();
    }
    
    QRectF bounds = m_lassoSelection.selectedStrokes[0].boundingBox;
    for (int i = 1; i < m_lassoSelection.selectedStrokes.size(); ++i) {
        bounds = bounds.united(m_lassoSelection.selectedStrokes[i].boundingBox);
    }
    return bounds;
}

QTransform DocumentViewport::buildSelectionTransform() const
{
    // Build transform: rotate/scale around origin, then apply offset
    // 
    // CR-2B-6: Qt transforms are composed in REVERSE order (last added = first applied)
    // To achieve: 1) rotate/scale around origin, 2) then apply offset
    // We must add offset FIRST (so it's applied LAST to points)
    //
    // Application order (to point P):
    //   1. translate(-origin)     -> P - origin
    //   2. scale                  -> scale * (P - origin)
    //   3. rotate                 -> rotate * scale * (P - origin)
    //   4. translate(+origin)     -> origin + rotate * scale * (P - origin)
    //   5. translate(offset)      -> offset + origin + rotate * scale * (P - origin)
    //
    // Qt composition order (reverse):
    QTransform t;
    QPointF origin = m_lassoSelection.transformOrigin;
    
    t.translate(m_lassoSelection.offset.x(), m_lassoSelection.offset.y());  // Applied 5th (last)
    t.translate(origin.x(), origin.y());                                      // Applied 4th
    t.rotate(m_lassoSelection.rotation);                                      // Applied 3rd
    t.scale(m_lassoSelection.scaleX, m_lassoSelection.scaleY);                // Applied 2nd
    t.translate(-origin.x(), -origin.y());                                    // Applied 1st
    
    return t;
}

// ===== P3: Selection Stroke Caching =====

void DocumentViewport::invalidateSelectionCache()
{
    m_selectionCacheDirty = true;
}

void DocumentViewport::captureSelectionBackground()
{
    // P5: Capture viewport without selection for fast transform rendering
    // Uses same pattern as zoom/pan gesture caching
    
    // Temporarily disable selection rendering
    m_skipSelectionRendering = true;
    
    // Capture the viewport (this triggers a paint without selection)
    m_selectionBackgroundSnapshot = grab();
    m_backgroundSnapshotDpr = m_selectionBackgroundSnapshot.devicePixelRatio();
    
    // Re-enable selection rendering
    m_skipSelectionRendering = false;
}

// -----------------------------------------------------------------------------
// Phase O4.1: Object Drag/Resize Performance Optimization
// Same pattern as captureSelectionBackground() for lasso selection.
// -----------------------------------------------------------------------------
void DocumentViewport::captureObjectDragBackground()
{
    // Phase O4.1.3: Start throttle timer for drag updates
    m_dragUpdateTimer.start();
    
    // Temporarily disable selected object rendering
    m_skipSelectedObjectRendering = true;
    
    // Capture the viewport (this triggers a paint without selected objects)
    m_objectDragBackgroundSnapshot = grab();
    m_objectDragSnapshotDpr = m_objectDragBackgroundSnapshot.devicePixelRatio();
    
    // Re-enable selected object rendering
    m_skipSelectedObjectRendering = false;
    
    // Phase O4.1.2: Pre-render selected objects to cache at current zoom
    // This is the key optimization - no image scaling needed during drag!
    cacheSelectedObjectsForDrag();
}

void DocumentViewport::renderSelectedObjectsOnly(QPainter& painter)
{
    // Phase O4.1.2: Use pre-rendered cache if available (FAST!)
    // BF-Rotation: Fixed to use quadToQuad for proper rotated object rendering
    // (same approach as lasso selection's renderLassoSelection)
    
    if (!m_dragObjectRenderedCache.isNull() && m_selectedObjects.size() == 1) {
        InsertedObject* obj = m_selectedObjects.first();
        if (obj) {
            
            // Calculate current document position of the object
            // Use cached page/tile location (no searching!)
            QPointF docOrigin;
            if (m_document->isEdgeless()) {
                docOrigin = QPointF(m_dragObjectTileCoord.first * Document::EDGELESS_TILE_SIZE,
                                m_dragObjectTileCoord.second * Document::EDGELESS_TILE_SIZE);
            } else {
                docOrigin = pagePosition(m_dragObjectPageIndex);
            }
            
            // Object's document position (top-left of unrotated local bounds)
            QPointF docPos = docOrigin + obj->position;
            
            // Current object size (may have changed during resize)
            QSizeF currentSize = obj->size;
            
            // Object's center in document coordinates
            QPointF docCenter = docPos + QPointF(currentSize.width() / 2.0, 
                                                  currentSize.height() / 2.0);
            
            // Helper to rotate a point around center
            auto rotatePoint = [](const QPointF& pt, const QPointF& center, qreal angleDegrees) -> QPointF {
                if (qAbs(angleDegrees) < 0.01) return pt;
                qreal rad = qDegreesToRadians(angleDegrees);
                qreal cosA = qCos(rad);
                qreal sinA = qSin(rad);
                QPointF translated = pt - center;
                return QPointF(
                    translated.x() * cosA - translated.y() * sinA + center.x(),
                    translated.x() * sinA + translated.y() * cosA + center.y()
                );
            };
            
            // Calculate the 4 corners of the object in document coordinates
            // These are rotated around the object's center
            qreal rotation = obj->rotation;
            QPolygonF docCorners;
            docCorners << rotatePoint(docPos, docCenter, rotation)
                       << rotatePoint(docPos + QPointF(currentSize.width(), 0), docCenter, rotation)
                       << rotatePoint(docPos + QPointF(currentSize.width(), currentSize.height()), docCenter, rotation)
                       << rotatePoint(docPos + QPointF(0, currentSize.height()), docCenter, rotation);
            
            // Convert corners to viewport coordinates
            QPolygonF vpCorners;
            for (const QPointF& pt : docCorners) {
                vpCorners << documentToViewport(pt);
            }
            
            // Source rect: the cache was rendered at original size at zoom level
            // Cache size in logical pixels (accounting for DPR)
            qreal cacheDpr = m_dragObjectRenderedCache.devicePixelRatio();
            QSizeF cacheLogicalSize(m_dragObjectRenderedCache.width() / cacheDpr,
                                    m_dragObjectRenderedCache.height() / cacheDpr);
            
            // The source rectangle maps to the original object's corners
            // (cache was rendered at m_resizeOriginalSize * m_zoomLevel)
            QPolygonF sourceRect;
            sourceRect << QPointF(0, 0)
                       << QPointF(cacheLogicalSize.width(), 0)
                       << QPointF(cacheLogicalSize.width(), cacheLogicalSize.height())
                       << QPointF(0, cacheLogicalSize.height());
            
            // Use quadToQuad to create transform from cache to viewport polygon
            // This correctly handles rotation, scaling, and perspective
            QTransform blitTransform;
            if (QTransform::quadToQuad(sourceRect, vpCorners, blitTransform)) {
                painter.save();
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.setTransform(blitTransform, true);
                painter.drawPixmap(0, 0, m_dragObjectRenderedCache);
                painter.restore();
            } else {
                // Fallback: simple draw at viewport position (shouldn't normally happen)
                QPointF vpPos = documentToViewport(docPos);
                painter.drawPixmap(vpPos.toPoint(), m_dragObjectRenderedCache);
            }
        }
    } else {
        // Fallback: render objects directly (multi-selection or no cache)
        if (m_selectedObjects.isEmpty()) return;
        
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj || !obj->visible) continue;
            
            // BF.4 FIX: Only calculate the page/tile ORIGIN, not origin + obj->position.
            QPointF origin;
            
            if (m_document->isEdgeless()) {
                for (const auto& coord : m_document->allLoadedTileCoords()) {
                    Page* tile = m_document->getTile(coord.first, coord.second);
                    if (!tile) continue;
                    
                    for (const auto& tileObj : tile->objects) {
                        if (tileObj.get() == obj) {
                            origin = QPointF(coord.first * Document::EDGELESS_TILE_SIZE,
                                            coord.second * Document::EDGELESS_TILE_SIZE);
                            break;
                        }
                    }
                }
            } else {
                // PERF FIX: Only search loaded pages to avoid triggering lazy loading
                // Selected objects must be on already-loaded pages
                for (int i : m_document->loadedPageIndices()) {
                    Page* page = m_document->page(i);  // Already loaded, no disk I/O
                    if (!page) continue;
                    
                    for (const auto& pageObj : page->objects) {
                        if (pageObj.get() == obj) {
                            origin = pagePosition(i);
                            break;
                        }
                    }
                }
            }
            
            QPointF viewportOrigin = documentToViewport(origin);
            
            painter.save();
            painter.translate(viewportOrigin);
            painter.scale(m_zoomLevel, m_zoomLevel);
            obj->render(painter, 1.0);
            painter.restore();
        }
    }
    
    // Also render the selection handles
    renderObjectSelection(painter);
}

// -----------------------------------------------------------------------------
// Phase O4.1.2: Pre-render selected objects to cache at current zoom level
// BF-Rotation: Renders at IDENTITY rotation (like lasso selection cache).
// The rotation is applied during rendering via quadToQuad in renderSelectedObjectsOnly().
// -----------------------------------------------------------------------------
void DocumentViewport::cacheSelectedObjectsForDrag()
{
    
    if (m_selectedObjects.isEmpty() || !m_document) {
        m_dragObjectRenderedCache = QPixmap();
        return;
    }
    
    // For now, only cache single object selection (most common case)
    if (m_selectedObjects.size() != 1) {
        m_dragObjectRenderedCache = QPixmap();
        return;
    }
    
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj || !obj->visible) {
        m_dragObjectRenderedCache = QPixmap();
        return;
    }
    
    // Find and cache which page/tile contains this object
    m_dragObjectPageIndex = -1;
    m_dragObjectTileCoord = {0, 0};
    
    if (m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            for (const auto& tileObj : tile->objects) {
                if (tileObj.get() == obj) {
                    m_dragObjectTileCoord = coord;
                    break;
                }
            }
        }
    } else {
        // PERF FIX: Only search loaded pages to avoid triggering lazy loading
        // Selected objects must be on already-loaded pages
        for (int i : m_document->loadedPageIndices()) {
            Page* page = m_document->page(i);  // Already loaded, no disk I/O
            if (!page) continue;
            
            for (const auto& pageObj : page->objects) {
                if (pageObj.get() == obj) {
                    m_dragObjectPageIndex = i;
                    break;
                }
            }
        }
    }
    
    // Calculate the size of the rendered object at current zoom
    // FIX: Only create cache for the object SIZE, not position + size!
    qreal dpr = devicePixelRatioF();
    QSizeF objectSize = obj->size * m_zoomLevel;
    
    // Cache should only be the size of the object itself
    QSize cacheSize(qCeil(objectSize.width() * dpr) + 2,
                    qCeil(objectSize.height() * dpr) + 2);
    
    if (cacheSize.width() <= 0 || cacheSize.height() <= 0) {
        m_dragObjectRenderedCache = QPixmap();
        return;
    }
    
    // Create the cache pixmap
    m_dragObjectRenderedCache = QPixmap(cacheSize);
    m_dragObjectRenderedCache.setDevicePixelRatio(dpr);
    m_dragObjectRenderedCache.fill(Qt::transparent);
    
    // BF-Rotation: Render at IDENTITY rotation (rotation = 0)
    // This matches the lasso selection approach where cache is at identity
    // and the transform is applied during rendering via quadToQuad.
    qreal originalRotation = obj->rotation;
    obj->rotation = 0.0;  // Temporarily set to identity
    
    // Render the object to the cache
    // IMPORTANT: Translate by -position so object renders at (0,0) in cache
    // ImageObject::render() internally draws at (position.x * zoom, position.y * zoom)
    QPainter cachePainter(&m_dragObjectRenderedCache);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    cachePainter.scale(m_zoomLevel, m_zoomLevel);
    cachePainter.translate(-obj->position);  // Offset so object renders at (0,0)
    obj->render(cachePainter, 1.0);
    cachePainter.end();
    
    // Restore original rotation
    obj->rotation = originalRotation;
}

void DocumentViewport::rebuildSelectionCache()
{
    if (!m_lassoSelection.isValid()) {
        m_selectionStrokeCache = QPixmap();
        m_selectionCacheDirty = true;
        m_selectionHasTransparency = false;
        return;
    }
    
    qreal dpr = devicePixelRatioF();
    QRectF bounds = m_lassoSelection.boundingBox;
    
    // Add padding for stroke thickness (strokes may extend beyond bounding box)
    constexpr qreal STROKE_PADDING = 20.0;
    bounds.adjust(-STROKE_PADDING, -STROKE_PADDING, STROKE_PADDING, STROKE_PADDING);
    
    // Calculate cache size at current zoom with high DPI support
    int cacheW = qCeil(bounds.width() * m_zoomLevel * dpr);
    int cacheH = qCeil(bounds.height() * m_zoomLevel * dpr);
    
    // Safety check: prevent excessively large caches
    constexpr int MAX_CACHE_DIM = 4096;
    if (cacheW > MAX_CACHE_DIM || cacheH > MAX_CACHE_DIM || cacheW <= 0 || cacheH <= 0) {
        // Fall back to non-cached rendering for very large selections
        m_selectionStrokeCache = QPixmap();
        m_selectionCacheDirty = true;
        m_selectionHasTransparency = false;
        return;
    }
    
    // P4: Detect semi-transparent strokes
    // We need to handle semi-transparent strokes specially to prevent alpha compounding
    // But we must preserve the relative opacity between different strokes
    m_selectionHasTransparency = false;
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        if (stroke.color.alpha() < 255) {
            m_selectionHasTransparency = true;
            break;
        }
    }
    
    // Create cache pixmap
    m_selectionStrokeCache = QPixmap(cacheW, cacheH);
    m_selectionStrokeCache.setDevicePixelRatio(dpr);
    m_selectionStrokeCache.fill(Qt::transparent);
    
    // Render strokes to cache at identity transform
    QPainter cachePainter(&m_selectionStrokeCache);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    
    // Scale to current zoom and offset to cache origin
    cachePainter.scale(m_zoomLevel, m_zoomLevel);
    cachePainter.translate(-bounds.topLeft());
    
    // P4: Render each stroke at identity (no selection transform)
    // For semi-transparent strokes, render to a temp buffer with full opacity,
    // then composite with the stroke's alpha. Opaque strokes render directly.
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        int strokeAlpha = stroke.color.alpha();
        
        if (strokeAlpha < 255) {
            // Semi-transparent stroke: render opaque to temp buffer, then composite
            // This prevents alpha compounding within the stroke's self-intersections
            QRectF strokeBounds = stroke.boundingBox;
            strokeBounds.adjust(-stroke.baseThickness, -stroke.baseThickness,
                               stroke.baseThickness, stroke.baseThickness);
            
            // Create temp buffer for this stroke
            int tempW = qCeil(strokeBounds.width() * m_zoomLevel * dpr) + 4;
            int tempH = qCeil(strokeBounds.height() * m_zoomLevel * dpr) + 4;
            
            // Safety check for temp buffer size
            if (tempW > 0 && tempH > 0 && tempW <= 4096 && tempH <= 4096) {
                QPixmap tempBuffer(tempW, tempH);
                tempBuffer.setDevicePixelRatio(dpr);
                tempBuffer.fill(Qt::transparent);
                
                QPainter tempPainter(&tempBuffer);
                tempPainter.setRenderHint(QPainter::Antialiasing, true);
                tempPainter.scale(m_zoomLevel, m_zoomLevel);
                tempPainter.translate(-strokeBounds.topLeft());
                
                // Render stroke with full opacity
                VectorStroke opaqueStroke = stroke;
                opaqueStroke.color.setAlpha(255);
                VectorLayer::renderStroke(tempPainter, opaqueStroke);
                tempPainter.end();
                
                // Composite temp buffer to cache with stroke's alpha
                cachePainter.save();
                cachePainter.resetTransform();  // Work in cache pixel coords
                cachePainter.setOpacity(strokeAlpha / 255.0);
                
                // Calculate where to blit in cache coordinates
                QPointF cachePos = (strokeBounds.topLeft() - bounds.topLeft()) * m_zoomLevel;
                cachePainter.drawPixmap(cachePos, tempBuffer);
                
                cachePainter.setOpacity(1.0);
                cachePainter.restore();
            } else {
                // Fallback: render directly (may have alpha compounding)
                VectorLayer::renderStroke(cachePainter, stroke);
            }
        } else {
            // Opaque stroke: render directly
            VectorLayer::renderStroke(cachePainter, stroke);
        }
    }
    
    cachePainter.end();
    
    // Store cache metadata
    m_selectionCacheBounds = bounds;
    m_selectionCacheZoom = m_zoomLevel;
    m_selectionCacheDirty = false;
}

void DocumentViewport::renderLassoSelection(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // P3: Check if cache needs rebuild (dirty or zoom changed)
    bool useCache = true;
    if (m_selectionCacheDirty || !qFuzzyCompare(m_selectionCacheZoom, m_zoomLevel)) {
        rebuildSelectionCache();
    }
    
    // If cache is still invalid (very large selection), fall back to direct rendering
    if (m_selectionStrokeCache.isNull()) {
        useCache = false;
    }
    
    if (useCache) {
        // P3: Render using cached pixmap with transform applied
        QTransform selectionTransform = buildSelectionTransform();
        
        // Calculate page origin for paged mode
        QPointF pageOrigin;
        if (!m_document->isEdgeless()) {
            pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        }
        
        // The cache was rendered at identity with cache bounds as origin.
        // We need to:
        // 1. Position at cache bounds origin (in document coords)
        // 2. Apply selection transform (rotate/scale around selection center, then offset)
        // 3. Convert to viewport coordinates
        
        // Transform the cache bounds corners through the selection transform
        QRectF cacheBounds = m_selectionCacheBounds;
        QPolygonF corners;
        corners << cacheBounds.topLeft() << cacheBounds.topRight()
                << cacheBounds.bottomRight() << cacheBounds.bottomLeft();
        
        // Apply selection transform to corners
        QPolygonF transformedCorners = selectionTransform.map(corners);
        
        // Convert to viewport coordinates
        QPolygonF vpCorners;
        for (const QPointF& pt : transformedCorners) {
            if (m_document->isEdgeless()) {
                vpCorners << documentToViewport(pt);
            } else {
                vpCorners << documentToViewport(pt + pageOrigin);
            }
        }
        
        // Use QTransform::quadToQuad to map the cache rectangle to the transformed polygon
        QPolygonF sourceRect;
        sourceRect << QPointF(0, 0)
                   << QPointF(cacheBounds.width() * m_zoomLevel, 0)
                   << QPointF(cacheBounds.width() * m_zoomLevel, cacheBounds.height() * m_zoomLevel)
                   << QPointF(0, cacheBounds.height() * m_zoomLevel);
        
        QTransform blitTransform;
        if (QTransform::quadToQuad(sourceRect, vpCorners, blitTransform)) {
            painter.save();
            painter.setTransform(blitTransform, true);
            // P4: Alpha is now baked into the cache per-stroke, no uniform alpha needed
            painter.drawPixmap(0, 0, m_selectionStrokeCache);
            painter.restore();
        } else {
            // Fallback: simple positioning (no rotation/scale - shouldn't normally happen)
            QPointF vpOrigin = m_document->isEdgeless() 
                ? documentToViewport(selectionTransform.map(cacheBounds.topLeft()))
                : documentToViewport(selectionTransform.map(cacheBounds.topLeft()) + pageOrigin);
            // P4: Alpha is now baked into the cache per-stroke, no uniform alpha needed
            painter.drawPixmap(vpOrigin, m_selectionStrokeCache);
        }
    } else {
        // Fallback: Direct rendering for very large selections
        QTransform transform = buildSelectionTransform();
        
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke;
            transformedStroke.id = stroke.id;
            transformedStroke.color = stroke.color;
            transformedStroke.baseThickness = stroke.baseThickness;
            
            for (const StrokePoint& pt : stroke.points) {
                StrokePoint tPt;
                tPt.pos = transform.map(pt.pos);
                tPt.pressure = pt.pressure;
                transformedStroke.points.append(tPt);
            }
            transformedStroke.updateBoundingBox();
            
            painter.save();
            
            if (m_document->isEdgeless()) {
                painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
                painter.scale(m_zoomLevel, m_zoomLevel);
            } else {
                QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
                painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
                painter.scale(m_zoomLevel, m_zoomLevel);
                painter.translate(pageOrigin);
            }
            
            VectorLayer::renderStroke(painter, transformedStroke);
            painter.restore();
        }
    }
    
    // Draw the bounding box
    drawSelectionBoundingBox(painter);
    
    // Draw transform handles
    drawSelectionHandles(painter);
    
    painter.restore();
}

void DocumentViewport::drawSelectionBoundingBox(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    QRectF box = m_lassoSelection.boundingBox;
    QTransform transform = buildSelectionTransform();
    
    // Transform the four corners
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport coordinates
    QPolygonF vpCorners;
    if (m_document->isEdgeless()) {
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt);
        }
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    // Draw dashed bounding box (marching ants style)
    // Use static offset that increments for animation effect
    static int dashOffset = 0;
    
    QPen blackPen(Qt::black, 1, Qt::DashLine);
    blackPen.setDashOffset(dashOffset);
    QPen whitePen(Qt::white, 1, Qt::DashLine);
    whitePen.setDashOffset(dashOffset + 4);  // Offset for contrast
    
    painter.setPen(whitePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(vpCorners);
    
    painter.setPen(blackPen);
    painter.drawPolygon(vpCorners);
    
    // Note: For animated marching ants, call update() from a timer
    // and increment dashOffset. For now, static dashed line.
    // dashOffset = (dashOffset + 1) % 16;
}

QVector<QPointF> DocumentViewport::getHandlePositions() const
{
    // Returns 9 positions: 8 scale handles + 1 rotation handle
    // Positions are in document/page coordinates (before transform)
    QRectF box = m_lassoSelection.boundingBox;
    
    QVector<QPointF> positions;
    positions.reserve(9);
    
    // Scale handles: TL, T, TR, L, R, BL, B, BR (8 handles)
    positions << box.topLeft();                                    // 0: TopLeft
    positions << QPointF(box.center().x(), box.top());             // 1: Top
    positions << box.topRight();                                   // 2: TopRight
    positions << QPointF(box.left(), box.center().y());            // 3: Left
    positions << QPointF(box.right(), box.center().y());           // 4: Right
    positions << box.bottomLeft();                                 // 5: BottomLeft
    positions << QPointF(box.center().x(), box.bottom());          // 6: Bottom
    positions << box.bottomRight();                                // 7: BottomRight
    
    // Rotation handle: above top center
    // Use a fixed offset in document coords (will scale with zoom)
    qreal rotateOffset = ROTATE_HANDLE_OFFSET / m_zoomLevel;
    positions << QPointF(box.center().x(), box.top() - rotateOffset);  // 8: Rotate
    
    return positions;
}

void DocumentViewport::drawSelectionHandles(QPainter& painter)
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    QTransform transform = buildSelectionTransform();
    QVector<QPointF> handlePositions = getHandlePositions();
    
    // Determine page origin for coordinate conversion
    QPointF pageOrigin;
    if (!m_document->isEdgeless()) {
        pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
    }
    
    // Convert handle positions to viewport coordinates
    auto toViewport = [&](const QPointF& docPt) -> QPointF {
        QPointF transformed = transform.map(docPt);
        if (m_document->isEdgeless()) {
            return documentToViewport(transformed);
        } else {
            return documentToViewport(transformed + pageOrigin);
        }
    };
    
    // Draw style for handles
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen handlePen(Qt::black, 1);
    painter.setPen(handlePen);
    painter.setBrush(Qt::white);
    
    // Draw the 8 scale handles (squares)
    qreal halfSize = HANDLE_VISUAL_SIZE / 2.0;
    for (int i = 0; i < 8; ++i) {
        QPointF vpPos = toViewport(handlePositions[i]);
        QRectF handleRect(vpPos.x() - halfSize, vpPos.y() - halfSize,
                          HANDLE_VISUAL_SIZE, HANDLE_VISUAL_SIZE);
        painter.drawRect(handleRect);
    }
    
    // Draw rotation handle (circle) and connecting line
    QPointF topCenterVp = toViewport(handlePositions[1]);  // Top center
    QPointF rotateVp = toViewport(handlePositions[8]);     // Rotation handle
    
    // Line from top center to rotation handle
    painter.setPen(QPen(Qt::black, 1));
    painter.drawLine(topCenterVp, rotateVp);
    
    // Rotation handle circle
    painter.setBrush(Qt::white);
    painter.drawEllipse(rotateVp, halfSize, halfSize);
    
    // Draw a small rotation indicator inside the circle
    painter.setPen(QPen(Qt::black, 1));
    QPointF arrowStart(rotateVp.x() - halfSize * 0.4, rotateVp.y());
    QPointF arrowEnd(rotateVp.x() + halfSize * 0.4, rotateVp.y() - halfSize * 0.3);
    painter.drawLine(arrowStart, rotateVp);
    painter.drawLine(rotateVp, arrowEnd);
}

DocumentViewport::HandleHit DocumentViewport::hitTestSelectionHandles(const QPointF& viewportPos) const
{
    if (!m_lassoSelection.isValid()) {
        return HandleHit::None;
    }
    
    QTransform transform = buildSelectionTransform();
    QVector<QPointF> handlePositions = getHandlePositions();
    
    // Determine page origin for coordinate conversion
    QPointF pageOrigin;
    if (!m_document->isEdgeless()) {
        pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
    }
    
    // Convert handle positions to viewport coordinates
    auto toViewport = [&](const QPointF& docPt) -> QPointF {
        QPointF transformed = transform.map(docPt);
        if (m_document->isEdgeless()) {
            return documentToViewport(transformed);
        } else {
            return documentToViewport(transformed + pageOrigin);
        }
    };
    
    // Touch-friendly hit area (larger than visual)
    qreal hitRadius = HANDLE_HIT_SIZE / 2.0;
    
    // Map handle indices to HandleHit enum
    // Order matches getHandlePositions(): TL(0), T(1), TR(2), L(3), R(4), BL(5), B(6), BR(7), Rotate(8)
    static const HandleHit handleTypes[] = {
        HandleHit::TopLeft, HandleHit::Top, HandleHit::TopRight,
        HandleHit::Left, HandleHit::Right,
        HandleHit::BottomLeft, HandleHit::Bottom, HandleHit::BottomRight,
        HandleHit::Rotate
    };
    
    // Test rotation handle first (highest priority, on top visually)
    {
        QPointF vpPos = toViewport(handlePositions[8]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return HandleHit::Rotate;
        }
    }
    
    // Test scale handles in reverse order (corners have priority over edges)
    // Test corners: TL, TR, BL, BR (indices 0, 2, 5, 7)
    int cornerIndices[] = {0, 2, 5, 7};
    for (int idx : cornerIndices) {
        QPointF vpPos = toViewport(handlePositions[idx]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return handleTypes[idx];
        }
    }
    
    // Test edge handles: T, L, R, B (indices 1, 3, 4, 6)
    int edgeIndices[] = {1, 3, 4, 6};
    for (int idx : edgeIndices) {
        QPointF vpPos = toViewport(handlePositions[idx]);
        qreal dx = viewportPos.x() - vpPos.x();
        qreal dy = viewportPos.y() - vpPos.y();
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            return handleTypes[idx];
        }
    }
    
    // Test if inside bounding box (for move)
    // Transform the bounding box corners and check if point is inside
    QRectF box = m_lassoSelection.boundingBox;
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport
    QPolygonF vpCorners;
    for (const QPointF& pt : corners) {
        if (m_document->isEdgeless()) {
            vpCorners << documentToViewport(pt);
        } else {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    if (vpCorners.containsPoint(viewportPos, Qt::OddEvenFill)) {
        return HandleHit::Inside;
    }
    
    return HandleHit::None;
}

void DocumentViewport::startSelectionTransform(HandleHit handle, const QPointF& viewportPos)
{
    if (!m_lassoSelection.isValid() || handle == HandleHit::None) {
        return;
    }
    
    m_isTransformingSelection = true;
    m_transformHandle = handle;
    m_transformStartPos = viewportPos;
    
    // P5: Capture background snapshot for fast transform rendering
    // Only capture if we don't already have a valid snapshot
    // (consecutive transforms reuse the existing snapshot)
    if (m_selectionBackgroundSnapshot.isNull()) {
        captureSelectionBackground();
    }
    
    // Store document position for coordinate-independent calculations
    if (m_document->isEdgeless()) {
        m_transformStartDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        m_transformStartDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    // CR-2B-8 + CR-2B-9: Before starting a new transform, "bake in" only the OFFSET.
    // 
    // We must NOT bake in rotation or scale because:
    // - Rotation: Baking creates an axis-aligned bounding box, losing the tilt.
    //   Subsequent operations would use X/Y axes instead of the rotated axes.
    // - Scale: Similar issue - we'd lose the local coordinate orientation.
    //
    // ONLY offset is safe to bake in because it's pure translation.
    // Rotation and scale remain as cumulative values.
    if (!m_lassoSelection.offset.isNull()) {
        // Translate bounding box and origin by the offset
        m_lassoSelection.boundingBox.translate(m_lassoSelection.offset);
        m_lassoSelection.transformOrigin += m_lassoSelection.offset;
        
        // Translate stored strokes to match
        for (VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            for (StrokePoint& pt : stroke.points) {
                pt.pos += m_lassoSelection.offset;
            }
            stroke.updateBoundingBox();
        }
        
        // Reset offset only (rotation and scale remain)
        m_lassoSelection.offset = QPointF(0, 0);
        
        // P3: Strokes changed, invalidate cache so it rebuilds with new positions
        invalidateSelectionCache();
    }
    
    // Store current transform state so we can compute deltas
    m_transformStartBounds = m_lassoSelection.boundingBox;
    m_transformStartRotation = m_lassoSelection.rotation;
    m_transformStartScaleX = m_lassoSelection.scaleX;
    m_transformStartScaleY = m_lassoSelection.scaleY;
    m_transformStartOffset = m_lassoSelection.offset;
}

void DocumentViewport::updateSelectionTransform(const QPointF& viewportPos)
{
    if (!m_isTransformingSelection || !m_lassoSelection.isValid()) {
        return;
    }
    
    // Get current document position
    QPointF currentDocPos;
    if (m_document->isEdgeless()) {
        currentDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        currentDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    switch (m_transformHandle) {
        case HandleHit::Inside: {
            // Move: offset by delta in document coordinates
            QPointF delta = currentDocPos - m_transformStartDocPos;
            m_lassoSelection.offset = m_transformStartOffset + delta;
            break;
        }
        
        case HandleHit::Rotate: {
            // Rotate around transform origin
            // Calculate angle from origin to start and current positions
            QPointF origin = m_lassoSelection.transformOrigin;
            
            // Use viewport coordinates for angle calculation (more intuitive for user)
            QPointF originVp;
            if (m_document->isEdgeless()) {
                originVp = documentToViewport(origin);
            } else {
                QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
                originVp = documentToViewport(origin + pageOrigin);
            }
            
            qreal startAngle = std::atan2(m_transformStartPos.y() - originVp.y(),
                                          m_transformStartPos.x() - originVp.x());
            qreal currentAngle = std::atan2(viewportPos.y() - originVp.y(),
                                            viewportPos.x() - originVp.x());
            
            qreal deltaAngle = (currentAngle - startAngle) * 180.0 / M_PI;
            m_lassoSelection.rotation = m_transformStartRotation + deltaAngle;
            break;
        }
        
        case HandleHit::TopLeft:
        case HandleHit::Top:
        case HandleHit::TopRight:
        case HandleHit::Left:
        case HandleHit::Right:
        case HandleHit::BottomLeft:
        case HandleHit::Bottom:
        case HandleHit::BottomRight:
            // Scale handles
            updateScaleFromHandle(m_transformHandle, viewportPos);
            break;
            
        case HandleHit::None:
            break;
    }
    
    // P2: Dirty region update - only repaint selection area + handles
    // Calculate visual bounds in viewport coordinates
    QRectF visualBoundsVp = getSelectionVisualBounds();
    if (!visualBoundsVp.isEmpty()) {
        // Expand for handles and rotation handle offset
        visualBoundsVp.adjust(
            -HANDLE_HIT_SIZE, 
            -ROTATE_HANDLE_OFFSET - HANDLE_HIT_SIZE,  // Rotation handle above
            HANDLE_HIT_SIZE, 
            HANDLE_HIT_SIZE
        );
        update(visualBoundsVp.toRect());
    } else {
        update();  // Fallback to full update
    }
}

QRectF DocumentViewport::getSelectionVisualBounds() const
{
    // Calculate the visual bounding box of the selection in viewport coordinates
    if (!m_lassoSelection.isValid()) {
        return QRectF();
    }
    
    QRectF box = m_lassoSelection.boundingBox;
    QTransform transform = buildSelectionTransform();
    
    // Transform the four corners
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport coordinates and get bounding rect
    QPolygonF vpCorners;
    if (m_document && m_document->isEdgeless()) {
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt);
        }
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        for (const QPointF& pt : corners) {
            vpCorners << documentToViewport(pt + pageOrigin);
        }
    }
    
    return vpCorners.boundingRect();
}

void DocumentViewport::updateScaleFromHandle(HandleHit handle, const QPointF& viewportPos)
{
    // Get current document position
    QPointF currentDocPos;
    if (m_document->isEdgeless()) {
        currentDocPos = viewportToDocument(viewportPos);
    } else {
        QPointF pageOrigin = pagePosition(m_lassoSelection.sourcePageIndex);
        currentDocPos = viewportToDocument(viewportPos) - pageOrigin;
    }
    
    QPointF origin = m_lassoSelection.transformOrigin;
    QRectF startBounds = m_transformStartBounds;
    
    // Calculate original distances from center to edges
    qreal origLeft = startBounds.left() - origin.x();
    qreal origRight = startBounds.right() - origin.x();
    qreal origTop = startBounds.top() - origin.y();
    qreal origBottom = startBounds.bottom() - origin.y();
    
    // Calculate new distance from origin to current position
    qreal dx = currentDocPos.x() - origin.x();
    qreal dy = currentDocPos.y() - origin.y();
    
    // Apply rotation to get the position relative to the unrotated bounds
    qreal rotRad = m_transformStartRotation * M_PI / 180.0;
    qreal cosR = std::cos(-rotRad);
    qreal sinR = std::sin(-rotRad);
    qreal localX = dx * cosR - dy * sinR;
    qreal localY = dx * sinR + dy * cosR;
    
    // Calculate scale factors based on which handle is being dragged
    qreal newScaleX = m_transformStartScaleX;
    qreal newScaleY = m_transformStartScaleY;
    
    switch (handle) {
        case HandleHit::TopLeft:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::Top:
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::TopRight:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            if (std::abs(origTop) > 0.001) newScaleY = localY / origTop;
            break;
            
        case HandleHit::Left:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            break;
            
        case HandleHit::Right:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            break;
            
        case HandleHit::BottomLeft:
            if (std::abs(origLeft) > 0.001) newScaleX = localX / origLeft;
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        case HandleHit::Bottom:
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        case HandleHit::BottomRight:
            if (std::abs(origRight) > 0.001) newScaleX = localX / origRight;
            if (std::abs(origBottom) > 0.001) newScaleY = localY / origBottom;
            break;
            
        default:
            break;
    }
    
    // Clamp scale to reasonable values (prevent inversion and extreme scaling)
    // Use 0.1 minimum to allow shrinking but prevent disappearance
    newScaleX = qBound(0.1, newScaleX, 10.0);
    newScaleY = qBound(0.1, newScaleY, 10.0);
    
    m_lassoSelection.scaleX = newScaleX;
    m_lassoSelection.scaleY = newScaleY;
}

void DocumentViewport::finalizeSelectionTransform()
{
    m_isTransformingSelection = false;
    m_transformHandle = HandleHit::None;
    // Transform is applied visually; actual stroke modification happens on:
    // - Click elsewhere (apply and clear)
    // - Paste (apply to new location)
    // - Delete (remove originals)
    update();
}

void DocumentViewport::transformStrokePoints(VectorStroke& stroke, const QTransform& transform)
{
    for (StrokePoint& pt : stroke.points) {
        pt.pos = transform.map(pt.pos);
    }
    stroke.updateBoundingBox();
}

void DocumentViewport::applySelectionTransform()
{
    if (!m_lassoSelection.isValid() || !m_document) {
        return;
    }
    
    QTransform transform = buildSelectionTransform();
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // More complex: strokes may span multiple tiles after transform
        // Strategy: remove all original strokes, then add transformed strokes
        // using the same tile-splitting logic as regular stroke creation
        
        EdgelessUndoAction undoAction;
        undoAction.type = PageUndoAction::TransformSelection;
        undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;
        
        // First, collect and remove original strokes from their source tiles
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;
            
            // Find and remove strokes that match our selection by ID
            QVector<VectorStroke>& layerStrokes = layer->strokes();
            for (int i = layerStrokes.size() - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        // Store for undo (tile-local coords)
                        undoAction.removedSegments.append({coord, layerStrokes[i]});
                        layerStrokes.removeAt(i);
                        layer->invalidateStrokeCache();
                        m_document->markTileDirty(coord);
                        break;
                    }
                }
            }
        }
        
        // Now add transformed strokes back using the same tile-splitting logic
        // as finishStrokeEdgeless() to handle strokes crossing tile boundaries
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            // Note: addStrokeToEdgelessTiles() generates new IDs for each segment
            
            // Use the shared helper that properly splits strokes at tile boundaries
            // It returns the added stroke segments for undo tracking
            auto addedSegments = addStrokeToEdgelessTiles(transformedStroke, m_lassoSelection.sourceLayerIndex);
            for (const auto& seg : addedSegments) {
                undoAction.addedSegments.append({seg.first, seg.second});
            }
        }
        
        // Push to edgeless undo stack
        if (!undoAction.removedSegments.isEmpty() || !undoAction.addedSegments.isEmpty()) {
            pushEdgelessUndoAction(undoAction);
        }
        
    } else {
        // ========== PAGED MODE ==========
        // Simpler: all strokes are on the same page/layer
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;
        
        // Prepare undo action
        PageUndoAction undoAction;
        undoAction.type = PageUndoAction::TransformSelection;
        undoAction.pageIndex = m_lassoSelection.sourcePageIndex;
        undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;
        
        // Remove original strokes by ID (and track for undo)
        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = layerStrokes.size() - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    undoAction.removedStrokes.append(layerStrokes[i]);
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        
        // Add transformed strokes with new IDs
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            layer->addStroke(transformedStroke);
            undoAction.addedStrokes.append(transformedStroke);
        }
        
        layer->invalidateStrokeCache();
        
        // Mark page dirty for lazy save (BUG FIX: was missing)
        m_document->markPageDirty(m_lassoSelection.sourcePageIndex);
        
        // Push to paged undo stack
        pushUndoAction(m_lassoSelection.sourcePageIndex, undoAction);
    }
    
    clearLassoSelection();
    emit documentModified();
}

void DocumentViewport::cancelSelectionTransform()
{
    // Simply clear the selection without applying the transform
    // The original strokes remain untouched
    clearLassoSelection();
}

bool DocumentViewport::handleEscapeKey()
{
    // Handle Escape key for cancelling selections/states.
    // Returns true if something was cancelled, false if nothing to cancel.
    // Called by MainWindow to determine whether to toggle to launcher.
    
    // Priority 1: Cancel lasso selection or drawing (Lasso tool only)
    // Note: Lasso selection is cleared when switching away from Lasso tool,
    // so this check only needs to handle the Lasso tool.
    if (m_currentTool == ToolType::Lasso) {
        if (m_lassoSelection.isValid() || m_isDrawingLasso) {
            cancelSelectionTransform();
            return true;
        }
    }
    
    // Priority 2: Deselect objects or clear object clipboard (ObjectSelect tool only)
    if (m_currentTool == ToolType::ObjectSelect) {
        if (hasSelectedObjects() || !m_objectClipboard.isEmpty()) {
            cancelObjectSelectAction();
            return true;
        }
    }
    
    // Priority 3: Cancel text selection (Highlighter tool only)
    // Note: Text selection is cleared when switching away from Highlighter tool.
    if (m_currentTool == ToolType::Highlighter) {
        if (m_textSelection.isValid() || m_textSelection.isSelecting) {
            bool hadValidSelection = m_textSelection.isValid();
            m_textSelection.clear();
            if (hadValidSelection) {
                emit textSelectionChanged(false);
            }
            update();
            return true;
        }
    }
    
    // Nothing to cancel
    return false;
}

// ===== Context-Dependent Shortcut Handlers =====
// Called by MainWindow's QShortcut system

void DocumentViewport::handleCopyAction()
{
    // Copy behavior depends on current tool and selection state
    switch (m_currentTool) {
        case ToolType::Lasso:
            if (m_lassoSelection.isValid()) {
                copySelection();
            }
            break;
            
        case ToolType::ObjectSelect:
            if (hasSelectedObjects()) {
                copySelectedObjects();
            }
            break;
            
        case ToolType::Highlighter:
            if (m_textSelection.isValid()) {
                copySelectedTextToClipboard();
            }
            break;
            
        default:
            // No copy action for other tools
            break;
    }
}

void DocumentViewport::handleCutAction()
{
    // Cut currently only works for Lasso tool
    if (m_currentTool == ToolType::Lasso && m_lassoSelection.isValid()) {
        cutSelection();
    }
}

void DocumentViewport::handlePasteAction()
{
    // Paste behavior depends on current tool
    switch (m_currentTool) {
        case ToolType::Lasso:
            if (m_clipboard.hasContent) {
                pasteSelection();
            }
            break;
            
        case ToolType::ObjectSelect:
            pasteForObjectSelect();
            break;
            
        default:
            // No paste action for other tools
            break;
    }
}

void DocumentViewport::handleDeleteAction()
{
    // Delete behavior depends on current tool and selection state
    switch (m_currentTool) {
        case ToolType::Lasso:
            if (m_lassoSelection.isValid()) {
                deleteSelection();
            }
            break;
            
        case ToolType::ObjectSelect:
            if (hasSelectedObjects()) {
                deleteSelectedObjects();
            }
            break;
            
        case ToolType::Highlighter:
            // For highlighter, Escape cancels selection, Delete doesn't do anything special
            // (we can't delete PDF text)
            break;
            
        default:
            break;
    }
}

// ===== Clipboard Operations (Task 2.10.7) =====

void DocumentViewport::copySelection()
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    m_clipboard.clear();
    
    // Get current transform and apply it to strokes before copying
    QTransform transform = buildSelectionTransform();
    
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        VectorStroke transformedStroke = stroke;
        transformStrokePoints(transformedStroke, transform);
        // Give new ID to avoid conflicts when pasting
        transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_clipboard.strokes.append(transformedStroke);
    }
    
    m_clipboard.hasContent = true;
    
    // Action Bar: Notify that stroke clipboard now has content
    emit strokeClipboardChanged(true);
}

void DocumentViewport::cutSelection()
{
    if (!m_lassoSelection.isValid()) {
        return;
    }
    
    // Copy first
    copySelection();
    
    // Then delete
    deleteSelection();
}

void DocumentViewport::pasteSelection()
{
    if (!m_clipboard.hasContent || m_clipboard.strokes.isEmpty() || !m_document) {
        return;
    }
    
    // Calculate clipboard bounding box
    QRectF clipboardBounds;
    for (const VectorStroke& stroke : m_clipboard.strokes) {
        if (clipboardBounds.isNull()) {
            clipboardBounds = stroke.boundingBox;
        } else {
            clipboardBounds = clipboardBounds.united(stroke.boundingBox);
        }
    }
    
    // Calculate paste offset: center clipboard content at viewport center
    QPointF viewCenter(width() / 2.0, height() / 2.0);
    QPointF docCenter = viewportToDocument(viewCenter);
    QPointF clipboardCenter = clipboardBounds.center();
    QPointF offset = docCenter - clipboardCenter;
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Add strokes to appropriate tiles, splitting at tile boundaries
        // Uses the same logic as finishStrokeEdgeless() for consistency
        
        EdgelessUndoAction undoAction;
        undoAction.type = PageUndoAction::AddStroke;
        undoAction.layerIndex = m_edgelessActiveLayerIndex;
        
        for (const VectorStroke& stroke : m_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            
            // Apply paste offset (stroke is now in document coordinates)
            for (StrokePoint& pt : pastedStroke.points) {
                pt.pos += offset;
            }
            pastedStroke.updateBoundingBox();
            // Note: addStrokeToEdgelessTiles() generates new IDs for each segment
            
            // Use the shared helper that properly splits strokes at tile boundaries
            auto addedSegments = addStrokeToEdgelessTiles(pastedStroke, m_edgelessActiveLayerIndex);
            for (const auto& seg : addedSegments) {
                undoAction.segments.append({seg.first, seg.second});
            }
        }
        
        // Push to edgeless undo stack
        if (!undoAction.segments.isEmpty()) {
            pushEdgelessUndoAction(undoAction);
        }
        
    } else {
        // ========== PAGED MODE ==========
        // Paste to current page's active layer
        
        int pageIndex = currentPageIndex();
        if (pageIndex < 0 || pageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(pageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->activeLayer();
        if (!layer) return;
        
        // Prepare undo action
        PageUndoAction undoAction;
        undoAction.type = PageUndoAction::AddStroke;  // Will add multiple strokes
        undoAction.pageIndex = pageIndex;
        undoAction.layerIndex = page->activeLayerIndex;
        
        // Adjust offset for paged mode (use page-local coordinates)
        QPointF pageOrigin = pagePosition(pageIndex);
        QPointF pageCenter = docCenter - pageOrigin;
        offset = pageCenter - clipboardCenter;
        
        for (const VectorStroke& stroke : m_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            
            // Apply paste offset
            for (StrokePoint& pt : pastedStroke.points) {
                pt.pos += offset;
            }
            pastedStroke.updateBoundingBox();
            pastedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            
            layer->addStroke(pastedStroke);
            undoAction.addedStrokes.append(pastedStroke);
        }
        
        layer->invalidateStrokeCache();
        
        // Mark page dirty for lazy save (BUG FIX: was missing)
        m_document->markPageDirty(pageIndex);
        
        // Push to paged undo stack (use addedStrokes for multiple strokes)
        pushUndoAction(pageIndex, undoAction);
    }
    
    update();
    emit documentModified();
}

void DocumentViewport::deleteSelection()
{
    if (!m_lassoSelection.isValid() || !m_document) {
        return;
    }
    
    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        // Remove strokes from their tiles by ID
        
        EdgelessUndoAction undoAction;
        undoAction.type = PageUndoAction::RemoveMultiple;
        undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;
        
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;
            
            QVector<VectorStroke>& layerStrokes = layer->strokes();
            bool modified = false;
            
            for (int i = layerStrokes.size() - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        // Store for undo (tile-local coords)
                        undoAction.segments.append({coord, layerStrokes[i]});
                        layerStrokes.removeAt(i);
                        modified = true;
                        break;
                    }
                }
            }
            
            if (modified) {
                layer->invalidateStrokeCache();
                m_document->markTileDirty(coord);
            }
        }
        
        // Push to edgeless undo stack
        if (!undoAction.segments.isEmpty()) {
            pushEdgelessUndoAction(undoAction);
        }
        
    } else {
        // ========== PAGED MODE ==========
        
        if (m_lassoSelection.sourcePageIndex < 0 || 
            m_lassoSelection.sourcePageIndex >= m_document->pageCount()) {
            return;
        }
        
        Page* page = m_document->page(m_lassoSelection.sourcePageIndex);
        if (!page) return;
        
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;
        
        // Prepare undo action
        PageUndoAction undoAction;
        undoAction.type = PageUndoAction::RemoveMultiple;
        undoAction.pageIndex = m_lassoSelection.sourcePageIndex;
        undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;
        
        // Remove strokes by ID
        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = layerStrokes.size() - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    undoAction.strokes.append(layerStrokes[i]);
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        
        layer->invalidateStrokeCache();
        
        // Mark page dirty for lazy save (BUG FIX: was missing)
        if (!undoAction.strokes.isEmpty()) {
            m_document->markPageDirty(m_lassoSelection.sourcePageIndex);
        }
        
        // Push to paged undo stack
        if (!undoAction.strokes.isEmpty()) {
            pushUndoAction(m_lassoSelection.sourcePageIndex, undoAction);
        }
    }
    
    clearLassoSelection();
    update();
    emit documentModified();
}

// =========================================================================
// Public Clipboard Operations (Action Bar support)
// =========================================================================

void DocumentViewport::copyLassoSelection()
{
    copySelection();
}

void DocumentViewport::cutLassoSelection()
{
    cutSelection();
}

void DocumentViewport::pasteLassoSelection()
{
    pasteSelection();
}

void DocumentViewport::deleteLassoSelection()
{
    deleteSelection();
}

void DocumentViewport::copyTextSelection()
{
    copySelectedTextToClipboard();
}

void DocumentViewport::clearLassoSelection()
{
    bool hadSelection = m_lassoSelection.isValid();
    
    m_lassoSelection.clear();
    m_lassoPath.clear();
    m_isDrawingLasso = false;
    
    // P1: Reset cache state
    m_lastRenderedLassoIdx = 0;
    m_lassoPathLength = 0;
    
    // P3: Clear selection stroke cache
    m_selectionStrokeCache = QPixmap();
    m_selectionCacheDirty = true;
    
    // P5: Clear background snapshot
    m_selectionBackgroundSnapshot = QPixmap();
    
    // Action Bar: Notify that lasso selection was cleared
    if (hadSelection) {
        emit lassoSelectionChanged(false);
    }
    
    update();
}

// ===== Highlighter Tool Methods (Phase A) =====

// Note: PDF_TO_PAGE_SCALE and PAGE_TO_PDF_SCALE defined in Constants section at top of file

void DocumentViewport::loadTextBoxesForPage(int pageIndex)
{
    // Already cached?
    if (pageIndex == m_textBoxCachePageIndex && !m_textBoxCache.isEmpty()) {
        return;
    }
    
    m_textBoxCache.clear();
    m_textBoxCachePageIndex = -1;
    
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    // Check if page has PDF background
    Page* page = m_document->page(pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        return;
    }
    
    // Get PDF provider
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->supportsTextExtraction()) {
        return;
    }
    
    // Get PDF page index (may differ from document page index)
    int pdfPageIndex = page->pdfPageNumber;
    if (pdfPageIndex < 0) {
        pdfPageIndex = pageIndex;  // Fallback: assume 1:1 mapping
    }
    
    // Load text boxes
    m_textBoxCache = pdf->textBoxes(pdfPageIndex);
    m_textBoxCachePageIndex = pageIndex;
    
    // Debug output removed - too verbose during normal use
}

void DocumentViewport::clearTextBoxCache()
{
    m_textBoxCache.clear();
    m_textBoxCachePageIndex = -1;
    m_lastHitBoxIndex = -1;  // Reset locality hint
}

// ============================================================================
// PDF Link Support (Phase D.1)
// ============================================================================

void DocumentViewport::loadLinksForPage(int pageIndex)
{
    // Already cached? (check both index and non-empty, consistent with loadTextBoxesForPage)
    // Note: empty cache with valid index means the page has no links, which is valid
    if (pageIndex == m_linkCachePageIndex && pageIndex >= 0) {
        return;
    }
    
    m_linkCache.clear();
    m_linkCachePageIndex = -1;
    
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        return;
    }
    
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->supportsLinks()) {
        return;
    }
    
    int pdfPageIndex = page->pdfPageNumber;
    if (pdfPageIndex < 0) pdfPageIndex = pageIndex;
    
    m_linkCache = pdf->links(pdfPageIndex);
    m_linkCachePageIndex = pageIndex;
    
    // Debug output removed - too verbose during normal scrolling
}

void DocumentViewport::clearLinkCache()
{
    m_linkCache.clear();
    m_linkCachePageIndex = -1;
}

const PdfLink* DocumentViewport::findLinkAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadLinksForPage(pageIndex);
    
    if (m_linkCache.isEmpty()) return nullptr;
    
    // Page was already validated in loadLinksForPage, use cached page size
    // Link cache is only populated if page exists and is PDF, so this is safe
    Page* page = m_document->page(pageIndex);
    if (!page) return nullptr;  // Defensive check (shouldn't happen if cache is populated)
    
    // Link areas are normalized (0-1), convert pagePos to normalized coords
    const QSizeF& pageSize = page->size;
    const qreal normX = pagePos.x() / pageSize.width();
    const qreal normY = pagePos.y() / pageSize.height();
    
    for (const PdfLink& link : m_linkCache) {
        if (link.area.contains(QPointF(normX, normY))) {
            return &link;
        }
    }
    return nullptr;
}

void DocumentViewport::activatePdfLink(const PdfLink& link)
{
    switch (link.type) {
        case PdfLinkType::Goto:
            if (link.targetPage >= 0 && link.targetPage < m_document->pageCount()) {
                #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "PDF link: navigating to page" << link.targetPage;
                #endif
                scrollToPage(link.targetPage);
            }
            break;
        case PdfLinkType::Uri:
            if (!link.uri.isEmpty()) {
                #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "PDF link: opening URL" << link.uri;
                #endif
                QDesktopServices::openUrl(QUrl(link.uri));
            }
            break;
        default:
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "PDF link: unsupported type" << static_cast<int>(link.type);
            #endif
            break;
    }
}

void DocumentViewport::updateLinkCursor(const QPointF& viewportPos)
{
    if (m_currentTool != ToolType::Highlighter) return;
    
    PageHit hit = viewportToPage(viewportPos);
    if (!hit.valid()) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    // Optimization: viewportToPage already validated the page exists,
    // so we only need to check the background type
    Page* page = m_document->page(hit.pageIndex);
    if (page->backgroundType != Page::BackgroundType::PDF) {
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    
    // Check if hovering over a link (loadLinksForPage is called inside)
    const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
    if (link && link->type != PdfLinkType::None) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::IBeamCursor);  // Text selection cursor
    }
}

bool DocumentViewport::isHighlighterEnabled() const
{
    if (!m_document) return false;
    
    // Check if current page has PDF
    Page* page = m_document->page(m_currentPageIndex);
    return page && page->backgroundType == Page::BackgroundType::PDF;
}

void DocumentViewport::setAutoHighlightEnabled(bool enabled)
{
    if (m_autoHighlightEnabled == enabled) {
        return;  // No change
    }
    
    m_autoHighlightEnabled = enabled;
    emit autoHighlightEnabledChanged(enabled);
    
    qDebug() << "Auto-highlight mode:" << (enabled ? "ON" : "OFF");
}

void DocumentViewport::setHighlighterColor(const QColor& color)
{
    m_highlighterColor = color;
}

void DocumentViewport::updateHighlighterCursor()
{
    if (m_currentTool != ToolType::Highlighter) {
        // Not in Highlighter mode - restore default cursor
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    // Phase D.1: Use link-aware cursor update (hand on links, I-beam otherwise)
    // Get CURRENT mouse position (not cached) since view may have changed
    QPointF currentPos = mapFromGlobal(QCursor::pos());
    updateLinkCursor(currentPos);
}

void DocumentViewport::handlePointerPress_Highlighter(const PointerEvent& pe)
{
    // Check if highlighter is enabled on this page
    PageHit hit = viewportToPage(pe.viewportPos);
    if (!hit.valid()) {
        bool hadSelection = m_textSelection.isValid();
        m_textSelection.clear();
        if (hadSelection) {
            emit textSelectionChanged(false);
        }
        m_pointerActive = false;  // Reset so hover works
        return;
    }
    
    Page* page = m_document->page(hit.pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        // Not a PDF page - highlighter disabled
        bool hadSelection = m_textSelection.isValid();
        m_textSelection.clear();
        if (hadSelection) {
            emit textSelectionChanged(false);
        }
        m_pointerActive = false;  // Reset so hover works
        return;
    }
    
    // Phase D.1: Check for PDF link click (priority over text selection)
    const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
    if (link && link->type != PdfLinkType::None) {
        activatePdfLink(*link);
        // Reset pointer state since link click doesn't involve dragging
        m_pointerActive = false;
        updateHighlighterCursor();
        return;  // Don't start text selection
    }
    
    // Load text boxes for this page if not cached
    loadTextBoxesForPage(hit.pageIndex);
    
    // Check for double-click (word selection) and triple-click (line selection)
    // Using static variables for timing - thread-safe for single UI thread
    // Note: QElapsedTimer::isValid() returns false until first restart(), which
    // correctly handles the first click (clickCount becomes 1, timer starts)
    static QElapsedTimer lastClickTimer;
    static QPointF lastClickPos;
    static int clickCount = 0;
    
    const qreal doubleClickDistance = 5.0;  // pixels
    const int doubleClickTime = 400;  // ms
    
    if (lastClickTimer.isValid() && 
        lastClickTimer.elapsed() < doubleClickTime &&
        QLineF(lastClickPos, pe.viewportPos).length() < doubleClickDistance) {
        clickCount++;
    } else {
        clickCount = 1;
    }
    lastClickTimer.restart();
    lastClickPos = pe.viewportPos;
    
    if (clickCount == 2) {
        // Double-click: select word
        selectWordAtPoint(hit.pagePoint, hit.pageIndex);
        return;
    } else if (clickCount >= 3) {
        // Triple-click: select line
        selectLineAtPoint(hit.pagePoint, hit.pageIndex);
        clickCount = 0;  // Reset
        return;
    }
    
    // Single click: start text-flow selection at character position
    // Convert page coords to PDF coords
    QPointF pdfPos(hit.pagePoint.x() * PAGE_TO_PDF_SCALE, hit.pagePoint.y() * PAGE_TO_PDF_SCALE);
    
    CharacterPosition charPos = findCharacterAtPoint(pdfPos);
    
    m_textSelection.clear();
    m_textSelection.pageIndex = hit.pageIndex;
    
    if (charPos.isValid()) {
        // Start selection at this character
        m_textSelection.startBoxIndex = charPos.boxIndex;
        m_textSelection.startCharIndex = charPos.charIndex;
        m_textSelection.endBoxIndex = charPos.boxIndex;
        m_textSelection.endCharIndex = charPos.charIndex;
    } else {
        // Clicked outside text - try to find nearest character
        // For now, just mark selection as started but without valid position
        m_textSelection.startBoxIndex = -1;
        m_textSelection.startCharIndex = -1;
        m_textSelection.endBoxIndex = -1;
        m_textSelection.endCharIndex = -1;
    }
    
    m_textSelection.isSelecting = true;
    update();
}

void DocumentViewport::handlePointerMove_Highlighter(const PointerEvent& pe)
{
    if (!m_textSelection.isSelecting) {
        return;
    }
    
    PageHit hit = viewportToPage(pe.viewportPos);
    if (!hit.valid() || hit.pageIndex != m_textSelection.pageIndex) {
        // Moved off the page - for now, just ignore moves outside the page
        return;
    }
    
    // Convert page coords to PDF coords
    QPointF pdfPos(hit.pagePoint.x() * PAGE_TO_PDF_SCALE, hit.pagePoint.y() * PAGE_TO_PDF_SCALE);
    
    CharacterPosition charPos = findCharacterAtPoint(pdfPos);
    
    if (charPos.isValid()) {
        // PERF: Only update if position actually changed
        // This avoids expensive string/rect rebuilding on every mouse move
        bool positionChanged = (charPos.boxIndex != m_textSelection.endBoxIndex ||
                                charPos.charIndex != m_textSelection.endCharIndex);
        
        // If start wasn't valid (clicked outside text initially), set it now
        if (m_textSelection.startBoxIndex < 0) {
            m_textSelection.startBoxIndex = charPos.boxIndex;
            m_textSelection.startCharIndex = charPos.charIndex;
            positionChanged = true;  // Force update on first valid hit
        }
        
        if (positionChanged) {
            // Update end position (start stays anchored)
            m_textSelection.endBoxIndex = charPos.boxIndex;
            m_textSelection.endCharIndex = charPos.charIndex;
            
            // Recompute selected text and highlight rectangles
            updateSelectedTextAndRects();
            
            // Only repaint when selection actually changed
            update();
        }
    }
    // Note: No update() if position unchanged or charPos invalid
}

void DocumentViewport::handlePointerRelease_Highlighter(const PointerEvent& pe)
{
    Q_UNUSED(pe);
    
    if (!m_textSelection.isSelecting) {
        // Phase D.1: Still need to clear pointer state and update cursor
        m_pointerActive = false;
        updateHighlighterCursor();
        return;
    }
    
    m_textSelection.isSelecting = false;
    
    // Finalize selection
    if (m_textSelection.isValid()) {
        finalizeTextSelection();
        
        // Phase B.4: Auto-create strokes if toggle is ON
        if (m_autoHighlightEnabled) {
            createHighlightStrokes();
            // Note: createHighlightStrokes() already clears m_textSelection
        }
    }
    
    // Phase D.1: Clear pointer state so hover code works again
    m_pointerActive = false;
    updateHighlighterCursor();
    
    update();
}

DocumentViewport::CharacterPosition DocumentViewport::findCharacterAtPoint(const QPointF& pdfPos) const
{
    CharacterPosition result;
    
    if (m_textBoxCache.isEmpty()) {
        return result;
    }
    
    // Helper lambda to check a single box and return character position
    auto checkBox = [&](int boxIdx) -> bool {
        const PdfTextBox& box = m_textBoxCache[boxIdx];
        
        // Quick bounding box check first
        if (!box.boundingBox.contains(pdfPos)) {
            return false;
        }
        
        // Check character-level bounding boxes for precision
        if (!box.charBoundingBoxes.isEmpty()) {
            for (int charIdx = 0; charIdx < box.charBoundingBoxes.size(); ++charIdx) {
                if (box.charBoundingBoxes[charIdx].contains(pdfPos)) {
                    result.boxIndex = boxIdx;
                    result.charIndex = charIdx;
                    m_lastHitBoxIndex = boxIdx;  // Update locality hint
                    return true;
                }
            }
            // Point is in box but not in any char rect - find nearest char
            // Use the char whose horizontal center is closest to the point
            qreal minDist = std::numeric_limits<qreal>::max();
            int bestCharIdx = 0;
            for (int charIdx = 0; charIdx < box.charBoundingBoxes.size(); ++charIdx) {
                qreal charCenterX = box.charBoundingBoxes[charIdx].center().x();
                qreal dist = qAbs(pdfPos.x() - charCenterX);
                if (dist < minDist) {
                    minDist = dist;
                    bestCharIdx = charIdx;
                }
            }
            result.boxIndex = boxIdx;
            result.charIndex = bestCharIdx;
            m_lastHitBoxIndex = boxIdx;  // Update locality hint
            return true;
        } else {
            // No character boxes - return the whole word (char 0)
            result.boxIndex = boxIdx;
            result.charIndex = 0;
            m_lastHitBoxIndex = boxIdx;  // Update locality hint
            return true;
        }
    };
    
    // PERF: Spatial locality optimization
    // Check last hit box and its neighbors first (cursor usually stays nearby)
    if (m_lastHitBoxIndex >= 0 && m_lastHitBoxIndex < m_textBoxCache.size()) {
        // Check last hit box
        if (checkBox(m_lastHitBoxIndex)) {
            return result;
        }
        // Check neighbors (next and previous boxes in reading order)
        if (m_lastHitBoxIndex + 1 < m_textBoxCache.size() && checkBox(m_lastHitBoxIndex + 1)) {
            return result;
        }
        if (m_lastHitBoxIndex > 0 && checkBox(m_lastHitBoxIndex - 1)) {
            return result;
        }
    }
    
    // Fallback: Full linear scan (skip already-checked boxes)
    for (int boxIdx = 0; boxIdx < m_textBoxCache.size(); ++boxIdx) {
        // Skip boxes we already checked in the locality optimization
        if (m_lastHitBoxIndex >= 0 && 
            (boxIdx == m_lastHitBoxIndex || 
             boxIdx == m_lastHitBoxIndex + 1 || 
             boxIdx == m_lastHitBoxIndex - 1)) {
            continue;
        }
        if (checkBox(boxIdx)) {
            return result;
        }
    }
    
    return result;  // Invalid - point not in any text box
}

void DocumentViewport::updateSelectedTextAndRects()
{
    m_textSelection.selectedText.clear();
    m_textSelection.highlightRects.clear();
    
    if (m_textBoxCache.isEmpty() || 
        m_textSelection.startBoxIndex < 0 || 
        m_textSelection.endBoxIndex < 0) {
        return;
    }
    
    // Determine selection direction (forward or backward)
    int fromBox, fromChar, toBox, toChar;
    if (m_textSelection.startBoxIndex < m_textSelection.endBoxIndex ||
        (m_textSelection.startBoxIndex == m_textSelection.endBoxIndex && 
         m_textSelection.startCharIndex <= m_textSelection.endCharIndex)) {
        // Forward selection
        fromBox = m_textSelection.startBoxIndex;
        fromChar = m_textSelection.startCharIndex;
        toBox = m_textSelection.endBoxIndex;
        toChar = m_textSelection.endCharIndex;
    } else {
        // Backward selection (user dragged left/up)
        fromBox = m_textSelection.endBoxIndex;
        fromChar = m_textSelection.endCharIndex;
        toBox = m_textSelection.startBoxIndex;
        toChar = m_textSelection.startCharIndex;
    }
    
    // Build selected text and highlight rectangles
    QString selectedText;
    const qreal lineThreshold = 5.0;  // PDF points - boxes on same line
    
    // Group consecutive boxes by line for highlight rect generation
    qreal currentLineY = -1;
    QRectF currentLineRect;
    
    for (int boxIdx = fromBox; boxIdx <= toBox && boxIdx < m_textBoxCache.size(); ++boxIdx) {
        const PdfTextBox& box = m_textBoxCache[boxIdx];
        
        // Skip empty text boxes (safety check)
        if (box.text.isEmpty()) {
            continue;
        }
        
        // Determine character range for this box
        int startChar = (boxIdx == fromBox) ? fromChar : 0;
        int endChar = (boxIdx == toBox) ? toChar : (box.text.length() - 1);
        
        // Clamp to valid range (now safe since we checked for empty text)
        int maxCharIdx = box.text.length() - 1;
        startChar = qBound(0, startChar, maxCharIdx);
        endChar = qBound(0, endChar, maxCharIdx);
        
        if (startChar > endChar) {
            continue;  // Invalid range
        }
        
        // Extract text for this range
        QString boxText = box.text.mid(startChar, endChar - startChar + 1);
        if (!selectedText.isEmpty() && !boxText.isEmpty()) {
            selectedText += " ";  // Space between words
        }
        selectedText += boxText;
        
        // Compute highlight rect for this box's selected characters
        QRectF charRect;
        if (!box.charBoundingBoxes.isEmpty()) {
            for (int c = startChar; c <= endChar && c < box.charBoundingBoxes.size(); ++c) {
                if (charRect.isNull()) {
                    charRect = box.charBoundingBoxes[c];
                } else {
                    charRect = charRect.united(box.charBoundingBoxes[c]);
                }
            }
        } else {
            // No char boxes - use whole word box
            charRect = box.boundingBox;
        }
        
        if (charRect.isNull()) {
            continue;
        }
        
        // Check if this box is on the same line as current line rect
        qreal boxCenterY = charRect.center().y();
        if (currentLineY < 0 || qAbs(boxCenterY - currentLineY) > lineThreshold) {
            // New line - save previous line rect and start new one
            if (!currentLineRect.isNull()) {
                m_textSelection.highlightRects.append(currentLineRect);
            }
            currentLineRect = charRect;
            currentLineY = boxCenterY;
        } else {
            // Same line - extend the rect
            currentLineRect = currentLineRect.united(charRect);
        }
    }
    
    // Don't forget the last line
    if (!currentLineRect.isNull()) {
        m_textSelection.highlightRects.append(currentLineRect);
    }
    
    m_textSelection.selectedText = selectedText;
}

void DocumentViewport::finalizeTextSelection()
{
    if (!m_textSelection.isValid()) {
        return;
    }
    
    // Emit signal for UI feedback
    emit textSelected(m_textSelection.selectedText);
    
    // Action Bar: Notify that text selection now exists
    emit textSelectionChanged(true);
    
    // qDebug() << "Text selected:" << m_textSelection.selectedText.left(50) 
             // << (m_textSelection.selectedText.length() > 50 ? "..." : "");
}

// ============================================================================
// PDF Search Highlighting
// ============================================================================

void DocumentViewport::setSearchMatches(const QVector<PdfSearchMatch>& matches, 
                                         int currentIndex, int pageIndex)
{
    m_searchMatches = matches;
    m_currentSearchMatchIndex = currentIndex;
    m_searchMatchPageIndex = pageIndex;
    
    // Trigger repaint to show highlights
    update();
}

void DocumentViewport::clearSearchMatches()
{
    m_searchMatches.clear();
    m_currentSearchMatchIndex = -1;
    m_searchMatchPageIndex = -1;
    
    // Trigger repaint to remove highlights
    update();
}

void DocumentViewport::selectWordAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadTextBoxesForPage(pageIndex);
    
    // Convert to PDF coords
    QPointF pdfPos(pagePos.x() * PAGE_TO_PDF_SCALE, pagePos.y() * PAGE_TO_PDF_SCALE);
    
    // Find text box containing point
    for (int boxIdx = 0; boxIdx < m_textBoxCache.size(); ++boxIdx) {
        const PdfTextBox& box = m_textBoxCache[boxIdx];
        if (box.boundingBox.contains(pdfPos)) {
            // Skip empty text boxes
            if (box.text.isEmpty()) {
                continue;
            }
            
            m_textSelection.clear();
            m_textSelection.pageIndex = pageIndex;
            
            // Select entire word (box)
            m_textSelection.startBoxIndex = boxIdx;
            m_textSelection.startCharIndex = 0;
            m_textSelection.endBoxIndex = boxIdx;
            m_textSelection.endCharIndex = box.text.length() - 1;
            
            updateSelectedTextAndRects();
            finalizeTextSelection();
            update();
            return;
        }
    }
}

void DocumentViewport::selectLineAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadTextBoxesForPage(pageIndex);
    
    // Convert to PDF coords
    QPointF pdfPos(pagePos.x() * PAGE_TO_PDF_SCALE, pagePos.y() * PAGE_TO_PDF_SCALE);
    
    // Find text box containing point
    int clickedBoxIdx = -1;
    for (int i = 0; i < m_textBoxCache.size(); ++i) {
        if (m_textBoxCache[i].boundingBox.contains(pdfPos)) {
            clickedBoxIdx = i;
            break;
        }
    }
    
    if (clickedBoxIdx < 0) {
        return;  // No text box at point
    }
    
    const qreal lineThreshold = 5.0;  // PDF points
    qreal targetY = m_textBoxCache[clickedBoxIdx].boundingBox.center().y();
    
    // Find all boxes on the same line (similar Y coordinate)
    int firstBoxOnLine = clickedBoxIdx;
    int lastBoxOnLine = clickedBoxIdx;
    
    for (int i = 0; i < m_textBoxCache.size(); ++i) {
        qreal boxY = m_textBoxCache[i].boundingBox.center().y();
        if (qAbs(boxY - targetY) <= lineThreshold) {
            if (i < firstBoxOnLine) firstBoxOnLine = i;
            if (i > lastBoxOnLine) lastBoxOnLine = i;
        }
    }
    
    // Set selection to span entire line
    m_textSelection.clear();
    m_textSelection.pageIndex = pageIndex;
    m_textSelection.startBoxIndex = firstBoxOnLine;
    m_textSelection.startCharIndex = 0;
    m_textSelection.endBoxIndex = lastBoxOnLine;
    
    const PdfTextBox& lastBox = m_textBoxCache[lastBoxOnLine];
    // Safety: handle empty text boxes
    m_textSelection.endCharIndex = lastBox.text.isEmpty() ? 0 : (lastBox.text.length() - 1);
    
    updateSelectedTextAndRects();
    finalizeTextSelection();
    update();
}

// ============================================================================
// Text Selection Rendering
// ============================================================================

void DocumentViewport::renderTextSelectionOverlay(QPainter& painter, int pageIndex)
{
    // Only render if there's a valid selection or actively selecting
    if (m_textSelection.highlightRects.isEmpty() && !m_textSelection.isSelecting) {
        return;
    }
    
    // Only render on the page being selected
    if (m_textSelection.pageIndex != pageIndex) {
        return;
    }
    
    painter.save();
    
    // Selection color (Windows selection blue with transparency)
    QColor selectionColor(0, 120, 215, 100);
    painter.setBrush(selectionColor);
    painter.setPen(Qt::NoPen);
    
    // Draw highlight rectangles (per-line segments, in PDF coords → convert to page coords)
    for (const QRectF& pdfRect : m_textSelection.highlightRects) {
        QRectF pageRect(
            pdfRect.x() * PDF_TO_PAGE_SCALE,
            pdfRect.y() * PDF_TO_PAGE_SCALE,
            pdfRect.width() * PDF_TO_PAGE_SCALE,
            pdfRect.height() * PDF_TO_PAGE_SCALE
        );
        painter.drawRect(pageRect);
    }
    
    painter.restore();
}

void DocumentViewport::renderSearchMatchesOverlay(QPainter& painter, int pageIndex)
{
    // Only render if we have matches on this page
    if (m_searchMatches.isEmpty() || m_searchMatchPageIndex != pageIndex) {
        return;
    }
    
    painter.save();
    painter.setPen(Qt::NoPen);
    
    // Draw all matches
    for (int i = 0; i < m_searchMatches.size(); ++i) {
        const PdfSearchMatch& match = m_searchMatches[i];
        
        // Choose color: orange for current, yellow for others
        QColor fillColor = (i == m_currentSearchMatchIndex) 
            ? m_searchHighlightCurrent 
            : m_searchHighlightOther;
        
        painter.setBrush(fillColor);
        
        // Convert PDF coords to page coords
        const QRectF& pdfRect = match.boundingRect;
        QRectF pageRect(
            pdfRect.x() * PDF_TO_PAGE_SCALE,
            pdfRect.y() * PDF_TO_PAGE_SCALE,
            pdfRect.width() * PDF_TO_PAGE_SCALE,
            pdfRect.height() * PDF_TO_PAGE_SCALE
        );
        
        painter.drawRect(pageRect);
    }
    
    painter.restore();
}

VectorStroke DocumentViewport::createHighlightStroke(const QRectF& rect, const QColor& color) const
{
    VectorStroke stroke;
    
    // Generate unique ID
    stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set color (should include alpha for semi-transparency)
    stroke.color = color;
    
    // Stroke width = rectangle height (text line height)
    stroke.baseThickness = rect.height();
    
    // Create a horizontal line through the center of the rectangle
    // This is how markers work: a thick line that covers the text area
    StrokePoint startPoint;
    startPoint.pos = QPointF(rect.left(), rect.center().y());
    startPoint.pressure = 1.0;  // Uniform pressure for highlights
    
    StrokePoint endPoint;
    endPoint.pos = QPointF(rect.right(), rect.center().y());
    endPoint.pressure = 1.0;
    
    stroke.points.append(startPoint);
    stroke.points.append(endPoint);
    
    // Calculate bounding box
    stroke.updateBoundingBox();
    
    return stroke;
}

QVector<QString> DocumentViewport::createHighlightStrokes()
{
    QVector<QString> createdIds;
    
    // Validate selection
    if (!m_textSelection.isValid() || m_textSelection.highlightRects.isEmpty()) {
        return createdIds;
    }
    
    if (!m_document) {
        return createdIds;
    }
    
    // Get the page where selection exists
    int pageIndex = m_textSelection.pageIndex;
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return createdIds;
    }
    
    // Get the active layer for this page
    VectorLayer* layer = page->activeLayer();
    if (!layer) {
        return createdIds;
    }
    
    // Convert each highlight rect to a stroke
    // highlightRects are in PDF coordinates, need to convert to page coordinates
    for (const QRectF& pdfRect : m_textSelection.highlightRects) {
        // Skip degenerate rectangles (zero width or height)
        if (pdfRect.width() < 0.1 || pdfRect.height() < 0.1) {
            continue;
        }
        
        // Convert from PDF coords (72 DPI) to page coords (96 DPI)
        QRectF pageRect(
            pdfRect.x() * PDF_TO_PAGE_SCALE,
            pdfRect.y() * PDF_TO_PAGE_SCALE,
            pdfRect.width() * PDF_TO_PAGE_SCALE,
            pdfRect.height() * PDF_TO_PAGE_SCALE
        );
        
        // Create the stroke
        VectorStroke stroke = createHighlightStroke(pageRect, m_highlighterColor);
        
        // Add to layer
        layer->addStroke(stroke);
        
        // Push individual undo action (each stroke can be undone separately)
        pushUndoAction(pageIndex, PageUndoAction::AddStroke, stroke);
        
        createdIds.append(stroke.id);
    }
    
    // Invalidate stroke cache for this page
    layer->invalidateStrokeCache();
    
    // Mark page dirty for lazy save (BUG FIX: was missing)
    if (!createdIds.isEmpty()) {
        m_document->markPageDirty(pageIndex);
    }
    
    // Phase C.3.1: Create LinkObject alongside highlight strokes
    if (!createdIds.isEmpty() && !m_textSelection.highlightRects.isEmpty()) {
        createLinkObjectForHighlight(pageIndex);
    }
    
    // Clear the text selection
    m_textSelection.clear();
    
    // Emit document modified (only if we created strokes)
    if (!createdIds.isEmpty()) {
        emit documentModified();
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Created" << createdIds.size() << "highlight strokes on page" << pageIndex;
#endif
    
    return createdIds;
}

void DocumentViewport::copySelectedTextToClipboard()
{
    if (!m_textSelection.isValid() || m_textSelection.selectedText.isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "copySelectedTextToClipboard: No text selected";
        #endif
        return;
    }
    
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_textSelection.selectedText);
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Copied to clipboard:" << m_textSelection.selectedText.left(50)
             << (m_textSelection.selectedText.length() > 50 ? "..." : "");
    #endif
}

void DocumentViewport::addPointToStroke(const QPointF& pagePos, qreal pressure)
{
    // ========== OPTIMIZATION: Point Decimation ==========
    // At 360Hz, consecutive points are often <1 pixel apart.
    // Skip points that are too close to reduce memory and rendering work.
    // This typically reduces point count by 50-70% with no visible quality loss.
    
    if (!m_currentStroke.points.isEmpty()) {
        const QPointF& lastPos = m_currentStroke.points.last().pos;
        qreal dx = pagePos.x() - lastPos.x();
        qreal dy = pagePos.y() - lastPos.y();
        qreal distSq = dx * dx + dy * dy;
        
        if (distSq < MIN_DISTANCE_SQ) {
            // Point too close - but update pressure if higher (preserve pressure peaks)
            if (pressure > m_currentStroke.points.last().pressure) {
                m_currentStroke.points.last().pressure = pressure;
            }
            return;  // Skip this point
        }
    }
    
    StrokePoint pt;
    pt.pos = pagePos;
    pt.pressure = qBound(0.1, pressure, 1.0);
    m_currentStroke.points.append(pt);
    
    // ========== OPTIMIZATION: Dirty Region Update ==========
    // Only repaint the small region around the new point instead of the entire widget.
    // This significantly improves performance, especially on lower-end hardware.
    
    // Use current stroke's thickness (may be pen or marker - marker is typically larger)
    qreal padding = m_currentStroke.baseThickness * 2 * m_zoomLevel;  // Extra padding for stroke width
    
    // Convert page position to viewport coordinates
    QPointF vpPos = pageToViewport(m_activeDrawingPage, pagePos);
    QRectF dirtyRect(vpPos.x() - padding, vpPos.y() - padding, padding * 2, padding * 2);
    
    // Include line from previous point if exists
    if (m_currentStroke.points.size() > 1) {
        const auto& prevPt = m_currentStroke.points[m_currentStroke.points.size() - 2];
        QPointF prevVpPos = pageToViewport(m_activeDrawingPage, prevPt.pos);
        QRectF prevRect(prevVpPos.x() - padding, prevVpPos.y() - padding, padding * 2, padding * 2);
        dirtyRect = dirtyRect.united(prevRect);
    }
    
    // Update only the dirty region (much faster than full widget repaint)
    update(dirtyRect.toRect().adjusted(-2, -2, 2, 2));
}

// ===== Incremental Stroke Rendering (Task 2.3) =====

void DocumentViewport::resetCurrentStrokeCache()
{
    // Create cache at viewport size with high DPI support
    qreal dpr = devicePixelRatioF();
    QSize physicalSize(static_cast<int>(width() * dpr), 
                       static_cast<int>(height() * dpr));
    
    m_currentStrokeCache = QPixmap(physicalSize);
    m_currentStrokeCache.setDevicePixelRatio(dpr);
    m_currentStrokeCache.fill(Qt::transparent);
    m_lastRenderedPointIndex = 0;
    
    // Track the transform state when cache was created
    m_cacheZoom = m_zoomLevel;
    m_cachePan = m_panOffset;
}

void DocumentViewport::renderCurrentStrokeIncremental(QPainter& painter)
{
    // ========== OPTIMIZATION: Incremental Stroke Rendering ==========
    // Instead of re-rendering the entire current stroke every frame,
    // we accumulate rendered segments in m_currentStrokeCache and only
    // render NEW segments to the cache. This reduces CPU load significantly
    // when drawing long strokes at high poll rates (360Hz).
    
    const int n = m_currentStroke.points.size();
    if (n < 1) return;
    
    // For paged mode, require valid drawing page
    bool isEdgeless = m_document && m_document->isEdgeless();
    if (!isEdgeless && m_activeDrawingPage < 0) return;
    
    // Ensure cache is valid (may need recreation after resize or transform change)
    qreal dpr = devicePixelRatioF();
    QSize expectedSize(static_cast<int>(width() * dpr), 
                       static_cast<int>(height() * dpr));
    
    // Check if cache needs full rebuild (size changed, or transform changed during drawing)
    bool needsRebuild = m_currentStrokeCache.isNull() || 
                        m_currentStrokeCache.size() != expectedSize ||
                        !qFuzzyCompare(m_cacheZoom, m_zoomLevel) ||
                        m_cachePan != m_panOffset;
    
    if (needsRebuild) {
        resetCurrentStrokeCache();
        // Must re-render all points since transform changed
    }
    
    // ========== FIX: Semi-Transparent Stroke Rendering ==========
    // For strokes with alpha < 255 (e.g., marker at 50% opacity), we must draw
    // with FULL OPACITY to the cache, then blit with the desired opacity.
    // Otherwise, overlapping segments at joints would compound the alpha,
    // making in-progress strokes appear darker than finished strokes.
    
    int strokeAlpha = m_currentStroke.color.alpha();
    bool hasSemiTransparency = (strokeAlpha < 255);
    
    // Create the drawing color - use full opacity for cache, apply alpha on blit
    QColor drawColor = m_currentStroke.color;
    if (hasSemiTransparency) {
        drawColor.setAlpha(255);  // Draw opaque to cache
    }
    
    // Render new segments to the cache (if any)
    if (n > m_lastRenderedPointIndex && n >= 2) {
        QPainter cachePainter(&m_currentStrokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Apply transform to convert coords to viewport coords
        // The cache is in viewport coordinates (widget pixels)
        cachePainter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        cachePainter.scale(m_zoomLevel, m_zoomLevel);
        
        // For paged mode, translate to page position
        // For edgeless, stroke points are already in document coords - no extra translate
        if (!isEdgeless) {
            cachePainter.translate(pagePosition(m_activeDrawingPage));
        }
        
        // Use line-based rendering for incremental updates (fast)
        // RoundCap ensures segments connect smoothly at joints
        QPen pen(drawColor, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
        // Start from the last rendered point (or 1 if starting fresh)
        int startIdx = qMax(1, m_lastRenderedPointIndex);
        
        // Render each new segment
        for (int i = startIdx; i < n; ++i) {
            const auto& p0 = m_currentStroke.points[i - 1];
            const auto& p1 = m_currentStroke.points[i];
            
            qreal avgPressure = (p0.pressure + p1.pressure) / 2.0;
            qreal width = qMax(m_currentStroke.baseThickness * avgPressure, 1.0);
            
            pen.setWidthF(width);
            cachePainter.setPen(pen);
            cachePainter.drawLine(p0.pos, p1.pos);
        }
        
        // Draw start cap if this is the first render
        if (m_lastRenderedPointIndex == 0 && n >= 1) {
            qreal startRadius = qMax(m_currentStroke.baseThickness * m_currentStroke.points[0].pressure, 1.0) / 2.0;
            cachePainter.setPen(Qt::NoPen);
            cachePainter.setBrush(drawColor);
            cachePainter.drawEllipse(m_currentStroke.points[0].pos, startRadius, startRadius);
        }
        
        m_lastRenderedPointIndex = n;
    }
    
    // Blit the cached current stroke to the viewport
    // For semi-transparent strokes, apply the alpha here (not per-segment)
    if (hasSemiTransparency) {
        painter.setOpacity(strokeAlpha / 255.0);
    }
    painter.drawPixmap(0, 0, m_currentStrokeCache);
    if (hasSemiTransparency) {
        painter.setOpacity(1.0);  // Restore full opacity
    }
    
    // Draw end cap at current position (always needs updating as it moves)
    if (n >= 1) {
        // Apply transform to draw end cap at correct position
        painter.save();
        painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        painter.scale(m_zoomLevel, m_zoomLevel);
        
        // For paged mode, translate to page position
        // For edgeless, stroke points are already in document coords
        if (!isEdgeless) {
            painter.translate(pagePosition(m_activeDrawingPage));
        }
        
        qreal endRadius = qMax(m_currentStroke.baseThickness * m_currentStroke.points[n - 1].pressure, 1.0) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_currentStroke.color);
        painter.drawEllipse(m_currentStroke.points[n - 1].pos, endRadius, endRadius);
        
        painter.restore();
    }
}

// ===== Eraser Tool (Task 2.4) =====

void DocumentViewport::eraseAt(const PointerEvent& pe)
{
    if (!m_document) return;
    
    // Branch for edgeless mode (Phase E4)
    if (m_document->isEdgeless()) {
        eraseAtEdgeless(pe.viewportPos);
        return;
    }
    
    // Paged mode: require valid page hit
    if (!pe.pageHit.valid()) return;
    
    Page* page = m_document->page(pe.pageHit.pageIndex);
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer || layer->locked) return;
    
    // Find strokes at eraser position
    QVector<QString> hitIds = layer->strokesAtPoint(pe.pageHit.pagePoint, m_eraserSize);
    
    if (hitIds.isEmpty()) return;
    
    // Collect strokes for undo before removing
    // Use a set for O(1) lookup instead of O(n) per ID
    QSet<QString> hitIdSet(hitIds.begin(), hitIds.end());
    QVector<VectorStroke> removedStrokes;
    removedStrokes.reserve(hitIds.size());
    
    for (const VectorStroke& s : layer->strokes()) {
        if (hitIdSet.contains(s.id)) {
            removedStrokes.append(s);
            if (removedStrokes.size() == hitIds.size()) {
                break;  // Found all strokes, no need to continue
            }
        }
    }
    
    // Remove strokes
    for (const QString& id : hitIds) {
        layer->removeStroke(id);
    }
    
    // Stroke cache is automatically invalidated by removeStroke()
    
    // Mark page dirty for lazy save (BUG FIX: was missing)
    if (!removedStrokes.isEmpty()) {
        m_document->markPageDirty(pe.pageHit.pageIndex);
    }
    
    // Push undo action
    if (removedStrokes.size() == 1) {
        pushUndoAction(pe.pageHit.pageIndex, PageUndoAction::RemoveStroke, removedStrokes[0]);
    } else if (removedStrokes.size() > 1) {
        pushUndoAction(pe.pageHit.pageIndex, PageUndoAction::RemoveMultiple, removedStrokes);
    }
    
    emit documentModified();
    
    // ========== OPTIMIZATION: Dirty Region Update for Eraser ==========
    // Calculate elliptical region around eraser position for targeted repaint
    // Use ellipse to match the circular eraser shape and avoid "square brush" artifact
    // Use toAlignedRect() to properly round floating-point to integer coords
    qreal eraserRadius = m_eraserSize * m_zoomLevel + 10;  // Add padding for stroke edges
    QPointF vpPos = pe.viewportPos;
    QRectF dirtyRectF(vpPos.x() - eraserRadius, vpPos.y() - eraserRadius,
                      eraserRadius * 2, eraserRadius * 2);
    update(QRegion(dirtyRectF.toAlignedRect(), QRegion::Ellipse));
}

void DocumentViewport::eraseAtEdgeless(QPointF viewportPos)
{
    // ========== EDGELESS ERASER (Phase E4) ==========
    // In edgeless mode, strokes are split across tiles. The eraser must:
    // 1. Convert viewport position to document coordinates
    // 2. Check the center tile AND neighboring tiles (for cross-tile strokes)
    // 3. Convert document coords to tile-local coords for hit testing
    // 4. Collect strokes for undo, then remove them
    // 5. Mark tiles dirty and remove if empty
    
    if (!m_document || !m_document->isEdgeless()) return;
    
    // Convert viewport position to document coordinates
    QPointF docPt = viewportToDocument(viewportPos);
    
    // Get center tile coordinate
    Document::TileCoord centerTile = m_document->tileCoordForPoint(docPt);
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Collect all erased strokes for undo (Phase E6)
    EdgelessUndoAction undoAction;
    undoAction.type = PageUndoAction::RemoveStroke;
    undoAction.layerIndex = m_edgelessActiveLayerIndex;
    
    // Check center tile + 8 neighbors (3x3 grid)
    // This catches strokes that span tile boundaries
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            int tx = centerTile.first + dx;
            int ty = centerTile.second + dy;
            
            Page* tile = m_document->getTile(tx, ty);
            if (!tile) continue;  // Empty tile
            
            // Get the active layer (use edgeless active layer index)
            if (m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer || layer->locked) continue;
            
            // Convert document point to tile-local coordinates
            QPointF tileOrigin(tx * tileSize, ty * tileSize);
            QPointF localPt = docPt - tileOrigin;
            
            // Find strokes at eraser position
            QVector<QString> hitIds = layer->strokesAtPoint(localPt, m_eraserSize);
            
            if (hitIds.isEmpty()) continue;
            
            // Collect strokes for undo BEFORE removing (Phase E6)
            for (const QString& id : hitIds) {
                // Find the stroke by ID and copy it for undo
                for (const VectorStroke& stroke : layer->strokes()) {
                    if (stroke.id == id) {
                        undoAction.segments.append({{tx, ty}, stroke});
                        break;
                    }
                }
            }
            
            // Remove strokes
            for (const QString& id : hitIds) {
                layer->removeStroke(id);
            }
            
            // Mark tile as dirty for persistence (before potential removal)
            m_document->markTileDirty({tx, ty});
            
            // Remove tile if now empty (saves memory, tile file deleted on next save)
            m_document->removeTileIfEmpty(tx, ty);
        }
    }
    
    // Push undo action if any strokes were erased
    if (!undoAction.segments.isEmpty()) {
        pushEdgelessUndoAction(undoAction);
        emit documentModified();
        
        // Dirty region update - use elliptical region to match circular eraser
        // Use toAlignedRect() to properly round floating-point to integer coords
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 10;  // Add padding for stroke edges
        QRectF dirtyRectF(viewportPos.x() - eraserRadius, viewportPos.y() - eraserRadius,
                          eraserRadius * 2, eraserRadius * 2);
        update(QRegion(dirtyRectF.toAlignedRect(), QRegion::Ellipse));
    }
}

void DocumentViewport::drawEraserCursor(QPainter& painter)
{
    // Show eraser cursor for: selected eraser tool OR active hardware eraser
    bool showCursor = (m_currentTool == ToolType::Eraser || m_hardwareEraserActive);
    
    if (!showCursor) {
        return;
    }
    
    // Only draw if pointer is currently inside the viewport
    // m_pointerInViewport is set by enterEvent/leaveEvent for reliable tracking
    // This fixes the issue where cursor would stay visible after pen leaves
    if (!m_pointerInViewport) {
        return;
    }
    
    // Additional check: pointer position should be within bounds
    // (defensive check in case enterEvent wasn't called)
    if (!rect().contains(m_lastPointerPos.toPoint())) {
        return;
    }
    
    // Draw eraser circle at last pointer position (in viewport coordinates)
    // The eraser size is in document units, so scale by zoom for screen display
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    
    qreal screenRadius = m_eraserSize * m_zoomLevel;
    painter.drawEllipse(m_lastPointerPos, screenRadius, screenRadius);
}

// ===== Undo/Redo System (Task 2.5) =====

void DocumentViewport::pushUndoAction(int pageIndex, PageUndoAction::Type type, const VectorStroke& stroke)
{
    PageUndoAction action;
    action.type = type;
    action.pageIndex = pageIndex;
    action.stroke = stroke;
    
    m_undoStacks[pageIndex].push(action);
    trimUndoStack(pageIndex);
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::pushUndoAction(int pageIndex, PageUndoAction::Type type, const QVector<VectorStroke>& strokes)
{
    PageUndoAction action;
    action.type = type;
    action.pageIndex = pageIndex;
    action.strokes = strokes;
    
    m_undoStacks[pageIndex].push(action);
    trimUndoStack(pageIndex);
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::pushUndoAction(int pageIndex, const PageUndoAction& action)
{
    // For complete actions (TransformSelection, etc.)
    PageUndoAction fullAction = action;
    fullAction.pageIndex = pageIndex;  // Ensure page index is set
    
    m_undoStacks[pageIndex].push(fullAction);
    trimUndoStack(pageIndex);
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::clearRedoStack(int pageIndex)
{
    if (m_redoStacks.contains(pageIndex)) {
        bool hadRedo = !m_redoStacks[pageIndex].isEmpty();
        m_redoStacks[pageIndex].clear();
        if (hadRedo) {
            emit redoAvailableChanged(false);
        }
    }
}

void DocumentViewport::clearUndoStacksFrom(int pageIndex)
{
    // Clear undo/redo stacks for all pages >= pageIndex
    // Used when inserting/deleting pages to prevent stale undo from applying to wrong pages
    // Preserves undo for pages before the affected index (user's "done" work)
    
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    
    // Clear undo stacks for affected pages
    for (auto it = m_undoStacks.begin(); it != m_undoStacks.end(); ) {
        if (it.key() >= pageIndex) {
            it = m_undoStacks.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear redo stacks for affected pages
    for (auto it = m_redoStacks.begin(); it != m_redoStacks.end(); ) {
        if (it.key() >= pageIndex) {
            it = m_redoStacks.erase(it);
        } else {
            ++it;
        }
    }
    
    // Emit signals if availability changed
    if (hadUndo && !canUndo()) {
        emit undoAvailableChanged(false);
    }
    if (hadRedo && !canRedo()) {
        emit redoAvailableChanged(false);
    }
}

// ============================================================================
// Layer Management (Phase 5)
// ============================================================================

void DocumentViewport::setEdgelessActiveLayerIndex(int layerIndex)
{
    if (layerIndex < 0) layerIndex = 0;
    m_edgelessActiveLayerIndex = layerIndex;
}

void DocumentViewport::trimUndoStack(int pageIndex)
{
    // Limit stack size to prevent unbounded memory growth
    // QStack inherits from QVector, so we can use remove() for O(n) trimming
    // This only runs when stack exceeds limit (rare - once every MAX_UNDO_PER_PAGE actions)
    QStack<PageUndoAction>& stack = m_undoStacks[pageIndex];
    
    while (stack.size() > MAX_UNDO_PER_PAGE) {
        // Remove oldest entry (at the bottom of the stack = index 0)
        // QStack inherits QVector's remove() method
        stack.remove(0);
    }
}

// ===== Edgeless Undo/Redo (Phase E6) =====

void DocumentViewport::pushEdgelessUndoAction(const EdgelessUndoAction& action)
{
    m_edgelessUndoStack.push(action);
    trimEdgelessUndoStack();
    clearEdgelessRedoStack();
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::undoEdgeless()
{
    if (m_edgelessUndoStack.isEmpty() || !m_document) return;
    
    EdgelessUndoAction action = m_edgelessUndoStack.pop();
    
    // Check if this is an object action (Phase O2.7)
    bool isObjectAction = (action.type == PageUndoAction::ObjectInsert ||
                           action.type == PageUndoAction::ObjectDelete ||
                           action.type == PageUndoAction::ObjectMove ||
                           action.type == PageUndoAction::ObjectAffinityChange);
    
    if (isObjectAction) {
        // Object actions (Phase O2.7.3)
        switch (action.type) {
            case PageUndoAction::ObjectInsert:
                // Undo insert = remove the object (BF.6)
                {
                    deselectObjectById(action.objectId);
                    Page* tile = m_document->getTile(action.objectTileCoord.first, 
                                                     action.objectTileCoord.second);
                    if (tile) {
                        tile->removeObject(action.objectId);
                        m_document->markTileDirty(action.objectTileCoord);
                        m_document->removeTileIfEmpty(action.objectTileCoord.first,
                                                      action.objectTileCoord.second);
                    }
                    m_document->recalculateMaxObjectExtent();
                }
                break;
                
            case PageUndoAction::ObjectDelete:
                // Undo delete = restore the object to the tile
                {
                    Page* tile = m_document->getOrCreateTile(action.objectTileCoord.first,
                                                             action.objectTileCoord.second);
                    if (tile) {
                        auto obj = InsertedObject::fromJson(action.objectData);
                        if (obj) {
                            // Phase O2.C: Load any external assets (type-agnostic)
                            obj->loadAssets(m_document->bundlePath());
                            m_document->updateMaxObjectExtent(obj.get());
                            tile->addObject(std::move(obj));
                            m_document->markTileDirty(action.objectTileCoord);
                        }
                    }
                }
                break;
                
            case PageUndoAction::ObjectMove:
                // Undo move = restore old position (may involve cross-tile move)
                {
                    if (action.objectOldTile != action.objectNewTile) {
                        // Cross-tile move: move object back from new tile to old tile
                        Page* newTile = m_document->getTile(action.objectNewTile.first,
                                                            action.objectNewTile.second);
                        if (newTile) {
                            std::unique_ptr<InsertedObject> obj = newTile->extractObject(action.objectId);
                            if (obj) {
                                obj->position = action.objectOldPosition;
                                Page* oldTile = m_document->getOrCreateTile(action.objectOldTile.first,
                                                                            action.objectOldTile.second);
                                if (oldTile) {
                                    oldTile->addObject(std::move(obj));
                                    m_document->markTileDirty(action.objectOldTile);
                                }
                                m_document->markTileDirty(action.objectNewTile);
                                m_document->removeTileIfEmpty(action.objectNewTile.first,
                                                              action.objectNewTile.second);
                            }
                        }
                    } else {
                        // Same-tile move: just restore position
                        Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                         action.objectTileCoord.second);
                        if (tile) {
                            InsertedObject* obj = tile->objectById(action.objectId);
                            if (obj) {
                                obj->position = action.objectOldPosition;
                            }
                            m_document->markTileDirty(action.objectTileCoord);
                        }
                    }
                }
                break;
                
            case PageUndoAction::ObjectAffinityChange:
                // Undo affinity change = restore old affinity
                {
                    Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                     action.objectTileCoord.second);
                    if (tile) {
                        tile->updateObjectAffinity(action.objectId, action.objectOldAffinity);
                        m_document->markTileDirty(action.objectTileCoord);
                    }
                }
                break;
                
            // -----------------------------------------------------------------
            // ObjectResize (Phase O3.1.5, O3.1.8.3): Undo resize/rotate
            // Restores old position, size, and rotation.
            // -----------------------------------------------------------------
            case PageUndoAction::ObjectResize:
                {
                    Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                     action.objectTileCoord.second);
                    if (tile) {
                        InsertedObject* obj = tile->objectById(action.objectId);
                        if (obj) {
                            obj->position = action.objectOldPosition;
                            obj->size = action.objectOldSize;
                            obj->rotation = action.objectOldRotation;  // O3.1.8.3
                            #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "Undo ObjectResize (edgeless): obj" << action.objectId
                                     << "pos" << obj->position << "size" << obj->size 
                                     << "rot" << obj->rotation;
                            #endif
                        }
                        m_document->markTileDirty(action.objectTileCoord);
                    }
                }
                break;
                
            default:
                break;
        }
    } else if (action.type == PageUndoAction::TransformSelection) {
        // Handle TransformSelection specially (compound action)
        // Step 1: Remove the added strokes (undo the add)
        for (const auto& seg : action.addedSegments) {
            Page* tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
            if (!tile) continue;
            
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (layer) {
                layer->removeStroke(seg.stroke.id);
                layer->invalidateStrokeCache();
            }
            m_document->markTileDirty(seg.tileCoord);
            m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
        }
        
        // Step 2: Add back the removed strokes (undo the remove)
        for (const auto& seg : action.removedSegments) {
            Page* tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
            if (!tile) continue;
            
            while (tile->layerCount() <= action.layerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (layer) {
                layer->addStroke(seg.stroke);
                layer->invalidateStrokeCache();
            }
            m_document->markTileDirty(seg.tileCoord);
        }
    } else {
        // Apply undo to each segment (may span multiple tiles)
        for (const auto& seg : action.segments) {
            Page* tile = nullptr;
            
            if (action.type == PageUndoAction::AddStroke) {
                // Undoing an add = remove the stroke (tile might not exist if already removed)
                tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
            } else {
                // Undoing a remove = add the stroke back (may need to recreate tile)
                tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
            }
            
            if (!tile) continue;
            
            // Ensure layer exists
            while (tile->layerCount() <= action.layerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (!layer) continue;
            
            switch (action.type) {
                case PageUndoAction::AddStroke:
                    // Undo add = remove the stroke
                    layer->removeStroke(seg.stroke.id);
                    // Mark dirty BEFORE potential removal (removeTileIfEmpty clears dirty flag)
                    m_document->markTileDirty(seg.tileCoord);
                    // Check if tile is now empty
                    m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
                    break;
                    
                case PageUndoAction::RemoveStroke:
                case PageUndoAction::RemoveMultiple:
                    // Undo remove = add the stroke back
                    layer->addStroke(seg.stroke);
                    m_document->markTileDirty(seg.tileCoord);
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Push to redo stack
    m_edgelessRedoStack.push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    
    // M.7.3: Notify if objects were added/removed (for sidebar refresh)
    if (action.type == PageUndoAction::ObjectInsert ||
        action.type == PageUndoAction::ObjectDelete) {
        emit linkObjectListMayHaveChanged();
    }
    
    update();
}

void DocumentViewport::redoEdgeless()
{
    if (m_edgelessRedoStack.isEmpty() || !m_document) return;
    
    EdgelessUndoAction action = m_edgelessRedoStack.pop();
    
    // Check if this is an object action (Phase O2.7)
    bool isObjectAction = (action.type == PageUndoAction::ObjectInsert ||
                           action.type == PageUndoAction::ObjectDelete ||
                           action.type == PageUndoAction::ObjectMove ||
                           action.type == PageUndoAction::ObjectAffinityChange);
    
    if (isObjectAction) {
        // Object actions (Phase O2.7.3)
        switch (action.type) {
            case PageUndoAction::ObjectInsert:
                // Redo insert = re-add the object from JSON
                {
                    Page* tile = m_document->getOrCreateTile(action.objectTileCoord.first,
                                                             action.objectTileCoord.second);
                    if (tile) {
                        auto obj = InsertedObject::fromJson(action.objectData);
                        if (obj) {
                            // Phase O2.C: Load any external assets (type-agnostic)
                            obj->loadAssets(m_document->bundlePath());
                            m_document->updateMaxObjectExtent(obj.get());
                            tile->addObject(std::move(obj));
                            m_document->markTileDirty(action.objectTileCoord);
                        }
                    }
                }
                break;
                
            case PageUndoAction::ObjectDelete:
                // Redo delete = remove the object again
                {
                    Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                     action.objectTileCoord.second);
                    if (tile) {
                        tile->removeObject(action.objectId);
                        m_document->markTileDirty(action.objectTileCoord);
                        m_document->removeTileIfEmpty(action.objectTileCoord.first,
                                                      action.objectTileCoord.second);
                    }
                    m_document->recalculateMaxObjectExtent();
                }
                break;
                
            case PageUndoAction::ObjectMove:
                // Redo move = apply new position (may involve cross-tile move)
                {
                    if (action.objectOldTile != action.objectNewTile) {
                        // Cross-tile move: move object from old tile to new tile
                        Page* oldTile = m_document->getTile(action.objectOldTile.first,
                                                            action.objectOldTile.second);
                        if (oldTile) {
                            std::unique_ptr<InsertedObject> obj = oldTile->extractObject(action.objectId);
                            if (obj) {
                                obj->position = action.objectNewPosition;
                                Page* newTile = m_document->getOrCreateTile(action.objectNewTile.first,
                                                                            action.objectNewTile.second);
                                if (newTile) {
                                    newTile->addObject(std::move(obj));
                                    m_document->markTileDirty(action.objectNewTile);
                                }
                                m_document->markTileDirty(action.objectOldTile);
                                m_document->removeTileIfEmpty(action.objectOldTile.first,
                                                              action.objectOldTile.second);
                            }
                        }
                    } else {
                        // Same-tile move: just apply new position
                        Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                         action.objectTileCoord.second);
                        if (tile) {
                            InsertedObject* obj = tile->objectById(action.objectId);
                            if (obj) {
                                obj->position = action.objectNewPosition;
                            }
                            m_document->markTileDirty(action.objectTileCoord);
                        }
                    }
                }
                break;
                
            case PageUndoAction::ObjectAffinityChange:
                // Redo affinity change = apply new affinity
                {
                    Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                     action.objectTileCoord.second);
                    if (tile) {
                        tile->updateObjectAffinity(action.objectId, action.objectNewAffinity);
                        m_document->markTileDirty(action.objectTileCoord);
                    }
                }
                break;
                
            // -----------------------------------------------------------------
            // ObjectResize (Phase O3.1.5, O3.1.8.3): Redo resize/rotate
            // Applies new position, size, and rotation.
            // -----------------------------------------------------------------
            case PageUndoAction::ObjectResize:
                {
                    Page* tile = m_document->getTile(action.objectTileCoord.first,
                                                     action.objectTileCoord.second);
                    if (tile) {
                        InsertedObject* obj = tile->objectById(action.objectId);
                        if (obj) {
                            obj->position = action.objectNewPosition;
                            obj->size = action.objectNewSize;
                            obj->rotation = action.objectNewRotation;  // O3.1.8.3
                            #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "Redo ObjectResize (edgeless): obj" << action.objectId
                                     << "pos" << obj->position << "size" << obj->size
                                     << "rot" << obj->rotation;
                            #endif
                        }
                        m_document->markTileDirty(action.objectTileCoord);
                    }
                }
                break;
                
            default:
                break;
        }
    } else if (action.type == PageUndoAction::TransformSelection) {
        // Handle TransformSelection specially (compound action)
        // Step 1: Remove the original strokes again (redo the remove)
        for (const auto& seg : action.removedSegments) {
            Page* tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
            if (!tile) continue;
            
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (layer) {
                layer->removeStroke(seg.stroke.id);
                layer->invalidateStrokeCache();
            }
            m_document->markTileDirty(seg.tileCoord);
            m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
        }
        
        // Step 2: Add the transformed strokes again (redo the add)
        for (const auto& seg : action.addedSegments) {
            Page* tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
            if (!tile) continue;
            
            while (tile->layerCount() <= action.layerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (layer) {
                layer->addStroke(seg.stroke);
                layer->invalidateStrokeCache();
            }
            m_document->markTileDirty(seg.tileCoord);
        }
    } else {
        // Apply redo to each segment
        for (const auto& seg : action.segments) {
            Page* tile = nullptr;
            
            if (action.type == PageUndoAction::AddStroke) {
                // Redoing an add = add the stroke back (may need to recreate tile)
                tile = m_document->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second);
            } else {
                // Redoing a remove = remove the stroke (tile might not exist if already removed)
                tile = m_document->getTile(seg.tileCoord.first, seg.tileCoord.second);
            }
            
            if (!tile) continue;
            
            // Ensure layer exists
            while (tile->layerCount() <= action.layerIndex) {
                tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
            }
            VectorLayer* layer = tile->layer(action.layerIndex);
            if (!layer) continue;
            
            switch (action.type) {
                case PageUndoAction::AddStroke:
                    // Redo add = add the stroke again
                    layer->addStroke(seg.stroke);
                    m_document->markTileDirty(seg.tileCoord);
                    break;
                    
                case PageUndoAction::RemoveStroke:
                case PageUndoAction::RemoveMultiple:
                    // Redo remove = remove the stroke again
                    layer->removeStroke(seg.stroke.id);
                    // Mark dirty BEFORE potential removal (removeTileIfEmpty clears dirty flag)
                    m_document->markTileDirty(seg.tileCoord);
                    // Check if tile is now empty
                    m_document->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    // Push to undo stack
    m_edgelessUndoStack.push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    
    // M.7.3: Notify if objects were added/removed (for sidebar refresh)
    if (action.type == PageUndoAction::ObjectInsert ||
        action.type == PageUndoAction::ObjectDelete) {
        emit linkObjectListMayHaveChanged();
    }
    
    update();
}

void DocumentViewport::clearEdgelessRedoStack()
{
    if (!m_edgelessRedoStack.isEmpty()) {
        m_edgelessRedoStack.clear();
        emit redoAvailableChanged(canRedo());
    }
}

void DocumentViewport::trimEdgelessUndoStack()
{
    while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
        // Remove oldest entry (at the bottom of the stack = index 0)
        m_edgelessUndoStack.remove(0);
    }
}

QVector<DocumentViewport::TileSegment> DocumentViewport::splitStrokeIntoTileSegments(
    const QVector<StrokePoint>& points) const
{
    QVector<TileSegment> segments;
    
    if (points.isEmpty() || !m_document) {
        return segments;
    }
    
    // Start first segment
    TileSegment currentSegment;
    currentSegment.coord = m_document->tileCoordForPoint(points.first().pos);
    currentSegment.points.append(points.first());
    
    // Walk through remaining points, detecting tile boundary crossings
    for (int i = 1; i < points.size(); ++i) {
        const StrokePoint& pt = points[i];
        Document::TileCoord ptTile = m_document->tileCoordForPoint(pt.pos);
        
        if (ptTile != currentSegment.coord) {
            // Tile boundary crossed!
            // Both segments need the boundary-crossing line segment (prevPt → pt)
            // so that each segment's cap is covered by the other's stroke body.
            // (BUG-DRW-004 fix)
            StrokePoint prevPt = currentSegment.points.last();
            
            // End current segment WITH the new point (extends past boundary)
            currentSegment.points.append(pt);
            segments.append(currentSegment);
            
            // Start new segment with PREVIOUS point (extends before boundary)
            // Now both tiles have the line segment crossing the boundary
            currentSegment.coord = ptTile;
            currentSegment.points.clear();
            currentSegment.points.append(prevPt);  // Previous point (in old tile)
            currentSegment.points.append(pt);      // Current point (in new tile)
        } else {
            // Same tile, just add point
            currentSegment.points.append(pt);
        }
    }
    
    // Don't forget the last segment
    if (!currentSegment.points.isEmpty()) {
        segments.append(currentSegment);
    }
    
    return segments;
}

void DocumentViewport::undo()
{
    // Edgeless mode uses global undo stack
    if (m_document && m_document->isEdgeless()) {
        undoEdgeless();
        return;
    }
    
    // Paged mode: per-page undo
    int pageIdx = m_currentPageIndex;
    
    if (!m_undoStacks.contains(pageIdx) || m_undoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    PageUndoAction action = m_undoStacks[pageIdx].pop();
    
    // Check if this is a stroke action (needs layer) or object action (works on page)
    bool isStrokeAction = (action.type == PageUndoAction::AddStroke ||
                           action.type == PageUndoAction::RemoveStroke ||
                           action.type == PageUndoAction::RemoveMultiple ||
                           action.type == PageUndoAction::TransformSelection);
    
    if (isStrokeAction) {
        // Get the correct layer (use action.layerIndex for lasso actions, otherwise active layer)
        VectorLayer* layer = nullptr;
        if (action.type == PageUndoAction::TransformSelection || 
            action.type == PageUndoAction::RemoveMultiple) {
            layer = page->layer(action.layerIndex);
        } else {
            layer = page->activeLayer();
        }
        if (!layer) return;
        
        switch (action.type) {
            case PageUndoAction::AddStroke:
                // Undo adding = remove the stroke(s)
                if (!action.addedStrokes.isEmpty()) {
                    // Multiple strokes (paste or transform)
                    for (const auto& s : action.addedStrokes) {
                        layer->removeStroke(s.id);
                    }
                } else {
                    // Single stroke
                    layer->removeStroke(action.stroke.id);
                }
                break;
                
            case PageUndoAction::RemoveStroke:
                // Undo removing = add the stroke back
                layer->addStroke(action.stroke);
                break;
                
            case PageUndoAction::RemoveMultiple:
                // Undo removing multiple = add all strokes back
                for (const auto& s : action.strokes) {
                    layer->addStroke(s);
                }
                break;
                
            case PageUndoAction::TransformSelection:
                // Undo transform: remove added strokes, restore removed strokes
                for (const auto& s : action.addedStrokes) {
                    layer->removeStroke(s.id);
                }
                for (const auto& s : action.removedStrokes) {
                    layer->addStroke(s);
                }
                break;
                
            default:
                break;  // Handled below
        }
        
        layer->invalidateStrokeCache();
    } else {
        // Object actions (Phase O2.7)
        switch (action.type) {
            case PageUndoAction::ObjectInsert:
                // Undo insert = remove the object (BF.6)
                deselectObjectById(action.objectId);
                page->removeObject(action.objectId);
                m_document->recalculateMaxObjectExtent();
                break;
                
            case PageUndoAction::ObjectDelete:
                // Undo delete = restore the object from JSON
                {
                    auto obj = InsertedObject::fromJson(action.objectData);
                    if (obj) {
                        // Phase O2.C: Load any external assets (type-agnostic)
                        obj->loadAssets(m_document->bundlePath());
                        m_document->updateMaxObjectExtent(obj.get());
                        page->addObject(std::move(obj));
                    }
                }
                break;
                
            case PageUndoAction::ObjectMove:
                // Undo move = restore old position
                {
                    InsertedObject* obj = page->objectById(action.objectId);
                    if (obj) {
                        obj->position = action.objectOldPosition;
                    }
                }
                break;
                
            case PageUndoAction::ObjectAffinityChange:
                // Undo affinity change = restore old affinity
                page->updateObjectAffinity(action.objectId, action.objectOldAffinity);
                break;
                
            // -----------------------------------------------------------------
            // ObjectResize (Phase O3.1.5, O3.1.8.3): Undo resize/rotate
            // Restores old position, size, and rotation.
            // -----------------------------------------------------------------
            case PageUndoAction::ObjectResize:
                {
                    InsertedObject* obj = page->objectById(action.objectId);
                    if (obj) {
                        obj->position = action.objectOldPosition;
                        obj->size = action.objectOldSize;
                        obj->rotation = action.objectOldRotation;  // O3.1.8.3
                        #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Undo ObjectResize (paged): obj" << action.objectId
                                 << "pos" << obj->position << "size" << obj->size
                                 << "rot" << obj->rotation;
                        #endif
                    }
                }
                break;
                
            default:
                break;
        }
    }
    
    // Mark page dirty for lazy save (BUG FIX: was missing)
    m_document->markPageDirty(pageIdx);
    
    // Push to redo stack
    m_redoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    
    // M.7.3: Notify if objects were added/removed (for sidebar refresh)
    if (action.type == PageUndoAction::ObjectInsert ||
        action.type == PageUndoAction::ObjectDelete) {
        emit linkObjectListMayHaveChanged();
    }
    
    update();
}

void DocumentViewport::redo()
{
    // Edgeless mode uses global redo stack
    if (m_document && m_document->isEdgeless()) {
        redoEdgeless();
        return;
    }
    
    // Paged mode: per-page redo
    int pageIdx = m_currentPageIndex;
    
    if (!m_redoStacks.contains(pageIdx) || m_redoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    PageUndoAction action = m_redoStacks[pageIdx].pop();
    
    // Check if this is a stroke action (needs layer) or object action (works on page)
    bool isStrokeAction = (action.type == PageUndoAction::AddStroke ||
                           action.type == PageUndoAction::RemoveStroke ||
                           action.type == PageUndoAction::RemoveMultiple ||
                           action.type == PageUndoAction::TransformSelection);
    
    if (isStrokeAction) {
        // Get the correct layer (use action.layerIndex for lasso actions, otherwise active layer)
        VectorLayer* layer = nullptr;
        if (action.type == PageUndoAction::TransformSelection || 
            action.type == PageUndoAction::RemoveMultiple) {
            layer = page->layer(action.layerIndex);
        } else {
            layer = page->activeLayer();
        }
        if (!layer) return;
        
        switch (action.type) {
            case PageUndoAction::AddStroke:
                // Redo adding = add the stroke(s) again
                if (!action.addedStrokes.isEmpty()) {
                    // Multiple strokes (paste or transform)
                    for (const auto& s : action.addedStrokes) {
                        layer->addStroke(s);
                    }
                } else {
                    // Single stroke
                    layer->addStroke(action.stroke);
                }
                break;
                
            case PageUndoAction::RemoveStroke:
                // Redo removing = remove the stroke again
                layer->removeStroke(action.stroke.id);
                break;
                
            case PageUndoAction::RemoveMultiple:
                // Redo removing multiple = remove all strokes again
                for (const auto& s : action.strokes) {
                    layer->removeStroke(s.id);
                }
                break;
                
            case PageUndoAction::TransformSelection:
                // Redo transform: remove original strokes, add transformed strokes
                for (const auto& s : action.removedStrokes) {
                    layer->removeStroke(s.id);
                }
                for (const auto& s : action.addedStrokes) {
                    layer->addStroke(s);
                }
                break;
                
            default:
                break;  // Handled below
        }
        
        layer->invalidateStrokeCache();
    } else {
        // Object actions (Phase O2.7)
        switch (action.type) {
            case PageUndoAction::ObjectInsert:
                // Redo insert = re-add the object from JSON
                {
                    auto obj = InsertedObject::fromJson(action.objectData);
                    if (obj) {
                        qDebug() << "redo ObjectInsert: restored position =" << obj->position
                                 << "size =" << obj->size;
                        // Phase O2.C: Load any external assets (type-agnostic)
                        obj->loadAssets(m_document->bundlePath());
                        m_document->updateMaxObjectExtent(obj.get());
                        InsertedObject* rawPtr = obj.get();
                        page->addObject(std::move(obj));
                        qDebug() << "redo ObjectInsert: AFTER addObject, rawPtr->position =" << rawPtr->position;
                    }
                }
                break;
                
            case PageUndoAction::ObjectDelete:
                // Redo delete = remove the object again
                page->removeObject(action.objectId);
                m_document->recalculateMaxObjectExtent();
                break;
                
            case PageUndoAction::ObjectMove:
                // Redo move = apply new position
                {
                    InsertedObject* obj = page->objectById(action.objectId);
                    if (obj) {
                        obj->position = action.objectNewPosition;
                    }
                }
                break;
                
            case PageUndoAction::ObjectAffinityChange:
                // Redo affinity change = apply new affinity
                page->updateObjectAffinity(action.objectId, action.objectNewAffinity);
                break;
                
            // -----------------------------------------------------------------
            // ObjectResize (Phase O3.1.5, O3.1.8.3): Redo resize/rotate
            // Applies new position, size, and rotation.
            // -----------------------------------------------------------------
            case PageUndoAction::ObjectResize:
                {
                    InsertedObject* obj = page->objectById(action.objectId);
                    if (obj) {
                        obj->position = action.objectNewPosition;
                        obj->size = action.objectNewSize;
                        obj->rotation = action.objectNewRotation;  // O3.1.8.3
                        #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Redo ObjectResize (paged): obj" << action.objectId
                                 << "pos" << obj->position << "size" << obj->size
                                 << "rot" << obj->rotation;
                        #endif
                    }
                }
                break;
                
            default:
                break;
        }
    }
    
    // Mark page dirty for lazy save (BUG FIX: was missing)
    m_document->markPageDirty(pageIdx);
    
    // Push back to undo stack
    m_undoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    
    // M.7.3: Notify if objects were added/removed (for sidebar refresh)
    if (action.type == PageUndoAction::ObjectInsert ||
        action.type == PageUndoAction::ObjectDelete) {
        emit linkObjectListMayHaveChanged();
    }
    
    update();
}

bool DocumentViewport::canUndo() const
{
    // Edgeless mode uses global undo stack
    if (m_document && m_document->isEdgeless()) {
        return !m_edgelessUndoStack.isEmpty();
    }
    // Paged mode: per-page undo
    return m_undoStacks.contains(m_currentPageIndex) && 
           !m_undoStacks[m_currentPageIndex].isEmpty();
}

bool DocumentViewport::canRedo() const
{
    // Edgeless mode uses global redo stack
    if (m_document && m_document->isEdgeless()) {
        return !m_edgelessRedoStack.isEmpty();
    }
    // Paged mode: per-page redo
    return m_redoStacks.contains(m_currentPageIndex) && 
           !m_redoStacks[m_currentPageIndex].isEmpty();
}

// ===== Object Undo Helpers (Phase O2.7) =====

void DocumentViewport::pushObjectInsertUndo(InsertedObject* obj, int pageIndex,
                                            Document::TileCoord tileCoord)
{
    if (!obj) return;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pushObjectInsertUndo: obj->position =" << obj->position
             << "obj->size =" << obj->size
             << "obj->zOrder =" << obj->zOrder
             << "obj->layerAffinity =" << obj->layerAffinity;
    #endif
    if (m_document && m_document->isEdgeless()) {
        // Edgeless mode: use global stack
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectInsert;
        action.objectTileCoord = tileCoord;
        action.objectData = obj->toJson();
        action.objectId = obj->id;
        
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
        
        // Enforce max stack size
        while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
            m_edgelessUndoStack.removeFirst();
        }
    } else {
        // Paged mode: use per-page stack
        int idx = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
        
        PageUndoAction action;
        action.type = PageUndoAction::ObjectInsert;
        action.pageIndex = idx;
        action.objectData = obj->toJson();
        action.objectId = obj->id;
        
        m_undoStacks[idx].push(action);
        m_redoStacks[idx].clear();
    }
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
}

void DocumentViewport::pushObjectDeleteUndo(InsertedObject* obj, int pageIndex,
                                            Document::TileCoord tileCoord)
{
    if (!obj) return;
    
    if (m_document && m_document->isEdgeless()) {
        // Edgeless mode: use global stack
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectDelete;
        action.objectTileCoord = tileCoord;
        action.objectData = obj->toJson();
        action.objectId = obj->id;
        
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
        
        // Enforce max stack size
        while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
            m_edgelessUndoStack.removeFirst();
        }
    } else {
        // Paged mode: use per-page stack
        int idx = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
        
        PageUndoAction action;
        action.type = PageUndoAction::ObjectDelete;
        action.pageIndex = idx;
        action.objectData = obj->toJson();
        action.objectId = obj->id;
        
        m_undoStacks[idx].push(action);
        m_redoStacks[idx].clear();
    }
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
}

void DocumentViewport::pushObjectMoveUndo(InsertedObject* obj, const QPointF& oldPos,
                                          int pageIndex,
                                          Document::TileCoord oldTile,
                                          Document::TileCoord newTile)
{
    if (!obj) return;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pushObjectMoveUndo: obj" << obj->id << "oldPos =" << oldPos 
             << "newPos =" << obj->position;
    #endif
    if (m_document && m_document->isEdgeless()) {
        // Edgeless mode: use global stack
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectMove;
        action.objectId = obj->id;
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        action.objectOldTile = oldTile;
        action.objectNewTile = newTile;
        action.objectTileCoord = newTile;  // Current location
        
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
        
        // Enforce max stack size
        while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
            m_edgelessUndoStack.removeFirst();
        }
    } else {
        // Paged mode: use per-page stack
        int idx = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
        
        PageUndoAction action;
        action.type = PageUndoAction::ObjectMove;
        action.pageIndex = idx;
        action.objectId = obj->id;
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        
        m_undoStacks[idx].push(action);
        m_redoStacks[idx].clear();
    }
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
}

// -----------------------------------------------------------------------------
// pushObjectResizeUndo - Phase O3.1.5, extended O3.1.8.3
// Records object resize/rotate for undo/redo. Stores position, size, and rotation
// since resizing from corners can change position, and rotation is a transform.
// -----------------------------------------------------------------------------
void DocumentViewport::pushObjectResizeUndo(InsertedObject* obj, 
                                            const QPointF& oldPos, 
                                            const QSizeF& oldSize,
                                            qreal oldRotation)
{
    if (!obj) return;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pushObjectResizeUndo: obj" << obj->id 
             << "oldPos =" << oldPos << "newPos =" << obj->position
             << "oldSize =" << oldSize << "newSize =" << obj->size
             << "oldRot =" << oldRotation << "newRot =" << obj->rotation;
    #endif
    if (m_document && m_document->isEdgeless()) {
        // ===== Edgeless mode: use global stack =====
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectResize;
        action.objectId = obj->id;
        action.objectData = obj->toJson();  // Full snapshot for safety
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        action.objectOldSize = oldSize;
        action.objectNewSize = obj->size;
        action.objectOldRotation = oldRotation;        // Phase O3.1.8.3
        action.objectNewRotation = obj->rotation;      // Phase O3.1.8.3
        
        // Find which tile contains this object
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                action.objectTileCoord = coord;
                break;
            }
        }
        
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
        
        // Enforce max stack size
        while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
            m_edgelessUndoStack.removeFirst();
        }
    } else {
        // ===== Paged mode: use per-page stack =====
        PageUndoAction action;
        action.type = PageUndoAction::ObjectResize;
        action.pageIndex = m_currentPageIndex;
        action.objectId = obj->id;
        action.objectData = obj->toJson();  // Full snapshot for safety
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        action.objectOldSize = oldSize;
        action.objectNewSize = obj->size;
        action.objectOldRotation = oldRotation;        // Phase O3.1.8.3
        action.objectNewRotation = obj->rotation;      // Phase O3.1.8.3
        
        m_undoStacks[m_currentPageIndex].push(action);
        m_redoStacks[m_currentPageIndex].clear();
    }
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
}

// -----------------------------------------------------------------------------
// pushObjectAffinityUndo - Phase O3.5.3
// Records object affinity change for undo/redo.
// -----------------------------------------------------------------------------
void DocumentViewport::pushObjectAffinityUndo(InsertedObject* obj, int oldAffinity)
{
    if (!obj) return;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pushObjectAffinityUndo: obj" << obj->id 
             << "oldAffinity =" << oldAffinity 
             << "newAffinity =" << obj->getLayerAffinity();
    #endif
    if (m_document && m_document->isEdgeless()) {
        // ===== Edgeless mode: use global stack =====
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectAffinityChange;
        action.objectId = obj->id;
        action.objectOldAffinity = oldAffinity;
        action.objectNewAffinity = obj->getLayerAffinity();
        
        // Find which tile contains this object
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                action.objectTileCoord = coord;
                break;
            }
        }
        
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
        
        // Enforce max stack size
        while (m_edgelessUndoStack.size() > MAX_UNDO_EDGELESS) {
            m_edgelessUndoStack.removeFirst();
        }
    } else {
        // ===== Paged mode: use per-page stack =====
        PageUndoAction action;
        action.type = PageUndoAction::ObjectAffinityChange;
        action.pageIndex = m_currentPageIndex;
        action.objectId = obj->id;
        action.objectOldAffinity = oldAffinity;
        action.objectNewAffinity = obj->getLayerAffinity();
        
        m_undoStacks[m_currentPageIndex].push(action);
        m_redoStacks[m_currentPageIndex].clear();
    }
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
}

// -----------------------------------------------------------------------------
// findPageContainingObject - Phase O3.5.3
// Helper to find the Page (or tile) containing a given object.
// -----------------------------------------------------------------------------
Page* DocumentViewport::findPageContainingObject(InsertedObject* obj, Document::TileCoord* outTileCoord)
{
    if (!m_document || !obj) return nullptr;
    
    if (m_document->isEdgeless()) {
        // Search all loaded tiles
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                if (outTileCoord) *outTileCoord = coord;
                return tile;
            }
        }
        return nullptr;
    } else {
        // Paged mode: object should be on current page
        if (outTileCoord) *outTileCoord = {0, 0};
        return m_document->page(m_currentPageIndex);
    }
}

// -----------------------------------------------------------------------------
// getMaxAffinity - Phase O3.5.3
// Returns the maximum valid affinity value (layerCount - 1).
// -----------------------------------------------------------------------------
int DocumentViewport::getMaxAffinity() const
{
    if (!m_document) return 0;
    
    if (m_document->isEdgeless()) {
        return m_document->edgelessLayerCount() - 1;
    } else {
        Page* page = m_document->page(m_currentPageIndex);
        if (page) {
            return page->layerCount() - 1;
        }
        return 0;
    }
}

// ===== Benchmark (Task 2.6) =====

void DocumentViewport::startBenchmark()
{
    m_benchmarking = true;
    m_paintTimestamps.clear();
    m_benchmarkTimer.start();
    
    // Start periodic display updates (1000ms = 1 update/sec)
    m_benchmarkDisplayTimer.start(1000);
}

void DocumentViewport::stopBenchmark()
{
    m_benchmarking = false;
    m_benchmarkDisplayTimer.stop();
}

int DocumentViewport::getPaintRate() const
{
    if (!m_benchmarking) return 0;
    
    qint64 now = m_benchmarkTimer.elapsed();
    
    // Remove timestamps older than 1 second
    while (!m_paintTimestamps.empty() && now - m_paintTimestamps.front() > 1000) {
        m_paintTimestamps.pop_front();
    }
    
    return static_cast<int>(m_paintTimestamps.size());
}

// ===== Rendering Helpers (Task 1.3.3) =====

void DocumentViewport::renderPage(QPainter& painter, Page* page, int pageIndex)
{
    if (!page || !m_document) return;
    
    Q_UNUSED(pageIndex);  // Used for PDF page lookup via page->pdfPageNumber
    
    QSizeF pageSize = page->size;
    QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
    
    // 1. Fill with page background color
    painter.fillRect(pageRect, page->backgroundColor);
    
    // 2. Render background based on type
    switch (page->backgroundType) {
        case Page::BackgroundType::None:
            // Just the background color (already filled)
            break;
            
        case Page::BackgroundType::PDF:
            // Render PDF page from cache (Task 1.3.6)
            if (m_document->isPdfLoaded() && page->pdfPageNumber >= 0) {
                qreal dpi = effectivePdfDpi();
                QPixmap pdfPixmap = getCachedPdfPage(page->pdfPageNumber, dpi);
                
                if (!pdfPixmap.isNull()) {
                    // Scale pixmap to fit page rect
                    painter.drawPixmap(pageRect.toRect(), pdfPixmap);
                }
            }
            break;
            
        case Page::BackgroundType::Custom:
            // Draw custom background image
            if (!page->customBackground.isNull()) {
                painter.drawPixmap(pageRect.toRect(), page->customBackground);
            }
            break;
            
        case Page::BackgroundType::Grid:
            {
                // Draw grid lines
                painter.setPen(QPen(page->gridColor, 1.0 / m_zoomLevel));  // Constant line width
                qreal spacing = page->gridSpacing;
                
                // Vertical lines
                for (qreal x = spacing; x < pageSize.width(); x += spacing) {
                    painter.drawLine(QPointF(x, 0), QPointF(x, pageSize.height()));
                }
                
                // Horizontal lines
                for (qreal y = spacing; y < pageSize.height(); y += spacing) {
                    painter.drawLine(QPointF(0, y), QPointF(pageSize.width(), y));
                }
            }
            break;
            
        case Page::BackgroundType::Lines:
            {
                // Draw horizontal ruled lines
                painter.setPen(QPen(page->gridColor, 1.0 / m_zoomLevel));  // Constant line width
                qreal spacing = page->lineSpacing;
                
                for (qreal y = spacing; y < pageSize.height(); y += spacing) {
                    painter.drawLine(QPointF(0, y), QPointF(pageSize.width(), y));
                }
            }
            break;
    }
    
    // 3. Render objects with affinity = -1 (below all stroke layers)
    // This is for objects like pasted test paper images that should appear
    // underneath all strokes.
    // Phase O3.5.8: Objects with affinity -1 are tied to Layer 0, so check Layer 0 visibility
    VectorLayer* layer0 = page->layer(0);
    bool layer0Visible = layer0 && layer0->visible;
    
    // Phase O4.1: Prepare object exclude set for background snapshot capture
    QSet<QString> objectExcludeIds;
    if (m_skipSelectedObjectRendering) {
        for (InsertedObject* obj : m_selectedObjects) {
            if (obj) objectExcludeIds.insert(obj->id);
        }
    }
    const QSet<QString>* objectExcludePtr = objectExcludeIds.isEmpty() ? nullptr : &objectExcludeIds;
    
    page->renderObjectsWithAffinity(painter, 1.0, -1, layer0Visible, objectExcludePtr);
    
    // 4. Render vector layers with ZOOM-AWARE stroke cache, interleaved with objects
    // The cache is built at pageSize * zoom * dpr physical pixels, ensuring
    // sharp rendering at any zoom level. The cache's devicePixelRatio is set
    // to zoom * dpr, so Qt handles coordinate mapping correctly.
    painter.setRenderHint(QPainter::Antialiasing, true);
    qreal dpr = devicePixelRatioF();
    
    // CR-2B-7: Check if this page has selected strokes that should be excluded
    bool hasSelectionOnThisPage = m_lassoSelection.isValid() && 
                                   m_lassoSelection.sourcePageIndex == pageIndex;
    QSet<QString> excludeIds;
    if (hasSelectionOnThisPage) {
        excludeIds = m_lassoSelection.getSelectedIds();
    }
    
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        bool layerIsVisible = layer && layer->visible;
        
        if (layerIsVisible) {
            // CR-2B-7: If this layer contains selected strokes, render with exclusion
            // to hide originals (they'll be rendered transformed in renderLassoSelection)
            if (hasSelectionOnThisPage && layerIdx == m_lassoSelection.sourceLayerIndex) {
                // Render manually, skipping selected strokes (bypasses cache)
                painter.save();
                // painter.scale(m_zoomLevel, m_zoomLevel);
                layer->renderExcluding(painter, excludeIds);
                painter.restore();
            } else {
                // Use zoom-aware cache for maximum performance
                // The painter is scaled by zoom, cache is at zoom * dpr resolution
                layer->renderWithZoomCache(painter, pageSize, m_zoomLevel, dpr);
            }
        }
        
        // Phase O3.5.8: Render objects with affinity = layerIdx
        // Objects with affinity K are tied to Layer K+1, so check visibility of Layer K+1
        VectorLayer* nextLayer = page->layer(layerIdx + 1);
        bool nextLayerVisible = nextLayer ? nextLayer->visible : true;  // If no next layer, show objects
        page->renderObjectsWithAffinity(painter, 1.0, layerIdx, nextLayerVisible, objectExcludePtr);
    }
    
    // 5. Render text selection overlay (Phase A: Highlighter tool)
    if (m_currentTool == ToolType::Highlighter) {
        renderTextSelectionOverlay(painter, pageIndex);
    }
    
    // 5b. Render PDF search match highlights
    renderSearchMatchesOverlay(painter, pageIndex);
    
    // 6. Draw page border (optional, for visual separation)
    // CUSTOMIZABLE: Page border color (theme setting)
    // The border does not need to be redrawn every time the page is rendered. 
    painter.setPen(QPen(QColor(180, 180, 180), 1.0 / m_zoomLevel));  // Light gray border
    painter.drawRect(pageRect);
}

// ===== Edgeless Mode Rendering (Phase E2) =====

void DocumentViewport::renderEdgelessMode(QPainter& painter)
{
    if (!m_document || !m_document->isEdgeless()) return;
    
    // Get visible rect in document coordinates
    QRectF viewRect = visibleRect();
    
    // ========== TILE RENDERING STRATEGY ==========
    // With stroke splitting, cross-tile strokes are stored as separate segments in each tile.
    // Each segment is rendered when its tile is rendered - no margin needed for cross-tile!
    // Small margin handles thick strokes extending slightly beyond tile boundary.
    // CR-9: STROKE_MARGIN is max expected stroke width + anti-aliasing buffer
    constexpr int STROKE_MARGIN = 100;
    
    // Phase O1.5: Object margin - objects can extend beyond tile boundaries
    // Calculate extra margin based on largest object in document
    int objectMargin = m_document->maxObjectExtent();
    
    // Total margin is max of stroke margin and object margin
    int totalMargin = qMax(STROKE_MARGIN, objectMargin);
    
    // CR-5: Single tilesInRect() call - use total margin for all tiles
    // Background pass will filter to viewRect bounds
    QRectF strokeRect = viewRect.adjusted(-totalMargin, -totalMargin, totalMargin, totalMargin);
    QVector<Document::TileCoord> allTiles = m_document->tilesInRect(strokeRect);
    
    // Pre-calculate visible tile range for background filtering
    int tileSize = Document::EDGELESS_TILE_SIZE;
    int minVisibleTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxVisibleTx = static_cast<int>(std::floor(viewRect.right() / tileSize));
    int minVisibleTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxVisibleTy = static_cast<int>(std::floor(viewRect.bottom() / tileSize));
    
    // Apply view transform (same as paged mode)
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // ========== PASS 1: Render backgrounds for VISIBLE tiles only ==========
    // This ensures non-blank canvas without wasting time on off-screen tiles.
    // For 1920x1080 viewport with 1024x1024 tiles: up to 9 tiles (3x3 worst case)
    // 
    // Uses Page::renderBackgroundPattern() to share grid/lines logic with Page::renderBackground().
    // Empty tile coordinates use document defaults; existing tiles use their own settings.
    for (const auto& coord : allTiles) {
        // CR-5: Skip tiles outside visible rect (margin tiles are for strokes only)
        if (coord.first < minVisibleTx || coord.first > maxVisibleTx ||
            coord.second < minVisibleTy || coord.second > maxVisibleTy) {
            continue;
        }
        
        QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
        QRectF tileRect(tileOrigin.x(), tileOrigin.y(), tileSize, tileSize);
        
        // Check if tile exists - use its settings, otherwise use document defaults
        Page* tile = m_document->getTile(coord.first, coord.second);
        
        if (tile) {
            // Existing tile: use its background settings
            Page::renderBackgroundPattern(
                painter,
                tileRect,
                tile->backgroundColor,
                tile->backgroundType,
                tile->gridColor,
                tile->gridSpacing,
                tile->lineSpacing,
                1.0 / m_zoomLevel  // Constant pen width in screen pixels
            );
        } else {
            // Empty tile coordinate: use document defaults
            Page::renderBackgroundPattern(
                painter,
                tileRect,
                m_document->defaultBackgroundColor,
                m_document->defaultBackgroundType,
                m_document->defaultGridColor,
                m_document->defaultGridSpacing,
                m_document->defaultLineSpacing,
                1.0 / m_zoomLevel  // Constant pen width in screen pixels
            );
        }
    }
    
    // ========== PASS 2: Render objects with default affinity (-1) ==========
    // These render BELOW all stroke layers (e.g., background images, pasted test papers)
    renderEdgelessObjectsWithAffinity(painter, -1, allTiles);
    
    // ========== PASS 3: Interleaved layer strokes and objects ==========
    // For each layer index, render strokes from all tiles, then objects with that affinity.
    // This ensures correct z-order: Layer 0 strokes → Affinity 0 objects → Layer 1 strokes → ...
    
    // First, determine the maximum layer count across all visible tiles
    int maxLayerCount = 0;
    for (const auto& coord : allTiles) {
        Page* tile = m_document->getTile(coord.first, coord.second);
        if (tile) {
            maxLayerCount = qMax(maxLayerCount, tile->layerCount());
        }
    }
    
    // Render layers interleaved with objects
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int layerIdx = 0; layerIdx < maxLayerCount; ++layerIdx) {
        // PASS 3a: Render this layer's strokes from all tiles
        for (const auto& coord : allTiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
            
            painter.save();
            painter.translate(tileOrigin);
            renderTileLayerStrokes(painter, tile, layerIdx);
            painter.restore();
        }
        
        // PASS 3b: Render objects with affinity = layerIdx
        renderEdgelessObjectsWithAffinity(painter, layerIdx, allTiles);
    }
    
    // Draw tile boundary grid (debug)
    if (m_showTileBoundaries) {
        drawTileBoundaries(painter, viewRect);
    }
    
    painter.restore();
    
    // Render current stroke with incremental caching
    if (m_isDrawing && !m_currentStroke.points.isEmpty() && m_activeDrawingPage >= 0) {
        renderCurrentStrokeIncremental(painter);
    }
    
    // Task 2.9: Draw straight line preview (edgeless mode)
    if (m_isDrawingStraightLine) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Edgeless: coordinates are in document space
        QPointF vpStart = documentToViewport(m_straightLineStart);
        QPointF vpEnd = documentToViewport(m_straightLinePreviewEnd);
        
        // Use current tool's color and thickness
        QColor previewColor = (m_currentTool == ToolType::Marker) 
                              ? m_markerColor : m_penColor;
        qreal previewThickness = (m_currentTool == ToolType::Marker)
                                 ? m_markerThickness : m_penThickness;
        
        QPen pen(previewColor, previewThickness * m_zoomLevel, 
                 Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(vpStart, vpEnd);
        
        painter.restore();
    }
    
    // Task 2.10: Draw lasso selection path (edgeless mode)
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if (m_isDrawingLasso && m_lassoPath.size() > 1) {
        renderLassoPathIncremental(painter);
    }
    
    // Task 2.10.3: Draw lasso selection (edgeless mode)
    // P5: Skip during background snapshot capture
    if (m_lassoSelection.isValid() && !m_skipSelectionRendering) {
        renderLassoSelection(painter);
    }
    
    // Phase O2: Draw object selection (edgeless mode)
    // Phase O4.1: Skip during background snapshot capture
    if ((m_currentTool == ToolType::ObjectSelect || !m_selectedObjects.isEmpty())
        && !m_skipSelectedObjectRendering) {
        renderObjectSelection(painter);
    }
}

// NOTE: renderTile() was removed (CR-2) - it was dead code duplicating 
// renderEdgelessMode() + renderTileStrokes()

void DocumentViewport::renderTileStrokes(QPainter& painter, Page* tile, Document::TileCoord coord)
{
    if (!tile) return;
    
    QSizeF tileSize = tile->size;
    
    // Render only vector layers (strokes may extend beyond tile bounds - OK!)
    painter.setRenderHint(QPainter::Antialiasing, true);
    qreal dpr = devicePixelRatioF();
    
    // CR-2B-7: Check if this tile has selected strokes that should be excluded
    // Note: In edgeless mode, selected strokes are stored in document coordinates,
    // but they originated from specific tiles. We check by ID across all tiles
    // since a selection might span multiple tiles.
    QSet<QString> excludeIds;
    if (m_lassoSelection.isValid()) {
        excludeIds = m_lassoSelection.getSelectedIds();
    }
    
    for (int layerIdx = 0; layerIdx < tile->layerCount(); ++layerIdx) {
        VectorLayer* layer = tile->layer(layerIdx);
        if (layer && layer->visible) {
            // CR-2B-7: If there's a selection on the active layer, exclude selected strokes
            if (!excludeIds.isEmpty() && layerIdx == m_edgelessActiveLayerIndex) {
                // Render manually, skipping selected strokes
                // Note: painter is already in tile-local coordinates
                layer->renderExcluding(painter, excludeIds);
            } else {
                layer->renderWithZoomCache(painter, tileSize, m_zoomLevel, dpr);
            }
        }
    }
    
    // NOTE: Objects are now rendered via renderEdgelessObjectsWithAffinity()
    // in the multi-pass rendering loop, not here.
    // tile->renderObjects(painter, 1.0);  // REMOVED - handled by multi-pass
}

void DocumentViewport::renderTileLayerStrokes(QPainter& painter, Page* tile, int layerIdx)
{
    if (!tile) return;
    if (layerIdx < 0 || layerIdx >= tile->layerCount()) return;
    
    VectorLayer* layer = tile->layer(layerIdx);
    if (!layer || !layer->visible) return;
    
    QSizeF tileSize = tile->size;
    qreal dpr = devicePixelRatioF();
    
    // CR-2B-7: Check if this layer has selected strokes that should be excluded
    QSet<QString> excludeIds;
    if (m_lassoSelection.isValid()) {
        excludeIds = m_lassoSelection.getSelectedIds();
    }
    
    // CR-2B-7: If there's a selection on the active layer, exclude selected strokes
    if (!excludeIds.isEmpty() && layerIdx == m_edgelessActiveLayerIndex) {
        // Render manually, skipping selected strokes
        layer->renderExcluding(painter, excludeIds);
    } else {
        layer->renderWithZoomCache(painter, tileSize, m_zoomLevel, dpr);
    }
}

/**
 * @brief Render objects with a specific layer affinity across all tiles.
 * 
 * IMPORTANT (BF.4): Objects store position in tile-local coordinates.
 * The render() function internally applies obj->position, so we must ONLY
 * translate the painter to the tile origin, NOT to (tileOrigin + obj->position).
 * Otherwise position gets applied twice, causing objects to appear at 2× distance.
 * 
 * Compare with paged mode: Page::renderObjectsWithAffinity() doesn't translate
 * at all because objects are already in page-local coords and render() handles it.
 */
void DocumentViewport::renderEdgelessObjectsWithAffinity(
    QPainter& painter, int affinity, const QVector<Document::TileCoord>& allTiles)
{
    if (!m_document) return;
    
    // Phase O3.5.8: Check if the tied layer is visible
    // Objects with affinity = K are tied to Layer K+1
    // Special case: affinity = -1 is tied to Layer 0
    int tiedLayerIndex = affinity + 1;
    const auto& layers = m_document->edgelessLayers();
    
    if (tiedLayerIndex >= 0 && tiedLayerIndex < static_cast<int>(layers.size())) {
        if (!layers[tiedLayerIndex].visible) {
            return;  // Layer is hidden, don't render its tied objects
        }
    }
    // If tiedLayerIndex is out of range (no such layer), show objects by default
    
    int tileSize = Document::EDGELESS_TILE_SIZE;
    QRectF viewRect = visibleRect();
    
    // Iterate all loaded tiles and render objects with matching affinity
    for (const auto& coord : allTiles) {
        Page* tile = m_document->getTile(coord.first, coord.second);
        if (!tile) continue;
        
        // Check if this tile has objects with this affinity
        auto it = tile->objectsByAffinity.find(affinity);
        if (it == tile->objectsByAffinity.end() || it->second.empty()) {
            continue;
        }
        
        // Calculate tile origin in document coordinates
        QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
        
        // Sort objects by zOrder within this affinity group
        std::vector<InsertedObject*> objs = it->second;
        std::sort(objs.begin(), objs.end(),
                  [](InsertedObject* a, InsertedObject* b) {
                      return a->zOrder < b->zOrder;
                  });
        
        // Render each object
        for (InsertedObject* obj : objs) {
            if (!obj->visible) continue;
            
            // Phase O4.1: Skip selected objects during background snapshot capture
            if (m_skipSelectedObjectRendering && m_selectedObjects.contains(obj)) {
                continue;
            }
            
            // Convert tile-local position to document coordinates for visibility check
            QPointF docPos = tileOrigin + obj->position;
            QRectF objRect(docPos, obj->size);
            
            // Skip if object doesn't intersect visible area (with some margin)
            if (!objRect.intersects(viewRect.adjusted(-200, -200, 200, 200))) {
                continue;
            }
            
            // BF.4: Only translate to tile origin, NOT to docPos.
            // The object's render() function already applies obj->position internally.
            // If we translate to docPos AND render applies position, position gets doubled!
            painter.save();
            painter.translate(tileOrigin);
            obj->render(painter, 1.0);  // render() will add obj->position
            painter.restore();
        }
    }
}

void DocumentViewport::drawTileBoundaries(QPainter& painter, QRectF viewRect)
{
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Calculate visible tile range
    int minTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxTx = static_cast<int>(std::ceil(viewRect.right() / tileSize));
    int minTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxTy = static_cast<int>(std::ceil(viewRect.bottom() / tileSize));
    
    // Semi-transparent dashed lines
    painter.setPen(QPen(QColor(100, 100, 100, 100), 1.0 / m_zoomLevel, Qt::DashLine));
    
    // Vertical lines
    for (int tx = minTx; tx <= maxTx; ++tx) {
        qreal x = tx * tileSize;
        painter.drawLine(QPointF(x, viewRect.top()), QPointF(x, viewRect.bottom()));
    }
    
    // Horizontal lines
    for (int ty = minTy; ty <= maxTy; ++ty) {
        qreal y = ty * tileSize;
        painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
    }
    
    // Draw origin marker (tile 0,0 corner)
    QPointF origin(0, 0);
    if (viewRect.contains(origin)) {
        painter.setPen(QPen(QColor(255, 100, 100), 2.0 / m_zoomLevel));
        painter.drawLine(QPointF(-20 / m_zoomLevel, 0), QPointF(20 / m_zoomLevel, 0));
        painter.drawLine(QPointF(0, -20 / m_zoomLevel), QPointF(0, 20 / m_zoomLevel));
    }
}

qreal DocumentViewport::minZoomForEdgeless() const
{
    // ========== EDGELESS MIN ZOOM CALCULATION ==========
    // With 1024x1024 tiles, a 1920x1080 viewport can show up to:
    //   - Best case (aligned): 2x2 = 4 tiles
    //   - Worst case (straddling): 3x3 = 9 tiles
    //
    // This limit prevents zooming out so far that too many tiles are visible.
    // We allow ~2 tiles worth of document space per viewport dimension.
    // At worst case (pan straddling tile boundaries), this means up to 9 tiles.
    //
    // Memory: 9 tiles × ~4MB each = ~36MB stroke cache at zoom 1.0, DPR 1.0
    
    constexpr qreal maxVisibleSize = 2.0 * Document::EDGELESS_TILE_SIZE;  // 2048
    
    // Use logical pixels (Qt handles DPI automatically)
    qreal minZoomX = static_cast<qreal>(width()) / maxVisibleSize;
    qreal minZoomY = static_cast<qreal>(height()) / maxVisibleSize;
    
    // Take the larger (more restrictive) value, with 10% floor
    return qMax(qMax(minZoomX, minZoomY), 0.1);
}

qreal DocumentViewport::effectivePdfDpi() const
{
    // Base DPI for 100% zoom on a 1x DPR screen
    constexpr qreal baseDpi = 96.0;
    
    // Get device pixel ratio for high DPI support
    // This handles Retina displays, Windows scaling (125%, 150%, 200%), etc.
    // Qt caches this value internally, so calling it is very fast
    qreal dpr = devicePixelRatioF();
    
    // Scale DPI with zoom level AND device pixel ratio for crisp rendering
    // At zoom > 1.0, we want higher DPI to avoid pixelation
    // At zoom < 1.0, we can use lower DPI to save memory/time
    // On high DPI screens, we need extra resolution to match physical pixels
    // 
    // Example: 200% Windows scaling (dpr=2.0) at zoom 1.0 → 192 DPI
    // Example: 100% scaling (dpr=1.0) at zoom 2.0 → 192 DPI
    qreal scaledDpi = baseDpi * m_zoomLevel * dpr;
    
    // Cap at reasonable maximum (300 DPI is print quality)
    // This prevents excessive memory usage at very high zoom on high DPI screens
    return qMin(scaledDpi, 300.0);
}

// ===== Private Methods =====

void DocumentViewport::clampPanOffset()
{
    if (!m_document) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    // For edgeless documents, allow unlimited pan (infinite canvas)
    if (m_document->isEdgeless()) {
        // No clamping for edgeless - user can pan anywhere
        return;
    }
    
    // Paged mode: require at least one page
    if (m_document->pageCount() == 0) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    QSizeF contentSize = totalContentSize();
    qreal viewWidth = width() / m_zoomLevel;
    qreal viewHeight = height() / m_zoomLevel;
    
    // Allow some overscroll (50% of viewport)
    qreal overscrollX = viewWidth * 0.5;
    qreal overscrollY = viewHeight * 0.5;
    
    // Minimum pan: allow some overscroll at start
    qreal minX = -overscrollX;
    qreal minY = -overscrollY;
    
    // Maximum pan: can scroll to show end of content
    // If content is smaller than viewport, center it
    qreal maxX = qMax(0.0, contentSize.width() - viewWidth + overscrollX);
    qreal maxY = qMax(0.0, contentSize.height() - viewHeight + overscrollY);
    
    m_panOffset.setX(qBound(minX, m_panOffset.x(), maxX));
    m_panOffset.setY(qBound(minY, m_panOffset.y(), maxY));
}

void DocumentViewport::updateCurrentPageIndex()
{
    if (!m_document || m_document->pageCount() == 0) {
        m_currentPageIndex = 0;
        return;
    }
    
    // For edgeless documents, always page 0
    if (m_document->isEdgeless()) {
        m_currentPageIndex = 0;
        return;
    }
    
    int oldIndex = m_currentPageIndex;
    
    // Find the page that is most visible (has most area in viewport center)
    QRectF viewRect = visibleRect();
    QPointF viewCenter = viewRect.center();
    
    // First, try to find which page contains the viewport center
    int centerPage = pageAtPoint(viewCenter);
    if (centerPage >= 0) {
        m_currentPageIndex = centerPage;
    } else {
        // No page at center (likely in a gap) - find the closest page
        QVector<int> visible = visiblePages();
        if (!visible.isEmpty()) {
            if (m_layoutMode == LayoutMode::TwoColumn && visible.size() >= 2) {
                // In 2-column mode, when center is in the gap between columns,
                // find the visible page whose center is closest to viewport center
                qreal minDist = std::numeric_limits<qreal>::max();
                int bestPage = visible.first();
                
                for (int pageIdx : visible) {
                    QRectF rect = pageRect(pageIdx);
                    // Distance from viewport center to page center
                    QPointF pageCenter = rect.center();
                    qreal dist = QLineF(viewCenter, pageCenter).length();
                    if (dist < minDist) {
                        minDist = dist;
                        bestPage = pageIdx;
                    }
                }
                m_currentPageIndex = bestPage;
            } else {
                // Single column mode or only one visible page
                m_currentPageIndex = visible.first();
            }
        } else {
            // No visible pages - estimate based on scroll position using binary search
            // PERF FIX: Use cached Y positions for O(log n) lookup instead of O(n)
            ensurePageLayoutCache();
            int pageCount = m_document->pageCount();
            
            if (m_layoutMode == LayoutMode::SingleColumn && !m_pageYCache.isEmpty()) {
                // Binary search to find page closest to viewport center Y
                qreal targetY = viewCenter.y();
                int low = 0;
                int high = pageCount - 1;
            int closestPage = 0;
            
                while (low <= high) {
                    int mid = (low + high) / 2;
                    qreal pageY = m_pageYCache[mid];
                    QSizeF pageSize = m_document->pageSizeAt(mid);
                    qreal pageCenterY = pageY + pageSize.height() / 2.0;
                    
                    if (pageCenterY < targetY) {
                        closestPage = mid;  // This page or later
                        low = mid + 1;
                    } else {
                        high = mid - 1;
                    }
                }
                
                // Check neighboring pages to find the actual closest
                qreal minDist = std::numeric_limits<qreal>::max();
                for (int i = qMax(0, closestPage - 1); i <= qMin(pageCount - 1, closestPage + 1); ++i) {
                QRectF rect = pageRect(i);
                    qreal dist = qAbs(rect.center().y() - viewCenter.y());
                if (dist < minDist) {
                    minDist = dist;
                        m_currentPageIndex = i;
                }
            }
            } else {
                // Two-column fallback: just pick the first page (rare edge case)
                m_currentPageIndex = 0;
            }
        }
    }
    
    if (m_currentPageIndex != oldIndex) {
        emit currentPageChanged(m_currentPageIndex);
        // Undo/redo availability may change when page changes
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
        
        // Update cursor if Highlighter tool is active (may toggle enabled/disabled)
        if (m_currentTool == ToolType::Highlighter) {
            updateHighlighterCursor();
        }
    }
}

void DocumentViewport::emitScrollFractions()
{
    if (!m_document || m_document->pageCount() == 0) {
        emit horizontalScrollChanged(0.0);
        emit verticalScrollChanged(0.0);
        return;
    }
    
    QSizeF contentSize = totalContentSize();
    qreal viewportWidth = width() / m_zoomLevel;
    qreal viewportHeight = height() / m_zoomLevel;
    
    // Calculate horizontal scroll fraction
    qreal scrollableWidth = contentSize.width() - viewportWidth;
    qreal hFraction = 0.0;
    if (scrollableWidth > 0) {
        hFraction = qBound(0.0, m_panOffset.x() / scrollableWidth, 1.0);
    }
    
    // Calculate vertical scroll fraction
    qreal scrollableHeight = contentSize.height() - viewportHeight;
    qreal vFraction = 0.0;
    if (scrollableHeight > 0) {
        vFraction = qBound(0.0, m_panOffset.y() / scrollableHeight, 1.0);
    }
    
    emit horizontalScrollChanged(hFraction);
    emit verticalScrollChanged(vFraction);
}
