#ifndef BATCHEXPORTDIALOG_H
#define BATCHEXPORTDIALOG_H

#include <QDialog>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QWidget;

/**
 * Combined notebook export dialog.
 *
 * The PDF tab exposes rendering/page options and excludes edgeless notebooks; it
 * is omitted entirely when none of the selected notebooks can be exported to PDF.
 * The package tab exports every selected notebook as an .snbx archive.
 */
class BatchExportDialog : public QDialog
{
    Q_OBJECT

public:
    enum ExportFormat {
        Pdf,
        Snbx
    };

    enum DpiPreset {
        DpiScreen = 96,
        DpiDraft = 150,
        DpiPrint = 300,
        DpiCustom = -1
    };

    explicit BatchExportDialog(const QStringList& bundlePaths,
                               QWidget* parent = nullptr,
                               ExportFormat initialFormat = Pdf);

    ExportFormat selectedFormat() const;
    QString outputDirectory() const;

    int dpi() const;
    QString pageRange() const;
    bool annotationsOnly() const;
    bool darkModeBackground() const;
    bool darkenStrokes() const;
    bool includeMetadata() const;
    bool includeOutline() const;

    bool includePdf() const;

    QStringList bundles() const { return m_bundlePaths; }
    QStringList validPdfBundles() const { return m_validPdfBundles; }
    QStringList skippedPdfBundles() const { return m_skippedPdfBundles; }

private slots:
    void browsePdfOutput();
    void browseSnbxOutput();
    void onPageRangeToggled(bool rangeSelected);
    void onDpiPresetChanged();
    void validateExportButton();
    void acceptExport();

private:
    void filterEdgelessNotebooks();
    void setupUi();
    QWidget* createPdfTab();
    QWidget* createSnbxTab();
    QString chooseOutputDirectory(QLineEdit* edit) const;
    QString mobileOutputDirectory(const QString& extension) const;
    void loadSettings();
    void saveSettings() const;

    QStringList m_bundlePaths;
    QStringList m_validPdfBundles;
    QStringList m_skippedPdfBundles;

    QTabWidget* m_tabs = nullptr;
    QWidget* m_pdfTab = nullptr;
    QWidget* m_snbxTab = nullptr;

    QLabel* m_pdfWarningLabel = nullptr;
    QLineEdit* m_pdfOutputEdit = nullptr;
    QLineEdit* m_snbxOutputEdit = nullptr;

    QRadioButton* m_allPagesRadio = nullptr;
    QRadioButton* m_pageRangeRadio = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;

    QButtonGroup* m_dpiGroup = nullptr;
    QRadioButton* m_dpiScreenRadio = nullptr;
    QRadioButton* m_dpiDraftRadio = nullptr;
    QRadioButton* m_dpiPrintRadio = nullptr;
    QRadioButton* m_dpiCustomRadio = nullptr;
    QSpinBox* m_dpiSpinBox = nullptr;

    QCheckBox* m_annotationsOnlyCheckbox = nullptr;
    QCheckBox* m_darkModeBgCheckbox = nullptr;
    QCheckBox* m_darkenStrokesCheckbox = nullptr;
    QCheckBox* m_includeMetadataCheckbox = nullptr;
    QCheckBox* m_includeOutlineCheckbox = nullptr;
    QCheckBox* m_includePdfCheckbox = nullptr;

    QPushButton* m_exportButton = nullptr;
};

#endif // BATCHEXPORTDIALOG_H
