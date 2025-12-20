#include "PictureWindow.h"
#include "InkCanvas.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVariantMap>
#include <QApplication>
#include <QStyle>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QTouchEvent>
#include <QTime>

PictureWindow::PictureWindow(const QRect &rect, const QString &imagePath, QWidget *parent)
    : QWidget(parent), imagePath(imagePath), canvasRect(rect), isUpdatingPosition(false),
      lastScaledSize(QSize()), dragging(false), resizing(false), isUserInteracting(false), currentResizeHandle(None),
      maintainAspectRatio(true), aspectRatio(1.0), editMode(false), longPressTimer(new QTimer(this)),
      wasLongPress(false), updateThrottleTimer(new QTimer(this)), hasPendingUpdate(false), frameOnlyMode(false)
{
    setupUI();
    applyStyle();
    loadImage();
    
    // Set up long press timer for edit mode
    longPressTimer->setSingleShot(true);
    longPressTimer->setInterval(500); // 500ms long press
    connect(longPressTimer, &QTimer::timeout, this, &PictureWindow::enterEditMode);
    
    // ✅ PERFORMANCE: Set up throttle timer for pan updates
    updateThrottleTimer->setSingleShot(true);
    updateThrottleTimer->setInterval(16); // ~60 FPS maximum update rate
    connect(updateThrottleTimer, &QTimer::timeout, this, [this]() {
        if (hasPendingUpdate) {
            hasPendingUpdate = false;
            updateScreenPositionImmediate();
        }
    });
    
    // Set initial screen position based on canvas coordinates
    updateScreenPositionImmediate();
    
    // Enable mouse tracking for resize handles
    setMouseTracking(true);
    
    // Enable touch events
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    
    // Set window flags for proper behavior
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_DeleteOnClose, false); // We'll handle deletion manually
    
    // Make the widget invisible - it will be rendered by the canvas instead
    setVisible(false);
    
    // Initially allow mouse events for long press detection
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    // Connect to canvas pan/zoom changes to update position
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        connect(inkCanvas, &InkCanvas::panChanged, this, &PictureWindow::updateScreenPosition);
        connect(inkCanvas, &InkCanvas::zoomChanged, this, &PictureWindow::updateScreenPosition);
    }
    
    // Connect to parent widget resize events to handle layout changes
    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }
}

PictureWindow::~PictureWindow() = default;

void PictureWindow::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(0);
    
    // Header with title and delete button
    headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);
    
    titleLabel = new QLabel("Picture", this);
    titleLabel->setStyleSheet("font-weight: bold; color: #333; font-size: 10px;");
    
    deleteButton = new QPushButton("×", this);
    deleteButton->setFixedSize(16, 16);
    deleteButton->setStyleSheet(R"(
        QPushButton {
            background-color: #ff4444;
            color: white;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 10px;
        }
        QPushButton:hover {
            background-color: #ff6666;
        }
        QPushButton:pressed {
            background-color: #cc2222;
        }
    )");
    
    connect(deleteButton, &QPushButton::clicked, this, &PictureWindow::onDeleteClicked);
    
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(deleteButton);
    
    // Image display label
    imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setScaledContents(false); // We'll handle scaling manually
    imageLabel->setStyleSheet("border: none; background: transparent;");
    
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(imageLabel);
}

