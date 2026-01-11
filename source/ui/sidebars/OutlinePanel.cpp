#include "OutlinePanel.h"
#include "OutlineItemDelegate.h"
// Note: PdfProvider.h already included via OutlinePanel.h

#include <QScroller>
#include <QHeaderView>
#include <QApplication>
#include <QPalette>

// ============================================================================
// Constructor
// ============================================================================

OutlinePanel::OutlinePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

// ============================================================================
// Setup
// ============================================================================

void OutlinePanel::setupUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create tree widget
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(20);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setExpandsOnDoubleClick(false);  // We handle expand via arrow only
    m_tree->setAnimated(true);
    
    // Enable mouse tracking for proper hover effects (mouse, stylus)
    m_tree->setMouseTracking(true);
    m_tree->viewport()->setMouseTracking(true);
    m_tree->setAttribute(Qt::WA_Hover, true);
    m_tree->viewport()->setAttribute(Qt::WA_Hover, true);
    
    // Enable kinetic scrolling for touch only (not mouse - mouse should click normally)
    QScroller::grabGesture(m_tree->viewport(), QScroller::TouchGesture);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Set custom item delegate for page numbers with leader dots
    m_delegate = new OutlineItemDelegate(this);
    m_tree->setItemDelegate(m_delegate);

    // Connect signals
    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &OutlinePanel::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, &OutlinePanel::onItemCollapsed);

    layout->addWidget(m_tree);

    // Apply initial theme
    updateTheme(false);
}

// ============================================================================
// Outline Data
// ============================================================================

void OutlinePanel::setOutline(const QVector<PdfOutlineItem>& outline)
{
    m_outline = outline;
    m_tree->clear();
    m_lastHighlightedPage = -1;
    
    // Clear previous document's expansion state
    // (State is per-document, not persistent across documents)
    m_expandedItems.clear();

    if (outline.isEmpty()) {
        return;
    }

    // Populate tree (applyDefaultExpansion sets initial expansion state)
    populateTree(outline, nullptr);
}

void OutlinePanel::clearOutline()
{
    m_outline.clear();
    m_tree->clear();
    m_expandedItems.clear();
    m_lastHighlightedPage = -1;
}

void OutlinePanel::populateTree(const QVector<PdfOutlineItem>& items, QTreeWidgetItem* parent)
{
    for (const PdfOutlineItem& item : items) {
        QTreeWidgetItem* treeItem;
        
        if (parent) {
            treeItem = new QTreeWidgetItem(parent);
        } else {
            treeItem = new QTreeWidgetItem(m_tree);
        }

        // Set display text (title only - delegate will add page number)
        treeItem->setText(0, item.title);
        treeItem->setToolTip(0, item.title);  // Full title on hover

        // Store navigation data
        treeItem->setData(0, PageRole, item.targetPage);
        treeItem->setData(0, PositionXRole, item.targetPosition.x());
        treeItem->setData(0, PositionYRole, item.targetPosition.y());

        // Apply default expansion from PDF
        applyDefaultExpansion(treeItem, item);

        // Recursively add children
        if (!item.children.isEmpty()) {
            populateTree(item.children, treeItem);
        }
    }
}

void OutlinePanel::applyDefaultExpansion(QTreeWidgetItem* item, const PdfOutlineItem& outlineItem)
{
    // Use PDF's isOpen hint if available
    if (outlineItem.isOpen) {
        item->setExpanded(true);
    } else if (item->parent() == nullptr) {
        // Fallback: expand first level items
        item->setExpanded(true);
    }
}

// ============================================================================
// Navigation Highlighting
// ============================================================================

