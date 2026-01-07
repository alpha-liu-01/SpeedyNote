#include "LauncherWindow.h"
#include "MainWindow.h"
#include "RecentNotebooksManager.h"
#include "SpnPackageManager.h"
#include "DocumentConverter.h"
#include <QApplication>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFrame>
#include <QPixmap>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QPointer>
#include <QDesktopServices>
#include <QUrl>
#include <QResizeEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QScroller>
#include <QHash>

LauncherWindow::LauncherWindow(QWidget *parent)
    : QMainWindow(parent), lastCalculatedWidth(0), lastColumnCount(0), notebookManager(nullptr)
{
    setupUi();
    applyModernStyling();
    
    // Set window properties
    setWindowTitle(tr("SpeedyNote - Launcher"));
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize logicalSize = screen->availableGeometry().size() * 0.89;
        resize(logicalSize);
    }
    
    // Set window icon
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    
    // Use singleton instance of RecentNotebooksManager
    notebookManager = RecentNotebooksManager::getInstance(this);
    
    // Connect to thumbnail update signal to invalidate pixmap cache
    connect(notebookManager, &RecentNotebooksManager::thumbnailUpdated,
            this, [this](const QString& folderPath, const QString& coverImagePath) {
                invalidatePixmapCacheForPath(coverImagePath);
                // Don't repopulate entire grids here - that causes memory leaks!
                // The cache invalidation is enough; thumbnails will refresh on next launcher open
            });
    
    // Don't populate grids in constructor - showEvent will handle it
    // This prevents double population (constructor + showEvent)
}

LauncherWindow::~LauncherWindow()
{
    // Clean up QScroller instances to prevent memory leaks
    if (recentScrollArea && recentScrollArea->viewport()) {
        QScroller::ungrabGesture(recentScrollArea->viewport());
    }
    if (starredScrollArea && starredScrollArea->viewport()) {
        QScroller::ungrabGesture(starredScrollArea->viewport());
    }
    
    // Clear grids and pixmap cache before destruction
    clearRecentGrid();
    clearStarredGrid();
    clearPixmapCache();
}


void LauncherWindow::setupUi()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // Create main splitter
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setHandleWidth(1); // Minimal handle width to remove gap
    mainSplitter->setChildrenCollapsible(false); // Prevent accidental collapse
    
    // Create sidebar with tabs
    tabList = new QListWidget();
    tabList->setObjectName("sidebarTabList"); // Give it a specific name for styling
    tabList->setFixedWidth(205);
    tabList->setSpacing(4);
    
    // Add tab items with explicit sizing
    QListWidgetItem *returnItem = new QListWidgetItem(loadThemedIcon("recent"), tr("Return"));
    QListWidgetItem *newItem = new QListWidgetItem(loadThemedIcon("addtab"), tr("New"));
    QListWidgetItem *openPdfItem = new QListWidgetItem(loadThemedIcon("pdf"), tr("Open File"));
    QListWidgetItem *openNotebookItem = new QListWidgetItem(loadThemedIcon("folder"), tr("Open Notes"));
    QListWidgetItem *recentItem = new QListWidgetItem(loadThemedIcon("benchmark"), tr("Recent"));
    QListWidgetItem *starredItem = new QListWidgetItem(loadThemedIcon("star"), tr("Starred"));
    
    // Set explicit size hints for touch-friendly interface
    QSize itemSize(190, 60); // Width, Height - much taller for touch
    returnItem->setSizeHint(itemSize);
    newItem->setSizeHint(itemSize);
    openPdfItem->setSizeHint(itemSize);
    openNotebookItem->setSizeHint(itemSize);
    recentItem->setSizeHint(itemSize);
    starredItem->setSizeHint(itemSize);
    
    // Set font for each item
    QFont itemFont;
    itemFont.setPointSize(14);
    itemFont.setWeight(QFont::Medium);
    returnItem->setFont(itemFont);
    newItem->setFont(itemFont);
    openPdfItem->setFont(itemFont);
    openNotebookItem->setFont(itemFont);
    recentItem->setFont(itemFont);
    starredItem->setFont(itemFont);
    
    tabList->addItem(returnItem);
    tabList->addItem(newItem);
    tabList->addItem(openPdfItem);
    tabList->addItem(openNotebookItem);
    tabList->addItem(recentItem);
    tabList->addItem(starredItem);
    
    tabList->setCurrentRow(4); // Start with Recent tab (now index 4)
    
    // Create content area
    contentStack = new QStackedWidget();
    
    // Setup individual tabs
    setupReturnTab();
    setupNewTab();
    setupOpenPdfTab();
    setupOpenNotebookTab();
    setupRecentTab();
    setupStarredTab();
    
    contentStack->addWidget(returnTab);
    contentStack->addWidget(newTab);
    contentStack->addWidget(openPdfTab);
    contentStack->addWidget(openNotebookTab);
    contentStack->addWidget(recentTab);
    contentStack->addWidget(starredTab);
    contentStack->setCurrentIndex(4); // Start with Recent tab (now index 4)
    
    // Add to splitter
    mainSplitter->addWidget(tabList);
    mainSplitter->addWidget(contentStack);
    
    // Configure stretch factors: sidebar doesn't stretch, content area does
    mainSplitter->setStretchFactor(0, 0); // Sidebar: no stretch
    mainSplitter->setStretchFactor(1, 1); // Content: stretch to fill
    
    // Set initial sizes to remove gap: sidebar gets its fixed width, content gets the rest
    QList<int> sizes;
    sizes << 205 << 1000; // Sidebar: 205px (matches fixed width), Content: large value
    mainSplitter->setSizes(sizes);
    
    // Main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->addWidget(mainSplitter);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Connect tab selection with custom handling
    connect(tabList, &QListWidget::currentRowChanged, this, &LauncherWindow::onTabChanged);
}

