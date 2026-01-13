#include "SpnPackageManager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QTemporaryDir>
#include <QDataStream>
#include <QBuffer>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QUuid>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QByteArray>
#include <QDataStream>
#include <QVersionNumber>
#include <QCoreApplication>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

const QString SpnPackageManager::SPN_EXTENSION = ".spn";
const QString SpnPackageManager::TEMP_PREFIX = "speedynote_";

bool SpnPackageManager::isSpnPackage(const QString &path)
{
    return path.endsWith(SPN_EXTENSION, Qt::CaseInsensitive) && QFile::exists(path) && QFileInfo(path).isFile();
}

QString SpnPackageManager::extractSpnToTemp(const QString &spnPath)
{
    if (!isSpnPackage(spnPath)) {
        qWarning() << "Not a valid .spn package:" << spnPath;
        return QString();
    }
    
    // Create unique temporary directory
    QString tempBaseName = TEMP_PREFIX + QFileInfo(spnPath).baseName() + "_" + 
                          QCryptographicHash::hash(spnPath.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + tempBaseName;
    
    // Clean up existing temp dir if it exists
    if (QDir(tempDir).exists()) {
        QDir(tempDir).removeRecursively();
    }
    
    // Create temp directory
    if (!QDir().mkpath(tempDir)) {
        qWarning() << "Failed to create temp directory:" << tempDir;
        return QString();
    }
    
    // Extract .spn file to temp directory
    if (!unpackSpnToDirectory(spnPath, tempDir)) {
        QDir(tempDir).removeRecursively();
        return QString();
    }
    
    // ✅ Create a lock file to mark this directory as "in use" by current session
    QString lockFilePath = tempDir + "/.inuse.lock";
    QFile lockFile(lockFilePath);
    if (lockFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&lockFile);
        out << QString::number(QCoreApplication::applicationPid()) << "\n";
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        lockFile.close();
    }
    
    return tempDir;
}

bool SpnPackageManager::updateSpnFromTemp(const QString &spnPath, const QString &tempDir)
{
    if (!QDir(tempDir).exists()) {
        qWarning() << "Temp directory doesn't exist:" << tempDir;
        return false;
    }
    
    return packDirectoryToSpn(tempDir, spnPath);
}

bool SpnPackageManager::convertFolderToSpn(const QString &folderPath, QString &spnPath)
{
    QFileInfo folderInfo(folderPath);
    if (!folderInfo.exists() || !folderInfo.isDir()) {
        return false;
    }
    
    // Create .spn package path
    QString baseName = folderInfo.fileName();
    QString parentDir = folderInfo.absolutePath();
    spnPath = parentDir + "/" + baseName + SPN_EXTENSION;
    
    // Check if .spn already exists
    if (QFile::exists(spnPath)) {
        qWarning() << "SPN package already exists:" << spnPath;
        return false;
    }
    
    // Pack folder into .spn file
    if (!packDirectoryToSpn(folderPath, spnPath)) {
        return false;
    }
    
    // Remove the old folder
    QDir oldDir(folderPath);
    if (!oldDir.removeRecursively()) {
        qWarning() << "Failed to remove old folder:" << folderPath;
        // Continue anyway, the .spn package is created
    }
    
    return true;
}

bool SpnPackageManager::convertFolderToSpnPath(const QString &folderPath, const QString &targetSpnPath)
{
    QFileInfo folderInfo(folderPath);
    if (!folderInfo.exists() || !folderInfo.isDir()) {
        return false;
    }
    
    // Check if target .spn already exists
    if (QFile::exists(targetSpnPath)) {
        qWarning() << "Target SPN package already exists:" << targetSpnPath;
        return false;
    }
    
    // Pack folder into .spn file at target path
    return packDirectoryToSpn(folderPath, targetSpnPath);
}

