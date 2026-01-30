#include "SearchView.h"
#include "SearchListView.h"
#include "SearchModel.h"
#include "NotebookCardDelegate.h"
#include "../ThemeColors.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

// ============================================================================
// CompositeSearchDelegate - Handles section headers, folder items, notebook cards
// ============================================================================

/**
 * @brief Composite delegate for search results with section headers, folders, and notebooks.
 * 
 * Renders three types of items:
 * - Section headers ("FOLDERS", "NOTEBOOKS") - full width, gray text
 * - Folder items - simple list items with folder icon and arrow
 * - Notebook items - delegated to NotebookCardDelegate
 */
class CompositeSearchDelegate : public QStyledItemDelegate {
public:
    CompositeSearchDelegate(NotebookCardDelegate* cardDelegate,
                            QListView* listView,
                            QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_cardDelegate(cardDelegate)
        , m_listView(listView)
    {
        // Pre-load and pre-scale folder icons for efficiency
        QPixmap lightIcon(":/resources/icons/folder.png");
        QPixmap darkIcon(":/resources/icons/folder_reversed.png");
        
        if (!lightIcon.isNull()) {
            m_folderIconLight = lightIcon.scaled(FOLDER_ICON_SIZE, FOLDER_ICON_SIZE,
                Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!darkIcon.isNull()) {
            m_folderIconDark = darkIcon.scaled(FOLDER_ICON_SIZE, FOLDER_ICON_SIZE,
                Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    
    void setDarkMode(bool dark) { m_darkMode = dark; }
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        int itemType = index.data(SearchModel::ItemTypeRole).toInt();
        
        switch (itemType) {
            case SearchModel::SectionHeaderItem:
                paintSectionHeader(painter, option, index);
                break;
            case SearchModel::FolderResultItem:
                paintFolderItem(painter, option, index);
                break;
            case SearchModel::NotebookResultItem:
                m_cardDelegate->paint(painter, option, index);
                break;
        }
    }
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        int itemType = index.data(SearchModel::ItemTypeRole).toInt();
        
        switch (itemType) {
            case SearchModel::SectionHeaderItem: {
                // Section header spans full width to force onto its own row
                int viewportWidth = m_listView ? m_listView->viewport()->width() : 600;
                // Subtract spacing (12px on each side) to fit within viewport
                int fullWidth = qMax(viewportWidth - 24, 300);
                return QSize(fullWidth, SECTION_HEADER_HEIGHT);
            }
            case SearchModel::FolderResultItem: {
                // Folder item spans full width to force onto its own row
                int viewportWidth = m_listView ? m_listView->viewport()->width() : 600;
                // Subtract spacing (12px on each side) to fit within viewport
                int fullWidth = qMax(viewportWidth - 24, 300);
                return QSize(fullWidth, FOLDER_ITEM_HEIGHT);
            }
            case SearchModel::NotebookResultItem:
                return m_cardDelegate->sizeHint(option, index);
        }
        
        return m_cardDelegate->sizeHint(option, index);
    }

private:
    void paintSectionHeader(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        
        QRect rect = option.rect;
        QString title = index.data(SearchModel::SectionTitleRole).toString();
        
        // Draw section header text
        QColor textColor = ThemeColors::textSecondary(m_darkMode);
        painter->setPen(textColor);
        
        QFont font = painter->font();
        font.setPointSize(11);
        font.setBold(true);
        painter->setFont(font);
        
        QRect textRect = rect.adjusted(8, 0, -8, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
        
        painter->restore();
    }
    
    void paintFolderItem(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        
        QRect rect = option.rect;
        QString folderName = index.data(SearchModel::FolderNameRole).toString();
        bool hovered = option.state & QStyle::State_MouseOver;
        
        // === Card-style background with rounded corners ===
        QColor bgColor = hovered 
            ? ThemeColors::itemHover(m_darkMode) 
            : ThemeColors::itemBackground(m_darkMode);
        
        QPainterPath cardPath;
        cardPath.addRoundedRect(rect, FOLDER_CORNER_RADIUS, FOLDER_CORNER_RADIUS);
        
        // Shadow (light mode only)
        if (!m_darkMode) {
            QRect shadowRect = rect.translated(0, 2);
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(shadowRect, FOLDER_CORNER_RADIUS, FOLDER_CORNER_RADIUS);
            painter->fillPath(shadowPath, ThemeColors::cardShadow());
        }
        
        painter->fillPath(cardPath, bgColor);
        
        // Border
        painter->setPen(QPen(ThemeColors::cardBorder(m_darkMode), 1));
        painter->drawPath(cardPath);
        
        // === Folder icon (using pre-scaled cached icon) ===
        const QPixmap& folderIcon = m_darkMode ? m_folderIconDark : m_folderIconLight;
        
        if (!folderIcon.isNull()) {
            QRect iconRect(rect.left() + 12, rect.center().y() - FOLDER_ICON_SIZE/2, 
                           FOLDER_ICON_SIZE, FOLDER_ICON_SIZE);
            painter->drawPixmap(iconRect, folderIcon);
        }
        
        // === Folder name ===
        QColor textColor = ThemeColors::textPrimary(m_darkMode);
        painter->setPen(textColor);
        
        QFont nameFont = painter->font();
        nameFont.setPointSize(14);
        painter->setFont(nameFont);
        
        QRect nameRect(rect.left() + 44, rect.top(), rect.width() - 80, rect.height());
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, folderName);
        
        // === Arrow indicator ===
        QColor arrowColor = ThemeColors::textSecondary(m_darkMode);
        painter->setPen(arrowColor);
        
        QFont arrowFont = painter->font();
        arrowFont.setPointSize(16);
        painter->setFont(arrowFont);
        
        QRect arrowRect(rect.right() - 36, rect.top(), 28, rect.height());
        painter->drawText(arrowRect, Qt::AlignCenter, "→");
        
        painter->restore();
    }
    
    NotebookCardDelegate* m_cardDelegate;
    QListView* m_listView;
    bool m_darkMode = false;
    
    // Cached folder icons (avoid loading from resources on every paint)
    QPixmap m_folderIconLight;
    QPixmap m_folderIconDark;
    
    static constexpr int SECTION_HEADER_HEIGHT = 32;
    static constexpr int FOLDER_ITEM_HEIGHT = 48;
    static constexpr int FOLDER_CORNER_RADIUS = 12;  // Match notebook card corner radius
    static constexpr int FOLDER_ICON_SIZE = 24;
};

// ============================================================================
// SearchView Implementation
// ============================================================================

SearchView::SearchView(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    
    // Debounce timer for real-time search
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(DEBOUNCE_MS);
    connect(m_debounceTimer, &QTimer::timeout, this, &SearchView::performSearch);
}

void SearchView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);
    
    // === Search Bar ===
    m_searchBar = new QWidget(this);
    m_searchBar->setObjectName("SearchBar");
    
    auto* searchBarLayout = new QHBoxLayout(m_searchBar);
    searchBarLayout->setContentsMargins(0, 0, 0, 0);
    searchBarLayout->setSpacing(8);
    
    // Search input
    m_searchInput = new QLineEdit(m_searchBar);
    m_searchInput->setObjectName("SearchInput");
    m_searchInput->setPlaceholderText(tr("Search notebooks..."));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setMinimumHeight(SEARCH_BAR_HEIGHT);
    
    connect(m_searchInput, &QLineEdit::textChanged, 
            this, &SearchView::onSearchTextChanged);
    connect(m_searchInput, &QLineEdit::returnPressed, 
            this, &SearchView::onSearchTriggered);
    
    // Search button (zoom icon)
    m_searchButton = new QPushButton(m_searchBar);
    m_searchButton->setObjectName("SearchButton");
    m_searchButton->setFixedSize(SEARCH_BAR_HEIGHT, SEARCH_BAR_HEIGHT);
    m_searchButton->setToolTip(tr("Search"));
    updateSearchIcon();
    
    connect(m_searchButton, &QPushButton::clicked, 
            this, &SearchView::onSearchTriggered);
    
    // Clear button (×)
    m_clearButton = new QPushButton("×", m_searchBar);
    m_clearButton->setObjectName("ClearButton");
    m_clearButton->setFixedSize(SEARCH_BAR_HEIGHT, SEARCH_BAR_HEIGHT);
    m_clearButton->setToolTip(tr("Clear search"));
    m_clearButton->setVisible(false);
    
    connect(m_clearButton, &QPushButton::clicked, this, &SearchView::clearSearch);
    
    searchBarLayout->addWidget(m_searchInput, 1);
    searchBarLayout->addWidget(m_searchButton);
    searchBarLayout->addWidget(m_clearButton);
    
    mainLayout->addWidget(m_searchBar);
    
    // === Status Label ===
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("StatusLabel");
    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);
    
