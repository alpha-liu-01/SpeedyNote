#include "NotesTreeWidget.h"

#include <QMouseEvent>
#include <QScrollBar>

// ============================================================================
// Constructor
// ============================================================================

NotesTreeWidget::NotesTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &NotesTreeWidget::onKineticScrollTick);
}

// ============================================================================
// Mouse / touch handling — mirrors OutlinePanelTreeWidget.
// ============================================================================

void NotesTreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos         = event->pos();
        m_touchScrolling   = false;
        m_isTouchInput     = (event->source() != Qt::MouseEventNotSynthesized);

        if (m_isTouchInput) {
            stopKineticScroll();
            m_touchScrollStart = verticalScrollBar()->value();
            m_velocityTimer.start();
            m_lastVelocity = 0.0;
            event->accept();
            return;
        }
    }
    QTreeWidget::mousePressEvent(event);
}

void NotesTreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isTouchInput) {
        const QPoint delta = event->pos() - m_pressPos;
        if (!m_touchScrolling && delta.manhattanLength() > SCROLL_THRESHOLD) {
            m_touchScrolling = true;
        }

        if (m_touchScrolling) {
            const int currentY   = event->pos().y();
            const int deltaY     = currentY - m_pressPos.y();
            const int newScroll  = m_touchScrollStart - deltaY;
            const int oldScroll  = verticalScrollBar()->value();
            verticalScrollBar()->setValue(newScroll);

            const int scrollDelta = newScroll - oldScroll;
            const qint64 frameTime = m_velocityTimer.restart();
            if (frameTime > 0 && scrollDelta != 0) {
                const qreal instant = static_cast<qreal>(scrollDelta)
                                    / static_cast<qreal>(frameTime);
                constexpr qreal alpha = 0.4;
                m_lastVelocity = alpha * instant + (1.0 - alpha) * m_lastVelocity;
            } else if (frameTime > 50) {
                m_lastVelocity *= 0.5;
            }
        }
        event->accept();
        return;
    }
    QTreeWidget::mouseMoveEvent(event);
}

void NotesTreeWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const bool wasTouch    = m_isTouchInput;
        const bool wasScrolling = m_touchScrolling;
        m_isTouchInput    = false;
        m_touchScrolling  = false;

        if (wasTouch) {
            if (!wasScrolling) {
                QTreeWidgetItem* item = itemAt(event->pos());
                if (item) {
                    int depth = 0;
                    for (QTreeWidgetItem* p = item->parent(); p; p = p->parent()) {
                        ++depth;
                    }
                    const int indicatorLeft  = indentation() * depth;
                    const int indicatorRight = indentation() * (depth + 1);
                    const int clickX         = event->pos().x();
                    const bool hasChildren   = item->childCount() > 0
                                            || item->isExpanded();  // allow lazy-populated rows

                    if (hasChildren
                        && clickX >= indicatorLeft && clickX < indicatorRight) {
                        item->setExpanded(!item->isExpanded());
                    } else {
                        emit itemClicked(item, 0);
                    }
                }
            } else if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
                startKineticScroll(m_lastVelocity);
            }
            event->accept();
            return;
        }
    }
    QTreeWidget::mouseReleaseEvent(event);
}

// ============================================================================
// Kinetic scrolling
// ============================================================================

void NotesTreeWidget::startKineticScroll(qreal velocity)
{
    m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, velocity, KINETIC_MAX_VELOCITY);
    m_kineticTimer.start();
}

void NotesTreeWidget::stopKineticScroll()
{
    if (m_kineticTimer.isActive()) {
        m_kineticTimer.stop();
        m_kineticVelocity = 0.0;
    }
}

void NotesTreeWidget::onKineticScrollTick()
{
    const int scrollDelta = static_cast<int>(m_kineticVelocity * KINETIC_TICK_MS);
    const int currentPos  = verticalScrollBar()->value();
    int       newPos      = currentPos + scrollDelta;

    const int minPos = verticalScrollBar()->minimum();
    const int maxPos = verticalScrollBar()->maximum();
    newPos = qBound(minPos, newPos, maxPos);
    verticalScrollBar()->setValue(newPos);

    m_kineticVelocity *= KINETIC_DECELERATION;
    const bool hitBounds = (newPos == minPos || newPos == maxPos) && scrollDelta != 0;
    if (qAbs(m_kineticVelocity) < KINETIC_MIN_VELOCITY || hitBounds) {
        stopKineticScroll();
    }
}
