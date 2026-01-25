#ifndef OUTLINEPANELTREEWIDGET_H
#define OUTLINEPANELTREEWIDGET_H

#include <QTreeWidget>
#include <QPoint>

class QMouseEvent;

/**
 * @brief A QTreeWidget subclass that implements manual touch scrolling.
 * 
 * QScroller conflicts with QTreeWidget's native scrolling on Android,
 * causing scroll oscillation and reverse acceleration.
 * 
 * This class implements simple direct touch scrolling:
 * - Touch drag = scroll by delta
 * - Tap = emit itemClicked
 */
class OutlinePanelTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit OutlinePanelTreeWidget(QWidget* parent = nullptr);
    
    /**
     * @brief Get the last mouse press position (viewport coordinates).
     */
    QPoint lastPressPosition() const { return m_pressPos; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QPoint m_pressPos;
    bool m_isTouchInput = false;
    int m_touchScrollStartPos = 0;
    bool m_touchScrolling = false;
    
    static constexpr int SCROLL_THRESHOLD = 15;  // Pixels to start scrolling
};

#endif // OUTLINEPANELTREEWIDGET_H
