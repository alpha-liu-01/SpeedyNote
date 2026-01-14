#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QPropertyAnimation>

class NotebookListModel;
class NotebookItemDelegate;

/**
 * @brief The main launcher window for SpeedyNote.
 * 
 * Launcher provides a touch-first interface for:
 * - Viewing recent notebooks (Timeline view)
 * - Managing starred notebooks (Starred view)
 * - Searching notebooks by name or PDF filename
 * - Creating new notebooks (via FAB)
 * - Opening existing notebooks
 * 
 * The launcher is modal to MainWindow and uses smooth animations
 * for show/hide transitions.
 * 
 * Phase P.3: New launcher implementation replacing old LauncherWindow.
 */
class Launcher : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)

public:
    explicit Launcher(QWidget* parent = nullptr);
    ~Launcher() override;
    
    /**
     * @brief Show the launcher with a fade-in animation.
     */
    void showWithAnimation();
    
    /**
     * @brief Hide the launcher with a fade-out animation.
     */
    void hideWithAnimation();
    
    /**
     * @brief Get the current fade opacity (for animation).
     */
    qreal fadeOpacity() const { return m_fadeOpacity; }
    
    /**
     * @brief Set the fade opacity (for animation).
     */
    void setFadeOpacity(qreal opacity);

signals:
    /**
     * @brief Emitted when a notebook is selected from the list.
     * @param bundlePath Full path to the .snb bundle.
     */
    void notebookSelected(const QString& bundlePath);
    
    /**
     * @brief Emitted when user requests a new edgeless document.
     */
    void createNewEdgeless();
    
    /**
     * @brief Emitted when user requests a new paged document.
     */
    void createNewPaged();
    
    /**
     * @brief Emitted when user wants to open a PDF for annotation.
     */
    void openPdfRequested();
    
    /**
     * @brief Emitted when user wants to open an existing .snb notebook.
     */
    void openNotebookRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUi();
    void setupTimeline();
    void setupStarred();
    void setupSearch();
    void setupFAB();
    void setupConnections();
    void applyStyle();
    
    // === View Management ===
    enum class View {
        Timeline,
        Starred,
        Search
    };
    void switchToView(View view);
    
    // === UI Components ===
    QWidget* m_centralWidget = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    
    // Navigation tabs
    QPushButton* m_timelineTab = nullptr;
    QPushButton* m_starredTab = nullptr;
    
    // Timeline view
    QWidget* m_timelineView = nullptr;
    QListView* m_timelineList = nullptr;
    
    // Starred view
    QWidget* m_starredView = nullptr;
    QListView* m_starredList = nullptr;
    
    // Search
    QLineEdit* m_searchInput = nullptr;
    QWidget* m_searchResultsView = nullptr;
    QListView* m_searchResultsList = nullptr;
    
    // FAB (Floating Action Button)
    QPushButton* m_fabButton = nullptr;
    QWidget* m_fabMenu = nullptr;
    QPushButton* m_fabEdgelessBtn = nullptr;
    QPushButton* m_fabPagedBtn = nullptr;
    QPushButton* m_fabPdfBtn = nullptr;
    QPushButton* m_fabOpenBtn = nullptr;
    bool m_fabExpanded = false;
    
    // Animation
    QPropertyAnimation* m_fadeAnimation = nullptr;
    qreal m_fadeOpacity = 1.0;
    
    // Models (will be implemented in later tasks)
    // NotebookListModel* m_timelineModel = nullptr;
    // NotebookListModel* m_starredModel = nullptr;
    // NotebookListModel* m_searchModel = nullptr;
    // NotebookItemDelegate* m_itemDelegate = nullptr;
    
    View m_currentView = View::Timeline;
};

#endif // LAUNCHER_H

