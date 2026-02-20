#ifndef NOTEBOOKLIBRARY_H
#define NOTEBOOKLIBRARY_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include <QPixmap>
#include <QTimer>

/**
 * @brief Metadata for a notebook stored in the library.
 * 
 * This struct holds all information needed to display a notebook
 * in the Launcher without loading the full Document.
 * 
 * Phase P.2: Part of the new NotebookLibrary system that replaces
 * RecentNotebooksManager with a more comprehensive approach.
 */
struct NotebookInfo {
    QString bundlePath;       ///< Full path to the .snb bundle directory
    QString name;             ///< Display name (from document.json or derived from path)
    QString documentId;       ///< Unique ID from document.json
    QDateTime lastModified;   ///< When the notebook was last saved
    QDateTime lastAccessed;   ///< When the notebook was last opened
    bool isStarred = false;   ///< Whether the notebook is starred/favorited
    QString starredFolder;    ///< Folder name if starred (empty = unfiled)
    bool isPdfBased = false;  ///< True if this is a PDF annotation notebook
    bool isEdgeless = false;  ///< True if this is an edgeless (infinite canvas) notebook
    QString pdfFileName;      ///< Original PDF filename (for search), if PDF-based
    
    /**
     * @brief Check if this notebook info is valid.
     * @return True if bundlePath is non-empty.
     */
    bool isValid() const { return !bundlePath.isEmpty(); }
    
    /**
     * @brief Get the display name, falling back to bundle folder name.
     * @return The name if set, otherwise the last component of bundlePath.
     */
    QString displayName() const {
        if (!name.isEmpty()) {
            return name;
        }
        // Extract folder name from path
        qsizetype lastSlash = bundlePath.lastIndexOf('/');
        if (lastSlash >= 0 && lastSlash < bundlePath.length() - 1) {
            QString folderName = bundlePath.mid(lastSlash + 1);
            // Remove .snb extension if present
            if (folderName.endsWith(".snb", Qt::CaseInsensitive)) {
                folderName.chop(4);
            }
            return folderName;
        }
        return bundlePath;
    }
};

// Register NotebookInfo with Qt's meta type system for QVariant support
Q_DECLARE_METATYPE(NotebookInfo)

/**
 * @brief Central manager for notebook metadata, recent/starred lists, and thumbnails.
 * 
 * NotebookLibrary is a singleton that:
 * - Tracks recently opened notebooks
 * - Manages starred notebooks and folders
 * - Provides search functionality
 * - Manages thumbnail cache on disk
 * 
 * Data is persisted to a JSON file in the app's data directory.
 * 
 * Phase P.2: Replaces RecentNotebooksManager with comprehensive library management.
 */
