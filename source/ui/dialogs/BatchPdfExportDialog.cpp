#include "BatchPdfExportDialog.h"
#include "../../core/Document.h"
#include "../ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

// ============================================================================
// Construction
// ============================================================================

BatchPdfExportDialog::BatchPdfExportDialog(const QStringList& bundlePaths, QWidget* parent)
    : QDialog(parent)
    , m_bundlePaths(bundlePaths)
{
#ifdef Q_OS_ANDROID
    setWindowTitle(tr("Share as PDF"));
#else
    setWindowTitle(tr("Export to PDF"));
#endif
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Detect dark mode
    m_darkMode = palette().color(QPalette::Window).lightness() < 128;
    
    // Filter out edgeless notebooks (they can't be exported to PDF)
    filterEdgelessNotebooks();
    
    // Dialog size
#ifdef Q_OS_ANDROID
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
#else
    setMinimumSize(520, 500);
    setMaximumSize(700, 650);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
#endif
    
    setupUi();
    updateTitle();
    updateWarningLabel();
    
    // Load last used settings
    QSettings settings;
    settings.beginGroup("BatchPdfExport");
    int lastDpi = settings.value("dpi", DpiDraft).toInt();
    bool lastAnnotationsOnly = settings.value("annotationsOnly", false).toBool();
    bool lastIncludeMetadata = settings.value("includeMetadata", true).toBool();
    bool lastIncludeOutline = settings.value("includeOutline", true).toBool();
    QString lastOutputDir = settings.value("outputDirectory").toString();
    settings.endGroup();
    
    // Apply saved settings
    if (lastDpi == DpiScreen) m_dpiScreenRadio->setChecked(true);
    else if (lastDpi == DpiPrint) m_dpiPrintRadio->setChecked(true);
    else if (lastDpi == DpiDraft) m_dpiDraftRadio->setChecked(true);
    else {
        m_dpiCustomRadio->setChecked(true);
        m_dpiSpinBox->setValue(lastDpi);
        m_dpiSpinBox->setEnabled(true);
    }
    
    if (m_annotationsOnlyCheckbox) m_annotationsOnlyCheckbox->setChecked(lastAnnotationsOnly);
    if (m_includeMetadataCheckbox) m_includeMetadataCheckbox->setChecked(lastIncludeMetadata);
    if (m_includeOutlineCheckbox) m_includeOutlineCheckbox->setChecked(lastIncludeOutline);
    
#ifndef Q_OS_ANDROID
    if (!lastOutputDir.isEmpty() && QDir(lastOutputDir).exists()) {
        m_outputEdit->setText(lastOutputDir);
    } else {
        m_outputEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
#endif
    
    validateAndUpdateExportButton();
    
#ifndef Q_OS_ANDROID
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

void BatchPdfExportDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // ===== Title =====
    m_titleLabel = new QLabel();
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);
    
    // ===== Warning for skipped edgeless notebooks =====
    m_warningLabel = new QLabel();
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(
        "QLabel { color: #e67e22; font-size: 13px; padding: 8px; "
        "background: rgba(230, 126, 34, 0.1); border-radius: 6px; }");
    m_warningLabel->setVisible(false);  // Hidden unless there are skipped notebooks
    mainLayout->addWidget(m_warningLabel);
    
    // ===== Output Directory (Desktop only) =====
#ifndef Q_OS_ANDROID
    QGroupBox* outputGroup = new QGroupBox(tr("Output Folder"));
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    outputLayout->setSpacing(8);
    
    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText(tr("Select output folder..."));
    m_outputEdit->setMinimumHeight(36);
    connect(m_outputEdit, &QLineEdit::textChanged,
            this, &BatchPdfExportDialog::validateAndUpdateExportButton);
    outputLayout->addWidget(m_outputEdit, 1);
    
    m_browseButton = new QPushButton(tr("Browse..."));
    m_browseButton->setMinimumHeight(36);
    m_browseButton->setMinimumWidth(90);
    connect(m_browseButton, &QPushButton::clicked,
            this, &BatchPdfExportDialog::onBrowseClicked);
    outputLayout->addWidget(m_browseButton);
    
    mainLayout->addWidget(outputGroup);
#else
    // Android: show share note
    QLabel* shareNote = new QLabel(
        tr("Exported PDFs will be shared using Android's share sheet."));
    shareNote->setWordWrap(true);
    shareNote->setStyleSheet("color: palette(mid); font-size: 13px; padding: 8px;");
    mainLayout->addWidget(shareNote);
#endif
    
    // ===== Page Range =====
    QGroupBox* pagesGroup = new QGroupBox(tr("Pages"));
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesGroup);
    pagesLayout->setSpacing(8);
    
    m_allPagesRadio = new QRadioButton(tr("All pages"));
    m_allPagesRadio->setChecked(true);
    pagesLayout->addWidget(m_allPagesRadio);
    
    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->setSpacing(8);
    
    m_pageRangeRadio = new QRadioButton(tr("Page range:"));
    rangeLayout->addWidget(m_pageRangeRadio);
    
    m_pageRangeEdit = new QLineEdit();
    m_pageRangeEdit->setPlaceholderText(tr("e.g., 1-10, 15, 20-30"));
    m_pageRangeEdit->setEnabled(false);
    m_pageRangeEdit->setMinimumHeight(32);
    connect(m_pageRangeEdit, &QLineEdit::textChanged,
            this, &BatchPdfExportDialog::validateAndUpdateExportButton);
    rangeLayout->addWidget(m_pageRangeEdit, 1);
    
    pagesLayout->addLayout(rangeLayout);
    
    // Note about page range applying to all
    QLabel* rangeNote = new QLabel(tr("Page range applies to all notebooks"));
    rangeNote->setStyleSheet("color: palette(mid); font-size: 12px;");
    pagesLayout->addWidget(rangeNote);
    
    connect(m_allPagesRadio, &QRadioButton::toggled, this, [this](bool checked) {
        onPageRangeToggled(!checked);
    });
    connect(m_pageRangeRadio, &QRadioButton::toggled, this, [this](bool checked) {
        onPageRangeToggled(checked);
    });
    
    mainLayout->addWidget(pagesGroup);
    
    // ===== Quality/DPI =====
    QGroupBox* qualityGroup = new QGroupBox(tr("Quality"));
    QGridLayout* qualityLayout = new QGridLayout(qualityGroup);
    qualityLayout->setSpacing(8);
    
    m_dpiGroup = new QButtonGroup(this);
    
    m_dpiScreenRadio = new QRadioButton(tr("96 DPI (Screen)"));
    m_dpiScreenRadio->setToolTip(tr("Smallest file size, for on-screen viewing"));
    m_dpiGroup->addButton(m_dpiScreenRadio, DpiScreen);
    qualityLayout->addWidget(m_dpiScreenRadio, 0, 0);
    
    m_dpiDraftRadio = new QRadioButton(tr("150 DPI (Standard)"));
    m_dpiDraftRadio->setToolTip(tr("Good balance between quality and file size"));
    m_dpiDraftRadio->setChecked(true);  // Default
    m_dpiGroup->addButton(m_dpiDraftRadio, DpiDraft);
    qualityLayout->addWidget(m_dpiDraftRadio, 0, 1);
    
    m_dpiPrintRadio = new QRadioButton(tr("300 DPI (Print)"));
    m_dpiPrintRadio->setToolTip(tr("High quality for printing"));
    m_dpiGroup->addButton(m_dpiPrintRadio, DpiPrint);
    qualityLayout->addWidget(m_dpiPrintRadio, 1, 0);
    
    QHBoxLayout* customDpiLayout = new QHBoxLayout();
    customDpiLayout->setSpacing(8);
    
    m_dpiCustomRadio = new QRadioButton(tr("Custom:"));
    m_dpiGroup->addButton(m_dpiCustomRadio, DpiCustom);
    customDpiLayout->addWidget(m_dpiCustomRadio);
    
    m_dpiSpinBox = new QSpinBox();
    m_dpiSpinBox->setRange(72, 600);
    m_dpiSpinBox->setValue(300);
    m_dpiSpinBox->setSuffix(tr(" DPI"));
    m_dpiSpinBox->setEnabled(false);
    m_dpiSpinBox->setMinimumWidth(100);
    customDpiLayout->addWidget(m_dpiSpinBox);
    customDpiLayout->addStretch();
    
    qualityLayout->addLayout(customDpiLayout, 1, 1);
    
    connect(m_dpiGroup, &QButtonGroup::idClicked,
            this, &BatchPdfExportDialog::onDpiPresetChanged);
    
    mainLayout->addWidget(qualityGroup);
    
    // ===== Options =====
    QGroupBox* optionsGroup = new QGroupBox(tr("Options"));
    QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setSpacing(8);
    
    m_annotationsOnlyCheckbox = new QCheckBox(tr("Annotations only (blank background)"));
    m_annotationsOnlyCheckbox->setToolTip(
        tr("Export strokes and images only, without original PDF content or page backgrounds."));
    optionsLayout->addWidget(m_annotationsOnlyCheckbox);
    
    m_includeMetadataCheckbox = new QCheckBox(tr("Include PDF metadata"));
    m_includeMetadataCheckbox->setToolTip(tr("Preserve title, author, and other metadata from source PDFs."));
    m_includeMetadataCheckbox->setChecked(true);
    optionsLayout->addWidget(m_includeMetadataCheckbox);
    
    m_includeOutlineCheckbox = new QCheckBox(tr("Include bookmarks/outline"));
    m_includeOutlineCheckbox->setToolTip(tr("Preserve PDF bookmarks and outline from source PDFs."));
    m_includeOutlineCheckbox->setChecked(true);
    optionsLayout->addWidget(m_includeOutlineCheckbox);
    
    mainLayout->addWidget(optionsGroup);
    
    // ===== Spacer =====
    mainLayout->addStretch();
    
    // ===== Buttons =====
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelButton->setMinimumSize(100, 40);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);
    
