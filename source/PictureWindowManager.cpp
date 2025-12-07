#include "PictureWindowManager.h"
#include "PictureWindow.h"
#include "InkCanvas.h"
#include "MainWindow.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QDebug>
#include <QFileInfo>
#include <QUuid>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSet>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

PictureWindowManager::PictureWindowManager(InkCanvas *canvas, QObject *parent)
    : QObject(parent), canvas(canvas), selectionMode(false), isDestroying(false)
{
    // ✅ PERFORMANCE: Initialize position update throttle timer to avoid lag during scrolling
    positionUpdateThrottleTimer = new QTimer(this);
    positionUpdateThrottleTimer->setSingleShot(true);
    positionUpdateThrottleTimer->setInterval(32); // ~30 FPS max update rate
    hasPendingPositionUpdate = false;
    connect(positionUpdateThrottleTimer, &QTimer::timeout, this, [this]() {
        if (hasPendingPositionUpdate) {
            hasPendingPositionUpdate = false;
            // Actually update positions - but only for visible windows
            for (PictureWindow *window : currentWindows) {
                if (window && window->isVisible()) {
                    window->updateScreenPositionImmediate();
                }
            }
        }
    });
    
    // Connect to canvas signals to update window positions when canvas changes
    if (canvas) {
        connect(canvas, &InkCanvas::panChanged, this, &PictureWindowManager::updateAllWindowPositions);
        connect(canvas, &InkCanvas::zoomChanged, this, &PictureWindowManager::updateAllWindowPositions);
    }
}

PictureWindowManager::~PictureWindowManager() {
    // ✅ Set destruction flag to prevent dangerous operations
    isDestroying = true;
    
    // ✅ Clear windows first before cleanup
    clearAllWindows();
    
    // ✅ Skip cleanup entirely during destruction to prevent crashes
    // The canvas pointer may be dangling (pointing to freed memory)
    // cleanupUnusedImages(); // Disabled - too dangerous during destruction
}

PictureWindow* PictureWindowManager::createPictureWindow(const QRect &rect, const QString &imagePath) {
    // qDebug() << "PictureWindowManager::createPictureWindow() called";
    // qDebug() << "  Rect:" << rect;
    // qDebug() << "  Image path:" << imagePath;
    
    if (!canvas) {
        // qDebug() << "  ERROR: Canvas is null!";
        return nullptr;
    }
    
    if (imagePath.isEmpty()) {
        // qDebug() << "  ERROR: Image path is empty!";
        return nullptr;
    }
    
    // Convert screen coordinates to canvas coordinates
    QRect canvasRect = convertScreenToCanvasRect(rect);
    // qDebug() << "  Canvas rect:" << canvasRect;
    
    // Apply bounds checking to ensure window is within canvas bounds
    QRect canvasBounds = canvas->getCanvasRect();
    // qDebug() << "  Canvas bounds:" << canvasBounds;
    
    // Ensure window stays within canvas bounds
    int maxX = canvasBounds.width() - canvasRect.width();
    int maxY = canvasBounds.height() - canvasRect.height();
    
    canvasRect.setX(qMax(0, qMin(canvasRect.x(), maxX)));
    canvasRect.setY(qMax(0, qMin(canvasRect.y(), maxY)));
    // qDebug() << "  Bounds-checked canvas rect:" << canvasRect;
    
    // Create new picture window with canvas coordinates
    PictureWindow *window = new PictureWindow(canvasRect, imagePath, canvas);
    // qDebug() << "  PictureWindow created successfully";
    
    // Connect signals
    connectWindowSignals(window);
    
    // Add to current windows list
    currentWindows.append(window);
    // qDebug() << "  Added to current windows list. Total windows:" << currentWindows.size();
    
    // Window is invisible - will be rendered by canvas
    // qDebug() << "  Picture window created (invisible, rendered by canvas)";
    
    // Mark canvas as edited since we created a new picture window
    if (canvas) {
        canvas->setEdited(true);
        int currentPage = canvas->getLastActivePage();
        cacheUpdatedPages.remove(currentPage); // Clear cache flag to force re-clone
        canvas->update(); // Trigger repaint to show the new picture
    }
    
    emit windowCreated(window);
    // qDebug() << "  windowCreated signal emitted";
    return window;
}

void PictureWindowManager::removePictureWindow(PictureWindow *window) {
    if (!window) return;
    
    // ✅ Delete the associated image file before removing the window
    QString imagePath = window->getImagePath();
    if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
        // Only delete if the image is inside the notebook folder (don't delete user's original files)
        QString saveFolder = getSaveFolder();
        if (!saveFolder.isEmpty() && imagePath.startsWith(saveFolder)) {
            QFile::remove(imagePath);
            // qDebug() << "Deleted image file:" << imagePath;
        }
    }
    
    // Remove from current windows
    currentWindows.removeAll(window);
    
    // ✅ CRASH FIX: Invalidate and clean up the entire permanent cache
    // After deletion, the cache is stale and needs to be rebuilt from disk
    // We need to delete the cached clone instances to prevent memory leaks
    for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
        QList<PictureWindow*> cachedWindows = it.value();
        // If this page's cache contains the deleted window, invalidate the entire page cache
        if (cachedWindows.contains(window)) {
            // Delete all cached clone instances for this page
            for (PictureWindow* cachedWindow : cachedWindows) {
                if (cachedWindow && cachedWindow != window) {
                    // ✅ MEMORY SAFETY: Use deleteLater() for QWidget-derived objects
                    cachedWindow->deleteLater();
                }
            }
            // Remove this page from cache - it will be reloaded from disk next time
            it = pageWindows.erase(it);
            if (it == pageWindows.end()) break;
        } else {
            // Just remove the window from this page's list (shouldn't normally happen)
            it.value().removeAll(window);
        }
    }

    // ✅ CRASH FIX: Also remove from combinedTempWindows to prevent accessing deleted windows
    combinedTempWindows.removeAll(window);
    
    emit windowRemoved(window);
    
    // Delete the window
    window->deleteLater();
}

