#ifndef MARKDOWNNOTESSIDEBAR_H
#define MARKDOWNNOTESSIDEBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QList>
#include <QString>
#include "../text/MarkdownNoteEntry.h"

class MarkdownNotesSidebar : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownNotesSidebar(QWidget *parent = nullptr);
    ~MarkdownNotesSidebar();
    
    // Note management
    void removeNote(const QString &noteId);
    void clearNotes();
    
    /**
     * @brief Load notes for the current page from LinkObjects.
     * @param notes List of note display data (loaded from files).
     * 
     * Clears existing notes and creates entries for each note in the list.
     * Uses the NoteDisplayData format with LinkObject connection.
     */
    void loadNotesForPage(const QList<NoteDisplayData>& notes);
    
    // Find note by ID
    MarkdownNoteEntry* findNoteEntry(const QString &noteId);
    
    // Search functionality
    void setCurrentPageInfo(int currentPage, int totalPages);
    bool isInSearchMode() const { return searchMode; }
    
    /**
     * @brief Set edgeless mode (hides page range controls).
     * @param edgeless true for edgeless document, false for paged document.
     */
    void setEdgelessMode(bool edgeless);
    
    /**
     * @brief Show/hide warning about hidden tiles in edgeless mode.
     * @param hasHiddenTiles true if some tiles are not loaded (notes may be hidden).
     * @param loadedCount Number of tiles currently loaded.
     * @param totalCount Total number of tiles in the document.
     */
    void setHiddenTilesWarning(bool hasHiddenTiles, int loadedCount = 0, int totalCount = 0);
    
    void exitSearchMode();
    void onNewNoteCreated(); // Auto-exit search mode when a new note is created
    
    /**
     * @brief Display search results using NoteDisplayData.
     * @param results List of matching notes with display data.
     * 
     * Called by MainWindow after searchMarkdownNotes() completes.
     */
    void displaySearchResults(const QList<NoteDisplayData>& results);
    
    /**
     * @brief Scroll sidebar to show a specific note entry.
     * @param noteId The note UUID to scroll to.
     */
    void scrollToNote(const QString& noteId);
    
    /**
     * @brief Set a note entry to edit or preview mode.
     * @param noteId The note UUID.
     * @param editMode true for edit mode, false for preview mode.
     */
    void setNoteEditMode(const QString& noteId, bool editMode);
    
    /**
     * @brief Update the sidebar theme.
     * @param darkMode true for dark theme, false for light theme.
     */
    void setDarkMode(bool darkMode);

signals:
    // Signals for LinkObject-based notes
    void noteContentSaved(const QString& noteId, const QString& title, const QString& content);
    void noteDeletedWithLink(const QString& noteId, const QString& linkObjectId);
    void linkObjectClicked(const QString& linkObjectId);
    
    // Search signal
    void searchRequested(const QString& query, int fromPage, int toPage);
    
    // Emitted when exiting search mode to request notes reload
    void reloadNotesRequested();

private slots:
    void onNoteContentChanged(const QString &noteId);
    void onSearchButtonClicked();
    void onExitSearchClicked();
    void onSearchAllPagesToggled(bool checked);
    void onLinkObjectClicked(const QString& linkObjectId);
    void onNoteDeletedWithLink(const QString& noteId, const QString& linkObjectId);

private:
    void setupUI();
    void setupSearchUI();
    void applyStyle();
    void performSearch();
    void updateSearchRangeDefaults();
    
    // Main layout
    QVBoxLayout *mainLayout;
    
    // Search UI widgets
    QWidget *searchContainer;
    QVBoxLayout *searchLayout;
    QHBoxLayout *searchBarLayout;
    QLineEdit *searchInput;
    QPushButton *searchButton;
    QPushButton *exitSearchButton;
    QWidget *pageRangeContainer;  // Container for page range controls (hidden in edgeless mode)
    QHBoxLayout *pageRangeLayout;
    QLabel *pageRangeLabel;
    QSpinBox *fromPageSpinBox;
    QLabel *toLabel;
    QSpinBox *toPageSpinBox;
    QCheckBox *searchAllPagesCheckBox;
    QLabel *searchStatusLabel;
    
    // Notes display
    QScrollArea *scrollArea;
    QWidget *scrollContent;
    QVBoxLayout *scrollLayout;
    QLabel *emptyLabel;
    
    // M.7.2: Warning for hidden tiles in edgeless mode
    QLabel *hiddenTilesWarningLabel = nullptr;
    
    QList<MarkdownNoteEntry*> noteEntries;
    bool isDarkMode = false;
    bool isEdgeless = false;  // True when viewing edgeless document
    
    // Search state
    bool searchMode = false;
    QString lastSearchQuery;
    int currentPage = 0;
    int totalPages = 1;
};

#endif // MARKDOWNNOTESSIDEBAR_H

