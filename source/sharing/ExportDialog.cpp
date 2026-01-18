#include "ExportDialog.h"
#include "NotebookExporter.h"
#include "../core/Document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QProgressBar>
#include <QPushButton>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>

ExportDialog::ExportDialog(Document* doc, QWidget* parent)
    : QDialog(parent)
    , m_document(doc)
{
    setWindowTitle(tr("Export Notebook"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Mobile-friendly size (slightly larger than typical desktop dialogs)
    setMinimumSize(400, 250);
    setMaximumSize(600, 400);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
    // Get PDF size for display
    if (m_document) {
        m_pdfSize = NotebookExporter::estimatePdfSize(m_document);
    }
    
    setupUI();
    
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

void ExportDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // Title
    m_titleLabel = new QLabel(tr("Export Notebook Package"));
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);
    
    // Description
    QString notebookName = m_document ? m_document->name : tr("Untitled");
    m_descLabel = new QLabel(
        tr("Export \"%1\" as a shareable .snbx package.\n\n"
           "The package can be shared with others or transferred to another device.")
        .arg(notebookName)
    );
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("font-size: 14px; color: palette(text);");
    m_descLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_descLabel);
    
    // Include PDF checkbox (only show if document has a PDF)
    if (m_pdfSize > 0) {
        m_includePdfCheckbox = new QCheckBox();
        updatePdfCheckboxText();
        m_includePdfCheckbox->setStyleSheet("font-size: 14px; padding: 8px;");
        m_includePdfCheckbox->setMinimumHeight(48);  // Mobile-friendly touch target
        mainLayout->addWidget(m_includePdfCheckbox);
    }
    
    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(0);  // Indeterminate mode
    m_progressBar->setTextVisible(false);
    m_progressBar->setMinimumHeight(24);
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);
    
    // Spacer
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16);
    
    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelBtn->setMinimumSize(120, 48);  // Mobile-friendly size
    m_cancelBtn->setStyleSheet(R"(
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
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    m_exportBtn = new QPushButton(tr("Export"));
    m_exportBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_exportBtn->setMinimumSize(120, 48);  // Mobile-friendly size
    m_exportBtn->setDefault(true);
    m_exportBtn->setStyleSheet(R"(
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
            background: palette(mid);
            border-color: palette(mid);
            color: palette(dark);
        }
    )");
    connect(m_exportBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_exportBtn);
    
    mainLayout->addLayout(buttonLayout);
}

void ExportDialog::updatePdfCheckboxText()
{
    if (!m_includePdfCheckbox) {
        return;
    }
    
    // Format file size for display
    QString sizeStr;
    if (m_pdfSize < 1024) {
        sizeStr = tr("%1 bytes").arg(m_pdfSize);
    } else if (m_pdfSize < 1024 * 1024) {
        sizeStr = tr("%1 KB").arg(m_pdfSize / 1024);
    } else {
        double sizeMB = static_cast<double>(m_pdfSize) / (1024.0 * 1024.0);
        sizeStr = tr("%1 MB").arg(sizeMB, 0, 'f', 1);
    }
    
    m_includePdfCheckbox->setText(tr("Include PDF file (adds %1)").arg(sizeStr));
}

bool ExportDialog::includePdf() const
{
    if (m_includePdfCheckbox) {
        return m_includePdfCheckbox->isChecked();
    }
    return false;
}

void ExportDialog::showProgress()
{
    if (m_progressBar) {
        m_progressBar->show();
    }
    if (m_exportBtn) {
        m_exportBtn->setEnabled(false);
    }
    if (m_cancelBtn) {
        m_cancelBtn->setEnabled(false);
    }
    if (m_includePdfCheckbox) {
        m_includePdfCheckbox->setEnabled(false);
    }
}

void ExportDialog::hideProgress()
{
    if (m_progressBar) {
        m_progressBar->hide();
    }
    if (m_exportBtn) {
        m_exportBtn->setEnabled(true);
    }
    if (m_cancelBtn) {
        m_cancelBtn->setEnabled(true);
    }
    if (m_includePdfCheckbox) {
        m_includePdfCheckbox->setEnabled(true);
    }
}