class NotebookLibrary : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     * @return Pointer to the NotebookLibrary instance.
     */
    static NotebookLibrary* instance();
    
    // === Recent Management ===
    
    /**
     * @brief Get all notebooks sorted by last accessed time (most recent first).
     */
    QList<NotebookInfo> recentNotebooks() const;
    
    /**
     * @brief Add or update a notebook in the library.
     * @param bundlePath Full path to the .snb bundle.
     * 
     * If the notebook exists, updates lastAccessed. Otherwise, reads
     * metadata from document.json and adds to the library.
     */
    void addToRecent(const QString& bundlePath);
    
    /**
     * @brief Remove a notebook from the library.
     * @param bundlePath Full path to the .snb bundle.
     */
    void removeFromRecent(const QString& bundlePath);
    
    /**
     * @brief Update the lastAccessed timestamp for a notebook.
     * @param bundlePath Full path to the .snb bundle.
     */
    void updateLastAccessed(const QString& bundlePath);
    
    // === Starred Management ===
    
    /**
     * @brief Get all starred notebooks.
     */
    QList<NotebookInfo> starredNotebooks() const;
    
    /**
     * @brief Set the starred status of a notebook.
     * @param bundlePath Full path to the .snb bundle.
     * @param starred True to star, false to unstar.
     */
    void setStarred(const QString& bundlePath, bool starred);
    
    /**
     * @brief Assign a notebook to a starred folder.
     * @param bundlePath Full path to the .snb bundle.
     * @param folder Folder name (empty string = unfiled).
     */
    void setStarredFolder(const QString& bundlePath, const QString& folder);
    
    /**
     * @brief Get the ordered list of starred folder names.
     */
    QStringList starredFolders() const;
    
    /**
     * @brief Get the most recently used folders (up to 5).
     * 
     * Returns folders ordered by most recent usage first.
     * Used by FolderPickerDialog to show quick-access folders.
     * 
     * L-008: Part of the Folder Picker UI feature.
     */
    QStringList recentFolders() const;
    
    /**
     * @brief Record that a folder was used (e.g., notebooks moved to it).
     * @param folder The folder name that was used.
     * 
     * Moves the folder to the front of the recent list.
     * Called automatically by moveNotebooksToFolder().
     * 
     * L-008: Part of the Folder Picker UI feature.
     */
    void recordFolderUsage(const QString& folder);
    
    // === Bulk Operations (L-007) ===
    
    /**
     * @brief Star multiple notebooks at once.
     * @param bundlePaths List of notebook bundle paths to star.
     * 
     * More efficient than calling setStarred() multiple times because
     * it only emits libraryChanged() once at the end.
     */
    void starNotebooks(const QStringList& bundlePaths);
    
    /**
     * @brief Unstar multiple notebooks at once.
     * @param bundlePaths List of notebook bundle paths to unstar.
     * 
     * More efficient than calling setStarred() multiple times because
     * it only emits libraryChanged() once at the end.
     */
    void unstarNotebooks(const QStringList& bundlePaths);
    
    /**
     * @brief Move multiple notebooks to a folder.
     * @param bundlePaths List of notebook bundle paths to move.
     * @param folder Target folder name. If the notebooks are not starred,
     *               they will be starred first.
     * 
     * More efficient than calling setStarredFolder() multiple times because
     * it only emits libraryChanged() once at the end.
     */
    void moveNotebooksToFolder(const QStringList& bundlePaths, const QString& folder);
    
    /**
     * @brief Remove multiple notebooks from their folders (move to Unfiled).
     * @param bundlePaths List of notebook bundle paths to remove from folders.
     * 
     * The notebooks remain starred, just moved to the Unfiled section.
     * More efficient than calling setStarredFolder() multiple times.
     */
    void removeNotebooksFromFolder(const QStringList& bundlePaths);
    
    /**
     * @brief Create a new starred folder.
     * @param name Folder name (must be unique).
     */
    void createStarredFolder(const QString& name);
    
    /**
     * @brief Delete a starred folder.
     * @param name Folder name.
     * 
     * Notebooks in this folder become unfiled.
     */
    void deleteStarredFolder(const QString& name);
    
    /**
     * @brief Reorder a starred folder.
     * @param name Folder name.
     * @param newIndex New position in the folder list.
     */
    void reorderStarredFolder(const QString& name, int newIndex);
    
    // === Search ===
    
    /**
     * @brief Search notebooks by name and PDF filename.
     * @param query Search query string.
     * @return Matching notebooks sorted by relevance.
     */
    QList<NotebookInfo> search(const QString& query) const;
    
    /**
     * @brief Search starred folders by name.
     * @param query Search query string.
     * @return Matching folder names (case-insensitive substring match).
     * 
     * L-009: Enables folder search in SearchView.
     */
    QStringList searchStarredFolders(const QString& query) const;
    
    // === Thumbnails ===
    
    /**
     * @brief Get the path to the cached thumbnail for a notebook.
     * @param bundlePath Full path to the .snb bundle.
     * @return Path to thumbnail file, or empty if not cached.
     */
    QString thumbnailPathFor(const QString& bundlePath) const;
    
    /**
     * @brief Save a thumbnail to the disk cache.
     * @param bundlePath Full path to the .snb bundle.
     * @param thumbnail The thumbnail pixmap to save.
     */
    void saveThumbnail(const QString& bundlePath, const QPixmap& thumbnail);
    
    /**
     * @brief Invalidate (delete) the cached thumbnail for a notebook.
     * @param bundlePath Full path to the .snb bundle.
     */
    void invalidateThumbnail(const QString& bundlePath);
    
    // === Persistence ===
    
    /**
     * @brief Save the library to disk.
     */
    void save();
    
    /**
     * @brief Load the library from disk.
     */
    void load();

signals:
    /**
     * @brief Emitted when the library contents change.
     */
    void libraryChanged();
    
    /**
     * @brief Emitted when a thumbnail is updated.
     * @param bundlePath The notebook whose thumbnail changed.
     */
    void thumbnailUpdated(const QString& bundlePath);

private:
    /**
     * @brief Private constructor for singleton.
     */
    explicit NotebookLibrary(QObject* parent = nullptr);
    
    /**
     * @brief Find a notebook by path.
     * @return Pointer to the NotebookInfo, or nullptr if not found.
     */
    NotebookInfo* findNotebook(const QString& bundlePath);
    const NotebookInfo* findNotebook(const QString& bundlePath) const;
    
    /**
     * @brief Schedule a debounced save operation.
     * 
     * Multiple calls within the debounce window are coalesced into one save.
     */
    void scheduleSave();
    
    /**
     * @brief Mark the library as changed and schedule save.
     */
    void markDirty();
    
    QString m_libraryFilePath;        ///< Path to the library JSON file
    QString m_thumbnailCachePath;     ///< Path to the thumbnail cache directory
    QList<NotebookInfo> m_notebooks;  ///< All tracked notebooks
    QStringList m_starredFolderOrder; ///< Ordered list of starred folder names
    QStringList m_recentFolders;      ///< Recently used folders (L-008), max 5
    QTimer m_saveTimer;               ///< Timer for debounced auto-save
    
    static constexpr int SAVE_DEBOUNCE_MS = 1000;  ///< Debounce delay for auto-save
    static constexpr int LIBRARY_VERSION = 1;      ///< Current library file format version
    static constexpr qint64 MAX_CACHE_SIZE_BYTES = 200 * 1024 * 1024; ///< 200 MiB cache limit
    static constexpr int MAX_RECENT_FOLDERS = 5;   ///< Max folders in recent list (L-008)
    
    /**
     * @brief Clean up old thumbnails if cache exceeds size limit.
     * 
     * Uses LRU eviction based on file modification time.
     */
    void cleanupThumbnailCache();
    
    static NotebookLibrary* s_instance;
};

#endif // NOTEBOOKLIBRARY_H

