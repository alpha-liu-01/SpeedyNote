#ifndef PDFMISMATCHDIALOG_H
#define PDFMISMATCHDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>

/**
 * @brief Dialog shown when user selects a PDF that doesn't match the stored hash.
 * 
 * Warns the user that the selected PDF appears to be different from the original,
 * and offers options to use it anyway, choose a different file, or cancel.
 */
class PdfMismatchDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Result {
        UseThisPdf,      ///< Accept the different PDF (update stored hash)
        ChooseDifferent, ///< Go back to file picker
        Cancel           ///< Abort relink entirely
    };

    /**
     * @brief Construct the mismatch dialog.
     * @param originalName Original PDF filename (from stored path).
     * @param originalSize Original PDF size in bytes (0 if unknown).
     * @param selectedPath Path to the selected PDF file.
     * @param parent Parent widget.
     */
    explicit PdfMismatchDialog(const QString& originalName, 
                                qint64 originalSize,
                                const QString& selectedPath, 
                                QWidget* parent = nullptr);
    
    Result getResult() const { return m_result; }

private slots:
    void onUseThisPdf();
    void onChooseDifferent();
    void onCancel();

private:
    void setupUI();
    static QString formatFileSize(qint64 bytes);
    
    Result m_result = Result::Cancel;
    QString m_originalName;
    qint64 m_originalSize;
    QString m_selectedPath;
};

#endif // PDFMISMATCHDIALOG_H

