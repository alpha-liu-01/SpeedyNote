#ifndef SDLCONTROLLERMANAGER_H
#define SDLCONTROLLERMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QThread>
#include <QTimer>
#include <SDL2/SDL.h>

class SDLControllerManager : public QObject {
    Q_OBJECT
public:
    explicit SDLControllerManager(QObject *parent = nullptr);
    ~SDLControllerManager();
    SDL_Joystick* getJoystick() const { return joystick; }
    
    // New: Controller mapping management
    void setPhysicalButtonMapping(const QString &logicalButton, int physicalSDLButton);
    int getPhysicalButtonMapping(const QString &logicalButton) const;
    QMap<QString, int> getAllPhysicalMappings() const;
    void saveControllerMappings();
    void loadControllerMappings();
    
    // New: Get available physical buttons for mapping
    QStringList getAvailablePhysicalButtons() const;
    QString getPhysicalButtonName(int sdlButton) const;
    int getJoystickButtonCount() const;
    QMap<QString, int> getDefaultMappings() const; // Default mappings for Joy-Con L

signals:
    void buttonHeld(QString buttonName);
    void buttonReleased(QString buttonName);
    void buttonSinglePress(QString buttonName);
    void leftStickAngleChanged(int angle);
    void leftStickReleased();
    
    // New: Signal for raw button detection during mapping setup
    void rawButtonPressed(int sdlButton, QString buttonName);

public slots:
    void start();
    void stop();
    void reconnect(); // Reconnect controller without restarting app
    void startButtonDetection(); // New: Start detecting raw button presses for mapping
    void stopButtonDetection();  // New: Stop detecting raw button presses

private:
    QTimer *pollTimer;
    SDL_Joystick *joystick = nullptr; // Changed from SDL_GameController to SDL_Joystick
    bool sdlInitialized = false;
    bool leftStickActive = false;  // Whether stick is out of deadzone
    bool buttonDetectionMode = false; // New: Whether we're in button detection mode

    int lastAngle = -1;
    QString getButtonName(Uint8 sdlButton);
    QString getLogicalButtonName(Uint8 sdlButton); // New: Get logical button name from physical button

    // New: Physical to logical button mapping
    QMap<QString, int> physicalButtonMappings; // logicalButton -> physicalSDLButton
    QMap<QString, quint32> buttonPressTime;
    QMap<QString, bool> buttonHeldEmitted;  // NEW -> whether buttonHeld has been fired
    
    const quint32 HOLD_THRESHOLD = 300;  // ms
    const quint32 POLL_INTERVAL = 16;    // ms
};

#endif // SDLCONTROLLERMANAGER_H
