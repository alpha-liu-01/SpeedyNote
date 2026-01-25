// ============================================================================
// LayerPanel Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.3, 5.6.7, L.2)
// ============================================================================

#include "LayerPanel.h"
#include "../widgets/LayerItemWidget.h"
#include "../widgets/ActionBarButton.h"
#include "../widgets/LayerPanelPillButton.h"
#include "../../core/Page.h"
#include "../../core/Document.h"
#include "../../layers/VectorLayer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QIcon>
#include <QPalette>
#include <QApplication>
#include <QScroller>
#include <algorithm>  // Phase 5.4: for std::sort in merge

// ============================================================================
// Constructor
// ============================================================================

LayerPanel::LayerPanel(QWidget* parent)
    : QWidget(parent)
{
    // Detect initial dark mode
    m_darkMode = QApplication::palette().color(QPalette::Window).lightness() < 128;
    
    setupUI();
    updateButtonStates();
}

// ============================================================================
// Setup
// ============================================================================

void LayerPanel::setupUI()
{
    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Title
    m_titleLabel = new QLabel(tr("Layers"), this);
    m_titleLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(m_titleLabel);

    // Phase L.2: Layer scroll area with custom widgets
    m_layerScrollArea = new QScrollArea(this);
    m_layerScrollArea->setWidgetResizable(true);
    m_layerScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_layerScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_layerScrollArea->setMinimumHeight(100);
    m_layerScrollArea->setFrameShape(QFrame::NoFrame);
    
    // Phase L.4: Apply themed background to scroll area
    updateScrollAreaStyle();
    
    // Enable touch scrolling
    QScroller::grabGesture(m_layerScrollArea->viewport(), QScroller::TouchGesture);
    
    // Container widget inside scroll area
    m_layerContainer = new QWidget();
    m_layerLayout = new QVBoxLayout(m_layerContainer);
    m_layerLayout->setContentsMargins(4, 4, 4, 4);
    m_layerLayout->setSpacing(2);
    m_layerLayout->addStretch();  // Push items to top
    
    m_layerScrollArea->setWidget(m_layerContainer);
    mainLayout->addWidget(m_layerScrollArea, 1);  // Stretch factor 1

    // Phase L.3: Top row - Pill buttons (96×36px) for All/None and Merge
    QHBoxLayout* topButtonLayout = new QHBoxLayout();
    topButtonLayout->setSpacing(4);
    
    m_selectAllButton = new LayerPanelPillButton(tr("All/None"), this);
    m_selectAllButton->setToolTip(tr("Toggle select all/none"));
    m_selectAllButton->setDarkMode(m_darkMode);
    connect(m_selectAllButton, &LayerPanelPillButton::clicked, this, &LayerPanel::onSelectAllClicked);
    topButtonLayout->addWidget(m_selectAllButton);
    
    m_mergeButton = new LayerPanelPillButton(tr("Merge"), this);
    m_mergeButton->setToolTip(tr("Merge selected layers (2+ required)"));
    m_mergeButton->setDarkMode(m_darkMode);
    connect(m_mergeButton, &LayerPanelPillButton::clicked, this, &LayerPanel::onMergeClicked);
    topButtonLayout->addWidget(m_mergeButton);
    
    topButtonLayout->addStretch();
    mainLayout->addLayout(topButtonLayout);
    
    // Add spacing between top and bottom button rows
    mainLayout->addSpacing(8);

    // Phase L.3: Bottom row - Icon buttons (36×36px) using ActionBarButton
    QHBoxLayout* bottomButtonLayout = new QHBoxLayout();
    bottomButtonLayout->setSpacing(4);

    m_addButton = new ActionBarButton(this);
    m_addButton->setIconName("addtab");
    m_addButton->setDarkMode(m_darkMode);
    m_addButton->setToolTip(tr("Add new layer"));
    connect(m_addButton, &ActionBarButton::clicked, this, &LayerPanel::onAddLayerClicked);
    bottomButtonLayout->addWidget(m_addButton);

    m_removeButton = new ActionBarButton(this);
    m_removeButton->setIconName("trash");
    m_removeButton->setDarkMode(m_darkMode);
    m_removeButton->setToolTip(tr("Remove selected layer"));
    connect(m_removeButton, &ActionBarButton::clicked, this, &LayerPanel::onRemoveLayerClicked);
    bottomButtonLayout->addWidget(m_removeButton);

    m_moveUpButton = new ActionBarButton(this);
    m_moveUpButton->setIconName("layer_uparrow");
    m_moveUpButton->setDarkMode(m_darkMode);
    m_moveUpButton->setToolTip(tr("Move layer up"));
    connect(m_moveUpButton, &ActionBarButton::clicked, this, &LayerPanel::onMoveUpClicked);
    bottomButtonLayout->addWidget(m_moveUpButton);

    m_moveDownButton = new ActionBarButton(this);
    m_moveDownButton->setIconName("layer_downarrow");
    m_moveDownButton->setDarkMode(m_darkMode);
    m_moveDownButton->setToolTip(tr("Move layer down"));
    connect(m_moveDownButton, &ActionBarButton::clicked, this, &LayerPanel::onMoveDownClicked);
    bottomButtonLayout->addWidget(m_moveDownButton);

    m_duplicateButton = new ActionBarButton(this);
    m_duplicateButton->setIconName("copy");
    m_duplicateButton->setDarkMode(m_darkMode);
    m_duplicateButton->setToolTip(tr("Duplicate selected layer"));
    connect(m_duplicateButton, &ActionBarButton::clicked, this, &LayerPanel::onDuplicateClicked);
    bottomButtonLayout->addWidget(m_duplicateButton);

    bottomButtonLayout->addStretch();
    mainLayout->addLayout(bottomButtonLayout);
}

