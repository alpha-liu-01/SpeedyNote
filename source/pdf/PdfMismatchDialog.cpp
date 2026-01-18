#include "PdfMismatchDialog.h"
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>

PdfMismatchDialog::PdfMismatchDialog(const QString& originalName,
                                       qint64 originalSize,
                                       const QString& selectedPath,
                                       QWidget* parent)
    : QDialog(parent)
    , m_originalName(originalName)
    , m_originalSize(originalSize)
    , m_selectedPath(selectedPath)
{
    setWindowTitle(tr("Different PDF Detected"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.png"));
    setModal(true);
    
    // Set reasonable size
    setMinimumSize(450, 250);
    setMaximumSize(550, 350);
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

void PdfMismatchDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Header with warning icon
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);
    
    QLabel* iconLabel = new QLabel();
    QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(48, 48);
    iconLabel->setPixmap(icon);
    iconLabel->setFixedSize(48, 48);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    
    QLabel* titleLabel = new QLabel(tr("Different PDF Detected"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #d35400;");
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    headerLayout->addWidget(iconLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    mainLayout->addLayout(headerLayout);
    
    // Message
    QLabel* messageLabel = new QLabel(
        tr("The selected PDF appears to be different from the one originally used with this notebook.")
    );
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet("font-size: 12px; color: #555;");
    
    mainLayout->addWidget(messageLabel);
    
    // File comparison
    QFileInfo selectedInfo(m_selectedPath);
    QString selectedName = selectedInfo.fileName();
    qint64 selectedSize = selectedInfo.size();
    
    QString originalSizeStr = (m_originalSize > 0) ? formatFileSize(m_originalSize) : tr("unknown");
    QString selectedSizeStr = formatFileSize(selectedSize);
    
    QLabel* originalLabel = new QLabel(tr("Original: %1 (%2)").arg(m_originalName, originalSizeStr));
    originalLabel->setStyleSheet("font-size: 11px; color: #777; padding-left: 10px;");
    
    QLabel* selectedLabel = new QLabel(tr("Selected: %1 (%2)").arg(selectedName, selectedSizeStr));
    selectedLabel->setStyleSheet("font-size: 11px; color: #777; padding-left: 10px;");
    
    mainLayout->addWidget(originalLabel);
    mainLayout->addWidget(selectedLabel);
    
    // Warning
    QLabel* warningLabel = new QLabel(
        tr("Using a different PDF may cause annotations to appear in the wrong positions.")
    );
    warningLabel->setWordWrap(true);
    warningLabel->setStyleSheet("font-size: 11px; color: #c0392b; font-style: italic;");
    
    mainLayout->addWidget(warningLabel);
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    
    // Use This PDF button
    QPushButton* useBtn = new QPushButton(tr("Use This PDF"));
    useBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    useBtn->setMinimumHeight(35);
    useBtn->setStyleSheet(R"(
        QPushButton {
            padding: 8px 15px;
            border: 2px solid #e67e22;
            border-radius: 5px;
            background: palette(button);
            font-weight: bold;
        }
        QPushButton:hover {
            background: #e67e22;
            color: white;
        }
        QPushButton:pressed {
            background: #d35400;
        }
    )");
    connect(useBtn, &QPushButton::clicked, this, &PdfMismatchDialog::onUseThisPdf);
    
    // Choose Different button
    QPushButton* chooseBtn = new QPushButton(tr("Choose Different"));
    chooseBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    chooseBtn->setMinimumHeight(35);
    chooseBtn->setStyleSheet(R"(
        QPushButton {
            padding: 8px 15px;
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
    connect(chooseBtn, &QPushButton::clicked, this, &PdfMismatchDialog::onChooseDifferent);
    
    // Cancel button
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    cancelBtn->setMinimumSize(80, 35);
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            padding: 8px 15px;
            border: 1px solid palette(mid);
            border-radius: 5px;
            background: palette(button);
        }
        QPushButton:hover {
            background: palette(light);
        }
        QPushButton:pressed {
            background: palette(midlight);
        }
    )");
    connect(cancelBtn, &QPushButton::clicked, this, &PdfMismatchDialog::onCancel);
    
    buttonLayout->addWidget(useBtn);
    buttonLayout->addWidget(chooseBtn);
    buttonLayout->addWidget(cancelBtn);
    
    mainLayout->addLayout(buttonLayout);
}

QString PdfMismatchDialog::formatFileSize(qint64 bytes)
{
    if (bytes < 0) {
        return tr("unknown");
    }
    
    const qint64 KB = 1024;
    const qint64 MB = 1024 * KB;
    const qint64 GB = 1024 * MB;
    
    if (bytes >= GB) {
        return QString::number(static_cast<double>(bytes) / GB, 'f', 2) + " GB";
    } else if (bytes >= MB) {
        return QString::number(static_cast<double>(bytes) / MB, 'f', 1) + " MB";
    } else if (bytes >= KB) {
        return QString::number(static_cast<double>(bytes) / KB, 'f', 0) + " KB";
    } else {
        return QString::number(bytes) + " B";
    }
}

void PdfMismatchDialog::onUseThisPdf()
{
    m_result = Result::UseThisPdf;
    accept();
}

void PdfMismatchDialog::onChooseDifferent()
{
    m_result = Result::ChooseDifferent;
    accept();
}

void PdfMismatchDialog::onCancel()
{
    m_result = Result::Cancel;
    reject();
}

