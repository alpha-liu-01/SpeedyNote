#ifndef SEARCHVIEW_H
#define SEARCHVIEW_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

class SearchListView;
class SearchModel;
class NotebookCardDelegate;
struct NotebookInfo;

/**
 * @brief Search view for the Launcher.
 * 
 * Provides search functionality for notebooks by name and PDF filename.
 * 
 * Features:
 * - Search input with clear button
 * - Real-time search with 300ms debounce
 * - Virtualized grid of notebook cards (Model/View)
 * - "No results" message
 * - Keyboard-friendly: Enter to search, Escape to clear
 * - Touch-friendly scrolling with kinetic momentum
 * 
 * Search scope (per Q&A): Notebook names + PDF filenames
 * 
 * Phase P.3: Refactored to use Model/View for virtualization and performance.
 */
class SearchView : public QWidget {
    Q_OBJECT

public:
    explicit SearchView(QWidget* parent = nullptr);
    
    /**
     * @brief Set dark mode for theming.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Clear the search input and results.
     */
    void clearSearch();
    
    /**
     * @brief Focus the search input.
     */
    void focusSearchInput();

signals:
    /**
     * @brief Emitted when a notebook card is clicked.
     */
    void notebookClicked(const QString& bundlePath);
    
    /**
     * @brief Emitted when the 3-dot menu button on a notebook card is clicked,
     * or when a notebook card is right-clicked or long-pressed.
     */
    void notebookMenuRequested(const QString& bundlePath);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchTriggered();
    void performSearch();
    
    // Slots for list view signals
    void onNotebookClicked(const QString& bundlePath);
    void onNotebookMenuRequested(const QString& bundlePath, const QPoint& globalPos);

private:
    void setupUi();
    void showEmptyState(const QString& message);
    void showResults();
    void updateSearchIcon();
    
    // Search bar
    QWidget* m_searchBar = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QPushButton* m_searchButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    
    // Status and empty labels
    QLabel* m_statusLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    
    // Model/View components (virtualized rendering)
    SearchListView* m_listView = nullptr;
    SearchModel* m_model = nullptr;
    NotebookCardDelegate* m_delegate = nullptr;
    
    // Debounce timer
    QTimer* m_debounceTimer = nullptr;
    
    QString m_lastQuery;
    bool m_darkMode = false;
    
    // Constants
    static constexpr int DEBOUNCE_MS = 300;
    static constexpr int GRID_SPACING = 12;
    static constexpr int SEARCH_BAR_HEIGHT = 44;
};

#endif // SEARCHVIEW_H
