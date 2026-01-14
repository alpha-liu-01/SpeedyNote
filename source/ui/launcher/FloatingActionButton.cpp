#include "FloatingActionButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QToolTip>

FloatingActionButton::FloatingActionButton(QWidget* parent)
    : QWidget(parent)
{
    // Make this widget transparent and overlay on parent
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    
    setupUi();
    setupAnimations();
    
    // Install event filter on parent to detect clicks outside
    if (parent) {
        parent->installEventFilter(this);
    }
}

void FloatingActionButton::setupUi()
{
    // Calculate total size needed
    int totalHeight = MAIN_BUTTON_SIZE + 4 * (ACTION_BUTTON_SIZE + BUTTON_SPACING);
    int totalWidth = MAIN_BUTTON_SIZE;
    setFixedSize(totalWidth, totalHeight);
    
    // Create main FAB button
    m_mainButton = new QPushButton(this);
    m_mainButton->setFixedSize(MAIN_BUTTON_SIZE, MAIN_BUTTON_SIZE);
    m_mainButton->setCursor(Qt::PointingHandCursor);
    m_mainButton->setToolTip(tr("Create new notebook"));
    updateMainButtonIcon();
    
    // Style the main button
    m_mainButton->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: #1a73e8;"
        "  border: none;"
        "  border-radius: %1px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1557b0;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #104a9e;"
        "}"
    ).arg(MAIN_BUTTON_SIZE / 2));
    
    connect(m_mainButton, &QPushButton::clicked, this, &FloatingActionButton::toggle);
    
    // Create action buttons (bottom to top order when expanded)
    m_edgelessBtn = createActionButton("edgeless", tr("New Edgeless Canvas"));
    m_pagedBtn = createActionButton("paged", tr("New Paged Notebook"));
    m_pdfBtn = createActionButton("pdf", tr("Open PDF for Annotation"));
    m_openBtn = createActionButton("folder", tr("Open Notebook (.snb)"));
    
    m_actionButtons << m_edgelessBtn << m_pagedBtn << m_pdfBtn << m_openBtn;
    
    // Connect action buttons
    connect(m_edgelessBtn, &QPushButton::clicked, this, [this]() {
        setExpanded(false);
        emit createEdgeless();
    });
    connect(m_pagedBtn, &QPushButton::clicked, this, [this]() {
        setExpanded(false);
        emit createPaged();
    });
    connect(m_pdfBtn, &QPushButton::clicked, this, [this]() {
        setExpanded(false);
        emit openPdf();
    });
    connect(m_openBtn, &QPushButton::clicked, this, [this]() {
        setExpanded(false);
        emit openNotebook();
    });
    
    // Initial positions
    updateActionButtonPositions();
}

QPushButton* FloatingActionButton::createActionButton(const QString& iconName,
                                                       const QString& tooltip)
{
    QPushButton* btn = new QPushButton(this);
    btn->setFixedSize(ACTION_BUTTON_SIZE, ACTION_BUTTON_SIZE);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setVisible(false);  // Hidden initially
    
    // Load icon
    QString iconPath = m_darkMode 
        ? QString(":/resources/icons/%1_reversed.png").arg(iconName)
        : QString(":/resources/icons/%1.png").arg(iconName);
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(24, 24));
    
    // Style
    QString bgColor = m_darkMode ? "#424242" : "#ffffff";
    QString hoverColor = m_darkMode ? "#525252" : "#f5f5f5";
    QString pressColor = m_darkMode ? "#616161" : "#e0e0e0";
    
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1;"
        "  border: 1px solid %4;"
        "  border-radius: %2px;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "}"
    ).arg(bgColor)
     .arg(ACTION_BUTTON_SIZE / 2)
     .arg(hoverColor)
     .arg(m_darkMode ? "#616161" : "#e0e0e0"));
    
    return btn;
}

void FloatingActionButton::setupAnimations()
{
    m_expandAnim = new QPropertyAnimation(this, "expandProgress", this);
    m_expandAnim->setDuration(ANIMATION_DURATION);
    m_expandAnim->setEasingCurve(QEasingCurve::OutCubic);
    
    m_rotateAnim = new QPropertyAnimation(this, "rotation", this);
    m_rotateAnim->setDuration(ANIMATION_DURATION);
    m_rotateAnim->setEasingCurve(QEasingCurve::OutCubic);
    
    m_animGroup = new QParallelAnimationGroup(this);
    m_animGroup->addAnimation(m_expandAnim);
    m_animGroup->addAnimation(m_rotateAnim);
}

