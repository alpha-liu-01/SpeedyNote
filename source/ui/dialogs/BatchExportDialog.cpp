#include "BatchExportDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>

BatchExportDialog::BatchExportDialog(const QStringList& bundlePaths,
                                     QWidget* parent,
                                     ExportFormat initialFormat)
    : QDialog(parent)
    , m_bundlePaths(bundlePaths)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setWindowTitle(bundlePaths.size() == 1
        ? tr("Share Notebook")
        : tr("Share Notebooks"));
#else
    setWindowTitle(bundlePaths.size() == 1
        ? tr("Export Notebook")
        : tr("Export Notebooks"));
#endif
    setWindowIcon(QIcon(QStringLiteral(":/resources/icons/mainicon.svg")));
    setModal(true);

    filterEdgelessNotebooks();
    setupUi();
    loadSettings();

    const int requestedTab = initialFormat == Snbx ? 1 : 0;
    m_tabs->setCurrentIndex(
        requestedTab == 0 && m_validPdfBundles.isEmpty() ? 1 : requestedTab);
    validateExportButton();

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    setMinimumSize(560, 720);
    resize(600, 780);
    setMaximumSize(720, 880);
    if (parent) move(parent->geometry().center() - rect().center());
#endif
}

void BatchExportDialog::filterEdgelessNotebooks()
{
    for (const QString& bundlePath : m_bundlePaths) {
        QFile file(QDir(bundlePath).absoluteFilePath(QStringLiteral("document.json")));
        bool edgeless = false;
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
            edgeless = json.isObject()
                && json.object().value(QStringLiteral("mode")).toString()
                    == QStringLiteral("edgeless");
        }
        (edgeless ? m_skippedPdfBundles : m_validPdfBundles).append(bundlePath);
    }
}

void BatchExportDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(14);

    auto* heading = new QLabel(
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        tr("Choose how to share the selected notebook(s)."),
#else
        tr("Choose an export format and configure its options."),
#endif
        this);
    heading->setWordWrap(true);
    heading->setAlignment(Qt::AlignCenter);
    heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
    mainLayout->addWidget(heading);

    m_tabs = new QTabWidget(this);
    m_pdfTab = createPdfTab();
    m_snbxTab = createSnbxTab();
    m_tabs->addTab(m_pdfTab, tr("PDF"));
    m_tabs->addTab(m_snbxTab, tr("Notebook Package (.snbx)"));
    m_tabs->setTabEnabled(0, !m_validPdfBundles.isEmpty());
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &BatchExportDialog::validateExportButton);
    mainLayout->addWidget(m_tabs, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    cancelButton->setMinimumSize(100, 40);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_exportButton = new QPushButton(tr("Share"), this);
#else
    m_exportButton = new QPushButton(tr("Export"), this);
    m_exportButton->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
#endif
    m_exportButton->setMinimumSize(100, 40);
    m_exportButton->setDefault(true);
    m_exportButton->setStyleSheet(QStringLiteral(
        "QPushButton { font-weight: bold; background: #3498db; color: white;"
        " border: 2px solid #3498db; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton:hover { background: #2980b9; border-color: #2980b9; }"
        "QPushButton:pressed { background: #2471a3; border-color: #2471a3; }"
        "QPushButton:disabled { background: palette(midlight);"
        " border-color: palette(mid); color: palette(placeholderText); }"));
    connect(m_exportButton, &QPushButton::clicked,
            this, &BatchExportDialog::acceptExport);
    buttonLayout->addWidget(m_exportButton);
    mainLayout->addLayout(buttonLayout);
}

