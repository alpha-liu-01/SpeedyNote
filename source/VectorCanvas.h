#ifndef VECTORCANVAS_H
#define VECTORCANVAS_H

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QRectF>
#include <QPainterPath>
#include <QElapsedTimer>
#include <QStack>
#include <QJsonObject>
#include <QJsonArray>
#include <QPixmap>
#include <deque>

// A single point in a stroke with pressure
struct StrokePoint {
    QPointF pos;      // Position in canvas coordinates
    qreal pressure;   // 0.0 to 1.0
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["x"] = pos.x();
        obj["y"] = pos.y();
        obj["p"] = pressure;
        return obj;
    }
    
    static StrokePoint fromJson(const QJsonObject& obj) {
        StrokePoint pt;
        pt.pos = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
        pt.pressure = obj["p"].toDouble(1.0);
        return pt;
    }
};

// A complete stroke (pen down → pen up)
struct VectorStroke {
    QString id;                     // UUID for tracking
    QVector<StrokePoint> points;    // All points in the stroke
    QColor color;
    qreal baseThickness;            // Before pressure scaling
    QRectF boundingBox;             // Cached for fast culling/hit testing
    
    VectorStroke() : baseThickness(5.0) {}
    
    void updateBoundingBox() {
        if (points.isEmpty()) {
            boundingBox = QRectF();
            return;
        }
        qreal maxWidth = baseThickness * 2;
        qreal minX = points[0].pos.x(), maxX = minX;
        qreal minY = points[0].pos.y(), maxY = minY;
        for (const auto& pt : points) {
            minX = qMin(minX, pt.pos.x());
            maxX = qMax(maxX, pt.pos.x());
            minY = qMin(minY, pt.pos.y());
            maxY = qMax(maxY, pt.pos.y());
        }
        boundingBox = QRectF(minX - maxWidth, minY - maxWidth,
                             maxX - minX + maxWidth * 2,
                             maxY - minY + maxWidth * 2);
    }
    
    // Check if a point is near this stroke (for eraser)
    bool containsPoint(const QPointF& point, qreal tolerance) const {
        if (!boundingBox.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) {
            return false;
        }
        // Check each segment
        for (int i = 1; i < points.size(); ++i) {
            if (distanceToSegment(point, points[i-1].pos, points[i].pos) < tolerance + baseThickness) {
                return true;
            }
        }
        return false;
    }
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["color"] = color.name(QColor::HexArgb);
        obj["thickness"] = baseThickness;
        QJsonArray pointsArray;
        for (const auto& pt : points) {
            pointsArray.append(pt.toJson());
        }
        obj["points"] = pointsArray;
        return obj;
    }
    
    static VectorStroke fromJson(const QJsonObject& obj) {
        VectorStroke stroke;
        stroke.id = obj["id"].toString();
        stroke.color = QColor(obj["color"].toString());
        stroke.baseThickness = obj["thickness"].toDouble(5.0);
        QJsonArray pointsArray = obj["points"].toArray();
        for (const auto& val : pointsArray) {
            stroke.points.append(StrokePoint::fromJson(val.toObject()));
        }
        stroke.updateBoundingBox();
        return stroke;
    }
    
private:
    static qreal distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
        QPointF ab = b - a;
        QPointF ap = p - a;
        qreal lenSq = ab.x() * ab.x() + ab.y() * ab.y();
        if (lenSq < 0.0001) return QLineF(p, a).length();
        qreal t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / lenSq, 1.0);
        QPointF closest = a + t * ab;
        return QLineF(p, closest).length();
    }
};

// Undo action types
struct UndoAction {
    enum Type { AddStroke, RemoveStroke, RemoveMultiple };
    Type type;
    VectorStroke stroke;              // For AddStroke
    QVector<VectorStroke> strokes;    // For RemoveMultiple
};

class VectorCanvas : public QWidget {
    Q_OBJECT

public:
    enum class Tool { Pen, Eraser };
    
    explicit VectorCanvas(QWidget *parent = nullptr);
    ~VectorCanvas() override;
    
    // Input control - when false, events pass through to underlying widget
    void setInputActive(bool active) { inputActive = active; }
    bool isInputActive() const { return inputActive; }
    
    // Tool settings
    void setTool(Tool tool) { currentTool = tool; }
    Tool getTool() const { return currentTool; }
    void setPenColor(const QColor& color) { penColor = color; }
    QColor getPenColor() const { return penColor; }
    void setPenThickness(qreal thickness) { penThickness = thickness; }
    qreal getPenThickness() const { return penThickness; }
    void setEraserSize(qreal size) { eraserSize = size; }
    
    // Undo/Redo
    void undo();
    void redo();
    bool canUndo() const { return !undoStack.isEmpty(); }
    bool canRedo() const { return !redoStack.isEmpty(); }
    
    // Clear all strokes
    void clear();
    
    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
    
    // State
    bool isModified() const { return modified; }
    void setModified(bool mod) { modified = mod; }
    int strokeCount() const { return strokes.size(); }
    
    // Benchmark (measures canvas refresh rate, not input rate)
    void startBenchmark();
    void stopBenchmark();
    int getPaintRate() const; // Returns paints per second

signals:
    void strokeAdded();
    void strokeRemoved();
    void canvasModified();

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Strokes
    QVector<VectorStroke> strokes;
    VectorStroke currentStroke;
    bool drawing = false;
    
    // Tool state
    Tool currentTool = Tool::Pen;
    QColor penColor = Qt::black;
    qreal penThickness = 5.0;
    qreal eraserSize = 20.0;
    
    // Input control - when false, events pass through to parent
    bool inputActive = false;
    
    // Undo/Redo
    QStack<UndoAction> undoStack;
    QStack<UndoAction> redoStack;
    static const int MAX_UNDO = 50;
    
    // State
    bool modified = false;
    QPointF lastPoint;
    
    // Performance - stroke cache for completed strokes
    QPixmap strokeCache;        // Cached rendering of all completed strokes
    bool strokeCacheDirty = true;  // True when cache needs rebuilding
    void rebuildStrokeCache();  // Rebuild the cache from all strokes
    
    // Performance - incremental rendering for current stroke
    QPixmap currentStrokeCache;     // Accumulates current stroke as it's drawn
    int lastRenderedPointIndex = 0; // Index of last point rendered to currentStrokeCache
    void renderCurrentStrokeIncremental(QPainter& painter); // Incremental render
    void resetCurrentStrokeCache(); // Reset when starting new stroke
    
    // Benchmark (measures paint refresh rate)
    bool benchmarking = false;
    QElapsedTimer benchmarkTimer;
    mutable std::deque<qint64> paintTimestamps;
    
    // Internal methods
    void addPoint(const QPointF& pos, qreal pressure);
    void finishStroke();
    void eraseAt(const QPointF& pos);
    void renderStroke(QPainter& painter, const VectorStroke& stroke) const;
    void pushUndo(const UndoAction& action);
};

#endif // VECTORCANVAS_H
