// ============================================================================
// Page - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.6)
// ============================================================================

#include "Page.h"
#include <QUuid>       // Phase C.0.1: UUID generation for LinkObject position links
#include <algorithm>
#include <climits>  // For INT_MIN (Phase O3.5.5: affinity filtering)

// ===== Constructors =====

Page::Page()
    : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces))  // Phase C.0.1
    , size(816, 1056)  // Default: US Letter at 96 DPI (consistent with Document::defaultPageSize)
{
    // Create one default layer
    vectorLayers.push_back(std::make_unique<VectorLayer>("Layer 1"));
}

Page::Page(const QSizeF& pageSize)
    : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces))  // Phase C.0.1
    , size(pageSize)
{
    // Create one default layer
    vectorLayers.push_back(std::make_unique<VectorLayer>("Layer 1"));
}

// ===== Layer Management =====

VectorLayer* Page::activeLayer()
{
    if (activeLayerIndex >= 0 && activeLayerIndex < static_cast<int>(vectorLayers.size())) {
        return vectorLayers[activeLayerIndex].get();
    }
    return nullptr;
}

const VectorLayer* Page::activeLayer() const
{
    if (activeLayerIndex >= 0 && activeLayerIndex < static_cast<int>(vectorLayers.size())) {
        return vectorLayers[activeLayerIndex].get();
    }
    return nullptr;
}

VectorLayer* Page::addLayer(const QString& name)
{
    auto layer = std::make_unique<VectorLayer>(name);
    VectorLayer* ptr = layer.get();
    vectorLayers.push_back(std::move(layer));
    activeLayerIndex = static_cast<int>(vectorLayers.size()) - 1;
    modified = true;
    return ptr;
}

bool Page::removeLayer(int index)
{
    // Don't remove the last layer
    if (vectorLayers.size() <= 1) {
        return false;
    }
    
    if (index < 0 || index >= static_cast<int>(vectorLayers.size())) {
        return false;
    }
    
    // Phase O3.5.7: Adjust object affinities before removing the layer
    handleLayerDeleted(index);
    
    vectorLayers.erase(vectorLayers.begin() + index);
    
    // Adjust active layer index
    if (activeLayerIndex >= static_cast<int>(vectorLayers.size())) {
        activeLayerIndex = static_cast<int>(vectorLayers.size()) - 1;
    }
    
    modified = true;
    return true;
}

// -----------------------------------------------------------------------------
// Phase O3.5.7: Handle object affinities when a layer is deleted
// Option C: Move objects to layer below, shift higher affinities down
// -----------------------------------------------------------------------------
void Page::handleLayerDeleted(int deletedLayerIndex)
{
    if (objects.empty()) {
        return;  // No objects to adjust
    }
    
    // Objects with affinity = deletedLayerIndex - 1 are tied to the deleted layer
    // Option C: Move them down by 1 (to the layer below)
    // Objects with higher affinity also shift down by 1
    
    int deletedAffinity = deletedLayerIndex - 1;
    
    for (auto& obj : objects) {
        if (obj->layerAffinity >= deletedAffinity) {
            // This object was tied to the deleted layer or a layer above it
            // Shift down by 1 (but not below -1, the background)
            obj->layerAffinity = std::max(-1, obj->layerAffinity - 1);
        }
        // Objects with lower affinity are unaffected
    }
    
    // Rebuild the affinity map since affinities changed
    rebuildAffinityMap();
}

bool Page::moveLayer(int from, int to)
{
    int layerCount = static_cast<int>(vectorLayers.size());
    if (from < 0 || from >= layerCount ||
        to < 0 || to >= layerCount ||
        from == to) {
        return false;
    }
    
    // Move the layer
    auto layer = std::move(vectorLayers[from]);
    vectorLayers.erase(vectorLayers.begin() + from);
    vectorLayers.insert(vectorLayers.begin() + to, std::move(layer));
    
    // Adjust active layer index
    if (activeLayerIndex == from) {
        activeLayerIndex = to;
    } else if (from < activeLayerIndex && to >= activeLayerIndex) {
        activeLayerIndex--;
    } else if (from > activeLayerIndex && to <= activeLayerIndex) {
        activeLayerIndex++;
    }
    
    modified = true;
    
    // Phase O3.5.6: Adjust object affinities after layer move
    adjustObjectAffinitiesAfterLayerMove(from, to);
    
    return true;
}

