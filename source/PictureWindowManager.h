#ifndef PICTUREWINDOWMANAGER_H
#define PICTUREWINDOWMANAGER_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QRect>
#include <QVariantMap>
#include <QPainter>

class PictureWindow;
class InkCanvas;

class PictureWindowManager : public QObject {
    Q_OBJECT

public:
    explicit PictureWindowManager(InkCanvas *canvas, QObject *parent = nullptr);
    ~PictureWindowManager();

    // Picture window management
    PictureWindow* createPictureWindow(const QRect &rect, const QString &imagePath);
    void removePictureWindow(PictureWindow *window);
    void clearAllWindows();

    // Page management
    void saveWindowsForPage(int pageNumber); // Updates cache only, no disk writes
    void flushDirtyPagesToDisk(); // Write all dirty pages to disk (called by autosave)
    void loadWindowsForPage(int pageNumber);
    void deleteWindowsForPage(int pageNumber);
    void hideAllWindows();

    // Selection mode (for placing new pictures)
    void setSelectionMode(bool enabled);
    bool isSelectionMode() const { return selectionMode; }

    // Current page windows
    QList<PictureWindow*> getCurrentPageWindows() const;
    void exitAllEditModes();
    
    // Combined canvas support
    QList<PictureWindow*> loadWindowsForPageSeparately(int pageNumber);
    void setCombinedWindows(const QList<PictureWindow*> &windows);
    void saveWindowsForPageSeparately(int pageNumber, const QList<PictureWindow*> &windows);
    
    // Canvas rendering
    void renderPicturesToCanvas(QPainter &painter) const;
    void renderPicturesToCanvas(QPainter &painter, const QRect &updateRect) const;
    
    // Hit testing
    PictureWindow* hitTest(const QPoint &canvasPos) const;

    // File management
    QString copyImageToNotebook(const QString &sourcePath, int pageNumber);
    void cleanupUnusedImages();
    void clearCurrentPageWindows(); // Clear all pictures from current page
    void clearAllCachedWindows(); // Clear all cached windows (for destructor cleanup)
    QString getSaveFolder() const;
    QString getNotebookId() const;
    
    // Frame-only mode (for performance during touch panning)
    void setWindowsFrameOnlyMode(bool enabled);

signals:
    void windowCreated(PictureWindow *window);
    void windowRemoved(PictureWindow *window);

public slots:
    void onWindowDeleteRequested(PictureWindow *window);
    void updateAllWindowPositions();

private:
    // Helper methods
    QString getPictureDataFilePath(int pageNumber) const;
    void savePictureData(int pageNumber, const QList<PictureWindow*> &windows);
    QList<PictureWindow*> loadPictureData(int pageNumber);
    void updatePermanentCacheForWindow(PictureWindow *modifiedWindow, int pageNumber);
    void evictOldCachedPages(int currentPage); // ✅ LRU cache eviction
    QString extractImageHash(const QString &imagePath) const; // Extract hash portion from image filename
    
    
    QString generateImageFileName(const QString &originalPath, int pageNumber);
    
    void connectWindowSignals(PictureWindow *window);
    QRect convertScreenToCanvasRect(const QRect &screenRect) const;

    // Data members
    InkCanvas *canvas;
    QList<PictureWindow*> currentWindows;
    QMap<int, QList<PictureWindow*>> pageWindows;
    QList<int> pageAccessOrder; // ✅ LRU tracking: most recently accessed pages at the end
    QSet<int> cacheUpdatedPages; // ✅ Pages whose cache has been updated (to avoid redundant cloning)
    QList<PictureWindow*> combinedTempWindows; // ✅ Track temporary combined windows for cleanup
    QList<PictureWindow*> orphanedCacheWindows; // ✅ Track orphaned cache windows awaiting cleanup
    bool selectionMode;
    bool isDestroying; // ✅ Flag to prevent operations during destruction
    
    // ✅ PERFORMANCE: Throttle position updates during pan/zoom to avoid lag
    QTimer *positionUpdateThrottleTimer; // Throttles position updates during scrolling
    bool hasPendingPositionUpdate; // Flag to track if an update is pending
};

#endif // PICTUREWINDOWMANAGER_H