void LauncherWindow::setupReturnTab()
{
    returnTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(returnTab);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(30);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Return to Previous Document"));
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    // Description
    QLabel *descLabel = new QLabel(tr("Click the Return tab to go back to your previous document"));
    descLabel->setObjectName("descLabel");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
    
    layout->addStretch();
}

void LauncherWindow::setupNewTab()
{
    newTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(newTab);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(30);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Create New Notebook"));
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    // Description
    QLabel *descLabel = new QLabel(tr("Start a new notebook with a blank canvas"));
    descLabel->setObjectName("descLabel");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
    
    // Create button
    QPushButton *createBtn = new QPushButton(tr("Create New Notebook"));
    createBtn->setObjectName("primaryButton");
    createBtn->setFixedSize(190, 50);
    connect(createBtn, &QPushButton::clicked, this, &LauncherWindow::onNewNotebookClicked);
    layout->addWidget(createBtn, 0, Qt::AlignCenter);
    
    layout->addStretch();
}

void LauncherWindow::setupOpenPdfTab()
{
    openPdfTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(openPdfTab);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(30);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Open PDF"));
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    // Description
    QLabel *descLabel = new QLabel(tr("Select a PDF or PowerPoint file to create a notebook for annotation"));
    descLabel->setObjectName("descLabel");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
    
    // Open button
    QPushButton *openBtn = new QPushButton(tr("Browse for PDF/PPT"));
    openBtn->setObjectName("primaryButton");
    openBtn->setFixedSize(190, 50);
    connect(openBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenPdfClicked);
    layout->addWidget(openBtn, 0, Qt::AlignCenter);
    
    layout->addStretch();
}

void LauncherWindow::setupOpenNotebookTab()
{
    openNotebookTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(openNotebookTab);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(30);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Open Notebook"));
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    // Description
    QLabel *descLabel = new QLabel(tr("Select an existing SpeedyNote notebook (.spn) to open"));
    descLabel->setObjectName("descLabel");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
    
    // Open button
    QPushButton *openBtn = new QPushButton(tr("Browse for Notebook"));
    openBtn->setObjectName("primaryButton");
    openBtn->setFixedSize(190, 50);
    connect(openBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenNotebookClicked);
    layout->addWidget(openBtn, 0, Qt::AlignCenter);
    
    layout->addStretch();
}

void LauncherWindow::setupRecentTab()
{
    recentTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(recentTab);
    layout->setContentsMargins(20, 20, 20, 20);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Recent Notebooks"));
    titleLabel->setObjectName("titleLabel");
    layout->addWidget(titleLabel);
    
    // Scroll area for grid
    recentScrollArea = new QScrollArea();
    recentScrollArea->setWidgetResizable(true);
    recentScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recentScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // Enable touch scrolling
    recentScrollArea->setAttribute(Qt::WA_AcceptTouchEvents, true);
    
    // Enable kinetic scrolling for touch devices
    QScroller::grabGesture(recentScrollArea->viewport(), QScroller::LeftMouseButtonGesture);
    
    recentGridWidget = new QWidget();
    recentGridLayout = new QGridLayout(recentGridWidget);
    recentGridLayout->setSpacing(20);
    recentGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    
    recentScrollArea->setWidget(recentGridWidget);
    layout->addWidget(recentScrollArea);
}

void LauncherWindow::setupStarredTab()
{
    starredTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(starredTab);
    layout->setContentsMargins(20, 20, 20, 20);
    
    // Title
    QLabel *titleLabel = new QLabel(tr("Starred Notebooks"));
    titleLabel->setObjectName("titleLabel");
    layout->addWidget(titleLabel);
    
    // Scroll area for grid
    starredScrollArea = new QScrollArea();
    starredScrollArea->setWidgetResizable(true);
    starredScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    starredScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // Enable touch scrolling
    starredScrollArea->setAttribute(Qt::WA_AcceptTouchEvents, true);
    
    // Enable kinetic scrolling for touch devices
    QScroller::grabGesture(starredScrollArea->viewport(), QScroller::LeftMouseButtonGesture);
    
    starredGridWidget = new QWidget();
    starredGridLayout = new QGridLayout(starredGridWidget);
    starredGridLayout->setSpacing(20);
    starredGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    
    starredScrollArea->setWidget(starredGridWidget);
    layout->addWidget(starredScrollArea);
}

