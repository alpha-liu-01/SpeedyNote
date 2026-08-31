#include "LinkObject.h"
#include <QPainter>
#include <QJsonArray>

// Note: Static icon cache moved to function-local statics in ensureIconLoaded()
// to avoid "Must construct QGuiApplication before QPixmap" crash at startup.

// ============================================================================
// LinkSlot Serialization
// ============================================================================

QJsonObject LinkSlot::toJson() const
{
    QJsonObject obj;
    
    switch (type) {
        case Type::Empty:
            obj["type"] = "empty";
            break;
        case Type::Position:
            obj["type"] = "position";
            obj["x"] = targetPosition.x();
            obj["y"] = targetPosition.y();
            if (isEdgelessTarget) {
                // Edgeless mode: store tile coordinates
                obj["edgeless"] = true;
                obj["tileX"] = edgelessTileX;
                obj["tileY"] = edgelessTileY;
            } else {
                // Paged mode: store page UUID
                obj["pageUuid"] = targetPageUuid;
            }
            // Omitted when absent so a coordinate-only link round-trips byte for
            // byte, and so an older build reading this file sees exactly what it
            // wrote before.
            if (!targetObjectId.isEmpty()) {
                obj["targetObjectId"] = targetObjectId;
            }
            if (targetSlotIndex >= 0) {
                obj["targetSlotIndex"] = targetSlotIndex;
            }
            break;
        case Type::Url:
            obj["type"] = "url";
            obj["url"] = url;
            break;
        case Type::Markdown:
            obj["type"] = "markdown";
            obj["noteId"] = markdownNoteId;
            break;
    }
    
    return obj;
}

LinkSlot LinkSlot::fromJson(const QJsonObject& obj)
{
    LinkSlot slot;
    QString typeStr = obj["type"].toString();
    
    if (typeStr == "position") {
        slot.type = Type::Position;
        slot.targetPosition = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
        if (obj["edgeless"].toBool()) {
            // Edgeless mode: load tile coordinates
            slot.isEdgelessTarget = true;
            slot.edgelessTileX = obj["tileX"].toInt();
            slot.edgelessTileY = obj["tileY"].toInt();
        } else {
            // Paged mode: load page UUID
            slot.isEdgelessTarget = false;
            slot.targetPageUuid = obj["pageUuid"].toString();
        }
        // Absent in every file written before pairing existed; those links stay
        // coordinate-only, which is what they always were. The slot index must
        // default to -1 rather than toInt()'s 0, which would read as "slot 1".
        slot.targetObjectId = obj["targetObjectId"].toString();
        slot.targetSlotIndex = obj["targetSlotIndex"].toInt(-1);
    } else if (typeStr == "url") {
        slot.type = Type::Url;
        slot.url = obj["url"].toString();
    } else if (typeStr == "markdown") {
        slot.type = Type::Markdown;
        slot.markdownNoteId = obj["noteId"].toString();
    } else {
        slot.type = Type::Empty;
    }
    
    return slot;
}

// ============================================================================
// LinkObject Implementation
// ============================================================================

LinkObject::LinkObject()
{
    // Default size is icon size
    size = QSizeF(ICON_SIZE, ICON_SIZE);
}

void LinkObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible) return;
    
    if (!region.isEmpty()) {
        renderRegion(painter, zoom);
    }
    
    if (!shouldShowIcon()) {
        return;
    }
    
    ensureIconLoaded();
    
    // Get device pixel ratio for high DPI support
    qreal dpr = 1.0;
    if (painter.device()) {
        dpr = painter.device()->devicePixelRatioF();
    }
    
    // scaledSize is in logical pixels, multiply by DPR for physical pixels
    qreal logicalSize = ICON_SIZE * zoom;
    qreal physicalSize = logicalSize * dpr;
    
    QPixmap icon = tintedIcon(iconColor, physicalSize);
    icon.setDevicePixelRatio(dpr);  // Tell Qt this pixmap is at high DPI
    
    const QPointF iconTopLeft = iconRect().topLeft();
    QPointF drawPos(iconTopLeft.x() * zoom, iconTopLeft.y() * zoom);
    painter.drawPixmap(drawPos.toPoint(), icon);
}

