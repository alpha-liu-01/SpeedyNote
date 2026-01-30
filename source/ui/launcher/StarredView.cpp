#include "StarredView.h"
#include "StarredListView.h"
#include "StarredModel.h"
#include "NotebookCardDelegate.h"
#include "FolderHeaderDelegate.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QShowEvent>

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
    
    // === Model ===
    m_model = new StarredModel(this);
    
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
    connect(m_listView, &StarredListView::notebookLongPressed,
            this, &StarredView::onNotebookLongPressed);
    connect(m_listView, &StarredListView::folderClicked,
            this, &StarredView::onFolderClicked);
    connect(m_listView, &StarredListView::folderLongPressed,
            this, &StarredView::onFolderLongPressed);
    
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
        
        // Trigger repaint of visible items
        m_listView->viewport()->update();
    }
}

void StarredView::updateEmptyState()
{
    bool isEmpty = m_model->isEmpty();
    m_listView->setVisible(!isEmpty);
    m_emptyLabel->setVisible(isEmpty);
}

void StarredView::onNotebookClicked(const QString& bundlePath)
{
    emit notebookClicked(bundlePath);
}

void StarredView::onNotebookLongPressed(const QString& bundlePath, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
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
