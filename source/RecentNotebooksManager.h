#ifndef RECENTNOTEBOOKSMANAGER_H
#define RECENTNOTEBOOKSMANAGER_H

#include <QStringList>
#include <QSettings>
#include <QObject>
#include <QHash>

class RecentNotebooksManager : public QObject {
    Q_OBJECT

signals:
    void thumbnailUpdated(const QString& folderPath, const QString& coverImagePath);

public:
    explicit RecentNotebooksManager(QObject *parent = nullptr);
    
    // Singleton pattern for shared instance
    static RecentNotebooksManager* getInstance(QObject *parent = nullptr);

    void addRecentNotebook(const QString& folderPath, const QString& displayPath = QString());
    void removeRecentNotebook(const QString& folderPath);
    QStringList getRecentNotebooks() const;
    QString getCoverImagePathForNotebook(const QString& folderPath) const;
    QString getNotebookDisplayName(const QString& folderPath) const;
    void generateAndSaveCoverPreview(const QString& folderPath);
    
    // Starred notebooks functionality
    void addStarred(const QString& folderPath);
    void removeStarred(const QString& folderPath);
    bool isStarred(const QString& folderPath) const;
    QStringList getStarredNotebooks() const;

private:
    void loadRecentNotebooks();
    void saveRecentNotebooks();
    void loadStarredNotebooks();
    void saveStarredNotebooks();
    QString getCoverImageDir() const;
    QString sanitizeFolderName(const QString& folderPath) const;
    QString getPdfPathFromNotebook(const QString& folderPath) const;
    QString getNotebookIdFromPath(const QString& folderPath) const;

    QStringList recentNotebookPaths;
    QStringList starredNotebookPaths;
    const int MAX_RECENT_NOTEBOOKS = 16;
    
    // Singleton instance
    static RecentNotebooksManager* instance;
    QSettings settings;
    
    // Removed pendingThumbnails - no longer using delayed thumbnail generation
    
    // Caches for expensive operations to prevent repeated .spn extraction
    mutable QHash<QString, QString> pdfPathCache;
    mutable QHash<QString, QString> displayNameCache;
};

#endif // RECENTNOTEBOOKSMANAGER_H 