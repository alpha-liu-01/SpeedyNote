#ifndef SEARCHMODEL_H
#define SEARCHMODEL_H

#include <QAbstractListModel>
#include <QList>
#include "../../core/NotebookLibrary.h"

/**
 * @brief Data model for search results in SearchView.
 * 
 * SearchModel provides a structured list model for displaying mixed search
 * results containing section headers, folder items, and notebook cards.
 * 
 * Display structure:
 * - "FOLDERS" section header (if folders found)
 * - Folder items as simple list items
 * - "NOTEBOOKS" section header (if notebooks found)
 * - Notebook cards
 * 
 * L-009: Updated to support mixed folder + notebook search results.
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class SearchModel : public QAbstractListModel {
    Q_OBJECT

public:
    /**
     * @brief Item type for search results.
     * 
     * L-009: Distinguishes different item types in search results.
     */
    enum ItemType {
        SectionHeaderItem = 0,  // Section header ("FOLDERS", "NOTEBOOKS")
        FolderResultItem = 1,   // Folder search result (simple list item)
        NotebookResultItem = 2  // Notebook search result (card)
    };
    
    /**
     * @brief Data roles for this model.
     * 
     * These match NotebookCardDelegate::DataRoles for notebook items.
     */
    enum Roles {
        NotebookInfoRole = Qt::UserRole + 100,  // QVariant containing NotebookInfo (notebooks only)
        BundlePathRole,                          // QString: path to notebook bundle (notebooks only)
        DisplayNameRole,                         // QString: notebook display name or folder name
        ThumbnailPathRole,                       // QString: path to thumbnail file (notebooks only)
        IsStarredRole,                           // bool: whether notebook is starred (notebooks only)
        IsPdfBasedRole,                          // bool: whether notebook is PDF-based (notebooks only)
        IsEdgelessRole,                          // bool: whether notebook is edgeless (notebooks only)
        LastModifiedRole,                        // QDateTime: last modification time (notebooks only)
        
        // L-009: New roles for mixed results
        ItemTypeRole = Qt::UserRole + 150,       // ItemType: section/folder/notebook
        FolderNameRole,                          // QString: folder name (folders only)
        SectionTitleRole,                        // QString: section header text
    };

    explicit SearchModel(QObject* parent = nullptr);
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    /**
     * @brief Set the search results to display (notebooks only, legacy).
     * @param results List of NotebookInfo from NotebookLibrary::search().
     * 
     * This replaces any existing results and triggers a model reset.
     * Clears any folder results.
     */
    void setResults(const QList<NotebookInfo>& results);
    
    /**
     * @brief Set mixed search results (folders + notebooks).
     * @param folders List of folder names from searchStarredFolders().
     * @param notebooks List of NotebookInfo from search().
     * 
     * Folders are displayed first, followed by notebooks.
     * 
     * L-009: New method for mixed search results.
     */
    void setResults(const QStringList& folders, const QList<NotebookInfo>& notebooks);
    
    /**
     * @brief Clear all results.
     */
    void clear();
    
    /**
     * @brief Get the total number of results (folders + notebooks).
     */
    int resultCount() const { return m_folders.size() + m_notebooks.size(); }
    
    /**
     * @brief Get the number of folder results.
     */
    int folderCount() const { return m_folders.size(); }
    
    /**
     * @brief Get the number of notebook results.
     */
    int notebookCount() const { return m_notebooks.size(); }
    
    /**
     * @brief Check if the model has any results.
     */
    bool isEmpty() const { return m_folders.isEmpty() && m_notebooks.isEmpty(); }
    
    /**
     * @brief Get the item type at a specific index.
     * @param index The model index.
     * @return FolderResultItem or NotebookResultItem.
     */
    ItemType itemTypeAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the NotebookInfo at a specific index.
     * @param index The model index.
     * @return The NotebookInfo, or an invalid one if index is not a notebook.
     */
    NotebookInfo notebookAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the folder name at a specific index.
     * @param index The model index.
     * @return The folder name, or empty string if index is not a folder.
     */
    QString folderNameAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the bundle path at a specific index (notebooks only).
     * @param index The model index.
     * @return The bundle path, or empty string if index is not a notebook.
     */
    QString bundlePathAt(const QModelIndex& index) const;

private:
    /**
     * @brief Internal display item representation.
     */
    struct DisplayItem {
        ItemType type;
        QString text;            // Section title or folder name
        NotebookInfo notebook;   // Only valid for NotebookResultItem
    };
    
    void rebuildDisplayList();
    
    QStringList m_folders;           // Raw folder search results
    QList<NotebookInfo> m_notebooks; // Raw notebook search results
    QList<DisplayItem> m_displayList; // Flattened list with sections
};

#endif // SEARCHMODEL_H