bool SpnPackageManager::createSpnPackage(const QString &spnPath, const QString &notebookName)
{
    if (QFile::exists(spnPath)) {
        return false; // Already exists
    }
    
    // Create a temporary directory with basic structure
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return false;
    }
    
    // Create basic .speedynote_metadata.json
    QJsonObject metadata = createSpnHeader(notebookName);
    QJsonDocument doc(metadata);
    
    QFile metaFile(tempDir.path() + "/.speedynote_metadata.json");
    if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(doc.toJson());
        metaFile.close();
    }
    
    // Pack the temp directory into .spn file
    return packDirectoryToSpn(tempDir.path(), spnPath);
}

bool SpnPackageManager::createSpnPackageWithBackground(const QString &spnPath, const QString &notebookName,
                                                       BackgroundStyle style, const QColor &color, int density)
{
    if (QFile::exists(spnPath)) {
        return false; // Already exists
    }
    
    // Create a temporary directory with basic structure
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return false;
    }
    
    // Create .speedynote_metadata.json with custom background settings
    QString styleStr = backgroundStyleToString(style);
    QString colorStr = color.isValid() ? color.name() : "#ffffff";
    QJsonObject metadata = createSpnHeader(notebookName, styleStr, colorStr, density);
    QJsonDocument doc(metadata);
    
    QFile metaFile(tempDir.path() + "/.speedynote_metadata.json");
    if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(doc.toJson());
        metaFile.close();
    }
    
    // Pack the temp directory into .spn file
    return packDirectoryToSpn(tempDir.path(), spnPath);
}

QString SpnPackageManager::getSpnDisplayName(const QString &spnPath)
{
    if (!isSpnPackage(spnPath)) {
        return QFileInfo(spnPath).fileName();
    }
    
    QString baseName = QFileInfo(spnPath).baseName(); // Remove .spn extension
    return baseName;
}

bool SpnPackageManager::isValidSpnPackage(const QString &spnPath)
{
    if (!isSpnPackage(spnPath)) {
        return false;
    }
    
    // Try to extract to temp and check for SpeedyNote metadata
    QString tempDir = extractSpnToTemp(spnPath);
    if (tempDir.isEmpty()) {
        return false;
    }
    
    // Check for SpeedyNote metadata
    QString metadataFile = tempDir + "/.speedynote_metadata.json";
    QString oldIdFile = tempDir + "/.notebook_id.txt";
    
    bool isValid = QFile::exists(metadataFile) || QFile::exists(oldIdFile);
    
    // Clean up temp directory
    cleanupTempDir(tempDir);
    
    return isValid;
}

QString SpnPackageManager::getSuggestedSpnName(const QString &pdfPath)
{
    QFileInfo pdfInfo(pdfPath);
    return pdfInfo.baseName() + SPN_EXTENSION;
}

QString SpnPackageManager::getTempDirForSpn(const QString &spnPath)
{
    QString tempBaseName = TEMP_PREFIX + QFileInfo(spnPath).baseName() + "_" + 
                          QCryptographicHash::hash(spnPath.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + tempBaseName;
}

void SpnPackageManager::cleanupTempDir(const QString &tempDir)
{
    if (!tempDir.isEmpty() && QDir(tempDir).exists()) {
        // ✅ Remove lock file first (if it exists)
        QString lockFilePath = tempDir + "/.inuse.lock";
        if (QFile::exists(lockFilePath)) {
            QFile::remove(lockFilePath);
        }
        
        QDir(tempDir).removeRecursively();
    }
}

bool SpnPackageManager::packDirectoryToSpn(const QString &dirPath, const QString &spnPath)
{
    QFile spnFile(spnPath);
    if (!spnFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to create .spn file:" << spnPath;
        return false;
    }
    
    QDataStream stream(&spnFile);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // Write header
    stream << QString("SPEEDYNOTE_PACKAGE");
    stream << quint32(1); // Version
    
    // Get all files in directory (including hidden files)
    QDir sourceDir(dirPath);
    QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    QStringList files;
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relativePath = sourceDir.relativeFilePath(filePath);
        files.append(relativePath);
    }
    
    // Write file count
    stream << quint32(files.size());
    
    // Write each file
    for (const QString &relativePath : files) {
        QString fullPath = sourceDir.absoluteFilePath(relativePath);
        QFile file(fullPath);
        
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to read file:" << fullPath;
            continue;
        }
        
        QByteArray fileData = file.readAll();
        file.close();
        
        // Write file path and data
        stream << relativePath;
        stream << quint64(fileData.size());
        stream.writeRawData(fileData.constData(), fileData.size());
    }
    
    spnFile.close();
    return true;
}