    // === Results List View (Model/View) ===
    m_model = new SearchModel(this);
    m_delegate = new NotebookCardDelegate(this);
    
    m_listView = new SearchListView(this);
    m_listView->setObjectName("SearchListView");
    m_listView->setModel(m_model);
    
    // L-009: Create composite delegate for mixed folder + notebook results
    auto* compositeDelegate = new CompositeSearchDelegate(m_delegate, m_listView, this);
    m_compositeDelegate = compositeDelegate;
    m_listView->setItemDelegate(compositeDelegate);
    
    // Connect thumbnail updates
    connect(NotebookLibrary::instance(), &NotebookLibrary::thumbnailUpdated,
            m_delegate, &NotebookCardDelegate::invalidateThumbnail);
    
    // Connect list view signals
    connect(m_listView, &SearchListView::notebookClicked,
            this, &SearchView::onNotebookClicked);
    connect(m_listView, &SearchListView::notebookMenuRequested,
            this, &SearchView::onNotebookMenuRequested);
    connect(m_listView, &SearchListView::folderClicked,
            this, &SearchView::onFolderClicked);
    // Note: Long-press in SearchView shows context menu directly (no batch select)
    
    mainLayout->addWidget(m_listView, 1);
    
    // === Empty State Label ===
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName("EmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    mainLayout->addWidget(m_emptyLabel, 1);
    
    // Initial state: show hint
    showEmptyState(tr("Type to search notebooks and folders"));
}

void SearchView::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        updateSearchIcon();
        
