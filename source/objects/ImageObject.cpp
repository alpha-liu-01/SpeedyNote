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
    
    // BF.7: If imagePath is empty but we have a cached pixmap (unsaved document),
    // embed the image data as base64 so undo/redo works correctly
    if (imagePath.isEmpty() && !cachedPixmap.isNull()) {
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        cachedPixmap.save(&buffer, "PNG");
        obj["embeddedImageData"] = QString::fromLatin1(imageData.toBase64());
    }
    
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
    
    // BF.7: Check for embedded image data (unsaved document case)
    // This allows undo/redo to work even when the document hasn't been saved yet
    if (obj.contains("embeddedImageData")) {
        QString base64Data = obj["embeddedImageData"].toString();
        QByteArray imageData = QByteArray::fromBase64(base64Data.toLatin1());
        QPixmap pixmap;
        if (pixmap.loadFromData(imageData, "PNG")) {
            cachedPixmap = pixmap;
            // Update size if not already set
            if (size.isEmpty() && !cachedPixmap.isNull()) {
                size = cachedPixmap.size();
            }
        }
    }
    // Note: If no embedded data, caller should call loadImage() with the appropriate base path
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
    
    // Check if path is already absolute (legacy support)
    QFileInfo info(imagePath);
    if (info.isAbsolute()) {
        return imagePath;
    }
    
    // Resolve relative to base path
    if (basePath.isEmpty()) {
        return imagePath;
    }
    
    // Phase O1.6: Resolve against assets/images/ subdirectory
    // New format stores just the filename (e.g., "a1b2c3d4.png")
    // Full path becomes: bundlePath/assets/images/filename
    return basePath + "/assets/images/" + imagePath;
}

bool ImageObject::saveToAssets(const QString& bundlePath)
{
    if (bundlePath.isEmpty()) {
        qWarning() << "ImageObject::saveToAssets: bundlePath is empty";
        return false;
    }
    
    if (cachedPixmap.isNull()) {
        qWarning() << "ImageObject::saveToAssets: no image loaded";
        return false;
    }
    
    // Calculate hash if not already set
    if (imageHash.isEmpty()) {
        calculateHash();
    }
    
    if (imageHash.isEmpty()) {
        qWarning() << "ImageObject::saveToAssets: failed to calculate hash";
        return false;
    }
    
    // Use first 16 characters of hash as filename
    QString filename = imageHash.left(16) + ".png";
    QString assetsPath = bundlePath + "/assets/images";
    QString fullFilePath = assetsPath + "/" + filename;
    
    // Check if file already exists (deduplication)
    if (QFile::exists(fullFilePath)) {
        // Image already saved, just update path
        imagePath = filename;
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "ImageObject: reusing existing asset" << filename;
#endif
        return true;
    }
    
    // Ensure directory exists
    if (!QDir().mkpath(assetsPath)) {
        qWarning() << "ImageObject::saveToAssets: cannot create directory" << assetsPath;
        return false;
    }
    
    // Save image to assets folder
    if (!cachedPixmap.save(fullFilePath, "PNG")) {
        qWarning() << "ImageObject::saveToAssets: failed to save" << fullFilePath;
        return false;
    }
    
    // Update imagePath to just the filename
    imagePath = filename;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "ImageObject: saved to assets" << filename;
#endif
    
    return true;
}

// ===== Asset Management Overrides (Phase O2.C) =====

bool ImageObject::loadAssets(const QString& bundlePath)
{
    // Delegate to existing loadImage() method
    return loadImage(bundlePath);
}

bool ImageObject::saveAssets(const QString& bundlePath)
{
    // Delegate to existing saveToAssets() method
    return saveToAssets(bundlePath);
}
