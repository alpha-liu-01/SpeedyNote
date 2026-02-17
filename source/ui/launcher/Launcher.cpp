#include "Launcher.h"
#include "LauncherNavButton.h"
#include "TimelineModel.h"
#include "TimelineDelegate.h"
#include "TimelineListView.h"
#include "NotebookCardDelegate.h"
#include "StarredView.h"
#include "SearchView.h"
#include "FloatingActionButton.h"
#include "FolderPickerDialog.h"
#include "../ThemeColors.h"
#include "../dialogs/BatchPdfExportDialog.h"
#include "../dialogs/BatchSnbxExportDialog.h"
#include "../dialogs/ExportResultsDialog.h"
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include "../dialogs/BatchImportDialog.h"
#endif
#include "../widgets/ExportProgressWidget.h"
#include "../../MainWindow.h"
#include "../../core/NotebookLibrary.h"
#include "../../core/Document.h"
#include "../../batch/ExportQueueManager.h"
#include "../../android/AndroidShareHelper.h"
#include "../../platform/SystemNotification.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QGraphicsOpacityEffect>
#include <QFile>
#include <QFileDialog>
#include <QApplication>
#include <QScrollBar>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QCursor>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QEventLoop>
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#endif

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#include <jni.h>
#elif defined(Q_OS_IOS)
#include "ios/IOSShareHelper.h"
#endif

// ============================================================================
// Android Package Picker (JNI Integration)
// ============================================================================
#ifdef Q_OS_ANDROID

namespace {
    // Static variables for async package picker result
    static QStringList s_pickedPackagePaths;  // Supports multi-file selection
    static bool s_packagePickerCancelled = false;
    static QEventLoop* s_packagePickerLoop = nullptr;
}

// JNI callback: Called from Java when a single package file is picked and copied
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_ImportHelper_onPackageFilePicked(JNIEnv *env, jclass /*clazz*/, jstring localPath)
{
    const char* pathChars = env->GetStringUTFChars(localPath, nullptr);
    s_pickedPackagePaths.clear();
    s_pickedPackagePaths.append(QString::fromUtf8(pathChars));
    env->ReleaseStringUTFChars(localPath, pathChars);
    
    s_packagePickerCancelled = false;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "JNI callback: Package picked -" << s_pickedPackagePaths;
#endif
    
    if (s_packagePickerLoop) {
        s_packagePickerLoop->quit();
    }
}

// JNI callback: Called from Java when multiple package files are picked and copied
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_ImportHelper_onPackageFilesPicked(JNIEnv *env, jclass /*clazz*/, jobjectArray localPaths)
{
    s_pickedPackagePaths.clear();
    
    int count = env->GetArrayLength(localPaths);
    for (int i = 0; i < count; i++) {
        jstring jPath = (jstring)env->GetObjectArrayElement(localPaths, i);
        const char* pathChars = env->GetStringUTFChars(jPath, nullptr);
        s_pickedPackagePaths.append(QString::fromUtf8(pathChars));
        env->ReleaseStringUTFChars(jPath, pathChars);
        env->DeleteLocalRef(jPath);
    }
    
    s_packagePickerCancelled = false;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "JNI callback: Multiple packages picked -" << s_pickedPackagePaths.size() << "files";
#endif
    
    if (s_packagePickerLoop) {
        s_packagePickerLoop->quit();
    }
}

// JNI callback: Called from Java when package picking is cancelled or fails
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_ImportHelper_onPackagePickCancelled(JNIEnv * /*env*/, jclass /*clazz*/)
{
    s_pickedPackagePaths.clear();
    s_packagePickerCancelled = true;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "JNI callback: Package pick cancelled";
#endif
    
    if (s_packagePickerLoop) {
        s_packagePickerLoop->quit();
    }
}

// Helper function to pick .snbx package file(s) on Android via SAF
// Supports multi-file selection (Phase 3: Batch Import)
static QStringList pickSnbxFilesAndroid()
{
    // Reset state
    s_pickedPackagePaths.clear();
    s_packagePickerCancelled = false;
    
    // Get the destination directory for imported packages
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/imports";
    QDir().mkpath(destDir);
    
    // Clean up any leftover files from previous failed/interrupted imports
    // This prevents disk space leaks from accumulated import failures
    QDir importsDir(destDir);
    for (const QString& entry : importsDir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
        QString filePath = importsDir.absoluteFilePath(entry);
        QFile::remove(filePath);
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "pickSnbxFilesAndroid: Cleaned up old import:" << filePath;
#endif
    }
    
    // Get the Activity
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "pickSnbxFilesAndroid: Failed to get Android context";
        return QStringList();
    }
    
    // Call ImportHelper.pickPackageFile(activity, destDir)
    // Java side now enables EXTRA_ALLOW_MULTIPLE for multi-file selection
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/ImportHelper",
        "pickPackageFile",
        "(Landroid/app/Activity;Ljava/lang/String;)V",
        activity.object<jobject>(),
        QJniObject::fromString(destDir).object<jstring>()
    );
    
    // Wait for the result (file picker is async)
    QEventLoop loop;
    s_packagePickerLoop = &loop;
    loop.exec();
    s_packagePickerLoop = nullptr;
    
    if (s_packagePickerCancelled || s_pickedPackagePaths.isEmpty()) {
        return QStringList();
    }
    
    return s_pickedPackagePaths;
}

#endif // Q_OS_ANDROID

Launcher::Launcher(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SpeedyNote"));
    // Minimum size: 640x480 allows compact sidebar (60px) + content area (580px)
    // This supports screens as small as 1024x640 @ 125% DPI (= 820x512 logical)
    // with room for window chrome and taskbar
    setMinimumSize(560, 480);
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Enable drag-drop for desktop notebook import (Step 3.10)
    setAcceptDrops(true);
#endif
    
    setupUi();
    applyStyle();
}

Launcher::~Launcher()
{
}

void Launcher::setupUi()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // Main horizontal layout: Navigation sidebar | Content area
    auto* mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Navigation sidebar
    setupNavigation();
    mainLayout->addWidget(m_navSidebar);
    
    // Content area with content stack
    auto* contentArea = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    
    // Content stack
    m_contentStack = new QStackedWidget(this);
    contentLayout->addWidget(m_contentStack);
    
    // Add views to stack
    m_timelineView = new QWidget(this);
    m_timelineView->setObjectName("TimelineView");
    m_starredView = new StarredView(this);
    m_starredView->setObjectName("StarredViewWidget");
    m_searchView = new SearchView(this);
    m_searchView->setObjectName("SearchViewWidget");
    
    m_contentStack->addWidget(m_timelineView);
    m_contentStack->addWidget(m_starredView);
    m_contentStack->addWidget(m_searchView);
    
    mainLayout->addWidget(contentArea, 1); // Content area stretches
    
    // Setup view content (after views are created)
    setupTimeline();
    setupStarred();
    setupSearch();
    
    // FAB
    setupFAB();
    
    // Export progress widget (Phase 3)
    setupExportProgress();
    
    // Fade animation
    m_fadeAnimation = new QPropertyAnimation(this, "fadeOpacity", this);
    m_fadeAnimation->setDuration(200);
    
    // Set initial view
    switchToView(View::Timeline);
}