void LinkObject::renderRegion(QPainter& painter, qreal zoom) const
{
    if (region.style == HighlightRegion::Style::None || !region.color.isValid()) {
        return;
    }
    
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(region.color);
    
    for (const QRectF& localRect : region.rects) {
        if (localRect.width() < 0.1 || localRect.height() < 0.1) continue;
        
        // Object-local -> page/tile space, then to the painter's scaled space.
        const QRectF pageRect = localRect.translated(position);
        const QRectF r(pageRect.x() * zoom, pageRect.y() * zoom,
                       pageRect.width() * zoom, pageRect.height() * zoom);
        
        switch (region.style) {
            case HighlightRegion::Style::Cover:
                // Marker covering the whole line height.
                painter.drawRect(r);
                break;
            
            case HighlightRegion::Style::Underline: {
                // Derived in unzoomed document units, then scaled once.
                const qreal t = HighlightRegion::underlineThickness(localRect) * zoom;
                painter.drawRect(QRectF(r.left(), r.bottom() - t, r.width(), t));
                break;
            }
            
            case HighlightRegion::Style::DottedUnderline: {
                const qreal t = HighlightRegion::underlineThickness(localRect) * zoom;
                const qreal step = t * HighlightRegion::DOT_SPACING_FACTOR;
                if (step <= 0.0) break;
                
                const qreal y = r.bottom() - t * qreal(0.5);
                const qreal firstX = r.left() + t * qreal(0.5);
                const qreal lastX = r.right() - t * qreal(0.5);
                if (lastX < firstX) break;
                
                const bool wasAntialiased =
                    painter.renderHints().testFlag(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::Antialiasing, true);
                for (qreal x = firstX; x <= lastX; x += step) {
                    painter.drawEllipse(QPointF(x, y), t * 0.5, t * 0.5);
                }
                painter.setRenderHint(QPainter::Antialiasing, wasAntialiased);
                break;
            }
            
            case HighlightRegion::Style::None:
                break;
        }
    }
    
    painter.restore();
}

void LinkObject::ensureIconLoaded() const
{
    // Function-local statics are initialized on first call (after QApplication exists)
    // This avoids the "Must construct QGuiApplication before QPixmap" crash
}

const QPixmap& LinkObject::iconPixmap()
{
    // Function-local static - initialized on first call, thread-safe in C++11+
    // Using 256x256 PNG for high DPI support (always downscaling = crisp)
    static QPixmap pixmap(":/resources/icons/link_quote.png");
    return pixmap;
}

QPixmap LinkObject::tintedIcon(const QColor& color, qreal size) const
{
    // Check render cache - avoid recreating tinted icon every frame
    // Allow small size variation (1px) to avoid thrashing during smooth zoom
    if (!m_cachedTintedIcon.isNull() && 
        m_cachedColor == color && 
        qAbs(m_cachedSize - size) < 1.0) {
        return m_cachedTintedIcon;
    }
    
    const QPixmap& baseIcon = iconPixmap();
    
    // Scale icon
    QPixmap scaled = baseIcon.scaled(
        size, size, 
        Qt::KeepAspectRatio, 
        Qt::SmoothTransformation
    );
    
    // Apply color tint - preserve original alpha from icon, use RGB from color
    // No additional alpha blending - color.alpha() controls overall opacity
    QImage img = scaled.toImage();
    for (int y = 0; y < img.height(); y++) {
        for (int x = 0; x < img.width(); x++) {
            QColor pixel = img.pixelColor(x, y);
            if (pixel.alpha() > 0) {
                // Use tint color RGB, preserve icon's alpha shape
                int newAlpha = (color.alpha() == 255) 
                    ? pixel.alpha()  // Full opacity: preserve icon alpha
                    : (pixel.alpha() * color.alpha() / 255);  // Blend alphas
                pixel.setRed(color.red());
                pixel.setGreen(color.green());
                pixel.setBlue(color.blue());
                pixel.setAlpha(newAlpha);
                img.setPixelColor(x, y, pixel);
            }
        }
    }
    
    // Update cache
    m_cachedTintedIcon = QPixmap::fromImage(img);
    m_cachedColor = color;
    m_cachedSize = size;
    
    return m_cachedTintedIcon;
}

bool LinkObject::containsPoint(const QPointF& pt) const
{
    // The highlight body is the primary handle once a region exists; the badge
    // stays hit-testable so an annotation is never unreachable, and an empty
    // region reduces this to the historical 24x24 icon test.
    if (!region.isEmpty() && region.containsLocalPoint(pt - position)) {
        return true;
    }
    return shouldShowIcon() && iconRect().contains(pt);
}

void LinkObject::setRegionFromPageRects(const QVector<QRectF>& pageRects)
{
    region.rects.clear();

    if (pageRects.isEmpty()) {
        region.sourceRange = HighlightRegion::SourceRange();
        size = QSizeF(ICON_SIZE, ICON_SIZE);
        return;
    }

    QRectF bounds;
    for (const QRectF& r : pageRects) {
        bounds = bounds.isNull() ? r : bounds.united(r);
    }

    // Rects are stored object-local so a later move is a single position change.
    const QPointF origin = bounds.topLeft();
    region.rects.reserve(pageRects.size());
    for (const QRectF& r : pageRects) {
        region.rects.append(r.translated(-origin));
    }

    position = origin;
    size = bounds.size();
}

