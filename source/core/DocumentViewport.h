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
#include "ToolType.h"
#include "../strokes/VectorStroke.h"
#include <QStack>
#include <QMap>

// ============================================================================
// PageUndoAction - Represents a single undoable action (Task 2.5)
// ============================================================================

/**
 * @brief Represents a single undoable action for per-page undo/redo.
 * 
 * Named PageUndoAction to avoid conflict with VectorCanvas::UndoAction
 * (which will be removed in Phase 5).
 */
struct PageUndoAction {
    enum Type { 
        AddStroke,       ///< A stroke was added (undo = remove it)
        RemoveStroke,    ///< A single stroke was removed (undo = add it back)
        RemoveMultiple   ///< Multiple strokes removed at once (undo = add all back)
    };
    
    Type type;
    int pageIndex;                      ///< The page this action occurred on
    VectorStroke stroke;                ///< For AddStroke and RemoveStroke
    QVector<VectorStroke> strokes;      ///< For RemoveMultiple
};

// ============================================================================
// EdgelessUndoAction - Undo action for edgeless mode (Phase E6)
// ============================================================================

/**
 * @brief Represents an undoable action in edgeless mode.
 * 
 * Unlike paged mode, edgeless strokes can span multiple tiles. This struct
 * stores all affected segments so the entire action can be undone atomically.
 * 
 * Memory bound: MAX_UNDO_EDGELESS actions × ~20KB avg = ~2MB max
 */
struct EdgelessUndoAction {
    PageUndoAction::Type type = PageUndoAction::AddStroke;  ///< AddStroke or RemoveStroke
    int layerIndex = 0;                 ///< Which layer was affected
    
    /**
     * @brief A stroke segment with its tile coordinate.
     * 
     * When a stroke spans tiles, it's split into segments. Each segment
     * is stored with its tile coordinate for correct undo/redo application.
     */
    struct StrokeSegment {
        Document::TileCoord tileCoord;  ///< Tile containing this segment
        VectorStroke stroke;            ///< The stroke data (in tile-local coords)
    };
    QVector<StrokeSegment> segments;    ///< All segments in this action
};

#include <QWidget>
#include <QPointF>
#include <QSizeF>
#include <QRectF>
#include <QVector>
#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QMutex>
#include <QFutureWatcher>
#include <deque>

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
        // Note: qFuzzyCompare doesn't work well near 0, so use relative comparison
        if (pageIndex != page) return false;
        if (dpi == 0 || targetDpi == 0) return dpi == targetDpi;
        return qFuzzyCompare(dpi, targetDpi);
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
    Qt::MouseButtons buttons = Qt::NoButton;  ///< Mouse/stylus buttons
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;  ///< Keyboard modifiers (Ctrl, Shift, etc.)
    
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
    
    // Allow test class to access private members
    friend class DocumentViewportTests;
    
