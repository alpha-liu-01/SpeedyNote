#include "Launcher.h"
#include "LauncherNavButton.h"
#include "TimelineModel.h"
#include "TimelineDelegate.h"
#include "TimelineListView.h"
#include "StarredView.h"
#include "SearchView.h"
#include "FloatingActionButton.h"
#include "../../MainWindow.h"
#include "../../core/NotebookLibrary.h"
#include "../../core/Document.h"

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
#include <QScroller>
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

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#include <jni.h>
#endif

// ============================================================================
// Android Package Picker (JNI Integration)
// ============================================================================
#ifdef Q_OS_ANDROID

namespace {
    // Static variables for async package picker result
    static QString s_pickedPackagePath;
    static bool s_packagePickerCancelled = false;
    static QEventLoop* s_packagePickerLoop = nullptr;
}

// JNI callback: Called from Java when a package file is successfully picked and copied
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_ImportHelper_onPackageFilePicked(JNIEnv *env, jclass /*clazz*/, jstring localPath)
{
    const char* pathChars = env->GetStringUTFChars(localPath, nullptr);
    s_pickedPackagePath = QString::fromUtf8(pathChars);
    env->ReleaseStringUTFChars(localPath, pathChars);
    
    s_packagePickerCancelled = false;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "JNI callback: Package picked -" << s_pickedPackagePath;
#endif
    
    if (s_packagePickerLoop) {
        s_packagePickerLoop->quit();
    }
}

// JNI callback: Called from Java when package picking is cancelled or fails
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_ImportHelper_onPackagePickCancelled(JNIEnv * /*env*/, jclass /*clazz*/)
{
    s_pickedPackagePath.clear();
    s_packagePickerCancelled = true;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "JNI callback: Package pick cancelled";
#endif
    
    if (s_packagePickerLoop) {
        s_packagePickerLoop->quit();
    }
}

// Helper function to pick a .snbx package file on Android via SAF
static QString pickSnbxFileAndroid()
{
    // Reset state
    s_pickedPackagePath.clear();
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
        qDebug() << "pickSnbxFileAndroid: Cleaned up old import:" << filePath;
#endif
    }
    
    // Get the Activity
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "pickSnbxFileAndroid: Failed to get Android context";
        return QString();
    }
    
    // Call ImportHelper.pickPackageFile(activity, destDir)
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
    
    if (s_packagePickerCancelled || s_pickedPackagePath.isEmpty()) {
        return QString();
    }
    
    return s_pickedPackagePath;
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
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    
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

