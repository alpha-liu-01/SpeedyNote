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
    if (!m_statusLabel) {
        return;
    }
    // The label stays in the layout while empty so the match count appearing
    // does not shove the input narrower mid-typing. Since its width is fixed,
    // anything longer than a count (a translated "no results", say) is elided
    // rather than clipped, with the full text on the tooltip.
    const QString elided = m_statusLabel->fontMetrics().elidedText(
        status, Qt::ElideRight, m_statusLabel->width());
    m_statusLabel->setText(elided);
    m_statusLabel->setToolTip(elided == status ? QString() : status);
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
    
    // Search input. The placeholder carries what the dropped "Find:" label said.
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("Find"));
    m_searchInput->setMinimumWidth(80);
    m_searchInput->setClearButtonEnabled(true);
    connect(m_searchInput, &QLineEdit::textChanged, this, &PdfSearchBar::searchTextChanged);
    layout->addWidget(m_searchInput, 1);  // Stretch
    
    // Status label. Its width is reserved up front and it is never hidden, so
    // the input keeps a stable width whether or not a count is showing.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #cc6600; font-style: italic;");
    m_statusLabel->setFixedWidth(STATUS_WIDTH);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_statusLabel);
    
    // Next / Previous / Options are icon-only squares; their tooltips already
    // name the shortcuts, so dropping the captions costs no discoverability.
    m_prevButton = new QPushButton(this);
    m_prevButton->setFixedSize(24, 24);
    m_prevButton->setFlat(true);
    m_prevButton->setCursor(Qt::PointingHandCursor);
    m_prevButton->setToolTip(tr("Find Previous (Shift+F3)"));
    connect(m_prevButton, &QPushButton::clicked, this, &PdfSearchBar::onPrevClicked);
    layout->addWidget(m_prevButton);

    m_nextButton = new QPushButton(this);
    m_nextButton->setFixedSize(24, 24);
    m_nextButton->setFlat(true);
    m_nextButton->setCursor(Qt::PointingHandCursor);
    m_nextButton->setToolTip(tr("Find Next (F3)"));
    connect(m_nextButton, &QPushButton::clicked, this, &PdfSearchBar::onNextClicked);
    layout->addWidget(m_nextButton);
    
    // Options button with dropdown menu
    m_optionsButton = new QPushButton(this);
    m_optionsButton->setFixedSize(24, 24);
    m_optionsButton->setFlat(true);
    m_optionsButton->setCursor(Qt::PointingHandCursor);
    m_optionsButton->setToolTip(tr("Search Options"));
    layout->addWidget(m_optionsButton);
    
    // Options menu
    m_optionsMenu = new QMenu(this);
    
    m_caseSensitiveAction = m_optionsMenu->addAction(tr("Case Sensitive"));
    m_caseSensitiveAction->setCheckable(true);
    m_caseSensitiveAction->setChecked(false);
    // SBS2: option changes re-scan the document via the same live-query path.
    connect(m_caseSensitiveAction, &QAction::toggled, this, [this]() {
        emit searchTextChanged(searchText());
    });
    
    m_wholeWordAction = m_optionsMenu->addAction(tr("Whole Word"));
    m_wholeWordAction->setCheckable(true);
    m_wholeWordAction->setChecked(false);
    connect(m_wholeWordAction, &QAction::toggled, this, [this]() {
        emit searchTextChanged(searchText());
    });
    
    m_optionsButton->setMenu(m_optionsMenu);
    
    // Set fixed height for the bar
    setFixedHeight(36);

    // The card border and rounded corners come from a stylesheet, which a plain
    // QWidget only paints once it is told to honour styled backgrounds.
    setAttribute(Qt::WA_StyledBackground, true);
    applyCardStyle();
}

void PdfSearchBar::applyCardStyle()
{
    const QString background = m_darkMode ? QStringLiteral("#323232")
                                          : QStringLiteral("#f0f0f0");
    const QString border = m_darkMode ? QStringLiteral("#5a5a5a")
                                      : QStringLiteral("#b4b4b4");
    setStyleSheet(QStringLiteral(
        "PdfSearchBar { background-color: %1; border: 1px solid %2;"
        " border-radius: 6px; }"
        // A 24px square has no room for Qt's automatic dropdown arrow next to
        // the icon, so suppress the indicator on the options button.
        "PdfSearchBar QPushButton::menu-indicator { image: none; width: 0px; }")
        .arg(background, border));
}

void PdfSearchBar::updateIcons()
{
    QString suffix = m_darkMode ? "_reversed" : "";
    
    // Close button icon
    if (m_closeButton) {
        m_closeButton->setIcon(QIcon(QString(":/resources/icons/cross%1.png").arg(suffix)));
        m_closeButton->setIconSize(QSize(16, 16));
    }
    
    if (m_optionsButton) {
        m_optionsButton->setIcon(QIcon(QString(":/resources/icons/settings%1.png").arg(suffix)));
        m_optionsButton->setIconSize(QSize(16, 16));
    }
    
    // Next/Prev buttons with arrows
    if (m_nextButton) {
        m_nextButton->setIcon(QIcon(QString(":/resources/icons/down_arrow%1.png").arg(suffix)));
        m_nextButton->setIconSize(QSize(16, 16));
    }
    
    if (m_prevButton) {
        m_prevButton->setIcon(QIcon(QString(":/resources/icons/up_arrow%1.png").arg(suffix)));
        m_prevButton->setIconSize(QSize(16, 16));
    }
    
    QPalette pal = palette();
    if (m_darkMode) {
        pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    } else {
        pal.setColor(QPalette::WindowText, QColor(40, 40, 40));
    }
    setPalette(pal);
    applyCardStyle();
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

