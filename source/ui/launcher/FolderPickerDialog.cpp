#include "FolderPickerDialog.h"
#include "../ThemeColors.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QApplication>
#include <QScreen>

FolderPickerDialog::FolderPickerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Folder"));
    setModal(true);
    
    // Set minimum size for usability
    setMinimumSize(DIALOG_MIN_WIDTH, DIALOG_MIN_HEIGHT);
    
    // Prefer a comfortable size on larger screens
    if (parent) {
        QSize parentSize = parent->size();
        int preferredWidth = qMin(400, parentSize.width() - 40);
        int preferredHeight = qMin(500, parentSize.height() - 80);
        resize(preferredWidth, preferredHeight);
    } else {
        resize(380, 480);
    }
    
    // Detect dark mode from parent or system (before populating so colors are correct)
    m_darkMode = palette().color(QPalette::Window).lightness() < 128;
    
    setupUi();
    populateFolders();
    applyTheme();
}

void FolderPickerDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);
    mainLayout->setSpacing(SPACING);
    
    // === Header: Title + Close Button ===
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);
    
    m_titleLabel = new QLabel(tr("Select Folder"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    
    m_closeButton = new QPushButton("✕", this);
    m_closeButton->setFixedSize(36, 36);
    m_closeButton->setFlat(true);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    
    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(m_closeButton);
    mainLayout->addLayout(headerLayout);
    
    // === Search Input ===
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("Search folders..."));
    m_searchInput->setFixedHeight(SEARCH_HEIGHT);
    m_searchInput->setClearButtonEnabled(true);
    
    // Touch-friendly font size
    QFont searchFont = m_searchInput->font();
    searchFont.setPointSize(14);
    m_searchInput->setFont(searchFont);
    
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &FolderPickerDialog::onSearchTextChanged);
    mainLayout->addWidget(m_searchInput);
    
    // === Unified Folder List (scrolls together) ===
    m_folderList = new QListWidget(this);
    m_folderList->setFrameShape(QFrame::NoFrame);
    m_folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_folderList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_folderList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderList->setSpacing(2);
    
    // Enable context menu for long-press/right-click (delete empty folders)
    m_folderList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderList, &QListWidget::customContextMenuRequested,
            this, &FolderPickerDialog::onFolderContextMenu);
    
    connect(m_folderList, &QListWidget::itemClicked,
            this, &FolderPickerDialog::onFolderClicked);
    mainLayout->addWidget(m_folderList, 1);  // Stretch to fill
    
    // === Bottom Buttons ===
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(SPACING);
    
    m_newFolderButton = new QPushButton(tr("+ New Folder"), this);
    m_newFolderButton->setFixedHeight(BUTTON_HEIGHT);
    m_newFolderButton->setCursor(Qt::PointingHandCursor);
    connect(m_newFolderButton, &QPushButton::clicked,
            this, &FolderPickerDialog::onNewFolderClicked);
    
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setFixedHeight(BUTTON_HEIGHT);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    buttonLayout->addWidget(m_newFolderButton, 1);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void FolderPickerDialog::applyTheme()
{
    // Background
    QColor bgColor = ThemeColors::background(m_darkMode);
    QColor textColor = ThemeColors::textPrimary(m_darkMode);
    QColor secondaryText = ThemeColors::textSecondary(m_darkMode);
    QColor borderColor = ThemeColors::border(m_darkMode);
    QColor hoverColor = ThemeColors::itemHover(m_darkMode);
    QColor pressedColor = ThemeColors::pressed(m_darkMode);
    
    // Dialog background
    setStyleSheet(QString("QDialog { background-color: %1; }").arg(bgColor.name()));
    
    // Title
    m_titleLabel->setStyleSheet(QString("color: %1;").arg(textColor.name()));
    
    // Close button
    m_closeButton->setStyleSheet(QString(
        "QPushButton { color: %1; border: none; background: transparent; border-radius: 18px; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }"
    ).arg(textColor.name(), hoverColor.name(), pressedColor.name()));
    
    // Search input
    m_searchInput->setStyleSheet(QString(
        "QLineEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 8px;"
        "  padding: 8px 12px;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid %4;"
        "}"
    ).arg(ThemeColors::backgroundAlt(m_darkMode).name(),
          textColor.name(),
          borderColor.name(),
          ThemeColors::selectionBorder(m_darkMode).name()));
    
    // Unified list - touch-friendly styling
    QString listStyle = QString(
        "QListWidget {"
        "  background-color: transparent;"
        "  border: none;"
        "}"
        "QListWidget::item {"
        "  background-color: transparent;"
        "  color: %1;"
        "  padding: 12px 8px;"
        "  border-radius: 8px;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: %2;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %3;"
        "}"
    ).arg(textColor.name(),
          hoverColor.name(),
          ThemeColors::selection(m_darkMode).name());
    
    m_folderList->setStyleSheet(listStyle);
    
    // Scrollbar styling
    m_folderList->verticalScrollBar()->setStyleSheet(QString(
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 8px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %1;"
        "  border-radius: 4px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
    ).arg(borderColor.name()));
    
    // Buttons
    QString buttonStyle = QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 8px;"
        "  padding: 8px 16px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: %4;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %5;"
        "}"
    ).arg(ThemeColors::backgroundAlt(m_darkMode).name(),
          textColor.name(),
          borderColor.name(),
          hoverColor.name(),
          pressedColor.name());
    
    m_newFolderButton->setStyleSheet(buttonStyle);
    m_cancelButton->setStyleSheet(buttonStyle);
}

