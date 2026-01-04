/**
 * @file MouseDialHandler.h
 * @brief Handler for mouse-based dial emulation (side buttons + wheel)
 * 
 * This module can be excluded on Android builds via ENABLE_DIAL_CONTROLLER CMake option.
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 */

#ifndef MOUSEDIALHANDLER_H
#define MOUSEDIALHANDLER_H

#include <QObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMap>
#include <QString>

// Forward declarations
class MainWindow;
class DialController;

/**
 * @brief Handler for mouse-based dial control emulation
 * 
 * Allows using mouse side buttons + wheel as a dial controller substitute.
 * This is useful for devices without a physical Surface Dial.
 * 
 * Button combinations:
 * - Side button held + wheel = dial rotation
 * - Configurable button mappings for different dial modes
 */
class MouseDialHandler : public QObject
{
    Q_OBJECT

public:
    explicit MouseDialHandler(DialController *dialController, QObject *parent = nullptr);
    ~MouseDialHandler();

    // Enable/disable mouse dial mode
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Button state
    bool isDialButtonHeld() const { return m_dialButtonHeld; }

    // Event handling (call from MainWindow's event handlers)
    bool handleMousePress(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool handleWheel(QWheelEvent *event);

    // Button mappings
    void setButtonMappings(const QMap<QString, QString> &mappings);
    QMap<QString, QString> buttonMappings() const { return m_buttonMappings; }
    void saveButtonMappings();
    void loadButtonMappings();

signals:
    void dialButtonPressed();
    void dialButtonReleased();
    void wheelRotated(int delta);

private:
    DialController *m_dialController = nullptr;
    bool m_enabled = true;
    bool m_dialButtonHeld = false;
    Qt::MouseButton m_dialButton = Qt::MiddleButton; // Default dial trigger button
    
    QMap<QString, QString> m_buttonMappings;
    
    // Wheel accumulation for smoother dial emulation
    int m_wheelAccumulator = 0;
    static constexpr int WHEEL_THRESHOLD = 120; // Standard wheel delta
};

#endif // MOUSEDIALHANDLER_H