// ============================================================================
// Page/Document Connection (Phase 5.6.7)
// ============================================================================

void LayerPanel::setCurrentPage(Page* page)
{
    if (m_page == page && m_edgelessDoc == nullptr) {
        return;
    }

    m_page = page;
    m_edgelessDoc = nullptr;  // Clear edgeless mode
    refreshLayerList();
}

void LayerPanel::setEdgelessDocument(Document* doc)
{
    if (m_edgelessDoc == doc && m_page == nullptr) {
        return;
    }

    m_edgelessDoc = doc;
    m_page = nullptr;  // Clear paged mode
    refreshLayerList();
}

// ============================================================================
// Refresh
// ============================================================================

void LayerPanel::refreshLayerList()
{
    m_updatingList = true;

    // Clear existing layer items
    clearLayerItems();

    // Phase 5.6.7: Check both page and edgeless doc
    if (!m_page && !m_edgelessDoc) {
        m_updatingList = false;
        updateButtonStates();
        return;
    }

    // Create layer item widgets
    createLayerItems();

    // Update active layer visual state
    int activeIndex = getActiveLayerIndex();
    int layerCount = getLayerCount();
    if (activeIndex >= 0 && activeIndex < layerCount) {
        int widgetIdx = layerIndexToWidgetIndex(activeIndex);
        if (widgetIdx >= 0 && widgetIdx < m_layerItems.size()) {
            m_layerItems[widgetIdx]->setActive(true);
        }
    }

    m_updatingList = false;
    updateButtonStates();
}

// ============================================================================
// Button States
// ============================================================================

