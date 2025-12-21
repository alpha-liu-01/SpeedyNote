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

// Stroke data types (extracted to separate files for modularity - Phase 1.1.2)
#include "strokes/StrokePoint.h"
#include "strokes/VectorStroke.h"

// Undo action types (specific to VectorCanvas's undo system)
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