void PictureWindow::applyStyle() {
    // Detect dark mode using the same method as MainWindow
    bool isDarkMode = palette().color(QPalette::Window).lightness() < 128;
    
    QString backgroundColor = isDarkMode ? "#2b2b2b" : "white";
    QString borderColor = isDarkMode ? "#555555" : "#cccccc";
    QString headerBackgroundColor = isDarkMode ? "#3c3c3c" : "#f0f0f0";
    QString focusBorderColor = isDarkMode ? "#6ca9dc" : "#4a90e2";
    QString editModeColor = "#ff6600"; // Bright orange for edit mode (same for both themes)
    
    QString finalBorderColor = editMode ? editModeColor : borderColor;
    int borderWidth = editMode ? 4 : 2; // Make edit mode border even thicker
    
    setStyleSheet(QString(R"(
        PictureWindow {
            background-color: %1;
            border: %2px solid %3;
            border-radius: 4px;
        }
        PictureWindow:focus {
            border-color: %4;
        }
    )").arg(backgroundColor).arg(borderWidth).arg(finalBorderColor, focusBorderColor));
    
    // Style the header
    if (headerLayout && headerLayout->parentWidget()) {
        headerLayout->parentWidget()->setStyleSheet(QString(R"(
            background-color: %1;
            border-bottom: 1px solid %2;
        )").arg(headerBackgroundColor, borderColor));
    }
}

void PictureWindow::loadImage() {
    // qDebug() << "PictureWindow::loadImage() called";
    // qDebug() << "  Image path:" << imagePath;
    
    if (imagePath.isEmpty()) {
        // qDebug() << "  ERROR: Image path is empty!";
        return;
    }
    
    QFileInfo fileInfo(imagePath);
    // qDebug() << "  File exists:" << fileInfo.exists();
    // qDebug() << "  File size:" << fileInfo.size() << "bytes";
    // qDebug() << "  File readable:" << fileInfo.isReadable();
    
    originalPixmap = QPixmap(imagePath);
    // qDebug() << "  Pixmap loaded. Is null:" << originalPixmap.isNull();
    
    if (originalPixmap.isNull()) {
        // qDebug() << "  ERROR: Failed to load pixmap!";
        // Show error placeholder
        imageLabel->setText(tr("Failed to load image"));
        imageLabel->setStyleSheet("color: red; font-size: 12px;");
        return;
    }
    
    // qDebug() << "  Original pixmap size:" << originalPixmap.size();
    
    // Calculate aspect ratio
    aspectRatio = static_cast<double>(originalPixmap.width()) / originalPixmap.height();
    // qDebug() << "  Aspect ratio:" << aspectRatio;
    
    // Calculate optimal size for the image (constrain to reasonable limits while preserving aspect ratio)
    QSize imageSize = originalPixmap.size();
    int maxWidth = 400;
    int maxHeight = 300;
    
    // Calculate the optimal size that fits within max bounds while preserving aspect ratio
    QSize optimalSize = imageSize;
    if (imageSize.width() > maxWidth || imageSize.height() > maxHeight) {
        // Scale down proportionally to fit within max bounds
        optimalSize = imageSize.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio);
    }
    
    // Account for header height (32px) and margins
    int headerHeight = 32; // Updated header height
    int margins = 4; // 2px margin on each side
    QSize windowSize(optimalSize.width() + margins, optimalSize.height() + headerHeight + margins);
    
    // Always ensure the canvas rect has the correct aspect ratio
    if (canvasRect.size().isEmpty() || canvasRect.size() == QSize(200, 150)) {
        // New picture - use optimal size
        canvasRect.setSize(windowSize);
        // qDebug() << "  Set optimal window size preserving aspect ratio:" << windowSize;
        // qDebug() << "  Original image size:" << imageSize << "Optimal size:" << optimalSize;
    } else {
        // Existing picture - verify and correct aspect ratio if needed
        QSize currentSize = canvasRect.size();
        int headerHeight = 32;
        int margins = 4;
        
        // Calculate the actual image area size (excluding header and margins)
        QSize currentImageArea(currentSize.width() - margins, currentSize.height() - headerHeight - margins);
        
        // Check if current aspect ratio matches image aspect ratio (with small tolerance)
        double currentAspectRatio = static_cast<double>(currentImageArea.width()) / currentImageArea.height();
        double tolerance = 0.01; // 1% tolerance
        
        if (qAbs(currentAspectRatio - aspectRatio) > tolerance) {
            // Aspect ratio is wrong - correct it while keeping similar size
            int targetHeight = currentImageArea.height();
            int correctedWidth = static_cast<int>(targetHeight * aspectRatio);
            
            // If corrected width is too different, adjust height instead
            if (qAbs(correctedWidth - currentImageArea.width()) > currentImageArea.width() * 0.5) {
                int targetWidth = currentImageArea.width();
                int correctedHeight = static_cast<int>(targetWidth / aspectRatio);
                currentImageArea = QSize(targetWidth, correctedHeight);
            } else {
                currentImageArea = QSize(correctedWidth, targetHeight);
            }
            
            // Update canvas rect with corrected aspect ratio
            QSize correctedWindowSize(currentImageArea.width() + margins, currentImageArea.height() + headerHeight + margins);
            canvasRect.setSize(correctedWindowSize);
            
            // Invalidate cache since size changed
            invalidateCache();
            
            // qDebug() << "  Corrected aspect ratio from" << currentAspectRatio << "to" << aspectRatio;
            // qDebug() << "  New size:" << correctedWindowSize;
        } else {
            // qDebug() << "  Keeping existing canvas rect size (aspect ratio is correct):" << canvasRect.size();
        }
    }
    
    // Scale the image to fit the image label area
    QSize availableSize = QSize(imageSize.width(), imageSize.height());
    // qDebug() << "  Available size for image:" << availableSize;
    
    // Handle high DPI scaling properly
    qreal devicePixelRatio = devicePixelRatioF();
    QSize scaledSize = availableSize * devicePixelRatio;
    
    scaledPixmap = originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaledPixmap.setDevicePixelRatio(devicePixelRatio);
    // qDebug() << "  Scaled pixmap size:" << scaledPixmap.size() << "with DPR:" << devicePixelRatio;
    
    imageLabel->setPixmap(scaledPixmap);
    // qDebug() << "  Pixmap set to image label";
    
    // Update title to show filename
    titleLabel->setText(fileInfo.baseName());
    // qDebug() << "  Title updated to:" << fileInfo.baseName();
    
    // Update screen position with the new size
    updateScreenPositionImmediate();
}

QString PictureWindow::getImagePath() const {
    return imagePath;
}

void PictureWindow::setImagePath(const QString &imagePath) {
    this->imagePath = imagePath;
    loadImage();
}

QRect PictureWindow::getCanvasRect() const {
    return canvasRect;
}

void PictureWindow::setCanvasRect(const QRect &rect) {
    if (canvasRect != rect) {
        invalidateCache(); // Cache is invalid due to size/position change
    }
    canvasRect = rect;
    // ✅ PERFORMANCE: Only update screen position when not in preview mode
    // During drag/resize, the canvas handles positioning via outline preview
    updateScreenPosition();
}

void PictureWindow::updateScreenPosition() {
    // ✅ PERFORMANCE: Throttle updates during frequent pan operations
    if (!updateThrottleTimer->isActive()) {
        // If not currently throttling, update immediately and start throttling
        updateScreenPositionImmediate();
        updateThrottleTimer->start();
    } else {
        // If throttling is active, just mark that we have a pending update
        hasPendingUpdate = true;
    }
}

void PictureWindow::updateScreenPositionImmediate() {
    // Prevent recursive updates
    if (isUpdatingPosition) return;
    isUpdatingPosition = true;
    
    // Get the canvas parent to access coordinate conversion methods
    if (QWidget *canvas = parentWidget()) {
        // Try to cast to InkCanvas to use the coordinate conversion methods
        if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(canvas)) {
            QRect screenRect = inkCanvas->mapCanvasToWidget(canvasRect);
            setGeometry(screenRect);
        } else {
            // Fallback: use canvas coordinates directly
            setGeometry(canvasRect);
        }
    } else {
        // No parent, use canvas coordinates directly
        setGeometry(canvasRect);
    }
    
    // ✅ PERFORMANCE OPTIMIZATION: Only rescale image if size actually changed
    // This prevents expensive image rescaling during every pan operation
    if (imageLabel && !originalPixmap.isNull()) {
        QSize availableSize = imageLabel->size();
        if (!availableSize.isEmpty() && availableSize != lastScaledSize) {
            // Handle high DPI scaling properly
            qreal devicePixelRatio = devicePixelRatioF();
            QSize scaledSize = availableSize * devicePixelRatio;
            
            scaledPixmap = originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaledPixmap.setDevicePixelRatio(devicePixelRatio);
            imageLabel->setPixmap(scaledPixmap);
            
            // Remember the size we scaled for
            lastScaledSize = availableSize;
        }
    }
    
    isUpdatingPosition = false;
}

