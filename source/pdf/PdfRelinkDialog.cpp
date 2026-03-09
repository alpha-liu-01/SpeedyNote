#include "PdfRelinkDialog.h"
#include "PdfMismatchDialog.h"
#include "../core/Document.h"
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include "../android/PdfPickerAndroid.h"
#elif defined(Q_OS_IOS)
#include "../ios/PdfPickerIOS.h"
#endif

PdfRelinkDialog::PdfRelinkDialog(const QString& pdfPath,
                                   const QString& storedHash,
                                   qint64 storedSize,
                                   bool pdfIsLoaded,
                                   QWidget* parent)
    : QDialog(parent)
    , originalPdfPath(pdfPath)
    , m_storedHash(storedHash)
    , m_storedSize(storedSize)
    , m_pdfIsLoaded(pdfIsLoaded)
{
    if (originalPdfPath.isEmpty()) {
        setWindowTitle(tr("Link PDF"));
    } else if (m_pdfIsLoaded) {
        setWindowTitle(tr("Reconnect PDF"));
    } else {
        setWindowTitle(tr("PDF File Missing"));
    }
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
    
    bool noPdfReference = originalPdfPath.isEmpty();

    QLabel *iconLabel = new QLabel();
    QStyle::StandardPixmap iconType = (noPdfReference || m_pdfIsLoaded)
        ? QStyle::SP_MessageBoxInformation
        : QStyle::SP_MessageBoxWarning;
    QPixmap icon = QApplication::style()->standardIcon(iconType).pixmap(48, 48);
    iconLabel->setPixmap(icon);
    iconLabel->setFixedSize(48, 48);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    
    QString titleText;
    QString titleColor;
    if (noPdfReference) {
        titleText = tr("Link PDF File");
        titleColor = "#2980b9";
    } else if (m_pdfIsLoaded) {
        titleText = tr("Reconnect PDF File");
        titleColor = "#2980b9";
    } else {
        titleText = tr("PDF File Not Found");
        titleColor = "#d35400";
    }
    QLabel *titleLabel = new QLabel(titleText);
    titleLabel->setStyleSheet(
        QString("font-size: 16px; font-weight: bold; color: %1;").arg(titleColor));
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    headerLayout->addWidget(iconLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    mainLayout->addLayout(headerLayout);
    
    // Message
    QFileInfo fileInfo(originalPdfPath);
    QString fileName = fileInfo.fileName();
    
    QString messageText;
    if (noPdfReference) {
        messageText = tr("This notebook does not have a linked PDF file.\n\n"
                         "You can select a PDF file to link to this notebook.");
    } else if (m_pdfIsLoaded) {
        messageText = tr("A PDF file is currently linked to this notebook:\n\n"
                         "Current file: %1\n\n"
                         "You can select a different PDF file to link to this notebook.\n\n"
                         "What would you like to do?").arg(fileName);
    } else {
        messageText = tr("The PDF file linked to this notebook could not be found:\n\n"
                         "Missing file: %1\n\n"
                         "This may happen if the file was moved, renamed, or you're opening the notebook on a different computer.\n\n"
                         "What would you like to do?").arg(fileName);
    }
    
    QLabel *messageLabel = new QLabel(messageText);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet("font-size: 12px; color: #555;");
    messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    mainLayout->addWidget(messageLabel);
    
    // Buttons
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(10);
    
    // Relink PDF button
    QString relinkLabel;
    if (noPdfReference) {
        relinkLabel = tr("Choose PDF File...");
    } else if (m_pdfIsLoaded) {
        relinkLabel = tr("Choose New PDF File...");
    } else {
        relinkLabel = tr("Locate PDF File...");
    }
    QPushButton *relinkBtn = new QPushButton(relinkLabel);
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
    
    buttonLayout->addWidget(relinkBtn);

    // Continue without PDF / Unlink button (hidden when there's no PDF to unlink)
    if (!noPdfReference) {
        QString continueLabel = m_pdfIsLoaded
            ? tr("Unlink PDF")
            : tr("Continue Without PDF");
        QPushButton *continueBtn = new QPushButton(continueLabel);
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
        buttonLayout->addWidget(continueBtn);
    }
    
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
    QString startDir;
    if (!originalPdfPath.isEmpty()) {
        QFileInfo originalInfo(originalPdfPath);
        startDir = originalInfo.absolutePath();
    }
    
    // If no original path or directory doesn't exist, try the last-used open directory
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        QSettings settings("SpeedyNote", "App");
        startDir = settings.value("FileDialogs/lastOpenDirectory").toString();
        if (startDir.isEmpty() || !QDir(startDir).exists()) {
            startDir = QDir::homePath();
        }
    }
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Track copied PDFs for cleanup if user chooses "Choose Different"
    // On Android/iOS, picked files are copied to /pdfs/ directory, and if rejected
    // (hash mismatch + "Choose Different"), we should clean them up
    QString androidPdfsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
#endif
    
#if defined(Q_OS_IOS)
    // iOS: UIDocumentPickerViewController is async (remote VC, XPC result).
    // Present picker and handle the result in handlePickedPdf().
    PdfPickerIOS::pickPdfFile([this](const QString& selectedPdf) {
        handlePickedPdf(selectedPdf);
    });
    return;
#endif

    // Loop to allow "Choose Different" from mismatch dialog
    while (true) {
        QString selectedPdf;
        
#ifdef Q_OS_ANDROID
        // Use shared Android PDF picker (handles SAF permissions properly)
        // See source/android/PdfPickerAndroid.cpp for implementation
        selectedPdf = PdfPickerAndroid::pickPdfFile();
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
#if defined(Q_OS_ANDROID)
            // Clean up the rejected PDF copy (it's in our sandbox, safe to delete)
            if (selectedPdf.startsWith(androidPdfsDir)) {
                QFile::remove(selectedPdf);
            }
#endif
            reject();
            return;
        }
        
        // User chose "Choose Different" - clean up rejected file and loop
#if defined(Q_OS_ANDROID)
        // Clean up the rejected PDF copy before picking a new one
        if (selectedPdf.startsWith(androidPdfsDir)) {
            QFile::remove(selectedPdf);
        }
#endif
        
        // Note: On Android, startDir is not used since SAF picker doesn't support it
        startDir = pdfInfo.absolutePath();
    }
}