#ifdef Q_OS_ANDROID
    m_exportButton = new QPushButton(tr("Share"));
#else
    m_exportButton = new QPushButton(tr("Export"));
    m_exportButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
#endif
    m_exportButton->setMinimumSize(100, 40);
    m_exportButton->setDefault(true);
    m_exportButton->setStyleSheet(R"(
        QPushButton {
            font-weight: bold;
            background: #3498db;
            color: white;
            border: 2px solid #3498db;
            border-radius: 6px;
            padding: 8px 16px;
        }
        QPushButton:hover {
            background: #2980b9;
            border-color: #2980b9;
        }
        QPushButton:pressed {
            background: #2471a3;
            border-color: #2471a3;
        }
        QPushButton:disabled {
            background: palette(mid);
            border-color: palette(mid);
            color: palette(dark);
        }
    )");
    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
        // Save settings before accepting
        QSettings settings;
        settings.beginGroup("BatchPdfExport");
        settings.setValue("dpi", dpi());
        settings.setValue("annotationsOnly", annotationsOnly());
        settings.setValue("includeMetadata", includeMetadata());
        settings.setValue("includeOutline", includeOutline());
#ifndef Q_OS_ANDROID
        settings.setValue("outputDirectory", outputDirectory());
#endif
        settings.endGroup();
        
        accept();
    });
    buttonLayout->addWidget(m_exportButton);
    
    mainLayout->addLayout(buttonLayout);
}

