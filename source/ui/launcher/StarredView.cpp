#include "StarredView.h"
#include "NotebookCard.h"
#include "LauncherScrollArea.h"
#include "../../core/NotebookLibrary.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QLabel>
#include <QApplication>

// ============================================================================
// StarredView
// ============================================================================

StarredView::StarredView(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    
    // Connect to library changes
    connect(NotebookLibrary::instance(), &NotebookLibrary::libraryChanged,
            this, &StarredView::reload);
    
    // Initial load
    reload();
}

void StarredView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Scroll area - use LauncherScrollArea for reliable manual touch scrolling
    // (QScroller has known issues with inertia reversal and tablet devices)
    m_scrollArea = new LauncherScrollArea(this);
    m_scrollArea->setObjectName("StarredScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Scroll content
    m_scrollContent = new QWidget();
    m_scrollContent->setObjectName("StarredScrollContent");
    
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(CONTENT_MARGIN, CONTENT_MARGIN, 
                                        CONTENT_MARGIN, CONTENT_MARGIN);
    m_contentLayout->setSpacing(SECTION_SPACING);
    m_contentLayout->setAlignment(Qt::AlignTop);
    
    m_scrollArea->setWidget(m_scrollContent);
    mainLayout->addWidget(m_scrollArea);
}

void StarredView::reload()
{
    clearContent();
    buildContent();
}

void StarredView::clearContent()
{
    // Remove all widgets from layout
    while (m_contentLayout->count() > 0) {
        QLayoutItem* item = m_contentLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_folderGrids.clear();
}

void StarredView::buildContent()
{
    NotebookLibrary* lib = NotebookLibrary::instance();
    QList<NotebookInfo> starred = lib->starredNotebooks();
    QStringList folders = lib->starredFolders();
    
    // Group notebooks by folder
    QMap<QString, QList<NotebookInfo>> folderContents;
    QList<NotebookInfo> unfiled;
    
    for (const NotebookInfo& info : starred) {
        if (info.starredFolder.isEmpty()) {
            unfiled.append(info);
        } else {
            folderContents[info.starredFolder].append(info);
        }
    }
    
    // Add folder sections in order
    for (const QString& folderName : folders) {
        if (folderContents.contains(folderName)) {
            QWidget* section = createFolderSection(folderName, folderContents[folderName]);
            m_contentLayout->addWidget(section);
        }
    }
    
    // Add "Unfiled" section if there are unfiled notebooks
    if (!unfiled.isEmpty()) {
        QWidget* section = createFolderSection(tr("Unfiled"), unfiled);
        m_contentLayout->addWidget(section);
    }
    
    // Add stretch at bottom
    m_contentLayout->addStretch();
    
    // Show empty state if no starred notebooks
    if (starred.isEmpty()) {
        QLabel* emptyLabel = new QLabel(tr("No starred notebooks yet.\n\nLong-press a notebook in Timeline\nand select \"Star\" to add it here."));
        emptyLabel->setObjectName("EmptyLabel");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        
        QFont font = emptyLabel->font();
        font.setPointSize(12);
        emptyLabel->setFont(font);
        
        QPalette pal = emptyLabel->palette();
        pal.setColor(QPalette::WindowText, m_darkMode ? QColor(150, 150, 150) : QColor(120, 120, 120));
        emptyLabel->setPalette(pal);
        
        m_contentLayout->insertWidget(0, emptyLabel, 1);
    }
}

QWidget* StarredView::createFolderSection(const QString& folderName, 
                                          const QList<NotebookInfo>& notebooks)
{
    QWidget* section = new QWidget();
    section->setObjectName("FolderSection");
    
    auto* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(8);
    
    // Folder header
    FolderHeader* header = new FolderHeader(folderName, section);
    header->setDarkMode(m_darkMode);
    
    // Restore collapsed state
    bool collapsed = m_collapsedFolders.value(folderName, false);
    header->setCollapsed(collapsed);
    
    sectionLayout->addWidget(header);
    
    // Grid of notebooks
    QWidget* grid = createNotebookGrid(notebooks);
    grid->setVisible(!collapsed);
    m_folderGrids[folderName] = grid;
    sectionLayout->addWidget(grid);
    
    // Connect header
    connect(header, &FolderHeader::clicked, this, [this, folderName, header, grid]() {
        bool newCollapsed = !header->isCollapsed();
        header->setCollapsed(newCollapsed);
        grid->setVisible(!newCollapsed);
        m_collapsedFolders[folderName] = newCollapsed;
    });
    
    connect(header, &FolderHeader::longPressed, this, [this, folderName]() {
        // Don't emit for "Unfiled" pseudo-folder
        if (folderName != tr("Unfiled")) {
            emit folderLongPressed(folderName);
        }
    });
    
    return section;
}

QWidget* StarredView::createNotebookGrid(const QList<NotebookInfo>& notebooks)
{
    QWidget* gridWidget = new QWidget();
    gridWidget->setObjectName("NotebookGrid");
    
    // Use flow layout for responsive grid
    // For simplicity, using QGridLayout with fixed columns
    // Could be replaced with FlowLayout for true responsiveness
    auto* gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(GRID_SPACING);
    gridLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
    // Calculate columns based on available width (estimate)
    const int cardWidth = 120 + GRID_SPACING;
    const int estimatedWidth = 600;  // Will be adjusted on resize
    const int columns = qMax(3, estimatedWidth / cardWidth);
    
    int row = 0, col = 0;
    for (const NotebookInfo& info : notebooks) {
        NotebookCard* card = new NotebookCard(info, gridWidget);
        card->setDarkMode(m_darkMode);
        
        connect(card, &NotebookCard::clicked, this, [this, info]() {
            emit notebookClicked(info.bundlePath);
        });
        
        connect(card, &NotebookCard::longPressed, this, [this, info]() {
            emit notebookLongPressed(info.bundlePath);
        });
        
        gridLayout->addWidget(card, row, col);
        
        col++;
        if (col >= columns) {
            col = 0;
            row++;
        }
    }
    
    return gridWidget;
}

void StarredView::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        // Reload to apply dark mode to all children
        reload();
    }
}