// -----------------------------------------------------------------------------
// Phase O3.5.6: Adjust object affinities after a layer move
// -----------------------------------------------------------------------------
void Page::adjustObjectAffinitiesAfterLayerMove(int from, int to)
{
    if (objects.empty()) {
        return;  // No objects to adjust
    }
    
    // When a layer moves from index `from` to index `to`:
    // - The layer that was at `from` is now at `to`
    // - Layers between them shift by 1 in the opposite direction
    //
    // For affinity (which is layerIndex - 1):
    // - Objects with affinity (from - 1) should get affinity (to - 1)
    // - Other objects need adjustment based on the shift direction
    
    // Calculate the new affinity for each original affinity value
    // using the layer index remapping logic
    for (auto& obj : objects) {
        int oldAffinity = obj->layerAffinity;
        int oldLayerIndex = oldAffinity + 1;  // The layer this object was tied to
        int newLayerIndex = oldLayerIndex;
        
        // Determine new layer index based on how layers shifted
        if (oldLayerIndex == from) {
            // This object was tied to the moved layer
            newLayerIndex = to;
        } else if (from < to) {
            // Layer moved up: layers from (from+1) to (to) shift down by 1
            if (oldLayerIndex > from && oldLayerIndex <= to) {
                newLayerIndex = oldLayerIndex - 1;
            }
        } else if (from > to) {
            // Layer moved down: layers from (to) to (from-1) shift up by 1
            if (oldLayerIndex >= to && oldLayerIndex < from) {
                newLayerIndex = oldLayerIndex + 1;
            }
        }
        
        int newAffinity = newLayerIndex - 1;
        if (newAffinity != oldAffinity) {
            obj->layerAffinity = newAffinity;
        }
    }
    
    // Rebuild the affinity map since affinities changed
    rebuildAffinityMap();
}

bool Page::mergeLayers(int targetIndex, const QVector<int>& sourceIndices)
{
    int count = static_cast<int>(vectorLayers.size());
    
    // Validate target index
    if (targetIndex < 0 || targetIndex >= count) {
        return false;
    }
    
    // Validate all source indices
    for (int idx : sourceIndices) {
        if (idx < 0 || idx >= count || idx == targetIndex) {
            return false;
        }
    }
    
    // Ensure we don't remove all layers
    if (sourceIndices.size() >= count) {
        return false;
    }
    
    VectorLayer* target = vectorLayers[targetIndex].get();
    
    // Collect strokes from source layers into target
    for (int srcIdx : sourceIndices) {
        VectorLayer* source = vectorLayers[srcIdx].get();
        if (source) {
            for (VectorStroke& stroke : source->strokes()) {
                target->addStroke(std::move(stroke));
            }
            source->clear();
        }
    }
    
    // Remove source layers in reverse order to preserve indices
    QVector<int> sortedSources = sourceIndices;
    std::sort(sortedSources.begin(), sortedSources.end(), std::greater<int>());
    
    for (int srcIdx : sortedSources) {
        // Phase O3.5.7: Adjust object affinities before removing the layer
        handleLayerDeleted(srcIdx);
        
        vectorLayers.erase(vectorLayers.begin() + srcIdx);
    }
    
    // Adjust active layer index if needed
    if (activeLayerIndex >= static_cast<int>(vectorLayers.size())) {
        activeLayerIndex = static_cast<int>(vectorLayers.size()) - 1;
    }
    
    modified = true;
    return true;
}

