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

// TouchGestureMode enum - shared with MainWindow.h and InkCanvas.h
// Guard prevents redefinition when multiple headers are included
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,     // Touch gestures completely off
    YAxisOnly,    // Only Y-axis panning allowed (X-axis and zoom locked)
    Full          // Full touch gestures (panning and zoom)
};
#endif

#include "Document.h"
#include "Page.h"
#include "ToolType.h"
#include "../strokes/VectorStroke.h"
#include "../pdf/PdfProvider.h"
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
        // ===== Stroke types (existing) =====
        AddStroke,          ///< A stroke was added (undo = remove it)
        RemoveStroke,       ///< A single stroke was removed (undo = add it back)
        RemoveMultiple,     ///< Multiple strokes removed at once (undo = add all back)
        TransformSelection, ///< Lasso transform: removed originals, added transformed (undo = reverse)
        
        // ===== Object types (Phase O2.7) =====
        ObjectInsert,       ///< An object was inserted (undo = remove it)
        ObjectDelete,       ///< An object was deleted (undo = restore it)
        ObjectMove,         ///< An object was moved (undo = move back)
        ObjectAffinityChange, ///< Object layer affinity changed (undo = restore old affinity)
        
        // ===== Object resize (Phase O3.1.5) =====
        ObjectResize        ///< An object was resized (undo = restore old pos+size)
    };
    
    Type type;
    int pageIndex;                      ///< The page this action occurred on
    int layerIndex = 0;                 ///< Which layer was affected (for TransformSelection)
    
    // ===== Stroke fields (existing) =====
    VectorStroke stroke;                ///< For AddStroke and RemoveStroke
    QVector<VectorStroke> strokes;      ///< For RemoveMultiple
    QVector<VectorStroke> removedStrokes;  ///< For TransformSelection: original strokes removed
    QVector<VectorStroke> addedStrokes;    ///< For TransformSelection: new strokes added
    
    // ===== Object fields (Phase O2.7) =====
    QJsonObject objectData;             ///< Serialized object for ObjectInsert/ObjectDelete restore
    QString objectId;                   ///< Object ID for lookup (ObjectMove, ObjectAffinityChange, ObjectResize)
    QPointF objectOldPosition;          ///< For ObjectMove/ObjectResize: position before
    QPointF objectNewPosition;          ///< For ObjectMove/ObjectResize: position after
    int objectOldAffinity = -1;         ///< For ObjectAffinityChange: affinity before change
    int objectNewAffinity = -1;         ///< For ObjectAffinityChange: affinity after change
    
    // ===== Object resize fields (Phase O3.1.5) =====
    QSizeF objectOldSize;               ///< For ObjectResize: size before resize
    QSizeF objectNewSize;               ///< For ObjectResize: size after resize
    
    // ===== Object rotation fields (Phase O3.1.8.3) =====
    qreal objectOldRotation = 0.0;      ///< For ObjectResize: rotation before (degrees)
    qreal objectNewRotation = 0.0;      ///< For ObjectResize: rotation after (degrees)
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
    PageUndoAction::Type type = PageUndoAction::AddStroke;  ///< Action type
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
    QVector<StrokeSegment> segments;    ///< For AddStroke/RemoveStroke: the stroke segments
    
    // For TransformSelection: compound action with both removed and added strokes
    QVector<StrokeSegment> removedSegments;  ///< Original strokes that were removed
    QVector<StrokeSegment> addedSegments;    ///< New strokes that were added
    
    // ===== Object fields (Phase O2.7) =====
    Document::TileCoord objectTileCoord;  ///< Tile containing the object
    QJsonObject objectData;               ///< Serialized object for ObjectInsert/ObjectDelete restore
    QString objectId;                     ///< Object ID for lookup
    QPointF objectOldPosition;            ///< For ObjectMove/ObjectResize: position before (tile-local)
    QPointF objectNewPosition;            ///< For ObjectMove/ObjectResize: position after (tile-local)
    Document::TileCoord objectOldTile;    ///< For ObjectMove across tiles: original tile
    Document::TileCoord objectNewTile;    ///< For ObjectMove across tiles: destination tile
    int objectOldAffinity = -1;           ///< For ObjectAffinityChange: affinity before change
    int objectNewAffinity = -1;           ///< For ObjectAffinityChange: affinity after change
    
    // ===== Object resize fields (Phase O3.1.5) =====
    QSizeF objectOldSize;                 ///< For ObjectResize: size before resize
    QSizeF objectNewSize;                 ///< For ObjectResize: size after resize
    
    // ===== Object rotation fields (Phase O3.1.8.3) =====
    qreal objectOldRotation = 0.0;        ///< For ObjectResize: rotation before (degrees)
    qreal objectNewRotation = 0.0;        ///< For ObjectResize: rotation after (degrees)
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
class TouchGestureHandler;

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
    
    /**
     * @brief Object insertion mode for ObjectSelect tool.
     * 
     * Phase C.2.4: Determines what type of object is created when clicking
     * in create mode. Auto-switches when selecting an existing object.
     */
    enum class ObjectInsertMode {
        Image,  ///< Insert ImageObject (default)
        Link    ///< Insert LinkObject
    };
    Q_ENUM(ObjectInsertMode)
    
    /**
     * @brief Object action mode for ObjectSelect tool.
     * 
     * Phase C.4.1: Determines whether clicking creates new objects
     * or selects existing ones.
     */
    enum class ObjectActionMode {
        Select,  ///< Click selects existing objects (default)
        Create   ///< Click creates new object at position
    };
    Q_ENUM(ObjectActionMode)
    
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
    
    /**
     * @brief Check if auto-layout mode is enabled.
     * @return true if auto 1/2 column switching is enabled.
     * 
     * When enabled, layout automatically switches between SingleColumn and
     * TwoColumn based on whether viewport width >= 2 * page_width + gap.
     * Default is disabled (1-column only mode).
     */
    bool autoLayoutEnabled() const { return m_autoLayoutEnabled; }
    
    /**
     * @brief Enable or disable auto-layout mode.
     * @param enabled true to enable auto 1/2 column switching.
     * 
     * When disabled, reverts to SingleColumn layout.
     * Shortcut: Ctrl+2 toggles this setting.
     */
    void setAutoLayoutEnabled(bool enabled);
    
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
    
    // ===== Auto-Highlight Mode (Phase D) =====
    
    /**
     * @brief Enable or disable auto-highlight mode.
     * @param enabled True to auto-create highlight strokes on selection release.
     * 
     * When enabled, releasing mouse after text selection automatically
     * creates highlight strokes. When disabled, selection remains until
     * user copies or cancels. Called from HighlighterSubToolbar.
     */
    void setAutoHighlightEnabled(bool enabled);
    
    /**
     * @brief Check if auto-highlight mode is enabled.
     */
    bool isAutoHighlightEnabled() const { return m_autoHighlightEnabled; }
    
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
    
    // ===== Object Undo Helpers (Phase O2.7) =====
    
    /**
     * @brief Push an undo action for object insertion.
     * @param obj The inserted object (will be serialized to JSON).
     * @param pageIndex Page index (paged mode) or -1 for edgeless.
     * @param tileCoord Tile coordinate (edgeless mode only).
     * 
     * Undo will remove the object; redo will re-add it.
     */
    void pushObjectInsertUndo(InsertedObject* obj, int pageIndex = -1,
                              Document::TileCoord tileCoord = {0, 0});
    
    /**
     * @brief Push an undo action for object deletion.
     * @param obj The deleted object (will be serialized to JSON).
     * @param pageIndex Page index (paged mode) or -1 for edgeless.
     * @param tileCoord Tile coordinate (edgeless mode only).
     * 
     * Undo will restore the object; redo will re-delete it.
     */
    void pushObjectDeleteUndo(InsertedObject* obj, int pageIndex = -1,
                              Document::TileCoord tileCoord = {0, 0});
    
    /**
     * @brief Push an undo action for object move.
     * @param obj The moved object.
     * @param oldPos Position before move.
     * @param pageIndex Page index (paged mode) or -1 for edgeless.
     * @param oldTile Original tile (edgeless cross-tile move).
     * @param newTile New tile (edgeless cross-tile move).
     * 
     * Undo will restore old position; redo will apply new position.
     */
    void pushObjectMoveUndo(InsertedObject* obj, const QPointF& oldPos,
                            int pageIndex = -1,
                            Document::TileCoord oldTile = {0, 0},
                            Document::TileCoord newTile = {0, 0});
    
    /**
     * @brief Push an undo action for object resize/rotate (Phase O3.1.5, O3.1.8.3).
     * @param obj The object that was resized/rotated.
     * @param oldPos Position before resize.
     * @param oldSize Size before resize.
     * @param oldRotation Rotation before rotate (degrees).
     * 
     * Undo will restore old position+size+rotation; redo will apply new values.
     * Extended in O3.1.8.3 to include rotation.
     */
    void pushObjectResizeUndo(InsertedObject* obj, const QPointF& oldPos, 
                              const QSizeF& oldSize, qreal oldRotation = 0.0);
    
    /**
     * @brief Push an undo action for object affinity change (Phase O3.5.3).
     * @param obj The object that had its affinity changed.
     * @param oldAffinity The affinity value before the change.
     * 
     * Undo will restore old affinity; redo will apply new affinity.
     */
    void pushObjectAffinityUndo(InsertedObject* obj, int oldAffinity);
    
    // ===== Affinity Helpers (Phase O3.5.3) =====
    
    /**
     * @brief Find the Page containing the given object.
     * @param obj The object to find.
     * @param outTileCoord Output: tile coordinate if in edgeless mode.
     * @return Pointer to the Page, or nullptr if not found.
     */
    Page* findPageContainingObject(InsertedObject* obj, Document::TileCoord* outTileCoord = nullptr);
    
    /**
     * @brief Get the maximum valid affinity value.
     * @return layerCount - 1 for the current document mode.
     */
    int getMaxAffinity() const;
    
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
     * @brief Find an inserted object at a point in document coordinates.
     * @param docPoint Point in document coordinates.
     * @return Pointer to the topmost object at the point, or nullptr if none.
     * 
     * Phase O2: For paged mode, checks the page containing the point.
     * For edgeless mode, checks all loaded tiles.
     * Objects are checked in reverse z-order (topmost first) via Page::objectAtPoint().
     */
    InsertedObject* objectAtPoint(const QPointF& docPoint) const;
    
    // ===== Object Selection API (Phase O2) =====
    
    /**
     * @brief Select an object.
     * @param obj The object to select.
     * @param addToSelection If true, add to existing selection; if false, replace selection.
     * 
     * Emits objectSelectionChanged() if selection changes.
     */
    void selectObject(InsertedObject* obj, bool addToSelection = false);
    
    /**
     * @brief Deselect a specific object.
     * @param obj The object to deselect.
     * 
     * Emits objectSelectionChanged() if object was selected.
     */
    void deselectObject(InsertedObject* obj);
    
    /**
     * @brief Deselect all objects.
     * 
     * Emits objectSelectionChanged() if any objects were selected.
     */
    void deselectAllObjects();
    
    /**
     * @brief Handle cancel/escape action for ObjectSelect tool.
     * 
     * Behavior:
     * - If objects are selected: deselect all objects
     * - If no objects selected but clipboard has content: clear object clipboard
     * 
     * Used by Escape key handler and ObjectSelectSubToolbar cancel button.
     */
    void cancelObjectSelectAction();
    
    /**
     * @brief Clear the internal object clipboard.
     * 
     * Emits objectClipboardChanged(false).
     */
    void clearObjectClipboard();
    
    /**
     * @brief Deselect an object by its ID.
     * @param objectId The ID of the object to deselect.
     * 
     * Emits objectSelectionChanged() if the object was found and deselected.
     */
    void deselectObjectById(const QString& objectId);
    
    /**
     * @brief Move all selected objects by a delta.
     * @param delta The offset to add to each object's position.
     * 
     * Phase O2.3.3: Moves objects and triggers viewport update.
     * Does NOT mark pages dirty (caller handles that on drag end).
     */
    void moveSelectedObjects(const QPointF& delta);
    
    /**
     * @brief Get the list of currently selected objects.
     * @return List of selected object pointers (non-owning).
     */
    const QList<InsertedObject*>& selectedObjects() const { return m_selectedObjects; }
    
    /**
     * @brief Check if any objects are selected.
     * @return True if at least one object is selected.
     */
    bool hasSelectedObjects() const { return !m_selectedObjects.isEmpty(); }
    
    /**
     * @brief Check if a lasso selection exists.
     * @return True if there is an active lasso selection.
     * 
     * Action Bar: Used to sync state on tab switch.
     */
    bool hasLassoSelection() const { return m_lassoSelection.isValid(); }
    
    /**
     * @brief Check if text is currently selected (PDF text).
     * @return True if text is selected.
     * 
     * Action Bar: Used to sync state on tab switch.
     */
    bool hasTextSelection() const { return m_textSelection.isValid(); }
    
    /**
     * @brief Check if the internal stroke clipboard has content.
     * @return True if strokes can be pasted.
     * 
     * Action Bar: Used to sync state on tab switch.
     */
    bool hasStrokesInClipboard() const { return m_clipboard.hasContent; }
    
    /**
     * @brief Check if the internal object clipboard has content.
     * @return True if objects can be pasted.
     * 
     * Action Bar: Used to sync state on tab switch.
     */
    bool hasObjectsInClipboard() const { return !m_objectClipboard.isEmpty(); }
    
    /**
     * @brief Get the current object insert mode.
     * @return Current insert mode (Image or Link).
     * 
     * Phase C.2.4: Used by UI to reflect current mode state.
     */
    ObjectInsertMode objectInsertMode() const { return m_objectInsertMode; }
    
    /**
     * @brief Set the object insert mode.
     * @param mode The new insert mode (Image or Link).
     * 
     * Phase D: Called from ObjectSelectSubToolbar to change insert mode.
     */
    void setObjectInsertMode(ObjectInsertMode mode);
    
    /**
     * @brief Get the current object action mode.
     * @return Current action mode (Select or Create).
     * 
     * Phase C.4.1: Used by UI to reflect current mode state.
     */
    ObjectActionMode objectActionMode() const { return m_objectActionMode; }
    
    /**
     * @brief Set the object action mode.
     * @param mode The new action mode (Select or Create).
     * 
     * Phase D: Called from ObjectSelectSubToolbar to change action mode.
     */
    void setObjectActionMode(ObjectActionMode mode);
    
    // ===== Object Resize (Phase O3.1) =====
    
    /**
     * @brief Get the bounding rectangle of an object in viewport coordinates.
     * @param obj The object to get bounds for.
     * @return Bounding rectangle in viewport coordinates.
     * 
     * Converts the object's document-space bounds to viewport coordinates,
     * accounting for zoom and pan. Used for hit-testing resize handles.
     */
    QRectF objectBoundsInViewport(InsertedObject* obj) const;
    
    /**
     * @brief Detect which resize handle is at the given viewport position.
     * @param viewportPos Position in viewport coordinates.
     * @return The handle hit, or HandleHit::None if no handle hit.
     * 
     * Checks the 8 resize handles (corners + edges) and the rotation handle.
     * Only works when exactly one object is selected.
     * Reuses HandleHit enum from lasso transform.
     */
    HandleHit objectHandleAtPoint(const QPointF& viewportPos) const;
    
    /**
     * @brief Update object size during resize drag.
     * @param currentViewport Current viewport position of the pointer.
     * 
     * Called from handlePointerMove_ObjectSelect() during resize.
     * Calculates new size based on which handle is being dragged.
     * Implemented in O3.1.4.
     */
    void updateObjectResize(const QPointF& currentViewport);
    
    // ===== Object Z-Order (Phase O2.8) =====
    
    /**
     * @brief Bring selected objects to front (highest zOrder in their affinity group).
     * 
     * Sets zOrder = max + 1 for each selected object within its affinity group.
     */
    void bringSelectedToFront();
    
    /**
     * @brief Send selected objects to back (lowest zOrder in their affinity group).
     * 
     * Sets zOrder = min - 1 for each selected object within its affinity group.
     */
    void sendSelectedToBack();
    
    /**
     * @brief Bring selected objects forward one step in z-order.
     * 
     * Swaps with the next higher zOrder object in the same affinity group.
     */
    void bringSelectedForward();
    
    /**
     * @brief Send selected objects backward one step in z-order.
     * 
     * Swaps with the next lower zOrder object in the same affinity group.
     */
    void sendSelectedBackward();
    
    // ===== Layer Affinity Shortcuts (Phase O3.5.2) =====
    
    /**
     * @brief Increase affinity of selected objects (move up in layer stack).
     * 
     * Moves objects to render after the next higher layer.
     * Maximum affinity is layerCount - 1 (on top of all strokes).
     */
    void increaseSelectedAffinity();
    
    /**
     * @brief Decrease affinity of selected objects (move down in layer stack).
     * 
     * Moves objects to render after the previous layer.
     * Minimum affinity is -1 (background, below all strokes).
     */
    void decreaseSelectedAffinity();
    
    /**
     * @brief Send selected objects to background (affinity = -1).
     * 
     * Objects will render below all stroke layers.
     */
    void sendSelectedToBackground();
    
    /**
     * @brief Paste handler for ObjectSelect tool.
     * 
     * Phase O2.4: Tool-aware paste behavior.
     * Priority 1: System clipboard has image → insertImageFromClipboard()
     * Priority 2: Internal object clipboard → pasteObjects() (O2.6)
     * Does NOT fall back to lasso paste.
     */
    void pasteForObjectSelect();
    
    /**
     * @brief Insert image from system clipboard as an ImageObject.
     * 
     * Phase O2.4.3: Creates ImageObject at viewport center, adds to current page/tile,
     * saves to assets folder, creates undo entry, and selects the new object.
     */
    void insertImageFromClipboard();
    
    /**
     * @brief Insert image from a file path as an ImageObject.
     * @param filePath Path to the image file.
     * 
     * Phase O2.4: Handles pasting files copied from File Explorer.
     * Creates ImageObject at viewport center, adds to current page/tile,
     * saves to assets folder, and selects the new object.
     */
    void insertImageFromFile(const QString& filePath);
    
    /**
     * @brief Open file dialog and insert selected image.
     * 
     * Phase C.0.5: Opens a file dialog to select an image file,
     * then calls insertImageFromFile() to insert it at viewport center.
     */
    void insertImageFromDialog();
    
    /**
     * @brief Delete all currently selected objects.
     * 
     * Phase O2.5: Removes each selected object from its page/tile,
     * creates undo entries, marks pages dirty, and clears selection.
     */
    void deleteSelectedObjects();
    
    /**
     * @brief Copy selected objects to internal clipboard.
     * 
     * Phase O2.6: Serializes each selected object to JSON and stores
     * in m_objectClipboard. Does not modify selection.
     */
    void copySelectedObjects();
    
    /**
     * @brief Paste objects from internal clipboard.
     * 
     * Phase O2.6.3: Deserializes objects from m_objectClipboard,
     * assigns new UUIDs, offsets positions, adds to current page/tile,
     * and selects the pasted objects.
     */
    void pasteObjects();
    
    /**
     * @brief Activate a link slot on the selected LinkObject.
     * @param slotIndex The slot index (0-2) to activate.
     * 
     * Phase C.4.3: If exactly one LinkObject is selected, activates the
     * specified slot based on its type:
     * - Position: Navigate to the target page/position
     * - URL: Open in default browser
     * - Markdown: Open markdown note editor
     * - Empty: Show add link menu (Phase C.5.3)
     */
    void activateLinkSlot(int slotIndex);
    
    /**
     * @brief Show menu to add a link to an empty slot.
     * @param slotIndex The slot index (0-2) to populate.
     * 
     * Phase C.5.3 (TEMPORARY): Shows a simple QMenu with options to add
     * Position, URL, or Markdown links. URL uses QInputDialog for input.
     * This is a temporary UI until a proper subtoolbar is implemented.
     */
    void addLinkToSlot(int slotIndex);
    
    /**
     * @brief Clear the content of a LinkObject slot.
     * @param slotIndex The slot index (0-2) to clear.
     * 
     * Phase D: Called from ObjectSelectSubToolbar after long-press delete
     * confirmation. Clears the slot content (Position/URL/Markdown) without
     * deleting the entire LinkObject.
     */
    void clearLinkSlot(int slotIndex);
    
    /**
     * @brief Create an empty LinkObject at the specified page position.
     * @param pageIndex Index of the page to add the LinkObject to.
     * @param pagePos Position in page-local coordinates.
     * 
     * Phase C.4.5: Creates a new LinkObject with empty slots at the
     * specified position, adds to page, pushes undo, and selects it.
     */
    void createLinkObjectAtPosition(int pageIndex, const QPointF& pagePos);
    
    /**
     * @brief Create a LinkObject for a text highlight.
     * @param pageIndex Index of the page containing the highlight.
     * 
     * Phase C.3.2: Creates a LinkObject positioned at the start of the
     * first highlight rect, with description set to the selected text
     * and icon color matching the highlighter color.
     */
    void createLinkObjectForHighlight(int pageIndex);
    
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
     * @brief Get the center of the viewport in document coordinates.
     * @return Center point in document coordinates.
     * 
     * Useful for centering new objects at the current view position.
     * Phase O2.4.3: Used for image insertion from clipboard.
     */
    QPointF viewportCenterInDocument() const;
    
    /**
     * @brief Get the next available zOrder for objects with a given affinity on a page.
     * @param page The page to check (can be a tile in edgeless mode).
     * @param affinity The layer affinity to check.
     * @return max(existing zOrders) + 1, or 0 if no objects with that affinity exist.
     * 
     * Used when inserting/pasting objects to ensure they appear on top of existing objects.
     */
    int getNextZOrderForAffinity(Page* page, int affinity) const;
    
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
     * @brief Navigate to a specific position on a page identified by UUID.
     * @param pageUuid UUID of the target page.
     * @param position Position in page-local coordinates to center on.
     * 
     * Phase C.5.1: Used by LinkObject Position slots to navigate to linked
     * locations. Scrolls to the page and centers the view on the position.
     * No-op if page UUID not found.
     */
    void navigateToPosition(const QString& pageUuid, const QPointF& position);
    
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
    
    // ===== Touch Gesture Mode =====
    
    /**
     * @brief Set the touch gesture mode.
     * @param mode The new touch gesture mode.
     * 
     * Controls how touch input is handled:
     * - Disabled: Touch gestures ignored
     * - YAxisOnly: Single-finger vertical pan only (no zoom)
     * - Full: Single-finger pan + pinch-to-zoom
     */
    void setTouchGestureMode(TouchGestureMode mode);
    
    /**
     * @brief Get the current touch gesture mode.
     * @return Current touch gesture mode.
     */
    TouchGestureMode touchGestureMode() const;
    
    // ===== Public Clipboard Operations (Action Bar support) =====
    
    /**
     * @brief Copy current lasso selection to internal clipboard.
     * Action Bar: Called by LassoActionBar::copyRequested.
     */
    void copyLassoSelection();
    
    /**
     * @brief Cut current lasso selection (copy + delete).
     * Action Bar: Called by LassoActionBar::cutRequested.
     */
    void cutLassoSelection();
    
    /**
     * @brief Paste internal clipboard content.
     * Action Bar: Called by LassoActionBar::pasteRequested.
     */
    void pasteLassoSelection();
    
    /**
     * @brief Delete current lasso selection.
     * Action Bar: Called by LassoActionBar::deleteRequested.
     */
    void deleteLassoSelection();
    
    /**
     * @brief Copy selected PDF text to system clipboard.
     * Action Bar: Called by TextSelectionActionBar::copyRequested.
     */
    void copyTextSelection();
    
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
     * @brief Emitted when the object selection changes.
     * 
     * Phase O2: Notifies UI when objects are selected/deselected.
     * Connect to this to update toolbar state, properties panel, etc.
     */
    void objectSelectionChanged();
    
    /**
     * @brief Emitted when the object insert mode changes.
     * @param mode New object insert mode (Image or Link).
     * 
     * Phase C.2.4: Notifies UI when user switches between inserting
     * ImageObjects or LinkObjects. Auto-emitted when selecting objects.
     */
    void objectInsertModeChanged(ObjectInsertMode mode);
    
    /**
     * @brief Emitted when the object action mode changes.
     * @param mode New action mode (Select or Create).
     * 
     * Phase C.4.1: Notifies UI when user switches between selecting
     * existing objects or creating new ones.
     */
    void objectActionModeChanged(ObjectActionMode mode);
    
    /**
     * @brief Emitted when lasso selection state changes.
     * @param hasSelection True if lasso selection exists.
     * 
     * Action Bar: Used to show/hide LassoActionBar.
     */
    void lassoSelectionChanged(bool hasSelection);
    
    /**
     * @brief Emitted when PDF text selection state changes.
     * @param hasSelection True if text is selected.
     * 
     * Action Bar: Used to show/hide TextSelectionActionBar.
     */
    void textSelectionChanged(bool hasSelection);
    
    /**
     * @brief Emitted when internal stroke clipboard state changes.
     * @param hasStrokes True if clipboard contains strokes.
     * 
     * Action Bar: Used to show/hide Paste button in LassoActionBar.
     */
    void strokeClipboardChanged(bool hasStrokes);
    
    /**
     * @brief Emitted when internal object clipboard state changes.
     * @param hasObjects True if clipboard contains objects.
     * 
     * Action Bar: Used to show/hide Paste button in ObjectSelectActionBar.
     */
    void objectClipboardChanged(bool hasObjects);
    
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
    
    /**
     * @brief Emitted when text selection is finalized.
     * @param text The selected text content.
     * 
     * Phase A: Emitted by Highlighter tool when user completes text selection.
     */
    void textSelected(const QString& text);
    
    /**
     * @brief Emitted when auto-highlight mode changes.
     * 
     * Phase B: Can be connected to subtoolbar toggle button to sync state.
     * @param enabled New auto-highlight state.
     */
    void autoHighlightEnabledChanged(bool enabled);
    
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
    bool event(QEvent* event) override;  ///< Forwards touch events to handler
    