void PictureWindowManager::clearAllWindows() {
    // Clear current windows
    for (PictureWindow *window : currentWindows) {
        if (window) {
            // ✅ Delete the associated image file before removing the window
            QString imagePath = window->getImagePath();
            if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                // Only delete if the image is inside the notebook folder (don't delete user's original files)
                QString saveFolder = getSaveFolder();
                if (!saveFolder.isEmpty() && imagePath.startsWith(saveFolder)) {
                    QFile::remove(imagePath);
                    // qDebug() << "Cleared all image file:" << imagePath;
                }
            }
        }
        window->deleteLater();
    }
    currentWindows.clear();
    
    // Clear page windows
    for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
        for (PictureWindow *window : it.value()) {
            if (window) {
                // ✅ Delete the associated image file before removing the window
                QString imagePath = window->getImagePath();
                if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                    // Only delete if the image is inside the notebook folder (don't delete user's original files)
                    QString saveFolder = getSaveFolder();
                    if (!saveFolder.isEmpty() && imagePath.startsWith(saveFolder)) {
                        QFile::remove(imagePath);
                        // qDebug() << "Cleared all page image file:" << imagePath;
                    }
                }
            }
            window->deleteLater();
        }
    }
    pageWindows.clear();
}

void PictureWindowManager::clearAllCachedWindows() {
    // ✅ MEMORY LEAK FIX: Clean up all cached windows WITHOUT deleting image files
    // This is used during canvas destruction to prevent memory leaks

    // Clear combined temp windows (temporary clones used for pseudo-smooth scrolling)
    for (PictureWindow *window : combinedTempWindows) {
        if (window) {
            // Clear render cache to release large pixmap memory
            window->clearRenderCache();
            window->deleteLater();
        }
    }
    combinedTempWindows.clear();

    // Clear permanent page cache (windows loaded from disk and cached for reuse)
    for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
        for (PictureWindow *window : it.value()) {
            if (window) {
                // Clear render cache to release large pixmap memory
                window->clearRenderCache();
                window->deleteLater();
            }
        }
    }
    pageWindows.clear();
    pageAccessOrder.clear(); // ✅ Clear LRU tracking

    // Clear current windows (the visible windows on screen)
    for (PictureWindow *window : currentWindows) {
        if (window) {
            // Clear render cache to release large pixmap memory
            window->clearRenderCache();
            window->deleteLater();
        }
    }
    currentWindows.clear();
}


void PictureWindowManager::saveWindowsForPage(int pageNumber) {
    if (!canvas) return;
    
    // ✅ CRITICAL FIX: Don't update pageWindows cache during combined view mode
    // In combined mode, currentWindows contains temporary clones that will be deleted
    // Updating the cache with these clones causes memory leaks (clones of clones)
    // Only update cache in single-page mode where currentWindows are the actual permanent instances
    bool isCombinedMode = !combinedTempWindows.isEmpty();
    if (!isCombinedMode) {
        // Only update permanent cache in single-page mode
        pageWindows[pageNumber] = currentWindows;
    }
    
    // ✅ PERFORMANCE FIX: Don't write to disk here, just update cache
    // The autosave timer will call flushDirtyPagesToDisk() to write dirty pages
    // This prevents blocking disk I/O on every page switch
}

void PictureWindowManager::flushDirtyPagesToDisk() {
    // ✅ PERFORMANCE: Write all cached pages to disk (called by autosave)
    // This avoids blocking disk I/O on every page switch
    
    // For picture windows, we write all cached pages to disk
    // (Pictures are less frequently modified than markdown, so this is acceptable)
    for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
        int pageNumber = it.key();
        const QList<PictureWindow*> &windows = it.value();
        savePictureData(pageNumber, windows);
    }
}