int Page::duplicateLayer(int index)
{
    int count = static_cast<int>(vectorLayers.size());
    
    // Validate index
    if (index < 0 || index >= count) {
        return -1;
    }
    
    VectorLayer* source = vectorLayers[index].get();
    if (!source) {
        return -1;
    }
    
    // Create new layer
    auto newLayer = std::make_unique<VectorLayer>();
    newLayer->name = source->name + " Copy";
    newLayer->visible = source->visible;
    newLayer->opacity = source->opacity;
    newLayer->locked = false;  // Unlock the copy for immediate editing
    
    // Deep copy strokes with new UUIDs
    for (const VectorStroke& stroke : source->strokes()) {
        VectorStroke copy = stroke;  // Copy all properties
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New UUID
        newLayer->addStroke(std::move(copy));
    }
    
    // Insert above original (at index + 1)
    int newIndex = index + 1;
    vectorLayers.insert(vectorLayers.begin() + newIndex, std::move(newLayer));
    
    // Adjust active layer index if it's at or above the insertion point
    if (activeLayerIndex >= newIndex) {
        activeLayerIndex++;
    }
    
    modified = true;
    return newIndex;
}

VectorLayer* Page::layer(int index)
{
    if (index >= 0 && index < static_cast<int>(vectorLayers.size())) {
        return vectorLayers[index].get();
    }
    return nullptr;
}

const VectorLayer* Page::layer(int index) const
{
    if (index >= 0 && index < static_cast<int>(vectorLayers.size())) {
        return vectorLayers[index].get();
    }
    return nullptr;
}

void Page::releaseLayerCaches()
{
    for (auto& layer : vectorLayers) {
        if (layer) {
            layer->releaseStrokeCache();
        }
    }
}

bool Page::hasLayerCachesAllocated() const
{
    for (const auto& layer : vectorLayers) {
        if (layer && layer->hasStrokeCacheAllocated()) {
            return true;
        }
    }
    return false;
}

// ===== Object Management =====

void Page::addObject(std::unique_ptr<InsertedObject> obj)
{
    if (obj) {
        // Add to affinity map before moving
        int affinity = obj->getLayerAffinity();
        InsertedObject* ptr = obj.get();
        
        objects.push_back(std::move(obj));
        objectsByAffinity[affinity].push_back(ptr);
        
        modified = true;
    }
}

bool Page::removeObject(const QString& id)
{
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i]->id == id) {
            // Remove from affinity map first
            InsertedObject* obj = objects[i].get();
            int affinity = obj->getLayerAffinity();
            
            auto it = objectsByAffinity.find(affinity);
            if (it != objectsByAffinity.end()) {
                auto& group = it->second;
                group.erase(
                    std::remove(group.begin(), group.end(), obj),
                    group.end()
                );
                // Clean up empty groups
                if (group.empty()) {
                    objectsByAffinity.erase(it);
                }
            }
            
            // Remove from objects vector
            objects.erase(objects.begin() + i);
            modified = true;
            return true;
        }
    }
    return false;
}

std::unique_ptr<InsertedObject> Page::extractObject(const QString& id)
{
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i]->id == id) {
            // Remove from affinity map first
            InsertedObject* obj = objects[i].get();
            int affinity = obj->getLayerAffinity();
            
            auto it = objectsByAffinity.find(affinity);
            if (it != objectsByAffinity.end()) {
                auto& group = it->second;
                group.erase(
                    std::remove(group.begin(), group.end(), obj),
                    group.end()
                );
                if (group.empty()) {
                    objectsByAffinity.erase(it);
                }
            }
            
            // Extract from objects vector (move ownership out)
            std::unique_ptr<InsertedObject> extracted = std::move(objects[i]);
            objects.erase(objects.begin() + i);
            modified = true;
            return extracted;
        }
    }
    return nullptr;
}

