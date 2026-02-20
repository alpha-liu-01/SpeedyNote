#include "SearchModel.h"

SearchModel::SearchModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int SearchModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;  // Flat list, no children
    }
    return static_cast<int>(m_displayList.size());
}

QVariant SearchModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QVariant();
    }
    
    const DisplayItem& item = m_displayList.at(index.row());
    
    // Handle item type role
    if (role == ItemTypeRole) {
        return item.type;
    }
    
    switch (item.type) {
        case SectionHeaderItem:
            // Section header
            switch (role) {
                case Qt::DisplayRole:
                case SectionTitleRole:
                    return item.text;
                default:
                    return QVariant();
            }
            
        case FolderResultItem:
            // Folder item
            switch (role) {
                case Qt::DisplayRole:
                case DisplayNameRole:
                case FolderNameRole:
                    return item.text;
                    
                // Notebook-specific roles return empty/false for folders
                case NotebookInfoRole:
                case BundlePathRole:
                case ThumbnailPathRole:
                case SectionTitleRole:
                    return QVariant();
                case IsStarredRole:
                case IsPdfBasedRole:
                case IsEdgelessRole:
                    return false;
                case LastModifiedRole:
                    return QVariant();
                    
                default:
                    return QVariant();
            }
            
        case NotebookResultItem:
            // Notebook item
            switch (role) {
                case Qt::DisplayRole:
                case DisplayNameRole:
                    return item.notebook.displayName();
                    
                case NotebookInfoRole:
                    return QVariant::fromValue(item.notebook);
                    
                case BundlePathRole:
                    return item.notebook.bundlePath;
                    
                case ThumbnailPathRole:
                    return NotebookLibrary::instance()->thumbnailPathFor(item.notebook.bundlePath);
                    
                case IsStarredRole:
                    return item.notebook.isStarred;
                    
                case IsPdfBasedRole:
                    return item.notebook.isPdfBased;
                    
                case IsEdgelessRole:
                    return item.notebook.isEdgeless;
                    
                case LastModifiedRole:
                    return item.notebook.lastModified;
                    
                case FolderNameRole:
                case SectionTitleRole:
                    return QVariant();
                    
                default:
                    return QVariant();
            }
    }
    
    return QVariant();
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[NotebookInfoRole] = "notebookInfo";
    roles[BundlePathRole] = "bundlePath";
    roles[DisplayNameRole] = "displayName";
    roles[ThumbnailPathRole] = "thumbnailPath";
    roles[IsStarredRole] = "isStarred";
    roles[IsPdfBasedRole] = "isPdfBased";
    roles[IsEdgelessRole] = "isEdgeless";
    roles[LastModifiedRole] = "lastModified";
    roles[ItemTypeRole] = "itemType";
    roles[FolderNameRole] = "folderName";
    roles[SectionTitleRole] = "sectionTitle";
    return roles;
}

void SearchModel::setResults(const QList<NotebookInfo>& results)
{
    beginResetModel();
    m_folders.clear();
    m_notebooks = results;
    rebuildDisplayList();
    endResetModel();
}

void SearchModel::setResults(const QStringList& folders, const QList<NotebookInfo>& notebooks)
{
    beginResetModel();
    m_folders = folders;
    m_notebooks = notebooks;
    rebuildDisplayList();
    endResetModel();
}

void SearchModel::clear()
{
    if (!m_displayList.isEmpty()) {
        beginResetModel();
        m_folders.clear();
        m_notebooks.clear();
        m_displayList.clear();
        endResetModel();
    }
}

void SearchModel::rebuildDisplayList()
{
    m_displayList.clear();
    
    // Add folders section if there are folder results
    if (!m_folders.isEmpty()) {
        // Section header
        DisplayItem header;
        header.type = SectionHeaderItem;
        header.text = tr("FOLDERS");
        m_displayList.append(header);
        
        // Folder items
        for (const QString& folder : m_folders) {
            DisplayItem folderItem;
            folderItem.type = FolderResultItem;
            folderItem.text = folder;
            m_displayList.append(folderItem);
        }
    }
    
    // Add notebooks section if there are notebook results
    if (!m_notebooks.isEmpty()) {
        // Section header
        DisplayItem header;
        header.type = SectionHeaderItem;
        header.text = tr("NOTEBOOKS");
        m_displayList.append(header);
        
        // Notebook items
        for (const NotebookInfo& nb : m_notebooks) {
            DisplayItem nbItem;
            nbItem.type = NotebookResultItem;
            nbItem.notebook = nb;
            m_displayList.append(nbItem);
        }
    }
}

SearchModel::ItemType SearchModel::itemTypeAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return NotebookResultItem;  // Default
    }
    return m_displayList.at(index.row()).type;
}

NotebookInfo SearchModel::notebookAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return NotebookInfo();
    }
    
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == NotebookResultItem) {
        return item.notebook;
    }
    return NotebookInfo();
}

QString SearchModel::folderNameAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QString();
    }
    
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == FolderResultItem) {
        return item.text;
    }
    return QString();
}

QString SearchModel::bundlePathAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QString();
    }
    
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == NotebookResultItem) {
        return item.notebook.bundlePath;
    }
    return QString();
}
