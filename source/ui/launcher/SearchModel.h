#ifndef SEARCHMODEL_H
#define SEARCHMODEL_H

#include <QAbstractListModel>
#include <QList>
#include "../../core/NotebookLibrary.h"

/**
 * @brief Data model for search results in SearchView.
 * 
 * SearchModel provides a simple flat list model for displaying notebook
 * search results. It works with NotebookCardDelegate for rendering.
 * 
 * This is a simpler model compared to StarredModel (no folders/sections).
 * Results are set externally via setResults() after performing a search.
 * 
 * Data roles match NotebookCardDelegate::DataRoles for seamless integration.
 * 
 * Usage:
 * @code
 * SearchModel* model = new SearchModel(this);
 * QList<NotebookInfo> results = NotebookLibrary::instance()->search(query);
 * model->setResults(results);
 * @endcode
 * 
 * Phase P.3 Performance Optimization: Part of Model/View refactor.
 */
class SearchModel : public QAbstractListModel {
    Q_OBJECT

public:
    /**
     * @brief Data roles for this model.
     * 
     * These match NotebookCardDelegate::DataRoles for compatibility.
     */
    enum Roles {
        NotebookInfoRole = Qt::UserRole + 100,  // QVariant containing NotebookInfo
        BundlePathRole,                          // QString: path to notebook bundle
        DisplayNameRole,                         // QString: notebook display name
        ThumbnailPathRole,                       // QString: path to thumbnail file
        IsStarredRole,                           // bool: whether notebook is starred
        IsPdfBasedRole,                          // bool: whether notebook is PDF-based
        IsEdgelessRole,                          // bool: whether notebook is edgeless
    };

    explicit SearchModel(QObject* parent = nullptr);
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    /**
     * @brief Set the search results to display.
     * @param results List of NotebookInfo from NotebookLibrary::search().
     * 
     * This replaces any existing results and triggers a model reset.
     */
    void setResults(const QList<NotebookInfo>& results);
    
    /**
     * @brief Clear all results.
     * 
     * Equivalent to setResults({}).
     */
    void clear();
    
    /**
     * @brief Get the number of results.
     */
    int resultCount() const { return m_results.size(); }
    
    /**
     * @brief Check if the model has any results.
     */
    bool isEmpty() const { return m_results.isEmpty(); }
    
    /**
     * @brief Get the NotebookInfo at a specific index.
     * @param index The model index.
     * @return The NotebookInfo, or an invalid one if index is out of range.
     */
    NotebookInfo notebookAt(const QModelIndex& index) const;
    
    /**
     * @brief Get the bundle path at a specific index.
     * @param index The model index.
     * @return The bundle path, or empty string if index is out of range.
     */
    QString bundlePathAt(const QModelIndex& index) const;

private:
    QList<NotebookInfo> m_results;
};

#endif // SEARCHMODEL_H