void LayerPanel::updateButtonStates()
{
    // Phase 5.6.7: Check both page and edgeless doc
    bool hasSource = (m_page != nullptr || m_edgelessDoc != nullptr);
    int layerCount = hasSource ? getLayerCount() : 0;
    int activeLayerIndex = currentActiveIndex();

    // Add: always enabled if we have a source
    m_addButton->setEnabled(hasSource);

    // Remove: enabled if more than one layer and something selected
    m_removeButton->setEnabled(hasSource && layerCount > 1 && activeLayerIndex >= 0);

    // Move Up: enabled if not at top (layer index < layerCount - 1)
    m_moveUpButton->setEnabled(hasSource && activeLayerIndex >= 0 && 
                               activeLayerIndex < layerCount - 1);

    // Move Down: enabled if not at bottom (layer index > 0)
    m_moveDownButton->setEnabled(hasSource && activeLayerIndex > 0);
    
    // Phase 5.3/L.2: Selection button (combined All/None toggle)
    m_selectAllButton->setEnabled(hasSource && layerCount > 0);
    // m_deselectAllButton is nullptr (combined into selectAllButton)
    
    // Merge: enabled if 2+ layers are checked
    int checkedCount = selectedLayerCount();
    m_mergeButton->setEnabled(hasSource && checkedCount >= 2);
    
    // Phase 5.5: Duplicate: enabled if a layer is selected
    m_duplicateButton->setEnabled(hasSource && activeLayerIndex >= 0);
}

// ============================================================================
// Phase L.2: Layer Item Widget Management
// ============================================================================

void LayerPanel::createLayerItems()
{
    int layerCount = getLayerCount();
    
    // Add layers to list (top layer first, so reverse order)
    for (int i = layerCount - 1; i >= 0; --i) {
        LayerItemWidget* item = new LayerItemWidget(i, m_layerContainer);
        item->setLayerName(getLayerName(i));
        item->setLayerVisible(getLayerVisible(i));
        item->setDarkMode(m_darkMode);
        item->setSelected(false);
        
        // Connect signals
        connect(item, &LayerItemWidget::clicked,
                this, &LayerPanel::onLayerItemClicked);
        connect(item, &LayerItemWidget::visibilityToggled,
                this, &LayerPanel::onLayerVisibilityToggled);
        connect(item, &LayerItemWidget::selectionToggled,
                this, &LayerPanel::onLayerSelectionToggled);
        connect(item, &LayerItemWidget::nameChanged,
                this, &LayerPanel::onLayerNameChanged);
        
        // Insert before the stretch (which is at the end)
        m_layerLayout->insertWidget(m_layerLayout->count() - 1, item);
        m_layerItems.append(item);
    }
}

void LayerPanel::clearLayerItems()
{
    for (LayerItemWidget* item : m_layerItems) {
        m_layerLayout->removeWidget(item);
        item->deleteLater();
    }
    m_layerItems.clear();
}

int LayerPanel::currentActiveIndex() const
{
    // Find the active layer item
    for (const LayerItemWidget* item : m_layerItems) {
        if (item->isActive()) {
            return item->layerIndex();
        }
    }
    
    // Fallback to the stored active layer index
    return getActiveLayerIndex();
}

// ============================================================================
// Index Conversion
// ============================================================================

int LayerPanel::widgetIndexToLayerIndex(int widgetIndex) const
{
    // Widgets are in reverse order: widget 0 = top layer = highest index
    int layerCount = getLayerCount();
    if (layerCount == 0 || widgetIndex < 0 || widgetIndex >= layerCount) {
        return -1;
    }
    return layerCount - 1 - widgetIndex;
}

int LayerPanel::layerIndexToWidgetIndex(int layerIndex) const
{
    // Widgets are in reverse order: highest layer index = widget 0
    int layerCount = getLayerCount();
    if (layerCount == 0 || layerIndex < 0 || layerIndex >= layerCount) {
        return -1;
    }
    return layerCount - 1 - layerIndex;
}

// ============================================================================
// Phase L.2: Slots - LayerItemWidget Signal Handlers
// ============================================================================

void LayerPanel::onLayerItemClicked(int layerIndex)
{
    // Phase L.2: Handle layer item click (select as active)
    if (m_updatingList || (!m_page && !m_edgelessDoc)) {
        return;
    }

    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }

    // Update active layer visual state
    for (LayerItemWidget* item : m_layerItems) {
        item->setActive(item->layerIndex() == layerIndex);
    }

    // Update model active layer
    if (getActiveLayerIndex() != layerIndex) {
        setActiveLayerIndex(layerIndex);
        emit activeLayerChanged(layerIndex);
    }

    updateButtonStates();
}

