#include "PdfRelinkDialog.h"
#include "PdfMismatchDialog.h"
#include "../core/Document.h"
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include "../android/PdfPickerAndroid.h"
#elif defined(Q_OS_IOS)
#include "../ios/PdfPickerIOS.h"
#endif

PdfRelinkDialog::PdfRelinkDialog(const QString& missingPdfPath,
                                   const QString& storedHash,
                                   qint64 storedSize,
                                   QWidget* parent)
    : QDialog(parent)
    , originalPdfPath(missingPdfPath)
    , m_storedHash(storedHash)
    , m_storedSize(storedSize)
{
    setWindowTitle(tr("PDF File Missing"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    setModal(true);
    
    // Set reasonable size
    setMinimumSize(500, 380);
    setMaximumSize(600, 480);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
    setupUI();
    
    // Center the dialog
    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            move(screen->geometry().center() - rect().center());
        }
    }
}

void PdfRelinkDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Header with icon
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);
    
    QLabel *iconLabel = new QLabel();
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(48, 48);
    iconLabel->setPixmap(icon);
    iconLabel->setFixedSize(48, 48);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    
    QLabel *titleLabel = new QLabel(tr("PDF File Not Found"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d35400;");
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    headerLayout->addWidget(iconLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    mainLayout->addLayout(headerLayout);
    
    // Message
    QFileInfo fileInfo(originalPdfPath);
    QString fileName = fileInfo.fileName();
    
    QLabel *messageLabel = new QLabel(
        tr("The PDF file linked to this notebook could not be found:\n\n"
           "Missing file: %1\n\n"
           "This may happen if the file was moved, renamed, or you're opening the notebook on a different computer.\n\n"
           "What would you like to do?").arg(fileName)
    );
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet("font-size: 12px; color: #555;");
    messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    mainLayout->addWidget(messageLabel);
    
    // Buttons
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(10);
    
    // Relink PDF button
    QPushButton *relinkBtn = new QPushButton(tr("Locate PDF File..."));
    relinkBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    relinkBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    relinkBtn->setMinimumHeight(40);
    relinkBtn->setStyleSheet(R"(
        QPushButton {
            text-align: left;
            padding: 10px;
            border: 2px solid #3498db;
            border-radius: 5px;
            background: palette(button);
            font-weight: bold;
        }
        QPushButton:hover {
            background: #3498db;
            color: white;
        }
        QPushButton:pressed {
            background: #2980b9;
        }
    )");
    connect(relinkBtn, &QPushButton::clicked, this, &PdfRelinkDialog::onRelinkPdf);
    
    // Continue without PDF button
    QPushButton *continueBtn = new QPushButton(tr("Continue Without PDF"));
    continueBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton));
    continueBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    continueBtn->setMinimumHeight(40);
    continueBtn->setStyleSheet(R"(
        QPushButton {
            text-align: left;
            padding: 10px;
            border: 1px solid palette(mid);
            border-radius: 5px;
            background: palette(button);
        }
        QPushButton:hover {
            background: palette(light);
            border-color: palette(dark);
        }
        QPushButton:pressed {
            background: palette(midlight);
        }
    )");
    connect(continueBtn, &QPushButton::clicked, this, &PdfRelinkDialog::onContinueWithoutPdf);
    
    buttonLayout->addWidget(relinkBtn);
    buttonLayout->addWidget(continueBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    // Cancel button
    QHBoxLayout *cancelLayout = new QHBoxLayout();
    cancelLayout->addStretch();
    
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    cancelBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    cancelBtn->setMinimumSize(80, 30);
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            padding: 8px 20px;
            border: 1px solid palette(mid);
            border-radius: 3px;
            background: palette(button);
        }
        QPushButton:hover {
            background: palette(light);
        }
        QPushButton:pressed {
            background: palette(midlight);
        }
    )");
    connect(cancelBtn, &QPushButton::clicked, this, &PdfRelinkDialog::onCancel);
    
    cancelLayout->addWidget(cancelBtn);
    mainLayout->addLayout(cancelLayout);
}

