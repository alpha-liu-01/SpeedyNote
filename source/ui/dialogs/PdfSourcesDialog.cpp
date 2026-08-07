#include "PdfSourcesDialog.h"

#include "../../core/Document.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#ifdef Q_OS_ANDROID
#include "../../android/PdfPickerAndroid.h"
#elif defined(Q_OS_IOS)
#include "../../ios/PdfPickerIOS.h"
#endif

namespace {

QString statusText(PdfSourceHealthStatus status)
{
    switch (status) {
    case PdfSourceHealthStatus::AvailableExternal:
        return QCoreApplication::translate("PdfSourcesDialog", "Available");
    case PdfSourceHealthStatus::AvailableRelative:
        return QCoreApplication::translate("PdfSourcesDialog", "Available (relative copy)");
    case PdfSourceHealthStatus::AvailableBundled:
        return QCoreApplication::translate("PdfSourcesDialog", "Available (embedded copy)");
    case PdfSourceHealthStatus::PartialBundled:
        return QCoreApplication::translate("PdfSourcesDialog", "Embedded copy is incomplete");
    case PdfSourceHealthStatus::IdentityMismatch:
        return QCoreApplication::translate("PdfSourcesDialog", "Different file found");
    case PdfSourceHealthStatus::Unreadable:
        return QCoreApplication::translate("PdfSourcesDialog", "Unreadable or damaged");
    case PdfSourceHealthStatus::Missing:
    default:
        return QCoreApplication::translate("PdfSourcesDialog", "Missing");
    }
}

} // namespace

PdfSourcesDialog::PdfSourcesDialog(Document* document, QWidget* parent)
    : QDialog(parent)
    , m_document(document)
{
    setWindowTitle(tr("PDF Sources"));
    setWindowIcon(QIcon(QStringLiteral(":/resources/icons/mainicon.svg")));
    resize(780, 420);

    auto* mainLayout = new QVBoxLayout(this);
    auto* description = new QLabel(
        tr("PDF-backed pages keep a reference to the file they came from. "
           "Locate unavailable sources to restore their page backgrounds."),
        this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    m_sources = new QTreeWidget(this);
    m_sources->setColumnCount(4);
    m_sources->setHeaderLabels({
        tr("Source"), tr("Status"), tr("Pages"), tr("Location")
    });
    m_sources->setRootIsDecorated(false);
    m_sources->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sources->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sources->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_sources->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sources->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    mainLayout->addWidget(m_sources, 1);

    auto* actionLayout = new QHBoxLayout();
    m_locateButton = new QPushButton(tr("Locate..."), this);
    m_retryButton = new QPushButton(tr("Retry"), this);
    m_showFolderButton = new QPushButton(tr("Show in Folder"), this);
    m_locateFolderButton = new QPushButton(tr("Locate Folder..."), this);
    actionLayout->addWidget(m_locateButton);
    actionLayout->addWidget(m_retryButton);
    actionLayout->addWidget(m_showFolderButton);
    actionLayout->addStretch();
    actionLayout->addWidget(m_locateFolderButton);
    mainLayout->addLayout(actionLayout);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_showFolderButton->hide();
    m_locateFolderButton->hide();
#endif

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_sources, &QTreeWidget::itemSelectionChanged,
            this, &PdfSourcesDialog::updateButtonStates);
    connect(m_locateButton, &QPushButton::clicked,
            this, &PdfSourcesDialog::locateSelectedSource);
    connect(m_retryButton, &QPushButton::clicked,
            this, &PdfSourcesDialog::retrySelectedSource);
    connect(m_showFolderButton, &QPushButton::clicked,
            this, &PdfSourcesDialog::showSelectedSourceInFolder);
    connect(m_locateFolderButton, &QPushButton::clicked,
            this, &PdfSourcesDialog::locateSourcesInFolder);

    refreshRows();
}