bool SpnPackageManager::unpackSpnToDirectory(const QString &spnPath, const QString &dirPath)
{
    QFile spnFile(spnPath);
    if (!spnFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open .spn file:" << spnPath;
        return false;
    }
    
    QDataStream stream(&spnFile);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // Read and verify header
    QString header;
    quint32 version;
    stream >> header >> version;
    
    if (header != "SPEEDYNOTE_PACKAGE" || version != 1) {
        qWarning() << "Invalid .spn file format:" << spnPath;
        return false;
    }
    
    // Read file count
    quint32 fileCount;
    stream >> fileCount;
    
    // Extract each file
    for (quint32 i = 0; i < fileCount; ++i) {
        QString relativePath;
        quint64 fileSize;
        stream >> relativePath >> fileSize;
        
        // Create directory structure if needed
        QString fullPath = dirPath + "/" + relativePath;
        QFileInfo fileInfo(fullPath);
        QDir().mkpath(fileInfo.absolutePath());
        
        // Read file data
        QByteArray fileData(fileSize, 0);
        stream.readRawData(fileData.data(), fileSize);
        
        // Write file
        QFile outFile(fullPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(fileData);
            outFile.close();
        } else {
            qWarning() << "Failed to write file:" << fullPath;
        }
    }
    
    spnFile.close();
    return true;
}

QJsonObject SpnPackageManager::createSpnHeader(const QString &notebookName,
                                               const QString &backgroundStyle,
                                               const QString &backgroundColor, 
                                               int backgroundDensity)
{
    QJsonObject metadata;
    metadata["notebook_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "");
    metadata["version"] = "1.0";
    metadata["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    metadata["last_modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!notebookName.isEmpty()) {
        metadata["name"] = notebookName;
    }
    
    // Use provided background settings instead of hardcoded defaults
    metadata["pdf_path"] = "";
    metadata["last_accessed_page"] = 0;
    metadata["background_style"] = backgroundStyle;
    metadata["background_color"] = backgroundColor;
    metadata["background_density"] = backgroundDensity;
    metadata["bookmarks"] = QJsonArray();
    
    return metadata;
}

QString SpnPackageManager::backgroundStyleToString(BackgroundStyle style)
{
    switch (style) {
        case BackgroundStyle::None: return "None";
        case BackgroundStyle::Grid: return "Grid";
        case BackgroundStyle::Lines: return "Lines";
        default: return "None";
    }
}

void SpnPackageManager::cleanupOrphanedTempDirs()
{
    // ✅ DISK CLEANUP: Remove all orphaned temp directories from previous sessions
    // This is called on app startup to clean up temp folders that weren't cleaned up
    // due to crashes, force-close, or other errors

    QString tempBasePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir tempDir(tempBasePath);

    // Find all directories starting with our temp prefix
    QStringList filters;
    filters << TEMP_PREFIX + "*";
    QFileInfoList tempDirs = tempDir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);

    int cleanedCount = 0;
    qint64 freedSpace = 0;

    for (const QFileInfo &dirInfo : tempDirs) {
        QString dirPath = dirInfo.absoluteFilePath();
        
        // ✅ Check for lock file - if it exists, verify the process is still running
        QString lockFilePath = dirPath + "/.inuse.lock";
        bool isInUse = false;
        
        if (QFile::exists(lockFilePath)) {
            // Read the PID from the lock file
            QFile lockFile(lockFilePath);
            if (lockFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&lockFile);
                QString pidStr = in.readLine().trimmed();
                lockFile.close();
                
                bool ok;
                qint64 pid = pidStr.toLongLong(&ok);
                
                if (ok && pid > 0) {
                    // Check if the process is still running
#ifdef Q_OS_WIN
                    // Windows: Try to open the process handle
                    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
                    if (process != NULL) {
                        DWORD exitCode;
                        if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE) {
                            isInUse = true; // Process is still running
                        }
                        CloseHandle(process);
                    }
#else
                    // Linux/Unix: Check if /proc/[pid] exists
                    QString procPath = QString("/proc/%1").arg(pid);
                    if (QDir(procPath).exists()) {
                        isInUse = true; // Process is still running
                    }
#endif
                }
            }
        }
        
        if (isInUse) {
            // Directory is actually in use by a running process, skip it
            continue;
        }

        // Calculate size before deletion (for logging)
        qint64 dirSize = 0;
        QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            dirSize += QFileInfo(it.filePath()).size();
        }

        // Try to remove the directory
        QDir dir(dirPath);
        if (dir.removeRecursively()) {
            cleanedCount++;
            freedSpace += dirSize;
        }
    }

    if (cleanedCount > 0) {
        // qDebug() << "Cleaned up" << cleanedCount << "orphaned temp directories, freed" 
                 // << (freedSpace / 1024.0 / 1024.0) << "MB";
    }
}