void OutlinePanel::highlightPage(int pageIndex)
{
    if (m_outline.isEmpty() || pageIndex < 0) {
        return;
    }

    // Only update if page changed
    if (pageIndex == m_lastHighlightedPage) {
        return;
    }
    m_lastHighlightedPage = pageIndex;

    // Find best matching item (floor match: highest page <= current)
    QTreeWidgetItem* bestMatch = findItemForPage(pageIndex);

    if (bestMatch) {
        // Block signals to prevent triggering navigation
        m_tree->blockSignals(true);

        // Clear previous selection
        m_tree->clearSelection();

        // Select and scroll to item
        bestMatch->setSelected(true);
        m_tree->scrollToItem(bestMatch, QAbstractItemView::EnsureVisible);

        // Auto-expand parents if panel is visible
        if (isVisible()) {
            QTreeWidgetItem* parent = bestMatch->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
        }

        m_tree->blockSignals(false);
    }
}

QTreeWidgetItem* OutlinePanel::findItemForPage(int pageIndex)
{
    QTreeWidgetItem* bestMatch = nullptr;
    int bestMatchPage = -1;

    // Iterate through all items
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        int itemPage = item->data(0, PageRole).toInt();

        // Floor match: find highest page <= current
        if (itemPage >= 0 && itemPage <= pageIndex && itemPage > bestMatchPage) {
            bestMatch = item;
            bestMatchPage = itemPage;
        }

        ++it;
    }

    return bestMatch;
}

// ============================================================================
// Item Interaction
// ============================================================================

void OutlinePanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }

    int pageIndex = item->data(0, PageRole).toInt();
    if (pageIndex < 0) {
        return;
    }

    // Get position data
    qreal posX = item->data(0, PositionXRole).toReal();
    qreal posY = item->data(0, PositionYRole).toReal();
    QPointF position(posX, posY);

    // Emit navigation request
    emit navigationRequested(pageIndex, position);
}

void OutlinePanel::onItemExpanded(QTreeWidgetItem* item)
{
    // Track expanded state by path
    QString path = getItemPath(item);
    m_expandedItems.insert(path);
}

void OutlinePanel::onItemCollapsed(QTreeWidgetItem* item)
{
    // Remove from expanded set
    QString path = getItemPath(item);
    m_expandedItems.remove(path);
}

QString OutlinePanel::getItemPath(QTreeWidgetItem* item) const
{
    // Build unique path from root to item (using titles)
    QStringList pathParts;
    QTreeWidgetItem* current = item;
    
    while (current) {
        pathParts.prepend(current->text(0));
        current = current->parent();
    }
    
    return pathParts.join("/");
}

// ============================================================================
// State Management
// ============================================================================

void OutlinePanel::saveState()
{
    // m_expandedItems is already updated via onItemExpanded/Collapsed
    // Nothing extra needed here
}

void OutlinePanel::restoreState()
{
    if (m_expandedItems.isEmpty()) {
        return;
    }

    // Collapse all first
    m_tree->collapseAll();

    // Re-expand saved items
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        QString path = getItemPath(item);
        
        if (m_expandedItems.contains(path)) {
            item->setExpanded(true);
        }
        
        ++it;
    }
}

// ============================================================================
// Theme
// ============================================================================

void OutlinePanel::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;

    // Update delegate theme
    if (m_delegate) {
        m_delegate->setDarkMode(darkMode);
    }

    QString bgColor = darkMode ? "#2D2D2D" : "#F5F5F5";

    // Stylesheet only for tree container and branch arrows
    // Item painting is handled by the custom delegate
    m_tree->setStyleSheet(QString(R"(
        QTreeWidget {
            background-color: %1;
            border: none;
            outline: none;
        }
        QTreeWidget::item {
            height: 36px;
        }
        QTreeWidget::branch {
            background-color: %1;
        }
        QTreeWidget::branch:has-children:!has-siblings:closed,
        QTreeWidget::branch:closed:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/right_arrow%2.png);
        }
        QTreeWidget::branch:open:has-children:!has-siblings,
        QTreeWidget::branch:open:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/down_arrow%2.png);
        }
    )").arg(bgColor, darkMode ? "_reversed" : ""));

    // Force repaint to apply delegate theme changes
    m_tree->viewport()->update();
}

