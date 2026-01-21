#ifndef CONTROLPANELDIALOG_H
#define CONTROLPANELDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QColor>
#include <QSpinBox>
#include <QTreeWidget>

#include "ui/dialogs/KeyCaptureDialog.h"

#ifdef SPEEDYNOTE_CONTROLLER_SUPPORT
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

protected:
    /**
     * @brief Override done() to fix Android keyboard crash (BUG-A001).
     * Hides the virtual keyboard before dialog destruction.
     */
    void done(int result) override;

private slots:
    void applyChanges();
    void chooseAccentColor();
    void chooseBackgroundColor();
    void chooseGridColor();
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

    // === Background tab ===
    QWidget *backgroundTab;
    QComboBox *styleCombo;
    QComboBox *pageSizeCombo;
    QLabel *pageSizeDimLabel;
    QPushButton *bgColorButton;
    QPushButton *gridColorButton;
    QSpinBox *gridSpacingSpin;
    QSpinBox *lineSpacingSpin;
    QColor selectedBgColor;
    QColor selectedGridColor;
    void createBackgroundTab();
    void onPageSizePresetChanged(int index);

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

    // === Keyboard Shortcuts tab (Phase 5.1) ===
    QWidget *shortcutsTab;
    QTreeWidget *shortcutsTree;
    QPushButton *resetAllShortcutsButton;
    QPushButton *openConfigFolderButton;
    void createShortcutsTab();
    void populateShortcutsTree();
    void onShortcutItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onEditShortcut();
    void onResetShortcut();
    void onResetAllShortcuts();
    void onOpenConfigFolder();
    void updateShortcutDisplay(QTreeWidgetItem* item, const QString& actionId);

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
