#pragma once

// ============================================================================
// DocumentViewport - The main canvas widget for displaying documents
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3)
//
// DocumentViewport is a QWidget that:
// - Displays pages from a Document with pan/zoom
// - Routes input to the correct page
// - Manages caching for smooth scrolling
// - Communicates with MainWindow via signals
//
// Replaces: InkCanvas (view/input portions)
// ============================================================================

#include "Document.h"
#include "Page.h"

#include <QWidget>
#include <QPointF>
#include <QSizeF>
#include <QRectF>
#include <QVector>

// Forward declarations
class QPaintEvent;
class QResizeEvent;
class QMouseEvent;
class QTabletEvent;
class QWheelEvent;

/**
 * @brief Layout mode for page arrangement.
 */
enum class LayoutMode {
    SingleColumn,   ///< Vertical scroll, 1 page wide (default)
    TwoColumn       ///< Vertical scroll, 2 pages side-by-side
    // Future: Grid, Horizontal, etc.
};

/**
 * @brief Result of viewport-to-page coordinate conversion.
 */
struct PageHit {
    int pageIndex = -1;     ///< Page index, or -1 if no page hit
    QPointF pagePoint;      ///< Point in page-local coordinates
    
    bool valid() const { return pageIndex >= 0; }
};

/**
 * @brief Cache entry for a rendered PDF page (Task 1.3.6).
 */
struct PdfCacheEntry {
    int pageIndex = -1;     ///< Which page this is (-1 = invalid)
    qreal dpi = 0;          ///< DPI at which it was rendered
    QPixmap pixmap;         ///< The rendered PDF image
    
    bool isValid() const { return pageIndex >= 0 && !pixmap.isNull(); }
    bool matches(int page, qreal targetDpi) const {
        return pageIndex == page && qFuzzyCompare(dpi, targetDpi);
    }
};

/**
 * @brief Unified pointer event for all input types (Task 1.3.8).
 * 
 * This abstracts mouse, tablet, and single-touch input into a common format.
 * Multi-touch gestures are handled separately by GestureState.
 */
struct PointerEvent {
    enum Type { Press, Move, Release };
    enum Source { Mouse, Stylus, Touch, Unknown };
    
    Type type = Move;
    Source source = Unknown;
    
    QPointF viewportPos;      ///< Position in widget/viewport coordinates
    PageHit pageHit;          ///< Resolved page index + page-local coords
    
    // Pressure-sensitive input (mouse defaults to 1.0)
    qreal pressure = 1.0;     ///< 0.0 to 1.0
    qreal tiltX = 0;          ///< Stylus tilt X (-90 to 90 degrees)
    qreal tiltY = 0;          ///< Stylus tilt Y (-90 to 90 degrees)
    qreal rotation = 0;       ///< Stylus rotation (0 to 360 degrees)
    
    // Hardware state
    bool isEraser = false;    ///< True if using eraser end OR eraser button
    int stylusButtons = 0;    ///< Barrel button bitmask
    Qt::MouseButtons buttons; ///< Mouse/stylus buttons
    Qt::KeyboardModifiers modifiers;  ///< Keyboard modifiers (Ctrl, Shift, etc.)
    
    // Timestamp for velocity calculations
    qint64 timestamp = 0;
};

/**
 * @brief State for multi-touch gesture recognition (Task 1.3.8 stub).
 * 
 * Full implementation comes in Phase 2/4.
 */
struct GestureState {
    enum Type { None, Pan, PinchZoom, TwoFingerTap };
    Type activeGesture = None;
    
    QPointF panDelta;         ///< Accumulated pan delta
    qreal zoomFactor = 1.0;   ///< Pinch zoom factor
    QPointF zoomCenter;       ///< Center point of pinch
    
    // Inertia scrolling (future)
    QPointF velocity;
    bool inertiaActive = false;
    
    void reset() {
        activeGesture = None;
        panDelta = QPointF();
        zoomFactor = 1.0;
        zoomCenter = QPointF();
        velocity = QPointF();
        inertiaActive = false;
    }
};

/**
 * @brief The main canvas widget for displaying and interacting with documents.
 * 
 * DocumentViewport handles:
 * - Rendering pages with backgrounds, PDF content, strokes, and objects
 * - Pan and zoom transforms
 * - Routing input events to the correct page
 * - Managing caches for smooth scrolling
 * 
 * One DocumentViewport instance per tab (each tab has its own view state).
 */
