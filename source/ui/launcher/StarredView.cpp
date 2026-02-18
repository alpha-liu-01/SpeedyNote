#include "StarredView.h"
#include "StarredListView.h"
#include "StarredModel.h"
#include "NotebookCardDelegate.h"
#include "FolderHeaderDelegate.h"
#include "FolderPickerDialog.h"
#include "../ThemeColors.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QMenu>

// ============================================================================
// CompositeStarredDelegate - Local delegate that dispatches to folder/card delegates
// ============================================================================

/**
 * @brief Composite delegate that handles both folder headers and notebook cards.
 * 
 * QListView only supports a single item delegate. This composite delegate
 * checks the ItemTypeRole and dispatches painting/sizeHint to the appropriate
 * specialized delegate (FolderHeaderDelegate or NotebookCardDelegate).
 * 
 * For folder headers, returns a wide sizeHint so they span the full viewport
 * width, forcing them onto their own row in IconMode.
 */
class CompositeStarredDelegate : public QStyledItemDelegate {
public:
    CompositeStarredDelegate(NotebookCardDelegate* cardDelegate,
                             FolderHeaderDelegate* folderDelegate,
                             QListView* listView,
                             QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_cardDelegate(cardDelegate)
        , m_folderDelegate(folderDelegate)
        , m_listView(listView)
    {
    }
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        int itemType = index.data(StarredModel::ItemTypeRole).toInt();
        
        if (itemType == StarredModel::FolderHeaderItem) {
            m_folderDelegate->paint(painter, option, index);
        } else {
            m_cardDelegate->paint(painter, option, index);
        }
    }
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        int itemType = index.data(StarredModel::ItemTypeRole).toInt();
        
        if (itemType == StarredModel::FolderHeaderItem) {
            // Folder headers should span the full viewport width
            // This forces them onto their own row in IconMode
            QSize baseSize = m_folderDelegate->sizeHint(option, index);
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
        m_folderDelegate->setDarkMode(dark);
    }

private:
    NotebookCardDelegate* m_cardDelegate;
    FolderHeaderDelegate* m_folderDelegate;
    QListView* m_listView;
};

// ============================================================================
// StarredView
// ============================================================================

StarredView::StarredView(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    
    // Initial load
    reload();
}

void StarredView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(CONTENT_MARGIN, CONTENT_MARGIN, 
                                   CONTENT_MARGIN, CONTENT_MARGIN);
    mainLayout->setSpacing(0);
    
    // === Select Mode Header (L-007) ===
    setupSelectModeHeader();
    mainLayout->addWidget(m_selectModeHeader);
    m_selectModeHeader->setVisible(false);  // Hidden by default
    
    // === Model ===
    m_model = new StarredModel(this);
    
    // Connect model reload signal to update empty state visibility
    // This ensures m_listView becomes visible when items are added to an empty model
    // (e.g., user stars a notebook while on Timeline view)
    connect(m_model, &StarredModel::dataReloaded, this, &StarredView::updateEmptyState);
    
    // === List View (create first so delegate can reference it) ===
    m_listView = new StarredListView(this);
    m_listView->setObjectName("StarredListView");
    
    // === Delegates ===
    m_cardDelegate = new NotebookCardDelegate(this);
    m_folderDelegate = new FolderHeaderDelegate(this);
    
    // Create composite delegate that handles both item types
    // Pass listView so folder headers can span viewport width
    auto* compositeDelegate = new CompositeStarredDelegate(
        m_cardDelegate, m_folderDelegate, m_listView, this);
    
    m_listView->setStarredModel(m_model);
    m_listView->setItemDelegate(compositeDelegate);
    
    // Connect thumbnail updates
    connect(NotebookLibrary::instance(), &NotebookLibrary::thumbnailUpdated,
            m_cardDelegate, &NotebookCardDelegate::invalidateThumbnail);
    
    // Connect list view signals
    connect(m_listView, &StarredListView::notebookClicked,
            this, &StarredView::onNotebookClicked);
    connect(m_listView, &StarredListView::notebookMenuRequested,
            this, &StarredView::onNotebookMenuRequested);
    connect(m_listView, &StarredListView::notebookLongPressed,
            this, &StarredView::onNotebookLongPressed);
    connect(m_listView, &StarredListView::folderClicked,
            this, &StarredView::onFolderClicked);
    connect(m_listView, &StarredListView::folderLongPressed,
            this, &StarredView::onFolderLongPressed);
    
    // Connect select mode signals (L-007)
    connect(m_listView, &StarredListView::selectModeChanged,
            this, &StarredView::onSelectModeChanged);
    connect(m_listView, &StarredListView::batchSelectionChanged,
            this, &StarredView::onBatchSelectionChanged);
    
    mainLayout->addWidget(m_listView, 1);
    
    // === Empty State Label ===
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName("EmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setText(tr("No starred notebooks yet.\n\nLong-press a notebook in Timeline\nand select \"Star\" to add it here."));
    
    QFont font = m_emptyLabel->font();
    font.setPointSize(12);
    m_emptyLabel->setFont(font);
    
    mainLayout->addWidget(m_emptyLabel, 1);
    
    // Initial state
    updateEmptyState();
}

