#include "NotebookLibrary.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <algorithm>

// Static singleton instance
NotebookLibrary* NotebookLibrary::s_instance = nullptr;

NotebookLibrary::NotebookLibrary(QObject* parent)
    : QObject(parent)
{
    // Set up paths for library data (persistent) and thumbnail cache (clearable)
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    
    m_libraryFilePath = dataPath + "/notebook_library.json";
    m_thumbnailCachePath = cachePath + "/thumbnails";
    
    // Ensure directories exist
    QDir().mkpath(dataPath);
    QDir().mkpath(m_thumbnailCachePath);
    
    // Set up debounced save timer
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &NotebookLibrary::save);
    
    // Load existing library data
    load();
}

NotebookLibrary* NotebookLibrary::instance()
{
    if (!s_instance) {
        s_instance = new NotebookLibrary();
    }
    return s_instance;
}

// === Recent Management ===

QList<NotebookInfo> NotebookLibrary::recentNotebooks() const
{
    // Return copy sorted by lastAccessed (newest first)
    QList<NotebookInfo> sorted = m_notebooks;
    std::sort(sorted.begin(), sorted.end(), [](const NotebookInfo& a, const NotebookInfo& b) {
        return a.lastAccessed > b.lastAccessed;
    });
    return sorted;
}

void NotebookLibrary::addToRecent(const QString& bundlePath)
{
    // Check if already exists
    NotebookInfo* existing = findNotebook(bundlePath);
    if (existing) {
        // Update lastAccessed and refresh metadata
        existing->lastAccessed = QDateTime::currentDateTime();
        
        // Re-read lastModified from document.json (not the folder!)
        // Folder mtime only changes when files are added/removed, not modified
        QFileInfo docJsonInfo(bundlePath + "/document.json");
        existing->lastModified = docJsonInfo.lastModified();
        
        markDirty();
        return;
    }
    
    // Read metadata from document.json
    QString docJsonPath = bundlePath + "/document.json";
    QFile docFile(docJsonPath);
    if (!docFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "NotebookLibrary: Cannot read" << docJsonPath;
        return;
    }
    
    QByteArray data = docFile.readAll();
    docFile.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "NotebookLibrary: JSON parse error in" << docJsonPath;
        return;
    }
    
    QJsonObject obj = doc.object();
    
    // Create new NotebookInfo
    NotebookInfo nb;
    nb.bundlePath = bundlePath;
    nb.name = obj["name"].toString();
    nb.documentId = obj["notebook_id"].toString();
    nb.lastAccessed = QDateTime::currentDateTime();
    
    // Get lastModified from document.json file (not the folder!)
    // Folder mtime only changes when files are added/removed, not modified
    QFileInfo docJsonInfo(bundlePath + "/document.json");
    nb.lastModified = docJsonInfo.lastModified();
    
    // Determine type from mode field
    QString mode = obj["mode"].toString();
    nb.isEdgeless = (mode == "edgeless");
    
    // Check if PDF-based
    QString pdfPath = obj["pdf_path"].toString();
    nb.isPdfBased = !pdfPath.isEmpty();
    if (nb.isPdfBased) {
        // Extract just the filename from the path
        QFileInfo pdfInfo(pdfPath);
        nb.pdfFileName = pdfInfo.fileName();
    }
    
    // Add to list
    m_notebooks.append(nb);
    markDirty();
}

void NotebookLibrary::removeFromRecent(const QString& bundlePath)
{
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks[i].bundlePath == bundlePath) {
            m_notebooks.removeAt(i);
            markDirty();
            return;
        }
    }
}

void NotebookLibrary::updateLastAccessed(const QString& bundlePath)
{
    NotebookInfo* nb = findNotebook(bundlePath);
    if (nb) {
        nb->lastAccessed = QDateTime::currentDateTime();
        markDirty();
    }
}

// === Starred Management ===

QList<NotebookInfo> NotebookLibrary::starredNotebooks() const
{
    // Collect starred notebooks grouped by folder
    // Order: folders in m_starredFolderOrder, then unfiled (empty folder)
    QList<NotebookInfo> result;
    
    // First, add notebooks in each folder (in folder order)
    for (const QString& folder : m_starredFolderOrder) {
        for (const auto& nb : m_notebooks) {
            if (nb.isStarred && nb.starredFolder == folder) {
                result.append(nb);
            }
        }
    }
    
    // Then, add unfiled starred notebooks (empty starredFolder)
    for (const auto& nb : m_notebooks) {
        if (nb.isStarred && nb.starredFolder.isEmpty()) {
            result.append(nb);
        }
    }
    
    return result;
}

