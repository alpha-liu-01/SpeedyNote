#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>

class QLabel;
class QCheckBox;
class QProgressBar;
class QPushButton;
class Document;

/**
 * @brief Dialog for exporting notebooks as .snbx packages.
 * 
 * Displays options for export:
 * - Checkbox to optionally include the PDF file
 * - Progress indicator during export
 * - Export and Cancel buttons
 * 
 * Part of the Share/Import feature (Phase 1).
 */
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the export dialog.
     * @param doc Document to export (used to get PDF size info)
     * @param parent Parent widget
     */
    explicit ExportDialog(Document* doc, QWidget* parent = nullptr);
    
    /**
     * @brief Check if user wants to include PDF in export.
     * @return True if "Include PDF" checkbox is checked
     */
    bool includePdf() const;
    
    /**
     * @brief Show the progress bar in indeterminate mode.
     * Call this when export begins.
     */
    void showProgress();
    
    /**
     * @brief Hide the progress bar and re-enable buttons.
     * Call this when export completes or fails.
     */
    void hideProgress();

private:
    void setupUI();
    void updatePdfCheckboxText();
    
    Document* m_document = nullptr;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QCheckBox* m_includePdfCheckbox = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    
    qint64 m_pdfSize = 0;
};

#endif // EXPORTDIALOG_H

