#ifndef OUTLINEPANELTREEWIDGET_H
#define OUTLINEPANELTREEWIDGET_H

#include <QTreeWidget>
#include <QTimer>
#include <QPoint>
#include <QElapsedTimer>

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

private slots:
    void onKineticScrollTick();

private:
    void startKineticScroll(qreal velocity);
    void stopKineticScroll();
    
    QPoint m_pressPos;
    bool m_isTouchInput = false;
    int m_touchScrollStartPos = 0;
    bool m_touchScrolling = false;
    
    // Kinetic scrolling state
    QTimer m_kineticTimer;
    QElapsedTimer m_velocityTimer;
    qreal m_kineticVelocity = 0.0;
    qreal m_lastVelocity = 0.0;     // Last calculated velocity (for release)
    
    static constexpr int SCROLL_THRESHOLD = 15;           // Pixels to start scrolling
    static constexpr int KINETIC_TICK_MS = 16;            // ~60 FPS
    static constexpr qreal KINETIC_DECELERATION = 0.92;   // Velocity multiplier per tick (faster decay)
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.05;   // Stop below this (pixels/ms)
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;    // Cap velocity (pixels/ms)
};

#endif // OUTLINEPANELTREEWIDGET_H
