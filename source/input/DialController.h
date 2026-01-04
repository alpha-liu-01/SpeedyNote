/**
 * @file DialController.h
 * @brief Controller for MagicDial (Surface Dial) input handling
 * 
 * This module can be excluded on Android builds via ENABLE_DIAL_CONTROLLER CMake option.
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 */

#ifndef DIALCONTROLLER_H
#define DIALCONTROLLER_H

#include <QObject>
#include <QWidget>

#include "DialTypes.h"  // Shared enum definitions

// Forward declarations
class MainWindow;
class DocumentViewport;

/**
 * @brief Controller for MagicDial (Surface Dial) and similar rotary input devices
 * 
 * Handles:
 * - Dial rotation events
 * - Mode switching
 * - Integration with DocumentViewport for tool/zoom/pan operations
 */
class DialController : public QObject
{
    Q_OBJECT

public:
    explicit DialController(MainWindow *mainWindow, QObject *parent = nullptr);
    ~DialController();

    // Mode management
    DialMode currentMode() const { return m_currentMode; }
    void setMode(DialMode mode);
    
    // Temporary mode (for stylus button hold)
    void setTemporaryMode(DialMode mode);
    void restoreFromTemporaryMode();
    bool isInTemporaryMode() const { return m_inTemporaryMode; }

    // Dial input handling
    void handleDialInput(int angle);
    void handleDialPressed();
    void handleDialReleased();

signals:
    void modeChanged(DialMode mode);
    void dialRotated(int angle);
    void dialPressed();
    void dialReleased();

private:
    MainWindow *m_mainWindow = nullptr;
    DialMode m_currentMode = ToolSwitching;
    DialMode m_previousMode = ToolSwitching;
    bool m_inTemporaryMode = false;
    
    // Accumulator for dial rotation
    int m_accumulatedAngle = 0;
    
    // Mode-specific handlers
    void handleToolSelection(int angle);
    void handleZoom(int angle);
    void handlePanScroll(int angle);
    void handleThickness(int angle);
    void handlePresetSelection(int angle);
    void handlePageSwitch(int angle);
    
    // Helper to get current viewport
    DocumentViewport* currentViewport() const;
};

#endif // DIALCONTROLLER_H
