// ============================================================================
// DocumentViewport - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.1)
// ============================================================================

#include "DocumentViewport.h"
#include "DarkModeUtils.h"
#include "ObjectConstraints.h"      // Page containment for inserted objects
#include "TouchGestureHandler.h"
// Note: ShortcutManager.h no longer needed here - all shortcuts handled by MainWindow
#include "MarkdownNote.h"           // Phase M.2: For markdown note creation
#include "../layers/VectorLayer.h"
#include "../pdf/PdfProvider.h"     // Use abstract interface, not concrete impl
#include "../objects/ImageObject.h"
#include "../objects/LinkObject.h"  // Phase C.2.3: For cloneWithBackLink
#include "../objects/OcrTextObject.h"  // Phase 1D: OCR text object deletion
#include "../objects/TextBoxObject.h"
#include "../ui/banners/MissingPdfBanner.h"  // Phase R.3: Missing PDF notification
#include "../ui/panels/InlineTextBoxEditor.h"
#include "../ui/panels/TextBoxFormatBar.h"
#include "../../markdown/qmarkdowntextedit.h"

#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTouchEvent>
#include <QNativeGestureEvent>  // macOS trackpad pinch-to-zoom
#include "../compat/qt_compat.h"  // Qt5/Qt6 input device shims
#include <QtMath>     // For qPow
#include <QtConcurrent>   // For async PDF rendering
#include <QThreadStorage> // For thread-local PDF provider caching
#include <cmath>      // For std::floor, std::ceil
#include <algorithm>  // For std::remove_if
#include <vector>     // DIAGNOSTIC: runRasterBenchmark() sample buffers
#include <cstring>    // DIAGNOSTIC: runRasterBenchmark() memcpy baseline
#include <QSysInfo>   // DIAGNOSTIC: runRasterBenchmark() architecture readout
#include <limits>
#include <climits>    // For INT_MIN (Phase O3.5.5: affinity filtering)
#include <set>        // For touched-container tracking (Phase M.9)
#include <QDateTime>  // For timestamp
#include <QUuid>      // For stroke IDs
#include <QSet>       // For efficient ID lookup in eraseAt
#include <QClipboard>     // For clipboard access (O2.4)
#include <QGuiApplication> // For clipboard access (O2.4)
#include <QScreen>         // For refresh rate in the perf HUD context
#include <QApplication>    // For focusWidget() - text input focus check
#include <QLineEdit>       // For text input focus check
#include <QTextEdit>       // For text input focus check
#include <QPlainTextEdit>  // For text input focus check
#include <QSignalBlocker>
#include <QElapsedTimer>  // For double/triple click detection (Phase A)
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>      // For clipboard content type check (O2.4)
#include <QDragEnterEvent> // Plan D2: cross-document page-transfer drops
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include "PageTransferMime.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>       // BUG-A008: JNI for eraser tool type detection
#include <QJniEnvironment>  // BUG-A008: For cached JNI method calls

// File-scope JNI cache for eraser detection (BUG-A008).
// Initialized once via initEraserJni(), called during stylus hover
// so the expensive FindClass/GetStaticMethodID doesn't hit pen-down latency.
static jclass s_eraserActivityClass = nullptr;
static jmethodID s_eraserIsEraserMethod = nullptr;
static bool s_eraserJniInitialized = false;

static void initEraserJni()
{
    if (s_eraserJniInitialized) return;
    s_eraserJniInitialized = true;
    
    QJniEnvironment env;
    jclass localClass = env->FindClass("org/speedynote/app/SpeedyNoteActivity");
    if (localClass) {
        s_eraserActivityClass = static_cast<jclass>(env->NewGlobalRef(localClass));
        s_eraserIsEraserMethod = env->GetStaticMethodID(
            s_eraserActivityClass, "isEraserToolActive", "()Z");
        env->DeleteLocalRef(localClass);
    }
}
#endif
#include <QFileDialog>    // For insertImageFromDialog (Phase C.0.5)
#include <QDesktopServices>  // For opening URLs (Phase C.4.3)
#include <QUrl>              // For URL handling (Phase C.4.3)
#include <QTextDocument>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QMenu>             // For addLinkToSlot menu (Phase C.5.3 - temporary)
#include "../ui/ThemeColors.h"
#include <QInputDialog>      // For URL input dialog (Phase C.5.3 - temporary)

// ===== Constants =====

// PDF uses 72 DPI, Page uses 96 DPI - scale factor for coordinate conversion
static constexpr qreal PDF_TO_PAGE_SCALE = 96.0 / 72.0;  // PDF coords → Page coords
static constexpr qreal PAGE_TO_PDF_SCALE = 72.0 / 96.0;  // Page coords → PDF coords

// Note: eventMatchesAction() helper was removed - all keyboard shortcuts
// are now handled by MainWindow's QShortcut system for focus-independent operation.

// Static clipboard storage shared across all DocumentViewport instances
DocumentViewport::StrokeClipboard DocumentViewport::s_clipboard;
QList<QJsonObject> DocumentViewport::s_objectClipboard;
QMap<QString, DocumentViewport::ClipboardImageAsset>
    DocumentViewport::s_objectClipboardAssets;

// ===== Thread-Local PDF Provider Cache =====
// 
// Each thread in the QThreadPool keeps its own cached PdfProvider to avoid
// re-opening and parsing the PDF file for every page render. This significantly
// improves scrolling performance for large PDFs.
//
// Cache entry: stores the PDF path and the provider instance.
// When the path changes (different document), the old provider is released
// and a new one is created for the new document.

struct ThreadPdfCache {
    QString pdfPath;
    std::unique_ptr<PdfProvider> provider;
    
    PdfProvider* getOrCreate(const QString& path) {
        if (pdfPath != path || !provider || !provider->isValid()) {
            // Different file or invalid provider - create new one
            pdfPath = path;
            provider = PdfProvider::create(path);
        }
        return provider.get();
    }
    
    void clear() {
        pdfPath.clear();
        provider.reset();
    }
};

// Thread-local storage: each worker thread in QThreadPool gets its own cache
static QThreadStorage<ThreadPdfCache> s_threadPdfCache;

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
    
    // Plan D2: accept cross-document page-transfer drops onto the canvas.
    setAcceptDrops(true);
    
    // Set focus policy for keyboard shortcuts
    setFocusPolicy(Qt::StrongFocus);
    
    // This widget is a drawing canvas, not a text input field.
    // Explicitly disable input method so that setFocus() on Android doesn't
    // ping the InputMethodManager (which adds ~50-100ms latency on slow devices).
    setAttribute(Qt::WA_InputMethodEnabled, false);
    
    // Performance: we paint the entire widget ourselves (background + pages), so tell Qt
    // not to auto-erase before paintEvent. This eliminates a redundant full-screen fill
    // per frame, which matters on Android where each pixel write goes through an extra
    // buffer copy to the Surface.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    
    // PDF preload timer - debounces preload requests during rapid scrolling
    m_pdfPreloadTimer = new QTimer(this);
    m_pdfPreloadTimer->setSingleShot(true);
    connect(m_pdfPreloadTimer, &QTimer::timeout, this, &DocumentViewport::doAsyncPdfPreload);

    m_objectGeometryFeedbackTimer = new QTimer(this);
    m_objectGeometryFeedbackTimer->setSingleShot(true);
    m_objectGeometryFeedbackTimer->setInterval(900);
    connect(m_objectGeometryFeedbackTimer, &QTimer::timeout, this, [this]() {
        const QRect dirty = m_objectGeometryFeedbackAnchor
            .adjusted(-16.0, -16.0, 260.0, 80.0).toAlignedRect();
        m_objectGeometryFeedbackText.clear();
        m_objectGeometryFeedbackAnchor = QRectF();
        update(dirty);
    });
    connect(this, &DocumentViewport::zoomChanged, this,
            [this]() {
        updateInlineTextEditorGeometry();
        updateTextBoxFormatBarGeometry();
    });
    connect(this, &DocumentViewport::panChanged, this,
            [this]() {
        updateInlineTextEditorGeometry();
        updateTextBoxFormatBarGeometry();
    });
    connect(this, &DocumentViewport::objectSelectionChanged,
            this, &DocumentViewport::syncTextBoxFormatBar);
    connect(this, &DocumentViewport::textBoxLayoutCommitted,
            this, &DocumentViewport::syncTextBoxFormatBar);

    // Scroll-settle timer (SP1) - defers heavy housekeeping (preload/evict) until
    // the immediate-pan route (wheel/touchpad/scroll-bar) stops for a beat.
    m_scrollSettleTimer = new QTimer(this);
    m_scrollSettleTimer->setSingleShot(true);
    m_scrollSettleTimer->setInterval(SCROLL_SETTLE_MS);
    connect(m_scrollSettleTimer, &QTimer::timeout, this, &DocumentViewport::onScrollSettled);
    
    // Gesture timeout timer - fallback for detecting gesture end (zoom or pan)
    m_gestureTimeoutTimer = new QTimer(this);
    m_gestureTimeoutTimer->setSingleShot(true);
    connect(m_gestureTimeoutTimer, &QTimer::timeout, this, &DocumentViewport::onGestureTimeout);
    
    // Touch gesture handler (encapsulates pan/zoom/tap logic)
    m_touchHandler = new TouchGestureHandler(this, this);
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Handle app suspend/resume (screen lock, home button, etc.)
    // Resets touch state when app returns to foreground to fix gesture reliability
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &DocumentViewport::onApplicationStateChanged);
#endif
    
    // Focus-cache rebuild debounce. When the user is panning or zooming we
    // skip Focus tier (cache-free Direct render) so we don't rebuild the
    // viewport-clipped pixmap every frame. After 150 ms of stillness we
    // clear the suspend flag and trigger one more paint that lets the focus
    // cache build (and stays built across subsequent stationary paints).
    m_focusRebuildTimer = new QTimer(this);
    m_focusRebuildTimer->setSingleShot(true);
    connect(m_focusRebuildTimer, &QTimer::timeout, this, [this]() {
        m_focusCacheSuspended = false;
        update();
    });

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
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    commitInlineTextEdit();

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

    // Stop focus-cache rebuild debounce (prevents lambda from firing during
    // destruction and dereferencing this).
    if (m_focusRebuildTimer) {
        m_focusRebuildTimer->stop();
    }
    
    // Stop touch handler gestures (including inertia timer)
    // Must happen before m_gesture.reset() to avoid accessing stale gesture state
    if (m_touchHandler) {
        m_touchHandler->setMode(TouchGestureMode::Disabled);
    }
    
    // Wait for and clean up any active async PDF watchers.
    // Must happen before clearing caches or m_document pointer, since the
    // finished-signal handlers access m_activePdfWatchers and m_pdfCacheMutex.
    cancelAndWaitForBackgroundThreads();
    
    // Clear gesture cached frame (releases memory)
    m_gesture.reset();
    
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
    m_undoStack.clear();
    m_redoStack.clear();
    
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

    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    commitInlineTextEdit();
    
    // End any active gesture (cached frame is from old document)
    if (m_gesture.isActive()) {
        m_gesture.reset();
        m_gestureTimeoutTimer->stop();
    }
    m_backtickHeld = false;  // Reset key tracking for new document
    
    // Cancel live object previews while their source objects are still valid,
    // then clear selection before changing m_document to avoid dangling access.
    cancelObjectPointerGesture();
    bool hadSelection = !m_selectedObjects.isEmpty();
    m_selectedObjects.clear();
    m_hoveredObject = nullptr;
    
    // Clear undo/redo stacks (actions refer to old document)
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    m_undoStack.clear();
    m_redoStack.clear();
    
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
    // Drop OCR block cache so hit-testing doesn't reuse stale rects from the
    // previous document (also resets m_ocrBlockCacheTileVersion so the next
    // edgeless press forces a fresh rebuild against the new document's
    // tileLoadVersion stream).
    invalidateOcrBlockCache();
    
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
    syncTextBoxFormatBar();
}

// ===== PDF source warning banner =====

void DocumentViewport::showPdfSourceWarning(int sourceCount, int affectedPages,
                                            const QString& singleSourceName,
                                            const QString& warningSignature)
{
    if (!m_missingPdfBanner) {
        m_missingPdfBanner = new MissingPdfBanner(this);
        
        connect(m_missingPdfBanner, &MissingPdfBanner::reviewSourcesClicked,
                this, [this]() { emit requestPdfSources(); });
        connect(m_missingPdfBanner, &MissingPdfBanner::dismissed,
                this, [this]() {
            m_dismissedPdfWarningSignature = m_pdfWarningSignature;
        });
    }
    
    m_pdfWarningSignature = warningSignature;
    m_missingPdfBanner->setSummary(sourceCount, affectedPages, singleSourceName);
    
    m_missingPdfBanner->setFixedWidth(width());
    m_missingPdfBanner->move(0, 0);
    
    if (m_dismissedPdfWarningSignature != warningSignature) {
        // Also cancels an in-flight hide animation if health changed again.
        m_missingPdfBanner->showAnimated();
    }
}

void DocumentViewport::hidePdfSourceWarning()
{
    m_pdfWarningSignature.clear();
    m_dismissedPdfWarningSignature.clear();
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
    
    // PDF dark mode depends on overall dark mode — invalidate cache so pages
    // are re-rendered with or without lightness inversion.
    if (m_pdfDarkModeEnabled) {
        invalidatePdfCache();
    }

    if (m_textBoxFormatBar)
        m_textBoxFormatBar->setDarkMode(dark);
    if (m_inlineTextBoxEditor && m_inlineEditSession.active)
        updateInlineTextEditorGeometry();
    updateTextBoxFormatBarGeometry();

    // Trigger repaint
    update();
}

void DocumentViewport::setPdfDarkModeEnabled(bool enabled)
{
    if (m_pdfDarkModeEnabled == enabled) {
        return;
    }
    m_pdfDarkModeEnabled = enabled;

    // Re-render cached PDF pages with/without inversion
    invalidatePdfCache();
    update();
}

void DocumentViewport::setSkipImageMasking(bool skip)
{
    if (m_skipImageMasking == skip) {
        return;
    }
    m_skipImageMasking = skip;

    if (m_isDarkMode && m_pdfDarkModeEnabled) {
        invalidatePdfCache();
        update();
    }
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

void DocumentViewport::notifyPdfChanged()
{
    invalidatePdfCache();
    update();
}

// ===== Tool State Management (Task 2.1) =====

void DocumentViewport::setCurrentTool(ToolType tool)
{
    if (m_currentTool == tool) {
        return;
    }

    commitInlineTextEdit();
    
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
    
    // Clear object selection and cancel creation when switching away from ObjectSelect tool
    if (previousTool == ToolType::ObjectSelect && tool != ToolType::ObjectSelect) {
        clearObjectSelection();
    }
    
    // Cancel any in-progress eraser lasso when switching away from Eraser
    if (previousTool == ToolType::Eraser && tool != ToolType::Eraser) {
        if (m_isDrawingEraserLasso) {
            m_isDrawingEraserLasso = false;
            m_eraserLassoPageIndex = -1;
            m_lassoPath.clear();
            m_pointerActive = false;
        }
    }
    
    // Clean up Pan tool state when switching away
    if (previousTool == ToolType::Pan && tool != ToolType::Pan) {
        if (m_isPanToolDragging) {
            endPanGesture();
            m_isPanToolDragging = false;
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

void DocumentViewport::setPenMinStrokeWidth(qreal minWidth)
{
    // Clamp into a sane range; the preset editor further bounds it to
    // `[0, thickness]` but guard against malformed external callers.
    minWidth = qBound(0.0, minWidth, 100.0);

    if (qFuzzyCompare(m_penMinStrokeWidth, minWidth)) {
        return;
    }

    m_penMinStrokeWidth = minWidth;
    // No repaint needed: the floor only affects strokes captured after this
    // point.  Existing strokes already have their pressure values baked in.
}

qreal DocumentViewport::applyPenPressureFloor(qreal rawPressure) const
{
    // Marker uses a fixed pressure of 1.0; no floor needed and no division
    // concerns.  Callers still wrap marker handling with useFixedPressure,
    // but handle it here defensively so the helper is symmetric.
    if (m_currentTool == ToolType::Marker) {
        return qBound(0.1, rawPressure, 1.0);
    }

    const qreal base = m_currentStroke.baseThickness;
    const qreal minP = (base > 0.0)
        ? qBound(0.1, m_penMinStrokeWidth / base, 1.0)
        : 0.1;
    return qBound(minP, rawPressure, 1.0);
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

    // A mode change must not let an in-flight gesture complete under stale UI
    // state (most visibly, a Text rubber band creating after Image is chosen).
    if (hasActiveObjectPointerGesture()) {
        cancelObjectPointerGesture();
    }
    m_objectInsertMode = mode;
    emit objectInsertModeChanged(mode);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Object insert mode changed to:" << static_cast<int>(mode);
#endif
}

void DocumentViewport::setObjectActionMode(ObjectActionMode mode)
{
    if (m_objectActionMode == mode) {
        return;
    }

    // The effective mode is cached at press time. Cancel the active gesture
    // instead of allowing release to perform the old mode after the toggle.
    if (hasActiveObjectPointerGesture()) {
        cancelObjectPointerGesture();
    }
    m_objectActionMode = mode;
    m_objectGestureActionMode = mode;
    emit objectActionModeChanged(mode);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Object action mode changed to:" << (mode == ObjectActionMode::Select ? "Select" : "Create");
#endif
}

DocumentViewport::ObjectActionMode
DocumentViewport::effectiveObjectActionModeForPointer(
    ObjectActionMode persistentMode,
    PointerEvent::Source source,
    Qt::MouseButton button)
{
    if (source == PointerEvent::Mouse && button == Qt::RightButton) {
        return persistentMode == ObjectActionMode::Select
            ? ObjectActionMode::Create
            : ObjectActionMode::Select;
    }
    return persistentMode;
}

bool DocumentViewport::hasActiveObjectPointerGesture() const
{
    return m_currentTool == ToolType::ObjectSelect
        && (m_pointerActive || m_objectGestureButton != Qt::NoButton
            || m_isCreatingTextBox || m_isDraggingObjects || m_isResizingObject);
}

void DocumentViewport::beginObjectPointerGesture(const PointerEvent& pe)
{
    m_objectGestureButton =
        pe.source == PointerEvent::Mouse ? pe.button : Qt::NoButton;
    m_objectGestureActionMode = effectiveObjectActionModeForPointer(
        m_objectActionMode, pe.source, pe.button);
}

void DocumentViewport::cancelObjectPointerGesture()
{
    // Drag and resize previews mutate the live object. Restore their snapshots
    // so Escape, a tool switch, or a mode switch is a true cancellation rather
    // than an untracked document edit with no undo entry or dirty signal.
    if (m_isDraggingObjects) {
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            auto it = m_objectOriginalPositions.constFind(obj->id);
            if (it != m_objectOriginalPositions.constEnd()) {
                obj->position = *it;
            }
        }
    }

    if (m_isResizingObject && !m_selectedObjects.isEmpty()) {
        if (InsertedObject* obj = m_selectedObjects.first()) {
            if (m_hasResizeTextBoxState
                && obj->type() == QLatin1String("textbox")) {
                static_cast<TextBoxObject*>(obj)->applyState(
                    m_resizeOriginalTextBoxState);
            } else {
                obj->position = m_resizeOriginalPosition;
                obj->size = m_resizeOriginalSize;
                obj->rotation = m_resizeOriginalRotation;
            }
        }
    }

    m_isDraggingObjects = false;
    m_isCreatingTextBox = false;
    m_isResizingObject = false;
    m_objectResizeHandle = HandleHit::None;
    m_resizeObjectPageIndex = -1;
    m_hasResizeTextBoxState = false;
    m_textBoxResizeActivated = false;
    m_textBoxResizeChanged = false;
    m_objectOriginalPositions.clear();
    m_objectOriginalPageIndices.clear();
    m_objectDragBackgroundSnapshot = QPixmap();
    m_dragObjectRenderedCache = QPixmap();
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;
    m_hardwareEraserActive = false;
    resetObjectPointerGesture();
    update();
}

void DocumentViewport::resetObjectPointerGesture()
{
    m_objectGestureButton = Qt::NoButton;
    m_objectGestureActionMode = m_objectActionMode;
}

void DocumentViewport::setEraserMode(EraserMode mode)
{
    if (m_eraserMode == mode) {
        return;
    }

    // Cancel any in-progress eraser lasso when switching away from Lasso mode
    if (m_isDrawingEraserLasso) {
        m_isDrawingEraserLasso = false;
        m_eraserLassoPageIndex = -1;
        m_lassoPath.clear();
        m_pointerActive = false;
    }

    m_eraserMode = mode;
    emit eraserModeChanged(mode);
    update();
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

    // Pan/zoom is in flight: suspend the focus cache so the next paint
    // takes the cache-free Direct tier instead of rebuilding a viewport-
    // clipped pixmap every frame. The 150 ms timer will re-enable Focus
    // tier and request one more paint when the user stops moving.
    if (m_focusRebuildTimer) {
        m_focusCacheSuspended = true;
        m_focusRebuildTimer->start(150);
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

    // If we just zoomed back below the divisor-one threshold, no page on
    // screen will pick the Focus tier this paint - so any allocated focus
    // pixmaps are dead weight. Eagerly release them to claw memory back
    // immediately rather than waiting for the next eviction sweep.
    releaseFocusCachesBelowThreshold();

    // Clamp pan offset (bounds change with zoom)
    clampPanOffset();
    
    update();
    emit zoomChanged(m_zoomLevel);
    emitScrollFractions();
}

void DocumentViewport::setPanOffset(QPointF offset, bool steppedScroll)
{
    // Pan in flight: suspend the focus cache (see setZoomLevel for rationale).
    if (m_focusRebuildTimer) {
        m_focusCacheSuspended = true;
        m_focusRebuildTimer->start(150);
    }

    m_panOffset = offset;
    clampPanOffset();
    
    updateCurrentPageIndex();
    
    update();
    emit panChanged(m_panOffset);
    emitScrollFractions();
    
    // SP1: defer the heavy housekeeping (PDF preload, stroke-cache preload, tile
    // eviction) to onScrollSettled() so it runs once ~SCROLL_SETTLE_MS after the
    // user stops scrolling instead of on every wheel/touchpad event.
    onScrollActivity(steppedScroll);
}

void DocumentViewport::onScrollActivity(bool steppedScroll)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6: behavior unchanged - always gate rendering to cache-only while
    // scrolling (SP2). The discrete-step distinction below is Qt5-only.
    Q_UNUSED(steppedScroll);
    m_scrollActive = true;
#else
    // Qt5: a discrete mouse-wheel step renders synchronously (its pre-SP2, still
    // instant, behavior). Under SP2 the cache-only-while-scrolling gate leaves
    // the newly revealed page blank until the settle timer fires ~SCROLL_SETTLE_MS
    // later; on the Qt5 build the adjacent-page async preload does not fill the
    // cache in time, so every notch flashes blank and stalls. Do NOT mark the
    // stepped route active so paintEvent takes the synchronous getCachedPdfPage
    // path. Continuous sources (scroll-bar drag, touchpad pixel-delta) keep the
    // deferred cache-only path. Heavy housekeeping stays deferred either way.
    m_scrollActive = steppedScroll ? false : true;
#endif
    if (m_scrollSettleTimer) {
        m_scrollSettleTimer->start();
    }
}

void DocumentViewport::onScrollSettled()
{
    m_scrollActive = false;
    if (!m_document) {
        return;
    }

    // Visible pages may have changed while scrolling: resize the cache first,
    // then preload adjacent pages (async, debounced) and reclaim memory.
    updatePdfCacheCapacity();
    preloadPdfCache();
    preloadStrokeCaches();
    evictDistantTiles();

    // Final clean repaint (matters in SP2, where painting draws cache-only
    // while scrolling and needs one repaint to show freshly rendered pages).
    update();
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
    //   Y: 0 = top edge, 1 = bottom edge (ALREADY converted from PDF coords by MuPdfProvider)
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

qreal DocumentViewport::searchMatchPageYFraction(const PdfSearchMatch& match) const
{
    // SBS1: page-local Y fraction of a match center, for reveal + scroll-bar ticks.
    if (!m_document || match.pageIndex < 0) {
        return -1.0;
    }
    // Tile/edgeless sources have no page-axis position.
    if (match.source == PdfSearchMatch::OcrTextTile ||
        match.source == PdfSearchMatch::TextBoxObjTile) {
        return -1.0;
    }

    const qreal pageHeight = m_document->pageSizeAt(match.pageIndex).height();
    if (pageHeight <= 0.0) {
        return -1.0;
    }

    // Convert the match rect to page coords exactly as renderSearchMatchesOverlay.
    QRectF pageRect = match.boundingRect;
    if (match.source == PdfSearchMatch::PdfText) {
        pageRect = QRectF(pageRect.x() * PDF_TO_PAGE_SCALE,
                          pageRect.y() * PDF_TO_PAGE_SCALE,
                          pageRect.width() * PDF_TO_PAGE_SCALE,
                          pageRect.height() * PDF_TO_PAGE_SCALE);
    }

    const qreal centerY = pageRect.y() + pageRect.height() / 2.0;
    return qBound(0.0, centerY / pageHeight, 1.0);
}

bool DocumentViewport::isPagePositionVisible(int pageIndex, qreal normY) const
{
    // SBS1: is the given page-local Y currently within the viewport (vertical)?
    if (!m_document || m_zoomLevel <= 0.0) {
        return false;
    }
    pageIndex = qBound(0, pageIndex, m_document->pageCount() - 1);

    const qreal docY = pagePosition(pageIndex).y()
                     + qBound(0.0, normY, 1.0) * m_document->pageSizeAt(pageIndex).height();
    const qreal viewTop = m_panOffset.y();
    const qreal viewBottom = viewTop + height() / m_zoomLevel;
    const qreal margin = 20.0 / m_zoomLevel;  // treat matches hugging an edge as off-screen
    return docY >= viewTop + margin && docY <= viewBottom - margin;
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
        emit userWarning(tr("Target page not found."));
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
        QPointF lastPos = m_edgelessPositionHistory.last();
        // Consider positions within 10 pixels as "same"
        if ((currentPos - lastPos).manhattanLength() < 10.0) {
            return;
        }
    }
    
    // Trim history if at capacity - discard oldest entry
    if (m_edgelessPositionHistory.size() >= MAX_POSITION_HISTORY) {
        m_edgelessPositionHistory.removeFirst();
    }
    
    m_edgelessPositionHistory.append(currentPos);
    
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
    
    QPointF previousPos = m_edgelessPositionHistory.takeLast();
    
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

bool DocumentViewport::syncPositionToDocument()
{
    // Only applies to edgeless mode
    if (!m_document || !m_document->isEdgeless()) {
        return false;
    }

    // currentCenterPosition() is deterministic w.r.t. m_panOffset/m_zoomLevel,
    // and applyRestoredEdgelessPosition() initialises m_panOffset from
    // lastPosition with the inverse math, so an untouched doc compares byte-
    // for-byte equal here. That lets close-time autosave skip a full bundle
    // rewrite when the user opens and closes without panning.
    const QPointF currentPos = currentCenterPosition();
    QVector<QPointF> historyVec(m_edgelessPositionHistory.cbegin(),
                                m_edgelessPositionHistory.cend());

    bool changed = false;
    if (m_document->edgelessLastPosition() != currentPos) {
        m_document->setEdgelessLastPosition(currentPos);
        changed = true;
    }
    if (m_document->edgelessPositionHistory() != historyVec) {
        m_document->setEdgelessPositionHistory(historyVec);
        changed = true;
    }

#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[PositionHistory] Synced to document: lastPos =" << currentPos
             << "| history size =" << historyVec.size()
             << "| changed =" << changed;
#endif

    return changed;
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
    
    // Restore position history from Document (already in oldest-to-newest order)
    const QVector<QPointF>& savedHistory = m_document->edgelessPositionHistory();
    m_edgelessPositionHistory = QList<QPointF>(savedHistory.cbegin(), savedHistory.cend());
    
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

void DocumentViewport::scrollBy(QPointF delta, bool steppedScroll)
{
    setPanOffset(m_panOffset + delta, steppedScroll);
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
        // SP1: scroll-bar route defers preload/evict to the settle timer too.
        onScrollActivity();
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
        // SP1: scroll-bar route defers preload/evict to the settle timer too.
        onScrollActivity();
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

qreal DocumentViewport::pageTrackFraction(int pageIndex) const
{
    // SB2: normalized top-of-page position in the scroll bar's track space.
    if (!m_document || m_document->isEdgeless()) {
        return -1.0;
    }
    const int count = m_document->pageCount();
    if (count <= 0) {
        return -1.0;
    }

    ensurePageLayoutCache();
    const qreal contentHeight = m_cachedContentSize.height();
    if (contentHeight <= 0.0) {
        return -1.0;
    }

    if (pageIndex <= 0) {
        return 0.0;
    }
    if (pageIndex >= count) {
        return 1.0;  // bottom of the last page
    }
    if (pageIndex >= m_pageYCache.size()) {
        return 1.0;  // defensive: cache smaller than expected
    }
    return qBound(0.0, m_pageYCache[pageIndex] / contentHeight, 1.0);
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

int DocumentViewport::nearestPageToPoint(QPointF documentPt) const
{
    // Edgeless documents have no page layout to search
    if (!m_document || m_document->isEdgeless() || m_document->pageCount() == 0) {
        return -1;
    }
    
    // Exact hit is by definition the nearest page
    int exact = pageAtPoint(documentPt);
    if (exact >= 0) {
        return exact;
    }
    
    const int pageCount = m_document->pageCount();
    
    // Squared distance from the point to a page rect (0 when inside)
    auto distanceSquaredTo = [&](int pageIndex) -> qreal {
        QRectF rect = pageRect(pageIndex);
        qreal dx = qMax(qMax(rect.left() - documentPt.x(), documentPt.x() - rect.right()), 0.0);
        qreal dy = qMax(qMax(rect.top() - documentPt.y(), documentPt.y() - rect.bottom()), 0.0);
        return dx * dx + dy * dy;
    };
    
    // Narrow the search to a window around the point instead of scanning every
    // page -- this runs per frame during object drags, and documents can hold
    // thousands of pages.
    int windowStart = 0;
    int windowEnd = pageCount - 1;
    
    ensurePageLayoutCache();
    if (!m_pageYCache.isEmpty() && m_pageYCache.size() == pageCount) {
        int low = 0;
        int high = pageCount - 1;
        int candidate = 0;
        while (low <= high) {
            int mid = (low + high) / 2;
            if (documentPt.y() < m_pageYCache[mid]) {
                high = mid - 1;
            } else {
                candidate = mid;
                low = mid + 1;
            }
        }
        // Two-column layout puts two pages per row, so widen the window enough
        // to cover the rows on either side of the candidate.
        const int margin = (m_layoutMode == LayoutMode::SingleColumn) ? 1 : 3;
        windowStart = qMax(0, candidate - margin);
        windowEnd = qMin(pageCount - 1, candidate + margin);
    }
    
    int best = windowStart;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (int i = windowStart; i <= windowEnd; ++i) {
        qreal dist = distanceSquaredTo(i);
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    
    return best;
}

QPointF DocumentViewport::clampObjectPositionToPage(int pageIndex, QPointF pagePos, QSizeF size) const
{
    // Edgeless mode is an infinite canvas -- nothing to clamp against
    if (!m_document || m_document->isEdgeless()) {
        return pagePos;
    }
    if (pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return pagePos;
    }
    
    return ObjectConstraints::clampPosition(pagePos, size, m_document->pageSizeAt(pageIndex));
}

QVector<int> DocumentViewport::loadedPagesNear(const QPointF& docPoint, int excludePageIndex) const
{
    // An object can only overhang its page by its own extent, so only pages
    // adjacent to the point are plausible owners. Ordered nearest-first, and
    // limited to loaded pages so hover never triggers lazy page loading.
    const int SEARCH_RADIUS = 2;
    
    QVector<int> result;
    if (!m_document || m_document->isEdgeless()) {
        return result;
    }
    
    int anchor = nearestPageToPoint(docPoint);
    if (anchor < 0) {
        return result;
    }
    
    result.reserve(2 * SEARCH_RADIUS + 1);
    const int pageCount = m_document->pageCount();
    
    auto consider = [&](int index) {
        if (index < 0 || index >= pageCount) return;
        if (index == excludePageIndex) return;
        if (!m_document->isPageLoaded(index)) return;
        result.append(index);
    };
    
    consider(anchor);
    for (int offset = 1; offset <= SEARCH_RADIUS; ++offset) {
        consider(anchor - offset);
        consider(anchor + offset);
    }
    
    return result;
}

bool DocumentViewport::clampObjectToPage(InsertedObject* obj, int pageIndex) const
{
    if (!obj) {
        return false;
    }
    
    QPointF clamped = clampObjectPositionToPage(pageIndex, obj->position, obj->size);
    if (clamped == obj->position) {
        return false;
    }
    
    obj->position = clamped;
    return true;
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
        
        // Objects are anchored in tile-local coordinates but may extend past
        // their own tile, so a point can be covered by an object anchored up to
        // maxObjectExtent away - and only by those. Walking every loaded tile
        // made hit-testing proportional to canvas size on every pointer move,
        // which the object tools do while merely hovering.
        const int tileSize = Document::EDGELESS_TILE_SIZE;
        const qreal reach = m_document->maxObjectExtent();
        const int minTx = static_cast<int>(std::floor((docPoint.x() - reach) / tileSize));
        const int maxTx = static_cast<int>(std::floor((docPoint.x() + reach) / tileSize));
        const int minTy = static_cast<int>(std::floor((docPoint.y() - reach) / tileSize));
        const int maxTy = static_cast<int>(std::floor((docPoint.y() + reach) / tileSize));
        
        for (int tx = minTx; tx <= maxTx; ++tx) {
            for (int ty = minTy; ty <= maxTy; ++ty) {
                Page* tile = m_document->getTile(tx, ty);
                if (!tile) continue;
                
                const QPointF tileLocal =
                    docPoint - QPointF(tx * tileSize, ty * tileSize);
                if (InsertedObject* obj = tile->objectAtPoint(tileLocal, affinityFilter)) {
                    return obj;
                }
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
                if (InsertedObject* obj = page->objectAtPoint(pageLocal, affinityFilter)) {
                    return obj;
                }
            }
        }
        
        // Fallback for objects sitting outside their page: current builds keep
        // objects contained, but documents saved before that could not. Without
        // this sweep such an object is visible yet impossible to select, since
        // the point it occupies belongs to no page (or to a different one).
        // Skipped entirely for documents that hold no objects at all, which is
        // the common case for a pointer move that hit nothing.
        if (m_document->maxObjectExtent() <= 0) {
            return nullptr;
        }
        
        for (int neighbour : loadedPagesNear(docPoint, pageIdx)) {
            Page* page = m_document->page(neighbour);
            if (!page) continue;
            
            InsertedObject* obj = page->objectAtPoint(docPoint - pagePosition(neighbour),
                                                      page->activeLayerIndex - 1);
            if (obj) {
                return obj;
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
    if (m_inlineEditSession.active
        && obj->id == m_inlineEditSession.objectId) {
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

    // User text boxes derive height from content, so only expose width
    // handles. OCR text remains geometry-driven and keeps all handles.
    if (obj->type() == QLatin1String("textbox")) {
        if (QLineF(viewportPos, handles[3]).length() <= hitRadius)
            return HandleHit::Left;
        if (QLineF(viewportPos, handles[4]).length() <= hitRadius)
            return HandleHit::Right;
        return HandleHit::None;
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
            
            // Rotation leaves position/size alone, but clamp anyway so a
            // rotated object near an edge can never end up unreachable.
            clampObjectToPage(obj, m_resizeObjectPageIndex);
            updateTextBoxFormatBarGeometry();
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

    if (m_hasResizeTextBoxState
        && (m_objectResizeHandle == HandleHit::Left
            || m_objectResizeHandle == HandleHit::Right)) {
        auto* textBox = static_cast<TextBoxObject*>(obj);
        qreal proposedWidth = m_objectResizeHandle == HandleHit::Right
            ? localX + halfW
            : halfW - localX;
        proposedWidth = qMax(TextBoxObject::MINIMUM_WIDTH, proposedWidth);
        if (m_document && !m_document->isEdgeless()
            && m_resizeObjectPageIndex >= 0) {
            proposedWidth = qMin(
                proposedWidth,
                qMax<qreal>(TextBoxObject::MINIMUM_WIDTH,
                            m_document->pageSizeAt(
                                m_resizeObjectPageIndex).width()));
        }

        if (!m_textBoxResizeActivated
            && qAbs(proposedWidth - m_resizeOriginalSize.width()) < 0.01) {
            return;
        }

        if (!m_textBoxResizeActivated) {
            textBox->applyState(m_resizeOriginalTextBoxState);
            if (textBox->usesLegacyLayout())
                textBox->upgradeToCurrentLayout();
            m_resizeBaseTextBoxState = textBox->captureState();
            m_resizeLastAcceptedTextBoxState =
                m_resizeOriginalTextBoxState;
            m_textBoxResizeActivated = true;
        }

        textBox->applyState(m_resizeBaseTextBoxState);
        textBox->reflowToWidth(proposedWidth);
        TextBoxState candidate = textBox->captureState();

        auto rotatedObjectPoint = [](const TextBoxState& state,
                                     const QPointF& localPoint) {
            const QPointF center(
                state.size.width() / 2.0, state.size.height() / 2.0);
            const QPointF delta = localPoint - center;
            const qreal radians = qDegreesToRadians(state.rotation);
            const QPointF rotated(
                delta.x() * qCos(radians) - delta.y() * qSin(radians),
                delta.x() * qSin(radians) + delta.y() * qCos(radians));
            return state.position + center + rotated;
        };
        auto positionForAnchoredPoint = [](const TextBoxState& state,
                                           const QPointF& localPoint,
                                           const QPointF& targetPoint) {
            const QPointF center(
                state.size.width() / 2.0, state.size.height() / 2.0);
            const QPointF delta = localPoint - center;
            const qreal radians = qDegreesToRadians(state.rotation);
            const QPointF rotated(
                delta.x() * qCos(radians) - delta.y() * qSin(radians),
                delta.x() * qSin(radians) + delta.y() * qCos(radians));
            return targetPoint - center - rotated;
        };

        const bool resizingRight =
            m_objectResizeHandle == HandleHit::Right;
        const QPointF originalAnchor = rotatedObjectPoint(
            m_resizeOriginalTextBoxState,
            resizingRight
                ? QPointF(0.0, 0.0)
                : QPointF(m_resizeOriginalTextBoxState.size.width(), 0.0));
        const QPointF candidateLocalAnchor = resizingRight
            ? QPointF(0.0, 0.0)
            : QPointF(candidate.size.width(), 0.0);
        candidate.position = positionForAnchoredPoint(
            candidate, candidateLocalAnchor, originalAnchor);

        if (!textBoxGeometryProposalAllowed(
                m_resizeOriginalTextBoxState, candidate,
                m_resizeObjectPageIndex)) {
            textBox->applyState(m_resizeLastAcceptedTextBoxState);
            showObjectGeometryFeedback(
                tr("Text box cannot grow beyond the page"),
                objectBoundsInViewport(textBox));
            updateTextBoxFormatBarGeometry();
            return;
        }

        textBox->applyState(candidate);
        m_resizeLastAcceptedTextBoxState = candidate;
        m_textBoxResizeChanged =
            candidate.size != m_resizeOriginalTextBoxState.size
            || candidate.position != m_resizeOriginalTextBoxState.position
            || candidate.textLayoutVersion
                != m_resizeOriginalTextBoxState.textLayoutVersion
            || !qFuzzyCompare(1.0 + candidate.fontSize,
                              1.0 + m_resizeOriginalTextBoxState.fontSize);
        updateTextBoxFormatBarGeometry();
        return;
    }
    
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
    
    // Aspect ratio enforcement for locked ImageObjects
    if (auto* img = dynamic_cast<ImageObject*>(obj)) {
        if (img->maintainAspectRatio && img->originalAspectRatio > 0.0) {
            bool isCorner = (m_objectResizeHandle == HandleHit::TopLeft ||
                             m_objectResizeHandle == HandleHit::TopRight ||
                             m_objectResizeHandle == HandleHit::BottomLeft ||
                             m_objectResizeHandle == HandleHit::BottomRight);
            bool isHorizontalEdge = (m_objectResizeHandle == HandleHit::Left ||
                                     m_objectResizeHandle == HandleHit::Right);
            if (isCorner) {
                qreal uniform = (scaleX + scaleY) / 2.0;
                scaleX = uniform;
                scaleY = uniform;
            } else if (isHorizontalEdge) {
                scaleY = scaleX;
            } else {
                scaleX = scaleY;
            }
        }
    }
    
    // Clamp scale factors (prevent flip and ensure minimum size)
    const qreal MIN_SCALE = 0.1;
    qreal maxScaleX = 10.0;
    qreal maxScaleY = 10.0;
    
    // Paged mode: cap the scale so the object cannot grow larger than the page.
    // The cap depends only on the page size, not on where the object sits: an
    // object flush against an edge still grows, and the position clamp below
    // slides it inward. Capping the scale rather than the resulting rect keeps
    // this compatible with the aspect-ratio lock above, so a locked image
    // shrinks uniformly instead of distorting.
    if (m_document && !m_document->isEdgeless() && m_resizeObjectPageIndex >= 0) {
        QSizeF pageSize = m_document->pageSizeAt(m_resizeObjectPageIndex);
        qreal pageLimitX = ObjectConstraints::maxScaleToFitPage(
            m_resizeOriginalSize.width(), pageSize.width());
        qreal pageLimitY = ObjectConstraints::maxScaleToFitPage(
            m_resizeOriginalSize.height(), pageSize.height());
        
        // A locked aspect ratio scales both axes together, so the tighter of
        // the two limits has to apply to both.
        auto* lockedImage = dynamic_cast<ImageObject*>(obj);
        if (lockedImage && lockedImage->maintainAspectRatio &&
            lockedImage->originalAspectRatio > 0.0) {
            qreal uniformLimit = qMin(pageLimitX, pageLimitY);
            pageLimitX = uniformLimit;
            pageLimitY = uniformLimit;
        }
        
        // Never cap below MIN_SCALE: an object that already overflows its page
        // must still be shrinkable back into bounds.
        maxScaleX = qMin(maxScaleX, qMax(pageLimitX, MIN_SCALE));
        maxScaleY = qMin(maxScaleY, qMax(pageLimitY, MIN_SCALE));
    }
    
    scaleX = qBound(MIN_SCALE, scaleX, maxScaleX);
    scaleY = qBound(MIN_SCALE, scaleY, maxScaleY);
    
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
    
    // Growth is symmetric about the centre, so an object near an edge would
    // spill over it. Sliding the result back in lets it keep growing away from
    // the edge instead of refusing to resize at all.
    newPos = clampObjectPositionToPage(m_resizeObjectPageIndex, newPos, newSize);
    
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
    // Perf instrumentation. Declared before the QPainter on purpose: the
    // painter must be destroyed (and its work flushed) before the sampler's
    // destructor takes the end timestamp. Recording from a destructor also
    // means the early returns in the fast paths below are all measured.
    ViewportPerfMonitor::FrameSampler perfSample(m_perf, event->rect(), size(),
                                                 devicePixelRatioF());
    
    QPainter painter(this);
    // Note: Antialiasing is deferred until after gesture fast paths.
    // Gesture paths only blit cached pixmaps and don't need it.
    
    // ========== FAST PATH: Viewport Gesture (Zoom or Pan) ==========
    // During viewport gestures, draw transformed cached frame instead of re-rendering.
    // This provides smooth FPS during rapid zoom/pan operations.
    if (m_gesture.isActive() && !m_gesture.cachedFrame.isNull() 
        && m_gesture.startZoom > 0) {  // Guard against division by zero
        
        // Background is filled per branch below, not here: the pan path only
        // needs the strip its shifted frame leaves exposed, whereas a scaled
        // frame can leave a border of any shape. Nothing pre-clears for us,
        // since WA_OpaquePaintEvent is set.
        
        // Calculate frame size in LOGICAL pixels (not physical)
        // grab() returns a pixmap at device pixel ratio, so we must divide by DPR
        // to get the logical size that matches the widget's coordinate system
        qreal dpr = m_gesture.frameDevicePixelRatio;
        QSizeF logicalSize(m_gesture.cachedFrame.width() / dpr,
                           m_gesture.cachedFrame.height() / dpr);
        
        // Draw based on gesture type
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);  // Speed over quality
        
        if (m_gesture.activeType == ViewportGestureState::Zoom) {
            perfSample.setPath(ViewportPerfMonitor::FramePath::GestureZoom);
            // A scaled frame can expose a border on any side, so clear it all.
            painter.fillRect(rect(), m_backgroundColor);
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
            perfSample.setPath(ViewportPerfMonitor::FramePath::GesturePan);
            // PAN: Shift the cached frame by pan delta
            // Pan delta in document coords → convert to viewport pixels
            QPointF panDeltaDoc = m_gesture.targetPan - m_gesture.startPan;
            QPointF panDeltaPixels = panDeltaDoc * m_gesture.startZoom * -1.0;  // Negate: pan offset increase = viewport moves opposite
            
            // Note: no need to snap panDeltaPixels to whole device pixels. With
            // SmoothPixmapTransform off, QRasterPaintEngine already quantises a
            // pure-translate drawPixmap to the device pixel grid - verified by
            // byte-comparing renders at fractional and integral offsets, which
            // come out identical at both DPR 1 and DPR 2.
            
            // Clear only what the shifted frame won't cover.
            fillBackgroundAround(painter, QRectF(panDeltaPixels, logicalSize));
            
            painter.drawPixmap(panDeltaPixels, m_gesture.cachedFrame);
        } else {
            // Defensive: ViewportGestureState::ZoomAndPan is declared but never
            // assigned. If that changes, clear rather than present a stale
            // backing store.
            painter.fillRect(rect(), m_backgroundColor);
        }
        
        // Skip normal rendering during gesture
        return;
    }
    
    // ========== FAST PATH: Selection Transform ==========
    // During selection transform, draw cached background + transformed selection cache.
    // This avoids re-rendering all tiles/pages, providing smooth transform performance.
    if (m_isTransformingSelection && !m_selectionBackgroundSnapshot.isNull() 
        && m_lassoSelection.isValid() && !m_skipSelectionRendering) {
        
        perfSample.setPath(ViewportPerfMonitor::FramePath::SelectionTransform);
        
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
        
        perfSample.setPath(ViewportPerfMonitor::FramePath::ObjectDrag);
        
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
    
    // Enable antialiasing for normal (non-gesture) rendering.
    // Deferred to here so gesture fast paths above skip the overhead.
    painter.setRenderHint(QPainter::Antialiasing, true);
    
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
        renderEdgelessMode(painter, dirtyRect);
        
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
    
    // Task 2.10: Draw lasso selection path while drawing (regular lasso or eraser lasso)
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if ((m_isDrawingLasso || m_isDrawingEraserLasso) && m_lassoPath.size() > 1) {
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
    
    // Plan D2: cross-document page-transfer drop insertion indicator.
    if (m_dropIndicatorActive && !m_dropIndicatorLine.isNull()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen indicatorPen(QColor(0, 122, 255), 3.0);
        indicatorPen.setCosmetic(true);
        indicatorPen.setCapStyle(Qt::RoundCap);
        painter.setPen(indicatorPen);
        painter.drawLine(m_dropIndicatorLine);
        painter.restore();
    }
    
    // Debug overlay is now handled by DebugOverlay widget (source/ui/DebugOverlay.cpp)
    // Toggle with Ctrl+Shift+D
}

// ============================================================================
// Plan D2: cross-document page-transfer drop target
// ============================================================================

// Decode the payload and reject same-document / edgeless / empty drags.
static bool acceptablePageTransfer(const QMimeData* mime, const Document* destDoc,
                                   QString* outToken = nullptr,
                                   QStringList* outUuids = nullptr)
{
    if (!mime || !destDoc || destDoc->isEdgeless()) {
        return false;
    }
    if (!mime->hasFormat(PageTransfer::mimeType())) {
        return false;
    }
    QString token;
    QStringList uuids;
    if (!PageTransfer::decode(mime->data(PageTransfer::mimeType()), token, uuids)) {
        return false;
    }
    // Defensive: two viewports should never show the same document, but reject
    // a same-document drop to avoid accidental self-duplication.
    if (token == destDoc->sessionId()) {
        return false;
    }
    if (outToken) *outToken = token;
    if (outUuids) *outUuids = uuids;
    return true;
}

void DocumentViewport::dragEnterEvent(QDragEnterEvent* event)
{
    if (acceptablePageTransfer(event->mimeData(), m_document)) {
        m_dropIndicatorActive = true;
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
        return;
    }
    m_dropIndicatorActive = false;
    event->ignore();
}

void DocumentViewport::dragMoveEvent(QDragMoveEvent* event)
{
    if (!acceptablePageTransfer(event->mimeData(), m_document)) {
        event->ignore();
        return;
    }
    QLineF line;
    m_dropInsertIndex = dropInsertIndexAt(SN_DRAG_POS(event), line);
    m_dropIndicatorLine = line;
    m_dropIndicatorActive = true;
    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
    update();
}

void DocumentViewport::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    if (m_dropIndicatorActive) {
        m_dropIndicatorActive = false;
        m_dropInsertIndex = -1;
        m_dropIndicatorLine = QLineF();
        update();
    }
}

void DocumentViewport::dropEvent(QDropEvent* event)
{
    QString token;
    QStringList uuids;
    const bool ok = acceptablePageTransfer(event->mimeData(), m_document, &token, &uuids);

    // Clear indicator regardless of outcome.
    const int destIndex = m_dropInsertIndex >= 0
        ? m_dropInsertIndex
        : (m_document ? m_document->pageCount() : 0);
    m_dropIndicatorActive = false;
    m_dropInsertIndex = -1;
    m_dropIndicatorLine = QLineF();
    update();

    if (!ok) {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
    emit pageTransferDropped(token, uuids, destIndex);
}

int DocumentViewport::dropInsertIndexAt(const QPointF& viewportPos, QLineF& outLineViewport) const
{
    outLineViewport = QLineF();
    if (!m_document) {
        return 0;
    }
    const int pageCount = m_document->pageCount();
    if (pageCount == 0) {
        return 0;
    }

    ensurePageLayoutCache();
    const QPointF docPt = viewportToDocument(viewportPos);
    const qreal contentWidth = totalContentSize().width();

    // Helper to build a horizontal indicator line (viewport coords) at a
    // document-space Y spanning the content width.
    auto horizontalLine = [&](qreal docY) {
        const QPointF a = documentToViewport(QPointF(0, docY));
        const QPointF b = documentToViewport(QPointF(contentWidth, docY));
        return QLineF(a, b);
    };

    if (m_layoutMode == LayoutMode::SingleColumn) {
        // Binary search (O(log n)) for the last page whose top Y is at or above
        // docPt.y(), mirroring pageAtPoint()'s approach for large PDFs. The
        // insertion point is before that page if the point is in its upper half,
        // otherwise after it.
        int cand = -1;
        if (!m_pageYCache.isEmpty()) {
            int lo = 0, hi = pageCount - 1;
            const qreal y = docPt.y();
            while (lo <= hi) {
                const int mid = (lo + hi) / 2;
                if (m_pageYCache[mid] <= y) {
                    cand = mid;
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
        }

        int index;
        if (cand < 0) {
            index = 0;
        } else if (docPt.y() <= pageRect(cand).center().y()) {
            index = cand;
        } else {
            index = cand + 1;
        }
        index = qBound(0, index, pageCount);

        qreal boundaryY;
        if (index == 0) {
            boundaryY = pageRect(0).top();
        } else if (index >= pageCount) {
            boundaryY = pageRect(pageCount - 1).bottom();
        } else {
            boundaryY = (pageRect(index - 1).bottom() + pageRect(index).top()) / 2.0;
        }
        outLineViewport = horizontalLine(boundaryY);
        return index;
    }

    // TwoColumn: rows of two pages (left = even index, right = odd index).
    const int numRows = (pageCount + 1) / 2;

    // Find the row whose vertical span contains docPt.y(), else the nearest.
    int row = -1;
    for (int r = 0; r < numRows; ++r) {
        const int leftIdx = r * 2;
        QRectF leftRect = pageRect(leftIdx);
        const int rightIdx = leftIdx + 1;
        qreal rowTop = leftRect.top();
        qreal rowBottom = leftRect.bottom();
        if (rightIdx < pageCount) {
            QRectF rightRect = pageRect(rightIdx);
            rowTop = qMin(rowTop, rightRect.top());
            rowBottom = qMax(rowBottom, rightRect.bottom());
        }
        if (docPt.y() < rowTop) {
            // Above this row -> insert before the row (horizontal indicator).
            outLineViewport = horizontalLine(rowTop);
            return qBound(0, leftIdx, pageCount);
        }
        if (docPt.y() <= rowBottom) {
            row = r;
            break;
        }
    }

    if (row < 0) {
        // Below the last row -> append at end.
        const int lastIdx = pageCount - 1;
        outLineViewport = horizontalLine(pageRect(lastIdx).bottom());
        return pageCount;
    }

    const int leftIdx = row * 2;
    const int rightIdx = leftIdx + 1;
    const QRectF leftRect = pageRect(leftIdx);

    // Vertical indicator line spanning the row's page height at a given docX.
    auto verticalLine = [&](qreal docX, const QRectF& rect) {
        const QPointF a = documentToViewport(QPointF(docX, rect.top()));
        const QPointF b = documentToViewport(QPointF(docX, rect.bottom()));
        return QLineF(a, b);
    };

    if (rightIdx >= pageCount) {
        // Only a left page in this (last) row: before or after it.
        if (docPt.x() < leftRect.center().x()) {
            outLineViewport = verticalLine(leftRect.left(), leftRect);
            return qBound(0, leftIdx, pageCount);
        }
        outLineViewport = verticalLine(leftRect.right(), leftRect);
        return qBound(0, leftIdx + 1, pageCount);
    }

    const QRectF rightRect = pageRect(rightIdx);
    if (docPt.x() < leftRect.center().x()) {
        // Before the left page.
        outLineViewport = verticalLine(leftRect.left(), leftRect);
        return qBound(0, leftIdx, pageCount);
    }
    if (docPt.x() < rightRect.center().x()) {
        // Between the two pages.
        const qreal midX = (leftRect.right() + rightRect.left()) / 2.0;
        outLineViewport = verticalLine(midX, leftRect);
        return qBound(0, rightIdx, pageCount);
    }
    // After the right page.
    outLineViewport = verticalLine(rightRect.right(), rightRect);
    return qBound(0, rightIdx + 1, pageCount);
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
        updateInlineTextEditorGeometry();
        updateTextBoxFormatBarGeometry();
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
    updateInlineTextEditorGeometry();
    updateTextBoxFormatBarGeometry();
}

void DocumentViewport::mousePressEvent(QMouseEvent* event)
{
    m_contextMenuObjectId.clear();

    // A right-press on the box being edited is asking for that editor's text
    // menu. It has to be recognised before the outside-click commit below,
    // which would otherwise dismiss the editor the menu belongs to. The editor
    // widget covers only the text area, so these are the presses that land on
    // the box's padding ring or border.
    m_contextMenuTargetsInlineEditor =
        m_currentTool == ToolType::ObjectSelect
        && event->button() == Qt::RightButton
        && inlineEditTargetContains(SN_MOUSE_POS(event));
    if (m_contextMenuTargetsInlineEditor) {
        event->accept();
        return;
    }

    // Child-widget presses stay in the editor. Any press delivered to the
    // canvas is an explicit outside click and commits the current session.
    if (m_inlineEditSession.active)
        commitInlineTextEdit();
    else {
        closeTextBoxFormatPopups(true);
        finishTextBoxFormatInteraction(true);
    }

    // Middle mouse button: start pan gesture (independent of active tool)
    if (event->button() == Qt::MiddleButton) {
        beginPanGesture();
        m_middleMouseLastPos = SN_MOUSE_POS(event);
        m_isMiddleMousePanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    
    const bool objectAlternateButton =
        m_currentTool == ToolType::ObjectSelect && event->button() == Qt::RightButton;

    // Drawing tools use left only. ObjectSelect also consumes right as the
    // temporary alternate action mode.
    if (event->button() != Qt::LeftButton && !objectAlternateButton) {
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

    // Ignore a second mouse button while an ObjectSelect gesture is active.
    if (m_currentTool == ToolType::ObjectSelect
        && m_objectGestureButton != Qt::NoButton) {
        event->accept();
        return;
    }

    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Press);
    handlePointerEvent(pe);
    event->accept();
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event)
{
    // Middle mouse pan (independent of tool system)
    if (m_isMiddleMousePanning && (event->buttons() & Qt::MiddleButton)) {
        QPointF delta = SN_MOUSE_POS(event) - m_middleMouseLastPos;
        QPointF docDelta(-delta.x() / m_zoomLevel, -delta.y() / m_zoomLevel);
        updatePanGesture(docDelta);
        m_middleMouseLastPos = SN_MOUSE_POS(event);
        event->accept();
        return;
    }
    
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
    const bool objectRightDrag =
        m_currentTool == ToolType::ObjectSelect
        && (event->buttons() & Qt::RightButton)
        && !m_contextMenuTargetsInlineEditor;
    if (m_pointerActive || (event->buttons() & Qt::LeftButton) || objectRightDrag) {
        PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Move);
        handlePointerEvent(pe);
    } else {
        // Track position for eraser cursor even when not pressing (hover)
        QPointF oldPos = m_lastPointerPos;
        m_lastPointerPos = SN_MOUSE_POS(event);
        
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
    // Middle mouse pan release
    if (event->button() == Qt::MiddleButton && m_isMiddleMousePanning) {
        endPanGesture();
        m_isMiddleMousePanning = false;
        updateHighlighterCursor();
        event->accept();
        return;
    }
    
    const bool objectAlternateButton =
        m_currentTool == ToolType::ObjectSelect && event->button() == Qt::RightButton;
    if (event->button() != Qt::LeftButton && !objectAlternateButton) {
        event->ignore();
        return;
    }
    
    // CRITICAL: Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    // Ignore duplicate mouse releases from a stylus gesture, but never swallow
    // the release of a mouse ObjectSelect gesture if source state was disturbed.
    if (m_activeSource == PointerEvent::Stylus
        && m_objectGestureButton == Qt::NoButton) {
        event->accept();
        return;
    }
    
    // The matching press was claimed for the inline editor; this release only
    // exists to carry the context menu that follows it.
    if (objectAlternateButton && m_contextMenuTargetsInlineEditor) {
        event->accept();
        return;
    }

    PointerEvent pe = mouseToPointerEvent(event, PointerEvent::Release);
    handlePointerEvent(pe);

    // The release just placed an inline editor under the cursor, either by
    // creating a text box or by opening an existing one. Windows raises the
    // context menu off this same release, so the brand-new editor would answer
    // it with its own menu.
    if (objectAlternateButton && m_inlineEditSession.active
        && m_inlineTextBoxEditor) {
        m_inlineTextBoxEditor->suppressNextContextMenu();
    }

    event->accept();
}

void DocumentViewport::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_contextMenuTargetsInlineEditor) {
        m_contextMenuTargetsInlineEditor = false;
        if (m_inlineTextBoxEditor && m_inlineEditSession.active) {
            m_inlineTextBoxEditor->showTextContextMenu(event->globalPos());
            event->accept();
            return;
        }
    }
    if (m_currentTool == ToolType::ObjectSelect) {
        const QString target = m_contextMenuObjectId;
        m_contextMenuObjectId.clear();
        // Only a right-press that landed on an object opens a menu. Pressing
        // bare page is how a new object gets created, and that must not be
        // interrupted by one.
        if (!target.isEmpty() && objectById(target))
            showObjectContextMenu(event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

void DocumentViewport::showObjectContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, m_isDarkMode);

    const bool hasSelection = !m_selectedObjects.isEmpty();

    QAction* cutAction = menu.addAction(tr("Cut"));
    cutAction->setEnabled(hasSelection);
    connect(cutAction, &QAction::triggered, this, [this]() {
        copySelectedObjects();
        deleteSelectedObjects();
    });

    QAction* copyAction = menu.addAction(tr("Copy"));
    copyAction->setEnabled(hasSelection);
    connect(copyAction, &QAction::triggered,
            this, &DocumentViewport::copySelectedObjects);

    QAction* pasteAction = menu.addAction(tr("Paste"));
    connect(pasteAction, &QAction::triggered,
            this, &DocumentViewport::pasteForObjectSelect);

    menu.addSeparator();

    if (m_selectedObjects.size() == 1
        && m_selectedObjects.first()->type() == QLatin1String("textbox")) {
        const QString textBoxId = m_selectedObjects.first()->id;
        QAction* editAction = menu.addAction(tr("Edit Text"));
        connect(editAction, &QAction::triggered, this, [this, textBoxId]() {
            InsertedObject* object = objectById(textBoxId);
            if (object && object->type() == QLatin1String("textbox"))
                startInlineTextEdit(static_cast<TextBoxObject*>(object), false);
        });
        menu.addSeparator();
    }

    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(hasSelection);
    connect(deleteAction, &QAction::triggered,
            this, &DocumentViewport::deleteSelectedObjects);

    menu.exec(globalPos);
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
        updateZoomGesture(zoomFactor, SN_WHEEL_POS(event));
        
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
        // 120 units = one step, scroll by s_wheelScrollSpeed document units per step
        qreal scrollSpeed = s_wheelScrollSpeed;
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
        
        // Plain wheel (no modifier) → Immediate scroll (unchanged behavior).
        // A discrete mouse wheel reports angleDelta (no pixelDelta); a touchpad
        // reports pixelDelta as a high-frequency stream. Flag the discrete wheel
        // as a "stepped" scroll so the Qt5 build renders it synchronously instead
        // of flashing blank (see onScrollActivity). Qt6 ignores the flag.
        const bool steppedWheel = pixelDelta.isNull() && !angleDelta.isNull();
        scrollBy(scrollDelta, steppedWheel);
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
    
    // ===== Note: perf HUD toggle moved to MainWindow =====
    // The paint instrumentation toggle is registered as "view.perf_hud" in
    // ShortcutManager so it works in release builds, is remappable, and does
    // not require the viewport to hold keyboard focus.
    
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

    if (hasActiveObjectPointerGesture()) {
        cancelObjectPointerGesture();
    }
    
    QWidget::focusOutEvent(event);
}

void DocumentViewport::hideEvent(QHideEvent* event)
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[DocumentViewport] hideEvent - clearing gesture state"
             << "wasActive:" << m_gesture.isActive();
#endif
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    if (m_inlineEditSession.active)
        commitInlineTextEdit();
    
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

    if (hasActiveObjectPointerGesture()) {
        cancelObjectPointerGesture();
    }
    
    // Release stroke cache when hidden (reclaim memory while not visible)
    if (!m_currentStrokeCache.isNull()) {
        m_currentStrokeCache = QPixmap();
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
    syncTextBoxFormatBar();
}

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void DocumentViewport::enterEvent(QEnterEvent* event)
#else
void DocumentViewport::enterEvent(QEvent* event)
#endif
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
    // Stylus events over the inline editor or the formatting bar arrive here
    // by propagation. Leave them unhandled so Qt synthesizes mouse events for
    // those widgets instead of treating the pen as a canvas interaction.
    if (!m_pointerActive && pointerOverTextOverlay(SN_EVENT_POS(event))) {
        event->ignore();
        return;
    }

    // Determine event type
    PointerEvent::Type peType;
    switch (event->type()) {
        case QEvent::TabletPress:
            peType = PointerEvent::Press;
            if (m_inlineEditSession.active)
                commitInlineTextEdit();
            else {
                closeTextBoxFormatPopups(true);
                finishTextBoxFormatInteraction(true);
            }
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

    // Do not let a stylus press preempt a mouse ObjectSelect gesture and erase
    // its initiating-button/effective-mode state.
    if (m_pointerActive && m_activeSource == PointerEvent::Mouse) {
        event->accept();
        return;
    }
    
    // ===== Tablet Hover Tracking for Eraser Cursor =====
    // TabletMove events arrive even when the pen is hovering (not pressed).
    // We need to track position for eraser cursor even during hover.
    // handlePointerEvent() returns early if m_pointerActive is false,
    // so we handle hover tracking separately here.
    if (event->type() == QEvent::TabletMove && !m_pointerActive) {
#ifdef Q_OS_ANDROID
        // Pre-warm JNI eraser detection during hover (before first press).
        // FindClass + GetStaticMethodID are expensive (~20-50ms on slow devices);
        // doing it here moves the cost to hover time, not pen-down latency.
        initEraserJni();
#endif
        QPointF newPos = SN_EVENT_POS(event);
        
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
                             SN_IS_ERASER_TABLET(event);
        
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
        
        QTouchEvent* touchEvent = static_cast<QTouchEvent*>(event);
        
        // Skip touchpad events — only handle real touchscreen input.
        // On macOS, trackpad gestures are delivered as both raw QTouchEvents AND
        // synthesized QWheelEvent (for scroll) / QNativeGestureEvent (for pinch).
        // Intercepting the raw touch events here would conflict with the OS-level
        // gesture processing.  Letting them fall through keeps 2-finger scroll
        // working normally, while pinch-to-zoom is handled via NativeGesture below.
        if (touchEvent->device() &&
            touchEvent->device()->type() == SN_TOUCHPAD_DEVICE_TYPE) {
            return QWidget::event(event);
        }
        
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
        
        // Check if the touch started on a child widget (like MissingPdfBanner)
        // If so, let Qt's normal event propagation handle it instead of intercepting
        if (event->type() == QEvent::TouchBegin && !SN_TOUCH_POINTS(touchEvent).isEmpty()) {
            QPointF touchPos = SN_TP_POS(SN_TOUCH_POINTS(touchEvent).first());
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
    
    // Handle native gesture events (macOS trackpad pinch-to-zoom).
    // On macOS the trackpad delivers pinch as QNativeGestureEvent with
    // Qt::ZoomNativeGesture, bypassing the touch handler entirely.
    // This works regardless of the TouchGestureMode setting, so trackpad
    // pinch-to-zoom is always available.
    if (event->type() == QEvent::NativeGesture) {
        auto* nge = static_cast<QNativeGestureEvent*>(event);
        
        if (nge->gestureType() == Qt::ZoomNativeGesture) {
            // value() is the incremental scale delta (e.g. 0.02 = 2% zoom in)
            qreal scaleFactor = 1.0 + nge->value();
            if (!qFuzzyCompare(scaleFactor, 1.0)) {
                updateZoomGesture(scaleFactor, SN_NGE_POS(nge));
            }
            event->accept();
            return true;
        }
        
        if (nge->gestureType() == Qt::EndNativeGesture) {
            if (m_gesture.activeType == ViewportGestureState::Zoom) {
                endZoomGesture();
            }
            event->accept();
            return true;
        }
    }
    
    return QWidget::event(event);
}

// ===== PDF Cache Helpers (Task 1.3.6) =====

QPixmap DocumentViewport::lookupCachedPdfPage(const QString& sourceId, int pageIndex, qreal dpi) const
{
    if (!m_document) {
        return QPixmap();
    }

    // Thread-safe cache lookup. Never renders (SP2): safe on the paint path
    // while scrolling.
    QMutexLocker locker(&m_pdfCacheMutex);
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(sourceId, pageIndex, dpi)) {
            return entry.pixmap;  // Cache hit
        }
    }
    return QPixmap();  // Miss - caller decides whether to render synchronously
}

QPixmap DocumentViewport::getCachedPdfPage(const QString& sourceId, int pageIndex, qreal dpi)
{
    if (!m_document) {
        return QPixmap();
    }
    
    // Fast path: return the cached pixmap if present (no render).
    QPixmap cached = lookupCachedPdfPage(sourceId, pageIndex, dpi);
    if (!cached.isNull()) {
        return cached;
    }
    
    // Cache miss - render synchronously (for visible pages that MUST be shown).
    // This should only happen on first paint of a new page (settled state).
    // The expensive render runs without holding the cache mutex; we lock only
    // to insert the result below.
    
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
    QImage pdfImage = m_document->renderPdfPageToImage(sourceId, pageIndex, dpi);
    if (pdfImage.isNull()) {
        return QPixmap();
    }

    // Apply HSL lightness inversion for PDF dark mode
    if (m_isDarkMode && m_pdfDarkModeEnabled) {
        QVector<QRect> imgRegions;
        if (!m_skipImageMasking) {
            imgRegions = m_document->pdfImageRegions(sourceId, pageIndex, dpi);
        }
        DarkModeUtils::invertImageLightness(pdfImage, imgRegions);
    }
    
    m_document->trimPdfStore();
    
    QPixmap pixmap = QPixmap::fromImage(pdfImage);
    
    // Add to cache (thread-safe)
    QMutexLocker locker(&m_pdfCacheMutex);
    
    // Double-check it wasn't added by another thread while we were rendering
    for (const PdfCacheEntry& entry : m_pdfCache) {
        if (entry.matches(sourceId, pageIndex, dpi)) {
            return entry.pixmap;  // Another thread added it
        }
    }
    
    PdfCacheEntry entry;
    entry.sourceId = sourceId;
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
    if (!m_document) {
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
    
    // Collect (sourceId, pdfPageNum) pairs that need preloading, resolving each page
    // to its own PDF source so multi-source documents preload the correct backgrounds.
    // renderPageNum is the index the provider actually uses: for a bundled source it
    // is the compact mini-PDF index (via pageMap); otherwise it equals pdfPageNum.
    struct PreloadItem { QString sourceId; int pdfPageNum; int renderPageNum; QString path; };
    QList<PreloadItem> pagesToPreload;
    {
        QMutexLocker locker(&m_pdfCacheMutex);
        for (int i = preloadStart; i <= preloadEnd; ++i) {
            Page* page = m_document->page(i);
            if (!page || page->backgroundType != Page::BackgroundType::PDF) {
                continue;
            }
            int pdfPageNum = page->pdfPageNumber;
            const QString sourceId = page->pdfSourceId;
            
            // Check if already cached
            bool alreadyCached = false;
            for (const PdfCacheEntry& entry : m_pdfCache) {
                if (entry.matches(sourceId, pdfPageNum, dpi)) {
                    alreadyCached = true;
                    break;
                }
            }
            if (alreadyCached) {
                continue;
            }
            
            QString path = m_document->pdfPathForSource(sourceId);
            if (path.isEmpty()) {
                continue;  // Source unavailable (will render placeholder synchronously)
            }
            const int renderPageNum = m_document->resolveSourcePageIndex(sourceId, pdfPageNum);
            if (renderPageNum < 0) {
                continue;  // Bundled source without this page mapped
            }
            pagesToPreload.append({ sourceId, pdfPageNum, renderPageNum, path });
        }
    }
    
    if (pagesToPreload.isEmpty()) {
        return;  // All pages already cached
    }
    
    // Launch async render for each page that needs caching
    for (const PreloadItem& item : pagesToPreload) {
        const QString sourceId = item.sourceId;
        const int pdfPageNum = item.pdfPageNum;
        const int renderPageNum = item.renderPageNum;
        const QString pdfPath = item.path;
        QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
        
        // Track watcher for cleanup
        m_activePdfWatchers.append(watcher);
        
        // THREAD SAFETY FIX: QPixmap must only be created on the main thread.
        // The background thread returns QImage, and we convert to QPixmap here
        // in the finished handler which runs on the main thread.
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, sourceId, pdfPageNum, dpi]() {
            // BUG-A006 FIX: Check if watcher was cancelled (e.g., by invalidatePdfCache)
            // This happens when document/page changes while render is in progress
            m_activePdfWatchers.removeOne(watcher);
            
            bool wasCancelled = watcher->isCanceled();
            QImage pdfImage;
            if (!wasCancelled) {
                pdfImage = watcher->result();
            }
            delete watcher;
            
            if (wasCancelled) {
                return;
            }
            
            // Check if rendering failed
            if (pdfImage.isNull()) {
                return;
            }
            
            // Guard against document changing between render start and signal delivery
            if (!m_document) {
                return;
            }

            // Apply HSL lightness inversion for PDF dark mode
            if (m_isDarkMode && m_pdfDarkModeEnabled) {
                QVector<QRect> imgRegions;
                if (!m_skipImageMasking) {
                    imgRegions = m_document->pdfImageRegions(sourceId, pdfPageNum, dpi);
                }
                DarkModeUtils::invertImageLightness(pdfImage, imgRegions);
            }

            // SAFE: QPixmap::fromImage on main thread
            QPixmap pixmap = QPixmap::fromImage(pdfImage);
            
            // Add to cache (thread-safe access to shared cache)
            QMutexLocker locker(&m_pdfCacheMutex);
            
            // Check if already added (race condition prevention)
            for (const PdfCacheEntry& entry : m_pdfCache) {
                if (entry.matches(sourceId, pdfPageNum, dpi)) {
                    return;  // Already cached by another path
                }
            }
            
            PdfCacheEntry entry;
            entry.sourceId = sourceId;
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
        QFuture<QImage> future = QtConcurrent::run([renderPageNum, dpi, pdfPath]() -> QImage {
            // Use thread-local cached PDF provider to avoid re-opening the PDF
            // for every page render. Each thread pool worker caches its own provider
            // (keyed by resolved source path).
            ThreadPdfCache& cache = s_threadPdfCache.localData();
            PdfProvider* threadPdf = cache.getOrCreate(pdfPath);
            if (!threadPdf || !threadPdf->isValid()) {
                return QImage();  // Return null image on failure
            }
            
            QImage result = threadPdf->renderPageToImage(renderPageNum, dpi);
            threadPdf->trimStore();
            return result;
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

void DocumentViewport::cancelAndWaitForBackgroundThreads()
{
    if (m_pdfPreloadTimer)
        m_pdfPreloadTimer->stop();
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_activePdfWatchers.clear();
}

void DocumentViewport::invalidatePdfCache()
{
    // Cancel pending async preloads
    if (m_pdfPreloadTimer) {
        m_pdfPreloadTimer->stop();
    }
    
    // Cancel active background PDF render threads but don't wait (non-blocking).
    // Watchers remain in m_activePdfWatchers so the destructor or
    // cancelAndWaitForBackgroundThreads() can properly wait for them.
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
    }
    
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

void DocumentViewport::invalidatePdfCachePage(const QString& sourceId, int pageIndex)
{
    // Thread-safe page removal
    QMutexLocker locker(&m_pdfCacheMutex);
    m_pdfCache.erase(
        std::remove_if(m_pdfCache.begin(), m_pdfCache.end(),
                       [&sourceId, pageIndex](const PdfCacheEntry& entry) {
                           return entry.pageIndex == pageIndex && entry.sourceId == sourceId;
                       }),
        m_pdfCache.end()
    );
}

void DocumentViewport::updatePdfCacheCapacity()
{
    // Calculate visible page count
    QVector<int> visible = visiblePages();
    int visibleCount = static_cast<int>(visible.size());
    
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
    
    // MEMORY OPTIMIZATION: Keep caches/pages for visible ± buffer pages, evict the rest.
    // At high zoom * dpr each page cache is large (capped at MAX_STROKE_CACHE_DIM),
    // so the buffer shrinks to limit total memory while remaining safe for panning.
    qreal effectiveScale = m_zoomLevel * devicePixelRatioF();
    int pageBuffer;
    if (effectiveScale <= 2.0)
        pageBuffer = 2;
    else if (effectiveScale <= 4.0)
        pageBuffer = 1;
    else
        pageBuffer = 0;
    int keepStart = qMax(0, first - pageBuffer);
    int keepEnd = qMin(pageCount - 1, last + pageBuffer);

    // Never preload beyond the eviction keep window, otherwise the next
    // preload/scroll evicts these pages and we reload (and re-decode every
    // image asset) on the following stroke. At high zoom pageBuffer == 0, so
    // this clamps preload to the visible pages only.
    preloadStart = qMax(preloadStart, keepStart);
    preloadEnd   = qMin(preloadEnd, keepEnd);
    
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
                if (m_inlineEditSession.active
                    && m_inlineEditSession.document == m_document
                    && m_inlineEditSession.pageIndex == i) {
                    continue;
                }
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
    const qreal effScale = m_zoomLevel * dpr;

    // Phase O1.7.5: Preload nearby pages (triggers lazy loading if needed)
    // page() will automatically load from disk if not already in memory
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        Page* page = m_document->page(i);  // This triggers lazy load
        if (!page) continue;

        // Skip building the capped cache when this page would switch to
        // Focus tier on the next paint. The capped pixmap (~67 MB at high
        // zoom) would be allocated here only to be `releaseStrokeCache()`'d
        // immediately by `dispatchTileLayer` / `renderPage`. Off-screen-but-
        // nearby pages keep using Capped (chooseRenderTier returns Capped
        // when the tile doesn't intersect the viewport), so they still get
        // the smooth-pan benefit of preload.
        const qreal pageMaxDim = qMax(page->size.width(), page->size.height());
        const bool wouldBeBlurred =
            effScale * pageMaxDim > VectorLayer::MAX_STROKE_CACHE_DIM;
        if (wouldBeBlurred && i >= first && i <= last) {
            continue;
        }

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
    
    // Dynamic margin: at high zoom * dpr, each tile cache is large (up to
    // MAX_STROKE_CACHE_DIM^2 * 4 bytes) but the viewport covers a tiny
    // fraction of a tile. Reduce the margin to limit total memory.
    // At low effective scale the caches are small, so a generous margin
    // is affordable and ensures smooth panning without disk-load stutters.
    qreal effectiveScale = m_zoomLevel * devicePixelRatioF();
    int keepMargin;
    if (effectiveScale <= 2.0)
        keepMargin = 2;
    else if (effectiveScale <= 4.0)
        keepMargin = 1;
    else
        keepMargin = 0;
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    QRectF keepRect = viewRect.adjusted(
        -keepMargin * tileSize, -keepMargin * tileSize,
        keepMargin * tileSize, keepMargin * tileSize);
    
    // Get all loaded tiles and check which to evict
    QVector<Document::TileCoord> loadedTiles = m_document->allLoadedTileCoords();
    
    int evictedCount = 0;
    bool selectionChanged = false;
    
    for (const auto& coord : loadedTiles) {
        if (m_inlineEditSession.active
            && m_inlineEditSession.document == m_document
            && m_inlineEditSession.tileCoord == coord) {
            continue;
        }
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
    
    // M.7.3 / Phase M.9: Eviction no longer changes the outline — the
    // persistent link-outline cache in Document already has every tile
    // on disk, regardless of memory residency.  No emit needed.
    
#ifdef SPEEDYNOTE_DEBUG
    if (evictedCount > 0) {
        qDebug() << "Evicted" << evictedCount << "tiles, remaining:" << m_document->tileCount();
    }
#endif
}

void DocumentViewport::releaseFocusCachesBelowThreshold()
{
    if (!m_document) return;

    const qreal effScale = m_zoomLevel * devicePixelRatioF();

    auto maybeRelease = [effScale](Page* page) {
        if (!page) return;
        const qreal pageMaxDim = qMax(page->size.width(), page->size.height());
        // Same predicate as `chooseRenderTier`: when this is false, no tile
        // on this page would have picked Focus tier on the next paint.
        if (effScale * pageMaxDim <= VectorLayer::MAX_STROKE_CACHE_DIM) {
            for (int i = 0; i < page->layerCount(); ++i) {
                if (auto* layer = page->layer(i)) {
                    if (layer->hasFocusCacheAllocated()) {
                        layer->releaseFocusCache();
                    }
                }
            }
        }
    };

    if (m_document->isEdgeless()) {
        // Tiles all share `EDGELESS_TILE_SIZE`; check once, then sweep.
        const qreal pageMaxDim = Document::EDGELESS_TILE_SIZE;
        if (effScale * pageMaxDim > VectorLayer::MAX_STROKE_CACHE_DIM) {
            return;  // Tiles still pick Focus tier; nothing to release.
        }
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            maybeRelease(m_document->getTile(coord.first, coord.second));
        }
    } else {
        // Paged: only the visible page set could possibly hold focus caches
        // (eviction sweep already cleared the rest); checking visible pages
        // is enough.
        for (int idx : visiblePages()) {
            maybeRelease(m_document->page(idx));
        }
    }
}

// ===== Input Routing (Task 1.3.8) =====

PointerEvent DocumentViewport::mouseToPointerEvent(QMouseEvent* event, PointerEvent::Type type)
{
    PointerEvent pe;
    pe.type = type;
    pe.source = PointerEvent::Mouse;
    pe.viewportPos = SN_MOUSE_POS(event);
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Mouse has no pressure sensitivity
    pe.pressure = 1.0;
    pe.tiltX = 0;
    pe.tiltY = 0;
    pe.rotation = 0;
    
    // Hardware state
    pe.isEraser = false;
    pe.stylusButtons = 0;
    pe.button = event->button();
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
    pe.viewportPos = SN_EVENT_POS(event);
    pe.pageHit = viewportToPage(pe.viewportPos);
    
    // Tablet pressure and tilt
    pe.pressure = event->pressure();
    pe.tiltX = event->xTilt();
    pe.tiltY = event->yTilt();
    pe.rotation = event->rotation();
    
    // Check for eraser - either eraser end of stylus or eraser button
    // Qt6: pointerType() returns the type of pointing device
    // Also check deviceType() as a fallback - some drivers report eraser via device type
    pe.isEraser = SN_IS_ERASER_TABLET(event);

    // Alternative detection: some tablets report eraser via deviceType() instead of pointerType()
    if (!pe.isEraser && SN_IS_STYLUS_TABLET(event)) {
#ifdef SN_HAS_POINTING_DEVICE
        // Qt6 only: check device name for eraser identification
        const QPointingDevice* device = event->pointingDevice();
        if (device && device->name().contains("eraser", Qt::CaseInsensitive)) {
            pe.isEraser = true;
        }
#endif
    }
    
#ifdef Q_OS_ANDROID
    // BUG-A008: Qt on Android doesn't properly translate Android's TOOL_TYPE_ERASER
    // to QPointingDevice::PointerType::Eraser. Query Android directly via JNI.
    // 
    // Performance: JNI class/method lookup is pre-warmed during stylus hover
    // (see initEraserJni()). Only the cheap CallStaticBooleanMethod runs here.
    if (!pe.isEraser) {
        initEraserJni();  // No-op after first call
        
        if (s_eraserActivityClass && s_eraserIsEraserMethod) {
            QJniEnvironment env;
            pe.isEraser = static_cast<bool>(
                env->CallStaticBooleanMethod(s_eraserActivityClass, s_eraserIsEraserMethod));
        }
    }
#endif
    
    // Barrel buttons - Qt provides via buttons()
    // Common mappings: barrel button 1 = Qt::MiddleButton, barrel button 2 = Qt::RightButton
    pe.stylusButtons = static_cast<int>(event->buttons());
    pe.button = event->button();
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
        if (m_eraserMode == EraserMode::Lasso) {
            // Lasso eraser: start drawing a freeform region
            m_lassoPath.clear();
            resetLassoPathCache();
            
            QPointF pt;
            if (m_document->isEdgeless()) {
                pt = viewportToDocument(pe.viewportPos);
                m_eraserLassoPageIndex = -1;
            } else if (pe.pageHit.valid()) {
                pt = pe.pageHit.pagePoint;
                m_eraserLassoPageIndex = pe.pageHit.pageIndex;
            } else {
                return;
            }
            
            m_lassoPath << pt;
            m_isDrawingEraserLasso = true;
            m_pointerActive = true;
            update();
        } else {
            eraseAt(pe);
            qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
            QRectF cursorRectF(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                               eraserRadius * 2, eraserRadius * 2);
            update(QRegion(cursorRectF.toAlignedRect(), QRegion::Ellipse));
        }
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
    } else if (m_currentTool == ToolType::Pan) {
        handlePointerPress_Pan(pe);
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
        if (m_isDrawingEraserLasso) {
            // Lasso eraser: append point (mirrors handlePointerMove_Lasso logic)
            QPointF pt;
            if (m_document->isEdgeless()) {
                pt = viewportToDocument(pe.viewportPos);
            } else if (pe.pageHit.valid() && pe.pageHit.pageIndex == m_eraserLassoPageIndex) {
                pt = pe.pageHit.pagePoint;
            } else if (m_eraserLassoPageIndex >= 0) {
                QPointF docPos = viewportToDocument(pe.viewportPos);
                QPointF pageOrigin = pagePosition(m_eraserLassoPageIndex);
                pt = docPos - pageOrigin;
            } else {
                return;
            }
            
            // Point decimation: compare against the last ADDED path point in doc coords
            bool hasLastPoint = !m_lassoPath.isEmpty();
            QPointF lastPt;
            if (hasLastPoint) {
                lastPt = m_lassoPath.last();
                qreal dx = pt.x() - lastPt.x();
                qreal dy = pt.y() - lastPt.y();
                if (dx * dx + dy * dy < 4.0) {
                    return;
                }
            }
            
            m_lassoPath << pt;
            
            // Dirty rect: convert actual path endpoints to viewport coords
            if (hasLastPoint) {
                QPointF vpLast, vpCurrent;
                if (m_document->isEdgeless()) {
                    vpLast = documentToViewport(lastPt);
                    vpCurrent = documentToViewport(pt);
                } else {
                    QPointF pageOrigin = pagePosition(m_eraserLassoPageIndex);
                    vpLast = documentToViewport(lastPt + pageOrigin);
                    vpCurrent = documentToViewport(pt + pageOrigin);
                }
                QRectF dirtyRect = QRectF(vpLast, vpCurrent).normalized();
                dirtyRect.adjust(-4, -4, 4, 4);
                update(dirtyRect.toRect());
            } else {
                QPointF vpPt = m_document->isEdgeless()
                    ? documentToViewport(pt)
                    : documentToViewport(pt + pagePosition(m_eraserLassoPageIndex));
                QRectF dirtyRect(vpPt.x() - 5, vpPt.y() - 5, 10, 10);
                update(dirtyRect.toRect());
            }
            return;
        }
        
        eraseAt(pe);
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
        
        QRectF oldRectF(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius,
                        eraserRadius * 2, eraserRadius * 2);
        QRectF newRectF(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius,
                        eraserRadius * 2, eraserRadius * 2);
        
        QRegion dirtyRegion(oldRectF.toAlignedRect(), QRegion::Ellipse);
        dirtyRegion += QRegion(newRectF.toAlignedRect(), QRegion::Ellipse);
        update(dirtyRegion);
        return;
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
    
    if (m_currentTool == ToolType::Pan && m_isPanToolDragging) {
        handlePointerMove_Pan(pe);
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
    
    // Eraser lasso: finalize and delete strokes inside the region
    if (m_isDrawingEraserLasso) {
        if (m_lassoPath.size() >= 2) {
            m_lassoPath << m_lassoPath.first();
        }
        finalizeEraserLasso();
        
        m_isDrawingEraserLasso = false;
        m_eraserLassoPageIndex = -1;
        m_lassoPath.clear();
        m_pointerActive = false;
        m_activeSource = PointerEvent::Unknown;
        m_hardwareEraserActive = false;
        
        update();
        preloadStrokeCaches();
        return;
    }
    
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
    
    // Pan tool - finalize pan gesture
    if (m_currentTool == ToolType::Pan && m_isPanToolDragging) {
        handlePointerRelease_Pan(pe);
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
        // Marker uses fixed pressure (1.0) for consistent thickness.
        // Pen applies the preset's min-width floor at capture time.
        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = useFixedPressure ? 1.0 : applyPenPressureFloor(pe.pressure);
        pt.timestamp = pe.timestamp;
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
    addPointToStroke(pe.pageHit.pagePoint, effectivePressure, pe.timestamp);
}

void DocumentViewport::continueStroke(const PointerEvent& pe)
{
    if (!m_isDrawing || !m_document) return;
    
    // Task 2.8: Marker uses fixed pressure (1.0) for consistent thickness.
    // Pen applies the active preset's min-width floor (baked into pressure).
    bool useFixedPressure = (m_currentTool == ToolType::Marker);
    qreal effectivePressure = useFixedPressure ? 1.0 : applyPenPressureFloor(pe.pressure);

    // For edgeless mode, use document coordinates directly
    if (m_document->isEdgeless()) {
        QPointF docPt = viewportToDocument(pe.viewportPos);

        // Point decimation (same logic as addPointToStroke but for document coords)
        // Zoom-aware: threshold is constant in screen pixels, not document space.
        if (!m_currentStroke.points.isEmpty()) {
            const QPointF& lastPos = m_currentStroke.points.last().pos;
            qreal dx = docPt.x() - lastPos.x();
            qreal dy = docPt.y() - lastPos.y();
            qreal distSq = dx * dx + dy * dy;

            qreal docThreshold = MIN_SCREEN_DISTANCE / m_zoomLevel;
            if (distSq < docThreshold * docThreshold) {
                // Point too close - but update pressure peak if higher.
                // Compare the floored effective pressure, not the raw reading,
                // so the stored pressure can never slip below the min-width floor.
                if (!useFixedPressure && effectivePressure > m_currentStroke.points.last().pressure) {
                    m_currentStroke.points.last().pressure = effectivePressure;
                }
                return;
            }
        }

        StrokePoint pt;
        pt.pos = docPt;
        pt.pressure = effectivePressure;
        pt.timestamp = pe.timestamp;
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
    addPointToStroke(pagePos, effectivePressure, pe.timestamp);
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
            pushPageStrokeUndo(m_activeDrawingPage, UndoAction::AddStroke, m_currentStroke, page->activeLayerIndex);
        }
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;  // Reset incremental rendering state
    
    // Keep m_currentStrokeCache allocated for reuse by the next stroke.
    // resetCurrentStrokeCache() will clear it with fill(Qt::transparent).
    // This avoids a costly 4MB+ dealloc+realloc cycle on every stroke start,
    // which matters on bandwidth-limited devices (Cortex-A9, etc.).
    // The cache is released on resize or when the widget is hidden.
    
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
        
        // Add to tile's layer (addStroke handles cache update incrementally)
        layer->addStroke(localStroke);
        
        // Mark tile as dirty for persistence (Phase E5)
        m_document->markTileDirty(seg.coord);
        
        addedStrokes.append({seg.coord, localStroke});
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "  -> Tile" << seg.coord.first << "," << seg.coord.second
                 << "points:" << localStroke.points.size();
#endif
    }
    
    if (!addedStrokes.isEmpty()) {
        UndoAction undoAction;
        undoAction.type = UndoAction::AddStroke;
        undoAction.layerIndex = m_edgelessActiveLayerIndex;
        for (const auto& pair : addedStrokes) {
            UndoAction::StrokeSegment seg;
            seg.tileCoord = pair.first;
            seg.stroke = pair.second;
            undoAction.segments.append(seg);
            m_ocrDirtyTiles.insert(pair.first);
        }
        pushUndoAction(undoAction);
        emit strokesChanged();
    }
    
    // Clear stroke state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;
    // Keep m_currentStrokeCache for reuse (see finishStroke() comment)
    
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
        
        // Add to tile's layer (addStroke handles cache update incrementally)
        layer->addStroke(localStroke);
        
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
            m_document->markTileDirty(startTile);
            
            {
                UndoAction undoAction;
                undoAction.type = UndoAction::AddStroke;
                undoAction.layerIndex = m_edgelessActiveLayerIndex;
                UndoAction::StrokeSegment seg;
                seg.tileCoord = startTile;
                seg.stroke = localStroke;
                undoAction.segments.append(seg);
                m_ocrDirtyTiles.insert(startTile);
                pushUndoAction(undoAction);
                emit strokesChanged();
            }
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
                m_document->markTileDirty(seg.coord);
                
                addedStrokes.append({seg.coord, localStroke});
            }
            
            if (!addedStrokes.isEmpty()) {
                UndoAction undoAction;
                undoAction.type = UndoAction::AddStroke;
                undoAction.layerIndex = m_edgelessActiveLayerIndex;
                for (const auto& pair : addedStrokes) {
                    UndoAction::StrokeSegment seg;
                    seg.tileCoord = pair.first;
                    seg.stroke = pair.second;
                    undoAction.segments.append(seg);
                    m_ocrDirtyTiles.insert(pair.first);
                }
                pushUndoAction(undoAction);
                emit strokesChanged();
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
        
        // Mark page dirty for lazy save (BUG FIX: was missing)
        m_document->markPageDirty(m_straightLinePageIndex);
        
        // Push to undo stack (same pattern as finishStroke)
        pushPageStrokeUndo(m_straightLinePageIndex, UndoAction::AddStroke, stroke, page->activeLayerIndex);
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
        if (!isEdgeless) {
            int srcPage = m_isDrawingEraserLasso ? m_eraserLassoPageIndex
                                                 : m_lassoSelection.sourcePageIndex;
            if (srcPage >= 0)
                pageOrigin = pagePosition(srcPage);
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
        
        m_lastRenderedLassoIdx = static_cast<int>(m_lassoPath.size()) - 1;
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

    beginObjectPointerGesture(pe);

    // The right button is a quick inverse of the persistent mode, so a
    // right-press that lands on an object means "act on this one" rather than
    // "stack a new one on top of it". The left button in Create mode still
    // inserts wherever it is pressed.
    InsertedObject* const alternateTarget =
        (pe.source == PointerEvent::Mouse
         && m_objectGestureButton == Qt::RightButton)
        ? objectAtPoint(viewportToDocument(pe.viewportPos))
        : nullptr;
    m_contextMenuObjectId = alternateTarget ? alternateTarget->id : QString();

    // The right mouse button temporarily uses the opposite action mode without
    // changing the persistent action-bar state.
    if (m_objectGestureActionMode == ObjectActionMode::Create
        && !alternateTarget) {
        PageHit hit = viewportToPage(pe.viewportPos);
        if (hit.pageIndex < 0) {
            // Click not on any page - ignore in paged mode
            if (!m_document->isEdgeless()) {
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "handlePointerPress_ObjectSelect: Create mode click not on page";
#endif
                cancelObjectPointerGesture();
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
            // Re-entrancy guard: see m_objectInsertDialogActive comment in
            // DocumentViewport.h. Without this, stylus presses leaked into
            // the modal QFileDialog's nested event loop on ChromeOS Crostini /
            // KDE Plasma 6 Wayland re-trigger this branch and open another
            // dialog (stack-of-dialogs crash).
            if (m_objectInsertDialogActive) {
                cancelObjectPointerGesture();
                return;
            }
            m_objectInsertDialogActive = true;
            // Open file dialog and insert image
            // Note: insertImageFromDialog() positions at viewport center for now
            // TODO: Create insertImageAtPosition() for click-to-place
            insertImageFromDialog();   // blocking modal
            // Defer clearing the guard by one event-loop tick so any tablet
            // events delivered during the modal (and still queued when it
            // closes) are drained and rejected by the guard before we accept
            // a fresh canvas tap.
            QTimer::singleShot(0, this, [this]() {
                m_objectInsertDialogActive = false;
            });
            cancelObjectPointerGesture();
        } else if (m_objectInsertMode == ObjectInsertMode::Link) {
            // Create empty LinkObject at position
            // Pass viewportPos so edgeless mode can determine correct tile
            createLinkObjectAtPosition(hit.pageIndex, hit.pagePoint, pe.viewportPos);
            cancelObjectPointerGesture();
        } else if (m_objectInsertMode == ObjectInsertMode::Text) {
            m_isCreatingTextBox = true;
            if (m_document && m_document->isEdgeless()) {
                m_textBoxCreateStartDoc = viewportToDocument(pe.viewportPos);
            } else {
                m_textBoxCreateStartDoc = hit.pagePoint;
            }
            m_textBoxCreatePageIndex = hit.pageIndex;
            m_pointerActive = true;
            return;
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
            m_resizeObjectPageIndex = -1;              // Resolved by the search below
            m_hasResizeTextBoxState = false;
            m_textBoxResizeActivated = false;
            m_textBoxResizeChanged = false;
            if (obj->type() == QLatin1String("textbox")
                && (handle == HandleHit::Left || handle == HandleHit::Right)) {
                auto* textBox = static_cast<TextBoxObject*>(obj);
                m_resizeOriginalTextBoxState = textBox->captureState();
                m_resizeBaseTextBoxState = m_resizeOriginalTextBoxState;
                m_resizeLastAcceptedTextBoxState = m_resizeOriginalTextBoxState;
                m_hasResizeTextBoxState = true;
            }
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
                        // Cache for the per-frame page containment clamp
                        m_resizeObjectPageIndex = i;
                        break;
                    }
                }
            }
            m_resizeObjectDocCenter = docPos + QPointF(obj->size.width() / 2.0, 
                                                        obj->size.height() / 2.0);
            
            // Phase O4.1: Capture background for fast resize rendering
            captureObjectDragBackground();
            if (m_hasResizeTextBoxState) {
                // Reflow changes line breaks and height; stretching the old
                // pixmap would visually scale text and contradict the model.
                m_dragObjectRenderedCache = QPixmap();
            }
            
            return;  // Don't start object drag
            }
            // LinkObject: fall through to handle as drag instead
        }
    }
    
    // Convert to document coordinates
    QPointF docPoint = viewportToDocument(pe.viewportPos);
    
    // Hit test for object
    InsertedObject* hitObject = objectAtPoint(docPoint);

    // Ctrl+Click link following (Phase 2E)
    if ((pe.modifiers & Qt::ControlModifier) && hitObject) {
        auto* textBox = dynamic_cast<TextBoxObject*>(hitObject);
        if (textBox && !textBox->text.isEmpty()) {
            QString href;

            // Strategy 1: use the same layout and coordinate transform as
            // rendering/search. The helper also handles rotated text boxes.
            bool foundLocal = false;
            QPointF localPos;
            if (m_document->isEdgeless()) {
                for (const auto& coord : m_document->allLoadedTileCoords()) {
                    Page* tile = m_document->getTile(coord.first, coord.second);
                    if (!tile) continue;
                    for (const auto& obj : tile->objects) {
                        if (obj.get() == hitObject) {
                            const QPointF tileOrigin(
                                coord.first * Document::EDGELESS_TILE_SIZE,
                                coord.second * Document::EDGELESS_TILE_SIZE);
                            localPos =
                                docPoint - tileOrigin - textBox->position;
                            foundLocal = true;
                            break;
                        }
                    }
                    if (foundLocal) break;
                }
            } else {
                const int pageIdx = pageAtPoint(docPoint);
                if (pageIdx >= 0) {
                    localPos =
                        docPoint - pagePosition(pageIdx) - textBox->position;
                    foundLocal = true;
                }
            }
            if (foundLocal)
                href = textBox->anchorAtLocalPoint(localPos, m_zoomLevel);

            // Strategy 2: Regex for Markdown [text](url) links
            if (href.isEmpty()) {
                static const QRegularExpression mdLinkRx(
                    QStringLiteral("\\[([^\\]]*)\\]\\(([^)]+)\\)"));
                QRegularExpressionMatchIterator it = mdLinkRx.globalMatch(textBox->text);
                if (it.hasNext()) {
                    href = it.next().captured(2).trimmed();
                }
            }

            // Strategy 3: Regex for bare URLs (https://..., http://...)
            if (href.isEmpty()) {
                static const QRegularExpression bareUrlRx(
                    QStringLiteral("\\bhttps?://[^\\s)>]+"));
                QRegularExpressionMatch m = bareUrlRx.match(textBox->text);
                if (m.hasMatch()) {
                    href = m.captured(0).trimmed();
                }
            }

            if (!href.isEmpty()) {
                cancelObjectPointerGesture();
                QDesktopServices::openUrl(QUrl(href));
                return;
            }
        }
    }

    // Double-click: edit a user text box, or offer to convert OCR text
    static QElapsedTimer lastObjClickTimer;
    static QPointF lastObjClickPos;
    static int objClickCount = 0;

    if (hitObject && lastObjClickTimer.isValid()
        && lastObjClickTimer.elapsed() < 400
        && QLineF(lastObjClickPos, pe.viewportPos).length() < 5.0) {
        objClickCount++;
    } else {
        objClickCount = 1;
    }
    lastObjClickTimer.restart();
    lastObjClickPos = pe.viewportPos;

    if (objClickCount == 2 && hitObject) {
        const QString t = hitObject->type();
        if (t == QLatin1String("textbox")) {
            cancelObjectPointerGesture();
            if (!m_selectedObjects.contains(hitObject)) {
                deselectAllObjects();
                selectObject(hitObject, false);
            }
            startInlineTextEdit(
                static_cast<TextBoxObject*>(hitObject), false);
            return;
        }
        if (t == QLatin1String("ocr_text")) {
            cancelObjectPointerGesture();
            if (!m_selectedObjects.contains(hitObject)) {
                deselectAllObjects();
                selectObject(hitObject, false);
            }
            emit convertOcrTextRequested(hitObject);
            return;
        }
    }

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
            
            // O2.3.2: Store original positions for undo, and so every move can
            // be recomputed from the start position instead of accumulated
            m_objectOriginalPositions.clear();
            for (InsertedObject* obj : m_selectedObjects) {
                if (obj) {
                    m_objectOriginalPositions[obj->id] = obj->position;
                }
            }
            captureObjectDragOriginPages();
            
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

    // Phase 2C: Rubber-band for text box creation
    if (m_isCreatingTextBox) {
        m_lastPointerPos = pe.viewportPos;
        update();
        return;
    }
    
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
        // Total delta from the drag start, NOT an incremental step: page
        // clamping would otherwise swallow movement and pin the selection to
        // the edge it first touched.
        updateObjectDrag(docPoint - m_objectDragStartDoc);
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
    if (pe.source == PointerEvent::Mouse
        && m_objectGestureButton != Qt::NoButton
        && pe.button != m_objectGestureButton) {
        if (!(pe.buttons & m_objectGestureButton)) {
            // The initiating release was lost (for example during input-source
            // interleaving). Roll back rather than leaving a stuck preview.
            cancelObjectPointerGesture();
        }
        return;
    }

    auto finishGesture = [this]() {
        m_pointerActive = false;
        m_activeSource = PointerEvent::Unknown;
        m_hardwareEraserActive = false;
        resetObjectPointerGesture();
    };

    // Phase 2C: Finalize text box creation
    if (m_isCreatingTextBox) {
        m_isCreatingTextBox = false;

        PageHit releaseHit = viewportToPage(pe.viewportPos);
        QPointF releasePagePoint;
        int pageIndex = m_textBoxCreatePageIndex;

        if (m_document && m_document->isEdgeless()) {
            releasePagePoint = viewportToDocument(pe.viewportPos);
        } else {
            releasePagePoint = (releaseHit.pageIndex == pageIndex && releaseHit.pageIndex >= 0)
                ? releaseHit.pagePoint : m_textBoxCreateStartDoc;
        }

        const QRectF rect = proposedTextBoxCreationRect(
            m_textBoxCreateStartDoc, releasePagePoint, pageIndex);

        finishGesture();
        createTextBoxAtRect(pageIndex, rect, pe.viewportPos);
        return;
    }
    
    // Phase O3.1.3: Finalize resize/rotate operation
    if (m_isResizingObject) {
        InsertedObject* obj = m_selectedObjects.isEmpty() ? nullptr : m_selectedObjects.first();
        // Check if any transform property changed (position, size, or rotation)
        bool changed = m_hasResizeTextBoxState
            ? m_textBoxResizeChanged
            : obj && (obj->size != m_resizeOriginalSize ||
                      obj->position != m_resizeOriginalPosition ||
                      obj->rotation != m_resizeOriginalRotation);
        if (m_hasResizeTextBoxState && !changed && obj
            && obj->type() == QLatin1String("textbox")) {
            static_cast<TextBoxObject*>(obj)->applyState(
                m_resizeOriginalTextBoxState);
        }
        if (changed) {
            // Phase O3.1.5/O3.1.8.3: Create undo entry for resize/rotate
            bool aspectLock = true;
            if (auto* img = dynamic_cast<ImageObject*>(obj))
                aspectLock = img->maintainAspectRatio;
            
            // Mark dirty
            if (m_document) {
                if (m_document->isEdgeless()) {
                    // May need to relocate to different tile if position changed
                    relocateObjectsToCorrectTiles();
                    // Mark tile dirty - use cached tile coord for efficiency
                    m_document->markTileDirty(m_dragObjectTileCoord);
                } else {
                    int pageIdx = m_resizeObjectPageIndex >= 0
                        ? m_resizeObjectPageIndex
                        : ((m_dragObjectPageIndex >= 0)
                            ? m_dragObjectPageIndex
                            : m_currentPageIndex);
                    m_document->markPageDirty(pageIdx);
                    m_pendingThumbnailPages.insert(pageIdx);
                    emit pageModified(pageIdx);
                }
            }
            pushObjectResizeUndo(obj, m_resizeOriginalPosition,
                                 m_resizeOriginalSize,
                                 m_resizeOriginalRotation, aspectLock,
                                 m_hasResizeTextBoxState
                                     ? &m_resizeOriginalTextBoxState
                                     : nullptr);
            
            emit documentModified();
            if (m_hasResizeTextBoxState)
                emit textBoxLayoutCommitted();
        }
        
        m_isResizingObject = false;
        m_objectResizeHandle = HandleHit::None;
        m_resizeObjectPageIndex = -1;
        m_hasResizeTextBoxState = false;
        m_textBoxResizeActivated = false;
        m_textBoxResizeChanged = false;
        finishGesture();
        
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
                    // Paged mode: relocate objects that crossed page boundaries
                    relocateObjectsToCorrectPages();
                    
                    // Safety net for objects that were already outside their
                    // page (saved by an older build, or whose origin page was
                    // not loaded when the drag started). Runs before the undo
                    // entries below so the repair is undoable.
                    for (InsertedObject* obj : m_selectedObjects) {
                        if (!obj) continue;
                        clampObjectToPage(obj, pageIndexForObject(obj));
                    }
                    
                    int pageIdx = (m_dragObjectPageIndex >= 0) ? m_dragObjectPageIndex : m_currentPageIndex;
                    m_document->markPageDirty(pageIdx);
                }
            }
            
            // Create undo entry for each moved object
            for (InsertedObject* obj : m_selectedObjects) {
                if (!obj) continue;
                auto it = m_objectOriginalPositions.find(obj->id);
                if (it == m_objectOriginalPositions.end()) continue;
                QPointF oldPos = it.value();

                Document::TileCoord oldTile = {0, 0};
                Document::TileCoord newTile = {0, 0};
                int oldPageIdx = -1;
                int newPageIdx = -1;

                if (m_document->isEdgeless()) {
                    if (m_selectedObjects.size() == 1) {
                        newTile = m_dragObjectTileCoord;
                    } else {
                        for (const auto& coord : m_document->allLoadedTileCoords()) {
                            Page* tile = m_document->getTile(coord.first, coord.second);
                            if (tile && tile->objectById(obj->id)) {
                                newTile = coord;
                                break;
                            }
                        }
                    }
                    oldTile = newTile;
                } else {
                    // Per-object origin recorded at drag start. The cached
                    // m_dragObjectPageIndex only covers single selections, so
                    // a multi-select drag across pages used to record the
                    // wrong source page in the undo entry.
                    int srcPage = (m_dragObjectPageIndex >= 0) ? m_dragObjectPageIndex : m_currentPageIndex;
                    oldPageIdx = m_objectOriginalPageIndices.value(obj->id, srcPage);
                    
                    // Where it ended up (relocation may have moved it). Loaded
                    // pages only - Document::page() loads from disk on demand,
                    // so a full sweep would page in the whole notebook.
                    newPageIdx = pageIndexForObject(obj);
                    if (newPageIdx < 0) newPageIdx = oldPageIdx;
                }

                if (oldPos != obj->position || oldPageIdx != newPageIdx) {
                    pushObjectMoveUndo(obj, oldPos, m_currentPageIndex, oldTile, newTile,
                                       oldPageIdx, newPageIdx);
                    if (!m_document->isEdgeless()) {
                        m_pendingThumbnailPages.insert(oldPageIdx >= 0 ? oldPageIdx : m_currentPageIndex);
                        if (newPageIdx >= 0 && newPageIdx != oldPageIdx)
                            m_pendingThumbnailPages.insert(newPageIdx);
                    }
                }
            }
        }
        
        // Clear original positions
        m_objectOriginalPositions.clear();
        m_objectOriginalPageIndices.clear();
        m_isDraggingObjects = false;
        
        if (moved)
            emit documentModified();

        // Phase O4.1: Clear background snapshot and object cache, trigger full re-render
        m_objectDragBackgroundSnapshot = QPixmap();
        m_dragObjectRenderedCache = QPixmap();
        update();
    }
    
    finishGesture();
}

void DocumentViewport::clearObjectSelection()
{
    // The inline editor is anchored to a selected object, so it cannot outlive
    // the selection: leaving it up would float a live editor over a canvas with
    // nothing selected and silently drop whatever the user typed next.
    if (m_inlineEditSession.active)
        commitInlineTextEdit();

    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    cancelObjectPointerGesture();

    bool hadSelection = !m_selectedObjects.isEmpty();
    m_selectedObjects.clear();
    m_hoveredObject = nullptr;
    if (hadSelection) {
        for (int p : m_pendingThumbnailPages)
            emit pageModified(p);
        m_pendingThumbnailPages.clear();
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

QVector<DocumentViewport::PageRelocation> DocumentViewport::relocateObjectsToCorrectPages()
{
    QVector<PageRelocation> result;
    if (!m_document || m_document->isEdgeless() || m_selectedObjects.isEmpty())
        return result;

    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;

        // Find which page currently owns this object. Loaded pages only:
        // Document::page() loads from disk on demand, so walking every index
        // would pull an entire notebook into memory on each drag release.
        int currentPage = pageIndexForObject(obj);
        if (currentPage < 0) continue;

        QPointF pageOrigin = pagePosition(currentPage);
        QPointF docCenter = pageOrigin + obj->position + QPointF(obj->size.width() / 2.0,
                                                                  obj->size.height() / 2.0);
        // Resolved by centre; points in a page gap snap to the nearest page
        int targetPage = nearestPageToPoint(docCenter);
        if (targetPage < 0) targetPage = currentPage;

        if (targetPage == currentPage) continue;

        QPointF oldPos = obj->position;
        QPointF targetOrigin = pagePosition(targetPage);
        QPointF newPos = (pageOrigin + obj->position) - targetOrigin;
        
        // Pages can differ in size, so a position that was valid on the old
        // page is not necessarily inside the new one.
        newPos = ObjectConstraints::clampPosition(newPos, obj->size,
                                                  m_document->pageSizeAt(targetPage));

        Page* oldPage = m_document->page(currentPage);
        Page* newPage = m_document->page(targetPage);
        if (!oldPage || !newPage) continue;

        auto extracted = oldPage->extractObject(obj->id);
        if (!extracted) continue;
        extracted->position = newPos;
        InsertedObject* newPtr = extracted.get();
        newPage->addObject(std::move(extracted));

        // Update the pointer in m_selectedObjects (old unique_ptr moved,
        // but Page::addObject takes ownership of a new unique_ptr;
        // the raw pointer is still valid because extractObject returns
        // ownership and addObject takes it, keeping the same heap address).
        Q_UNUSED(newPtr);

        m_document->markPageDirty(currentPage);
        m_document->markPageDirty(targetPage);

        result.append({obj->id, currentPage, targetPage, oldPos, newPos});
    }

    return result;
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
        closeTextBoxFormatPopups(true);
        finishTextBoxFormatInteraction(true);
        emit objectSelectionChanged();
        
        // Phase C.2.4: Auto-switch insert mode based on selected object type.
        // Assign directly rather than calling setObjectInsertMode(): selection
        // happens during a Select press, and the public setter intentionally
        // cancels active gestures initiated by an external mode change.
        if (m_selectedObjects.size() == 1) {
            InsertedObject* selected = m_selectedObjects.first();
            ObjectInsertMode newMode = m_objectInsertMode;
            
            if (selected->type() == "image") {
                newMode = ObjectInsertMode::Image;
            } else if (selected->type() == "link") {
                newMode = ObjectInsertMode::Link;
            } else if (selected->type() == "textbox") {
                newMode = ObjectInsertMode::Text;
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
    
    if (m_selectedObjects.contains(obj)) {
        closeTextBoxFormatPopups(true);
        finishTextBoxFormatInteraction(true);
    }
    if (m_selectedObjects.removeOne(obj)) {
        emit objectSelectionChanged();
        update();
    }
}

void DocumentViewport::deselectAllObjects()
{
    if (hasActiveObjectPointerGesture()) {
        cancelObjectPointerGesture();
    }

    // Commit before the early return: page deletion deselects first and then
    // snapshots the page, so committing here is what puts in-progress text into
    // the undo snapshot instead of losing it behind a still-visible editor.
    if (m_inlineEditSession.active)
        commitInlineTextEdit();

    if (m_selectedObjects.isEmpty()) return;

    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    m_selectedObjects.clear();
    for (int p : m_pendingThumbnailPages)
        emit pageModified(p);
    m_pendingThumbnailPages.clear();
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
    if (!s_objectClipboard.isEmpty()) {
        clearObjectClipboard();
    }
}

void DocumentViewport::clearObjectClipboard()
{
    if (s_objectClipboard.isEmpty()) return;
    
    s_objectClipboard.clear();
    s_objectClipboardAssets.clear();
    emit objectClipboardChanged(false);
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "clearObjectClipboard: Object clipboard cleared";
#endif
}

void DocumentViewport::deselectObjectById(const QString& objectId)
{
    for (int i = static_cast<int>(m_selectedObjects.size()) - 1; i >= 0; --i) {
        if (m_selectedObjects[i] && m_selectedObjects[i]->id == objectId) {
            closeTextBoxFormatPopups(true);
            finishTextBoxFormatInteraction(true);
            m_selectedObjects.removeAt(i);
            emit objectSelectionChanged();
            update();
            return;
        }
    }
}

int DocumentViewport::pageIndexForObject(InsertedObject* obj) const
{
    if (!m_document || !obj || m_document->isEdgeless()) {
        return -1;
    }
    
    // Resolved by ownership rather than from a cache: callers use this after
    // relocation, when any cached page index may be stale. Only loaded pages
    // are searched so this never triggers lazy page loading.
    for (int i : m_document->loadedPageIndices()) {
        Page* page = m_document->page(i);
        if (page && page->objectById(obj->id)) {
            return i;
        }
    }
    
    return -1;
}

void DocumentViewport::captureObjectDragOriginPages()
{
    m_objectOriginalPageIndices.clear();
    if (!m_document || m_document->isEdgeless()) {
        return;
    }
    
    // Build an id set once so the page scan is a single pass
    QSet<QString> wanted;
    for (InsertedObject* obj : m_selectedObjects) {
        if (obj) wanted.insert(obj->id);
    }
    if (wanted.isEmpty()) {
        return;
    }
    
    for (int i : m_document->loadedPageIndices()) {
        Page* page = m_document->page(i);
        if (!page) continue;
        for (const auto& pageObj : page->objects) {
            if (pageObj && wanted.contains(pageObj->id)) {
                m_objectOriginalPageIndices[pageObj->id] = i;
            }
        }
    }
}

void DocumentViewport::updateObjectDrag(const QPointF& totalDelta)
{
    if (m_selectedObjects.isEmpty()) {
        return;
    }
    
    // Unclamped ("free") bounding box of the whole selection, in document
    // coordinates. Objects still hold their origin page's local coordinates --
    // ownership only changes on release.
    QRectF freeGroupRect;
    bool haveGroupRect = false;
    int sharedOriginPage = -1;
    bool oneOriginPage = true;
    
    const bool paged = m_document && !m_document->isEdgeless();
    
    if (paged) {
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            auto posIt = m_objectOriginalPositions.constFind(obj->id);
            if (posIt == m_objectOriginalPositions.constEnd()) continue;
            
            int originPage = m_objectOriginalPageIndices.value(obj->id, -1);
            if (originPage < 0) continue;
            
            if (sharedOriginPage < 0) {
                sharedOriginPage = originPage;
            } else if (sharedOriginPage != originPage) {
                oneOriginPage = false;
            }
            
            QRectF objRect(pagePosition(originPage) + *posIt + totalDelta, obj->size);
            freeGroupRect = haveGroupRect ? freeGroupRect.united(objRect) : objRect;
            haveGroupRect = true;
        }
    }
    
    if (paged && !oneOriginPage) {
        // A selection spanning several pages has no single target page, and its
        // bounding box is larger than any page. Clamping that box would yank
        // the whole group to a page centre, so clamp each object against the
        // page it came from instead. The trade-off is that such a selection
        // cannot be carried onto another page in one drag.
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            auto posIt = m_objectOriginalPositions.constFind(obj->id);
            if (posIt == m_objectOriginalPositions.constEnd()) continue;
            
            obj->position = clampObjectPositionToPage(
                m_objectOriginalPageIndices.value(obj->id, -1),
                *posIt + totalDelta, obj->size);
        }
    } else {
        // Correction that keeps the selection inside a page. Stays zero in
        // edgeless mode, which has no edges to clamp against.
        QPointF correction;
        
        if (haveGroupRect) {
            // Resolve the target page from the FREE centre, not the clamped
            // one: that is what lets a drag carry the selection across a page
            // gap onto the next page instead of sticking at the edge.
            int targetPage = nearestPageToPoint(freeGroupRect.center());
            
            if (targetPage >= 0) {
                QRectF localRect = freeGroupRect.translated(-pagePosition(targetPage));
                correction = ObjectConstraints::correctionToPage(
                    localRect, m_document->pageSizeAt(targetPage));
            }
        }
        
        const QPointF appliedDelta = totalDelta + correction;
        for (InsertedObject* obj : m_selectedObjects) {
            if (!obj) continue;
            auto posIt = m_objectOriginalPositions.constFind(obj->id);
            if (posIt == m_objectOriginalPositions.constEnd()) continue;
            obj->position = *posIt + appliedDelta;
        }
    }
    
    // Note: Page/tile dirty marking is done on drag release (O2.3.2)
    // to avoid marking dirty on every micro-movement during drag.
    // Tile boundary crossing is handled in O2.3.4.
    updateTextBoxFormatBarGeometry();
    
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

void DocumentViewport::moveSelectedObjects(const QPointF& delta)
{
    if (m_selectedObjects.isEmpty() || delta.isNull()) {
        return;
    }
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        obj->position += delta;
        clampObjectToPage(obj, pageIndexForObject(obj));
    }
    updateTextBoxFormatBarGeometry();
    
    if (!m_dragUpdateTimer.isValid() || 
        m_dragUpdateTimer.elapsed() >= DRAG_UPDATE_INTERVAL_MS) {
        m_dragUpdateTimer.restart();
        update();
    }
}

void DocumentViewport::pasteForObjectSelect()
{
#ifdef SPEEDYNOTE_DEBUG
    const char* modeName = m_objectInsertMode == ObjectInsertMode::Image ? "Image"
        : m_objectInsertMode == ObjectInsertMode::Link ? "Link" : "Text";
    qDebug() << "pasteForObjectSelect: Called, insertMode =" << modeName;
#endif
    
    // Phase O2.4.2: Tool-aware paste for ObjectSelect tool
    // Paste priority depends on ObjectInsertMode:
    // - Image mode: System clipboard images take priority, then internal clipboard
    // - Link/Text mode: paste internal objects only
    
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard || !clipboard->mimeData()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect: No clipboard or mimeData";
#endif
        return;
    }
    
    const QMimeData* mimeData = clipboard->mimeData();
    
    // ===== Link/Text modes: Internal object clipboard only =====
    // System images belong to the dedicated Image tool and must not interrupt
    // pasting copied objects while another subtype is active.
    if (m_objectInsertMode != ObjectInsertMode::Image) {
        if (!s_objectClipboard.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "pasteForObjectSelect (non-Image mode): Internal clipboard has"
                     << s_objectClipboard.size() << "objects";
#endif
            pasteObjects();
            return;
        }

#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (non-Image mode): "
                    "No internal clipboard content, skipping system image";
#endif
        return;
    }
    
    // ===== Image mode: System clipboard takes priority =====
    // Prefer local files because Explorer often advertises both a URL and a
    // decoded image; the URL preserves the original bytes and format.
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
                
                QImageReader reader(filePath);
                if (reader.canRead()) {
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

    // Fall back to raw image data for screenshots and application clipboards.
    if (mimeData->hasImage()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): Clipboard has raw image";
#endif
        insertImageFromClipboard();
        return;
    }
    
    // Internal object clipboard
    // Even in Image mode, paste internal objects if no system clipboard image
    if (!s_objectClipboard.isEmpty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pasteForObjectSelect (Image mode): Internal clipboard has" 
                 << s_objectClipboard.size() << "objects";
#endif
        pasteObjects();
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "pasteForObjectSelect: Nothing to paste";
#endif
}

bool DocumentViewport::prepareFreshImageForInsertion(ImageObject& imageObject)
{
    if (!m_document || imageObject.pixmap().isNull()) {
        return false;
    }

    const QSizeF insertionBounds = m_document->isEdgeless()
        ? QSizeF(Document::EDGELESS_TILE_SIZE, Document::EDGELESS_TILE_SIZE)
        : m_document->pageSizeAt(m_currentPageIndex);
    if (!insertionBounds.isValid() || insertionBounds.isEmpty()
        || !qIsFinite(insertionBounds.width()) || !qIsFinite(insertionBounds.height())) {
        return false;
    }

    imageObject.size = ObjectConstraints::freshImageInsertSize(
        QSizeF(imageObject.pixmap().size()),
        imageObject.pixmap().devicePixelRatio(),
        devicePixelRatioF(),
        insertionBounds);
    const QPointF center = viewportCenterInDocument();
    imageObject.position = center - QPointF(imageObject.size.width() / 2.0,
                                             imageObject.size.height() / 2.0);
    return true;
}

void DocumentViewport::insertImageFromClipboard()
{
    if (!m_document) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return;
    }

    QImage image = clipboard->image();
    if (image.isNull()) {
        return;
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ImageInsert] clipboard transfer:" << timer.elapsed() << "ms"
             << image.size();
#endif
    insertPreparedImage(image);
}

void DocumentViewport::insertImageFromFile(const QString& filePath)
{
    if (!m_document) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    constexpr qint64 MAX_RETAINED_SOURCE_BYTES = 64LL * 1024 * 1024;
    const qint64 sourceSize = QFileInfo(filePath).size();
    if (sourceSize < 0 || sourceSize > MAX_RETAINED_SOURCE_BYTES) {
        // Avoid duplicating arbitrarily large encoded files in RAM. These rare
        // inputs still get the fast insertion path, but their decoded pixels
        // are persisted as a background PNG instead of preserving source bytes.
        QImageReader reader(filePath);
        QImage image = reader.read();
        if (image.isNull()) {
            qWarning() << "insertImageFromFile: Failed to decode" << filePath
                       << reader.errorString();
            return;
        }
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[ImageInsert] streamed large source:" << sourceSize
                 << "bytes, decode:" << timer.elapsed() << "ms";
#endif
        insertPreparedImage(image);
        return;
    }

    QFile source(filePath);
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning() << "insertImageFromFile: Failed to open" << filePath;
        return;
    }
    const QByteArray encodedData = source.readAll();
    if (encodedData.size() != sourceSize) {
        qWarning() << "insertImageFromFile: Incomplete read from" << filePath;
        return;
    }
    source.close();
    const qint64 readMs = timer.elapsed();

    QBuffer sourceBuffer;
    sourceBuffer.setData(encodedData);
    sourceBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&sourceBuffer);
    const QByteArray format = reader.format();
    QImage image = reader.read();
    if (image.isNull()) {
        qWarning() << "insertImageFromFile: Failed to decode" << filePath
                   << reader.errorString();
        return;
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ImageInsert] file read:" << readMs << "ms, decode:"
             << (timer.elapsed() - readMs) << "ms, bytes:" << encodedData.size()
             << "format:" << format << "pixels:" << image.size();
#endif
    insertPreparedImage(image, encodedData, format);
}

void DocumentViewport::insertPreparedImage(const QImage& image,
                                           const QByteArray& encodedData,
                                           const QByteArray& encodedFormat)
{
    if (!m_document || image.isNull()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    auto imgObj = std::make_unique<ImageObject>();
    imgObj->setSourceImage(image, encodedData, encodedFormat);
    const qint64 pixmapMs = timer.elapsed();

    if (!prepareFreshImageForInsertion(*imgObj)) {
        qWarning() << "insertPreparedImage: Invalid image insertion bounds";
        return;
    }

    const int activeLayer = m_document->isEdgeless()
        ? m_edgelessActiveLayerIndex
        : (m_document->page(m_currentPageIndex)
           ? m_document->page(m_currentPageIndex)->activeLayerIndex : 0);
    const int defaultAffinity = activeLayer - 1;
    imgObj->setLayerAffinity(defaultAffinity);

    ImageObject* rawPtr = imgObj.get();
    Document::TileCoord insertedTileCoord = {0, 0};
    if (m_document->isEdgeless()) {
        const auto coord = m_document->tileCoordForPoint(imgObj->position);
        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            return;
        }
        imgObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);
        imgObj->position -= QPointF(coord.first * Document::EDGELESS_TILE_SIZE,
                                    coord.second * Document::EDGELESS_TILE_SIZE);
        targetTile->addObject(std::move(imgObj));
        m_document->markTileDirty(coord);
        insertedTileCoord = coord;
    } else {
        Page* targetPage = m_document->page(m_currentPageIndex);
        if (!targetPage) {
            return;
        }
        imgObj->zOrder = getNextZOrderForAffinity(targetPage, defaultAffinity);
        imgObj->position = clampObjectPositionToPage(
            m_currentPageIndex,
            imgObj->position - pagePosition(m_currentPageIndex),
            imgObj->size);
        targetPage->addObject(std::move(imgObj));
        m_document->markPageDirty(m_currentPageIndex);
    }

    m_document->updateMaxObjectExtent(rawPtr);
    const qint64 modelMs = timer.elapsed();

    pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);
    const qint64 undoMs = timer.elapsed();
    deselectAllObjects();
    selectObject(rawPtr, false);
    emit documentModified();

    const QRect dirty = objectBoundsInViewport(rawPtr)
        .adjusted(-32.0, -32.0, 32.0, 32.0).toAlignedRect();
    update(dirty);

    const qint64 enqueueStart = timer.elapsed();
    if (!m_document->bundlePath().isEmpty()) {
        if (!m_document->enqueueImageAssetWrite(rawPtr, image)) {
            qWarning() << "insertPreparedImage: Background asset write was not queued";
        }
    }
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[ImageInsert] pixmap:" << pixmapMs << "ms, model:"
             << (modelMs - pixmapMs) << "ms, undo:" << (undoMs - modelMs)
             << "ms, enqueue:" << (timer.elapsed() - enqueueStart)
             << "ms, visible work total:" << undoMs << "ms";
#endif
}

void DocumentViewport::insertImageFromDialog()
{
    // Phase C.0.5: Open file dialog to select an image.
    // Parent to the top-level window (not `this`) so Qt applies its
    // strongest available modal grab; on platforms that respect it this
    // helps suppress stray tablet events leaking to widgets behind the
    // dialog. The m_objectInsertDialogActive guard is the actual safety
    // net for platforms that ignore the grab.
    QString filePath = QFileDialog::getOpenFileName(
        window(),
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
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    if (m_inlineEditSession.active) {
        endInlineTextEdit(false, true);
        if (m_selectedObjects.isEmpty())
            return;
    }
    
    // Separate OcrTextObjects (derived cache — no undo) from regular objects
    QVector<InsertedObject*> regularObjects;
    QVector<InsertedObject*> ocrObjects;
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        if (obj->type() == QStringLiteral("ocr_text"))
            ocrObjects.append(obj);
        else
            regularObjects.append(obj);
    }
    
    // Handle OcrTextObject deletion (no undo, update suppressedStrokeIds)
    int ocrDeletedCount = 0;
    for (InsertedObject* obj : ocrObjects) {
        auto* ocrObj = static_cast<OcrTextObject*>(obj);
        
        auto deleteOcrFromPage = [&](Page* page, const QString& pageId,
                                     int pageIndex, Document::TileCoord tileCoord) {
            if (!page || !page->objectById(ocrObj->id))
                return false;
            
            for (const auto& sid : ocrObj->sourceStrokeIds)
                page->suppressedStrokeIds.insert(sid);
            
            // Remove matching OcrTextBlock
            bool removedBlock = false;
            for (int b = page->ocrTextBlocks.size() - 1; b >= 0; --b) {
                if (page->ocrTextBlocks[b].id == ocrObj->id) {
                    // A block with no strokes cannot be kept out by stroke
                    // suppression, so remember it by fingerprint instead.
                    if (ocrObj->sourceStrokeIds.isEmpty()) {
                        page->dismissedOcrBlockKeys.insert(
                            ocrBlockDismissalKey(page->ocrTextBlocks[b]));
                    }
                    page->ocrTextBlocks.removeAt(b);
                    removedBlock = true;
                    break;
                }
            }

            // Keep the Highlighter's OCR cache in sync for this page/tile.
            if (removedBlock) {
                invalidateOcrBlockCache(pageIndex);
            }
            
            page->removeObject(ocrObj->id);
            
            if (m_document->isEdgeless()) {
                m_document->markTileDirty(tileCoord);
                m_document->saveTileOcr(tileCoord);
            } else {
                m_document->markPageDirty(pageIndex);
                m_document->savePageOcr(pageId, page);
            }
            ocrDeletedCount++;
            return true;
        };
        
        if (m_document->isEdgeless()) {
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && deleteOcrFromPage(tile, tile->uuid, -1, coord))
                    break;
            }
        } else {
            Page* currentPage = m_document->page(m_currentPageIndex);
            if (currentPage && deleteOcrFromPage(currentPage, currentPage->uuid,
                                                  m_currentPageIndex, {})) {
                // found on current page
            } else {
                for (int i : m_document->loadedPageIndices()) {
                    Page* page = m_document->page(i);
                    if (page && deleteOcrFromPage(page, page->uuid, i, {}))
                        break;
                }
            }
        }
    }
    
    // Phase M.2: Cascade delete markdown notes linked to LinkObjects
    int noteCount = 0;
    for (InsertedObject* obj : regularObjects) {
        if (LinkObject* link = dynamic_cast<LinkObject*>(obj)) {
            for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
                if (link->linkSlots[i].type == LinkSlot::Type::Markdown) {
                    noteCount++;
                }
            }
        }
    }
    
    if (noteCount > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "deleteSelectedObjects: Cascade deleting" << noteCount << "markdown note(s)";
        #endif
    }
    
    // Delete markdown note files before removing LinkObjects
    for (InsertedObject* obj : regularObjects) {
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
    // Phase M.9: Track containers whose outline contribution may have
    // changed, so we can refresh the cache without rescanning the world.
    std::set<Document::TileCoord> touchedTiles;
    std::set<int>                 touchedPages;

    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        for (InsertedObject* obj : regularObjects) {
            if (!obj) continue;
            
            bool found = false;
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    pushObjectDeleteUndo(obj, -1, coord);
                    tile->removeObject(obj->id);
                    m_document->markTileDirty(coord);
                    touchedTiles.insert(coord);
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
        Page* currentPage = m_document->page(m_currentPageIndex);
        if (currentPage) {
            for (InsertedObject* obj : regularObjects) {
                if (!obj) continue;
                
                if (currentPage->objectById(obj->id)) {
                    pushObjectDeleteUndo(obj, m_currentPageIndex, {});
                    currentPage->removeObject(obj->id);
                    m_document->markPageDirty(m_currentPageIndex);
                    touchedPages.insert(m_currentPageIndex);
                    deletedCount++;
                } else {
                    bool found = false;
                    for (int i : m_document->loadedPageIndices()) {
                        Page* page = m_document->page(i);
                        if (page && page->objectById(obj->id)) {
                            pushObjectDeleteUndo(obj, i, {});
                            page->removeObject(obj->id);
                            m_document->markPageDirty(i);
                            touchedPages.insert(i);
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
    if (deletedCount > 0 || ocrDeletedCount > 0) {
        emit documentModified();
        // Phase M.9: refresh per-container so the subsequent sidebar
        // query is O(#entries) not O(total-tiles).
        for (const auto& c : touchedTiles) m_document->refreshLinkOutlineFor(c);
        for (int p : touchedPages) m_document->refreshLinkOutlineFor(p);
        emit linkObjectListMayHaveChanged();  // M.7.3: Refresh sidebar
    }
    
    update();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "deleteSelectedObjects: Deleted" << deletedCount << "regular +" << ocrDeletedCount << "OCR objects";
    #endif
}

// ===== OCR to text box conversion =====

Page* DocumentViewport::locateObjectContainer(
    const QString& objectId, int& pageIndex,
    Document::TileCoord& tileCoord) const
{
    pageIndex = -1;
    tileCoord = {0, 0};
    if (!m_document || objectId.isEmpty())
        return nullptr;

    if (m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(objectId)) {
                tileCoord = coord;
                return tile;
            }
        }
        return nullptr;
    }

    Page* current = m_document->page(m_currentPageIndex);
    if (current && current->objectById(objectId)) {
        pageIndex = m_currentPageIndex;
        return current;
    }
    for (int i : m_document->loadedPageIndices()) {
        Page* page = m_document->page(i);
        if (page && page->objectById(objectId)) {
            pageIndex = i;
            return page;
        }
    }
    return nullptr;
}

InsertedObject* DocumentViewport::objectById(const QString& objectId) const
{
    int pageIndex = -1;
    Document::TileCoord tileCoord = {0, 0};
    Page* container = locateObjectContainer(objectId, pageIndex, tileCoord);
    return container ? container->objectById(objectId) : nullptr;
}

void DocumentViewport::forgetObject(const QString& objectId)
{
    if (objectId.isEmpty())
        return;

    if (m_inlineEditSession.active
        && m_inlineEditSession.objectId == objectId) {
        // The object is already doomed, so the session cannot commit into it.
        endInlineTextEdit(false, true);
    }

    if (m_textBoxFormatTransaction.active
        && m_textBoxFormatTransaction.objectId == objectId) {
        closeTextBoxFormatPopups(false);
        finishTextBoxFormatInteraction(false);
    }

    if (m_hoveredObject && m_hoveredObject->id == objectId)
        m_hoveredObject = nullptr;

    deselectObjectById(objectId);
}

void DocumentViewport::persistOcrSidecar(Page* container, int pageIndex,
                                         Document::TileCoord tileCoord)
{
    if (!m_document || !container)
        return;

    if (m_document->isEdgeless()) {
        m_document->markTileDirty(tileCoord);
        m_document->saveTileOcr(tileCoord);
    } else {
        if (pageIndex >= 0)
            m_document->markPageDirty(pageIndex);
        m_document->savePageOcr(container->uuid, container);
    }
    invalidateOcrBlockCache(pageIndex);
}

void DocumentViewport::applyOcrConversion(const UndoAction& action)
{
    if (!m_document)
        return;

    // The container may have been evicted since the action was recorded, so
    // reload it the way the other object undo paths do.
    Page* container = m_document->isEdgeless()
        ? m_document->getOrCreateTile(action.objectTileCoord.first,
                                      action.objectTileCoord.second)
        : m_document->page(action.objectPageIndex);
    if (!container)
        return;

    const QString ocrId =
        action.ocrSourceObjectData.value(QStringLiteral("id")).toString();

    for (const auto& strokeId : action.ocrSuppressedStrokeIdsAdded)
        container->suppressedStrokeIds.insert(strokeId);
    if (!action.ocrDismissedBlockKeyAdded.isEmpty()) {
        container->dismissedOcrBlockKeys.insert(
            action.ocrDismissedBlockKeyAdded);
    }

    for (int i = container->ocrTextBlocks.size() - 1; i >= 0; --i) {
        if (container->ocrTextBlocks[i].id == ocrId) {
            container->ocrTextBlocks.removeAt(i);
            break;
        }
    }

    if (!ocrId.isEmpty()) {
        forgetObject(ocrId);
        container->removeObject(ocrId);
    }

    auto textBox = InsertedObject::fromJson(action.objectData);
    if (textBox) {
        m_document->updateMaxObjectExtent(textBox.get());
        container->addObject(std::move(textBox));
    }

    m_document->recalculateMaxObjectExtent();
    persistOcrSidecar(container, action.objectPageIndex,
                      action.objectTileCoord);
}

void DocumentViewport::revertOcrConversion(const UndoAction& action)
{
    if (!m_document)
        return;

    Page* container = m_document->isEdgeless()
        ? m_document->getOrCreateTile(action.objectTileCoord.first,
                                      action.objectTileCoord.second)
        : m_document->page(action.objectPageIndex);
    if (!container)
        return;

    forgetObject(action.objectId);
    container->removeObject(action.objectId);

    // Only the ids this conversion added: a stroke suppressed by an earlier
    // OCR deletion must stay suppressed.
    for (const auto& strokeId : action.ocrSuppressedStrokeIdsAdded)
        container->suppressedStrokeIds.remove(strokeId);
    if (!action.ocrDismissedBlockKeyAdded.isEmpty()) {
        container->dismissedOcrBlockKeys.remove(
            action.ocrDismissedBlockKeyAdded);
    }

    if (action.ocrSourceBlockValid) {
        const int index = qBound(0, action.ocrSourceBlockIndex,
                                 container->ocrTextBlocks.size());
        container->ocrTextBlocks.insert(
            index, OcrTextBlock::fromJson(action.ocrSourceBlock));
    }

    auto ocrObject = InsertedObject::fromJson(action.ocrSourceObjectData);
    if (auto* restored = dynamic_cast<OcrTextObject*>(ocrObject.get())) {
        restored->showConfidence = action.ocrSourceObjectData
            .value(QStringLiteral("uiShowConfidence")).toBool(false);
        restored->ocrSnapEnabled = action.ocrSourceObjectData
            .value(QStringLiteral("uiSnapEnabled")).toBool(false);
        restored->ocrCjkGridMode = action.ocrSourceObjectData
            .value(QStringLiteral("uiCjkGridMode")).toBool(false);
        restored->ocrGridSpacing = action.ocrSourceObjectData
            .value(QStringLiteral("uiGridSpacing")).toInt(32);
    }
    if (ocrObject) {
        m_document->updateMaxObjectExtent(ocrObject.get());
        container->addObject(std::move(ocrObject));
    }

    m_document->recalculateMaxObjectExtent();
    persistOcrSidecar(container, action.objectPageIndex,
                      action.objectTileCoord);
}

bool DocumentViewport::convertOcrTextToTextBox(OcrTextObject* ocr,
                                               bool startEditing)
{
    if (!ocr || !m_document)
        return false;

    int pageIndex = -1;
    Document::TileCoord tileCoord = {0, 0};
    Page* container = locateObjectContainer(ocr->id, pageIndex, tileCoord);
    if (!container)
        return false;

    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    if (m_inlineEditSession.active)
        commitInlineTextEdit();

    // Build the replacement first: a paged conversion whose reflowed height
    // does not fit must leave the OCR block and its sidecar untouched.
    TextBoxObject textBox;
    textBox.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    textBox.position = ocr->position;
    textBox.rotation = ocr->rotation;
    textBox.setLayerAffinity(ocr->getLayerAffinity());
    // Conversion replaces the block in place, so it keeps the stacking the user
    // already sees. Promoting it to the top would reorder it against unrelated
    // neighbours on every convert.
    textBox.zOrder = ocr->zOrder;
    textBox.visible = true;
    textBox.text = ocr->text;
    textBox.fontFamily = ocr->fontFamily;
    textBox.fontColor = ocr->fontColor;
    textBox.backgroundColor = ocr->backgroundColor;
    textBox.alignment = ocr->alignment;
    textBox.showBorder = ocr->showBorder;
    textBox.fontSize = ocr->estimateBaseFontSize();
    textBox.textLayoutVersion = TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;

    qreal width = qMax(ocr->size.width(), TextBoxObject::MINIMUM_WIDTH);
    if (!m_document->isEdgeless() && pageIndex >= 0) {
        const qreal pageWidth =
            qMax<qreal>(1.0, m_document->pageSizeAt(pageIndex).width());
        width = qMin(width, pageWidth);
    }
    textBox.size = QSizeF(width, 1.0);
    textBox.reflowToWidth(width);

    const QRectF ocrBounds = objectBoundsInViewport(ocr);
    if (!m_document->isEdgeless() && pageIndex >= 0) {
        const QSizeF pageSize = m_document->pageSizeAt(pageIndex);
        textBox.position = clampObjectPositionToPage(
            pageIndex, textBox.position, textBox.size);
        const QRectF bounds(textBox.position, textBox.size);
        if (bounds.bottom() > pageSize.height() + 0.01
            || bounds.right() > pageSize.width() + 0.01) {
            showObjectGeometryFeedback(
                tr("Not enough room on this page to convert this text"),
                ocrBounds);
            return false;
        }
    }

    UndoAction action;
    action.type = UndoAction::OcrConvertToTextBox;
    action.objectPageIndex = pageIndex;
    action.objectTileCoord = tileCoord;
    action.objectId = textBox.id;
    action.objectData = textBox.toJson();
    action.ocrSourceObjectData = ocr->toJson();
    // Display-only flags are driven by the OCR toolbar and are deliberately
    // absent from the persisted OCR schema, so carry them in the snapshot.
    action.ocrSourceObjectData[QStringLiteral("uiShowConfidence")] =
        ocr->showConfidence;
    action.ocrSourceObjectData[QStringLiteral("uiSnapEnabled")] =
        ocr->ocrSnapEnabled;
    action.ocrSourceObjectData[QStringLiteral("uiCjkGridMode")] =
        ocr->ocrCjkGridMode;
    action.ocrSourceObjectData[QStringLiteral("uiGridSpacing")] =
        ocr->ocrGridSpacing;

    for (int i = 0; i < container->ocrTextBlocks.size(); ++i) {
        if (container->ocrTextBlocks[i].id == ocr->id) {
            action.ocrSourceBlock = container->ocrTextBlocks[i].toJson();
            action.ocrSourceBlockValid = true;
            action.ocrSourceBlockIndex = i;
            break;
        }
    }
    for (const auto& strokeId : ocr->sourceStrokeIds) {
        if (!container->suppressedStrokeIds.contains(strokeId))
            action.ocrSuppressedStrokeIdsAdded.append(strokeId);
    }

    // Without stroke ids there is nothing to suppress, so the block is recorded
    // by geometry fingerprint instead; otherwise a scan already in flight would
    // hand the block straight back.
    if (ocr->sourceStrokeIds.isEmpty() && action.ocrSourceBlockValid) {
        const QString key = ocrBlockDismissalKey(
            OcrTextBlock::fromJson(action.ocrSourceBlock));
        if (!container->dismissedOcrBlockKeys.contains(key))
            action.ocrDismissedBlockKeyAdded = key;
    }

    applyOcrConversion(action);
    pushUndoAction(action);

    InsertedObject* created = container->objectById(action.objectId);
    deselectAllObjects();
    if (created)
        selectObject(created, false);

    if (!m_document->isEdgeless() && pageIndex >= 0) {
        m_pendingThumbnailPages.insert(pageIndex);
        emit pageModified(pageIndex);
    }
    emit documentModified();
    emit textBoxLayoutCommitted();
    update();

    if (startEditing && created)
        startInlineTextEdit(static_cast<TextBoxObject*>(created), false);
    return true;
}

void DocumentViewport::copySelectedObjects()
{
    // Phase O2.6.2: Copy selected objects to internal clipboard
    if (m_selectedObjects.isEmpty()) {
        return;
    }
    
    // Clear previous clipboard contents
    s_objectClipboard.clear();
    s_objectClipboardAssets.clear();
    
    // Serialize each selected object to JSON
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        if (obj->type() == QStringLiteral("ocr_text")) continue;
        
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
                        s_objectClipboard.append(clone->toJson());
                        foundTile = true;
                        break;
                    }
                }
                if (!foundTile) {
                    // Fallback: copy without back-link if tile not found
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "copySelectedObjects (edgeless): tile not found for link->id =" << link->id;
#endif
                    s_objectClipboard.append(link->toJson());
                }
            } else {
                // Paged mode: use page UUID
                QString sourcePageUuid;
                Page* currentPage = m_document->page(m_currentPageIndex);
                if (currentPage) {
                    sourcePageUuid = currentPage->uuid;
                }
                
                auto clone = link->cloneWithBackLink(sourcePageUuid);
                s_objectClipboard.append(clone->toJson());
            }
        } else {
            s_objectClipboard.append(obj->toJson());
            
            // Cache image assets for cross-document paste
            if (auto* img = dynamic_cast<ImageObject*>(obj)) {
                if (img->isLoaded() && !img->imagePath.isEmpty()) {
                    ClipboardImageAsset asset;
                    asset.pixmap = img->pixmap();
                    asset.encodedData = img->encodedAssetData();
                    asset.format = img->assetFormat();
                    constexpr qint64 MAX_CLIPBOARD_SOURCE_BYTES =
                        64LL * 1024 * 1024;
                    if (asset.encodedData.isEmpty() && m_document
                        && !m_document->bundlePath().isEmpty()) {
                        QFile source(img->fullPath(m_document->bundlePath()));
                        if (source.size() <= MAX_CLIPBOARD_SOURCE_BYTES
                            && source.open(QIODevice::ReadOnly)) {
                            asset.encodedData = source.readAll();
                        }
                    }
                    s_objectClipboardAssets[img->imagePath] = std::move(asset);
                }
            }
        }
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "copySelectedObjects: Copied" << s_objectClipboard.size() << "objects to internal clipboard";
    #endif

    // Put plain text on system clipboard for cross-app paste (Phase 2E)
    QString plainText;
    for (InsertedObject* obj : m_selectedObjects) {
        if (!obj) continue;
        auto* textBox = dynamic_cast<TextBoxObject*>(obj);
        if (textBox && !textBox->text.isEmpty()) {
            if (!plainText.isEmpty()) plainText += QLatin1Char('\n');
            plainText += textBox->text;
        }
    }
    if (!plainText.isEmpty()) {
        QGuiApplication::clipboard()->setText(plainText);
    }
    
    // Notify that object clipboard has content (for action bar paste button)
    emit objectClipboardChanged(!s_objectClipboard.isEmpty());
}

void DocumentViewport::pasteObjects()
{
    // Phase O2.6.3: Paste objects from internal clipboard
    if (!m_document || s_objectClipboard.isEmpty()) {
        return;
    }
    
    // Clear current selection - we'll select the pasted objects
    deselectAllObjects();
    
    // Track newly pasted objects for selection
    QList<InsertedObject*> pastedObjects;
    // Phase M.9: containers touched by this paste, for per-container
    // outline cache refresh below.
    std::set<Document::TileCoord> touchedTiles;
    std::set<int>                 touchedPages;
    
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
                // Containment is applied per object below, once each one's
                // size is known - clamping the bare cursor point here would
                // still let a large object overhang the page.
                pastePagePos = hit.pagePoint;
                useCursorPosition = true;
            }
        }
    }
    
    // Fallback: paste at top-left with offset
    constexpr qreal PASTE_OFFSET = 20.0;
    if (!useCursorPosition) {
        pastePagePos = QPointF(PASTE_OFFSET, PASTE_OFFSET);
    }
    
    for (const QJsonObject& jsonObj : s_objectClipboard) {
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
        bool assetLoaded = false;
        if (!m_document->bundlePath().isEmpty()) {
            assetLoaded = obj->loadAssets(m_document->bundlePath());
        }
        
        // Cross-document fallback: use cached pixmap from clipboard
        if (!assetLoaded) {
            if (auto* img = dynamic_cast<ImageObject*>(obj.get())) {
                auto it = s_objectClipboardAssets.find(img->imagePath);
                if (it != s_objectClipboardAssets.end()) {
                    if (!it->encodedData.isEmpty()) {
                        QImage source;
                        source.loadFromData(it->encodedData);
                        if (!source.isNull()) {
                            img->setSourceImage(source, it->encodedData, it->format);
                        } else {
                            img->setPixmap(it->pixmap);
                        }
                    } else {
                        img->setPixmap(it->pixmap);
                    }
                }
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
            touchedTiles.insert(coord);
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
            
            // Objects copied from a larger page (or another document) can be
            // too big for this one, so fit before clamping.
            obj->size = ObjectConstraints::shrinkToFit(obj->size, targetPage->size);
            obj->position = clampObjectPositionToPage(m_currentPageIndex, obj->position, obj->size);
            
            targetPage->addObject(std::move(obj));
            m_document->markPageDirty(m_currentPageIndex);
            touchedPages.insert(m_currentPageIndex);
        }
        
        // Update max object extent
        m_document->updateMaxObjectExtent(rawPtr);
        
        // Create undo entry for this pasted object
        pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);

        // Preserve the responsive insertion path for cross-document images.
        // Other asset-bearing object types retain their existing synchronous
        // persistence contract.
        if (!m_document->bundlePath().isEmpty()) {
            if (auto* image = dynamic_cast<ImageObject*>(rawPtr)) {
                const QImage source = image->encodedAssetData().isEmpty()
                    ? image->pixmap().toImage() : QImage();
                if (!image->assetPersisted()
                    && !m_document->enqueueImageAssetWrite(image, source)) {
                    qWarning() << "pasteObjects: Background image write was not queued";
                }
            } else {
                rawPtr->saveAssets(m_document->bundlePath());
            }
        }
        
        // Track for selection
        pastedObjects.append(rawPtr);
    }
    
    // Select all pasted objects
    for (InsertedObject* obj : pastedObjects) {
        selectObject(obj, true);  // addToSelection = true
    }
    
    if (!pastedObjects.isEmpty()) {
        emit documentModified();
        // Phase M.9: refresh outline cache for each touched container.
        for (const auto& c : touchedTiles) m_document->refreshLinkOutlineFor(c);
        for (int p : touchedPages) m_document->refreshLinkOutlineFor(p);
        emit linkObjectListMayHaveChanged();  // M.7.3: Refresh sidebar

        // documentModified alone is not enough: the thumbnail refresh it drives
        // is skipped while the pasted objects are still selected, and the search
        // cache only listens for a layout commit. Pasted text would otherwise
        // stay unsearchable until something else committed.
        for (int p : touchedPages) {
            m_pendingThumbnailPages.insert(p);
            emit pageModified(p);
        }
        emit textBoxLayoutCommitted();
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

    const bool ocrSelection = (m_textSelection.source == TextSelection::Source::Ocr);
    const bool edgeless = m_document->isEdgeless();

    // Resolve the anchor position in DOCUMENT-space (for edgeless) or
    // PAGE-space (for paged).
    QRectF firstRect = m_textSelection.highlightRects[0];
    qreal firstRectX;
    qreal firstRectY;
    if (ocrSelection) {
        firstRectX = firstRect.x();
        firstRectY = firstRect.y();
    } else {
        firstRectX = firstRect.x() * PDF_TO_PAGE_SCALE;
        firstRectY = firstRect.y() * PDF_TO_PAGE_SCALE;
    }

    // Place icon to the LEFT of the first highlight rect (in the margin).
    // Paged mode pulls it back onto the page below; in edgeless the canvas is
    // infinite (negative coords are legal), so nothing constrains it.
    constexpr qreal MARGIN_PADDING = 4.0;
    qreal iconX = firstRectX - LinkObject::ICON_SIZE - MARGIN_PADDING;

    auto linkObj = std::make_unique<LinkObject>();
    linkObj->description = m_textSelection.selectedText;

    // Use a DARKER version of highlighter color for visibility on white pages.
    QColor darkened = m_highlighterColor;
    darkened.setRed(darkened.red() * 0.5);
    darkened.setGreen(darkened.green() * 0.5);
    darkened.setBlue(darkened.blue() * 0.5);
    darkened.setAlpha(255);
    linkObj->iconColor = darkened;

    if (edgeless) {
        // Find the tile containing the anchor point. In edgeless, positions
        // are stored tile-local, and the object's hit tests are done in
        // document space (tileOrigin + obj.position).
        QPointF docAnchor(iconX, firstRectY);
        auto coord = m_document->tileCoordForPoint(docAnchor);
        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            return;
        }

        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                           coord.second * Document::EDGELESS_TILE_SIZE);
        linkObj->position = docAnchor - tileOrigin;

        int activeLayer = m_edgelessActiveLayerIndex;
        int defaultAffinity = activeLayer - 1;
        linkObj->setLayerAffinity(defaultAffinity);
        linkObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);

        LinkObject* rawPtr = linkObj.get();
        targetTile->addObject(std::move(linkObj));
        m_document->markTileDirty(coord);

        pushObjectInsertUndo(rawPtr, pageIndex, coord);

#ifdef QT_DEBUG
        qDebug() << "Created LinkObject for highlight on edgeless tile"
                 << coord.first << coord.second
                 << "description:" << rawPtr->description.left(30);
#endif
        return;
    }

    // ---------- Paged path (original behavior) ----------
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return;
    }

    linkObj->position = clampObjectPositionToPage(
        pageIndex, QPointF(iconX, firstRectY), linkObj->size);

    int activeLayer = page->activeLayerIndex;
    int defaultAffinity = activeLayer - 1;
    linkObj->setLayerAffinity(defaultAffinity);
    linkObj->zOrder = getNextZOrderForAffinity(page, defaultAffinity);

    LinkObject* rawPtr = linkObj.get();
    page->addObject(std::move(linkObj));
    m_document->markPageDirty(pageIndex);

    pushObjectInsertUndo(rawPtr, pageIndex, {});

    // SB2: a highlight-created LinkObject adds a scroll-bar marker; keep the
    // outline cache current and notify listeners (scroll bar + notes sidebar).
    m_document->refreshLinkOutlineFor(pageIndex);
    emit linkObjectListMayHaveChanged();

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
        
        // A click near the right/bottom edge would otherwise leave part of the
        // 24pt icon hanging off the page
        linkObj->position = clampObjectPositionToPage(pageIndex, linkObj->position, linkObj->size);
        
        page->addObject(std::move(linkObj));
        m_document->markPageDirty(pageIndex);
    }
    
    // Push undo action
    pushObjectInsertUndo(rawPtr, pageIndex, insertedTileCoord);
    
    // Select the new object
    deselectAllObjects();
    selectObject(rawPtr, false);
    
    emit documentModified();
    // SB2: a brand-new LinkObject adds a scroll-bar marker. Refresh the outline
    // cache for the owning container and notify listeners (scroll bar + notes).
    markLinkContainerDirtyAndRefreshOutline(rawPtr);
    emit linkObjectListMayHaveChanged();
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

// ===== Phase 2C: Text Box Creation =====

bool DocumentViewport::hasActiveInlineTextEdit() const
{
    return m_inlineEditSession.active;
}

bool DocumentViewport::inlineEditTargetContains(
    const QPointF& viewportPos) const
{
    if (!m_inlineEditSession.active || !m_inlineTextBoxEditor)
        return false;
    if (m_inlineTextBoxEditor->isVisible()
        && m_inlineTextBoxEditor->geometry().contains(
               viewportPos.toPoint())) {
        return true;
    }
    TextBoxObject* textBox = resolveInlineTextBox();
    return textBox
        && objectBoundsInViewport(textBox).contains(viewportPos);
}

bool DocumentViewport::inlineTextEditorHasFocus() const
{
    return m_inlineTextBoxEditor && m_inlineEditSession.active
        && (m_inlineTextBoxEditor->hasFocus()
            || m_inlineTextBoxEditor->isAncestorOf(
                QApplication::focusWidget()));
}

bool DocumentViewport::textBoxFormatBarHasFocus() const
{
    return m_textBoxFormatBar && m_textBoxFormatBar->controlHasFocus();
}

TextBoxObject* DocumentViewport::selectedTextBoxForFormatting() const
{
    if (!m_document || m_selectedObjects.size() != 1)
        return nullptr;
    InsertedObject* selected = m_selectedObjects.first();
    if (!selected || selected->type() != QLatin1String("textbox"))
        return nullptr;
    auto* textBox = static_cast<TextBoxObject*>(selected);
    int pageIndex = -1;
    Document::TileCoord tileCoord = {0, 0};
    return locateTextBoxObject(textBox, pageIndex, tileCoord)
        ? textBox : nullptr;
}

bool DocumentViewport::locateTextBoxObject(
    TextBoxObject* textBox, int& pageIndex,
    Document::TileCoord& tileCoord) const
{
    pageIndex = -1;
    tileCoord = {0, 0};
    if (!m_document || !textBox)
        return false;

    if (m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(textBox->id) == textBox) {
                tileCoord = coord;
                return true;
            }
        }
        return false;
    }

    pageIndex = pageIndexForObject(textBox);
    return pageIndex >= 0 && pageIndex < m_document->pageCount();
}

TextBoxObject* DocumentViewport::resolveTextBoxFormatTarget() const
{
    if (!m_textBoxFormatTransaction.active || !m_document
        || m_textBoxFormatTransaction.document != m_document) {
        return nullptr;
    }

    Page* container = nullptr;
    if (m_document->isEdgeless()) {
        const auto coord = m_textBoxFormatTransaction.tileCoord;
        container = m_document->getTile(coord.first, coord.second);
    } else if (m_textBoxFormatTransaction.pageIndex >= 0
               && m_textBoxFormatTransaction.pageIndex
                    < m_document->pageCount()) {
        container = m_document->page(
            m_textBoxFormatTransaction.pageIndex);
    }
    InsertedObject* object = container
        ? container->objectById(m_textBoxFormatTransaction.objectId)
        : nullptr;
    if (!object) {
        // The transaction pins the page/tile it started on, but an edgeless
        // object can relocate mid-preview. Losing the target here would strand
        // the preview mutations with no undo record and no way to roll back,
        // so fall back to an id search across everything loaded.
        object = objectById(m_textBoxFormatTransaction.objectId);
    }
    if (!object || object->type() != QLatin1String("textbox"))
        return nullptr;
    return static_cast<TextBoxObject*>(object);
}

void DocumentViewport::ensureTextBoxFormatBar()
{
    if (m_textBoxFormatBar)
        return;

    m_textBoxFormatBar = new TextBoxFormatBar(this);
    m_textBoxFormatBar->setDarkMode(m_isDarkMode);
    m_textBoxFormatBar->hide();

    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::interactionStarted,
            this, &DocumentViewport::beginTextBoxFormatInteraction);
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::fontSizePreviewRequested,
            this, [this](qreal value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::FontSize, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::fontFamilyPreviewRequested,
            this, [this](const QString& value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::FontFamily, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::alignmentPreviewRequested,
            this, [this](TextAlignment value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::Alignment,
            static_cast<int>(value));
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::fontColorPreviewRequested,
            this, [this](const QColor& value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::FontColor, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::backgroundColorPreviewRequested,
            this, [this](const QColor& value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::BackgroundColor, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::backgroundOpacityPreviewRequested,
            this, [this](int value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::BackgroundOpacity, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::borderPreviewRequested,
            this, [this](bool value) {
        applyTextBoxFormatPreview(
            TextBoxFormatChange::Border, value);
    });
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::interactionFinished,
            this, &DocumentViewport::finishTextBoxFormatInteraction);
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::cancelInlineEditRequested,
            this, &DocumentViewport::cancelInlineTextEdit);
    connect(m_textBoxFormatBar,
            &TextBoxFormatBar::commitInlineEditRequested,
            this, &DocumentViewport::commitInlineTextEdit);
}

void DocumentViewport::syncTextBoxFormatBar()
{
    TextBoxObject* textBox = selectedTextBoxForFormatting();
    if (!textBox) {
        closeTextBoxFormatPopups(true);
        finishTextBoxFormatInteraction(true);
        if (m_textBoxFormatBar)
            m_textBoxFormatBar->hide();
        return;
    }

    ensureTextBoxFormatBar();
    if (!m_textBoxFormatTransaction.active)
        m_textBoxFormatBar->setValues(textBox->captureState());
    m_textBoxFormatBar->show();
    updateTextBoxFormatBarGeometry();
    m_textBoxFormatBar->raise();
}

bool DocumentViewport::pointerOverTextOverlay(const QPointF& viewportPos) const
{
    const QPoint pos = viewportPos.toPoint();
    if (m_textBoxFormatBar && m_textBoxFormatBar->isVisible()
        && m_textBoxFormatBar->geometry().contains(pos)) {
        return true;
    }
    return m_inlineTextBoxEditor && m_inlineTextBoxEditor->isVisible()
        && m_inlineTextBoxEditor->geometry().contains(pos);
}

/**
 * @brief Axis-aligned bounds of @p rect after rotating it about @p center.
 *
 * Object bounds are stored unrotated, so anything that positions a widget
 * beside a rotated object has to expand to the rotated hull first or it will
 * anchor to an edge the user cannot see.
 */
static QRectF rotatedViewportBounds(const QRectF& rect, const QPointF& center,
                                    qreal degrees)
{
    if (qFuzzyIsNull(degrees))
        return rect;

    const qreal radians = qDegreesToRadians(degrees);
    const qreal cosine = qCos(radians);
    const qreal sine = qSin(radians);
    auto rotate = [&](const QPointF& point) {
        const QPointF delta = point - center;
        return center + QPointF(delta.x() * cosine - delta.y() * sine,
                                delta.x() * sine + delta.y() * cosine);
    };

    QPolygonF corners;
    corners << rotate(rect.topLeft()) << rotate(rect.topRight())
            << rotate(rect.bottomRight()) << rotate(rect.bottomLeft());
    return corners.boundingRect();
}

void DocumentViewport::updateTextBoxFormatBarGeometry()
{
    if (!m_textBoxFormatBar || m_textBoxFormatBar->isHidden())
        return;
    TextBoxObject* textBox = selectedTextBoxForFormatting();
    if (!textBox) {
        m_textBoxFormatBar->hide();
        return;
    }

    const QRectF unrotated = objectBoundsInViewport(textBox);
    if (unrotated.isEmpty())
        return;
    const QRectF objectRect = rotatedViewportBounds(
        unrotated, unrotated.center(), textBox->rotation);

    const int inset = 8;
    const qreal gap = 8.0;
    const QRectF available = QRectF(rect()).adjusted(
        inset, inset, -inset, -inset);
    QSize size = m_textBoxFormatBar->sizeHint();
    size.setWidth(qMin(size.width(),
                       qMax(1, available.toAlignedRect().width())));
    size.setHeight(qMin(size.height(),
                        qMax(1, available.toAlignedRect().height())));

    const qreal centeredX = objectRect.center().x()
        - size.width() / 2.0;
    const qreal centeredY = objectRect.center().y()
        - size.height() / 2.0;
    const QVector<QRectF> candidates = {
        QRectF(centeredX, objectRect.top() - gap - size.height(),
               size.width(), size.height()),
        QRectF(centeredX, objectRect.bottom() + gap,
               size.width(), size.height()),
        QRectF(objectRect.right() + gap, centeredY,
               size.width(), size.height()),
        QRectF(objectRect.left() - gap - size.width(), centeredY,
               size.width(), size.height())
    };

    QRectF chosen;
    for (const QRectF& candidate : candidates) {
        if (available.contains(candidate)) {
            chosen = candidate;
            break;
        }
    }
    if (chosen.isEmpty()) {
        auto overflowScore = [&available](const QRectF& candidate) {
            return qMax<qreal>(0.0, available.left() - candidate.left())
                + qMax<qreal>(0.0, candidate.right() - available.right())
                + qMax<qreal>(0.0, available.top() - candidate.top())
                + qMax<qreal>(0.0, candidate.bottom()
                              - available.bottom());
        };
        chosen = candidates.first();
        qreal bestScore = overflowScore(chosen);
        for (int i = 1; i < candidates.size(); ++i) {
            const qreal score = overflowScore(candidates.at(i));
            if (score < bestScore) {
                chosen = candidates.at(i);
                bestScore = score;
            }
        }
        chosen.moveLeft(qBound(available.left(), chosen.left(),
                               available.right() - chosen.width()));
        chosen.moveTop(qBound(available.top(), chosen.top(),
                              available.bottom() - chosen.height()));
    }

    m_textBoxFormatBar->setGeometry(chosen.toAlignedRect());
    m_textBoxFormatBar->raise();
}

void DocumentViewport::beginTextBoxFormatInteraction()
{
    if (m_textBoxFormatTransaction.active)
        return;
    TextBoxObject* textBox = selectedTextBoxForFormatting();
    if (!textBox)
        return;

    TextBoxFormatTransaction transaction;
    transaction.document = m_document;
    transaction.objectId = textBox->id;
    transaction.startState = textBox->captureState();
    if (!locateTextBoxObject(textBox, transaction.pageIndex,
                             transaction.tileCoord)) {
        return;
    }
    transaction.attachedToInlineEdit =
        m_inlineEditSession.active
        && m_inlineEditSession.document == m_document
        && m_inlineEditSession.objectId == textBox->id;

    if (textBox->usesLegacyLayout())
        textBox->upgradeToCurrentLayout();
    transaction.lastAcceptedState = textBox->captureState();
    transaction.dirtyViewport = objectBoundsInViewport(textBox);
    transaction.active = true;
    m_textBoxFormatTransaction = transaction;
    if (m_textBoxFormatBar)
        m_textBoxFormatBar->setValues(transaction.lastAcceptedState);
}

void DocumentViewport::preserveTextBoxTopAnchor(
    const TextBoxState& previous, TextBoxState& candidate)
{
    auto rotatedPoint = [](const TextBoxState& state,
                           const QPointF& localPoint) {
        const QPointF center(
            state.size.width() / 2.0, state.size.height() / 2.0);
        const QPointF delta = localPoint - center;
        const qreal radians = qDegreesToRadians(state.rotation);
        return state.position + center + QPointF(
            delta.x() * qCos(radians) - delta.y() * qSin(radians),
            delta.x() * qSin(radians) + delta.y() * qCos(radians));
    };
    const QPointF anchoredTopLeft =
        rotatedPoint(previous, QPointF(0.0, 0.0));
    const QPointF candidateCenter(
        candidate.size.width() / 2.0,
        candidate.size.height() / 2.0);
    const QPointF candidateDelta = -candidateCenter;
    const qreal radians = qDegreesToRadians(candidate.rotation);
    const QPointF rotatedCandidateDelta(
        candidateDelta.x() * qCos(radians)
            - candidateDelta.y() * qSin(radians),
        candidateDelta.x() * qSin(radians)
            + candidateDelta.y() * qCos(radians));
    candidate.position =
        anchoredTopLeft - candidateCenter - rotatedCandidateDelta;
}

void DocumentViewport::applyTextBoxFormatPreview(
    TextBoxFormatChange change, const QVariant& value)
{
    if (!m_textBoxFormatTransaction.active)
        beginTextBoxFormatInteraction();
    TextBoxObject* textBox = resolveTextBoxFormatTarget();
    if (!textBox)
        return;

    const TextBoxState previous =
        m_textBoxFormatTransaction.lastAcceptedState;
    const QRectF oldBounds = objectBoundsInViewport(textBox);
    textBox->applyState(previous);

    switch (change) {
        case TextBoxFormatChange::FontSize:
            textBox->fontSize = qMax<qreal>(1.0, value.toReal());
            break;
        case TextBoxFormatChange::FontFamily:
            textBox->fontFamily = value.toString();
            break;
        case TextBoxFormatChange::Alignment:
            textBox->alignment =
                static_cast<TextAlignment>(value.toInt());
            break;
        case TextBoxFormatChange::FontColor:
            textBox->fontColor = value.value<QColor>();
            break;
        case TextBoxFormatChange::BackgroundColor:
            textBox->backgroundColor = value.value<QColor>();
            break;
        case TextBoxFormatChange::BackgroundOpacity: {
            QColor color = textBox->backgroundColor;
            color.setAlpha(qBound(0, value.toInt(), 255));
            textBox->backgroundColor = color;
            break;
        }
        case TextBoxFormatChange::Border:
            textBox->showBorder = value.toBool();
            break;
    }

    textBox->reflowToWidth(previous.size.width());
    TextBoxState candidate = textBox->captureState();
    preserveTextBoxTopAnchor(previous, candidate);

    if (!textBoxGeometryProposalAllowed(
            m_textBoxFormatTransaction.startState, candidate,
            m_textBoxFormatTransaction.pageIndex)) {
        textBox->applyState(previous);
        if (m_textBoxFormatBar)
            m_textBoxFormatBar->setValues(previous);
        showObjectGeometryFeedback(
            tr("Text box cannot grow beyond the page"), oldBounds);
        updateInlineTextEditorGeometry();
        updateTextBoxFormatBarGeometry();
        return;
    }

    textBox->applyState(candidate);
    m_textBoxFormatTransaction.lastAcceptedState = candidate;
    if (m_textBoxFormatTransaction.attachedToInlineEdit)
        m_inlineEditSession.lastAcceptedState = candidate;
    m_textBoxFormatTransaction.dirtyViewport =
        m_textBoxFormatTransaction.dirtyViewport
            .united(oldBounds)
            .united(objectBoundsInViewport(textBox));
    updateInlineTextEditorGeometry();
    updateTextBoxFormatBarGeometry();
    update(m_textBoxFormatTransaction.dirtyViewport
               .adjusted(-10.0, -10.0, 10.0, 10.0)
               .toAlignedRect());
}

void DocumentViewport::markTextBoxFormatCommitted(
    int pageIndex, Document::TileCoord tileCoord)
{
    if (!m_document)
        return;
    if (m_document->isEdgeless()) {
        m_document->markTileDirty(tileCoord);
    } else if (pageIndex >= 0) {
        m_document->markPageDirty(pageIndex);
        m_pendingThumbnailPages.insert(pageIndex);
        emit pageModified(pageIndex);
    }
    emit documentModified();
    emit textBoxLayoutCommitted();
}

void DocumentViewport::finishTextBoxFormatInteraction(bool accept)
{
    if (!m_textBoxFormatTransaction.active)
        return;

    TextBoxObject* textBox = resolveTextBoxFormatTarget();
    const TextBoxFormatTransaction transaction =
        m_textBoxFormatTransaction;
    m_textBoxFormatTransaction.clear();
    if (!textBox) {
        syncTextBoxFormatBar();
        return;
    }

    if (!accept) {
        textBox->applyState(transaction.startState);
        if (transaction.attachedToInlineEdit)
            m_inlineEditSession.lastAcceptedState =
                transaction.startState;
    } else if (transaction.attachedToInlineEdit) {
        m_inlineEditSession.lastAcceptedState =
            textBox->captureState();
    } else if (!textBoxStatesEqual(
                   transaction.startState,
                   textBox->captureState())) {
        const Document::TileCoord oldTile = transaction.tileCoord;
        Document::TileCoord newTile = oldTile;
        if (m_document->isEdgeless()) {
            relocateObjectsToCorrectTiles();
            int ignoredPage = -1;
            locateTextBoxObject(textBox, ignoredPage, newTile);
        }
        pushObjectTextEditUndo(
            textBox, transaction.startState, textBox->captureState(),
            transaction.pageIndex, oldTile, newTile);
        if (m_document->isEdgeless() && oldTile != newTile)
            m_document->markTileDirty(oldTile);
        markTextBoxFormatCommitted(
            transaction.pageIndex, newTile);
    }

    if (m_textBoxFormatBar)
        m_textBoxFormatBar->setValues(textBox->captureState());
    updateInlineTextEditorGeometry();
    updateTextBoxFormatBarGeometry();
    const QRectF dirty = transaction.dirtyViewport
        .united(objectBoundsInViewport(textBox))
        .adjusted(-10.0, -10.0, 10.0, 10.0);
    update(dirty.toAlignedRect());
}

void DocumentViewport::closeTextBoxFormatPopups(bool acceptPreview)
{
    if (m_textBoxFormatBar)
        m_textBoxFormatBar->closePopups(acceptPreview);
}

bool DocumentViewport::textBoxStatesEqual(const TextBoxState& lhs,
                                          const TextBoxState& rhs)
{
    return lhs.text == rhs.text
        && lhs.fontFamily == rhs.fontFamily
        && qAbs(lhs.fontSize - rhs.fontSize) < 0.001
        && lhs.fontColor == rhs.fontColor
        && lhs.backgroundColor == rhs.backgroundColor
        && lhs.alignment == rhs.alignment
        && lhs.showBorder == rhs.showBorder
        && lhs.textLayoutVersion == rhs.textLayoutVersion
        && QLineF(lhs.position, rhs.position).length() < 0.001
        && qAbs(lhs.size.width() - rhs.size.width()) < 0.001
        && qAbs(lhs.size.height() - rhs.size.height()) < 0.001
        && qAbs(lhs.rotation - rhs.rotation) < 0.001;
}

TextBoxObject* DocumentViewport::resolveInlineTextBox() const
{
    if (!m_inlineEditSession.active || !m_document
        || m_inlineEditSession.document != m_document) {
        return nullptr;
    }

    Page* container = nullptr;
    if (m_document->isEdgeless()) {
        container = m_document->getTile(
            m_inlineEditSession.tileCoord.first,
            m_inlineEditSession.tileCoord.second);
    } else if (m_inlineEditSession.pageIndex >= 0
               && m_inlineEditSession.pageIndex
                    < m_document->pageCount()) {
        container = m_document->page(m_inlineEditSession.pageIndex);
    }
    if (!container)
        return nullptr;

    InsertedObject* object =
        container->objectById(m_inlineEditSession.objectId);
    if (!object || object->type() != QLatin1String("textbox"))
        return nullptr;
    return static_cast<TextBoxObject*>(object);
}

QRectF DocumentViewport::inlineTextEditorRect(TextBoxObject* textBox) const
{
    if (!textBox)
        return QRectF();

    const QRectF outer = objectBoundsInViewport(textBox);
    if (outer.isEmpty())
        return QRectF();

    const qreal padding =
        TextBoxObject::CONTENT_PADDING * m_zoomLevel;
    QRectF content = outer.adjusted(
        padding, padding, -padding, -padding);
    if (content.width() < 4.0)
        content.setWidth(4.0);
    if (content.height() < 4.0)
        content.setHeight(4.0);

    return rotatedViewportBounds(content, outer.center(),
                                 textBox->rotation);
}

void DocumentViewport::updateInlineTextEditorGeometry()
{
    if (!m_inlineTextBoxEditor || !m_inlineEditSession.active)
        return;

    TextBoxObject* textBox = resolveInlineTextBox();
    if (!textBox) {
        m_inlineTextBoxEditor->hide();
        m_inlineEditSession.clear();
        const bool hadSelection = !m_selectedObjects.isEmpty();
        m_selectedObjects.clear();
        m_hoveredObject = nullptr;
        if (hadSelection)
            emit objectSelectionChanged();
        update();
        return;
    }

    m_inlineTextBoxEditor->configure(
        textBox->captureState(), m_zoomLevel, m_isDarkMode);
    QRect geometry = inlineTextEditorRect(textBox)
        .adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect();
    geometry.setWidth(qMax(8, geometry.width()));
    geometry.setHeight(qMax(8, geometry.height()));
    m_inlineTextBoxEditor->setGeometry(geometry);
    m_inlineTextBoxEditor->raise();
}

void DocumentViewport::startInlineTextEdit(TextBoxObject* textBox,
                                           bool newBox)
{
    if (!textBox || !m_document
        || textBox->type() != QLatin1String("textbox")) {
        return;
    }

    if (m_inlineEditSession.active) {
        if (m_inlineEditSession.objectId == textBox->id)
            return;
        commitInlineTextEdit();
    }
    if (!m_selectedObjects.contains(textBox)) {
        deselectAllObjects();
        selectObject(textBox, false);
    }

    InlineTextEditSession session;
    session.document = m_document;
    session.objectId = textBox->id;
    session.newBox = newBox;
    session.startState = textBox->captureState();

    if (m_document->isEdgeless()) {
        bool found = false;
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(textBox->id)) {
                session.tileCoord = coord;
                found = true;
                break;
            }
        }
        if (!found)
            return;
    } else {
        session.pageIndex = pageIndexForObject(textBox);
        if (session.pageIndex < 0)
            return;
    }

    if (textBox->usesLegacyLayout())
        textBox->upgradeToCurrentLayout();
    session.lastAcceptedState = textBox->captureState();
    session.active = true;
    m_inlineEditSession = session;
    syncTextBoxFormatBar();

    if (!m_inlineTextBoxEditor) {
        m_inlineTextBoxEditor = new InlineTextBoxEditor(this);
        connect(m_inlineTextBoxEditor,
                &InlineTextBoxEditor::sourceChanged,
                this, &DocumentViewport::handleInlineTextSourceChanged);
        connect(m_inlineTextBoxEditor,
                &InlineTextBoxEditor::commitRequested,
                this, &DocumentViewport::commitInlineTextEdit);
        connect(m_inlineTextBoxEditor,
                &InlineTextBoxEditor::cancelRequested,
                this, &DocumentViewport::cancelInlineTextEdit);
    }

    m_inlineTextBoxEditor->configure(
        session.lastAcceptedState, m_zoomLevel, m_isDarkMode);
    m_inlineTextBoxEditor->setText(session.lastAcceptedState.text);
    updateInlineTextEditorGeometry();
    m_inlineTextBoxEditor->show();
    m_inlineTextBoxEditor->raise();
    m_inlineTextBoxEditor->editor()->setFocus(Qt::OtherFocusReason);
    updateTextBoxFormatBarGeometry();
    update();
}

void DocumentViewport::handleInlineTextSourceChanged(
    const QString& source)
{
    if (m_revertingInlineText || !m_inlineEditSession.active
        || !m_inlineTextBoxEditor) {
        return;
    }

    TextBoxObject* textBox = resolveInlineTextBox();
    if (!textBox) {
        m_inlineTextBoxEditor->hide();
        m_inlineEditSession.clear();
        const bool hadSelection = !m_selectedObjects.isEmpty();
        m_selectedObjects.clear();
        m_hoveredObject = nullptr;
        if (hadSelection)
            emit objectSelectionChanged();
        update();
        return;
    }

    const QTextCursor cursorBeforeChange =
        m_inlineTextBoxEditor->takeCursorBeforeLastChange();
    const QRectF oldBounds = objectBoundsInViewport(textBox);
    const TextBoxState previous =
        m_inlineEditSession.lastAcceptedState;

    textBox->applyState(previous);
    textBox->text = source;
    textBox->reflowToWidth(previous.size.width());
    TextBoxState candidate = textBox->captureState();

    // Preserve the local top-left point in world/container coordinates while
    // content-derived height changes around the object's rotation center.
    preserveTextBoxTopAnchor(previous, candidate);

    if (!textBoxGeometryProposalAllowed(
            previous, candidate, m_inlineEditSession.pageIndex)) {
        textBox->applyState(previous);
        m_revertingInlineText = true;
        const int documentLength = previous.text.size();
        QTextCursor restored(
            m_inlineTextBoxEditor->editor()->document());
        const int anchor =
            qBound(0, cursorBeforeChange.anchor(), documentLength);
        const int position =
            qBound(0, cursorBeforeChange.position(), documentLength);
        restored.setPosition(anchor);
        restored.setPosition(position, QTextCursor::KeepAnchor);
        m_inlineTextBoxEditor->setText(previous.text, &restored);
        m_revertingInlineText = false;
        showObjectGeometryFeedback(
            tr("Text box cannot grow beyond the page"),
            oldBounds);
        updateInlineTextEditorGeometry();
        return;
    }

    textBox->applyState(candidate);
    m_inlineEditSession.lastAcceptedState = candidate;
    updateInlineTextEditorGeometry();
    updateTextBoxFormatBarGeometry();
    const QRectF dirty = oldBounds.united(
        objectBoundsInViewport(textBox))
        .adjusted(-8.0, -8.0, 8.0, 8.0);
    update(dirty.toAlignedRect());
}

void DocumentViewport::markInlineTextEditCommitted()
{
    if (!m_document || !m_inlineEditSession.active)
        return;

    if (m_document->isEdgeless()) {
        m_document->markTileDirty(m_inlineEditSession.tileCoord);
    } else if (m_inlineEditSession.pageIndex >= 0) {
        m_document->markPageDirty(m_inlineEditSession.pageIndex);
        m_pendingThumbnailPages.insert(
            m_inlineEditSession.pageIndex);
        emit pageModified(m_inlineEditSession.pageIndex);
    }
    emit documentModified();
    emit textBoxLayoutCommitted();
}

void DocumentViewport::removeUncommittedInlineTextBox()
{
    if (!m_document || !m_inlineEditSession.active)
        return;

    TextBoxObject* textBox = resolveInlineTextBox();
    const QRectF dirty = textBox
        ? objectBoundsInViewport(textBox)
            .adjusted(-12.0, -12.0, 12.0, 12.0)
        : QRectF();
    const QString objectId = m_inlineEditSession.objectId;
    m_selectedObjects.removeAll(textBox);
    if (m_hoveredObject == textBox)
        m_hoveredObject = nullptr;

    if (m_document->isEdgeless()) {
        Page* tile = m_document->getTile(
            m_inlineEditSession.tileCoord.first,
            m_inlineEditSession.tileCoord.second);
        if (tile)
            tile->removeObject(objectId);
        m_document->removeTileIfEmpty(
            m_inlineEditSession.tileCoord.first,
            m_inlineEditSession.tileCoord.second);
    } else if (m_inlineEditSession.pageIndex >= 0) {
        Page* page = m_document->page(
            m_inlineEditSession.pageIndex);
        if (page)
            page->removeObject(objectId);
    }
    emit objectSelectionChanged();
    if (!dirty.isEmpty())
        update(dirty.toAlignedRect());
    else
        update();
}

void DocumentViewport::endInlineTextEdit(bool commit,
                                         bool targetBeingDeleted)
{
    if (!m_inlineEditSession.active)
        return;

    TextBoxObject* textBox = resolveInlineTextBox();
    if (!textBox) {
        if (m_inlineTextBoxEditor)
            m_inlineTextBoxEditor->hide();
        m_inlineEditSession.clear();
        const bool hadSelection = !m_selectedObjects.isEmpty();
        m_selectedObjects.clear();
        m_hoveredObject = nullptr;
        if (hadSelection)
            emit objectSelectionChanged();
        update();
        return;
    }

    const bool removeEmptyNew =
        m_inlineEditSession.newBox
        && textBox->text.trimmed().isEmpty();
    if (targetBeingDeleted && m_inlineEditSession.newBox) {
        removeUncommittedInlineTextBox();
    } else if (targetBeingDeleted) {
        // The delete action snapshots the current accepted object state.
    } else if (!commit) {
        textBox->applyState(m_inlineEditSession.startState);
        if (m_inlineEditSession.newBox)
            removeUncommittedInlineTextBox();
    } else if (removeEmptyNew) {
        removeUncommittedInlineTextBox();
    } else {
        const Document::TileCoord oldTile =
            m_inlineEditSession.tileCoord;
        Document::TileCoord newTile = oldTile;
        if (m_document->isEdgeless()) {
            relocateObjectsToCorrectTiles();
            for (const auto& coord :
                 m_document->allLoadedTileCoords()) {
                Page* tile =
                    m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(textBox->id)) {
                    newTile = coord;
                    break;
                }
            }
        }

        if (m_inlineEditSession.newBox) {
            m_inlineEditSession.tileCoord = newTile;
            pushObjectInsertUndo(
                textBox, m_inlineEditSession.pageIndex,
                m_inlineEditSession.tileCoord);
            markInlineTextEditCommitted();
        } else if (!textBoxStatesEqual(
                       m_inlineEditSession.startState,
                       textBox->captureState())) {
            pushObjectTextEditUndo(
                textBox, m_inlineEditSession.startState,
                textBox->captureState(),
                m_inlineEditSession.pageIndex,
                oldTile, newTile);
            m_inlineEditSession.tileCoord = newTile;
            markInlineTextEditCommitted();
        }
    }

    if (m_inlineTextBoxEditor)
        m_inlineTextBoxEditor->hide();
    m_inlineEditSession.clear();
    update();
}

void DocumentViewport::commitInlineTextEdit()
{
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    endInlineTextEdit(true);
    syncTextBoxFormatBar();
}

void DocumentViewport::cancelInlineTextEdit()
{
    closeTextBoxFormatPopups(false);
    finishTextBoxFormatInteraction(false);
    endInlineTextEdit(false);
    syncTextBoxFormatBar();
}

QRectF DocumentViewport::proposedTextBoxCreationRect(
    const QPointF& startPoint, const QPointF& currentPoint,
    int pageIndex) const
{
    const qreal horizontalDistance =
        qAbs(currentPoint.x() - startPoint.x());
    const bool clickCreation =
        horizontalDistance < TextBoxObject::MINIMUM_WIDTH;
    qreal width = clickCreation
        ? TextBoxObject::DEFAULT_CREATION_WIDTH
        : qMax(TextBoxObject::MINIMUM_WIDTH, horizontalDistance);

    if (m_document && !m_document->isEdgeless() && pageIndex >= 0) {
        const QSizeF pageSize = m_document->pageSizeAt(pageIndex);
        if (pageSize.width() > 0.0)
            width = qMin(width, pageSize.width());
    }
    width = qMax<qreal>(1.0, width);

    TextBoxObject probe;
    probe.textLayoutVersion = TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
    probe.fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
    probe.fontColor = m_penColor;
    probe.text.clear();
    probe.size = QSizeF(width, 1.0);
    probe.reflowToWidth(width);

    const qreal left = clickCreation
        ? startPoint.x() - width / 2.0
        : qMin(startPoint.x(), currentPoint.x());
    QPointF topLeft(left, startPoint.y() - probe.size.height() / 2.0);

    if (m_document && !m_document->isEdgeless() && pageIndex >= 0) {
        topLeft = ObjectConstraints::clampPosition(
            topLeft, probe.size, m_document->pageSizeAt(pageIndex));
    }
    return QRectF(topLeft, probe.size);
}

QRectF DocumentViewport::proposedTextBoxCreationRectInViewport() const
{
    if (!m_document || !m_isCreatingTextBox)
        return QRectF();

    QPointF currentPoint;
    if (m_document->isEdgeless()) {
        currentPoint = viewportToDocument(m_lastPointerPos);
        const QRectF documentRect = proposedTextBoxCreationRect(
            m_textBoxCreateStartDoc, currentPoint, -1);
        return QRectF(documentToViewport(documentRect.topLeft()),
                      documentRect.size() * m_zoomLevel);
    }

    const PageHit hit = viewportToPage(m_lastPointerPos);
    currentPoint =
        hit.pageIndex == m_textBoxCreatePageIndex && hit.pageIndex >= 0
        ? hit.pagePoint
        : m_textBoxCreateStartDoc;
    const QRectF pageRect = proposedTextBoxCreationRect(
        m_textBoxCreateStartDoc, currentPoint, m_textBoxCreatePageIndex);
    const QPointF documentTopLeft =
        pagePosition(m_textBoxCreatePageIndex) + pageRect.topLeft();
    return QRectF(documentToViewport(documentTopLeft),
                  pageRect.size() * m_zoomLevel);
}

bool DocumentViewport::textBoxGeometryProposalAllowed(
    const TextBoxState& oldState, const TextBoxState& proposedState,
    int pageIndex) const
{
    if (!m_document || m_document->isEdgeless() || pageIndex < 0)
        return true;

    const QSizeF pageSize = m_document->pageSizeAt(pageIndex);
    auto overflow = [&pageSize](const TextBoxState& state) {
        const QRectF rect(state.position, state.size);
        return QVector<qreal>{
            qMax<qreal>(0.0, -rect.left()),
            qMax<qreal>(0.0, -rect.top()),
            qMax<qreal>(0.0, rect.right() - pageSize.width()),
            qMax<qreal>(0.0, rect.bottom() - pageSize.height())
        };
    };

    const QVector<qreal> oldOverflow = overflow(oldState);
    const QVector<qreal> proposedOverflow = overflow(proposedState);
    for (int i = 0; i < proposedOverflow.size(); ++i) {
        if (proposedOverflow[i] > oldOverflow[i] + 0.01)
            return false;
    }
    return true;
}

void DocumentViewport::showObjectGeometryFeedback(
    const QString& message, const QRectF& anchorViewportRect)
{
    m_objectGeometryFeedbackText = message;
    m_objectGeometryFeedbackAnchor = anchorViewportRect;
    if (m_objectGeometryFeedbackTimer)
        m_objectGeometryFeedbackTimer->start();
    update(anchorViewportRect.adjusted(-16.0, -16.0, 260.0, 80.0)
               .toAlignedRect());
}

QColor DocumentViewport::textBackdropForPage(const Page* page) const
{
    // PDF paper is whatever the renderer makes of it, not what the page color
    // says, so the inversion setting decides for those pages.
    if (page && page->backgroundType == Page::BackgroundType::PDF)
        return TextBoxObject::defaultBackgroundColor(
            m_isDarkMode && m_pdfDarkModeEnabled);

    QColor paper;
    if (page)
        paper = page->backgroundColor;
    else if (m_document)
        paper = m_document->defaultBackgroundColor;

    const bool darkPaper = paper.isValid() && paper.alpha() > 0
        ? paper.lightness() < 128
        : m_isDarkMode;
    return TextBoxObject::defaultBackgroundColor(darkPaper);
}

void DocumentViewport::createTextBoxAtRect(int pageIndex, const QRectF& rect, const QPointF& viewportPos)
{
    if (!m_document) return;
    Q_UNUSED(viewportPos);

    auto textObj = std::make_unique<TextBoxObject>();
    textObj->position = rect.topLeft();
    textObj->size = QSizeF(qMax<qreal>(1.0, rect.width()), 1.0);
    textObj->text = QString();
    textObj->fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
    textObj->textLayoutVersion =
        TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
    textObj->showBorder = true;
    textObj->visible = true;
    textObj->fontColor = m_penColor;
    textObj->reflowToWidth(textObj->size.width());

    TextBoxObject* rawPtr = textObj.get();

    if (m_document->isEdgeless()) {
        auto coord = m_document->tileCoordForPoint(rect.topLeft());

        Page* targetTile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!targetTile) {
            qWarning() << "createTextBoxAtRect: Failed to get/create tile";
            return;
        }

        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                           coord.second * Document::EDGELESS_TILE_SIZE);
        textObj->position = rect.topLeft() - tileOrigin;
        textObj->backgroundColor = textBackdropForPage(targetTile);

        int activeLayer = m_edgelessActiveLayerIndex;
        int defaultAffinity = activeLayer - 1;
        textObj->setLayerAffinity(defaultAffinity);
        textObj->zOrder = getNextZOrderForAffinity(targetTile, defaultAffinity);

        targetTile->addObject(std::move(textObj));
    } else {
        Page* page = m_document->page(pageIndex);
        if (!page) {
            qWarning() << "createTextBoxAtRect: No page at index" << pageIndex;
            return;
        }
        textObj->backgroundColor = textBackdropForPage(page);

        int activeLayer = page->activeLayerIndex;
        int defaultAffinity = activeLayer - 1;
        textObj->setLayerAffinity(defaultAffinity);
        textObj->zOrder = getNextZOrderForAffinity(page, defaultAffinity);

        const qreal pageWidth = qMax<qreal>(1.0, page->size.width());
        if (textObj->size.width() > pageWidth)
            textObj->reflowToWidth(pageWidth);
        textObj->position = clampObjectPositionToPage(
            pageIndex, textObj->position, textObj->size);

        page->addObject(std::move(textObj));
    }

    deselectAllObjects();
    selectObject(rawPtr, false);

    update();
    startInlineTextEdit(rawPtr, true);
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

                // Mark page/tile dirty and refresh the outline cache so the
                // right sidebar drops the now-broken slot reference.
                markLinkContainerDirtyAndRefreshOutline(link);

                emit documentModified();
                emit linkObjectListMayHaveChanged();   // M.7.3: refresh the right sidebar
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
    // Mark page/tile dirty and refresh the outline cache so the right
    // sidebar drops the cleared slot instead of rendering it as
    // "(missing note)" on its next lazy populate.
    markLinkContainerDirtyAndRefreshOutline(link);

    emit documentModified();
    emit linkObjectListMayHaveChanged();   // M.7.3: refresh the right sidebar
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
        emit userWarning(tr("Cannot create note: please save the document first."));
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
        emit userWarning(tr("Failed to create note file. Check disk space and permissions."));
        return;
    }
    
    // Update slot
    link->linkSlots[slotIndex].type = LinkSlot::Type::Markdown;
    link->linkSlots[slotIndex].markdownNoteId = noteId;
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "createMarkdownNoteForSlot: Created note" << noteId 
             << "for slot" << slotIndex << "title:" << note.title;
    #endif
    // Phase M.9: this link just gained a markdown slot, so its container's
    // outline contribution changed — mark the container dirty and refresh
    // the outline cache entry for that container before refreshNotesOutline
    // runs.
    markLinkContainerDirtyAndRefreshOutline(link);

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

void DocumentViewport::toggleImageAspectRatioLock()
{
    if (!m_document || m_selectedObjects.size() != 1) return;
    
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj || obj->type() != "image") return;
    
    auto* img = dynamic_cast<ImageObject*>(obj);
    if (!img) return;
    
    bool oldLock = img->maintainAspectRatio;
    QPointF oldPos = img->position;
    QSizeF oldSize = img->size;
    
    if (!oldLock) {
        // Locking: adjust width to match original aspect ratio, keeping height
        img->maintainAspectRatio = true;
        if (img->originalAspectRatio > 0.0) {
            QPointF oldCenter = img->center();
            img->size.setWidth(img->size.height() * img->originalAspectRatio);
            img->position.setX(oldCenter.x() - img->size.width() / 2.0);
            img->position.setY(oldCenter.y() - img->size.height() / 2.0);
            
            // Restoring the aspect ratio widens the image, which can push it
            // past a page edge
            int pageIndex = pageIndexForObject(obj);
            if (pageIndex >= 0) {
                img->size = ObjectConstraints::shrinkToFit(
                    img->size, m_document->pageSizeAt(pageIndex));
                // Re-derive from the centre: fitting the size would otherwise
                // pin the top-left and undo the centring above
                img->position = oldCenter - QPointF(img->size.width() / 2.0,
                                                    img->size.height() / 2.0);
                clampObjectToPage(img, pageIndex);
            }
        }
    } else {
        // Unlocking: just clear the flag, no size change
        img->maintainAspectRatio = false;
    }
    
    pushObjectResizeUndo(obj, oldPos, oldSize, obj->rotation, oldLock);
    
    if (m_document->isEdgeless()) {
        Document::TileCoord tileCoord = {0, 0};
        findPageContainingObject(obj, &tileCoord);
        m_document->markTileDirty(tileCoord);
    } else {
        m_document->markPageDirty(m_currentPageIndex);
    }
    
    emit objectSelectionChanged();
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
    
    // ===== Phase 2C: Draw rubber-band rectangle for text box creation =====
    if (m_isCreatingTextBox) {
        const QRectF rubberBand = proposedTextBoxCreationRectInViewport();
        QPen rbPenWhite(Qt::white, 1, Qt::DashLine);
        rbPenWhite.setCosmetic(true);
        QPen rbPenBlack(Qt::black, 1, Qt::DashLine);
        rbPenBlack.setCosmetic(true);
        rbPenBlack.setDashOffset(4);
        painter.setBrush(QColor(0, 120, 215, 30));
        painter.setPen(rbPenWhite);
        painter.drawRect(rubberBand);
        painter.setPen(rbPenBlack);
        painter.drawRect(rubberBand);
    }

    if (!m_objectGeometryFeedbackText.isEmpty()) {
        QFont feedbackFont = painter.font();
        feedbackFont.setBold(true);
        painter.setFont(feedbackFont);
        const QFontMetricsF metrics(feedbackFont);
        const QSizeF textSize(
            metrics.horizontalAdvance(m_objectGeometryFeedbackText) + 20.0,
            metrics.height() + 12.0);
        QPointF topLeft(
            m_objectGeometryFeedbackAnchor.left(),
            m_objectGeometryFeedbackAnchor.bottom() + 8.0);
        topLeft.setX(qBound<qreal>(
            6.0, topLeft.x(), qMax<qreal>(6.0, width() - textSize.width() - 6.0)));
        topLeft.setY(qBound<qreal>(
            6.0, topLeft.y(), qMax<qreal>(6.0, height() - textSize.height() - 6.0)));
        const QRectF messageRect(topLeft, textSize);
        painter.setPen(QPen(QColor(180, 40, 40), 1.0));
        painter.setBrush(QColor(120, 20, 20, 220));
        painter.drawRoundedRect(messageRect, 5.0, 5.0);
        painter.setPen(Qt::white);
        painter.drawText(messageRect.adjusted(10.0, 6.0, -10.0, -6.0),
                         Qt::AlignCenter, m_objectGeometryFeedbackText);
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
        if (m_inlineEditSession.active
            && obj->id == m_inlineEditSession.objectId)
            continue;
        
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
        if (obj && (!m_inlineEditSession.active
                    || obj->id != m_inlineEditSession.objectId)) {
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
            
            // Draw scale handles (squares) - rotated with the object.
            // User text boxes only expose left/right width handles.
            QPen handlePen(Qt::black, 1);
            painter.setPen(handlePen);
            painter.setBrush(Qt::white);
            
            qreal halfSize = HANDLE_VISUAL_SIZE / 2.0;
            const bool userTextBox =
                obj->type() == QLatin1String("textbox");
            for (int i = 0; i < 8; ++i) {
                if (userTextBox && i != 3 && i != 4)
                    continue;
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
        
        // Only tiles the lasso reaches can contribute, and within those, a stroke
        // whose bounds miss the lasso cannot intersect it. Both rejections come
        // before the per-point copy below, which used to be paid for every stroke
        // on the canvas no matter how small the lasso was.
        const QRectF lassoBounds = m_lassoPath.boundingRect();
        const QVector<Document::TileCoord> tiles = m_document->tilesInRect(
            lassoBounds.adjusted(-EDGELESS_STROKE_MARGIN, -EDGELESS_STROKE_MARGIN,
                                 EDGELESS_STROKE_MARGIN, EDGELESS_STROKE_MARGIN));
        
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
                
                if (!stroke.boundingBox.translated(tileOrigin).intersects(lassoBounds)) {
                    continue;
                }
                
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
        
        // Bounds rejection first: strokeIntersectsLasso() tests every point of the
        // stroke against the polygon, and each of those tests walks the polygon.
        const QRectF lassoBounds = m_lassoPath.boundingRect();
        const auto& strokes = layer->strokes();
        for (int i = 0; i < strokes.size(); ++i) {
            const VectorStroke& stroke = strokes[i];
            
            if (!stroke.boundingBox.intersects(lassoBounds)) continue;
            
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
    
    // QWidget::grab() includes visible child widgets. The floating format bar
    // remains live and follows the object during a drag, so including it in
    // the cached canvas would leave a second, frozen copy at the drag origin.
    const bool restoreFormatBar =
        m_textBoxFormatBar && !m_textBoxFormatBar->isHidden();
    if (restoreFormatBar)
        m_textBoxFormatBar->hide();

    // Temporarily disable selected object rendering
    m_skipSelectedObjectRendering = true;
    
    // Capture the viewport (this triggers a paint without selected objects)
    m_objectDragBackgroundSnapshot = grab();
    m_objectDragSnapshotDpr = m_objectDragBackgroundSnapshot.devicePixelRatio();
    
    // Re-enable selected object rendering
    m_skipSelectedObjectRendering = false;

    if (restoreFormatBar) {
        m_textBoxFormatBar->show();
        updateTextBoxFormatBarGeometry();
        m_textBoxFormatBar->raise();
    }
    
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
    
    UndoAction undoAction;
    undoAction.type = UndoAction::TransformSelection;
    undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;

    if (m_document->isEdgeless()) {
        // ========== EDGELESS MODE ==========
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;

            QVector<VectorStroke>& layerStrokes = layer->strokes();
            for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        UndoAction::StrokeSegment seg;
                        seg.tileCoord = coord;
                        seg.stroke = layerStrokes[i];
                        undoAction.removedSegments.append(seg);
                        layerStrokes.removeAt(i);
                        layer->invalidateStrokeCache();
                        m_document->markTileDirty(coord);
                        break;
                    }
                }
            }
        }

        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            auto addedSegments = addStrokeToEdgelessTiles(transformedStroke, m_lassoSelection.sourceLayerIndex);
            for (const auto& s : addedSegments) {
                UndoAction::StrokeSegment seg;
                seg.tileCoord = s.first;
                seg.stroke = s.second;
                undoAction.addedSegments.append(seg);
            }
        }
    } else {
        // ========== PAGED MODE (with cross-page relocation) ==========
        int srcPage = m_lassoSelection.sourcePageIndex;
        if (srcPage < 0 || srcPage >= m_document->pageCount()) return;

        Page* page = m_document->page(srcPage);
        if (!page) return;
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;

        // Remove original strokes from source page
        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    UndoAction::StrokeSegment seg;
                    seg.pageIndex = srcPage;
                    seg.stroke = layerStrokes[i];
                    undoAction.removedSegments.append(seg);
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        layer->invalidateStrokeCache();

        // Add transformed strokes -- each may land on a different page
        QPointF srcOrigin = pagePosition(srcPage);
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke;
            transformStrokePoints(transformedStroke, transform);
            transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            transformedStroke.updateBoundingBox();

            // Determine destination page by stroke centre in document coords
            QPointF docCenter = srcOrigin + transformedStroke.boundingBox.center();
            int destPage = pageAtPoint(docCenter);
            if (destPage < 0) {
                // Landed in page gap -- snap to nearest page
                qreal minDist = std::numeric_limits<qreal>::max();
                for (int p = 0; p < m_document->pageCount(); ++p) {
                    QRectF pr = pageRect(p);
                    qreal dist = qAbs(docCenter.y() - pr.center().y());
                    if (dist < minDist) { minDist = dist; destPage = p; }
                }
                if (destPage < 0) destPage = srcPage;
            }

            // If destination differs, translate stroke points to destination-local coords
            if (destPage != srcPage) {
                QPointF dstOrigin = pagePosition(destPage);
                QPointF offset = srcOrigin - dstOrigin;
                for (auto& pt : transformedStroke.points)
                    pt.pos += offset;
                transformedStroke.updateBoundingBox();
            }

            Page* dstPageObj = m_document->page(destPage);
            if (!dstPageObj) continue;
            while (dstPageObj->layerCount() <= m_lassoSelection.sourceLayerIndex)
                dstPageObj->addLayer(QString("Layer %1").arg(dstPageObj->layerCount() + 1));
            VectorLayer* dstLayer = dstPageObj->layer(m_lassoSelection.sourceLayerIndex);
            if (!dstLayer) continue;
            dstLayer->addStroke(transformedStroke);
            dstLayer->invalidateStrokeCache();
            m_document->markPageDirty(destPage);

            UndoAction::StrokeSegment seg;
            seg.pageIndex = destPage;
            seg.stroke = transformedStroke;
            undoAction.addedSegments.append(seg);
        }

        m_document->markPageDirty(srcPage);
    }

    if (!undoAction.removedSegments.isEmpty() || !undoAction.addedSegments.isEmpty()) {
        markOcrDirtyTiles(undoAction);
        pushUndoAction(undoAction);
        emit strokesChanged();
    }
    
    clearLassoSelection();
    emit documentModified();

    if (!m_document->isEdgeless()) {
        QSet<int> pages;
        for (const auto& s : undoAction.removedSegments) if (s.pageIndex >= 0) pages.insert(s.pageIndex);
        for (const auto& s : undoAction.addedSegments) if (s.pageIndex >= 0) pages.insert(s.pageIndex);
        for (int p : pages) emit pageModified(p);
    }
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
    if (m_inlineEditSession.active) {
        cancelInlineTextEdit();
        return true;
    }
    
    // Priority 1: Cancel lasso selection or drawing (Lasso tool only)
    // Note: Lasso selection is cleared when switching away from Lasso tool,
    // so this check only needs to handle the Lasso tool.
    if (m_currentTool == ToolType::Lasso) {
        if (m_lassoSelection.isValid() || m_isDrawingLasso) {
            cancelSelectionTransform();
            return true;
        }
    }
    
    // Priority 2: Cancel an in-progress ObjectSelect gesture.
    if (m_currentTool == ToolType::ObjectSelect
        && (m_isCreatingTextBox || m_isDraggingObjects || m_isResizingObject)) {
        cancelObjectPointerGesture();
        return true;
    }

    // Priority 3: Deselect objects or clear object clipboard (ObjectSelect tool only)
    if (m_currentTool == ToolType::ObjectSelect) {
        if (hasSelectedObjects() || !s_objectClipboard.isEmpty()) {
            cancelObjectSelectAction();
            return true;
        }
    }
    
    // Priority 4: Cancel text selection (Highlighter tool only)
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
            if (s_clipboard.hasContent) {
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
    
    s_clipboard.clear();
    
    // Get current transform and apply it to strokes before copying
    QTransform transform = buildSelectionTransform();
    
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        VectorStroke transformedStroke = stroke;
        transformStrokePoints(transformedStroke, transform);
        // Give new ID to avoid conflicts when pasting
        transformedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s_clipboard.strokes.append(transformedStroke);
    }
    
    s_clipboard.hasContent = true;
    
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
    if (!s_clipboard.hasContent || s_clipboard.strokes.isEmpty() || !m_document) {
        return;
    }
    
    // Calculate clipboard bounding box
    QRectF clipboardBounds;
    for (const VectorStroke& stroke : s_clipboard.strokes) {
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
        UndoAction undoAction;
        undoAction.type = UndoAction::AddStroke;
        undoAction.layerIndex = m_edgelessActiveLayerIndex;

        for (const VectorStroke& stroke : s_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            for (StrokePoint& pt : pastedStroke.points)
                pt.pos += offset;
            pastedStroke.updateBoundingBox();

            auto addedSegments = addStrokeToEdgelessTiles(pastedStroke, m_edgelessActiveLayerIndex);
            for (const auto& seg : addedSegments) {
                UndoAction::StrokeSegment s;
                s.tileCoord = seg.first;
                s.stroke = seg.second;
                undoAction.segments.append(s);
            }
        }
        if (!undoAction.segments.isEmpty())
            pushUndoAction(undoAction);
    } else {
        int pageIndex = currentPageIndex();
        if (pageIndex < 0 || pageIndex >= m_document->pageCount()) return;
        Page* page = m_document->page(pageIndex);
        if (!page) return;
        VectorLayer* layer = page->activeLayer();
        if (!layer) return;

        UndoAction undoAction;
        undoAction.type = UndoAction::AddStroke;
        undoAction.layerIndex = page->activeLayerIndex;

        QPointF pageOrigin = pagePosition(pageIndex);
        QPointF pageCenter = docCenter - pageOrigin;
        offset = pageCenter - clipboardCenter;

        for (const VectorStroke& stroke : s_clipboard.strokes) {
            VectorStroke pastedStroke = stroke;
            for (StrokePoint& pt : pastedStroke.points)
                pt.pos += offset;
            pastedStroke.updateBoundingBox();
            pastedStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            layer->addStroke(pastedStroke);

            UndoAction::StrokeSegment seg;
            seg.pageIndex = pageIndex;
            seg.stroke = pastedStroke;
            undoAction.segments.append(seg);
        }
        m_document->markPageDirty(pageIndex);
        pushUndoAction(undoAction);
    }
    
    update();
    emit documentModified();
}

void DocumentViewport::deleteSelection()
{
    if (!m_lassoSelection.isValid() || !m_document) {
        return;
    }
    
    UndoAction undoAction;
    undoAction.type = UndoAction::RemoveMultiple;
    undoAction.layerIndex = m_lassoSelection.sourceLayerIndex;

    if (m_document->isEdgeless()) {
        auto tiles = m_document->allLoadedTileCoords();
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_lassoSelection.sourceLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_lassoSelection.sourceLayerIndex);
            if (!layer) continue;

            QVector<VectorStroke>& layerStrokes = layer->strokes();
            bool modified = false;
            for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
                for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                    if (layerStrokes[i].id == selectedStroke.id) {
                        UndoAction::StrokeSegment seg;
                        seg.tileCoord = coord;
                        seg.stroke = layerStrokes[i];
                        undoAction.segments.append(seg);
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
    } else {
        int srcPage = m_lassoSelection.sourcePageIndex;
        if (srcPage < 0 || srcPage >= m_document->pageCount()) return;
        Page* page = m_document->page(srcPage);
        if (!page) return;
        VectorLayer* layer = page->layer(m_lassoSelection.sourceLayerIndex);
        if (!layer) return;

        QVector<VectorStroke>& layerStrokes = layer->strokes();
        for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
            for (const VectorStroke& selectedStroke : m_lassoSelection.selectedStrokes) {
                if (layerStrokes[i].id == selectedStroke.id) {
                    UndoAction::StrokeSegment seg;
                    seg.pageIndex = srcPage;
                    seg.stroke = layerStrokes[i];
                    undoAction.segments.append(seg);
                    layerStrokes.removeAt(i);
                    break;
                }
            }
        }
        layer->invalidateStrokeCache();
        if (!undoAction.segments.isEmpty())
            m_document->markPageDirty(srcPage);
    }

    if (!undoAction.segments.isEmpty())
        pushUndoAction(undoAction);
    
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

void DocumentViewport::recolorLassoSelection(const QColor& newColor)
{
    if (!m_lassoSelection.isValid() || !m_document || !newColor.isValid()) {
        return;
    }

    // Build the set of selected stroke IDs once for O(1) lookup per layer
    // stroke (the existing selection stores strokes by value, not just by id).
    const QSet<QString>& selectedIds = m_lassoSelection.getSelectedIds();

    UndoAction action;
    action.type = UndoAction::RecolorStrokes;
    action.layerIndex = m_lassoSelection.sourceLayerIndex;
    action.recolorNewColor = newColor;

    // Apply NEW color to one stroke, preserving its existing alpha so marker /
    // highlighter opacity is kept. Snapshots the old stroke into action.segments
    // first so undo() can restore the previous color.
    auto patchStroke = [&](VectorStroke& stroke,
                           int pageIndex,
                           Document::TileCoord coord) {
        UndoAction::StrokeSegment seg;
        seg.pageIndex = pageIndex;
        seg.tileCoord = coord;
        seg.stroke    = stroke;             // OLD color baked in here
        action.segments.append(seg);

        QColor c = newColor;
        c.setAlpha(stroke.color.alpha());
        stroke.color = c;
    };

    // Walk one container's layer, patch every stroke whose id is selected,
    // and invalidate the layer cache if anything changed. Returns true iff
    // at least one stroke was patched, so the caller can mark just the
    // affected page/tile dirty.
    auto applyToLayer = [&](VectorLayer* layer,
                            int pageIndex,
                            Document::TileCoord coord) -> bool {
        if (!layer) return false;
        QVector<VectorStroke>& strokes = layer->strokes();
        bool modified = false;
        for (int i = 0; i < strokes.size(); ++i) {
            if (selectedIds.contains(strokes[i].id)) {
                patchStroke(strokes[i], pageIndex, coord);
                modified = true;
            }
        }
        if (modified) layer->invalidateStrokeCache();
        return modified;
    };

    if (m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || action.layerIndex >= tile->layerCount()) continue;
            if (applyToLayer(tile->layer(action.layerIndex), -1, coord))
                m_document->markTileDirty(coord);
        }
    } else {
        int srcPage = m_lassoSelection.sourcePageIndex;
        if (srcPage < 0 || srcPage >= m_document->pageCount()) return;
        Page* page = m_document->page(srcPage);
        if (!page) return;
        if (applyToLayer(page->layer(action.layerIndex), srcPage, {0, 0}))
            m_document->markPageDirty(srcPage);
    }

    if (action.segments.isEmpty()) {
        return;
    }

    // Also patch the selection's own cached stroke copies so subsequent
    // recolors / transforms see the up-to-date colors. This keeps the
    // selection stroke cache (rendered overlay) consistent with the layer.
    for (VectorStroke& s : m_lassoSelection.selectedStrokes) {
        QColor c = newColor;
        c.setAlpha(s.color.alpha());
        s.color = c;
    }

    pushUndoAction(action);
    // No markOcrDirtyTiles call: stroke color does not affect OCR text content.
    emit strokesChanged();
    emit documentModified();

    // Selection stays active so the user can re-recolor or transform.
    // Invalidate the selection overlay cache so the new color paints.
    m_selectionStrokeCache = QPixmap();
    m_selectionCacheDirty = true;
    update();

    if (!m_document->isEdgeless()) {
        QSet<int> pages;
        for (const auto& s : action.segments) {
            if (s.pageIndex >= 0) pages.insert(s.pageIndex);
        }
        for (int p : pages) emit pageModified(p);
    }
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
    
    // Resolve the page's own PDF source (empty id = primary), so text selection
    // works for imported/non-primary PDF-backed pages, not just the primary PDF.
    // Mirrors PdfSearchEngine::searchPage and the page-rendering path.
    QString srcId;
    int pdfPageIdx = -1;
    if (!m_document->pdfBindingForNotebookPage(pageIndex, srcId, pdfPageIdx)) {
        return;
    }
    const PdfProvider* pdf = m_document->providerForSource(srcId);
    const int providerPage = m_document->resolveSourcePageIndex(srcId, pdfPageIdx);
    if (!pdf || !pdf->supportsTextExtraction() || providerPage < 0) {
        return;
    }

    m_textBoxCache = pdf->textBoxes(providerPage);
    m_textBoxCachePageIndex = pageIndex;
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
    
    // Resolve the page's own PDF source (empty id = primary) so links resolve for
    // imported/non-primary PDF-backed pages too. providerPage is in provider-document
    // space (original page for external files, mini-PDF index for bundled sources).
    QString srcId;
    int pdfPageIdx = -1;
    if (!m_document->pdfBindingForNotebookPage(pageIndex, srcId, pdfPageIdx)) {
        return;
    }
    const PdfProvider* pdf = m_document->providerForSource(srcId);
    const int providerPage = m_document->resolveSourcePageIndex(srcId, pdfPageIdx);
    if (!pdf || !pdf->supportsLinks() || providerPage < 0) {
        return;
    }

    m_linkCache = pdf->links(providerPage);
    m_linkCachePageIndex = pageIndex;
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

void DocumentViewport::activatePdfLink(const PdfLink& link, int fromPageIndex)
{
    switch (link.type) {
        case PdfLinkType::Goto:
            {
                // Resolve the destination within the SAME source as the clicked page.
                // link.targetPage is in provider-document space; convert it back to an
                // original page number, then find the notebook page showing that
                // (source, original page). Unresolved -> no-op (e.g. the destination
                // page was not imported into this document).
                QString srcId;
                int fromPdfPage = -1;
                int notebookPageIndex = -1;
                if (m_document->pdfBindingForNotebookPage(fromPageIndex, srcId, fromPdfPage)) {
                    const int origTarget = m_document->originalPageForProviderIndex(srcId, link.targetPage);
                    if (origTarget >= 0) {
                        notebookPageIndex = m_document->notebookPageIndexForSourcePage(srcId, origTarget);
                    }
                }
                if (notebookPageIndex >= 0) {
                    #ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "PDF link: navigating to PDF page" << link.targetPage 
                             << "(notebook page" << notebookPageIndex << ")";
                    #endif
                    scrollToPage(notebookPageIndex);
                } else {
                    qWarning() << "PDF link: target PDF page" << link.targetPage 
                               << "not found in notebook";
                }
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
    if (!m_document) {
        setCursor(Qt::ArrowCursor);
        return;
    }

    const bool ocrMode = (m_highlighterMode == HighlighterMode::Ocr);

    // Edgeless: no PDF pages, and hit testing is done against tiles.
    // OCR mode is always available (blocks may live on any tile), PDF mode
    // is not applicable.
    if (m_document->isEdgeless()) {
        setCursor(ocrMode ? Qt::IBeamCursor : Qt::ForbiddenCursor);
        return;
    }

    PageHit hit = viewportToPage(viewportPos);
    if (!hit.valid()) {
        setCursor(Qt::ArrowCursor);
        return;
    }

    Page* page = m_document->page(hit.pageIndex);
    if (!page) {
        setCursor(Qt::ArrowCursor);
        return;
    }

    // OCR mode works on any page; PDF mode needs a PDF background.
    if (!ocrMode && page->backgroundType != Page::BackgroundType::PDF) {
        setCursor(Qt::ForbiddenCursor);
        return;
    }

    // PDF links are only relevant in PDF mode.
    if (!ocrMode) {
        const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
        if (link && link->type != PdfLinkType::None) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }

    setCursor(Qt::IBeamCursor);  // Text selection cursor
}

bool DocumentViewport::isHighlighterEnabled() const
{
    if (!m_document) return false;
    // In OCR mode the highlighter is always enabled (OCR blocks can exist on
    // any background type / tile). In PDF mode it only applies to PDF pages,
    // which don't exist in edgeless documents.
    if (m_highlighterMode == HighlighterMode::Ocr) {
        return true;
    }
    if (m_document->isEdgeless()) {
        return false;  // No PDF background in edgeless mode
    }
    Page* page = m_document->page(m_currentPageIndex);
    return page && page->backgroundType == Page::BackgroundType::PDF;
}

void DocumentViewport::setAutoHighlightStyle(HighlightStyle style)
{
    if (m_autoHighlightStyle == style) {
        return;
    }

    m_autoHighlightStyle = style;
    emit autoHighlightStyleChanged(style);
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Auto-highlight style:" << static_cast<int>(style);
    #endif
}

void DocumentViewport::setHighlighterMode(HighlighterMode mode)
{
    if (m_highlighterMode == mode) {
        return;
    }

    m_highlighterMode = mode;

    // Clear any in-flight selection because its cache reference is about to
    // point at the "wrong" cache. Signal listeners so action bar / UI updates.
    bool hadSelection = m_textSelection.isValid();
    m_textSelection.clear();
    m_lastHitBoxIndex = -1;
    m_lastOcrHitBlockIndex = -1;
    if (hadSelection) {
        emit textSelectionChanged(false);
    }

    emit highlighterModeChanged(mode);
    update();

    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Highlighter selection source:"
             << (mode == HighlighterMode::Pdf ? "PDF" : "OCR");
    #endif
}

// ============================================================================
// OCR Highlighter Cache
// ============================================================================

void DocumentViewport::loadOcrBlocksForPage(int pageIndex)
{
    if (!m_document) {
        m_ocrBlockCache.clear();
        m_ocrBlockCachePageIndex = -1;
        m_lastOcrHitBlockIndex = -1;
        return;
    }

    // Shared block-to-OcrBlockRef builder. Fills per-character rects from the
    // engine's real per-character geometry (wordSegments[].charBoundingBoxes,
    // flattened to block.text length); when that geometry is unavailable it
    // falls back to a proportional split of the block's bounding rect. The same
    // source/fallback policy is used by PdfSearchEngine::searchOcrBlocks so that
    // text-selection highlights line up with search highlights. Returns false if
    // the block is empty or has an invalid bounding rect (caller should skip).
    auto buildRef = [](const OcrTextBlock& block, QPointF originOffset,
                       OcrBlockRef& outRef) -> bool {
        if (block.text.isEmpty() || !block.boundingRect.isValid()) return false;
        outRef.text = block.text;
        outRef.blockRect = originOffset.isNull()
            ? block.boundingRect
            : block.boundingRect.translated(originOffset);

        const int n = outRef.text.length();
        if (n > 0) {
            outRef.charRects.resize(n);
            const QVector<QRectF> flat = flattenOcrBlockCharRects(block);
            if (flat.size() == n) {
                // Real per-character boxes (canvas space); shift into document
                // space for edgeless tiles via originOffset.
                for (int i = 0; i < n; ++i) {
                    outRef.charRects[i] = originOffset.isNull()
                        ? flat[i]
                        : flat[i].translated(originOffset);
                }
            } else {
                // Fallback: proportional split of the block bounding rect.
                const qreal charW = outRef.blockRect.width() / n;
                const qreal top = outRef.blockRect.top();
                const qreal left = outRef.blockRect.left();
                const qreal height = outRef.blockRect.height();
                for (int i = 0; i < n; ++i) {
                    outRef.charRects[i] = QRectF(left + i * charW, top, charW, height);
                }
            }
        }
        return true;
    };

    // Edgeless mode: tiles can be loaded/unloaded as the user pans, so we
    // cache against Document::tileLoadVersion() rather than a page index.
    // All block rects are converted to DOCUMENT-space coordinates (by adding
    // each tile's origin) so hit testing & rendering line up with the
    // edgeless view transform. Callers pass pageIndex == 0 in edgeless mode
    // (since pagePosition(0) == (0,0) in edgeless).
    if (m_document->isEdgeless()) {
        const quint64 currentVersion = m_document->tileLoadVersion();
        if (m_ocrBlockCachePageIndex == 0 &&
            m_ocrBlockCacheTileVersion == currentVersion) {
            return;  // Cache still valid: loaded tile set unchanged since last build.
        }

        m_ocrBlockCache.clear();
        m_lastOcrHitBlockIndex = -1;

        const int tileSize = Document::EDGELESS_TILE_SIZE;
        const QVector<Document::TileCoord> loadedTiles = m_document->allLoadedTileCoords();

        // Stage blocks from every loaded tile in a single pass, remembering
        // each tile's document-space origin. This lets us pre-size
        // m_ocrBlockCache exactly while calling ocrBlocksForSearch() at most
        // once per tile (it can be O(n) to copy when a page is ocrDirty).
        struct StagedTile {
            QPointF origin;
            QVector<OcrTextBlock> blocks;
        };
        QVector<StagedTile> staged;
        staged.reserve(loadedTiles.size());
        int estimated = 0;
        for (const auto& coord : loadedTiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile) continue;
            QVector<OcrTextBlock> blocks = tile->ocrBlocksForSearch();
            if (blocks.isEmpty()) continue;
            estimated += blocks.size();
            staged.append({QPointF(coord.first * tileSize,
                                   coord.second * tileSize),
                           std::move(blocks)});
        }
        m_ocrBlockCache.reserve(estimated);

        for (const StagedTile& s : staged) {
            for (const OcrTextBlock& block : s.blocks) {
                OcrBlockRef ref;
                if (buildRef(block, s.origin, ref)) {
                    m_ocrBlockCache.append(std::move(ref));
                }
            }
        }

        // Sort in reading order (top-to-bottom, then left-to-right) so
        // drag-selection across tile boundaries produces sensible ranges.
        std::sort(m_ocrBlockCache.begin(), m_ocrBlockCache.end(),
                  [](const OcrBlockRef& a, const OcrBlockRef& b) {
                      constexpr qreal lineThreshold = 8.0;
                      const qreal ay = a.blockRect.center().y();
                      const qreal by = b.blockRect.center().y();
                      if (qAbs(ay - by) > lineThreshold) return ay < by;
                      return a.blockRect.left() < b.blockRect.left();
                  });

        // pageIndex == 0 in edgeless means "loaded"; a negative index (as
        // sent by invalidateOcrBlockCache) will clear the cache normally.
        m_ocrBlockCachePageIndex = 0;
        m_ocrBlockCacheTileVersion = currentVersion;
        return;
    }

    // --- Paged mode ---
    if (pageIndex == m_ocrBlockCachePageIndex) {
        return;  // Already cached (may legitimately be empty)
    }

    m_ocrBlockCache.clear();
    m_ocrBlockCachePageIndex = -1;
    m_lastOcrHitBlockIndex = -1;

    if (pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }

    Page* page = m_document->page(pageIndex);
    if (!page) {
        return;
    }

    // Use "search"-quality blocks (excludes dirty / stale ones). Unlike the
    // PDF text cache, OCR data is available on any background type.
    const QVector<OcrTextBlock> blocks = page->ocrBlocksForSearch();
    m_ocrBlockCache.reserve(blocks.size());

    for (const OcrTextBlock& block : blocks) {
        OcrBlockRef ref;
        if (buildRef(block, QPointF(), ref)) {
            m_ocrBlockCache.append(std::move(ref));
        }
    }

    m_ocrBlockCachePageIndex = pageIndex;
}

void DocumentViewport::invalidateOcrBlockCache(int pageIndex)
{
    // pageIndex < 0 means "invalidate unconditionally"; otherwise only if the
    // cache currently refers to that page.
    if (pageIndex < 0 || pageIndex == m_ocrBlockCachePageIndex) {
        m_ocrBlockCache.clear();
        m_ocrBlockCachePageIndex = -1;
        // Force edgeless rebuilds on the next press even if the loaded tile
        // set happens to be unchanged (OCR content itself was invalidated).
        m_ocrBlockCacheTileVersion = 0;
        m_lastOcrHitBlockIndex = -1;
    }
}

DocumentViewport::CharacterPosition DocumentViewport::findOcrCharAtPoint(const QPointF& pagePoint) const
{
    CharacterPosition result;

    if (m_ocrBlockCache.isEmpty()) {
        return result;
    }

    auto checkBlock = [&](int blockIdx) -> bool {
        const OcrBlockRef& ref = m_ocrBlockCache[blockIdx];
        if (!ref.blockRect.contains(pagePoint)) {
            return false;
        }
        if (ref.charRects.isEmpty()) {
            result.boxIndex = blockIdx;
            result.charIndex = 0;
            m_lastOcrHitBlockIndex = blockIdx;
            return true;
        }

        // Exact hit first.
        for (int c = 0; c < ref.charRects.size(); ++c) {
            if (ref.charRects[c].contains(pagePoint)) {
                result.boxIndex = blockIdx;
                result.charIndex = c;
                m_lastOcrHitBlockIndex = blockIdx;
                return true;
            }
        }

        // In block but between chars: pick the char whose horizontal center
        // is closest to the pointer (matches PDF behavior).
        qreal bestDist = std::numeric_limits<qreal>::max();
        int bestIdx = 0;
        for (int c = 0; c < ref.charRects.size(); ++c) {
            qreal d = qAbs(pagePoint.x() - ref.charRects[c].center().x());
            if (d < bestDist) {
                bestDist = d;
                bestIdx = c;
            }
        }
        result.boxIndex = blockIdx;
        result.charIndex = bestIdx;
        m_lastOcrHitBlockIndex = blockIdx;
        return true;
    };

    // Locality hint: last hit block first, then its neighbors.
    if (m_lastOcrHitBlockIndex >= 0 && m_lastOcrHitBlockIndex < m_ocrBlockCache.size()) {
        if (checkBlock(m_lastOcrHitBlockIndex)) return result;
        if (m_lastOcrHitBlockIndex + 1 < m_ocrBlockCache.size() &&
            checkBlock(m_lastOcrHitBlockIndex + 1)) return result;
        if (m_lastOcrHitBlockIndex > 0 &&
            checkBlock(m_lastOcrHitBlockIndex - 1)) return result;
    }

    for (int i = 0; i < m_ocrBlockCache.size(); ++i) {
        if (m_lastOcrHitBlockIndex >= 0 &&
            (i == m_lastOcrHitBlockIndex ||
             i == m_lastOcrHitBlockIndex + 1 ||
             i == m_lastOcrHitBlockIndex - 1)) {
            continue;
        }
        if (checkBlock(i)) return result;
    }

    return result;
}

void DocumentViewport::setHighlighterColor(const QColor& color)
{
    m_highlighterColor = color;
}

void DocumentViewport::updateHighlighterCursor()
{
    if (m_currentTool == ToolType::Pan) {
        setCursor(m_isPanToolDragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
        return;
    }
    
    if (m_currentTool != ToolType::Highlighter) {
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
    if (!m_document) {
        return;
    }

    const bool ocrMode = (m_highlighterMode == HighlighterMode::Ocr);
    const bool edgeless = m_document->isEdgeless();

    // Shared bail-out: drop any in-flight selection, notify listeners once,
    // and reset m_pointerActive so hover updates still work.
    auto bailOut = [this]() {
        bool hadSelection = m_textSelection.isValid();
        m_textSelection.clear();
        if (hadSelection) emit textSelectionChanged(false);
        m_pointerActive = false;
    };

    // Compute a PageHit in a way that works uniformly for paged and edgeless:
    // - Paged: viewportToPage() gives a real page index + page-local point.
    // - Edgeless: there are no PDF pages, so synthesize pageIndex = 0 with
    //   the document-space coordinate as "page point". (pagePosition(0) is
    //   always (0,0) in edgeless, so this is the natural equivalent.)
    PageHit hit;
    if (edgeless) {
        if (!ocrMode) {
            // PDF text selection is not applicable to edgeless documents.
            bailOut();
            return;
        }
        hit.pageIndex = 0;
        hit.pagePoint = viewportToDocument(pe.viewportPos);
    } else {
        hit = viewportToPage(pe.viewportPos);
        if (!hit.valid()) {
            bailOut();
            return;
        }

        Page* page = m_document->page(hit.pageIndex);
        if (!page) {
            bailOut();
            return;
        }

        // PDF mode requires a PDF background page; OCR mode works on any
        // page (the underlying OCR blocks are attached to the page
        // regardless of background type, and don't require the "show
        // recognized text" overlay).
        if (!ocrMode && page->backgroundType != Page::BackgroundType::PDF) {
            bailOut();
            return;
        }
    }

    // PDF links are a PDF-only concept; skip the lookup when selecting OCR text.
    if (!ocrMode) {
        const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
        if (link && link->type != PdfLinkType::None) {
            activatePdfLink(*link, hit.pageIndex);
            m_pointerActive = false;
            updateHighlighterCursor();
            return;
        }
    }

    // Load the appropriate cache for this page.
    if (ocrMode) {
        loadOcrBlocksForPage(hit.pageIndex);
    } else {
        loadTextBoxesForPage(hit.pageIndex);
    }

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

    // Single click: start text-flow selection at character position.
    CharacterPosition charPos;
    if (ocrMode) {
        charPos = findOcrCharAtPoint(hit.pagePoint);
    } else {
        QPointF pdfPos(hit.pagePoint.x() * PAGE_TO_PDF_SCALE,
                       hit.pagePoint.y() * PAGE_TO_PDF_SCALE);
        charPos = findCharacterAtPoint(pdfPos);
    }

    m_textSelection.clear();
    m_textSelection.source = ocrMode ? TextSelection::Source::Ocr
                                     : TextSelection::Source::Pdf;
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
    if (!m_document) {
        return;
    }

    PageHit hit;
    if (m_document->isEdgeless()) {
        // Edgeless: selection spans the whole document canvas. Synthesize
        // pageIndex=0 with a document-space coordinate.
        hit.pageIndex = 0;
        hit.pagePoint = viewportToDocument(pe.viewportPos);
    } else {
        hit = viewportToPage(pe.viewportPos);
        if (!hit.valid() || hit.pageIndex != m_textSelection.pageIndex) {
            // Moved off the page - for now, just ignore moves outside the page
            return;
        }
    }

    CharacterPosition charPos;
    if (m_textSelection.source == TextSelection::Source::Ocr) {
        charPos = findOcrCharAtPoint(hit.pagePoint);
    } else {
        QPointF pdfPos(hit.pagePoint.x() * PAGE_TO_PDF_SCALE,
                       hit.pagePoint.y() * PAGE_TO_PDF_SCALE);
        charPos = findCharacterAtPoint(pdfPos);
    }

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
        
        // Auto-create strokes if a highlight style is selected
        if (m_autoHighlightStyle != HighlightStyle::None) {
            createHighlightStrokes();
            // Note: createHighlightStrokes() already clears m_textSelection
        }
    }
    
    // Phase D.1: Clear pointer state so hover code works again
    m_pointerActive = false;
    updateHighlighterCursor();
    
    update();
}

// =============================================================================
// Pan Tool Handlers
// =============================================================================

void DocumentViewport::handlePointerPress_Pan(const PointerEvent& pe)
{
    beginPanGesture();
    m_panToolLastPos = pe.viewportPos;
    m_isPanToolDragging = true;
    setCursor(Qt::ClosedHandCursor);
}

void DocumentViewport::handlePointerMove_Pan(const PointerEvent& pe)
{
    QPointF delta = pe.viewportPos - m_panToolLastPos;
    QPointF docDelta(-delta.x() / m_zoomLevel, -delta.y() / m_zoomLevel);
    updatePanGesture(docDelta);
    m_panToolLastPos = pe.viewportPos;
}

void DocumentViewport::handlePointerRelease_Pan(const PointerEvent& pe)
{
    Q_UNUSED(pe);
    endPanGesture();
    m_isPanToolDragging = false;
    setCursor(Qt::OpenHandCursor);
    
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;
}

// =============================================================================

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
    if (m_textSelection.source == TextSelection::Source::Ocr) {
        updateSelectedTextAndRects_Ocr();
    } else {
        updateSelectedTextAndRects_Pdf();
    }
}

void DocumentViewport::updateSelectedTextAndRects_Pdf()
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
        int endChar = (boxIdx == toBox) ? toChar : static_cast<int>(box.text.length() - 1);
        
        // Clamp to valid range (now safe since we checked for empty text)
        int maxCharIdx = static_cast<int>(box.text.length()) - 1;
        startChar = qBound(0, startChar, maxCharIdx);
        endChar = qBound(0, endChar, maxCharIdx);
        
        if (startChar > endChar) {
            continue;  // Invalid range
        }
        
        // Extract text for this range
        QString boxText = box.text.mid(startChar, endChar - startChar + 1);
        if (!selectedText.isEmpty() && !boxText.isEmpty()) {
            // CJK-aware separator: MuPdfProvider emits one PdfTextBox per
            // CJK glyph, so a hard-coded space would turn copied "中文"
            // into "中 文" in the clipboard. Mirrors the same predicate used
            // by PdfSearchEngine::searchPage and the OCR text-join below.
            QChar prevTrailing = selectedText.at(selectedText.length() - 1);
            QChar nextLeading  = boxText.at(0);
            if (!isCjkLikeChar(prevTrailing) && !isCjkLikeChar(nextLeading)) {
                selectedText += QLatin1Char(' ');
            }
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

void DocumentViewport::updateSelectedTextAndRects_Ocr()
{
    m_textSelection.selectedText.clear();
    m_textSelection.highlightRects.clear();

    if (m_ocrBlockCache.isEmpty() ||
        m_textSelection.startBoxIndex < 0 ||
        m_textSelection.endBoxIndex < 0) {
        return;
    }

    // Normalize selection direction.
    int fromBlock, fromChar, toBlock, toChar;
    if (m_textSelection.startBoxIndex < m_textSelection.endBoxIndex ||
        (m_textSelection.startBoxIndex == m_textSelection.endBoxIndex &&
         m_textSelection.startCharIndex <= m_textSelection.endCharIndex)) {
        fromBlock = m_textSelection.startBoxIndex;
        fromChar  = m_textSelection.startCharIndex;
        toBlock   = m_textSelection.endBoxIndex;
        toChar    = m_textSelection.endCharIndex;
    } else {
        fromBlock = m_textSelection.endBoxIndex;
        fromChar  = m_textSelection.endCharIndex;
        toBlock   = m_textSelection.startBoxIndex;
        toChar    = m_textSelection.startCharIndex;
    }

    // Line-grouping threshold in PAGE coordinates (px). OCR blocks are usually
    // short (single word / phrase), so we regroup consecutive blocks within
    // the same horizontal band into one highlight rect.
    const qreal lineThreshold = 8.0;

    QString selectedText;
    qreal currentLineY = -1;
    QRectF currentLineRect;
    QChar prevTrailingChar;
    bool hasPrevChar = false;

    for (int bIdx = fromBlock; bIdx <= toBlock && bIdx < m_ocrBlockCache.size(); ++bIdx) {
        const OcrBlockRef& ref = m_ocrBlockCache[bIdx];
        if (ref.text.isEmpty()) {
            continue;
        }

        const int maxCharIdx = ref.text.length() - 1;
        int startChar = (bIdx == fromBlock) ? fromChar : 0;
        int endChar   = (bIdx == toBlock)   ? toChar   : maxCharIdx;
        startChar = qBound(0, startChar, maxCharIdx);
        endChar   = qBound(0, endChar,   maxCharIdx);
        if (startChar > endChar) {
            continue;
        }

        // Text joining with CJK-aware spacing between blocks.
        QString blockText = ref.text.mid(startChar, endChar - startChar + 1);
        if (!blockText.isEmpty()) {
            if (hasPrevChar) {
                QChar leadChar = blockText.at(0);
                if (!isCjkLikeChar(prevTrailingChar) && !isCjkLikeChar(leadChar)) {
                    selectedText += QLatin1Char(' ');
                }
            }
            selectedText += blockText;
            prevTrailingChar = blockText.at(blockText.length() - 1);
            hasPrevChar = true;
        }

        // Build the per-character rect for this block's selection range.
        QRectF charRect;
        if (!ref.charRects.isEmpty()) {
            for (int c = startChar; c <= endChar && c < ref.charRects.size(); ++c) {
                if (charRect.isNull()) charRect = ref.charRects[c];
                else                   charRect = charRect.united(ref.charRects[c]);
            }
        } else {
            charRect = ref.blockRect;
        }
        if (charRect.isNull()) {
            continue;
        }

        qreal boxCenterY = charRect.center().y();
        if (currentLineY < 0 || qAbs(boxCenterY - currentLineY) > lineThreshold) {
            if (!currentLineRect.isNull()) {
                m_textSelection.highlightRects.append(currentLineRect);
            }
            currentLineRect = charRect;
            currentLineY = boxCenterY;
        } else {
            currentLineRect = currentLineRect.united(charRect);
        }
    }

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
    const bool ocrMode = (m_highlighterMode == HighlighterMode::Ocr);

    if (ocrMode) {
        loadOcrBlocksForPage(pageIndex);
        for (int bIdx = 0; bIdx < m_ocrBlockCache.size(); ++bIdx) {
            const OcrBlockRef& ref = m_ocrBlockCache[bIdx];
            if (ref.text.isEmpty()) continue;
            if (!ref.blockRect.contains(pagePos)) continue;

            m_textSelection.clear();
            m_textSelection.source = TextSelection::Source::Ocr;
            m_textSelection.pageIndex = pageIndex;
            m_textSelection.startBoxIndex = bIdx;
            m_textSelection.startCharIndex = 0;
            m_textSelection.endBoxIndex = bIdx;
            m_textSelection.endCharIndex = ref.text.length() - 1;

            updateSelectedTextAndRects();
            finalizeTextSelection();
            update();
            return;
        }
        return;
    }

    // PDF mode (original implementation)
    loadTextBoxesForPage(pageIndex);
    QPointF pdfPos(pagePos.x() * PAGE_TO_PDF_SCALE, pagePos.y() * PAGE_TO_PDF_SCALE);
    
    for (int boxIdx = 0; boxIdx < m_textBoxCache.size(); ++boxIdx) {
        const PdfTextBox& box = m_textBoxCache[boxIdx];
        if (box.boundingBox.contains(pdfPos)) {
            if (box.text.isEmpty()) {
                continue;
            }
            
            m_textSelection.clear();
            m_textSelection.source = TextSelection::Source::Pdf;
            m_textSelection.pageIndex = pageIndex;
            
            m_textSelection.startBoxIndex = boxIdx;
            m_textSelection.startCharIndex = 0;
            m_textSelection.endBoxIndex = boxIdx;
            m_textSelection.endCharIndex = static_cast<int>(box.text.length()) - 1;
            
            updateSelectedTextAndRects();
            finalizeTextSelection();
            update();
            return;
        }
    }
}

void DocumentViewport::selectLineAtPoint(const QPointF& pagePos, int pageIndex)
{
    const bool ocrMode = (m_highlighterMode == HighlighterMode::Ocr);

    if (ocrMode) {
        loadOcrBlocksForPage(pageIndex);
        int clickedIdx = -1;
        for (int i = 0; i < m_ocrBlockCache.size(); ++i) {
            if (m_ocrBlockCache[i].blockRect.contains(pagePos)) {
                clickedIdx = i;
                break;
            }
        }
        if (clickedIdx < 0) {
            return;
        }

        // Page-coord threshold ≈ slightly larger than PDF one because OCR
        // block Y-centers can be noisier than embedded PDF text.
        const qreal lineThreshold = 10.0;
        qreal targetY = m_ocrBlockCache[clickedIdx].blockRect.center().y();

        int firstOnLine = clickedIdx;
        int lastOnLine  = clickedIdx;
        for (int i = 0; i < m_ocrBlockCache.size(); ++i) {
            qreal y = m_ocrBlockCache[i].blockRect.center().y();
            if (qAbs(y - targetY) <= lineThreshold) {
                if (i < firstOnLine) firstOnLine = i;
                if (i > lastOnLine)  lastOnLine  = i;
            }
        }

        m_textSelection.clear();
        m_textSelection.source = TextSelection::Source::Ocr;
        m_textSelection.pageIndex = pageIndex;
        m_textSelection.startBoxIndex = firstOnLine;
        m_textSelection.startCharIndex = 0;
        m_textSelection.endBoxIndex = lastOnLine;

        const OcrBlockRef& lastRef = m_ocrBlockCache[lastOnLine];
        m_textSelection.endCharIndex = lastRef.text.isEmpty() ? 0 : lastRef.text.length() - 1;

        updateSelectedTextAndRects();
        finalizeTextSelection();
        update();
        return;
    }

    // PDF mode (original implementation)
    loadTextBoxesForPage(pageIndex);
    QPointF pdfPos(pagePos.x() * PAGE_TO_PDF_SCALE, pagePos.y() * PAGE_TO_PDF_SCALE);
    
    int clickedBoxIdx = -1;
    for (int i = 0; i < m_textBoxCache.size(); ++i) {
        if (m_textBoxCache[i].boundingBox.contains(pdfPos)) {
            clickedBoxIdx = i;
            break;
        }
    }
    
    if (clickedBoxIdx < 0) {
        return;
    }
    
    const qreal lineThreshold = 5.0;  // PDF points
    qreal targetY = m_textBoxCache[clickedBoxIdx].boundingBox.center().y();
    
    int firstBoxOnLine = clickedBoxIdx;
    int lastBoxOnLine = clickedBoxIdx;
    
    for (int i = 0; i < m_textBoxCache.size(); ++i) {
        qreal boxY = m_textBoxCache[i].boundingBox.center().y();
        if (qAbs(boxY - targetY) <= lineThreshold) {
            if (i < firstBoxOnLine) firstBoxOnLine = i;
            if (i > lastBoxOnLine) lastBoxOnLine = i;
        }
    }
    
    m_textSelection.clear();
    m_textSelection.source = TextSelection::Source::Pdf;
    m_textSelection.pageIndex = pageIndex;
    m_textSelection.startBoxIndex = firstBoxOnLine;
    m_textSelection.startCharIndex = 0;
    m_textSelection.endBoxIndex = lastBoxOnLine;
    
    const PdfTextBox& lastBox = m_textBoxCache[lastBoxOnLine];
    m_textSelection.endCharIndex = lastBox.text.isEmpty() ? 0 : static_cast<int>(lastBox.text.length() - 1);
    
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

    // Edgeless selections use document-space coordinates and span tiles, so
    // skip the page-index match test. Paged rendering only draws the overlay
    // on the page that actually has the selection.
    const bool edgelessRender = (pageIndex < 0);
    if (!edgelessRender && m_textSelection.pageIndex != pageIndex) {
        return;
    }
    
    painter.save();
    
    // Selection color (Windows selection blue with transparency)
    QColor selectionColor(0, 120, 215, 100);
    painter.setBrush(selectionColor);
    painter.setPen(Qt::NoPen);
    
    // Highlight rectangles are stored in PDF coords for PDF-source selections
    // (need scaling) and in page coords for OCR-source selections (no scaling).
    const bool ocrSelection = (m_textSelection.source == TextSelection::Source::Ocr);
    for (const QRectF& rect : m_textSelection.highlightRects) {
        QRectF pageRect;
        if (ocrSelection) {
            pageRect = rect;
        } else {
            pageRect = QRectF(
                rect.x() * PDF_TO_PAGE_SCALE,
                rect.y() * PDF_TO_PAGE_SCALE,
                rect.width() * PDF_TO_PAGE_SCALE,
                rect.height() * PDF_TO_PAGE_SCALE
            );
        }
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

        QRectF pageRect;
        if (match.source == PdfSearchMatch::PdfText) {
            // PDF coords (72 DPI) → page coords (96 DPI)
            const QRectF& r = match.boundingRect;
            pageRect = QRectF(
                r.x() * PDF_TO_PAGE_SCALE,
                r.y() * PDF_TO_PAGE_SCALE,
                r.width() * PDF_TO_PAGE_SCALE,
                r.height() * PDF_TO_PAGE_SCALE);
        } else {
            // OcrText: already in page coords (96 DPI)
            pageRect = match.boundingRect;
        }
        
        painter.drawRect(pageRect);
    }
    
    painter.restore();
}

void DocumentViewport::renderSearchMatchesOverlayEdgeless(QPainter& painter)
{
    if (m_searchMatches.isEmpty()) return;

    painter.save();
    painter.setPen(Qt::NoPen);

    int tileSize = Document::EDGELESS_TILE_SIZE;

    for (int i = 0; i < m_searchMatches.size(); ++i) {
        const PdfSearchMatch& match = m_searchMatches[i];
        if (match.source != PdfSearchMatch::OcrTextTile
            && match.source != PdfSearchMatch::TextBoxObjTile) continue;

        QColor fillColor = (i == m_currentSearchMatchIndex) 
            ? m_searchHighlightCurrent 
            : m_searchHighlightOther;
        painter.setBrush(fillColor);

        // Convert tile-local rect to document coords
        QRectF docRect(
            static_cast<qreal>(match.tileX) * tileSize + match.boundingRect.x(),
            static_cast<qreal>(match.tileY) * tileSize + match.boundingRect.y(),
            match.boundingRect.width(),
            match.boundingRect.height());

        painter.drawRect(docRect);
    }

    painter.restore();
}

VectorStroke DocumentViewport::createHighlightStroke(const QRectF& rect,
                                                     const QColor& color,
                                                     HighlightStyle style) const
{
    VectorStroke stroke;
    stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    stroke.color = color;

    // Geometry depends on style:
    //  - Cover:     horizontal line through the center, thickness = rect height
    //               (original "marker covers the text" behavior).
    //  - Underline: thin horizontal line along the bottom edge.
    //  - DottedUnderline is routed through createDottedUnderlineStrokes() and
    //    is never passed to this helper; we treat it as Underline just so a
    //    stray caller still produces something visible rather than a 0-thick
    //    stroke.
    qreal y = rect.center().y();
    qreal thickness = rect.height();
    if (style == HighlightStyle::Underline || style == HighlightStyle::DottedUnderline) {
        thickness = qMax(qreal(1.5), rect.height() * qreal(0.10));
        y = rect.bottom() - thickness * qreal(0.5);
    }
    stroke.baseThickness = thickness;

    StrokePoint startPoint;
    startPoint.pos = QPointF(rect.left(), y);
    startPoint.pressure = 1.0;

    StrokePoint endPoint;
    endPoint.pos = QPointF(rect.right(), y);
    endPoint.pressure = 1.0;

    stroke.points.append(startPoint);
    stroke.points.append(endPoint);

    stroke.updateBoundingBox();
    return stroke;
}

QVector<VectorStroke> DocumentViewport::createDottedUnderlineStrokes(const QRectF& rect,
                                                                      const QColor& color) const
{
    QVector<VectorStroke> dots;

    // Dot thickness scales with text height; center-to-center spacing is 3x
    // thickness (dot + two-dot gap). Line sits on the bottom edge of the rect.
    const qreal thickness = qMax(qreal(1.5), rect.height() * qreal(0.10));
    const qreal step      = thickness * qreal(3.0);
    const qreal y         = rect.bottom() - thickness * qreal(0.5);
    const qreal firstX    = rect.left()  + thickness * qreal(0.5);
    const qreal lastX     = rect.right() - thickness * qreal(0.5);
    if (lastX < firstX || step <= 0.0) {
        return dots;  // Rect too narrow for even one dot.
    }

    dots.reserve(static_cast<int>((lastX - firstX) / step) + 1);
    for (qreal x = firstX; x <= lastX; x += step) {
        VectorStroke dot;
        dot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        dot.color = color;
        dot.baseThickness = thickness;
        StrokePoint p;
        p.pos = QPointF(x, y);
        p.pressure = 1.0;
        dot.points.append(p);
        dot.updateBoundingBox();
        dots.append(dot);
    }
    return dots;
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

    // Nothing to do if caller reached here with None (defensive; the release
    // handler guards this, but we stay safe for future call sites).
    if (m_autoHighlightStyle == HighlightStyle::None) {
        m_textSelection.clear();
        return createdIds;
    }

    const bool ocrSelection = (m_textSelection.source == TextSelection::Source::Ocr);
    const bool edgeless     = m_document->isEdgeless();
    const bool dotted       = (m_autoHighlightStyle == HighlightStyle::DottedUnderline);
    const int pageIndex     = m_textSelection.pageIndex;
    const HighlightStyle style = m_autoHighlightStyle;

    // ---------- Edgeless path: strokes are split across tiles ----------
    if (edgeless) {
        // Only OCR selections reach here in edgeless (PDF mode is disabled).
        if (!ocrSelection) {
            m_textSelection.clear();
            return createdIds;
        }

        // Emit one UndoAction per highlight rect (line) so the undo
        // granularity matches the paged path. A single logical highlight
        // stroke (or for Dotted, its whole row of dots) may be split across
        // multiple tiles; those segments are grouped into the same UndoAction
        // so they undo together with a single Ctrl+Z.
        for (const QRectF& srcRect : m_textSelection.highlightRects) {
            if (srcRect.width() < 0.1 || srcRect.height() < 0.1) continue;

            // OCR selection rects are already in document-space for edgeless
            // (see loadOcrBlocksForPage edgeless branch).
            QVector<VectorStroke> strokesForLine;
            if (dotted) {
                strokesForLine = createDottedUnderlineStrokes(srcRect, m_highlighterColor);
            } else {
                strokesForLine.append(createHighlightStroke(srcRect, m_highlighterColor, style));
            }
            if (strokesForLine.isEmpty()) continue;

            UndoAction undoAction;
            undoAction.type = UndoAction::AddStroke;
            undoAction.layerIndex = m_edgelessActiveLayerIndex;

            for (const auto& s : strokesForLine) {
                auto addedSegments = addStrokeToEdgelessTiles(s, m_edgelessActiveLayerIndex);
                if (addedSegments.isEmpty()) continue;
                undoAction.segments.reserve(undoAction.segments.size() + addedSegments.size());
                for (const auto& pair : addedSegments) {
                    UndoAction::StrokeSegment seg;
                    seg.tileCoord = pair.first;
                    seg.stroke    = pair.second;
                    undoAction.segments.append(seg);
                    createdIds.append(pair.second.id);
                }
            }

            if (!undoAction.segments.isEmpty())
                pushUndoAction(undoAction);
        }

        // Create LinkObject on the tile containing the first highlight rect.
        if (!createdIds.isEmpty()) {
            createLinkObjectForHighlight(pageIndex);
        }

        m_textSelection.clear();

        if (!createdIds.isEmpty()) {
            emit documentModified();
            update();
        }
        return createdIds;
    }

    // ---------- Paged path ----------
    Page* page = m_document->page(pageIndex);
    if (!page) {
        return createdIds;
    }

    // Get the active layer for this page
    VectorLayer* layer = page->activeLayer();
    if (!layer) {
        return createdIds;
    }

    // Convert each highlight rect to one (Cover/Underline) or many (Dotted)
    // strokes. PDF-source rects are in PDF coords (72 DPI) and need scaling
    // to page coords (96 DPI). OCR-source rects are already in page coords.
    for (const QRectF& srcRect : m_textSelection.highlightRects) {
        if (srcRect.width() < 0.1 || srcRect.height() < 0.1) {
            continue;
        }

        QRectF pageRect;
        if (ocrSelection) {
            pageRect = srcRect;
        } else {
            pageRect = QRectF(
                srcRect.x() * PDF_TO_PAGE_SCALE,
                srcRect.y() * PDF_TO_PAGE_SCALE,
                srcRect.width()  * PDF_TO_PAGE_SCALE,
                srcRect.height() * PDF_TO_PAGE_SCALE
            );
        }

        if (dotted) {
            QVector<VectorStroke> dots = createDottedUnderlineStrokes(pageRect, m_highlighterColor);
            if (dots.isEmpty()) continue;

            // Append dots to the layer and push a single grouped undo per line
            // so Ctrl+Z removes the whole dotted row at once.
            for (const auto& d : dots) {
                layer->addStroke(d);
                createdIds.append(d.id);
            }
            pushPageStrokesUndo(pageIndex, UndoAction::AddStroke, dots, page->activeLayerIndex);
        } else {
            VectorStroke stroke = createHighlightStroke(pageRect, m_highlighterColor, style);
            layer->addStroke(stroke);
            pushPageStrokeUndo(pageIndex, UndoAction::AddStroke, stroke, page->activeLayerIndex);
            createdIds.append(stroke.id);
        }
    }

    // Invalidate stroke cache for this page
    layer->invalidateStrokeCache();

    // Mark page dirty for lazy save
    if (!createdIds.isEmpty()) {
        m_document->markPageDirty(pageIndex);
    }

    // Create LinkObject alongside highlight strokes
    if (!createdIds.isEmpty() && !m_textSelection.highlightRects.isEmpty()) {
        createLinkObjectForHighlight(pageIndex);
    }

    // Clear the text selection
    m_textSelection.clear();

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

void DocumentViewport::addPointToStroke(const QPointF& pagePos, qreal pressure, qint64 timestamp)
{
    // ========== OPTIMIZATION: Point Decimation ==========
    // At 360Hz, consecutive points are often <1 pixel apart.
    // Skip points that are too close to reduce memory and rendering work.
    // This typically reduces point count by 50-70% with no visible quality loss.
    // The threshold is zoom-aware: MIN_SCREEN_DISTANCE screen pixels mapped to
    // document space, so decimation granularity stays constant on screen.
    
    // Apply the pen preset's min-width floor here as well so callers that
    // forward raw pressure (e.g. paged-mode startStroke) inherit the floor
    // without every call site having to remember.  For markers this is a
    // no-op since useFixedPressure already clamps pressure to 1.0 upstream.
    const qreal flooredPressure = applyPenPressureFloor(pressure);

    if (!m_currentStroke.points.isEmpty()) {
        const QPointF& lastPos = m_currentStroke.points.last().pos;
        qreal dx = pagePos.x() - lastPos.x();
        qreal dy = pagePos.y() - lastPos.y();
        qreal distSq = dx * dx + dy * dy;

        qreal docThreshold = MIN_SCREEN_DISTANCE / m_zoomLevel;
        if (distSq < docThreshold * docThreshold) {
            // Point too close - but update pressure peak if higher.
            // Compare the *floored* pressure so the stored peak never slips
            // below the preset's min-width floor.
            if (flooredPressure > m_currentStroke.points.last().pressure) {
                m_currentStroke.points.last().pressure = flooredPressure;
            }
            return;  // Skip this point
        }
    }

    StrokePoint pt;
    pt.pos = pagePos;
    pt.pressure = flooredPressure;
    pt.timestamp = timestamp;
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
    
    // Reuse existing pixmap if size and DPR match (avoids expensive reallocation).
    // At 1280x800 this is a 4MB alloc+free cycle; at 4K it's ~33MB.
    // On memory-bandwidth-limited devices (e.g. Cortex-A9), avoiding the
    // reallocation saves significant time at stroke start.
    if (m_currentStrokeCache.isNull()
        || m_currentStrokeCache.size() != physicalSize
        || !qFuzzyCompare(m_currentStrokeCache.devicePixelRatio(), dpr)) {
        m_currentStrokeCache = QPixmap(physicalSize);
        m_currentStrokeCache.setDevicePixelRatio(dpr);
    }
    m_currentStrokeCache.fill(Qt::transparent);
    m_lastRenderedPointIndex = 0;
    
    // Track the transform state when cache was created
    m_cacheZoom = m_zoomLevel;
    m_cachePan = m_panOffset;
}

QRect DocumentViewport::currentStrokeTailRect(int fromIndex, const QTransform& toCache) const
{
    const int n = static_cast<int>(m_currentStroke.points.size());
    if (fromIndex < 0 || fromIndex >= n) {
        return QRect();
    }
    
    qreal minX = m_currentStroke.points[fromIndex].pos.x();
    qreal maxX = minX;
    qreal minY = m_currentStroke.points[fromIndex].pos.y();
    qreal maxY = minY;
    for (int i = fromIndex + 1; i < n; ++i) {
        const QPointF& pos = m_currentStroke.points[i].pos;
        minX = qMin(minX, pos.x());
        maxX = qMax(maxX, pos.x());
        minY = qMin(minY, pos.y());
        maxY = qMax(maxY, pos.y());
    }
    
    // A full thickness of padding: half covers the outline's own half-width at
    // maximum pressure, the rest absorbs the Catmull-Rom curve bowing outside the
    // control points it was built from.
    const qreal pad = m_currentStroke.baseThickness + 2.0;
    const QRectF bounds(minX - pad, minY - pad,
                        (maxX - minX) + pad * 2.0, (maxY - minY) + pad * 2.0);
    
    // Two more pixels after mapping for the antialiased fringe.
    return toCache.mapRect(bounds).toAlignedRect().adjusted(-2, -2, 2, 2)
        .intersected(QRect(0, 0, width(), height()));
}

QVector<QPair<int, int>> DocumentViewport::currentStrokeRangesTouching(
    const QRect& cacheRect, const QTransform& toCache) const
{
    QVector<QPair<int, int>> runs;
    const int n = static_cast<int>(m_currentStroke.points.size());
    if (n < 2) {
        return runs;
    }
    
    // Same padding the target region was built with, so a segment is only
    // discarded when its outline cannot reach the region.
    const qreal pad = m_currentStroke.baseThickness + 2.0;
    const QRectF target(cacheRect);
    
    // Testing segments rather than points keeps a long segment that merely
    // passes through the region from being missed.
    int runStart = -1;
    for (int i = 0; i + 1 < n; ++i) {
        const QPointF& a = m_currentStroke.points[i].pos;
        const QPointF& b = m_currentStroke.points[i + 1].pos;
        const QRectF box(QPointF(qMin(a.x(), b.x()) - pad, qMin(a.y(), b.y()) - pad),
                         QPointF(qMax(a.x(), b.x()) + pad, qMax(a.y(), b.y()) + pad));
        
        if (toCache.mapRect(box).intersects(target)) {
            if (runStart < 0) {
                runStart = i;
            }
        } else if (runStart >= 0) {
            runs.append({runStart, i});
            runStart = -1;
        }
    }
    if (runStart >= 0) {
        runs.append({runStart, n - 1});
    }
    
    // Widen each run with curve context and merge the runs that meet, so every
    // repainted segment sees the neighbours a full-stroke render would give it.
    QVector<QPair<int, int>> merged;
    merged.reserve(runs.size());
    for (const QPair<int, int>& run : runs) {
        const int first = qMax(0, run.first - STROKE_TAIL_CONTEXT_POINTS);
        const int last = qMin(n - 1, run.second + STROKE_TAIL_CONTEXT_POINTS);
        if (!merged.isEmpty() && first <= merged.last().second + 1) {
            merged.last().second = qMax(merged.last().second, last);
        } else {
            merged.append({first, last});
        }
    }
    return merged;
}

void DocumentViewport::renderCurrentStrokeIncremental(QPainter& painter)
{
    // ========== In-Progress Stroke Rendering ==========
    // Renders the current stroke to m_currentStrokeCache using the same
    // VectorLayer::renderStroke() path as finalized strokes, giving it
    // Catmull-Rom smoothed curves. New points rewrite only the tail of the
    // cache (tracked via m_lastRenderedPointIndex); repaints that added no
    // points reuse it as-is.
    
    const int n = static_cast<int>(m_currentStroke.points.size());
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
    
    // ========== Sub-Pixel Grid Alignment ==========
    // The layer zoom cache rasterizes strokes in page-local coordinates where
    // the page origin is always at physical pixel (0,0). This live cache
    // rasterizes in viewport coordinates where the page origin lands at
    // physical pixel (pagePos - pan) * zoom * dpr, which is generally
    // fractional. The resulting anti-aliasing mismatch causes a visible
    // sub-pixel shift on pen-up. Fix: snap the page/tile origin to the
    // nearest integer physical pixel so both caches produce identical
    // anti-aliasing for every polygon vertex.
    QPointF snapOrigin;
    if (isEdgeless) {
        int tileSize = Document::EDGELESS_TILE_SIZE;
        int tx = static_cast<int>(std::floor(m_currentStroke.points[0].pos.x() / tileSize));
        int ty = static_cast<int>(std::floor(m_currentStroke.points[0].pos.y() / tileSize));
        snapOrigin = QPointF(tx * tileSize, ty * tileSize);
    } else {
        snapOrigin = pagePosition(m_activeDrawingPage);
    }
    QPointF originPhysical = (snapOrigin - m_panOffset) * m_zoomLevel * dpr;
    QPointF snapCorrection(std::round(originPhysical.x()) - originPhysical.x(),
                           std::round(originPhysical.y()) - originPhysical.y());
    
    // Pre-compute the snap translate in logical pixels (reused by cache painter and end cap)
    qreal snapTxLogical = snapCorrection.x() / dpr;
    qreal snapTyLogical = snapCorrection.y() / dpr;
    
    // ========== Semi-Transparent Stroke Rendering ==========
    // For strokes with alpha < 255 (e.g., marker at 50% opacity), we draw
    // with FULL OPACITY to the cache, then blit with the desired opacity.
    // This prevents alpha compounding at segment joints / cap overlaps.
    
    int strokeAlpha = m_currentStroke.color.alpha();
    bool hasSemiTransparency = (strokeAlpha < 255);
    
    // Update the cache when new points arrive, rewriting only the tail whose
    // shape can still change and leaving the settled prefix in place. Redrawing
    // the whole stroke per point costs O(n) each frame and O(n^2) across the
    // stroke, which is what drags a long stroke below 1 fps on slower tablets.
    if (n > m_lastRenderedPointIndex && n >= 2) {
        // Same mapping the cache was rasterized with, kept as a QTransform so the
        // tail bounds can be carried into cache pixels by exactly that mapping.
        QTransform toCache;
        toCache.translate(snapTxLogical, snapTyLogical);
        toCache.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        toCache.scale(m_zoomLevel, m_zoomLevel);
        if (!isEdgeless) {
            toCache.translate(snapOrigin.x(), snapOrigin.y());
        }
        
        // Everything drawn into the cache is fully opaque; the stroke's own alpha
        // is applied once at blit time, so overlapping ranges, caps and joints
        // cannot compound and repainting a region twice is harmless.
        auto renderRange = [&](QPainter& cachePainter, int first, int last) {
            // Copying the stroke shares its point vector, so slicing a range out
            // of the copy costs only that range.
            VectorStroke segment = m_currentStroke;
            segment.points = m_currentStroke.points.mid(first, last - first + 1);
            if (hasSemiTransparency) {
                segment.color.setAlpha(255);
            }
            VectorLayer::renderStroke(cachePainter, segment);
        };
        
        // A reset cache holds nothing, so there is no settled content to preserve.
        if (m_lastRenderedPointIndex <= 0) {
            m_currentStrokeCache.fill(Qt::transparent);
            
            QPainter cachePainter(&m_currentStrokeCache);
            cachePainter.setRenderHint(QPainter::Antialiasing, true);
            cachePainter.setWorldTransform(toCache);
            renderRange(cachePainter, 0, n - 1);
        } else {
            // Whatever the previous render left volatile has to be redone, along
            // with every point added since.
            const int redrawFrom =
                qBound(0, m_lastRenderedPointIndex - 1 - STROKE_TAIL_VOLATILE_POINTS, n - 1);
            const QRect tailRect = currentStrokeTailRect(redrawFrom, toCache);
            
            // An empty rect means the new points are off-screen; the cache only
            // spans the viewport, so there is nothing to repaint.
            if (!tailRect.isEmpty()) {
                // Clearing the region also removes any older part of the stroke
                // that crosses it, so the repaint covers every range reaching in,
                // not just the tail.
                const QVector<QPair<int, int>> ranges =
                    currentStrokeRangesTouching(tailRect, toCache);
                
                QPainter cachePainter(&m_currentStrokeCache);
                cachePainter.setCompositionMode(QPainter::CompositionMode_Clear);
                cachePainter.fillRect(tailRect, Qt::transparent);
                cachePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                
                // A hard-edged clip keeps settled pixels outside the region
                // untouched, and the context points make the geometry inside it
                // identical to a full render, so the two meet without a seam.
                cachePainter.setClipRect(tailRect);
                cachePainter.setRenderHint(QPainter::Antialiasing, true);
                cachePainter.setWorldTransform(toCache);
                
                for (const QPair<int, int>& range : ranges) {
                    renderRange(cachePainter, range.first, range.second);
                }
            }
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
        painter.save();
        
        // Apply same sub-pixel snap so the end cap aligns with the cached stroke body
        painter.translate(snapTxLogical, snapTyLogical);
        
        painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
        painter.scale(m_zoomLevel, m_zoomLevel);
        
        // For paged mode, translate to page position
        // For edgeless, stroke points are already in document coords
        if (!isEdgeless) {
            painter.translate(snapOrigin);
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
    
    // Stroke cache is incrementally patched by removeStroke()
    
    // Mark page dirty for lazy save (BUG FIX: was missing)
    if (!removedStrokes.isEmpty()) {
        m_document->markPageDirty(pe.pageHit.pageIndex);
    }
    
    // Push undo action
    if (removedStrokes.size() == 1) {
        pushPageStrokeUndo(pe.pageHit.pageIndex, UndoAction::RemoveStroke, removedStrokes[0], page->activeLayerIndex);
    } else if (removedStrokes.size() > 1) {
        pushPageStrokesUndo(pe.pageHit.pageIndex, UndoAction::RemoveMultiple, removedStrokes, page->activeLayerIndex);
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
    
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    UndoAction undoAction;
    undoAction.type = UndoAction::RemoveStroke;
    undoAction.layerIndex = m_edgelessActiveLayerIndex;

    // Scan only the tiles the eraser disc can actually reach. A fixed 3x3
    // neighbourhood scanned nine tiles' worth of strokes on every pointer move,
    // and at a 1024-unit tile size an ordinary eraser touches one.
    const qreal reach = m_eraserSize + EDGELESS_STROKE_MARGIN;
    const int minTx = static_cast<int>(std::floor((docPt.x() - reach) / tileSize));
    const int maxTx = static_cast<int>(std::floor((docPt.x() + reach) / tileSize));
    const int minTy = static_cast<int>(std::floor((docPt.y() - reach) / tileSize));
    const int maxTy = static_cast<int>(std::floor((docPt.y() + reach) / tileSize));

    for (int tx = minTx; tx <= maxTx; ++tx) {
        for (int ty = minTy; ty <= maxTy; ++ty) {
            Page* tile = m_document->getTile(tx, ty);
            if (!tile) continue;
            if (m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer || layer->locked) continue;

            QPointF tileOrigin(tx * tileSize, ty * tileSize);
            QPointF localPt = docPt - tileOrigin;
            QVector<QString> hitIds = layer->strokesAtPoint(localPt, m_eraserSize);
            if (hitIds.isEmpty()) continue;

            for (const QString& id : hitIds) {
                for (const VectorStroke& stroke : layer->strokes()) {
                    if (stroke.id == id) {
                        UndoAction::StrokeSegment seg;
                        seg.tileCoord = {tx, ty};
                        seg.stroke = stroke;
                        undoAction.segments.append(seg);
                        break;
                    }
                }
            }
            for (const QString& id : hitIds)
                layer->removeStroke(id);
            m_document->markTileDirty({tx, ty});
            m_document->removeTileIfEmpty(tx, ty);
        }
    }

    if (!undoAction.segments.isEmpty()) {
        markOcrDirtyTiles(undoAction);
        pushUndoAction(undoAction);
        emit strokesChanged();
        emit documentModified();
        
        // Dirty region update - use elliptical region to match circular eraser
        // Use toAlignedRect() to properly round floating-point to integer coords
        qreal eraserRadius = m_eraserSize * m_zoomLevel + 10;  // Add padding for stroke edges
        QRectF dirtyRectF(viewportPos.x() - eraserRadius, viewportPos.y() - eraserRadius,
                          eraserRadius * 2, eraserRadius * 2);
        update(QRegion(dirtyRectF.toAlignedRect(), QRegion::Ellipse));
    }
}

void DocumentViewport::fillBackgroundAround(QPainter& painter, const QRectF& coveredLogical)
{
    const QRect vp = rect();
    
    // Round inward: only pixels certain to be overdrawn may be skipped, so a
    // fractional edge ends up in a band instead of becoming an unfilled seam.
    const int left = static_cast<int>(std::ceil(coveredLogical.left()));
    const int top = static_cast<int>(std::ceil(coveredLogical.top()));
    const int right = static_cast<int>(std::floor(coveredLogical.left() + coveredLogical.width()));
    const int bottom = static_cast<int>(std::floor(coveredLogical.top() + coveredLogical.height()));
    const QRect covered = QRect(left, top, std::max(0, right - left),
                                std::max(0, bottom - top)).intersected(vp);
    
    if (covered.isEmpty()) {
        // Panned further than a full viewport: nothing survives, clear it all.
        painter.fillRect(vp, m_backgroundColor);
        return;
    }
    
    // Top and bottom bands span the full width; the side bands cover only the
    // remaining vertical extent. Together with `covered` these tile the
    // viewport exactly - no overlap, no gap.
    if (covered.top() > vp.top()) {
        painter.fillRect(QRect(vp.left(), vp.top(), vp.width(), covered.top() - vp.top()),
                         m_backgroundColor);
    }
    if (covered.bottom() < vp.bottom()) {
        painter.fillRect(QRect(vp.left(), covered.bottom() + 1, vp.width(),
                               vp.bottom() - covered.bottom()),
                         m_backgroundColor);
    }
    if (covered.left() > vp.left()) {
        painter.fillRect(QRect(vp.left(), covered.top(), covered.left() - vp.left(),
                               covered.height()),
                         m_backgroundColor);
    }
    if (covered.right() < vp.right()) {
        painter.fillRect(QRect(covered.right() + 1, covered.top(),
                               vp.right() - covered.right(), covered.height()),
                         m_backgroundColor);
    }
}

void DocumentViewport::drawEraserCursor(QPainter& painter)
{
    // Show eraser cursor for: selected eraser tool OR active hardware eraser,
    // but only in Normal mode (Lasso mode uses the lasso path as feedback)
    bool showCursor = (m_currentTool == ToolType::Eraser || m_hardwareEraserActive)
                      && m_eraserMode == EraserMode::Normal;
    
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

void DocumentViewport::finalizeEraserLasso()
{
    if (!m_document || m_lassoPath.size() < 3) {
        return;
    }

    UndoAction undoAction;
    undoAction.type = UndoAction::RemoveMultiple;

    if (m_document->isEdgeless()) {
        int layerIdx = m_edgelessActiveLayerIndex;
        undoAction.layerIndex = layerIdx;

        const QRectF lassoBounds = m_lassoPath.boundingRect();

        // Only tiles the lasso reaches can hold strokes it selects, so scanning
        // every loaded tile made this proportional to canvas size rather than to
        // the gesture.
        const QVector<Document::TileCoord> tiles = m_document->tilesInRect(
            lassoBounds.adjusted(-EDGELESS_STROKE_MARGIN, -EDGELESS_STROKE_MARGIN,
                                 EDGELESS_STROKE_MARGIN, EDGELESS_STROKE_MARGIN));
        for (const auto& coord : tiles) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || layerIdx >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(layerIdx);
            if (!layer || layer->locked || layer->isEmpty()) continue;

            QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                               coord.second * Document::EDGELESS_TILE_SIZE);

            QSet<QString> idsToRemove;
            const auto& strokes = layer->strokes();
            for (const VectorStroke& stroke : strokes) {
                // Quick bounding-box rejection before expensive per-point copy + test
                QRectF docBBox = stroke.boundingBox.translated(tileOrigin);
                if (!docBBox.intersects(lassoBounds)) continue;

                VectorStroke docStroke = stroke;
                for (auto& pt : docStroke.points) {
                    pt.pos += tileOrigin;
                }
                if (strokeIntersectsLasso(docStroke, m_lassoPath)) {
                    idsToRemove.insert(stroke.id);
                }
            }

            if (idsToRemove.isEmpty()) continue;

            QVector<VectorStroke>& layerStrokes = layer->strokes();
            for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
                if (idsToRemove.contains(layerStrokes[i].id)) {
                    UndoAction::StrokeSegment seg;
                    seg.tileCoord = coord;
                    seg.stroke = layerStrokes[i];
                    undoAction.segments.append(seg);
                    layerStrokes.removeAt(i);
                }
            }
            // Every removal was inside the lasso, so repairing that region is
            // enough. Invalidating instead threw away a cache the next paint
            // then had to rebuild at full tile size.
            layer->patchCacheAfterRemovals(lassoBounds.translated(-tileOrigin));
            m_document->markTileDirty(coord);
        }
    } else {
        if (m_eraserLassoPageIndex < 0 || m_eraserLassoPageIndex >= m_document->pageCount()) return;
        Page* page = m_document->page(m_eraserLassoPageIndex);
        if (!page) return;
        VectorLayer* layer = page->activeLayer();
        if (!layer || layer->locked) return;

        undoAction.layerIndex = page->activeLayerIndex;

        // Bounds rejection before the per-point polygon test, matching the
        // edgeless branch above.
        const QRectF lassoBounds = m_lassoPath.boundingRect();

        QSet<QString> idsToRemove;
        for (const VectorStroke& stroke : layer->strokes()) {
            if (!stroke.boundingBox.intersects(lassoBounds)) continue;
            if (strokeIntersectsLasso(stroke, m_lassoPath)) {
                idsToRemove.insert(stroke.id);
            }
        }

        if (!idsToRemove.isEmpty()) {
            QVector<VectorStroke>& layerStrokes = layer->strokes();
            for (int i = static_cast<int>(layerStrokes.size()) - 1; i >= 0; --i) {
                if (idsToRemove.contains(layerStrokes[i].id)) {
                    UndoAction::StrokeSegment seg;
                    seg.pageIndex = m_eraserLassoPageIndex;
                    seg.stroke = layerStrokes[i];
                    undoAction.segments.append(seg);
                    layerStrokes.removeAt(i);
                }
            }
            layer->patchCacheAfterRemovals(lassoBounds);
            m_document->markPageDirty(m_eraserLassoPageIndex);
        }
    }

    if (!undoAction.segments.isEmpty()) {
        markOcrDirtyTiles(undoAction);
        pushUndoAction(undoAction);
        emit strokesChanged();
        emit documentModified();
    }
}

// ===== Undo/Redo System (unified) =====

void DocumentViewport::pushUndoAction(const UndoAction& action)
{
    m_undoStack.push(action);
    trimUndoStack();
    m_redoStack.clear();
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(false);
}

std::set<Document::TileCoord> DocumentViewport::takeOcrDirtyTiles()
{
    std::set<Document::TileCoord> result;
    result.swap(m_ocrDirtyTiles);
    return result;
}

std::set<int> DocumentViewport::takeOcrDirtyPages()
{
    std::set<int> result;
    result.swap(m_ocrDirtyPages);
    return result;
}

void DocumentViewport::markOcrDirtyTiles(const UndoAction& action)
{
    if (!m_document || !m_document->isEdgeless()) return;
    for (const auto& seg : action.segments)
        m_ocrDirtyTiles.insert(seg.tileCoord);
    for (const auto& seg : action.removedSegments)
        m_ocrDirtyTiles.insert(seg.tileCoord);
    for (const auto& seg : action.addedSegments)
        m_ocrDirtyTiles.insert(seg.tileCoord);
}

void DocumentViewport::pushPageStrokeUndo(int pageIndex, UndoAction::Type type, const VectorStroke& stroke, int layerIndex)
{
    UndoAction action;
    action.type = type;
    action.layerIndex = layerIndex;
    UndoAction::StrokeSegment seg;
    seg.pageIndex = pageIndex;
    seg.stroke = stroke;
    action.segments.append(seg);
    pushUndoAction(action);
    if (!m_document->isEdgeless()) {
        emit pageModified(pageIndex);
        m_ocrDirtyPages.insert(pageIndex);
    }
    emit strokesChanged();
}

void DocumentViewport::pushPageStrokesUndo(int pageIndex, UndoAction::Type type, const QVector<VectorStroke>& strokes, int layerIndex)
{
    UndoAction action;
    action.type = type;
    action.layerIndex = layerIndex;
    for (const auto& s : strokes) {
        UndoAction::StrokeSegment seg;
        seg.pageIndex = pageIndex;
        seg.stroke = s;
        action.segments.append(seg);
    }
    pushUndoAction(action);
    if (!m_document->isEdgeless()) {
        emit pageModified(pageIndex);
        m_ocrDirtyPages.insert(pageIndex);
    }
    emit strokesChanged();
}

void DocumentViewport::clearUndoStacksFrom(int pageIndex)
{
    bool hadUndo = canUndo();
    bool hadRedo = canRedo();
    
    auto referencesPage = [pageIndex](const UndoAction& a) {
        for (const auto& seg : a.segments)
            if (seg.pageIndex >= pageIndex) return true;
        for (const auto& seg : a.removedSegments)
            if (seg.pageIndex >= pageIndex) return true;
        for (const auto& seg : a.addedSegments)
            if (seg.pageIndex >= pageIndex) return true;
        if (a.objectPageIndex >= pageIndex) return true;
        if (a.objectOldPageIndex >= pageIndex) return true;
        if (a.objectNewPageIndex >= pageIndex) return true;
        return false;
    };
    
    QStack<UndoAction> kept;
    for (const auto& a : m_undoStack)
        if (!referencesPage(a)) kept.push(a);
    m_undoStack = kept;
    
    QStack<UndoAction> keptRedo;
    for (const auto& a : m_redoStack)
        if (!referencesPage(a)) keptRedo.push(a);
    m_redoStack = keptRedo;
    
    if (hadUndo && !canUndo()) emit undoAvailableChanged(false);
    if (hadRedo && !canRedo()) emit redoAvailableChanged(false);

    // Pages at/after pageIndex are shifting, so any pending OCR dirty indices in
    // that range now point at the wrong page. Drop them (same reasoning as the
    // undo-stack purge above); the affected pages re-mark themselves dirty on the
    // next edit.
    m_ocrDirtyPages.erase(m_ocrDirtyPages.lower_bound(pageIndex), m_ocrDirtyPages.end());
}

bool DocumentViewport::deletePagesWithUndo(const QList<int>& indices)
{
    if (!m_document || indices.isEmpty()) {
        return false;
    }

    // Collect valid, unique indices.
    QList<int> targets;
    for (int i : indices) {
        if (i >= 0 && i < m_document->pageCount() && !targets.contains(i)) {
            targets.append(i);
        }
    }
    if (targets.isEmpty()) {
        return false;
    }

    // Respect the document's minimum-page guard: never delete every page.
    if (targets.size() >= m_document->pageCount()) {
        return false;
    }

    // Clear any object selection first: removePage() frees the page's objects,
    // and m_selectedObjects holds raw pointers into them that would otherwise
    // dangle (crash on the next paint / selection query).
    if (hasSelectedObjects()) {
        deselectAllObjects();
    }

    // Remove in descending index order so earlier removals don't shift the
    // indices of pages we have yet to remove.
    std::sort(targets.begin(), targets.end(), [](int a, int b) { return a > b; });
    const int minIndex = targets.last();  // smallest index (sorted descending)

    UndoAction action;
    action.type = UndoAction::PageDelete;

    for (int idx : targets) {
        Page* page = m_document->page(idx);  // ensures the page is loaded
        if (!page) {
            continue;
        }
        UndoAction::DeletedPageSnapshot snap;
        snap.index = idx;
        snap.pageJson = page->toJson();
        if (m_document->removePage(idx)) {
            action.deletedPages.append(snap);
        }
    }

    if (action.deletedPages.isEmpty()) {
        return false;
    }

    action.focusPageIndex = qBound(0, minIndex, m_document->pageCount() - 1);

    // Drop now-stale stroke/object undo history for shifted pages, then push the
    // grouped PageDelete on top so one Ctrl+Z restores the whole set. Pushing
    // AFTER the purge prevents the action from clearing itself.
    clearUndoStacksFrom(minIndex);
    pushUndoAction(action);

    return true;
}

bool DocumentViewport::importPagesWithUndo(Document* srcDoc, const QStringList& srcPageUuids, int destIndex)
{
    if (!m_document || !srcDoc || srcPageUuids.isEmpty()) {
        return false;
    }

    PageImportResult result = m_document->importPagesFrom(srcDoc, srcPageUuids, destIndex);
    if (result.insertedPageJson.isEmpty()) {
        return false;
    }

    UndoAction action;
    action.type = UndoAction::PageInsert;
    QSet<QString> retainedSourceIds;
    for (int k = 0; k < result.insertedPageJson.size(); ++k) {
        UndoAction::DeletedPageSnapshot snap;
        snap.index = result.destStartIndex + k;
        snap.pageJson = result.insertedPageJson[k];
        action.deletedPages.append(snap);

        const QString sourceId = snap.pageJson.value(QStringLiteral("pdfSourceId")).toString();
        if (!sourceId.isEmpty()) retainedSourceIds.insert(sourceId);
    }
    for (const QString& sourceId : retainedSourceIds) {
        m_document->retainPdfSourceForUndo(sourceId);
    }

    const int focusIndex = result.destStartIndex >= 0 ? result.destStartIndex : destIndex;
    action.focusPageIndex = qBound(0, focusIndex, m_document->pageCount() - 1);

    clearUndoStacksFrom(focusIndex);
    pushUndoAction(action);

    emit pageStructureChangedByUndo(action.focusPageIndex);
    return true;
}

// ============================================================================
// Layer Management (Phase 5)
// ============================================================================

void DocumentViewport::setEdgelessActiveLayerIndex(int layerIndex)
{
    if (layerIndex < 0) layerIndex = 0;
    m_edgelessActiveLayerIndex = layerIndex;
}

void DocumentViewport::trimUndoStack()
{
    while (m_undoStack.size() > MAX_UNDO_ACTIONS) {
        m_undoStack.remove(0);
    }

    // Count limits alone are unsafe for image history: 100 clipboard 4K
    // snapshots can retain several GiB. Preserve the newest action while
    // evicting older history until recovery payloads fit a platform budget.
    const qint64 imageRecoveryBudget = sizeof(void*) >= 8
        ? 512LL * 1024 * 1024
        : 128LL * 1024 * 1024;
    auto recoveryBytes = [](const UndoAction& action) {
        return static_cast<qint64>(action.objectImageEncodedData.size())
            + (action.objectImageSnapshot.isNull()
                   ? 0
                   : static_cast<qint64>(
                         action.objectImageSnapshot.sizeInBytes()));
    };
    qint64 retainedBytes = 0;
    for (const UndoAction& action : m_undoStack) {
        retainedBytes += recoveryBytes(action);
    }
    while (m_undoStack.size() > 1 && retainedBytes > imageRecoveryBudget) {
        retainedBytes -= recoveryBytes(m_undoStack.first());
        m_undoStack.remove(0);
    }
}

// (undoEdgeless/redoEdgeless/clearEdgelessRedoStack/trimEdgelessUndoStack removed --
//  all undo/redo is now handled by the unified undo() and redo() below)

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

// ============================================================================
// Unified undo/redo helpers
// ============================================================================

static Page* getContainer(Document* doc, const UndoAction::StrokeSegment& seg, bool create)
{
    if (doc->isEdgeless()) {
        return create
            ? doc->getOrCreateTile(seg.tileCoord.first, seg.tileCoord.second)
            : doc->getTile(seg.tileCoord.first, seg.tileCoord.second);
    }
    return doc->page(seg.pageIndex);
}

static void markSegDirty(Document* doc, const UndoAction::StrokeSegment& seg)
{
    if (doc->isEdgeless())
        doc->markTileDirty(seg.tileCoord);
    else
        doc->markPageDirty(seg.pageIndex);
}

static void tryRemoveEmptyTile(Document* doc, const UndoAction::StrokeSegment& seg)
{
    if (doc->isEdgeless())
        doc->removeTileIfEmpty(seg.tileCoord.first, seg.tileCoord.second);
}

static Page* getObjContainer(Document* doc, const UndoAction& a, bool create)
{
    if (doc->isEdgeless()) {
        return create
            ? doc->getOrCreateTile(a.objectTileCoord.first, a.objectTileCoord.second)
            : doc->getTile(a.objectTileCoord.first, a.objectTileCoord.second);
    }
    return doc->page(a.objectPageIndex);
}

static void markObjDirty(Document* doc, const UndoAction& a)
{
    if (doc->isEdgeless())
        doc->markTileDirty(a.objectTileCoord);
    else
        doc->markPageDirty(a.objectPageIndex);
}

static QSet<int> collectAffectedPages(const UndoAction& action)
{
    QSet<int> pages;
    for (const auto& s : action.segments) if (s.pageIndex >= 0) pages.insert(s.pageIndex);
    for (const auto& s : action.removedSegments) if (s.pageIndex >= 0) pages.insert(s.pageIndex);
    for (const auto& s : action.addedSegments) if (s.pageIndex >= 0) pages.insert(s.pageIndex);
    if (action.objectPageIndex >= 0) pages.insert(action.objectPageIndex);
    if (action.objectOldPageIndex >= 0) pages.insert(action.objectOldPageIndex);
    if (action.objectNewPageIndex >= 0) pages.insert(action.objectNewPageIndex);
    return pages;
}

void DocumentViewport::undo()
{
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    if (m_inlineEditSession.active)
        commitInlineTextEdit();
    if (m_undoStack.isEmpty() || !m_document) return;

    UndoAction action = m_undoStack.pop();

    if (action.type == UndoAction::PageDelete) {
        // Restore removed pages in ascending index order (Plan A2).
        QVector<UndoAction::DeletedPageSnapshot> snaps = action.deletedPages;
        std::sort(snaps.begin(), snaps.end(),
                  [](const UndoAction::DeletedPageSnapshot& a,
                     const UndoAction::DeletedPageSnapshot& b) { return a.index < b.index; });
        for (const auto& snap : snaps) {
            m_document->restorePageFromSnapshot(snap.index, snap.pageJson);
        }
        m_redoStack.push(action);
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
        int focus = snaps.isEmpty() ? action.focusPageIndex : snaps.first().index;
        emit pageStructureChangedByUndo(qBound(0, focus, m_document->pageCount() - 1));
        return;
    }

    if (action.type == UndoAction::PageInsert) {
        // Remove imported pages in descending index order (Plan B).
        if (hasSelectedObjects()) {
            deselectAllObjects();
        }
        QVector<UndoAction::DeletedPageSnapshot> snaps = action.deletedPages;
        std::sort(snaps.begin(), snaps.end(),
                  [](const UndoAction::DeletedPageSnapshot& a,
                     const UndoAction::DeletedPageSnapshot& b) { return a.index > b.index; });
        for (const auto& snap : snaps) {
            m_document->removePage(snap.index);
        }
        m_redoStack.push(action);
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
        emit pageStructureChangedByUndo(qBound(0, action.focusPageIndex, m_document->pageCount() - 1));
        return;
    }

    bool isObjectAction = (action.type == UndoAction::ObjectInsert ||
                           action.type == UndoAction::ObjectDelete ||
                           action.type == UndoAction::ObjectMove ||
                           action.type == UndoAction::ObjectAffinityChange ||
                           action.type == UndoAction::ObjectResize ||
                           action.type == UndoAction::ObjectTextEdit ||
                           action.type == UndoAction::OcrLockChange ||
                           action.type == UndoAction::OcrConvertToTextBox);

    if (isObjectAction) {
        switch (action.type) {
            case UndoAction::OcrConvertToTextBox: {
                revertOcrConversion(action);
                break;
            }
            case UndoAction::ObjectInsert: {
                deselectObjectById(action.objectId);
                Page* c = getObjContainer(m_document, action, false);
                if (c) {
                    c->removeObject(action.objectId);
                    markObjDirty(m_document, action);
                    if (m_document->isEdgeless())
                        m_document->removeTileIfEmpty(action.objectTileCoord.first,
                                                      action.objectTileCoord.second);
                }
                m_document->recalculateMaxObjectExtent();
                break;
            }
            case UndoAction::ObjectDelete: {
                Page* c = getObjContainer(m_document, action, true);
                if (c) {
                    auto obj = InsertedObject::fromJson(action.objectData);
                    if (obj) {
                        if (dynamic_cast<ImageObject*>(obj.get())
                            && action.objectImageSnapshot.isNull()
                            && action.objectImageEncodedData.isEmpty()) {
                            m_document->flushPendingImageWrites();
                        }
                        const bool assetLoaded = obj->loadAssets(m_document->bundlePath());
                        if (!assetLoaded) {
                            if (auto* image = dynamic_cast<ImageObject*>(obj.get())) {
                                QImage recovery = action.objectImageSnapshot;
                                if (recovery.isNull() && !action.objectImageEncodedData.isEmpty()) {
                                    recovery.loadFromData(action.objectImageEncodedData);
                                }
                                if (!recovery.isNull()) {
                                    image->setSourceImage(recovery,
                                                          action.objectImageEncodedData,
                                                          action.objectImageFormat);
                                }
                            }
                        }
                        m_document->updateMaxObjectExtent(obj.get());
                        c->addObject(std::move(obj));
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::ObjectMove: {
                bool crossContainer = m_document->isEdgeless()
                    ? (action.objectOldTile != action.objectNewTile)
                    : (action.objectOldPageIndex != action.objectNewPageIndex
                       && action.objectOldPageIndex >= 0 && action.objectNewPageIndex >= 0);
                if (crossContainer) {
                    Page* newC = m_document->isEdgeless()
                        ? m_document->getTile(action.objectNewTile.first, action.objectNewTile.second)
                        : m_document->page(action.objectNewPageIndex);
                    if (newC) {
                        auto obj = newC->extractObject(action.objectId);
                        if (obj) {
                            obj->position = action.objectOldPosition;
                            Page* oldC = m_document->isEdgeless()
                                ? m_document->getOrCreateTile(action.objectOldTile.first, action.objectOldTile.second)
                                : m_document->page(action.objectOldPageIndex);
                            if (oldC) {
                                oldC->addObject(std::move(obj));
                                if (m_document->isEdgeless())
                                    m_document->markTileDirty(action.objectOldTile);
                                else
                                    m_document->markPageDirty(action.objectOldPageIndex);
                            }
                            if (m_document->isEdgeless()) {
                                m_document->markTileDirty(action.objectNewTile);
                                m_document->removeTileIfEmpty(action.objectNewTile.first,
                                                              action.objectNewTile.second);
                            } else {
                                m_document->markPageDirty(action.objectNewPageIndex);
                            }
                        }
                    }
                } else {
                    Page* c = getObjContainer(m_document, action, false);
                    if (c) {
                        InsertedObject* obj = c->objectById(action.objectId);
                        if (obj) obj->position = action.objectOldPosition;
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::ObjectAffinityChange: {
                Page* c = getObjContainer(m_document, action, false);
                if (c) {
                    c->updateObjectAffinity(action.objectId, action.objectOldAffinity);
                    markObjDirty(m_document, action);
                }
                break;
            }
            case UndoAction::ObjectResize: {
                Page* c = nullptr;
                const bool crossTile = m_document->isEdgeless()
                    && action.objectOldTile != action.objectNewTile;
                std::unique_ptr<InsertedObject> movedObject;
                Page* sourceTile = nullptr;
                if (crossTile) {
                    sourceTile = m_document->getTile(
                        action.objectNewTile.first,
                        action.objectNewTile.second);
                    if (sourceTile)
                        movedObject =
                            sourceTile->extractObject(action.objectId);
                } else {
                    c = getObjContainer(m_document, action, false);
                }
                InsertedObject* obj = movedObject
                    ? movedObject.get()
                    : (c ? c->objectById(action.objectId) : nullptr);
                if (obj) {
                    if (action.objectHasTextBoxState) {
                        if (auto* textBox =
                                dynamic_cast<TextBoxObject*>(obj)) {
                            textBox->applyState(
                                action.objectOldTextBoxState);
                        }
                    } else {
                        obj->position = action.objectOldPosition;
                        obj->size = action.objectOldSize;
                        obj->rotation = action.objectOldRotation;
                    }
                    if (obj->type() == "image") {
                        if (auto* img = dynamic_cast<ImageObject*>(obj))
                            img->maintainAspectRatio =
                                action.objectOldAspectLock;
                    }
                }
                if (movedObject) {
                    Page* oldTile = m_document->getOrCreateTile(
                        action.objectOldTile.first,
                        action.objectOldTile.second);
                    if (oldTile) {
                        oldTile->addObject(std::move(movedObject));
                        m_document->markTileDirty(action.objectOldTile);
                        m_document->markTileDirty(action.objectNewTile);
                        m_document->removeTileIfEmpty(
                            action.objectNewTile.first,
                            action.objectNewTile.second);
                    } else if (sourceTile) {
                        if (action.objectHasTextBoxState) {
                            if (auto* textBox =
                                    dynamic_cast<TextBoxObject*>(
                                        movedObject.get())) {
                                textBox->applyState(
                                    action.objectNewTextBoxState);
                            }
                        } else {
                            movedObject->position =
                                action.objectNewPosition;
                            movedObject->size = action.objectNewSize;
                            movedObject->rotation =
                                action.objectNewRotation;
                        }
                        sourceTile->addObject(std::move(movedObject));
                    }
                } else if (c) {
                    markObjDirty(m_document, action);
                }
                break;
            }
            case UndoAction::ObjectTextEdit: {
                if (m_document->isEdgeless()
                    && action.objectOldTile != action.objectNewTile) {
                    Page* source = m_document->getTile(
                        action.objectNewTile.first,
                        action.objectNewTile.second);
                    std::unique_ptr<InsertedObject> moved =
                        source ? source->extractObject(action.objectId)
                               : nullptr;
                    if (moved) {
                        if (auto* textBox =
                                dynamic_cast<TextBoxObject*>(moved.get())) {
                            textBox->applyState(
                                action.objectOldTextBoxState);
                        }
                        Page* target = m_document->getOrCreateTile(
                            action.objectOldTile.first,
                            action.objectOldTile.second);
                        if (target) {
                            target->addObject(std::move(moved));
                            m_document->markTileDirty(
                                action.objectOldTile);
                            m_document->markTileDirty(
                                action.objectNewTile);
                            m_document->removeTileIfEmpty(
                                action.objectNewTile.first,
                                action.objectNewTile.second);
                        } else {
                            if (auto* textBox =
                                    dynamic_cast<TextBoxObject*>(
                                        moved.get())) {
                                textBox->applyState(
                                    action.objectNewTextBoxState);
                            }
                            source->addObject(std::move(moved));
                        }
                    }
                } else {
                    Page* c = getObjContainer(
                        m_document, action, false);
                    if (c) {
                        InsertedObject* obj =
                            c->objectById(action.objectId);
                        if (auto* tbox =
                                dynamic_cast<TextBoxObject*>(obj)) {
                            tbox->applyState(
                                action.objectOldTextBoxState);
                        }
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::OcrLockChange: {
                if (m_document->isEdgeless()) {
                    for (const auto& coord : m_document->allLoadedTileCoords()) {
                        Page* tile = m_document->getTile(coord.first, coord.second);
                        if (!tile) continue;
                        bool modified = false;
                        for (const auto& oid : action.ocrLockObjectIds) {
                            InsertedObject* obj = tile->objectById(oid);
                            if (auto* ocr = dynamic_cast<OcrTextObject*>(obj)) {
                                ocr->ocrLocked = !action.ocrLockNewState;
                                modified = true;
                            }
                        }
                        if (modified)
                            m_document->markTileDirty(coord);
                    }
                } else {
                    Page* c = m_document->page(action.objectPageIndex);
                    if (c) {
                        for (const auto& oid : action.ocrLockObjectIds) {
                            InsertedObject* obj = c->objectById(oid);
                            if (auto* ocr = dynamic_cast<OcrTextObject*>(obj))
                                ocr->ocrLocked = !action.ocrLockNewState;
                        }
                        m_document->markPageDirty(action.objectPageIndex);
                    }
                }
                break;
            }
            default: break;
        }
    } else if (action.type == UndoAction::TransformSelection) {
        // Remove added strokes
        for (const auto& seg : action.addedSegments) {
            Page* c = getContainer(m_document, seg, false);
            if (!c) continue;
            VectorLayer* layer = c->layer(action.layerIndex);
            if (layer) layer->removeStroke(seg.stroke.id);
            markSegDirty(m_document, seg);
            tryRemoveEmptyTile(m_document, seg);
        }
        // Restore removed strokes
        for (const auto& seg : action.removedSegments) {
            Page* c = getContainer(m_document, seg, true);
            if (!c) continue;
            while (c->layerCount() <= action.layerIndex)
                c->addLayer(QString("Layer %1").arg(c->layerCount() + 1));
            VectorLayer* layer = c->layer(action.layerIndex);
            if (layer) layer->addStroke(seg.stroke);
            markSegDirty(m_document, seg);
        }
    } else if (action.type == UndoAction::RecolorStrokes) {
        // In-place restore of each stroke's OLD color (the snapshot in
        // seg.stroke carries the pre-recolor color verbatim, alpha included).
        for (const auto& seg : action.segments) {
            Page* c = getContainer(m_document, seg, /*createIfMissing*/false);
            if (!c) continue;
            VectorLayer* layer = c->layer(action.layerIndex);
            if (!layer) continue;
            QVector<VectorStroke>& strokes = layer->strokes();
            for (VectorStroke& s : strokes) {
                if (s.id == seg.stroke.id) {
                    s.color = seg.stroke.color;
                    break;
                }
            }
            layer->invalidateStrokeCache();
            markSegDirty(m_document, seg);
        }
        // Keep the lasso selection overlay in sync with the layer. The
        // recolor flow intentionally leaves the selection alive (so the user
        // can re-recolor / transform), which means its cached stroke copies
        // and rasterized overlay would otherwise still paint the post-recolor
        // color even after undo restored the underlying layer to OLD colors.
        if (m_lassoSelection.isValid() && !m_lassoSelection.selectedStrokes.isEmpty()) {
            QHash<QString, QColor> oldById;
            oldById.reserve(action.segments.size());
            for (const auto& seg : action.segments)
                oldById.insert(seg.stroke.id, seg.stroke.color);
            bool patched = false;
            for (VectorStroke& s : m_lassoSelection.selectedStrokes) {
                auto it = oldById.constFind(s.id);
                if (it != oldById.constEnd()) {
                    s.color = it.value();
                    patched = true;
                }
            }
            if (patched) {
                m_selectionStrokeCache = QPixmap();
                m_selectionCacheDirty = true;
            }
        }
    } else {
        for (const auto& seg : action.segments) {
            Page* c = getContainer(m_document, seg,
                                   action.type != UndoAction::AddStroke);
            if (!c) continue;
            while (c->layerCount() <= action.layerIndex)
                c->addLayer(QString("Layer %1").arg(c->layerCount() + 1));
            VectorLayer* layer = c->layer(action.layerIndex);
            if (!layer) continue;

            switch (action.type) {
                case UndoAction::AddStroke:
                    layer->removeStroke(seg.stroke.id);
                    markSegDirty(m_document, seg);
                    tryRemoveEmptyTile(m_document, seg);
                    break;
                case UndoAction::RemoveStroke:
                case UndoAction::RemoveMultiple:
                    layer->addStroke(seg.stroke);
                    markSegDirty(m_document, seg);
                    break;
                default: break;
            }
        }
    }

    // Auto-navigate if the action's page differs from current view (paged mode)
    if (!m_document->isEdgeless()) {
        int actionPage = -1;
        if (!action.segments.isEmpty())
            actionPage = action.segments.first().pageIndex;
        else if (!action.removedSegments.isEmpty())
            actionPage = action.removedSegments.first().pageIndex;
        else if (action.objectPageIndex >= 0)
            actionPage = action.objectPageIndex;
        else if (action.objectOldPageIndex >= 0)
            actionPage = action.objectOldPageIndex;
        if (actionPage >= 0 && !visiblePages().contains(actionPage))
            scrollToPage(actionPage);
    }

    m_redoStack.push(action);
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    if (UndoAction::affectsTextLayout(action.type)) {
        emit textBoxLayoutCommitted();
    }
    if (action.type == UndoAction::ObjectInsert || action.type == UndoAction::ObjectDelete) {
        // Phase M.9: refresh the single container this action touches.
        if (m_document->isEdgeless()) {
            m_document->refreshLinkOutlineFor(action.objectTileCoord);
        } else if (action.objectPageIndex >= 0) {
            m_document->refreshLinkOutlineFor(action.objectPageIndex);
        }
        emit linkObjectListMayHaveChanged();
    }
    if (action.type == UndoAction::AddStroke || action.type == UndoAction::RemoveStroke ||
        action.type == UndoAction::RemoveMultiple || action.type == UndoAction::TransformSelection ||
        action.type == UndoAction::RecolorStrokes) {
        // RecolorStrokes leaves the stroke set unchanged (only colours move),
        // so it gets the strokesChanged repaint but skips OCR dirty-marking
        // (text content is unaffected by colour).
        if (action.type != UndoAction::RecolorStrokes) {
            markOcrDirtyTiles(action);
            if (!m_document->isEdgeless())
                for (int p : collectAffectedPages(action)) m_ocrDirtyPages.insert(p);
        }
        emit strokesChanged();
    }
    if (!m_document->isEdgeless())
        for (int p : collectAffectedPages(action)) emit pageModified(p);
    update();
}

void DocumentViewport::redo()
{
    closeTextBoxFormatPopups(true);
    finishTextBoxFormatInteraction(true);
    if (m_inlineEditSession.active)
        commitInlineTextEdit();
    if (m_redoStack.isEmpty() || !m_document) return;

    UndoAction action = m_redoStack.pop();

    if (action.type == UndoAction::PageDelete) {
        // Re-remove pages in descending index order (Plan A2).
        QVector<UndoAction::DeletedPageSnapshot> snaps = action.deletedPages;
        std::sort(snaps.begin(), snaps.end(),
                  [](const UndoAction::DeletedPageSnapshot& a,
                     const UndoAction::DeletedPageSnapshot& b) { return a.index > b.index; });
        for (const auto& snap : snaps) {
            m_document->removePage(snap.index);
        }
        m_undoStack.push(action);
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
        emit pageStructureChangedByUndo(qBound(0, action.focusPageIndex, m_document->pageCount() - 1));
        return;
    }

    if (action.type == UndoAction::PageInsert) {
        // Re-insert imported pages in ascending index order (Plan B).
        QVector<UndoAction::DeletedPageSnapshot> snaps = action.deletedPages;
        std::sort(snaps.begin(), snaps.end(),
                  [](const UndoAction::DeletedPageSnapshot& a,
                     const UndoAction::DeletedPageSnapshot& b) { return a.index < b.index; });
        for (const auto& snap : snaps) {
            m_document->restorePageFromSnapshot(snap.index, snap.pageJson);
        }
        m_undoStack.push(action);
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
        int focus = snaps.isEmpty() ? action.focusPageIndex : snaps.first().index;
        emit pageStructureChangedByUndo(qBound(0, focus, m_document->pageCount() - 1));
        return;
    }

    bool isObjectAction = (action.type == UndoAction::ObjectInsert ||
                           action.type == UndoAction::ObjectDelete ||
                           action.type == UndoAction::ObjectMove ||
                           action.type == UndoAction::ObjectAffinityChange ||
                           action.type == UndoAction::ObjectResize ||
                           action.type == UndoAction::ObjectTextEdit ||
                           action.type == UndoAction::OcrLockChange ||
                           action.type == UndoAction::OcrConvertToTextBox);

    if (isObjectAction) {
        switch (action.type) {
            case UndoAction::OcrConvertToTextBox: {
                applyOcrConversion(action);
                break;
            }
            case UndoAction::ObjectInsert: {
                Page* c = getObjContainer(m_document, action, true);
                if (c) {
                    auto obj = InsertedObject::fromJson(action.objectData);
                    if (obj) {
                        if (dynamic_cast<ImageObject*>(obj.get())
                            && action.objectImageSnapshot.isNull()
                            && action.objectImageEncodedData.isEmpty()) {
                            m_document->flushPendingImageWrites();
                        }
                        const bool assetLoaded = obj->loadAssets(m_document->bundlePath());
                        if (!assetLoaded) {
                            if (auto* image = dynamic_cast<ImageObject*>(obj.get())) {
                                QImage recovery = action.objectImageSnapshot;
                                if (recovery.isNull() && !action.objectImageEncodedData.isEmpty()) {
                                    recovery.loadFromData(action.objectImageEncodedData);
                                }
                                if (!recovery.isNull()) {
                                    image->setSourceImage(recovery,
                                                          action.objectImageEncodedData,
                                                          action.objectImageFormat);
                                }
                            }
                        }
                        m_document->updateMaxObjectExtent(obj.get());
                        c->addObject(std::move(obj));
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::ObjectDelete: {
                deselectObjectById(action.objectId);
                if (m_hoveredObject && m_hoveredObject->id == action.objectId) {
                    m_hoveredObject = nullptr;
                }
                Page* c = getObjContainer(m_document, action, false);
                if (c) {
                    c->removeObject(action.objectId);
                    markObjDirty(m_document, action);
                    if (m_document->isEdgeless())
                        m_document->removeTileIfEmpty(action.objectTileCoord.first,
                                                      action.objectTileCoord.second);
                }
                m_document->recalculateMaxObjectExtent();
                break;
            }
            case UndoAction::ObjectMove: {
                bool crossContainer = m_document->isEdgeless()
                    ? (action.objectOldTile != action.objectNewTile)
                    : (action.objectOldPageIndex != action.objectNewPageIndex
                       && action.objectOldPageIndex >= 0 && action.objectNewPageIndex >= 0);
                if (crossContainer) {
                    Page* oldC = m_document->isEdgeless()
                        ? m_document->getTile(action.objectOldTile.first, action.objectOldTile.second)
                        : m_document->page(action.objectOldPageIndex);
                    if (oldC) {
                        auto obj = oldC->extractObject(action.objectId);
                        if (obj) {
                            obj->position = action.objectNewPosition;
                            Page* newC = m_document->isEdgeless()
                                ? m_document->getOrCreateTile(action.objectNewTile.first, action.objectNewTile.second)
                                : m_document->page(action.objectNewPageIndex);
                            if (newC) {
                                newC->addObject(std::move(obj));
                                if (m_document->isEdgeless())
                                    m_document->markTileDirty(action.objectNewTile);
                                else
                                    m_document->markPageDirty(action.objectNewPageIndex);
                            }
                            if (m_document->isEdgeless()) {
                                m_document->markTileDirty(action.objectOldTile);
                                m_document->removeTileIfEmpty(action.objectOldTile.first,
                                                              action.objectOldTile.second);
                            } else {
                                m_document->markPageDirty(action.objectOldPageIndex);
                            }
                        }
                    }
                } else {
                    Page* c = getObjContainer(m_document, action, false);
                    if (c) {
                        InsertedObject* obj = c->objectById(action.objectId);
                        if (obj) obj->position = action.objectNewPosition;
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::ObjectAffinityChange: {
                Page* c = getObjContainer(m_document, action, false);
                if (c) {
                    c->updateObjectAffinity(action.objectId, action.objectNewAffinity);
                    markObjDirty(m_document, action);
                }
                break;
            }
            case UndoAction::ObjectResize: {
                Page* c = nullptr;
                const bool crossTile = m_document->isEdgeless()
                    && action.objectOldTile != action.objectNewTile;
                std::unique_ptr<InsertedObject> movedObject;
                Page* sourceTile = nullptr;
                if (crossTile) {
                    sourceTile = m_document->getTile(
                        action.objectOldTile.first,
                        action.objectOldTile.second);
                    if (sourceTile)
                        movedObject =
                            sourceTile->extractObject(action.objectId);
                } else {
                    c = getObjContainer(m_document, action, false);
                }
                InsertedObject* obj = movedObject
                    ? movedObject.get()
                    : (c ? c->objectById(action.objectId) : nullptr);
                if (obj) {
                    if (action.objectHasTextBoxState) {
                        if (auto* textBox =
                                dynamic_cast<TextBoxObject*>(obj)) {
                            textBox->applyState(
                                action.objectNewTextBoxState);
                        }
                    } else {
                        obj->position = action.objectNewPosition;
                        obj->size = action.objectNewSize;
                        obj->rotation = action.objectNewRotation;
                    }
                    if (obj->type() == "image") {
                        if (auto* img = dynamic_cast<ImageObject*>(obj))
                            img->maintainAspectRatio =
                                action.objectNewAspectLock;
                    }
                }
                if (movedObject) {
                    Page* newTile = m_document->getOrCreateTile(
                        action.objectNewTile.first,
                        action.objectNewTile.second);
                    if (newTile) {
                        newTile->addObject(std::move(movedObject));
                        m_document->markTileDirty(action.objectOldTile);
                        m_document->markTileDirty(action.objectNewTile);
                        m_document->removeTileIfEmpty(
                            action.objectOldTile.first,
                            action.objectOldTile.second);
                    } else if (sourceTile) {
                        if (action.objectHasTextBoxState) {
                            if (auto* textBox =
                                    dynamic_cast<TextBoxObject*>(
                                        movedObject.get())) {
                                textBox->applyState(
                                    action.objectOldTextBoxState);
                            }
                        } else {
                            movedObject->position =
                                action.objectOldPosition;
                            movedObject->size = action.objectOldSize;
                            movedObject->rotation =
                                action.objectOldRotation;
                        }
                        sourceTile->addObject(std::move(movedObject));
                    }
                } else if (c) {
                    markObjDirty(m_document, action);
                }
                break;
            }
            case UndoAction::ObjectTextEdit: {
                if (m_document->isEdgeless()
                    && action.objectOldTile != action.objectNewTile) {
                    Page* source = m_document->getTile(
                        action.objectOldTile.first,
                        action.objectOldTile.second);
                    std::unique_ptr<InsertedObject> moved =
                        source ? source->extractObject(action.objectId)
                               : nullptr;
                    if (moved) {
                        if (auto* textBox =
                                dynamic_cast<TextBoxObject*>(moved.get())) {
                            textBox->applyState(
                                action.objectNewTextBoxState);
                        }
                        Page* target = m_document->getOrCreateTile(
                            action.objectNewTile.first,
                            action.objectNewTile.second);
                        if (target) {
                            target->addObject(std::move(moved));
                            m_document->markTileDirty(
                                action.objectOldTile);
                            m_document->markTileDirty(
                                action.objectNewTile);
                            m_document->removeTileIfEmpty(
                                action.objectOldTile.first,
                                action.objectOldTile.second);
                        } else {
                            if (auto* textBox =
                                    dynamic_cast<TextBoxObject*>(
                                        moved.get())) {
                                textBox->applyState(
                                    action.objectOldTextBoxState);
                            }
                            source->addObject(std::move(moved));
                        }
                    }
                } else {
                    Page* c = getObjContainer(
                        m_document, action, false);
                    if (c) {
                        InsertedObject* obj =
                            c->objectById(action.objectId);
                        if (auto* tbox =
                                dynamic_cast<TextBoxObject*>(obj)) {
                            tbox->applyState(
                                action.objectNewTextBoxState);
                        }
                        markObjDirty(m_document, action);
                    }
                }
                break;
            }
            case UndoAction::OcrLockChange: {
                if (m_document->isEdgeless()) {
                    for (const auto& coord : m_document->allLoadedTileCoords()) {
                        Page* tile = m_document->getTile(coord.first, coord.second);
                        if (!tile) continue;
                        bool modified = false;
                        for (const auto& oid : action.ocrLockObjectIds) {
                            InsertedObject* obj = tile->objectById(oid);
                            if (auto* ocr = dynamic_cast<OcrTextObject*>(obj)) {
                                ocr->ocrLocked = action.ocrLockNewState;
                                modified = true;
                            }
                        }
                        if (modified)
                            m_document->markTileDirty(coord);
                    }
                } else {
                    Page* c = m_document->page(action.objectPageIndex);
                    if (c) {
                        for (const auto& oid : action.ocrLockObjectIds) {
                            InsertedObject* obj = c->objectById(oid);
                            if (auto* ocr = dynamic_cast<OcrTextObject*>(obj))
                                ocr->ocrLocked = action.ocrLockNewState;
                        }
                        m_document->markPageDirty(action.objectPageIndex);
                    }
                }
                break;
            }
            default: break;
        }
    } else if (action.type == UndoAction::TransformSelection) {
        // Remove original strokes (redo the remove)
        for (const auto& seg : action.removedSegments) {
            Page* c = getContainer(m_document, seg, false);
            if (!c) continue;
            VectorLayer* layer = c->layer(action.layerIndex);
            if (layer) layer->removeStroke(seg.stroke.id);
            markSegDirty(m_document, seg);
            tryRemoveEmptyTile(m_document, seg);
        }
        // Add transformed strokes (redo the add)
        for (const auto& seg : action.addedSegments) {
            Page* c = getContainer(m_document, seg, true);
            if (!c) continue;
            while (c->layerCount() <= action.layerIndex)
                c->addLayer(QString("Layer %1").arg(c->layerCount() + 1));
            VectorLayer* layer = c->layer(action.layerIndex);
            if (layer) layer->addStroke(seg.stroke);
            markSegDirty(m_document, seg);
        }
    } else if (action.type == UndoAction::RecolorStrokes) {
        // In-place re-apply of the stored target color, preserving each
        // stroke's existing alpha (matches recolorLassoSelection's policy).
        QSet<QString> actionIds;
        actionIds.reserve(action.segments.size());
        for (const auto& seg : action.segments) {
            actionIds.insert(seg.stroke.id);
            Page* c = getContainer(m_document, seg, /*createIfMissing*/false);
            if (!c) continue;
            VectorLayer* layer = c->layer(action.layerIndex);
            if (!layer) continue;
            QVector<VectorStroke>& strokes = layer->strokes();
            for (VectorStroke& s : strokes) {
                if (s.id == seg.stroke.id) {
                    QColor cnew = action.recolorNewColor;
                    cnew.setAlpha(s.color.alpha());
                    s.color = cnew;
                    break;
                }
            }
            layer->invalidateStrokeCache();
            markSegDirty(m_document, seg);
        }
        // Mirror the layer patch into the lasso selection overlay (see undo()
        // for the full rationale). Single colour source here, alpha kept per-
        // stroke to preserve marker / highlighter opacity.
        if (m_lassoSelection.isValid() && !m_lassoSelection.selectedStrokes.isEmpty()) {
            bool patched = false;
            for (VectorStroke& s : m_lassoSelection.selectedStrokes) {
                if (actionIds.contains(s.id)) {
                    QColor cnew = action.recolorNewColor;
                    cnew.setAlpha(s.color.alpha());
                    s.color = cnew;
                    patched = true;
                }
            }
            if (patched) {
                m_selectionStrokeCache = QPixmap();
                m_selectionCacheDirty = true;
            }
        }
    } else {
        for (const auto& seg : action.segments) {
            Page* c = getContainer(m_document, seg,
                                   action.type == UndoAction::AddStroke);
            if (!c) continue;
            while (c->layerCount() <= action.layerIndex)
                c->addLayer(QString("Layer %1").arg(c->layerCount() + 1));
            VectorLayer* layer = c->layer(action.layerIndex);
            if (!layer) continue;

            switch (action.type) {
                case UndoAction::AddStroke:
                    layer->addStroke(seg.stroke);
                    markSegDirty(m_document, seg);
                    break;
                case UndoAction::RemoveStroke:
                case UndoAction::RemoveMultiple:
                    layer->removeStroke(seg.stroke.id);
                    markSegDirty(m_document, seg);
                    tryRemoveEmptyTile(m_document, seg);
                    break;
                default: break;
            }
        }
    }

    // Auto-navigate if the action's page differs from current view (paged mode)
    if (!m_document->isEdgeless()) {
        int actionPage = -1;
        if (!action.segments.isEmpty())
            actionPage = action.segments.first().pageIndex;
        else if (!action.addedSegments.isEmpty())
            actionPage = action.addedSegments.first().pageIndex;
        else if (action.objectPageIndex >= 0)
            actionPage = action.objectPageIndex;
        else if (action.objectNewPageIndex >= 0)
            actionPage = action.objectNewPageIndex;
        if (actionPage >= 0 && !visiblePages().contains(actionPage))
            scrollToPage(actionPage);
    }

    m_undoStack.push(action);
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    if (UndoAction::affectsTextLayout(action.type)) {
        emit textBoxLayoutCommitted();
    }
    if (action.type == UndoAction::ObjectInsert || action.type == UndoAction::ObjectDelete) {
        // Phase M.9: refresh the single container this action touches.
        if (m_document->isEdgeless()) {
            m_document->refreshLinkOutlineFor(action.objectTileCoord);
        } else if (action.objectPageIndex >= 0) {
            m_document->refreshLinkOutlineFor(action.objectPageIndex);
        }
        emit linkObjectListMayHaveChanged();
    }
    if (action.type == UndoAction::AddStroke || action.type == UndoAction::RemoveStroke ||
        action.type == UndoAction::RemoveMultiple || action.type == UndoAction::TransformSelection ||
        action.type == UndoAction::RecolorStrokes) {
        // RecolorStrokes leaves the stroke set unchanged (only colours move),
        // so it gets the strokesChanged repaint but skips OCR dirty-marking
        // (text content is unaffected by colour).
        if (action.type != UndoAction::RecolorStrokes) {
            markOcrDirtyTiles(action);
            if (!m_document->isEdgeless())
                for (int p : collectAffectedPages(action)) m_ocrDirtyPages.insert(p);
        }
        emit strokesChanged();
    }
    if (!m_document->isEdgeless())
        for (int p : collectAffectedPages(action)) emit pageModified(p);
    update();
}

bool DocumentViewport::canUndo() const
{
    return !m_undoStack.isEmpty();
}

bool DocumentViewport::canRedo() const
{
    return !m_redoStack.isEmpty();
}

// ===== Object Undo Helpers (unified) =====

void DocumentViewport::pushObjectInsertUndo(InsertedObject* obj, int pageIndex,
                                            Document::TileCoord tileCoord)
{
    if (!obj) return;

    UndoAction action;
    action.type = UndoAction::ObjectInsert;
    if (auto* image = dynamic_cast<ImageObject*>(obj)) {
        action.objectData = image->toJsonWithoutRecoveryData();
        action.objectImageEncodedData = image->encodedAssetData();
        action.objectImageFormat = image->assetFormat();
        if (action.objectImageEncodedData.isEmpty()) {
            action.objectImageSnapshot = image->pixmap().toImage();
        }
    } else {
        action.objectData = obj->toJson();
    }
    action.objectId = obj->id;
    if (m_document && m_document->isEdgeless()) {
        action.objectTileCoord = tileCoord;
    } else {
        action.objectPageIndex = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
    }
    pushUndoAction(action);
}

void DocumentViewport::pushObjectDeleteUndo(InsertedObject* obj, int pageIndex,
                                            Document::TileCoord tileCoord)
{
    if (!obj) return;

    UndoAction action;
    action.type = UndoAction::ObjectDelete;
    if (auto* image = dynamic_cast<ImageObject*>(obj)) {
        action.objectData = image->toJsonWithoutRecoveryData();
        action.objectImageEncodedData = image->encodedAssetData();
        action.objectImageFormat = image->assetFormat();
        if (action.objectImageEncodedData.isEmpty()) {
            action.objectImageSnapshot = image->pixmap().toImage();
        }
    } else {
        action.objectData = obj->toJson();
    }
    action.objectId = obj->id;
    if (m_document && m_document->isEdgeless()) {
        action.objectTileCoord = tileCoord;
    } else {
        action.objectPageIndex = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
    }
    pushUndoAction(action);
}

void DocumentViewport::pushObjectMoveUndo(InsertedObject* obj, const QPointF& oldPos,
                                          int pageIndex,
                                          Document::TileCoord oldTile,
                                          Document::TileCoord newTile,
                                          int oldPageIndex,
                                          int newPageIndex)
{
    if (!obj) return;

    UndoAction action;
    action.type = UndoAction::ObjectMove;
    action.objectId = obj->id;
    action.objectOldPosition = oldPos;
    action.objectNewPosition = obj->position;
    if (m_document && m_document->isEdgeless()) {
        action.objectOldTile = oldTile;
        action.objectNewTile = newTile;
        action.objectTileCoord = newTile;
    } else {
        int idx = (pageIndex >= 0) ? pageIndex : m_currentPageIndex;
        action.objectPageIndex = idx;
        action.objectOldPageIndex = (oldPageIndex >= 0) ? oldPageIndex : idx;
        action.objectNewPageIndex = (newPageIndex >= 0) ? newPageIndex : idx;
    }
    pushUndoAction(action);
}

void DocumentViewport::pushObjectResizeUndo(InsertedObject* obj,
                                            const QPointF& oldPos,
                                            const QSizeF& oldSize,
                                            qreal oldRotation,
                                            bool oldAspectLock,
                                            const TextBoxState* oldTextBoxState)
{
    if (!obj) return;

    UndoAction action;
    action.type = UndoAction::ObjectResize;
    action.objectId = obj->id;
    if (auto* image = dynamic_cast<ImageObject*>(obj)) {
        action.objectData = image->toJsonWithoutRecoveryData();
    } else {
        action.objectData = obj->toJson();
    }
    action.objectOldPosition = oldPos;
    action.objectNewPosition = obj->position;
    action.objectOldSize = oldSize;
    action.objectNewSize = obj->size;
    action.objectOldRotation = oldRotation;
    action.objectNewRotation = obj->rotation;
    action.objectOldAspectLock = oldAspectLock;
    if (oldTextBoxState) {
        if (auto* textBox = dynamic_cast<TextBoxObject*>(obj)) {
            action.objectHasTextBoxState = true;
            action.objectOldTextBoxState = *oldTextBoxState;
            action.objectNewTextBoxState = textBox->captureState();
        }
    }
    if (obj->type() == "image") {
        if (auto* img = dynamic_cast<ImageObject*>(obj))
            action.objectNewAspectLock = img->maintainAspectRatio;
    } else {
        action.objectNewAspectLock = oldAspectLock;
    }
    if (m_document && m_document->isEdgeless()) {
        action.objectOldTile = m_dragObjectTileCoord;
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                action.objectTileCoord = coord;
                action.objectNewTile = coord;
                break;
            }
        }
    } else {
        action.objectPageIndex = m_resizeObjectPageIndex >= 0
            ? m_resizeObjectPageIndex : m_currentPageIndex;
    }
    pushUndoAction(action);
}

void DocumentViewport::pushObjectAffinityUndo(InsertedObject* obj, int oldAffinity)
{
    if (!obj) return;

    UndoAction action;
    action.type = UndoAction::ObjectAffinityChange;
    action.objectId = obj->id;
    action.objectOldAffinity = oldAffinity;
    action.objectNewAffinity = obj->getLayerAffinity();
    if (m_document && m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                action.objectTileCoord = coord;
                break;
            }
        }
    } else {
        action.objectPageIndex = m_currentPageIndex;
    }
    pushUndoAction(action);
}

void DocumentViewport::pushObjectTextEditUndo(
    TextBoxObject* obj, const TextBoxState& oldState,
    const TextBoxState& newState, int pageIndex,
    Document::TileCoord oldTile, Document::TileCoord newTile)
{
    if (!obj)
        return;

    UndoAction action;
    action.type = UndoAction::ObjectTextEdit;
    action.objectId = obj->id;
    action.objectData = obj->toJson();
    action.objectHasTextBoxState = true;
    action.objectOldTextBoxState = oldState;
    action.objectNewTextBoxState = newState;
    action.objectPageIndex = pageIndex;
    action.objectOldTile = oldTile;
    action.objectNewTile = newTile;
    action.objectTileCoord = newTile;
    pushUndoAction(action);
}

void DocumentViewport::pushOcrLockUndo(const QVector<QString>& objectIds, bool newState)
{
    if (objectIds.isEmpty()) return;

    UndoAction action;
    action.type = UndoAction::OcrLockChange;
    action.ocrLockObjectIds = objectIds;
    action.ocrLockNewState = newState;
    if (m_document && m_document->isEdgeless()) {
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(objectIds.first())) {
                action.objectTileCoord = coord;
                break;
            }
        }
    } else {
        action.objectPageIndex = m_currentPageIndex;
    }
    pushUndoAction(action);
    emit documentModified();
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
// markLinkContainerDirtyAndRefreshOutline
// Centralised dirty-mark + outline-cache-refresh used by every code path that
// mutates a LinkObject's markdown slots. Keeping this in one helper prevents
// the "(missing note)" sidebar drift that previously occurred when one path
// (clearLinkSlot) forgot to refresh the outline cache after the others did.
// -----------------------------------------------------------------------------
void DocumentViewport::markLinkContainerDirtyAndRefreshOutline(LinkObject* link)
{
    if (!link || !m_document) return;

    Document::TileCoord tileCoord{};
    Page* page = findPageContainingObject(link, &tileCoord);
    if (!page) return;

    if (m_document->isEdgeless()) {
        m_document->markTileDirty(tileCoord);
        m_document->refreshLinkOutlineFor(tileCoord);
    } else {
        // Use cached UUID→index lookup (O(1) from Phase C.0.2)
        int pageIndex = m_document->pageIndexByUuid(page->uuid);
        if (pageIndex >= 0) {
            m_document->markPageDirty(pageIndex);
            m_document->refreshLinkOutlineFor(pageIndex);
        }
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

// ===== Performance Instrumentation =====

DocumentViewport::RasterBenchmark DocumentViewport::runRasterBenchmark() const
{
    // Enough samples to take a median without making the F10 keypress feel stuck;
    // at ~12 ms for the slowest case this is a fraction of a second in total.
    constexpr int kSamples = 5;
    
    // Median rather than mean: one scheduler preemption should not decide it.
    auto median = [](std::vector<qreal>& samples) {
        if (samples.empty()) {
            return 0.0;
        }
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    
    RasterBenchmark result;
    result.buildArch = QSysInfo::buildCpuArchitecture();
    result.runtimeArch = QSysInfo::currentCpuArchitecture();
    
    // Device pixels, so the byte count matches what a real pan frame moves.
    const qreal dpr = devicePixelRatioF();
    const QSize surface(qMax(64, qRound(width() * dpr)), qMax(64, qRound(height() * dpr)));
    const qsizetype bytes = qsizetype(surface.width()) * surface.height() * 4;
    result.megapixels = double(surface.width()) * surface.height() / 1e6;
    
    QElapsedTimer timer;
    
    // Raw memcpy: what the memory system allows with no Qt in the path at all.
    {
        std::vector<quint8> src(size_t(bytes), 0x5a);
        std::vector<quint8> dst(size_t(bytes), 0x00);
        std::vector<qreal> samples;
        for (int i = 0; i < kSamples; ++i) {
            timer.start();
            std::memcpy(dst.data(), src.data(), size_t(bytes));
            samples.push_back(timer.nsecsElapsed() / 1e6);
        }
        result.memcpyMs = median(samples);
    }
    
    // fillRect writes without reading a source, so a slow fill next to a healthy
    // memcpy points at Qt's code generation rather than at the copy loop.
    {
        QImage target(surface, QImage::Format_ARGB32_Premultiplied);
        std::vector<qreal> samples;
        for (int i = 0; i < kSamples; ++i) {
            QPainter painter(&target);
            timer.start();
            painter.fillRect(QRect(QPoint(0, 0), surface), QColor(40, 90, 160));
            samples.push_back(timer.nsecsElapsed() / 1e6);
        }
        result.fillRectMs = median(samples);
    }
    
    // The operation the gesture-pan fast path performs: unscaled, unclipped,
    // aligned, full-surface. Painters are built outside the timed region so
    // their setup and teardown stay out of the number.
    {
        QPixmap source(surface);
        source.fill(QColor(40, 90, 160));
        QPixmap target(surface);
        target.fill(Qt::white);
        std::vector<qreal> samples;
        for (int i = 0; i < kSamples; ++i) {
            QPainter painter(&target);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            timer.start();
            painter.drawPixmap(0, 0, source);
            samples.push_back(timer.nsecsElapsed() / 1e6);
        }
        result.blitMs = median(samples);
    }
    
    result.valid = true;
    return result;
}

void DocumentViewport::startBenchmark()
{
    m_perf.setEnabled(true);
    m_rasterBenchmark = runRasterBenchmark();
}

void DocumentViewport::stopBenchmark()
{
    m_perf.setEnabled(false);
}

int DocumentViewport::getPaintRate() const
{
    return m_perf.stats(ViewportPerfMonitor::Bucket::All).frames;
}

DocumentViewport::PerfContext DocumentViewport::perfContext() const
{
    PerfContext ctx;
    
    const qreal dpr = devicePixelRatioF();
    ctx.viewportLogical = size();
    ctx.viewportPhysical = QSize(qRound(width() * dpr), qRound(height() * dpr));
    ctx.devicePixelRatio = dpr;
    
    if (const QScreen* s = screen()) {
        ctx.screenRefreshRate = s->refreshRate();
    }
    
    ctx.strokeCacheTier = QStringLiteral("n/a");
    if (!m_document) {
        return ctx;
    }
    
    // Reproduce what renderPage/dispatchTileLayer would pick for the content
    // the user is currently looking at.
    QSizeF tileSize;
    QPointF tileOrigin;
    bool haveTile = false;
    
    if (m_document->isEdgeless()) {
        const int tilePx = Document::EDGELESS_TILE_SIZE;
        const QPointF center = visibleRect().center();
        tileOrigin = QPointF(std::floor(center.x() / tilePx) * tilePx,
                             std::floor(center.y() / tilePx) * tilePx);
        tileSize = QSizeF(tilePx, tilePx);
        haveTile = true;
    } else if (Page* page = m_document->page(currentPageIndex())) {
        tileOrigin = pagePosition(currentPageIndex());
        tileSize = page->size;
        haveTile = true;
    }
    
    if (haveTile) {
        switch (chooseRenderTier(tileSize, visibleRect().translated(-tileOrigin), nullptr)) {
        case VectorLayer::RenderTier::Capped:
            ctx.strokeCacheTier = QStringLiteral("Capped");
            break;
        case VectorLayer::RenderTier::Focus:
            ctx.strokeCacheTier = QStringLiteral("Focus");
            break;
        case VectorLayer::RenderTier::Direct:
            ctx.strokeCacheTier = QStringLiteral("Direct");
            break;
        }
    }
    
    return ctx;
}

// ===== Rendering Helpers (Task 1.3.3) =====

VectorLayer::RenderTier
DocumentViewport::chooseRenderTier(const QSizeF& tileSize,
                                   const QRectF& tileLocalViewport,
                                   QRectF* outFocusRect) const
{
    using Tier = VectorLayer::RenderTier;
    // Effective scale = how many physical screen pixels one logical unit
    // produces. The capped pixmap cache hits the divisor when the long
    // side at this scale exceeds MAX_STROKE_CACHE_DIM.
    const qreal effScale = m_zoomLevel * devicePixelRatioF();
    const qreal pageMaxDim = qMax(tileSize.width(), tileSize.height());
    const bool wouldBeBlurred =
        effScale * pageMaxDim > VectorLayer::MAX_STROKE_CACHE_DIM;

    const QRectF tileBounds(QPointF(0, 0), tileSize);
    // If the cap would not kick in at the current zoom, the legacy capped
    // pixmap is sharp and small; nothing to gain from the focus cache.
    // If the tile is entirely off-screen, the user can't see the blur, so
    // keep the capped cache (also: focus rect would be empty here).
    if (!wouldBeBlurred || !tileLocalViewport.intersects(tileBounds)) {
        return Tier::Capped;
    }

    if (outFocusRect) {
        *outFocusRect = tileLocalViewport.intersected(tileBounds);
    }
    return m_focusCacheSuspended ? Tier::Direct : Tier::Focus;
}

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
            // Render PDF page from cache (Task 1.3.6), resolving the page's own source.
            if (page->pdfPageNumber >= 0) {
                PdfProvider* prov = m_document->providerForSource(page->pdfSourceId);
                // Skip pages whose original number can't be served by the resolved
                // provider (e.g. a bundled source without the original PDF where the
                // page isn't in the mini-PDF's page map). Rendering would return null
                // and, since nulls aren't cached, retry on every repaint. Draw blank.
                const int resolvedPage = m_document->resolveSourcePageIndex(page->pdfSourceId, page->pdfPageNumber);
                if (prov && prov->isValid() && resolvedPage >= 0 && resolvedPage < prov->pageCount()) {
                    qreal dpi = effectivePdfDpi();
                    // SP2: never render synchronously while scrolling - draw the
                    // cached pixmap if present, else fall back to the page
                    // background (already filled above). The settle handler
                    // renders the final visible pages once scrolling stops.
                    QPixmap pdfPixmap = isScrolling()
                        ? lookupCachedPdfPage(page->pdfSourceId, page->pdfPageNumber, dpi)
                        : getCachedPdfPage(page->pdfSourceId, page->pdfPageNumber, dpi);
                    
                    if (!pdfPixmap.isNull()) {
                        // Scale pixmap to fit page rect
                        painter.drawPixmap(pageRect.toRect(), pdfPixmap);
                    }
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
    const QString suppressedTextObjectId =
        m_inlineEditSession.active
        && m_inlineEditSession.document == m_document
        && m_inlineEditSession.pageIndex == pageIndex
            ? m_inlineEditSession.objectId
            : QString();
    
    page->renderObjectsWithAffinity(
        painter, 1.0, -1, layer0Visible, objectExcludePtr,
        suppressedTextObjectId);
    
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
    
    // Pre-compute the tile-local viewport and tier choice once per page;
    // every layer on the same page gets the same tier and the same focus
    // rect (they share size and origin).
    const QPointF pageOrigin = pagePosition(pageIndex);
    const QRectF tileLocalVp = visibleRect().translated(-pageOrigin);
    QRectF focusRect;
    const VectorLayer::RenderTier tier =
        chooseRenderTier(pageSize, tileLocalVp, &focusRect);

    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        bool layerIsVisible = layer && layer->visible;
        
        if (layerIsVisible) {
            // When we won't draw from the capped pixmap this paint, free it
            // outright - holding a 4096^2 pixmap per layer per page burns
            // ~67 MB without serving any frame.
            if (tier != VectorLayer::RenderTier::Capped &&
                layer->hasStrokeCacheAllocated()) {
                layer->releaseStrokeCache();
            }

            // CR-2B-7: If this layer contains selected strokes, render with
            // exclusion so the originals don't double-render under the lasso
            // overlay. The tiered excluder also gets the high-zoom path under
            // viewport clipping (fixes the previous "DPI cap bypassed when
            // something is selected" symptom). Both dispatchers manage their
            // own painter save/restore, so no extra wrapping needed here.
            if (hasSelectionOnThisPage && layerIdx == m_lassoSelection.sourceLayerIndex) {
                layer->renderExcludingTiered(painter, excludeIds,
                                             pageSize, m_zoomLevel, dpr,
                                             tier, focusRect);
            } else {
                layer->renderTiered(painter, pageSize, m_zoomLevel, dpr,
                                    tier, focusRect);
            }
        }
        
        // Phase O3.5.8: Render objects with affinity = layerIdx
        // Objects with affinity K are tied to Layer K+1, so check visibility of Layer K+1
        VectorLayer* nextLayer = page->layer(layerIdx + 1);
        bool nextLayerVisible = nextLayer ? nextLayer->visible : true;  // If no next layer, show objects
        page->renderObjectsWithAffinity(
            painter, 1.0, layerIdx, nextLayerVisible,
            objectExcludePtr, suppressedTextObjectId);
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

void DocumentViewport::renderEdgelessMode(QPainter& painter, const QRect& dirtyRect)
{
    if (!m_document || !m_document->isEdgeless()) return;
    
    // Confine the tile walk to the damaged area. Qt clips the rasterization
    // either way, but every visible tile's background pattern, stroke layers and
    // objects were still being walked and issued each frame - so a live stroke,
    // whose dirty rect is a few pixels across, paid for the whole viewport.
    // Widened slightly so rounding cannot drop a tile that just reaches in.
    const QRectF dirtyF(dirtyRect);
    const qreal slack = 2.0 / m_zoomLevel;
    const QRectF dirtyDoc = QRectF(viewportToDocument(dirtyF.topLeft()),
                                   viewportToDocument(dirtyF.bottomRight()))
                                .adjusted(-slack, -slack, slack, slack);
    const QRectF viewRect = visibleRect().intersected(dirtyDoc);
    
    // ========== TILE RENDERING STRATEGY ==========
    // With stroke splitting, cross-tile strokes are stored as separate segments in each tile.
    // Each segment is rendered when its tile is rendered - no margin needed for cross-tile!
    // Small margin handles thick strokes extending slightly beyond tile boundary.
    // CR-9: EDGELESS_STROKE_MARGIN is max expected stroke width + anti-aliasing buffer
    
    // Phase O1.5: Object margin - objects can extend beyond tile boundaries
    // Calculate extra margin based on largest object in document
    int objectMargin = m_document->maxObjectExtent();
    
    // Total margin is max of stroke margin and object margin
    int totalMargin = qMax(EDGELESS_STROKE_MARGIN, objectMargin);
    
    // CR-5: Single tilesInRect() call - use total margin for all tiles
    // Background pass will filter to viewRect bounds
    // An empty viewRect means nothing damaged intersects the canvas; leaving the
    // tile list empty skips every tile pass below while the overlays and the
    // in-progress stroke at the end of this function still run.
    QVector<Document::TileCoord> allTiles;
    if (!viewRect.isEmpty()) {
        QRectF strokeRect = viewRect.adjusted(-totalMargin, -totalMargin, totalMargin, totalMargin);
        allTiles = m_document->tilesInRect(strokeRect);
    }
    
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
            renderTileLayerStrokes(painter, tile, layerIdx, coord);
            painter.restore();
        }
        
        // PASS 3b: Render objects with affinity = layerIdx
        renderEdgelessObjectsWithAffinity(painter, layerIdx, allTiles);
    }
    
    // Render text selection overlay (Highlighter tool) in edgeless mode.
    // Cache holds document-space rects, so the active view transform maps
    // them to the screen correctly. pageIndex = -1 signals edgeless.
    if (m_currentTool == ToolType::Highlighter) {
        renderTextSelectionOverlay(painter, -1);
    }

    // Render search match highlights in edgeless mode
    renderSearchMatchesOverlayEdgeless(painter);

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
    
    // Task 2.10: Draw lasso selection path (edgeless mode, regular lasso or eraser lasso)
    // P1: Use incremental rendering for O(1) per frame instead of O(n)
    if ((m_isDrawingLasso || m_isDrawingEraserLasso) && m_lassoPath.size() > 1) {
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

void DocumentViewport::dispatchTileLayer(QPainter& painter, VectorLayer* layer,
                                         int layerIdx,
                                         const QSizeF& tileSize,
                                         Document::TileCoord coord, qreal dpr,
                                         const QSet<QString>& excludeIds)
{
    if (!layer || !layer->visible) return;

    // Tile-local viewport for the tier dispatcher. The painter is already
    // translated to the tile origin (in document coords); we just need to
    // express the visible rect in the same tile-local frame.
    const int tilePx = Document::EDGELESS_TILE_SIZE;
    const QPointF tileOrigin(coord.first * tilePx, coord.second * tilePx);
    const QRectF tileLocalVp = visibleRect().translated(-tileOrigin);
    QRectF focusRect;
    const VectorLayer::RenderTier tier =
        chooseRenderTier(tileSize, tileLocalVp, &focusRect);

    if (tier != VectorLayer::RenderTier::Capped &&
        layer->hasStrokeCacheAllocated()) {
        layer->releaseStrokeCache();
    }

    // CR-2B-7: If there's a selection on the active layer, exclude selected
    // strokes. The lasso source layer goes through the tiered excluder so
    // the high-zoom path also respects the focus rect (and the previous
    // unbounded `renderExcluding` no longer bypasses the DPI cap).
    const bool isLassoSourceLayer =
        !excludeIds.isEmpty() && layerIdx == m_edgelessActiveLayerIndex;
    if (isLassoSourceLayer) {
        layer->renderExcludingTiered(painter, excludeIds,
                                     tileSize, m_zoomLevel, dpr,
                                     tier, focusRect);
    } else {
        layer->renderTiered(painter, tileSize, m_zoomLevel, dpr,
                            tier, focusRect);
    }
}

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
        dispatchTileLayer(painter, tile->layer(layerIdx), layerIdx,
                          tileSize, coord, dpr, excludeIds);
    }
    
    // NOTE: Objects are now rendered via renderEdgelessObjectsWithAffinity()
    // in the multi-pass rendering loop, not here.
    // tile->renderObjects(painter, 1.0);  // REMOVED - handled by multi-pass
}

void DocumentViewport::renderTileLayerStrokes(QPainter& painter, Page* tile,
                                              int layerIdx,
                                              Document::TileCoord coord)
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

    dispatchTileLayer(painter, layer, layerIdx, tileSize, coord, dpr, excludeIds);
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
            if (m_inlineEditSession.active
                && m_inlineEditSession.document == m_document
                && m_inlineEditSession.tileCoord == coord
                && m_inlineEditSession.objectId == obj->id
                && obj->type() == QLatin1String("textbox")) {
                static_cast<TextBoxObject*>(obj)
                    ->renderWithTextSuppressed(painter, 1.0);
            } else {
                obj->render(painter, 1.0);
            }
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
