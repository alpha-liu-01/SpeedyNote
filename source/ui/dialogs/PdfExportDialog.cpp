#include "PdfExportDialog.h"

#include "../../core/Document.h"
#include "../../pdf/MuPdfExporter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QGroupBox>

// ============================================================================
// Construction
// ============================================================================

PdfExportDialog::PdfExportDialog(Document* document, QWidget* parent)
    : QDialog(parent)
    , m_document(document)
{
    setWindowTitle(tr("Export to PDF"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Reasonable dialog size
    setMinimumSize(500, 420);
    setMaximumSize(700, 550);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
    setupUI();
    
    // Set default output path
    m_outputEdit->setText(generateDefaultFilename());
    
    // Initial validation
    validateAndUpdateExportButton();
    
    // Center the dialog
    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            move(screen->geometry().center() - rect().center());
        }
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void PdfExportDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // ===== Title =====
    QLabel* titleLabel = new QLabel(tr("Export to PDF"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);
    
    mainLayout->addSpacing(8);
    
    // ===== Output Path Section =====
    QGroupBox* outputGroup = new QGroupBox(tr("Output File"));
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    outputLayout->setSpacing(8);
    
    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText(tr("Select output file..."));
    m_outputEdit->setMinimumHeight(36);
    connect(m_outputEdit, &QLineEdit::textChanged, 
            this, &PdfExportDialog::validateAndUpdateExportButton);
    outputLayout->addWidget(m_outputEdit, 1);
    
    m_browseBtn = new QPushButton(tr("Browse..."));
    m_browseBtn->setMinimumHeight(36);
    m_browseBtn->setMinimumWidth(90);
    connect(m_browseBtn, &QPushButton::clicked, 
            this, &PdfExportDialog::onBrowseClicked);
    outputLayout->addWidget(m_browseBtn);
    
    mainLayout->addWidget(outputGroup);
    
    // ===== Page Range Section =====
    QGroupBox* pagesGroup = new QGroupBox(tr("Pages"));
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesGroup);
    pagesLayout->setSpacing(8);
    
    // All pages radio
    m_allPagesRadio = new QRadioButton(tr("All pages"));
    m_allPagesRadio->setChecked(true);
    pagesLayout->addWidget(m_allPagesRadio);
    
    // Page range radio with input
    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->setSpacing(8);
    
    m_pageRangeRadio = new QRadioButton(tr("Page range:"));
    rangeLayout->addWidget(m_pageRangeRadio);
    
    m_pageRangeEdit = new QLineEdit();
    m_pageRangeEdit->setPlaceholderText(tr("e.g., 1-10, 15, 20-30"));
    m_pageRangeEdit->setEnabled(false);  // Disabled until range is selected
    m_pageRangeEdit->setMinimumHeight(32);
    connect(m_pageRangeEdit, &QLineEdit::textChanged,
            this, &PdfExportDialog::validateAndUpdateExportButton);
    rangeLayout->addWidget(m_pageRangeEdit, 1);
    
    pagesLayout->addLayout(rangeLayout);
    
    // Page count hint
    int pageCount = m_document ? m_document->pageCount() : 0;
    m_pageCountLabel = new QLabel(tr("Document has %n page(s)", "", pageCount));
    m_pageCountLabel->setStyleSheet("color: palette(mid); font-size: 12px;");
    pagesLayout->addWidget(m_pageCountLabel);
    
    // Connect radio buttons
    connect(m_allPagesRadio, &QRadioButton::toggled, this, [this](bool checked) {
        onPageRangeToggled(!checked);
    });
    connect(m_pageRangeRadio, &QRadioButton::toggled, this, [this](bool checked) {
        onPageRangeToggled(checked);
    });
    
    mainLayout->addWidget(pagesGroup);
    
    // ===== Quality/DPI Section =====
    QGroupBox* qualityGroup = new QGroupBox(tr("Quality"));
    QGridLayout* qualityLayout = new QGridLayout(qualityGroup);
    qualityLayout->setSpacing(8);
    
    // Create button group for DPI presets
    m_dpiGroup = new QButtonGroup(this);
    
    // Screen quality (96 DPI)
    m_dpiScreenRadio = new QRadioButton(tr("96 DPI (Screen)"));
    m_dpiScreenRadio->setToolTip(tr("Smallest file size, suitable for on-screen viewing"));
    m_dpiGroup->addButton(m_dpiScreenRadio, DpiScreen);
    qualityLayout->addWidget(m_dpiScreenRadio, 0, 0);
    
    // Draft quality (150 DPI)
    m_dpiDraftRadio = new QRadioButton(tr("150 DPI (Draft)"));
    m_dpiDraftRadio->setToolTip(tr("Good balance between quality and file size"));
    m_dpiGroup->addButton(m_dpiDraftRadio, DpiDraft);
    qualityLayout->addWidget(m_dpiDraftRadio, 0, 1);
    
    // Print quality (300 DPI) - default
    m_dpiPrintRadio = new QRadioButton(tr("300 DPI (Print)"));
    m_dpiPrintRadio->setToolTip(tr("High quality, suitable for printing"));
    m_dpiPrintRadio->setChecked(true);
    m_dpiGroup->addButton(m_dpiPrintRadio, DpiPrint);
    qualityLayout->addWidget(m_dpiPrintRadio, 1, 0);
    
    // Custom DPI
    QHBoxLayout* customDpiLayout = new QHBoxLayout();
    customDpiLayout->setSpacing(8);
    
    m_dpiCustomRadio = new QRadioButton(tr("Custom:"));
    m_dpiGroup->addButton(m_dpiCustomRadio, DpiCustom);
    customDpiLayout->addWidget(m_dpiCustomRadio);
    
    m_dpiSpinBox = new QSpinBox();
    m_dpiSpinBox->setRange(72, 600);
    m_dpiSpinBox->setValue(300);
    m_dpiSpinBox->setSuffix(tr(" DPI"));
    m_dpiSpinBox->setEnabled(false);  // Enabled only when custom is selected
    m_dpiSpinBox->setMinimumWidth(100);
    customDpiLayout->addWidget(m_dpiSpinBox);
    customDpiLayout->addStretch();
    
    qualityLayout->addLayout(customDpiLayout, 1, 1);
    
    // Connect DPI preset changes
    connect(m_dpiGroup, &QButtonGroup::idClicked,
            this, &PdfExportDialog::onDpiPresetChanged);
    
    mainLayout->addWidget(qualityGroup);
    
    // ===== Spacer =====
    mainLayout->addStretch();
    
    // ===== Action Buttons =====
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    
    buttonLayout->addStretch();
    
    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelBtn->setMinimumSize(100, 40);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelBtn);
    
    m_exportBtn = new QPushButton(tr("Export"));
    m_exportBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_exportBtn->setMinimumSize(100, 40);
    m_exportBtn->setDefault(true);
    m_exportBtn->setStyleSheet(R"(
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
    connect(m_exportBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_exportBtn);
    
    mainLayout->addLayout(buttonLayout);
}

// ============================================================================
// Slots
// ============================================================================

void PdfExportDialog::onBrowseClicked()
{
    // Default to Documents folder
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    
    // If there's already a path, use its directory
    QString currentPath = m_outputEdit->text();
    if (!currentPath.isEmpty()) {
        QFileInfo info(currentPath);
        if (info.absoluteDir().exists()) {
            defaultDir = info.absolutePath();
        }
    }
    
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export PDF"),
        defaultDir + "/" + QFileInfo(currentPath).fileName(),
        tr("PDF Files (*.pdf);;All Files (*)")
    );
    
    if (!filePath.isEmpty()) {
        // Ensure .pdf extension
        if (!filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
            filePath += ".pdf";
        }
        m_outputEdit->setText(filePath);
    }
}

