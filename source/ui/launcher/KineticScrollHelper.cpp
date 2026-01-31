#include "KineticScrollHelper.h"

#include <QScrollBar>
#include <QtMath>

KineticScrollHelper::KineticScrollHelper(QScrollBar* scrollBar, QObject* parent)
    : QObject(parent)
    , m_scrollBar(scrollBar)
{
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &KineticScrollHelper::onKineticTick);
}

bool KineticScrollHelper::isTouchInput(QMouseEvent* event)
{
    // Touch events are synthesized to mouse events with this source
    return (event->source() != Qt::MouseEventNotSynthesized);
}

void KineticScrollHelper::startTracking()
{
    // Stop any ongoing kinetic scroll
    stop();
    
    // Initialize velocity tracking
    m_velocityTimer.start();
    m_lastVelocity = 0.0;
}

void KineticScrollHelper::updateVelocity(int scrollDelta)
{
    qint64 frameTime = m_velocityTimer.restart();
    if (frameTime > 0) {
        qreal instantVelocity = static_cast<qreal>(scrollDelta) / frameTime;
        
        // Exponential smoothing for stable velocity
        m_lastVelocity = VELOCITY_SMOOTHING * instantVelocity + 
                        (1.0 - VELOCITY_SMOOTHING) * m_lastVelocity;
        
        // Decay velocity if no movement for a while
        if (frameTime > 50 && qAbs(scrollDelta) < 1) {
            m_lastVelocity *= 0.5;
        }
    }
}

bool KineticScrollHelper::finishTracking()
{
    if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
        // Cap velocity to prevent extreme scroll distances
        m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, m_lastVelocity, KINETIC_MAX_VELOCITY);
        m_kineticTimer.start();
        return true;
    }
    return false;
}

void KineticScrollHelper::stop()
{
    m_kineticTimer.stop();
    m_kineticVelocity = 0.0;
}

void KineticScrollHelper::onKineticTick()
{
    if (!m_scrollBar) {
        stop();
        return;
    }
    
    // Apply velocity to scroll position
    int delta = static_cast<int>(m_kineticVelocity * KINETIC_TICK_MS);
    int currentValue = m_scrollBar->value();
    int newValue = currentValue + delta;
    
    // Clamp to valid range
    int minValue = m_scrollBar->minimum();
    int maxValue = m_scrollBar->maximum();
    newValue = qBound(minValue, newValue, maxValue);
    
    m_scrollBar->setValue(newValue);
    
    // Apply deceleration
    m_kineticVelocity *= KINETIC_DECELERATION;
    
    // Stop if velocity is too low or hit bounds
    if (qAbs(m_kineticVelocity) < KINETIC_MIN_VELOCITY ||
        newValue == minValue || newValue == maxValue) {
        stop();
    }
}
