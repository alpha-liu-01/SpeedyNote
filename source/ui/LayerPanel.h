#pragma once

// ============================================================================
// LayerPanel - SAI2-style layer management widget
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.3)
//
// LayerPanel provides UI for managing layers on a Page:
// - View list of layers with visibility toggles
// - Select active layer (for drawing)
// - Add, remove, and reorder layers
//
// LayerPanel talks DIRECTLY to Page - not through DocumentViewport.
// This keeps the architecture clean: Page owns layers, LayerPanel controls them.
//
// Usage:
// 1. MainWindow creates LayerPanel in sidebar
// 2. When tab/page changes, call setCurrentPage(page)
// 3. LayerPanel emits signals when layers change (for undo, save tracking)
// ============================================================================

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

class Page;
class VectorLayer;

/**
 * @brief Widget for managing layers on a Page.
 * 
 * Provides a list view of layers with visibility toggles, and buttons
 * for adding, removing, and reordering layers. The selected layer
 * becomes the active layer for drawing.
 */
class LayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(QWidget* parent = nullptr);
    ~LayerPanel() override = default;

    // =========================================================================
    // Page Connection
    // =========================================================================

    /**
     * @brief Set the page to manage layers for.
     * @param page The page (can be nullptr to clear).
     * 
     * Refreshes the layer list to show the new page's layers.
     * Call this when the user switches tabs or scrolls to a new page.
     */
    void setCurrentPage(Page* page);

    /**
     * @brief Get the currently connected page.
     * @return Pointer to the page, or nullptr if none.
     */
    Page* currentPage() const { return m_page; }

    // =========================================================================
    // Refresh
    // =========================================================================

    /**
     * @brief Refresh the layer list from the current page.
     * 
     * Call this after external changes to the page's layers
     * (e.g., undo/redo that affects layer structure).
     */
    void refreshLayerList();

signals:
    // =========================================================================
    // Change Notifications
    // =========================================================================
    // Emitted AFTER the change has been made to the Page.
    // MainWindow can connect to these for:
    // - Marking document as modified
    // - Undo system integration
    // - Viewport refresh

    /**
     * @brief Emitted when a layer is added.
     * @param index The index of the new layer.
     */
    void layerAdded(int index);

    /**
     * @brief Emitted when a layer is removed.
     * @param index The index that was removed.
     */
    void layerRemoved(int index);

    /**
     * @brief Emitted when a layer is moved.
     * @param from Original index.
     * @param to New index.
     */
    void layerMoved(int from, int to);

    /**
     * @brief Emitted when the active layer changes.
     * @param index The new active layer index.
     */
    void activeLayerChanged(int index);

    /**
     * @brief Emitted when a layer's visibility changes.
     * @param index The layer index.
     * @param visible The new visibility state.
     */
    void layerVisibilityChanged(int index, bool visible);

private slots:
    /**
     * @brief Handle Add Layer button click.
     */
    void onAddLayerClicked();

    /**
     * @brief Handle Remove Layer button click.
     */
    void onRemoveLayerClicked();

    /**
     * @brief Handle Move Up button click.
     */
    void onMoveUpClicked();

    /**
     * @brief Handle Move Down button click.
     */
    void onMoveDownClicked();

    /**
     * @brief Handle layer selection change.
     * @param currentRow The newly selected row.
     */
    void onLayerSelectionChanged(int currentRow);

    /**
     * @brief Handle visibility checkbox click.
     * @param item The item that was clicked.
     */
    void onItemClicked(QListWidgetItem* item);

private:
    // Connected page (not owned)
    Page* m_page = nullptr;

    // UI elements
    QListWidget* m_layerList = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QLabel* m_titleLabel = nullptr;

    // Flag to prevent recursive updates
    bool m_updatingList = false;

    // Setup methods
    void setupUI();

    /**
     * @brief Update button enabled states based on current selection.
     */
    void updateButtonStates();

    /**
     * @brief Create a list item for a layer.
     * @param layerIndex The layer index in the page.
     * @return The created item (caller owns).
     */
    QListWidgetItem* createLayerItem(int layerIndex);

    /**
     * @brief Convert list row to layer index.
     * 
     * The list shows layers in reverse order (top layer at top of list),
     * so we need to convert between row and layer index.
     * 
     * @param row The list row (0 = top of list).
     * @return The layer index in the page.
     */
    int rowToLayerIndex(int row) const;

    /**
     * @brief Convert layer index to list row.
     * @param layerIndex The layer index in the page.
     * @return The list row.
     */
    int layerIndexToRow(int layerIndex) const;
};