void PdfExportDialog::onPageRangeToggled(bool rangeSelected)
{
    m_pageRangeEdit->setEnabled(rangeSelected);
    if (rangeSelected) {
        m_pageRangeEdit->setFocus();
    }
    validateAndUpdateExportButton();
}

void PdfExportDialog::onDpiPresetChanged()
{
    // Enable spinbox only when custom is selected
    bool customSelected = m_dpiCustomRadio->isChecked();
    m_dpiSpinBox->setEnabled(customSelected);
    if (customSelected) {
        m_dpiSpinBox->setFocus();
        m_dpiSpinBox->selectAll();
    }
}

void PdfExportDialog::validateAndUpdateExportButton()
{
    bool valid = true;
    
    // Check output path
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        valid = false;
    }
    
    // Check page range if selected
    if (m_pageRangeRadio->isChecked()) {
        QString range = m_pageRangeEdit->text().trimmed();
        if (range.isEmpty()) {
            valid = false;
        }
        // Note: Don't validate page range syntax during typing - it causes
        // spurious warnings for incomplete input like "3-". Full validation
        // happens when the user clicks Export via pageRange() and parsePageRange().
    }
    
    m_exportBtn->setEnabled(valid);
}

// ============================================================================
// Getters
// ============================================================================

QString PdfExportDialog::outputPath() const
{
    QString path = m_outputEdit->text().trimmed();
    
    // Ensure .pdf extension
    if (!path.isEmpty() && !path.endsWith(".pdf", Qt::CaseInsensitive)) {
        path += ".pdf";
    }
    
    return path;
}

QString PdfExportDialog::pageRange() const
{
    if (m_allPagesRadio->isChecked()) {
        return QString();  // Empty means all pages
    }
    return m_pageRangeEdit->text().trimmed();
}

int PdfExportDialog::dpi() const
{
    if (m_dpiScreenRadio->isChecked()) {
        return DpiScreen;
    } else if (m_dpiDraftRadio->isChecked()) {
        return DpiDraft;
    } else if (m_dpiPrintRadio->isChecked()) {
        return DpiPrint;
    } else {
        return m_dpiSpinBox->value();
    }
}

bool PdfExportDialog::isAllPages() const
{
    return m_allPagesRadio->isChecked();
}

QString PdfExportDialog::generateDefaultFilename() const
{
    QString baseName = "exported";
    
    if (m_document) {
        // Use document name if available
        QString docName = m_document->name;
        if (!docName.isEmpty()) {
            baseName = docName;
        }
    }
    
    // Get default documents directory
    QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    
    return docsDir + "/" + baseName + "_exported.pdf";
}

