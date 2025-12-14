#ifndef EXISTINGPICTUREDIALOG_H
#define EXISTINGPICTUREDIALOG_H

#include <QDialog>
#include <QGridLayout>
#include <QScrollArea>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPointer>

class InkCanvas;
class PictureWindowManager;

// Represents a selectable picture thumbnail
class PictureThumbnail : public QWidget {
    Q_OBJECT

public:
    explicit PictureThumbnail(const QString &imagePath, int pageNumber, QWidget *parent = nullptr);
    
    QString getImagePath() const { return imagePath; }
    int getPageNumber() const { return pageNumber; }
    
    void setSelected(bool selected);
    bool isSelected() const { return selected; }

signals:
    void clicked(PictureThumbnail *thumbnail);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString imagePath;
    int pageNumber;
    QPixmap thumbnail;
    bool selected = false;
    bool hovered = false;
    
    static const int THUMB_SIZE = 100;
};

class ExistingPictureDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExistingPictureDialog(InkCanvas *canvas, int currentPage, QWidget *parent = nullptr);
    
    QString getSelectedImagePath() const { return selectedImagePath; }

private slots:
    void onPageRangeChanged();
    void onThumbnailClicked(PictureThumbnail *thumbnail);
    void onSelectClicked();
    void onExtendRangeClicked();

private:
    void loadPicturesForRange();
    void clearThumbnails();
    
    QPointer<InkCanvas> canvas; // ✅ SAFETY: Use QPointer to detect if canvas is deleted
    PictureWindowManager *pictureManager;
    int currentPage;
    int totalPages;
    
    // UI elements
    QSpinBox *fromPageSpinBox;
    QSpinBox *toPageSpinBox;
    QPushButton *extendRangeButton;
    QScrollArea *scrollArea;
    QWidget *thumbnailContainer;
    QGridLayout *thumbnailGrid;
    QPushButton *selectButton;
    QPushButton *cancelButton;
    QLabel *statusLabel;
    
    // Thumbnails
    QList<PictureThumbnail*> thumbnails;
    PictureThumbnail *selectedThumbnail = nullptr;
    QString selectedImagePath;
    
    // Range limit for performance
    static const int DEFAULT_PAGE_RANGE = 10;
    static const int MAX_PAGE_RANGE = 50;
};

#endif // EXISTINGPICTUREDIALOG_H

