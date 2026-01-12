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
#include <functional>
#include "MarkdownNoteEntry.h"

class MarkdownNotesSidebar : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownNotesSidebar(QWidget *parent = nullptr);
    ~MarkdownNotesSidebar();
    
    // Note management
    void addNote(const MarkdownNoteData &data);
    void removeNote(const QString &noteId);
    void updateNote(const MarkdownNoteData &data);
    void clearNotes();
    
    // Load notes for specific page(s)
    void loadNotesForPages(const QList<MarkdownNoteData> &notes);
    
    // Get all current notes
    QList<MarkdownNoteData> getAllNotes() const;
    
    // Find note by ID
    MarkdownNoteEntry* findNoteEntry(const QString &noteId);
    
    // Search functionality
    void setNoteProvider(std::function<QList<MarkdownNoteData>()> provider);
    void setCurrentPageInfo(int currentPage, int totalPages);
    bool isInSearchMode() const { return searchMode; }
    void exitSearchMode();
    void onNewNoteCreated(); // Auto-exit search mode when a new note is created

signals:
    void noteContentChanged(const QString &noteId, const MarkdownNoteData &data);
    void noteDeleted(const QString &noteId);
    void highlightLinkClicked(const QString &highlightId);

private slots:
    void onNoteContentChanged(const QString &noteId);
    void onNoteDeleted(const QString &noteId);
    void onHighlightLinkClicked(const QString &highlightId);
    void onSearchButtonClicked();
    void onExitSearchClicked();
    void onSearchAllPagesToggled(bool checked);

private:
    void setupUI();
    void setupSearchUI();
    void applyStyle();
    void performSearch();
    void updateSearchRangeDefaults();
    void displaySearchResults(const QList<MarkdownNoteData> &results);
    
    // Main layout
    QVBoxLayout *mainLayout;
    
    // Search UI widgets
    QWidget *searchContainer;
    QVBoxLayout *searchLayout;
    QHBoxLayout *searchBarLayout;
    QLineEdit *searchInput;
    QPushButton *searchButton;
    QPushButton *exitSearchButton;
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
    
    QList<MarkdownNoteEntry*> noteEntries;
    bool isDarkMode = false;
    
    // Search state
    bool searchMode = false;
    QString lastSearchQuery;
    std::function<QList<MarkdownNoteData>()> noteProvider;
    int currentPage = 0;
    int totalPages = 1;
    QList<MarkdownNoteData> normalModeNotes; // Store notes to restore after exiting search
};

#endif // MARKDOWNNOTESSIDEBAR_H