private:
    // ===== Document Reference =====
    Document* m_document = nullptr;
    
    // ===== View State =====
    qreal m_zoomLevel = 1.0;
    QPointF m_panOffset;
    int m_currentPageIndex = 0;
    
    // ===== Touch Gesture Handler =====
    // Touch gesture logic is encapsulated in TouchGestureHandler (see TouchGestureHandler.h)
    TouchGestureHandler* m_touchHandler = nullptr;  ///< Handles touch pan/zoom/tap
    
    // =========================================================================
    // CUSTOMIZABLE VALUES
    // =========================================================================
    // These values should eventually come from user settings (ControlPanelDialog).
    // TODO: Load from QSettings or a Settings class in Phase 4.
    // =========================================================================
    
    // ----- Layout Settings -----
    LayoutMode m_layoutMode = LayoutMode::SingleColumn;
    int m_pageGap = 20;  ///< CUSTOMIZABLE: Pixels between pages (range: 0-100)
    bool m_autoLayoutEnabled = false;  ///< Auto 1/2 column mode (default: 1-column only)
    
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
        QVector<int> originalIndices;            ///< Indices in the layer (legacy, unused)
        int sourcePageIndex = -1;                ///< Source page (paged mode)
        std::pair<int, int> sourceTileCoord = {0, 0};  ///< First source tile (edgeless mode)
        int sourceLayerIndex = 0;
        
        QRectF boundingBox;                      ///< Selection bounding box
        QPointF transformOrigin;                 ///< Center for rotate/scale
        qreal rotation = 0;                      ///< Current rotation angle
        qreal scaleX = 1.0, scaleY = 1.0;        ///< Current scale factors
        QPointF offset;                          ///< Move offset
        
        mutable QSet<QString> m_cachedIds;       ///< Cached stroke IDs for CR-2B-7 exclusion
        
        bool isValid() const { return !selectedStrokes.isEmpty(); }
        bool hasTransform() const {
            return !qFuzzyIsNull(rotation) || 
                   !qFuzzyCompare(scaleX, 1.0) || 
                   !qFuzzyCompare(scaleY, 1.0) || 
                   !qFuzzyIsNull(offset.x()) ||
                   !qFuzzyIsNull(offset.y());
        }
        /// CR-2B-7: Get set of selected stroke IDs for exclusion during layer render
        /// Uses cached set for performance (rebuilt when selection changes)
        const QSet<QString>& getSelectedIds() const {
            if (m_cachedIds.isEmpty() && !selectedStrokes.isEmpty()) {
                for (const VectorStroke& s : selectedStrokes) {
                    m_cachedIds.insert(s.id);
                }
            }
            return m_cachedIds;
        }
        void clear() {
            m_cachedIds.clear();  // Clear cached IDs 
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
    
    // P1: Lasso path incremental rendering cache
    QPixmap m_lassoPathCache;            ///< Cached lasso path segments at viewport resolution
    int m_lastRenderedLassoIdx = 0;      ///< Index of last rendered path point
    qreal m_lassoPathCacheZoom = 0;      ///< Zoom level when cache was created
    QPointF m_lassoPathCachePan;         ///< Pan offset when cache was created
    qreal m_lassoPathLength = 0;         ///< Cumulative path length for dash offset
    
    void resetLassoPathCache();          ///< Creates/resets the lasso path cache
    void renderLassoPathIncremental(QPainter& painter);  ///< Renders lasso path incrementally
    
    // P3: Selection stroke caching
    QPixmap m_selectionStrokeCache;      ///< Strokes rendered at identity transform
    bool m_selectionCacheDirty = true;   ///< Cache needs rebuild
    qreal m_selectionCacheZoom = 0;      ///< Zoom level when cache was created
    QRectF m_selectionCacheBounds;       ///< Document-space bounds of cached strokes
    
    // P4: Semi-transparent selection handling
    bool m_selectionHasTransparency = false;  ///< Whether selection contains transparent strokes
    
    void rebuildSelectionCache();        ///< Rebuild cache with strokes at identity
    void invalidateSelectionCache();     ///< Mark cache as needing rebuild
    
    // P5: Background snapshot for transform performance
    // Similar to zoom/pan gesture caching - captures viewport without selection
    QPixmap m_selectionBackgroundSnapshot;   ///< Viewport snapshot excluding selection
    qreal m_backgroundSnapshotDpr = 1.0;     ///< Device pixel ratio of snapshot
    bool m_skipSelectionRendering = false;   ///< Temp flag during snapshot capture
    
    void captureSelectionBackground();       ///< Capture background for transform
    
    // ============================================================================
    // Text Selection State (Highlighter Tool) - Phase A
    // ============================================================================
    
    /**
     * @brief Temporary state for text selection from PDF.
     * 
     * Active when ToolType::Highlighter is selected and user is selecting text.
     * Cleared when tool changes or selection is finalized.
     */
    /**
     * @brief Character position within text boxes (for hit testing).
     */
    struct CharacterPosition {
        int boxIndex = -1;                      ///< Index into m_textBoxCache
        int charIndex = -1;                     ///< Character index within that box
        
        bool isValid() const { return boxIndex >= 0 && charIndex >= 0; }
    };
    
    /**
     * @brief Text-flow selection model (like Notepad/Word).
     * 
     * Selection is defined by start and end character positions in reading order.
     * The selection flows character-by-character, not as a rectangle.
     */
    struct TextSelection {
        int pageIndex = -1;                     ///< Page being selected from (-1 = none)
        
        // Start position (anchor) - where selection began
        int startBoxIndex = -1;                 ///< Index into m_textBoxCache
        int startCharIndex = -1;                ///< Character index within that box (-1 = end of box)
        
        // End position (cursor) - current selection endpoint
        int endBoxIndex = -1;                   ///< Index into m_textBoxCache
        int endCharIndex = -1;                  ///< Character index within that box
        
        // Computed from start/end positions
        QString selectedText;                   ///< All characters from start to end
        QVector<QRectF> highlightRects;         ///< Per-line highlight rectangles (PDF coords)
        
        bool isSelecting = false;               ///< Currently dragging?
        
        bool isValid() const { 
            return pageIndex >= 0 && startBoxIndex >= 0 && endBoxIndex >= 0; 
        }
        
        /// Check if selection is empty (start == end)
        bool isEmpty() const {
            return startBoxIndex == endBoxIndex && startCharIndex == endCharIndex;
        }
        
        void clear() {
            pageIndex = -1;
            startBoxIndex = startCharIndex = -1;
            endBoxIndex = endCharIndex = -1;
            selectedText.clear();
            highlightRects.clear();
            isSelecting = false;
        }
    };
    TextSelection m_textSelection;
    
    // Text box cache (loaded on-demand for current page)
    QVector<PdfTextBox> m_textBoxCache;
    int m_textBoxCachePageIndex = -1;
    mutable int m_lastHitBoxIndex = -1;  ///< PERF: Spatial locality hint for findCharacterAtPoint
    
    // Link cache (loaded on-demand for current page) - Phase D.1
    QVector<PdfLink> m_linkCache;
    int m_linkCachePageIndex = -1;
    
    // Highlighter tool settings
    QColor m_highlighterColor = QColor(255, 255, 0, 128);  ///< Yellow, 50% alpha
    bool m_autoHighlightEnabled = false;  ///< When true, releasing selection auto-creates stroke (Phase B)
    
    // ===== Object Selection (Phase O2) =====
    
    /**
     * @brief Currently selected inserted objects.
     * 
     * Non-owning pointers to objects in pages/tiles.
     * Objects are owned by Page::objects vector.
     * Selection is cleared when switching pages/tiles or when objects are deleted.
     */
    QList<InsertedObject*> m_selectedObjects;
    
    /**
     * @brief Object currently under the cursor (for hover highlight).
     * 
     * Non-owning pointer, nullptr if no object is hovered.
     * Updated on mouse move when ObjectSelect tool is active.
     */
    InsertedObject* m_hoveredObject = nullptr;
    
    /**
     * @brief Current object insertion mode.
     * 
     * Phase C.2.4: Determines whether clicking in create mode inserts
     * an ImageObject or LinkObject. Auto-updated when selecting objects.
     */
    ObjectInsertMode m_objectInsertMode = ObjectInsertMode::Image;
    
    /**
     * @brief Current object action mode.
     * 
     * Phase C.4.1: Determines whether clicking selects existing objects
     * or creates new ones. Default is Select.
     */
    ObjectActionMode m_objectActionMode = ObjectActionMode::Select;
    
    /**
     * @brief Whether we're currently dragging selected objects.
     */
    bool m_isDraggingObjects = false;
    
    /**
     * @brief Viewport position where object drag started.
     */
    QPointF m_objectDragStartViewport;
    
    /**
     * @brief Document position where object drag started.
     */
    QPointF m_objectDragStartDoc;
    
    /**
     * @brief Original positions of objects before drag started.
     * 
     * Maps object ID to its position at drag start.
     * Used to create undo entry when drag completes.
     * Cleared when drag ends or is cancelled.
     */
    QMap<QString, QPointF> m_objectOriginalPositions;
    
    /**
     * @brief Internal clipboard for copied objects.
     * 
     * Phase O2.6: Stores serialized objects (via toJson()) for paste.
     * Separate from system clipboard - only for internal object copy/paste.
     * Each entry is a complete JSON representation of an InsertedObject.
     */
    QList<QJsonObject> m_objectClipboard;
    
    // ===== Object Resize State (Phase O3.1) =====
    
    /**
     * @brief Whether a resize operation is in progress.
     * 
     * Set to true when user starts dragging a resize handle,
     * set to false when drag is released or cancelled.
     */
    bool m_isResizingObject = false;
    
    /**
     * @brief Which resize handle is being dragged.
     * 
     * Valid when m_isResizingObject is true.
     * Determines how mouse movement affects object size.
     */
    HandleHit m_objectResizeHandle = HandleHit::None;
    
    /**
     * @brief Viewport position where resize drag started.
     * 
     * Used to calculate drag delta during resize operation.
     */
    QPointF m_resizeStartViewport;
    
    /**
     * @brief Object size before resize started.
     * 
     * Used for undo and to calculate new size based on drag delta.
     */
    QSizeF m_resizeOriginalSize;
    
    /**
     * @brief Object position before resize started.
     * 
     * Needed because some resize handles (e.g., TopLeft) change both
     * position and size. Used for undo entry.
     */
    QPointF m_resizeOriginalPosition;
    
    /**
     * @brief Object rotation before resize/rotate started (Phase O3.1.8.2).
     * 
     * Stored when user starts dragging any handle, used for rotation undo.
     */
    qreal m_resizeOriginalRotation = 0.0;
    
    /**
     * @brief Object center in DOCUMENT coordinates at resize start.
     * 
     * In edgeless mode, object positions are tile-local, but pointer events
     * give document-global coordinates. This member stores the document-global
     * center for correct scale calculations in updateObjectResize().
     * 
     * BF: Without this, edgeless resize mixed tile-local and document-global
     * coordinates, causing extreme scaling jumps.
     */
    QPointF m_resizeObjectDocCenter;
    
    // =========================================================================
    // Phase O4.1: Object Drag/Resize Performance Optimization
    // =========================================================================
    // Same pattern as lasso selection (m_selectionBackgroundSnapshot):
    // 1. Capture viewport without selected objects when drag/resize starts
    // 2. During drag/resize, draw cached background + objects at current position
    // 3. On release, clear snapshot and do full re-render
    
    /**
     * @brief Viewport snapshot excluding selected objects.
     * 
     * Captured when drag/resize starts. During drag/resize, this is drawn
     * as background instead of re-rendering everything.
     */
    QPixmap m_objectDragBackgroundSnapshot;
    
    /**
     * @brief Device pixel ratio of the object drag snapshot.
     */
    qreal m_objectDragSnapshotDpr = 1.0;
    
    /**
     * @brief Temporary flag to exclude selected objects during snapshot capture.
     * 
     * Set true before grab(), false after. paintEvent checks this to skip
     * rendering selected objects.
     */
    bool m_skipSelectedObjectRendering = false;
    
    /**
     * @brief Phase O4.1.3: Throttle drag updates to ~60fps.
     * 
     * High-DPI mice/tablets can send 100s of events per second.
     * We throttle repaints to avoid excessive CPU usage.
     */
    QElapsedTimer m_dragUpdateTimer;
    static constexpr qint64 DRAG_UPDATE_INTERVAL_MS = 16;  // ~60fps
    
    /**
     * @brief Capture background snapshot for object drag/resize optimization.
     * 
     * Similar to captureSelectionBackground() for lasso selection.
     */
    void captureObjectDragBackground();
    
    /**
     * @brief Render only the selected objects (for fast path during drag/resize).
     */
    void renderSelectedObjectsOnly(QPainter& painter);
    
    /**
     * @brief Phase O4.1.2: Pre-rendered cache of selected objects at current zoom.
     * 
     * When drag/resize starts, we render the selected objects to this pixmap
     * at the current zoom level. During drag, we just draw this cache at the
     * new position - no image scaling needed! This is much faster than calling
     * ImageObject::render() which scales the source image every frame.
     */
    QPixmap m_dragObjectRenderedCache;
    
    /**
     * @brief Offset from viewport origin to where the cache should be drawn.
     * 
     * This is the viewport position of the object's origin (page/tile origin
     * in viewport coords) at the time the cache was created. During drag,
     * we calculate the new position based on drag delta.
     */
    QPointF m_dragObjectCacheOrigin;
    
    /**
     * @brief Page index or tile coord where the dragged object lives.
     * 
     * Cached at drag start to avoid searching all pages/tiles every frame.
     */
    int m_dragObjectPageIndex = -1;
    Document::TileCoord m_dragObjectTileCoord = {0, 0};
    
    /**
     * @brief Pre-render selected objects to cache at current zoom level.
     */
    void cacheSelectedObjectsForDrag();
    
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
    mutable QSizeF m_cachedContentSize;   ///< Cached total content size (computed during layout)
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
     * @brief Update cache capacity based on visible pages and layout mode.
     * 
     * Capacity = visible_pages + buffer (3 for 1-column, 6 for 2-column).
     * If capacity decreases, immediately evicts furthest entries.
     */
    void updatePdfCacheCapacity();
    
    /**
     * @brief Evict furthest cache entries until within capacity.
     * 
     * Must be called with m_pdfCacheMutex locked.
     * Evicts pages furthest from m_currentPageIndex first.
     */
    void evictFurthestCacheEntries();
    
    /**
     * @brief Invalidate page layout cache - call when pages added/removed/resized.
     */
    void invalidatePageLayoutCache() { m_pageLayoutDirty = true; }
    
    /**
     * @brief Check and apply auto-layout if enabled.
     * 
     * Called on resize and after zoom settles. Switches between SingleColumn
     * and TwoColumn based on viewport width vs 2 * page_width + gap.
     */
    void checkAutoLayout();
    
    /**
     * @brief Recenter content horizontally in viewport.
     * 
     * Called when layout mode changes to ensure content remains centered.
     * Sets pan X to a negative value so content appears centered when
     * narrower than the viewport.
     */
    void recenterHorizontally();
    
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
    
    // ===== Object Selection Tool Handlers (Phase O2) =====
    
    /**
     * @brief Handle pointer press for object selection tool.
     * Hit tests for objects, handles selection with Shift modifier.
     */
    void handlePointerPress_ObjectSelect(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer move for object selection tool.
     * Updates hover state and handles object dragging.
     */
    void handlePointerMove_ObjectSelect(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer release for object selection tool.
     * Finalizes object drag operation.
     */
    void handlePointerRelease_ObjectSelect(const PointerEvent& pe);
    
    /**
     * @brief Clear the current object selection.
     */
    void clearObjectSelection();
    
    /**
     * @brief Relocate selected objects to correct tiles after movement (edgeless mode).
     * 
     * Phase O2.3.4: Called after drag ends to handle tile boundary crossing.
     * For each selected object, checks if its position puts it in a different tile.
     * If so, extracts from old tile and adds to new tile with adjusted position.
     * 
     * @return Number of objects that were relocated to different tiles.
     */
    int relocateObjectsToCorrectTiles();
    
    /**
     * @brief Render object selection visual feedback.
     * Draws bounding boxes, handles (for single selection), and hover highlight.
     * @param painter The painter to render to.
     */
    void renderObjectSelection(QPainter& painter);
    
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
    QRectF getSelectionVisualBounds() const;  ///< P2: Visual bounds in viewport coords for dirty region
    
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
     * @brief Add a stroke to edgeless tiles with proper splitting at tile boundaries.
     * 
     * Takes a stroke with points in DOCUMENT coordinates, splits it at tile boundaries,
     * and adds each segment to the appropriate tile in tile-local coordinates.
     * This is the same logic used by finishStrokeEdgeless() for consistent behavior.
     * 
     * @param stroke The stroke in document coordinates
     * @param layerIndex Which layer to add the stroke to
     * @return Vector of (tileCoord, localStroke) pairs for undo tracking
     */
    QVector<QPair<Document::TileCoord, VectorStroke>> addStrokeToEdgelessTiles(
        const VectorStroke& stroke, int layerIndex);
    
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
    
    // ===== Highlighter Tool Methods (Phase A) =====
    
    /**
     * @brief Handle pointer press for highlighter tool.
     * Starts text selection if on a PDF page.
     */
    void handlePointerPress_Highlighter(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer move for highlighter tool.
     * Updates text selection rectangle and hit-tested text boxes.
     */
    void handlePointerMove_Highlighter(const PointerEvent& pe);
    
    /**
     * @brief Handle pointer release for highlighter tool.
     * Finalizes text selection.
     */
    void handlePointerRelease_Highlighter(const PointerEvent& pe);
    
    /**
     * @brief Load text boxes from PDF for the specified page.
     * @param pageIndex The page to load text boxes for.
     * 
     * Caches text boxes in m_textBoxCache for hit testing.
     * No-op if page has no PDF background.
     */
    void loadTextBoxesForPage(int pageIndex);
    
    /**
     * @brief Clear the text box cache.
     */
    void clearTextBoxCache();
    
    // ===== PDF Link Support (Phase D.1) =====
    
    /**
     * @brief Load PDF links for a page into cache.
     * @param pageIndex The page to load links for.
     */
    void loadLinksForPage(int pageIndex);
    
    /**
     * @brief Clear the link cache.
     */
    void clearLinkCache();
    
    /**
     * @brief Find a PDF link at the given page position.
     * @param pagePos Position in page coordinates (96 DPI).
     * @param pageIndex The page to search.
     * @return Pointer to the link if found, nullptr otherwise.
     */
    const PdfLink* findLinkAtPoint(const QPointF& pagePos, int pageIndex);
    
    /**
     * @brief Activate a PDF link (navigate or open URL).
     * @param link The link to activate.
     */
    void activatePdfLink(const PdfLink& link);
    
    /**
     * @brief Update cursor based on hover state over PDF links.
     * @param viewportPos Current pointer position in viewport coordinates.
     */
    void updateLinkCursor(const QPointF& viewportPos);
    
    /**
     * @brief Check if highlighter tool is enabled for current page.
     * @return True if current page has PDF background.
     */
    bool isHighlighterEnabled() const;
    
    /**
     * @brief Find the character at a given point (for text-flow selection).
     * @param pdfPos Point in PDF coordinates (72 DPI).
     * @return CharacterPosition with boxIndex and charIndex, or invalid if not found.
     */
    CharacterPosition findCharacterAtPoint(const QPointF& pdfPos) const;
    
    /**
     * @brief Update selected text and highlight rects from start/end positions.
     * Called after changing startBoxIndex/endBoxIndex etc.
     */
    void updateSelectedTextAndRects();
    
    /**
     * @brief Finalize the current text selection.
     * Emits textSelected signal with combined text.
     */
    void finalizeTextSelection();
    
    /**
     * @brief Select the word at the given point (double-click).
     * @param pagePos Position in page coordinates.
     * @param pageIndex Page index.
     */
    void selectWordAtPoint(const QPointF& pagePos, int pageIndex);
    
    /**
     * @brief Select the entire line at the given point (triple-click).
     * @param pagePos Position in page coordinates.
     * @param pageIndex Page index.
     */
    void selectLineAtPoint(const QPointF& pagePos, int pageIndex);
    
    /**
     * @brief Copy selected text to system clipboard.
     */
    void copySelectedTextToClipboard();
    
    /**
     * @brief Render the text selection overlay.
     * @param painter The painter to render to (page-transformed).
     * @param pageIndex The page being rendered.
     */
    void renderTextSelectionOverlay(QPainter& painter, int pageIndex);
    
    /**
     * @brief Create a marker-style stroke for a highlight rectangle (Phase B.6).
     * 
     * Creates a horizontal stroke through the center of the rectangle,
     * with width equal to the rectangle height (text line height).
     * Used to convert text selection highlight rects to VectorStrokes.
     * 
     * @param rect Rectangle in page coordinates (96 DPI).
     * @param color Highlight color (typically m_highlighterColor).
     * @return VectorStroke configured as a horizontal marker.
     */
    VectorStroke createHighlightStroke(const QRectF& rect, const QColor& color) const;
    
    /**
     * @brief Create highlight strokes from current text selection (Phase B.3).
     * 
     * Converts each rectangle in m_textSelection.highlightRects to a VectorStroke
     * and adds it to the current layer on the selection's page.
     * Each stroke gets its own undo action (can be undone individually).
     * Clears the text selection after creating strokes.
     * 
     * @return List of created stroke IDs.
     */
    QVector<QString> createHighlightStrokes();
    
    /**
     * @brief Update cursor based on Highlighter tool availability.
     * Sets IBeamCursor on PDF pages, ForbiddenCursor on non-PDF pages,
     * and restores ArrowCursor when Highlighter is not active.
     */
    void updateHighlighterCursor();
    
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
     * @brief Push a complete undo action (for complex operations like TransformSelection).
     * @param pageIndex The page index.
     * @param action The complete action with all fields populated.
     */
    void pushUndoAction(int pageIndex, const PageUndoAction& action);
    
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
     * @brief Render a single tile's strokes and objects.
     * @deprecated Use renderTileLayerStrokes() for proper layer interleaving.
     * Renders only the strokes/objects of a tile (no background).
     * Used when backgrounds are pre-rendered for the entire visible area.
     * @param painter The QPainter to render to (already translated to tile origin).
     * @param tile The tile (Page) to render.
     * @param coord The tile coordinate (for debugging).
     */
    void renderTileStrokes(QPainter& painter, Page* tile, Document::TileCoord coord);
    
    /**
     * @brief Render a single layer's strokes from a tile.
     * Used for multi-pass edgeless rendering with layer-interleaved objects.
     * @param painter The QPainter to render to (already translated to tile origin).
     * @param tile The tile (Page) to render from.
     * @param layerIdx The layer index to render.
     */
    void renderTileLayerStrokes(QPainter& painter, Page* tile, int layerIdx);
    
    /**
     * @brief Render objects with a specific affinity from all loaded tiles.
     * This enables layer-interleaved rendering for edgeless mode:
     * - renderEdgelessObjectsWithAffinity(-1) → objects below all strokes
     * - renderEdgelessObjectsWithAffinity(0)  → objects above Layer 0
     * - renderEdgelessObjectsWithAffinity(1)  → objects above Layer 1
     * 
     * Objects are rendered at document coordinates, allowing them to
     * extend across tile boundaries without clipping.
     * 
     * @param painter The QPainter to render to (in document coordinates).
     * @param affinity The layer affinity value to render.
     * @param allTiles The list of all tiles to check for objects.
     */
    void renderEdgelessObjectsWithAffinity(QPainter& painter, int affinity, 
                                            const QVector<Document::TileCoord>& allTiles);
    
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