void FolderPickerDialog::setTitle(const QString& title)
{
    m_titleLabel->setText(title.isEmpty() ? tr("Select Folder") : title);
}

void FolderPickerDialog::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        applyTheme();
        
        // Repopulate to update section header colors (they use m_darkMode directly)
        QString currentFilter = m_searchInput->text();
        populateFolders();
        if (!currentFilter.isEmpty()) {
            filterFolders(currentFilter);
        }
    }
}

QListWidgetItem* FolderPickerDialog::createSectionHeader(const QString& text)
{
    auto* item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, QString());  // No folder name for headers
    item->setData(Qt::UserRole + 1, SectionHeader);
    item->setSizeHint(QSize(0, SECTION_HEADER_HEIGHT));
    
    // Make header non-selectable
    item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    
    // Style the header with bold font
    QFont headerFont = item->font();
    headerFont.setPointSize(11);
    headerFont.setBold(true);
    item->setFont(headerFont);
    
    // Set secondary text color
    item->setForeground(ThemeColors::textSecondary(m_darkMode));
    
    return item;
}

QListWidgetItem* FolderPickerDialog::createFolderItem(const QString& folderName)
{
    auto* item = new QListWidgetItem(QString("📁  %1").arg(folderName));
    item->setData(Qt::UserRole, folderName);
    item->setData(Qt::UserRole + 1, FolderItem);
    item->setSizeHint(QSize(0, ITEM_HEIGHT));
    return item;
}

void FolderPickerDialog::populateFolders()
{
    NotebookLibrary* lib = NotebookLibrary::instance();
    
    // Get all folders from library (already ordered)
    m_allFolders = lib->starredFolders();
    
    // Get recent folders (L-008: tracked by usage in NotebookLibrary)
    m_recentFolders = lib->recentFolders();
    
    // Clear and rebuild the unified list
    m_folderList->clear();
    
    // Add Recent section if there are recent folders
    if (!m_recentFolders.isEmpty()) {
        m_folderList->addItem(createSectionHeader(tr("left_arrow")));
        for (const QString& folder : m_recentFolders) {
            m_folderList->addItem(createFolderItem(folder));
        }
    }
    
    // Add All Folders section (sorted alphabetically)
    QStringList sortedFolders = m_allFolders;
    sortedFolders.sort(Qt::CaseInsensitive);
    
    if (!sortedFolders.isEmpty()) {
        m_folderList->addItem(createSectionHeader(tr("ALL FOLDERS")));
        for (const QString& folder : sortedFolders) {
            m_folderList->addItem(createFolderItem(folder));
        }
    }
}

void FolderPickerDialog::filterFolders(const QString& text)
{
    QString filter = text.trimmed().toLower();
    
    // Track which sections have visible items
    bool currentSectionHasVisibleItems = false;
    QListWidgetItem* currentSectionHeader = nullptr;
    
    for (int i = 0; i < m_folderList->count(); ++i) {
        QListWidgetItem* item = m_folderList->item(i);
        int itemType = item->data(Qt::UserRole + 1).toInt();
        
        if (itemType == SectionHeader) {
            // Before moving to new section, update previous section header visibility
            if (currentSectionHeader) {
                currentSectionHeader->setHidden(!currentSectionHasVisibleItems);
            }
            
            // Start tracking new section
            currentSectionHeader = item;
            currentSectionHasVisibleItems = false;
        } else {
            // Folder item - check if it matches filter
            QString folder = item->data(Qt::UserRole).toString();
            bool matches = filter.isEmpty() || folder.toLower().contains(filter);
            item->setHidden(!matches);
            
            if (matches) {
                currentSectionHasVisibleItems = true;
            }
        }
    }
    
    // Update last section header visibility
    if (currentSectionHeader) {
        currentSectionHeader->setHidden(!currentSectionHasVisibleItems);
    }
}

