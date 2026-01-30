#include "TimelineModel.h"
#include "../../core/NotebookLibrary.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

TimelineModel::TimelineModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_lastKnownDate(QDate::currentDate())
{
    // Connect to library changes
    connect(NotebookLibrary::instance(), &NotebookLibrary::libraryChanged,
            this, &TimelineModel::reload);
    
    // Setup midnight timer for automatic date rollover refresh
    m_midnightTimer = new QTimer(this);
    m_midnightTimer->setSingleShot(true);
    connect(m_midnightTimer, &QTimer::timeout, this, [this]() {
        // Check if date actually changed (handles timezone/DST edge cases)
        QDate today = QDate::currentDate();
        if (today != m_lastKnownDate) {
            m_lastKnownDate = today;
            reload();  // Refresh sections and trigger repaint
        }
        // Schedule next midnight check
        scheduleMidnightRefresh();
    });
    
    // Initial load
    reload();
    
    // Schedule first midnight refresh
    scheduleMidnightRefresh();
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
            
        case DisplayNameRole:
            if (!item.isHeader) {
                return item.notebook.displayName();
            }
            return QString();
            
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
    roles[BundlePathRole] = "bundlePath";
    roles[DisplayNameRole] = "displayName";
    roles[ThumbnailPathRole] = "thumbnailPath";
    roles[IsStarredRole] = "isStarred";
    roles[IsPdfBasedRole] = "isPdfBased";
    roles[IsEdgelessRole] = "isEdgeless";
    roles[LastModifiedRole] = "lastModified";
    roles[IsSectionHeaderRole] = "isSectionHeader";
    roles[SectionNameRole] = "sectionName";
    roles[LastAccessedRole] = "lastAccessed";
    return roles;
}

void TimelineModel::reload()
{
    beginResetModel();
    buildDisplayList();
    endResetModel();
    emit dataReloaded();
}

bool TimelineModel::refreshIfDateChanged()
{
    QDate today = QDate::currentDate();
    if (today != m_lastKnownDate) {
        m_lastKnownDate = today;
        reload();
        // Reschedule midnight timer since date changed
        scheduleMidnightRefresh();
        return true;
    }
    return false;
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
    
    // Track the date when sections were computed
    m_lastKnownDate = QDate::currentDate();
    
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

void TimelineModel::scheduleMidnightRefresh()
{
    // Calculate milliseconds until midnight + 1 second (to ensure we're past midnight)
    QDateTime now = QDateTime::currentDateTime();
    QDateTime midnight = QDateTime(now.date().addDays(1), QTime(0, 0, 1));
    
    qint64 msUntilMidnight = now.msecsTo(midnight);
    
    // Sanity check: if calculation is negative or too large, use fallback
    if (msUntilMidnight <= 0 || msUntilMidnight > 24 * 60 * 60 * 1000 + 1000) {
        msUntilMidnight = 60 * 60 * 1000;  // Fallback: check in 1 hour
    }
    
    m_midnightTimer->start(static_cast<int>(msUntilMidnight));
}

