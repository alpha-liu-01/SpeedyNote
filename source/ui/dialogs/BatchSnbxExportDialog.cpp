#include "BatchSnbxExportDialog.h"
#include "../ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QSettings>

// ============================================================================
// Construction
// ============================================================================

BatchSnbxExportDialog::BatchSnbxExportDialog(const QStringList& bundlePaths, QWidget* parent)
    : QDialog(parent)
    , m_bundlePaths(bundlePaths)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setWindowTitle(tr("Share Notebook Package"));
#else
    setWindowTitle(tr("Export Notebook Package"));
#endif
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    setModal(true);
    
    // Detect dark mode
    m_darkMode = palette().color(QPalette::Window).lightness() < 128;
    
    // Dialog size - simpler dialog, smaller size
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
#else
    setMinimumSize(420, 280);
    setMaximumSize(600, 400);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
#endif
    
    setupUi();
    updateTitle();
    
    // Load last used settings
    QSettings settings;
    settings.beginGroup("BatchSnbxExport");
    bool lastIncludePdf = settings.value("includePdf", true).toBool();
    QString lastOutputDir = settings.value("outputDirectory").toString();
    settings.endGroup();
    
    if (m_includePdfCheckbox) {
        m_includePdfCheckbox->setChecked(lastIncludePdf);
    }
    
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if (!lastOutputDir.isEmpty() && QDir(lastOutputDir).exists()) {
        m_outputEdit->setText(lastOutputDir);
    } else {
        m_outputEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
#endif
    
    validateAndUpdateExportButton();
    
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Center the dialog
    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            move(screen->geometry().center() - rect().center());
        }
    }
#endif
}

// ============================================================================
// UI Setup
// ============================================================================

void BatchSnbxExportDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // ===== Title =====
    m_titleLabel = new QLabel();
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);
    
    // ===== Description =====
    m_descLabel = new QLabel();
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("font-size: 14px; color: palette(text);");
    m_descLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_descLabel);
    
    // ===== Output Directory (Desktop only) =====
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QGroupBox* outputGroup = new QGroupBox(tr("Output Folder"));
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    outputLayout->setSpacing(8);
    
    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText(tr("Select output folder..."));
    m_outputEdit->setMinimumHeight(36);
    connect(m_outputEdit, &QLineEdit::textChanged,
            this, &BatchSnbxExportDialog::validateAndUpdateExportButton);
    outputLayout->addWidget(m_outputEdit, 1);
    
    m_browseButton = new QPushButton(tr("Browse..."));
    m_browseButton->setMinimumHeight(36);
    m_browseButton->setMinimumWidth(90);
    connect(m_browseButton, &QPushButton::clicked,
            this, &BatchSnbxExportDialog::onBrowseClicked);
    outputLayout->addWidget(m_browseButton);
    
    mainLayout->addWidget(outputGroup);
#else
    // Android: show share note
    QLabel* shareNote = new QLabel(
        tr("The exported packages will be shared using Android's share sheet."));
    shareNote->setWordWrap(true);
    shareNote->setStyleSheet("color: palette(placeholderText); font-size: 13px; padding: 8px;");
    mainLayout->addWidget(shareNote);
#endif
    
    // ===== Options =====
    m_includePdfCheckbox = new QCheckBox(tr("Include PDF copy in package"));
    m_includePdfCheckbox->setToolTip(
        tr("Embed the source PDF file in the package.\n"
           "This makes the package larger but allows the recipient to view the original PDF."));
    m_includePdfCheckbox->setChecked(true);  // Default: include PDF
    m_includePdfCheckbox->setStyleSheet("font-size: 14px; padding: 8px;");
    m_includePdfCheckbox->setMinimumHeight(48);  // Touch-friendly
    mainLayout->addWidget(m_includePdfCheckbox);
    
    // ===== Spacer =====
    mainLayout->addStretch();
    
    // ===== Buttons =====
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16);
    
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setMinimumSize(120, 48);
    m_cancelButton->setStyleSheet(R"(
        QPushButton {
            font-size: 14px;
            padding: 12px 24px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(button);
        }
        QPushButton:hover {
            background: palette(light);
        }
        QPushButton:pressed {
            background: palette(midlight);
        }
    )");
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_exportButton = new QPushButton(tr("Share"));
#else
    m_exportButton = new QPushButton(tr("Export"));
    m_exportButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
