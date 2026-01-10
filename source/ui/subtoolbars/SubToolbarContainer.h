#ifndef SUBTOOLBARCONTAINER_H
#define SUBTOOLBARCONTAINER_H

#include <QWidget>
#include <QHash>
#include "../../core/ToolType.h"

class SubToolbar;

/**
 * @brief Manages subtoolbar swapping and positioning.
 * 
 * The container holds references to all subtoolbars and shows/hides
 * them based on the current tool. It also handles positioning relative
 * to the DocumentViewport.
 * 
 * Positioning:
 * - 24px from left edge of viewport
 * - Vertically centered based on current subtoolbar's height
 * - Recalculates on viewport resize and subtoolbar swap
 * 
 * Usage:
 * 1. Create container as child of MainWindow (or viewport parent)
 * 2. Register subtoolbars with setSubToolbar()
 * 3. Connect toolbar's toolSelected signal to onToolChanged()
 * 4. Call updatePosition() on viewport resize
 */
class SubToolbarContainer : public QWidget {
    Q_OBJECT

public:
    explicit SubToolbarContainer(QWidget* parent = nullptr);
    
    /**
     * @brief Register a subtoolbar for a specific tool.
     * @param tool The tool type.
     * @param subtoolbar The subtoolbar to show for this tool (nullptr = no subtoolbar).
     * 
     * The container takes ownership of the subtoolbar.
     */
    void setSubToolbar(ToolType tool, SubToolbar* subtoolbar);
    
    /**
     * @brief Show the subtoolbar for a specific tool.
     * @param tool The tool type.
     * 
     * If no subtoolbar is registered for the tool, the container is hidden.
     */
    void showForTool(ToolType tool);
    
    /**
     * @brief Update position relative to viewport.
     * @param viewportRect The viewport's geometry in parent coordinates.
     * 
     * Call this when the viewport is resized or moved.
     */
    void updatePosition(const QRect& viewportRect);
    
    /**
     * @brief Handle tab change for per-tab state management.
     * @param newTabIndex The index of the new active tab.
     * @param oldTabIndex The index of the previous active tab (-1 if none).
     * 
     * This saves state for the old tab and restores state for the new tab
     * on all registered subtoolbars.
     */
    void onTabChanged(int newTabIndex, int oldTabIndex);
    
    /**
     * @brief Get the currently visible subtoolbar.
     * @return The current subtoolbar, or nullptr if none is visible.
     */
    SubToolbar* currentSubToolbar() const;
    
    /**
     * @brief Get the current tool type.
     */
    ToolType currentTool() const;
    
    /**
     * @brief Set dark mode for all registered subtoolbars.
     * @param darkMode True for dark mode, false for light mode.
     * 
     * Call this when the application theme changes.
     */
    void setDarkMode(bool darkMode);

public slots:
    /**
     * @brief Slot to handle tool changes from Toolbar.
     * @param tool The newly selected tool.
     */
    void onToolChanged(ToolType tool);

private:
    /**
     * @brief Update the container's size to fit the current subtoolbar.
     */
    void updateSize();
    
    /**
     * @brief Show/hide the container based on whether there's a subtoolbar.
     */
    void updateVisibility();

    QHash<ToolType, SubToolbar*> m_subtoolbars;
    SubToolbar* m_currentSubToolbar = nullptr;
    ToolType m_currentTool = ToolType::Pen;
    int m_currentTabIndex = 0;
    QRect m_viewportRect;
    
    /// Offset from left edge of viewport
    static constexpr int LEFT_OFFSET = 24;
};

#endif // SUBTOOLBARCONTAINER_H

