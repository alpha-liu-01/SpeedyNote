#ifndef CONTROLPANELDIALOG_H
#define CONTROLPANELDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QColor>

#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
#include "KeyCaptureDialog.h"
#include "ControllerMappingDialog.h"
#endif

// Forward declarations
class MainWindow;

/**
 * @brief Control Panel dialog for application settings.
 * 
 * Phase CP.1: Cleaned up after migration. Contains only working tabs:
 * - Theme: Custom accent color settings
 * - Language: Language selection
 * - Cache: Cache management (TODO: integrate with NotebookLibrary)
 * - About: Application info
 * 
 * Controller-related tabs are conditionally compiled with SPEEDYNOTE_CONTROLLER_SUPPORT.
 */
class ControlPanelDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct the Control Panel dialog.
     * @param mainWindow Reference to MainWindow for settings access.
     * @param parent Parent widget.
     */
    explicit ControlPanelDialog(MainWindow *mainWindow, QWidget *parent = nullptr);

private slots:
    void applyChanges();
    void chooseAccentColor();
    void loadSettings();

#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
    void openControllerMapping();
    void reconnectController();
#endif

private:
    MainWindow *mainWindowRef;

    // === Dialog widgets ===
    QTabWidget *tabWidget;
    QPushButton *applyButton;
    QPushButton *okButton;
    QPushButton *cancelButton;

    // === Theme tab ===
    QWidget *themeTab;
    QCheckBox *useCustomAccentCheckbox;
    QPushButton *accentColorButton;
    QColor selectedAccentColor;
    void createThemeTab();

    // === Language tab ===
    QWidget *languageTab;
    QComboBox *languageCombo;
    QCheckBox *useSystemLanguageCheckbox;
    void createLanguageTab();

    // === Cache tab ===
    QWidget *cacheTab;
    void createCacheTab();

    // === About tab ===
    QWidget *aboutTab;
    void createAboutTab();

#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
    // === Controller Mapping tab (conditional) ===
    QWidget *controllerMappingTab;
    QPushButton *reconnectButton;
    QLabel *controllerStatusLabel;
    
    void createControllerMappingTab();
    void updateControllerStatus();
#endif
};

#endif // CONTROLPANELDIALOG_H