void Launcher::setupNavigation()
{
    m_navSidebar = new QWidget(this);
    m_navSidebar->setObjectName("LauncherNavSidebar");
    m_navSidebar->setFixedWidth(LauncherNavButton::EXPANDED_WIDTH + 16); // Button width + margins
    
    auto* navLayout = new QVBoxLayout(m_navSidebar);
    navLayout->setContentsMargins(8, 8, 8, 8);
    navLayout->setSpacing(8);
    
    // Return button (only visible if MainWindow exists)
    m_returnBtn = new LauncherNavButton(m_navSidebar);
    m_returnBtn->setIconName("recent");  // TODO: Replace with actual icon name
    m_returnBtn->setText(tr("Return"));
    m_returnBtn->setCheckable(false);
    navLayout->addWidget(m_returnBtn);
    
    // Check if MainWindow exists and show/hide return button
    bool hasMainWindow = (MainWindow::findExistingMainWindow() != nullptr);
    m_returnBtn->setVisible(hasMainWindow);
    
    // Separator
    auto* separator = new QFrame(m_navSidebar);
    separator->setFrameShape(QFrame::HLine);
    separator->setObjectName("LauncherNavSeparator");
    separator->setFixedHeight(1);
    navLayout->addWidget(separator);
    
    // Timeline button
    m_timelineBtn = new LauncherNavButton(m_navSidebar);
    m_timelineBtn->setIconName("layer_uparrow");  // TODO: Replace with actual icon name
    m_timelineBtn->setText(tr("Timeline"));
    m_timelineBtn->setCheckable(true);
    navLayout->addWidget(m_timelineBtn);
    
    // Starred button
    m_starredBtn = new LauncherNavButton(m_navSidebar);
    m_starredBtn->setIconName("star");  // TODO: Replace with actual icon name
    m_starredBtn->setText(tr("Starred"));
    m_starredBtn->setCheckable(true);
    navLayout->addWidget(m_starredBtn);
    
    // Search button
    m_searchBtn = new LauncherNavButton(m_navSidebar);
    m_searchBtn->setIconName("zoom");  // Uses existing zoom icon
    m_searchBtn->setText(tr("Search"));
    m_searchBtn->setCheckable(true);
    navLayout->addWidget(m_searchBtn);
    
    // Spacer to push buttons to top
    navLayout->addStretch();
    
    // Connect navigation buttons
    connect(m_returnBtn, &LauncherNavButton::clicked, this, [this]() {
        // Find and show the existing MainWindow before hiding the Launcher
        MainWindow* mainWindow = MainWindow::findExistingMainWindow();
        if (mainWindow) {
            // Transfer window geometry for seamless transition
            mainWindow->move(pos());
            mainWindow->resize(size());
            if (isMaximized()) {
                mainWindow->showMaximized();
            } else if (isFullScreen()) {
                mainWindow->showFullScreen();
            } else {
                mainWindow->showNormal();
            }
            mainWindow->raise();
            mainWindow->activateWindow();
        }
        hideWithAnimation();
    });
    
    connect(m_timelineBtn, &LauncherNavButton::clicked, this, [this]() {
        switchToView(View::Timeline);
    });
    
    connect(m_starredBtn, &LauncherNavButton::clicked, this, [this]() {
        switchToView(View::Starred);
    });
    
    connect(m_searchBtn, &LauncherNavButton::clicked, this, [this]() {
        switchToView(View::Search);
    });
}

// ============================================================================
// CompositeTimelineDelegate - Local delegate for timeline (headers + cards)
// ============================================================================

/**
 * @brief Composite delegate that handles both section headers and notebook cards.
 * 
 * Uses TimelineDelegate for section headers (Today, Yesterday, etc.) and
 * NotebookCardDelegate for notebook cards in a grid layout.
 * 
 * For section headers, returns a wide sizeHint so they span the full viewport
 * width, forcing them onto their own row in IconMode.
 */
class CompositeTimelineDelegate : public QStyledItemDelegate {
public:
    CompositeTimelineDelegate(NotebookCardDelegate* cardDelegate,
                              TimelineDelegate* headerDelegate,
                              QListView* listView,
                              QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_cardDelegate(cardDelegate)
        , m_headerDelegate(headerDelegate)
        , m_listView(listView)
    {
    }
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
        
        if (isHeader) {
            m_headerDelegate->paint(painter, option, index);
        } else {
            m_cardDelegate->paint(painter, option, index);
        }
    }
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
        
        if (isHeader) {
            // Section headers should span the full viewport width
            // This forces them onto their own row in IconMode
            QSize baseSize = m_headerDelegate->sizeHint(option, index);
            int viewportWidth = m_listView ? m_listView->viewport()->width() : 600;
            // Subtract spacing to account for IconMode margins
            int headerWidth = qMax(viewportWidth - 24, baseSize.width());
            return QSize(headerWidth, baseSize.height());
        } else {
            return m_cardDelegate->sizeHint(option, index);
        }
    }
    
    void setDarkMode(bool dark)
    {
        m_cardDelegate->setDarkMode(dark);
        m_headerDelegate->setDarkMode(dark);
    }

private:
    NotebookCardDelegate* m_cardDelegate;
    TimelineDelegate* m_headerDelegate;
    QListView* m_listView;
};

void Launcher::setupTimeline()
{
    // Create layout for timeline view
    auto* layout = new QVBoxLayout(m_timelineView);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);
    
    // Setup select mode header (L-007) - initially hidden
    setupTimelineSelectModeHeader();
    layout->addWidget(m_timelineSelectModeHeader);
    m_timelineSelectModeHeader->setVisible(false);
    
    // Create model
    m_timelineModel = new TimelineModel(this);
    
    // Create delegates
    auto* cardDelegate = new NotebookCardDelegate(this);
    m_timelineDelegate = new TimelineDelegate(this);  // Used for section headers only
    
    // CR-P.1: Connect thumbnailUpdated to invalidate card delegate's cache
    connect(NotebookLibrary::instance(), &NotebookLibrary::thumbnailUpdated,
            cardDelegate, &NotebookCardDelegate::invalidateThumbnail);
    
    cardDelegate->setDarkMode(isDarkMode());
    m_timelineDelegate->setDarkMode(isDarkMode());
    
    // Create list view (configured in constructor for IconMode grid layout)
    m_timelineList = new TimelineListView(m_timelineView);
    m_timelineList->setObjectName("TimelineList");
    m_timelineList->setTimelineModel(m_timelineModel);
    
    // Create composite delegate that handles both item types
    auto* compositeDelegate = new CompositeTimelineDelegate(
        cardDelegate, m_timelineDelegate, m_timelineList, this);
    m_timelineList->setItemDelegate(compositeDelegate);
    
    // Connect click
    connect(m_timelineList, &QListView::clicked,
            this, &Launcher::onTimelineItemClicked);
    
    // 3-dot menu button or right-click shows context menu (only when NOT in select mode)
    // TimelineListView handles all input and emits menuRequested
    connect(m_timelineList, &TimelineListView::menuRequested,
            this, [this](const QModelIndex& index, const QPoint& globalPos) {
        if (!index.isValid()) return;
        
        QString bundlePath = index.data(TimelineModel::BundlePathRole).toString();
        if (!bundlePath.isEmpty()) {
            showNotebookContextMenu(bundlePath, globalPos);
        }
    });
    
    // Long-press enters batch select mode (L-007)
    connect(m_timelineList, &TimelineListView::longPressed,
            this, &Launcher::onTimelineLongPressed);
    
    // Connect select mode signals (L-007)
    connect(m_timelineList, &TimelineListView::selectModeChanged,
            this, &Launcher::onTimelineSelectModeChanged);
    connect(m_timelineList, &TimelineListView::batchSelectionChanged,
            this, &Launcher::onTimelineBatchSelectionChanged);
    
    layout->addWidget(m_timelineList);
}

