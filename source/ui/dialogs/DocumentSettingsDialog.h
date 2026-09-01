#ifndef DOCUMENTSETTINGSDIALOG_H
#define DOCUMENTSETTINGSDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QColor>

// Forward declarations
class MainWindow;
class Document;

/**
 * @brief Per-document override settings dialog ("Current Document Settings").
 *
 * Unlike ControlPanelDialog (which edits global QSettings defaults for NEW
 * documents), this dialog applies overrides directly to the currently open
 * Document - a "live" apply. It replaces the standalone OCR Language item in
 * the navigation-bar overflow menu.
 *
 * Tabs:
 * - Page: page-size override (sets Document::defaultPageSize; new pages added
 *   via Ctrl+Shift+A use it - existing pages are NOT resized) plus the
 *   document-wide background.
 * - OCR: recognizer language override, the three toggles that are also on the
 *   OCR subtoolbar (auto-recognize, show text, snap to grid), and the
 *   CJK grid-cell mode override.
 * - Theme: PDF dark inversion overrides (PDF-backed documents only).
 */
class DocumentSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct the per-document settings dialog.
     * @param mainWindow Reference to MainWindow (OCR data + worker refresh).
     * @param doc The currently open document to override (may be nullptr).
     * @param parent Parent widget.
     */
    explicit DocumentSettingsDialog(MainWindow* mainWindow, Document* doc,
                                    QWidget* parent = nullptr);

protected:
    /**
     * @brief Override done() to fix Android/iOS keyboard crash (BUG-A001),
     * mirroring ControlPanelDialog::done().
     */
    void done(int result) override;

private slots:
    void applyChanges();
    void onPageSizePresetChanged(int index);
    void chooseBackgroundColor();
    void chooseGridColor();

private:
    MainWindow* mainWindowRef = nullptr;
    Document* m_doc = nullptr;

    // === Dialog widgets ===
    QTabWidget* tabWidget = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* okButton = nullptr;
    QPushButton* cancelButton = nullptr;

    // === Page tab ===
    QWidget* pageTab = nullptr;
    QComboBox* pageSizeCombo = nullptr;
    QLabel* pageSizeDimLabel = nullptr;
    // Background section (applies document-wide, not just to new pages).
    QComboBox* bgStyleCombo = nullptr;
    QPushButton* bgColorButton = nullptr;
    QPushButton* gridColorButton = nullptr;
    QSpinBox* gridSpacingSpin = nullptr;
    QSpinBox* lineSpacingSpin = nullptr;
    QColor selectedBgColor;
    QColor selectedGridColor;
    // The document's background type at load time (as Page::BackgroundType int).
    // Used to preserve a non-listed type (PDF/Custom) on apply so editing only
    // colour/spacing never silently rewrites it to "None".
    int loadedBgTypeValue = 0;
    bool bgTypeInCombo = true;
    void createPageTab();

    // === OCR tab ===
    QWidget* ocrTab = nullptr;
    QComboBox* ocrLanguageCombo = nullptr;
    QCheckBox* autoOcrCheck = nullptr;
    QCheckBox* showTextCheck = nullptr;
    QCheckBox* snapToGridCheck = nullptr;
    // Tri-state override of the global CJK setting: -1 inherit / 0 off / 1 on.
    QComboBox* cjkGridModeCombo = nullptr;
    void createOcrTab();

    // === Theme tab (PDF-backed documents only) ===
    QWidget* themeTab = nullptr;
    QComboBox* pdfInvertDarkCombo = nullptr;
    QComboBox* pdfInvertImagesCombo = nullptr;
    void createThemeTab();

    void loadSettings();
};

#endif // DOCUMENTSETTINGSDIALOG_H
