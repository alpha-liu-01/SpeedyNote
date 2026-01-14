#ifndef SEARCHVIEW_H
#define SEARCHVIEW_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QList>

class NotebookCard;
struct NotebookInfo;

/**
 * @brief Search view for the Launcher.
 * 
 * Provides search functionality for notebooks by name and PDF filename.
 * 
 * Features:
 * - Search input with clear button
 * - Real-time search with 300ms debounce
 * - Grid of NotebookCard results
 * - "No results" message
 * - Keyboard-friendly: Enter to search, Escape to clear
 * - Touch-friendly scrolling
 * 
 * Search scope (per Q&A): Notebook names + PDF filenames
 * 
 * Phase P.3.6: Part of the new Launcher implementation.
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
     * @brief Emitted when a notebook card is long-pressed.
     */
    void notebookLongPressed(const QString& bundlePath);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchTriggered();
    void performSearch();

private:
    void setupUi();
    void setupTouchScrolling();
    void displayResults(const QList<NotebookInfo>& results);
    void clearResults();
    void showEmptyState(const QString& message);
    void updateSearchIcon();
    
    // Search bar
    QWidget* m_searchBar = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QPushButton* m_searchButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    
    // Results area
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QGridLayout* m_gridLayout = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    
    // Result cards
    QList<NotebookCard*> m_resultCards;
    
    // Debounce timer
    QTimer* m_debounceTimer = nullptr;
    
    QString m_lastQuery;
    bool m_darkMode = false;
    
    // Constants
    static constexpr int DEBOUNCE_MS = 300;
    static constexpr int GRID_COLUMNS = 4;
    static constexpr int GRID_SPACING = 12;
    static constexpr int SEARCH_BAR_HEIGHT = 44;
};

#endif // SEARCHVIEW_H