void Launcher::setupTimeline()
{
    // Create layout for timeline view
    auto* layout = new QVBoxLayout(m_timelineView);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);
    
    // Create model and delegate
    m_timelineModel = new TimelineModel(this);
    m_timelineDelegate = new TimelineDelegate(this);
    
    // CR-P.1: Connect thumbnailUpdated to invalidate delegate's cache
    connect(NotebookLibrary::instance(), &NotebookLibrary::thumbnailUpdated,
            m_timelineDelegate, &TimelineDelegate::invalidateThumbnail);
    
    m_timelineDelegate->setDarkMode(isDarkMode());
    
    // Create list view (using custom TimelineListView for long-press support)
    m_timelineList = new TimelineListView(m_timelineView);
    m_timelineList->setObjectName("TimelineList");
    m_timelineList->setModel(m_timelineModel);
    m_timelineList->setItemDelegate(m_timelineDelegate);
    
    // Configure list view for touch
    m_timelineList->setViewMode(QListView::ListMode);
    m_timelineList->setFlow(QListView::TopToBottom);
    m_timelineList->setWrapping(false);
    m_timelineList->setResizeMode(QListView::Adjust);
    m_timelineList->setLayoutMode(QListView::SinglePass);
    
    // Selection
    m_timelineList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_timelineList->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Scrolling
    m_timelineList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_timelineList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Appearance
    m_timelineList->setFrameShape(QFrame::NoFrame);
    m_timelineList->setSpacing(0);
    m_timelineList->setUniformItemSizes(false);  // Headers and cards have different heights
    
    // Enable mouse tracking for hover effects
    m_timelineList->setMouseTracking(true);
    m_timelineList->viewport()->setMouseTracking(true);
    m_timelineList->setAttribute(Qt::WA_Hover, true);
    m_timelineList->viewport()->setAttribute(Qt::WA_Hover, true);
    
    // Touch scrolling with QScroller
    QScroller::grabGesture(m_timelineList->viewport(), QScroller::TouchGesture);
    QScroller* scroller = QScroller::scroller(m_timelineList->viewport());
    QScrollerProperties props = scroller->scrollerProperties();
    props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
    props.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.2);
    props.setScrollMetric(QScrollerProperties::DragStartDistance, 0.002);  // Start drag sooner
    scroller->setScrollerProperties(props);
    
    // Connect click
    connect(m_timelineList, &QListView::clicked,
            this, &Launcher::onTimelineItemClicked);
    
    // Context menu for right-click
    m_timelineList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_timelineList, &QListView::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QModelIndex index = m_timelineList->indexAt(pos);
        if (!index.isValid()) return;
        
        // Ignore section headers
        bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
        if (isHeader) return;
        
        QString bundlePath = index.data(TimelineModel::BundlePathRole).toString();
        if (!bundlePath.isEmpty()) {
            showNotebookContextMenu(bundlePath, m_timelineList->viewport()->mapToGlobal(pos));
        }
    });
    
    // Context menu for long-press (touch devices)
    connect(m_timelineList, &TimelineListView::longPressed,
            this, [this](const QModelIndex& index, const QPoint& globalPos) {
        if (!index.isValid()) return;
        
        // Ignore section headers
        bool isHeader = index.data(TimelineModel::IsSectionHeaderRole).toBool();
        if (isHeader) return;
        
        QString bundlePath = index.data(TimelineModel::BundlePathRole).toString();
        if (!bundlePath.isEmpty()) {
            showNotebookContextMenu(bundlePath, globalPos);
        }
    });
    
    layout->addWidget(m_timelineList);
}

void Launcher::setupStarred()
{
    m_starredView->setDarkMode(isDarkMode());
    
    // Connect signals
    connect(m_starredView, &StarredView::notebookClicked, this, [this](const QString& bundlePath) {
        emit notebookSelected(bundlePath);
    });
    
    connect(m_starredView, &StarredView::notebookLongPressed, this, [this](const QString& bundlePath) {
        showNotebookContextMenu(bundlePath, QCursor::pos());
    });
    
    connect(m_starredView, &StarredView::folderLongPressed, this, [this](const QString& folderName) {
        showFolderContextMenu(folderName, QCursor::pos());
    });
}