void PictureWindowManager::loadWindowsForPage(int pageNumber) {
    if (!canvas) return;

    // ✅ MEMORY LEAK FIX: Clean up temporary combined windows when switching to regular page mode
    for (PictureWindow *window : combinedTempWindows) {
        if (window) {
            // Check if this window is in the permanent page cache
            bool isPermanent = false;
            for (const QList<PictureWindow*> &pageList : pageWindows.values()) {
                if (pageList.contains(window)) {
                    isPermanent = true;
                    break;
                }
            }

            // Only delete temporary cloned instances, not permanent cached ones
            if (!isPermanent) {
                // ✅ CRITICAL MEMORY LEAK FIX: Clear cached rendering before deletion
                // During inertia scrolling, each clone may have a huge cached pixmap (e.g., 4K image)
                // This cache must be explicitly released to prevent memory accumulation
                window->clearRenderCache();
                // ✅ MEMORY SAFETY: Use deleteLater() for QWidget-derived objects
                window->deleteLater();
            }
        }
    }
    combinedTempWindows.clear();

    QList<PictureWindow*> newPageWindows;

    // Check if we have windows for this page in memory
    if (pageWindows.contains(pageNumber)) {

        // ✅ LRU: Mark this page as recently accessed
        pageAccessOrder.removeAll(pageNumber);
        pageAccessOrder.append(pageNumber);

        newPageWindows = pageWindows[pageNumber];
    } else {
        // Load from file
        newPageWindows = loadPictureData(pageNumber);

        // Update page windows map
        if (!newPageWindows.isEmpty()) {
            pageWindows[pageNumber] = newPageWindows;

            // ✅ LRU: Mark this newly loaded page as recently accessed
            pageAccessOrder.removeAll(pageNumber);
            pageAccessOrder.append(pageNumber);

            // ✅ MEMORY LEAK FIX: Evict old cached pages using LRU strategy
            evictOldCachedPages(pageNumber);
        }
    }

    // Update the current window list and show the windows
    currentWindows = newPageWindows;
    for (PictureWindow *window : currentWindows) {
        // Validate that the window is within canvas bounds
        if (!window->isValidForCanvas()) {
            QRect canvasBounds = canvas->getCanvasRect();
            QRect windowRect = window->getCanvasRect();
            
            // Clamp the window position to canvas bounds
            int newX = qMax(0, qMin(windowRect.x(), canvasBounds.width() - windowRect.width()));
            int newY = qMax(0, qMin(windowRect.y(), canvasBounds.height() - windowRect.height()));
            
            if (newX != windowRect.x() || newY != windowRect.y()) {
                QRect adjustedRect(newX, newY, windowRect.width(), windowRect.height());
                window->setCanvasRect(adjustedRect);
            }
        }
        
        // Ensure canvas connections are set up for loaded windows
        window->ensureCanvasConnections();
        
        // Update screen position (even though window is invisible)
        window->updateScreenPositionImmediate();
    }
    
    // Trigger canvas repaint to show loaded pictures
    if (canvas && !currentWindows.isEmpty()) {
        canvas->update();
    }
}

void PictureWindowManager::deleteWindowsForPage(int pageNumber) {
    // Remove windows from memory
    if (pageWindows.contains(pageNumber)) {
        QList<PictureWindow*> windows = pageWindows[pageNumber];
        for (PictureWindow *window : windows) {
            if (window) {
                // ✅ Delete the associated image file before removing the window
                QString imagePath = window->getImagePath();
                if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                    // Only delete if the image is inside the notebook folder (don't delete user's original files)
                    QString saveFolder = getSaveFolder();
                    if (!saveFolder.isEmpty() && imagePath.startsWith(saveFolder)) {
                        QFile::remove(imagePath);
                        // qDebug() << "Deleted page image file:" << imagePath;
                    }
                }
            }
            window->deleteLater();
        }
        pageWindows.remove(pageNumber);
    }
    
    // Delete metadata file
    QString filePath = getPictureDataFilePath(pageNumber);
    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }
    
    // Clear current windows if they belong to this page
    if (pageWindows.value(pageNumber) == currentWindows) {
        currentWindows.clear();
    }
}

void PictureWindowManager::hideAllWindows() {
    // Hide all current windows
    for (PictureWindow *window : currentWindows) {
        window->hide();
    }
}

void PictureWindowManager::setWindowsFrameOnlyMode(bool enabled) {
    // Set frame-only mode for all current windows
    for (PictureWindow* window : currentWindows) {
        if (window) {
            window->setFrameOnlyMode(enabled);
        }
    }
}

void PictureWindowManager::setSelectionMode(bool enabled) {
    selectionMode = enabled;
    
    // Change cursor for canvas
    if (canvas) {
        canvas->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    }
}

QList<PictureWindow*> PictureWindowManager::getCurrentPageWindows() const {
    return currentWindows;
}

