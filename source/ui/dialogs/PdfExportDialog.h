#pragma once

// ============================================================================
// PdfExportDialog - PDF Export Options Dialog
// ============================================================================
// Part of SpeedyNote PDF Export feature (Phase 8)
//
// Provides a user-friendly dialog for configuring PDF export options:
// - Output file path selection
// - Page range (all pages or custom range like "1-10, 15")
// - DPI/quality presets (96 screen, 150 draft, 300 print, custom)
// ============================================================================

#include <QDialog>
#include <QString>

// Forward declarations
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QButtonGroup;
class Document;

/**
 * @brief Dialog for configuring PDF export options.
 * 
 * Allows the user to select:
 * - Output file path (with file browser)
 * - Page range (all or custom range)
 * - Export quality/DPI
 * 
 * Usage:
 * @code
 * PdfExportDialog dialog(document, this);
 * if (dialog.exec() == QDialog::Accepted) {
 *     PdfExportOptions options = dialog.exportOptions();
 *     // Use options with MuPdfExporter
 * }
 * @endcode
 */
class PdfExportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief DPI preset values.
     */
    enum DpiPreset {
        DpiScreen = 96,    ///< Screen quality (smallest file)
        DpiDraft = 150,    ///< Draft print quality
        DpiPrint = 300,    ///< Standard print quality (default)
        DpiCustom = -1     ///< Custom value from spinbox
    };

    /**
     * @brief Construct the export dialog.
     * @param document The document to export (for default filename and page count)
     * @param parent Parent widget
     */
    explicit PdfExportDialog(Document* document, QWidget* parent = nullptr);
    
    /**
     * @brief Get the selected output file path.
     */
    QString outputPath() const;
    
    /**
     * @brief Get the page range string.
     * @return Empty string for "all pages", or range like "1-10, 15"
     */
    QString pageRange() const;
    
    /**
     * @brief Get the selected DPI value.
     * @return DPI value (96, 150, 300, or custom value)
     */
    int dpi() const;
    
    /**
     * @brief Check if "all pages" is selected.
     */
    bool isAllPages() const;
    
    /**
     * @brief Check if "annotations only" is selected.
     * @return true if strokes should be exported on blank background (no PDF/grid/lines)
     */
    bool annotationsOnly() const;

private slots:
    void onBrowseClicked();
    void onPageRangeToggled(bool rangeSelected);
    void onDpiPresetChanged();
    void validateAndUpdateExportButton();

private:
    void setupUI();
    QString generateDefaultFilename() const;
    
    // Document reference
    Document* m_document = nullptr;
    
    // Output path section
    QLabel* m_outputLabel = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    
    // Page range section
    QRadioButton* m_allPagesRadio = nullptr;
    QRadioButton* m_pageRangeRadio = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;
    QLabel* m_pageCountLabel = nullptr;
    
    // DPI/Quality section
    QRadioButton* m_dpiScreenRadio = nullptr;
    QRadioButton* m_dpiDraftRadio = nullptr;
    QRadioButton* m_dpiPrintRadio = nullptr;
    QRadioButton* m_dpiCustomRadio = nullptr;
    QSpinBox* m_dpiSpinBox = nullptr;
    QButtonGroup* m_dpiGroup = nullptr;
    
    // Options section
    class QCheckBox* m_annotationsOnlyCheckbox = nullptr;
    
    // Action buttons
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
};

