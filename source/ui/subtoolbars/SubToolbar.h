#ifndef SUBTOOLBAR_H
#define SUBTOOLBAR_H

#include <QWidget>
#include <QVBoxLayout>

class QFrame;

/**
 * @brief Abstract base class for all subtoolbars.
 * 
 * Subtoolbars provide tool-specific options and float on the left side
 * of the DocumentViewport, vertically centered.
 * 
 * Subclasses must implement:
 * - refreshFromSettings(): Load preset values from QSettings
 * - restoreTabState(int): Restore per-tab state when switching tabs
 * - saveTabState(int): Save per-tab state before switching away
 * 
 * Styling:
 * - Fixed width: ~44px (36 button + 8 padding)
 * - Rounded corners (8px radius)
 * - Shadow/border for depth
 * - Theme-aware background color
 */
class SubToolbar : public QWidget {
    Q_OBJECT

public:
    explicit SubToolbar(QWidget* parent = nullptr);
    virtual ~SubToolbar() = default;
    
    /**
     * @brief Refresh button values from QSettings.
     * 
     * Called when subtoolbar becomes visible to ensure values
     * are synchronized with global settings.
     */
    virtual void refreshFromSettings() = 0;
    
    /**
     * @brief Restore per-tab state when switching to a tab.
     * @param tabIndex The tab index to restore state for.
     */
    virtual void restoreTabState(int tabIndex) = 0;
    
    /**
     * @brief Save per-tab state before switching away from a tab.
     * @param tabIndex The tab index to save state for.
     */
    virtual void saveTabState(int tabIndex) = 0;
    
    /**
     * @brief Clear per-tab state when a tab is closed.
     * @param tabIndex The tab index to clear state for.
     * 
     * Default implementation does nothing. Subclasses with m_tabStates
     * should override to remove the entry for the closed tab.
     */
    virtual void clearTabState(int tabIndex) { Q_UNUSED(tabIndex); }
    
    /**
     * @brief Set dark mode and update all button icons accordingly.
     * @param darkMode True for dark mode, false for light mode.
     * 
     * Subclasses should override this to propagate dark mode to their buttons.
     */
    virtual void setDarkMode(bool darkMode);

protected:
    /**
     * @brief Add a horizontal separator line between button groups.
     */
    void addSeparator();
    
    /**
     * @brief Add a widget to the subtoolbar layout.
     * @param widget The widget to add.
     */
    void addWidget(QWidget* widget);
    
    /**
     * @brief Add a stretch to push remaining widgets up.
     */
    void addStretch();
    
    /**
     * @brief Apply shared styling (background, border, shadow).
     * 
     * Called automatically in constructor. Can be overridden if needed.
     */
    virtual void setupStyle();
    
    /**
     * @brief Check if the application is in dark mode.
     */
    bool isDarkMode() const;
    
    /**
     * @brief The main vertical layout for button arrangement.
     */
    QVBoxLayout* m_layout = nullptr;
    
    /// Fixed width for all subtoolbars
    static constexpr int SUBTOOLBAR_WIDTH = 44;
    
    /// Padding around buttons
    static constexpr int PADDING = 4;
    
    /// Border radius for rounded corners
    static constexpr int BORDER_RADIUS = 8;
};

#endif // SUBTOOLBAR_H

