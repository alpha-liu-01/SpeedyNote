// ============================================================================
// DocumentManager Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.1)
// ============================================================================

#include "DocumentManager.h"
#include "Document.h"
#include "NotebookLibrary.h"
#include "../sharing/NotebookImporter.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>

// Settings key for recent documents persistence
const QString DocumentManager::SETTINGS_RECENT_KEY = QStringLiteral("RecentDocuments");

// Temp bundle prefixes for unsaved documents
const QString DocumentManager::TEMP_EDGELESS_PREFIX = QStringLiteral("speedynote_edgeless_");
const QString DocumentManager::TEMP_PAGED_PREFIX = QStringLiteral("speedynote_paged_");

// ============================================================================
// Constructor / Destructor
// ============================================================================

DocumentManager::DocumentManager(QObject* parent)
    : QObject(parent)
{
    loadRecentFromSettings();
}

DocumentManager::~DocumentManager()
{
    // Clean up temp bundles and delete all owned documents
    for (Document* doc : m_documents) {
        // Clean up temp bundle if exists (handles discarded edgeless docs)
        cleanupTempBundle(doc);
        
        // Phase C.0.4: Clean up orphaned assets before deletion
        // This is the same cleanup that closeDocument() does, but for
        // documents still open when the application quits.
        doc->cleanupOrphanedAssets();
        
        delete doc;
    }
    m_documents.clear();
    m_tempBundlePaths.clear();
}

// ============================================================================
// Document Lifecycle
// ============================================================================

Document* DocumentManager::createDocument(const QString& name)
{
    // Create a new document with default settings
    auto docPtr = Document::createNew(name.isEmpty() ? tr("Untitled") : name);
    
    if (!docPtr) {
        qWarning() << "DocumentManager::createDocument: Failed to create document";
        return nullptr;
    }
    
    Document* doc = docPtr.release();  // Transfer ownership to DocumentManager
    
    m_documents.append(doc);
    m_documentPaths[doc] = QString();  // New document has no path yet
    m_modifiedFlags[doc] = false;      // New document is not modified
    
    emit documentCreated(doc);
    return doc;
}

Document* DocumentManager::createEdgelessDocument(const QString& name)
{
    // Create a new edgeless (infinite canvas) document
    auto docPtr = Document::createNew(
        name.isEmpty() ? tr("Untitled Canvas") : name,
        Document::Mode::Edgeless
    );
    
    if (!docPtr) {
        qWarning() << "DocumentManager::createEdgelessDocument: Failed to create document";
        return nullptr;
    }
    
    Document* doc = docPtr.release();  // Transfer ownership to DocumentManager
    
    m_documents.append(doc);
    m_documentPaths[doc] = QString();  // New document has no path yet
    m_modifiedFlags[doc] = false;      // New document is not modified
    
    // ========== TEMP BUNDLE CREATION (A3: Create immediately) ==========
    // Create a temp .snb bundle directory immediately to enable tile eviction.
    // This prevents unbounded memory growth for unsaved edgeless canvases.
    QString tempPath = createTempBundlePath(doc);
    if (!tempPath.isEmpty()) {
        m_tempBundlePaths[doc] = tempPath;
        
        // CRITICAL: Call saveBundle() to:
        // 1. Write document.json manifest
        // 2. Set m_lazyLoadEnabled = true (enables evictDistantTiles())
        // Without this, eviction won't work and memory will grow unbounded.
        if (doc->saveBundle(tempPath)) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Initialized temp bundle at" << tempPath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to initialize temp bundle, tile eviction disabled";
        }
    } else {
        qWarning() << "DocumentManager: Failed to create temp bundle dir, tile eviction disabled";
    }
    // ====================================================================
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager: Created edgeless document" << doc->name;
#endif
    
    emit documentCreated(doc);
    return doc;
}