QList<PictureWindow*> PictureWindowManager::loadWindowsForPageSeparately(int pageNumber) {
    if (!canvas) return QList<PictureWindow*>();

    // ✅ PSEUDO-SMOOTH SCROLLING: Always create new window instances for combined views
    // The same notebook page appears at different Y-offsets in adjacent combined pages
    // (e.g., page 2 appears at bottom of display-page-1 and top of display-page-2)
    // We need separate window instances for each position, but reuse the cached data

    QList<PictureWindow*> windows;

    // Check if we have cached windows for this page that we can clone from
    if (pageWindows.contains(pageNumber) && !pageWindows[pageNumber].isEmpty()) {
        // Clone the cached windows to create new instances with same data
        // ✅ LRU: Mark this page as recently accessed
        pageAccessOrder.removeAll(pageNumber);
        pageAccessOrder.append(pageNumber);

        const QList<PictureWindow*> &cachedWindows = pageWindows[pageNumber];
        for (PictureWindow *cachedWindow : cachedWindows) {
            // Serialize the cached window and create a new instance from it
            QVariantMap data = cachedWindow->serialize();
            QString imagePath = data.value("image_path", "").toString();

            // ✅ CRITICAL FIX: Resolve relative paths to absolute paths
            QFileInfo fileInfo(imagePath);
            if (fileInfo.isRelative()) {
                imagePath = getSaveFolder() + "/" + imagePath;
                data["image_path"] = imagePath; // Update data with absolute path
            }

            if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                // Create new window instance with same data
                PictureWindow *window = new PictureWindow(QRect(0, 0, 200, 150), imagePath, canvas);
                window->deserialize(data);
                windows.append(window);
            }
        }
    } else {
        // Load windows from file for the first time
        QList<PictureWindow*> loadedWindows = loadPictureData(pageNumber);

        // ✅ CRITICAL FIX: Store CLONES in permanent cache, not the loaded instances
        // The loaded instances will be returned and potentially Y-adjusted for combined views
        // We need the cache to always have the original unadjusted coordinates
        QList<PictureWindow*> permanentCache;
        for (PictureWindow *loadedWindow : loadedWindows) {
            // Clone for permanent cache
            QVariantMap data = loadedWindow->serialize();
            QString imagePath = data.value("image_path", "").toString();

            // ✅ CRITICAL FIX: Resolve relative paths to absolute paths
            QFileInfo fileInfo(imagePath);
            if (fileInfo.isRelative()) {
                imagePath = getSaveFolder() + "/" + imagePath;
                data["image_path"] = imagePath; // Update data with absolute path
            }

            if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                PictureWindow *cacheWindow = new PictureWindow(QRect(0, 0, 200, 150), imagePath, canvas);
                cacheWindow->deserialize(data);
                permanentCache.append(cacheWindow);
            }
        }

        // Store in permanent cache
        if (!permanentCache.isEmpty()) {
            pageWindows[pageNumber] = permanentCache;

            // ✅ LRU: Mark this newly loaded page as recently accessed
            pageAccessOrder.removeAll(pageNumber);
            pageAccessOrder.append(pageNumber);

            // ✅ MEMORY LEAK FIX: Evict old cached pages using LRU strategy
            evictOldCachedPages(pageNumber);
        }

        // ✅ CRITICAL FIX: Return the LOADED windows for immediate use, NOT clones
        // The loaded windows are separate instances that will be deleted when the page switches
        // The permanent cache holds different instances (permanentCache) that stay alive
        windows = loadedWindows;
    }
    
    // Apply bounds checking and setup connections for all windows
    for (PictureWindow *window : windows) {
        // Validate that the window is within canvas bounds
        if (!window->isValidForCanvas()) {
            QRect canvasBounds = canvas->getCanvasRect();
            QRect windowRect = window->getCanvasRect();
            
            // Clamp the window position to canvas bounds
            int newX = qMax(0, qMin(windowRect.x(), canvasBounds.width() - windowRect.width()));
            int newY = qMax(0, qMin(windowRect.y(), canvasBounds.height() - windowRect.height()));
            
            if (newX != windowRect.x() || newY != windowRect.y()) {
                QRect adjustedRect(newX, newY, windowRect.width(), windowRect.height());
                window->setCanvasRect(adjustedRect);
            }
        }
        
        // Ensure canvas connections are set up for loaded windows
        window->ensureCanvasConnections();
        
        // Connect window signals
        connectWindowSignals(window);
        
        // Update screen position (even though window is invisible)
        window->updateScreenPositionImmediate();
    }
    
    return windows;
}

void PictureWindowManager::setCombinedWindows(const QList<PictureWindow*> &windows) {
    // ✅ CRASH FIX: Hide current windows BEFORE deleting any windows
    // Otherwise we might try to hide already-deleted windows
    for (PictureWindow *window : currentWindows) {
        window->hide();
    }

    // ✅ MEMORY LEAK FIX: Clean up old temporary combined windows
    // These are cloned instances created for pseudo-smooth scrolling that need deletion
    for (PictureWindow *window : combinedTempWindows) {
        if (window) {
            // Don't delete if it's in the new incoming windows list (will be reused)
            if (windows.contains(window)) {
                continue;
            }

            // Check if this window is in the permanent page cache
            bool isPermanent = false;
            for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
                if (it.value().contains(window)) {
                    isPermanent = true;
                    break;
                }
            }

            // Only delete temporary cloned instances, not permanent cached ones
            if (!isPermanent) {
                // ✅ CRITICAL MEMORY LEAK FIX: Clear cached rendering before deletion
                // During inertia scrolling, each clone may have a huge cached pixmap (e.g., 4K image)
                // This cache must be explicitly released to prevent memory accumulation
                window->clearRenderCache();
                // ✅ MEMORY SAFETY: Use deleteLater() for QWidget-derived objects
                window->deleteLater();
            }
        }
    }
    combinedTempWindows.clear();
    
    // Set new combined windows as current
    currentWindows = windows;

    // ✅ Track these combined windows for cleanup on next call
    combinedTempWindows = windows;
    
    // Update screen positions for all combined windows
    for (PictureWindow *window : currentWindows) {
        window->updateScreenPositionImmediate();
    }
    
    // Trigger canvas repaint to show combined pictures
    if (canvas && !currentWindows.isEmpty()) {
        canvas->update();
    }
}