QVariantMap PictureWindow::serialize() const {
    QVariantMap data;
    data["canvas_x"] = canvasRect.x();
    data["canvas_y"] = canvasRect.y();
    data["canvas_width"] = canvasRect.width();
    data["canvas_height"] = canvasRect.height();
    // ✅ CRITICAL FIX: Store relative path (filename only) instead of absolute path
    // This ensures the image can be found when the .spn is extracted to different temp folders
    QFileInfo fileInfo(imagePath);
    data["image_path"] = fileInfo.fileName(); // Just the filename, not the full path
    data["maintain_aspect_ratio"] = maintainAspectRatio;
    data["aspect_ratio"] = aspectRatio;
    
    // Add current screen geometry for debugging
    QRect screenGeometry = geometry();
    data["screen_x"] = screenGeometry.x();
    data["screen_y"] = screenGeometry.y();
    data["screen_width"] = screenGeometry.width();
    data["screen_height"] = screenGeometry.height();
    
    // Add canvas size information for debugging and consistency checks
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        QSize canvasSize = inkCanvas->getCanvasSize();
        data["canvas_buffer_width"] = canvasSize.width();
        data["canvas_buffer_height"] = canvasSize.height();
        data["zoom_factor"] = inkCanvas->getZoomFactor();
        QPointF panOffset = inkCanvas->getPanOffset();
        data["pan_x"] = panOffset.x();
        data["pan_y"] = panOffset.y();
    }
    
    // qDebug() << "PictureWindow::serialize() - Canvas rect:" << canvasRect 
             // << "Screen rect:" << screenGeometry;
    
    return data;
}

void PictureWindow::deserialize(const QVariantMap &data) {
    int x = data.value("canvas_x", 0).toInt();
    int y = data.value("canvas_y", 0).toInt();
    int width = data.value("canvas_width", 200).toInt();
    int height = data.value("canvas_height", 150).toInt();
    QString loadedImagePath = data.value("image_path", "").toString();
    maintainAspectRatio = data.value("maintain_aspect_ratio", true).toBool();
    aspectRatio = data.value("aspect_ratio", 1.0).toDouble();
    
    QRect loadedRect = QRect(x, y, width, height);
    
    // qDebug() << "PictureWindow::deserialize() - Loading canvas rect:" << loadedRect
             // << "Image path:" << loadedImagePath;
    
    // Apply bounds checking to loaded coordinates
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        QRect canvasBounds = inkCanvas->getCanvasRect();
        
        // Ensure window stays within canvas bounds
        int maxX = canvasBounds.width() - loadedRect.width();
        int maxY = canvasBounds.height() - loadedRect.height();
        
        loadedRect.setX(qMax(0, qMin(loadedRect.x(), maxX)));
        loadedRect.setY(qMax(0, qMin(loadedRect.y(), maxY)));
        
        // qDebug() << "  Canvas bounds:" << canvasBounds << "Bounds-checked rect:" << loadedRect;
    }
    
    canvasRect = loadedRect;
    
    // Load image first to get the correct aspect ratio
    if (!loadedImagePath.isEmpty()) {
        imagePath = loadedImagePath;
        loadImage();
    }
    
    // Ensure canvas connections are set up
    ensureCanvasConnections();
    
    // Update screen position after everything is set up
    updateScreenPositionImmediate();
    
    // qDebug() << "PictureWindow::deserialize() - Final canvas rect:" << canvasRect
             // << "Final screen rect:" << geometry();
}

bool PictureWindow::isValidForCanvas() const {
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        QRect canvasBounds = inkCanvas->getCanvasRect();
        
        // Check if the window is at least partially within the canvas bounds
        return canvasBounds.intersects(canvasRect);
    }
    
    // If no canvas parent, assume valid
    return true;
}

QString PictureWindow::getCoordinateInfo() const {
    QString info;
    info += QString("Canvas Coordinates: (%1, %2) %3x%4\n")
            .arg(canvasRect.x()).arg(canvasRect.y())
            .arg(canvasRect.width()).arg(canvasRect.height());
    
    QRect screenRect = geometry();
    info += QString("Screen Coordinates: (%1, %2) %3x%4\n")
            .arg(screenRect.x()).arg(screenRect.y())
            .arg(screenRect.width()).arg(screenRect.height());
    
    info += QString("Image Path: %1\n").arg(imagePath);
    info += QString("Aspect Ratio: %1\n").arg(aspectRatio);
    
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        QSize canvasSize = inkCanvas->getCanvasSize();
        info += QString("Canvas Size: %1x%2\n")
                .arg(canvasSize.width()).arg(canvasSize.height());
        
        info += QString("Zoom Factor: %1\n").arg(inkCanvas->getZoomFactor());
        
        QPointF panOffset = inkCanvas->getPanOffset();
        info += QString("Pan Offset: (%1, %2)\n")
                .arg(panOffset.x()).arg(panOffset.y());
        
        info += QString("Valid for Canvas: %1\n").arg(isValidForCanvas() ? "Yes" : "No");
    } else {
        info += "No InkCanvas parent found\n";
    }
    
    return info;
}

void PictureWindow::ensureCanvasConnections() {
    // Connect to canvas pan/zoom changes to update position
    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
        // Disconnect any existing connections to avoid duplicates
        disconnect(inkCanvas, &InkCanvas::panChanged, this, &PictureWindow::updateScreenPosition);
        disconnect(inkCanvas, &InkCanvas::zoomChanged, this, &PictureWindow::updateScreenPosition);
        
        // Reconnect
        connect(inkCanvas, &InkCanvas::panChanged, this, &PictureWindow::updateScreenPosition);
        connect(inkCanvas, &InkCanvas::zoomChanged, this, &PictureWindow::updateScreenPosition);
        
        // Install event filter to handle canvas resize events (for layout changes)
        inkCanvas->installEventFilter(this);
    }
}

void PictureWindow::onDeleteClicked() {
    emit deleteRequested(this);
}

void PictureWindow::enterEditMode() {
    editMode = true;
    wasLongPress = true; // Mark that edit mode was entered via long press
    // qDebug() << "PictureWindow: Entered edit mode via long press - wasLongPress set to true";
    
    invalidateCache(); // Cache is invalid due to edit mode change
    
    // Visual feedback for edit mode
    applyStyle();
    // qDebug() << "PictureWindow: Applied edit mode styling (orange border)";
    
    // Emit signal to disable canvas pan
    emit editModeChanged(this, true);
}

