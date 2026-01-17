#include "SaveDocumentDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QRegularExpression>

SaveDocumentDialog::SaveDocumentDialog(const QString& title,
                                       const QString& defaultName,
                                       QWidget* parent)
    : QDialog(parent)
    , m_title(title)
    , m_defaultName(defaultName)
{
    setWindowTitle(title);
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Mobile-friendly size
    setMinimumSize(420, 280);
    setMaximumSize(600, 400);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
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

void SaveDocumentDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(28, 28, 28, 28);
    
    // Title
    m_titleLabel = new QLabel(m_title);
    m_titleLabel->setStyleSheet("font-size: 20px; font-weight: bold;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);
    
    // Prompt
    m_promptLabel = new QLabel(tr("Enter a name for your document:"));
    m_promptLabel->setStyleSheet("font-size: 15px; color: palette(text);");
    m_promptLabel->setAlignment(Qt::AlignLeft);
    mainLayout->addWidget(m_promptLabel);
    
    // Name input field
    m_nameEdit = new QLineEdit();
    m_nameEdit->setText(m_defaultName);
    m_nameEdit->setPlaceholderText(tr("Document name"));
    m_nameEdit->selectAll();  // Select all for easy replacement
    m_nameEdit->setClearButtonEnabled(true);  // Show clear button
    m_nameEdit->setMinimumHeight(56);  // Large touch target
    m_nameEdit->setStyleSheet(R"(
        QLineEdit {
            font-size: 18px;
            padding: 12px 16px;
            border: 2px solid palette(mid);
            border-radius: 8px;
            background: palette(base);
        }
        QLineEdit:focus {
            border-color: #3498db;
        }
    )");
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SaveDocumentDialog::onTextChanged);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_saveBtn->isEnabled()) {
            accept();
        }
    });
    mainLayout->addWidget(m_nameEdit);
    
    // Spacer
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16);
    
    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_cancelBtn->setMinimumSize(130, 52);  // Mobile-friendly size
    m_cancelBtn->setStyleSheet(R"(
        QPushButton {
            font-size: 15px;
            padding: 14px 24px;
            border: 1px solid palette(mid);
            border-radius: 8px;
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
    
    m_saveBtn = new QPushButton(tr("Save"));
    m_saveBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_saveBtn->setMinimumSize(130, 52);  // Mobile-friendly size
    m_saveBtn->setDefault(true);
    m_saveBtn->setStyleSheet(R"(
        QPushButton {
            font-size: 15px;
            font-weight: bold;
            padding: 14px 24px;
            border: 2px solid #27ae60;
            border-radius: 8px;
            background: #27ae60;
            color: white;
        }
        QPushButton:hover {
            background: #219a52;
            border-color: #219a52;
        }
        QPushButton:pressed {
            background: #1e8449;
            border-color: #1e8449;
        }
        QPushButton:disabled {
            background: palette(mid);
            border-color: palette(mid);
            color: palette(dark);
        }
    )");
    connect(m_saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_saveBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    // Initial validation
    onTextChanged(m_nameEdit->text());
}

void SaveDocumentDialog::onTextChanged(const QString& text)
{
    // Disable save button if name is empty or whitespace-only
    bool valid = !text.trimmed().isEmpty();
    m_saveBtn->setEnabled(valid);
}

QString SaveDocumentDialog::documentName() const
{
    return sanitizeFilename(m_nameEdit->text().trimmed());
}

QString SaveDocumentDialog::sanitizeFilename(const QString& name) const
{
    // Remove invalid filename characters: < > : " / \ | ? *
    return QString(name).replace(QRegularExpression("[<>:\"/\\\\|?*]"), "_");
}

QString SaveDocumentDialog::getDocumentName(QWidget* parent,
                                            const QString& title,
                                            const QString& defaultName,
                                            bool* ok)
{
    SaveDocumentDialog dialog(title, defaultName, parent);
    
    if (dialog.exec() == QDialog::Accepted) {
        if (ok) *ok = true;
        return dialog.documentName();
    } else {
        if (ok) *ok = false;
        return QString();
    }
}