void PictureWindowManager::saveWindowsForPageSeparately(int pageNumber, const QList<PictureWindow*> &windows) {
    if (!canvas) return;
    
    // ✅ CRITICAL PERFORMANCE FIX: Skip if cache already updated for this page
    // This prevents expensive serialize/clone operations on every page switch
    if (cacheUpdatedPages.contains(pageNumber)) {
        return; // Cache already up-to-date
    }
    
    // ✅ PERFORMANCE FIX: Update cache but don't write to disk
    // InkCanvas has already adjusted coordinates, so we store these as the new cache
    
    // Delete old clones from cache (but not if they're in currentWindows)
    if (pageWindows.contains(pageNumber)) {
        for (PictureWindow *oldWindow : pageWindows[pageNumber]) {
            // Only delete if it's not in currentWindows (to avoid deleting active windows)
            if (!currentWindows.contains(oldWindow)) {
                oldWindow->clearRenderCache(); // Free cached pixmaps
                oldWindow->deleteLater();
            }
        }
    }
    
    // Create clones of the passed-in windows for the cache
    // (InkCanvas passes us temporary adjusted windows that will be deleted)
    QList<PictureWindow*> cacheWindows;
    for (PictureWindow *window : windows) {
        QVariantMap data = window->serialize();
        PictureWindow *cloneWindow = new PictureWindow(QRect(0, 0, 400, 300), window->getImagePath(), canvas);
        cloneWindow->deserialize(data);
        cacheWindows.append(cloneWindow);
    }
    
    // Update cache with clones
    pageWindows[pageNumber] = cacheWindows;
    
    // Mark cache as updated (so we don't clone again until picture is modified)
    cacheUpdatedPages.insert(pageNumber);
    
    // flushDirtyPagesToDisk() will write all cached pages to disk later
}

void PictureWindowManager::exitAllEditModes() {
    for (PictureWindow *window : currentWindows) {
        if (window && window->isInEditMode()) {
            window->forceExitEditMode();
        }
    }
}

void PictureWindowManager::renderPicturesToCanvas(QPainter &painter) const {
    if (!canvas) return;
    
    // Render all current picture windows to the canvas
    for (PictureWindow *window : currentWindows) {
        if (window) {
            // Get the canvas coordinates of the picture window
            QRect canvasRect = window->getCanvasRect();
            
            // Render the picture window content directly in canvas coordinates
            window->renderToCanvas(painter, canvasRect);
        }
    }
}

void PictureWindowManager::renderPicturesToCanvas(QPainter &painter, const QRect &updateRect) const {
    if (!canvas) return;
    
    // Only render picture windows that intersect with the update rectangle
    for (PictureWindow *window : currentWindows) {
        if (window) {
            // Get the canvas coordinates of the picture window
            QRect canvasRect = window->getCanvasRect();
            
            // Only render if the picture intersects with the update area
            if (canvasRect.intersects(updateRect)) {
                // Simple cached rendering - no complex clipping
                window->renderToCanvas(painter, canvasRect);
            }
        }
    }
}

PictureWindow* PictureWindowManager::hitTest(const QPoint &canvasPos) const {
    // Check if the canvas position hits any picture window (in reverse order for top-most)
    for (int i = currentWindows.size() - 1; i >= 0; --i) {
        PictureWindow *window = currentWindows[i];
        if (window && window->getCanvasRect().contains(canvasPos)) {
            return window;
        }
    }
    return nullptr;
}

QString PictureWindowManager::copyImageToNotebook(const QString &sourcePath, int pageNumber) {
    // qDebug() << "PictureWindowManager::copyImageToNotebook() called";
    // qDebug() << "  Source path:" << sourcePath;
    // qDebug() << "  Page number:" << pageNumber;
    
    QString saveFolder = getSaveFolder();
    // qDebug() << "  Save folder:" << saveFolder;
    
    if (saveFolder.isEmpty()) {
        // qDebug() << "  ERROR: Save folder is empty!";
        return QString();
    }
    
    if (!QFile::exists(sourcePath)) {
        // qDebug() << "  ERROR: Source file does not exist!";
        return QString();
    }
    
    // Generate unique filename
    QString targetFileName = generateImageFileName(sourcePath, pageNumber);
    QString targetPath = saveFolder + "/" + targetFileName;
    // qDebug() << "  Target filename:" << targetFileName;
    // qDebug() << "  Target path:" << targetPath;
    
    // Copy the file
    if (QFile::exists(targetPath)) {
        // qDebug() << "  Target file already exists, removing it first";
        QFile::remove(targetPath); // Remove existing file if it exists
    }
    
    QFileInfo sourceInfo(sourcePath);
    // qDebug() << "  Source file size:" << sourceInfo.size() << "bytes";
    
    if (QFile::copy(sourcePath, targetPath)) {
        QFileInfo targetInfo(targetPath);
        // qDebug() << "  SUCCESS: File copied successfully!";
        // qDebug() << "  Target file size:" << targetInfo.size() << "bytes";
        
        #ifdef Q_OS_WIN
        // Set hidden attribute on Windows (optional, for consistency with other metadata files)
        // SetFileAttributesW(reinterpret_cast<const wchar_t *>(targetPath.utf16()), FILE_ATTRIBUTE_HIDDEN);
        #endif
        
        return targetPath;
    } else {
        // qDebug() << "  ERROR: Failed to copy file!";
        return QString();
    }
}