#ifdef Q_OS_IOS
void PdfRelinkDialog::handlePickedPdf(const QString& selectedPdf)
{
    QString pdfsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";

    if (selectedPdf.isEmpty()) {
        return;
    }

    QFileInfo pdfInfo(selectedPdf);
    if (!pdfInfo.exists() || !pdfInfo.isFile()) {
        QMessageBox::warning(this, tr("Invalid File"),
            tr("The selected file is not a valid PDF file."));
        // Re-present the picker
        PdfPickerIOS::pickPdfFile([this](const QString& path) {
            handlePickedPdf(path);
        });
        return;
    }

    if (verifyAndConfirmPdf(selectedPdf)) {
        newPdfPath = selectedPdf;
        result = RelinkPdf;
        accept();
        return;
    }

    if (result == Cancel) {
        if (selectedPdf.startsWith(pdfsDir)) {
            QFile::remove(selectedPdf);
        }
        reject();
        return;
    }

    // User chose "Choose Different" — clean up and re-pick
    if (selectedPdf.startsWith(pdfsDir)) {
        QFile::remove(selectedPdf);
    }
    PdfPickerIOS::pickPdfFile([this](const QString& path) {
        handlePickedPdf(path);
    });
}
#endif

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
    QString title = m_pdfIsLoaded ? tr("Unlink PDF") : tr("Continue Without PDF");
    QString message = m_pdfIsLoaded
        ? tr("Are you sure you want to unlink the current PDF file?\n\n"
             "You can still use the notebook for taking notes, but PDF annotation features will not be available.\n\n"
             "You can relink a PDF file later from the menu.")
        : tr("Are you sure you want to continue without linking a PDF file?\n\n"
             "You can still use the notebook for taking notes, but PDF annotation features will not be available.");
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        title,
        message,
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