void Launcher::setupStarred()
{
    m_starredView->setDarkMode(isDarkMode());
    
    // Connect signals
    connect(m_starredView, &StarredView::notebookClicked, this, [this](const QString& bundlePath) {
        emit notebookSelected(bundlePath);
    });
    
    // 3-dot menu button, right-click, or long-press shows context menu
    connect(m_starredView, &StarredView::notebookMenuRequested, this, [this](const QString& bundlePath) {
        showNotebookContextMenu(bundlePath, QCursor::pos());
    });
    
    // TODO (L-007): notebookLongPressed will enter batch select mode
    // For now, it's handled by notebookMenuRequested
    
    connect(m_starredView, &StarredView::folderLongPressed, this, [this](const QString& folderName) {
        showFolderContextMenu(folderName, QCursor::pos());
    });
    
    // Export signals (Phase 3: Batch Operations)
    connect(m_starredView, &StarredView::exportToPdfRequested, 
            this, &Launcher::showPdfExportDialog);
    connect(m_starredView, &StarredView::exportToSnbxRequested, 
            this, &Launcher::showSnbxExportDialog);
    
    // Delete signal (L-010: Batch Delete)
    // Use lambda so we can conditionally exit select mode based on whether
    // the user confirmed the deletion dialog.
    connect(m_starredView, &StarredView::deleteNotebooksRequested,
            this, [this](const QStringList& paths) {
        if (deleteNotebooks(paths)) {
            m_starredView->exitSelectMode();
        }
    });
}

void Launcher::setupSearch()
{
    m_searchView->setDarkMode(isDarkMode());
    
    // Connect signals
    connect(m_searchView, &SearchView::notebookClicked, this, [this](const QString& bundlePath) {
        emit notebookSelected(bundlePath);
    });
    
    // 3-dot menu button, right-click, or long-press shows context menu
    connect(m_searchView, &SearchView::notebookMenuRequested, this, [this](const QString& bundlePath) {
        showNotebookContextMenu(bundlePath, QCursor::pos());
    });
    
    // L-009: Folder clicked in search results → navigate to StarredView
    connect(m_searchView, &SearchView::folderClicked, this, [this](const QString& folderName) {
        switchToView(View::Starred);
        m_starredView->scrollToFolder(folderName);
    });
}

void Launcher::setupFAB()
{
    // Create FAB on central widget so it overlays content
    m_fab = new FloatingActionButton(m_centralWidget);
    
    m_fab->setDarkMode(isDarkMode());
    
    // Position in bottom-right
    m_fab->positionInParent();
    m_fab->raise();  // Ensure it's above other widgets
    m_fab->show();
    
    // Connect signals
    connect(m_fab, &FloatingActionButton::createEdgeless, this, &Launcher::createNewEdgeless);
    connect(m_fab, &FloatingActionButton::createPaged, this, &Launcher::createNewPaged);
    connect(m_fab, &FloatingActionButton::openPdf, this, &Launcher::openPdfRequested);
    connect(m_fab, &FloatingActionButton::openNotebook, this, &Launcher::openNotebookRequested);
    
    // Import package - platform-specific handling (Phase 3: Batch Import)
    connect(m_fab, &FloatingActionButton::importPackage, this, [this]() {
#ifdef Q_OS_ANDROID
        // Android: Use ImportHelper to pick .snbx file(s) via SAF
        // Supports multi-file selection (Phase 3: Batch Import)
        QStringList packagePaths = pickSnbxFilesAndroid();
        if (!packagePaths.isEmpty()) {
            performBatchImport(packagePaths);
        }
#elif defined(Q_OS_IOS)
        // TODO Phase 4: iOS .snbx import via UIDocumentPickerViewController
        qDebug() << "Launcher: iOS .snbx import picker not yet implemented";
#else
        // Desktop: Show BatchImportDialog for full batch import experience
        QString destDir;
        QStringList files = BatchImportDialog::getImportFiles(this, &destDir);
        
        if (!files.isEmpty() && !destDir.isEmpty()) {
            performBatchImport(files, destDir);
        }
#endif
    });
}

bool Launcher::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    // Luminance formula: 0.299*R + 0.587*G + 0.114*B
    return (0.299 * windowColor.redF() + 0.587 * windowColor.greenF() + 0.114 * windowColor.blueF()) < 0.5;
}

void Launcher::applyStyle()
{
    bool isDark = isDarkMode();
    
    // Load appropriate stylesheet
    QString stylePath = isDark 
        ? ":/resources/styles/launcher_dark.qss"
        : ":/resources/styles/launcher.qss";
    
    QFile styleFile(stylePath);
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString styleSheet = QString::fromUtf8(styleFile.readAll());
        setStyleSheet(styleSheet);
        styleFile.close();
    }
}

void Launcher::switchToView(View view)
{
    m_currentView = view;
    
    switch (view) {
        case View::Timeline:
            m_contentStack->setCurrentWidget(m_timelineView);
            break;
        case View::Starred:
            m_contentStack->setCurrentWidget(m_starredView);
            break;
        case View::Search:
            m_contentStack->setCurrentWidget(m_searchView);
            m_searchView->focusSearchInput();
            break;
    }
    
    updateNavigationState();
}

void Launcher::updateNavigationState()
{
    // Update button checked states
    m_timelineBtn->setChecked(m_currentView == View::Timeline);
    m_starredBtn->setChecked(m_currentView == View::Starred);
    m_searchBtn->setChecked(m_currentView == View::Search);
}

void Launcher::setNavigationCompact(bool compact)
{
    m_returnBtn->setCompact(compact);
    m_timelineBtn->setCompact(compact);
    m_starredBtn->setCompact(compact);
    m_searchBtn->setCompact(compact);
    
    // Update sidebar width
    if (compact) {
        m_navSidebar->setFixedWidth(LauncherNavButton::BUTTON_HEIGHT + 16);
    } else {
        m_navSidebar->setFixedWidth(LauncherNavButton::EXPANDED_WIDTH + 16);
    }
}

void Launcher::showWithAnimation()
{
    // Note: Return button visibility is updated in showEvent() which is
    // called when show() is invoked below
    
    m_fadeOpacity = 0.0;
    show();
    
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void Launcher::hideWithAnimation()
{
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    
    // CR-P.3: Qt::SingleShotConnection auto-disconnects after first emit
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        hide();
    }, Qt::SingleShotConnection);
    
    m_fadeAnimation->start();
}

void Launcher::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = opacity;
    setWindowOpacity(opacity);
}

void Launcher::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    // Custom painting can be added here for background effects
}

void Launcher::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    
    // Reposition FAB in bottom-right corner
    if (m_fab) {
        m_fab->positionInParent();
    }
    
    // Trigger compact mode for navigation buttons when:
    // 1. Window width < 768px (narrow window), OR
    // 2. Portrait orientation (height > width)
    const int windowWidth = event->size().width();
    const int windowHeight = event->size().height();
    const bool shouldBeCompact = (windowWidth < 768) || (windowHeight > windowWidth);
    
    setNavigationCompact(shouldBeCompact);
}

void Launcher::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    
    // Update Return button visibility based on whether MainWindow exists
    // This must be checked each time the Launcher is shown because MainWindow
    // may have been created/destroyed since the Launcher was last visible
    bool hasMainWindow = (MainWindow::findExistingMainWindow() != nullptr);
    if (m_returnBtn) {
        m_returnBtn->setVisible(hasMainWindow);
    }
    
    // Refresh timeline if date has changed since last shown
    // This handles scenarios like system sleep/hibernate during midnight
    if (m_timelineModel) {
        m_timelineModel->refreshIfDateChanged();
    }
}

// =============================================================================
// Drag-Drop Import (Desktop only - Step 3.10)
// =============================================================================

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)

void Launcher::dragEnterEvent(QDragEnterEvent* event)
{
    // Accept drag if it contains file URLs
    if (event->mimeData()->hasUrls()) {
        // Check if any of the URLs are .snbx files
        bool hasSnbxFiles = false;
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                if (filePath.endsWith(".snbx", Qt::CaseInsensitive)) {
                    hasSnbxFiles = true;
                    break;
                }
            }
        }
        
        if (hasSnbxFiles) {
            event->acceptProposedAction();
            return;
        }
    }
    
    event->ignore();
}

