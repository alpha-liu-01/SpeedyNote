#ifndef INKCANVAS_H
#define INKCANVAS_H

#include <QWidget>
#include <QTabletEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QElapsedTimer>
#include <deque>
#include <QColor>
#include "core/ToolType.h"
#include <QImage>
#include <poppler-qt6.h>
#include <QCache>
#include <QTimer>
#include <QMenu>
#include <QClipboard>
#include <QFutureWatcher>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include "PictureWindowManager.h"
#include "MarkdownNoteEntry.h"
// ToolType.h already included above
#include "ButtonMappingTypes.h"
#include "SpnPackageManager.h"
#include "PdfRelinkDialog.h"

class MainWindow; // Forward declaration

class PictureWindowManager;
class PictureWindow;
class VectorCanvas; // Forward declaration for vector overlay

// Guard to prevent redefinition if MainWindow.h is also included
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,     // Touch gestures completely off
    YAxisOnly,    // Only Y-axis panning allowed (X-axis and zoom locked)
    Full          // Full touch gestures (panning and zoom)
};
#endif

enum class BackgroundStyle {
    None,
    Grid,
    Lines
};

// Structure to store a persistent text highlight
struct TextHighlight {
    QString id;              // Unique ID for this highlight (for linking to markdown windows)
    int pageNumber;          // Page number (0-based)
    QRectF boundingBox;      // Combined bounding box in PDF coordinates (for quick intersection checks)
    QList<QRectF> textBoxRects; // Individual text box rectangles for precise rendering
    QString text;            // The highlighted text content
    QColor color;            // Highlight color
    QString markdownWindowId; // ID of associated markdown window (empty if none) - for Step 3
    
    // Serialization helpers
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["pageNumber"] = pageNumber;
        obj["boundingBox"] = QString("%1,%2,%3,%4")
            .arg(boundingBox.x()).arg(boundingBox.y())
            .arg(boundingBox.width()).arg(boundingBox.height());
        
        // Serialize individual text box rectangles
        QJsonArray rectsArray;
        for (const QRectF &rect : textBoxRects) {
            rectsArray.append(QString("%1,%2,%3,%4")
                .arg(rect.x()).arg(rect.y())
                .arg(rect.width()).arg(rect.height()));
        }
        obj["textBoxRects"] = rectsArray;
        
        obj["text"] = text;
        obj["color"] = color.name(QColor::HexArgb);
        obj["markdownWindowId"] = markdownWindowId;
        return obj;
    }
    
    static TextHighlight fromJson(const QJsonObject &obj) {
        TextHighlight highlight;
        highlight.id = obj["id"].toString();
        highlight.pageNumber = obj["pageNumber"].toInt();
        
        // Parse combined bounding box
        QString bbox = obj["boundingBox"].toString();
        QStringList parts = bbox.split(',');
        if (parts.size() == 4) {
            highlight.boundingBox = QRectF(
                parts[0].toDouble(), parts[1].toDouble(),
                parts[2].toDouble(), parts[3].toDouble()
            );
        }
        
        // Parse individual text box rectangles
        QJsonArray rectsArray = obj["textBoxRects"].toArray();
        for (const QJsonValue &value : rectsArray) {
            QString rectStr = value.toString();
            QStringList rectParts = rectStr.split(',');
            if (rectParts.size() == 4) {
                highlight.textBoxRects.append(QRectF(
                    rectParts[0].toDouble(), rectParts[1].toDouble(),
                    rectParts[2].toDouble(), rectParts[3].toDouble()
                ));
            }
        }
        
        highlight.text = obj["text"].toString();
        highlight.color = QColor(obj["color"].toString());
        highlight.markdownWindowId = obj["markdownWindowId"].toString();
        return highlight;
    }
};

class InkCanvas : public QWidget {
    Q_OBJECT

signals:
    void zoomChanged(int newZoom);
    void panChanged(int panX, int panY);
    void touchGestureEnded(); // Signal emitted when touch gestures end
    void touchPanningChanged(bool active); // Signal emitted when touch panning starts/stops (for window performance optimization)
    void ropeSelectionCompleted(const QPoint &position); // Signal emitted when rope tool selection is completed
    void pdfLinkClicked(int targetPage); // Signal emitted when a PDF link is clicked
    void pdfTextSelected(const QString &text); // Signal emitted when PDF text is selected
    void pdfTextSelectionCleared(); // Signal emitted when PDF text selection is cleared (after menu action or tap outside)
    void pdfLoaded(); // Signal emitted when a PDF is loaded
    void autoScrollRequested(int direction); // Signal for autoscrolling to next/prev page
    void earlySaveRequested(); // Signal for proactive save before autoscroll threshold
    void markdownNotesUpdated(); // Signal emitted when markdown notes are added/updated/removed
    void highlightDoubleClicked(const QString &highlightId); // Signal emitted when a highlight is double-clicked

public:
    explicit InkCanvas(QWidget *parent = nullptr);
    virtual ~InkCanvas();