// ============================================================================
// Edgeless Filtering
// ============================================================================

void BatchPdfExportDialog::filterEdgelessNotebooks()
{
    m_validBundles.clear();
    m_skippedBundles.clear();
    
    for (const QString& bundlePath : m_bundlePaths) {
        // Load document metadata to check if edgeless
        // Use a lightweight check - just read the document.json
        QString docJsonPath = bundlePath + "/document.json";
        QFile file(docJsonPath);
        
        bool isEdgeless = false;
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString mode = obj.value("mode").toString();
                isEdgeless = (mode == "edgeless");
            }
        }
        
        if (isEdgeless) {
            m_skippedBundles.append(bundlePath);
        } else {
            m_validBundles.append(bundlePath);
        }
    }
}

void BatchPdfExportDialog::updateTitle()
{
    int count = m_validBundles.size();
    
#ifdef Q_OS_ANDROID
    if (count == 1) {
        m_titleLabel->setText(tr("Share Notebook as PDF"));
    } else {
        m_titleLabel->setText(tr("Share %1 Notebooks as PDF").arg(count));
    }
#else
    if (count == 1) {
        m_titleLabel->setText(tr("Export Notebook to PDF"));
    } else {
        m_titleLabel->setText(tr("Export %1 Notebooks to PDF").arg(count));
    }
#endif
}

