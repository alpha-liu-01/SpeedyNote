#include "ExistingPictureDialog.h"
#include "InkCanvas.h"
#include "PictureWindowManager.h"
#include "PictureWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// ============================================================================
// PictureThumbnail Implementation
// ============================================================================

PictureThumbnail::PictureThumbnail(const QString &imagePath, int pageNumber, QWidget *parent)
    : QWidget(parent)
    , imagePath(imagePath)
    , pageNumber(pageNumber)
{
    setFixedSize(THUMB_SIZE + 10, THUMB_SIZE + 25);
    setCursor(Qt::PointingHandCursor);
    
    // ✅ Make background transparent
    setAttribute(Qt::WA_TranslucentBackground);
    
    // Load and scale the image
    QPixmap originalPixmap(imagePath);
    if (!originalPixmap.isNull()) {
        thumbnail = originalPixmap.scaled(THUMB_SIZE, THUMB_SIZE, 
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

void PictureThumbnail::setSelected(bool selected) {
    this->selected = selected;
    update();
}

void PictureThumbnail::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background - only show for selected/hovered states, transparent otherwise
    if (selected) {
        painter.fillRect(rect(), QColor(70, 130, 220));
        painter.setPen(QPen(QColor(50, 100, 200), 3));
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    } else if (hovered) {
        painter.fillRect(rect(), QColor(230, 230, 230, 150)); // Semi-transparent hover
        painter.setPen(QPen(QColor(180, 180, 180), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    // No background when not selected/hovered - transparent
    
    // Draw thumbnail centered
    if (!thumbnail.isNull()) {
        int x = (width() - thumbnail.width()) / 2;
        int y = 5;
        painter.drawPixmap(x, y, thumbnail);
    } else {
        // Placeholder for missing image
        painter.setPen(Qt::gray);
        painter.drawText(QRect(5, 5, THUMB_SIZE, THUMB_SIZE), 
            Qt::AlignCenter, tr("No\nImage"));
    }
    
    // Page number label at bottom
    QColor textColor = selected ? Qt::white : Qt::black;
    painter.setPen(textColor);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    painter.drawText(QRect(0, THUMB_SIZE + 5, width(), 20), 
        Qt::AlignCenter, tr("Page %1").arg(pageNumber + 1));
}

void PictureThumbnail::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(this);
    }
    QWidget::mousePressEvent(event);
}

void PictureThumbnail::enterEvent(QEnterEvent *) {
    hovered = true;
    update();
}

void PictureThumbnail::leaveEvent(QEvent *) {
    hovered = false;
    update();
}

// ============================================================================
// ExistingPictureDialog Implementation
// ============================================================================

ExistingPictureDialog::ExistingPictureDialog(InkCanvas *canvas, int currentPage, QWidget *parent)
    : QDialog(parent)
    , canvas(canvas)
    , currentPage(currentPage)
{
    setWindowTitle(tr("Select Existing Picture"));
    setModal(true);
    setMinimumSize(500, 400);
    resize(600, 500);
    
    pictureManager = canvas ? canvas->getPictureManager() : nullptr;
    // Use PDF pages if available, otherwise use a reasonable default
    // For blank notebooks, we still want to allow browsing existing pictures
    int pdfPages = canvas ? canvas->getTotalPdfPages() : 0;
    totalPages = qMax(pdfPages, currentPage + 100); // Allow range beyond current page
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    
    // Page range controls
    QHBoxLayout *rangeLayout = new QHBoxLayout();
    
    QLabel *rangeLabel = new QLabel(tr("Page range:"), this);
    rangeLayout->addWidget(rangeLabel);
    
    fromPageSpinBox = new QSpinBox(this);
    fromPageSpinBox->setMinimum(1);
    fromPageSpinBox->setMaximum(totalPages);
    fromPageSpinBox->setValue(qMax(1, currentPage + 1 - DEFAULT_PAGE_RANGE / 2));
    connect(fromPageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ExistingPictureDialog::onPageRangeChanged);
    rangeLayout->addWidget(fromPageSpinBox);
    
    QLabel *toLabel = new QLabel(tr("to"), this);
    rangeLayout->addWidget(toLabel);
    
    toPageSpinBox = new QSpinBox(this);
    toPageSpinBox->setMinimum(1);
    toPageSpinBox->setMaximum(totalPages);
    toPageSpinBox->setValue(qMin(totalPages, currentPage + 1 + DEFAULT_PAGE_RANGE / 2));
    connect(toPageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ExistingPictureDialog::onPageRangeChanged);
    rangeLayout->addWidget(toPageSpinBox);
    
    extendRangeButton = new QPushButton(tr("Extend Range"), this);
    connect(extendRangeButton, &QPushButton::clicked, 
            this, &ExistingPictureDialog::onExtendRangeClicked);
    rangeLayout->addWidget(extendRangeButton);
    
    rangeLayout->addStretch();
    mainLayout->addLayout(rangeLayout);
    
    // Status label
    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(statusLabel);
    
    // Scroll area for thumbnails
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    thumbnailContainer = new QWidget();
    thumbnailGrid = new QGridLayout(thumbnailContainer);
    thumbnailGrid->setSpacing(10);
    thumbnailGrid->setContentsMargins(10, 10, 10, 10);
    
    scrollArea->setWidget(thumbnailContainer);
    mainLayout->addWidget(scrollArea, 1);
    
    // Button row
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setMinimumWidth(80);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    
    selectButton = new QPushButton(tr("Select"), this);
    selectButton->setMinimumWidth(80);
    selectButton->setEnabled(false);
    selectButton->setDefault(true);
    connect(selectButton, &QPushButton::clicked, this, &ExistingPictureDialog::onSelectClicked);
    buttonLayout->addWidget(selectButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Load pictures for initial range
    loadPicturesForRange();
}

void ExistingPictureDialog::onPageRangeChanged() {
    // ✅ SAFETY: Check UI elements exist
    if (!fromPageSpinBox || !toPageSpinBox || !statusLabel) return;
    
    // Ensure from <= to
    if (fromPageSpinBox->value() > toPageSpinBox->value()) {
        if (sender() == fromPageSpinBox) {
            toPageSpinBox->setValue(fromPageSpinBox->value());
        } else {
            fromPageSpinBox->setValue(toPageSpinBox->value());
        }
    }
    
    // Check range limit
    int range = toPageSpinBox->value() - fromPageSpinBox->value() + 1;
    if (range > MAX_PAGE_RANGE) {
        statusLabel->setText(tr("⚠️ Range too large (max %1 pages). Reduce range or click 'Extend Range'.").arg(MAX_PAGE_RANGE));
        return;
    }
    
    loadPicturesForRange();
}

void ExistingPictureDialog::onExtendRangeClicked() {
    // ✅ SAFETY: Check UI elements exist
    if (!fromPageSpinBox || !toPageSpinBox) return;
    
    // Double the range centered on current page
    int currentFrom = fromPageSpinBox->value();
    int currentTo = toPageSpinBox->value();
    int currentRange = currentTo - currentFrom + 1;
    int newRange = qMin(currentRange * 2, MAX_PAGE_RANGE);
    
    int center = (currentFrom + currentTo) / 2;
    int newFrom = qMax(1, center - newRange / 2);
    int newTo = qMin(totalPages, newFrom + newRange - 1);
    
    fromPageSpinBox->setValue(newFrom);
    toPageSpinBox->setValue(newTo);
    
    loadPicturesForRange();
}

void ExistingPictureDialog::onThumbnailClicked(PictureThumbnail *thumbnail) {
    // Deselect previous
    if (selectedThumbnail) {
        selectedThumbnail->setSelected(false);
    }
    
    // Select new
    selectedThumbnail = thumbnail;
    if (selectedThumbnail) {
        selectedThumbnail->setSelected(true);
        selectedImagePath = selectedThumbnail->getImagePath();
        selectButton->setEnabled(true);
    } else {
        selectedImagePath.clear();
        selectButton->setEnabled(false);
    }
}

void ExistingPictureDialog::onSelectClicked() {
    if (!selectedImagePath.isEmpty()) {
        accept();
    }
}

void ExistingPictureDialog::clearThumbnails() {
    // Clear selection
    selectedThumbnail = nullptr;
    selectedImagePath.clear();
    if (selectButton) {
        selectButton->setEnabled(false);
    }
    
    // Delete all thumbnail widgets
    for (PictureThumbnail *thumb : thumbnails) {
        if (thumb) {
            thumb->deleteLater();
        }
    }
    thumbnails.clear();
    
    // Clear grid layout items
    if (thumbnailGrid) {
        QLayoutItem *item;
        while ((item = thumbnailGrid->takeAt(0)) != nullptr) {
            delete item;
        }
    }
}

void ExistingPictureDialog::loadPicturesForRange() {
    clearThumbnails();
    
    if (!pictureManager || !canvas) {
        statusLabel->setText(tr("No pictures available."));
        return;
    }
    
    int fromPage = fromPageSpinBox->value() - 1;  // Convert to 0-indexed
    int toPage = toPageSpinBox->value() - 1;
    
    // Collect all unique image paths in the range
    QMap<QString, int> imagePaths;  // path -> first page it appears on
    
    QString saveFolder = canvas->getSaveFolder();
    QString notebookId = canvas->getNotebookId();
    
    // ✅ SAFETY: Validate folder and notebook ID
    if (saveFolder.isEmpty() || notebookId.isEmpty()) {
        statusLabel->setText(tr("No pictures available."));
        return;
    }
    
    for (int page = fromPage; page <= toPage; ++page) {
        // Get picture data file path for this page
        QString filePath = saveFolder + QString("/.%1_pictures_%2.json")
            .arg(notebookId).arg(page, 5, 10, QChar('0'));
        
        if (!QFile::exists(filePath)) continue;
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonArray windowsArray = doc.array();
        
        for (const QJsonValue &value : windowsArray) {
            QJsonObject obj = value.toObject();
            QString imgPath = obj.value("image_path").toString();
            
            // Resolve relative paths
            QFileInfo fileInfo(imgPath);
            if (fileInfo.isRelative()) {
                imgPath = saveFolder + "/" + imgPath;
            }
            
            // Only add if file exists and not already in map
            if (!imgPath.isEmpty() && QFile::exists(imgPath) && !imagePaths.contains(imgPath)) {
                imagePaths[imgPath] = page;
            }
        }
    }
    
    // Create thumbnails
    int row = 0, col = 0;
    const int COLS = 4;
    
    for (auto it = imagePaths.begin(); it != imagePaths.end(); ++it) {
        PictureThumbnail *thumb = new PictureThumbnail(it.key(), it.value(), this);
        connect(thumb, &PictureThumbnail::clicked, 
                this, &ExistingPictureDialog::onThumbnailClicked);
        
        thumbnailGrid->addWidget(thumb, row, col);
        thumbnails.append(thumb);
        
        col++;
        if (col >= COLS) {
            col = 0;
            row++;
        }
    }
    
    // Add spacer at end
    thumbnailGrid->setRowStretch(row + 1, 1);
    thumbnailGrid->setColumnStretch(COLS, 1);
    
    // Update status
    int pictureCount = imagePaths.size();
    if (pictureCount == 0) {
        statusLabel->setText(tr("No pictures found in pages %1 to %2.")
            .arg(fromPage + 1).arg(toPage + 1));
    } else {
        statusLabel->setText(tr("Found %1 picture(s) in pages %2 to %3.")
            .arg(pictureCount).arg(fromPage + 1).arg(toPage + 1));
    }
}

