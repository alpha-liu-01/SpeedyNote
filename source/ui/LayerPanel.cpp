// ============================================================================
// LayerPanel Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.3, 5.6.7)
// ============================================================================

#include "LayerPanel.h"
#include "../core/Page.h"
#include "../core/Document.h"
#include "../layers/VectorLayer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QPalette>
#include <QApplication>
#include <algorithm>  // Phase 5.4: for std::sort in merge

// ============================================================================
// Constructor
// ============================================================================

LayerPanel::LayerPanel(QWidget* parent)
    : QWidget(parent)
{
    // Phase 5.2: Load visibility icon with theme support
    loadVisibilityIcon();
    
    setupUI();
    updateButtonStates();
}

void LayerPanel::loadVisibilityIcon()
{
    // Detect dark mode from application palette
    bool isDark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    QString iconPath = isDark
        ? ":/resources/icons/notvisible_reversed.png"
        : ":/resources/icons/notvisible.png";
    m_notVisibleIcon = QIcon(iconPath);
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

    // Layer list
    m_layerList = new QListWidget(this);
    m_layerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerList->setDragDropMode(QAbstractItemView::NoDragDrop);  // Use buttons for reorder
    m_layerList->setMinimumHeight(100);
    mainLayout->addWidget(m_layerList, 1);  // Stretch factor 1

    // Connect list signals
    connect(m_layerList, &QListWidget::currentRowChanged,
            this, &LayerPanel::onLayerSelectionChanged);
    connect(m_layerList, &QListWidget::itemClicked,
            this, &LayerPanel::onItemClicked);
    connect(m_layerList, &QListWidget::itemChanged,
            this, &LayerPanel::onItemChanged);

    // Phase 5.3: Selection buttons bar
    QHBoxLayout* selectionLayout = new QHBoxLayout();
    selectionLayout->setSpacing(2);
    
    m_selectAllButton = new QPushButton(tr("All"), this);
    m_selectAllButton->setToolTip(tr("Select all layers"));
    m_selectAllButton->setFixedHeight(22);
    connect(m_selectAllButton, &QPushButton::clicked, this, &LayerPanel::onSelectAllClicked);
    selectionLayout->addWidget(m_selectAllButton);
    
    m_deselectAllButton = new QPushButton(tr("None"), this);
    m_deselectAllButton->setToolTip(tr("Deselect all layers"));
    m_deselectAllButton->setFixedHeight(22);
    connect(m_deselectAllButton, &QPushButton::clicked, this, &LayerPanel::onDeselectAllClicked);
    selectionLayout->addWidget(m_deselectAllButton);
    
    m_mergeButton = new QPushButton(tr("Merge"), this);
    m_mergeButton->setToolTip(tr("Merge selected layers (2+ required)"));
    m_mergeButton->setFixedHeight(22);
    connect(m_mergeButton, &QPushButton::clicked, this, &LayerPanel::onMergeClicked);
    selectionLayout->addWidget(m_mergeButton);
    
    selectionLayout->addStretch();
    mainLayout->addLayout(selectionLayout);

    // Button bar (add/remove/move)
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(2);

    m_addButton = new QPushButton("+", this);
    m_addButton->setToolTip(tr("Add new layer"));
    m_addButton->setFixedSize(28, 28);
    connect(m_addButton, &QPushButton::clicked, this, &LayerPanel::onAddLayerClicked);
    buttonLayout->addWidget(m_addButton);

    m_removeButton = new QPushButton("-", this);
    m_removeButton->setToolTip(tr("Remove selected layer"));
    m_removeButton->setFixedSize(28, 28);
    connect(m_removeButton, &QPushButton::clicked, this, &LayerPanel::onRemoveLayerClicked);
    buttonLayout->addWidget(m_removeButton);

    m_moveUpButton = new QPushButton("↑", this);
    m_moveUpButton->setToolTip(tr("Move layer up"));
    m_moveUpButton->setFixedSize(28, 28);
    connect(m_moveUpButton, &QPushButton::clicked, this, &LayerPanel::onMoveUpClicked);
    buttonLayout->addWidget(m_moveUpButton);

    m_moveDownButton = new QPushButton("↓", this);
    m_moveDownButton->setToolTip(tr("Move layer down"));
    m_moveDownButton->setFixedSize(28, 28);
    connect(m_moveDownButton, &QPushButton::clicked, this, &LayerPanel::onMoveDownClicked);
    buttonLayout->addWidget(m_moveDownButton);

    // Phase 5.5: Duplicate button
    m_duplicateButton = new QPushButton("⧉", this);  // Unicode clone/copy symbol
    m_duplicateButton->setToolTip(tr("Duplicate selected layer"));
    m_duplicateButton->setFixedSize(28, 28);
    connect(m_duplicateButton, &QPushButton::clicked, this, &LayerPanel::onDuplicateClicked);
    buttonLayout->addWidget(m_duplicateButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
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

    m_layerList->clear();

    // Phase 5.6.7: Check both page and edgeless doc
    if (!m_page && !m_edgelessDoc) {
        m_updatingList = false;
        updateButtonStates();
        return;
    }

    // Add layers to list (top layer first, so reverse order)
    int layerCount = getLayerCount();
    for (int i = layerCount - 1; i >= 0; --i) {
        QListWidgetItem* item = createLayerItem(i);
        if (item) {
            m_layerList->addItem(item);
        }
    }

    // Select the active layer
    int activeIndex = getActiveLayerIndex();
    if (activeIndex >= 0 && activeIndex < layerCount) {
        int row = layerIndexToRow(activeIndex);
        m_layerList->setCurrentRow(row);
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
    int currentRow = m_layerList->currentRow();
    int selectedLayerIndex = (currentRow >= 0) ? rowToLayerIndex(currentRow) : -1;

    // Add: always enabled if we have a source
    m_addButton->setEnabled(hasSource);

    // Remove: enabled if more than one layer and something selected
    m_removeButton->setEnabled(hasSource && layerCount > 1 && selectedLayerIndex >= 0);

    // Move Up: enabled if not at top (layer index < layerCount - 1)
    m_moveUpButton->setEnabled(hasSource && selectedLayerIndex >= 0 && 
                               selectedLayerIndex < layerCount - 1);

    // Move Down: enabled if not at bottom (layer index > 0)
    m_moveDownButton->setEnabled(hasSource && selectedLayerIndex > 0);
    
    // Phase 5.3: Selection buttons
    m_selectAllButton->setEnabled(hasSource && layerCount > 0);
    m_deselectAllButton->setEnabled(hasSource && layerCount > 0);
    
    // Merge: enabled if 2+ layers are checked
    int checkedCount = selectedLayerCount();
    m_mergeButton->setEnabled(hasSource && checkedCount >= 2);
    
    // Phase 5.5: Duplicate: enabled if a layer is selected
    m_duplicateButton->setEnabled(hasSource && selectedLayerIndex >= 0);
}

// ============================================================================
// Item Creation
// ============================================================================

QListWidgetItem* LayerPanel::createLayerItem(int layerIndex)
{
    // Phase 5.6.7: Use abstracted accessors
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return nullptr;
    }

    QString name = getLayerName(layerIndex);
    bool visible = getLayerVisible(layerIndex);
    bool locked = getLayerLocked(layerIndex);

    // Phase 5.2: Create item with just the layer name
    // Use icon for hidden layers, no icon for visible layers
    QListWidgetItem* item = new QListWidgetItem(name);
    
    if (!visible) {
        item->setIcon(m_notVisibleIcon);
    }
    
    // Store layer index as data
    item->setData(Qt::UserRole, layerIndex);
    
    // Phase 5.3: Add checkbox for selection
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    item->setCheckState(Qt::Unchecked);

    // Visual indication for locked layers
    if (locked) {
        item->setForeground(Qt::gray);
    }

    return item;
}

// ============================================================================
// Index Conversion
// ============================================================================

int LayerPanel::rowToLayerIndex(int row) const
{
    // Phase 5.6.7: Use abstracted layer count
    int layerCount = getLayerCount();
    if (layerCount == 0 || row < 0) {
        return -1;
    }
    // List is reversed: row 0 = top layer = highest index
    return layerCount - 1 - row;
}

int LayerPanel::layerIndexToRow(int layerIndex) const
{
    // Phase 5.6.7: Use abstracted layer count
    int layerCount = getLayerCount();
    if (layerCount == 0 || layerIndex < 0) {
        return -1;
    }
    // List is reversed: highest layer index = row 0
    return layerCount - 1 - layerIndex;
}

// ============================================================================
// Slots - Selection
// ============================================================================

void LayerPanel::onLayerSelectionChanged(int currentRow)
{
    // Phase 5.6.7: Check both sources
    if (m_updatingList || (!m_page && !m_edgelessDoc) || currentRow < 0) {
        updateButtonStates();
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        updateButtonStates();
        return;
    }

    // Update active layer
    if (getActiveLayerIndex() != layerIndex) {
        setActiveLayerIndex(layerIndex);
        emit activeLayerChanged(layerIndex);
    }

    updateButtonStates();
}

void LayerPanel::onItemClicked(QListWidgetItem* item)
{
    // Phase 5.6.7: Check both sources
    if (m_updatingList || (!m_page && !m_edgelessDoc) || !item) {
        return;
    }

    int layerIndex = item->data(Qt::UserRole).toInt();
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }

    // Check if user clicked in the visibility area
    // Phase 5.3: Checkbox is in the first ~20 pixels, visibility icon after that
    // We detect clicks in the 20-50 pixel range for visibility toggle
    
    // Get click position
    QPoint clickPos = m_layerList->mapFromGlobal(QCursor::pos());
    QRect itemRect = m_layerList->visualItemRect(item);
    
    // If click is after checkbox but in icon area (20-50 pixels), toggle visibility
    int relativeX = clickPos.x() - itemRect.left();
    if (relativeX >= 20 && relativeX < 50) {
        bool newVisible = !getLayerVisible(layerIndex);
        setLayerVisible(layerIndex, newVisible);
        
        // Phase 5.2: Update item icon based on visibility
        m_updatingList = true;
        if (newVisible) {
            item->setIcon(QIcon());  // No icon for visible layers
        } else {
            item->setIcon(m_notVisibleIcon);  // Show "not visible" icon
        }
        m_updatingList = false;
        
        emit layerVisibilityChanged(layerIndex, newVisible);
    }
}