void NotebookLibrary::setStarred(const QString& bundlePath, bool starred)
{
    NotebookInfo* nb = findNotebook(bundlePath);
    if (!nb) {
        return;
    }
    
    if (nb->isStarred == starred) {
        return; // No change
    }
    
    nb->isStarred = starred;
    
    // Clear folder assignment when unstarring
    if (!starred) {
        nb->starredFolder.clear();
    }
    
    markDirty();
}

void NotebookLibrary::setStarredFolder(const QString& bundlePath, const QString& folder)
{
    NotebookInfo* nb = findNotebook(bundlePath);
    if (!nb) {
        return;
    }
    
    // Auto-star if assigning to a folder
    if (!folder.isEmpty() && !nb->isStarred) {
        nb->isStarred = true;
    }
    
    // Validate folder exists (unless unfiled)
    if (!folder.isEmpty() && !m_starredFolderOrder.contains(folder)) {
        qWarning() << "NotebookLibrary: Folder" << folder << "does not exist";
        return;
    }
    
    nb->starredFolder = folder;
    markDirty();
}

QStringList NotebookLibrary::starredFolders() const
{
    return m_starredFolderOrder;
}

QStringList NotebookLibrary::recentFolders() const
{
    // Filter to only include folders that still exist
    QStringList result;
    for (const QString& folder : m_recentFolders) {
        if (m_starredFolderOrder.contains(folder)) {
            result.append(folder);
        }
    }
    return result;
}

void NotebookLibrary::recordFolderUsage(const QString& folder)
{
    if (folder.isEmpty()) {
        return;  // Don't track "Unfiled"
    }
    
    // Remove if already in list (will re-add at front)
    m_recentFolders.removeAll(folder);
    
    // Add to front
    m_recentFolders.prepend(folder);
    
    // Trim to max size
    while (m_recentFolders.size() > MAX_RECENT_FOLDERS) {
        m_recentFolders.removeLast();
    }
    
    // Save (no need to emit libraryChanged - UI doesn't depend on this)
    scheduleSave();
}

// -----------------------------------------------------------------------------
// Bulk Operations (L-007)
// -----------------------------------------------------------------------------

void NotebookLibrary::starNotebooks(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty()) {
        return;
    }
    
    bool anyChanged = false;
    
    for (const QString& path : bundlePaths) {
        NotebookInfo* nb = findNotebook(path);
        if (nb && !nb->isStarred) {
            nb->isStarred = true;
            anyChanged = true;
        }
    }
    
    if (anyChanged) {
        markDirty();
    }
}

void NotebookLibrary::unstarNotebooks(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty()) {
        return;
    }
    
    bool anyChanged = false;
    
    for (const QString& path : bundlePaths) {
        NotebookInfo* nb = findNotebook(path);
        if (nb && nb->isStarred) {
            nb->isStarred = false;
            nb->starredFolder.clear();  // Clear folder assignment when unstarring
            anyChanged = true;
        }
    }
    
    if (anyChanged) {
        markDirty();
    }
}

void NotebookLibrary::moveNotebooksToFolder(const QStringList& bundlePaths, const QString& folder)
{
    if (bundlePaths.isEmpty()) {
        return;
    }
    
    // Validate folder exists (unless unfiled)
    if (!folder.isEmpty() && !m_starredFolderOrder.contains(folder)) {
        qWarning() << "NotebookLibrary: Folder" << folder << "does not exist";
        return;
    }
    
    bool anyChanged = false;
    
    for (const QString& path : bundlePaths) {
        NotebookInfo* nb = findNotebook(path);
        if (!nb) {
            continue;
        }
        
        // Auto-star if assigning to a folder
        if (!folder.isEmpty() && !nb->isStarred) {
            nb->isStarred = true;
            anyChanged = true;
        }
        
        if (nb->starredFolder != folder) {
            nb->starredFolder = folder;
            anyChanged = true;
        }
    }
    
    if (anyChanged) {
        // Record folder usage for recent folders tracking (L-008)
        if (!folder.isEmpty()) {
            recordFolderUsage(folder);
        }
        markDirty();
    }
}

