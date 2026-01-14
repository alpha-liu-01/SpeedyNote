#include "RecentNotebooksManager.h"
#include "SpnPackageManager.h"
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QApplication>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDataStream>
#include <QDateTime>
#include <QCryptographicHash>
#include <QTimer>
#include <QPointer>

// Initialize static member
RecentNotebooksManager* RecentNotebooksManager::instance = nullptr;

RecentNotebooksManager::RecentNotebooksManager(QObject *parent)
    : QObject(parent), settings("SpeedyNote", "App") {
    QDir().mkpath(getCoverImageDir()); // Ensure cover image directory exists
    loadRecentNotebooks();
    loadStarredNotebooks();
}

RecentNotebooksManager* RecentNotebooksManager::getInstance(QObject *parent)
{
    if (!instance) {
        instance = new RecentNotebooksManager(parent);
    }
    return instance;
}

void RecentNotebooksManager::addRecentNotebook(const QString& folderPath, const QString& displayPathOverride) {
    if (folderPath.isEmpty()) return;

    // Use display path to avoid duplicates between .spn files and temp folders
    QString displayPath = displayPathOverride.isEmpty() ? folderPath : displayPathOverride;
    
    // Normalize path separators to prevent duplicates
    displayPath = QDir::toNativeSeparators(QFileInfo(displayPath).absoluteFilePath());
    
    // Remove entries that match the display path or are related to the same notebook ID
    QStringList toRemove;
    QString currentNotebookId = getNotebookIdFromPath(displayPath);
    
    for (const QString& existingPath : recentNotebookPaths) {
        bool shouldRemove = false;
        
        // Normalize existing path for comparison
        QString normalizedExistingPath = QDir::toNativeSeparators(QFileInfo(existingPath).absoluteFilePath());
        
        if (normalizedExistingPath == displayPath) {
            shouldRemove = true;
        }
        
        // Also check notebook ID if available
        if (!shouldRemove && !currentNotebookId.isEmpty()) {
            QString existingNotebookId = getNotebookIdFromPath(existingPath);
            if (!existingNotebookId.isEmpty() && existingNotebookId == currentNotebookId) {
                shouldRemove = true;
            }
        }
        
        if (shouldRemove) {
            toRemove.append(existingPath);
        }
    }
    
    for (const QString& pathToRemove : toRemove) {
        recentNotebookPaths.removeAll(pathToRemove);
    }
    
    recentNotebookPaths.prepend(displayPath);

    // Trim the list if it exceeds the maximum size
    while (recentNotebookPaths.size() > MAX_RECENT_NOTEBOOKS) {
        recentNotebookPaths.removeLast();
    }

    // Invalidate caches for the added notebook (file might have changed)
    pdfPathCache.remove(displayPath);
    displayNameCache.remove(displayPath);
    
    // Generate thumbnail once - no delayed generation to avoid memory leaks
    generateAndSaveCoverPreview(displayPath);
    saveRecentNotebooks();
}

QStringList RecentNotebooksManager::getRecentNotebooks() const {
    return recentNotebookPaths;
}

void RecentNotebooksManager::removeRecentNotebook(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    
    // ✅ Normalize path for comparison
    QString normalizedPath = QDir::toNativeSeparators(QFileInfo(folderPath).absoluteFilePath());
    
    // Remove all matching entries (there might be multiple due to different path formats)
    if (recentNotebookPaths.removeAll(normalizedPath) > 0) {
        // Invalidate caches for the removed notebook
        pdfPathCache.remove(normalizedPath);
        displayNameCache.remove(normalizedPath);
        saveRecentNotebooks();
    }
    
    // Also try to remove the original path in case normalization changed it
    if (recentNotebookPaths.removeAll(folderPath) > 0) {
        saveRecentNotebooks();
    }
}

