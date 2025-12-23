// ============================================================================
// LayerPanel Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.3)
// ============================================================================

#include "LayerPanel.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>

// ============================================================================
// Constructor
// ============================================================================

LayerPanel::LayerPanel(QWidget* parent)
    : QWidget(parent)
{
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

    // Button bar
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

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

// ============================================================================
// Page Connection
// ============================================================================

void LayerPanel::setCurrentPage(Page* page)
{
    if (m_page == page) {
        return;
    }

    m_page = page;
    refreshLayerList();
}

// ============================================================================
// Refresh
// ============================================================================

void LayerPanel::refreshLayerList()
{
    m_updatingList = true;

    m_layerList->clear();

    if (!m_page) {
        m_updatingList = false;
        updateButtonStates();
        return;
    }

    // Add layers to list (top layer first, so reverse order)
    int layerCount = m_page->layerCount();
    for (int i = layerCount - 1; i >= 0; --i) {
        QListWidgetItem* item = createLayerItem(i);
        m_layerList->addItem(item);
    }

    // Select the active layer
    int activeIndex = m_page->activeLayerIndex;
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
    bool hasPage = (m_page != nullptr);
    int layerCount = hasPage ? m_page->layerCount() : 0;
    int currentRow = m_layerList->currentRow();
    int selectedLayerIndex = (currentRow >= 0) ? rowToLayerIndex(currentRow) : -1;

    // Add: always enabled if we have a page
    m_addButton->setEnabled(hasPage);

    // Remove: enabled if more than one layer and something selected
    m_removeButton->setEnabled(hasPage && layerCount > 1 && selectedLayerIndex >= 0);

    // Move Up: enabled if not at top (layer index < layerCount - 1)
    m_moveUpButton->setEnabled(hasPage && selectedLayerIndex >= 0 && 
                               selectedLayerIndex < layerCount - 1);

    // Move Down: enabled if not at bottom (layer index > 0)
    m_moveDownButton->setEnabled(hasPage && selectedLayerIndex > 0);
}

// ============================================================================
// Item Creation
// ============================================================================

QListWidgetItem* LayerPanel::createLayerItem(int layerIndex)
{
    if (!m_page || layerIndex < 0 || layerIndex >= m_page->layerCount()) {
        return nullptr;
    }

    VectorLayer* layer = m_page->layer(layerIndex);
    if (!layer) {
        return nullptr;
    }

    // Create item with visibility indicator and name
    QString displayText;
    if (layer->visible) {
        displayText = QString("👁 %1").arg(layer->name);
    } else {
        displayText = QString("   %1").arg(layer->name);
    }

    QListWidgetItem* item = new QListWidgetItem(displayText);
    
    // Store layer index as data
    item->setData(Qt::UserRole, layerIndex);

    // Visual indication for locked layers
    if (layer->locked) {
        item->setForeground(Qt::gray);
    }

    return item;
}

// ============================================================================
// Index Conversion
// ============================================================================

int LayerPanel::rowToLayerIndex(int row) const
{
    if (!m_page || row < 0) {
        return -1;
    }
    // List is reversed: row 0 = top layer = highest index
    return m_page->layerCount() - 1 - row;
}

int LayerPanel::layerIndexToRow(int layerIndex) const
{
    if (!m_page || layerIndex < 0) {
        return -1;
    }
    // List is reversed: highest layer index = row 0
    return m_page->layerCount() - 1 - layerIndex;
}

// ============================================================================
// Slots - Selection
// ============================================================================

void LayerPanel::onLayerSelectionChanged(int currentRow)
{
    if (m_updatingList || !m_page || currentRow < 0) {
        updateButtonStates();
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    if (layerIndex < 0 || layerIndex >= m_page->layerCount()) {
        updateButtonStates();
        return;
    }

    // Update active layer on the page
    if (m_page->activeLayerIndex != layerIndex) {
        m_page->activeLayerIndex = layerIndex;
        emit activeLayerChanged(layerIndex);
    }

    updateButtonStates();
}

void LayerPanel::onItemClicked(QListWidgetItem* item)
{
    if (m_updatingList || !m_page || !item) {
        return;
    }

    int layerIndex = item->data(Qt::UserRole).toInt();
    if (layerIndex < 0 || layerIndex >= m_page->layerCount()) {
        return;
    }

    VectorLayer* layer = m_page->layer(layerIndex);
    if (!layer) {
        return;
    }

    // Check if user clicked in the visibility area (first few characters)
    // For simplicity, clicking anywhere toggles visibility
    // In a more sophisticated UI, we'd use a custom widget with a checkbox
    
    // Get click position - if it's in the left part, toggle visibility
    QPoint clickPos = m_layerList->mapFromGlobal(QCursor::pos());
    QRect itemRect = m_layerList->visualItemRect(item);
    
    // If click is in the first 30 pixels, toggle visibility
    if (clickPos.x() < itemRect.left() + 30) {
        layer->visible = !layer->visible;
        
        // Update item display
        QString displayText;
        if (layer->visible) {
            displayText = QString("👁 %1").arg(layer->name);
        } else {
            displayText = QString("   %1").arg(layer->name);
        }
        item->setText(displayText);
        
        emit layerVisibilityChanged(layerIndex, layer->visible);
    }
}

// ============================================================================
// Slots - Buttons
// ============================================================================

void LayerPanel::onAddLayerClicked()
{
    if (!m_page) {
        return;
    }

    // Generate a unique layer name
    int layerCount = m_page->layerCount();
    QString layerName = QString("Layer %1").arg(layerCount + 1);

    // Add the layer
    VectorLayer* newLayer = m_page->addLayer(layerName);
    if (!newLayer) {
        return;
    }

    int newIndex = m_page->layerCount() - 1;  // New layer is at the top

    // Set as active
    m_page->activeLayerIndex = newIndex;

    // Refresh and select
    refreshLayerList();

    emit layerAdded(newIndex);
    emit activeLayerChanged(newIndex);
}

void LayerPanel::onRemoveLayerClicked()
{
    if (!m_page) {
        return;
    }

    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    if (layerIndex < 0 || layerIndex >= m_page->layerCount()) {
        return;
    }

    // Don't remove the last layer
    if (m_page->layerCount() <= 1) {
        return;
    }

    // Remove the layer
    if (!m_page->removeLayer(layerIndex)) {
        return;
    }

    // Adjust active layer index if needed
    if (m_page->activeLayerIndex >= m_page->layerCount()) {
        m_page->activeLayerIndex = m_page->layerCount() - 1;
    }

    // Refresh
    refreshLayerList();

    emit layerRemoved(layerIndex);
    emit activeLayerChanged(m_page->activeLayerIndex);
}

void LayerPanel::onMoveUpClicked()
{
    if (!m_page) {
        return;
    }

    int currentRow = m_layerList->currentRow();
    if (currentRow < 0) {
        return;
    }

    int layerIndex = rowToLayerIndex(currentRow);
    if (layerIndex < 0 || layerIndex >= m_page->layerCount() - 1) {
        return;  // Can't move up if already at top
    }

    // Move layer up (increase index)
    int newIndex = layerIndex + 1;
    if (!m_page->moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Update active layer index
    if (m_page->activeLayerIndex == layerIndex) {
        m_page->activeLayerIndex = newIndex;
    }

    // Refresh and reselect
    refreshLayerList();
    m_layerList->setCurrentRow(layerIndexToRow(newIndex));

    emit layerMoved(layerIndex, newIndex);
}

void LayerPanel::onMoveDownClicked()
{
    if (!m_page) {
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
    if (!m_page->moveLayer(layerIndex, newIndex)) {
        return;
    }

    // Update active layer index
    if (m_page->activeLayerIndex == layerIndex) {
        m_page->activeLayerIndex = newIndex;
    }

    // Refresh and reselect
    refreshLayerList();
    m_layerList->setCurrentRow(layerIndexToRow(newIndex));

    emit layerMoved(layerIndex, newIndex);
}
