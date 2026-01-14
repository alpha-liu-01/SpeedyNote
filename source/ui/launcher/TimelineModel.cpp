#include "TimelineModel.h"
#include "../../core/NotebookLibrary.h"

#include <QDate>
#include <QDateTime>

TimelineModel::TimelineModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // Connect to library changes
    connect(NotebookLibrary::instance(), &NotebookLibrary::libraryChanged,
            this, &TimelineModel::reload);
    
    // Initial load
    reload();
}

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return QVariant();
    }
    
    const DisplayItem& item = m_items[index.row()];
    
    switch (role) {
        case Qt::DisplayRole:
            if (item.isHeader) {
                return item.sectionName;
            } else {
                return item.notebook.displayName();
            }
            
        case IsSectionHeaderRole:
            return item.isHeader;
            
        case SectionNameRole:
            return item.sectionName;
            
        case NotebookInfoRole:
            if (!item.isHeader) {
                return QVariant::fromValue(item.notebook);
            }
            return QVariant();
            
        case BundlePathRole:
            return item.notebook.bundlePath;
            
        case ThumbnailPathRole:
            if (!item.isHeader) {
                return NotebookLibrary::instance()->thumbnailPathFor(item.notebook.bundlePath);
            }
            return QString();
            
        case LastModifiedRole:
            return item.notebook.lastModified;
            
        case LastAccessedRole:
            return item.notebook.lastAccessed;
            
        case IsPdfBasedRole:
            return item.notebook.isPdfBased;
            
        case IsEdgelessRole:
            return item.notebook.isEdgeless;
            
        case IsStarredRole:
            return item.notebook.isStarred;
    }
    
    return QVariant();
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[NotebookInfoRole] = "notebookInfo";
    roles[IsSectionHeaderRole] = "isSectionHeader";
    roles[SectionNameRole] = "sectionName";
    roles[BundlePathRole] = "bundlePath";
    roles[ThumbnailPathRole] = "thumbnailPath";
    roles[LastModifiedRole] = "lastModified";
    roles[LastAccessedRole] = "lastAccessed";
    roles[IsPdfBasedRole] = "isPdfBased";
    roles[IsEdgelessRole] = "isEdgeless";
    roles[IsStarredRole] = "isStarred";
    return roles;
}

void TimelineModel::reload()
{
    beginResetModel();
    buildDisplayList();
    endResetModel();
    emit dataReloaded();
}

QString TimelineModel::sectionForDate(const QDateTime& date) const
{
    if (!date.isValid()) {
        return tr("Unknown");
    }
    
    const QDate today = QDate::currentDate();
    const QDate dateDay = date.date();
    
    // Today
    if (dateDay == today) {
        return tr("Today");
    }
    
    // Yesterday
    if (dateDay == today.addDays(-1)) {
        return tr("Yesterday");
    }
    
    // This Week (within last 7 days, but not today/yesterday)
    if (dateDay >= today.addDays(-7)) {
        return tr("This Week");
    }
    
    // This Month
    if (dateDay.year() == today.year() && dateDay.month() == today.month()) {
        return tr("This Month");
    }
    
    // Last Month
    QDate lastMonth = today.addMonths(-1);
    if (dateDay.year() == lastMonth.year() && dateDay.month() == lastMonth.month()) {
        return tr("Last Month");
    }
    
    // This Year - show month name
    if (dateDay.year() == today.year()) {
        return QLocale().monthName(dateDay.month());
    }
    
    // Previous years - show year (collapsible in UI)
    return QString::number(dateDay.year());
}

void TimelineModel::buildDisplayList()
{
    m_items.clear();
    
    QList<NotebookInfo> notebooks = NotebookLibrary::instance()->recentNotebooks();
    
    if (notebooks.isEmpty()) {
        return;
    }
    
    QString currentSection;
    
    for (const NotebookInfo& notebook : notebooks) {
        // Determine section based on lastAccessed
        QString section = sectionForDate(notebook.lastAccessed);
        
        // Insert section header if this is a new section
        if (section != currentSection) {
            DisplayItem header;
            header.isHeader = true;
            header.sectionName = section;
            m_items.append(header);
            currentSection = section;
        }
        
        // Add notebook item
        DisplayItem item;
        item.isHeader = false;
        item.sectionName = section;
        item.notebook = notebook;
        m_items.append(item);
    }
}

