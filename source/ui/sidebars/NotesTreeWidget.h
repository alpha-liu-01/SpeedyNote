#ifndef NOTESTREEWIDGET_H
#define NOTESTREEWIDGET_H

// ============================================================================
// NotesTreeWidget — Touch-optimized QTreeWidget for the right-sidebar
//                   markdown-notes hierarchy.
// ============================================================================
// Behaviourally identical to OutlinePanelTreeWidget (kept in sync — see that
// file for the rationale behind avoiding QScroller on Android/Qt6).
//
// It is a separate class because the outline panel's taps translate to
// `itemClicked` on single rows whereas the notes tree needs a tap zone that
// preserves the same indent-band-expand behaviour even when an L3 row is
// inflated to a full `MarkdownNoteEntry` via setItemWidget().  Keeping the
// touch logic in this subclass (and nothing else) gives NotesTreePanel a
// clean Qt-level contract.
// ============================================================================

#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>
#include <QTreeWidget>

class QMouseEvent;

class NotesTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit NotesTreeWidget(QWidget* parent = nullptr);

    /// Last mouse-press position (viewport coords).
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
    bool   m_isTouchInput      = false;
    int    m_touchScrollStart  = 0;
    bool   m_touchScrolling    = false;

    QTimer        m_kineticTimer;
    QElapsedTimer m_velocityTimer;
    qreal         m_kineticVelocity = 0.0;
    qreal         m_lastVelocity    = 0.0;

    // Constants mirrored 1:1 with OutlinePanelTreeWidget so both sidebars
    // feel identical under finger input.
    static constexpr int   SCROLL_THRESHOLD     = 15;
    static constexpr int   KINETIC_TICK_MS      = 16;
    static constexpr qreal KINETIC_DECELERATION = 0.92;
    static constexpr qreal KINETIC_MIN_VELOCITY = 0.05;
    static constexpr qreal KINETIC_MAX_VELOCITY = 3.0;
};

#endif // NOTESTREEWIDGET_H
