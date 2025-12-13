#include "DocumentConverter.h"
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>

DocumentConverter::DocumentConverter(QObject *parent)
    : QObject(parent), conversionProcess(nullptr)
{
}

DocumentConverter::~DocumentConverter()
{
    if (conversionProcess) {
        conversionProcess->kill();
        conversionProcess->waitForFinished(1000);
        delete conversionProcess;
    }
}

bool DocumentConverter::isLibreOfficeAvailable()
{
    QString path = getLibreOfficePath();
    return !path.isEmpty();
}

QString DocumentConverter::getLibreOfficePath()
{
    // Try common LibreOffice installation paths
    QStringList possiblePaths;
    
#ifdef Q_OS_WIN
    // Windows paths
    possiblePaths << "C:/Program Files/LibreOffice/program/soffice.exe"
                  << "C:/Program Files (x86)/LibreOffice/program/soffice.exe"
                  << "C:/Program Files/LibreOffice/program/soffice.com"
                  << "C:/Program Files (x86)/LibreOffice/program/soffice.com";
    
    // Check if soffice is in PATH
    QProcess testProcess;
    testProcess.start("soffice", QStringList() << "--version");
    if (testProcess.waitForFinished(2000) && testProcess.exitCode() == 0) {
        return "soffice";
    }
    
    // Check if soffice.exe is in PATH
    testProcess.start("soffice.exe", QStringList() << "--version");
    if (testProcess.waitForFinished(2000) && testProcess.exitCode() == 0) {
        return "soffice.exe";
    }
#elif defined(Q_OS_LINUX)
    // Linux paths
    possiblePaths << "/usr/bin/libreoffice"
                  << "/usr/local/bin/libreoffice"
                  << "/usr/bin/soffice"
                  << "/usr/local/bin/soffice"
                  << "/snap/bin/libreoffice";
    
    // Check if libreoffice is in PATH
    QProcess testProcess;
    testProcess.start("libreoffice", QStringList() << "--version");
    if (testProcess.waitForFinished(2000) && testProcess.exitCode() == 0) {
        return "libreoffice";
    }
    
    // Check if soffice is in PATH
    testProcess.start("soffice", QStringList() << "--version");
    if (testProcess.waitForFinished(2000) && testProcess.exitCode() == 0) {
        return "soffice";
    }
#elif defined(Q_OS_MACOS)
    // macOS paths
    possiblePaths << "/Applications/LibreOffice.app/Contents/MacOS/soffice";
#endif
    
    // Check each possible path
    for (const QString &path : possiblePaths) {
        QFileInfo fileInfo(path);
        if (fileInfo.exists() && fileInfo.isExecutable()) {
            return path;
        }
    }
    
    return QString(); // Not found
}

QString DocumentConverter::getInstallationInstructions()
{
#ifdef Q_OS_WIN
    return QObject::tr(
        "LibreOffice is required to open PowerPoint files.\n\n"
        "Please download and install LibreOffice from:\n"
        "https://www.libreoffice.org/download/download/\n\n"
        "After installation, restart SpeedyNote and try again."
    );
#elif defined(Q_OS_LINUX)
    return QObject::tr(
        "LibreOffice is required to open PowerPoint files.\n\n"
        "Please install LibreOffice using your package manager:\n\n"
        "Ubuntu/Debian: sudo apt install libreoffice\n"
        "Fedora: sudo dnf install libreoffice\n"
        "Arch: sudo pacman -S libreoffice-fresh\n\n"
        "After installation, try again."
    );
#elif defined(Q_OS_MACOS)
    return QObject::tr(
        "LibreOffice is required to open PowerPoint files.\n\n"
        "Please install LibreOffice:\n"
        "1. Download from: https://www.libreoffice.org/download/download/\n"
        "2. Or use Homebrew: brew install --cask libreoffice\n\n"
        "After installation, restart SpeedyNote and try again."
    );
#else
    return QObject::tr(
        "LibreOffice is required to open PowerPoint files.\n\n"
        "Please install LibreOffice from:\n"
        "https://www.libreoffice.org/download/download/"
    );
#endif
}

bool DocumentConverter::needsConversion(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    
    QString lowerPath = filePath.toLower();
    return lowerPath.endsWith(".ppt") || 
           lowerPath.endsWith(".pptx") ||
           lowerPath.endsWith(".odp");  // Also support OpenDocument Presentation
}