QWidget* BatchExportDialog::createPdfTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 14, 12, 12);
    layout->setSpacing(10);

    auto* description = new QLabel(
        tr("Export paged notebooks as PDF documents."), tab);
    description->setAlignment(Qt::AlignCenter);
    layout->addWidget(description);

    m_pdfWarningLabel = new QLabel(tab);
    m_pdfWarningLabel->setWordWrap(true);
    m_pdfWarningLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #e67e22; font-size: 13px; padding: 8px;"
        " background: rgba(230, 126, 34, 0.1); border-radius: 6px; }"));
    const int skipped = m_skippedPdfBundles.size();
    if (skipped > 0) {
        m_pdfWarningLabel->setText(skipped == 1
            ? tr("1 edgeless notebook will be skipped because it cannot be exported to PDF.")
            : tr("%1 edgeless notebooks will be skipped because they cannot be exported to PDF.")
                  .arg(skipped));
    } else {
        m_pdfWarningLabel->hide();
    }
    layout->addWidget(m_pdfWarningLabel);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    auto* outputGroup = new QGroupBox(tr("Output Folder"), tab);
    auto* outputLayout = new QHBoxLayout(outputGroup);
    m_pdfOutputEdit = new QLineEdit(outputGroup);
    m_pdfOutputEdit->setPlaceholderText(tr("Select output folder..."));
    m_pdfOutputEdit->setMinimumHeight(34);
    connect(m_pdfOutputEdit, &QLineEdit::textChanged,
            this, &BatchExportDialog::validateExportButton);
    outputLayout->addWidget(m_pdfOutputEdit, 1);
    auto* browse = new QPushButton(tr("Browse..."), outputGroup);
    browse->setMinimumHeight(34);
    connect(browse, &QPushButton::clicked,
            this, &BatchExportDialog::browsePdfOutput);
    outputLayout->addWidget(browse);
    layout->addWidget(outputGroup);
#else
    auto* shareNote = new QLabel(
        tr("Exported PDFs will be sent to the system share sheet."), tab);
    shareNote->setWordWrap(true);
    layout->addWidget(shareNote);
