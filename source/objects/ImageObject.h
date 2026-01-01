#pragma once

// ============================================================================
// ImageObject - An image inserted onto a page
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.5)
// 
// ImageObject represents an image that has been inserted onto a page.
// It stores the path to the image file and caches the pixmap for rendering.
// ============================================================================

#include "InsertedObject.h"
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
     * @brief Deserialize from JSON.
     * @param obj JSON object containing image data.
     */
    void loadFromJson(const QJsonObject& obj) override;
    
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
    void unloadImage() { cachedPixmap = QPixmap(); }
    
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
    QPixmap cachedPixmap;  ///< Cached pixmap for rendering
};