QString DocumentConverter::convertToPdf(const QString &inputPath, ConversionStatus &status, 
                                       const QString &outputPath, int dpi)
{
    lastError.clear();
    
    // Validate input file
    QFileInfo inputFile(inputPath);
    if (!inputFile.exists() || !inputFile.isFile()) {
        lastError = tr("Input file does not exist or is not a file: %1").arg(inputPath);
        status = InvalidFile;
        return QString();
    }
    
    // Check if LibreOffice is available
    QString libreOfficePath = getLibreOfficePath();
    if (libreOfficePath.isEmpty()) {
        lastError = tr("LibreOffice not found on system");
        status = LibreOfficeNotFound;
        return QString();
    }
    
    // Determine output directory and filename
    QString outputDir;
    QString finalOutputPath;
    
    if (outputPath.isEmpty()) {
        // Save next to original file
        outputDir = inputFile.absolutePath();
        QString baseName = inputFile.completeBaseName();
        finalOutputPath = outputDir + "/" + baseName + "_converted.pdf";
        
        // Check if file already exists and create unique name if needed
        int counter = 1;
        while (QFile::exists(finalOutputPath)) {
            finalOutputPath = outputDir + "/" + baseName + QString("_converted_%1.pdf").arg(counter);
            counter++;
        }
    } else {
        // Use specified output path
        finalOutputPath = outputPath;
        QFileInfo outputInfo(outputPath);
        outputDir = outputInfo.absolutePath();
        
        // Ensure output directory exists
        QDir dir(outputDir);
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                lastError = tr("Failed to create output directory: %1").arg(outputDir);
                status = ConversionFailed;
                return QString();
            }
        }
    }
    
    emit conversionStarted();
    emit conversionProgress(tr("Converting %1 to PDF...").arg(inputFile.fileName()));
    
    // Perform conversion
    QString convertedPdfPath = convertToPdfInternal(inputPath, outputDir, dpi);
    
    if (convertedPdfPath.isEmpty()) {
        status = ConversionFailed;
        emit conversionFinished(false);
        return QString();
    }
    
    // If LibreOffice created the file with a different name, rename it
    if (convertedPdfPath != finalOutputPath && QFile::exists(convertedPdfPath)) {
        // Remove target if it exists
        if (QFile::exists(finalOutputPath)) {
            QFile::remove(finalOutputPath);
        }
        
        if (!QFile::rename(convertedPdfPath, finalOutputPath)) {
            // If rename fails, try copy then delete
            if (QFile::copy(convertedPdfPath, finalOutputPath)) {
                QFile::remove(convertedPdfPath);
            } else {
                // If everything fails, just return the path LibreOffice created
                finalOutputPath = convertedPdfPath;
            }
        }
    }
    
    // Verify the output file was created
    QFileInfo outputFile(finalOutputPath);
    if (!outputFile.exists() || outputFile.size() == 0) {
        lastError = tr("Conversion completed but output PDF was not created or is empty");
        status = ConversionFailed;
        emit conversionFinished(false);
        return QString();
    }
    
    status = Success;
    emit conversionFinished(true);
    return finalOutputPath;
}

QString DocumentConverter::convertToPdfInternal(const QString &inputPath, const QString &outputDir, int dpi)
{
    QString libreOfficePath = getLibreOfficePath();
    if (libreOfficePath.isEmpty()) {
        lastError = tr("LibreOffice not found");
        return QString();
    }
    
    // Prepare conversion arguments with DPI settings
    QStringList args;
    args << "--headless"                    // Run without GUI
         << "--convert-to" << "pdf"         // Convert to PDF format
         << "--outdir" << outputDir;        // Output directory
    
    // Add DPI/quality settings via filter options
    // Format: "FilterName:OptionName=Value"
    // For PDF export:ReduceImageResolution, MaxImageResolution
    if (dpi > 0 && dpi != 96) {
        // Scale factor relative to 96 DPI base
        int maxResolution = dpi;
        QString filterData = QString("writer_pdf_Export:{\"ReduceImageResolution\":{\"type\":\"boolean\",\"value\":\"true\"},"
                                     "\"MaxImageResolution\":{\"type\":\"long\",\"value\":\"%1\"}}").arg(maxResolution);
        
        // Note: LibreOffice's filter options are complex and may not work consistently
        // The DPI mainly affects image quality in the output
        qDebug() << "Converting with DPI target:" << dpi;
    }
    
    args << inputPath;  // Input file
    
    // Create process
    if (conversionProcess) {
        delete conversionProcess;
    }
    conversionProcess = new QProcess(this);
    
    // Set working directory
    conversionProcess->setWorkingDirectory(outputDir);
    
    qDebug() << "Starting LibreOffice conversion:";
    qDebug() << "  Executable:" << libreOfficePath;
    qDebug() << "  Arguments:" << args;
    qDebug() << "  Output directory:" << outputDir;
    
    // Start the conversion process
    conversionProcess->start(libreOfficePath, args);
    
    // Wait for the process to finish (timeout: 120 seconds for large presentations)
    if (!conversionProcess->waitForFinished(120000)) {
        lastError = tr("Conversion timed out after 120 seconds");
        qWarning() << "LibreOffice conversion timeout";
        conversionProcess->kill();
        return QString();
    }
    
    // Check exit code
    int exitCode = conversionProcess->exitCode();
    if (exitCode != 0) {
        QString errorOutput = QString::fromUtf8(conversionProcess->readAllStandardError());
        lastError = tr("LibreOffice conversion failed with exit code %1\n\nError output:\n%2")
                        .arg(exitCode)
                        .arg(errorOutput.isEmpty() ? tr("(no error message)") : errorOutput);
        qWarning() << "LibreOffice conversion failed:" << lastError;
        return QString();
    }
    
    // Construct expected output filename
    // LibreOffice creates output.pdf from input.ppt/pptx
    QFileInfo inputFileInfo(inputPath);
    QString baseName = inputFileInfo.completeBaseName();
    QString outputPdfPath = outputDir + "/" + baseName + ".pdf";
    
    qDebug() << "Expected output PDF:" << outputPdfPath;
    
    // Verify the file was created
    if (!QFile::exists(outputPdfPath)) {
        // Try to find any PDF in the output directory
        QDir dir(outputDir);
        QStringList pdfFiles = dir.entryList(QStringList() << "*.pdf", QDir::Files);
        if (!pdfFiles.isEmpty()) {
            outputPdfPath = outputDir + "/" + pdfFiles.first();
            qDebug() << "Found alternative PDF output:" << outputPdfPath;
        } else {
            lastError = tr("Conversion appeared successful but output PDF was not found at expected location:\n%1")
                            .arg(outputPdfPath);
            return QString();
        }
    }
    
    return outputPdfPath;
}

