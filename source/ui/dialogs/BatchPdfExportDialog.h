#ifndef BATCHPDFEXPORTDIALOG_H
#define BATCHPDFEXPORTDIALOG_H

/**
 * @file BatchPdfExportDialog.h
 * @brief Dialog for batch PDF export with full options.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - Configurable DPI (presets and custom)
 * - Page range selection
 * - Annotations-only mode
 * - Automatic filtering of edgeless notebooks (with warning)
 * - Desktop: folder picker for output location
 * - Android: uses share sheet (no output picker)
 * 
 * @see docs/private/BATCH_OPERATIONS.md
 */

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QButtonGroup;
class QCheckBox;
class QComboBox;

/**
 * @brief Dialog for configuring batch PDF export options.
 * 
 * Supports exporting 1 or more notebooks to PDF. Automatically detects
 * and filters out edgeless notebooks (which cannot be exported to PDF).
 * 
 * Usage:
 * @code
 * QStringList bundles = {"~/Notes/Math.snb", "~/Notes/Physics.snb"};
 * BatchPdfExportDialog dialog(bundles, this);
 * if (dialog.exec() == QDialog::Accepted) {
 *     // Use dialog.validBundles(), dialog.outputDirectory(), dialog.dpi(), etc.
 * }
 * @endcode
 */
class BatchPdfExportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief DPI preset values.
     */
    enum DpiPreset {
        DpiScreen = 96,    ///< Screen quality (smallest file)
        DpiDraft = 150,    ///< Draft quality (default)
        DpiPrint = 300,    ///< Print quality
        DpiCustom = -1     ///< Custom value from spinbox
    };

    /**
     * @brief Construct the batch PDF export dialog.
     * @param bundlePaths List of .snb bundle paths to export
     * @param parent Parent widget
     */
    explicit BatchPdfExportDialog(const QStringList& bundlePaths, QWidget* parent = nullptr);
    
    // === Output Options ===
    
    /**
     * @brief Get the output directory (desktop only).
     * @return Selected output directory path
     */
    QString outputDirectory() const;
    
    /**
     * @brief Get the selected DPI value.
     * @return DPI value (96, 150, 300, or custom)
     */
    int dpi() const;
    
    /**
     * @brief Get the page range string.
     * @return Empty string for "all pages", or range like "1-10, 15"
     */
    QString pageRange() const;
    
    /**
     * @brief Check if "all pages" is selected.
     */
    bool isAllPages() const;
    
    /**
     * @brief Check if annotations-only export is selected.
     * @return true if strokes should be exported on blank background
     */
    bool annotationsOnly() const;
    
    /**
     * @brief Check if PDF metadata should be preserved.
     */
    bool includeMetadata() const;
    
    /**
     * @brief Check if PDF outline/bookmarks should be preserved.
     */
    bool includeOutline() const;
    
    // === Bundle Information ===
    
    /**
     * @brief Get bundles that can be exported (excludes edgeless).
     * @return List of valid bundle paths
     */
    QStringList validBundles() const;
    
    /**
     * @brief Get bundles that were skipped (edgeless notebooks).
     * @return List of skipped bundle paths
     */
    QStringList skippedBundles() const;
    
    /**
     * @brief Get the total count of input bundles.
     */
    int totalBundleCount() const { return static_cast<int>(m_bundlePaths.size()); }
    
    /**
     * @brief Get the count of exportable bundles.
     */
    int validBundleCount() const { return static_cast<int>(m_validBundles.size()); }
    
    /**
     * @brief Get the count of skipped bundles.
     */
    int skippedBundleCount() const { return static_cast<int>(m_skippedBundles.size()); }

private slots:
    void onBrowseClicked();
    void onPageRangeToggled(bool rangeSelected);
    void onDpiPresetChanged();
    void validateAndUpdateExportButton();

private:
    void setupUi();
    void filterEdgelessNotebooks();
    void updateTitle();
    void updateWarningLabel();
    
    // Input bundles
    QStringList m_bundlePaths;
    QStringList m_validBundles;
    QStringList m_skippedBundles;
    
    // Dark mode
    bool m_darkMode = false;
    
    // UI elements - Title
    QLabel* m_titleLabel = nullptr;
    QLabel* m_warningLabel = nullptr;  // Shows skipped edgeless count
    
    // UI elements - Output (desktop only)
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    
    // UI elements - Page range
    QRadioButton* m_allPagesRadio = nullptr;
    QRadioButton* m_pageRangeRadio = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;
    
    // UI elements - Quality/DPI
    QButtonGroup* m_dpiGroup = nullptr;
    QRadioButton* m_dpiScreenRadio = nullptr;
    QRadioButton* m_dpiDraftRadio = nullptr;
    QRadioButton* m_dpiPrintRadio = nullptr;
    QRadioButton* m_dpiCustomRadio = nullptr;
    QSpinBox* m_dpiSpinBox = nullptr;
    
    // UI elements - Options
    QCheckBox* m_annotationsOnlyCheckbox = nullptr;
    QCheckBox* m_includeMetadataCheckbox = nullptr;
    QCheckBox* m_includeOutlineCheckbox = nullptr;
    
    // UI elements - Buttons
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_exportButton = nullptr;
};

#endif // BATCHPDFEXPORTDIALOG_H
