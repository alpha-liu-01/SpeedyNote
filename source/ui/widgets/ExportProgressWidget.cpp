#include "ExportProgressWidget.h"
#include "../ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QShowEvent>
#include <QFileInfo>

// ============================================================================
// Construction
// ============================================================================

ExportProgressWidget::ExportProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    // Widget flags for floating appearance
    setWindowFlags(Qt::Widget);  // Stay as child widget, not popup
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    
    // Detect dark mode from parent
    if (parent) {
        m_darkMode = parent->palette().color(QPalette::Window).lightness() < 128;
    }
    
    // Fixed width, dynamic height
    setFixedWidth(WIDGET_WIDTH);
    setMinimumHeight(WIDGET_MIN_HEIGHT);
    
    setupUi();
    
    // Dismiss timer
    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(DISMISS_TIMEOUT_MS);
    connect(m_dismissTimer, &QTimer::timeout, 
            this, &ExportProgressWidget::onDismissTimerExpired);
    
    // Opacity effect for proper fade animation of all child widgets
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);
    
    // Fade animation - animate the opacity effect, not the widget directly
    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setDuration(FADE_DURATION_MS);
    connect(m_fadeAnimation, &QPropertyAnimation::finished,
            this, &ExportProgressWidget::onFadeAnimationFinished);
    
    // Install event filter on parent for resize events
    if (parent) {
        parent->installEventFilter(this);
    }
    
    // Start hidden
    hide();
}

ExportProgressWidget::~ExportProgressWidget()
{
    // Timers and animations are parented, auto-deleted
}

// ============================================================================
// UI Setup
// ============================================================================

void ExportProgressWidget::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 16, 12);
    mainLayout->setSpacing(8);
    
    // === Top row: Icon + Status ===
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(10);
    
    m_iconLabel = new QLabel();
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(m_iconLabel);
    
    m_statusLabel = new QLabel();
    m_statusLabel->setWordWrap(true);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(13);
    statusFont.setBold(true);
    m_statusLabel->setFont(statusFont);
    topLayout->addWidget(m_statusLabel, 1);
    
    mainLayout->addLayout(topLayout);
    
    // === Progress bar (shown during export) ===
    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "  background: rgba(128, 128, 128, 0.3);"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QProgressBar::chunk {"
        "  background: #3498db;"
        "  border-radius: 4px;"
        "}"
    );
    mainLayout->addWidget(m_progressBar);
    
    // === Detail label (file count, queue info) ===
    m_detailLabel = new QLabel();
    m_detailLabel->setWordWrap(true);
    QFont detailFont = m_detailLabel->font();
    detailFont.setPointSize(11);
    m_detailLabel->setFont(detailFont);
    mainLayout->addWidget(m_detailLabel);
    
    // === Details button (shown on complete) ===
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_detailsButton = new QPushButton(tr("Details"));
    m_detailsButton->setFixedHeight(32);
    m_detailsButton->setCursor(Qt::PointingHandCursor);
    m_detailsButton->hide();  // Hidden until complete
    connect(m_detailsButton, &QPushButton::clicked,
            this, &ExportProgressWidget::onDetailsClicked);
    buttonLayout->addWidget(m_detailsButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Apply initial theme
    setDarkMode(m_darkMode);
}

// ============================================================================
// Public Methods
// ============================================================================

