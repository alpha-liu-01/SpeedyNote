#ifndef EXPORTRESULTSDIALOG_H
#define EXPORTRESULTSDIALOG_H

/**
 * @file ExportResultsDialog.h
 * @brief Dialog showing detailed results of a batch export operation.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - Summary header with success/skip/fail counts
 * - Scrollable list of individual file results
 * - Color-coded status icons (green ✓, yellow ⚠, red ✗)
 * - "Retry Failed" button for re-exporting failed items
 * - "Show in Folder" button to open output directory (desktop only)
 * 
 * @see docs/private/BATCH_OPERATIONS.md
 */

#include <QDialog>
#include "../../batch/BatchOperations.h"

class QLabel;
class QListWidget;
class QPushButton;

/**
 * @brief Dialog showing detailed results of a batch export operation.
 * 
 * Displays individual file results with status icons and messages.
 * Provides retry functionality for failed exports.
 * 
 * Usage:
 * @code
 * ExportResultsDialog dialog(result, "/output/dir", this);
 * connect(&dialog, &ExportResultsDialog::retryRequested, this, &MyClass::retryExport);
 * dialog.exec();
 * @endcode
 */
class ExportResultsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the results dialog.
     * @param result Batch operation result with per-file details
     * @param outputDir Output directory path (for "Show in Folder" button)
     * @param parent Parent widget
     */
    explicit ExportResultsDialog(const BatchOps::BatchResult& result,
                                  const QString& outputDir,
                                  QWidget* parent = nullptr);
    
    /**
     * @brief Set dialog title.
     * @param title Custom title (default is "Export Results")
     */
    void setTitle(const QString& title);
    
    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode
     */
    void setDarkMode(bool dark);

signals:
    /**
     * @brief Emitted when user clicks "Retry Failed".
     * @param failedPaths List of input paths that failed
     */
    void retryRequested(const QStringList& failedPaths);

private slots:
    void onRetryClicked();
    void onShowFolderClicked();

private:
    void setupUi();
    void populateResults();
    void updateSummary();
    QString formatFileSize(qint64 bytes) const;
    QString extractDisplayName(const QString& path) const;
    
    // Data
    BatchOps::BatchResult m_result;
    QString m_outputDir;
    
    // Appearance
    bool m_darkMode = false;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QListWidget* m_resultsList = nullptr;
    QPushButton* m_retryButton = nullptr;
    QPushButton* m_showFolderButton = nullptr;
    QPushButton* m_okButton = nullptr;
    
    // Constants
    static constexpr int DIALOG_MIN_WIDTH = 450;
    static constexpr int DIALOG_MIN_HEIGHT = 350;
    static constexpr int DIALOG_MAX_WIDTH = 650;
    static constexpr int DIALOG_MAX_HEIGHT = 550;
};

#endif // EXPORTRESULTSDIALOG_H
