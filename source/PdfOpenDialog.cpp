#include "PdfOpenDialog.h"
#include "SpnPackageManager.h"
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>

PdfOpenDialog::PdfOpenDialog(const QString &pdfPath, QWidget *parent)
    : QDialog(parent), result(Cancel), pdfPath(pdfPath)
{
    setWindowTitle(tr("Open File with SpeedyNote"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Remove setFixedSize and use proper size management instead
    // Calculate size based on DPI
    QScreen *screen = QGuiApplication::primaryScreen();
    qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    
    // Set minimum and maximum sizes instead of fixed size
    int baseWidth = 500;
    int baseHeight = 250; // Increased to accommodate third button
    
    // Scale sizes appropriately for DPI
    int scaledWidth = static_cast<int>(baseWidth * qMax(1.0, dpr * 0.8));
    int scaledHeight = static_cast<int>(baseHeight * qMax(1.0, dpr * 0.8));
    
    setMinimumSize(scaledWidth, scaledHeight);
    setMaximumSize(scaledWidth + 100, scaledHeight + 80); // Allow some flexibility
    
    // Set size policy to prevent unwanted resizing
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
    setupUI();
    
    // Ensure the dialog is properly sized after UI setup
    adjustSize();
    
    // Center the dialog on parent or screen
    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else {
        move(screen->geometry().center() - rect().center());
    }
}

void PdfOpenDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Set layout size constraint to prevent unwanted resizing
    mainLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    
    // Icon and title
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);
    
    QLabel *iconLabel = new QLabel();
    QPixmap icon = QIcon(":/resources/icons/mainicon.png").pixmap(48, 48);
    iconLabel->setPixmap(icon);
    iconLabel->setFixedSize(48, 48);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    
    QLabel *titleLabel = new QLabel(tr("Open PDF with SpeedyNote"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    headerLayout->addWidget(iconLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    mainLayout->addLayout(headerLayout);
    
    // PDF file info
    QFileInfo fileInfo(pdfPath);
    QString fileName = fileInfo.fileName();
    QString folderName = fileInfo.baseName(); // Filename without extension
    
    QLabel *messageLabel = new QLabel(tr("PDF File: %1\n\nHow would you like to open this PDF?").arg(fileName));
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet("font-size: 12px; color: #666;");
    messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    mainLayout->addWidget(messageLabel);
    
    // Buttons
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(10);
    
    // Create new .spn package button (in same folder as PDF)
    QPushButton *createFolderBtn = new QPushButton(tr("Create New Package Here (\"%1.spn\")").arg(folderName));
    createFolderBtn->setIcon(QIcon(":/resources/icons/mainicon.png"));
    createFolderBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    createFolderBtn->setMinimumHeight(40);
    createFolderBtn->setStyleSheet(R"(
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
    connect(createFolderBtn, &QPushButton::clicked, this, &PdfOpenDialog::onCreateNewFolder);
    
    // Create new .spn package button (custom location)
    QPushButton *createCustomLocationBtn = new QPushButton(tr("Create New Package Elsewhere..."));
    createCustomLocationBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    createCustomLocationBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    createCustomLocationBtn->setMinimumHeight(40);
    createCustomLocationBtn->setStyleSheet(R"(
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
    connect(createCustomLocationBtn, &QPushButton::clicked, this, &PdfOpenDialog::onCreateNewFolderCustomLocation);
    
    // Use existing notebook button (folder or .spn)
    QPushButton *existingFolderBtn = new QPushButton(tr("Use Existing Notebook..."));
    existingFolderBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    existingFolderBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    existingFolderBtn->setMinimumHeight(40);
    existingFolderBtn->setStyleSheet(R"(
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
    connect(existingFolderBtn, &QPushButton::clicked, this, &PdfOpenDialog::onUseExistingFolder);
    
    buttonLayout->addWidget(createFolderBtn);
    buttonLayout->addWidget(createCustomLocationBtn);
    buttonLayout->addWidget(existingFolderBtn);
    
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
    connect(cancelBtn, &QPushButton::clicked, this, &PdfOpenDialog::onCancel);
    
    cancelLayout->addWidget(cancelBtn);
    mainLayout->addLayout(cancelLayout);
}

void PdfOpenDialog::resizeEvent(QResizeEvent *event)
{
    // Handle resize events smoothly to prevent jitter during window dragging
    if (event->size() != event->oldSize()) {
        // Only process if size actually changed
        QDialog::resizeEvent(event);
        
        // Ensure the dialog stays within reasonable bounds
        QSize newSize = event->size();
        QSize minSize = minimumSize();
        QSize maxSize = maximumSize();
        
        // Clamp the size to prevent unwanted resizing
        newSize = newSize.expandedTo(minSize).boundedTo(maxSize);
        
        if (newSize != event->size()) {
            // If size needs to be adjusted, do it without triggering another resize event
            resize(newSize);
        }
    } else {
        // If size hasn't changed, just call parent implementation
        QDialog::resizeEvent(event);
    }
}

void PdfOpenDialog::onCreateNewFolder()
{
    QFileInfo fileInfo(pdfPath);
    QString suggestedName = fileInfo.baseName();
    
    // Get the directory where the PDF is located
    QString pdfDir = fileInfo.absolutePath();
    QString newSpnPath = pdfDir + "/" + suggestedName + ".spn";
    
    // Check if .spn package already exists
    if (QDir(newSpnPath).exists()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            tr("Package Exists"), 
            tr("A SpeedyNote package named \"%1.spn\" already exists in the same directory as the PDF.\n\nDo you want to use this existing package?").arg(suggestedName),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
        );
        
        if (reply == QMessageBox::Yes) {
            selectedFolder = newSpnPath;
            result = UseExistingFolder;
            accept();
            return;
        } else if (reply == QMessageBox::Cancel) {
            return; // Stay in dialog
        }
        // If No, continue to create with different name
        
        // Try to create with incremented name
        int counter = 1;
        do {
            newSpnPath = pdfDir + "/" + suggestedName + QString("_%1.spn").arg(counter);
            counter++;
        } while (QDir(newSpnPath).exists() && counter < 100);
        
        if (counter >= 100) {
            QMessageBox::warning(this, tr("Error"), tr("Could not create a unique package name."));
            return;
        }
    }
    
    // Create the new .spn package
    if (SpnPackageManager::createSpnPackage(newSpnPath)) {
        selectedFolder = newSpnPath;
        result = CreateNewFolder;
        accept();
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to create SpeedyNote package: %1").arg(newSpnPath));
    }
}

void PdfOpenDialog::onCreateNewFolderCustomLocation()
{
    QFileInfo fileInfo(pdfPath);
    QString suggestedName = fileInfo.baseName();
    
    // Let user choose where to save the .spn file
    QString newSpnPath = QFileDialog::getSaveFileName(
        this,
        tr("Choose Location for New SpeedyNote Package"),
        QFileInfo(pdfPath).absolutePath() + "/" + suggestedName + ".spn",
        tr("SpeedyNote Packages (*.spn)")
    );
    
    if (newSpnPath.isEmpty()) {
        return; // User cancelled
    }
    
    // Ensure .spn extension
    if (!newSpnPath.toLower().endsWith(".spn")) {
        newSpnPath += ".spn";
    }
    
    // Check if .spn package already exists
    if (QDir(newSpnPath).exists()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            tr("Package Exists"), 
            tr("A SpeedyNote package already exists at this location.\n\nDo you want to use this existing package?"),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            selectedFolder = newSpnPath;
            result = CreateNewFolderCustomLocation;
            accept();
            return;
        } else {
            return; // Stay in dialog
        }
    }
    
    // Create the new .spn package at the custom location
    if (SpnPackageManager::createSpnPackage(newSpnPath)) {
        selectedFolder = newSpnPath;
        result = CreateNewFolderCustomLocation;
        accept();
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to create SpeedyNote package: %1").arg(newSpnPath));
    }
}

void PdfOpenDialog::onUseExistingFolder()
{
    // Use QFileDialog to select either folders or .spn files
    QString selected = QFileDialog::getOpenFileName(
        this,
        tr("Select Existing Notebook (.spn) or Folder"),
        QFileInfo(pdfPath).absolutePath(),
        tr("SpeedyNote Packages (*.spn);;All Files (*)")
    );
    
    // If no .spn file selected, try folder selection
    if (selected.isEmpty()) {
        selected = QFileDialog::getExistingDirectory(
            this, 
            tr("Select Existing Notebook Folder"), 
            QFileInfo(pdfPath).absolutePath()
        );
    }
    
    if (!selected.isEmpty()) {
        selectedFolder = selected;
        result = UseExistingFolder;
        accept();
    }
}

void PdfOpenDialog::onCancel()
{
    result = Cancel;
    reject();
}

// Static method to check if a matching notebook folder exists and is valid
// This checks for a folder with the same name as the PDF (without extension)
// or a .spn package with the same name in the same directory as the PDF file
bool PdfOpenDialog::hasValidNotebookFolder(const QString &pdfPath, QString &folderPath)
{
    QFileInfo fileInfo(pdfPath);
    QString suggestedName = fileInfo.baseName(); // Filename without extension
    QString pdfDir = fileInfo.absolutePath(); // Directory containing the PDF
    
    // First, check for .spn package
    QString potentialSpnPath = pdfDir + "/" + suggestedName + ".spn";
    if (SpnPackageManager::isValidSpnPackage(potentialSpnPath)) {
        // Extract to temp and check if it's valid for this PDF
        QString tempDir = SpnPackageManager::extractSpnToTemp(potentialSpnPath);
        if (!tempDir.isEmpty() && isValidNotebookFolder(tempDir, pdfPath)) {
            SpnPackageManager::cleanupTempDir(tempDir);
            folderPath = potentialSpnPath; // Return the .spn package path
            return true;
        }
        SpnPackageManager::cleanupTempDir(tempDir);
    }
    
    // Then, check for regular folder
    QString potentialFolderPath = pdfDir + "/" + suggestedName;
    if (isValidNotebookFolder(potentialFolderPath, pdfPath)) {
        folderPath = potentialFolderPath;
        return true;
    }
    
    return false;
}

// Static method to validate if a folder is a valid SpeedyNote notebook folder for the given PDF
// Returns true only if the folder exists, contains SpeedyNote data, and is linked to the exact same PDF
bool PdfOpenDialog::isValidNotebookFolder(const QString &folderPath, const QString &pdfPath)
{
    QDir folder(folderPath);
    if (!folder.exists()) {
        return false;
    }
    
    // Priority 1: Check for .pdf_path.txt file with matching PDF path
    QString pdfPathFile = folderPath + "/.pdf_path.txt";
    if (QFile::exists(pdfPathFile)) {
        QFile file(pdfPathFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString storedPdfPath = in.readLine().trimmed();
            file.close();
            
            if (!storedPdfPath.isEmpty()) {
                // Compare absolute paths to handle relative path differences
                QFileInfo currentPdf(pdfPath);
                QFileInfo storedPdf(storedPdfPath);
                
                // Check if the stored PDF file still exists and matches
                if (storedPdf.exists() && 
                    currentPdf.absoluteFilePath() == storedPdf.absoluteFilePath()) {
                    return true;
                }
            }
        }
    }
    
    // Priority 2: Check for .notebook_id.txt file (SpeedyNote notebook folder)
    QString notebookIdFile = folderPath + "/.notebook_id.txt";
    if (QFile::exists(notebookIdFile)) {
        // Additional check: ensure this folder has some notebook content
        QStringList contentFiles = folder.entryList(
            QStringList() << "*.png" << ".pdf_path.txt" << ".bookmarks.txt" << ".background_config.txt" << ".speedynote_metadata.json", 
            QDir::Files
        );
        
        // If it has notebook-related files, it's likely a valid SpeedyNote folder
        // but not necessarily for this specific PDF
        if (!contentFiles.isEmpty()) {
            // This is a SpeedyNote notebook, but for a different PDF
            // Return false so the user gets the dialog to choose what to do
            return false;
        }
    }
    
    return false;
} 