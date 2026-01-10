#include "SubToolbar.h"

#include <QFrame>
#include <QPalette>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

SubToolbar::SubToolbar(QWidget* parent)
    : QWidget(parent)
{
    // Set fixed width
    setFixedWidth(SUBTOOLBAR_WIDTH);
    
    // Create main layout
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    
    // Apply styling
    setupStyle();
}

void SubToolbar::addSeparator()
{
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(2);
    separator->setFixedWidth(SUBTOOLBAR_WIDTH - 2 * PADDING);
    
    // Style the separator based on theme
    if (isDarkMode()) {
        separator->setStyleSheet("background-color: #555555; border: none;");
    } else {
        separator->setStyleSheet("background-color: #CCCCCC; border: none;");
    }
    
    m_layout->addWidget(separator, 0, Qt::AlignHCenter);
}

void SubToolbar::addWidget(QWidget* widget)
{
    if (widget) {
        m_layout->addWidget(widget, 0, Qt::AlignHCenter);
    }
}

void SubToolbar::addStretch()
{
    m_layout->addStretch();
}

void SubToolbar::setupStyle()
{
    // Set background and border via stylesheet
    QString bgColor;
    QString borderColor;
    
    if (isDarkMode()) {
        bgColor = "#2D2D2D";
        borderColor = "#404040";
    } else {
        bgColor = "#F5F5F5";
        borderColor = "#D0D0D0";
    }
    
    setStyleSheet(QString(
        "SubToolbar {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(bgColor, borderColor).arg(BORDER_RADIUS));
    
    // Add drop shadow for depth
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(0, 0, 0, isDarkMode() ? 100 : 50));
    setGraphicsEffect(shadow);
}

bool SubToolbar::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance (simplified)
    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

void SubToolbar::setDarkMode(bool darkMode)
{
    Q_UNUSED(darkMode);
    
    // Update the subtoolbar's own styling (background, border, shadow)
    setupStyle();
    
    // Update separator styling
    // Find all QFrame children that are separators and update their style
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::HLine) {
            if (isDarkMode()) {
                frame->setStyleSheet("background-color: #555555; border: none;");
            } else {
                frame->setStyleSheet("background-color: #CCCCCC; border: none;");
            }
        }
    }
    
    // Subclasses should override this to also propagate dark mode to their buttons
}

