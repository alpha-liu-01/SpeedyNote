#pragma once

// ============================================================================
// OutlinePanel - PDF Table of Contents navigation widget
// ============================================================================
// Part of the SpeedyNote document architecture (Phase E.2)
//
// OutlinePanel displays the PDF outline (table of contents) and allows users
// to navigate the document by clicking entries. It also highlights the current
// section as the user scrolls through the document.
//
// Features:
// - Hierarchical tree view of PDF outline
// - Touch-friendly (36px row height, kinetic scrolling)
// - Click to navigate to page/position
// - Automatic highlighting of current section
// - Session-only state persistence (expand/collapse)
//
// Usage:
// 1. MainWindow creates OutlinePanel in LeftSidebarContainer
// 2. When document changes: call setOutline() with PDF outline data
// 3. Connect navigationRequested signal to viewport navigation
// 4. Connect viewport's currentPageChanged to highlightPage()
// ============================================================================

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSet>
#include <QPointF>

#include "../../pdf/PdfProvider.h"  // For PdfOutlineItem (QVector requires complete type)

class OutlineItemDelegate;

/**
 * @brief Widget for displaying and navigating PDF outline (table of contents).
 * 
 * Provides a tree view of the PDF outline with navigation capabilities.
 * Users can click items to jump to specific pages/positions in the document.
 */
class OutlinePanel : public QWidget {
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget* parent = nullptr);
    ~OutlinePanel() override = default;

    // =========================================================================
    // Outline Data
    // =========================================================================

    /**
     * @brief Set the outline data to display.
     * @param outline The PDF outline items (hierarchical).
     * 
     * Clears any existing outline and populates the tree with new data.
     * Applies default expansion state from PDF or first-level expansion.
     */
    void setOutline(const QVector<PdfOutlineItem>& outline);

    /**
     * @brief Clear the outline display.
     * 
     * Call when switching to a document without an outline.
     */
    void clearOutline();

    /**
     * @brief Check if an outline is currently loaded.
     * @return True if outline data is present.
     */
    bool hasOutline() const { return !m_outline.isEmpty(); }

    // =========================================================================
    // Navigation Highlighting
    // =========================================================================

    /**
     * @brief Highlight the outline item for the given page.
     * @param pageIndex The current page (0-based).
     * 
     * Uses floor-match algorithm: highlights the item with highest
     * targetPage <= pageIndex. Auto-expands parents if panel is visible.
     */
    void highlightPage(int pageIndex);

    // =========================================================================
    // State Management (for multi-tab support)
    // =========================================================================

    /**
     * @brief Save current expansion state.
     * 
     * Call before switching to another document/tab.
     */
    void saveState();

    /**
     * @brief Restore previously saved expansion state.
     * 
     * Call after switching back to this document/tab.
     */
    void restoreState();

    // =========================================================================
    // Theme
    // =========================================================================

    /**
     * @brief Update theme colors.
     * @param darkMode True for dark theme.
     */
    void updateTheme(bool darkMode);

signals:
    /**
     * @brief Emitted when user clicks an outline item to navigate.
     * @param pageIndex The target page (0-based).
     * @param position The target position within page (normalized 0-1), 
     *                 or (-1,-1) if not specified.
     */
    void navigationRequested(int pageIndex, QPointF position);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);

private:
    void setupUi();
    void populateTree(const QVector<PdfOutlineItem>& items, QTreeWidgetItem* parent = nullptr);
    void applyDefaultExpansion(QTreeWidgetItem* item, const PdfOutlineItem& outlineItem);
    QTreeWidgetItem* findItemForPage(int pageIndex);
    QString getItemPath(QTreeWidgetItem* item) const;

    QTreeWidget* m_tree = nullptr;
    OutlineItemDelegate* m_delegate = nullptr;
    QVector<PdfOutlineItem> m_outline;  // Cached for state restoration

    // State per document (session only)
    QSet<QString> m_expandedItems;      // Track expanded items by path
    int m_lastHighlightedPage = -1;
    bool m_darkMode = false;

    // Custom data roles for tree items
    static constexpr int PageRole = Qt::UserRole;
    static constexpr int PositionXRole = Qt::UserRole + 1;
    static constexpr int PositionYRole = Qt::UserRole + 2;
};

