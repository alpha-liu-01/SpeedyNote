#ifndef PDFOPENDIALOG_H
#define PDFOPENDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QPixmap>
#include <QFileInfo>
#include <QResizeEvent>

class PdfOpenDialog : public QDialog {
    Q_OBJECT

public:
    enum Result {
        Cancel,
        CreateNewFolder,
        CreateNewFolderCustomLocation,
        UseExistingFolder
    };

    explicit PdfOpenDialog(const QString &pdfPath, QWidget *parent = nullptr);
    
    Result getResult() const { return result; }
    QString getSelectedFolder() const { return selectedFolder; }
    
    // Static method to check if a matching notebook folder exists and is valid
    static bool hasValidNotebookFolder(const QString &pdfPath, QString &folderPath);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onCreateNewFolder();
    void onCreateNewFolderCustomLocation();
    void onUseExistingFolder();
    void onCancel();

private:
    void setupUI();
    static bool isValidNotebookFolder(const QString &folderPath, const QString &pdfPath);

    Result result;
    QString selectedFolder;
    QString pdfPath;
};

#endif // PDFOPENDIALOG_H 