void Launcher::dragMoveEvent(QDragMoveEvent* event)
{
    // Accept the move only if we have valid .snbx files (consistent with dragEnterEvent)
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile() && url.toLocalFile().endsWith(".snbx", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void Launcher::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    
    // Collect all .snbx files from the drop
    QStringList snbxFiles;
    const QList<QUrl> urls = event->mimeData()->urls();
    
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".snbx", Qt::CaseInsensitive)) {
                // Verify file exists
                if (QFile::exists(filePath)) {
                    snbxFiles.append(filePath);
                }
            }
        }
    }
    
    if (snbxFiles.isEmpty()) {
        event->ignore();
        return;
    }
    
    event->acceptProposedAction();
    
    // Show confirmation dialog for multiple files
    if (snbxFiles.size() > 1) {
        QString message = tr("Import %1 notebooks?").arg(snbxFiles.size());
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Import Notebooks"),
            message,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );
        
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    // Determine destination directory (default to Documents/SpeedyNote)
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) 
                      + "/SpeedyNote";
    QDir().mkpath(destDir);
    
    // Perform the import
    performBatchImport(snbxFiles, destDir);
}

#endif // !Q_OS_ANDROID && !Q_OS_IOS

void Launcher::onTimelineItemClicked(const QModelIndex& index)
{
    // Ignore clicks on section headers
    bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
    if (isHeader) {
        return;
    }
    
    // Get the bundle path
    QString bundlePath = index.data(TimelineModel::BundlePathRole).toString();
    if (!bundlePath.isEmpty()) {
        emit notebookSelected(bundlePath);
    }
}

void Launcher::keyPressEvent(QKeyEvent* event)
{
    // Escape key: first exit select mode if active, then return to MainWindow
    if (event->key() == Qt::Key_Escape) {
        // Check if any view is in batch select mode and exit it first
        if (m_currentView == View::Timeline && m_timelineList->isSelectMode()) {
            m_timelineList->exitSelectMode();
            return;
        }
        if (m_currentView == View::Starred && m_starredView->isSelectModeActive()) {
            m_starredView->exitSelectMode();
            return;
        }
        
        // No select mode active - request return to MainWindow
        // MainWindow will check if there are open tabs before toggling
        emit returnToMainWindowRequested();
        return;
    }
    
    // Ctrl+L also toggles (launcher shortcut)
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier) {
        emit returnToMainWindowRequested();
        return;
    }
    
    // Ctrl+F switches to search view
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::ControlModifier) {
        switchToView(View::Search);
        return;
    }
    
    QMainWindow::keyPressEvent(event);
}

// ============================================================================
// Context Menus (Phase P.3.8)
// ============================================================================

void Launcher::showNotebookContextMenu(const QString& bundlePath, const QPoint& globalPos)
{
    NotebookLibrary* lib = NotebookLibrary::instance();
    
    // Find notebook info - copy the data we need since recentNotebooks() returns by value
    // (taking address of elements in the returned copy would be a use-after-free bug)
    bool isStarred = false;
    for (const NotebookInfo& nb : lib->recentNotebooks()) {
        if (nb.bundlePath == bundlePath) {
            isStarred = nb.isStarred;
            break;
        }
    }
    
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, isDarkMode());
    
    QAction* starAction = menu.addAction(isStarred ? tr("Unstar") : tr("Star"));
    connect(starAction, &QAction::triggered, this, [this, bundlePath]() {
        toggleNotebookStar(bundlePath);
    });
    
    menu.addSeparator();
    
    // Move to folder submenu (only show if starred)
    if (isStarred) {
        QMenu* folderMenu = menu.addMenu(tr("Move to Folder"));
        ThemeColors::styleMenu(folderMenu, isDarkMode());
        
        // Unfiled option
        QAction* unfiledAction = folderMenu->addAction(tr("Unfiled"));
        connect(unfiledAction, &QAction::triggered, this, [bundlePath]() {
            NotebookLibrary::instance()->setStarredFolder(bundlePath, QString());
        });
        
        folderMenu->addSeparator();
        
        // Recent folders (L-008: quick access to last used folders)
        QStringList recentFolders = lib->recentFolders();
        if (!recentFolders.isEmpty()) {
            for (const QString& folder : recentFolders) {
                // Show with clock icon to indicate recent
                QAction* folderAction = folderMenu->addAction(QString("⏱  %1").arg(folder));
                connect(folderAction, &QAction::triggered, this, [bundlePath, folder]() {
                    NotebookLibrary::instance()->moveNotebooksToFolder({bundlePath}, folder);
                });
            }
            folderMenu->addSeparator();
        }
        
        // More Folders... (opens FolderPickerDialog)
        QAction* moreFoldersAction = folderMenu->addAction(tr("More Folders..."));
        connect(moreFoldersAction, &QAction::triggered, this, [this, bundlePath]() {
            QString folder = FolderPickerDialog::getFolder(this, tr("Move to Folder"));
            if (!folder.isEmpty()) {
                NotebookLibrary::instance()->moveNotebooksToFolder({bundlePath}, folder);
            }
        });
        
        // New Folder...
        QAction* newFolderAction = folderMenu->addAction(tr("+ New Folder..."));
        connect(newFolderAction, &QAction::triggered, this, [this, bundlePath]() {
            bool ok;
            QString name = QInputDialog::getText(this, tr("New Folder"),
                                                  tr("Folder name:"), 
                                                  QLineEdit::Normal, QString(), &ok);
            if (ok && !name.isEmpty()) {
                NotebookLibrary::instance()->createStarredFolder(name);
                NotebookLibrary::instance()->moveNotebooksToFolder({bundlePath}, name);
            }
        });
        
        menu.addSeparator();
    }
    
    // Rename action
    QAction* renameAction = menu.addAction(tr("Rename"));
    connect(renameAction, &QAction::triggered, this, [this, bundlePath]() {
        renameNotebook(bundlePath);
    });
    
    // Duplicate action
    QAction* duplicateAction = menu.addAction(tr("Duplicate"));
    connect(duplicateAction, &QAction::triggered, this, [this, bundlePath]() {
        duplicateNotebook(bundlePath);
    });
    
    menu.addSeparator();
    
    // Export submenu (Phase 3: Batch Operations)
    QMenu* exportMenu = menu.addMenu(tr("Export"));
    ThemeColors::styleMenu(exportMenu, isDarkMode());
    
    QAction* exportPdfAction = exportMenu->addAction(tr("To PDF..."));
    connect(exportPdfAction, &QAction::triggered, this, [this, bundlePath]() {
        showPdfExportDialog({bundlePath});
    });
    
    QAction* exportSnbxAction = exportMenu->addAction(tr("To SNBX..."));
    connect(exportSnbxAction, &QAction::triggered, this, [this, bundlePath]() {
        showSnbxExportDialog({bundlePath});
    });
    
    menu.addSeparator();
    
    // Show in file manager action (not available on Android/iOS - sandboxed storage)
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QAction* showAction = menu.addAction(tr("Show in File Manager"));
    connect(showAction, &QAction::triggered, this, [this, bundlePath]() {
        showInFileManager(bundlePath);
    });
    
    menu.addSeparator();
#endif
    
    // Delete action
    QAction* deleteAction = menu.addAction(tr("Delete"));
    connect(deleteAction, &QAction::triggered, this, [this, bundlePath]() {
        deleteNotebooks({bundlePath});
    });
    
    menu.exec(globalPos);
}

