#include "PdfRelinkDialog.h"
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>

PdfRelinkDialog::PdfRelinkDialog(const QString &missingPdfPath, QWidget *parent)
    : QDialog(parent), originalPdfPath(missingPdfPath)
{
    setWindowTitle(tr("PDF File Missing"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Set reasonable size
    setMinimumSize(500, 200);
    setMaximumSize(600, 300);
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
    
    QString selectedPdf = QFileDialog::getOpenFileName(
        this,
        tr("Locate PDF File"),
        startDir,
        tr("PDF Files (*.pdf);;All Files (*)")
    );
    
    if (!selectedPdf.isEmpty()) {
        // Verify it's a valid PDF file
        QFileInfo pdfInfo(selectedPdf);
        if (pdfInfo.exists() && pdfInfo.isFile()) {
            newPdfPath = selectedPdf;
            result = RelinkPdf;
            accept();
        } else {
            QMessageBox::warning(this, tr("Invalid File"), 
                tr("The selected file is not a valid PDF file."));
        }
    }
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