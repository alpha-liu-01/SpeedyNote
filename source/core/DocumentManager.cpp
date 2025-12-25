// ============================================================================
// DocumentManager Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.1)
// ============================================================================

#include "DocumentManager.h"
#include "Document.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// Settings key for recent documents persistence
const QString DocumentManager::SETTINGS_RECENT_KEY = QStringLiteral("RecentDocuments");

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
    // Delete all owned documents
    for (Document* doc : m_documents) {
        delete doc;
    }
    m_documents.clear();
}

// ============================================================================
// Document Lifecycle
// ============================================================================

Document* DocumentManager::createDocument(const QString& name)
{
    // Create a new document with default settings
    auto docPtr = Document::createNew(name.isEmpty() ? QStringLiteral("Untitled") : name);
    
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
    
    // Handle .snx and .json files - load from JSON
    if (suffix == "snx" || suffix == "json") {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "DocumentManager::loadDocument: Cannot open file:" << path;
            return nullptr;
        }
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "DocumentManager::loadDocument: JSON parse error:" << parseError.errorString();
            return nullptr;
        }
        
        if (!jsonDoc.isObject()) {
            qWarning() << "DocumentManager::loadDocument: Invalid JSON format (not an object)";
            return nullptr;
        }
        
        auto docPtr = Document::fromFullJson(jsonDoc.object());
        if (!docPtr) {
            qWarning() << "DocumentManager::loadDocument: Failed to parse document from JSON";
            return nullptr;
        }
        
        Document* doc = docPtr.release();
        m_documents.append(doc);
        m_documentPaths[doc] = path;
        m_modifiedFlags[doc] = false;
        
        // Try to load PDF if referenced
        if (doc->hasPdfReference() && !doc->isPdfLoaded()) {
            if (!doc->loadPdf(doc->pdfPath())) {
                qWarning() << "DocumentManager::loadDocument: Failed to load referenced PDF:" 
                           << doc->pdfPath();
                // Don't fail - document can still be used, PDF can be relinked
            }
        }
        
        addToRecent(path);
        emit documentLoaded(doc);
        return doc;
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
    
    // Emit signal before deletion so receivers can clean up
    emit documentClosed(doc);
    
    // Remove from collections
    m_documents.removeAt(index);
    m_documentPaths.remove(doc);
    m_modifiedFlags.remove(doc);
    
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
    
    // Serialize document to JSON
    QJsonObject jsonObj = doc->toFullJson();
    QJsonDocument jsonDoc(jsonObj);
    
    // Write to file
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DocumentManager::doSave: Cannot open file for writing:" << path;
        return false;
    }
    
    // Use indented format for readability (can be changed to Compact for smaller files)
    QByteArray data = jsonDoc.toJson(QJsonDocument::Indented);
    
    qint64 bytesWritten = file.write(data);
    file.close();
    
    if (bytesWritten != data.size()) {
        qWarning() << "DocumentManager::doSave: Write failed, expected" << data.size() 
                   << "bytes, wrote" << bytesWritten;
        return false;
    }
    
    // Update state
    clearModified(doc);
    addToRecent(path);
    
    emit documentSaved(doc);
    return true;
}
