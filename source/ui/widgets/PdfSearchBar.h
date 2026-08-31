#pragma once

// ============================================================================
// PdfSearchBar - Search bar widget for PDF text search
// ============================================================================
// Floats as a compact card in the top-right corner of the viewport, the way
// editors place Find. MainWindow positions it; see updatePdfSearchBarPosition.
// Features: case-sensitive toggle, whole-word toggle, next/prev navigation.
// ============================================================================

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QAction>

/**
 * @brief Search bar widget for PDF text search.
 * 
 * Layout: [X] [input____] [Status] [▼] [▲] [⚙]
 * 
 * The search bar is shown in the viewport's top-right corner when Ctrl+F is
 * pressed on a PDF document. It emits signals when the user requests
 * next/previous match navigation.
 */
class PdfSearchBar : public QWidget {
    Q_OBJECT
    
public:
    /**
     * @brief Width the bar asks for when the viewport has room for it.
     *
     * Fixed rather than derived from the layout so the input does not jump
     * narrower the moment the match counter appears beside it.
     */
    static constexpr int PREFERRED_WIDTH = 340;

    /**
     * @brief Space held for the match counter, occupied whether or not a count
     *        is currently showing.
     */
    static constexpr int STATUS_WIDTH = 76;

    explicit PdfSearchBar(QWidget *parent = nullptr);
    ~PdfSearchBar() override;
    
    /**
     * @brief Get the current search text.
     */
    QString searchText() const;
    
    /**
     * @brief Check if case-sensitive matching is enabled.
     */
    bool caseSensitive() const;
    
    /**
     * @brief Check if whole-word matching is enabled.
     */
    bool wholeWord() const;
    
    /**
     * @brief Set status text (e.g., "No results found").
     */
    void setStatus(const QString& status);
    
    /**
     * @brief Clear status text.
     */
    void clearStatus();
    
    /**
     * @brief Show the search bar and focus the input.
     */
    void showAndFocus();
    
    /**
     * @brief Set dark mode for icon switching.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
signals:
    /**
     * @brief Emitted when user requests next match.
     * @param text Search text.
     * @param caseSensitive Case-sensitive matching.
     * @param wholeWord Whole-word matching.
     */
    void searchNextRequested(const QString& text, bool caseSensitive, bool wholeWord);
    
    /**
     * @brief Emitted when user requests previous match.
     * @param text Search text.
     * @param caseSensitive Case-sensitive matching.
     * @param wholeWord Whole-word matching.
     */
    void searchPrevRequested(const QString& text, bool caseSensitive, bool wholeWord);
    
    /**
     * @brief Emitted when the search bar is closed.
     */
    void closed();

    /**
     * @brief Emitted when the query text or match options change (SBS2).
     * @param text Current search text.
     *
     * Drives the debounced whole-document scan / live match count. Distinct
     * from searchNextRequested/searchPrevRequested, which stay the explicit
     * jump interaction.
     */
    void searchTextChanged(const QString& text);
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    
private slots:
    void onNextClicked();
    void onPrevClicked();
    void onCloseClicked();
    
private:
    void setupUi();
    void updateIcons();
    /// Repaint the floating-card background and border for the current theme.
    void applyCardStyle();
    bool isDarkMode() const;
    
    // UI components
    QLineEdit *m_searchInput = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_optionsButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    
    // Options menu
    QMenu *m_optionsMenu = nullptr;
    QAction *m_caseSensitiveAction = nullptr;
    QAction *m_wholeWordAction = nullptr;
    
    // State
    bool m_darkMode = false;
};

