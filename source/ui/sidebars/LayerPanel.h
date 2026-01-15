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
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVector>

class Page;
class Document;
class VectorLayer;
class LayerItemWidget;
class ActionBarButton;
class LayerPanelPillButton;

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

    // Phase L.2: LayerItemWidget signal handlers
    
    /**
     * @brief Handle layer item clicked (select as active).
     * @param index The layer index.
     */
    void onLayerItemClicked(int index);

    /**
     * @brief Handle visibility toggle from layer item.
     * @param index The layer index.
     * @param visible The new visibility state.
     */
    void onLayerVisibilityToggled(int index, bool visible);

    /**
     * @brief Handle selection toggle from layer item.
     * @param index The layer index.
     * @param selected The new selection state.
     */
    void onLayerSelectionToggled(int index, bool selected);

    /**
     * @brief Handle layer name changed from layer item.
     * @param index The layer index.
     * @param newName The new name.
     */
    void onLayerNameChanged(int index, const QString& newName);

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

    // Phase L.2: Layer list using custom widgets
    QScrollArea* m_layerScrollArea = nullptr;
    QWidget* m_layerContainer = nullptr;
    QVBoxLayout* m_layerLayout = nullptr;
    QVector<LayerItemWidget*> m_layerItems;
    
    // UI elements
    QLabel* m_titleLabel = nullptr;
    
    // Phase L.3: Icon buttons (36×36px) using ActionBarButton
    ActionBarButton* m_addButton = nullptr;
    ActionBarButton* m_removeButton = nullptr;
    ActionBarButton* m_moveUpButton = nullptr;
    ActionBarButton* m_moveDownButton = nullptr;
    ActionBarButton* m_duplicateButton = nullptr;
    
    // Phase L.3: Pill buttons (72×36px) using LayerPanelPillButton
    LayerPanelPillButton* m_selectAllButton = nullptr;
    LayerPanelPillButton* m_mergeButton = nullptr;

    // Flag to prevent recursive updates
    bool m_updatingList = false;
    
    // Dark mode state for theming
    bool m_darkMode = false;

    // Setup methods
    void setupUI();

    /**
     * @brief Update button enabled states based on current selection.
     */
    void updateButtonStates();

    /**
     * @brief Create or update layer item widgets.
     */
    void createLayerItems();
    
    /**
     * @brief Clear all layer item widgets.
     */
    void clearLayerItems();
    
    /**
     * @brief Phase L.4: Update scroll area styling based on theme.
     */
    void updateScrollAreaStyle();
    
    /**
     * @brief Get the currently active layer item index (selected in UI).
     * @return The active layer index, or -1 if none.
     */
    int currentActiveIndex() const;

    /**
     * @brief Convert widget index to layer index.
     * 
     * The list shows layers in reverse order (top layer at top of list),
     * so we need to convert between widget position and layer index.
     * 
     * @param widgetIndex The position in m_layerItems (0 = top of list).
     * @return The layer index in the page/manifest.
     */
    int widgetIndexToLayerIndex(int widgetIndex) const;

    /**
     * @brief Convert layer index to widget index.
     * @param layerIndex The layer index in the page/manifest.
     * @return The position in m_layerItems.
     */
    int layerIndexToWidgetIndex(int layerIndex) const;
    
public:
    /**
     * @brief Set dark mode for theming.
     * @param dark True for dark mode, false for light mode.
     */
    void setDarkMode(bool dark);

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
