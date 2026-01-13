#include "Launcher.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QKeyEvent>
#include <QGraphicsOpacityEffect>

Launcher::Launcher(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SpeedyNote"));
    setMinimumSize(800, 600);
    
    setupUi();
    setupConnections();
    applyStyle();
}

Launcher::~Launcher()
{
}

void Launcher::setupUi()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    auto* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // TODO P.3.2: Navigation tabs (Timeline | Starred)
    setupTimeline();
    setupStarred();
    setupSearch();
    
    // Content stack
    m_contentStack = new QStackedWidget(this);
    mainLayout->addWidget(m_contentStack);
    
    // Add views to stack (placeholders for now)
    m_timelineView = new QWidget(this);
    m_starredView = new QWidget(this);
    m_searchResultsView = new QWidget(this);
    
    m_contentStack->addWidget(m_timelineView);
    m_contentStack->addWidget(m_starredView);
    m_contentStack->addWidget(m_searchResultsView);
    
    // FAB
    setupFAB();
    
    // Fade animation
    m_fadeAnimation = new QPropertyAnimation(this, "fadeOpacity", this);
    m_fadeAnimation->setDuration(200);
}

void Launcher::setupTimeline()
{
    // TODO P.3.3: Implement timeline list view with NotebookListModel
}

void Launcher::setupStarred()
{
    // TODO P.3.4: Implement starred view with folders
}

void Launcher::setupSearch()
{
    // TODO P.3.5: Implement search bar and results
}

void Launcher::setupFAB()
{
    // TODO P.3.6: Implement floating action button with expand animation
    m_fabButton = new QPushButton("+", this);
    m_fabButton->setFixedSize(56, 56);
    m_fabButton->hide(); // Will be positioned in later task
}

void Launcher::setupConnections()
{
    // TODO P.3.7: Connect signals/slots
}

void Launcher::applyStyle()
{
    // TODO P.3.8: Apply styling (will use QSS file)
}

void Launcher::switchToView(View view)
{
    m_currentView = view;
    
    switch (view) {
        case View::Timeline:
            m_contentStack->setCurrentWidget(m_timelineView);
            break;
        case View::Starred:
            m_contentStack->setCurrentWidget(m_starredView);
            break;
        case View::Search:
            m_contentStack->setCurrentWidget(m_searchResultsView);
            break;
    }
}

void Launcher::showWithAnimation()
{
    m_fadeOpacity = 0.0;
    show();
    
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void Launcher::hideWithAnimation()
{
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        hide();
        disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
    }, Qt::SingleShotConnection);
    
    m_fadeAnimation->start();
}

void Launcher::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = opacity;
    setWindowOpacity(opacity);
}

void Launcher::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    // Custom painting can be added here for background effects
}

void Launcher::keyPressEvent(QKeyEvent* event)
{
    // Escape key hides launcher
    if (event->key() == Qt::Key_Escape) {
        hideWithAnimation();
        return;
    }
    
    // Ctrl+H also hides (toggle shortcut)
    if (event->key() == Qt::Key_H && event->modifiers() == Qt::ControlModifier) {
        hideWithAnimation();
        return;
    }
    
    QMainWindow::keyPressEvent(event);
}

