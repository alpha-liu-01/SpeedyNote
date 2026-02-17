#ifndef BATCHIMPORTDIALOG_H
#define BATCHIMPORTDIALOG_H

/**
 * @file BatchImportDialog.h
 * @brief Dialog for batch importing .snbx notebook packages (Desktop only).
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - File list showing selected .snbx files
 * - "Add Files..." button to select individual files
 * - "Add Folder..." button to scan a directory for .snbx files
 * - Destination directory picker
 * - Duplicate detection and warnings
 * 
 * On Android, this dialog is not used - the native document picker
 * handles file selection directly.
 * 
 * @see docs/private/BATCH_OPERATIONS.md
 */

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)  // Desktop only

#include <QDialog>
#include <QStringList>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;

/**
 * @brief Dialog for selecting and importing multiple .snbx files.
 * 
 * Desktop-only dialog that allows users to:
 * - Add individual .snbx files
 * - Scan a folder for .snbx files
 * - Choose destination directory for imported notebooks
 * 
 * Usage:
 * @code
 * BatchImportDialog dialog(this);
 * if (dialog.exec() == QDialog::Accepted) {
 *     QStringList files = dialog.selectedFiles();
 *     QString destDir = dialog.destinationDirectory();
 *     // Import files...
 * }
 * @endcode
 */
class BatchImportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct the import dialog.
     * @param parent Parent widget
     */
    explicit BatchImportDialog(QWidget* parent = nullptr);
    
    /**
     * @brief Get the list of selected .snbx files.
     * @return List of absolute paths to .snbx files
     */
    QStringList selectedFiles() const { return m_selectedFiles; }
    
    /**
     * @brief Get the destination directory for imported notebooks.
     * @return Absolute path to destination directory
     */
    QString destinationDirectory() const;
    
    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Static convenience method to get import files.
     * 
     * Shows the dialog and returns selected files if accepted.
     * 
     * @param parent Parent widget
     * @param destDir Output: destination directory chosen by user
     * @return List of selected files, or empty if cancelled
     */
    static QStringList getImportFiles(QWidget* parent, QString* destDir = nullptr);

private slots:
    void onAddFilesClicked();
    void onAddFolderClicked();
    void onRemoveSelectedClicked();
    void onClearAllClicked();
    void onBrowseDestClicked();
    void onImportClicked();
    void updateImportButton();

private:
    void setupUi();
    void addFiles(const QStringList& files);
    void updateFileCount();
    bool isDuplicate(const QString& filePath) const;
    QString extractDisplayName(const QString& filePath) const;
    
    // Data
    QStringList m_selectedFiles;
    bool m_darkMode = false;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    QLabel* m_fileCountLabel = nullptr;
    QListWidget* m_fileList = nullptr;
    QPushButton* m_addFilesButton = nullptr;
    QPushButton* m_addFolderButton = nullptr;
    QPushButton* m_removeSelectedButton = nullptr;
    QPushButton* m_clearAllButton = nullptr;
    QLineEdit* m_destEdit = nullptr;
    QPushButton* m_destBrowseButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_importButton = nullptr;
    
    // Constants
    static constexpr int DIALOG_MIN_WIDTH = 550;
    static constexpr int DIALOG_MIN_HEIGHT = 450;
};

#endif // !Q_OS_ANDROID && !Q_OS_IOS

#endif // BATCHIMPORTDIALOG_H