qint64 SpnPackageManager::getTempDirsTotalSize()
{
    // ✅ Calculate total size of all SpeedyNote temp directories
    // This can be used to show the user how much space is being used by cache

    QString tempBasePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir tempDir(tempBasePath);

    // Find all directories starting with our temp prefix
    QStringList filters;
    filters << TEMP_PREFIX + "*";
    QFileInfoList tempDirs = tempDir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);

    qint64 totalSize = 0;

    for (const QFileInfo &dirInfo : tempDirs) {
        QString dirPath = dirInfo.absoluteFilePath();

        // Calculate size of this directory
        QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            totalSize += QFileInfo(it.filePath()).size();
        }
    }

    return totalSize;
}

QString SpnPackageManager::readNotebookIdFromSpn(const QString &spnPath)
{
    if (!isSpnPackage(spnPath)) {
        return QString();
    }
    
    QFile spnFile(spnPath);
    if (!spnFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open .spn file for reading notebook ID:" << spnPath;
        return QString();
    }
    
    QDataStream stream(&spnFile);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // Read and verify header
    QString header;
    quint32 version;
    stream >> header >> version;
    
    // Check stream status after reads
    if (stream.status() != QDataStream::Ok) {
        spnFile.close();
        return QString();
    }
    
    if (header != "SPEEDYNOTE_PACKAGE" || version != 1) {
        spnFile.close();
        return QString();
    }
    
    // Read file count
    quint32 fileCount;
    stream >> fileCount;
    
    if (stream.status() != QDataStream::Ok) {
        spnFile.close();
        return QString();
    }
    
    // Sanity check: file count should be reasonable
    if (fileCount > 100000) {
        qWarning() << "Suspicious file count in .spn package:" << fileCount;
        spnFile.close();
        return QString();
    }
    
    // Search for .speedynote_metadata.json file
    for (quint32 i = 0; i < fileCount; ++i) {
        QString relativePath;
        quint64 fileSize;
        stream >> relativePath >> fileSize;
        
        // Check stream status after each read
        if (stream.status() != QDataStream::Ok) {
            spnFile.close();
            return QString();
        }
        
        // Sanity check: metadata file should be small (< 1MB)
        const quint64 MAX_METADATA_SIZE = 1024 * 1024; // 1MB
        
        // Check if this is the metadata file
        if (relativePath == ".speedynote_metadata.json") {
            if (fileSize > MAX_METADATA_SIZE) {
                qWarning() << "Metadata file too large:" << fileSize;
                spnFile.close();
                return QString();
            }
            
            // Read the metadata content
            QByteArray fileData(static_cast<qsizetype>(fileSize), 0);
            if (stream.readRawData(fileData.data(), fileSize) != static_cast<int>(fileSize)) {
                spnFile.close();
                return QString();
            }
            spnFile.close();
            
            // Parse JSON to extract notebook_id
            QJsonDocument doc = QJsonDocument::fromJson(fileData);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                return obj["notebook_id"].toString();
            }
            return QString();
        } else {
            // Skip this file's data - check for potential overflow
            qint64 currentPos = spnFile.pos();
            qint64 newPos = currentPos + static_cast<qint64>(fileSize);
            
            // Sanity check: new position shouldn't exceed file size
            if (newPos < currentPos || newPos > spnFile.size()) {
                qWarning() << "Invalid file size in .spn package, potential corruption";
                spnFile.close();
                return QString();
            }
            
            if (!spnFile.seek(newPos)) {
                spnFile.close();
                return QString();
            }
        }
    }
    
    spnFile.close();
    return QString();
}

