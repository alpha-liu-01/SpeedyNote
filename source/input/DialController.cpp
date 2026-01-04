/**
 * @file DialController.cpp
 * @brief Implementation of MagicDial controller
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 * TODO MW2.2: Move dial handling logic from MainWindow to here
 */

#include "DialController.h"
#include "../MainWindow.h"
#include "../core/DocumentViewport.h"

#include <QDebug>

DialController::DialController(MainWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
    qDebug() << "DialController: Initialized";
}

DialController::~DialController()
{
}

void DialController::setMode(DialMode mode)
{
    if (m_currentMode != mode) {
        m_currentMode = mode;
        m_accumulatedAngle = 0;
        emit modeChanged(mode);
        qDebug() << "DialController: Mode changed to" << static_cast<int>(mode);
    }
}

void DialController::setTemporaryMode(DialMode mode)
{
    if (!m_inTemporaryMode) {
        m_previousMode = m_currentMode;
        m_inTemporaryMode = true;
    }
    setMode(mode);
}

void DialController::restoreFromTemporaryMode()
{
    if (m_inTemporaryMode) {
        m_inTemporaryMode = false;
        setMode(m_previousMode);
    }
}

void DialController::handleDialInput(int angle)
{
    m_accumulatedAngle += angle;
    emit dialRotated(angle);
    
    // TODO MW2.2: Move mode-specific handling from MainWindow::handleDialInput()
    switch (m_currentMode) {
        case ToolSwitching:
            handleToolSelection(angle);
            break;
        case ZoomControl:
            handleZoom(angle);
            break;
        case PanAndPageScroll:
            handlePanScroll(angle);
            break;
        case ThicknessControl:
            handleThickness(angle);
            break;
        case PresetSelection:
            handlePresetSelection(angle);
            break;
        case PageSwitching:
            handlePageSwitch(angle);
            break;
        case None:
        default:
            break;
    }
}

void DialController::handleDialPressed()
{
    emit dialPressed();
    // TODO MW2.2: Move from MainWindow::onDialPressed()
}

void DialController::handleDialReleased()
{
    m_accumulatedAngle = 0;
    emit dialReleased();
    // TODO MW2.2: Move from MainWindow::onDialReleased()
}

DocumentViewport* DialController::currentViewport() const
{
    return m_mainWindow ? m_mainWindow->currentViewport() : nullptr;
}

// Mode-specific handlers - TODO MW2.2: Implement these
void DialController::handleToolSelection(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow::handleToolSelection()
}

void DialController::handleZoom(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow::handleDialZoom()
}

void DialController::handlePanScroll(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow::handleDialPanScroll()
}

void DialController::handleThickness(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow::handleDialThickness()
}

void DialController::handlePresetSelection(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow::handlePresetSelection()
}

void DialController::handlePageSwitch(int angle)
{
    Q_UNUSED(angle);
    // TODO: Move from MainWindow pages handling
}