void LauncherWindow::populateRecentGrid()
{
    if (!recentGridLayout || !recentScrollArea || !recentScrollArea->viewport() || !notebookManager) return;
    
    // Clear existing widgets
    QLayoutItem *child;
    while ((child = recentGridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    
    QStringList recentPaths = notebookManager->getRecentNotebooks();
    
    // Calculate adaptive columns based on available width
    int availableWidth = recentScrollArea->viewport()->width() - 40;
    int spacing = recentGridLayout->spacing();
    int minButtonWithSpacing = MIN_BUTTON_WIDTH + spacing;
    
    // Calculate column count
    int adaptiveColumns = qMax(2, qMin(4, availableWidth / minButtonWithSpacing));
    lastColumnCount = adaptiveColumns;
    lastCalculatedWidth = availableWidth;
    
    // Calculate flexible button width to fill available space evenly
    int totalSpacing = (adaptiveColumns - 1) * spacing;
    int flexibleWidth = (availableWidth - totalSpacing) / adaptiveColumns;
    flexibleWidth = qMax(MIN_BUTTON_WIDTH, flexibleWidth);
    
    // Scale height proportionally to width
    int flexibleHeight = BUTTON_HEIGHT + (flexibleWidth - MIN_BUTTON_WIDTH) / 3;
    
    int row = 0, col = 0;
    
    for (const QString &path : recentPaths) {
        if (path.isEmpty()) continue;
        
        QPushButton *button = createNotebookButton(path, false);
        button->setFixedSize(flexibleWidth, flexibleHeight);
        recentGridLayout->addWidget(button, row, col);
        
        col++;
        if (col >= adaptiveColumns) {
            col = 0;
            row++;
        }
    }
}

void LauncherWindow::populateStarredGrid()
{
    if (!starredGridLayout || !starredScrollArea || !starredScrollArea->viewport() || !notebookManager) return;
    
    // Clear existing widgets
    QLayoutItem *child;
    while ((child = starredGridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    
    QStringList starredPaths = notebookManager->getStarredNotebooks();
    
    // Use the same calculated values as recent grid for consistency
    int availableWidth = lastCalculatedWidth > 0 ? lastCalculatedWidth : (starredScrollArea->viewport()->width() - 40);
    int spacing = starredGridLayout->spacing();
    int adaptiveColumns = lastColumnCount > 0 ? lastColumnCount : 3;
    
    // Calculate flexible button width
    int totalSpacing = (adaptiveColumns - 1) * spacing;
    int flexibleWidth = (availableWidth - totalSpacing) / adaptiveColumns;
    flexibleWidth = qMax(MIN_BUTTON_WIDTH, flexibleWidth);
    
    // Scale height proportionally
    int flexibleHeight = BUTTON_HEIGHT + (flexibleWidth - MIN_BUTTON_WIDTH) / 3;
    
    int row = 0, col = 0;
    
    for (const QString &path : starredPaths) {
        if (path.isEmpty()) continue;
        
        QPushButton *button = createNotebookButton(path, true);
        button->setFixedSize(flexibleWidth, flexibleHeight);
        starredGridLayout->addWidget(button, row, col);
        
        col++;
        if (col >= adaptiveColumns) {
            col = 0;
            row++;
        }
    }
}

QPushButton* LauncherWindow::createNotebookButton(const QString &path, bool isStarred)
{
    QPushButton *button = new QPushButton();
    // Size will be set by populateRecentGrid/populateStarredGrid for flexibility
    button->setObjectName("notebookButton");
    button->setProperty("notebookPath", path);
    button->setProperty("isStarred", isStarred);
    
    // Enable context menu
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(button, &QPushButton::customContextMenuRequested, this, &LauncherWindow::onNotebookRightClicked);
    
    // Main click handler
    if (isStarred) {
        connect(button, &QPushButton::clicked, this, &LauncherWindow::onStarredNotebookClicked);
    } else {
        connect(button, &QPushButton::clicked, this, &LauncherWindow::onRecentNotebookClicked);
    }
    
    // Create layout for button content
    QVBoxLayout *buttonLayout = new QVBoxLayout(button);
    buttonLayout->setContentsMargins(10, 10, 10, 10);
    buttonLayout->setSpacing(8);
    
    // Cover image - use stretch factor for flexible sizing
    QLabel *coverLabel = new QLabel();
    coverLabel->setMinimumHeight(COVER_HEIGHT);
    coverLabel->setAlignment(Qt::AlignCenter);
    coverLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Apply theme-appropriate styling for cover
    bool isDarkModeActive = isDarkMode();
    QString coverBg = isDarkModeActive ? "#2b2b2b" : "white";
    QString coverBorder = isDarkModeActive ? "#555555" : "#ddd";
    coverLabel->setStyleSheet(QString("border: 1px solid %1; border-radius: 0px; background: %2;").arg(coverBorder).arg(coverBg));
    
    // Set scaling mode to fill the entire area
    coverLabel->setScaledContents(true);
    
    QString coverPath;
    
    // ✅ Safety check for notebookManager
    if (notebookManager) {
        coverPath = notebookManager->getCoverImagePathForNotebook(path);
        
        // ✅ If cover doesn't exist, try to regenerate it (without canvas - uses saved files)
        if (coverPath.isEmpty() && QFile::exists(path)) {
            notebookManager->generateAndSaveCoverPreview(path, nullptr);
            coverPath = notebookManager->getCoverImagePathForNotebook(path);
        }
    }
    
    if (!coverPath.isEmpty()) {
        // Use cached pixmap if available to prevent memory leaks
        QString cacheKey = QString("%1_cropped").arg(coverPath);
        
        QPixmap finalPixmap;
        if (pixmapCache.contains(cacheKey)) {
            // Use cached version
            finalPixmap = pixmapCache.value(cacheKey);
        } else {
            // Load and process new pixmap
            QPixmap coverPixmap(coverPath);
            if (!coverPixmap.isNull()) {
                // Crop blank margins from the thumbnail first
                QPixmap croppedPixmap = cropBlankMargins(coverPixmap);
                
                // Scale to a reasonable size for caching
                finalPixmap = croppedPixmap.scaled(MAX_BUTTON_WIDTH, COVER_HEIGHT, 
                    Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                
                // Cache the processed pixmap (limit cache size)
                if (pixmapCache.size() < 30) {
                    pixmapCache.insert(cacheKey, finalPixmap);
                }
            }
        }
        
        if (!finalPixmap.isNull()) {
            coverLabel->setPixmap(finalPixmap);
        } else {
            coverLabel->setText(tr("No Preview"));
            QString textColor = isDarkModeActive ? "#cccccc" : "#666";
            coverLabel->setStyleSheet(coverLabel->styleSheet() + QString(" color: %1;").arg(textColor));
        }
    } else {
        coverLabel->setText(tr("No Preview"));
        QString textColor = isDarkModeActive ? "#cccccc" : "#666";
        coverLabel->setStyleSheet(coverLabel->styleSheet() + QString(" color: %1;").arg(textColor));
    }
    
    buttonLayout->addWidget(coverLabel, 1); // Stretch factor 1
    
    // Title - use notebook manager if available, otherwise fall back to file name
    QString displayName = notebookManager ? notebookManager->getNotebookDisplayName(path) 
                                          : QFileInfo(path).fileName();
    QLabel *titleLabel = new QLabel(displayName);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setWordWrap(false);
    titleLabel->setMaximumHeight(24);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    
    // Store full name for later eliding when button size is known
    titleLabel->setProperty("fullName", displayName);
    buttonLayout->addWidget(titleLabel);
    
    return button;
}

QPixmap LauncherWindow::cropBlankMargins(const QPixmap &pixmap) const
{
    if (pixmap.isNull()) return pixmap;
    
    QImage image = pixmap.toImage();
    int width = image.width();
    int height = image.height();
    
    if (width == 0 || height == 0) return pixmap;
    
    // Get the background color from corners (assuming blank areas are uniform)
    QColor bgColor = image.pixelColor(0, 0);
    
    // Define tolerance for "blank" detection (colors close to background)
    auto isBlankColor = [&bgColor](const QColor &c) {
        int tolerance = 30;
        return qAbs(c.red() - bgColor.red()) < tolerance &&
               qAbs(c.green() - bgColor.green()) < tolerance &&
               qAbs(c.blue() - bgColor.blue()) < tolerance;
    };
    
    // Find left boundary (first non-blank column)
    int left = 0;
    for (int x = 0; x < width; ++x) {
        bool columnIsBlank = true;
        for (int y = 0; y < height; y += 5) { // Sample every 5 pixels for speed
            if (!isBlankColor(image.pixelColor(x, y))) {
                columnIsBlank = false;
                break;
            }
        }
        if (!columnIsBlank) {
            left = x;
            break;
        }
    }
    
    // Find right boundary (last non-blank column)
    int right = width - 1;
    for (int x = width - 1; x >= 0; --x) {
        bool columnIsBlank = true;
        for (int y = 0; y < height; y += 5) {
            if (!isBlankColor(image.pixelColor(x, y))) {
                columnIsBlank = false;
                break;
            }
        }
        if (!columnIsBlank) {
            right = x;
            break;
        }
    }
    
    // Add small padding
    int padding = 5;
    left = qMax(0, left - padding);
    right = qMin(width - 1, right + padding);
    
    // Only crop if we found significant blank margins (at least 10% on each side)
    int cropWidth = right - left + 1;
    if (cropWidth < width * 0.5) {
        // Too much cropping, might remove important content - return original
        return pixmap;
    }
    
    if (left > width * 0.1 || (width - right) > width * 0.1) {
        // Significant margins found, crop them
        QImage croppedImage = image.copy(left, 0, cropWidth, height);
        return QPixmap::fromImage(croppedImage);
    }
    
    return pixmap;
}

void LauncherWindow::onNewNotebookClicked()
{
    // Try to find existing MainWindow first
    MainWindow *existingMainWindow = findExistingMainWindow();
    MainWindow *targetMainWindow = nullptr;
    
    if (existingMainWindow) {
        // Use existing MainWindow and add new tab
        targetMainWindow = existingMainWindow;
        targetMainWindow->show();
        targetMainWindow->raise();
        targetMainWindow->activateWindow();
        
        // Always create a new tab for the new document
        targetMainWindow->addNewTab();
    } else {
        // Create a new MainWindow with a blank notebook
        // Phase 3.0.4: Use static flag for viewport architecture
        targetMainWindow = new MainWindow(MainWindow::s_useNewViewport);
        
        // Connect to handle when MainWindow closes
        // Use QPointer to safely check if launcher still exists
        QPointer<LauncherWindow> launcherPtr = this;
        connect(targetMainWindow, &MainWindow::destroyed, this, [launcherPtr]() {
            if (!launcherPtr) return; // Launcher was destroyed
            // Only show launcher if no other MainWindows exist
            if (!launcherPtr->findExistingMainWindow()) {
                launcherPtr->show();
                launcherPtr->refreshRecentNotebooks();
            }
        });
    }
    
    // Preserve window state
    preserveWindowState(targetMainWindow, existingMainWindow != nullptr);
    
    // Hide the launcher
    hide();
}

void LauncherWindow::onOpenPdfClicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, 
        tr("Open PDF or PowerPoint File"), 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Documents (*.pdf *.ppt *.pptx *.odp);;PDF Files (*.pdf);;PowerPoint Files (*.ppt *.pptx);;OpenDocument Presentation (*.odp)"));
    
    if (!filePath.isEmpty()) {
        // Check if the file needs conversion (PPT/PPTX/ODP)
        QString pdfPath = filePath;
        
        if (DocumentConverter::needsConversion(filePath)) {
            // Check if LibreOffice is available
            if (!DocumentConverter::isLibreOfficeAvailable()) {
                QMessageBox::warning(this, 
                    tr("LibreOffice Required"), 
                    DocumentConverter::getInstallationInstructions());
                return;
            }
            
            // Show progress dialog
            QProgressDialog progressDialog(
                tr("Converting %1 to PDF...").arg(QFileInfo(filePath).fileName()),
                QString(), 0, 0, this);
            progressDialog.setWindowModality(Qt::WindowModal);
            progressDialog.setCancelButton(nullptr);
            progressDialog.setMinimumDuration(0);
            progressDialog.show();
            QCoreApplication::processEvents();
            
            // Perform conversion (will save next to original file by default)
            DocumentConverter converter(this);
            DocumentConverter::ConversionStatus status;
            pdfPath = converter.convertToPdf(filePath, status);
            
            progressDialog.close();
            
            if (status != DocumentConverter::Success || pdfPath.isEmpty()) {
                QString errorMsg = tr("Failed to convert PowerPoint to PDF.\n\n");
                
                if (status == DocumentConverter::LibreOfficeNotFound) {
                    errorMsg += DocumentConverter::getInstallationInstructions();
                } else {
                    errorMsg += tr("Error: %1").arg(converter.getLastError());
                }
                
                QMessageBox::critical(this, tr("Conversion Failed"), errorMsg);
                return;
            }
        }
        
        // Continue with normal PDF loading
        // Try to find existing MainWindow first
        MainWindow *existingMainWindow = findExistingMainWindow();
        MainWindow *targetMainWindow = nullptr;
        
        if (existingMainWindow) {
            // Use existing MainWindow and add new tab
            targetMainWindow = existingMainWindow;
            targetMainWindow->show();
            targetMainWindow->raise();
            targetMainWindow->activateWindow();
            
            // Always create a new tab for the new document
            targetMainWindow->addNewTab();
        } else {
            // Create new MainWindow
            // Phase 3.0.4: Use static flag for viewport architecture
            targetMainWindow = new MainWindow(MainWindow::s_useNewViewport);
            
            // Connect to handle when MainWindow closes
            // Use QPointer to safely check if launcher still exists
            QPointer<LauncherWindow> launcherPtr = this;
            connect(targetMainWindow, &MainWindow::destroyed, this, [launcherPtr]() {
                if (!launcherPtr) return; // Launcher was destroyed
                if (!launcherPtr->findExistingMainWindow()) {
                    launcherPtr->show();
                    launcherPtr->refreshRecentNotebooks();
                }
            });
        }
        
        // Preserve window state
        preserveWindowState(targetMainWindow, existingMainWindow != nullptr);
        
        // Hide launcher
        hide();
        
        // Use the same approach as file explorer integration - call openPdfFile directly
        // This will show the proper dialog and handle PDF linking correctly
        // Use QPointer to safely check if MainWindow still exists when timer fires
        QPointer<MainWindow> mainWindowPtr = targetMainWindow;
        QTimer::singleShot(100, [mainWindowPtr, pdfPath]() {
            if (mainWindowPtr) {
                mainWindowPtr->openPdfFile(pdfPath);
            }
        });
    }
}

void LauncherWindow::onOpenNotebookClicked()
{
    QString spnPath = QFileDialog::getOpenFileName(this, 
        tr("Open SpeedyNote Notebook"), 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("SpeedyNote Files (*.spn)"));
    
    if (!spnPath.isEmpty()) {
        openNotebook(spnPath);
    }
}

MainWindow* LauncherWindow::findExistingMainWindow()
{
    // Find existing MainWindow among all top-level widgets
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        MainWindow *mainWindow = qobject_cast<MainWindow*>(widget);
        if (mainWindow) {
            return mainWindow;
        }
    }
    return nullptr;
}

void LauncherWindow::preserveWindowState(QWidget* targetWindow, bool isExistingWindow)
{
    if (!targetWindow) return;
    
    if (isExistingWindow) {
        // For existing windows, just show them without changing their size/position
        if (targetWindow->isMaximized()) {
            targetWindow->showMaximized();
        } else if (targetWindow->isFullScreen()) {
            targetWindow->showFullScreen();
        } else {
            targetWindow->show();
        }
    } else {
        // For new windows, apply launcher's window state
        if (isMaximized()) {
            targetWindow->showMaximized();
        } else if (isFullScreen()) {
            targetWindow->showFullScreen();
        } else {
            targetWindow->resize(size());
            targetWindow->move(pos());
            targetWindow->show();
        }
    }
}

void LauncherWindow::onRecentNotebookClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString path = button->property("notebookPath").toString();
        openNotebook(path);
    }
}

void LauncherWindow::onStarredNotebookClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString path = button->property("notebookPath").toString();
        openNotebook(path);
    }
}