    void startBenchmark();
    void stopBenchmark();
    int getProcessedRate();
    void setPenColor(const QColor &color); // Added function to set pen color
    void setPenThickness(qreal thickness); // Added function to set pen thickness
    void adjustAllToolThicknesses(qreal zoomRatio); // Adjust all tool thicknesses for zoom changes
    void setTool(ToolType tool);
    void setSaveFolder(const QString &folderPath); // Function to set save folder
    void saveToFile(int pageNumber); // Function to save canvas to file
    void saveCurrentPage();  // ✅ New function (see below)
    void loadPage(int pageNumber);
    void deletePage(int pageNumber);
    void clearCurrentPage(); // Clear all content (drawing + pictures) from current page
    void setBackground(const QString &filePath, int pageNumber);

    void setZoom(int zoomLevel);
    int getZoom() const;
    void updatePanOffsets(int xOffset, int yOffset);

    int getPanOffsetX() const;  // Getter for panOffsetX
    int getPanOffsetY() const;  // Getter for panOffsetY

    void setPanX(int xOffset);  // Setter for panOffsetX
    void setPanY(int yOffset);  // Setter for panOffsetY
    
    void setPanWithTouchScroll(int xOffset, int yOffset);  // Efficient pan update for touch gestures

    void loadPdf(const QString &pdfPath);
    void loadPdfPage(int pageNumber);

    bool isPdfLoadedFunc() const;
    int getTotalPdfPages() const;
    Poppler::Document* getPdfDocument() const;
    
    void clearPdf();
    void clearPdfNoDelete();

    QString getSaveFolder() const { return saveFolder; }
    QString getDisplayPath() const; // ✅ Get display path (.spn package or folder)
    void syncSpnPackage(); // ✅ Sync changes back to .spn file
    void syncSpnPackageDeferred(); // ✅ OPTIMIZATION: Deferred sync with debouncing
    
    // ✅ Cache management helpers
    void invalidateBothPagesCache(int pageNumber); // Invalidate both pages of a combined canvas
    void insertPageIntoCache(int pageNumber, const QPixmap &pixmap); // Insert a page directly into cache (for smooth page switching)

    void setLastActivePage(int page) { lastActivePage = page; }
    int getLastActivePage() const { return lastActivePage; }
    // ✅ Zoom & Pan Management
    void setLastZoomLevel(int zoom) { lastZoomLevel = zoom; }
    int getLastZoomLevel() const { return lastZoomLevel; }

    void setLastPanX(int pan) { lastPanX = pan; }
    int getLastPanX() const { return lastPanX; }

    void setLastPanY(int pan) { lastPanY = pan; }
    int getLastPanY() const { return lastPanY; }
    QColor getPenColor(); // Added getter for pen color
    qreal getPenThickness(); // Added getter for pen thickness
    ToolType getCurrentTool(); // Added getter for tool type
    
    // Vector canvas overlay
    VectorCanvas* getVectorCanvas() const { return vectorCanvas; }
    void initializeVectorCanvas(); // Initialize the vector overlay on first page

    // Straight line mode toggle
    void setStraightLineMode(bool enabled) { straightLineMode = enabled; }
    bool isStraightLineMode() const { return straightLineMode; }
    
    // Reset straight line start point to current position (used when enabling via stylus button)
    void resetStraightLineStartPoint();

    // Rope tool mode toggle
    void setRopeToolMode(bool enabled) { 
        ropeToolMode = enabled;
#ifdef Q_OS_WIN
        // Clear auto-disable flag when mode is manually set to avoid incorrect auto-disable
        // from a previous Windows stylus hover lasso operation
        if (enabled) {
            windowsLassoNeedsAutoDisable = false;
        }
#endif
    }
    bool isRopeToolMode() const { return ropeToolMode; }

    void loadPdfPreviewAsync(int pageNumber);  // ✅ Load a quick preview of the PDF page
    // for notebook background below
    void setBackgroundStyle(BackgroundStyle style);
    void setBackgroundColor(const QColor &color);
    void setBackgroundDensity(int density);

