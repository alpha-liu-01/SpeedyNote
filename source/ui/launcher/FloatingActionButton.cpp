#include "FloatingActionButton.h"
#include "../../compat/qt_compat.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QToolTip>
#include <QMap>

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
    // 5 buttons on desktop, 4 on Android (Open Notebook hidden)
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    int numActionButtons = 4;  // Open Notebook hidden on Android
#else
    int numActionButtons = 5;
#endif
    int totalHeight = MAIN_BUTTON_SIZE + numActionButtons * (ACTION_BUTTON_SIZE + BUTTON_SPACING);
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
    m_edgelessBtn = createActionButton("fullscreen", tr("New Edgeless Canvas"));
    m_pagedBtn = createActionButton("bookmark", tr("New Paged Notebook"));
    m_pdfBtn = createActionButton("pdf", tr("Open PDF for Annotation"));
    m_openBtn = createActionButton("folder", tr("Open Notebook (.snb)"));
    m_importBtn = createActionButton("import", tr("Import Package (.snbx)"));
    
    m_actionButtons << m_edgelessBtn << m_pagedBtn << m_pdfBtn << m_openBtn << m_importBtn;
    
    // Hide "Open Notebook" on Android - users should use Import Package instead
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_openBtn->setVisible(false);
    m_actionButtons.removeOne(m_openBtn);
#endif
    
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
    connect(m_importBtn, &QPushButton::clicked, this, [this]() {
        setExpanded(false);
        emit importPackage();
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
    
    // Style - unified gray colors: dark #2a2e32/#3a3e42/#4d4d4d, light #F5F5F5/#E8E8E8/#D0D0D0
    QString bgColor = m_darkMode ? "#2a2e32" : "#F5F5F5";
    QString hoverColor = m_darkMode ? "#3a3e42" : "#E8E8E8";
    QString borderColor = m_darkMode ? "#4d4d4d" : "#D0D0D0";
    
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
     .arg(borderColor));
    
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
        SN_CONNECT_ONCE(m_animGroup, &QParallelAnimationGroup::finished, this, [this]() {
            if (!m_expanded) {
                for (QPushButton* btn : m_actionButtons) {
                    btn->setVisible(false);
                }
            }
        });
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
    // Get device pixel ratio for high-DPI support
    qreal dpr = devicePixelRatioF();
    
    // Load the appropriate icon based on theme
    // The main button has a blue background, so always use the reversed (white) icon
    QString iconPath = ":/resources/icons/addtab_reversed.png";
    QPixmap sourcePixmap(iconPath);
    
    // Scale the source icon at high resolution for crisp rendering
    constexpr int iconSize = 28;  // Logical size
    int scaledIconSize = qRound(iconSize * dpr);
    QPixmap scaledIcon = sourcePixmap.scaled(scaledIconSize, scaledIconSize, 
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaledIcon.setDevicePixelRatio(dpr);
    
    // Create output pixmap at high-DPI resolution
    int scaledButtonSize = qRound(MAIN_BUTTON_SIZE * dpr);
    QPixmap rotatedPixmap(scaledButtonSize, scaledButtonSize);
    rotatedPixmap.setDevicePixelRatio(dpr);
    rotatedPixmap.fill(Qt::transparent);
    
    QPainter painter(&rotatedPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Work in logical coordinates (QPainter handles DPR automatically)
    painter.translate(MAIN_BUTTON_SIZE / 2.0, MAIN_BUTTON_SIZE / 2.0);
    painter.rotate(m_rotation);
    painter.drawPixmap(-iconSize / 2, -iconSize / 2, scaledIcon);
    
    painter.end();
    
    m_mainButton->setIcon(QIcon(rotatedPixmap));
    m_mainButton->setIconSize(QSize(MAIN_BUTTON_SIZE, MAIN_BUTTON_SIZE));
}

void FloatingActionButton::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        
        // Map buttons to their icon names
        QMap<QPushButton*, QString> buttonIcons;
        buttonIcons[m_edgelessBtn] = "fullscreen";
        buttonIcons[m_pagedBtn] = "bookmark";
        buttonIcons[m_pdfBtn] = "pdf";
        buttonIcons[m_openBtn] = "folder";
        buttonIcons[m_importBtn] = "import";
        
        // Update action button styles and icons
        for (QPushButton* btn : m_actionButtons) {
            QString iconName = buttonIcons.value(btn);
            if (iconName.isEmpty()) continue;
            
            QString iconPath = dark 
                ? QString(":/resources/icons/%1_reversed.png").arg(iconName)
                : QString(":/resources/icons/%1.png").arg(iconName);
            btn->setIcon(QIcon(iconPath));
            
            // Unified gray colors: dark #2a2e32/#3a3e42/#4d4d4d, light #F5F5F5/#E8E8E8/#D0D0D0
            QString bgColor = dark ? "#2a2e32" : "#F5F5F5";
            QString hoverColor = dark ? "#3a3e42" : "#E8E8E8";
            QString borderColor = dark ? "#4d4d4d" : "#D0D0D0";
            
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