#endif

    auto* pagesGroup = new QGroupBox(tr("Pages"), tab);
    auto* pagesLayout = new QVBoxLayout(pagesGroup);
    m_allPagesRadio = new QRadioButton(tr("All pages"), pagesGroup);
    m_allPagesRadio->setChecked(true);
    pagesLayout->addWidget(m_allPagesRadio);
    auto* rangeLayout = new QHBoxLayout();
    m_pageRangeRadio = new QRadioButton(tr("Page range:"), pagesGroup);
    rangeLayout->addWidget(m_pageRangeRadio);
    m_pageRangeEdit = new QLineEdit(pagesGroup);
    m_pageRangeEdit->setPlaceholderText(tr("e.g., 1-10, 15, 20-30"));
    m_pageRangeEdit->setEnabled(false);
    connect(m_pageRangeEdit, &QLineEdit::textChanged,
            this, &BatchExportDialog::validateExportButton);
    rangeLayout->addWidget(m_pageRangeEdit, 1);
    pagesLayout->addLayout(rangeLayout);
    auto* rangeNote = new QLabel(tr("Page range applies to all notebooks"), pagesGroup);
    rangeNote->setStyleSheet(
        QStringLiteral("color: palette(placeholderText); font-size: 12px;"));
    pagesLayout->addWidget(rangeNote);
    connect(m_allPagesRadio, &QRadioButton::toggled, this, [this](bool checked) {
        onPageRangeToggled(!checked);
    });
    connect(m_pageRangeRadio, &QRadioButton::toggled, this,
            &BatchExportDialog::onPageRangeToggled);
    layout->addWidget(pagesGroup);

    auto* qualityGroup = new QGroupBox(tr("Quality"), tab);
    auto* qualityLayout = new QGridLayout(qualityGroup);
    m_dpiGroup = new QButtonGroup(this);
    m_dpiScreenRadio = new QRadioButton(tr("96 DPI (Screen)"), qualityGroup);
    m_dpiDraftRadio = new QRadioButton(tr("150 DPI (Standard)"), qualityGroup);
    m_dpiPrintRadio = new QRadioButton(tr("300 DPI (Print)"), qualityGroup);
    m_dpiCustomRadio = new QRadioButton(tr("Custom:"), qualityGroup);
    m_dpiDraftRadio->setChecked(true);
    m_dpiGroup->addButton(m_dpiScreenRadio, DpiScreen);
    m_dpiGroup->addButton(m_dpiDraftRadio, DpiDraft);
    m_dpiGroup->addButton(m_dpiPrintRadio, DpiPrint);
    m_dpiGroup->addButton(m_dpiCustomRadio, DpiCustom);
    qualityLayout->addWidget(m_dpiScreenRadio, 0, 0);
    qualityLayout->addWidget(m_dpiDraftRadio, 0, 1);
    qualityLayout->addWidget(m_dpiPrintRadio, 1, 0);
    auto* customLayout = new QHBoxLayout();
    customLayout->addWidget(m_dpiCustomRadio);
    m_dpiSpinBox = new QSpinBox(qualityGroup);
    m_dpiSpinBox->setRange(72, 600);
    m_dpiSpinBox->setValue(300);
    m_dpiSpinBox->setSuffix(tr(" DPI"));
    m_dpiSpinBox->setEnabled(false);
    customLayout->addWidget(m_dpiSpinBox);
    customLayout->addStretch();
    qualityLayout->addLayout(customLayout, 1, 1);
    connect(m_dpiGroup, &QButtonGroup::idClicked,
            this, &BatchExportDialog::onDpiPresetChanged);
    layout->addWidget(qualityGroup);

    auto* optionsGroup = new QGroupBox(tr("Options"), tab);
    auto* optionsLayout = new QVBoxLayout(optionsGroup);
    m_annotationsOnlyCheckbox =
        new QCheckBox(tr("Annotations only (blank background)"), optionsGroup);
    m_darkModeBgCheckbox =
        new QCheckBox(tr("Render PDF background in dark mode"), optionsGroup);
    m_darkenStrokesCheckbox =
        new QCheckBox(tr("Darken light-coloured strokes for printing"), optionsGroup);
    m_includeMetadataCheckbox =
        new QCheckBox(tr("Include PDF metadata"), optionsGroup);
    m_includeOutlineCheckbox =
        new QCheckBox(tr("Include bookmarks/outline"), optionsGroup);
    m_includeMetadataCheckbox->setChecked(true);
    m_includeOutlineCheckbox->setChecked(true);
    optionsLayout->addWidget(m_annotationsOnlyCheckbox);
    optionsLayout->addWidget(m_darkModeBgCheckbox);
    optionsLayout->addWidget(m_darkenStrokesCheckbox);
    optionsLayout->addWidget(m_includeMetadataCheckbox);
    optionsLayout->addWidget(m_includeOutlineCheckbox);
    connect(m_annotationsOnlyCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_darkModeBgCheckbox->setChecked(false);
        m_darkModeBgCheckbox->setEnabled(!checked);
    });
    connect(m_darkModeBgCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_annotationsOnlyCheckbox->setChecked(false);
        m_annotationsOnlyCheckbox->setEnabled(!checked);
    });
    layout->addWidget(optionsGroup);
    layout->addStretch();
    return tab;
}

QWidget* BatchExportDialog::createSnbxTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 18, 12, 12);
    layout->setSpacing(16);

    auto* description = new QLabel(
        tr("Export notebook packages for backup, sharing, or transfer to another device."),
        tab);
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    layout->addWidget(description);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    auto* outputGroup = new QGroupBox(tr("Output Folder"), tab);
    auto* outputLayout = new QHBoxLayout(outputGroup);
    m_snbxOutputEdit = new QLineEdit(outputGroup);
    m_snbxOutputEdit->setPlaceholderText(tr("Select output folder..."));
    m_snbxOutputEdit->setMinimumHeight(36);
    connect(m_snbxOutputEdit, &QLineEdit::textChanged,
            this, &BatchExportDialog::validateExportButton);
    outputLayout->addWidget(m_snbxOutputEdit, 1);
    auto* browse = new QPushButton(tr("Browse..."), outputGroup);
    browse->setMinimumHeight(36);
    connect(browse, &QPushButton::clicked,
            this, &BatchExportDialog::browseSnbxOutput);
    outputLayout->addWidget(browse);
    layout->addWidget(outputGroup);