QVector<QRectF> LinkObject::regionRectsInPageSpace() const
{
    QVector<QRectF> out;
    out.reserve(region.rects.size());
    for (const QRectF& r : region.rects) {
        out.append(r.translated(position));
    }
    return out;
}

QRectF LinkObject::iconRect() const
{
    // Empty region: position is the icon anchor (pre-region behaviour).
    // Region present: position anchors the highlight, so the badge goes in the
    // margin beside it.
    const QPointF topLeft = region.isEmpty()
                                ? position
                                : position - QPointF(ICON_SIZE + ICON_GAP, 0.0);
    return QRectF(topLeft, QSizeF(ICON_SIZE, ICON_SIZE));
}

bool LinkObject::shouldShowIcon() const
{
    // An empty-region annotation has no other handle, so its badge is
    // unconditional -- which is also why no icon disappears from a document
    // saved before regions existed.
    if (region.isEmpty()) {
        return true;
    }
    return filledSlotCount() > 0 || descriptionUserEdited;
}

QJsonObject LinkObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    
    obj["description"] = description;
    obj["iconColor"] = iconColor.name(QColor::HexArgb);
    if (descriptionUserEdited) {
        obj["descriptionUserEdited"] = true;
    }
    
    QJsonArray slotsArray;
    for (int i = 0; i < SLOT_COUNT; i++) {
        slotsArray.append(linkSlots[i].toJson());
    }
    obj["slots"] = slotsArray;
    
    // Omitted when empty so a standalone link icon serializes exactly as it did
    // before regions existed.
    if (!region.isEmpty()) {
        obj["region"] = region.toJson();
    }
    
    return obj;
}

void LinkObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    
    description = obj["description"].toString();
    iconColor = QColor(obj["iconColor"].toString());
    if (!iconColor.isValid()) {
        iconColor = QColor(100, 100, 100, 180);
    }
    descriptionUserEdited = obj["descriptionUserEdited"].toBool(false);
    
    QJsonArray slotsArray = obj["slots"].toArray();
    for (int i = 0; i < SLOT_COUNT && i < slotsArray.size(); i++) {
        linkSlots[i] = LinkSlot::fromJson(slotsArray[i].toObject());
    }
    
    if (obj.contains("region")) {
        region = HighlightRegion::fromJson(obj["region"].toObject());
    } else {
        region = HighlightRegion();
    }
}

int LinkObject::filledSlotCount() const
{
    int count = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (!linkSlots[i].isEmpty()) count++;
    }
    return count;
}

bool LinkObject::hasEmptySlot() const
{
    return firstEmptySlotIndex() >= 0;
}

int LinkObject::firstEmptySlotIndex() const
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (linkSlots[i].isEmpty()) return i;
    }
    return -1;
}

std::unique_ptr<LinkObject> LinkObject::cloneWithBackLink(const QString& sourcePageUuid) const
{
    auto copy = std::make_unique<LinkObject>();
    copy->description = description;
    copy->iconColor = iconColor;
    copy->descriptionUserEdited = descriptionUserEdited;
    copy->region = region;
    copy->size = size;
    // Note: position will be set by caller
    
    // Auto-fill slot 0 with back-link to original position (paged mode)
    copy->linkSlots[0].type = LinkSlot::Type::Position;
    copy->linkSlots[0].isEdgelessTarget = false;
    copy->linkSlots[0].targetPageUuid = sourcePageUuid;
    copy->linkSlots[0].targetPosition = position;
    
    return copy;
}

std::unique_ptr<LinkObject> LinkObject::cloneWithBackLinkEdgeless(int tileX, int tileY, const QPointF& docPosition) const
{
    auto copy = std::make_unique<LinkObject>();
    copy->description = description;
    copy->iconColor = iconColor;
    copy->descriptionUserEdited = descriptionUserEdited;
    copy->region = region;
    copy->size = size;
    // Note: position will be set by caller
    
    // Auto-fill slot 0 with back-link to original position (edgeless mode)
    copy->linkSlots[0].type = LinkSlot::Type::Position;
    copy->linkSlots[0].isEdgelessTarget = true;
    copy->linkSlots[0].edgelessTileX = tileX;
    copy->linkSlots[0].edgelessTileY = tileY;
    copy->linkSlots[0].targetPosition = docPosition;  // Document coordinates
    
    return copy;
}