void LayerPanel::onLayerVisibilityToggled(int layerIndex, bool visible)
{
    // Phase L.2: Handle visibility toggle from layer item
    if (m_updatingList || (!m_page && !m_edgelessDoc)) {
        return;
    }

    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }

    setLayerVisible(layerIndex, visible);
    emit layerVisibilityChanged(layerIndex, visible);
}

void LayerPanel::onLayerSelectionToggled(int layerIndex, bool selected)
{
    // Phase L.2: Handle selection toggle from layer item
    if (m_updatingList || (!m_page && !m_edgelessDoc)) {
        return;
    }

    Q_UNUSED(layerIndex)
    Q_UNUSED(selected)
    
    // Update button states (merge button depends on selection count)
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
}

void LayerPanel::onLayerNameChanged(int layerIndex, const QString& newName)
{
    // Phase L.2: Handle layer name changed from layer item
    if (m_updatingList || (!m_page && !m_edgelessDoc)) {
        return;
    }

    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }

    QString finalName = newName.trimmed();
    if (finalName.isEmpty()) {
        finalName = QString("Layer %1").arg(layerIndex + 1);
    }

    // Only update if name actually changed
    QString currentName = getLayerName(layerIndex);
    if (currentName != finalName) {
        setLayerName(layerIndex, finalName);
        emit layerRenamed(layerIndex, finalName);
    }
}

// ============================================================================
// Slots - Buttons
// ============================================================================

void LayerPanel::onAddLayerClicked()
{
    // Phase 5.6.7: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    // Generate a unique layer name
    int layerCount = getLayerCount();
    QString layerName = QString("Layer %1").arg(layerCount + 1);

    // Add the layer
    int newIndex = addLayer(layerName);
    if (newIndex < 0) {
        return;
    }

    // Set as active
    setActiveLayerIndex(newIndex);

    // Refresh and select
    refreshLayerList();

    emit layerAdded(newIndex);
    emit activeLayerChanged(newIndex);
}

void LayerPanel::onRemoveLayerClicked()
{
    // Phase 5.6.7/L.2: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int layerIndex = currentActiveIndex();
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }

    // Don't remove the last layer
    if (layerCount <= 1) {
        return;
    }

    // Remove the layer
    if (!removeLayer(layerIndex)) {
        return;
    }

    // Refresh
    refreshLayerList();

    emit layerRemoved(layerIndex);
    emit activeLayerChanged(getActiveLayerIndex());
}

void LayerPanel::onMoveUpClicked()
{
    // Phase 5.6.7/L.2: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int layerIndex = currentActiveIndex();
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount - 1) {
        return;  // Can't move up if already at top
    }

    // Move layer up (increase index)
    int newIndex = layerIndex + 1;
    if (!moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Refresh list (refreshLayerList already sets active layer from model)
    refreshLayerList();

    emit layerMoved(layerIndex, newIndex);
}

