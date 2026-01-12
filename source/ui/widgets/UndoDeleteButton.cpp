#include "UndoDeleteButton.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPalette>
#include <QApplication>
#include <QTimer>

// ============================================================================
// UndoDeleteButton
// ============================================================================

UndoDeleteButton::UndoDeleteButton(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    
    // Confirmation timer
    m_confirmTimer = new QTimer(this);
    m_confirmTimer->setSingleShot(true);
    m_confirmTimer->setInterval(UNDO_TIMEOUT_MS);
    connect(m_confirmTimer, &QTimer::timeout, this, &UndoDeleteButton::onTimerExpired);
    
    // Load default icons
    updateIcons();
    
    setToolTip(tr("Delete page"));
}

UndoDeleteButton::~UndoDeleteButton()
{
    // Timer is parented, will be deleted automatically
}

void UndoDeleteButton::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        updateIcons();
        update();
    }
}

void UndoDeleteButton::confirmDelete()
{
    if (m_state == State::UndoPending) {
        m_confirmTimer->stop();
        emit deleteConfirmed();
        resetToNormal();
    }
}

bool UndoDeleteButton::isUndoPending() const
{
    return m_state == State::UndoPending;
}

void UndoDeleteButton::reset()
{
    if (m_state != State::Normal) {
        m_confirmTimer->stop();
        resetToNormal();
    }
}

QSize UndoDeleteButton::sizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

QSize UndoDeleteButton::minimumSizeHint() const
{
    return QSize(BUTTON_SIZE, BUTTON_SIZE);
}

void UndoDeleteButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background circle
    QColor bgColor = backgroundColor();
    
    // Apply press/hover effects
    if (m_pressed) {
        bgColor = bgColor.darker(120);
    } else if (m_hovered) {
        bgColor = bgColor.lighter(110);
    }
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawEllipse(rect());
    
    // Choose icon based on state
    const QIcon& currentIcon = (m_state == State::UndoPending) ? m_undoIcon : m_deleteIcon;
    
    // Draw icon centered
    if (!currentIcon.isNull()) {
        const int iconX = (BUTTON_SIZE - ICON_SIZE) / 2;
        const int iconY = (BUTTON_SIZE - ICON_SIZE) / 2;
        const QRect iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);
        
        QIcon::Mode iconMode = m_pressed ? QIcon::Active : QIcon::Normal;
        currentIcon.paint(&painter, iconRect, Qt::AlignCenter, iconMode, QIcon::On);
    }
}

void UndoDeleteButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void UndoDeleteButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        
        // Check if release is within button bounds
        if (rect().contains(event->pos())) {
            if (m_state == State::Normal) {
                // First click: Request delete and enter undo-pending state
                m_state = State::UndoPending;
                startUndoTimer();
                setToolTip(tr("Click to undo delete"));
                emit deleteRequested();
            } else {
                // Click while in UndoPending: User wants to undo
                m_confirmTimer->stop();
                emit undoRequested();
                resetToNormal();
            }
        }
        
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void UndoDeleteButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void UndoDeleteButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;  // Cancel press if mouse leaves
    update();
    QWidget::leaveEvent(event);
}

void UndoDeleteButton::onTimerExpired()
{
    if (m_state == State::UndoPending) {
        emit deleteConfirmed();
        resetToNormal();
    }
}

bool UndoDeleteButton::isDarkMode() const
{
    // Use cached value if set, otherwise detect from palette
    if (m_darkMode) {
        return true;
    }
    
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance
    const qreal luminance = 0.299 * windowColor.redF() 
                          + 0.587 * windowColor.greenF() 
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

QColor UndoDeleteButton::backgroundColor() const
{
    if (m_state == State::UndoPending) {
        // Undo state: Use a distinct color to draw attention
        // A warm orange/amber color for "undo available"
        if (isDarkMode()) {
            return QColor(120, 80, 40);  // Dark amber
        } else {
            return QColor(255, 200, 120);  // Light amber
        }
    }
    
    // Normal state: Same neutral color as other action bar buttons
    if (isDarkMode()) {
        return QColor(60, 60, 60);
    } else {
        return QColor(220, 220, 220);
    }
}

void UndoDeleteButton::updateIcons()
{
    const QString suffix = m_darkMode ? "_reversed" : "";
    
    // Delete icon (trash/bin)
    m_deleteIcon = QIcon(QString(":/resources/icons/deletePage%1.png").arg(suffix));
    
    // Fallback if deletePage icon doesn't exist, try "delete" or "trash"
    if (m_deleteIcon.isNull()) {
        m_deleteIcon = QIcon(QString(":/resources/icons/delete%1.png").arg(suffix));
    }
    
    // Undo icon
    m_undoIcon = QIcon(QString(":/resources/icons/undo%1.png").arg(suffix));
    
    update();
}

void UndoDeleteButton::startUndoTimer()
{
    m_confirmTimer->start();
}

void UndoDeleteButton::resetToNormal()
{
    m_state = State::Normal;
    setToolTip(tr("Delete page"));
    update();
}

