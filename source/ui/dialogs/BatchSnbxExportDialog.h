#ifndef BATCHSNBXEXPORTDIALOG_H
#define BATCHSNBXEXPORTDIALOG_H

/**
 * @file BatchSnbxExportDialog.h
 * @brief Dialog for batch SNBX (package) export.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - Simple UI with just output location and "Include PDF" option
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
class QCheckBox;

/**
 * @brief Dialog for configuring batch SNBX export options.
 * 
 * Supports exporting 1 or more notebooks to SNBX packages.
 * All notebooks can be exported to SNBX (including edgeless).
 * 
 * Usage:
 * @code
 * QStringList bundles = {"~/Notes/Math.snb", "~/Notes/Physics.snb"};
 * BatchSnbxExportDialog dialog(bundles, this);
 * if (dialog.exec() == QDialog::Accepted) {
 *     // Use dialog.outputDirectory(), dialog.includePdf()
 * }
 * @endcode
 */
class BatchSnbxExportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the batch SNBX export dialog.
     * @param bundlePaths List of .snb bundle paths to export
     * @param parent Parent widget
     */
    explicit BatchSnbxExportDialog(const QStringList& bundlePaths, QWidget* parent = nullptr);
    
    /**
     * @brief Get the output directory (desktop only).
     * @return Selected output directory path
     */
    QString outputDirectory() const;
    
    /**
     * @brief Check if PDF should be included in packages.
     * @return true if source PDF should be embedded (default: true)
     */
    bool includePdf() const;
    
    /**
     * @brief Get the list of bundles to export.
     */
    QStringList bundles() const { return m_bundlePaths; }
    
    /**
     * @brief Get the number of bundles to export.
     */
    int bundleCount() const { return static_cast<int>(m_bundlePaths.size()); }

private slots:
    void onBrowseClicked();
    void validateAndUpdateExportButton();

private:
    void setupUi();
    void updateTitle();
    
    // Input bundles
    QStringList m_bundlePaths;
    
    // Dark mode
    bool m_darkMode = false;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    
    // Output (desktop only)
    QLineEdit* m_outputEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    
    // Options
    QCheckBox* m_includePdfCheckbox = nullptr;
    
    // Buttons
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_exportButton = nullptr;
};

#endif // BATCHSNBXEXPORTDIALOG_H