void RecentNotebooksManager::loadRecentNotebooks() {
    QStringList rawPaths = settings.value("recentNotebooks").toStringList();
    
    // ✅ Normalize all loaded paths to prevent separator inconsistencies
    recentNotebookPaths.clear();
    for (const QString& path : rawPaths) {
        if (!path.isEmpty()) {
            QString normalizedPath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
            recentNotebookPaths.append(normalizedPath);
        }
    }
}

void RecentNotebooksManager::saveRecentNotebooks() {
    settings.setValue("recentNotebooks", recentNotebookPaths);
}

QString RecentNotebooksManager::sanitizeFolderName(const QString& folderPath) const {
    // ✅ Create a more unique cache key to prevent collisions
    QString baseName = QFileInfo(folderPath).baseName();
    QString absolutePath = QFileInfo(folderPath).absoluteFilePath();
    
    // Create a hash of the full path to ensure uniqueness
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(absolutePath.toUtf8());
    QString pathHash = hash.result().toHex().left(8); // First 8 chars of hash
    
    return QString("%1_%2").arg(baseName.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_")).arg(pathHash);
}

QString RecentNotebooksManager::getCoverImageDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/RecentCovers/";
}

void RecentNotebooksManager::generateAndSaveCoverPreview(const QString& folderPath) {
    // Early validation - don't proceed with invalid paths
    if (folderPath.isEmpty()) {
        return;
    }
    
    // For .spn files, verify the file exists and is readable
    if (folderPath.endsWith(".spn", Qt::CaseInsensitive)) {
        QFileInfo fileInfo(folderPath);
        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            return;
        }
    } else {
        // For folders, verify the folder exists
        QFileInfo fileInfo(folderPath);
        if (!fileInfo.exists() || !fileInfo.isDir()) {
            return;
        }
    }
    
    QString coverDir = getCoverImageDir();
    QString coverFileName = sanitizeFolderName(folderPath) + "_cover.png";
    QString coverFilePath = coverDir + coverFileName;

    QImage coverImage(400, 300, QImage::Format_ARGB32_Premultiplied);
    coverImage.fill(Qt::white); // Default background
    QPainter painter(&coverImage);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Generate cover from saved files
    {
        QString notebookIdStr;
        QString actualFolderPath = folderPath;
        bool needsCleanup = false;
        
        // ✅ Handle .spn packages using SpnPackageManager (correct format!)
        if (folderPath.endsWith(".spn", Qt::CaseInsensitive)) {
            // Read notebook ID directly from .spn without full extraction
            SpnPackageManager::SpnMetadata metadata = SpnPackageManager::readMetadataFromSpn(folderPath);
            if (metadata.isValid) {
                notebookIdStr = metadata.notebookId;
            }
            
            // For thumbnail image, we need to extract the first page image
            // Try to extract page 1 image directly from .spn
            if (!notebookIdStr.isEmpty()) {
                // Try page 1 (pages start at 1, not 0!)
                QString page1FileName = QString("%1_00001.png").arg(notebookIdStr);
                QByteArray page1Data = SpnPackageManager::extractFileFromSpn(folderPath, page1FileName);
                
                if (!page1Data.isEmpty()) {
                    QImage pageImage;
                    if (pageImage.loadFromData(page1Data)) {
                        painter.drawImage(coverImage.rect(), pageImage, pageImage.rect());
                        painter.end();
                        coverImage.save(coverFilePath, "PNG");
                        emit thumbnailUpdated(folderPath, coverFilePath);
                        return;
                    }
                }
            }
            
            // If direct extraction failed, fall back to full extraction
            actualFolderPath = SpnPackageManager::extractSpnToTemp(folderPath);
            if (!actualFolderPath.isEmpty()) {
                needsCleanup = true;
            }
        }
        
        // ✅ Try to get notebook ID from folder if not already obtained
        if (notebookIdStr.isEmpty()) {
            // Try JSON metadata first
            QString jsonFile = actualFolderPath + "/.speedynote_metadata.json";
            if (QFile::exists(jsonFile)) {
                QFile file(jsonFile);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QByteArray data = file.readAll();
                    file.close();
                    
                    QJsonParseError error;
                    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
                    
                    if (error.error == QJsonParseError::NoError) {
                        QJsonObject obj = doc.object();
                        notebookIdStr = obj["notebook_id"].toString();
                    }
                }
            }
            
            // Fallback to old system for backwards compatibility
            if (notebookIdStr.isEmpty()) {
                QFile idFile(actualFolderPath + "/.notebook_id.txt");
                if (idFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&idFile);
                    notebookIdStr = in.readLine().trimmed();
                    idFile.close();
                }
            }
        }

        // ✅ Try to load page image - pages start at 1, not 0!
        QString firstPagePath, secondPagePath;
        if (!notebookIdStr.isEmpty()) {
            // Try page 1 first (correct numbering)
            firstPagePath = actualFolderPath + QString("/%1_00001.png").arg(notebookIdStr);
            // Also try page 0 for backwards compatibility with any old notebooks
            secondPagePath = actualFolderPath + QString("/%1_00000.png").arg(notebookIdStr);
        }

        QImage pageImage;
        if (!firstPagePath.isEmpty() && QFile::exists(firstPagePath)) {
            pageImage.load(firstPagePath);
        } else if (!secondPagePath.isEmpty() && QFile::exists(secondPagePath)) {
            pageImage.load(secondPagePath);
        }

        if (!pageImage.isNull()) {
            painter.drawImage(coverImage.rect(), pageImage, pageImage.rect());
        } else {
            // If no image found, draw a placeholder
            painter.fillRect(coverImage.rect(), Qt::darkGray);
            painter.setPen(Qt::white);
            painter.drawText(coverImage.rect(), Qt::AlignCenter, tr("No Preview Available"));
        }
        
        // ✅ Clean up temporary directory if we extracted an .spn file
        if (needsCleanup && !actualFolderPath.isEmpty()) {
            SpnPackageManager::cleanupTempDir(actualFolderPath);
        }
    }
    
    painter.end();
    coverImage.save(coverFilePath, "PNG");
    
    // ✅ Emit signal to notify that thumbnail was updated
    emit thumbnailUpdated(folderPath, coverFilePath);
}

