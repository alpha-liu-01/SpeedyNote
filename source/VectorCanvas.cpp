#include "VectorCanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QUuid>
#include <QtMath>
#include <QLineF>

VectorCanvas::VectorCanvas(QWidget *parent)
    : QWidget(parent)
{
    // Enable tablet events
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setMouseTracking(true);
    
    // Transparent background - overlay on top of pixmap canvas
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    
    // Accept focus for keyboard shortcuts (undo)
    setFocusPolicy(Qt::StrongFocus);
}

VectorCanvas::~VectorCanvas() = default;

void VectorCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    // Benchmark: track paint timestamps
    if (benchmarking) {
        paintTimestamps.push_back(benchmarkTimer.elapsed());
    }
    
    QPainter painter(this);
    
    // ========== PERFORMANCE OPTIMIZATION: Stroke Caching ==========
    // Completed strokes are cached in a QPixmap and only rebuilt when changed.
    // This makes paint performance O(1) instead of O(n*points) during drawing.
    
    // Rebuild cache if needed (strokes added, removed, or canvas resized)
    // Compare physical sizes to detect resize (cache stores physical pixels)
    QSize expectedPhysicalSize = size() * devicePixelRatioF();
    if (strokeCacheDirty || strokeCache.size() != expectedPhysicalSize) {
        rebuildStrokeCache();
    }
    
    // Draw the cached completed strokes (single blit operation)
    if (!strokeCache.isNull()) {
        painter.drawPixmap(0, 0, strokeCache);
    }
    
    // Draw current stroke being drawn
    // Use INCREMENTAL rendering - only draw the last few segments, not the entire stroke
    // This is much faster than re-rendering the full polygon every frame
    if (drawing && !currentStroke.points.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderCurrentStrokeIncremental(painter);
    }
    
    // Draw eraser cursor when in eraser mode and input is active
    if (inputActive && currentTool == Tool::Eraser && underMouse()) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(lastPoint, eraserSize, eraserSize);
    }
}

void VectorCanvas::rebuildStrokeCache()
{
    // ========== HIGH-DPI FIX ==========
    // Create pixmap at physical pixel size, not logical size.
    // On 125% scaling, a 100x100 widget needs 125x125 pixels in the cache.
    qreal dpr = devicePixelRatioF();
    QSize physicalSize = size() * dpr;
    
    strokeCache = QPixmap(physicalSize);
    strokeCache.setDevicePixelRatio(dpr);  // Tell Qt this is a high-DPI pixmap
    strokeCache.fill(Qt::transparent);
    
    if (strokes.isEmpty()) {
        strokeCacheDirty = false;
        return;
    }
    
    QPainter cachePainter(&strokeCache);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    
    // Render all completed strokes to cache
    // QPainter automatically scales because we set devicePixelRatio on the pixmap
    for (const auto& stroke : strokes) {
        renderStroke(cachePainter, stroke);
    }
    
    strokeCacheDirty = false;
}

