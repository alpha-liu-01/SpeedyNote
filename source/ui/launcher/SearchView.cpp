#include "SearchView.h"
#include "SearchListView.h"
#include "SearchModel.h"
#include "NotebookCardDelegate.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>

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
    m_listView->setItemDelegate(m_delegate);
    
    // Connect thumbnail updates
    connect(NotebookLibrary::instance(), &NotebookLibrary::thumbnailUpdated,
            m_delegate, &NotebookCardDelegate::invalidateThumbnail);
    
    // Connect list view signals
    connect(m_listView, &SearchListView::notebookClicked,
            this, &SearchView::onNotebookClicked);
    connect(m_listView, &SearchListView::notebookLongPressed,
            this, &SearchView::onNotebookLongPressed);
    
    mainLayout->addWidget(m_listView, 1);
    
    // === Empty State Label ===
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName("EmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    mainLayout->addWidget(m_emptyLabel, 1);
    
    // Initial state: show hint
    showEmptyState(tr("Type to search notebooks by name or PDF filename"));
}

void SearchView::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        updateSearchIcon();
        
        // Update delegate
        if (m_delegate) {
            m_delegate->setDarkMode(dark);
            // Trigger repaint of visible items
            m_listView->viewport()->update();
        }
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
    showEmptyState(tr("Type to search notebooks by name or PDF filename"));
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
        showEmptyState(tr("Type to search notebooks by name or PDF filename"));
        return;
    }
    
    // Perform search
    QList<NotebookInfo> results = NotebookLibrary::instance()->search(query);
    
    // Update status
    if (results.isEmpty()) {
        m_statusLabel->setText(tr("No results found for \"%1\"").arg(query));
    } else {
        m_statusLabel->setText(tr("%n notebook(s) found", "", results.size()));
    }
    m_statusLabel->setVisible(true);
    
    // Display results
    if (results.isEmpty()) {
        m_model->clear();
        showEmptyState(tr("No notebooks match your search.\n\nTry a different search term."));
    } else {
        m_model->setResults(results);
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

void SearchView::onNotebookLongPressed(const QString& bundlePath, const QPoint& globalPos)
{
    Q_UNUSED(globalPos)
    emit notebookLongPressed(bundlePath);
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
