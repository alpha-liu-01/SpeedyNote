#include "ActionBar.h"

#include <QFrame>
#include <QPalette>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

ActionBar::ActionBar(QWidget* parent)
    : QWidget(parent)
{
    // Set fixed width (same as SubToolbar)
    setFixedWidth(ACTIONBAR_WIDTH);
    
    // Create main layout
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    
    // Apply styling
    setupStyle();
}

void ActionBar::addButton(QWidget* button)
{
    if (button) {
        m_layout->addWidget(button, 0, Qt::AlignHCenter);
    }
}

void ActionBar::addSeparator()
{
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(2);
    separator->setFixedWidth(ACTIONBAR_WIDTH - 2 * PADDING);
    
    // Style the separator based on theme (unified gray: dark #4d4d4d, light #D0D0D0)
    if (isDarkMode()) {
        separator->setStyleSheet("background-color: #4d4d4d; border: none;");
    } else {
        separator->setStyleSheet("background-color: #D0D0D0; border: none;");
    }
    
    m_layout->addWidget(separator, 0, Qt::AlignHCenter);
}

void ActionBar::addStretch()
{
    m_layout->addStretch();
}

void ActionBar::setupStyle()
{
    // Set background and border via stylesheet
    // Unified gray colors: dark #2a2e32/#4d4d4d, light #F5F5F5/#D0D0D0
    QString bgColor;
    QString borderColor;
    
    if (isDarkMode()) {
        bgColor = "#2a2e32";
        borderColor = "#4d4d4d";
    } else {
        bgColor = "#F5F5F5";
        borderColor = "#D0D0D0";
    }
    
    setStyleSheet(QString(
        "ActionBar {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(bgColor, borderColor).arg(BORDER_RADIUS));
    
    // Add drop shadow for depth (same as SubToolbar)
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(0, 0, 0, isDarkMode() ? 100 : 50));
    setGraphicsEffect(shadow);
}

bool ActionBar::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance (simplified)
    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

void ActionBar::setDarkMode(bool darkMode)
{
    Q_UNUSED(darkMode);
    
    // Update the action bar's own styling (background, border, shadow)
    setupStyle();
    
    // Update separator styling (unified gray: dark #4d4d4d, light #D0D0D0)
    // Find all QFrame children that are separators and update their style
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::HLine) {
            if (isDarkMode()) {
                frame->setStyleSheet("background-color: #4d4d4d; border: none;");
            } else {
                frame->setStyleSheet("background-color: #D0D0D0; border: none;");
            }
        }
    }
    
    // Subclasses should override this to also propagate dark mode to their buttons
}

