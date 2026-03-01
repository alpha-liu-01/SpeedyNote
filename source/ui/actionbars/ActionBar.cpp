#include "ActionBar.h"

#include <QFrame>
#include <QPalette>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

ActionBar::ActionBar(QWidget* parent)
    : QWidget(parent)
    , m_darkMode(isDarkMode())
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
    
    separator->setStyleSheet(m_darkMode
        ? "background-color: #4d4d4d; border: none;"
        : "background-color: #D0D0D0; border: none;");
    
    m_layout->addWidget(separator, 0, Qt::AlignHCenter);
}

void ActionBar::addStretch()
{
    m_layout->addStretch();
}

void ActionBar::setupStyle()
{
    QString bgColor = m_darkMode ? "#2a2e32" : "#F5F5F5";
    QString borderColor = m_darkMode ? "#4d4d4d" : "#D0D0D0";
    
    setStyleSheet(QString(
        "ActionBar {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(bgColor, borderColor).arg(BORDER_RADIUS));
    
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(0, 0, 0, m_darkMode ? 100 : 50));
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
    m_darkMode = darkMode;
    
    setupStyle();
    
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::HLine) {
            frame->setStyleSheet(m_darkMode
                ? "background-color: #4d4d4d; border: none;"
                : "background-color: #D0D0D0; border: none;");
        }
    }
}