void NotebookLibrary::removeNotebooksFromFolder(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty()) {
        return;
    }
    
    bool anyChanged = false;
    
    for (const QString& path : bundlePaths) {
        NotebookInfo* nb = findNotebook(path);
        if (nb && !nb->starredFolder.isEmpty()) {
            nb->starredFolder.clear();  // Move to unfiled
            anyChanged = true;
        }
    }
    
    if (anyChanged) {
        markDirty();
    }
}

void NotebookLibrary::createStarredFolder(const QString& name)
{
    if (name.isEmpty()) {
        qWarning() << "NotebookLibrary: Cannot create folder with empty name";
        return;
    }
    
    if (m_starredFolderOrder.contains(name)) {
        qWarning() << "NotebookLibrary: Folder" << name << "already exists";
        return;
    }
    
    m_starredFolderOrder.append(name);
    markDirty();
}

void NotebookLibrary::deleteStarredFolder(const QString& name)
{
    int index = m_starredFolderOrder.indexOf(name);
    if (index < 0) {
        return; // Folder doesn't exist
    }
    
    // Move all notebooks in this folder to unfiled
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks[i].starredFolder == name) {
            m_notebooks[i].starredFolder.clear();
        }
    }
    
    m_starredFolderOrder.removeAt(index);
    markDirty();
}

void NotebookLibrary::reorderStarredFolder(const QString& name, int newIndex)
{
    int currentIndex = m_starredFolderOrder.indexOf(name);
    if (currentIndex < 0) {
        return; // Folder doesn't exist
    }
    
    // Clamp newIndex to valid range
    newIndex = qBound(0, newIndex, m_starredFolderOrder.size() - 1);
    
    if (currentIndex == newIndex) {
        return; // No change
    }
    
    // Remove and reinsert at new position
    m_starredFolderOrder.removeAt(currentIndex);
    m_starredFolderOrder.insert(newIndex, name);
    markDirty();
}

// === Search ===

QList<NotebookInfo> NotebookLibrary::search(const QString& query) const
{
    if (query.isEmpty()) {
        return {};
    }
    
    QString lowerQuery = query.toLower();
    
    // Collect matches with scores: 2 = exact match, 1 = contains
    QList<QPair<int, NotebookInfo>> scored;
    
    for (const auto& nb : m_notebooks) {
        int score = 0;
        
        // Check display name
        QString lowerName = nb.displayName().toLower();
        if (lowerName == lowerQuery) {
            score = 2; // Exact match
        } else if (lowerName.contains(lowerQuery)) {
            score = 1; // Contains
        }
        
        // Check PDF filename (if PDF-based and not already exact match)
        if (nb.isPdfBased && !nb.pdfFileName.isEmpty() && score < 2) {
            QString lowerPdf = nb.pdfFileName.toLower();
            if (lowerPdf == lowerQuery) {
                score = 2; // Exact match
            } else if (lowerPdf.contains(lowerQuery) && score < 1) {
                score = 1; // Contains (only if name didn't already match)
            }
        }
        
        if (score > 0) {
            scored.append({score, nb});
        }
    }
    
    // Sort by score descending, then by lastAccessed descending
    std::sort(scored.begin(), scored.end(), 
        [](const QPair<int, NotebookInfo>& a, const QPair<int, NotebookInfo>& b) {
            if (a.first != b.first) {
                return a.first > b.first; // Higher score first
            }
            return a.second.lastAccessed > b.second.lastAccessed; // More recent first
        });
    
    // Extract results
    QList<NotebookInfo> results;
    results.reserve(scored.size());
    for (const auto& pair : scored) {
        results.append(pair.second);
    }
    
    return results;
}

// === Thumbnails ===

QString NotebookLibrary::thumbnailPathFor(const QString& bundlePath) const
{
    // Find the notebook to get its documentId
    const NotebookInfo* nb = findNotebook(bundlePath);
    if (!nb || nb->documentId.isEmpty()) {
        return QString();
    }
    
    QString cachePath = m_thumbnailCachePath + "/" + nb->documentId + ".png";
    
    // Only return path if file exists
    if (QFileInfo::exists(cachePath)) {
        return cachePath;
    }
    
    return QString();
}

