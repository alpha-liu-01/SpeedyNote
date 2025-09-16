#ifndef CONTROLPANELDIALOG_H
#define CONTROLPANELDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QColor>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QLabel>
#include <QLabel>

#include "InkCanvas.h" // Needed for BackgroundStyle enum
#include "KeyCaptureDialog.h"

// Forward declarations
class MainWindow;
#include "ControllerMappingDialog.h" // New: Include controller mapping dialog

class ControlPanelDialog : public QDialog {
    Q_OBJECT

public:
    explicit ControlPanelDialog(MainWindow *mainWindow, InkCanvas *targetCanvas, QWidget *parent = nullptr);

private slots:
    void applyChanges();
    void chooseColor();
    void addKeyboardMapping();       // New: add keyboard shortcut
    void removeKeyboardMapping();    // New: remove keyboard shortcut
    void openControllerMapping();    // New: open controller mapping dialog
    void reconnectController();      // New: reconnect controller
    void selectFolderCompatibility(); // New: compatibility folder selection

private:
    InkCanvas *canvas;

    QTabWidget *tabWidget;
    QWidget *backgroundTab;

    QComboBox *styleCombo;
    QPushButton *colorButton;
    QSpinBox *densitySpin;

    QPushButton *applyButton;
    QPushButton *okButton;
    QPushButton *cancelButton;

    QColor selectedColor;

    void createBackgroundTab();
    void loadFromCanvas();

    MainWindow *mainWindowRef;
    InkCanvas *canvasRef;
    QWidget *performanceTab;
    QWidget *toolbarTab;
    void createToolbarTab();
    void createPerformanceTab();

    QWidget *controllerMappingTab;
    QPushButton *reconnectButton;
    QLabel *controllerStatusLabel;

    // Mapping comboboxes for hold and press
    QMap<QString, QComboBox*> holdMappingCombos;
    QMap<QString, QComboBox*> pressMappingCombos;

    void createButtonMappingTab();
    void createControllerMappingTab(); // New: create controller mapping tab
    void createKeyboardMappingTab();  // New: keyboard mapping tab
    void createMouseDialTab();        // New: mouse dial control tab

    // Keyboard mapping widgets
    QWidget *keyboardTab;
    QTableWidget *keyboardTable;
    QPushButton *addKeyboardMappingButton;
    QPushButton *removeKeyboardMappingButton;
    
    // Mouse dial control widgets
    QWidget *mouseDialTab;
    QMap<QString, QComboBox*> mouseDialMappingCombos;
    
    // Theme widgets
    QWidget *themeTab;

    QCheckBox *useCustomAccentCheckbox;
    QPushButton *accentColorButton;
    QColor selectedAccentColor;
    
    // Color palette widgets
    QCheckBox *useBrighterPaletteCheckbox;
    
    // About tab widgets
    QWidget *aboutTab;
    
    // Compatibility tab widgets
    QWidget *compatibilityTab;
    QPushButton *selectFolderCompatibilityButton;
    
    // Language tab widgets
    QWidget *languageTab;
    QComboBox *languageCombo;
    QCheckBox *useSystemLanguageCheckbox;
    
    void createThemeTab();
    void chooseAccentColor();
    void createAboutTab();            // New: create about tab
    void createCompatibilityTab();    // New: create compatibility tab
    void createLanguageTab();         // New: create language tab
    void updateControllerStatus();    // Update controller connection status display
};

#endif // CONTROLPANELDIALOG_H
