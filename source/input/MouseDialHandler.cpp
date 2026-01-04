/**
 * @file MouseDialHandler.cpp
 * @brief Implementation of mouse-based dial emulation
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 * TODO MW2.3: Move mouse dial logic from MainWindow to here
 */

#include "MouseDialHandler.h"
#include "DialController.h"

#include <QSettings>
#include <QDebug>

MouseDialHandler::MouseDialHandler(DialController *dialController, QObject *parent)
    : QObject(parent)
    , m_dialController(dialController)
{
    loadButtonMappings();
    qDebug() << "MouseDialHandler: Initialized";
}

MouseDialHandler::~MouseDialHandler()
{
}

void MouseDialHandler::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        if (!enabled) {
            m_dialButtonHeld = false;
            m_wheelAccumulator = 0;
        }
    }
}

bool MouseDialHandler::handleMousePress(QMouseEvent *event)
{
    if (!m_enabled) return false;
    
    // Check if dial trigger button is pressed
    if (event->button() == m_dialButton) {
        m_dialButtonHeld = true;
        m_wheelAccumulator = 0;
        emit dialButtonPressed();
        return true;
    }
    
    // TODO MW2.3: Handle side button combinations for mode switching
    return false;
}

bool MouseDialHandler::handleMouseRelease(QMouseEvent *event)
{
    if (!m_enabled) return false;
    
    if (event->button() == m_dialButton && m_dialButtonHeld) {
        m_dialButtonHeld = false;
        m_wheelAccumulator = 0;
        emit dialButtonReleased();
        return true;
    }
    
    return false;
}

bool MouseDialHandler::handleWheel(QWheelEvent *event)
{
    if (!m_enabled || !m_dialButtonHeld) return false;
    
    // Accumulate wheel delta
    m_wheelAccumulator += event->angleDelta().y();
    
    // Convert to dial rotation when threshold is reached
    while (m_wheelAccumulator >= WHEEL_THRESHOLD) {
        m_wheelAccumulator -= WHEEL_THRESHOLD;
        if (m_dialController) {
            m_dialController->handleDialInput(10); // Positive rotation
        }
        emit wheelRotated(10);
    }
    while (m_wheelAccumulator <= -WHEEL_THRESHOLD) {
        m_wheelAccumulator += WHEEL_THRESHOLD;
        if (m_dialController) {
            m_dialController->handleDialInput(-10); // Negative rotation
        }
        emit wheelRotated(-10);
    }
    
    return true; // Consume the event
}

void MouseDialHandler::setButtonMappings(const QMap<QString, QString> &mappings)
{
    m_buttonMappings = mappings;
}

void MouseDialHandler::saveButtonMappings()
{
    QSettings settings;
    settings.beginGroup("MouseDial");
    settings.beginWriteArray("ButtonMappings");
    
    int i = 0;
    for (auto it = m_buttonMappings.constBegin(); it != m_buttonMappings.constEnd(); ++it) {
        settings.setArrayIndex(i++);
        settings.setValue("button", it.key());
        settings.setValue("action", it.value());
    }
    
    settings.endArray();
    settings.endGroup();
}

void MouseDialHandler::loadButtonMappings()
{
    QSettings settings;
    settings.beginGroup("MouseDial");
    
    int size = settings.beginReadArray("ButtonMappings");
    m_buttonMappings.clear();
    
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString button = settings.value("button").toString();
        QString action = settings.value("action").toString();
        if (!button.isEmpty() && !action.isEmpty()) {
            m_buttonMappings[button] = action;
        }
    }
    
    settings.endArray();
    settings.endGroup();
}