Document* DocumentManager::loadDocument(const QString& path)
{
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::loadDocument: Empty path";
        return nullptr;
    }
    
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        qWarning() << "DocumentManager::loadDocument: File does not exist:" << path;
        return nullptr;
    }
    
    QString suffix = fileInfo.suffix().toLower();
    
    // Handle PDF files - create document for PDF annotation
    if (suffix == "pdf") {
        auto docPtr = Document::createForPdf(fileInfo.baseName(), path);
        if (!docPtr) {
            qWarning() << "DocumentManager::loadDocument: Failed to load PDF:" << path;
            return nullptr;
        }
        
        Document* doc = docPtr.release();
        m_documents.append(doc);
        m_documentPaths[doc] = QString();  // PDF-based doc has no .snx path yet
        m_modifiedFlags[doc] = false;
        
        addToRecent(path);
        emit documentLoaded(doc);
        return doc;
    }
    
    // Handle .snbx packages - extract and load the contained notebook
    if (suffix == "snbx") {
        // Determine extraction destination
#ifdef Q_OS_ANDROID
        // On Android, extract to app-private notebooks directory
        QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
#else
        // On desktop, extract next to the .snbx file
        QString destDir = fileInfo.absolutePath();
#endif
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Importing .snbx package" << path << "to" << destDir;
#endif
        
        auto importResult = NotebookImporter::importPackage(path, destDir);
        if (!importResult.success) {
            qWarning() << "DocumentManager::loadDocument: Failed to import .snbx:" << importResult.errorMessage;
            return nullptr;
        }
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Extracted to" << importResult.extractedSnbPath;
        if (!importResult.embeddedPdfPath.isEmpty()) {
            qDebug() << "DocumentManager: Embedded PDF at" << importResult.embeddedPdfPath;
        }
#endif
        
#ifdef Q_OS_ANDROID
        // Clean up the source .snbx file from /imports/ directory
        // This prevents disk space leaks from accumulated imports
        // Note: Only do this on Android where we control the import copy location
        QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString importsDir = appDataDir + "/imports";
        if (path.startsWith(importsDir)) {
            QFile::remove(path);
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Cleaned up imported .snbx file:" << path;
#endif
        }
#endif
        
        // Recursively load the extracted .snb bundle
        // The dual-path system in Document::loadBundle() will resolve the PDF
        return loadDocument(importResult.extractedSnbPath);
    }
    
    // Handle .snb bundle directories - edgeless documents with O(1) tile loading
    if (suffix == "snb" || fileInfo.isDir()) {
        // Check if it's a valid bundle (has document.json)
        QString manifestPath = path + "/document.json";
        if (!QFile::exists(manifestPath)) {
            if (suffix == "snb") {
                qWarning() << "DocumentManager::loadDocument: Invalid bundle (no manifest):" << path;
                return nullptr;
            }
            // Not a bundle directory, fall through to other handlers
        } else {
            auto docPtr = Document::loadBundle(path);
            if (!docPtr) {
                qWarning() << "DocumentManager::loadDocument: Failed to load bundle:" << path;
                return nullptr;
            }
            
            Document* doc = docPtr.release();
            m_documents.append(doc);
            m_documentPaths[doc] = path;
            m_modifiedFlags[doc] = false;
            
            addToRecent(path);
            
            // Phase P.2.8: Add to NotebookLibrary for launcher
            NotebookLibrary::instance()->addToRecent(path);
            
            emit documentLoaded(doc);
            return doc;
        }
    }
    
    qWarning() << "DocumentManager::loadDocument: Unsupported file format:" << suffix;
    return nullptr;
}

bool DocumentManager::saveDocument(Document* doc)
{
    if (!doc) {
        qWarning() << "DocumentManager::saveDocument: Null document";
        return false;
    }
    
    QString path = documentPath(doc);
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::saveDocument: Document has no path, use saveDocumentAs";
        return false;
    }
    
    return doSave(doc, path);
}

bool DocumentManager::saveDocumentAs(Document* doc, const QString& path)
{
    if (!doc) {
        qWarning() << "DocumentManager::saveDocumentAs: Null document";
        return false;
    }
    
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::saveDocumentAs: Empty path";
        return false;
    }
    
    if (doSave(doc, path)) {
        m_documentPaths[doc] = path;
        return true;
    }
    
    return false;
}