class DocumentViewport : public QWidget {
    Q_OBJECT
    
public:
    // ===== Constructor & Destructor =====
    
    /**
     * @brief Construct a new DocumentViewport.
     * @param parent Parent widget (typically the tab container).
     */
    explicit DocumentViewport(QWidget* parent = nullptr);
    
    /**
     * @brief Destructor.
     */
    ~DocumentViewport() override;
    
    // ===== Document Management =====
    
    /**
     * @brief Set the document to display.
     * @param doc Pointer to the document (not owned, must outlive viewport).
     * 
     * Resets view state (pan, zoom) and triggers repaint.
     * Pass nullptr to clear the document.
     */
    void setDocument(Document* doc);
    
    /**
     * @brief Get the currently displayed document.
     * @return Pointer to the document, or nullptr if none set.
     */
    Document* document() const { return m_document; }
    
    // ===== View State Getters =====
    
    /**
     * @brief Get the current zoom level.
     * @return Zoom level (1.0 = 100%, 2.0 = 200%, etc.)
     */
    qreal zoomLevel() const { return m_zoomLevel; }
    
    /**
     * @brief Get the current pan offset.
     * @return Pan offset in document coordinates.
     */
    QPointF panOffset() const { return m_panOffset; }
    
    /**
     * @brief Get the index of the "current" page (most visible or centered).
     * @return 0-based page index.
     */
    int currentPageIndex() const { return m_currentPageIndex; }
    
    // ===== Layout =====
    
    /**
     * @brief Get the current layout mode.
     */
    LayoutMode layoutMode() const { return m_layoutMode; }
    
    /**
     * @brief Set the layout mode.
     * @param mode New layout mode.
     */
    void setLayoutMode(LayoutMode mode);
    
    /**
     * @brief Get the gap between pages in pixels.
     */
    int pageGap() const { return m_pageGap; }
    
    /**
     * @brief Set the gap between pages.
     * @param gap Gap in pixels.
     */
    void setPageGap(int gap);
    
    // ===== Layout Engine (Task 1.3.2) =====
    
    /**
     * @brief Get the position of a page in document coordinates.
     * @param pageIndex 0-based page index.
     * @return Top-left corner of the page in document coordinates.
     */
    QPointF pagePosition(int pageIndex) const;
    
    /**
     * @brief Get the full rectangle of a page in document coordinates.
     * @param pageIndex 0-based page index.
     * @return Rectangle including position and size.
     */
    QRectF pageRect(int pageIndex) const;
    
    /**
     * @brief Get the total size of all pages (bounding box).
     * @return Size of the content area containing all pages.
     */
    QSizeF totalContentSize() const;
    
    /**
     * @brief Find which page contains a point in document coordinates.
     * @param documentPt Point in document coordinates.
     * @return Page index, or -1 if point is not on any page.
     */
    int pageAtPoint(QPointF documentPt) const;
    
    /**
     * @brief Get the list of pages currently visible in the viewport.
     * @return Vector of page indices that intersect the viewport.
     */
    QVector<int> visiblePages() const;
    
    /**
     * @brief Get the visible rectangle in document coordinates.
     * @return The area of the document currently visible in the viewport.
     */
    QRectF visibleRect() const;
    
    // ===== Coordinate Transforms (Task 1.3.5) =====
    
    /**
     * @brief Convert viewport pixel coordinates to document coordinates.
     * @param viewportPt Point in viewport/widget coordinates (logical pixels).
     * @return Point in document coordinates.
     * 
     * This is the inverse of documentToViewport().
     */
    QPointF viewportToDocument(QPointF viewportPt) const;
    
    /**
     * @brief Convert document coordinates to viewport pixel coordinates.
     * @param docPt Point in document coordinates.
     * @return Point in viewport/widget coordinates (logical pixels).
     * 
     * This is the inverse of viewportToDocument().
     */
    QPointF documentToViewport(QPointF docPt) const;
    
    /**
     * @brief Convert viewport pixel coordinates to page-local coordinates.
     * @param viewportPt Point in viewport/widget coordinates.
     * @return PageHit containing page index and page-local coordinates.
     * 
     * Returns invalid PageHit (pageIndex=-1) if point is not on any page.
     */
    PageHit viewportToPage(QPointF viewportPt) const;
    