QString RecentNotebooksManager::getCoverImagePathForNotebook(const QString& folderPath) const {
    QString coverDir = getCoverImageDir();
    QString coverFileName = sanitizeFolderName(folderPath) + "_cover.png";
    QString coverFilePath = coverDir + coverFileName;
    if (QFile::exists(coverFilePath)) {
        return coverFilePath;
    }
    return ""; // Return empty if no cover exists
}

QString RecentNotebooksManager::getPdfPathFromNotebook(const QString& folderPath) const {
    // Check cache first to avoid expensive .spn extraction
    if (pdfPathCache.contains(folderPath)) {
        return pdfPathCache.value(folderPath);
    }
    
    QString pdfPath;
    
    // ✅ Handle .spn packages using SpnPackageManager (efficient, no temp extraction)
    if (folderPath.endsWith(".spn", Qt::CaseInsensitive)) {
        SpnPackageManager::SpnMetadata metadata = SpnPackageManager::readMetadataFromSpn(folderPath);
        if (metadata.isValid) {
            pdfPath = metadata.pdfPath;
        }
    } else {
        // Regular folder - read metadata directly
        QString jsonFile = folderPath + "/.speedynote_metadata.json";
        if (QFile::exists(jsonFile)) {
            QFile file(jsonFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QByteArray data = file.readAll();
                file.close();
                
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(data, &error);
                
                if (error.error == QJsonParseError::NoError) {
                    QJsonObject obj = doc.object();
                    pdfPath = obj["pdf_path"].toString();
                }
            }
        }
        
        // Fallback to old system
        if (pdfPath.isEmpty()) {
            QString metadataFile = folderPath + "/.pdf_path.txt";
            if (QFile::exists(metadataFile)) {
                QFile file(metadataFile);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&file);
                    pdfPath = in.readLine().trimmed();
                    file.close();
                }
            }
        }
    }
    
    // Cache the result to avoid repeated expensive operations
    pdfPathCache[folderPath] = pdfPath;
    
    return pdfPath;
}