void PdfRelinkDialog::onRelinkPdf()
{
    QFileInfo originalInfo(originalPdfPath);
    QString startDir = originalInfo.absolutePath();
    
    // If original directory doesn't exist, use home directory
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Track copied PDFs for cleanup if user chooses "Choose Different"
    // On Android/iOS, picked files are copied to /pdfs/ directory, and if rejected
    // (hash mismatch + "Choose Different"), we should clean them up
    QString androidPdfsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
#endif
    
    // Loop to allow "Choose Different" from mismatch dialog
    while (true) {
        QString selectedPdf;
        
#ifdef Q_OS_ANDROID
        // Use shared Android PDF picker (handles SAF permissions properly)
        // See source/android/PdfPickerAndroid.cpp for implementation
        selectedPdf = PdfPickerAndroid::pickPdfFile();
#elif defined(Q_OS_IOS)
        // TODO Phase 4: iOS PDF picker
        selectedPdf = QString();
#else
        // Desktop: Use native file dialog
        selectedPdf = QFileDialog::getOpenFileName(
            this,
            tr("Locate PDF File"),
            startDir,
            tr("PDF Files (*.pdf);;All Files (*)")
        );
#endif
        
        if (selectedPdf.isEmpty()) {
            // User cancelled file dialog
            return;
        }
        
        // Verify it's a valid PDF file
        QFileInfo pdfInfo(selectedPdf);
        if (!pdfInfo.exists() || !pdfInfo.isFile()) {
            QMessageBox::warning(this, tr("Invalid File"), 
                tr("The selected file is not a valid PDF file."));
            continue;
        }
        
        // Verify hash if we have one stored
        if (verifyAndConfirmPdf(selectedPdf)) {
            newPdfPath = selectedPdf;
            result = RelinkPdf;
            accept();
            return;
        }
        
        // verifyAndConfirmPdf returned false - either user chose "Choose Different"
        // (loop continues) or "Cancel" (we should exit)
        // Check if we should exit entirely
        if (result == Cancel) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
            // Clean up the rejected PDF copy (it's in our sandbox, safe to delete)
            if (selectedPdf.startsWith(androidPdfsDir)) {
                QFile::remove(selectedPdf);
            }
#endif
            reject();
            return;
        }
        
        // User chose "Choose Different" - clean up rejected file and loop
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Clean up the rejected PDF copy before picking a new one
        if (selectedPdf.startsWith(androidPdfsDir)) {
            QFile::remove(selectedPdf);
        }
#endif
        
        // Note: On Android, startDir is not used since SAF picker doesn't support it
        startDir = pdfInfo.absolutePath();
    }
}

bool PdfRelinkDialog::verifyAndConfirmPdf(const QString& selectedPath)
{
    // No stored hash = legacy document, accept any PDF
    if (m_storedHash.isEmpty()) {
        return true;
    }
    
    // Compute hash of selected file
    QString selectedHash = Document::computePdfHash(selectedPath);
    
    // Hash matches - accept
    if (selectedHash == m_storedHash) {
        return true;
    }
    
    // Hash mismatch - show warning dialog
    QFileInfo originalInfo(originalPdfPath);
    QString originalName = originalInfo.fileName();
    
    PdfMismatchDialog mismatchDialog(originalName, m_storedSize, selectedPath, this);
    mismatchDialog.exec();
    
    switch (mismatchDialog.getResult()) {
        case PdfMismatchDialog::Result::UseThisPdf:
            // User accepts the different PDF
            return true;
            
        case PdfMismatchDialog::Result::ChooseDifferent:
            // User wants to pick a different file - return false to continue loop
            return false;
            
        case PdfMismatchDialog::Result::Cancel:
            // User wants to abort entirely
            result = Cancel;
            return false;
    }
    
    return false;
}

void PdfRelinkDialog::onContinueWithoutPdf()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Continue Without PDF"),
        tr("Are you sure you want to continue without linking a PDF file?\n\n"
           "You can still use the notebook for taking notes, but PDF annotation features will not be available."),
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        result = ContinueWithoutPdf;
        accept();
    }
}

void PdfRelinkDialog::onCancel()
{
    result = Cancel;
    reject();
} 