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
            obj["pageUuid"] = targetPageUuid;
            obj["x"] = targetPosition.x();
            obj["y"] = targetPosition.y();
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
        slot.targetPageUuid = obj["pageUuid"].toString();
        slot.targetPosition = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
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
    
    QPointF drawPos(position.x() * zoom, position.y() * zoom);
    painter.drawPixmap(drawPos.toPoint(), icon);
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
    // Use icon bounds for hit testing
    QRectF iconRect(position, QSizeF(ICON_SIZE, ICON_SIZE));
    return iconRect.contains(pt);
}

QJsonObject LinkObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    
    obj["description"] = description;
    obj["iconColor"] = iconColor.name(QColor::HexArgb);
    
    QJsonArray slotsArray;
    for (int i = 0; i < SLOT_COUNT; i++) {
        slotsArray.append(linkSlots[i].toJson());
    }
    obj["slots"] = slotsArray;
    
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
    
    QJsonArray slotsArray = obj["slots"].toArray();
    for (int i = 0; i < SLOT_COUNT && i < slotsArray.size(); i++) {
        linkSlots[i] = LinkSlot::fromJson(slotsArray[i].toObject());
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
    // Note: position will be set by caller
    
    // Auto-fill slot 0 with back-link to original position
    copy->linkSlots[0].type = LinkSlot::Type::Position;
    copy->linkSlots[0].targetPageUuid = sourcePageUuid;
    copy->linkSlots[0].targetPosition = position;
    
    return copy;
}