void FolderPickerDialog::onSearchTextChanged(const QString& text)
{
    filterFolders(text);
}

void FolderPickerDialog::onFolderClicked(QListWidgetItem* item)
{
    if (!item) return;
    
    // Ignore clicks on section headers
    int itemType = item->data(Qt::UserRole + 1).toInt();
    if (itemType == SectionHeader) {
        return;
    }
    
    m_selectedFolder = item->data(Qt::UserRole).toString();
    accept();
}

void FolderPickerDialog::onNewFolderClicked()
{
    bool ok;
    QString folderName = QInputDialog::getText(
        this,
        tr("New Folder"),
        tr("Folder name:"),
        QLineEdit::Normal,
        QString(),
        &ok
    );
    
    if (!ok || folderName.trimmed().isEmpty()) {
        return;
    }
    
    folderName = folderName.trimmed();
    
    // Check if folder already exists
    NotebookLibrary* lib = NotebookLibrary::instance();
    if (lib->starredFolders().contains(folderName, Qt::CaseInsensitive)) {
        QMessageBox::warning(
            this,
            tr("Folder Exists"),
            tr("A folder named \"%1\" already exists.").arg(folderName)
        );
        return;
    }
    
    // Create the folder
    lib->createStarredFolder(folderName);
    
    // Select the new folder and return
    m_selectedFolder = folderName;
    accept();
}

void FolderPickerDialog::onFolderContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_folderList->itemAt(pos);
    if (!item) return;
    
    // Only show context menu for folder items, not section headers
    int itemType = item->data(Qt::UserRole + 1).toInt();
    if (itemType != FolderItem) {
        return;
    }
    
    QString folderName = item->data(Qt::UserRole).toString();
    if (folderName.isEmpty()) return;
    
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, m_darkMode);
    
    // Delete action - only enabled for empty folders
    bool isEmpty = isFolderEmpty(folderName);
    QAction* deleteAction = menu.addAction(tr("Delete Folder"));
    deleteAction->setEnabled(isEmpty);
    
    if (!isEmpty) {
        // Show tooltip explaining why delete is disabled
        deleteAction->setToolTip(tr("Folder contains notebooks"));
    }
    
    connect(deleteAction, &QAction::triggered, this, [this, folderName]() {
        deleteFolder(folderName);
    });
    
    menu.exec(m_folderList->viewport()->mapToGlobal(pos));
}

bool FolderPickerDialog::isFolderEmpty(const QString& folderName) const
{
    NotebookLibrary* lib = NotebookLibrary::instance();
    
    // Check if any starred notebook is in this folder
    for (const NotebookInfo& nb : lib->starredNotebooks()) {
        if (nb.starredFolder == folderName) {
            return false;  // Found a notebook in this folder
        }
    }
    
    return true;  // No notebooks in this folder
}

void FolderPickerDialog::deleteFolder(const QString& folderName)
{
    // Double-check it's empty
    if (!isFolderEmpty(folderName)) {
        QMessageBox::warning(
            this,
            tr("Cannot Delete"),
            tr("Folder \"%1\" contains notebooks. Remove notebooks from the folder first.").arg(folderName)
        );
        return;
    }
    
    // Confirm deletion
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Delete Folder"),
        tr("Delete folder \"%1\"?").arg(folderName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Delete the folder
    NotebookLibrary::instance()->deleteStarredFolder(folderName);
    
    // Refresh the list
    populateFolders();
    filterFolders(m_searchInput->text());
}

QString FolderPickerDialog::getFolder(QWidget* parent, const QString& title)
{
    FolderPickerDialog dialog(parent);
    
    if (!title.isEmpty()) {
        dialog.setTitle(title);
    }
    
    // Detect dark mode from parent
    if (parent) {
        bool dark = parent->palette().color(QPalette::Window).lightness() < 128;
        dialog.setDarkMode(dark);
    }
    
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.selectedFolder();
    }
    
    return QString();
}
