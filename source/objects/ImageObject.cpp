// ============================================================================
// ImageObject - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.5)
// ============================================================================

#include "ImageObject.h"
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QBuffer>

void ImageObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible || cachedPixmap.isNull()) {
        return;
    }
    
    // Calculate the target rectangle at the given zoom level
    QRectF targetRect(
        position.x() * zoom,
        position.y() * zoom,
        size.width() * zoom,
        size.height() * zoom
    );
    
    // Apply rotation if needed
    if (rotation != 0.0) {
        painter.save();
        QPointF centerPoint = targetRect.center();
        painter.translate(centerPoint);
        painter.rotate(rotation);
        painter.translate(-centerPoint);
        painter.drawPixmap(targetRect.toRect(), cachedPixmap);
        painter.restore();
    } else {
        // Simple case: no rotation
        painter.drawPixmap(targetRect.toRect(), cachedPixmap);
    }
}

QJsonObject ImageObject::toJson() const
{
    // Start with base class serialization
    QJsonObject obj = InsertedObject::toJson();
    
    // Add image-specific properties
    obj["imagePath"] = imagePath;
    obj["imageHash"] = imageHash;
    obj["maintainAspectRatio"] = maintainAspectRatio;
    obj["originalAspectRatio"] = originalAspectRatio;
    
    return obj;
}

void ImageObject::loadFromJson(const QJsonObject& obj)
{
    // Load base class properties
    InsertedObject::loadFromJson(obj);
    
    // Load image-specific properties
    imagePath = obj["imagePath"].toString();
    imageHash = obj["imageHash"].toString();
    maintainAspectRatio = obj["maintainAspectRatio"].toBool(true);
    originalAspectRatio = obj["originalAspectRatio"].toDouble(1.0);
    
    // Note: cachedPixmap is NOT loaded here
    // Caller should call loadImage() with the appropriate base path
}

bool ImageObject::loadImage(const QString& basePath)
{
    if (imagePath.isEmpty()) {
        return false;
    }
    
    QString path = fullPath(basePath);
    
    // Try to load the image
    QImage image(path);
    if (image.isNull()) {
        return false;
    }
    
    // Convert to pixmap and cache
    cachedPixmap = QPixmap::fromImage(image);
    
    // Update aspect ratio if this is the first load
    if (originalAspectRatio <= 0.0 && !cachedPixmap.isNull() && cachedPixmap.height() > 0) {
        originalAspectRatio = static_cast<qreal>(cachedPixmap.width()) / 
                              static_cast<qreal>(cachedPixmap.height());
    }
    
    // Update size if not set
    if (size.isEmpty() && !cachedPixmap.isNull()) {
        size = cachedPixmap.size();
    }
    
    return true;
}

void ImageObject::setPixmap(const QPixmap& pixmap)
{
    cachedPixmap = pixmap;
    
    if (!cachedPixmap.isNull()) {
        // Update aspect ratio (guard against height=0)
        if (cachedPixmap.height() > 0) {
            originalAspectRatio = static_cast<qreal>(cachedPixmap.width()) / 
                                  static_cast<qreal>(cachedPixmap.height());
        }
        
        // Update size if not set
        if (size.isEmpty()) {
            size = cachedPixmap.size();
        }
    }
}

void ImageObject::calculateHash()
{
    if (cachedPixmap.isNull()) {
        imageHash.clear();
        return;
    }
    
    // Convert pixmap to PNG bytes for consistent hashing
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    cachedPixmap.save(&buffer, "PNG");
    buffer.close();
    
    // Calculate SHA-256 hash
    QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    imageHash = QString::fromLatin1(hash.toHex());
}

void ImageObject::resizeToWidth(qreal newWidth)
{
    if (maintainAspectRatio && originalAspectRatio > 0.0) {
        size.setWidth(newWidth);
        size.setHeight(newWidth / originalAspectRatio);
    } else {
        size.setWidth(newWidth);
    }
}

void ImageObject::resizeToHeight(qreal newHeight)
{
    if (maintainAspectRatio && originalAspectRatio > 0.0) {
        size.setHeight(newHeight);
        size.setWidth(newHeight * originalAspectRatio);
    } else {
        size.setHeight(newHeight);
    }
}

QString ImageObject::fullPath(const QString& basePath) const
{
    if (imagePath.isEmpty()) {
        return QString();
    }
    
    // Check if path is already absolute
    QFileInfo info(imagePath);
    if (info.isAbsolute()) {
        return imagePath;
    }
    
    // Resolve relative to base path
    if (basePath.isEmpty()) {
        return imagePath;
    }
    
    return QDir(basePath).filePath(imagePath);
}
