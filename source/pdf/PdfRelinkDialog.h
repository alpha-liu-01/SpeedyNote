#ifndef PDFRELINKDIALOG_H
#define PDFRELINKDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>

class PdfRelinkDialog : public QDialog
{
    Q_OBJECT

public:
    enum Result {
        Cancel,
        RelinkPdf,
        ContinueWithoutPdf
    };

    /**
     * @brief Construct the PDF relink dialog.
     * @param missingPdfPath Path to the missing PDF file.
     * @param storedHash Stored hash for verification (empty = legacy, skip verification).
     * @param storedSize Stored file size for display (0 = unknown).
     * @param parent Parent widget.
     */
    explicit PdfRelinkDialog(const QString& missingPdfPath,
                             const QString& storedHash = QString(),
                             qint64 storedSize = 0,
                             QWidget* parent = nullptr);
    
    Result getResult() const { return result; }
    QString getNewPdfPath() const { return newPdfPath; }

private slots:
    void onRelinkPdf();
    void onContinueWithoutPdf();
    void onCancel();

private:
    void setupUI();
    bool verifyAndConfirmPdf(const QString& selectedPath);
    
    Result result = Cancel;
    QString originalPdfPath;
    QString newPdfPath;
    QString m_storedHash;
    qint64 m_storedSize = 0;
};

#endif // PDFRELINKDIALOG_H 