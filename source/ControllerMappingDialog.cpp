#include "ControllerMappingDialog.h"
#include "SDLControllerManager.h"
#include <QApplication>

ControllerMappingDialog::ControllerMappingDialog(SDLControllerManager *controllerManager, QWidget *parent)
    : QDialog(parent), controller(controllerManager) {
    
    setWindowTitle(tr("Controller Button Mapping"));
    setModal(true);
    resize(600, 500);
    
    // Setup timeout timer for button mapping
    mappingTimeoutTimer = new QTimer(this);
    mappingTimeoutTimer->setSingleShot(true);
    mappingTimeoutTimer->setInterval(10000); // 10 second timeout
    
    connect(mappingTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (!currentMappingButton.isEmpty()) {
            QMessageBox::information(this, tr("Mapping Timeout"), 
                tr("Button mapping timed out. Please try again."));
            
            // Reset the button text
            if (mappingButtons.contains(currentMappingButton)) {
                mappingButtons[currentMappingButton]->setText(tr("Click to Map"));
                mappingButtons[currentMappingButton]->setEnabled(true);
            }
            
            currentMappingButton.clear();
            controller->stopButtonDetection();
        }
    });
    
    // Connect to controller signals
    connect(controller, &SDLControllerManager::rawButtonPressed, 
            this, &ControllerMappingDialog::onRawButtonPressed);
    
    setupUI();
    loadCurrentMappings();
}

void ControllerMappingDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(tr("Map your physical controller buttons to Joy-Con functions.\n"
                                           "Click 'Map' next to each function, then press the corresponding button on your controller."), this);
    instructionLabel->setWordWrap(true);
    instructionLabel->setStyleSheet("font-weight: bold; margin-bottom: 10px;");
    mainLayout->addWidget(instructionLabel);
    
    // Mapping grid
    QWidget *mappingWidget = new QWidget(this);
    mappingLayout = new QGridLayout(mappingWidget);
    
    // Headers
    mappingLayout->addWidget(new QLabel(tr("<b>Joy-Con Function</b>"), this), 0, 0);
    mappingLayout->addWidget(new QLabel(tr("<b>Description</b>"), this), 0, 1);
    mappingLayout->addWidget(new QLabel(tr("<b>Current Mapping</b>"), this), 0, 2);
    mappingLayout->addWidget(new QLabel(tr("<b>Action</b>"), this), 0, 3);
    
    // Get logical button descriptions
    QMap<QString, QString> descriptions = getLogicalButtonDescriptions();
    
    // Create mapping rows for each logical button
    QStringList logicalButtons = {"LEFTSHOULDER", "RIGHTSHOULDER", "PADDLE2", "PADDLE4", 
                                 "Y", "A", "B", "X", "LEFTSTICK", "START", "GUIDE"};
    
    int row = 1;
    for (const QString &logicalButton : logicalButtons) {
        // Function name
        QLabel *functionLabel = new QLabel(logicalButton, this);
        functionLabel->setStyleSheet("font-weight: bold;");
        buttonLabels[logicalButton] = functionLabel;
        mappingLayout->addWidget(functionLabel, row, 0);
        
        // Description
        QLabel *descLabel = new QLabel(descriptions.value(logicalButton, "Unknown"), this);
        descLabel->setWordWrap(true);
        mappingLayout->addWidget(descLabel, row, 1);
        
        // Current mapping display
        QLabel *currentLabel = new QLabel("Not mapped", this);
        currentLabel->setStyleSheet("color: gray;");
        currentMappingLabels[logicalButton] = currentLabel;
        mappingLayout->addWidget(currentLabel, row, 2);
        
        // Map button
        QPushButton *mapButton = new QPushButton(tr("Map"), this);
        mappingButtons[logicalButton] = mapButton;
        connect(mapButton, &QPushButton::clicked, this, [this, logicalButton]() {
            startButtonMapping(logicalButton);
        });
        mappingLayout->addWidget(mapButton, row, 3);
        
        row++;
    }
    
    mainLayout->addWidget(mappingWidget);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    resetButton = new QPushButton(tr("Reset to Defaults"), this);
    connect(resetButton, &QPushButton::clicked, this, &ControllerMappingDialog::resetToDefaults);
    buttonLayout->addWidget(resetButton);
    
    buttonLayout->addStretch();
    
    applyButton = new QPushButton(tr("Apply"), this);
    connect(applyButton, &QPushButton::clicked, this, &ControllerMappingDialog::applyMappings);
    buttonLayout->addWidget(applyButton);
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
}

QMap<QString, QString> ControllerMappingDialog::getLogicalButtonDescriptions() const {
    QMap<QString, QString> descriptions;
    descriptions["LEFTSHOULDER"] = tr("SL Button (Side Left)");
    descriptions["RIGHTSHOULDER"] = tr("SR Button (Side Right)");
    descriptions["PADDLE2"] = tr("L Button (Left Shoulder)");
    descriptions["PADDLE4"] = tr("ZL Button (Left Trigger)");

    descriptions["Y"] = tr("Up Arrow (D-Pad Up)");
    descriptions["A"] = tr("Right Arrow (D-Pad Right)");
    descriptions["B"] = tr("Down Arrow (D-Pad Down)");
    descriptions["X"] = tr("Left Arrow (D-Pad Left)");
    descriptions["LEFTSTICK"] = tr("Analog Stick Press");
    descriptions["START"] = tr("Minus Button (-)");
    descriptions["GUIDE"] = tr("Screenshot Button");
    // descriptions["PREVIOUS_PAGE"] = tr("Previous Page");
    // descriptions["NEXT_PAGE"] = tr("Next Page");
    return descriptions;
}

