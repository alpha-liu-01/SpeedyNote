// ============================================================================
// ImageObject - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
// ============================================================================

#include "ImageObject.h"
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QBuffer>
#include <QPainter>
#include <QSaveFile>
#include <QtMath>

namespace {

QByteArray normalizedImageFormat(QByteArray format, bool hasOriginalBytes)
{
    format = format.trimmed().toLower();
    if (format == "jpeg") {
        return QByteArrayLiteral("jpg");
    }
    if (format == "png" || format == "jpg" || format == "bmp"
        || format == "gif" || format == "webp") {
        return format;
    }
    return hasOriginalBytes ? QByteArrayLiteral("img") : QByteArrayLiteral("png");
}

QString imageAssetFilename(const QString& hash, const QByteArray& format)
{
    return hash.left(16) + "." + QString::fromLatin1(format);
}

}  // namespace

void ImageObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible) {
        return;
    }

    // Calculate the target rectangle at the given zoom level
    QRectF targetRect(
        position.x() * zoom,
        position.y() * zoom,
        size.width() * zoom,
        size.height() * zoom
    );

    if (cachedPixmap.isNull()) {
        // The asset failed to load (file missing / unreadable). Draw a visible
        // "missing image" placeholder instead of nothing, so the user can see
        // which image is broken. The object and its imagePath are preserved,
        // so the reference can still be re-linked if the file reappears.
        if (imagePath.isEmpty() || targetRect.isEmpty()) {
            return;
        }
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QColor border(180, 60, 60);
        painter.fillRect(targetRect, QColor(245, 230, 230));
        QPen pen(border);
        pen.setWidthF(qMax(1.0, zoom));
        painter.setPen(pen);
        painter.drawRect(targetRect);
        // Diagonal cross signals a broken/missing image.
        painter.drawLine(targetRect.topLeft(), targetRect.bottomRight());
        painter.drawLine(targetRect.topRight(), targetRect.bottomLeft());
        if (targetRect.width() > 60.0 && targetRect.height() > 24.0) {
            painter.drawText(targetRect, Qt::AlignCenter, QStringLiteral("Missing image"));
        }
        painter.restore();
        return;
    }

    const QPixmap* renderPixmap = &cachedPixmap;

    // Integer insertion scaling intentionally preserves the full-resolution
    // source for export. Avoid paying the full 4K -> display-size smooth scale
    // on every paint by caching the current effective device-pixel size.
    const QTransform deviceTransform = painter.deviceTransform();
    const QPointF mappedOrigin = deviceTransform.map(targetRect.topLeft());
    const QPointF mappedX = deviceTransform.map(targetRect.topRight());
    const QPointF mappedY = deviceTransform.map(targetRect.bottomLeft());
    const QSize desiredPixels(
        qMax(1, qRound(QLineF(mappedOrigin, mappedX).length())),
        qMax(1, qRound(QLineF(mappedOrigin, mappedY).length())));
    constexpr int MAX_DISPLAY_CACHE_DIMENSION = 4096;
    if (desiredPixels.width() < cachedPixmap.width()
        && desiredPixels.height() < cachedPixmap.height()
        && desiredPixels.width() <= MAX_DISPLAY_CACHE_DIMENSION
        && desiredPixels.height() <= MAX_DISPLAY_CACHE_DIMENSION) {
        if (m_displayPixmap.isNull() || m_displayPixmapPixelSize != desiredPixels) {
            m_displayPixmap = cachedPixmap.scaled(
                desiredPixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_displayPixmapPixelSize = desiredPixels;
        }
        if (!m_displayPixmap.isNull()) {
            renderPixmap = &m_displayPixmap;
        }
    }

    QRectF sourceRect(renderPixmap->rect());

    if (rotation != 0.0) {
        painter.save();
        QPointF centerPoint = targetRect.center();
        painter.translate(centerPoint);
        painter.rotate(rotation);
        painter.translate(-centerPoint);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(targetRect, *renderPixmap, sourceRect);
        painter.restore();
    } else {
        bool hadSmooth = painter.testRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(targetRect, *renderPixmap, sourceRect);
        if (!hadSmooth) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        }
    }
}

QJsonObject ImageObject::toJson() const
{
    return toJsonImpl(true);
}

QJsonObject ImageObject::toJsonWithoutRecoveryData() const
{
    return toJsonImpl(false);
}

