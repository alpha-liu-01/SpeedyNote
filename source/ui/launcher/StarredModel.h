#ifndef STARREDMODEL_H
#define STARREDMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QMap>
#include "../../core/NotebookLibrary.h"

/**
 * @brief Data model for starred notebooks organized in folders.
 * 
 * StarredModel provides a flat list model that represents a hierarchical
 * folder structure. It presents folder headers and notebook cards as
 * items in a single list, suitable for QListView rendering.
 * 
 * Display list structure:
 * @code
 * [FolderHeader: "Work"]
 * [NotebookCard: notebook1]
 * [NotebookCard: notebook2]
 * [FolderHeader: "Personal"]  <- collapsed, no children in list
 * [FolderHeader: "Unfiled"]
 * [NotebookCard: notebook3]
 * @endcode
 * 
 * When a folder is collapsed, its notebooks are NOT in the display list.
 * This provides true virtualization - collapsed folders don't create items.
 * 
 * Works with both NotebookCardDelegate and FolderHeaderDelegate.
 * The view can use ItemTypeRole to determine which delegate to use.
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class StarredModel : public QAbstractListModel {
    Q_OBJECT

public:
    /**
     * @brief Item type for distinguishing folder headers from notebook cards.
     */
    enum ItemType {
        FolderHeaderItem = 0,
        NotebookCardItem = 1
    };
    Q_ENUM(ItemType)
    
    /**
     * @brief Data roles for this model.
     * 
     * Roles are designed to be compatible with both NotebookCardDelegate
     * and FolderHeaderDelegate.
     */
    enum Roles {
        // Item type role (for both delegates)
        ItemTypeRole = Qt::UserRole + 1,
        
        // NotebookCardDelegate roles (Qt::UserRole + 100 range)
        NotebookInfoRole = Qt::UserRole + 100,
        BundlePathRole,
        DisplayNameRole,
        ThumbnailPathRole,
        IsStarredRole,
        IsPdfBasedRole,
        IsEdgelessRole,
        
        // FolderHeaderDelegate roles (Qt::UserRole + 200 range)
        FolderNameRole = Qt::UserRole + 200,
        IsCollapsedRole,
    };

    explicit StarredModel(QObject* parent = nullptr);
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    /**
     * @brief Reload data from NotebookLibrary.
     * 
     * Only rebuilds if the content signature has changed (smart reload).
     * @return True if a rebuild occurred, false if data was unchanged.
     */
    bool reload();
    
    /**
     * @brief Force a full reload regardless of content signature.
     */
    void forceReload();
    
    /**
     * @brief Toggle collapsed state of a folder.
     * @param folderName The folder to toggle.
     */
    void toggleFolder(const QString& folderName);
    
    /**
     * @brief Check if a folder is collapsed.
     * @param folderName The folder to check.
     * @return True if collapsed, false if expanded.
     */
    bool isFolderCollapsed(const QString& folderName) const;
    
    /**
     * @brief Set collapsed state for a folder.
     * @param folderName The folder name.
     * @param collapsed True to collapse, false to expand.
     */
    void setFolderCollapsed(const QString& folderName, bool collapsed);
    
    /**
     * @brief Check if the model has any items.
     */
    bool isEmpty() const { return m_displayList.isEmpty(); }
    
    /**
     * @brief Get the item type at a specific index.
     * @param index The model index.
     * @return The item type, or -1 if invalid.
     */
    ItemType itemTypeAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the folder name at a specific index (for folder headers).
     * @param index The model index.
     * @return The folder name, or empty string if not a folder header.
     */
    QString folderNameAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the bundle path at a specific index (for notebook cards).
     * @param index The model index.
     * @return The bundle path, or empty string if not a notebook card.
     */
    QString bundlePathAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the NotebookInfo at a specific index (for notebook cards).
     * @param index The model index.
     * @return The NotebookInfo, or invalid one if not a notebook card.
     */
    NotebookInfo notebookAt(const QModelIndex& index) const;

signals:
    /**
     * @brief Emitted when the model data is reloaded.
     */
    void dataReloaded();
    
    /**
     * @brief Emitted when a folder's collapsed state changes.
     * @param folderName The folder that was toggled.
     * @param collapsed The new collapsed state.
     */
    void folderToggled(const QString& folderName, bool collapsed);

private:
    /**
     * @brief Build the display list from NotebookLibrary data.
     * 
     * Creates folder headers and notebook card items, respecting
     * collapsed folder state.
     */
    void buildDisplayList();
    
    /**
     * @brief Compute a content signature for smart reload detection.
     * 
     * The signature captures structural content (folders, notebooks, assignments)
     * but NOT metadata (lastAccessed, lastModified).
     * 
     * @return A string signature representing the current content.
     */
    QString computeContentSignature() const;
    
    // Internal item representation
    struct DisplayItem {
        ItemType type;
        QString folderName;      // For FolderHeaderItem
        NotebookInfo notebook;   // For NotebookCardItem
    };
    
    QList<DisplayItem> m_displayList;
    
    // Folder collapsed state (persisted across reloads)
    QMap<QString, bool> m_collapsedFolders;
    
    // Content signature for smart reload
    QString m_contentSignature;
};

#endif // STARREDMODEL_H