void LauncherWindow::openNotebook(const QString &path)
{
    if (path.isEmpty()) return;
    
    // Try to find existing MainWindow first
    MainWindow *existingMainWindow = findExistingMainWindow();
    MainWindow *targetMainWindow = nullptr;
    
    // Check if this .spn notebook is already open in an existing MainWindow
    if (existingMainWindow && path.endsWith(".spn", Qt::CaseInsensitive)) {
        // Check for duplicate before creating new tab
        if (existingMainWindow->switchToExistingNotebook(path)) {
            // Notebook is already open - just show the window
            existingMainWindow->show();
            existingMainWindow->raise();
            existingMainWindow->activateWindow();
            hide();
            return;
        }
    }
    
    if (existingMainWindow) {
        // Use existing MainWindow and add new tab
        targetMainWindow = existingMainWindow;
        targetMainWindow->show();
        targetMainWindow->raise();
        targetMainWindow->activateWindow();
        
        // Create a new tab for the new document
        targetMainWindow->addNewTab();
    } else {
        // Create new MainWindow
        // Phase 3.0.4: Use static flag for viewport architecture
        targetMainWindow = new MainWindow(MainWindow::s_useNewViewport);
        
        // Connect to handle when MainWindow closes
        // Use QPointer to safely check if launcher still exists
        QPointer<LauncherWindow> launcherPtr = this;
        connect(targetMainWindow, &MainWindow::destroyed, this, [launcherPtr]() {
            if (!launcherPtr) return; // Launcher was destroyed
            // Only show launcher if no other MainWindows exist
            if (!launcherPtr->findExistingMainWindow()) {
                launcherPtr->show();
                launcherPtr->refreshRecentNotebooks();
                launcherPtr->refreshStarredNotebooks();
            }
        });
    }
    
    // Preserve window state when showing MainWindow
    preserveWindowState(targetMainWindow, existingMainWindow != nullptr);
    
    // Hide launcher
    hide();
    
    // Open the notebook
    if (path.endsWith(".spn", Qt::CaseInsensitive)) {
        targetMainWindow->openSpnPackage(path);
    } else {
        // Handle folder-based notebooks
        InkCanvas *canvas = targetMainWindow->currentCanvas();
        if (canvas) {
            canvas->setSaveFolder(path);
            if (!targetMainWindow->showLastAccessedPageDialog(canvas)) {
                targetMainWindow->switchPage(1);
                if (targetMainWindow->pageInput) {
                    targetMainWindow->pageInput->setValue(1);
                }
            } else {
                if (targetMainWindow->pageInput) {
                    targetMainWindow->pageInput->setValue(targetMainWindow->getCurrentPageForCanvas(canvas) + 1);
                }
            }
            targetMainWindow->updateTabLabel();
            targetMainWindow->updateBookmarkButtonState();
        }
    }
}