#else
    auto* shareNote = new QLabel(
        tr("Exported packages will be sent to the system share sheet."), tab);
    shareNote->setWordWrap(true);
    layout->addWidget(shareNote);
#endif

    m_includePdfCheckbox =
        new QCheckBox(tr("Include PDF copy in package"), tab);
    m_includePdfCheckbox->setToolTip(
        tr("Embed the source PDF content so the package remains portable."));
    m_includePdfCheckbox->setChecked(true);
    m_includePdfCheckbox->setMinimumHeight(48);
    layout->addWidget(m_includePdfCheckbox);
    layout->addStretch();
    return tab;
}

void BatchExportDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("BatchPdfExport"));
    const int lastDpi = settings.value(QStringLiteral("dpi"), DpiDraft).toInt();
    m_annotationsOnlyCheckbox->setChecked(
        settings.value(QStringLiteral("annotationsOnly"), false).toBool());
    m_darkModeBgCheckbox->setChecked(
        settings.value(QStringLiteral("darkModeBackground"), false).toBool());
    m_darkenStrokesCheckbox->setChecked(
        settings.value(QStringLiteral("darkenStrokes"), false).toBool());
    m_includeMetadataCheckbox->setChecked(
        settings.value(QStringLiteral("includeMetadata"), true).toBool());
    m_includeOutlineCheckbox->setChecked(
        settings.value(QStringLiteral("includeOutline"), true).toBool());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QString pdfOutput = settings.value(QStringLiteral("outputDirectory")).toString();
#endif
    settings.endGroup();

    if (lastDpi == DpiScreen) m_dpiScreenRadio->setChecked(true);
    else if (lastDpi == DpiPrint) m_dpiPrintRadio->setChecked(true);
    else if (lastDpi == DpiDraft) m_dpiDraftRadio->setChecked(true);
    else {
        m_dpiCustomRadio->setChecked(true);
        m_dpiSpinBox->setValue(lastDpi);
        m_dpiSpinBox->setEnabled(true);
    }

    settings.beginGroup(QStringLiteral("BatchSnbxExport"));
    m_includePdfCheckbox->setChecked(
        settings.value(QStringLiteral("includePdf"), true).toBool());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QString snbxOutput = settings.value(QStringLiteral("outputDirectory")).toString();
#endif
    settings.endGroup();

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_pdfOutputEdit->setText(
        !pdfOutput.isEmpty() && QDir(pdfOutput).exists() ? pdfOutput : documents);
    m_snbxOutputEdit->setText(
        !snbxOutput.isEmpty() && QDir(snbxOutput).exists() ? snbxOutput : documents);
#endif
}

void BatchExportDialog::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("BatchPdfExport"));
    settings.setValue(QStringLiteral("dpi"), dpi());
    settings.setValue(QStringLiteral("annotationsOnly"), annotationsOnly());
    settings.setValue(QStringLiteral("darkModeBackground"), darkModeBackground());
    settings.setValue(QStringLiteral("darkenStrokes"), darkenStrokes());
    settings.setValue(QStringLiteral("includeMetadata"), includeMetadata());
    settings.setValue(QStringLiteral("includeOutline"), includeOutline());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    settings.setValue(QStringLiteral("outputDirectory"), m_pdfOutputEdit->text().trimmed());
#endif
    settings.endGroup();

    settings.beginGroup(QStringLiteral("BatchSnbxExport"));
    settings.setValue(QStringLiteral("includePdf"), includePdf());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    settings.setValue(QStringLiteral("outputDirectory"), m_snbxOutputEdit->text().trimmed());
#endif
    settings.endGroup();
}

BatchExportDialog::ExportFormat BatchExportDialog::selectedFormat() const
{
    return m_tabs && m_tabs->currentWidget() == m_snbxTab ? Snbx : Pdf;
}