    /**
     * @brief Convert page-local coordinates to viewport pixel coordinates.
     * @param pageIndex The page index.
     * @param pagePt Point in page-local coordinates.
     * @return Point in viewport/widget coordinates.
     */
    QPointF pageToViewport(int pageIndex, QPointF pagePt) const;
    
    /**
     * @brief Convert page-local coordinates to document coordinates.
     * @param pageIndex The page index.
     * @param pagePt Point in page-local coordinates.
     * @return Point in document coordinates.
     */
    QPointF pageToDocument(int pageIndex, QPointF pagePt) const;
    
    /**
     * @brief Convert document coordinates to page-local coordinates.
     * @param docPt Point in document coordinates.
     * @return PageHit containing page index and page-local coordinates.
     * 
     * Returns invalid PageHit (pageIndex=-1) if point is not on any page.
     */
    PageHit documentToPage(QPointF docPt) const;
    
    // ===== View State Setters (Slots) =====
    
public slots:
    /**
     * @brief Set the zoom level.
     * @param zoom New zoom level (clamped to valid range).
     */
    void setZoomLevel(qreal zoom);
    
    /**
     * @brief Set the pan offset.
     * @param offset New pan offset in document coordinates.
     */
    void setPanOffset(QPointF offset);
    
    /**
     * @brief Scroll to make a specific page visible.
     * @param pageIndex 0-based page index.
     */
    void scrollToPage(int pageIndex);
    
    /**
     * @brief Scroll by a delta amount.
     * @param delta Scroll delta in document coordinates.
     */
    void scrollBy(QPointF delta);
    
    /**
     * @brief Zoom to fit the entire document in the viewport.
     */
    void zoomToFit();
    
    /**
     * @brief Zoom to fit the page width in the viewport.
     */
    void zoomToWidth();
    
    /**
     * @brief Scroll to the home position (origin).
     */
    void scrollToHome();
    
    /**
     * @brief Set horizontal scroll position as a fraction (0.0 to 1.0).
     * @param fraction Scroll fraction.
     */
    void setHorizontalScrollFraction(qreal fraction);
    
    /**
     * @brief Set vertical scroll position as a fraction (0.0 to 1.0).
     * @param fraction Scroll fraction.
     */
    void setVerticalScrollFraction(qreal fraction);
    
signals:
    // ===== View State Signals =====
    
    /**
     * @brief Emitted when the zoom level changes.
     * @param zoom New zoom level.
     */
    void zoomChanged(qreal zoom);
    
    /**
     * @brief Emitted when the pan offset changes.
     * @param offset New pan offset.
     */
    void panChanged(QPointF offset);
    
    /**
     * @brief Emitted when the current page changes.
     * @param pageIndex New current page index.
     */
    void currentPageChanged(int pageIndex);
    
    /**
     * @brief Emitted when the document is modified.
     */
    void documentModified();
    
    /**
     * @brief Emitted when horizontal scroll position changes.
     * @param fraction Scroll position as fraction (0.0 to 1.0).
     */
    void horizontalScrollChanged(qreal fraction);
    
    /**
     * @brief Emitted when vertical scroll position changes.
     * @param fraction Scroll position as fraction (0.0 to 1.0).
     */
    void verticalScrollChanged(qreal fraction);
    
protected:
    // ===== Qt Event Overrides =====
    
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    
private:
    // ===== Document Reference =====
    Document* m_document = nullptr;
    
    // ===== View State =====
    qreal m_zoomLevel = 1.0;
    QPointF m_panOffset;
    int m_currentPageIndex = 0;
    
    // ===== Layout =====
    LayoutMode m_layoutMode = LayoutMode::SingleColumn;
    int m_pageGap = 20;  // Pixels between pages
    
    // ===== PDF Cache (Task 1.3.6) =====
    QVector<PdfCacheEntry> m_pdfCache;
    int m_pdfCacheCapacity = 2;  // Default for single column
    qreal m_cachedDpi = 0;       // DPI at which cache was rendered
    
    // ===== Zoom Limits =====
    static constexpr qreal MIN_ZOOM = 0.1;   // 10%
    static constexpr qreal MAX_ZOOM = 10.0;  // 1000%
    