void LauncherWindow::onNotebookRightClicked(const QPoint &pos)
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;
    
    QString path = button->property("notebookPath").toString();
    bool isStarred = button->property("isStarred").toBool();
    rightClickedPath = path;
    
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    
    // Star/Unstar action
    QAction *starAction;
    if (isStarred) {
        starAction = menu->addAction(loadThemedIcon("star_reversed"), tr("Remove from Starred"));
    } else {
        starAction = menu->addAction(loadThemedIcon("star"), tr("Add to Starred"));
    }
    connect(starAction, &QAction::triggered, this, [this]() {
        toggleStarredStatus(rightClickedPath);
    });
    
    menu->addSeparator();
    
    // Delete from Recent (only for recent notebooks, not starred)
    if (!isStarred) {
        QAction *deleteAction = menu->addAction(loadThemedIcon("cross"), tr("Remove from Recent"));
        connect(deleteAction, &QAction::triggered, this, [this]() {
            removeFromRecent(rightClickedPath);
        });
        
        menu->addSeparator();
    }
    
    // Open in file explorer
    QAction *explorerAction = menu->addAction(loadThemedIcon("folder"), tr("Show in Explorer"));
    connect(explorerAction, &QAction::triggered, this, [path]() {
        QString dirPath;
        if (path.endsWith(".spn", Qt::CaseInsensitive)) {
            dirPath = QFileInfo(path).absolutePath();
        } else {
            dirPath = path;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });
    
    menu->popup(button->mapToGlobal(pos));
}