        // Update delegates
        if (m_delegate) {
            m_delegate->setDarkMode(dark);
        }
        // L-009: Update composite delegate
        if (m_compositeDelegate) {
            static_cast<CompositeSearchDelegate*>(m_compositeDelegate)->setDarkMode(dark);
        }
        // Trigger repaint of visible items
        m_listView->viewport()->update();
    }
}

void SearchView::updateSearchIcon()
{
    QString iconPath = m_darkMode 
        ? ":/resources/icons/zoom_reversed.png" 
        : ":/resources/icons/zoom.png";
    m_searchButton->setIcon(QIcon(iconPath));
    m_searchButton->setIconSize(QSize(20, 20));
}

void SearchView::clearSearch()
{
    m_searchInput->clear();
    m_lastQuery.clear();
    m_clearButton->setVisible(false);
    m_statusLabel->setVisible(false);
    m_model->clear();
    showEmptyState(tr("Type to search notebooks and folders"));
}

void SearchView::focusSearchInput()
{
    m_searchInput->setFocus();
    m_searchInput->selectAll();
}

void SearchView::onSearchTextChanged(const QString& text)
{
    // Show/hide clear button
    m_clearButton->setVisible(!text.isEmpty());
    
    // Restart debounce timer
    m_debounceTimer->start();
}

void SearchView::onSearchTriggered()
{
    // Cancel debounce and search immediately
    m_debounceTimer->stop();
    performSearch();
}

void SearchView::performSearch()
{
    QString query = m_searchInput->text().trimmed();
    
    // Skip if query unchanged
    if (query == m_lastQuery) {
        return;
    }
    m_lastQuery = query;
    
    if (query.isEmpty()) {
        m_model->clear();
        m_statusLabel->setVisible(false);
        showEmptyState(tr("Type to search notebooks and folders"));
        return;
    }
    
    // Perform search for both folders and notebooks (L-009)
    NotebookLibrary* lib = NotebookLibrary::instance();
    QStringList folders = lib->searchStarredFolders(query);
    QList<NotebookInfo> notebooks = lib->search(query);
    
    int folderCount = folders.size();
    int notebookCount = notebooks.size();
    int totalCount = folderCount + notebookCount;
    
    // Update status with both counts
    if (totalCount == 0) {
        m_statusLabel->setText(tr("No results found for \"%1\"").arg(query));
    } else {
        // Build status text showing both counts
        QStringList parts;
        if (notebookCount > 0) {
            parts << tr("%n notebook(s)", "", notebookCount);
        }
        if (folderCount > 0) {
            parts << tr("%n folder(s)", "", folderCount);
        }
        m_statusLabel->setText(parts.join(", ") + tr(" found"));
    }
    m_statusLabel->setVisible(true);
    
    // Display results
    if (totalCount == 0) {
        m_model->clear();
        showEmptyState(tr("No results match your search.\n\nTry a different search term."));
    } else {
        m_model->setResults(folders, notebooks);
        showResults();
    }
}

void SearchView::showEmptyState(const QString& message)
{
    m_listView->hide();
    m_emptyLabel->setText(message);
    m_emptyLabel->show();
}

void SearchView::showResults()
{
    m_emptyLabel->hide();
    m_listView->show();
}

void SearchView::onNotebookClicked(const QString& bundlePath)
{
    emit notebookClicked(bundlePath);
}

void SearchView::onNotebookMenuRequested(const QString& bundlePath, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    // Emit signal for context menu (3-dot button, right-click, or long-press)
    emit notebookMenuRequested(bundlePath);
}

void SearchView::onFolderClicked(const QString& folderName)
{
    // L-009: Emit signal to navigate to StarredView and scroll to folder
    emit folderClicked(folderName);
}

void SearchView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_searchInput->text().isEmpty()) {
            clearSearch();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