SpnPackageManager::SpnMetadata SpnPackageManager::readMetadataFromSpn(const QString &spnPath)
{
    SpnMetadata metadata;
    
    QByteArray metadataContent = extractFileFromSpn(spnPath, ".speedynote_metadata.json");
    if (metadataContent.isEmpty()) {
        return metadata;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(metadataContent);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        metadata.notebookId = obj["notebook_id"].toString();
        metadata.pdfPath = obj["pdf_path"].toString();
        metadata.isValid = !metadata.notebookId.isEmpty();
    }
    
    return metadata;
}

QByteArray SpnPackageManager::extractFileFromSpn(const QString &spnPath, const QString &fileName)
{
    if (!isSpnPackage(spnPath)) {
        return QByteArray();
    }
    
    QFile spnFile(spnPath);
    if (!spnFile.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    
    QDataStream stream(&spnFile);
    stream.setVersion(QDataStream::Qt_6_0);
    
    // Read and verify header
    QString header;
    quint32 version;
    stream >> header >> version;
    
    if (stream.status() != QDataStream::Ok || header != "SPEEDYNOTE_PACKAGE" || version != 1) {
        spnFile.close();
        return QByteArray();
    }
    
    // Read file count
    quint32 fileCount;
    stream >> fileCount;
    
    if (stream.status() != QDataStream::Ok || fileCount > 100000) {
        spnFile.close();
        return QByteArray();
    }
    
    // Search for the requested file
    const quint64 MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB limit for extracted files
    
    for (quint32 i = 0; i < fileCount; ++i) {
        QString relativePath;
        quint64 fileSize;
        stream >> relativePath >> fileSize;
        
        if (stream.status() != QDataStream::Ok) {
            spnFile.close();
            return QByteArray();
        }
        
        if (relativePath == fileName) {
            if (fileSize > MAX_FILE_SIZE) {
                spnFile.close();
                return QByteArray();
            }
            
            QByteArray fileData(static_cast<qsizetype>(fileSize), 0);
            if (stream.readRawData(fileData.data(), fileSize) != static_cast<int>(fileSize)) {
                spnFile.close();
                return QByteArray();
            }
            spnFile.close();
            return fileData;
        } else {
            // Skip this file's data
            qint64 currentPos = spnFile.pos();
            qint64 newPos = currentPos + static_cast<qint64>(fileSize);
            
            if (newPos < currentPos || newPos > spnFile.size() || !spnFile.seek(newPos)) {
                spnFile.close();
                return QByteArray();
            }
        }
    }
    
    spnFile.close();
    return QByteArray();
}