void DocumentManager::closeDocument(Document* doc)
{
    if (!doc) {
        return;
    }
    
    int index = m_documents.indexOf(doc);
    if (index < 0) {
        qWarning() << "DocumentManager::closeDocument: Document not found";
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager::closeDocument: Closing document" << doc 
             << "remaining=" << (m_documents.size() - 1);
#endif
    
    // Emit signal before deletion so receivers can clean up
    // Phase P.2.8: MainWindow should connect to this signal to save thumbnail
    // via NotebookLibrary::instance()->saveThumbnail(path, thumbnail)
    emit documentClosed(doc);
    
    // ========== TEMP BUNDLE CLEANUP ==========
    // If document was using a temp bundle and user didn't save,
    // clean up the temp directory to prevent storage space leak.
    // Note: If user saved to a permanent location, cleanupTempBundle()
    // was already called in doSave(), so this is a no-op.
    cleanupTempBundle(doc);
    // ==========================================
    
    // Remove from collections
    m_documents.removeAt(index);
    m_documentPaths.remove(doc);
    m_modifiedFlags.remove(doc);
    // Note: m_tempBundlePaths already cleaned by cleanupTempBundle()
    
    // Phase C.0.4: Clean up orphaned assets before deletion
    // This deletes image files that are no longer referenced by any object.
    doc->cleanupOrphanedAssets();
    
    // Delete the document
    delete doc;
}

// ============================================================================
// Document Access
// ============================================================================

Document* DocumentManager::documentAt(int index) const
{
    if (index < 0 || index >= m_documents.size()) {
        return nullptr;
    }
    return m_documents.at(index);
}

int DocumentManager::documentCount() const
{
    return m_documents.size();
}

int DocumentManager::indexOf(Document* doc) const
{
    return m_documents.indexOf(doc);
}

// ============================================================================
// Document State
// ============================================================================

bool DocumentManager::hasUnsavedChanges(Document* doc) const
{
    if (!doc) {
        return false;
    }
    return m_modifiedFlags.value(doc, false) || doc->modified;
}

QString DocumentManager::documentPath(Document* doc) const
{
    if (!doc) {
        return QString();
    }
    return m_documentPaths.value(doc);
}

void DocumentManager::setDocumentPath(Document* doc, const QString& path)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    QString oldPath = m_documentPaths.value(doc);
    if (oldPath != path) {
        m_documentPaths[doc] = path;
        
        // Also update the document's internal bundle path if it's a .snb bundle
        if (path.endsWith(".snb", Qt::CaseInsensitive) || QFileInfo(path).isDir()) {
            doc->setBundlePath(path);
        }
    }
}

void DocumentManager::markModified(Document* doc)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    bool wasModified = m_modifiedFlags.value(doc, false);
    m_modifiedFlags[doc] = true;
    doc->markModified();
    
    if (!wasModified) {
        emit documentModified(doc);
    }
}

void DocumentManager::clearModified(Document* doc)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    m_modifiedFlags[doc] = false;
    doc->clearModified();
}

// ============================================================================
// Recent Documents
// ============================================================================

QStringList DocumentManager::recentDocuments() const
{
    return m_recentPaths;
}

void DocumentManager::addToRecent(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    
    // Remove existing entry (if any) to move it to front
    m_recentPaths.removeAll(path);
    
    // Add to front
    m_recentPaths.prepend(path);
    
    // Trim to max size
    while (m_recentPaths.size() > MAX_RECENT) {
        m_recentPaths.removeLast();
    }
    
    saveRecentToSettings();
    emit recentDocumentsChanged();
}

void DocumentManager::clearRecentDocuments()
{
    if (m_recentPaths.isEmpty()) {
        return;
    }
    
    m_recentPaths.clear();
    saveRecentToSettings();
    emit recentDocumentsChanged();
}