void NotebookLibrary::saveThumbnail(const QString& bundlePath, const QPixmap& thumbnail)
{
    if (thumbnail.isNull()) {
        return;
    }
    
    // Find the notebook to get its documentId
    const NotebookInfo* nb = findNotebook(bundlePath);
    if (!nb || nb->documentId.isEmpty()) {
        qWarning() << "NotebookLibrary: Cannot save thumbnail - notebook not found or no ID";
        return;
    }
    
    // Ensure cache directory exists
    QDir().mkpath(m_thumbnailCachePath);
    
    QString cachePath = m_thumbnailCachePath + "/" + nb->documentId + ".png";
    
    if (!thumbnail.save(cachePath, "PNG")) {
        qWarning() << "NotebookLibrary: Failed to save thumbnail to" << cachePath;
        return;
    }
    
    // Emit signal for UI update
    emit thumbnailUpdated(bundlePath);
    
    // Check if cache cleanup is needed
    cleanupThumbnailCache();
}

void NotebookLibrary::invalidateThumbnail(const QString& bundlePath)
{
    // Find the notebook to get its documentId
    const NotebookInfo* nb = findNotebook(bundlePath);
    if (!nb || nb->documentId.isEmpty()) {
        return;
    }
    
    QString cachePath = m_thumbnailCachePath + "/" + nb->documentId + ".png";
    
    if (QFile::exists(cachePath)) {
        QFile::remove(cachePath);
        emit thumbnailUpdated(bundlePath);
    }
}

void NotebookLibrary::cleanupThumbnailCache()
{
    QDir cacheDir(m_thumbnailCachePath);
    if (!cacheDir.exists()) {
        return;
    }
    
    // Get all PNG files with their info
    QFileInfoList files = cacheDir.entryInfoList({"*.png"}, QDir::Files);
    
    // Calculate total size
    qint64 totalSize = 0;
    for (const auto& fileInfo : files) {
        totalSize += fileInfo.size();
    }
    
    // If under limit, no cleanup needed
    if (totalSize <= MAX_CACHE_SIZE_BYTES) {
        return;
    }
    
    // Sort by last modified time (oldest first for LRU eviction)
    std::sort(files.begin(), files.end(), 
        [](const QFileInfo& a, const QFileInfo& b) {
            return a.lastModified() < b.lastModified();
        });
    
    // Delete oldest files until under limit
    for (const auto& fileInfo : files) {
        if (totalSize <= MAX_CACHE_SIZE_BYTES) {
            break;
        }
        
        qint64 fileSize = fileInfo.size();
        if (QFile::remove(fileInfo.absoluteFilePath())) {
            totalSize -= fileSize;
            // CR-P.2: Removed qDebug for production code
        }
    }
}

// === Persistence ===

void NotebookLibrary::save()
{
    // Stop any pending save timer
    m_saveTimer.stop();
    
    // Build JSON structure
    QJsonObject root;
    root["version"] = LIBRARY_VERSION;
    
    // Serialize notebooks
    QJsonArray notebooksArray;
    for (const auto& nb : m_notebooks) {
        QJsonObject nbObj;
        nbObj["path"] = nb.bundlePath;
        nbObj["name"] = nb.name;
        nbObj["documentId"] = nb.documentId;
        nbObj["lastModified"] = nb.lastModified.toString(Qt::ISODate);
        nbObj["lastAccessed"] = nb.lastAccessed.toString(Qt::ISODate);
        nbObj["isStarred"] = nb.isStarred;
        nbObj["starredFolder"] = nb.starredFolder;
        nbObj["isPdfBased"] = nb.isPdfBased;
        nbObj["isEdgeless"] = nb.isEdgeless;
        nbObj["pdfFileName"] = nb.pdfFileName;
        notebooksArray.append(nbObj);
    }
    root["notebooks"] = notebooksArray;
    
    // Serialize starred folder order
    QJsonArray foldersArray;
    for (const auto& folder : m_starredFolderOrder) {
        foldersArray.append(folder);
    }
    root["starredFolders"] = foldersArray;
    
    // Serialize recent folders (L-008)
    QJsonArray recentFoldersArray;
    for (const auto& folder : m_recentFolders) {
        recentFoldersArray.append(folder);
    }
    root["recentFolders"] = recentFoldersArray;
    
    // Write to file
    QFile file(m_libraryFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    } else {
        qWarning() << "NotebookLibrary: Failed to save to" << m_libraryFilePath;
    }
}

