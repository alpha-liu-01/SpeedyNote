#include "SubToolbar.h"

#include <QFrame>
#include <QPalette>
#include <QApplication>

SubToolbar::SubToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(PADDING_LEFT, 0, PADDING_RIGHT, 0);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Transparent background -- the parent ExpandableToolButton handles painting
    // Scoped by object name so child dialogs don't inherit transparent bg
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName("SubToolbarWidget");
    setStyleSheet("#SubToolbarWidget { background: transparent; }");
}

void SubToolbar::addSeparator()
{
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedWidth(2);
    separator->setFixedHeight(SEPARATOR_HEIGHT);

    if (isDarkMode()) {
        separator->setStyleSheet("background-color: #4d4d4d; border: none;");
    } else {
        separator->setStyleSheet("background-color: #D0D0D0; border: none;");
    }

    m_layout->addWidget(separator, 0, Qt::AlignVCenter);
}

void SubToolbar::addWidget(QWidget* widget)
{
    if (widget) {
        m_layout->addWidget(widget, 0, Qt::AlignVCenter);
    }
}

void SubToolbar::addStretch()
{
    m_layout->addStretch();
}

bool SubToolbar::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);

    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();

    return luminance < 0.5;
}

void SubToolbar::setDarkMode(bool darkMode)
{
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::VLine) {
            if (darkMode) {
                frame->setStyleSheet("background-color: #4d4d4d; border: none;");
            } else {
                frame->setStyleSheet("background-color: #D0D0D0; border: none;");
            }
        }
    }
}
