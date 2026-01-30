#include "StarredModel.h"

StarredModel::StarredModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // Connect to library changes
    connect(NotebookLibrary::instance(), &NotebookLibrary::libraryChanged,
            this, [this]() { reload(); });
    
    // Initial load
    forceReload();
}

int StarredModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;  // Flat list, no children
    }
    return m_displayList.size();
}

QVariant StarredModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QVariant();
    }
    
    const DisplayItem& item = m_displayList.at(index.row());
    
    switch (role) {
        case Qt::DisplayRole:
            if (item.type == FolderHeaderItem) {
                return item.folderName;
            } else {
                return item.notebook.displayName();
            }
            
        case ItemTypeRole:
            return static_cast<int>(item.type);
            
        // === NotebookCardDelegate roles ===
        case NotebookInfoRole:
            if (item.type == NotebookCardItem) {
                return QVariant::fromValue(item.notebook);
            }
            return QVariant();
            
        case BundlePathRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.bundlePath;
            }
            return QString();
            
        case DisplayNameRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.displayName();
            }
            return QString();
            
        case ThumbnailPathRole:
            if (item.type == NotebookCardItem) {
                return NotebookLibrary::instance()->thumbnailPathFor(item.notebook.bundlePath);
            }
            return QString();
            
        case IsStarredRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.isStarred;
            }
            return false;
            
        case IsPdfBasedRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.isPdfBased;
            }
            return false;
            
        case IsEdgelessRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.isEdgeless;
            }
            return false;
            
        case LastModifiedRole:
            if (item.type == NotebookCardItem) {
                return item.notebook.lastModified;
            }
            return QDateTime();
            
        // === Batch select mode roles (L-007) ===
        case IsInSelectModeRole:
            return m_selectMode;
            
        case IsSelectedInBatchRole:
            if (item.type == NotebookCardItem) {
                return m_selectedBundlePaths.contains(item.notebook.bundlePath);
            }
            return false;
            
        // === FolderHeaderDelegate roles ===
        case FolderNameRole:
            if (item.type == FolderHeaderItem) {
                return item.folderName;
            }
            return QString();
            
        case IsCollapsedRole:
            if (item.type == FolderHeaderItem) {
                return m_collapsedFolders.value(item.folderName, false);
            }
            return false;
            
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> StarredModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    
    // Item type
    roles[ItemTypeRole] = "itemType";
    
    // NotebookCardDelegate roles
    roles[NotebookInfoRole] = "notebookInfo";
    roles[BundlePathRole] = "bundlePath";
    roles[DisplayNameRole] = "displayName";
    roles[ThumbnailPathRole] = "thumbnailPath";
    roles[IsStarredRole] = "isStarred";
    roles[IsPdfBasedRole] = "isPdfBased";
    roles[IsEdgelessRole] = "isEdgeless";
    roles[LastModifiedRole] = "lastModified";
    
    // Batch select mode roles (L-007)
    roles[IsInSelectModeRole] = "isInSelectMode";
    roles[IsSelectedInBatchRole] = "isSelectedInBatch";
    
    // FolderHeaderDelegate roles
    roles[FolderNameRole] = "folderName";
    roles[IsCollapsedRole] = "isCollapsed";
    
    return roles;
}

bool StarredModel::reload()
{
    // Check if content actually changed (smart reload)
    QString newSignature = computeContentSignature();
    if (newSignature == m_contentSignature) {
        return false;  // No change, skip rebuild
    }
    
    m_contentSignature = newSignature;
    
    beginResetModel();
    buildDisplayList();
    endResetModel();
    
    emit dataReloaded();
    return true;
}

void StarredModel::forceReload()
{
    m_contentSignature = computeContentSignature();
    
    beginResetModel();
    buildDisplayList();
    endResetModel();
    
    emit dataReloaded();
}

void StarredModel::toggleFolder(const QString& folderName)
{
    bool currentState = m_collapsedFolders.value(folderName, false);
    setFolderCollapsed(folderName, !currentState);
}

bool StarredModel::isFolderCollapsed(const QString& folderName) const
{
    return m_collapsedFolders.value(folderName, false);
}

void StarredModel::setFolderCollapsed(const QString& folderName, bool collapsed)
{
    if (m_collapsedFolders.value(folderName, false) == collapsed) {
        return;  // No change
    }
    
    m_collapsedFolders[folderName] = collapsed;
    
    // Rebuild display list to add/remove notebook items
    beginResetModel();
    buildDisplayList();
    endResetModel();
    
    emit folderToggled(folderName, collapsed);
}

StarredModel::ItemType StarredModel::itemTypeAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return FolderHeaderItem;  // Default, but shouldn't be used for invalid indices
    }
    return m_displayList.at(index.row()).type;
}

QString StarredModel::folderNameAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QString();
    }
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == FolderHeaderItem) {
        return item.folderName;
    }
    return QString();
}

QString StarredModel::bundlePathAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return QString();
    }
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == NotebookCardItem) {
        return item.notebook.bundlePath;
    }
    return QString();
}

