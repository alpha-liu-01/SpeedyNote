#pragma once

// ============================================================================
// LayerPanel - SAI2-style layer management widget
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.3, 5.6.7)
//
// LayerPanel provides UI for managing layers on a Page or edgeless Document:
// - View list of layers with visibility toggles
// - Select active layer (for drawing)
// - Add, remove, and reorder layers
//
// Phase 5.6.7: LayerPanel now supports two modes:
// 1. Page mode: setCurrentPage(page) - works with Page's vectorLayers
// 2. Edgeless mode: setEdgelessDocument(doc) - works with Document's layer manifest
//
// Usage:
// 1. MainWindow creates LayerPanel in sidebar
// 2. When tab/page changes:
//    - For paged mode: call setCurrentPage(page)
//    - For edgeless mode: call setEdgelessDocument(doc)
// 3. LayerPanel emits signals when layers change (for undo, save tracking)
// ============================================================================

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

class Page;
class Document;
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
    // Page/Document Connection (Phase 5.6.7)
    // =========================================================================

    /**
     * @brief Set the page to manage layers for (paged mode).
     * @param page The page (can be nullptr to clear).
     * 
     * Refreshes the layer list to show the new page's layers.
     * Call this when the user switches tabs or scrolls to a new page.
     * Clears any previously set edgeless document.
     */
    void setCurrentPage(Page* page);

    /**
     * @brief Set the document to manage layers for (edgeless mode).
     * @param doc The edgeless document (can be nullptr to clear).
     * 
     * In edgeless mode, layers are managed via the document's manifest
     * rather than a specific Page/tile. This ensures layer operations
     * affect all tiles consistently.
     * Clears any previously set page.
     */
    void setEdgelessDocument(Document* doc);

    /**
     * @brief Get the currently connected page.
     * @return Pointer to the page, or nullptr if none or in edgeless mode.
     */
    Page* currentPage() const { return m_page; }

    /**
     * @brief Get the currently connected edgeless document.
     * @return Pointer to the document, or nullptr if none or in paged mode.
     */
    Document* edgelessDocument() const { return m_edgelessDoc; }

    /**
     * @brief Check if LayerPanel is in edgeless mode.
     * @return True if managing an edgeless document, false if managing a page.
     */
    bool isEdgelessMode() const { return m_edgelessDoc != nullptr; }

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

    /**
     * @brief Emitted when a layer is renamed.
     * @param index The layer index.
     * @param newName The new layer name.
     */
    void layerRenamed(int index, const QString& newName);

    // =========================================================================
    // Phase 5.3: Selection Signals
    // =========================================================================

    /**
     * @brief Emitted when the selection (checkboxes) changes.
     * @param selectedIndices List of selected layer indices.
     */
    void selectionChanged(QVector<int> selectedIndices);

    /**
     * @brief Emitted when layers are merged.
     * @param targetIndex The layer that received merged strokes.
     * @param mergedIndices The layer indices that were merged into target (and removed).
     */
    void layersMerged(int targetIndex, QVector<int> mergedIndices);

    /**
     * @brief Emitted when a layer is duplicated.
     * @param originalIndex The index of the original layer.
     * @param newIndex The index of the new duplicated layer.
     */
    void layerDuplicated(int originalIndex, int newIndex);

public:
    // =========================================================================
    // Phase 5.3: Selection API
    // =========================================================================

    /**
     * @brief Get the currently selected (checked) layer indices.
     * @return Vector of layer indices that are checked.
     */
    QVector<int> selectedLayerIndices() const;

    /**
     * @brief Get the count of selected layers.
     * @return Number of checked layers.
     */
    int selectedLayerCount() const;

    /**
     * @brief Select all layers (check all checkboxes).
     */
    void selectAllLayers();

    /**
     * @brief Deselect all layers (uncheck all checkboxes).
     */
    void deselectAllLayers();

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

    /**
     * @brief Handle item text changed (rename).
     * @param item The item that was changed.
     */
    void onItemChanged(QListWidgetItem* item);

    /**
     * @brief Phase 5.3: Handle Select All button click.
     */
    void onSelectAllClicked();

    /**
     * @brief Phase 5.3: Handle Deselect All button click.
     */
    void onDeselectAllClicked();

    /**
     * @brief Phase 5.3: Handle Merge button click.
     */
    void onMergeClicked();

    /**
     * @brief Phase 5.5: Handle Duplicate button click.
     */
    void onDuplicateClicked();

private:
    // Connected page (paged mode, not owned)
    Page* m_page = nullptr;
    
    // Connected document (edgeless mode, not owned) - Phase 5.6.7
    Document* m_edgelessDoc = nullptr;

    // UI elements
    QListWidget* m_layerList = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QLabel* m_titleLabel = nullptr;
    
    // Phase 5.3: Selection buttons
    QPushButton* m_selectAllButton = nullptr;
    QPushButton* m_deselectAllButton = nullptr;
    QPushButton* m_mergeButton = nullptr;
    
    // Phase 5.5: Duplicate button
    QPushButton* m_duplicateButton = nullptr;
    
    // Phase 5.2: Visibility icon (loaded based on theme)
    QIcon m_notVisibleIcon;

    // Flag to prevent recursive updates
    bool m_updatingList = false;

    // Setup methods
    void setupUI();
    
    /**
     * @brief Load the visibility icon based on current theme.
     */
    void loadVisibilityIcon();

    /**
     * @brief Update button enabled states based on current selection.
     */
    void updateButtonStates();

    /**
     * @brief Create a list item for a layer.
     * @param layerIndex The layer index in the page/manifest.
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
     * @return The layer index in the page/manifest.
     */
    int rowToLayerIndex(int row) const;

    /**
     * @brief Convert layer index to list row.
     * @param layerIndex The layer index in the page/manifest.
     * @return The list row.
     */
    int layerIndexToRow(int layerIndex) const;

    // =========================================================================
    // Abstracted layer access (Phase 5.6.7)
    // =========================================================================
    // These helpers abstract whether we're working with Page or Document manifest.

    /**
     * @brief Get total layer count from page or manifest.
     */
    int getLayerCount() const;

    /**
     * @brief Get layer name at index.
     */
    QString getLayerName(int index) const;

    /**
     * @brief Get layer visibility at index.
     */
    bool getLayerVisible(int index) const;

    /**
     * @brief Get layer locked state at index.
     */
    bool getLayerLocked(int index) const;

    /**
     * @brief Get active layer index.
     */
    int getActiveLayerIndex() const;

    /**
     * @brief Set layer visibility.
     */
    void setLayerVisible(int index, bool visible);

    /**
     * @brief Set layer name.
     */
    void setLayerName(int index, const QString& name);

    /**
     * @brief Set active layer index.
     */
    void setActiveLayerIndex(int index);

    /**
     * @brief Add a new layer.
     * @return Index of the new layer, or -1 on failure.
     */
    int addLayer(const QString& name);

    /**
     * @brief Remove a layer.
     * @return True if removed.
     */
    bool removeLayer(int index);

    /**
     * @brief Move a layer.
     * @return True if moved.
     */
    bool moveLayer(int from, int to);
};