void VectorCanvas::renderStroke(QPainter& painter, const VectorStroke& stroke) const
{
    if (stroke.points.size() < 2) {
        // Single point - draw a dot
        if (stroke.points.size() == 1) {
            qreal radius = stroke.baseThickness * stroke.points[0].pressure / 2.0;
            painter.setPen(Qt::NoPen);
            painter.setBrush(stroke.color);
            painter.drawEllipse(stroke.points[0].pos, radius, radius);
        }
        return;
    }
    
    // ========== OPTIMIZATION: Render as filled polygon ==========
    // Instead of N drawLine() calls with varying widths, render the stroke
    // as a single filled polygon representing the variable-width outline.
    // This is much faster: 1 draw call instead of N, and GPU-friendly.
    
    const int n = stroke.points.size();
    
    // Pre-calculate half-widths for each point
    QVector<qreal> halfWidths(n);
    for (int i = 0; i < n; ++i) {
        qreal width = stroke.baseThickness * stroke.points[i].pressure;
        halfWidths[i] = qMax(width, 1.0) / 2.0;
    }
    
    // Build the stroke outline polygon
    // Left edge goes forward, right edge goes backward
    QVector<QPointF> leftEdge(n);
    QVector<QPointF> rightEdge(n);
    
    for (int i = 0; i < n; ++i) {
        const QPointF& pos = stroke.points[i].pos;
        qreal hw = halfWidths[i];
        
        // Calculate perpendicular direction
        QPointF tangent;
        if (i == 0) {
            // First point: use direction to next point
            tangent = stroke.points[1].pos - pos;
        } else if (i == n - 1) {
            // Last point: use direction from previous point
            tangent = pos - stroke.points[n - 2].pos;
        } else {
            // Middle points: average of incoming and outgoing directions
            tangent = stroke.points[i + 1].pos - stroke.points[i - 1].pos;
        }
        
        // Normalize tangent
        qreal len = qSqrt(tangent.x() * tangent.x() + tangent.y() * tangent.y());
        if (len < 0.0001) {
            // Degenerate case: use arbitrary perpendicular
            tangent = QPointF(1.0, 0.0);
            len = 1.0;
        }
        tangent /= len;
        
        // Perpendicular vector (rotate 90 degrees)
        QPointF perp(-tangent.y(), tangent.x());
        
        // Calculate left and right edge points
        leftEdge[i] = pos + perp * hw;
        rightEdge[i] = pos - perp * hw;
    }
    
    // Build polygon: left edge forward, then right edge backward
    QPolygonF polygon;
    polygon.reserve(n * 2 + 2);
    
    for (int i = 0; i < n; ++i) {
        polygon << leftEdge[i];
    }
    for (int i = n - 1; i >= 0; --i) {
        polygon << rightEdge[i];
    }
    
    // Draw filled polygon with WindingFill to handle self-intersections
    // OddEvenFill (default) leaves holes where stroke crosses itself
    // WindingFill fills all enclosed areas regardless of winding count
    painter.setPen(Qt::NoPen);
    painter.setBrush(stroke.color);
    painter.drawPolygon(polygon, Qt::WindingFill);
    
    // Draw round end caps for a smooth look
    qreal startRadius = halfWidths[0];
    qreal endRadius = halfWidths[n - 1];
    painter.drawEllipse(stroke.points[0].pos, startRadius, startRadius);
    painter.drawEllipse(stroke.points[n - 1].pos, endRadius, endRadius);
}

void VectorCanvas::resetCurrentStrokeCache()
{
    // Reset incremental rendering state for new stroke
    qreal dpr = devicePixelRatioF();
    QSize physicalSize = size() * dpr;
    
    currentStrokeCache = QPixmap(physicalSize);
    currentStrokeCache.setDevicePixelRatio(dpr);
    currentStrokeCache.fill(Qt::transparent);
    lastRenderedPointIndex = 0;
}