void LayerPanel::onMoveDownClicked()
{
    // Phase 5.6.7/L.2: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int layerIndex = currentActiveIndex();
    if (layerIndex <= 0) {
        return;  // Can't move down if already at bottom
    }

    // Move layer down (decrease index)
    int newIndex = layerIndex - 1;
    if (!moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Refresh list (refreshLayerList already sets active layer from model)
    refreshLayerList();

    emit layerMoved(layerIndex, newIndex);
}

// ============================================================================
// Abstracted Layer Access (Phase 5.6.7)
// ============================================================================
// These helpers abstract whether we're working with Page or Document manifest.

int LayerPanel::getLayerCount() const
{
    if (m_edgelessDoc) {
        return m_edgelessDoc->edgelessLayerCount();
    }
    if (m_page) {
        return m_page->layerCount();
    }
    return 0;
}

QString LayerPanel::getLayerName(int index) const
{
    if (m_edgelessDoc) {
        const LayerDefinition* def = m_edgelessDoc->edgelessLayerDef(index);
        return def ? def->name : QString();
    }
    if (m_page) {
        VectorLayer* layer = m_page->layer(index);
        return layer ? layer->name : QString();
    }
    return QString();
}

bool LayerPanel::getLayerVisible(int index) const
{
    if (m_edgelessDoc) {
        const LayerDefinition* def = m_edgelessDoc->edgelessLayerDef(index);
        return def ? def->visible : true;
    }
    if (m_page) {
        VectorLayer* layer = m_page->layer(index);
        return layer ? layer->visible : true;
    }
    return true;
}

bool LayerPanel::getLayerLocked(int index) const
{
    if (m_edgelessDoc) {
        const LayerDefinition* def = m_edgelessDoc->edgelessLayerDef(index);
        return def ? def->locked : false;
    }
    if (m_page) {
        VectorLayer* layer = m_page->layer(index);
        return layer ? layer->locked : false;
    }
    return false;
}

int LayerPanel::getActiveLayerIndex() const
{
    if (m_edgelessDoc) {
        return m_edgelessDoc->edgelessActiveLayerIndex();
    }
    if (m_page) {
        return m_page->activeLayerIndex;
    }
    return 0;
}

void LayerPanel::setLayerVisible(int index, bool visible)
{
    if (m_edgelessDoc) {
        m_edgelessDoc->setEdgelessLayerVisible(index, visible);
    } else if (m_page) {
        VectorLayer* layer = m_page->layer(index);
        if (layer) {
            layer->visible = visible;
        }
    }
}

void LayerPanel::setLayerName(int index, const QString& name)
{
    if (m_edgelessDoc) {
        m_edgelessDoc->setEdgelessLayerName(index, name);
    } else if (m_page) {
        VectorLayer* layer = m_page->layer(index);
        if (layer) {
            layer->name = name;
        }
    }
}

void LayerPanel::setActiveLayerIndex(int index)
{
    if (m_edgelessDoc) {
        m_edgelessDoc->setEdgelessActiveLayerIndex(index);
    } else if (m_page) {
        m_page->activeLayerIndex = index;
    }
}

int LayerPanel::addLayer(const QString& name)
{
    if (m_edgelessDoc) {
        return m_edgelessDoc->addEdgelessLayer(name);
    }
    if (m_page) {
        VectorLayer* layer = m_page->addLayer(name);
        if (layer) {
            return m_page->layerCount() - 1;
        }
    }
    return -1;
}

bool LayerPanel::removeLayer(int index)
{
    if (m_edgelessDoc) {
        return m_edgelessDoc->removeEdgelessLayer(index);
    }
    if (m_page) {
        return m_page->removeLayer(index);
    }
    return false;
}

bool LayerPanel::moveLayer(int from, int to)
{
    if (m_edgelessDoc) {
        return m_edgelessDoc->moveEdgelessLayer(from, to);
    }
    if (m_page) {
        return m_page->moveLayer(from, to);
    }
    return false;
}

// ============================================================================
// Phase 5.3/L.2: Selection API
// ============================================================================

QVector<int> LayerPanel::selectedLayerIndices() const
{
    QVector<int> indices;
    
    for (const LayerItemWidget* item : m_layerItems) {
        if (item && item->isSelected()) {
            indices.append(item->layerIndex());
        }
    }
    
    // Sort in ascending order (bottom layer first)
    std::sort(indices.begin(), indices.end());
    return indices;
}

int LayerPanel::selectedLayerCount() const
{
    int count = 0;
    for (const LayerItemWidget* item : m_layerItems) {
        if (item && item->isSelected()) {
            ++count;
        }
    }
    return count;
}

void LayerPanel::selectAllLayers()
{
    m_updatingList = true;
    for (LayerItemWidget* item : m_layerItems) {
        if (item) {
            item->setSelected(true);
        }
    }
    m_updatingList = false;
    
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
}

void LayerPanel::deselectAllLayers()
{
    m_updatingList = true;
    for (LayerItemWidget* item : m_layerItems) {
        if (item) {
            item->setSelected(false);
        }
    }
    m_updatingList = false;
    
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
}

void LayerPanel::toggleSelectAllLayers()
{
    // Same logic as the All/None button
    if (selectedLayerCount() > 0) {
        deselectAllLayers();
    } else {
        selectAllLayers();
    }
}

// ============================================================================
// Phase 6.6: Keyboard Shortcut Actions
// ============================================================================

void LayerPanel::addNewLayerAction()
{
    // Delegate to the button handler which has the full workflow
    onAddLayerClicked();
}

void LayerPanel::toggleActiveLayerVisibility()
{
    if (!m_page && !m_edgelessDoc) {
        return;
    }
    
    int activeIndex = getActiveLayerIndex();
    int layerCount = getLayerCount();
    if (activeIndex < 0 || activeIndex >= layerCount) {
        return;
    }
    
    bool currentVisible = getLayerVisible(activeIndex);
    setLayerVisible(activeIndex, !currentVisible);
    
    // Update the widget if it exists
    int widgetIdx = layerIndexToWidgetIndex(activeIndex);
    if (widgetIdx >= 0 && widgetIdx < m_layerItems.size()) {
        m_layerItems[widgetIdx]->setLayerVisible(!currentVisible);
    }
    
    emit layerVisibilityChanged(activeIndex, !currentVisible);
}

void LayerPanel::selectTopLayer()
{
    if (!m_page && !m_edgelessDoc) {
        return;
    }
    
    int layerCount = getLayerCount();
    if (layerCount <= 0) {
        return;
    }
    
    // Top layer has the highest index
    int topIndex = layerCount - 1;
    onLayerItemClicked(topIndex);
}

void LayerPanel::selectBottomLayer()
{
    if (!m_page && !m_edgelessDoc) {
        return;
    }
    
    int layerCount = getLayerCount();
    if (layerCount <= 0) {
        return;
    }
    
    // Bottom layer has index 0
    onLayerItemClicked(0);
}

void LayerPanel::mergeSelectedLayers()
{
    // Delegate to the button handler which has the full workflow
    onMergeClicked();
}

// ============================================================================
// Phase 5.3/L.2: Selection Slots
// ============================================================================

void LayerPanel::onSelectAllClicked()
{
    // Phase L.2: Toggle All/None - if any selected, deselect all; else select all
    if (selectedLayerCount() > 0) {
        deselectAllLayers();
    } else {
        selectAllLayers();
    }
}

void LayerPanel::onDeselectAllClicked()
{
    // No longer used - combined into onSelectAllClicked as toggle
    deselectAllLayers();
}

void LayerPanel::onMergeClicked()
{
    // Phase 5.4: Merge selected layers
    QVector<int> selected = selectedLayerIndices();
    if (selected.size() < 2) {
        return;  // Need at least 2 layers to merge
    }
    
    // Sort to ensure we have the lowest index first
    std::sort(selected.begin(), selected.end());
    
    // Target is the bottom-most selected layer (lowest index)
    int targetIndex = selected.first();
    
    // Remove target from the list of layers to merge into it
    selected.removeFirst();
    
    // Perform the actual merge
    bool success = false;
    if (m_edgelessDoc) {
        // Edgeless mode: use Document's merge method
        success = m_edgelessDoc->mergeEdgelessLayers(targetIndex, selected);
    } else if (m_page) {
        // Paged mode: use Page's merge method
        success = m_page->mergeLayers(targetIndex, selected);
    }
    
    if (success) {
        // Refresh the layer list
        refreshLayerList();
        
        // CRITICAL FIX: Emit activeLayerChanged to sync viewport's active layer index.
        // After merge, the active layer should be the target layer.
        // refreshLayerList() doesn't emit this signal because m_updatingList is true.
        // Without this, the viewport's m_edgelessActiveLayerIndex may point to a removed
        // layer, causing the eraser to check the wrong layer and fail to find strokes.
        emit activeLayerChanged(targetIndex);
        
        // Emit signal for MainWindow to update viewport and undo system
        emit layersMerged(targetIndex, selected);
    }
}

void LayerPanel::onDuplicateClicked()
{
    // Phase 5.5/L.2: Duplicate selected layer
    if (!m_page && !m_edgelessDoc) {
        return;
    }
    
    int layerIndex = currentActiveIndex();
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }
    
    // Perform the duplicate
    int newIndex = -1;
    if (m_edgelessDoc) {
        // Edgeless mode: use Document's duplicate method
        newIndex = m_edgelessDoc->duplicateEdgelessLayer(layerIndex);
    } else if (m_page) {
        // Paged mode: use Page's duplicate method
        newIndex = m_page->duplicateLayer(layerIndex);
    }
    
    if (newIndex >= 0) {
        // Set as active before refresh so refreshLayerList picks it up
        setActiveLayerIndex(newIndex);
        
        // Refresh the layer list (will set active layer from model)
        refreshLayerList();
        
        // Emit signals for MainWindow to update viewport
        emit activeLayerChanged(newIndex);
        emit layerDuplicated(layerIndex, newIndex);
    }
}