void Launcher::setupSearch()
{
    m_searchView->setDarkMode(isDarkMode());
    
    // Connect signals
    connect(m_searchView, &SearchView::notebookClicked, this, [this](const QString& bundlePath) {
        emit notebookSelected(bundlePath);
    });
    
    connect(m_searchView, &SearchView::notebookLongPressed, this, [this](const QString& bundlePath) {
        showNotebookContextMenu(bundlePath, QCursor::pos());
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
    
    // Import package - platform-specific handling
    connect(m_fab, &FloatingActionButton::importPackage, this, [this]() {
#ifdef Q_OS_ANDROID
        // Use ImportHelper to pick .snbx file via SAF
        QString packagePath = pickSnbxFileAndroid();
        if (!packagePath.isEmpty()) {
            emit notebookSelected(packagePath);  // DocumentManager handles .snbx extraction
        }
#else
        // Desktop: Use QFileDialog
        QString packagePath = QFileDialog::getOpenFileName(this,
            tr("Import Notebook Package"),
            QDir::homePath(),
            tr("SpeedyNote Package (*.snbx)"));
        
        if (!packagePath.isEmpty()) {
            emit notebookSelected(packagePath);  // DocumentManager handles .snbx extraction
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
    // Escape key requests return to MainWindow
    // MainWindow will check if there are open tabs before toggling
    if (event->key() == Qt::Key_Escape) {
        emit returnToMainWindowRequested();
        return;
    }
    
    // Ctrl+L also toggles (launcher shortcut)
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier) {
        emit returnToMainWindowRequested();
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
    QAction* starAction = menu.addAction(isStarred ? tr("Unstar") : tr("Star"));
    connect(starAction, &QAction::triggered, this, [this, bundlePath]() {
        toggleNotebookStar(bundlePath);
    });
    
    menu.addSeparator();
    
    // Move to folder submenu (only show if starred)
    if (isStarred) {
        QMenu* folderMenu = menu.addMenu(tr("Move to Folder"));
        
        // Unfiled option
        QAction* unfiledAction = folderMenu->addAction(tr("Unfiled"));
        connect(unfiledAction, &QAction::triggered, this, [bundlePath]() {
            NotebookLibrary::instance()->setStarredFolder(bundlePath, QString());
        });
        
        folderMenu->addSeparator();
        
        // Existing folders
        QStringList folders = lib->starredFolders();
        for (const QString& folder : folders) {
            QAction* folderAction = folderMenu->addAction(folder);
            connect(folderAction, &QAction::triggered, this, [bundlePath, folder]() {
                NotebookLibrary::instance()->setStarredFolder(bundlePath, folder);
            });
        }
        
        folderMenu->addSeparator();
        
        // Create new folder
        QAction* newFolderAction = folderMenu->addAction(tr("New Folder..."));
        connect(newFolderAction, &QAction::triggered, this, [this, bundlePath]() {
            bool ok;
            QString name = QInputDialog::getText(this, tr("New Folder"),
                                                  tr("Folder name:"), 
                                                  QLineEdit::Normal, QString(), &ok);
            if (ok && !name.isEmpty()) {
                NotebookLibrary::instance()->createStarredFolder(name);
                NotebookLibrary::instance()->setStarredFolder(bundlePath, name);
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
    
    // Show in file manager action
    QAction* showAction = menu.addAction(tr("Show in File Manager"));
    connect(showAction, &QAction::triggered, this, [this, bundlePath]() {
        showInFileManager(bundlePath);
    });
    
    menu.addSeparator();
    
    // Delete action
    QAction* deleteAction = menu.addAction(tr("Delete"));
    connect(deleteAction, &QAction::triggered, this, [this, bundlePath]() {
        deleteNotebook(bundlePath);
    });
    
    menu.exec(globalPos);
}

void Launcher::showFolderContextMenu(const QString& folderName, const QPoint& globalPos)
{
    QMenu menu(this);
    
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

void Launcher::deleteNotebook(const QString& bundlePath)
{
    // Extract display name for confirmation
    QString displayName = bundlePath;
    int lastSlash = bundlePath.lastIndexOf('/');
    if (lastSlash >= 0) {
        displayName = bundlePath.mid(lastSlash + 1);
        if (displayName.endsWith(".snb", Qt::CaseInsensitive)) {
            displayName.chop(4);
        }
    }
    
    QMessageBox::StandardButton reply = QMessageBox::warning(
        this,
        tr("Delete Notebook"),
        tr("Permanently delete \"%1\"?\n\nThis action cannot be undone.").arg(displayName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
#ifdef Q_OS_ANDROID
        // BUG-A003 Storage Cleanup: Check if this document has an imported PDF in sandbox
        // If so, delete the PDF too to prevent storage leaks
        QString pdfToDelete = findImportedPdfPath(bundlePath);
#endif
        
        // Remove from library
        NotebookLibrary::instance()->removeFromRecent(bundlePath);
        
        // Delete from disk
        QDir bundleDir(bundlePath);
        if (bundleDir.exists()) {
            bundleDir.removeRecursively();
        }
        
#ifdef Q_OS_ANDROID
        // Delete imported PDF if found
        if (!pdfToDelete.isEmpty() && QFile::exists(pdfToDelete)) {
            QFile::remove(pdfToDelete);
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Launcher::deleteNotebook: Also deleted imported PDF:" << pdfToDelete;
#endif
        }
#endif
    }
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

#ifdef Q_OS_ANDROID
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

