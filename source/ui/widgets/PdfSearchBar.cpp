#include "PdfSearchBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QPalette>
#include <QStyle>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PdfSearchBar::PdfSearchBar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    
    // Detect initial dark mode
    m_darkMode = isDarkMode();
    updateIcons();
}

PdfSearchBar::~PdfSearchBar() = default;

// ============================================================================
// Public Methods
// ============================================================================

QString PdfSearchBar::searchText() const
{
    return m_searchInput ? m_searchInput->text() : QString();
}

bool PdfSearchBar::caseSensitive() const
{
    return m_caseSensitiveAction ? m_caseSensitiveAction->isChecked() : false;
}

bool PdfSearchBar::wholeWord() const
{
    return m_wholeWordAction ? m_wholeWordAction->isChecked() : false;
}

void PdfSearchBar::setStatus(const QString& status)
{
    if (m_statusLabel) {
        m_statusLabel->setText(status);
        m_statusLabel->setVisible(!status.isEmpty());
    }
}

void PdfSearchBar::clearStatus()
{
    setStatus(QString());
}

void PdfSearchBar::showAndFocus()
{
    show();
    if (m_searchInput) {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }
}

void PdfSearchBar::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcons();
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void PdfSearchBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        onCloseClicked();
        return;
    }
    
    if (event->key() == Qt::Key_F3) {
        if (event->modifiers() & Qt::ShiftModifier) {
            onPrevClicked();
        } else {
            onNextClicked();
        }
        return;
    }
    
    QWidget::keyPressEvent(event);
}

// ============================================================================
// Slots
// ============================================================================

void PdfSearchBar::onNextClicked()
{
    QString text = searchText();
    if (!text.isEmpty()) {
        emit searchNextRequested(text, caseSensitive(), wholeWord());
    }
}

void PdfSearchBar::onPrevClicked()
{
    QString text = searchText();
    if (!text.isEmpty()) {
        emit searchPrevRequested(text, caseSensitive(), wholeWord());
    }
}

void PdfSearchBar::onCloseClicked()
{
    hide();
    emit closed();
}

// ============================================================================
// Private Methods
// ============================================================================

void PdfSearchBar::setupUi()
{
    // Main horizontal layout
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);
    
    // Close button
    m_closeButton = new QPushButton(this);
    m_closeButton->setFixedSize(24, 24);
    m_closeButton->setFlat(true);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setToolTip(tr("Close (Escape)"));
    connect(m_closeButton, &QPushButton::clicked, this, &PdfSearchBar::onCloseClicked);
    layout->addWidget(m_closeButton);
    
    // "Find:" label
    QLabel *findLabel = new QLabel(tr("Find:"), this);
    layout->addWidget(findLabel);
    
    // Search input
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("Search in PDF..."));
    m_searchInput->setMinimumWidth(110);
    m_searchInput->setClearButtonEnabled(true);
    layout->addWidget(m_searchInput, 1);  // Stretch
    
    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #cc6600; font-style: italic;");
    m_statusLabel->setVisible(false);
    layout->addWidget(m_statusLabel);
    
    // Next button
    m_nextButton = new QPushButton(tr("Next"), this);
    m_nextButton->setFixedHeight(24);
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setToolTip(tr("Find Next (F3)"));
    connect(m_nextButton, &QPushButton::clicked, this, &PdfSearchBar::onNextClicked);
    layout->addWidget(m_nextButton);
    
    // Previous button
    m_prevButton = new QPushButton(tr("Previous"), this);
    m_prevButton->setFixedHeight(24);
    m_prevButton->setCursor(Qt::PointingHandCursor);
    m_prevButton->setToolTip(tr("Find Previous (Shift+F3)"));
    connect(m_prevButton, &QPushButton::clicked, this, &PdfSearchBar::onPrevClicked);
    layout->addWidget(m_prevButton);
    
    // Options button with dropdown menu
    m_optionsButton = new QPushButton(tr("Options"), this);
    m_optionsButton->setFixedHeight(24);
    m_optionsButton->setCursor(Qt::PointingHandCursor);
    m_optionsButton->setToolTip(tr("Search Options"));
    layout->addWidget(m_optionsButton);
    
    // Options menu
    m_optionsMenu = new QMenu(this);
    
    m_caseSensitiveAction = m_optionsMenu->addAction(tr("Case Sensitive"));
    m_caseSensitiveAction->setCheckable(true);
    m_caseSensitiveAction->setChecked(false);
    
    m_wholeWordAction = m_optionsMenu->addAction(tr("Whole Word"));
    m_wholeWordAction->setCheckable(true);
    m_wholeWordAction->setChecked(false);
    
    m_optionsButton->setMenu(m_optionsMenu);
    
    // Set fixed height for the bar
    setFixedHeight(36);
    
    // Style the background
    setAutoFillBackground(true);
}

void PdfSearchBar::updateIcons()
{
    QString suffix = m_darkMode ? "_reversed" : "";
    
    // Close button icon
    if (m_closeButton) {
        m_closeButton->setIcon(QIcon(QString(":/resources/icons/cross%1.png").arg(suffix)));
        m_closeButton->setIconSize(QSize(16, 16));
    }
    
    // Options button - no icon needed, text-only with dropdown arrow
    // (Qt automatically adds dropdown arrow when menu is attached)
    
    // Next/Prev buttons with arrows
    if (m_nextButton) {
        m_nextButton->setIcon(QIcon(QString(":/resources/icons/down_arrow%1.png").arg(suffix)));
        m_nextButton->setIconSize(QSize(12, 12));
    }
    
    if (m_prevButton) {
        m_prevButton->setIcon(QIcon(QString(":/resources/icons/up_arrow%1.png").arg(suffix)));
        m_prevButton->setIconSize(QSize(12, 12));
    }
    
    // Update background color based on theme
    QPalette pal = palette();
    if (m_darkMode) {
        pal.setColor(QPalette::Window, QColor(50, 50, 50));
        pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    } else {
        pal.setColor(QPalette::Window, QColor(240, 240, 240));
        pal.setColor(QPalette::WindowText, QColor(40, 40, 40));
    }
    setPalette(pal);
}

bool PdfSearchBar::isDarkMode() const
{
    // Detect dark mode by checking the window background luminance
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

