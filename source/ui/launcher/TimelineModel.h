#ifndef TIMELINEMODEL_H
#define TIMELINEMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QDate>
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
    /**
     * @brief Data roles for TimelineModel.
     * 
     * Notebook card roles (100+) match NotebookCardDelegate::DataRoles for compatibility.
     * Batch select mode roles (200+) for selection state.
     * Timeline-specific roles (300+) are unique to this model.
     */
    enum Roles {
        // NotebookCardDelegate-compatible roles (Qt::UserRole + 100 range)
        NotebookInfoRole = Qt::UserRole + 100,
        BundlePathRole,
        DisplayNameRole,
        ThumbnailPathRole,
        IsStarredRole,
        IsPdfBasedRole,
        IsEdgelessRole,
        LastModifiedRole,  // QDateTime: last modification time (for card display)
        
        // Batch select mode roles (Qt::UserRole + 200 range, L-007)
        IsInSelectModeRole = Qt::UserRole + 200,
        IsSelectedInBatchRole,
        
        // Timeline-specific roles (Qt::UserRole + 300 range)
        IsSectionHeaderRole = Qt::UserRole + 300,
        SectionNameRole,
        LastAccessedRole,
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
    
    /**
     * @brief Refresh if the date has changed since last reload.
     * 
     * Call this when the view becomes visible to handle scenarios where:
     * - The system was asleep/hibernated during midnight
     * - The launcher was hidden for an extended period
     * 
     * @return True if a reload was triggered, false if data was still fresh.
     */
    bool refreshIfDateChanged();
    
    // -------------------------------------------------------------------------
    // Batch Select Mode (L-007)
    // -------------------------------------------------------------------------
    
    /**
     * @brief Set whether the view is in select mode.
     * @param selectMode True if in select mode.
     */
    void setSelectMode(bool selectMode);
    
    /**
     * @brief Check if the view is in select mode.
     */
    bool isSelectMode() const { return m_selectMode; }
    
    /**
     * @brief Set the selected bundle paths.
     * @param selectedPaths Set of bundle paths that are selected.
     */
    void setSelectedBundlePaths(const QSet<QString>& selectedPaths);
    
    /**
     * @brief Check if a bundle path is selected.
     * @param bundlePath The bundle path to check.
     */
    bool isSelected(const QString& bundlePath) const;

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
    
    /**
     * @brief Schedule timer to fire at next midnight for date rollover refresh.
     */
    void scheduleMidnightRefresh();

    // Internal item representation
    struct DisplayItem {
        bool isHeader = false;
        QString sectionName;
        NotebookInfo notebook;  // Only valid if !isHeader
    };
    
    QList<DisplayItem> m_items;
    
    // Date tracking for automatic refresh
    QTimer* m_midnightTimer = nullptr;  ///< Timer that fires at midnight
    QDate m_lastKnownDate;              ///< Date when sections were last computed
    
    // Batch select mode state (L-007)
    bool m_selectMode = false;
    QSet<QString> m_selectedBundlePaths;
};

#endif // TIMELINEMODEL_H

