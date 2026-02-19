#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QPropertyAnimation>
#include <QFrame>

#include "../../batch/BatchOperations.h"  // For BatchOps::BatchResult

class LauncherNavButton;
class TimelineModel;
class TimelineDelegate;
class TimelineListView;
class StarredView;
class SearchView;
class FloatingActionButton;
class ExportProgressWidget;

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

    /**
     * @brief Import .snbx package files (used by iOS QFileOpenEvent handler).
     */
    void importFiles(const QStringList& snbxFiles);

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

    /**
     * @brief Emitted when user presses Escape to return to MainWindow.
     * MainWindow should check if there are open tabs before toggling.
     */
    void returnToMainWindowRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Drag-drop support for desktop (Step 3.10)
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
#endif

private:
    void setupUi();
    void setupNavigation();
    void setupTimeline();
    void setupStarred();
    void setupSearch();
    void setupFAB();
    void applyStyle();
    void updateNavigationState();
    bool isDarkMode() const;
    void setNavigationCompact(bool compact);
    void onTimelineItemClicked(const QModelIndex& index);
    void showNotebookContextMenu(const QString& bundlePath, const QPoint& globalPos);
    void showFolderContextMenu(const QString& folderName, const QPoint& globalPos);
    bool deleteNotebooks(const QStringList& bundlePaths);
    void toggleNotebookStar(const QString& bundlePath);
    void renameNotebook(const QString& bundlePath);
    void duplicateNotebook(const QString& bundlePath);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    void showInFileManager(const QString& bundlePath);
#endif
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString findImportedPdfPath(const QString& bundlePath);
#endif
    
    // === Timeline Select Mode (L-007) ===
    void setupTimelineSelectModeHeader();
    void showTimelineSelectModeHeader(int count);
    void hideTimelineSelectModeHeader();
    void showTimelineOverflowMenu();
    void updateTimelineHeaderButtonIcons();
    void onTimelineSelectModeChanged(bool active);
    void onTimelineBatchSelectionChanged(int count);
    void onTimelineLongPressed(const QModelIndex& index, const QPoint& globalPos);
    
    // === Batch Export Helpers (Phase 3) ===
    void showPdfExportDialog(const QStringList& bundlePaths);
    void showSnbxExportDialog(const QStringList& bundlePaths);
    
    // === Export Progress (Phase 3) ===
    void setupExportProgress();
    void onExportProgress(const QString& currentFile, int current, int total, int queuedJobs);
    void onExportJobComplete(const BatchOps::BatchResult& result, const QString& outputDir);
    void onExportDetailsRequested();
    
    // === Batch Import (Phase 3) ===
    void performBatchImport(const QStringList& snbxFiles, const QString& destDir = QString());
    
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
    
    // Navigation sidebar
    QWidget* m_navSidebar = nullptr;
    LauncherNavButton* m_returnBtn = nullptr;
    LauncherNavButton* m_timelineBtn = nullptr;
    LauncherNavButton* m_starredBtn = nullptr;
    LauncherNavButton* m_searchBtn = nullptr;
    
    // Timeline view
    QWidget* m_timelineView = nullptr;
    TimelineListView* m_timelineList = nullptr;
    TimelineModel* m_timelineModel = nullptr;
    TimelineDelegate* m_timelineDelegate = nullptr;  // For section headers only
    
    // Timeline select mode header (L-007)
    QWidget* m_timelineSelectModeHeader = nullptr;
    QLabel* m_timelineSelectionCountLabel = nullptr;
    QPushButton* m_timelineBackButton = nullptr;
    QPushButton* m_timelineOverflowMenuButton = nullptr;
    
    // Starred view
    StarredView* m_starredView = nullptr;
    
    // Search view
    SearchView* m_searchView = nullptr;
    
    // FAB (Floating Action Button)
    FloatingActionButton* m_fab = nullptr;
    
    // Export progress widget (Phase 3)
    ExportProgressWidget* m_exportProgressWidget = nullptr;
    BatchOps::BatchResult m_lastExportResult;  // Stored for "Details" dialog
    QString m_lastExportOutputDir;
    
    // Animation
    QPropertyAnimation* m_fadeAnimation = nullptr;
    qreal m_fadeOpacity = 1.0;
    
    View m_currentView = View::Timeline;
};

#endif // LAUNCHER_H