void StarredView::reload()
{
    // ANDROID FIX: Only reload if visible to avoid visual artifacts
    // When NotebookLibrary::libraryChanged is emitted (e.g., when opening a notebook
    // updates lastAccessed time), rebuilding the entire view causes visual artifacts.
    // 
    // If not visible, defer the reload until the view becomes visible via showEvent.
    if (!isVisible()) {
        m_needsReload = true;
        return;
    }
    
    m_needsReload = false;
    
    // Model handles smart reload (checks content signature internally)
    m_model->reload();
    
    updateEmptyState();
}

void StarredView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Perform deferred reload if needed
    if (m_needsReload) {
        m_needsReload = false;
        m_model->reload();
        updateEmptyState();
    }
}

void StarredView::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        
        // Update delegates
        m_cardDelegate->setDarkMode(dark);
        m_folderDelegate->setDarkMode(dark);
        
        // Update empty label color
        QPalette pal = m_emptyLabel->palette();
        pal.setColor(QPalette::WindowText, dark ? QColor(150, 150, 150) : QColor(120, 120, 120));
        m_emptyLabel->setPalette(pal);
        
        // Update header button icons (always, so they're ready when shown)
        updateHeaderButtonIcons();
        
        // Update select mode header colors if visible
        if (m_selectModeHeader->isVisible()) {
            showSelectModeHeader(m_listView->selectionCount());
        }
        
        // Trigger repaint of visible items
        m_listView->viewport()->update();
    }
}

bool StarredView::isSelectModeActive() const
{
    return m_listView->isSelectMode();
}

void StarredView::exitSelectMode()
{
    m_listView->exitSelectMode();
}

void StarredView::scrollToFolder(const QString& folderName)
{
    if (folderName.isEmpty()) {
        return;
    }
    
    // Ensure folder is expanded so user can see its contents
    if (m_model->isFolderCollapsed(folderName)) {
        m_model->setFolderCollapsed(folderName, false);
    }
    
    // Find the row for this folder
    int row = m_model->rowForFolder(folderName);
    if (row >= 0) {
        QModelIndex folderIndex = m_model->index(row);
        // Scroll to make the folder visible at the top
        m_listView->scrollTo(folderIndex, QAbstractItemView::PositionAtTop);
    }
}

void StarredView::updateEmptyState()
{
    bool isEmpty = m_model->isEmpty();
    m_listView->setVisible(!isEmpty);
    m_emptyLabel->setVisible(isEmpty);
}

// -----------------------------------------------------------------------------
// Batch Select Mode (L-007)
// -----------------------------------------------------------------------------

