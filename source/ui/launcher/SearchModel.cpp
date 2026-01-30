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
    return m_results.size();
}

QVariant SearchModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return QVariant();
    }
    
    const NotebookInfo& info = m_results.at(index.row());
    
    switch (role) {
        case Qt::DisplayRole:
        case DisplayNameRole:
            return info.displayName();
            
        case NotebookInfoRole:
            return QVariant::fromValue(info);
            
        case BundlePathRole:
            return info.bundlePath;
            
        case ThumbnailPathRole:
            return NotebookLibrary::instance()->thumbnailPathFor(info.bundlePath);
            
        case IsStarredRole:
            return info.isStarred;
            
        case IsPdfBasedRole:
            return info.isPdfBased;
            
        case IsEdgelessRole:
            return info.isEdgeless;
            
        default:
            return QVariant();
    }
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
    return roles;
}

void SearchModel::setResults(const QList<NotebookInfo>& results)
{
    beginResetModel();
    m_results = results;
    endResetModel();
}

void SearchModel::clear()
{
    if (!m_results.isEmpty()) {
        beginResetModel();
        m_results.clear();
        endResetModel();
    }
}

NotebookInfo SearchModel::notebookAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return NotebookInfo();
    }
    return m_results.at(index.row());
}

QString SearchModel::bundlePathAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return QString();
    }
    return m_results.at(index.row()).bundlePath;
}