void ControllerMappingDialog::loadCurrentMappings() {
    QMap<QString, int> currentMappings = controller->getAllPhysicalMappings();
    
    // Get appropriate text color for current theme
    QString textColor = isDarkMode() ? "white" : "black";
    
    for (auto it = currentMappings.begin(); it != currentMappings.end(); ++it) {
        const QString &logicalButton = it.key();
        int physicalButton = it.value();
        
        if (currentMappingLabels.contains(logicalButton)) {
            QString physicalName = controller->getPhysicalButtonName(physicalButton);
            currentMappingLabels[logicalButton]->setText(physicalName);
            currentMappingLabels[logicalButton]->setStyleSheet(QString("color: %1; font-weight: bold;").arg(textColor));
        }
    }
}

void ControllerMappingDialog::updateMappingDisplay() {
    loadCurrentMappings();
}

void ControllerMappingDialog::startButtonMapping(const QString &logicalButton) {
    if (!controller) return;
    
    // Disable all mapping buttons during mapping
    for (auto button : mappingButtons) {
        button->setEnabled(false);
    }
    
    // Update the current button being mapped
    currentMappingButton = logicalButton;
    mappingButtons[logicalButton]->setText(tr("Press button..."));
    
    // Start button detection mode
    controller->startButtonDetection();
    
    // Start timeout timer
    mappingTimeoutTimer->start();
    
    // Show status message
    QApplication::setOverrideCursor(Qt::WaitCursor);
}

void ControllerMappingDialog::onRawButtonPressed(int sdlButton, const QString &buttonName) {
    if (currentMappingButton.isEmpty()) return;
    
    // Stop detection and timeout
    controller->stopButtonDetection();
    mappingTimeoutTimer->stop();
    QApplication::restoreOverrideCursor();
    
    // Check if this button is already mapped to another function
    QMap<QString, int> currentMappings = controller->getAllPhysicalMappings();
    QString conflictingButton;
    for (auto it = currentMappings.begin(); it != currentMappings.end(); ++it) {
        if (it.value() == sdlButton && it.key() != currentMappingButton) {
            conflictingButton = it.key();
            break;
        }
    }
    
    if (!conflictingButton.isEmpty()) {
        int ret = QMessageBox::question(this, tr("Button Conflict"), 
            tr("The button '%1' is already mapped to '%2'.\n\nDo you want to reassign it to '%3'?")
            .arg(buttonName).arg(conflictingButton).arg(currentMappingButton),
            QMessageBox::Yes | QMessageBox::No);
        
        if (ret != QMessageBox::Yes) {
            // Reset UI and return
            mappingButtons[currentMappingButton]->setText(tr("Map"));
            for (auto button : mappingButtons) {
                button->setEnabled(true);
            }
            currentMappingButton.clear();
            return;
        }
        
        // Clear the conflicting mapping
        controller->setPhysicalButtonMapping(conflictingButton, -1);
        currentMappingLabels[conflictingButton]->setText("Not mapped");
        currentMappingLabels[conflictingButton]->setStyleSheet("color: gray;");
    }
    
    // Set the new mapping
    controller->setPhysicalButtonMapping(currentMappingButton, sdlButton);
    
    // Get appropriate text color for current theme
    QString textColor = isDarkMode() ? "white" : "black";
    
    // Update UI
    currentMappingLabels[currentMappingButton]->setText(buttonName);
    currentMappingLabels[currentMappingButton]->setStyleSheet(QString("color: %1; font-weight: bold;").arg(textColor));
    mappingButtons[currentMappingButton]->setText(tr("Map"));
    
    // Re-enable all buttons
    for (auto button : mappingButtons) {
        button->setEnabled(true);
    }
    
    currentMappingButton.clear();
    
    QMessageBox::information(this, tr("Mapping Complete"), 
        tr("Button '%1' has been successfully mapped!").arg(buttonName));
}

void ControllerMappingDialog::resetToDefaults() {
    int ret = QMessageBox::question(this, tr("Reset to Defaults"), 
        tr("Are you sure you want to reset all button mappings to their default values?\n\nThis will overwrite your current configuration."),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        // Get default mappings and apply them
        QMap<QString, int> defaults = controller->getDefaultMappings();
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            controller->setPhysicalButtonMapping(it.key(), it.value());
        }
        
        // Update display
        updateMappingDisplay();
        
        QMessageBox::information(this, tr("Reset Complete"), 
            tr("All button mappings have been reset to their default values."));
    }
}

void ControllerMappingDialog::applyMappings() {
    // Mappings are already applied in real-time, just close the dialog
    accept();
}

bool ControllerMappingDialog::isDarkMode() const {
    // Same logic as MainWindow::isDarkMode()
    QColor bg = palette().color(QPalette::Window);
    return bg.lightness() < 128;  // Lightness scale: 0 (black) - 255 (white)
} 