void PictureWindow::exitEditMode() {
    editMode = false;
    wasLongPress = false; // Reset the flag
    // qDebug() << "PictureWindow: Exited edit mode";
    
    invalidateCache(); // Cache is invalid due to edit mode change
    
    // Reset cursor to normal
    setCursor(Qt::ArrowCursor);
    
    // Visual feedback for normal mode
    applyStyle();
    
    // Emit signal to re-enable canvas pan
    emit editModeChanged(this, false);
}

void PictureWindow::forceExitEditMode() {
    if (editMode) {
        exitEditMode();
    }
}

void PictureWindow::setFrameOnlyMode(bool enabled) {
    if (frameOnlyMode == enabled) return;
    
    frameOnlyMode = enabled;
    
    // Invalidate cache when changing frame-only mode
    invalidateCache();
}

bool PictureWindow::isFrameOnlyMode() const {
    return frameOnlyMode;
}

void PictureWindow::renderToCanvas(QPainter &painter, const QRect &targetRect) const {
    if (originalPixmap.isNull()) return;
    
    // ✅ PERFORMANCE: If in frame-only mode (touch panning), draw just the border
    if (frameOnlyMode) {
        // Draw simple border frame for performance
        QPen borderPen(QColor("#888888"), 2); // Gray border
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(targetRect);
        return;
    }
    
    // Check if we can use cached rendering
    bool canUseCache = !cachedRendering.isNull() && 
                      cachedRect == targetRect && 
                      cachedEditMode == editMode;
    
    if (canUseCache) {
        // Use cached rendering for maximum performance - simple draw, no clipping
        painter.drawPixmap(targetRect, cachedRendering);
        return;
    }
    
    // Need to render from scratch - create cache
    cachedRendering = QPixmap(targetRect.size());
    cachedRendering.fill(Qt::transparent);
    cachedRect = targetRect;
    cachedEditMode = editMode;
    
    QPainter cachePainter(&cachedRendering);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate the image area (excluding header and borders)
    QRect imageRect(0, 0, targetRect.width(), targetRect.height());
    int headerHeight = 32; // Match the updated header height
    int margins = 4;
    
    // Adjust for header and margins
    imageRect.adjust(margins/2, headerHeight + margins/2, -margins/2, -margins/2);
    
    // Use the original pixmap and scale it properly for high-quality rendering
    QPixmap renderPixmap = originalPixmap;
    
    // Handle high DPI scaling for crisp rendering
    qreal devicePixelRatio = painter.device()->devicePixelRatioF();
    if (devicePixelRatio > 1.0) {
        // Scale the target size by device pixel ratio for crisp rendering
        QSize highResSize = imageRect.size() * devicePixelRatio;
        renderPixmap = originalPixmap.scaled(highResSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        renderPixmap.setDevicePixelRatio(devicePixelRatio);
    } else {
        // Scale normally for standard DPI
        renderPixmap = originalPixmap.scaled(imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    cachePainter.drawPixmap(imageRect, renderPixmap);
    
    // Always draw border and header (visible when in edit mode, invisible when not)
    if (editMode) {
        // Draw orange border for edit mode
        QPen borderPen(QColor("#ff6600"), 4);
        cachePainter.setPen(borderPen);
        cachePainter.setBrush(Qt::NoBrush);
        cachePainter.drawRect(QRect(0, 0, targetRect.width(), targetRect.height()));
        
        // Draw header background
        QRect headerRect(0, 0, targetRect.width(), headerHeight);
        cachePainter.fillRect(headerRect, QColor("#f0f0f0"));
        
        // Draw title text
        cachePainter.setPen(Qt::black);
        QFont font = cachePainter.font();
        font.setPointSize(9); // Slightly larger font for bigger header
        font.setBold(true);
        cachePainter.setFont(font);
        
        QFileInfo fileInfo(imagePath);
        QString title = fileInfo.baseName();
        cachePainter.drawText(headerRect.adjusted(6, 0, -28, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
        
        // Draw larger delete button
        QRect deleteRect(targetRect.width() - 24, 6, 20, 20); // Larger button
        cachePainter.fillRect(deleteRect, QColor("#ff4444"));
        cachePainter.setPen(Qt::white);
        QFont buttonFont = cachePainter.font();
        buttonFont.setPointSize(12); // Larger font for bigger button
        buttonFont.setBold(true);
        cachePainter.setFont(buttonFont);
        cachePainter.drawText(deleteRect, Qt::AlignCenter, "×");
        
        // Draw resize handles at corners
        cachePainter.setPen(QPen(QColor("#ff6600"), 2));
        cachePainter.setBrush(QBrush(QColor("#ff6600")));
        
        int handleSize = 12; // ✅ TOUCH UX: Larger visual handles for better touch feedback
        // Corner handles - use QRect constructor for drawEllipse
        cachePainter.drawEllipse(QRect(QPoint(-handleSize/2, -handleSize/2), QSize(handleSize, handleSize)));
        cachePainter.drawEllipse(QRect(QPoint(targetRect.width() - handleSize/2, -handleSize/2), QSize(handleSize, handleSize)));
        cachePainter.drawEllipse(QRect(QPoint(-handleSize/2, targetRect.height() - handleSize/2), QSize(handleSize, handleSize)));
        cachePainter.drawEllipse(QRect(QPoint(targetRect.width() - handleSize/2, targetRect.height() - handleSize/2), QSize(handleSize, handleSize)));
    }
    
    // Now draw the cached pixmap to the actual painter - simple, no clipping
    painter.drawPixmap(targetRect, cachedRendering);
}

bool PictureWindow::isClickOnDeleteButton(const QPoint &canvasPos) const {
    if (!editMode) return false;
    
    // Calculate delete button rect in canvas coordinates
    QRect deleteRect(canvasRect.right() - 24, canvasRect.y() + 8, 20, 20); // Larger button for better touch interaction
    
    return deleteRect.contains(canvasPos);
}

PictureWindow::ResizeHandle PictureWindow::getResizeHandleAtCanvasPos(const QPoint &canvasPos) const {
    if (!editMode) return None;
    
    // ✅ TOUCH UX: Much larger handles for excellent touch/pen interaction
    int handleSize = 40; // Increased to 40 pixels for much better touch interaction
    int tolerance = handleSize / 2; // 20 pixel radius around each corner
    
    // Check corner handles with larger hit areas
    if ((canvasPos - canvasRect.topLeft()).manhattanLength() <= tolerance)
        return TopLeft;
    if ((canvasPos - canvasRect.topRight()).manhattanLength() <= tolerance)
        return TopRight;
    if ((canvasPos - canvasRect.bottomLeft()).manhattanLength() <= tolerance)
        return BottomLeft;
    if ((canvasPos - canvasRect.bottomRight()).manhattanLength() <= tolerance)
        return BottomRight;
    
    // Also check edge handles for better usability (but smaller than corners)
    int edgeTolerance = 20; // Increased to 20 pixels from edges for better touch
    
    // Top edge (excluding corner areas)
    if (canvasPos.y() >= canvasRect.top() - edgeTolerance && 
        canvasPos.y() <= canvasRect.top() + edgeTolerance &&
        canvasPos.x() >= canvasRect.left() + tolerance && 
        canvasPos.x() <= canvasRect.right() - tolerance)
        return Top;
    
    // Bottom edge
    if (canvasPos.y() >= canvasRect.bottom() - edgeTolerance && 
        canvasPos.y() <= canvasRect.bottom() + edgeTolerance &&
        canvasPos.x() >= canvasRect.left() + tolerance && 
        canvasPos.x() <= canvasRect.right() - tolerance)
        return Bottom;
    
    // Left edge
    if (canvasPos.x() >= canvasRect.left() - edgeTolerance && 
        canvasPos.x() <= canvasRect.left() + edgeTolerance &&
        canvasPos.y() >= canvasRect.top() + tolerance && 
        canvasPos.y() <= canvasRect.bottom() - tolerance)
        return Left;
    
    // Right edge
    if (canvasPos.x() >= canvasRect.right() - edgeTolerance && 
        canvasPos.x() <= canvasRect.right() + edgeTolerance &&
        canvasPos.y() >= canvasRect.top() + tolerance && 
        canvasPos.y() <= canvasRect.bottom() - tolerance)
        return Right;
    
    return None;
}

bool PictureWindow::isClickOnHeader(const QPoint &canvasPos) const {
    if (!editMode) return false;
    
    int headerHeight = 32; // Increased from 24 to 32 for better touch/pen interaction
    QRect headerRect(canvasRect.x(), canvasRect.y(), canvasRect.width(), headerHeight);
    
    return headerRect.contains(canvasPos);
}

bool PictureWindow::isClickOnPictureBody(const QPoint &canvasPos) const {
    if (!editMode) return false;
    
    // ✅ TOUCH UX: Make entire picture body draggable (except corners and delete button)
    
    // First check if click is within the picture window bounds
    if (!canvasRect.contains(canvasPos)) {
        return false;
    }
    
    // Exclude corner resize handles (larger touch areas)
    int handleSize = 40;
    int tolerance = handleSize / 2; // 20 pixel radius
    
    if ((canvasPos - canvasRect.topLeft()).manhattanLength() <= tolerance ||
        (canvasPos - canvasRect.topRight()).manhattanLength() <= tolerance ||
        (canvasPos - canvasRect.bottomLeft()).manhattanLength() <= tolerance ||
        (canvasPos - canvasRect.bottomRight()).manhattanLength() <= tolerance) {
        return false; // Click is on a corner handle
    }
    
    // Exclude delete button area
    QRect deleteRect(canvasRect.right() - 24, canvasRect.y() + 8, 20, 20);
    if (deleteRect.contains(canvasPos)) {
        return false; // Click is on delete button
    }
    
    // Exclude edge resize handles (but with smaller tolerance than corners)
    int edgeTolerance = 20;
    
    // Check if click is on edge handles
    bool onTopEdge = (canvasPos.y() >= canvasRect.top() - edgeTolerance && 
                      canvasPos.y() <= canvasRect.top() + edgeTolerance &&
                      canvasPos.x() >= canvasRect.left() + tolerance && 
                      canvasPos.x() <= canvasRect.right() - tolerance);
    
    bool onBottomEdge = (canvasPos.y() >= canvasRect.bottom() - edgeTolerance && 
                         canvasPos.y() <= canvasRect.bottom() + edgeTolerance &&
                         canvasPos.x() >= canvasRect.left() + tolerance && 
                         canvasPos.x() <= canvasRect.right() - tolerance);
    
    bool onLeftEdge = (canvasPos.x() >= canvasRect.left() - edgeTolerance && 
                       canvasPos.x() <= canvasRect.left() + edgeTolerance &&
                       canvasPos.y() >= canvasRect.top() + tolerance && 
                       canvasPos.y() <= canvasRect.bottom() - tolerance);
    
    bool onRightEdge = (canvasPos.x() >= canvasRect.right() - edgeTolerance && 
                        canvasPos.x() <= canvasRect.right() + edgeTolerance &&
                        canvasPos.y() >= canvasRect.top() + tolerance && 
                        canvasPos.y() <= canvasRect.bottom() - tolerance);
    
    if (onTopEdge || onBottomEdge || onLeftEdge || onRightEdge) {
        return false; // Click is on an edge handle
    }
    
    // If we get here, click is on the picture body - perfect for dragging!
    return true;
}

void PictureWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Emit signal to indicate window interaction
        emit windowInteracted(this);
        
        // Handle double-click to exit edit mode
        const int doubleClickInterval = 500; // 500ms for double-click
        
        if (editMode && lastClickTime.isValid() && lastClickTime.msecsTo(QTime::currentTime()) < doubleClickInterval) {
            // Double-click detected in edit mode - exit edit mode
            // qDebug() << "PictureWindow: Double-click detected, exiting edit mode";
            exitEditMode();
            wasLongPress = false;
            lastClickTime = QTime(); // Reset
            return;
        }
        lastClickTime = QTime::currentTime();
        
        // Start long press timer for edit mode (only if not already in edit mode)
        if (!editMode) {
            longPressTimer->start();
            longPressStartPos = event->pos();
            
            // If not in edit mode, pass the event to canvas for drawing
            // Convert to canvas coordinates and forward
            if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
                QPoint canvasPos = mapToParent(event->pos());
                QMouseEvent *forwardedEvent = new QMouseEvent(
                    event->type(),
                    canvasPos,
                    event->globalPosition(),
                    event->button(),
                    event->buttons(),
                    event->modifiers()
                );
                QApplication::postEvent(inkCanvas, forwardedEvent);
            }
            return; // Don't process further in picture window
        }
        
        // Only allow interaction in edit mode
        if (editMode) {
            ResizeHandle handle = getResizeHandle(event->pos());
            
            if (handle != None) {
                // Start resizing
                resizing = true;
                isUserInteracting = true;
                currentResizeHandle = handle;
                resizeStartPosition = event->globalPosition().toPoint();
                resizeStartRect = geometry();
            } else if (event->pos().y() < 24) { // Header area
                // Start dragging
                dragging = true;
                isUserInteracting = true;
                dragStartPosition = event->globalPosition().toPoint();
                windowStartPosition = pos();
            }
        }
    }
    
    QWidget::mousePressEvent(event);
}

void PictureWindow::mouseMoveEvent(QMouseEvent *event) {
    // Cancel long press if mouse moves too much
    if (longPressTimer->isActive()) {
        QPoint delta = event->pos() - longPressStartPos;
        if (delta.manhattanLength() > 10) { // 10 pixel tolerance
            longPressTimer->stop();
            wasLongPress = false; // Reset since long press was cancelled
            
            // Forward the move event to canvas for drawing
            if (!editMode) {
                if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
                    QPoint canvasPos = mapToParent(event->pos());
                    QMouseEvent *forwardedEvent = new QMouseEvent(
                        event->type(),
                        canvasPos,
                        event->globalPosition(),
                        event->button(),
                        event->buttons(),
                        event->modifiers()
                    );
                    QApplication::postEvent(inkCanvas, forwardedEvent);
                }
                return;
            }
        }
    }
    
    if (resizing && editMode) {
        QPoint delta = event->globalPosition().toPoint() - resizeStartPosition;
        QRect newRect = resizeStartRect;
        
        // Apply resize based on the stored handle
        switch (currentResizeHandle) {
            case TopLeft:
                newRect.setTopLeft(newRect.topLeft() + delta);
                break;
            case TopRight:
                newRect.setTopRight(newRect.topRight() + delta);
                break;
            case BottomLeft:
                newRect.setBottomLeft(newRect.bottomLeft() + delta);
                break;
            case BottomRight:
                newRect.setBottomRight(newRect.bottomRight() + delta);
                break;
            case Top:
                newRect.setTop(newRect.top() + delta.y());
                break;
            case Bottom:
                newRect.setBottom(newRect.bottom() + delta.y());
                break;
            case Left:
                newRect.setLeft(newRect.left() + delta.x());
                break;
            case Right:
                newRect.setRight(newRect.right() + delta.x());
                break;
            default:
                break;
        }
        
        // Maintain aspect ratio if enabled
        if (maintainAspectRatio && aspectRatio > 0) {
            QSize newSize = newRect.size();
            int headerHeight = 24; // Account for header
            int availableHeight = newSize.height() - headerHeight;
            
            // Calculate new dimensions based on aspect ratio
            int newWidth = static_cast<int>(availableHeight * aspectRatio);
            int newHeight = static_cast<int>(newWidth / aspectRatio) + headerHeight;
            
            // Apply the constrained size
            newRect.setSize(QSize(newWidth, newHeight));
        }
        
        // Enforce minimum size
        newRect.setSize(newRect.size().expandedTo(QSize(100, 80)));
        
        // Keep resized window within canvas bounds
        if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
            // Convert to canvas coordinates to check bounds
            QRect tempCanvasRect = inkCanvas->mapWidgetToCanvas(newRect);
            QRect canvasBounds = inkCanvas->getCanvasRect();
            
            // Apply canvas bounds constraints
            int maxCanvasX = canvasBounds.width() - tempCanvasRect.width();
            int maxCanvasY = canvasBounds.height() - tempCanvasRect.height();
            
            tempCanvasRect.setX(qMax(0, qMin(tempCanvasRect.x(), maxCanvasX)));
            tempCanvasRect.setY(qMax(0, qMin(tempCanvasRect.y(), maxCanvasY)));
            
            // Also ensure the window doesn't get resized beyond canvas bounds
            if (tempCanvasRect.right() > canvasBounds.width()) {
                tempCanvasRect.setWidth(canvasBounds.width() - tempCanvasRect.x());
            }
            if (tempCanvasRect.bottom() > canvasBounds.height()) {
                tempCanvasRect.setHeight(canvasBounds.height() - tempCanvasRect.y());
            }
            
            // Convert back to screen coordinates for the constrained geometry
            newRect = inkCanvas->mapCanvasToWidget(tempCanvasRect);
        }
        
        // Update the widget geometry directly during resizing
        setGeometry(newRect);
        
        // Convert screen coordinates back to canvas coordinates
        convertScreenToCanvasRect(newRect);
        emit windowResized(this);
    } else if (dragging && editMode) {
        QPoint delta = event->globalPosition().toPoint() - dragStartPosition;
        QPoint newPos = windowStartPosition + delta;
        
        // Keep window within canvas bounds
        if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
            // Convert current position to canvas coordinates to check bounds
            QRect tempScreenRect(newPos, size());
            QRect tempCanvasRect = inkCanvas->mapWidgetToCanvas(tempScreenRect);
            QRect canvasBounds = inkCanvas->getCanvasRect();
            
            // Apply canvas bounds constraints
            int maxCanvasX = canvasBounds.width() - tempCanvasRect.width();
            int maxCanvasY = canvasBounds.height() - tempCanvasRect.height();
            
            tempCanvasRect.setX(qMax(0, qMin(tempCanvasRect.x(), maxCanvasX)));
            tempCanvasRect.setY(qMax(0, qMin(tempCanvasRect.y(), maxCanvasY)));
            
            // Convert back to screen coordinates for the constrained position
            QRect constrainedScreenRect = inkCanvas->mapCanvasToWidget(tempCanvasRect);
            newPos = constrainedScreenRect.topLeft();
        } else {
            // Fallback: Keep window within parent bounds
            if (parentWidget()) {
                QRect parentRect = parentWidget()->rect();
                newPos.setX(qMax(0, qMin(newPos.x(), parentRect.width() - width())));
                newPos.setY(qMax(0, qMin(newPos.y(), parentRect.height() - height())));
            }
        }
        
        // Update the widget position directly during dragging
        move(newPos);
        
        // Convert screen position back to canvas coordinates
        QRect newScreenRect(newPos, size());
        convertScreenToCanvasRect(newScreenRect);
        emit windowMoved(this);
    } else if (editMode) {
        // Update cursor for resize handles only in edit mode
        updateCursor(event->pos());
    } else {
        // Reset cursor when not in edit mode
        setCursor(Qt::ArrowCursor);
    }
    
    QWidget::mouseMoveEvent(event);
}

void PictureWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Stop long press timer
        longPressTimer->stop();
        
        // Forward mouse release to canvas if not in edit mode and not a long press
        if (!editMode && !wasLongPress) {
            if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
                QPoint canvasPos = mapToParent(event->pos());
                QMouseEvent *forwardedEvent = new QMouseEvent(
                    event->type(),
                    canvasPos,
                    event->globalPosition(),
                    event->button(),
                    event->buttons(),
                    event->modifiers()
                );
                QApplication::postEvent(inkCanvas, forwardedEvent);
            }
        }
        
        // Only exit edit mode if we were just clicking (not dragging/resizing) 
        // AND the click was a short tap (not a long press that entered edit mode)
        // qDebug() << "PictureWindow::mouseReleaseEvent - editMode:" << editMode 
                 // << "resizing:" << resizing << "dragging:" << dragging 
                 // << "wasLongPress:" << wasLongPress;
        
        if (editMode && !resizing && !dragging && !wasLongPress) {
            // qDebug() << "PictureWindow: Exiting edit mode due to short tap";
            exitEditMode();
        }
        
        // Reset the long press flag
        wasLongPress = false;
        
        resizing = false;
        dragging = false;
        isUserInteracting = false;
        currentResizeHandle = None;
    }
    
    QWidget::mouseReleaseEvent(event);
}