QString RecentNotebooksManager::getNotebookIdFromPath(const QString& folderPath) const {
    QString notebookId;
    
    // ✅ Handle .spn packages using SpnPackageManager (efficient, no temp extraction)
    if (folderPath.endsWith(".spn", Qt::CaseInsensitive)) {
        SpnPackageManager::SpnMetadata metadata = SpnPackageManager::readMetadataFromSpn(folderPath);
        if (metadata.isValid) {
            return metadata.notebookId;
        }
    }
    
    // Regular folder - read metadata directly
    QString jsonFile = folderPath + "/.speedynote_metadata.json";
    if (QFile::exists(jsonFile)) {
        QFile file(jsonFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll();
            file.close();
            
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            
            if (error.error == QJsonParseError::NoError) {
                QJsonObject obj = doc.object();
                notebookId = obj["notebook_id"].toString();
            }
        }
    }
    
    // Fallback to old system
    if (notebookId.isEmpty()) {
        QFile idFile(folderPath + "/.notebook_id.txt");
        if (idFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&idFile);
            notebookId = in.readLine().trimmed();
            idFile.close();
        }
    }
    
    return notebookId;
}

QString RecentNotebooksManager::getNotebookDisplayName(const QString& folderPath) const {
    // Check cache first to avoid expensive operations
    if (displayNameCache.contains(folderPath)) {
        return displayNameCache.value(folderPath);
    }
    
    QString displayName;
    QString pdfPath = getPdfPathFromNotebook(folderPath);
    if (!pdfPath.isEmpty()) {
        displayName = QFileInfo(pdfPath).fileName();
    } else {
        // Fallback to folder name
        displayName = QFileInfo(folderPath).fileName();
    }
    
    // Cache the result
    displayNameCache[folderPath] = displayName;
    
    return displayName;
}

// Starred notebooks functionality
void RecentNotebooksManager::addStarred(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    
    // ✅ Normalize path to prevent duplicates
    QString normalizedPath = QDir::toNativeSeparators(QFileInfo(folderPath).absoluteFilePath());
    
    if (!starredNotebookPaths.contains(normalizedPath)) {
        starredNotebookPaths.append(normalizedPath);
        saveStarredNotebooks();
    }
}

void RecentNotebooksManager::removeStarred(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    
    // ✅ Normalize path for comparison
    QString normalizedPath = QDir::toNativeSeparators(QFileInfo(folderPath).absoluteFilePath());
    
    if (starredNotebookPaths.removeAll(normalizedPath) > 0) {
        saveStarredNotebooks();
    }
}

bool RecentNotebooksManager::isStarred(const QString& folderPath) const {
    if (folderPath.isEmpty()) return false;
    
    // ✅ Normalize path for comparison
    QString normalizedPath = QDir::toNativeSeparators(QFileInfo(folderPath).absoluteFilePath());
    
    return starredNotebookPaths.contains(normalizedPath);
}

QStringList RecentNotebooksManager::getStarredNotebooks() const {
    return starredNotebookPaths;
}

void RecentNotebooksManager::loadStarredNotebooks() {
    QStringList rawPaths = settings.value("starredNotebooks").toStringList();
    
    // ✅ Normalize all loaded paths to prevent separator inconsistencies
    starredNotebookPaths.clear();
    for (const QString& path : rawPaths) {
        if (!path.isEmpty()) {
            QString normalizedPath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
            starredNotebookPaths.append(normalizedPath);
        }
    }
}

void RecentNotebooksManager::saveStarredNotebooks() {
    settings.setValue("starredNotebooks", starredNotebookPaths);
} 