InsertedObject* Page::objectAtPoint(const QPointF& pt, int affinityFilter)
{
    // Check in reverse order (topmost first by z-order)
    // First, create a sorted list by z-order (descending)
    std::vector<InsertedObject*> sortedObjects;
    sortedObjects.reserve(objects.size());
    for (auto& obj : objects) {
        sortedObjects.push_back(obj.get());
    }
    std::sort(sortedObjects.begin(), sortedObjects.end(),
              [](InsertedObject* a, InsertedObject* b) {
                  return a->zOrder > b->zOrder;
              });
    
    for (InsertedObject* obj : sortedObjects) {
        // Phase O3.5.5: Affinity filtering (Option A - Strict)
        // If an affinity filter is provided (not INT_MIN), only consider objects
        // with matching affinity. This ensures users can only select objects
        // "tied to" the current layer.
        if (affinityFilter != INT_MIN && obj->layerAffinity != affinityFilter) {
            continue;  // Skip objects with non-matching affinity
        }
        
        if (obj->visible && obj->containsPoint(pt)) {
            return obj;
        }
    }
    return nullptr;
}

InsertedObject* Page::objectById(const QString& id)
{
    for (auto& obj : objects) {
        if (obj->id == id) {
            return obj.get();
        }
    }
    return nullptr;
}

void Page::sortObjectsByZOrder()
{
    std::sort(objects.begin(), objects.end(),
              [](const std::unique_ptr<InsertedObject>& a,
                 const std::unique_ptr<InsertedObject>& b) {
                  return a->zOrder < b->zOrder;
              });
}

void Page::rebuildAffinityMap()
{
    // Clear existing map
    objectsByAffinity.clear();
    
    // Group objects by their layerAffinity value
    for (auto& obj : objects) {
        if (obj) {
            int affinity = obj->getLayerAffinity();
            objectsByAffinity[affinity].push_back(obj.get());
        }
    }
}

bool Page::updateObjectAffinity(const QString& id, int newAffinity)
{
    // Find the object
    InsertedObject* obj = objectById(id);
    if (!obj) {
        return false;
    }
    
    int oldAffinity = obj->getLayerAffinity();
    
    // If affinity unchanged, nothing to do
    if (oldAffinity == newAffinity) {
        return true;
    }
    
    // Remove from old affinity group
    auto oldIt = objectsByAffinity.find(oldAffinity);
    if (oldIt != objectsByAffinity.end()) {
        auto& oldGroup = oldIt->second;
        oldGroup.erase(
            std::remove(oldGroup.begin(), oldGroup.end(), obj),
            oldGroup.end()
        );
        // Clean up empty groups
        if (oldGroup.empty()) {
            objectsByAffinity.erase(oldIt);
        }
    }
    
    // Update the object's affinity
    obj->setLayerAffinity(newAffinity);
    
    // Add to new affinity group
    objectsByAffinity[newAffinity].push_back(obj);
    
    modified = true;
    return true;
}

// ===== Rendering =====

/**
 * @deprecated This method renders objects AFTER all layers, bypassing the affinity system.
 * Use DocumentViewport::renderPage() which calls renderObjectsWithAffinity() for proper
 * interleaved rendering based on layer affinity.
 * 
 * This method is kept for backward compatibility but should not be used for main rendering.
 */
void Page::render(QPainter& painter, const QPixmap* pdfBackground, qreal zoom) const
{
    // 1. Render background
    renderBackground(painter, pdfBackground, zoom);
    
    // 2. Render layers (bottom to top)
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& layer : vectorLayers) {
        if (layer->visible) {
            // TODO: Handle layer opacity by rendering to intermediate pixmap
            layer->render(painter);
        }
    }
    
    // 3. Render objects (sorted by z-order)
    std::vector<InsertedObject*> sortedObjects;
    sortedObjects.reserve(objects.size());
    for (const auto& obj : objects) {
        sortedObjects.push_back(obj.get());
    }
    std::sort(sortedObjects.begin(), sortedObjects.end(),
              [](InsertedObject* a, InsertedObject* b) {
                  return a->zOrder < b->zOrder;
              });
    
    for (InsertedObject* obj : sortedObjects) {
        if (obj->visible) {
            obj->render(painter, zoom);
        }
    }
}