bool PictureWindow::event(QEvent *event) {
    switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
            handleTouchEvent(touchEvent);
            return true;
        }
        default:
            break;
    }
    return QWidget::event(event);
}

void PictureWindow::handleTouchEvent(QTouchEvent *event) {
    const QList<QEventPoint> &touchPoints = event->points();
    
    if (touchPoints.isEmpty()) return;
    
    QTouchEvent::TouchPoint primaryTouch = touchPoints.first();
    QPointF localPos = mapFromGlobal(primaryTouch.globalPosition().toPoint());
    
    switch (event->type()) {
        case QEvent::TouchBegin: {
            // Start long press timer for edit mode
            if (!editMode) {
                longPressTimer->start();
                longPressStartPos = localPos.toPoint();
            }
            
            // Only handle touch in edit mode
            if (editMode) {
                touchStartPos = localPos.toPoint();
                touchStartGlobalPos = primaryTouch.globalPosition().toPoint();
                
                ResizeHandle handle = getResizeHandle(touchStartPos);
                if (handle != None) {
                    // Start resizing with touch
                    resizing = true;
                    isUserInteracting = true;
                    currentResizeHandle = handle;
                    resizeStartPosition = touchStartGlobalPos;
                    resizeStartRect = geometry();
                } else if (touchStartPos.y() < 24) { // Header area
                    // Start dragging with touch
                    dragging = true;
                    isUserInteracting = true;
                    dragStartPosition = touchStartGlobalPos;
                    windowStartPosition = pos();
                }
            }
            break;
        }
        case QEvent::TouchUpdate: {
            // Cancel long press if touch moves too much
            if (longPressTimer->isActive()) {
                QPoint delta = localPos.toPoint() - longPressStartPos;
                if (delta.manhattanLength() > 15) { // Slightly higher tolerance for touch
                    longPressTimer->stop();
                    wasLongPress = false; // Reset since long press was cancelled
                }
            }
            
            if (editMode) {
                QPoint currentGlobalPos = primaryTouch.globalPosition().toPoint();
                
                if (resizing) {
                    // Handle touch resize (similar to mouse resize but with touch-friendly behavior)
                    QPoint delta = currentGlobalPos - resizeStartPosition;
                    QRect newRect = resizeStartRect;
                    
                    // Apply resize based on handle (simplified for touch)
                    switch (currentResizeHandle) {
                        case BottomRight:
                            newRect.setBottomRight(newRect.bottomRight() + delta);
                            break;
                        case TopLeft:
                            newRect.setTopLeft(newRect.topLeft() + delta);
                            break;
                        case TopRight:
                            newRect.setTopRight(newRect.topRight() + delta);
                            break;
                        case BottomLeft:
                            newRect.setBottomLeft(newRect.bottomLeft() + delta);
                            break;
                        default:
                            // For edge handles, treat as corner resize for easier touch interaction
                            newRect.setBottomRight(newRect.bottomRight() + delta);
                            break;
                    }
                    
                    // Maintain aspect ratio
                    if (maintainAspectRatio && aspectRatio > 0) {
                        QSize newSize = newRect.size();
                        int headerHeight = 24;
                        int availableHeight = newSize.height() - headerHeight;
                        
                        int newWidth = static_cast<int>(availableHeight * aspectRatio);
                        int newHeight = static_cast<int>(newWidth / aspectRatio) + headerHeight;
                        
                        newRect.setSize(QSize(newWidth, newHeight));
                    }
                    
                    // Enforce minimum size
                    newRect.setSize(newRect.size().expandedTo(QSize(100, 80)));
                    
                    // Apply bounds checking
                    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
                        QRect tempCanvasRect = inkCanvas->mapWidgetToCanvas(newRect);
                        QRect canvasBounds = inkCanvas->getCanvasRect();
                        
                        int maxCanvasX = canvasBounds.width() - tempCanvasRect.width();
                        int maxCanvasY = canvasBounds.height() - tempCanvasRect.height();
                        
                        tempCanvasRect.setX(qMax(0, qMin(tempCanvasRect.x(), maxCanvasX)));
                        tempCanvasRect.setY(qMax(0, qMin(tempCanvasRect.y(), maxCanvasY)));
                        
                        newRect = inkCanvas->mapCanvasToWidget(tempCanvasRect);
                    }
                    
                    setGeometry(newRect);
                    convertScreenToCanvasRect(newRect);
                    emit windowResized(this);
                    
                } else if (dragging) {
                    // Handle touch drag
                    QPoint delta = currentGlobalPos - dragStartPosition;
                    QPoint newPos = windowStartPosition + delta;
                    
                    // Apply bounds checking for dragging
                    if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(parentWidget())) {
                        QRect tempScreenRect(newPos, size());
                        QRect tempCanvasRect = inkCanvas->mapWidgetToCanvas(tempScreenRect);
                        QRect canvasBounds = inkCanvas->getCanvasRect();
                        
                        int maxCanvasX = canvasBounds.width() - tempCanvasRect.width();
                        int maxCanvasY = canvasBounds.height() - tempCanvasRect.height();
                        
                        tempCanvasRect.setX(qMax(0, qMin(tempCanvasRect.x(), maxCanvasX)));
                        tempCanvasRect.setY(qMax(0, qMin(tempCanvasRect.y(), maxCanvasY)));
                        
                        QRect constrainedScreenRect = inkCanvas->mapCanvasToWidget(tempCanvasRect);
                        newPos = constrainedScreenRect.topLeft();
                    }
                    
                    move(newPos);
                    QRect newScreenRect(newPos, size());
                    convertScreenToCanvasRect(newScreenRect);
                    emit windowMoved(this);
                }
            }
            break;
        }
        case QEvent::TouchEnd: {
            // Stop long press timer
            longPressTimer->stop();
            
            // Only exit edit mode if we were just tapping (not dragging/resizing) 
            // AND the tap was a short tap (not a long press that entered edit mode)
            if (editMode && !resizing && !dragging && !wasLongPress) {
                exitEditMode();
            }
            
            // Reset the long press flag
            wasLongPress = false;
            
            resizing = false;
            dragging = false;
            isUserInteracting = false;
            currentResizeHandle = None;
            break;
        }
        default:
            break;
    }
}

void PictureWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    
    // Update scaled image when window is resized
    if (imageLabel && !originalPixmap.isNull()) {
        QSize availableSize = imageLabel->size();
        if (!availableSize.isEmpty()) {
            // Handle high DPI scaling properly
            qreal devicePixelRatio = devicePixelRatioF();
            QSize scaledSize = availableSize * devicePixelRatio;
            
            scaledPixmap = originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaledPixmap.setDevicePixelRatio(devicePixelRatio);
            imageLabel->setPixmap(scaledPixmap);
        }
    }
    
    // Emit windowResized if this is a user-initiated resize or if we're not updating position due to canvas changes
    if (isUserInteracting || !isUpdatingPosition) {
        emit windowResized(this);
    }
}

void PictureWindow::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    
    // Draw resize handles
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor handleColor = hasFocus() ? QColor(74, 144, 226) : QColor(180, 180, 180);
    painter.setPen(QPen(handleColor, 2));
    painter.setBrush(QBrush(handleColor));
    
    // Draw corner handles
    int handleSize = 6;
    painter.drawEllipse(0, 0, handleSize, handleSize); // Top-left
    painter.drawEllipse(width() - handleSize, 0, handleSize, handleSize); // Top-right
    painter.drawEllipse(0, height() - handleSize, handleSize, handleSize); // Bottom-left
    painter.drawEllipse(width() - handleSize, height() - handleSize, handleSize, handleSize); // Bottom-right
}

