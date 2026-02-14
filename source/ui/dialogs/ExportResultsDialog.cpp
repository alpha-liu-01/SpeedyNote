#include "ExportResultsDialog.h"
#include "../ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QScrollBar>

// ============================================================================
// Construction
// ============================================================================

ExportResultsDialog::ExportResultsDialog(const BatchOps::BatchResult& result,
                                           const QString& outputDir,
                                           QWidget* parent)
    : QDialog(parent)
    , m_result(result)
    , m_outputDir(outputDir)
{
    setWindowTitle(tr("Export Results"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    setModal(true);
    
    // Detect dark mode
    m_darkMode = palette().color(QPalette::Window).lightness() < 128;
    
    // Dialog size
    setMinimumSize(DIALOG_MIN_WIDTH, DIALOG_MIN_HEIGHT);
    setMaximumSize(DIALOG_MAX_WIDTH, DIALOG_MAX_HEIGHT);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    
    setupUi();
    populateResults();
    updateSummary();
    
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

void ExportResultsDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // ===== Title =====
    m_titleLabel = new QLabel(tr("Export Results"));
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);
    
    // ===== Summary =====
    m_summaryLabel = new QLabel();
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setAlignment(Qt::AlignCenter);
    QFont summaryFont = m_summaryLabel->font();
    summaryFont.setPointSize(12);
    m_summaryLabel->setFont(summaryFont);
    mainLayout->addWidget(m_summaryLabel);
    
    // ===== Results List =====
    m_resultsList = new QListWidget();
    m_resultsList->setFrameShape(QFrame::NoFrame);
    m_resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_resultsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_resultsList->setFocusPolicy(Qt::NoFocus);
    m_resultsList->setSpacing(4);
    
    // Apply theme colors
    QColor bgColor = ThemeColors::background(m_darkMode);
    QColor textColor = ThemeColors::textPrimary(m_darkMode);
    QColor borderColor = ThemeColors::border(m_darkMode);
    QColor altBgColor = ThemeColors::backgroundAlt(m_darkMode);
    
    m_resultsList->setStyleSheet(QString(
        "QListWidget {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  color: %3;"
        "  padding: 2px 4px;"
        "  border: none;"
        "  background: transparent;"
        "  outline: none;"
        "}"
        "QListWidget::item:selected {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item:hover {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QListWidget::item:focus {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
    ).arg(altBgColor.name(), borderColor.name(), textColor.name()));
    
    // Scrollbar styling
    m_resultsList->verticalScrollBar()->setStyleSheet(QString(
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 8px;"
        "  margin: 4px 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %1;"
        "  border-radius: 4px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
    ).arg(borderColor.name()));
    
    mainLayout->addWidget(m_resultsList, 1);
    
    // ===== Buttons =====
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    
    // Retry Failed button
    m_retryButton = new QPushButton(tr("Retry Failed"));
    m_retryButton->setMinimumHeight(40);
    m_retryButton->setEnabled(m_result.hasErrors());
    m_retryButton->setVisible(m_result.hasErrors());
    connect(m_retryButton, &QPushButton::clicked, 
            this, &ExportResultsDialog::onRetryClicked);
    buttonLayout->addWidget(m_retryButton);
    
#ifndef Q_OS_ANDROID
    // Show in Folder button (desktop only)
    m_showFolderButton = new QPushButton(tr("Show in Folder"));
    m_showFolderButton->setMinimumHeight(40);
    m_showFolderButton->setEnabled(!m_outputDir.isEmpty() && QDir(m_outputDir).exists());
    connect(m_showFolderButton, &QPushButton::clicked,
            this, &ExportResultsDialog::onShowFolderClicked);
    buttonLayout->addWidget(m_showFolderButton);
#endif
    
    buttonLayout->addStretch();
    
    // OK button
    m_okButton = new QPushButton(tr("OK"));
    m_okButton->setMinimumSize(100, 40);
    m_okButton->setDefault(true);
    m_okButton->setStyleSheet(R"(
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
    )");
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_okButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Apply button styles
    QString buttonStyle = QString(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "}"
        "QPushButton:hover {"
        "  background: %4;"
        "}"
        "QPushButton:disabled {"
        "  background: %5;"
        "  color: %6;"
        "}"
    ).arg(ThemeColors::backgroundAlt(m_darkMode).name(),
          ThemeColors::textPrimary(m_darkMode).name(),
          ThemeColors::border(m_darkMode).name(),
          ThemeColors::itemHover(m_darkMode).name(),
          ThemeColors::background(m_darkMode).name(),
          ThemeColors::textDisabled(m_darkMode).name());
    
    m_retryButton->setStyleSheet(buttonStyle);
#ifndef Q_OS_ANDROID
    m_showFolderButton->setStyleSheet(buttonStyle);
#endif
}

void ExportResultsDialog::populateResults()
{
    m_resultsList->clear();
    
    for (const BatchOps::FileResult& fileResult : m_result.results) {
        QString displayName = extractDisplayName(fileResult.inputPath);
        QString statusIconName;
        QString statusText;
        
        switch (fileResult.status) {
            case BatchOps::FileStatus::Success:
                statusIconName = QStringLiteral("check");
                if (fileResult.outputSize > 0) {
                    statusText = tr("Exported (%1)").arg(formatFileSize(fileResult.outputSize));
                } else {
                    statusText = tr("Exported");
                }
                break;
                
            case BatchOps::FileStatus::Skipped:
                statusIconName = QStringLiteral("warning");
                statusText = fileResult.message.isEmpty() 
                    ? tr("Skipped") 
                    : fileResult.message;
                break;
                
            case BatchOps::FileStatus::Error:
                statusIconName = QStringLiteral("cross");
                statusText = fileResult.message.isEmpty() 
                    ? tr("Failed") 
                    : fileResult.message;
                break;
        }
        
        QString iconPath = m_darkMode
            ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(statusIconName)
            : QStringLiteral(":/resources/icons/%1.png").arg(statusIconName);
        
        // Create list item with rich text
        // Use proper line height for 2-line display
        QString itemText = QString(
            "<div style='line-height: 1.5;'>"
            "<img src='%1' width='12' height='12' /> "
            "<span style='font-weight: bold; font-size: 11px;'>%2</span><br/>"
            "<span style='color: %3; font-size: 9px;'>%4</span>"
            "</div>"
        ).arg(iconPath, displayName,
              ThemeColors::textSecondary(m_darkMode).name(), statusText);
        
        QListWidgetItem* item = new QListWidgetItem();
        
        // Create a label for rich text
        QLabel* label = new QLabel(itemText);
        label->setTextFormat(Qt::RichText);
        label->setContentsMargins(8, 8, 8, 8);
        label->setWordWrap(true);
        
        // Set background color for alternating rows
        int row = m_resultsList->count();
        QColor bgColor = (row % 2 == 1) 
            ? ThemeColors::backgroundAlt(m_darkMode) 
            : ThemeColors::background(m_darkMode);
        label->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(bgColor.name()));
        
        // Calculate proper height for 2 lines + margins
        // Line 1: ~18px (14px font + spacing), Line 2: ~16px (11px font + spacing), Margins: 16px
        item->setSizeHint(QSize(0, 58));
        m_resultsList->addItem(item);
        m_resultsList->setItemWidget(item, label);
    }
}