void Launcher::showFolderContextMenu(const QString& folderName, const QPoint& globalPos)
{
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, isDarkMode());
    
    // Rename action
    QAction* renameAction = menu.addAction(tr("Rename"));
    connect(renameAction, &QAction::triggered, this, [this, folderName]() {
        bool ok;
        QString newName = QInputDialog::getText(this, tr("Rename Folder"),
                                                 tr("New name:"),
                                                 QLineEdit::Normal, folderName, &ok);
        if (ok && !newName.isEmpty() && newName != folderName) {
            NotebookLibrary* lib = NotebookLibrary::instance();
            
            // Move all notebooks from old folder to new folder
            lib->createStarredFolder(newName);
            for (const NotebookInfo& info : lib->starredNotebooks()) {
                if (info.starredFolder == folderName) {
                    lib->setStarredFolder(info.bundlePath, newName);
                }
            }
            lib->deleteStarredFolder(folderName);
        }
    });
    
    menu.addSeparator();
    
    // Delete action
    QAction* deleteAction = menu.addAction(tr("Delete Folder"));
    connect(deleteAction, &QAction::triggered, this, [this, folderName]() {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Delete Folder"),
            tr("Delete folder \"%1\"?\n\nNotebooks in this folder will become unfiled.").arg(folderName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            NotebookLibrary::instance()->deleteStarredFolder(folderName);
        }
    });
    
    menu.exec(globalPos);
}

bool Launcher::deleteNotebooks(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty())
        return false;
    
    // --- Build display names for the confirmation dialog ---
    QStringList displayNames;
    displayNames.reserve(bundlePaths.size());
    for (const QString& path : bundlePaths) {
        QString name = path;
        int lastSlash = path.lastIndexOf('/');
        if (lastSlash >= 0) {
            name = path.mid(lastSlash + 1);
            if (name.endsWith(".snb", Qt::CaseInsensitive))
                name.chop(4);
        }
        displayNames.append(name);
    }
    
    // --- Confirmation dialog (adapts to single vs. batch) ---
    QMessageBox::StandardButton reply;
    if (bundlePaths.size() == 1) {
        // Single notebook — same wording as the original deleteNotebook()
        reply = QMessageBox::warning(
            this,
            tr("Delete Notebook"),
            tr("Permanently delete \"%1\"?\n\nThis action cannot be undone.").arg(displayNames.first()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
    } else {
        // Batch — show count and a (possibly truncated) name list
        const int maxShown = 10;
        QString nameList;
        for (int i = 0; i < qMin(displayNames.size(), maxShown); ++i) {
            nameList += QString::fromUtf8("  • ") + displayNames[i] + '\n';
        }
        if (displayNames.size() > maxShown) {
            nameList += tr("  ... and %1 more\n").arg(displayNames.size() - maxShown);
        }
        
        reply = QMessageBox::warning(
            this,
            tr("Delete Notebooks"),
            tr("Permanently delete %1 notebooks?\n\n%2\nThis action cannot be undone.")
                .arg(bundlePaths.size())
                .arg(nameList),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
    }
    
    if (reply != QMessageBox::Yes)
        return false;
    
    // --- Perform deletion for each notebook ---
    // Block libraryChanged signals during the batch to avoid N intermediate
    // model reloads.  We do a single explicit reload at the end instead.
    NotebookLibrary* lib = NotebookLibrary::instance();
    const QSignalBlocker blocker(lib);
    
    MainWindow* mainWindow = MainWindow::findExistingMainWindow();
    
    for (const QString& bundlePath : bundlePaths) {
        // BUG-TAB-002 FIX: If this notebook is open in MainWindow, close it first
        // This prevents undefined behavior (editing deleted files, save failures)
        // Use discardChanges=true because we're deleting - saving is pointless
        QString docId = Document::peekBundleId(bundlePath);
        if (!docId.isEmpty() && mainWindow) {
            mainWindow->closeDocumentById(docId, true);  // discardChanges=true
        }
        
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // BUG-A003 Storage Cleanup: Check if this document has an imported PDF in sandbox
        // If so, delete the PDF too to prevent storage leaks
        QString pdfToDelete = findImportedPdfPath(bundlePath);
#endif
        
        // Remove from library
        lib->removeFromRecent(bundlePath);
        
        // Delete from disk
        QDir bundleDir(bundlePath);
        if (bundleDir.exists()) {
            bundleDir.removeRecursively();
        }
        
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Delete imported PDF if found
        if (!pdfToDelete.isEmpty() && QFile::exists(pdfToDelete)) {
            QFile::remove(pdfToDelete);
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Launcher::deleteNotebooks: Also deleted imported PDF:" << pdfToDelete;
#endif
        }
#endif
    }
    
    // blocker goes out of scope here, re-enabling signals on NotebookLibrary.
    // Now do a single refresh of both views (L-010).
    m_timelineModel->reload();
    m_starredView->reload();
    return true;
}

void Launcher::toggleNotebookStar(const QString& bundlePath)
{
    NotebookLibrary* lib = NotebookLibrary::instance();
    
    // Find current starred state
    bool isCurrentlyStarred = false;
    for (const NotebookInfo& info : lib->recentNotebooks()) {
        if (info.bundlePath == bundlePath) {
            isCurrentlyStarred = info.isStarred;
            break;
        }
    }
    
    lib->setStarred(bundlePath, !isCurrentlyStarred);
}

void Launcher::renameNotebook(const QString& bundlePath)
{
    // Extract current display name
    QString currentName;
    int lastSlash = bundlePath.lastIndexOf('/');
    if (lastSlash >= 0) {
        currentName = bundlePath.mid(lastSlash + 1);
        if (currentName.endsWith(".snb", Qt::CaseInsensitive)) {
            currentName.chop(4);
        }
    }
    
    bool ok;
    QString newName = QInputDialog::getText(this, tr("Rename Notebook"),
                                             tr("New name:"),
                                             QLineEdit::Normal, currentName, &ok);
    
    if (!ok || newName.isEmpty() || newName == currentName) {
        return;
    }
    
    // Sanitize name (remove invalid characters)
    newName.replace('/', '_');
    newName.replace('\\', '_');
    
    // Build new path
    QDir parentDir(bundlePath);
    parentDir.cdUp();
    QString newPath = parentDir.absolutePath() + "/" + newName + ".snb";
    
    // Check if target exists
    if (QDir(newPath).exists()) {
        QMessageBox::warning(this, tr("Rename Failed"),
                            tr("A notebook named \"%1\" already exists.").arg(newName));
        return;
    }
    
    // BUG-TAB-001 FIX: If this notebook is open in MainWindow, close it first
    // This prevents stale path issues - the folder must not be renamed while open
    QString docId = Document::peekBundleId(bundlePath);
    if (!docId.isEmpty()) {
        MainWindow* mainWindow = MainWindow::findExistingMainWindow();
        if (mainWindow) {
            mainWindow->closeDocumentById(docId);
            // Document was saved and closed if it was open - proceed with rename
        }
    }
    
    // Rename the directory
    QDir bundleDir(bundlePath);
    if (bundleDir.rename(bundlePath, newPath)) {
        // Update document.json with the new name
        // This is necessary because NotebookLibrary reads the name from document.json,
        // and displayName() prioritizes the JSON name over the folder name.
        QString manifestPath = newPath + "/document.json";
        QFile manifestFile(manifestPath);
        if (manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = manifestFile.readAll();
            manifestFile.close();
            
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError) {
                QJsonObject obj = doc.object();
                obj["name"] = newName;  // Update the name field
                doc.setObject(obj);
                
                // Write back
                if (manifestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    manifestFile.write(doc.toJson(QJsonDocument::Indented));
                    manifestFile.close();
                }
            }
        }
        
        // Update library
        NotebookLibrary* lib = NotebookLibrary::instance();
        lib->removeFromRecent(bundlePath);
        lib->addToRecent(newPath);
    } else {
        QMessageBox::warning(this, tr("Rename Failed"),
                            tr("Could not rename the notebook."));
    }
}

void Launcher::duplicateNotebook(const QString& bundlePath)
{
    // Extract current name
    QString currentName;
    int lastSlash = bundlePath.lastIndexOf('/');
    if (lastSlash >= 0) {
        currentName = bundlePath.mid(lastSlash + 1);
        if (currentName.endsWith(".snb", Qt::CaseInsensitive)) {
            currentName.chop(4);
        }
    }
    
    // Generate unique name
    QDir parentDir(bundlePath);
    parentDir.cdUp();
    
    QString newName = currentName + " (Copy)";
    QString newPath = parentDir.absolutePath() + "/" + newName + ".snb";
    int copyNum = 2;
    
    while (QDir(newPath).exists()) {
        newName = QString("%1 (Copy %2)").arg(currentName).arg(copyNum++);
        newPath = parentDir.absolutePath() + "/" + newName + ".snb";
    }
    
    // Copy the directory recursively
    QDir sourceDir(bundlePath);
    if (!sourceDir.exists()) {
        QMessageBox::warning(this, tr("Duplicate Failed"),
                            tr("Source notebook not found."));
        return;
    }
    
    // Create destination directory
    QDir destDir(newPath);
    if (!destDir.mkpath(".")) {
        QMessageBox::warning(this, tr("Duplicate Failed"),
                            tr("Could not create destination directory."));
        return;
    }
    
    // Copy all files and subdirectories
    bool success = true;
    QDirIterator it(bundlePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, 
                    QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString sourcePath = it.next();
        QString relativePath = sourcePath.mid(bundlePath.length());
        QString destPath = newPath + relativePath;
        
        QFileInfo fi(sourcePath);
        if (fi.isDir()) {
            QDir().mkpath(destPath);
        } else {
            // Ensure parent directory exists
            QDir().mkpath(QFileInfo(destPath).absolutePath());
            if (!QFile::copy(sourcePath, destPath)) {
                success = false;
            }
        }
    }
    
    if (success) {
        // Add to library
        NotebookLibrary::instance()->addToRecent(newPath);
    } else {
        QMessageBox::warning(this, tr("Duplicate"),
                            tr("Some files could not be copied."));
    }
}

void Launcher::showInFileManager(const QString& bundlePath)
{
    // Open the containing folder and select the notebook
    QFileInfo fi(bundlePath);
    QString folderPath = fi.absolutePath();
    
#ifdef Q_OS_WIN
    // Windows: use explorer with /select
    QProcess::startDetached("explorer", QStringList() << "/select," << QDir::toNativeSeparators(bundlePath));
#elif defined(Q_OS_MAC)
    // macOS: use open with -R to reveal in Finder
    QProcess::startDetached("open", QStringList() << "-R" << bundlePath);
#else
    // Linux: use xdg-open on the parent directory
    // Note: Can't select file, just opens folder
    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
#endif
}

// =============================================================================
// Timeline Select Mode (L-007)
// =============================================================================

void Launcher::setupTimelineSelectModeHeader()
{
    constexpr int HEADER_HEIGHT = 48;
    
    m_timelineSelectModeHeader = new QWidget(m_timelineView);
    m_timelineSelectModeHeader->setFixedHeight(HEADER_HEIGHT);
    m_timelineSelectModeHeader->setObjectName("TimelineSelectModeHeader");
    
    auto* headerLayout = new QHBoxLayout(m_timelineSelectModeHeader);
    headerLayout->setContentsMargins(0, 0, 8, 8);
    headerLayout->setSpacing(8);
    
    // Back button (uses recent.png icon - arrow pointing left)
    m_timelineBackButton = new QPushButton(m_timelineSelectModeHeader);
    m_timelineBackButton->setObjectName("TimelineBackButton");
    m_timelineBackButton->setFixedSize(40, 40);
    m_timelineBackButton->setFlat(true);
    m_timelineBackButton->setCursor(Qt::PointingHandCursor);
    m_timelineBackButton->setIconSize(QSize(24, 24));
    
    connect(m_timelineBackButton, &QPushButton::clicked, this, [this]() {
        m_timelineList->exitSelectMode();
    });
    
    headerLayout->addWidget(m_timelineBackButton);
    
    // Selection count label
    m_timelineSelectionCountLabel = new QLabel(m_timelineSelectModeHeader);
    m_timelineSelectionCountLabel->setObjectName("TimelineSelectionCountLabel");
    
    QFont countFont = m_timelineSelectionCountLabel->font();
    countFont.setPointSize(14);
    countFont.setBold(true);
    m_timelineSelectionCountLabel->setFont(countFont);
    
    headerLayout->addWidget(m_timelineSelectionCountLabel, 1);  // Stretch
    
    // Overflow menu button (uses menu.png icon - three dots)
    m_timelineOverflowMenuButton = new QPushButton(m_timelineSelectModeHeader);
    m_timelineOverflowMenuButton->setObjectName("TimelineOverflowMenuButton");
    m_timelineOverflowMenuButton->setFixedSize(40, 40);
    m_timelineOverflowMenuButton->setFlat(true);
    m_timelineOverflowMenuButton->setCursor(Qt::PointingHandCursor);
    m_timelineOverflowMenuButton->setIconSize(QSize(24, 24));
    
    connect(m_timelineOverflowMenuButton, &QPushButton::clicked, 
            this, &Launcher::showTimelineOverflowMenu);
    
    headerLayout->addWidget(m_timelineOverflowMenuButton);
    
    // Set initial icons based on current theme
    updateTimelineHeaderButtonIcons();
}

void Launcher::showTimelineSelectModeHeader(int count)
{
    // Update count label
    if (count == 1) {
        m_timelineSelectionCountLabel->setText(tr("1 selected"));
    } else {
        m_timelineSelectionCountLabel->setText(tr("%1 selected").arg(count));
    }
    
    // Update icons for current theme
    updateTimelineHeaderButtonIcons();
    
    // Update button styles (hover/press effects)
    bool dark = isDarkMode();
    QString buttonStyle = QString(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: %1; border-radius: 20px; }"
        "QPushButton:pressed { background: %2; border-radius: 20px; }"
    ).arg(ThemeColors::itemHover(dark).name(),
          ThemeColors::pressed(dark).name());
    
    m_timelineBackButton->setStyleSheet(buttonStyle);
    m_timelineOverflowMenuButton->setStyleSheet(buttonStyle);
    
    // Update label color
    QPalette labelPal = m_timelineSelectionCountLabel->palette();
    labelPal.setColor(QPalette::WindowText, ThemeColors::textPrimary(dark));
    m_timelineSelectionCountLabel->setPalette(labelPal);
    
    // Show header
    m_timelineSelectModeHeader->setVisible(true);
}