void StarredView::setupSelectModeHeader()
{
    m_selectModeHeader = new QWidget(this);
    m_selectModeHeader->setFixedHeight(HEADER_HEIGHT);
    m_selectModeHeader->setObjectName("SelectModeHeader");
    
    auto* headerLayout = new QHBoxLayout(m_selectModeHeader);
    headerLayout->setContentsMargins(0, 0, 8, 8);
    headerLayout->setSpacing(8);
    
    // Back button (uses left_arrow.png icon - arrow pointing left)
    // Parent is m_selectModeHeader so it's properly contained in the header
    m_backButton = new QPushButton(m_selectModeHeader);
    m_backButton->setObjectName("BackButton");
    m_backButton->setFixedSize(40, 40);
    m_backButton->setFlat(true);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setIconSize(QSize(24, 24));
    
    connect(m_backButton, &QPushButton::clicked, this, [this]() {
        m_listView->exitSelectMode();
    });
    
    headerLayout->addWidget(m_backButton);
    
    // Selection count label
    m_selectionCountLabel = new QLabel(m_selectModeHeader);
    m_selectionCountLabel->setObjectName("SelectionCountLabel");
    
    QFont countFont = m_selectionCountLabel->font();
    countFont.setPointSize(14);
    countFont.setBold(true);
    m_selectionCountLabel->setFont(countFont);
    
    headerLayout->addWidget(m_selectionCountLabel, 1);  // Stretch
    
    // Overflow menu button (uses menu.png icon - three dots)
    m_overflowMenuButton = new QPushButton(m_selectModeHeader);
    m_overflowMenuButton->setObjectName("OverflowMenuButton");
    m_overflowMenuButton->setFixedSize(40, 40);
    m_overflowMenuButton->setFlat(true);
    m_overflowMenuButton->setCursor(Qt::PointingHandCursor);
    m_overflowMenuButton->setIconSize(QSize(24, 24));
    
    connect(m_overflowMenuButton, &QPushButton::clicked, this, &StarredView::showOverflowMenu);
    
    headerLayout->addWidget(m_overflowMenuButton);
    
    // Set initial icons based on current theme
    updateHeaderButtonIcons();
}

void StarredView::showSelectModeHeader(int count)
{
    // Update count label
    if (count == 1) {
        m_selectionCountLabel->setText(tr("1 selected"));
    } else {
        m_selectionCountLabel->setText(tr("%1 selected").arg(count));
    }
    
    // Update icons for current theme
    updateHeaderButtonIcons();
    
    // Update button styles (hover/press effects)
    QString buttonStyle = QString(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: %1; border-radius: 20px; }"
        "QPushButton:pressed { background: %2; border-radius: 20px; }"
    ).arg(ThemeColors::itemHover(m_darkMode).name(),
          ThemeColors::pressed(m_darkMode).name());
    
    m_backButton->setStyleSheet(buttonStyle);
    m_overflowMenuButton->setStyleSheet(buttonStyle);
    
    // Update label color
    QPalette labelPal = m_selectionCountLabel->palette();
    labelPal.setColor(QPalette::WindowText, ThemeColors::textPrimary(m_darkMode));
    m_selectionCountLabel->setPalette(labelPal);
    
    // Show header
    m_selectModeHeader->setVisible(true);
}

void StarredView::updateHeaderButtonIcons()
{
    // Update back button icon based on theme
    QString backIconPath = m_darkMode 
        ? ":/resources/icons/left_arrow_reversed.png" 
        : ":/resources/icons/left_arrow.png";
    m_backButton->setIcon(QIcon(backIconPath));
    
    // Update overflow menu button icon based on theme
    QString menuIconPath = m_darkMode 
        ? ":/resources/icons/menu_reversed.png" 
        : ":/resources/icons/menu.png";
    m_overflowMenuButton->setIcon(QIcon(menuIconPath));
}

void StarredView::hideSelectModeHeader()
{
    m_selectModeHeader->setVisible(false);
}