QJsonObject ImageObject::toJsonImpl(bool includeRecoveryData) const
{
    // Start with base class serialization
    QJsonObject obj = InsertedObject::toJson();
    
    // Add image-specific properties
    obj["imagePath"] = imagePath;
    obj["imageHash"] = imageHash;
    obj["maintainAspectRatio"] = maintainAspectRatio;
    obj["originalAspectRatio"] = originalAspectRatio;
    
    // BF.7 / data-safety: embed the image data as base64 whenever the asset is
    // not confirmed on disk. This covers unsaved documents (imagePath empty, for
    // undo/redo) AND the case where imagePath is set but the asset file is not
    // known to exist - so a later orphan cleanup or lost file can never turn the
    // reference into permanent data loss.
    if (includeRecoveryData && !cachedPixmap.isNull()
        && (imagePath.isEmpty() || !m_assetPersisted)) {
        QByteArray imageData = m_encodedAssetData;
        QByteArray format = m_assetFormat;
        if (imageData.isEmpty()) {
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            cachedPixmap.save(&buffer, "PNG");
            format = QByteArrayLiteral("png");
        }
        obj["embeddedImageData"] = QString::fromLatin1(imageData.toBase64());
        obj["embeddedImageFormat"] = QString::fromLatin1(format);
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
        if (pixmap.loadFromData(imageData)) {
            cachedPixmap = pixmap;
            m_encodedAssetData = imageData;
            m_assetFormat = normalizedImageFormat(
                obj["embeddedImageFormat"].toString("png").toLatin1(), true);
            clearDisplayCache();
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
    clearDisplayCache();
    m_encodedAssetData.clear();

    // The file we just read exists, so the asset is confirmed persisted.
    m_assetPersisted = true;

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
    clearDisplayCache();
    m_encodedAssetData.clear();
    m_assetFormat = QByteArrayLiteral("png");
    imageHash.clear();
    imagePath.clear();

    // A freshly supplied pixmap (clipboard/memory) is not yet on disk.
    m_assetPersisted = false;

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

void ImageObject::setSourceImage(const QImage& image,
                                 const QByteArray& encodedData,
                                 const QByteArray& encodedFormat)
{
    setPixmap(QPixmap::fromImage(image));
    if (cachedPixmap.isNull()) {
        return;
    }

    m_encodedAssetData = encodedData;
    m_assetFormat = normalizedImageFormat(encodedFormat, !encodedData.isEmpty());

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!encodedData.isEmpty()) {
        hasher.addData(encodedData);
    } else {
        // Clipboard images do not have source bytes. Hash a canonical pixel
        // representation quickly on the GUI thread; PNG compression remains
        // entirely in the background writer.
        const QImage canonical = image.convertToFormat(QImage::Format_RGBA8888);
        const qint32 width = canonical.width();
        const qint32 height = canonical.height();
        hasher.addData(QByteArray::fromRawData(
            reinterpret_cast<const char*>(&width), sizeof(width)));
        hasher.addData(QByteArray::fromRawData(
            reinterpret_cast<const char*>(&height), sizeof(height)));
        hasher.addData(QByteArray::fromRawData(
            reinterpret_cast<const char*>(canonical.constBits()),
            static_cast<int>(canonical.sizeInBytes())));
    }
    imageHash = QString::fromLatin1(hasher.result().toHex());
    imagePath = imageAssetFilename(imageHash, m_assetFormat);
}

void ImageObject::markAssetPersisted()
{
    m_assetPersisted = true;
    m_encodedAssetData.clear();
    m_encodedAssetData.squeeze();
}

void ImageObject::clearDisplayCache() const
{
    m_displayPixmap = QPixmap();
    m_displayPixmapPixelSize = QSize();
}

void ImageObject::calculateHash()
{
    if (cachedPixmap.isNull()) {
        imageHash.clear();
        return;
    }
    
    QByteArray bytes = m_encodedAssetData;
    if (bytes.isEmpty()) {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        cachedPixmap.save(&buffer, "PNG");
        m_assetFormat = QByteArrayLiteral("png");
        m_encodedAssetData = bytes;
    }

    // Calculate SHA-256 hash over the exact bytes that will be written.
    QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    imageHash = QString::fromLatin1(hash.toHex());
    imagePath = imageAssetFilename(imageHash, m_assetFormat);
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
    
    if (imagePath.isEmpty()) {
        imagePath = imageAssetFilename(imageHash, m_assetFormat);
    }
    QString filename = imagePath;
    QString assetsPath = bundlePath + "/assets/images";
    QString fullFilePath = assetsPath + "/" + filename;
    
    // Check if file already exists (deduplication)
    if (QFile::exists(fullFilePath)) {
        // Image already saved, just update path
        imagePath = filename;
        markAssetPersisted();
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "ImageObject: reusing existing asset" << filename;
#endif
        return true;
    }

    // A loaded original-format asset may have disappeared after its transient
    // source bytes were released. Rebuild it as PNG from the retained pixels
    // under a new matching content hash instead of writing PNG bytes into a
    // misleading .jpg/.webp/etc. filename.
    if (m_encodedAssetData.isEmpty()
        && QFileInfo(filename).suffix().compare("png", Qt::CaseInsensitive) != 0) {
        QByteArray recoveryPng;
        QBuffer recoveryBuffer(&recoveryPng);
        recoveryBuffer.open(QIODevice::WriteOnly);
        if (!cachedPixmap.save(&recoveryBuffer, "PNG")) {
            return false;
        }
        m_encodedAssetData = recoveryPng;
        m_assetFormat = QByteArrayLiteral("png");
        imageHash = QString::fromLatin1(
            QCryptographicHash::hash(recoveryPng, QCryptographicHash::Sha256).toHex());
        imagePath = imageAssetFilename(imageHash, m_assetFormat);
        filename = imagePath;
        fullFilePath = assetsPath + "/" + filename;
        if (QFile::exists(fullFilePath)) {
            markAssetPersisted();
            return true;
        }
    }
    
    // Ensure directory exists
    if (!QDir().mkpath(assetsPath)) {
        qWarning() << "ImageObject::saveToAssets: cannot create directory" << assetsPath;
        return false;
    }
    
    QByteArray bytes = m_encodedAssetData;
    if (bytes.isEmpty()) {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!cachedPixmap.save(&buffer, "PNG")) {
            qWarning() << "ImageObject::saveToAssets: failed to encode image";
            return false;
        }
        m_assetFormat = QByteArrayLiteral("png");
    }

    // Atomically publish the exact bytes used for hashing. This avoids the
    // previous second full-resolution PNG encode and prevents partial assets.
    QSaveFile output(fullFilePath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size()
        || !output.commit()) {
        qWarning() << "ImageObject::saveToAssets: failed to save" << fullFilePath;
        return false;
    }
    
    // Update imagePath to just the filename
    imagePath = filename;
    markAssetPersisted();
    
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
