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
#include <QDoubleSpinBox>
#include <QTreeWidget>

#include "ui/dialogs/KeyCaptureDialog.h"

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
    
    /**
     * @brief Switch to the Keyboard Shortcuts tab.
     * Used by keyboard shortcut Ctrl+Alt+Shift+K.
     */
    void switchToKeyboardShortcutsTab();

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

    // === Tools tab ===
    QWidget *toolsTab;
    QDoubleSpinBox *wheelScrollSpeedSpin;
    QCheckBox *ocrCjkGridModeCheck = nullptr;
    void createToolsTab();

    // === Theme tab ===
    QWidget *themeTab;
    QCheckBox *useCustomAccentCheckbox;
    QPushButton *accentColorButton;
    QColor selectedAccentColor;
    QCheckBox *pdfDarkModeCheckbox;
    QCheckBox *skipImageMaskingCheckbox;
    void createThemeTab();

    // === Language tab ===
    QWidget *languageTab;
    QComboBox *languageCombo;
    QCheckBox *useSystemLanguageCheckbox;
    QComboBox *ocrLanguageCombo = nullptr;
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

#ifdef Q_OS_LINUX
    // === Stylus tab (Linux only) ===
    QWidget *stylusTab;
    QCheckBox *palmRejectionCheckbox;
    QSpinBox *palmRejectionDelaySpinBox;
    void createStylusTab();
#endif
};

#endif // CONTROLPANELDIALOG_H