void ExportResultsDialog::updateSummary()
{
    QStringList parts;
    
    auto iconPath = [this](const QString& name) -> QString {
        return m_darkMode
            ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(name)
            : QStringLiteral(":/resources/icons/%1.png").arg(name);
    };
    
    // Success count (green)
    if (m_result.successCount > 0) {
        parts << QString("<img src='%1' width='12' height='12' /> "
                         "<span style='color: #27ae60;'>%2 exported</span>")
                 .arg(iconPath(QStringLiteral("check")))
                 .arg(m_result.successCount);
    }
    
    // Skip count (orange)
    if (m_result.skippedCount > 0) {
        parts << QString("<img src='%1' width='12' height='12' /> "
                         "<span style='color: #e67e22;'>%2 skipped</span>")
                 .arg(iconPath(QStringLiteral("warning")))
                 .arg(m_result.skippedCount);
    }
    
    // Error count (red)
    if (m_result.errorCount > 0) {
        parts << QString("<img src='%1' width='12' height='12' /> "
                         "<span style='color: #e74c3c;'>%2 failed</span>")
                 .arg(iconPath(QStringLiteral("cross")))
                 .arg(m_result.errorCount);
    }
    
    QString summaryText = parts.join("  •  ");
    
    // Add total size if significant
    if (m_result.totalOutputSize > 0) {
        summaryText += QString("<br/><span style='color: %1;'>Total size: %2</span>")
            .arg(ThemeColors::textSecondary(m_darkMode).name(),
                 formatFileSize(m_result.totalOutputSize));
    }
    
    m_summaryLabel->setText(summaryText);
    m_summaryLabel->setTextFormat(Qt::RichText);
}

// ============================================================================
// Public Methods
// ============================================================================

void ExportResultsDialog::setTitle(const QString& title)
{
    m_titleLabel->setText(title.isEmpty() ? tr("Export Results") : title);
    setWindowTitle(title.isEmpty() ? tr("Export Results") : title);
}

void ExportResultsDialog::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        // Would need to rebuild UI - for now, just update on construction
    }
}

// ============================================================================
// Private Slots
// ============================================================================

void ExportResultsDialog::onRetryClicked()
{
    QStringList failedPaths;
    
    for (const BatchOps::FileResult& fileResult : m_result.results) {
        if (fileResult.status == BatchOps::FileStatus::Error) {
            failedPaths.append(fileResult.inputPath);
        }
    }
    
    if (!failedPaths.isEmpty()) {
        emit retryRequested(failedPaths);
        accept();  // Close dialog after retry requested
    }
}

void ExportResultsDialog::onShowFolderClicked()
{
#ifndef Q_OS_ANDROID
    if (!m_outputDir.isEmpty() && QDir(m_outputDir).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
    }
#endif
}

// ============================================================================
// Private Methods
// ============================================================================

QString ExportResultsDialog::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024) {
        return tr("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return tr("%1 KB").arg(bytes / 1024);
    } else if (bytes < 1024 * 1024 * 1024) {
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return tr("%1 MB").arg(mb, 0, 'f', 1);
    } else {
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        return tr("%1 GB").arg(gb, 0, 'f', 2);
    }
}

QString ExportResultsDialog::extractDisplayName(const QString& path) const
{
    // Extract just the filename/bundle name from path
    QString name = QFileInfo(path).fileName();
    
    // Remove .snb extension for bundles
    if (name.endsWith(".snb", Qt::CaseInsensitive)) {
        name.chop(4);
    }
    // Remove .snbx extension for packages
    else if (name.endsWith(".snbx", Qt::CaseInsensitive)) {
        name.chop(5);
    }
    
    return name;
}