PictureWindow::ResizeHandle PictureWindow::getResizeHandle(const QPoint &pos) const {
    const int handleSize = 8;
    const QRect rect = this->rect();
    
    // Check corner handles first
    if (QRect(0, 0, handleSize, handleSize).contains(pos))
        return TopLeft;
    if (QRect(rect.width() - handleSize, 0, handleSize, handleSize).contains(pos))
        return TopRight;
    if (QRect(0, rect.height() - handleSize, handleSize, handleSize).contains(pos))
        return BottomLeft;
    if (QRect(rect.width() - handleSize, rect.height() - handleSize, handleSize, handleSize).contains(pos))
        return BottomRight;
    
    // Check edge handles
    if (QRect(0, 0, rect.width(), handleSize).contains(pos))
        return Top;
    if (QRect(0, rect.height() - handleSize, rect.width(), handleSize).contains(pos))
        return Bottom;
    if (QRect(0, 0, handleSize, rect.height()).contains(pos))
        return Left;
    if (QRect(rect.width() - handleSize, 0, handleSize, rect.height()).contains(pos))
        return Right;
    
    return None;
}

void PictureWindow::updateCursor(const QPoint &pos) {
    ResizeHandle handle = getResizeHandle(pos);
    
    switch (handle) {
        case TopLeft:
        case BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TopRight:
        case BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Top:
        case Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Left:
        case Right:
            setCursor(Qt::SizeHorCursor);
            break;
        default:
            if (pos.y() < 24) { // Header area
                setCursor(Qt::SizeAllCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            break;
    }
}

void PictureWindow::convertScreenToCanvasRect(const QRect &screenRect) {
    // Get the canvas parent to access coordinate conversion methods
    if (QWidget *canvas = parentWidget()) {
        // Try to cast to InkCanvas to use the coordinate conversion methods
        if (InkCanvas *inkCanvas = qobject_cast<InkCanvas*>(canvas)) {
            QRect newCanvasRect = inkCanvas->mapWidgetToCanvas(screenRect);
            
            // Store the converted canvas coordinates
            canvasRect = newCanvasRect;
        } else {
            // Fallback: use screen coordinates directly
            canvasRect = screenRect;
        }
    } else {
        // No parent, use screen coordinates directly
        canvasRect = screenRect;
    }
}

void PictureWindow::clearRenderCache() {
    // ✅ MEMORY LEAK FIX: Explicitly clear and release cached rendering pixmap
    // This is critical for preventing memory accumulation during inertia scrolling
    // where multiple window clones are created and deleted rapidly
    cachedRendering = QPixmap();
    cachedRect = QRect();
    cachedEditMode = false;
}
void PictureWindow::invalidateCache() const {
    // ✅ MEMORY LEAK FIX: Explicitly release cached pixmap data
    // During inertia scrolling, large cached pixmaps must be freed immediately
    if (!cachedRendering.isNull()) {
        cachedRendering = QPixmap();  // Release the pixmap
    }
    cachedRect = QRect();
    cachedEditMode = false;
}
