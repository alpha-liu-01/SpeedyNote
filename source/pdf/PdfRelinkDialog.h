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
     * @param pdfPath Path to the PDF file (missing or currently linked).
     * @param storedHash Stored hash for verification (empty = legacy, skip verification).
     * @param storedSize Stored file size for display (0 = unknown).
     * @param pdfIsLoaded true if the PDF is currently loaded (reconnect mode),
     *                    false if the PDF is missing (missing mode).
     * @param parent Parent widget.
     */
    explicit PdfRelinkDialog(const QString& pdfPath,
                             const QString& storedHash = QString(),
                             qint64 storedSize = 0,
                             bool pdfIsLoaded = false,
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
#ifdef Q_OS_IOS
    void handlePickedPdf(const QString& selectedPdf);
#endif
    
    Result result = Cancel;
    QString originalPdfPath;
    QString newPdfPath;
    QString m_storedHash;
    qint64 m_storedSize = 0;
    bool m_pdfIsLoaded = false;
};

#endif // PDFRELINKDIALOG_H 