void PictureWindowManager::cleanupUnusedImages() {
    // ✅ Clean up image files that are no longer referenced by any picture windows
    // ✅ Safety check: Don't cleanup during destruction - canvas pointer may be dangling
    if (isDestroying || !canvas) {
        // qDebug() << "Skipping cleanup - object is being destroyed or canvas is null";
        return;
    }
    
    QString saveFolder = getSaveFolder();
    if (saveFolder.isEmpty()) return;
    
    // Get all image files in the notebook folder
    QDir notebookDir(saveFolder);
    QStringList imageFilters;
    imageFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif" << "*.tiff" << "*.webp";
    QStringList imageFiles = notebookDir.entryList(imageFilters, QDir::Files);
    
    // Get all currently referenced image paths
    QSet<QString> referencedImages;
    
    // Check current windows
    for (PictureWindow *window : currentWindows) {
        if (window) {
            QString imagePath = window->getImagePath();
            if (!imagePath.isEmpty()) {
                referencedImages.insert(QFileInfo(imagePath).fileName());
            }
        }
    }
    
    // Check all page windows
    for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
        for (PictureWindow *window : it.value()) {
            if (window) {
                QString imagePath = window->getImagePath();
                if (!imagePath.isEmpty()) {
                    referencedImages.insert(QFileInfo(imagePath).fileName());
                }
            }
        }
    }
    
    // Delete unreferenced image files (only those that match our naming pattern)
    QString notebookId = getNotebookId();
    for (const QString &fileName : imageFiles) {
        if (fileName.startsWith(notebookId + "_img_") && !referencedImages.contains(fileName)) {
            QString fullPath = saveFolder + "/" + fileName;
            QFile::remove(fullPath);
            // qDebug() << "Cleaned up unused image file:" << fileName;
        }
    }
}

void PictureWindowManager::clearCurrentPageWindows() {
    // ✅ Safety check to prevent crashes during destruction
    if (isDestroying || !canvas) {
        // qDebug() << "clearCurrentPageWindows: destroying or canvas is null, skipping";
        return;
    }
    
    int currentPage = canvas->getLastActivePage();
    
    // Delete all current picture windows immediately
    for (PictureWindow *window : currentWindows) {
        if (window) {
            // ✅ Delete the associated image file before removing the window
            QString imagePath = window->getImagePath();
            if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                // Only delete if the image is inside the notebook folder (don't delete user's original files)
                QString saveFolder = getSaveFolder();
                if (!saveFolder.isEmpty() && imagePath.startsWith(saveFolder)) {
                    QFile::remove(imagePath);
                    // qDebug() << "Cleared image file:" << imagePath;
                }
            }
            
            // Remove from all page windows maps first
            for (auto it = pageWindows.begin(); it != pageWindows.end(); ++it) {
                it.value().removeAll(window);
            }

            // ✅ CRASH FIX: Also remove from combined temp windows to prevent dangling pointers
            combinedTempWindows.removeAll(window);
            
            // ✅ MEMORY SAFETY: Use deleteLater() for QWidget-derived objects
            // deleteLater() is safer and avoids timing issues with signal/slot connections
            window->deleteLater();
        }
    }
    currentWindows.clear();

    // ✅ CRASH FIX: Clear combinedTempWindows to prevent dangling pointers
    combinedTempWindows.clear();
    
    // Update the pageWindows map to reflect the cleared state
    pageWindows[currentPage] = QList<PictureWindow*>(); // Explicitly empty list
    
    // Mark canvas as edited since we cleared pictures
    canvas->setEdited(true);
    cacheUpdatedPages.remove(currentPage); // Clear cache flag to force re-clone
    
    canvas->update(); // Trigger repaint to remove all pictures
    
    // Save the cleared state to disk
    saveWindowsForPage(currentPage);
}

void PictureWindowManager::onWindowDeleteRequested(PictureWindow *window) {
    removePictureWindow(window);
    
    // Mark canvas as edited since we deleted a picture window
    if (canvas) {
        canvas->setEdited(true);
        int currentPage = canvas->getLastActivePage();
        cacheUpdatedPages.remove(currentPage); // Clear cache flag to force re-clone
    }
    
    // Save current state
    if (canvas) {
        int currentPage = canvas->getLastActivePage();
        saveWindowsForPage(currentPage);
    }
}

QString PictureWindowManager::getPictureDataFilePath(int pageNumber) const {
    QString saveFolder = getSaveFolder();
    QString notebookId = getNotebookId();
    
    if (saveFolder.isEmpty() || notebookId.isEmpty()) {
        return QString();
    }
    
    return saveFolder + QString("/.%1_pictures_%2.json").arg(notebookId).arg(pageNumber, 5, 10, QChar('0'));
}

void PictureWindowManager::savePictureData(int pageNumber, const QList<PictureWindow*> &windows) {
    QString filePath = getPictureDataFilePath(pageNumber);
    if (filePath.isEmpty()) return;
    
    QJsonArray windowsArray;
    for (PictureWindow *window : windows) {
        QVariantMap windowData = window->serialize();
        windowsArray.append(QJsonObject::fromVariantMap(windowData));
    }
    
    QJsonDocument doc(windowsArray);
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        
        #ifdef Q_OS_WIN
        // Set hidden attribute on Windows
        SetFileAttributesW(reinterpret_cast<const wchar_t *>(filePath.utf16()), FILE_ATTRIBUTE_HIDDEN);
        #endif
    }
}

