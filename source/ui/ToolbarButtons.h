#ifndef TOOLBARBUTTONS_H
#define TOOLBARBUTTONS_H

#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QWidget>

/**
 * Utility class for loading and applying button stylesheets.
 * Call applyButtonStyles() on a parent widget to style all toolbar buttons within it.
 */
class ButtonStyles {
public:
    /**
     * Load and apply the appropriate button stylesheet to a widget.
     * All ToolbarButton descendants within the widget will be styled.
     * @param widget The parent widget (e.g., NavigationBar, Toolbar)
     * @param darkMode True for dark theme, false for light theme
     */
    static void applyToWidget(QWidget *widget, bool darkMode);
    
    /**
     * Get the stylesheet string for the given theme.
     * @param darkMode True for dark theme, false for light theme
     * @return The stylesheet string
     */
    static QString getStylesheet(bool darkMode);
    
private:
    static QString loadFromResource(const QString &path);
};

/**
 * Base class for all toolbar-style buttons.
 * Provides common functionality:
 * - Fixed 36x36 logical pixel size
 * - Icon loading with light/dark theme support
 * - Theme-aware styling
 */
class ToolbarButton : public QPushButton {
    Q_OBJECT

public:
    explicit ToolbarButton(QWidget *parent = nullptr);
    
    /**
     * Set icons for light and dark themes.
     * @param baseName Base name without path/extension (e.g., "save")
     *                 Will load ":/resources/icons/save.png" for light
     *                 and ":/resources/icons/save_reversed.png" for dark
     */
    void setThemedIcon(const QString &baseName);
    
    /**
     * Update button appearance for current theme.
     * @param darkMode True for dark theme, false for light theme
     */
    virtual void setDarkMode(bool darkMode);
    
    /**
     * Get current dark mode state.
     */
    bool isDarkMode() const { return m_darkMode; }

protected:
    virtual void updateIcon();
    
    QString m_iconBaseName;
    bool m_darkMode = false;
};

/**
 * Instant action button - click triggers action, no persistent state.
 * States: idle, hover, pressed
 * Examples: Save, Undo, Redo, Menu, Launcher (back)
 */
class ActionButton : public ToolbarButton {
    Q_OBJECT

public:
    explicit ActionButton(QWidget *parent = nullptr);
};

/**
 * Toggle button - click toggles on/off state.
 * States: off, off+hover, on, on+hover, pressed
 * Examples: Bookmarks, Outline, Layers, Fullscreen, Markdown Notes
 */
class ToggleButton : public ToolbarButton {
    Q_OBJECT

public:
    explicit ToggleButton(QWidget *parent = nullptr);
};

/**
 * Three-state button - click cycles through 3 states.
 * States: state0, state1, state2 (with hover/pressed variants)
 * State 1 has a red shade indicator.
 * Examples: Touch gesture mode (off / y-axis only / on)
 * 
 * Uses Q_PROPERTY for QSS styling based on state.
 */
class ThreeStateButton : public ToolbarButton {
    Q_OBJECT
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)

public:
    explicit ThreeStateButton(QWidget *parent = nullptr);
    
    /**
     * Get current state (0, 1, or 2).
     */
    int state() const { return m_state; }
    
    /**
     * Set current state (0, 1, or 2).
     */
    void setState(int state);
    
    /**
     * Set icons for each of the three states.
     * @param baseName0 Icon base name for state 0
     * @param baseName1 Icon base name for state 1
     * @param baseName2 Icon base name for state 2
     */
    void setStateIcons(const QString &baseName0, 
                       const QString &baseName1, 
                       const QString &baseName2);

signals:
    void stateChanged(int newState);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void updateIcon() override;

private:
    int m_state = 0;
    QString m_stateIconBaseNames[3];
};

/**
 * Tool button - exclusive selection within a group, opens associated subtoolbar.
 * Visually identical to ToggleButton.
 * States: off, off+hover, on, on+hover, pressed
 * Examples: Pen, Marker, Eraser, Lasso, Object Selection, Text
 * 
 * Note: Use with QButtonGroup for exclusive selection.
 */
class ToolButton : public ToggleButton {
    Q_OBJECT

public:
    explicit ToolButton(QWidget *parent = nullptr);
};

#endif // TOOLBARBUTTONS_H

