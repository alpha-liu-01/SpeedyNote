#pragma once

// ============================================================================
// NotebookImporter - Import notebooks from .snbx packages
// ============================================================================
// Part of the Share/Import feature (Phase 2)
//
// Extracts .snbx packages (ZIP files) containing:
// - The .snb folder (notebook bundle)
// - Optionally, an embedded/ folder with the PDF
//
// The extracted notebook can be loaded by DocumentManager.
// ============================================================================

#include <QString>
#include <QObject>

/**
 * @brief Imports notebooks from compressed .snbx packages.
 * 
 * Usage:
 * @code
 * QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
 * auto result = NotebookImporter::importPackage("/path/to/MyNotebook.snbx", destDir);
 * if (result.success) {
 *     // Load result.extractedSnbPath via DocumentManager
 * }
 * @endcode
 */
class NotebookImporter : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief Result of an import operation.
     */
    struct ImportResult {
        bool success = false;           ///< True if import completed successfully
        QString errorMessage;           ///< Error description if success is false
        QString extractedSnbPath;       ///< Path to extracted .snb folder
        QString embeddedPdfPath;        ///< Path to extracted PDF (if any, empty otherwise)
    };
    
    /**
     * @brief Import a notebook from a .snbx package.
     * 
     * Extracts the ZIP file to the destination directory:
     * - Finds the .snb folder inside the ZIP
     * - Handles name conflicts via auto-rename (e.g., "Notebook (1).snb")
     * - Extracts embedded PDF if present
     * 
     * After extraction, the notebook can be loaded via DocumentManager.
     * The dual-path system in Document::loadBundle() will resolve the PDF path.
     * 
     * @param snbxPath Path to the .snbx file to import
     * @param destDir Directory to extract to (e.g., notebooks/)
     * @return ImportResult with success status and extracted paths
     */
    static ImportResult importPackage(const QString& snbxPath, const QString& destDir);
    
    /**
     * @brief Generate a unique name if a notebook with the same name exists.
     * 
     * If "Notebook.snb" exists, returns "Notebook (1).snb", etc.
     * 
     * @param baseName Base notebook folder name (e.g., "MyNotebook.snb")
     * @param destDir Directory to check for conflicts
     * @return Unique folder name (may be same as baseName if no conflict)
     */
    static QString resolveNameConflict(const QString& baseName, const QString& destDir);
    
signals:
    /**
     * @brief Emitted during import to report progress.
     * @param percent Progress percentage (0-100)
     * @param status Current status message
     */
    void progressChanged(int percent, const QString& status);
};

