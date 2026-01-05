#ifndef NAVIGATIONBAR_H
#define NAVIGATIONBAR_H

#include <QWidget>
#include <QColor>
#include <QPushButton>
#include "ToolbarButtons.h"

/**
 * NavigationBar - Top bar for global/app-wide actions.
 * 
 * Layout:
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │ [←][📁][💾][+]          document_name.snb          [⛶][📤][📝][⋮]      │
 * └──────────────────────────────────────────────────────────────────────────┘
 *   Left side              Center (clickable)           Right side
 * 
 * Left: Launcher, Left Sidebar Toggle, Save, Add
 * Center: Filename (click to toggle tab bar)
 * Right: Fullscreen, Share, Right Sidebar Toggle, Menu
 */
class NavigationBar : public QWidget {
    Q_OBJECT

public:
    explicit NavigationBar(QWidget *parent = nullptr);
    
    /**
     * Set the displayed filename.
     * Long names are elided with "..." in the middle.
     */
    void setFilename(const QString &filename);
    
    /**
     * Update theme colors.
     * @param darkMode True for dark theme icons
     * @param accentColor Background color for the navigation bar
     */
    void updateTheme(bool darkMode, const QColor &accentColor);
    
    /**
     * Set left sidebar toggle state (for external sync).
     */
    void setLeftSidebarChecked(bool checked);
    
    /**
     * Set right sidebar toggle state (for external sync).
     */
    void setRightSidebarChecked(bool checked);
    
    /**
     * Set fullscreen toggle state (for external sync).
     */
    void setFullscreenChecked(bool checked);

signals:
    // Left side
    void launcherClicked();
    void leftSidebarToggled(bool checked);
    void saveClicked();
    void addClicked();  // Stubbed - will show menu in future
    
    // Center
    void filenameClicked();  // Toggle tab container visibility
    
    // Right side
    void fullscreenToggled(bool checked);
    void shareClicked();  // Stubbed - placeholder
    void rightSidebarToggled(bool checked);  // Markdown notes
    void menuRequested();

private:
    void setupUi();
    void connectSignals();
    QString elideFilename(const QString &filename, int maxWidth);
    
    // Left buttons
    ActionButton *m_launcherButton;
    ToggleButton *m_leftSidebarButton;
    ActionButton *m_saveButton;
    ActionButton *m_addButton;
    
    // Center
    QPushButton *m_filenameButton;
    QString m_fullFilename;  // Store full name for tooltip
    
    // Right buttons
    ToggleButton *m_fullscreenButton;
    ActionButton *m_shareButton;
    ToggleButton *m_rightSidebarButton;
    ActionButton *m_menuButton;
    
    // State
    bool m_darkMode = false;
    QColor m_accentColor = QColor(0x22, 0x77, 0xCC);  // #2277CC default
};

#endif // NAVIGATIONBAR_H