QString BatchExportDialog::outputDirectory() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return mobileOutputDirectory(selectedFormat() == Pdf
        ? QStringLiteral("pdf") : QStringLiteral("snbx"));
#else
    QLineEdit* edit = selectedFormat() == Pdf ? m_pdfOutputEdit : m_snbxOutputEdit;
    return edit ? edit->text().trimmed() : QString();
#endif
}

QString BatchExportDialog::mobileOutputDirectory(const QString& extension) const
{
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    QDir dir(cacheDir);
    const QStringList oldExports = dir.entryList(
        {QStringLiteral("*.%1").arg(extension)}, QDir::Files);
    for (const QString& file : oldExports) QFile::remove(dir.absoluteFilePath(file));
    return cacheDir;
}

void BatchExportDialog::browsePdfOutput()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    chooseOutputDirectory(m_pdfOutputEdit);
#endif
}

void BatchExportDialog::browseSnbxOutput()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    chooseOutputDirectory(m_snbxOutputEdit);
#endif
}

QString BatchExportDialog::chooseOutputDirectory(QLineEdit* edit) const
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QString current = edit ? edit->text().trimmed() : QString();
    if (current.isEmpty() || !QDir(current).exists()) {
        current = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString selected = QFileDialog::getExistingDirectory(
        const_cast<BatchExportDialog*>(this), tr("Select Output Folder"), current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (edit && !selected.isEmpty()) edit->setText(selected);
    return selected;
#else
    Q_UNUSED(edit)
    return QString();
#endif
}

void BatchExportDialog::onPageRangeToggled(bool rangeSelected)
{
    m_pageRangeEdit->setEnabled(rangeSelected);
    if (rangeSelected) m_pageRangeEdit->setFocus();
    validateExportButton();
}

void BatchExportDialog::onDpiPresetChanged()
{
    const bool custom = m_dpiCustomRadio->isChecked();
    m_dpiSpinBox->setEnabled(custom);
    if (custom) {
        m_dpiSpinBox->setFocus();
        m_dpiSpinBox->selectAll();
    }
}

void BatchExportDialog::validateExportButton()
{
    if (!m_exportButton || !m_tabs) return;
    bool valid = !m_bundlePaths.isEmpty();
    if (selectedFormat() == Pdf) {
        valid = valid && !m_validPdfBundles.isEmpty();
        if (m_pageRangeRadio->isChecked()) {
            valid = valid && !m_pageRangeEdit->text().trimmed().isEmpty();
        }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        valid = valid && !m_pdfOutputEdit->text().trimmed().isEmpty();
#endif
    } else {
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        valid = valid && !m_snbxOutputEdit->text().trimmed().isEmpty();
#endif
    }
    m_exportButton->setEnabled(valid);
}

void BatchExportDialog::acceptExport()
{
    saveSettings();
    accept();
}

int BatchExportDialog::dpi() const
{
    if (m_dpiScreenRadio->isChecked()) return DpiScreen;
    if (m_dpiDraftRadio->isChecked()) return DpiDraft;
    if (m_dpiPrintRadio->isChecked()) return DpiPrint;
    return m_dpiSpinBox->value();
}

QString BatchExportDialog::pageRange() const
{
    return m_allPagesRadio->isChecked()
        ? QString() : m_pageRangeEdit->text().trimmed();
}

bool BatchExportDialog::annotationsOnly() const
{
    return m_annotationsOnlyCheckbox->isChecked();
}

bool BatchExportDialog::darkModeBackground() const
{
    return m_darkModeBgCheckbox->isChecked();
}

bool BatchExportDialog::darkenStrokes() const
{
    return m_darkenStrokesCheckbox->isChecked();
}

bool BatchExportDialog::includeMetadata() const
{
    return m_includeMetadataCheckbox->isChecked();
}

bool BatchExportDialog::includeOutline() const
{
    return m_includeOutlineCheckbox->isChecked();
}

bool BatchExportDialog::includePdf() const
{
    return m_includePdfCheckbox->isChecked();
}