void LauncherWindow::toggleStarredStatus(const QString &path)
{
    if (!notebookManager) return;
    
    if (notebookManager->isStarred(path)) {
        notebookManager->removeStarred(path);
    } else {
        notebookManager->addStarred(path);
    }
    
    // Refresh both grids
    refreshRecentNotebooks();
    refreshStarredNotebooks();
}

void LauncherWindow::removeFromRecent(const QString &path)
{
    if (!notebookManager) return;
    
    // Remove from recent notebooks
    notebookManager->removeRecentNotebook(path);
    
    // Refresh the recent grid to show the change
    refreshRecentNotebooks();
}

void LauncherWindow::refreshRecentNotebooks()
{
    // Always update the data, but only refresh UI if visible
    if (isVisible()) {
        // Just repopulate - don't clear cache as it might be needed
        populateRecentGrid();
        
        // Simple UI update
        if (recentScrollArea) {
            recentScrollArea->update();
        }
        update();
    }
    // If hidden, the data is still updated and showEvent will refresh UI
}

void LauncherWindow::refreshStarredNotebooks()
{
    // Always update the data, but only refresh UI if visible
    if (isVisible()) {
        // Just repopulate - don't clear cache as it might be needed
        populateStarredGrid();
        
        // Simple UI update
        if (starredScrollArea) {
            starredScrollArea->update();
        }
        update();
    }
    // If hidden, the data is still updated and showEvent will refresh UI
}