    BackgroundStyle getBackgroundStyle() const { return backgroundStyle; }
    QColor getBackgroundColor() const { return backgroundColor; }
    int getBackgroundDensity() const { return backgroundDensity; }

    // saveBackgroundMetadata moved to unified JSON metadata section

    int getBufferWidth() const { return buffer.width(); }
    int getBufferHeight() const { return buffer.height(); }
    QPixmap getBuffer() const { return buffer; } // Get buffer for concurrent saving



    bool isEdited() const { return edited; }  // ✅ Check if the canvas has been edited
    void setEdited(bool state) { edited = state; }  // ✅ Set the edited state

    void setPDFRenderDPI(int dpi) { pdfRenderDPI = dpi; }  // ✅ Set PDF render DPI

    // PDF inversion for dark mode
    void setPdfInversionEnabled(bool enabled);
    bool isPdfInversionEnabled() const { return pdfInversionEnabled; }

    void clearPdfCache() { 
        QMutexLocker locker(&pdfCacheMutex);
        pdfCache.clear(); 
    }
    
    // ✅ OPTIMIZATION: Invalidate only specific page(s) from PDF cache
    // Use this instead of clearPdfCache() when only specific pages are affected (e.g., adding highlight)
    void invalidatePdfPageCache(int pageNumber) {
        QMutexLocker locker(&pdfCacheMutex);
        // Combined canvas (N-1, N) shows page N on bottom half
        // Combined canvas (N, N+1) shows page N on top half
        // So we need to invalidate both cache entries
        if (pageNumber > 0) {
            pdfCache.remove(pageNumber - 1);
            pdfCacheAccessOrder.removeAll(pageNumber - 1);
        }
        pdfCache.remove(pageNumber);
        pdfCacheAccessOrder.removeAll(pageNumber);
    }
    void clearNoteCache() { 
        {
            QMutexLocker locker(&noteCacheMutex);
            noteCache.clear(); 
        }
        currentCachedNotePage = -1;
        if (noteCacheTimer && noteCacheTimer->isActive()) {
            noteCacheTimer->stop();
        }
        
        // Cancel and clean up any active note watchers
        for (QFutureWatcher<void>* watcher : activeNoteWatchers) {
            if (watcher) {
                if (!watcher->isFinished()) {
                    watcher->cancel();
                }
                watcher->deleteLater();
            }
        }
        activeNoteWatchers.clear();
    }

    // Touch and trackpad gesture support
    void setTouchGestureMode(TouchGestureMode mode) { touchGestureMode = mode; }
    TouchGestureMode getTouchGestureMode() const { return touchGestureMode; }
    bool isTouchPanningActive() const { return isTouchPanning; }
    bool isTrackpadScrollingActive() const { return isTrackpadScrolling; }
    bool isTrackpadPinchZoomingActive() const { return isTrackpadPinchZooming; }
    void handleTrackpadScroll(qreal pixelDeltaX, qreal pixelDeltaY, bool gestureStart, bool gestureEnd);
    void handleTrackpadPinchZoom(qreal scaleFactor, QPointF centerPoint);
    void setCtrlKeyPhysicallyPressed(bool pressed) { 
        ctrlKeyPhysicallyPressed = pressed;
        if (pressed) {
            ctrlKeyPressTimer.start(); // Record when Ctrl was pressed
        }
    }
    bool isCtrlKeyPhysicallyPressed() const { return ctrlKeyPhysicallyPressed; }
    qint64 getCtrlKeyPressAge() const { return ctrlKeyPressTimer.isValid() ? ctrlKeyPressTimer.elapsed() : -1; }

    // Rope tool selection actions
    void deleteRopeSelection(); // Delete the current rope tool selection
    void cancelRopeSelection(); // Cancel the current rope tool selection
    void copyRopeSelection(); // Copy the current rope tool selection
    void copyRopeSelectionToClipboard(); // Copy the current rope tool selection to clipboard
    void clearInProgressLasso(); // Clear any in-progress lasso drawing without completing it
    
    PictureWindowManager* getPictureManager() const { return pictureManager; }
    
    // Picture integration
    void setPictureSelectionMode(bool enabled);
    bool isPictureSelectionMode() const;
    void setPictureWindowEditMode(bool enabled);
    
    // Combined canvas helpers for picture window page determination
    bool isCombinedCanvasMode() const;
    int getSinglePageHeight() const;
    int getPageNumberForCanvasY(int canvasY) const; // Returns correct page for Y coordinate

