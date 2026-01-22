#pragma once

// ============================================================================
// PdfSearchBar - Search bar widget for PDF text search
// ============================================================================
// Displays at the bottom of the viewport for searching PDF text.
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
 * Layout: [X] Find: [input____] [Status] [▼Next] [▲Prev] [⚙Options]
 * 
 * The search bar is shown at the bottom of the viewport when Ctrl+F is pressed
 * on a PDF document. It emits signals when the user requests next/previous
 * match navigation.
 */
class PdfSearchBar : public QWidget {
    Q_OBJECT
    
public:
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
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    
private slots:
    void onNextClicked();
    void onPrevClicked();
    void onCloseClicked();
    
private:
    void setupUi();
    void updateIcons();
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

