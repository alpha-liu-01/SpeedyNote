#ifndef LAUNCHERSCROLLAREA_H
#define LAUNCHERSCROLLAREA_H

#include <QScrollArea>
#include <QTimer>
#include <QElapsedTimer>
#include <QPoint>

/**
 * @brief A QScrollArea subclass with reliable manual touch scrolling.
 * 
 * This class replaces QScroller-based touch scrolling which has known issues:
 * - Inertia scrolling can reverse direction and accelerate
 * - Unreliable behavior on Linux/Wayland tablet devices
 * - Conflicts between scroll gestures and child widget interactions
 * 
 * Features:
 * - Manual touch detection via mouse events (touch is synthesized to mouse)
 * - Smooth kinetic scrolling with configurable deceleration
 * - Velocity capping to prevent extreme scroll distances
 * - Proper boundary handling (stops at edges)
 * 
 * Usage:
 * Replace QScrollArea with LauncherScrollArea in StarredView and SearchView.
 * 
 * Based on the same pattern used in PagePanelListView and OutlinePanelTreeWidget.
 */
class LauncherScrollArea : public QScrollArea {
    Q_OBJECT

public:
    explicit LauncherScrollArea(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onKineticScrollTick();

private:
    void startKineticScroll(qreal velocity);
    void stopKineticScroll();
    bool isTouchInput(QMouseEvent* event) const;
    
    // Touch scrolling state
    bool m_touchScrolling = false;
    QPoint m_touchScrollStartPos;
    int m_scrollStartValue = 0;
    
    // Velocity tracking for kinetic scrolling
    QElapsedTimer m_velocityTimer;
    qreal m_lastVelocity = 0.0;
    
    // Kinetic scrolling animation
    QTimer m_kineticTimer;
    qreal m_kineticVelocity = 0.0;
    
    // Constants
    static constexpr int SCROLL_THRESHOLD = 10;       // Pixels before scroll starts
    static constexpr int KINETIC_TICK_MS = 16;        // ~60 FPS
    static constexpr qreal KINETIC_DECELERATION = 0.92;  // Per-tick multiplier
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.5;   // Stop threshold (px/ms)
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;   // Cap extreme velocities
};

#endif // LAUNCHERSCROLLAREA_H
