#include "NavigationBar.h"
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QPalette>
#include <QDebug>

NavigationBar::NavigationBar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    // Note: Don't call updateTheme() here - MainWindow::loadThemeSettings() 
    // will call it after loading user preferences to avoid double initialization
}

void NavigationBar::setupUi()
{
    // Fixed height for navigation bar
    setFixedHeight(44);
    
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);
    
    // === Left side buttons ===
    m_launcherButton = new ActionButton(this);
    m_launcherButton->setThemedIcon("recent");  // Back to launcher/recent
    m_launcherButton->setToolTip(tr("Back to Launcher"));
    mainLayout->addWidget(m_launcherButton);
    
    m_leftSidebarButton = new ToggleButton(this);
    m_leftSidebarButton->setThemedIcon("leftsidebar");
    m_leftSidebarButton->setToolTip(tr("Toggle Left Sidebar"));
    mainLayout->addWidget(m_leftSidebarButton);
    
    m_saveButton = new ActionButton(this);
    m_saveButton->setThemedIcon("save");
    m_saveButton->setToolTip(tr("Save (Ctrl+S)"));
    mainLayout->addWidget(m_saveButton);
    
    m_addButton = new ActionButton(this);
    m_addButton->setThemedIcon("addtab");
    m_addButton->setToolTip(tr("New Document"));
    mainLayout->addWidget(m_addButton);
    
    // === Center - Filename (with stretch on both sides) ===
    mainLayout->addStretch(1);
    
    m_filenameButton = new QPushButton(this);
    m_filenameButton->setText(tr("Untitled"));
    m_filenameButton->setFlat(true);
    m_filenameButton->setCursor(Qt::PointingHandCursor);
    m_filenameButton->setToolTip(tr("Click to toggle tab bar"));
    // Style will be set in updateTheme()
    mainLayout->addWidget(m_filenameButton);
    
    mainLayout->addStretch(1);
    
    // === Right side buttons ===
    m_fullscreenButton = new ToggleButton(this);
    m_fullscreenButton->setThemedIcon("fullscreen");
    m_fullscreenButton->setToolTip(tr("Toggle Fullscreen"));
    mainLayout->addWidget(m_fullscreenButton);
    
    m_shareButton = new ActionButton(this);
    m_shareButton->setThemedIcon("export");
#ifdef Q_OS_ANDROID
    m_shareButton->setToolTip(tr("Share Notebook"));
#else
    m_shareButton->setToolTip(tr("Export Notebook"));
#endif
    mainLayout->addWidget(m_shareButton);
    
    m_rightSidebarButton = new ToggleButton(this);
    m_rightSidebarButton->setThemedIcon("rightsidebar");
    m_rightSidebarButton->setToolTip(tr("Toggle Markdown Notes"));
    mainLayout->addWidget(m_rightSidebarButton);
    
    m_menuButton = new ActionButton(this);
    m_menuButton->setThemedIcon("menu");
    m_menuButton->setToolTip(tr("Menu"));
    mainLayout->addWidget(m_menuButton);
}

void NavigationBar::connectSignals()
{
    // Left side
    connect(m_launcherButton, &QPushButton::clicked, 
            this, &NavigationBar::launcherClicked);
    connect(m_leftSidebarButton, &QPushButton::toggled,
            this, &NavigationBar::leftSidebarToggled);
    connect(m_saveButton, &QPushButton::clicked,
            this, &NavigationBar::saveClicked);
    connect(m_addButton, &QPushButton::clicked,
            this, &NavigationBar::addClicked);
    
    // Center
    connect(m_filenameButton, &QPushButton::clicked,
            this, &NavigationBar::filenameClicked);
    
    // Right side
    connect(m_fullscreenButton, &QPushButton::toggled,
            this, &NavigationBar::fullscreenToggled);
    connect(m_shareButton, &QPushButton::clicked,
            this, &NavigationBar::shareClicked);
    connect(m_rightSidebarButton, &QPushButton::toggled,
            this, &NavigationBar::rightSidebarToggled);
    connect(m_menuButton, &QPushButton::clicked,
            this, &NavigationBar::menuRequested);
}

void NavigationBar::setFilename(const QString &filename)
{
    m_fullFilename = filename;
    
    // Elide if too long (max ~200px for filename area)
    QString displayName = elideFilename(filename, 200);
    m_filenameButton->setText(displayName);
    
    // Show full name in tooltip if elided
    if (displayName != filename) {
        m_filenameButton->setToolTip(filename + "\n" + tr("Click to toggle tab bar"));
    } else {
        m_filenameButton->setToolTip(tr("Click to toggle tab bar"));
    }
}

QString NavigationBar::elideFilename(const QString &filename, int maxWidth)
{
    QFontMetrics fm(m_filenameButton->font());
    
    if (fm.horizontalAdvance(filename) <= maxWidth) {
        return filename;
    }
    
    // Elide in the middle to preserve extension
    return fm.elidedText(filename, Qt::ElideMiddle, maxWidth);
}

void NavigationBar::updateTheme(bool darkMode, const QColor &accentColor)
{
    m_darkMode = darkMode;
    m_accentColor = accentColor;
    
    // Clear any existing stylesheet that might interfere with palette
    setStyleSheet(QString());
    
    // Apply background color using palette ONLY (most reliable for custom widgets)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, accentColor);
    setPalette(pal);
    
    // Apply button styles to each button individually instead of parent widget
    // This avoids the stylesheet interfering with our palette background
    for (QObject *child : children()) {
        if (auto *button = qobject_cast<ToolbarButton*>(child)) {
            button->setDarkMode(darkMode);
        }
    }
    
    // Update all button icons for theme
    m_launcherButton->setDarkMode(darkMode);
    m_leftSidebarButton->setDarkMode(darkMode);
    m_saveButton->setDarkMode(darkMode);
    m_addButton->setDarkMode(darkMode);
    m_fullscreenButton->setDarkMode(darkMode);
    m_shareButton->setDarkMode(darkMode);
    m_rightSidebarButton->setDarkMode(darkMode);
    m_menuButton->setDarkMode(darkMode);
    
    // Style filename button to match theme
    QString textColor = darkMode ? "#ffffff" : "#000000";
    m_filenameButton->setStyleSheet(QString(
        "QPushButton { "
        "   color: %1; "
        "   background: transparent; "
        "   border: none; "
        "   padding: 4px 12px; "
        "   font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "   background: rgba(%2, 30); "
        "}"
    ).arg(textColor)
     .arg(darkMode ? "255, 255, 255" : "0, 0, 0"));
}

void NavigationBar::setLeftSidebarChecked(bool checked)
{
    m_leftSidebarButton->setChecked(checked);
}

void NavigationBar::setRightSidebarChecked(bool checked)
{
    m_rightSidebarButton->setChecked(checked);
}

void NavigationBar::setFullscreenChecked(bool checked)
{
    m_fullscreenButton->setChecked(checked);
}

