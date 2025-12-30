// ============================================================================
// Page - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.6)
// ============================================================================

#include "Page.h"
#include <algorithm>

// ===== Constructors =====

Page::Page()
{
    // Create one default layer
    vectorLayers.push_back(std::make_unique<VectorLayer>("Layer 1"));
}

Page::Page(const QSizeF& pageSize)
    : size(pageSize)
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
    
    vectorLayers.erase(vectorLayers.begin() + index);
    
    // Adjust active layer index
    if (activeLayerIndex >= static_cast<int>(vectorLayers.size())) {
        activeLayerIndex = static_cast<int>(vectorLayers.size()) - 1;
    }
    
    modified = true;
    return true;
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
    return true;
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
        objects.push_back(std::move(obj));
        modified = true;
    }
}

bool Page::removeObject(const QString& id)
{
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i]->id == id) {
            objects.erase(objects.begin() + i);
            modified = true;
            return true;
        }
    }
    return false;
}

InsertedObject* Page::objectAtPoint(const QPointF& pt)
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

// ===== Rendering =====

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

// ===== Serialization =====

QJsonObject Page::toJson() const
{
    QJsonObject obj;
    
    // Identity
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
    page->pageIndex = obj["pageIndex"].toInt(0);
    page->size = QSizeF(obj["width"].toDouble(800), obj["height"].toDouble(600));
    
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
    
    page->modified = false;
    return page;
}

int Page::loadImages(const QString& basePath)
{
    int loaded = 0;
    for (auto& obj : objects) {
        if (obj->type() == "image") {
            ImageObject* img = static_cast<ImageObject*>(obj.get());
            if (img->loadImage(basePath)) {
                loaded++;
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
    
    // Clear objects
    objects.clear();
    
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