void Page::renderBackground(QPainter& painter, const QPixmap* pdfBackground, qreal zoom) const
{
    QRectF pageRect(0, 0, size.width() * zoom, size.height() * zoom);
    
    // Handle PDF and Custom backgrounds specially (they need pixmaps)
    if (backgroundType == BackgroundType::PDF) {
        painter.fillRect(pageRect, backgroundColor);
        if (pdfBackground && !pdfBackground->isNull()) {
            painter.drawPixmap(pageRect.toRect(), *pdfBackground);
        }
        return;
    }
    
    if (backgroundType == BackgroundType::Custom) {
        painter.fillRect(pageRect, backgroundColor);
        if (!customBackground.isNull()) {
            painter.drawPixmap(pageRect.toRect(), customBackground);
        }
        return;
    }
    
    // For None/Grid/Lines, use the shared helper
    // Note: spacing is scaled by zoom since we're drawing in zoomed coordinates
    renderBackgroundPattern(
        painter,
        pageRect,
        backgroundColor,
        backgroundType,
        gridColor,
        gridSpacing * zoom,
        lineSpacing * zoom,
        1.0  // pen width
    );
}

void Page::renderBackgroundPattern(
    QPainter& painter,
    const QRectF& rect,
    const QColor& bgColor,
    BackgroundType bgType,
    const QColor& gridColor,
    qreal gridSpacing,
    qreal lineSpacing,
    qreal penWidth)
{
    // Fill background color
    painter.fillRect(rect, bgColor);
    
    // Draw pattern based on type
    switch (bgType) {
        case BackgroundType::None:
        case BackgroundType::PDF:
        case BackgroundType::Custom:
            // These are handled elsewhere (PDF/Custom need pixmaps)
            break;
            
        case BackgroundType::Grid:
            {
                painter.setPen(QPen(gridColor, penWidth));
                
                // Vertical lines
                for (qreal x = rect.left() + gridSpacing; x < rect.right(); x += gridSpacing) {
                    painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
                }
                
                // Horizontal lines
                for (qreal y = rect.top() + gridSpacing; y < rect.bottom(); y += gridSpacing) {
                    painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
                }
            }
            break;
            
        case BackgroundType::Lines:
            {
                painter.setPen(QPen(gridColor, penWidth));
                
                // Horizontal lines only
                for (qreal y = rect.top() + lineSpacing; y < rect.bottom(); y += lineSpacing) {
                    painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
                }
            }
            break;
    }
}

void Page::renderObjects(QPainter& painter, qreal zoom) const
{
    if (objects.empty()) {
        return;
    }
    
    // Sort objects by z-order
    std::vector<InsertedObject*> sortedObjects;
    sortedObjects.reserve(objects.size());
    for (const auto& obj : objects) {
        sortedObjects.push_back(obj.get());
    }
    std::sort(sortedObjects.begin(), sortedObjects.end(),
              [](InsertedObject* a, InsertedObject* b) {
                  return a->zOrder < b->zOrder;
              });
    
    // Render each visible object
    for (InsertedObject* obj : sortedObjects) {
        if (obj->visible) {
            obj->render(painter, zoom);
        }
    }
}