QList<PictureWindow*> PictureWindowManager::loadPictureData(int pageNumber) {
    QList<PictureWindow*> windows;
    
    QString filePath = getPictureDataFilePath(pageNumber);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return windows;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return windows;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse picture window data:" << error.errorString();
        return windows;
    }
    
    QJsonArray windowsArray = doc.array();
    for (const QJsonValue &value : windowsArray) {
        QJsonObject windowObj = value.toObject();
        QVariantMap windowData = windowObj.toVariantMap();
        
        // Get image path from data
        QString imagePath = windowData.value("image_path", "").toString();

        // ✅ CRITICAL FIX: If path is relative (filename only), resolve it to absolute path
        // This handles .spn packages extracted to different temp folders
        QFileInfo fileInfo(imagePath);
        if (fileInfo.isRelative()) {
            // Resolve relative to save folder
            QString resolvedPath = getSaveFolder() + "/" + imagePath;
            imagePath = resolvedPath;
            // Also update the windowData so it's passed correctly to the window
            windowData["image_path"] = imagePath;
        }

        if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
            continue; // Skip windows with missing images
        }
        
        // Create window with default rect (will be updated by deserialize)
        PictureWindow *window = new PictureWindow(QRect(0, 0, 200, 150), imagePath, canvas);
        window->deserialize(windowData);
        
        // Connect signals
        connectWindowSignals(window);
        
        windows.append(window);
        // Window is invisible - will be rendered by canvas
    }
    
    return windows;
}

QString PictureWindowManager::getSaveFolder() const {
    // ✅ Extra safety check to prevent crashes during destruction
    if (isDestroying || !canvas) {
        // qDebug() << "PictureWindowManager::getSaveFolder() - destroying or canvas is null";
        return QString();
    }
    
    QString result = canvas->getSaveFolder();
    // qDebug() << "PictureWindowManager::getSaveFolder() returning:" << result;
    return result;
}

QString PictureWindowManager::getNotebookId() const {
    if (isDestroying || !canvas) return QString();
    
    // Try to get notebook ID from JSON metadata first
    QString notebookId = canvas->getNotebookId();
    if (!notebookId.isEmpty()) {
        return notebookId;
    }
    
    // Fallback to old method
    QString saveFolder = canvas->getSaveFolder();
    if (saveFolder.isEmpty()) return QString();
    
    QString idFile = saveFolder + "/.notebook_id.txt";
    if (QFile::exists(idFile)) {
        QFile file(idFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString fallbackId = in.readLine().trimmed();
            file.close();
            return fallbackId;
        }
    }
    
    return "notebook"; // Default fallback
}

QString PictureWindowManager::generateImageFileName(const QString &originalPath, int pageNumber) {
    QFileInfo fileInfo(originalPath);
    QString extension = fileInfo.suffix().toLower();
    QString notebookId = getNotebookId();
    
    // Create a hash of the original file path and content for uniqueness
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(originalPath.toUtf8());
    
    // Add file content to hash to ensure uniqueness even for files with same path
    QFile originalFile(originalPath);
    if (originalFile.open(QIODevice::ReadOnly)) {
        hash.addData(originalFile.read(1024)); // Hash first 1KB for performance
        originalFile.close();
    }
    
    QString hashString = hash.result().toHex().left(8); // Use first 8 characters of hash
    
    return QString("%1_img_p%2_%3.%4")
           .arg(notebookId)
           .arg(pageNumber, 5, 10, QChar('0'))
           .arg(hashString)
           .arg(extension);
}


void PictureWindowManager::updatePermanentCacheForWindow(PictureWindow *modifiedWindow, int pageNumber) {
    // ✅ USER MODIFICATION FIX: Update permanent cache when user moves/resizes a window
    // This ensures changes persist even during combined mode
    if (!pageWindows.contains(pageNumber)) {
        return; // No cache for this page yet
    }

    // Find the permanent cached window that corresponds to this modified window
    // Match by image path since that's unique per window
    QString imagePath = modifiedWindow->getImagePath();
    QList<PictureWindow*> &cachedWindows = pageWindows[pageNumber];

    for (PictureWindow *cachedWindow : cachedWindows) {
        if (cachedWindow && cachedWindow->getImagePath() == imagePath) {
            // Get the rect from the modified window
            QRect newRect = modifiedWindow->getCanvasRect();
            
            // ✅ COMBINED CANVAS FIX: Adjust Y coordinate for bottom-half windows
            // In combined mode, bottom-half windows have Y += singlePageHeight
            // But the cache stores normalized coordinates (Y relative to single page)
            if (canvas && canvas->isCombinedCanvasMode()) {
                int singlePageHeight = canvas->getSinglePageHeight();
                int primaryPage = canvas->getLastActivePage();
                
                // If this window is on the bottom half (next page), adjust Y coordinate
                if (pageNumber == primaryPage + 1 && newRect.top() >= singlePageHeight) {
                    newRect.moveTop(newRect.top() - singlePageHeight);
                }
            }
            
            // Update the permanent cached window's position/size
            cachedWindow->setCanvasRect(newRect);
            break;
        }
    }
}

