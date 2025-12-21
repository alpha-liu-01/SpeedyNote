#ifndef TOOLTYPE_H
#define TOOLTYPE_H

// ✅ Now it can be used anywhere
enum class ToolType {
    Pen,
    Marker,
    Eraser,
    VectorPen,      // Vector-based pen with undo support
    VectorEraser    // Stroke-based eraser for vector canvas
};

#endif // TOOLTYPE_H