void VectorCanvas::renderCurrentStrokeIncremental(QPainter& painter)
{
    // ========== OPTIMIZATION: Incremental Stroke Rendering ==========
    // Instead of re-rendering the entire current stroke every frame,
    // we accumulate rendered segments in currentStrokeCache and only
    // render NEW segments to the cache.
    
    const int n = currentStroke.points.size();
    if (n < 1) return;
    
    // Ensure cache is valid
    QSize expectedSize = size() * devicePixelRatioF();
    if (currentStrokeCache.isNull() || currentStrokeCache.size() != expectedSize) {
        resetCurrentStrokeCache();
    }
    
    // Render new segments to the cache (if any)
    if (n > lastRenderedPointIndex && n >= 2) {
        QPainter cachePainter(&currentStrokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Use line-based rendering for incremental updates (fast)
        QPen pen(currentStroke.color, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
        // Start from the last rendered point (or 1 if starting fresh)
        int startIdx = qMax(1, lastRenderedPointIndex);
        
        // Render each new segment
        for (int i = startIdx; i < n; ++i) {
            const auto& p0 = currentStroke.points[i - 1];
            const auto& p1 = currentStroke.points[i];
            
            qreal avgPressure = (p0.pressure + p1.pressure) / 2.0;
            qreal width = qMax(currentStroke.baseThickness * avgPressure, 1.0);
            
            pen.setWidthF(width);
            cachePainter.setPen(pen);
            cachePainter.drawLine(p0.pos, p1.pos);
        }
        
        // Draw start cap if this is the first render
        if (lastRenderedPointIndex == 0 && n >= 1) {
            qreal startRadius = qMax(currentStroke.baseThickness * currentStroke.points[0].pressure, 1.0) / 2.0;
            cachePainter.setPen(Qt::NoPen);
            cachePainter.setBrush(currentStroke.color);
            cachePainter.drawEllipse(currentStroke.points[0].pos, startRadius, startRadius);
        }
        
        lastRenderedPointIndex = n;
    }
    
    // Blit the cached current stroke
    painter.drawPixmap(0, 0, currentStrokeCache);
    
    // Draw end cap at current position (always needs updating)
    if (n >= 1) {
        qreal endRadius = qMax(currentStroke.baseThickness * currentStroke.points[n - 1].pressure, 1.0) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(currentStroke.color);
        painter.drawEllipse(currentStroke.points[n - 1].pos, endRadius, endRadius);
    }
}

void VectorCanvas::tabletEvent(QTabletEvent *event)
{
    // CRITICAL: If input is not active, let events pass through to parent (InkCanvas)
    if (!inputActive) {
        event->ignore();
        return;
    }
    
    QPointF pos = event->position();
    qreal pressure = event->pressure();
    
    // Clamp pressure to valid range
    pressure = qBound(0.1, pressure, 1.0);
    
    switch (event->type()) {
        case QEvent::TabletPress:
            if (currentTool == Tool::Pen) {
                drawing = true;
                currentStroke = VectorStroke();
                currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                currentStroke.color = penColor;
                currentStroke.baseThickness = penThickness;
                resetCurrentStrokeCache();  // Reset incremental rendering cache
                addPoint(pos, pressure);
            } else if (currentTool == Tool::Eraser) {
                drawing = true;
                eraseAt(pos);
            }
            lastPoint = pos;
            event->accept();
            break;
            
        case QEvent::TabletMove:
            if (drawing) {
                if (currentTool == Tool::Pen) {
                    addPoint(pos, pressure);
                } else if (currentTool == Tool::Eraser) {
                    eraseAt(pos);
                }
            }
            lastPoint = pos;
            event->accept();
            break;
            
        case QEvent::TabletRelease:
            if (drawing) {
                if (currentTool == Tool::Pen) {
                    finishStroke();
                }
                drawing = false;
            }
            event->accept();
            break;
            
        default:
            break;
    }
}

void VectorCanvas::mousePressEvent(QMouseEvent *event)
{
    // CRITICAL: If input is not active, let events pass through to parent (InkCanvas)
    if (!inputActive) {
        event->ignore();
        return;
    }
    
    // Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    QPointF pos = event->position();
    qreal pressure = 0.5; // Default pressure for mouse
    
    if (currentTool == Tool::Pen) {
        drawing = true;
        currentStroke = VectorStroke();
        currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        currentStroke.color = penColor;
        currentStroke.baseThickness = penThickness;
        resetCurrentStrokeCache();  // Reset incremental rendering cache
        addPoint(pos, pressure);
    } else if (currentTool == Tool::Eraser) {
        drawing = true;
        eraseAt(pos);
    }
    lastPoint = pos;
    event->accept();
}

void VectorCanvas::mouseMoveEvent(QMouseEvent *event)
{
    // CRITICAL: If input is not active, let events pass through to parent (InkCanvas)
    if (!inputActive) {
        event->ignore();
        return;
    }
    
    // Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    QPointF pos = event->position();
    lastPoint = pos;
    
    if (drawing) {
        if (currentTool == Tool::Pen) {
            addPoint(pos, 0.5);
        } else if (currentTool == Tool::Eraser) {
            eraseAt(pos);
            update(); // Update eraser cursor
        }
    } else if (currentTool == Tool::Eraser) {
        update(); // Update eraser cursor position
    }
}

void VectorCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    // CRITICAL: If input is not active, let events pass through to parent (InkCanvas)
    if (!inputActive) {
        event->ignore();
        return;
    }
    
    // Reject touch-synthesized mouse events
    if (event->source() == Qt::MouseEventSynthesizedBySystem ||
        event->source() == Qt::MouseEventSynthesizedByQt) {
        event->ignore();
        return;
    }
    
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
    if (drawing) {
        if (currentTool == Tool::Pen) {
            finishStroke();
        }
        drawing = false;
    }
    event->accept();
}

void VectorCanvas::addPoint(const QPointF& pos, qreal pressure)
{
    // ========== OPTIMIZATION: Point Decimation ==========
    // At 360Hz, consecutive points are often <1 pixel apart.
    // Skip points that are too close to reduce memory and rendering work.
    // This typically reduces point count by 50-70% with no visible quality loss.
    
    static constexpr qreal MIN_DISTANCE_SQ = 1.5 * 1.5;  // 1.5 pixels squared
    
    if (!currentStroke.points.isEmpty()) {
        const QPointF& lastPos = currentStroke.points.last().pos;
        qreal dx = pos.x() - lastPos.x();
        qreal dy = pos.y() - lastPos.y();
        qreal distSq = dx * dx + dy * dy;
        
        if (distSq < MIN_DISTANCE_SQ) {
            // Point too close - but update pressure if higher (preserve pressure peaks)
            if (pressure > currentStroke.points.last().pressure) {
                currentStroke.points.last().pressure = pressure;
            }
            return;  // Skip this point
        }
    }
    
    StrokePoint pt;
    pt.pos = pos;
    pt.pressure = pressure;
    currentStroke.points.append(pt);
    
    // Update dirty region for efficient repaint
    qreal padding = penThickness * 2;
    QRectF pointRect(pos.x() - padding, pos.y() - padding, padding * 2, padding * 2);
    
    if (currentStroke.points.size() > 1) {
        // Include line from previous point
        const auto& prevPt = currentStroke.points[currentStroke.points.size() - 2];
        pointRect = pointRect.united(QRectF(prevPt.pos.x() - padding, prevPt.pos.y() - padding, 
                                            padding * 2, padding * 2));
    }
    
    update(pointRect.toRect().adjusted(-2, -2, 2, 2));
}

void VectorCanvas::finishStroke()
{
    if (currentStroke.points.isEmpty()) {
        return;
    }
    
    currentStroke.updateBoundingBox();
    strokes.append(currentStroke);
    
    // Push to undo stack
    UndoAction action;
    action.type = UndoAction::AddStroke;
    action.stroke = currentStroke;
    pushUndo(action);
    
    // Clear redo stack on new action
    redoStack.clear();
    
    // Mark cache dirty - new stroke added
    strokeCacheDirty = true;
    
    // Reset incremental rendering state
    lastRenderedPointIndex = 0;
    
    modified = true;
    emit strokeAdded();
    emit canvasModified();
    
    // Clear current stroke
    currentStroke = VectorStroke();
    
    update();
}

void VectorCanvas::eraseAt(const QPointF& pos)
{
    QVector<int> toRemove;
    
    // Find all strokes that intersect with eraser
    for (int i = 0; i < strokes.size(); ++i) {
        if (strokes[i].containsPoint(pos, eraserSize)) {
            toRemove.append(i);
        }
    }
    
    if (toRemove.isEmpty()) {
        return;
    }
    
    // Collect strokes for undo
    UndoAction action;
    if (toRemove.size() == 1) {
        action.type = UndoAction::RemoveStroke;
        action.stroke = strokes[toRemove[0]];
    } else {
        action.type = UndoAction::RemoveMultiple;
        for (int idx : toRemove) {
            action.strokes.append(strokes[idx]);
        }
    }
    pushUndo(action);
    
    // Clear redo stack
    redoStack.clear();
    
    // Calculate dirty region before removal
    QRectF dirtyRect;
    for (int idx : toRemove) {
        dirtyRect = dirtyRect.united(strokes[idx].boundingBox);
    }
    
    // Remove strokes (in reverse order to maintain indices)
    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
    for (int idx : toRemove) {
        strokes.removeAt(idx);
    }
    
    // Mark cache dirty - strokes removed
    strokeCacheDirty = true;
    
    modified = true;
    emit strokeRemoved();
    emit canvasModified();
    
    update(dirtyRect.toRect().adjusted(-5, -5, 5, 5));
}

void VectorCanvas::undo()
{
    if (undoStack.isEmpty()) {
        return;
    }
    
    UndoAction action = undoStack.pop();
    
    switch (action.type) {
        case UndoAction::AddStroke:
            // Remove the added stroke
            for (int i = strokes.size() - 1; i >= 0; --i) {
                if (strokes[i].id == action.stroke.id) {
                    strokes.removeAt(i);
                    break;
                }
            }
            break;
            
        case UndoAction::RemoveStroke:
            // Re-add the removed stroke
            strokes.append(action.stroke);
            break;
            
        case UndoAction::RemoveMultiple:
            // Re-add all removed strokes
            strokes.append(action.strokes);
            break;
    }
    
    // Push to redo stack
    redoStack.push(action);
    
    // Mark cache dirty - strokes changed
    strokeCacheDirty = true;
    
    modified = true;
    emit canvasModified();
    update();
}

void VectorCanvas::redo()
{
    if (redoStack.isEmpty()) {
        return;
    }
    
    UndoAction action = redoStack.pop();
    
    switch (action.type) {
        case UndoAction::AddStroke:
            // Re-add the stroke
            strokes.append(action.stroke);
            break;
            
        case UndoAction::RemoveStroke:
            // Remove the stroke again
            for (int i = strokes.size() - 1; i >= 0; --i) {
                if (strokes[i].id == action.stroke.id) {
                    strokes.removeAt(i);
                    break;
                }
            }
            break;
            
        case UndoAction::RemoveMultiple:
            // Remove all strokes again
            for (const auto& stroke : action.strokes) {
                for (int i = strokes.size() - 1; i >= 0; --i) {
                    if (strokes[i].id == stroke.id) {
                        strokes.removeAt(i);
                        break;
                    }
                }
            }
            break;
    }
    
    // Push back to undo stack
    undoStack.push(action);
    
    // Mark cache dirty - strokes changed
    strokeCacheDirty = true;
    
    modified = true;
    emit canvasModified();
    update();
}

void VectorCanvas::clear()
{
    if (strokes.isEmpty()) {
        return;
    }
    
    // Push all strokes to undo as a single action
    UndoAction action;
    action.type = UndoAction::RemoveMultiple;
    action.strokes = strokes;
    pushUndo(action);
    
    redoStack.clear();
    strokes.clear();
    
    // Mark cache dirty - all strokes cleared
    strokeCacheDirty = true;
    
    modified = true;
    emit canvasModified();
    update();
}

void VectorCanvas::pushUndo(const UndoAction& action)
{
    undoStack.push(action);
    
    // Limit undo stack size
    while (undoStack.size() > MAX_UNDO) {
        undoStack.removeFirst();
    }
}

QJsonObject VectorCanvas::toJson() const
{
    QJsonObject obj;
    obj["version"] = 1;
    
    QJsonArray strokesArray;
    for (const auto& stroke : strokes) {
        strokesArray.append(stroke.toJson());
    }
    obj["strokes"] = strokesArray;
    
    return obj;
}

void VectorCanvas::fromJson(const QJsonObject& obj)
{
    strokes.clear();
    undoStack.clear();
    redoStack.clear();
    
    QJsonArray strokesArray = obj["strokes"].toArray();
    for (const auto& val : strokesArray) {
        strokes.append(VectorStroke::fromJson(val.toObject()));
    }
    
    // Mark cache dirty - new strokes loaded
    strokeCacheDirty = true;
    
    modified = false;
    update();
}

void VectorCanvas::startBenchmark()
{
    benchmarking = true;
    paintTimestamps.clear();
    benchmarkTimer.start();
}

void VectorCanvas::stopBenchmark()
{
    benchmarking = false;
}

int VectorCanvas::getPaintRate() const
{
    if (!benchmarking) return 0;
    
    qint64 now = benchmarkTimer.elapsed();
    
    // Remove timestamps older than 1 second
    while (!paintTimestamps.empty() && now - paintTimestamps.front() > 1000) {
        paintTimestamps.pop_front();
    }
    
    return static_cast<int>(paintTimestamps.size());
}