void LayerPanel::onItemChanged(QListWidgetItem* item)
{
    // Phase 5.2/5.6.7: Handle layer rename via inline editing
    // Phase 5.3: Also handle checkbox state changes
    // Skip if we're programmatically updating the list
    if (m_updatingList || (!m_page && !m_edgelessDoc) || !item) {
        return;
    }
    
    int layerIndex = item->data(Qt::UserRole).toInt();
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount) {
        return;
    }
    
    // Phase 5.3: Update button states when checkbox changes
    // (Merge button depends on selection count)
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
    
    // Get the edited text (no prefix to strip - we use icons now)
    QString newText = item->text().trimmed();
    
    // Don't allow empty names
    if (newText.isEmpty()) {
        newText = QString("Layer %1").arg(layerIndex + 1);
    }
    
    // Only update if name actually changed
    QString currentName = getLayerName(layerIndex);
    if (currentName != newText) {
        setLayerName(layerIndex, newText);
        emit layerRenamed(layerIndex, newText);
    }
    
    // Ensure the display text is correct
    m_updatingList = true;
    item->setText(getLayerName(layerIndex));
    m_updatingList = false;
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
    // Phase 5.6.7: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
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
    // Phase 5.6.7: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    int layerCount = getLayerCount();
    if (layerIndex < 0 || layerIndex >= layerCount - 1) {
        return;  // Can't move up if already at top
    }

    // Move layer up (increase index)
    int newIndex = layerIndex + 1;
    if (!moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Refresh and reselect
    refreshLayerList();
    m_layerList->setCurrentRow(layerIndexToRow(newIndex));

    emit layerMoved(layerIndex, newIndex);
}