void ExportProgressWidget::showProgress(const QString& currentFile, 
                                         int current, int total, int queuedJobs)
{
    m_state = State::Progress;
    stopDismissTimer();
    
    // Extract just the filename
    QString fileName = QFileInfo(currentFile).fileName();
    if (fileName.endsWith(".snb", Qt::CaseInsensitive)) {
        fileName.chop(4);
    }
    
    // Update icon - spinning indicator (using text for simplicity)
    m_iconLabel->setText("⟳");
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(16);
    m_iconLabel->setFont(iconFont);
    
    // Update status
    m_statusLabel->setText(tr("Exporting %1...").arg(fileName));
    
    // Update progress bar
    m_progressBar->setVisible(true);
    int progress = (total > 0) ? (current * 100 / total) : 0;
    m_progressBar->setValue(progress);
    
    // Update detail label
    QString detailText = tr("%1 of %2").arg(current).arg(total);
    if (queuedJobs > 0) {
        detailText += tr(" (%1 more queued)").arg(queuedJobs);
    }
    m_detailLabel->setText(detailText);
    m_detailLabel->setVisible(true);
    
    // Hide details button during progress
    m_detailsButton->hide();
    
    // Show and position
    positionInCorner();
    if (!isVisible()) {
        show();
        fadeIn();
    }
    
    update();
}

void ExportProgressWidget::showComplete(int successCount, int failCount, int skipCount)
{
    m_state = State::Complete;
    m_lastSuccess = successCount;
    m_lastFail = failCount;
    m_lastSkip = skipCount;
    
    // Update icon
    if (failCount > 0) {
        m_iconLabel->setText("⚠");  // Warning if any failures
    } else {
        m_iconLabel->setText("✓");  // Checkmark if all good
    }
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(18);
    m_iconLabel->setFont(iconFont);
    
    // Set icon color
    QColor iconColor;
    if (failCount > 0) {
        iconColor = QColor(0xe6, 0x7e, 0x22);  // Orange warning
    } else {
        iconColor = QColor(0x27, 0xae, 0x60);  // Green success
    }
    m_iconLabel->setStyleSheet(QString("color: %1;").arg(iconColor.name()));
    
    // Update status
    m_statusLabel->setText(tr("Export complete"));
    
    // Hide progress bar
    m_progressBar->hide();
    
    // Update detail label with summary
    QStringList parts;
    if (successCount > 0) {
        parts << tr("%1 exported").arg(successCount);
    }
    if (skipCount > 0) {
        parts << tr("%1 skipped").arg(skipCount);
    }
    if (failCount > 0) {
        parts << tr("%1 failed").arg(failCount);
    }
    m_detailLabel->setText(parts.join(", "));
    m_detailLabel->setVisible(true);
    
    // Show details button if there were any issues or multiple files
    bool showDetails = (failCount > 0 || skipCount > 0 || (successCount + failCount + skipCount) > 1);
    m_detailsButton->setVisible(showDetails);
    
    // Force layout update after button visibility change
    // This is needed because QGraphicsOpacityEffect can interfere with layout updates
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    
    // Show and position
    positionInCorner();
    if (!isVisible()) {
        show();
        fadeIn();
    }
    
    // Ensure button is properly rendered after visibility change
    if (showDetails) {
        m_detailsButton->raise();
        m_detailsButton->update();
    }
    
    // Start auto-dismiss timer
    startDismissTimer();
    
    update();
}

void ExportProgressWidget::showError(const QString& message)
{
    m_state = State::Error;
    stopDismissTimer();
    
    // Update icon
    m_iconLabel->setText("✗");
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(18);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setStyleSheet("color: #e74c3c;");  // Red error
    
    // Update status
    m_statusLabel->setText(tr("Export failed"));
    
    // Hide progress bar
    m_progressBar->hide();
    
    // Show error message
    m_detailLabel->setText(message);
    m_detailLabel->setVisible(true);
    
    // Hide details button for errors
    m_detailsButton->hide();
    
    // Show and position
    positionInCorner();
    if (!isVisible()) {
        show();
        fadeIn();
    }
    
    // Start auto-dismiss timer (longer for errors)
    m_dismissTimer->setInterval(DISMISS_TIMEOUT_MS * 2);  // 10 seconds for errors
    startDismissTimer();
    
    update();
}

void ExportProgressWidget::dismiss(bool animated)
{
    stopDismissTimer();
    
    if (animated && isVisible()) {
        fadeOut();
    } else {
        hide();
        m_state = State::Hidden;
        emit dismissed();
    }
}