void Launcher::updateTimelineHeaderButtonIcons()
{
    bool dark = isDarkMode();
    
    // Update back button icon based on theme
    QString backIconPath = dark 
        ? ":/resources/icons/recent_reversed.png" 
        : ":/resources/icons/recent.png";
    m_timelineBackButton->setIcon(QIcon(backIconPath));
    
    // Update overflow menu button icon based on theme
    QString menuIconPath = dark 
        ? ":/resources/icons/menu_reversed.png" 
        : ":/resources/icons/menu.png";
    m_timelineOverflowMenuButton->setIcon(QIcon(menuIconPath));
}

void Launcher::hideTimelineSelectModeHeader()
{
    m_timelineSelectModeHeader->setVisible(false);
}

void Launcher::showTimelineOverflowMenu()
{
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, isDarkMode());
    
    int selectedCount = m_timelineList->selectionCount();
    
    // Select All / Deselect All
    QAction* selectAllAction = menu.addAction(tr("Select All"));
    connect(selectAllAction, &QAction::triggered, this, [this]() {
        m_timelineList->selectAll();
    });
    
    QAction* deselectAllAction = menu.addAction(tr("Deselect All"));
    deselectAllAction->setEnabled(selectedCount > 0);
    connect(deselectAllAction, &QAction::triggered, this, [this]() {
        m_timelineList->deselectAll();
    });
    
    menu.addSeparator();
    
    // Export submenu (Phase 3: Batch Operations)
    QMenu* exportMenu = menu.addMenu(tr("Export"));
    ThemeColors::styleMenu(exportMenu, isDarkMode());
    exportMenu->setEnabled(selectedCount > 0);
    
    QAction* exportPdfAction = exportMenu->addAction(tr("To PDF..."));
    connect(exportPdfAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_timelineList->selectedBundlePaths();
        if (!selected.isEmpty()) {
            showPdfExportDialog(selected);
            m_timelineList->exitSelectMode();
        }
    });
    
    QAction* exportSnbxAction = exportMenu->addAction(tr("To SNBX..."));
    connect(exportSnbxAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_timelineList->selectedBundlePaths();
        if (!selected.isEmpty()) {
            showSnbxExportDialog(selected);
            m_timelineList->exitSelectMode();
        }
    });
    
    menu.addSeparator();
    
    // Move to Folder... (L-008: opens FolderPickerDialog)
    QAction* moveToFolderAction = menu.addAction(tr("Move to Folder..."));
    moveToFolderAction->setEnabled(selectedCount > 0);
    connect(moveToFolderAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_timelineList->selectedBundlePaths();
        if (selected.isEmpty()) return;
        
        QString title = selected.size() == 1 
            ? tr("Move to Folder") 
            : tr("Move %1 notebooks to...").arg(selected.size());
        
        QString folder = FolderPickerDialog::getFolder(this, title);
        if (!folder.isEmpty()) {
            NotebookLibrary::instance()->moveNotebooksToFolder(selected, folder);
            m_timelineList->exitSelectMode();
        }
    });
    
    // Star Selected (Timeline uses Star instead of Unstar)
    QAction* starAction = menu.addAction(tr("Star Selected"));
    starAction->setEnabled(selectedCount > 0);
    connect(starAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_timelineList->selectedBundlePaths();
        if (!selected.isEmpty()) {
            NotebookLibrary::instance()->starNotebooks(selected);
            m_timelineList->exitSelectMode();
        }
    });
    
    menu.addSeparator();
    
    // Delete Selected (L-010: Batch Delete)
    QAction* deleteAction = menu.addAction(tr("Delete Selected"));
    deleteAction->setEnabled(selectedCount > 0);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_timelineList->selectedBundlePaths();
        if (!selected.isEmpty() && deleteNotebooks(selected)) {
            m_timelineList->exitSelectMode();
        }
    });
    
    // Position menu relative to overflow button
    QPoint menuPos = m_timelineOverflowMenuButton->mapToGlobal(
        QPoint(m_timelineOverflowMenuButton->width(), m_timelineOverflowMenuButton->height()));
    menu.exec(menuPos);
}