void PictureWindowManager::connectWindowSignals(PictureWindow *window) {
    // Connect existing signals
    connect(window, &PictureWindow::deleteRequested, this, &PictureWindowManager::onWindowDeleteRequested);
    connect(window, &PictureWindow::windowMoved, this, [this, window](PictureWindow*) {
        // ✅ PERFORMANCE: Only mark as edited and save - don't trigger extra updates
        // The canvas handles its own updates more efficiently during movement
        if (canvas) {
            canvas->setEdited(true);
            
            int primaryPage = canvas->getLastActivePage();
            
            // ✅ CROSS-PAGE MOVE FIX: Clear cache flags for BOTH pages in combined mode
            // When a window moves across the page boundary (top↔bottom), we must:
            // 1. Remove it from the old page's cache
            // 2. Add it to the new page's cache
            // Clearing both flags ensures saveCombinedWindowsForPage rebuilds both caches correctly
            cacheUpdatedPages.remove(primaryPage);
            cacheUpdatedPages.remove(primaryPage + 1);

            // ✅ Use saveCombinedWindowsForPage for proper coordinate handling
            // This rebuilds both page caches based on current Y positions
            canvas->saveCombinedWindowsForPage(primaryPage);
        }
    });
    connect(window, &PictureWindow::windowResized, this, [this, window](PictureWindow*) {
        // ✅ PERFORMANCE: Only mark as edited and save - don't trigger extra updates
        // The canvas handles its own updates more efficiently during resize
        if (canvas) {
            canvas->setEdited(true);
            
            int primaryPage = canvas->getLastActivePage();
            
            // ✅ CROSS-PAGE MOVE FIX: Clear cache flags for BOTH pages in combined mode
            // Resizing could also cause the window to cross the page boundary
            cacheUpdatedPages.remove(primaryPage);
            cacheUpdatedPages.remove(primaryPage + 1);
            
            // ✅ Use saveCombinedWindowsForPage for proper coordinate handling
            canvas->saveCombinedWindowsForPage(primaryPage);
        }
    });
    connect(window, &PictureWindow::windowInteracted, this, [this, window](PictureWindow*) {
        // Window was clicked/interacted with
        // Could be used for future features like selection, focus management, etc.
    });
    connect(window, &PictureWindow::editModeChanged, this, [this, window](PictureWindow*, bool enabled) {
        // Handle edit mode changes to disable/enable canvas pan
        if (canvas) {
            canvas->setPictureWindowEditMode(enabled);
            // Only update the picture window's area instead of full canvas
            QRect canvasRect = window->getCanvasRect();
            QRect widgetRect = canvas->mapCanvasToWidget(canvasRect);
            canvas->update(widgetRect.adjusted(-10, -10, 10, 10)); // Larger padding for edit mode borders
            // qDebug() << "PictureWindowManager: Picture window edit mode changed to:" << enabled;
        }
    });
}

void PictureWindowManager::updateAllWindowPositions() {
    // ✅ PERFORMANCE: Throttle position updates to avoid lag during inertia scrolling
    // Instead of updating immediately on every pan change (60 FPS during inertia),
    // queue the update and use a timer to limit to ~30 FPS
    hasPendingPositionUpdate = true;
    
    // If timer is not running, start it
    if (!positionUpdateThrottleTimer->isActive()) {
        positionUpdateThrottleTimer->start();
    }
    // If timer is already running, it will process the pending update when it fires
}

QRect PictureWindowManager::convertScreenToCanvasRect(const QRect &screenRect) const {
    if (!canvas) {
        return screenRect;
    }
    
    // Use the coordinate conversion methods from InkCanvas
    QRect canvasRect = canvas->mapWidgetToCanvas(screenRect);
    return canvasRect;
}

void PictureWindowManager::evictOldCachedPages(int currentPage) {
    // ✅ MEMORY LEAK FIX: Implement LRU cache eviction for picture windows
    // Keep only the most recent 5 pages in cache to prevent memory buildup
    const int MAX_CACHED_PAGES = 5;

    if (pageWindows.size() <= MAX_CACHED_PAGES) {
        return; // Cache size is acceptable
    }

    // Evict least recently used pages (first entries in pageAccessOrder)
    int pagesToEvict = pageWindows.size() - MAX_CACHED_PAGES;

    for (int i = 0; i < pagesToEvict && i < pageAccessOrder.size(); ++i) {
        int pageToEvict = pageAccessOrder[i];

        // ✅ PSEUDO-INFINITE SCROLLING FIX: Don't evict current page or next page
        // In combined mode, we show pages N and N+1 together
        if (pageToEvict == currentPage || pageToEvict == currentPage + 1 || 
            pageToEvict == currentPage - 1) {
            continue; // Skip evicting nearby pages that might be visible
        }

        // ✅ Don't evict if this page's windows are in combinedTempWindows
        // ✅ Don't evict if this page's windows are currently displayed
        // Check BOTH combinedTempWindows (for combined mode) AND currentWindows (for single-page mode)
        bool isCurrentlyDisplayed = false;
        if (pageWindows.contains(pageToEvict)) {
            for (PictureWindow* window : pageWindows[pageToEvict]) {
                if (combinedTempWindows.contains(window) || currentWindows.contains(window)) {
                    isCurrentlyDisplayed = true;
                    break;
                }
            }
        }

        if (isCurrentlyDisplayed) {
            continue; // Skip evicting pages that are currently displayed
        }

        // Safe to evict this page
        if (pageWindows.contains(pageToEvict)) {
            QList<PictureWindow*> oldWindows = pageWindows[pageToEvict];
            for (PictureWindow* window : oldWindows) {
                if (window) {
                    // Clear render cache to release pixmap memory
                    window->clearRenderCache();
                    // ✅ MEMORY SAFETY: Use deleteLater() for QWidget-derived objects
                    window->deleteLater();
                }
            }
            pageWindows.remove(pageToEvict);
            pageAccessOrder.removeAll(pageToEvict);
        }
    }
}