void BatchPdfExportDialog::updateWarningLabel()
{
    int skipped = m_skippedBundles.size();
    
    if (skipped == 0) {
        m_warningLabel->setVisible(false);
        return;
    }
    
    if (skipped == 1) {
        m_warningLabel->setText(
            tr("⚠ 1 edgeless notebook will be skipped (edgeless notebooks cannot be exported to PDF)."));
    } else {
        m_warningLabel->setText(
            tr("⚠ %1 edgeless notebooks will be skipped (edgeless notebooks cannot be exported to PDF).").arg(skipped));
    }
    m_warningLabel->setVisible(true);
}

// ============================================================================
// Slots
// ============================================================================

void BatchPdfExportDialog::onBrowseClicked()
{
#ifndef Q_OS_ANDROID
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

void BatchPdfExportDialog::onPageRangeToggled(bool rangeSelected)
{
    m_pageRangeEdit->setEnabled(rangeSelected);
    if (rangeSelected) {
        m_pageRangeEdit->setFocus();
    }
    validateAndUpdateExportButton();
}

void BatchPdfExportDialog::onDpiPresetChanged()
{
    bool customSelected = m_dpiCustomRadio->isChecked();
    m_dpiSpinBox->setEnabled(customSelected);
    if (customSelected) {
        m_dpiSpinBox->setFocus();
        m_dpiSpinBox->selectAll();
    }
}

void BatchPdfExportDialog::validateAndUpdateExportButton()
{
    bool valid = true;
    
    // Must have at least one valid bundle
    if (m_validBundles.isEmpty()) {
        valid = false;
    }
    
#ifndef Q_OS_ANDROID
    // Desktop: must have output directory
    QString dir = m_outputEdit->text().trimmed();
    if (dir.isEmpty()) {
        valid = false;
    }
#endif
    
    // Page range must not be empty if selected
    if (m_pageRangeRadio->isChecked()) {
        QString range = m_pageRangeEdit->text().trimmed();
        if (range.isEmpty()) {
            valid = false;
        }
    }
    
    m_exportButton->setEnabled(valid);
}

// ============================================================================
// Getters
// ============================================================================

QString BatchPdfExportDialog::outputDirectory() const
{
#ifdef Q_OS_ANDROID
    // On Android, return cache directory for temporary export
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    
    // Ensure cache directory exists
    QDir().mkpath(cacheDir);
    
    // Clean up any old exported PDFs to prevent disk space leaks
    // The share intent copies the file, so we can safely delete old exports
    // This runs before each new export, ensuring cleanup even if user cancelled share
    QDir dir(cacheDir);
    QStringList pdfFiles = dir.entryList(QStringList() << "*.pdf", QDir::Files);
    for (const QString& pdfFile : pdfFiles) {
        QFile::remove(dir.absoluteFilePath(pdfFile));
    }
    
    return cacheDir;
#else
    return m_outputEdit->text().trimmed();
#endif
}

int BatchPdfExportDialog::dpi() const
{
    if (m_dpiScreenRadio->isChecked()) return DpiScreen;
    if (m_dpiDraftRadio->isChecked()) return DpiDraft;
    if (m_dpiPrintRadio->isChecked()) return DpiPrint;
    return m_dpiSpinBox->value();
}

QString BatchPdfExportDialog::pageRange() const
{
    if (m_allPagesRadio->isChecked()) {
        return QString();
    }
    return m_pageRangeEdit->text().trimmed();
}

bool BatchPdfExportDialog::isAllPages() const
{
    return m_allPagesRadio->isChecked();
}

bool BatchPdfExportDialog::annotationsOnly() const
{
    return m_annotationsOnlyCheckbox && m_annotationsOnlyCheckbox->isChecked();
}

bool BatchPdfExportDialog::includeMetadata() const
{
    return m_includeMetadataCheckbox && m_includeMetadataCheckbox->isChecked();
}

bool BatchPdfExportDialog::includeOutline() const
{
    return m_includeOutlineCheckbox && m_includeOutlineCheckbox->isChecked();
}

QStringList BatchPdfExportDialog::validBundles() const
{
    return m_validBundles;
}

QStringList BatchPdfExportDialog::skippedBundles() const
{
    return m_skippedBundles;
}