void LayerPanel::onMoveDownClicked()
{
    // Phase 5.6.7: Check both sources
    if (!m_page && !m_edgelessDoc) {
        return;
    }

    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    if (layerIndex <= 0) {
        return;  // Can't move down if already at bottom
    }

    // Move layer down (decrease index)
    int newIndex = layerIndex - 1;
    if (!moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Refresh and reselect
    refreshLayerList();
    m_layerList->setCurrentRow(layerIndexToRow(newIndex));

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
// Phase 5.3: Selection API
// ============================================================================

QVector<int> LayerPanel::selectedLayerIndices() const
{
    QVector<int> indices;
    
    for (int row = 0; row < m_layerList->count(); ++row) {
        QListWidgetItem* item = m_layerList->item(row);
        if (item && item->checkState() == Qt::Checked) {
            int layerIndex = rowToLayerIndex(row);
            if (layerIndex >= 0) {
                indices.append(layerIndex);
            }
        }
    }
    
    // Sort in ascending order (bottom layer first)
    std::sort(indices.begin(), indices.end());
    return indices;
}

int LayerPanel::selectedLayerCount() const
{
    int count = 0;
    for (int row = 0; row < m_layerList->count(); ++row) {
        QListWidgetItem* item = m_layerList->item(row);
        if (item && item->checkState() == Qt::Checked) {
            ++count;
        }
    }
    return count;
}

void LayerPanel::selectAllLayers()
{
    m_updatingList = true;
    for (int row = 0; row < m_layerList->count(); ++row) {
        QListWidgetItem* item = m_layerList->item(row);
        if (item) {
            item->setCheckState(Qt::Checked);
        }
    }
    m_updatingList = false;
    
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
}

void LayerPanel::deselectAllLayers()
{
    m_updatingList = true;
    for (int row = 0; row < m_layerList->count(); ++row) {
        QListWidgetItem* item = m_layerList->item(row);
        if (item) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    m_updatingList = false;
    
    updateButtonStates();
    emit selectionChanged(selectedLayerIndices());
}

// ============================================================================
// Phase 5.3: Selection Slots
// ============================================================================

void LayerPanel::onSelectAllClicked()
{
    selectAllLayers();
}

void LayerPanel::onDeselectAllClicked()
{
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
    // Phase 5.5: Duplicate selected layer
    if (!m_page && !m_edgelessDoc) {
        return;
    }
    
    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }
    
    int layerIndex = rowToLayerIndex(currentRow);
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
        // Refresh the layer list
        refreshLayerList();
        
        // Select the new layer and sync viewport
        emit activeLayerChanged(newIndex);
        
        // Emit signal for MainWindow to update viewport
        emit layerDuplicated(layerIndex, newIndex);
    }
}
