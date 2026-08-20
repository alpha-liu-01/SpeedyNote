#pragma once

// ============================================================================
// ImageObject - An image inserted onto a page
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
// 
// ImageObject represents an image that has been inserted onto a page.
// It stores the path to the image file and caches the pixmap for rendering.
// ============================================================================

#include "InsertedObject.h"
#include <QByteArray>
#include <QPixmap>
#include <QImage>

/**
 * @brief An image object that can be inserted onto a page.
 * 
 * Stores the path to an image file (relative to the notebook directory)
 * and caches the loaded pixmap for efficient rendering.
 */
class ImageObject : public InsertedObject {
public:
    // ===== Image-specific Properties =====
    QString imagePath;              ///< Path to image file (relative to notebook)
    QString imageHash;              ///< SHA-256 hash for deduplication (optional)
    bool maintainAspectRatio = true; ///< If true, preserve aspect ratio on resize
    qreal originalAspectRatio = 1.0; ///< Original width/height ratio
    
    /**
     * @brief Default constructor.
     */
    ImageObject() = default;
    
    /**
     * @brief Constructor with image path.
     * @param path Path to the image file.
     */
    explicit ImageObject(const QString& path) : imagePath(path) {}
    
    // ===== InsertedObject Interface =====
    
    /**
     * @brief Render this image.
     * @param painter The QPainter to render to.
     * @param zoom Current zoom level (1.0 = 100%).
     */
    void render(QPainter& painter, qreal zoom) const override;
    
    /**
     * @brief Get the type identifier.
     * @return "image"
     */
    QString type() const override { return QStringLiteral("image"); }
    
    /**
     * @brief Serialize to JSON.
     * @return JSON object containing image data.
     */
    QJsonObject toJson() const override;

    /**
     * @brief Serialize object metadata without an embedded pixel recovery copy.
     *
     * Object undo actions retain original encoded bytes or shared pixels
     * separately, so fresh-image insertion does not need to synchronously
     * PNG-encode a multi-megapixel image merely to create its undo record.
     */
    QJsonObject toJsonWithoutRecoveryData() const;
    
    /**
     * @brief Deserialize from JSON.
     * @param obj JSON object containing image data.
     */
    void loadFromJson(const QJsonObject& obj) override;
    
    // ===== Asset Management Overrides (Phase O2.C) =====
    
    /**
     * @brief Load image from the bundle's assets folder.
     * @param bundlePath Path to the .snb bundle directory.
     * @return True if image loaded successfully.
     * 
     * Delegates to loadImage(bundlePath).
     */
    bool loadAssets(const QString& bundlePath) override;
    
    /**
     * @brief Save image to the bundle's assets folder.
     * @param bundlePath Path to the .snb bundle directory.
     * @return True if saved successfully.
     * 
     * Delegates to saveToAssets(bundlePath).
     */
    bool saveAssets(const QString& bundlePath) override;
    
    /**
     * @brief Check if the image pixmap is loaded and ready to render.
     * @return True if cachedPixmap is valid.
     */
    bool isAssetLoaded() const override { return !cachedPixmap.isNull(); }
    
    // ===== Image-specific Methods =====
    
    /**
     * @brief Load the image from disk.
     * @param basePath Base directory for resolving relative paths.
     * @return True if image loaded successfully.
     * 
     * Call this after creating/loading to populate cachedPixmap.
     * If imagePath is absolute, basePath is ignored.
     */
    bool loadImage(const QString& basePath = QString());
    
    /**
     * @brief Check if the image is loaded.
     * @return True if cachedPixmap is valid.
     */
    bool isLoaded() const { return !cachedPixmap.isNull(); }
    
    /**
     * @brief Clear the cached pixmap to free memory.
     */
    void unloadImage() {
        cachedPixmap = QPixmap();
        clearDisplayCache();
    }
    
    /**
     * @brief Get the cached pixmap.
     * @return The cached pixmap (may be null if not loaded).
     */
    const QPixmap& pixmap() const { return cachedPixmap; }
    
    /**
     * @brief Set the pixmap directly (for images created from clipboard/memory).
     * @param pixmap The pixmap to use.
     * 
     * This sets the cached pixmap and updates size/aspect ratio.
     * imagePath will be empty until the image is saved to disk.
     */
    void setPixmap(const QPixmap& pixmap);

    /**
     * @brief Set a freshly decoded source image and optional original bytes.
     *
     * File imports retain their original encoded payload and format so asset
     * persistence can copy it without converting to PNG. Clipboard images pass
     * no encoded payload and are encoded once by the background asset writer.
     */
    void setSourceImage(const QImage& image,
                        const QByteArray& encodedData = QByteArray(),
                        const QByteArray& encodedFormat = QByteArray());

    const QByteArray& encodedAssetData() const { return m_encodedAssetData; }
    QByteArray assetFormat() const { return m_assetFormat; }
    bool assetPersisted() const { return m_assetPersisted; }

    /**
     * @brief Record that the planned asset exists on disk.
     *
     * The transient original-file payload is released after confirmation.
     */
    void markAssetPersisted();
    
    /**
     * @brief Calculate and store the SHA-256 hash of the image.
     * 
     * Used for deduplication when saving to notebook.
     */
    void calculateHash();
    
    /**
     * @brief Resize while maintaining aspect ratio.
     * @param newWidth The new width.
     * 
     * Height is calculated to maintain the original aspect ratio.
     */
    void resizeToWidth(qreal newWidth);
    
    /**
     * @brief Resize while maintaining aspect ratio.
     * @param newHeight The new height.
     * 
     * Width is calculated to maintain the original aspect ratio.
     */
    void resizeToHeight(qreal newHeight);
    
    /**
     * @brief Get the full path to the image file.
     * @param basePath Bundle path (e.g., "/path/to/notebook.snb").
     * @return Full path to the image file (basePath/assets/images/filename).
     * 
     * Phase O1.6: Resolves against assets/images/ subdirectory.
     * If imagePath is absolute (legacy), returns it unchanged.
     */
    QString fullPath(const QString& basePath) const;
    
    /**
     * @brief Save the image to the bundle's assets folder.
     * @param bundlePath Path to the .snb bundle directory.
     * @return True if saved successfully (or already exists).
     * 
     * Phase O1.6: Hash-based naming for deduplication.
     * - Calculates SHA-256 hash of image data
     * - Saves to assets/images/{hash16}.png if not exists
     * - Updates imagePath to just the filename
     * 
     * If an image with the same hash already exists, reuses it
     * (deduplication - multiple ImageObjects can share one file).
     */
    bool saveToAssets(const QString& bundlePath);
    
private:
    QJsonObject toJsonImpl(bool includeRecoveryData) const;
    void clearDisplayCache() const;

    QPixmap cachedPixmap;  ///< Cached pixmap for rendering

    /**
     * Original encoded bytes for a fresh file import. Retained only until the
     * background write is confirmed, or until the first save for documents
     * that do not yet have a bundle path.
     */
    QByteArray m_encodedAssetData;
    QByteArray m_assetFormat = QByteArrayLiteral("png");

    /// Display-resolution cache; the full pixmap remains available for export.
    mutable QPixmap m_displayPixmap;
    mutable QSize m_displayPixmapPixelSize;

    /**
     * @brief Transient flag: true once the image asset is confirmed on disk.
     *
     * NOT serialized. Set by saveToAssets() (after the file is written or its
     * existence is confirmed) and by loadImage() (the file it just read exists).
     * While false, toJson() embeds a base64 recovery copy so the pixels are
     * never left as a dangling reference if the asset later goes missing.
     */
    bool m_assetPersisted = false;
};
