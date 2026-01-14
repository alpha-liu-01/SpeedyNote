#ifndef CONTROLLERMAPPINGDIALOG_H
#define CONTROLLERMAPPINGDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QMessageBox>
#include <QTimer>
#include <QMap>

class SDLControllerManager;

class ControllerMappingDialog : public QDialog {
    Q_OBJECT

public:
    explicit ControllerMappingDialog(SDLControllerManager *controllerManager, QWidget *parent = nullptr);

private slots:
    void startButtonMapping(const QString &logicalButton);
    void onRawButtonPressed(int sdlButton, const QString &buttonName);
    void resetToDefaults();
    void applyMappings();

private:
    SDLControllerManager *controller;
    
    // UI elements
    QGridLayout *mappingLayout;
    QMap<QString, QLabel*> buttonLabels;
    QMap<QString, QLabel*> currentMappingLabels;
    QMap<QString, QPushButton*> mappingButtons;
    QPushButton *resetButton;
    QPushButton *applyButton;
    QPushButton *cancelButton;
    
    // Mapping state
    QString currentMappingButton;
    QTimer *mappingTimeoutTimer;
    
    // Helper methods
    void setupUI();
    QMap<QString, QString> getLogicalButtonDescriptions() const;
    void loadCurrentMappings();
    void updateMappingDisplay();
    bool isDarkMode() const;
};

#endif // CONTROLLERMAPPINGDIALOG_H 