    // ===== Input State (Task 1.3.8) =====
    int m_activeDrawingPage = -1;       ///< Page currently receiving strokes (-1 = none)
    bool m_pointerActive = false;       ///< True if pointer is pressed
    PointerEvent::Source m_activeSource = PointerEvent::Unknown;  ///< Active input source
    GestureState m_gestureState;        ///< Multi-touch gesture state
    QPointF m_lastPointerPos;           ///< Last pointer position (for delta calculation)
    
    // ===== Private Methods =====
    
    /**
     * @brief Clamp pan offset to valid bounds.
     */
    void clampPanOffset();
    
    /**
     * @brief Update the current page index based on pan position.
     */
    void updateCurrentPageIndex();
    
    /**
     * @brief Emit scroll fraction signals.
     */
    void emitScrollFractions();
    
    // ===== Pan & Zoom Helpers (Task 1.3.4) =====
    
    /**
     * @brief Get the viewport center point in document coordinates.
     * @return Center of viewport in document space.
     */
    QPointF viewportCenter() const;
    
    /**
     * @brief Zoom at a specific point, keeping that point stationary.
     * @param newZoom The new zoom level.
     * @param viewportPt The point in viewport coordinates to keep fixed.
     * 
     * Used for zoom-towards-cursor behavior with mouse wheel.
     */
    void zoomAtPoint(qreal newZoom, QPointF viewportPt);
    
    // ===== PDF Cache Helpers (Task 1.3.6) =====
    
    /**
     * @brief Get a cached PDF page pixmap, rendering if necessary.
     * @param pageIndex The page index.
     * @param dpi The target DPI.
     * @return Cached or freshly rendered pixmap (may be null if not a PDF page).
     */
    QPixmap getCachedPdfPage(int pageIndex, qreal dpi);
    
    /**
     * @brief Pre-load PDF pages around the visible area.
     * Called after scroll settles to ensure smooth scrolling.
     */
    void preloadPdfCache();
    
    /**
     * @brief Invalidate the entire PDF cache.
     * Called when zoom changes (DPI changed) or document changes.
     */
    void invalidatePdfCache();
    
    /**
     * @brief Invalidate a single page in the PDF cache.
     * @param pageIndex The page to invalidate.
     */
    void invalidatePdfCachePage(int pageIndex);
    
    /**
     * @brief Update cache capacity based on layout mode.
     */
    void updatePdfCacheCapacity();
    
    // ===== Stroke Cache Helpers (Task 1.3.7) =====
    
    /**
     * @brief Pre-load stroke caches for nearby pages.
     * Call after scroll settles for smooth scrolling.
     */
    void preloadStrokeCaches();
    
    // ===== Input Routing (Task 1.3.8) =====
    
    /**
     * @brief Convert QMouseEvent to PointerEvent.
     */
    PointerEvent mouseToPointerEvent(QMouseEvent* event, PointerEvent::Type type);
    
    /**
     * @brief Convert QTabletEvent to PointerEvent.
     */
    PointerEvent tabletToPointerEvent(QTabletEvent* event, PointerEvent::Type type);
    
    /**
     * @brief Main pointer event handler.
     * Routes to the correct page and handles the input.
     */
    void handlePointerEvent(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer press (start of stroke or action).
     */
    void handlePointerPress(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer move (continuing stroke).
     */
    void handlePointerMove(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer release (end of stroke).
     */
    void handlePointerRelease(const PointerEvent& pe);
    
    // ===== Rendering Helpers (Task 1.3.3) =====
    
    /**
     * @brief Render a single page (background + content).
     * @param painter The QPainter to render to.
     * @param page The page to render.
     * @param pageIndex The page index (for PDF pages).
     * 
     * Assumes painter is already translated to page position.
     * Handles solid color, grid, lines, and PDF backgrounds.
     */
    void renderPage(QPainter& painter, Page* page, int pageIndex);
    
    /**
     * @brief Get the effective DPI for rendering PDF at current zoom.
     * @return DPI value scaled by zoom level.
     */
    qreal effectivePdfDpi() const;
    
    /**
     * @brief Whether to show debug overlay.
     */
    bool m_showDebugOverlay = true;
};