// ============================================================================
// FolderHeader
// ============================================================================

FolderHeader::FolderHeader(const QString& folderName, QWidget* parent)
    : QWidget(parent)
    , m_folderName(folderName)
{
    setFixedHeight(HEADER_HEIGHT);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    
    // Long-press timer
    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(LONG_PRESS_MS);
    connect(m_longPressTimer, &QTimer::timeout, this, [this]() {
        m_longPressTriggered = true;
        m_pressed = false;
        update();
        emit longPressed();
    });
}

void FolderHeader::setCollapsed(bool collapsed)
{
    if (m_collapsed != collapsed) {
        m_collapsed = collapsed;
        update();
    }
}

void FolderHeader::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        update();
    }
}

void FolderHeader::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRect rect = this->rect();
    
    // Background on hover/press
    if (m_pressed) {
        QColor bgColor = m_darkMode ? QColor(60, 60, 65) : QColor(235, 235, 240);
        painter.fillRect(rect, bgColor);
    } else if (m_hovered) {
        QColor bgColor = m_darkMode ? QColor(55, 55, 60) : QColor(245, 245, 248);
        painter.fillRect(rect, bgColor);
    }
    
    // Chevron (▶ or ▼)
    QColor chevronColor = m_darkMode ? QColor(150, 150, 150) : QColor(100, 100, 100);
    painter.setPen(chevronColor);
    
    QFont chevronFont = painter.font();
    chevronFont.setPointSize(10);
    painter.setFont(chevronFont);
    
    QString chevron = m_collapsed ? "▶" : "▼";
    QRect chevronRect(8, 0, 20, rect.height());
    painter.drawText(chevronRect, Qt::AlignVCenter | Qt::AlignLeft, chevron);
    
    // Folder name
    QColor textColor = m_darkMode ? QColor(220, 220, 220) : QColor(50, 50, 50);
    painter.setPen(textColor);
    
    QFont nameFont = painter.font();
    nameFont.setPointSize(14);
    nameFont.setBold(true);
    painter.setFont(nameFont);
    
    QRect nameRect(32, 0, rect.width() - 40, rect.height());
    painter.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, m_folderName);
    
    // Bottom separator line
    QColor lineColor = m_darkMode ? QColor(70, 70, 75) : QColor(220, 220, 225);
    painter.setPen(QPen(lineColor, 1));
    painter.drawLine(0, rect.bottom(), rect.width(), rect.bottom());
}

void FolderHeader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = event->pos();
        m_longPressTriggered = false;
        m_longPressTimer->start();
        update();
    }
    QWidget::mousePressEvent(event);
}

void FolderHeader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_longPressTimer->stop();
        
        if (m_pressed && !m_longPressTriggered && rect().contains(event->pos())) {
            emit clicked();
        }
        
        m_pressed = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void FolderHeader::mouseMoveEvent(QMouseEvent* event)
{
    if (m_longPressTimer->isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_longPressTimer->stop();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void FolderHeader::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FolderHeader::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    m_longPressTimer->stop();
    update();
    QWidget::leaveEvent(event);
}

