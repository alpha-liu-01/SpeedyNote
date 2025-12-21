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
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Get visible rect for culling
    QRectF visibleRect = rect();
    
    // Render all strokes (with bounding box culling for performance)
    for (const auto& stroke : strokes) {
        if (stroke.boundingBox.intersects(visibleRect)) {
            renderStroke(painter, stroke);
        }
    }
    
    // Render current stroke being drawn
    if (drawing && !currentStroke.points.isEmpty()) {
        renderStroke(painter, currentStroke);
    }
    
    // Draw eraser cursor when in eraser mode
    if (currentTool == Tool::Eraser && underMouse()) {
        painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(lastPoint, eraserSize, eraserSize);
    }
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
    
    // Performance optimization: For many points, batch into segments
    // Draw variable-width stroke by drawing each segment with interpolated width
    for (int i = 1; i < stroke.points.size(); ++i) {
        const auto& p0 = stroke.points[i-1];
        const auto& p1 = stroke.points[i];
        
        // Interpolate pressure for smooth width transition
        qreal avgPressure = (p0.pressure + p1.pressure) / 2.0;
        qreal width = stroke.baseThickness * avgPressure;
        
        // Minimum width to ensure visibility
        width = qMax(width, 1.0);
        
        QPen pen(stroke.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(p0.pos, p1.pos);
    }
}

void VectorCanvas::tabletEvent(QTabletEvent *event)
{
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
    
    modified = false;
    update();
}

QRect VectorCanvas::strokeToWidgetRect(const QRectF& strokeRect) const
{
    return strokeRect.toRect().adjusted(-2, -2, 2, 2);
}