void ExportProgressWidget::setDarkMode(bool dark)
{
    m_darkMode = dark;
    
    // Update text colors
    QColor textPrimary = ThemeColors::textPrimary(dark);
    QColor textSecondary = ThemeColors::textSecondary(dark);
    
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(textPrimary.name()));
    m_detailLabel->setStyleSheet(QString("color: %1;").arg(textSecondary.name()));
    
    // Update button style
    QColor buttonBg = ThemeColors::backgroundAlt(dark);
    QColor buttonHover = ThemeColors::itemHover(dark);
    QColor buttonText = textPrimary;
    QColor borderColor = ThemeColors::border(dark);
    
    m_detailsButton->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background: %4;"
        "}"
    ).arg(buttonBg.name(), buttonText.name(), borderColor.name(), buttonHover.name()));
    
    update();
}

void ExportProgressWidget::setOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(m_opacity);
    }
}

// ============================================================================
// Events
// ============================================================================

void ExportProgressWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Opacity is handled by QGraphicsOpacityEffect, no manual opacity needed
    
    // Background color
    QColor bgColor = m_darkMode 
        ? QColor(0x2d, 0x2d, 0x2d)   // Dark card background
        : QColor(0xff, 0xff, 0xff);   // White
    
    // Shadow (light mode only)
    if (!m_darkMode) {
        QRect shadowRect = rect().adjusted(2, 2, 2, 2);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, CORNER_RADIUS, CORNER_RADIUS);
        painter.fillPath(shadowPath, QColor(0, 0, 0, 30));
    }
    
    // Main background
    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), CORNER_RADIUS, CORNER_RADIUS);
    painter.fillPath(bgPath, bgColor);
    
    // Border
    QColor borderColor = ThemeColors::border(m_darkMode);
    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(bgPath);
}

void ExportProgressWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    positionInCorner();
}

bool ExportProgressWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Reposition when parent resizes
    if (watched == parent() && event->type() == QEvent::Resize) {
        if (isVisible()) {
            positionInCorner();
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ============================================================================
// Private Slots
// ============================================================================

void ExportProgressWidget::onDismissTimerExpired()
{
    dismiss(true);  // Animated dismiss
}

void ExportProgressWidget::onDetailsClicked()
{
    stopDismissTimer();  // Stop auto-dismiss when user interacts
    emit detailsRequested();
}

void ExportProgressWidget::onFadeAnimationFinished()
{
    // Check if we were fading out (opacity is near 0)
    if (m_opacityEffect && m_opacityEffect->opacity() <= 0.01) {
        hide();
        m_state = State::Hidden;
        emit dismissed();
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void ExportProgressWidget::positionInCorner()
{
    QWidget* parentWidget = qobject_cast<QWidget*>(parent());
    if (!parentWidget) return;
    
    // Force layout to recalculate (needed when child visibility changes with QGraphicsOpacityEffect)
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    
    // Adjust size to content
    adjustSize();
    
    // Position in bottom-right corner
    int x = parentWidget->width() - width() - CORNER_MARGIN;
    int y = parentWidget->height() - height() - CORNER_MARGIN;
    
    move(x, y);
    raise();  // Ensure widget is on top
}

void ExportProgressWidget::startDismissTimer()
{
    m_dismissTimer->setInterval(DISMISS_TIMEOUT_MS);
    m_dismissTimer->start();
}

void ExportProgressWidget::stopDismissTimer()
{
    m_dismissTimer->stop();
}

void ExportProgressWidget::fadeIn()
{
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_opacity = 0.0;
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(0.0);
    }
    m_fadeAnimation->start();
}

void ExportProgressWidget::fadeOut()
{
    m_fadeAnimation->stop();
    qreal currentOpacity = m_opacityEffect ? m_opacityEffect->opacity() : m_opacity;
    m_fadeAnimation->setStartValue(currentOpacity);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}
