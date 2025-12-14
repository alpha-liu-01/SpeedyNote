#ifndef DOCUMENTCONVERTER_H
#define DOCUMENTCONVERTER_H

#include <QString>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

class DocumentConverter : public QObject {
    Q_OBJECT

public:
    enum ConversionStatus {
        Success,
        LibreOfficeNotFound,
        ConversionFailed,
        Timeout,
        InvalidFile
    };

    explicit DocumentConverter(QObject *parent = nullptr);
    ~DocumentConverter();

    // Check if LibreOffice is available on the system
    static bool isLibreOfficeAvailable();
    
    // Get the path to LibreOffice executable
    static QString getLibreOfficePath();
    
    // Get user-friendly installation instructions based on platform
    static QString getInstallationInstructions();
    
    // Convert PowerPoint file to PDF
    // outputPath: where to save the converted PDF (if empty, saves next to original file)
    // dpi: resolution for PDF rendering (default 96, higher = better quality but larger file)
    // Returns the path to the converted PDF on success, empty string on failure
    QString convertToPdf(const QString &inputPath, ConversionStatus &status, 
                        const QString &outputPath = QString(), int dpi = 96);
    
    // Check if a file needs conversion (is it a PowerPoint file?)
    static bool needsConversion(const QString &filePath);
    
    // Get the last error message
    QString getLastError() const { return lastError; }

signals:
    void conversionStarted();
    void conversionProgress(const QString &message);
    void conversionFinished(bool success);

private:
    QString findLibreOfficeExecutable();
    QString convertToPdfInternal(const QString &inputPath, const QString &outputDir, int dpi);
    
    QString lastError;
    QProcess* conversionProcess;
};

#endif // DOCUMENTCONVERTER_H