public:
    // ===== Handle Hit Types (for lasso selection) =====
    /**
     * @brief Handle hit types for lasso selection transform.
     */
    enum class HandleHit {
        None,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight,
        Rotate,   ///< Rotation handle above top center
        Inside    ///< Inside bounding box (for move)
    };
    
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
    
    // ===== Tool Management (Task 2.1) =====
    
    /**
     * @brief Set the current drawing tool.
     * @param tool The tool to use (Pen, Marker, Eraser, Highlighter, Lasso)
     */
    void setCurrentTool(ToolType tool);
    
    /**
     * @brief Get the current drawing tool.
     */
    ToolType currentTool() const { return m_currentTool; }
    
    /**
     * @brief Set the pen color for drawing.
     * @param color The color to use.
     */
    void setPenColor(const QColor& color);
    
    /**
     * @brief Get the current pen color.
     */
    QColor penColor() const { return m_penColor; }
    
    /**
     * @brief Set the pen thickness for drawing.
     * @param thickness Thickness in document units.
     */
    void setPenThickness(qreal thickness);
    
    /**
     * @brief Get the current pen thickness.
     */
    qreal penThickness() const { return m_penThickness; }
    
    /**
     * @brief Set the eraser size.
     * @param size Eraser radius in document units.
     */
    void setEraserSize(qreal size);
    
    /**
     * @brief Get the current eraser size.
     */
    qreal eraserSize() const { return m_eraserSize; }
    
    // ===== Marker Tool (Task 2.8) =====
    
    /**
     * @brief Set the marker color.
     * @param color The color to use (alpha channel sets opacity).
     * 
     * Marker has a separate color from pen. Default is #E6FF6E at 50% opacity.
     * The alpha channel in the color controls the marker opacity.
     */
    void setMarkerColor(const QColor& color);
    
    /**
     * @brief Get the current marker color (including opacity in alpha).
     */
    QColor markerColor() const { return m_markerColor; }
    
    /**
     * @brief Set the marker thickness.
     * @param thickness Thickness in document units.
     * 
     * Marker thickness is fixed (no pressure sensitivity).
     */
    void setMarkerThickness(qreal thickness);
    
    /**
     * @brief Get the current marker thickness.
     */
    qreal markerThickness() const { return m_markerThickness; }
    
    // ===== Straight Line Mode (Task 2.9) =====
    
    /**
     * @brief Enable or disable straight line mode.
     * When enabled, Pen and Marker strokes are constrained to straight lines.
     */
    void setStraightLineMode(bool enabled);
    
    /**
     * @brief Check if straight line mode is enabled.
     */
    bool straightLineMode() const { return m_straightLineMode; }
    
    // ===== Undo/Redo (Task 2.5) =====
    
    /**
     * @brief Undo the last action on the current page.
     */
    void undo();
    
    /**
     * @brief Redo the last undone action on the current page.
     */
    void redo();
    
    /**
     * @brief Check if undo is available for the current page.
     */
    bool canUndo() const;
    
    /**
     * @brief Check if redo is available for the current page.
     */
    bool canRedo() const;
    
    /**
     * @brief Clear undo/redo stacks for pages >= pageIndex.
     * 
     * Used when inserting/deleting pages to prevent stale undo history
     * from being applied to wrong pages. Preserves undo for pages before
     * the affected index.
     * 
     * @param pageIndex First page index to clear (inclusive)
     */
    void clearUndoStacksFrom(int pageIndex);
    
    // ===== Layer Management (Phase 5) =====
    
    /**
     * @brief Set the active layer index for edgeless mode.
     * @param layerIndex The layer index to draw on.
     * 
     * In edgeless mode, this is a global setting - all tiles share
     * the same active layer. In paged mode, use Page::activeLayerIndex instead.
     */
    void setEdgelessActiveLayerIndex(int layerIndex);
    
    /**
     * @brief Get the active layer index for edgeless mode.
     * @return The current active layer index.
     */
    int edgelessActiveLayerIndex() const { return m_edgelessActiveLayerIndex; }
    
    // ===== Benchmark (Task 2.6) =====
    
    /**
     * @brief Start measuring paint refresh rate.
     * 
     * Call this to begin tracking how often paintEvent is called.
     * Use getPaintRate() to retrieve the current rate.
     */
    void startBenchmark();
    
    /**
     * @brief Stop measuring paint refresh rate.
     */
    void stopBenchmark();
    
    /**
     * @brief Get the current paint refresh rate.
     * @return Paints per second (based on last 1 second of data).
     */
    int getPaintRate() const;
    
    /**
     * @brief Check if benchmarking is currently active.
     */
    bool isBenchmarking() const { return m_benchmarking; }
    
    /**
     * @brief Check if the hardware eraser (stylus eraser end) is active.
     */
    bool isHardwareEraserActive() const { return m_hardwareEraserActive; }
    
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
    
    // ===== Document Change Notifications =====
    
    /**
     * @brief Notify viewport that document structure changed (pages added/removed/resized).
     * 
     * Call this after modifying Document's page list (addPage, removePage, etc.).
     * Invalidates layout cache and triggers repaint.
     */
    void notifyDocumentStructureChanged();
    
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
    
    /**
     * @brief Emit scroll fraction signals for current state.
     * 
     * Call this to sync external UI (e.g., scrollbars) with current viewport state.
     * Emits horizontalScrollChanged() and verticalScrollChanged() signals.
     */
    void syncScrollState() { emitScrollFractions(); }
    
    // ===== Zoom Gesture API (for deferred zoom rendering) =====
    // These methods can be called by any input source (Ctrl+wheel, touch pinch, etc.)
    
    /**
     * @brief Begin a zoom gesture.
     * @param centerPoint The zoom center point in viewport coordinates.
     * 
     * Captures a snapshot of the current viewport for fast scaling during the gesture.
     * If already in a gesture, this call is ignored.
     */
    void beginZoomGesture(QPointF centerPoint);
    
    /**
     * @brief Update the zoom gesture with a new scale factor.
     * @param scaleFactor Multiplicative scale factor (1.0 = no change, 1.1 = 10% zoom in).
     * @param centerPoint The zoom center point in viewport coordinates.
     * 
     * If not in a gesture, automatically calls beginZoomGesture first.
     * The scaleFactor is accumulated multiplicatively for smooth zooming.
     */
    void updateZoomGesture(qreal scaleFactor, QPointF centerPoint);
    
    /**
     * @brief End the zoom gesture and apply the final zoom level.
     * 
     * Re-renders the viewport at the correct DPI for the new zoom level.
     * If not in a gesture, this call is ignored.
     */
    void endZoomGesture();
    
    /**
     * @brief Check if a zoom gesture is currently active.
     * @return True if in a zoom gesture.
     */
    bool isZoomGestureActive() const { return m_gesture.activeType == ViewportGestureState::Zoom; }
    
    // ===== Deferred Pan Gesture API =====
    // These methods can be called by any input source (Shift/Alt+wheel, touch pan, etc.)
    
    /**
     * @brief Begin a pan gesture.
     * 
     * Captures a snapshot of the current viewport for fast shifting during the gesture.
     * If already in a gesture, this call is ignored.
     */
    void beginPanGesture();
    
    /**
     * @brief Update the pan gesture with a pan delta.
     * @param panDelta The pan offset to add in document coordinates.
     * 
     * If not in a gesture, automatically calls beginPanGesture first.
     * The delta is accumulated for smooth panning.
     */
    void updatePanGesture(QPointF panDelta);
    
    /**
     * @brief End the pan gesture and apply the final pan offset.
     * 
     * Re-renders the viewport at the correct position.
     * If not in a gesture, this call is ignored.
     */
    void endPanGesture();
    
    /**
     * @brief Check if a pan gesture is currently active.
     * @return True if in a pan gesture.
     */
    bool isPanGestureActive() const { return m_gesture.activeType == ViewportGestureState::Pan; }
    
    /**
     * @brief Check if any viewport gesture (zoom or pan) is currently active.
     * @return True if in any gesture.
     */
    bool isGestureActive() const { return m_gesture.isActive(); }
    
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
     * @brief Emitted when the current tool changes.
     * @param tool New tool type.
     */
    void toolChanged(ToolType tool);
    
    /**
     * @brief Emitted when straight line mode is toggled.
     * @param enabled True if straight line mode is now enabled.
     */
    void straightLineModeChanged(bool enabled);
    
    /**
     * @brief Emitted when undo availability changes for current page.
     * @param available True if undo is now available.
     */
    void undoAvailableChanged(bool available);
    
    /**
     * @brief Emitted when redo availability changes for current page.
     * @param available True if redo is now available.
     */
    void redoAvailableChanged(bool available);
    
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
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    
private:
    // ===== Document Reference =====
    Document* m_document = nullptr;
    
    // ===== View State =====
    qreal m_zoomLevel = 1.0;
    QPointF m_panOffset;
    int m_currentPageIndex = 0;
    
    // =========================================================================
    // CUSTOMIZABLE VALUES
    // =========================================================================
    // These values should eventually come from user settings (ControlPanelDialog).
    // TODO: Load from QSettings or a Settings class in Phase 4.
    // =========================================================================
    
    // ----- Layout Settings -----
    LayoutMode m_layoutMode = LayoutMode::SingleColumn;
    int m_pageGap = 20;  ///< CUSTOMIZABLE: Pixels between pages (range: 0-100)
    
    // ----- Zoom Limits -----
    /// CUSTOMIZABLE: Minimum zoom level (power user setting, range: 0.05-0.5)
    static constexpr qreal MIN_ZOOM = 0.1;   // 10%
    /// CUSTOMIZABLE: Maximum zoom level (power user setting, range: 5.0-20.0)
    static constexpr qreal MAX_ZOOM = 10.0;  // 1000%
    
    // ----- Tool Defaults -----
    // These are initial values; MainWindow will set them from user preferences.
    ToolType m_currentTool = ToolType::Pen;
    QColor m_penColor = Qt::black;    ///< CUSTOMIZABLE: Default pen color (user preference)
    qreal m_penThickness = 5.0;       ///< CUSTOMIZABLE: Default pen thickness (range: 1-50 document units)
    qreal m_eraserSize = 20.0;        ///< CUSTOMIZABLE: Default eraser radius (range: 5-100 document units)
    
    // Marker tool settings (Task 2.8)
    QColor m_markerColor = QColor(0xE6, 0xFF, 0x6E, 128);  ///< CUSTOMIZABLE: Default marker color (#E6FF6E at 50% opacity)
    qreal m_markerThickness = 8.0;    ///< CUSTOMIZABLE: Default marker thickness (wider than pen, no pressure)
    
    // Straight line mode (Task 2.9)
    bool m_straightLineMode = false;        ///< Whether straight line mode is enabled
    bool m_isDrawingStraightLine = false;   ///< Currently drawing a straight line
    QPointF m_straightLineStart;            ///< Start point (document coords for edgeless, page coords for paged)
    QPointF m_straightLinePreviewEnd;       ///< Current preview end point
    int m_straightLinePageIndex = -1;       ///< Page index for paged mode straight line
    
    // Lasso Selection Tool (Task 2.10)
    struct LassoSelection {
        QVector<VectorStroke> selectedStrokes;  ///< Copies of selected strokes
        QVector<int> originalIndices;            ///< Indices in the layer (for removal)
        int sourcePageIndex = -1;                ///< Source page (paged mode)
        std::pair<int, int> sourceTileCoord = {0, 0};  ///< Source tile (edgeless mode)
        int sourceLayerIndex = 0;
        
        QRectF boundingBox;                      ///< Selection bounding box
        QPointF transformOrigin;                 ///< Center for rotate/scale
        qreal rotation = 0;                      ///< Current rotation angle
        qreal scaleX = 1.0, scaleY = 1.0;        ///< Current scale factors
        QPointF offset;                          ///< Move offset
        
        bool isValid() const { return !selectedStrokes.isEmpty(); }
        bool hasTransform() const {
            return !qFuzzyIsNull(rotation) || 
                   !qFuzzyCompare(scaleX, 1.0) || 
                   !qFuzzyCompare(scaleY, 1.0) || 
                   !offset.isNull();
        }
        /// CR-2B-7: Get set of selected stroke IDs for exclusion during layer render
        QSet<QString> getSelectedIds() const {
            QSet<QString> ids;
            for (const VectorStroke& s : selectedStrokes) {
                ids.insert(s.id);
            }
            return ids;
        }
        void clear() { 
            selectedStrokes.clear(); 
            originalIndices.clear();
            sourcePageIndex = -1;
            sourceTileCoord = {0, 0};
            sourceLayerIndex = 0;
            boundingBox = QRectF();
            transformOrigin = QPointF();
            rotation = 0;
            scaleX = 1.0;
            scaleY = 1.0;
            offset = QPointF();
        }
    };
    LassoSelection m_lassoSelection;
    QPolygonF m_lassoPath;               ///< The lasso path being drawn
    bool m_isDrawingLasso = false;       ///< Currently drawing a lasso path
    
    // Handle sizes (touch-friendly design)
    static constexpr qreal HANDLE_VISUAL_SIZE = 8.0;   ///< Visual handle size in pixels
    static constexpr qreal HANDLE_HIT_SIZE = 20.0;     ///< Hit area size in pixels (touch-friendly)
    static constexpr qreal ROTATE_HANDLE_OFFSET = 25.0; ///< Distance of rotation handle from top
    
    // Transform operation state
    bool m_isTransformingSelection = false;   ///< Currently dragging a handle
    HandleHit m_transformHandle = HandleHit::None;  ///< Which handle is being dragged
    QPointF m_transformStartPos;              ///< Viewport position where drag started
    QPointF m_transformStartDocPos;           ///< Document position where drag started
    QRectF m_transformStartBounds;            ///< Original bounding box when drag started
    qreal m_transformStartRotation = 0;       ///< Original rotation when drag started
    qreal m_transformStartScaleX = 1.0;       ///< Original scaleX when drag started
    qreal m_transformStartScaleY = 1.0;       ///< Original scaleY when drag started
    QPointF m_transformStartOffset;           ///< Original offset when drag started
    
    // Clipboard for copy/cut/paste operations
    struct StrokeClipboard {
        QVector<VectorStroke> strokes;        ///< Copied strokes (pre-transformed)
        bool hasContent = false;              ///< Whether clipboard has content
        
        void clear() {
            strokes.clear();
            hasContent = false;
        }
    };
    StrokeClipboard m_clipboard;
    
    // ----- Performance/Memory Settings -----
    /// CUSTOMIZABLE: PDF cache capacity - higher = more RAM, smoother scrolling (range: 4-16)
    int m_pdfCacheCapacity = 6;  // Default for single column (visible + ±2 buffer)
    /// CUSTOMIZABLE: Max undo actions per page - higher = more RAM (range: 10-200)
    static const int MAX_UNDO_PER_PAGE = 50;
    
    // =========================================================================
    // END CUSTOMIZABLE VALUES
    // =========================================================================
    
    // ===== PDF Cache State (Task 1.3.6) =====
    QVector<PdfCacheEntry> m_pdfCache;
    qreal m_cachedDpi = 0;       ///< DPI at which cache was rendered
    mutable QMutex m_pdfCacheMutex;  ///< Mutex for thread-safe cache access
    
    // ===== Async PDF Preloading =====
    QTimer* m_pdfPreloadTimer = nullptr;  ///< Debounce timer for preload requests
    QList<QFutureWatcher<QImage>*> m_activePdfWatchers;  ///< Active async render operations (returns QImage for thread safety)
    static constexpr int PDF_PRELOAD_DELAY_MS = 150;   ///< Debounce delay (ms) before preloading
    
    // ===== Page Layout Cache (Performance: O(1) page position lookup) =====
    mutable QVector<qreal> m_pageYCache;  ///< Cached Y position for each page (single column)
    mutable bool m_pageLayoutDirty = true; ///< True if cache needs rebuild
    
    // ===== Input State (Task 1.3.8) =====
    int m_activeDrawingPage = -1;       ///< Page currently receiving strokes (-1 = none)
    bool m_pointerActive = false;       ///< True if pointer is pressed
    PointerEvent::Source m_activeSource = PointerEvent::Unknown;  ///< Active input source
    GestureState m_gestureState;        ///< Multi-touch gesture state
    QPointF m_lastPointerPos;           ///< Last pointer position (for delta calculation)
    bool m_hardwareEraserActive = false; ///< True when stylus eraser end is being used
    
    // ===== Stroke Drawing State (Task 2.2) =====
    VectorStroke m_currentStroke;             ///< Stroke currently being drawn
    bool m_isDrawing = false;                 ///< True while actively drawing a stroke
    
    /// Point decimation threshold - skip points closer than 1.5 pixels (performance tuning, not user-facing)
    static constexpr qreal MIN_DISTANCE_SQ = 1.5 * 1.5;
    
    // ===== Incremental Stroke Rendering (Task 2.3) =====
    QPixmap m_currentStrokeCache;             ///< Cache for in-progress stroke segments
    int m_lastRenderedPointIndex = 0;         ///< Index of last point rendered to cache
    qreal m_cacheZoom = 1.0;                  ///< Zoom level when cache was built
    QPointF m_cachePan;                       ///< Pan offset when cache was built
    
    // ===== Undo/Redo State (Task 2.5) =====
    QMap<int, QStack<PageUndoAction>> m_undoStacks;  ///< Per-page undo stacks
    QMap<int, QStack<PageUndoAction>> m_redoStacks;  ///< Per-page redo stacks
    
    // ===== Edgeless Undo/Redo State (Phase E6) =====
    QStack<EdgelessUndoAction> m_edgelessUndoStack;  ///< Global undo stack for edgeless mode
    QStack<EdgelessUndoAction> m_edgelessRedoStack;  ///< Global redo stack for edgeless mode
    static constexpr int MAX_UNDO_EDGELESS = 100;    ///< Max edgeless undo actions (~2MB)
    
    // ===== Benchmark State (Task 2.6) =====
    bool m_benchmarking = false;                      ///< Whether benchmarking is active
    QElapsedTimer m_benchmarkTimer;                   ///< Timer for measuring intervals
    mutable std::deque<qint64> m_paintTimestamps;     ///< Timestamps of recent paints (mutable for const getPaintRate)
    QTimer m_benchmarkDisplayTimer;                   ///< Timer for periodic display updates
    
    // ===== Deferred Viewport Gesture State (Task 2.3 - Zoom/Pan Optimization) =====
    /**
     * @brief State for deferred zoom and pan rendering.
     * 
     * During viewport gestures (Ctrl+wheel for zoom, Shift+wheel for horizontal pan,
     * Alt+wheel for vertical pan, touch pinch), we defer expensive rendering.
     * Instead, we capture a snapshot of the viewport and transform it during the
     * gesture. Only when the gesture ends do we re-render at the correct DPI.
     * 
     * This provides consistent 60+ FPS during zoom and pan operations regardless
     * of document complexity (PDF pages or edgeless tiles).
     * 
     * The API is designed to be called by any input source - currently keyboard+wheel,
     * but future gesture modules can call these methods directly.
     */
    struct ViewportGestureState {
        enum Type { None, Zoom, Pan, ZoomAndPan };  ///< ZoomAndPan for future touch
        Type activeType = None;                      ///< Currently active gesture type
        
        // Shared state
        QPixmap cachedFrame;                         ///< Viewport snapshot for fast transform
        qreal frameDevicePixelRatio = 1.0;           ///< Device pixel ratio when frame was captured
        qreal startZoom = 1.0;                       ///< Zoom level when gesture started
        QPointF startPan;                            ///< Pan offset when gesture started
        
        // Zoom-specific state
        qreal targetZoom = 1.0;                      ///< Target zoom (accumulates changes)
        QPointF zoomCenter;                          ///< Zoom center in viewport coords
        
        // Pan-specific state
        QPointF targetPan;                           ///< Target pan offset (accumulates changes)
        
        bool isActive() const { return activeType != None; }
        
        void reset() {
            activeType = None;
            cachedFrame = QPixmap();
        }
    };
    ViewportGestureState m_gesture;
    QTimer* m_gestureTimeoutTimer = nullptr;  ///< Fallback gesture end detection
    static constexpr int GESTURE_TIMEOUT_MS = 3000;  ///< Timeout for gesture end fallback (3s)
    bool m_backtickHeld = false;  ///< Track backtick (`) key for deferred vertical pan
    
    /**
     * @brief Handle gesture timeout.
     * Ends the active gesture (zoom or pan) when timeout expires.
     */
    void onGestureTimeout();
    
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
     * @brief Request PDF preload (debounced).
     * Called during scroll - actual preload happens after delay.
     */
    void preloadPdfCache();
    
    /**
     * @brief Actually perform async PDF preload.
     * Called by timer after debounce delay. Runs in background threads.
     */
    void doAsyncPdfPreload();
    
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
    
    /**
     * @brief Invalidate page layout cache - call when pages added/removed/resized.
     */
    void invalidatePageLayoutCache() { m_pageLayoutDirty = true; }
    
    /**
     * @brief Rebuild page layout cache if dirty.
     * Makes pagePosition() O(1) instead of O(n).
     */
    void ensurePageLayoutCache() const;
    
    // ===== Stroke Cache Helpers (Task 1.3.7) =====
    
    /**
     * @brief Pre-load stroke caches for nearby pages.
     * Call after scroll settles for smooth scrolling.
     */
    void preloadStrokeCaches();
    
    /**
     * @brief Evict tiles that are far from the visible area.
     * 
     * For edgeless mode with lazy loading enabled, this saves dirty tiles
     * and removes them from memory to bound memory usage.
     */
    void evictDistantTiles();
    
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
    
    // ===== Stroke Drawing (Task 2.2) =====
    
    /**
     * @brief Start a new stroke at the given pointer position.
     * @param pe The pointer event that initiated the stroke.
     */
    void startStroke(const PointerEvent& pe);
    
    /**
     * @brief Continue the current stroke with a new point.
     * @param pe The pointer event with the new position.
     */
    void continueStroke(const PointerEvent& pe);
    
    /**
     * @brief Finish the current stroke and add it to the page's layer.
     */
    void finishStroke();
    
    /**
     * @brief Finish the current stroke in edgeless mode.
     * 
     * Converts stroke from document coordinates to tile-local coordinates
     * and adds it to the appropriate tile.
     */
    void finishStrokeEdgeless();
    
    /**
     * @brief Create a straight line stroke between two points (Task 2.9).
     * @param start Start point (document coords for edgeless, page coords for paged).
     * @param end End point (document coords for edgeless, page coords for paged).
     * 
     * Uses current tool (Pen/Marker) to determine color and thickness.
     * For edgeless mode, handles tile splitting if the line crosses tile boundaries.
     */
    void createStraightLineStroke(const QPointF& start, const QPointF& end);
    
    // ===== Lasso Selection Tool (Task 2.10) =====
    
    /**
     * @brief Handle pointer press for lasso tool.
     */
    void handlePointerPress_Lasso(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer move for lasso tool.
     */
    void handlePointerMove_Lasso(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer release for lasso tool.
     */
    void handlePointerRelease_Lasso(const PointerEvent& pe);
    
    /**
     * @brief Clear the current lasso selection.
     */
    void clearLassoSelection();
    
    /**
     * @brief Finalize lasso selection after path is complete.
     * Finds all strokes on the active layer that intersect with the lasso path.
     */
    void finalizeLassoSelection();
    
    /**
     * @brief Check if a stroke intersects with the lasso polygon.
     * @param stroke The stroke to test.
     * @param lasso The lasso polygon path.
     * @return True if any point of the stroke is inside the lasso.
     */
    bool strokeIntersectsLasso(const VectorStroke& stroke, const QPolygonF& lasso) const;
    
    /**
     * @brief Calculate the combined bounding box of selected strokes.
     * @return Bounding rectangle in document/page coordinates.
     */
    QRectF calculateSelectionBoundingBox() const;
    
    /**
     * @brief Render the lasso selection (selected strokes, bounding box, handles).
     * @param painter The painter to render to.
     */
    void renderLassoSelection(QPainter& painter);
    
    /**
     * @brief Draw the selection bounding box with dashed line.
     * @param painter The painter to render to.
     */
    void drawSelectionBoundingBox(QPainter& painter);
    
    /**
     * @brief Build transform matrix for current selection state.
     * @return Transform incorporating offset, scale, and rotation.
     */
    QTransform buildSelectionTransform() const;
    
    /**
     * @brief Draw selection transform handles.
     * @param painter The painter to render to.
     */
    void drawSelectionHandles(QPainter& painter);
    
    /**
     * @brief Hit test selection handles at viewport position.
     * @param viewportPos Position in viewport coordinates.
     * @return HandleHit indicating which handle was hit, or None.
     */
    HandleHit hitTestSelectionHandles(const QPointF& viewportPos) const;
    
    /**
     * @brief Get handle positions in document/page coordinates.
     * @return Vector of 8 scale handle positions + rotation handle position.
     */
    QVector<QPointF> getHandlePositions() const;
    
    /**
     * @brief Start a selection transform operation.
     * @param handle Which handle was grabbed.
     * @param viewportPos Starting position in viewport coordinates.
     */
    void startSelectionTransform(HandleHit handle, const QPointF& viewportPos);
    
    /**
     * @brief Update selection transform during drag.
     * @param viewportPos Current position in viewport coordinates.
     */
    void updateSelectionTransform(const QPointF& viewportPos);
    
    /**
     * @brief Finalize the current selection transform.
     */
    void finalizeSelectionTransform();
    
    /**
     * @brief Update scale factors based on handle drag.
     * @param handle Which scale handle is being dragged.
     * @param viewportPos Current viewport position.
     */
    void updateScaleFromHandle(HandleHit handle, const QPointF& viewportPos);
    
    /**
     * @brief Apply the current selection transform to actual strokes.
     * Removes original strokes and adds transformed versions.
     */
    void applySelectionTransform();
    
    /**
     * @brief Cancel the current selection (discard transform, restore originals).
     */
    void cancelSelectionTransform();
    
    /**
     * @brief Apply a transform to a stroke's points.
     * @param stroke The stroke to transform (modified in place).
     * @param transform The transform to apply.
     */
    static void transformStrokePoints(VectorStroke& stroke, const QTransform& transform);
    
    // ===== Clipboard Operations (Task 2.10.7) =====
    
    /**
     * @brief Copy current selection to clipboard.
     */
    void copySelection();
    
    /**
     * @brief Cut current selection (copy + delete).
     */
    void cutSelection();
    
    /**
     * @brief Paste clipboard content at viewport center.
     */
    void pasteSelection();
    
    /**
     * @brief Delete current selection.
     */
    void deleteSelection();
    
    /**
     * @brief Check if clipboard has content.
     */
    bool hasClipboardContent() const { return m_clipboard.hasContent; }
    
    /**
     * @brief Add a point to the current stroke with point decimation.
     * @param pagePos Point position in page-local coordinates.
     * @param pressure Pressure value (0.0 to 1.0).
     */
    void addPointToStroke(const QPointF& pagePos, qreal pressure);
    
    // ===== Incremental Stroke Rendering (Task 2.3) =====
    
    /**
     * @brief Reset the current stroke cache for a new stroke.
     * Creates a transparent pixmap at viewport size for accumulating stroke segments.
     */
    void resetCurrentStrokeCache();
    
    /**
     * @brief Render the in-progress stroke incrementally.
     * @param painter The QPainter to render to (viewport painter, unmodified transform).
     * 
     * Only renders NEW segments since last call, accumulating in m_currentStrokeCache.
     * This is much faster than re-rendering the entire stroke each frame.
     */
    void renderCurrentStrokeIncremental(QPainter& painter);
    
    // ===== Eraser Tool (Task 2.4) =====
    
    /**
     * @brief Erase strokes at the given pointer position.
     * @param pe The pointer event containing hit information.
     * 
     * Finds all strokes within eraser radius and removes them from the layer.
     * Invalidates stroke cache after removal.
     */
    void eraseAt(const PointerEvent& pe);
    
    /**
     * @brief Erase strokes in edgeless mode (Phase E4).
     * @param viewportPos The eraser position in viewport coordinates.
     * 
     * Converts to document coordinates and checks the center tile plus
     * 8 neighboring tiles for cross-tile stroke segments.
     */
    void eraseAtEdgeless(QPointF viewportPos);
    
    /**
     * @brief Draw the eraser cursor circle at the current pointer position.
     * @param painter The QPainter to render to (viewport coordinates).
     */
    void drawEraserCursor(QPainter& painter);
    
    // ===== Undo/Redo Helpers (Task 2.5) =====
    
    /**
     * @brief Push an undo action for a single stroke.
     * @param pageIndex The page where the action occurred.
     * @param type The action type.
     * @param stroke The affected stroke.
     */
    void pushUndoAction(int pageIndex, PageUndoAction::Type type, const VectorStroke& stroke);
    
    /**
     * @brief Push an undo action for multiple strokes.
     * @param pageIndex The page where the action occurred.
     * @param type The action type (should be RemoveMultiple).
     * @param strokes The affected strokes.
     */
    void pushUndoAction(int pageIndex, PageUndoAction::Type type, const QVector<VectorStroke>& strokes);
    
    /**
     * @brief Clear the redo stack for a page (called when new actions occur).
     * @param pageIndex The page whose redo stack to clear.
     */
    void clearRedoStack(int pageIndex);
    
    /**
     * @brief Trim undo stack to MAX_UNDO_PER_PAGE if exceeded.
     * @param pageIndex The page whose undo stack to trim.
     */
    void trimUndoStack(int pageIndex);
    
    // ===== Edgeless Undo/Redo Helpers (Phase E6) =====
    
    /**
     * @brief Push an undo action for edgeless mode.
     * @param action The action containing all affected stroke segments.
     */
    void pushEdgelessUndoAction(const EdgelessUndoAction& action);
    
    /**
     * @brief Undo the last action in edgeless mode.
     */
    void undoEdgeless();
    
    /**
     * @brief Redo the last undone action in edgeless mode.
     */
    void redoEdgeless();
    
    /**
     * @brief Clear the edgeless redo stack (called when new actions occur).
     */
    void clearEdgelessRedoStack();
    
    /**
     * @brief Trim edgeless undo stack to MAX_UNDO_EDGELESS if exceeded.
     */
    void trimEdgelessUndoStack();
    
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
    
    // ===== Edgeless Mode State (Phase E2/E3) =====
    
    /**
     * @brief Whether to show tile boundary grid lines (debug).
     */
    bool m_showTileBoundaries = true;
    
    /**
     * @brief Active layer index for edgeless mode.
     * 
     * In paged mode, each Page tracks its own activeLayerIndex.
     * In edgeless mode, all tiles share this viewport-level active layer.
     * When a new tile is created, strokes go to this layer.
     */
    int m_edgelessActiveLayerIndex = 0;
    
    /**
     * @brief For edgeless drawing, stores the first point's tile coordinate.
     * Used to determine which tile receives the finished stroke.
     */
    Document::TileCoord m_edgelessDrawingTile = {0, 0};
    
    /**
     * @brief Render the edgeless canvas (tiled architecture).
     * @param painter The QPainter to render to.
     */
    void renderEdgelessMode(QPainter& painter);
    
    /**
     * @brief Render a single tile.
     * Renders only the strokes/objects of a tile (no background).
     * Used when backgrounds are pre-rendered for the entire visible area.
     * @param painter The QPainter to render to (already translated to tile origin).
     * @param tile The tile (Page) to render.
     * @param coord The tile coordinate (for debugging).
     */
    void renderTileStrokes(QPainter& painter, Page* tile, Document::TileCoord coord);
    
    /**
     * @brief Draw tile boundary grid lines for debugging.
     * @param painter The QPainter to render to.
     * @param viewRect The visible rectangle in document coordinates.
     */
    void drawTileBoundaries(QPainter& painter, QRectF viewRect);
    
    /**
     * @brief Calculate minimum zoom for edgeless mode.
     * @return Min zoom to ensure at most ~9 tiles (3x3 worst case) are visible.
     */
    qreal minZoomForEdgeless() const;
};