void Page::renderObjectsWithAffinity(QPainter& painter, qreal zoom, int affinity, 
                                      bool layerVisible, const QSet<QString>* excludeIds) const
{
    // Phase O3.5.8: If the tied layer is hidden, skip rendering objects
    // Objects with affinity = K are tied to Layer K+1. When that layer is hidden,
    // the caller passes layerVisible=false.
    if (!layerVisible) {
        return;  // Layer is hidden, don't render its tied objects
    }
    
    // Find objects with the specified affinity
    auto it = objectsByAffinity.find(affinity);
    if (it == objectsByAffinity.end()) {
        return;  // No objects with this affinity
    }
    
    // Get the objects for this affinity group
    // Note: We need to copy and sort because the map stores non-const pointers
    // but we're in a const method. We sort by zOrder within this affinity group.
    std::vector<InsertedObject*> objs = it->second;
    
    std::sort(objs.begin(), objs.end(),
              [](InsertedObject* a, InsertedObject* b) {
                  return a->zOrder < b->zOrder;
              });
    
    // Render each visible object in this affinity group
    for (InsertedObject* obj : objs) {
        // Phase O4.1: Skip excluded objects (used during background snapshot capture)
        if (excludeIds && excludeIds->contains(obj->id)) {
            continue;
        }
        
        if (obj->visible) {
            obj->render(painter, zoom);
        }
    }
}

// ===== Serialization =====

QJsonObject Page::toJson() const
{
    QJsonObject obj;
    
    // Identity
    obj["uuid"] = uuid;  // Phase C.0.1: For LinkObject position links
    obj["pageIndex"] = pageIndex;
    obj["width"] = size.width();
    obj["height"] = size.height();
    
    // Background
    obj["backgroundType"] = static_cast<int>(backgroundType);
    obj["pdfPageNumber"] = pdfPageNumber;
    obj["backgroundColor"] = backgroundColor.name(QColor::HexArgb);
    obj["gridColor"] = gridColor.name(QColor::HexRgb);  // Use 6-char hex (#RRGGBB) for clarity
    obj["gridSpacing"] = gridSpacing;
    obj["lineSpacing"] = lineSpacing;
    // Note: customBackground pixmap is not serialized - path should be stored separately
    
    // Bookmarks (Task 1.2.6)
    obj["isBookmarked"] = isBookmarked;
    if (!bookmarkLabel.isEmpty()) {
        obj["bookmarkLabel"] = bookmarkLabel;
    }
    
    // Active layer
    obj["activeLayerIndex"] = activeLayerIndex;
    
    // Layers
    QJsonArray layersArray;
    for (const auto& layer : vectorLayers) {
        layersArray.append(layer->toJson());
    }
    obj["layers"] = layersArray;
    
    // Objects
    QJsonArray objectsArray;
    for (const auto& object : objects) {
        objectsArray.append(object->toJson());
    }
    obj["objects"] = objectsArray;
    
    return obj;
}

std::unique_ptr<Page> Page::fromJson(const QJsonObject& obj)
{
    auto page = std::make_unique<Page>();
    
    // Clear default layer (we'll load from JSON)
    page->vectorLayers.clear();
    
    // Identity
    // Phase C.0.1: Load UUID, or keep the generated one for legacy documents
    QString loadedUuid = obj["uuid"].toString();
    if (!loadedUuid.isEmpty()) {
        page->uuid = loadedUuid;
    }
    // else: page->uuid already has a freshly generated UUID from constructor
    
    page->pageIndex = obj["pageIndex"].toInt(0);
    // Default to US Letter at 96 DPI (consistent with Document::defaultPageSize)
    page->size = QSizeF(obj["width"].toDouble(816), obj["height"].toDouble(1056));
    
    // Background
    page->backgroundType = static_cast<BackgroundType>(obj["backgroundType"].toInt(0));
    page->pdfPageNumber = obj["pdfPageNumber"].toInt(-1);
    page->backgroundColor = QColor(obj["backgroundColor"].toString("#ffffffff"));
    page->gridColor = QColor(obj["gridColor"].toString("#c8c8c8"));  // Gray (200,200,200) in 6-char hex
    page->gridSpacing = obj["gridSpacing"].toInt(32);
    page->lineSpacing = obj["lineSpacing"].toInt(32);
    
    // Bookmarks (Task 1.2.6)
    page->isBookmarked = obj["isBookmarked"].toBool(false);
    page->bookmarkLabel = obj["bookmarkLabel"].toString();
    
    // Active layer
    page->activeLayerIndex = obj["activeLayerIndex"].toInt(0);
    
    // Layers
    QJsonArray layersArray = obj["layers"].toArray();
    for (const auto& val : layersArray) {
        page->vectorLayers.push_back(
            std::make_unique<VectorLayer>(VectorLayer::fromJson(val.toObject()))
        );
    }
    
    // Ensure at least one layer exists
    if (page->vectorLayers.empty()) {
        page->vectorLayers.push_back(std::make_unique<VectorLayer>("Layer 1"));
    }
    
    // Clamp active layer index
    if (page->activeLayerIndex >= static_cast<int>(page->vectorLayers.size())) {
        page->activeLayerIndex = static_cast<int>(page->vectorLayers.size()) - 1;
    }
    
    // Objects
    QJsonArray objectsArray = obj["objects"].toArray();
    for (const auto& val : objectsArray) {
        auto object = InsertedObject::fromJson(val.toObject());
        if (object) {
            page->objects.push_back(std::move(object));
        }
    }
    
    // Build affinity map after loading all objects
    page->rebuildAffinityMap();
    
    page->modified = false;
    return page;
}