void FloatingActionButton::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    
    // Show action buttons before animating in
    if (expanded) {
        for (QPushButton* btn : m_actionButtons) {
            btn->setVisible(true);
        }
    }
    
    // Animate
    m_animGroup->stop();
    
    m_expandAnim->setStartValue(m_expandProgress);
    m_expandAnim->setEndValue(expanded ? 1.0 : 0.0);
    
    m_rotateAnim->setStartValue(m_rotation);
    m_rotateAnim->setEndValue(expanded ? 45.0 : 0.0);
    
    // Hide buttons after collapse animation
    if (!expanded) {
        connect(m_animGroup, &QParallelAnimationGroup::finished, this, [this]() {
            if (!m_expanded) {
                for (QPushButton* btn : m_actionButtons) {
                    btn->setVisible(false);
                }
            }
            disconnect(m_animGroup, &QParallelAnimationGroup::finished, this, nullptr);
        }, Qt::SingleShotConnection);
    }
    
    m_animGroup->start();
}

void FloatingActionButton::toggle()
{
    setExpanded(!m_expanded);
}

void FloatingActionButton::setExpandProgress(qreal progress)
{
    m_expandProgress = progress;
    updateActionButtonPositions();
}

void FloatingActionButton::setRotation(qreal rotation)
{
    m_rotation = rotation;
    updateMainButtonIcon();
}

void FloatingActionButton::updateActionButtonPositions()
{
    // Main button is at the bottom
    int mainY = height() - MAIN_BUTTON_SIZE;
    int centerX = (width() - MAIN_BUTTON_SIZE) / 2;
    m_mainButton->move(centerX, mainY);
    
    // Action buttons stack upward from main button
    int btnCenterX = (width() - ACTION_BUTTON_SIZE) / 2;
    
    for (int i = 0; i < m_actionButtons.size(); ++i) {
        QPushButton* btn = m_actionButtons[i];
        
        // Target Y position when fully expanded
        int targetY = mainY - (i + 1) * (ACTION_BUTTON_SIZE + BUTTON_SPACING);
        
        // Interpolate based on expand progress
        int currentY = mainY + static_cast<int>((targetY - mainY) * m_expandProgress);
        
        btn->move(btnCenterX, currentY);
        
        // Fade in/out
        btn->setWindowOpacity(m_expandProgress);
    }
    
    update();
}

void FloatingActionButton::updateMainButtonIcon()
{
    // Draw rotated + sign
    QPixmap pixmap(MAIN_BUTTON_SIZE, MAIN_BUTTON_SIZE);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Translate to center and rotate
    painter.translate(MAIN_BUTTON_SIZE / 2, MAIN_BUTTON_SIZE / 2);
    painter.rotate(m_rotation);
    
    // Draw + sign
    painter.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap));
    int armLength = 12;
    painter.drawLine(-armLength, 0, armLength, 0);
    painter.drawLine(0, -armLength, 0, armLength);
    
    painter.end();
    
    m_mainButton->setIcon(QIcon(pixmap));
    m_mainButton->setIconSize(QSize(MAIN_BUTTON_SIZE, MAIN_BUTTON_SIZE));
}

void FloatingActionButton::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        
        // Update action button styles and icons
        for (int i = 0; i < m_actionButtons.size(); ++i) {
            QPushButton* btn = m_actionButtons[i];
            
            QString iconName;
            switch (i) {
                case 0: iconName = "edgeless"; break;
                case 1: iconName = "paged"; break;
                case 2: iconName = "pdf"; break;
                case 3: iconName = "folder"; break;
            }
            
            QString iconPath = dark 
                ? QString(":/resources/icons/%1_reversed.png").arg(iconName)
                : QString(":/resources/icons/%1.png").arg(iconName);
            btn->setIcon(QIcon(iconPath));
            
            QString bgColor = dark ? "#424242" : "#ffffff";
            QString hoverColor = dark ? "#525252" : "#f5f5f5";
            QString borderColor = dark ? "#616161" : "#e0e0e0";
            
            btn->setStyleSheet(QString(
                "QPushButton {"
                "  background-color: %1;"
                "  border: 1px solid %3;"
                "  border-radius: %2px;"
                "}"
                "QPushButton:hover {"
                "  background-color: %4;"
                "}"
            ).arg(bgColor)
             .arg(ACTION_BUTTON_SIZE / 2)
             .arg(borderColor)
             .arg(hoverColor));
        }
    }
}

void FloatingActionButton::positionInParent()
{
    if (parentWidget()) {
        int x = parentWidget()->width() - width() - MARGIN;
        int y = parentWidget()->height() - height() - MARGIN;
        move(x, y);
    }
}

void FloatingActionButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    // Transparent - buttons handle their own painting
}

bool FloatingActionButton::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && m_expanded) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            // Check if click is outside FAB area
            QPoint localPos = mapFromParent(me->pos());
            if (!rect().contains(localPos)) {
                setExpanded(false);
            }
        } else if (event->type() == QEvent::Resize) {
            positionInParent();
        }
    }
    return QWidget::eventFilter(watched, event);
}

