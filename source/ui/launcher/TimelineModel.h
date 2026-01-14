#ifndef TIMELINEMODEL_H
#define TIMELINEMODEL_H

#include <QAbstractListModel>
#include <QList>
#include "../../core/NotebookLibrary.h"

/**
 * @brief Data model for the Timeline view in the Launcher.
 * 
 * TimelineModel groups notebooks by time period (Today, Yesterday, This Week, etc.)
 * and presents them as a flat list with section headers for QListView.
 * 
 * Item roles:
 * - DisplayRole: Notebook display name (for cards) or section title (for headers)
 * - NotebookInfoRole: Full NotebookInfo struct (for cards only)
 * - IsSectionHeaderRole: True if this item is a section header
 * - SectionNameRole: Section name for the item (even if not a header)
 * 
 * Data source: NotebookLibrary::recentNotebooks()
 * 
 * Phase P.3.3: Part of the new Launcher implementation.
 */
class TimelineModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NotebookInfoRole = Qt::UserRole + 1,
        IsSectionHeaderRole,
        SectionNameRole,
        BundlePathRole,
        ThumbnailPathRole,
        LastModifiedRole,
        LastAccessedRole,
        IsPdfBasedRole,
        IsEdgelessRole,
        IsStarredRole
    };

    explicit TimelineModel(QObject* parent = nullptr);
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    /**
     * @brief Reload data from NotebookLibrary.
     * 
     * Call this when the library changes or when the view becomes visible.
     */
    void reload();

signals:
    /**
     * @brief Emitted when the model data is refreshed.
     */
    void dataReloaded();

private:
    /**
     * @brief Determine the section name for a given date.
     * @param date The date to classify.
     * @return Section name like "Today", "Yesterday", "This Week", etc.
     */
    QString sectionForDate(const QDateTime& date) const;
    
    /**
     * @brief Build the display list from NotebookLibrary data.
     * 
     * Groups notebooks by section and inserts section headers.
     */
    void buildDisplayList();

    // Internal item representation
    struct DisplayItem {
        bool isHeader = false;
        QString sectionName;
        NotebookInfo notebook;  // Only valid if !isHeader
    };
    
    QList<DisplayItem> m_items;
};

#endif // TIMELINEMODEL_H