// ============================================================================
// Phase L.4: Theme Integration
// ============================================================================

void LayerPanel::updateScrollAreaStyle()
{
    // Style the scroll area with a subtle inset background
    // Unified gray colors: dark #2a2e32/#4d4d4d, light #F5F5F5/#D0D0D0
    QString scrollStyle;
    QString containerStyle;
    
    if (m_darkMode) {
        // Dark mode: unified dark gray
        scrollStyle = QStringLiteral(
            "QScrollArea { "
            "  background-color: #2a2e32; "
            "  border: 1px solid #4d4d4d; "
            "  border-radius: 6px; "
            "}"
            "QScrollBar:vertical { "
            "  background: #2a2e32; "
            "  width: 8px; "
            "  margin: 2px; "
            "}"
            "QScrollBar::handle:vertical { "
            "  background: #4d4d4d; "
            "  border-radius: 3px; "
            "  min-height: 20px; "
            "}"
            "QScrollBar::handle:vertical:hover { "
            "  background: #5d5d5d; "
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
            "  height: 0px; "
            "}"
        );
        containerStyle = QStringLiteral("background-color: transparent;");
    } else {
        // Light mode: unified light gray
        scrollStyle = QStringLiteral(
            "QScrollArea { "
            "  background-color: #F5F5F5; "
            "  border: 1px solid #D0D0D0; "
            "  border-radius: 6px; "
            "}"
            "QScrollBar:vertical { "
            "  background: #F5F5F5; "
            "  width: 8px; "
            "  margin: 2px; "
            "}"
            "QScrollBar::handle:vertical { "
            "  background: #D0D0D0; "
            "  border-radius: 3px; "
            "  min-height: 20px; "
            "}"
            "QScrollBar::handle:vertical:hover { "
            "  background: #B0B0B0; "
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
            "  height: 0px; "
            "}"
        );
        containerStyle = QStringLiteral("background-color: transparent;");
    }
    
    m_layerScrollArea->setStyleSheet(scrollStyle);
    if (m_layerContainer) {
        m_layerContainer->setStyleSheet(containerStyle);
    }
}

void LayerPanel::setDarkMode(bool dark)
{
    if (m_darkMode == dark) {
        return;
    }
    
    m_darkMode = dark;
    
    // Update scroll area styling
    updateScrollAreaStyle();
    
    // Update pill buttons
    m_selectAllButton->setDarkMode(dark);
    m_mergeButton->setDarkMode(dark);
    
    // Update icon buttons (ActionBarButton handles icon switching internally)
    m_addButton->setDarkMode(dark);
    m_removeButton->setDarkMode(dark);
    m_moveUpButton->setDarkMode(dark);
    m_moveDownButton->setDarkMode(dark);
    m_duplicateButton->setDarkMode(dark);
    
    // Update layer item widgets
    for (LayerItemWidget* item : m_layerItems) {
        item->setDarkMode(dark);
    }
}