int Page::loadImages(const QString& basePath)
{
    if (basePath.isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Page::loadImages: basePath is empty, skipping";
        #endif
        return 0;
    }
    
    // CR-O2: Use virtual loadAssets() instead of type-specific code
    // This allows future object types with assets to work automatically.
    int loaded = 0;
    for (auto& obj : objects) {
        // loadAssets() returns true for objects without external assets (base class)
        // For ImageObject, it loads the pixmap from the assets folder
        if (!obj->isAssetLoaded()) {  // Only load if not already loaded
            if (obj->loadAssets(basePath)) {
                loaded++;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Page::loadImages: Loaded asset for" << obj->type() 
                         << "object" << obj->id;
#endif
            } else {
                qWarning() << "Page::loadImages: Failed to load asset for" 
                           << obj->type() << "object" << obj->id;
            }
        }
    }
    return loaded;
}

// ===== Factory Methods =====

std::unique_ptr<Page> Page::createDefault(const QSizeF& pageSize)
{
    auto page = std::make_unique<Page>(pageSize);
    page->backgroundType = BackgroundType::None;
    return page;
}

std::unique_ptr<Page> Page::createForPdf(const QSizeF& pageSize, int pdfPage)
{
    auto page = std::make_unique<Page>(pageSize);
    page->backgroundType = BackgroundType::PDF;
    page->pdfPageNumber = pdfPage;
    return page;
}

// ===== Utility =====

bool Page::hasContent() const
{
    // Check layers for strokes
    for (const auto& layer : vectorLayers) {
        if (!layer->isEmpty()) {
            return true;
        }
    }
    
    // Check for objects
    return !objects.empty();
}

void Page::clearContent()
{
    // Clear all layers
    for (auto& layer : vectorLayers) {
        layer->clear();
    }
    
    // Clear objects and affinity map
    objects.clear();
    objectsByAffinity.clear();
    
    modified = true;
}

QRectF Page::contentBoundingRect() const
{
    QRectF bounds;
    
    // Get bounds from all layers
    for (const auto& layer : vectorLayers) {
        QRectF layerBounds = layer->boundingBox();
        if (!layerBounds.isEmpty()) {
            if (bounds.isEmpty()) {
                bounds = layerBounds;
            } else {
                bounds = bounds.united(layerBounds);
            }
        }
    }
    
    // Get bounds from objects
    for (const auto& obj : objects) {
        QRectF objBounds = obj->boundingRect();
        if (!objBounds.isEmpty()) {
            if (bounds.isEmpty()) {
                bounds = objBounds;
            } else {
                bounds = bounds.united(objBounds);
            }
        }
    }
    
    return bounds;
}
