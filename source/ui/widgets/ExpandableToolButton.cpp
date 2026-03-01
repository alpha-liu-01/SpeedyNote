#include "ExpandableToolButton.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>

ExpandableToolButton::ExpandableToolButton(QWidget* parent)
    : QWidget(parent)
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(CONTENT_SPACING);

    m_toolButton = new ToolButton(this);
    m_toolButton->setProperty("inExpandable", QVariant(true));
    m_mainLayout->addWidget(m_toolButton);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(36);
}

void ExpandableToolButton::setContentWidget(QWidget* widget)
{
    if (m_contentWidget && m_contentWidget != widget) {
        m_mainLayout->removeWidget(m_contentWidget);
        delete m_contentWidget;
    }

    m_contentWidget = widget;

    if (m_contentWidget) {
        m_contentWidget->setParent(this);
        m_mainLayout->addWidget(m_contentWidget);
        m_contentWidget->setVisible(m_expanded);
    }
}

void ExpandableToolButton::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;

    m_expanded = expanded;
    updateContentVisibility();
    updateGeometry();
    emit expandedChanged(expanded);
}

void ExpandableToolButton::setThemedIcon(const QString& baseName)
{
    m_toolButton->setThemedIcon(baseName);
}

void ExpandableToolButton::setDarkMode(bool darkMode)
{
    m_darkMode = darkMode;
    m_toolButton->setDarkMode(darkMode);
    update();
}

QSize ExpandableToolButton::sizeHint() const
{
    int w = 36;
    if (m_expanded && m_contentWidget && m_contentWidget->isVisible()) {
        w += CONTENT_SPACING + m_contentWidget->sizeHint().width();
    }
    return QSize(w, 36);
}

QSize ExpandableToolButton::minimumSizeHint() const
{
    return sizeHint();
}

void ExpandableToolButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_expanded)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF bgRect = QRectF(rect()).adjusted(1, 2, -1, -2);
    QPainterPath path;
    path.addRoundedRect(bgRect, BORDER_RADIUS, BORDER_RADIUS);

    // Draw shadow / glow (two passes for softness)
    for (int i = 2; i >= 1; --i) {
        QPainterPath shadowPath;
        QRectF shadowRect = bgRect.adjusted(-i, i, i, i);
        shadowPath.addRoundedRect(shadowRect, BORDER_RADIUS + i, BORDER_RADIUS + i);
        painter.setPen(Qt::NoPen);
        if (m_darkMode) {
            painter.setBrush(QColor(255, 255, 255, 15 + i * 5));
        } else {
            painter.setBrush(QColor(0, 0, 0, 15 + i * 5));
        }
        painter.drawPath(shadowPath);
    }

    // Draw background with subtle border
    if (m_darkMode) {
        painter.setPen(QPen(QColor(255, 255, 255, 30), 0.5));
        painter.setBrush(QColor(0, 0, 0));
    } else {
        painter.setPen(QPen(QColor(0, 0, 0, 25), 0.5));
        painter.setBrush(QColor(255, 255, 255));
    }
    painter.drawPath(path);
}

void ExpandableToolButton::updateContentVisibility()
{
    if (m_contentWidget) {
        m_contentWidget->setVisible(m_expanded);
    }
}