void Launcher::onTimelineSelectModeChanged(bool active)
{
    if (active) {
        showTimelineSelectModeHeader(m_timelineList->selectionCount());
    } else {
        hideTimelineSelectModeHeader();
    }
}

void Launcher::onTimelineBatchSelectionChanged(int count)
{
    if (m_timelineList->isSelectMode()) {
        showTimelineSelectModeHeader(count);
    }
}

void Launcher::onTimelineLongPressed(const QModelIndex& index, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    
    if (!index.isValid()) return;
    
    QString bundlePath = index.data(TimelineModel::BundlePathRole).toString();
    if (!bundlePath.isEmpty()) {
        // Enter batch select mode with this notebook as the first selection
        m_timelineList->enterSelectMode(bundlePath);
    }
}

// =============================================================================
// Batch Export Helpers (Phase 3)
// =============================================================================

void Launcher::showPdfExportDialog(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty()) return;
    
    BatchPdfExportDialog dialog(bundlePaths, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Get valid bundles (excludes edgeless notebooks)
        QStringList validBundles = dialog.validBundles();
        if (!validBundles.isEmpty()) {
            BatchOps::ExportPdfOptions options;
            options.outputPath = dialog.outputDirectory();
            options.dpi = dialog.dpi();
            options.pageRange = dialog.pageRange();
            options.annotationsOnly = dialog.annotationsOnly();
            options.preserveMetadata = dialog.includeMetadata();
            options.preserveOutline = dialog.includeOutline();
            
            ExportQueueManager::instance()->enqueuePdfExport(validBundles, options);
        }
    }
}

void Launcher::showSnbxExportDialog(const QStringList& bundlePaths)
{
    if (bundlePaths.isEmpty()) return;
    
    BatchSnbxExportDialog dialog(bundlePaths, this);
    if (dialog.exec() == QDialog::Accepted) {
        BatchOps::ExportSnbxOptions options;
        options.outputPath = dialog.outputDirectory();
        options.includePdf = dialog.includePdf();
        
        ExportQueueManager::instance()->enqueueSnbxExport(bundlePaths, options);
    }
}

// =============================================================================
// Export Progress Widget Integration (Phase 3)
// =============================================================================

void Launcher::setupExportProgress()
{
    // Create progress widget on central widget so it overlays content
    m_exportProgressWidget = new ExportProgressWidget(m_centralWidget);
    m_exportProgressWidget->setDarkMode(isDarkMode());
    m_exportProgressWidget->hide();
    
    // Connect to ExportQueueManager signals
    ExportQueueManager* mgr = ExportQueueManager::instance();
    
    connect(mgr, &ExportQueueManager::progressChanged,
            this, &Launcher::onExportProgress);
    
    connect(mgr, &ExportQueueManager::jobComplete,
            this, &Launcher::onExportJobComplete);
    
    // Connect widget's Details button
    connect(m_exportProgressWidget, &ExportProgressWidget::detailsRequested,
            this, &Launcher::onExportDetailsRequested);
}

void Launcher::onExportProgress(const QString& currentFile, int current, int total, int queuedJobs)
{
    // Extract just the filename for display
    QString displayName = QFileInfo(currentFile).fileName();
    if (displayName.endsWith(".snb")) {
        displayName.chop(4);  // Remove .snb extension
    }
    
    m_exportProgressWidget->showProgress(displayName, current, total, queuedJobs);
}

void Launcher::onExportJobComplete(const BatchOps::BatchResult& result, const QString& outputDir)
{
    // Store for "Details" dialog
    m_lastExportResult = result;
    m_lastExportOutputDir = outputDir;
    
    // Count results and collect successful output paths
    int successCount = 0;
    int failCount = 0;
    int skipCount = 0;
    QStringList successfulOutputs;
    
    for (const auto& r : result.results) {
        switch (r.status) {
            case BatchOps::FileStatus::Success:
                successCount++;
                if (!r.outputPath.isEmpty()) {
                    successfulOutputs.append(r.outputPath);
                }
                break;
            case BatchOps::FileStatus::Skipped:
                skipCount++;
                break;
            case BatchOps::FileStatus::Error:
                failCount++;
                break;
        }
    }
    
    m_exportProgressWidget->showComplete(successCount, failCount, skipCount);
    
    // Show system notification (especially useful when app is backgrounded)
    // Check if notifications are available and app is not in foreground
    if (SystemNotification::isAvailable()) {
        QString title;
        QString message;
        bool success = (failCount == 0);
        
        if (failCount == 0 && skipCount == 0) {
            title = tr("Export Complete");
            message = tr("%n notebook(s) exported successfully", "", successCount);
        } else if (failCount > 0) {
            title = tr("Export Completed with Errors");
            message = tr("%1 succeeded, %2 failed").arg(successCount).arg(failCount);
            if (skipCount > 0) {
                message += tr(", %1 skipped").arg(skipCount);
            }
        } else {
            title = tr("Export Complete");
            message = tr("%1 exported, %2 skipped").arg(successCount).arg(skipCount);
        }
        
        SystemNotification::showExportNotification(title, message, success);
    }
    
#ifdef Q_OS_ANDROID
    // On Android, trigger share sheet with exported files
    if (!successfulOutputs.isEmpty() && AndroidShareHelper::isAvailable()) {
        // Determine MIME type from first output file
        QString firstOutput = successfulOutputs.first();
        QString mimeType = "application/octet-stream";  // Default
        QString chooserTitle = tr("Share Files");
        
        if (firstOutput.endsWith(".pdf", Qt::CaseInsensitive)) {
            mimeType = "application/pdf";
            chooserTitle = successfulOutputs.size() == 1 
                ? tr("Share PDF") 
                : tr("Share %1 PDFs").arg(successfulOutputs.size());
        } else if (firstOutput.endsWith(".snbx", Qt::CaseInsensitive)) {
            mimeType = "application/octet-stream";
            chooserTitle = successfulOutputs.size() == 1 
                ? tr("Share Notebook") 
                : tr("Share %1 Notebooks").arg(successfulOutputs.size());
        }
        
        AndroidShareHelper::shareMultipleFiles(successfulOutputs, mimeType, chooserTitle);
    }
#elif defined(Q_OS_IOS)
    if (!successfulOutputs.isEmpty() && IOSShareHelper::isAvailable()) {
        QString firstOutput = successfulOutputs.first();
        QString mimeType = "application/octet-stream";
        QString title = tr("Share Files");

        if (firstOutput.endsWith(".pdf", Qt::CaseInsensitive)) {
            mimeType = "application/pdf";
            title = successfulOutputs.size() == 1
                ? tr("Share PDF")
                : tr("Share %1 PDFs").arg(successfulOutputs.size());
        } else if (firstOutput.endsWith(".snbx", Qt::CaseInsensitive)) {
            mimeType = "application/octet-stream";
            title = successfulOutputs.size() == 1
                ? tr("Share Notebook")
                : tr("Share %1 Notebooks").arg(successfulOutputs.size());
        }

        IOSShareHelper::shareMultipleFiles(successfulOutputs, mimeType, title);
    }
#else
    Q_UNUSED(successfulOutputs)
#endif
}

