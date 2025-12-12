#ifndef PICTURESOURCEDIALOG_H
#define PICTURESOURCEDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

class PictureSourceDialog : public QDialog {
    Q_OBJECT

public:
    enum Result {
        Cancelled,
        LoadFromDisk,
        UseExisting
    };

    explicit PictureSourceDialog(QWidget *parent = nullptr);
    
    Result getResult() const { return result; }

private slots:
    void onLoadFromDisk();
    void onUseExisting();

private:
    Result result = Cancelled;
    
    QPushButton *loadFromDiskButton;
    QPushButton *useExistingButton;
    QPushButton *cancelButton;
};

#endif // PICTURESOURCEDIALOG_H

