#pragma once

// ============================================================================
// NotebookExporter - Export notebooks as .snbx packages
// ============================================================================
// Part of the Share/Import feature (Phase 1)
//
// Creates ZIP-compressed .snbx packages containing:
// - The .snb folder (notebook bundle)
// - Optionally, an embedded/ folder with the PDF
//
// The exported package can be shared via Android's share sheet or saved
// to disk on desktop platforms.
// ============================================================================

#include <QString>
#include <QObject>

class Document;

/**
 * @brief Exports notebooks as compressed .snbx packages.
 * 
 * Usage:
 * @code
 * NotebookExporter::ExportOptions options;
 * options.destPath = "/path/to/MyNotebook.snbx";
 * options.includePdf = true;
 * 
 * auto result = NotebookExporter::exportPackage(doc, options);
 * if (result.success) {
 *     // Share or save result.exportedPath
 * }
 * @endcode
 */
class NotebookExporter : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief Options for exporting a notebook package.
     */
    struct ExportOptions {
        bool includePdf = false;        ///< Whether to embed the PDF in the package
        QString destPath;               ///< Full path including .snbx extension
    };
    
    /**
     * @brief Result of an export operation.
     */
    struct ExportResult {
        bool success = false;           ///< True if export completed successfully
        QString errorMessage;           ///< Error description if success is false
        QString exportedPath;           ///< Path to the created .snbx file
        qint64 fileSize = 0;            ///< Size of the exported file in bytes
    };
    
    /**
     * @brief Export a notebook as a .snbx package.
     * 
     * Creates a ZIP file containing:
     * - NotebookName.snb/ folder with all notebook contents
     * - embedded/ folder with PDF (if includePdf is true and PDF exists)
     * 
     * The embedded PDF's path is stored as a relative path in document.json:
     * `pdf_relative_path = "../embedded/filename.pdf"`
     * 
     * @param doc The document to export (must be saved first)
     * @param options Export options including destination path
     * @return ExportResult with success status and file info
     */
    static ExportResult exportPackage(Document* doc, const ExportOptions& options);
    
    /**
     * @brief Get the estimated size of the PDF for UI display.
     * 
     * Returns 0 if the document has no PDF or PDF file doesn't exist.
     * 
     * @param doc The document to check
     * @return Size of the PDF in bytes, or 0 if no PDF
     */
    static qint64 estimatePdfSize(Document* doc);
    
signals:
    /**
     * @brief Emitted during export to report progress.
     * @param percent Progress percentage (0-100)
     */
    void progressChanged(int percent);
};

