#include "NotebookExporter.h"
#include "../core/Document.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// miniz - cross-platform ZIP library (MIT license)
#include "miniz.h"

// ============================================================================
// NotebookExporter Implementation
// ============================================================================

NotebookExporter::ExportResult NotebookExporter::exportPackage(
    Document* doc, 
    const ExportOptions& options)
{
    ExportResult result;
    
    // Validate inputs
    if (!doc) {
        result.errorMessage = QObject::tr("No document to export");
        return result;
    }
    
    if (options.destPath.isEmpty()) {
        result.errorMessage = QObject::tr("No destination path specified");
        return result;
    }
    
    QString bundlePath = doc->bundlePath();
    if (bundlePath.isEmpty()) {
        result.errorMessage = QObject::tr("Document must be saved before exporting");
        return result;
    }
    
    QDir bundleDir(bundlePath);
    if (!bundleDir.exists()) {
        result.errorMessage = QObject::tr("Document bundle not found: %1").arg(bundlePath);
        return result;
    }
    
    // Get the notebook name from the bundle folder name
    QString notebookName = bundleDir.dirName();
    if (!notebookName.endsWith(".snb")) {
        notebookName += ".snb";
    }
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "NotebookExporter: Exporting" << notebookName 
             << "to" << options.destPath
             << "(includePdf:" << options.includePdf << ")";
    #endif
    // Create parent directory for destination if needed
    QFileInfo destInfo(options.destPath);
    QDir destDir = destInfo.absoluteDir();
    if (!destDir.exists() && !destDir.mkpath(".")) {
        result.errorMessage = QObject::tr("Failed to create destination directory: %1")
                                .arg(destDir.absolutePath());
        return result;
    }
    
    // Remove existing file if present
    if (QFile::exists(options.destPath)) {
        if (!QFile::remove(options.destPath)) {
            result.errorMessage = QObject::tr("Failed to remove existing file: %1")
                                    .arg(options.destPath);
            return result;
        }
    }
    
    // Initialize miniz ZIP writer
    mz_zip_archive zipArchive;
    memset(&zipArchive, 0, sizeof(zipArchive));
    
    QByteArray destPathUtf8 = options.destPath.toUtf8();
    if (!mz_zip_writer_init_file(&zipArchive, destPathUtf8.constData(), 0)) {
        result.errorMessage = QObject::tr("Failed to create ZIP file: %1")
                                .arg(options.destPath);
        return result;
    }
    
    // Helper lambda to clean up on error
    auto cleanupOnError = [&]() {
        mz_zip_writer_end(&zipArchive);
        QFile::remove(options.destPath);
    };
    
    // ===== Step 1: Add all files from the .snb bundle =====
    
    // Iterate through all files in the bundle
    QDirIterator it(bundlePath, QDir::Files | QDir::NoDotAndDotDot, 
                    QDirIterator::Subdirectories);
    
    QString pdfFileName;
    QString embeddedPdfPath;
    
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relativePath = bundleDir.relativeFilePath(filePath);
        QString zipEntryPath = notebookName + "/" + relativePath;
        QByteArray zipEntryPathUtf8 = zipEntryPath.toUtf8();
        
        // Special handling for document.json if we're embedding PDF
        if (options.includePdf && relativePath == "document.json" && !doc->pdfPath().isEmpty()) {
            // Read the original document.json
            QFile jsonFile(filePath);
            if (!jsonFile.open(QIODevice::ReadOnly)) {
                result.errorMessage = QObject::tr("Failed to read document.json");
                cleanupOnError();
                return result;
            }
            
            QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll());
            jsonFile.close();
            
            if (!jsonDoc.isObject()) {
                result.errorMessage = QObject::tr("Invalid document.json format");
                cleanupOnError();
                return result;
            }
            
            // Modify the JSON to add embedded PDF relative path
            QJsonObject root = jsonDoc.object();
            
            // Get the PDF filename
            QFileInfo pdfInfo(doc->pdfPath());
            pdfFileName = pdfInfo.fileName();
            embeddedPdfPath = "../embedded/" + pdfFileName;
            
            // Add the embedded PDF relative path
            root["pdf_relative_path"] = embeddedPdfPath;
            
            // Write modified JSON to ZIP
            QJsonDocument modifiedDoc(root);
            QByteArray modifiedJson = modifiedDoc.toJson(QJsonDocument::Indented);
            
            if (!mz_zip_writer_add_mem(&zipArchive, zipEntryPathUtf8.constData(),
                                        modifiedJson.constData(), modifiedJson.size(),
                                        MZ_BEST_COMPRESSION)) {
                result.errorMessage = QObject::tr("Failed to add document.json to archive");
                cleanupOnError();
                return result;
            }
            
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "NotebookExporter: Modified document.json with embedded PDF path:" 
                     << embeddedPdfPath;
            #endif
        } else {
            // Regular file - add directly
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "NotebookExporter: Failed to read file:" << filePath;
                continue; // Skip this file but continue
            }
            
            QByteArray fileContent = file.readAll();
            file.close();
            
            if (!mz_zip_writer_add_mem(&zipArchive, zipEntryPathUtf8.constData(),
                                        fileContent.constData(), fileContent.size(),
                                        MZ_BEST_COMPRESSION)) {
                qWarning() << "NotebookExporter: Failed to add file to archive:" << filePath;
                // Continue - not fatal for non-essential files
            }
        }
    }
    
    // ===== Step 2: Add embedded PDF if requested =====
    
    if (options.includePdf && !doc->pdfPath().isEmpty()) {
        QString pdfPath = doc->pdfPath();
        
        if (QFile::exists(pdfPath)) {
            QFile pdfFile(pdfPath);
            if (pdfFile.open(QIODevice::ReadOnly)) {
                QString pdfZipPath = "embedded/" + QFileInfo(pdfPath).fileName();
                QByteArray pdfZipPathUtf8 = pdfZipPath.toUtf8();
                QByteArray pdfContent = pdfFile.readAll();
                pdfFile.close();
                
                // PDFs are already compressed, so use no compression for them
                if (!mz_zip_writer_add_mem(&zipArchive, pdfZipPathUtf8.constData(),
                                            pdfContent.constData(), pdfContent.size(),
                                            MZ_NO_COMPRESSION)) {
                    qWarning() << "NotebookExporter: Failed to add PDF to archive:" << pdfPath;
                    // Continue without PDF - not a fatal error
                }
                #ifdef SPEEDYNOTE_DEBUG
                else {
                    qDebug() << "NotebookExporter: Added embedded PDF:" << pdfZipPath
                             << "(" << pdfContent.size() << "bytes)";
                }
                #endif
            } else {
                qWarning() << "NotebookExporter: Failed to read PDF for embedding:" << pdfPath;
                // Continue without PDF - not a fatal error
            }
        } else {
            qWarning() << "NotebookExporter: PDF file not found for embedding:" << pdfPath;
            // Continue without PDF - not a fatal error
        }
    }
    
    // Finalize and close the ZIP archive
    if (!mz_zip_writer_finalize_archive(&zipArchive)) {
        result.errorMessage = QObject::tr("Failed to finalize ZIP archive");
        cleanupOnError();
        return result;
    }
    
    mz_zip_writer_end(&zipArchive);
    
    // Verify the export
    QFileInfo exportedFile(options.destPath);
    if (!exportedFile.exists()) {
        result.errorMessage = QObject::tr("Export failed - file was not created");
        return result;
    }
    
    // Success!
    result.success = true;
    result.exportedPath = options.destPath;
    result.fileSize = exportedFile.size();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "NotebookExporter: Export successful!" 
             << "Size:" << result.fileSize << "bytes"
             << "Path:" << result.exportedPath;
    #endif
    return result;
}

qint64 NotebookExporter::estimatePdfSize(Document* doc)
{
    if (!doc) {
        return 0;
    }
    
    QString pdfPath = doc->pdfPath();
    if (pdfPath.isEmpty()) {
        return 0;
    }
    
    QFileInfo pdfInfo(pdfPath);
    if (!pdfInfo.exists()) {
        return 0;
    }
    
    return pdfInfo.size();
}