void StarredView::showOverflowMenu()
{
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, m_darkMode);
    
    int selectedCount = m_listView->selectionCount();
    
    // Select All / Deselect All
    QAction* selectAllAction = menu.addAction(tr("Select All"));
    connect(selectAllAction, &QAction::triggered, this, [this]() {
        m_listView->selectAll();
    });
    
    QAction* deselectAllAction = menu.addAction(tr("Deselect All"));
    deselectAllAction->setEnabled(selectedCount > 0);
    connect(deselectAllAction, &QAction::triggered, this, [this]() {
        m_listView->deselectAll();
    });
    
    menu.addSeparator();
    
    // Export submenu (Phase 3: Batch Operations)
    // Emits signals to Launcher which handles the dialogs
    QMenu* exportMenu = menu.addMenu(tr("Export"));
    ThemeColors::styleMenu(exportMenu, m_darkMode);
    exportMenu->setEnabled(selectedCount > 0);
    
    QAction* exportPdfAction = exportMenu->addAction(tr("To PDF..."));
    connect(exportPdfAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (!selected.isEmpty()) {
            emit exportToPdfRequested(selected);
            m_listView->exitSelectMode();
        }
    });
    
    QAction* exportSnbxAction = exportMenu->addAction(tr("To SNBX..."));
    connect(exportSnbxAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (!selected.isEmpty()) {
            emit exportToSnbxRequested(selected);
            m_listView->exitSelectMode();
        }
    });
    
    menu.addSeparator();
    
    // Move to Folder... (L-008: opens FolderPickerDialog)
    QAction* moveToFolderAction = menu.addAction(tr("Move to Folder..."));
    moveToFolderAction->setEnabled(selectedCount > 0);
    connect(moveToFolderAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (selected.isEmpty()) return;
        
        QString title = selected.size() == 1 
            ? tr("Move to Folder") 
            : tr("Move %1 notebooks to...").arg(selected.size());
        
        QString folder = FolderPickerDialog::getFolder(this, title);
        if (!folder.isEmpty()) {
            NotebookLibrary::instance()->moveNotebooksToFolder(selected, folder);
            m_listView->exitSelectMode();
        }
    });
    
    // Remove from Folder
    QAction* removeFromFolderAction = menu.addAction(tr("Remove from Folder"));
    removeFromFolderAction->setEnabled(selectedCount > 0);
    connect(removeFromFolderAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (!selected.isEmpty()) {
            NotebookLibrary::instance()->removeNotebooksFromFolder(selected);
            m_listView->exitSelectMode();
        }
    });
    
    menu.addSeparator();
    
    // Unstar Selected
    QAction* unstarAction = menu.addAction(tr("Unstar Selected"));
    unstarAction->setEnabled(selectedCount > 0);
    connect(unstarAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (!selected.isEmpty()) {
            NotebookLibrary::instance()->unstarNotebooks(selected);
            m_listView->exitSelectMode();
        }
    });
    
    menu.addSeparator();
    
    // Delete Selected (L-010: Batch Delete)
    // Note: exitSelectMode is NOT called here. The Launcher slot handles it
    // conditionally — only exiting if the user confirms the deletion dialog.
    QAction* deleteAction = menu.addAction(tr("Delete Selected"));
    deleteAction->setEnabled(selectedCount > 0);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        QStringList selected = m_listView->selectedBundlePaths();
        if (!selected.isEmpty()) {
            emit deleteNotebooksRequested(selected);
        }
    });
    
    // Show menu below the overflow button
    QPoint pos = m_overflowMenuButton->mapToGlobal(
        QPoint(m_overflowMenuButton->width(), m_overflowMenuButton->height()));
    menu.exec(pos);
}

void StarredView::onSelectModeChanged(bool active)
{
    if (active) {
        showSelectModeHeader(m_listView->selectionCount());
    } else {
        hideSelectModeHeader();
    }
}

void StarredView::onBatchSelectionChanged(int count)
{
    if (m_listView->isSelectMode()) {
        showSelectModeHeader(count);
    }
}

void StarredView::onNotebookClicked(const QString& bundlePath)
{
    emit notebookClicked(bundlePath);
}

void StarredView::onNotebookMenuRequested(const QString& bundlePath, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    // Emit signal for context menu (3-dot button, right-click, or long-press)
    emit notebookMenuRequested(bundlePath);
}

void StarredView::onNotebookLongPressed(const QString& bundlePath, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    
    // Enter batch select mode with this notebook as the first selection
    m_listView->enterSelectMode(bundlePath);
    
    // Also emit for any external handlers that might want to know
    emit notebookLongPressed(bundlePath);
}

void StarredView::onFolderClicked(const QString& folderName)
{
    Q_UNUSED(folderName)
    // Folder toggle is handled by StarredListView + StarredModel
    // This slot is for any additional handling if needed
}

void StarredView::onFolderLongPressed(const QString& folderName, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    
    // Don't emit for "Unfiled" pseudo-folder
    if (folderName != tr("Unfiled")) {
        emit folderLongPressed(folderName);
    }
}