void Launcher::onExportDetailsRequested()
{
    ExportResultsDialog dialog(m_lastExportResult, m_lastExportOutputDir, this);
    dialog.setDarkMode(isDarkMode());
    
    // Connect retry signal to re-export failed files
    connect(&dialog, &ExportResultsDialog::retryRequested, this, [this](const QStringList& failedPaths) {
        // Determine if the last export was PDF or SNBX based on output files
        // For now, we assume the user will manually retry through the UI
        // A more robust solution would store the export type with the result
        
        // Show the appropriate export dialog pre-populated with failed paths
        // For simplicity, just show PDF dialog (most common case)
        if (!failedPaths.isEmpty()) {
            showPdfExportDialog(failedPaths);
        }
    });
    
    dialog.exec();
    
    // Dismiss the progress widget after dialog is closed
    if (m_exportProgressWidget) {
        m_exportProgressWidget->dismiss(true);
    }
}

// =============================================================================
// Batch Import (Phase 3)
// =============================================================================

void Launcher::performBatchImport(const QStringList& snbxFiles, const QString& destDir)
{
    if (snbxFiles.isEmpty()) {
        return;
    }
    
    // Determine destination directory
    QString importDestDir = destDir;
    if (importDestDir.isEmpty()) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Android/iOS: Use app data location for imports
        importDestDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
#else
        // Desktop: Use Documents/SpeedyNote as default
        importDestDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SpeedyNote";
#endif
    }
    QDir().mkpath(importDestDir);
    
    // Setup import options
    BatchOps::ImportOptions options;
    options.destDir = importDestDir;
    options.addToLibrary = true;  // Always add to library so they appear in timeline
    options.overwrite = false;    // Don't overwrite existing
    
    // Show progress for imports
    int total = snbxFiles.size();
    int current = 0;
    
    // Progress callback (with null check for safety)
    auto progressCallback = [this, &current, total](int cur, int tot, const QString& file, const QString& /*status*/) {
        Q_UNUSED(tot)
        current = cur;
        if (m_exportProgressWidget) {
            QString displayName = QFileInfo(file).completeBaseName();
            m_exportProgressWidget->showProgress(displayName, cur, total, 0);
        }
    };
    
    // Show initial progress
    if (m_exportProgressWidget) {
        if (total == 1) {
            QString displayName = QFileInfo(snbxFiles.first()).completeBaseName();
            m_exportProgressWidget->showProgress(displayName, 1, 1, 0);
        } else {
            m_exportProgressWidget->showProgress(tr("Importing..."), 0, total, 0);
        }
    }
    
    // Run import (synchronous for now - imports are typically fast)
    // For very large imports, this could be moved to a background thread
    BatchOps::BatchResult result = BatchOps::importSnbxBatch(snbxFiles, options, progressCallback);
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Clean up source .snbx files from temp /imports directory after import
    // These were copied from content:// URIs and are no longer needed
    QString importsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/imports";
    for (const QString& snbxFile : snbxFiles) {
        if (snbxFile.startsWith(importsDir)) {
            QFile::remove(snbxFile);
        }
    }
#endif
    
    // Store result for details dialog
    m_lastExportResult = result;
    m_lastExportOutputDir = importDestDir;
    
    // Show completion
    if (m_exportProgressWidget) {
        m_exportProgressWidget->showComplete(result.successCount, result.errorCount, result.skippedCount);
    }
    
    // Show system notification for import completion
    if (SystemNotification::isAvailable()) {
        QString title;
        QString message;
        bool success = (result.errorCount == 0);
        
        if (result.errorCount == 0 && result.skippedCount == 0) {
            title = tr("Import Complete");
            message = tr("%n notebook(s) imported successfully", "", result.successCount);
        } else if (result.errorCount > 0) {
            title = tr("Import Completed with Errors");
            message = tr("%1 succeeded, %2 failed").arg(result.successCount).arg(result.errorCount);
            if (result.skippedCount > 0) {
                message += tr(", %1 skipped").arg(result.skippedCount);
            }
        } else {
            title = tr("Import Complete");
            message = tr("%1 imported, %2 skipped").arg(result.successCount).arg(result.skippedCount);
        }
        
        SystemNotification::showImportNotification(title, message, success);
    }
    
    // Refresh views to show newly imported notebooks
    if (result.successCount > 0) {
        // Force reload of timeline and starred views
        if (m_timelineModel) {
            m_timelineModel->reload();
        }
        if (m_starredView) {
            m_starredView->reload();
        }
        
        // If only one notebook was imported, open it directly
        if (snbxFiles.size() == 1 && result.successCount == 1 && !result.results.isEmpty()) {
            QString importedPath = result.results.first().outputPath;
            if (!importedPath.isEmpty() && QDir(importedPath).exists()) {
                emit notebookSelected(importedPath);
            }
        }
    }
}

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
QString Launcher::findImportedPdfPath(const QString& bundlePath)
{
    // BUG-A003 Storage Cleanup: Check if this document has an imported PDF in sandbox.
    // Returns the path to the PDF if it's in our sandbox, empty string otherwise.
    
    // Read document.json to get the PDF path
    QString manifestPath = bundlePath + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    
    QByteArray data = manifestFile.readAll();
    manifestFile.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QString();
    }
    
    QJsonObject obj = doc.object();
    QString pdfPath = obj["pdf_path"].toString();
    
    if (pdfPath.isEmpty()) {
        return QString(); // Not a PDF-backed document
    }
    
    // Check if the PDF is in our sandbox directories:
    // 1. /files/pdfs/ - Direct PDF imports via SAF (BUG-A003)
    // 2. /files/notebooks/embedded/ - PDFs extracted from .snbx packages (Phase 2)
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString sandboxPdfDir = appDataDir + "/pdfs";
    QString embeddedDir = appDataDir + "/notebooks/embedded";
    
    if (pdfPath.startsWith(sandboxPdfDir) || pdfPath.startsWith(embeddedDir)) {
        // This PDF was imported to our sandbox - safe to delete
        return pdfPath;
    }
    
    // PDF is external (user's original file) - don't delete it
    return QString();
}
#endif