QString PdfSourcesDialog::selectedSourceId() const
{
    QTreeWidgetItem* item = m_sources ? m_sources->currentItem() : nullptr;
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void PdfSourcesDialog::refreshRows()
{
    if (!m_document || !m_sources) return;
    const QString previousId = m_sources->currentItem()
        ? m_sources->currentItem()->data(0, Qt::UserRole).toString()
        : QString();

    m_sources->clear();
    QVector<PdfSourceHealth> health = m_document->pdfSourceHealthSnapshot();
    std::stable_sort(health.begin(), health.end(),
                     [](const PdfSourceHealth& a, const PdfSourceHealth& b) {
        return a.requiresRepair() && !b.requiresRepair();
    });

    for (const PdfSourceHealth& source : health) {
        QString location = source.activePath;
        if (location.isEmpty()) {
            if (const PdfSource* raw = m_document->pdfSourceById(source.sourceId)) {
                location = !raw->path.isEmpty() ? raw->path : raw->relativePath;
            }
        }
        const QString pages = source.unavailablePages > 0
            ? tr("%1 of %2 unavailable").arg(source.unavailablePages).arg(source.referencedPages)
            : tr("%1").arg(source.referencedPages);
        auto* item = new QTreeWidgetItem({
            source.title, statusText(source.status), pages, location
        });
        item->setData(0, Qt::UserRole, source.sourceId);
        item->setData(0, Qt::UserRole + 1, source.requiresRepair());
        item->setData(0, Qt::UserRole + 2, source.activePath);
        if (source.requiresRepair()) {
            item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
        }
        m_sources->addTopLevelItem(item);
        if (source.sourceId == previousId) m_sources->setCurrentItem(item);
    }
    if (!m_sources->currentItem() && m_sources->topLevelItemCount() > 0) {
        m_sources->setCurrentItem(m_sources->topLevelItem(0));
    }
    updateButtonStates();
}

void PdfSourcesDialog::updateButtonStates()
{
    QTreeWidgetItem* item = m_sources ? m_sources->currentItem() : nullptr;
    const bool selected = item != nullptr;
    const bool needsRepair = selected && item->data(0, Qt::UserRole + 1).toBool();
    const QString activePath = selected
        ? item->data(0, Qt::UserRole + 2).toString() : QString();
    m_locateButton->setEnabled(selected);
    m_retryButton->setEnabled(needsRepair);
    m_showFolderButton->setEnabled(!activePath.isEmpty());
}

QString PdfSourcesDialog::choosePdfFile(const QString& startPath)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(startPath)
    return PdfPickerAndroid::pickPdfFile();
#elif defined(Q_OS_IOS)
    Q_UNUSED(startPath)
    return QString();
#else
    QString directory = QFileInfo(startPath).absolutePath();
    if (directory.isEmpty() || !QDir(directory).exists()) directory = QDir::homePath();
    return QFileDialog::getOpenFileName(
        this, tr("Locate PDF Source"), directory,
        tr("PDF Files (*.pdf);;All Files (*)"));
#endif
}

void PdfSourcesDialog::locateSelectedSource()
{
    if (!m_document || !m_sources->currentItem()) return;
    const QString sourceId = selectedSourceId();
    const PdfSource* source = m_document->pdfSourceById(sourceId);
    const QString startPath = source ? source->path : QString();

#ifdef Q_OS_IOS
    const QPointer<PdfSourcesDialog> guard(this);
    PdfPickerIOS::pickPdfFile([guard, sourceId](const QString& path) {
        if (guard && !path.isEmpty()) guard->handleLocatedPath(sourceId, path);
    });
#else
    const QString path = choosePdfFile(startPath);
    if (!path.isEmpty()) handleLocatedPath(sourceId, path);
#endif
}

void PdfSourcesDialog::handleLocatedPath(const QString& sourceId, const QString& path)
{
    if (!m_document || path.isEmpty()) return;
    emit sourcesAboutToChange();
    if (!m_document->locateSource(sourceId, path)) {
        QMessageBox::warning(
            this, tr("PDF Source Not Matched"),
            tr("The selected PDF is damaged or does not match the original source. "
               "No document links were changed."));
        emit sourcesChanged();
        return;
    }
    refreshRows();
    emit sourcesChanged();
}

void PdfSourcesDialog::retrySelectedSource()
{
    if (!m_document || !m_sources || !m_sources->currentItem()) return;
    const QString sourceId = selectedSourceId();
    emit sourcesAboutToChange();
    m_document->retryPdfSource(sourceId);
    refreshRows();
    emit sourcesChanged();
}

void PdfSourcesDialog::showSelectedSourceInFolder()
{
    QTreeWidgetItem* item = m_sources ? m_sources->currentItem() : nullptr;
    if (!item) return;
    const QString path = item->data(0, Qt::UserRole + 2).toString();
    if (!path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void PdfSourcesDialog::locateSourcesInFolder()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if (!m_document) return;
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Locate PDF Sources"), QDir::homePath());
    if (folder.isEmpty()) return;
    emit sourcesAboutToChange();

    const QFileInfoList files = QDir(folder).entryInfoList(
        {QStringLiteral("*.pdf")}, QDir::Files | QDir::Readable);
    QHash<QString, QString> hashes;
    int repaired = 0;
    const QVector<PdfSourceHealth> health = m_document->pdfSourceHealthSnapshot();
    for (const PdfSourceHealth& item : health) {
        if (!item.requiresRepair()) continue;
        const PdfSource* source = m_document->pdfSourceById(item.sourceId);
        // A folder-wide search must never guess for legacy hashless sources.
        // Those require explicit single-file selection and user confirmation.
        if (!source || source->hash.isEmpty()) continue;
        for (const QFileInfo& file : files) {
            if (source->size > 0 && source->size != file.size()) continue;
            if (!source->hash.isEmpty()) {
                const QString filePath = file.absoluteFilePath();
                QString fileHash = hashes.value(filePath);
                if (!hashes.contains(filePath)) {
                    fileHash = Document::computePdfHash(filePath);
                    hashes.insert(filePath, fileHash);
                }
                if (fileHash != source->hash) continue;
            }
            if (m_document->locateSource(item.sourceId, file.absoluteFilePath())) {
                ++repaired;
                break;
            }
        }
    }
    refreshRows();
    emit sourcesChanged();
    QMessageBox::information(
        this, tr("PDF Source Search"),
        tr("Repaired %1 PDF source(s).").arg(repaired));
#endif
}