#endif
    m_exportButton->setMinimumSize(120, 48);
    m_exportButton->setDefault(true);
    m_exportButton->setStyleSheet(R"(
        QPushButton {
            font-size: 14px;
            font-weight: bold;
            padding: 12px 24px;
            border: 2px solid #3498db;
            border-radius: 6px;
            background: #3498db;
            color: white;
        }
        QPushButton:hover {
            background: #2980b9;
            border-color: #2980b9;
        }
        QPushButton:pressed {
            background: #1f6dad;
            border-color: #1f6dad;
        }
        QPushButton:disabled {
            background: palette(midlight);
            border-color: palette(mid);
            color: palette(placeholderText);
        }
    )");
    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
        // Save settings before accepting
        QSettings settings;
        settings.beginGroup("BatchSnbxExport");
        settings.setValue("includePdf", includePdf());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        settings.setValue("outputDirectory", outputDirectory());
#endif
        settings.endGroup();
        
        accept();
    });
    buttonLayout->addWidget(m_exportButton);
    
    mainLayout->addLayout(buttonLayout);
}

void BatchSnbxExportDialog::updateTitle()
{
    int count = m_bundlePaths.size();
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (count == 1) {
        m_titleLabel->setText(tr("Share Notebook Package"));
        m_descLabel->setText(tr("Share the notebook as a .snbx package that can be imported on another device."));
    } else {
        m_titleLabel->setText(tr("Share %1 Notebook Packages").arg(count));
        m_descLabel->setText(tr("Share %1 notebooks as .snbx packages that can be imported on another device.").arg(count));
    }
#else
    if (count == 1) {
        m_titleLabel->setText(tr("Export Notebook Package"));
        m_descLabel->setText(tr("Export the notebook as a .snbx package that can be shared or transferred."));
    } else {
        m_titleLabel->setText(tr("Export %1 Notebook Packages").arg(count));
        m_descLabel->setText(tr("Export %1 notebooks as .snbx packages that can be shared or transferred.").arg(count));
    }
#endif
}

// ============================================================================
// Slots
// ============================================================================

void BatchSnbxExportDialog::onBrowseClicked()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QString currentDir = m_outputEdit->text();
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        currentDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Output Folder"),
        currentDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (!dir.isEmpty()) {
        m_outputEdit->setText(dir);
    }
#endif
}

void BatchSnbxExportDialog::validateAndUpdateExportButton()
{
    bool valid = true;
    
    // Must have at least one bundle
    if (m_bundlePaths.isEmpty()) {
        valid = false;
    }
    
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Desktop: must have output directory
    QString dir = m_outputEdit->text().trimmed();
    if (dir.isEmpty()) {
        valid = false;
    }
#endif
    
    m_exportButton->setEnabled(valid);
}

// ============================================================================
// Getters
// ============================================================================

QString BatchSnbxExportDialog::outputDirectory() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // On Android, return cache directory for temporary export
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    
    // Ensure cache directory exists
    QDir().mkpath(cacheDir);
    
    // Clean up any old exported SNBX packages to prevent disk space leaks
    // The share intent copies the file, so we can safely delete old exports
    // This runs before each new export, ensuring cleanup even if user cancelled share
    QDir dir(cacheDir);
    QStringList snbxFiles = dir.entryList(QStringList() << "*.snbx", QDir::Files);
    for (const QString& snbxFile : snbxFiles) {
        QFile::remove(dir.absoluteFilePath(snbxFile));
    }
    
    return cacheDir;
#else
    return m_outputEdit->text().trimmed();
#endif
}

bool BatchSnbxExportDialog::includePdf() const
{
    return m_includePdfCheckbox && m_includePdfCheckbox->isChecked();
}