void LauncherWindow::applyModernStyling()
{
    // Detect dark mode using the isDarkMode() method
    bool isDarkModeActive = isDarkMode();
    
    // Apply theme-appropriate styling
    QString mainBg = isDarkModeActive ? "#2b2b2b" : "#f8f9fa";
    QString cardBg = isDarkModeActive ? "#3c3c3c" : "#ffffff";
    QString borderColor = isDarkModeActive ? "#555555" : "#e9ecef";
    QString textColor = isDarkModeActive ? "#ffffff" : "#212529";
    QString secondaryTextColor = isDarkModeActive ? "#cccccc" : "#6c757d";
    QString hoverBorderColor = isDarkModeActive ? "#0078d4" : "#007bff";
    QString selectedBg = isDarkModeActive ? "#0078d4" : "#007bff";
    QString hoverBg = isDarkModeActive ? "#404040" : "#e9ecef";
    QString scrollBg = isDarkModeActive ? "#2b2b2b" : "#f8f9fa";
    QString scrollHandle = isDarkModeActive ? "#666666" : "#ced4da";
    QString scrollHandleHover = isDarkModeActive ? "#777777" : "#adb5bd";
    
    // Main window styling
    setStyleSheet(QString(R"(
        QMainWindow {
            background-color: %1;
        }
        
        QListWidget#sidebarTabList {
            background-color: %2;
            border: none;
            border-right: 1px solid %3;
            outline: none;
            font-size: 14px;
            padding: 10px 0px;
        }
        
        QListWidget#sidebarTabList::item {
            margin: 4px 8px;
            padding-left: 20px;
            border-radius: 0px;
        }
        
        QListWidget#sidebarTabList::item:selected {
            background-color: %4;
            color: white;
        }
        
        QListWidget#sidebarTabList::item:hover:!selected {
            background-color: %5;
        }
        
        QLabel#titleLabel {
            font-size: 24px;
            font-weight: bold;
            margin-bottom: 10px;
        }
        
        QLabel#descLabel {
            font-size: 14px;
            margin-bottom: 20px;
        }
        
        QPushButton#primaryButton {
            background-color: %4;
            border: none;
            border-radius: 0px;
            color: white;
            font-size: 16px;
            font-weight: bold;
            padding: 15px 30px;
        }
        
        QPushButton#primaryButton:hover {
            background-color: %6;
        }
        
        QPushButton#primaryButton:pressed {
            background-color: %7;
        }
        
        QPushButton#notebookButton {
            background-color: %2;
            border: 1px solid %3;
            border-radius: 0px;
            padding: 0px;
        }
        
        QPushButton#notebookButton:hover {
            border-color: %8;
        }
        
        QPushButton#notebookButton:pressed {
            background-color: %5;
        }
        
        QScrollArea {
            border: none;
            background-color: transparent;
        }
        
        QScrollBar:vertical {
            background-color: %9;
            width: 12px;
            border-radius: 0px;
        }
        
        QScrollBar::handle:vertical {
            background-color: %10;
            border-radius: 0px;
            min-height: 30px;
        }
        
        QScrollBar::handle:vertical:hover {
            background-color: %11;
        }
        
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            border: none;
            background: none;
        }
    )").arg(mainBg)                                    // %1
       .arg(cardBg)                                    // %2
       .arg(borderColor)                               // %3
       .arg(selectedBg)                                // %4
       .arg(hoverBg)                                   // %5
       .arg(isDarkModeActive ? "#005a9e" : "#0056b3")        // %6
       .arg(isDarkModeActive ? "#004578" : "#004085")        // %7
       .arg(hoverBorderColor)                          // %8
       .arg(scrollBg)                                  // %9
       .arg(scrollHandle)                              // %10
       .arg(scrollHandleHover));                       // %11
}

void LauncherWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    
    if (!event->oldSize().isValid()) return;
    if (!recentScrollArea || !recentScrollArea->viewport() || !recentGridLayout) return;
    
    int widthDiff = abs(event->size().width() - event->oldSize().width());
    if (widthDiff < 5) return; // Ignore tiny changes
    
    // Calculate what column count we would need
    int availableWidth = recentScrollArea->viewport()->width() - 40;
    int spacing = recentGridLayout->spacing();
    int minButtonWithSpacing = MIN_BUTTON_WIDTH + spacing;
    int newColumnCount = qMax(2, qMin(4, availableWidth / minButtonWithSpacing));
    
    if (lastColumnCount > 0 && newColumnCount != lastColumnCount) {
        // Column count changed - need to repopulate
        lastColumnCount = newColumnCount;
        lastCalculatedWidth = availableWidth;
        populateRecentGrid();
        populateStarredGrid();
    } else {
        // Same column count - just resize existing buttons
        resizeGridButtons();
    }
}

void LauncherWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    
    // Rebuild UI when becoming visible since hideEvent clears everything
    // Use direct calls instead of timer to avoid timer accumulation
    populateRecentGrid();
    populateStarredGrid();
    
    // Simple UI update
    update();
}

void LauncherWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    
    // Clear grids when hiding to free UI widgets immediately
    // This prevents widget accumulation when launcher is opened/closed repeatedly
    clearRecentGrid();
    clearStarredGrid();
    
    // Don't clear pixmap cache - keep thumbnails cached to avoid reload/memory churn
    // The cache is size-limited (50 items) so it won't grow unbounded
    
    // Reset layout cache to force recalculation next time
    lastCalculatedWidth = 0;
    lastColumnCount = 0;
}

#ifdef Q_OS_WIN
bool LauncherWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    // Detect Windows theme changes at runtime
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        
        // WM_SETTINGCHANGE (0x001A) is sent when system settings change
        if (msg->message == 0x001A) {
            // Check if this is a theme-related setting change
            if (msg->lParam != 0) {
                const wchar_t *lparam = reinterpret_cast<const wchar_t *>(msg->lParam);
                if (lparam && wcscmp(lparam, L"ImmersiveColorSet") == 0) {
                    // Windows theme changed - update Qt palette and our UI
                    // Use a small delay to ensure registry has been updated
                    // Use QPointer to safely check if launcher still exists
                    QPointer<LauncherWindow> launcherPtr = this;
                    QTimer::singleShot(100, this, [launcherPtr]() {
                        MainWindow::updateApplicationPalette(); // Update Qt's global palette
                        if (launcherPtr) {
                            launcherPtr->applyModernStyling(); // Update our custom styling
                        }
                    });
                }
            }
        }
    }
    
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

bool LauncherWindow::isDarkMode() const
{
    QSettings settings("SpeedyNote", "App");
    if (settings.contains("useDarkMode")) {
        return settings.value("useDarkMode", false).toBool();
    } else {
        // Auto-detect system dark mode
#ifdef Q_OS_WIN
        // On Windows, read the registry to detect dark mode
        // This works on Windows 10 1809+ and Windows 11
        QSettings winSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                              QSettings::NativeFormat);
        
        // AppsUseLightTheme: 0 = dark mode, 1 = light mode
        // If the key doesn't exist (older Windows), default to light mode
        int appsUseLightTheme = winSettings.value("AppsUseLightTheme", 1).toInt();
        return (appsUseLightTheme == 0);
#else
        // On Linux and other platforms, use palette-based detection
        QPalette palette = QApplication::palette();
        QColor windowColor = palette.color(QPalette::Window);
        return windowColor.lightness() < 128;
#endif
    }
}