void NotebookLibrary::load()
{
    m_notebooks.clear();
    m_starredFolderOrder.clear();
    m_recentFolders.clear();
    
    QFile file(m_libraryFilePath);
    if (!file.exists()) {
        return; // No library file yet, start fresh
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "NotebookLibrary: Failed to open" << m_libraryFilePath;
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "NotebookLibrary: JSON parse error:" << parseError.errorString();
        return;
    }
    
    QJsonObject root = doc.object();
    
    // Check version for future compatibility
    int version = root["version"].toInt(1);
    if (version > LIBRARY_VERSION) {
        qWarning() << "NotebookLibrary: File version" << version 
                   << "is newer than supported version" << LIBRARY_VERSION;
    }
    
    // Load starred folders first (order matters)
    QJsonArray foldersArray = root["starredFolders"].toArray();
    for (const auto& folderVal : foldersArray) {
        m_starredFolderOrder.append(folderVal.toString());
    }
    
    // Load recent folders (L-008)
    QJsonArray recentFoldersArray = root["recentFolders"].toArray();
    for (const auto& folderVal : recentFoldersArray) {
        QString folder = folderVal.toString();
        // Only add if folder still exists
        if (m_starredFolderOrder.contains(folder)) {
            m_recentFolders.append(folder);
        }
    }
    
    // Load notebooks, validating that paths still exist
    QJsonArray notebooksArray = root["notebooks"].toArray();
    int staleCount = 0;
    
    for (const auto& nbVal : notebooksArray) {
        QJsonObject nbObj = nbVal.toObject();
        QString path = nbObj["path"].toString();
        
        // Validate path exists (is a directory with .snb_marker or document.json)
        QFileInfo pathInfo(path);
        if (!pathInfo.exists() || !pathInfo.isDir()) {
            staleCount++;
            continue; // Skip stale entries
        }
        
        // Check for .snb_marker or document.json to confirm it's a valid bundle
        if (!QFileInfo::exists(path + "/.snb_marker") && 
            !QFileInfo::exists(path + "/document.json")) {
            staleCount++;
            continue; // Not a valid .snb bundle
        }
        
        NotebookInfo nb;
        nb.bundlePath = path;
        nb.name = nbObj["name"].toString();
        nb.documentId = nbObj["documentId"].toString();
        nb.lastModified = QDateTime::fromString(nbObj["lastModified"].toString(), Qt::ISODate);
        nb.lastAccessed = QDateTime::fromString(nbObj["lastAccessed"].toString(), Qt::ISODate);
        nb.isStarred = nbObj["isStarred"].toBool();
        nb.starredFolder = nbObj["starredFolder"].toString();
        nb.isPdfBased = nbObj["isPdfBased"].toBool();
        nb.isEdgeless = nbObj["isEdgeless"].toBool();
        nb.pdfFileName = nbObj["pdfFileName"].toString();
        
        m_notebooks.append(nb);
    }
    
    if (staleCount > 0) {
        #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "NotebookLibrary: Removed" << staleCount << "stale entries";
        #endif
        // Save to remove stale entries from disk
        scheduleSave();
    }
}

void NotebookLibrary::scheduleSave()
{
    // Restart the timer (debounce)
    m_saveTimer.start(SAVE_DEBOUNCE_MS);
}

void NotebookLibrary::markDirty()
{
    emit libraryChanged();
    scheduleSave();
}

// === Private Helpers ===

NotebookInfo* NotebookLibrary::findNotebook(const QString& bundlePath)
{
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks[i].bundlePath == bundlePath) {
            return &m_notebooks[i];
        }
    }
    return nullptr;
}

const NotebookInfo* NotebookLibrary::findNotebook(const QString& bundlePath) const
{
    for (const auto& nb : m_notebooks) {
        if (nb.bundlePath == bundlePath) {
            return &nb;
        }
    }
    return nullptr;
}