void DocumentManager::removeFromRecent(const QString& path)
{
    if (m_recentPaths.removeAll(path) > 0) {
        saveRecentToSettings();
        emit recentDocumentsChanged();
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void DocumentManager::loadRecentFromSettings()
{
    QSettings settings;
    m_recentPaths = settings.value(SETTINGS_RECENT_KEY).toStringList();
    
    // Validate paths - remove non-existent files
    QStringList validPaths;
    for (const QString& path : m_recentPaths) {
        if (QFileInfo::exists(path)) {
            validPaths.append(path);
        }
    }
    
    if (validPaths.size() != m_recentPaths.size()) {
        m_recentPaths = validPaths;
        saveRecentToSettings();
    }
}

void DocumentManager::saveRecentToSettings()
{
    QSettings settings;
    settings.setValue(SETTINGS_RECENT_KEY, m_recentPaths);
}

bool DocumentManager::doSave(Document* doc, const QString& path)
{
    if (!doc || path.isEmpty()) {
        return false;
    }
    
    // ========== UNIFIED BUNDLE FORMAT (.snb) - Phase O1.7.6 ==========
    // ALL documents (paged and edgeless) now use the bundle format.
    // This enables:
    // - Lazy loading for paged mode (pages loaded on demand)
    // - Asset folder for images/objects
    // - Consistent O(1) save/load for large documents
    
    QString bundlePath = path;
    // Ensure .snb extension
    if (!bundlePath.endsWith(".snb", Qt::CaseInsensitive)) {
        bundlePath += ".snb";
    }
    
    if (!doc->saveBundle(bundlePath)) {
        qWarning() << "DocumentManager::doSave: Failed to save bundle:" << bundlePath;
        return false;
    }
    
    // ========== TEMP BUNDLE CLEANUP ==========
    // If this was a temp bundle and now saving to a different location,
    // clean up the temp directory. Note: saveBundle() already updated
    // m_bundlePath to the new location, so no need to call setBundlePath().
    QString tempPath = m_tempBundlePaths.value(doc);
    if (!tempPath.isEmpty() && tempPath != bundlePath) {
        cleanupTempBundle(doc);
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Moved from temp bundle to" << bundlePath;
#endif
    }
    // ==========================================
    
    // Update state
    clearModified(doc);
    addToRecent(bundlePath);
    
    // Phase P.2.8: Update NotebookLibrary for launcher
    NotebookLibrary::instance()->addToRecent(bundlePath);
    
    emit documentSaved(doc);
    return true;
}

// ============================================================================
// Temp Bundle Management (Edgeless/Paged Auto-save)
// ============================================================================

QString DocumentManager::createTempBundlePath(Document* doc)
{
    if (!doc) {
        return QString();
    }
    
    // Create temp directory path for unsaved documents:
    // QStandardPaths::TempLocation + prefix + uuid
    QString tempBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString uuid = doc->id.left(8);  // Use first 8 chars of doc ID for uniqueness
    QString prefix = doc->isEdgeless() ? TEMP_EDGELESS_PREFIX : TEMP_PAGED_PREFIX;
    QString tempPath = tempBase + "/" + prefix + uuid + ".snb";
    
    // Create the directory
    QDir dir;
    if (!dir.mkpath(tempPath)) {
        qWarning() << "DocumentManager: Failed to create temp bundle directory:" << tempPath;
        return QString();
    }
    
    // Create subdirectories based on document type
    bool subdirOk = false;
    if (doc->isEdgeless()) {
        // Edgeless needs tiles subdirectory
        subdirOk = dir.mkpath(tempPath + "/tiles");
        if (!subdirOk) {
            qWarning() << "DocumentManager: Failed to create tiles subdirectory:" << tempPath + "/tiles";
        }
    } else {
        // Paged needs pages subdirectory
        subdirOk = dir.mkpath(tempPath + "/pages");
        if (!subdirOk) {
            qWarning() << "DocumentManager: Failed to create pages subdirectory:" << tempPath + "/pages";
        }
    }
    
    // If subdirectory creation failed, clean up the parent directory to avoid disk space leak
    if (!subdirOk) {
        QDir(tempPath).removeRecursively();
        return QString();
    }
    
    return tempPath;
}

QString DocumentManager::createAutoSavePath(Document* doc)
{
    if (!doc) {
        return QString();
    }
    
    // Create path in app's permanent storage (survives system cleanup)
    // This is used for Android auto-save where we want documents to persist
    QString notebooksDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
    QDir().mkpath(notebooksDir);
    
    // Use document name if available, otherwise generate from UUID
    QString baseName = doc->name;
    if (baseName.isEmpty()) {
        baseName = doc->isEdgeless() ? tr("Untitled Canvas") : tr("Untitled");
    }
    
    // Sanitize filename: remove/replace characters invalid for filenames
    // Invalid chars on various platforms: / \ : * ? " < > |
    baseName.replace(QRegularExpression(R"([/\\:*?"<>|])"), "_");
    baseName = baseName.trimmed();
    if (baseName.isEmpty()) {
        baseName = doc->isEdgeless() ? tr("Untitled Canvas") : tr("Untitled");
    }
    
    // Ensure unique filename
    QString filePath = notebooksDir + "/" + baseName + ".snb";
    if (QDir(filePath).exists()) {
        // File exists - append UUID suffix to make unique
        QString uuid = doc->id.left(8);
        filePath = notebooksDir + "/" + baseName + "_" + uuid + ".snb";
        
        // If UUID-suffixed path also exists (from a previous crashed session),
        // keep appending more UUID characters until we find a unique name
        int uuidLen = 8;
        while (QDir(filePath).exists() && uuidLen < doc->id.length()) {
            uuidLen += 4;
            uuid = doc->id.left(qMin(uuidLen, doc->id.length()));
            filePath = notebooksDir + "/" + baseName + "_" + uuid + ".snb";
        }
        
        // Final fallback: append timestamp if UUID is exhausted
        if (QDir(filePath).exists()) {
            QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
            filePath = notebooksDir + "/" + baseName + "_" + timestamp + ".snb";
        }
    }
    
    return filePath;
}

bool DocumentManager::isUsingTempBundle(Document* doc) const
{
    if (!doc) {
        return false;
    }
    return m_tempBundlePaths.contains(doc) && !m_tempBundlePaths.value(doc).isEmpty();
}

QString DocumentManager::tempBundlePath(Document* doc) const
{
    if (!doc) {
        return QString();
    }
    return m_tempBundlePaths.value(doc);
}

void DocumentManager::cleanupTempBundle(Document* doc)
{
    if (!doc) {
        return;
    }
    
    QString tempPath = m_tempBundlePaths.value(doc);
    if (tempPath.isEmpty()) {
        return;
    }
    
    // Remove from tracking
    m_tempBundlePaths.remove(doc);
    
    // Delete the temp directory recursively
    QDir tempDir(tempPath);
    if (tempDir.exists()) {
        if (tempDir.removeRecursively()) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Cleaned up temp bundle:" << tempPath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to clean up temp bundle:" << tempPath;
        }
    }
}

int DocumentManager::autoSaveModifiedDocuments()
{
    int savedCount = 0;
    
    for (Document* doc : m_documents) {
        if (!doc) continue;
        
        // Check if document has unsaved changes
        // IMPORTANT: Use hasUnsavedChanges() which checks both m_modifiedFlags
        // AND doc->modified. User edits often set doc->modified directly
        // (e.g., when writing strokes), not through DocumentManager::markModified()
        if (!hasUnsavedChanges(doc)) {
            continue;  // No changes to save
        }
        
        QString existingPath = m_documentPaths.value(doc);
        bool isUsingTemp = isUsingTempBundle(doc);
        bool hasPermPath = !existingPath.isEmpty() && !isUsingTemp;
        
        QString savePath;
        bool isNewDocument = false;
        
        if (hasPermPath) {
            // Document has a permanent save path - save in-place
            savePath = existingPath;
        } else {
            // New document - create auto-save path in app storage
            savePath = createAutoSavePath(doc);
            isNewDocument = true;
            
            if (savePath.isEmpty()) {
                qWarning() << "DocumentManager: Failed to create auto-save path for" << doc->name;
                continue;
            }
        }
        
        // Perform the save
        if (doc->saveBundle(savePath)) {
            // Update document path if this was a new save location
            if (isNewDocument) {
                m_documentPaths[doc] = savePath;
                
                // Clean up temp bundle if it existed (edgeless docs)
                cleanupTempBundle(doc);
            }
            
            // Clear modified flag
            clearModified(doc);
            
            // Add to NotebookLibrary so it appears in Launcher
            NotebookLibrary::instance()->addToRecent(savePath);
            
            savedCount++;
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Auto-saved" << doc->name << "to" << savePath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to auto-save" << doc->name << "to" << savePath;
        }
    }
    
    // Flush NotebookLibrary to disk immediately
    // Critical: without this, the library changes won't persist if app is killed
    // Always save, even if savedCount is 0, to flush any pending library changes
    // (e.g., documents opened during the session)
    NotebookLibrary::instance()->save();
    
    return savedCount;
}