QIcon LauncherWindow::loadThemedIcon(const QString& baseName)
{
    QString path = isDarkMode()
        ? QString(":/resources/icons/%1_reversed.png").arg(baseName)
        : QString(":/resources/icons/%1.png").arg(baseName);
    return QIcon(path);
}

void LauncherWindow::onTabChanged(int index)
{
    // Use QPointer to safely check if launcher still exists when timer fires
    QPointer<LauncherWindow> launcherPtr = this;
    
    // Handle direct actions for certain tabs
    switch (index) {
        case 0: // Return tab
            {
                MainWindow *existingMainWindow = findExistingMainWindow();
                if (existingMainWindow) {
                    // Show and return to existing MainWindow (preserve its existing state)
                    preserveWindowState(existingMainWindow, true);
                    hide();
                } else {
                    // No existing window, stay on launcher
                    QMessageBox::information(this, tr("No Document"), 
                        tr("There is no previous document to return to."));
                }
                // Reset to Recent tab to allow clicking Return again
                QTimer::singleShot(50, this, [launcherPtr]() {
                    if (launcherPtr && launcherPtr->tabList) {
                        launcherPtr->tabList->setCurrentRow(4);
                    }
                });
            }
            break;
            
        case 1: // New tab - direct action
            onNewNotebookClicked();
            // Reset to Recent tab to allow clicking New again
            QTimer::singleShot(50, this, [launcherPtr]() {
                if (launcherPtr && launcherPtr->tabList) {
                    launcherPtr->tabList->setCurrentRow(4);
                }
            });
            break;
            
        case 2: // Open PDF tab - direct action
            onOpenPdfClicked();
            // Reset to Recent tab to allow clicking Open PDF again
            QTimer::singleShot(50, this, [launcherPtr]() {
                if (launcherPtr && launcherPtr->tabList) {
                    launcherPtr->tabList->setCurrentRow(4);
                }
            });
            break;
            
        case 3: // Open Notebook tab - direct action
            onOpenNotebookClicked();
            // Reset to Recent tab to allow clicking Open Notebook again
            QTimer::singleShot(50, this, [launcherPtr]() {
                if (launcherPtr && launcherPtr->tabList) {
                    launcherPtr->tabList->setCurrentRow(4);
                }
            });
            break;
            
        case 4: // Recent tab - show content
        case 5: // Starred tab - show content
        default:
            // Show the corresponding content page
            contentStack->setCurrentIndex(index);
            break;
    }
}

void LauncherWindow::resizeGridButtons()
{
    if (!recentScrollArea || !recentScrollArea->viewport() || !recentGridLayout || !starredGridLayout) return;
    
    // Resize existing buttons without repopulating (prevents flickering)
    int availableWidth = recentScrollArea->viewport()->width() - 40;
    int spacing = recentGridLayout->spacing();
    int adaptiveColumns = lastColumnCount > 0 ? lastColumnCount : 3;
    
    // Calculate flexible button width
    int totalSpacing = (adaptiveColumns - 1) * spacing;
    int flexibleWidth = (availableWidth - totalSpacing) / adaptiveColumns;
    flexibleWidth = qMax(MIN_BUTTON_WIDTH, flexibleWidth);
    
    // Scale height proportionally
    int flexibleHeight = BUTTON_HEIGHT + (flexibleWidth - MIN_BUTTON_WIDTH) / 3;
    
    // Resize all buttons in recent grid
    for (int i = 0; i < recentGridLayout->count(); ++i) {
        QLayoutItem *item = recentGridLayout->itemAt(i);
        if (item && item->widget()) {
            item->widget()->setFixedSize(flexibleWidth, flexibleHeight);
        }
    }
    
    // Resize all buttons in starred grid
    for (int i = 0; i < starredGridLayout->count(); ++i) {
        QLayoutItem *item = starredGridLayout->itemAt(i);
        if (item && item->widget()) {
            item->widget()->setFixedSize(flexibleWidth, flexibleHeight);
        }
    }
    
    lastCalculatedWidth = availableWidth;
}

void LauncherWindow::clearRecentGrid()
{
    if (!recentGridLayout) return;
    
    // Clear all widgets from the recent grid
    QLayoutItem *child;
    while ((child = recentGridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            // ✅ No need to disconnect - deleteLater() handles signal cleanup automatically
            child->widget()->deleteLater();
        }
        delete child;
    }
}

void LauncherWindow::clearStarredGrid()
{
    if (!starredGridLayout) return;
    
    // Clear all widgets from the starred grid
    QLayoutItem *child;
    while ((child = starredGridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            // ✅ No need to disconnect - deleteLater() handles signal cleanup automatically
            child->widget()->deleteLater();
        }
        delete child;
    }
}

void LauncherWindow::clearPixmapCache()
{
    // Clear the pixmap cache to free memory
    pixmapCache.clear();
}

void LauncherWindow::invalidatePixmapCacheForPath(const QString &path)
{
    // Remove all cache entries that match this path (different sizes)
    QStringList keysToRemove;
    for (auto it = pixmapCache.constBegin(); it != pixmapCache.constEnd(); ++it) {
        if (it.key().startsWith(path + "_")) {
            keysToRemove.append(it.key());
        }
    }
    
    for (const QString &key : keysToRemove) {
        pixmapCache.remove(key);
    }
}