    // PDF text selection and link functionality
    void setPdfTextSelectionEnabled(bool enabled) { 
        pdfTextSelectionEnabled = enabled; 
        
        // Enable mouse tracking for PDF text selection
        setMouseTracking(enabled);
        
        // Change cursor when entering/exiting text selection mode
        if (enabled && isPdfLoaded) {
            setCursor(Qt::IBeamCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        
        // Clear any existing selection when disabling
        if (!enabled) {
            clearPdfTextSelection();
        }
        
        update(); // Refresh the display
    }
    bool isPdfTextSelectionEnabled() const { return pdfTextSelectionEnabled; }
    void clearPdfTextSelection(); // Clear current PDF text selection
    QString getSelectedPdfText() const; // Get currently selected PDF text
    bool hasSelectedPdfText() const { return !selectedTextBoxes.isEmpty(); } // Check if text is selected
    
    // Persistent text highlight management
    void addHighlightFromSelection(); // Add a persistent highlight from current selection
    void removeHighlightAtSelection(); // Remove highlight(s) that overlap with current selection
    bool isSelectionHighlighted() const; // Check if current selection overlaps with any persistent highlight
    QList<TextHighlight> getHighlightsForPage(int pageNumber) const; // Get all highlights for a specific page
    void loadHighlightsFromMetadata(); // Load highlights from JSON metadata
    void saveHighlightsToMetadata(); // Save highlights to JSON metadata
    
    // Markdown notes management
    QString addMarkdownNoteFromSelection(); // Add a markdown note linked to current selection, returns note ID
    void addMarkdownNote(const MarkdownNoteData &note); // Add a markdown note directly
    void updateMarkdownNote(const MarkdownNoteData &note); // Update an existing markdown note
    void removeMarkdownNote(const QString &noteId); // Remove a markdown note
    void clearHighlightsForPage(int pageNumber); // Clear all highlights for a specific page
    void clearMarkdownNotesForPage(int pageNumber); // Clear all markdown notes for a specific page
    MarkdownNoteData* findMarkdownNote(const QString &noteId); // Find a note by ID
    QList<MarkdownNoteData> getMarkdownNotesForPages(int page1, int page2 = -1) const; // Get notes for page(s) in combined canvas
    QList<MarkdownNoteData> getAllMarkdownNotes() const; // Get all notes in the notebook
    void linkHighlightToNote(const QString &highlightId, const QString &noteId); // Link a highlight to a note
    TextHighlight* findHighlightById(const QString &highlightId); // Find a highlight by ID
    void handleHighlightDoubleClick(const QString &highlightId); // Handle double-click on highlight

    // Canvas coordinate system support
    QSize getCanvasSize() const { return buffer.size(); }
    QRect getCanvasRect() const { return QRect(0, 0, buffer.width(), buffer.height()); }
    qreal getZoomFactor() const { return zoomFactor / 100.0; }
    QPointF getPanOffset() const { return QPointF(panOffsetX, panOffsetY); }
    
    // Coordinate conversion methods
    QPointF mapWidgetToCanvas(const QPointF &widgetPoint) const;
    QPointF mapCanvasToWidget(const QPointF &canvasPoint) const;
    QRect mapWidgetToCanvas(const QRect &widgetRect) const;
    QRect mapCanvasToWidget(const QRect &canvasRect) const;
    
    // Autoscroll threshold getter for MainWindow
    int getAutoscrollThreshold() const;
    
    // Background image getter for MainWindow
    QPixmap getBackgroundImage() const { return backgroundImage; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override; // Handle resizing
    
    // Touch event handling
    bool event(QEvent *event) override;

private:
    QPointF mapLogicalWidgetToPhysicalBuffer(const QPointF& logicalWidgetPoint);
    QRect mapRectBufferToWidgetLogical(const QRectF& physicalBufferRect);
    
    // Helper function to get correct DPI scale factor (Wayland-aware)
    qreal getEffectiveDpiScale(QScreen *screen = nullptr) const;

    QPixmap buffer;            // Off-screen buffer
    QImage background;
    QPointF lastPoint;
    QPointF straightLineStartPoint;  // Stores the start point for straight line mode
    bool drawing;
    QColor penColor; // Added pen color property
    qreal penThickness; // Added pen thickness property
    ToolType currentTool;
    ToolType previousTool; // To restore tool after erasing
    
    // Separate thickness values for each tool
    qreal penToolThickness = 5.0;    // Default pen thickness
    qreal markerToolThickness = 5.0; // Default marker thickness (will be scaled by 8x in drawing)
    qreal eraserToolThickness = 5.0; // Default eraser thickness (will be scaled by 6x in drawing)
    
    // Vector canvas overlay (for vector-based drawing with undo support)
    VectorCanvas* vectorCanvas = nullptr;
    
    QString saveFolder;
    QString actualPackagePath; // ✅ Store .spn package path for display purposes
    QString tempWorkingDir; // ✅ Temporary directory for .spn packages
    bool isSpnPackage = false; // ✅ Flag to indicate if working with .spn package
    QString notebookId;
    QPixmap backgroundImage;
    bool straightLineMode = false;  // Flag for straight line mode
    bool ropeToolMode = false; // Flag for rope tool mode
    QPixmap selectionBuffer; // Buffer for the selected area in rope tool mode (physical pixels, masked)
    QRect selectionRect; // Bounding rectangle of the current selection in LOGICAL WIDGET coordinates
    QRectF exactSelectionRectF; // Floating-point version for smoother movement
    QPolygonF lassoPathPoints; // Points of the lasso selection in LOGICAL WIDGET coordinates
    bool selectingWithRope = false; // True if currently drawing the lasso
    bool movingSelection = false; // True if currently moving the selection
    
#ifdef Q_OS_WIN
    // Windows-specific: Track if we're in stylus hover mode (side button pressed while hovering)
    // On Windows, stylus side buttons only work while hovering, not when pen tip touches screen
    bool windowsStylusHoverDrawing = false;
    bool windowsHoverWasStraightLine = false;  // True if hover mode was for straight line
    bool windowsHoverWasLasso = false;         // True if hover mode was for lasso
    bool windowsLassoNeedsAutoDisable = false; // True if lasso mode should auto-disable after interaction
#endif
    bool selectionJustCopied = false; // True if selection was just copied and hasn't been moved yet
    bool selectionAreaCleared = false; // True if the selection area has been cleared from the buffer
    QPainterPath selectionMaskPath; // Path used to clear the selection area from buffer
    QRectF selectionBufferRect; // Buffer rectangle for the selection area
    QPointF lastMovePoint; // Last point during selection movement (logical widget coordinates)

    int zoomFactor;     // Zoom percentage (100 = normal)
    int panOffsetX;     // Horizontal pan offset
    int panOffsetY;     // Vertical pan offset
    
    // Internal floating-point zoom for smoother gestures
    qreal internalZoomFactor = 100.0;

    void initializeBuffer();   // Helper to initialize the buffer
    void drawStroke(const QPointF &start, const QPointF &end, qreal pressure);    
    void eraseStroke(const QPointF &start, const QPointF &end, qreal pressure);
    QRectF calculatePreviewRect(const QPointF &start, const QPointF &oldEnd, const QPointF &newEnd);
    

    QCache<int, QPixmap> pdfCache; // Caches 5 pages of the PDF
    mutable QMutex pdfCacheMutex; // Thread safety for pdfCache
    QList<int> pdfCacheAccessOrder; // Track access order for LRU eviction (most recent at end)
    
    // ✅ PDF TEXT BOX CACHE: Cache text boxes to avoid re-allocation on every page visit
    struct TextBoxCacheEntry {
        QList<Poppler::TextBox*> textBoxes;
        QList<int> pageNumbers;
        ~TextBoxCacheEntry() {
            qDeleteAll(textBoxes);
        }
    };
    QMap<int, TextBoxCacheEntry*> pdfTextBoxCache; // Maps page number -> text boxes
    QList<int> pdfTextBoxCacheAccessOrder; // Track access order for LRU eviction
    
    std::unique_ptr<Poppler::Document> pdfDocument;
    int currentPdfPage;
    bool isPdfLoaded = false;
    int totalPdfPages = 0;

    int lastActivePage = 0;  // ✅ Stores last active page per tab
    int lastZoomLevel = 100;  // ✅ Default zoom level
    int lastPanX = 0;  // ✅ Default pan X
    int lastPanY = 0;  // ✅ Default pan Y

    // QImage *pdfImage = nullptr;  // ✅ Image for the current PDF page

    bool edited = false;  // ✅ Track if the canvas has been edited

    bool benchmarking;
    std::deque<qint64> processedTimestamps;
    QElapsedTimer benchmarkTimer;

    // ✅ Unified JSON metadata
    QString pdfPath;
    int lastAccessedPage = 0;
    BackgroundStyle backgroundStyle = BackgroundStyle::None;
    QColor backgroundColor = Qt::white;
    int backgroundDensity = 20;
    QStringList bookmarks; // ✅ Add bookmarks to JSON metadata

public:
    // ✅ Metadata management
    void loadNotebookId();
    
    // Combined canvas window management (picture windows only)
    void saveCombinedWindowsForPage(int pageNumber); // Save picture windows for combined canvas pages
    void loadCombinedWindowsForPage(int pageNumber); // Load picture windows for combined canvas pages
    
    // Autoscroll threshold detection (for touch gestures and external pan changes)
    void checkAutoscrollThreshold(int oldPanY, int newPanY);
    void saveNotebookId();
    void saveBackgroundMetadata();
    
    // ✅ New unified JSON metadata system
    void loadNotebookMetadata();
    void saveNotebookMetadata();
    void saveNotebookMetadataDeferred(); // ✅ OPTIMIZATION: Deferred save with debouncing
    void flushPendingMetadataSave(); // ✅ Force immediate save if pending
    void flushPendingSpnSync(); // ✅ Force immediate .spn sync if pending
    void setLastAccessedPage(int pageNumber);
    int getLastAccessedPage() const;
    QString getPdfPath() const; // ✅ Get PDF path from JSON metadata
    QString getNotebookId() const; // ✅ Get notebook ID from JSON metadata
    bool handleMissingPdf(QWidget *parent = nullptr); // ✅ Handle missing PDF file
    
    // ✅ Bookmark management through JSON
    void addBookmark(const QString &bookmark);
    void removeBookmark(const QString &bookmark);
    QStringList getBookmarks() const;
    void setBookmarks(const QStringList &bookmarkList);
    
    // ✅ Image reference counting for picture windows
    void incrementImageReference(const QString &imageHash);
    bool decrementImageReference(const QString &imageHash); // Returns true if file should be deleted (count reached 0)
    int getImageReferenceCount(const QString &imageHash) const;
    
private:
    void rebuildImageReferences(); // Rebuild reference counts by scanning all picture metadata files
    MainWindow* findMainWindow(); // Helper to find parent MainWindow
    QString extractImageHashFromPath(const QString &imagePath) const; // Extract hash portion from image filename
    
    // Combined canvas window management
    QList<PictureWindow*> loadPictureWindowsForPage(int pageNumber); // Load picture windows without affecting current
    
    // PDF text selection helpers for combined canvas
    void loadPdfTextBoxesForSinglePage(int pageNumber, TextBoxCacheEntry* entry); // Load text boxes for single page
    void loadPdfTextBoxesForCombinedCanvas(int pageNumber, int singlePageHeight, TextBoxCacheEntry* entry); // Load text boxes for combined canvas
    // ✅ Migration from old txt files to JSON
    void migrateOldMetadataFiles();
    
    // ✅ Clipboard image paste functionality
    QString pasteImageFromClipboard(); // Returns path to saved clipboard image or empty string on failure

    int pdfRenderDPI = 192;  // Default to 288 DPI
    bool pdfInversionEnabled = false;  // PDF color inversion for dark mode

    // Touch gesture support
    TouchGestureMode touchGestureMode = TouchGestureMode::Disabled;
    QPointF lastTouchPos;
    qreal lastPinchScale = 1.0;
    bool isPanning = false;
    int activeTouchPoints = 0;
    
    // Efficient touch pan scrolling (Squid-style optimization)
    bool isTouchPanning = false; // True when actively panning with touch (not mouse wheel)
    QPixmap cachedFrame; // Cached frame for efficient touch panning (canvas region only)
    QRect cachedCanvasRegion; // Canvas region position when cached (for performance optimization)
    QPoint cachedFrameOffset; // Offset of cached frame during panning
    int touchPanStartX = 0; // Pan X value when touch gesture started
    int touchPanStartY = 0; // Pan Y value when touch gesture started
    
    // Touch panning timeout (Linux fix for stuck isTouchPanning state)
    QTimer* touchPanningTimeoutTimer = nullptr; // Timer to reset isTouchPanning if no events arrive
    void resetTouchPanningState(); // Called by timeout timer to clear stuck state
    
    // Inertia scrolling (momentum scrolling)
    QTimer* inertiaTimer = nullptr; // Timer for inertia animation
    qreal inertiaVelocityX = 0.0; // Current inertia velocity X
    qreal inertiaVelocityY = 0.0; // Current inertia velocity Y
    
    // Auto-save timer (incremental saves to reduce page-switch burden)
    QTimer* autoSaveTimer = nullptr; // Timer for periodic auto-save
    int autoSaveInterval = 10000; // Auto-save interval in milliseconds (default 10 seconds)
    
    // Deferred metadata save timer (debounce rapid page switches)
    QTimer* metadataSaveTimer = nullptr; // Timer for debouncing metadata saves
    bool metadataSavePending = false; // True if there's a pending metadata save
    
    // ✅ OPTIMIZATION: Deferred .spn package sync (avoids expensive re-pack on every save)
    QTimer* spnSyncTimer = nullptr; // Timer for debouncing .spn sync
    bool spnSyncPending = false; // True if there's a pending .spn sync
    qreal inertiaPanX = 0.0; // Smooth pan X with sub-pixel precision
    qreal inertiaPanY = 0.0; // Smooth pan Y with sub-pixel precision
    QPointF lastTouchVelocity; // Last measured velocity for inertia
    QElapsedTimer velocityTimer; // Timer for velocity calculation
    QList<QPair<QPointF, qint64>> recentVelocities; // Recent velocities for smoothing
    
    // Page switch cooldown during inertia
    QElapsedTimer pageSwitchCooldown; // Prevents rapid page switches during inertia
    bool pageSwitchInProgress = false; // True when waiting for page switch to complete
    
    // Touch gesture continuation across page switches (for active dragging)
    bool continueTouchGestureAfterPageLoad = false; // True when we should continue gesture after page load
    int pendingContinuationPanX = 0; // Stored pan X to restore after page load
    int pendingContinuationPanY = 0; // Stored pan Y to restore after page load
    QPointF pendingContinuationTouchPos; // Touch position at the moment of page switch
    QElapsedTimer pageSwitchRecoveryTimer; // Timer to ignore spurious multi-touch events after page switch

    // Trackpad gesture support (scrolling and pinch-to-zoom)
    QTimer* trackpadScrollTimeoutTimer = nullptr;
    QTimer* trackpadPinchZoomTimeoutTimer = nullptr;
    QTimer* trackpadZoomAnimationTimer = nullptr;
    bool isTrackpadScrolling = false;
    bool isTrackpadPinchZooming = false;
    qreal targetZoomFactor = 100.0;
    QPointF trackpadZoomCenterPoint;
    QElapsedTimer trackpadVelocityTimer;
    QList<QPair<QPointF, qint64>> trackpadRecentVelocities;
    bool ctrlKeyPhysicallyPressed = false;
    QElapsedTimer ctrlKeyPressTimer;
    void resetTrackpadScrollingState();
    void resetTrackpadPinchZoomState();
    void updateTrackpadZoomAnimation();

    // Background style members (moved to unified JSON metadata section above)

    bool pdfTextSelectionEnabled = false;
    
    // PDF text selection members
    bool pdfTextSelecting = false; // True when actively selecting text
    QPointF pdfSelectionStart; // Start point of text selection (logical widget coordinates)
    QPointF pdfSelectionEnd; // End point of text selection (logical widget coordinates)
    QList<Poppler::TextBox*> currentPdfTextBoxes; // Text boxes for current page(s)
    QList<Poppler::TextBox*> selectedTextBoxes; // Currently selected text boxes (temporary selection)
    QList<int> currentPdfTextBoxPageNumbers; // Page number for each text box (for combined canvas)
    
    // Persistent text highlights storage
    QList<TextHighlight> persistentHighlights; // All saved highlights for this notebook
    
    // Markdown notes storage
    QList<MarkdownNoteData> markdownNotes; // All saved markdown notes for this notebook
    
    // ✅ Image reference counting for picture windows (key = hash portion of filename, value = count)
    QHash<QString, int> imageReferences;
    
    // ✅ MEMORY LEAK FIX: Cache only page sizes instead of full Page objects
    QMap<int, QSizeF> pdfPageSizeCache; // Maps page number -> page size
    int currentTextPageNumber = -1; // Track which page we're displaying text for
    int currentTextPageNumberSecond = -1; // For combined canvas
    int currentTextBoxesLoadedForPage = -1; // Track which page currentPdfTextBoxes belong to
    
    // PDF text selection throttling (60 FPS)
    QTimer* pdfTextSelectionTimer = nullptr; // Timer for throttling text selection updates
    QPointF pendingSelectionStart; // Pending selection start point
    QPointF pendingSelectionEnd; // Pending selection end point
    bool hasPendingSelection = false; // True if there's a pending selection update
    
    // Intelligent PDF cache system
    QTimer* pdfCacheTimer = nullptr; // Timer for delayed adjacent page caching
    int currentCachedPage = -1; // Currently displayed page for cache management
    int pendingCacheTargetPage = -1; // Target page for pending cache operation (to validate timer relevance)
    QList<QFutureWatcher<void>*> activePdfWatchers; // Track active PDF cache watchers for cleanup
    
    // Intelligent note page cache system
    QCache<int, QPixmap> noteCache; // Cache for note pages (PNG files)
    mutable QMutex noteCacheMutex; // Thread safety for noteCache
    QList<int> noteCacheAccessOrder; // Track access order for LRU eviction (most recent at end)
    QTimer* noteCacheTimer = nullptr; // Timer for delayed adjacent note page caching
    int currentCachedNotePage = -1; // Currently displayed note page for cache management
    int pendingNoteCacheTargetPage = -1; // Target page for pending note cache operation (to validate timer relevance)
    QList<QFutureWatcher<void>*> activeNoteWatchers; // Track active note cache watchers for cleanup
    
    PictureWindowManager* pictureManager = nullptr;
    
    // Picture selection state
    bool pictureSelectionMode = false;
    QPoint pictureSelectionStart;
    bool pictureSelecting = false;
    bool pictureWindowEditMode = false;
    
    // Picture window interaction state
    PictureWindow* activePictureWindow = nullptr;
    bool pictureDragging = false;
    bool pictureResizing = false;
    QPoint pictureInteractionStartPos;
    QRect pictureStartRect;
    QRect picturePreviousRect; // Track previous position to clear ghost images
    QRect picturePreviewRect;  // Store preview position during drag/resize for performance
    int pictureResizeHandle = 0; // Store as int to avoid incomplete type issues
    
    // Picture interaction helper methods
    void handlePictureMouseMove(QMouseEvent *event);
    void handlePictureMouseRelease(QMouseEvent *event);
    
    // Helper methods for PDF text selection
    void loadPdfTextBoxes(int pageNumber); // Load text boxes for a page
    QPointF mapWidgetToPdfCoordinates(const QPointF &widgetPoint); // Map widget coordinates to PDF coordinates
    QPointF mapPdfToWidgetCoordinates(const QPointF &pdfPoint, int pageNumber = -1); // Map PDF coordinates to widget coordinates
    void updatePdfTextSelection(const QPointF &start, const QPointF &end, bool isFinal = false); // Update text selection
    void handlePdfLinkClick(const QPointF &clickPoint); // Handle PDF link clicks
    void showPdfTextSelectionMenu(const QPoint &position); // Show context menu for PDF text selection
    QList<Poppler::TextBox*> getTextBoxesInSelection(const QPointF &start, const QPointF &end); // Get text boxes in selection area
    
    // Helper to check if individual text box rects truly overlap (not just bounding boxes)
    bool doTextBoxRectsOverlap(const QList<QRectF> &rects1, const QList<QRectF> &rects2) const;
    
    // Intelligent PDF cache helper methods
    void renderPdfPageToCache(int pageNumber); // Render a single page and add to cache
    void renderPdfPageToCacheThreadSafe(int pageNumber, Poppler::Document* sharedDocument); // Thread-safe render with separate document instance
    void checkAndCacheAdjacentPages(int targetPage); // Check and cache adjacent pages if needed
    bool isValidPageNumber(int pageNumber) const; // Check if page number is valid
    void drawHighlightsOnPageImage(QImage &pageImage, int pageNumber, Poppler::Document* pdfDoc); // Draw highlights on a PDF page image during rendering
    void refreshCurrentPdfPage(); // Refresh the currently displayed PDF page (for highlight updates)
    
    // Intelligent note cache helper methods
    void loadSingleNotePageToCache(int pageNumber); // Load a single note page and add to cache
    void checkAndCacheAdjacentNotePages(int targetPage); // Check and cache adjacent note pages if needed
    QString getNotePageFilePath(int pageNumber) const; // Get file path for note page
    
private slots:
    void processPendingTextSelection(); // Process pending text selection updates (throttled to 60 FPS)
    void cacheAdjacentPages(); // Cache adjacent pages after delay
    void cacheAdjacentPagesImmediately(); // Cache adjacent pages immediately (for highlight changes)
    void cacheAdjacentNotePages(); // Cache adjacent note pages after delay
    void updateInertiaScroll(); // Update inertia scrolling animation
    void onAutoSaveTimeout(); // Perform auto-save when timer expires
    void onMetadataSaveTimeout(); // Perform deferred metadata save when timer expires
    void onSpnSyncTimeout(); // ✅ Perform deferred .spn package sync when timer expires
};

#endif // INKCANVAS_H