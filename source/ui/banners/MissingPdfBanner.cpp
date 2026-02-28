#include "MissingPdfBanner.h"
#include "../../compat/qt_compat.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <QApplication>

MissingPdfBanner::MissingPdfBanner(QWidget* parent)
    : QWidget(parent)
    , m_animation(new QPropertyAnimation(this, "slideOffset", this))
{
    setupUi();
    
    // Start hidden (above the viewport)
    m_slideOffset = -BANNER_HEIGHT;
    setFixedHeight(BANNER_HEIGHT);
    
    // Configure animation
    m_animation->setDuration(ANIMATION_DURATION);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
}

void MissingPdfBanner::setupUi()
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);
    
    // Warning icon
    m_iconLabel = new QLabel(this);
    QPixmap warningIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(20, 20);
    m_iconLabel->setPixmap(warningIcon);
    m_iconLabel->setFixedSize(20, 20);
    
    // Message label
    m_messageLabel = new QLabel(tr("PDF file not found"), this);
    m_messageLabel->setStyleSheet("color: #5a3d00; font-weight: 500;");
    m_messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    // Locate PDF button
    m_locateButton = new QPushButton(tr("Locate PDF"), this);
    m_locateButton->setCursor(Qt::PointingHandCursor);
    m_locateButton->setStyleSheet(R"(
        QPushButton {
            background: #d35400;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 4px 12px;
            font-weight: bold;
            font-size: 11px;
        }
        QPushButton:hover {
            background: #e67e22;
        }
        QPushButton:pressed {
            background: #ba4a00;
        }
    )");
    connect(m_locateButton, &QPushButton::clicked, this, &MissingPdfBanner::locatePdfClicked);
    
    // Dismiss button
    m_dismissButton = new QPushButton(tr("Dismiss"), this);
    m_dismissButton->setCursor(Qt::PointingHandCursor);
    m_dismissButton->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            color: #5a3d00;
            border: 1px solid #c9a227;
            border-radius: 4px;
            padding: 4px 12px;
            font-size: 11px;
        }
        QPushButton:hover {
            background: rgba(0, 0, 0, 0.05);
            border-color: #8b7355;
        }
        QPushButton:pressed {
            background: rgba(0, 0, 0, 0.1);
        }
    )");
    connect(m_dismissButton, &QPushButton::clicked, this, [this]() {
        hideAnimated();
        emit dismissed();
    });
    
    layout->addWidget(m_iconLabel);
    layout->addWidget(m_messageLabel);
    layout->addStretch();
    layout->addWidget(m_locateButton);
    layout->addWidget(m_dismissButton);
}

void MissingPdfBanner::setPdfName(const QString& pdfName)
{
    m_messageLabel->setText(tr("PDF file not found: %1").arg(pdfName));
}

void MissingPdfBanner::showAnimated()
{
    // Disconnect any pending hide-on-finish connections
    disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr);
    
    show();
    m_animation->stop();
    m_animation->setStartValue(m_slideOffset);
    m_animation->setEndValue(0);
    m_animation->start();
}

void MissingPdfBanner::hideAnimated()
{
    // Disconnect any pending connections before adding new one
    disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr);
    
    m_animation->stop();
    m_animation->setStartValue(m_slideOffset);
    m_animation->setEndValue(-BANNER_HEIGHT);
    
    SN_CONNECT_ONCE(m_animation, &QPropertyAnimation::finished, this, [this]() {
        if (m_slideOffset <= -BANNER_HEIGHT) {
            hide();
        }
    });
    
    m_animation->start();
}

void MissingPdfBanner::setSlideOffset(int offset)
{
    m_slideOffset = offset;
    // Move the banner vertically based on offset
    move(x(), offset);
    update();
}

void MissingPdfBanner::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Yellow/orange warning background
    QColor bgColor(0xFFF3CD);  // Bootstrap warning yellow
    painter.fillRect(rect(), bgColor);
    
    // Bottom border for separation
    painter.setPen(QPen(QColor(0xC9, 0xA2, 0x27), 1));  // Darker yellow border
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