NotebookInfo StarredModel::notebookAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayList.size()) {
        return NotebookInfo();
    }
    const DisplayItem& item = m_displayList.at(index.row());
    if (item.type == NotebookCardItem) {
        return item.notebook;
    }
    return NotebookInfo();
}

void StarredModel::buildDisplayList()
{
    m_displayList.clear();
    
    NotebookLibrary* lib = NotebookLibrary::instance();
    QList<NotebookInfo> starred = lib->starredNotebooks();
    QStringList folders = lib->starredFolders();
    
    // Group notebooks by folder
    QMap<QString, QList<NotebookInfo>> folderContents;
    QList<NotebookInfo> unfiled;
    
    for (const NotebookInfo& info : starred) {
        if (info.starredFolder.isEmpty()) {
            unfiled.append(info);
        } else {
            folderContents[info.starredFolder].append(info);
        }
    }
    
    // Add folder sections in order
    for (const QString& folderName : folders) {
        if (folderContents.contains(folderName)) {
            // Add folder header
            DisplayItem header;
            header.type = FolderHeaderItem;
            header.folderName = folderName;
            m_displayList.append(header);
            
            // Add notebooks only if folder is NOT collapsed
            if (!m_collapsedFolders.value(folderName, false)) {
                const QList<NotebookInfo>& notebooks = folderContents[folderName];
                for (const NotebookInfo& info : notebooks) {
                    DisplayItem item;
                    item.type = NotebookCardItem;
                    item.folderName = folderName;
                    item.notebook = info;
                    m_displayList.append(item);
                }
            }
        }
    }
    
    // Add "Unfiled" section if there are unfiled notebooks
    if (!unfiled.isEmpty()) {
        QString unfiledName = tr("Unfiled");
        
        DisplayItem header;
        header.type = FolderHeaderItem;
        header.folderName = unfiledName;
        m_displayList.append(header);
        
        // Add notebooks only if folder is NOT collapsed
        if (!m_collapsedFolders.value(unfiledName, false)) {
            for (const NotebookInfo& info : unfiled) {
                DisplayItem item;
                item.type = NotebookCardItem;
                item.folderName = unfiledName;
                item.notebook = info;
                m_displayList.append(item);
            }
        }
    }
}

QString StarredModel::computeContentSignature() const
{
    // Compute a signature that captures the structural content of starred notebooks
    // This includes: which notebooks are starred, which folder each is in, and folder order
    // It does NOT include: lastAccessed, lastModified, or other metadata
    // 
    // This allows us to skip expensive rebuilds when only metadata changes
    // (e.g., when opening a notebook updates lastAccessed time)
    
    NotebookLibrary* lib = NotebookLibrary::instance();
    QList<NotebookInfo> starred = lib->starredNotebooks();
    QStringList folders = lib->starredFolders();
    
    QStringList parts;
    
    // Add folder order
    parts << "FOLDERS:" + folders.join(",");
    
    // Group notebooks by folder and add their paths
    QMap<QString, QStringList> folderContents;
    QStringList unfiled;
    
    for (const NotebookInfo& info : starred) {
        if (info.starredFolder.isEmpty()) {
            unfiled << info.bundlePath;
        } else {
            folderContents[info.starredFolder] << info.bundlePath;
        }
    }
    
    // Add each folder's contents in order
    for (const QString& folderName : folders) {
        if (folderContents.contains(folderName)) {
            QStringList paths = folderContents[folderName];
            paths.sort();  // Consistent ordering
            parts << folderName + ":" + paths.join(",");
        }
    }
    
    // Add unfiled
    if (!unfiled.isEmpty()) {
        unfiled.sort();
        parts << "UNFILED:" + unfiled.join(",");
    }
    
    return parts.join(";");
}

// -----------------------------------------------------------------------------
// Batch Select Mode (L-007)
// -----------------------------------------------------------------------------

void StarredModel::setSelectMode(bool selectMode)
{
    if (m_selectMode == selectMode) {
        return;
    }
    
    m_selectMode = selectMode;
    
    // Clear selection when exiting select mode
    if (!selectMode) {
        m_selectedBundlePaths.clear();
    }
    
    // Notify all items that select mode changed (affects visual appearance)
    if (!m_displayList.isEmpty()) {
        emit dataChanged(index(0), index(m_displayList.size() - 1), 
                         {IsInSelectModeRole, IsSelectedInBatchRole});
    }
}

void StarredModel::setSelectedBundlePaths(const QSet<QString>& selectedPaths)
{
    if (m_selectedBundlePaths == selectedPaths) {
        return;
    }
    
    m_selectedBundlePaths = selectedPaths;
    
    // Notify all items that selection changed
    if (!m_displayList.isEmpty()) {
        emit dataChanged(index(0), index(m_displayList.size() - 1), 
                         {IsSelectedInBatchRole});
    }
}

bool StarredModel::isSelected(const QString& bundlePath) const
{
    return m_selectedBundlePaths.contains(bundlePath);
}
