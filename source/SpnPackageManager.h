#ifndef SPNPACKAGEMANAGER_H
#define SPNPACKAGEMANAGER_H

#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>

// Forward declaration to avoid circular includes
enum class BackgroundStyle;

class SpnPackageManager
{
public:
    // Check if a path is a .spn package file
    static bool isSpnPackage(const QString &path);
    
    // Convert a regular folder to .spn package file
    static bool convertFolderToSpn(const QString &folderPath, QString &spnPath);
    
    // Convert folder to .spn package with specific target path
    static bool convertFolderToSpnPath(const QString &folderPath, const QString &targetSpnPath);
    
    // Extract .spn package to a temporary working directory
    static QString extractSpnToTemp(const QString &spnPath);
    
    // Update .spn package file from working directory
    static bool updateSpnFromTemp(const QString &spnPath, const QString &tempDir);
    
    // Create a new .spn package file
    static bool createSpnPackage(const QString &spnPath, const QString &notebookName = QString());
    
    // Create a new .spn package with custom background settings
    static bool createSpnPackageWithBackground(const QString &spnPath, const QString &notebookName,
                                               BackgroundStyle style, const QColor &color, int density);
    
    // Get the display name for a .spn package
    static QString getSpnDisplayName(const QString &spnPath);
    
    // Check if .spn package file is valid
    static bool isValidSpnPackage(const QString &spnPath);
    
    // Get suggested .spn name from PDF path
    static QString getSuggestedSpnName(const QString &pdfPath);
    
    // Get temporary directory for a .spn package
    static QString getTempDirForSpn(const QString &spnPath);
    
    // Clean up temporary directory
    static void cleanupTempDir(const QString &tempDir);

private:
    static const QString SPN_EXTENSION;
    static const QString TEMP_PREFIX;
    
    // Internal helper methods
    static bool packDirectoryToSpn(const QString &dirPath, const QString &spnPath);
    static bool unpackSpnToDirectory(const QString &spnPath, const QString &dirPath);
    static QJsonObject createSpnHeader(const QString &notebookName, 
                                       const QString &backgroundStyle = "None",
                                       const QString &backgroundColor = "#ffffff", 
                                       int backgroundDensity = 20);
    
    // Helper function to convert BackgroundStyle enum to string
    static QString backgroundStyleToString(BackgroundStyle style);
};

#endif // SPNPACKAGEMANAGER_H 