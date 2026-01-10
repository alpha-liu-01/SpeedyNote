#include "SubToolbarContainer.h"
#include "SubToolbar.h"

SubToolbarContainer::SubToolbarContainer(QWidget* parent)
    : QWidget(parent)
{
    // Container itself is transparent - subtoolbars provide their own styling
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    // Start hidden until a subtoolbar is shown
    hide();
}

void SubToolbarContainer::setSubToolbar(ToolType tool, SubToolbar* subtoolbar)
{
    // Remove old subtoolbar if exists
    if (m_subtoolbars.contains(tool)) {
        SubToolbar* old = m_subtoolbars.take(tool);
        if (old) {
            old->setParent(nullptr);
            old->deleteLater();
        }
    }
    
    // Register new subtoolbar
    if (subtoolbar) {
        subtoolbar->setParent(this);
        subtoolbar->hide();  // Hidden until this tool is selected
        m_subtoolbars.insert(tool, subtoolbar);
    }
    
    // If this is the current tool, update display
    if (tool == m_currentTool) {
        showForTool(tool);
    }
}

void SubToolbarContainer::showForTool(ToolType tool)
{
    // Hide current subtoolbar
    if (m_currentSubToolbar) {
        m_currentSubToolbar->hide();
    }
    
    m_currentTool = tool;
    
    // Find and show new subtoolbar
    m_currentSubToolbar = m_subtoolbars.value(tool, nullptr);
    
    if (m_currentSubToolbar) {
        // Refresh from settings when becoming visible
        m_currentSubToolbar->refreshFromSettings();
        
        // Position subtoolbar at (0,0) within container
        m_currentSubToolbar->move(0, 0);
        m_currentSubToolbar->show();
    }
    
    updateSize();
    updateVisibility();
    updatePosition(m_viewportRect);
}

void SubToolbarContainer::updatePosition(const QRect& viewportRect)
{
    m_viewportRect = viewportRect;
    
    if (!m_currentSubToolbar || !isVisible()) {
        return;
    }
    
    // Calculate X position: LEFT_OFFSET from viewport left edge
    int x = viewportRect.left() + LEFT_OFFSET;
    
    // Calculate Y position: vertically centered in viewport
    int subtoolbarHeight = m_currentSubToolbar->sizeHint().height();
    int y = viewportRect.top() + (viewportRect.height() - subtoolbarHeight) / 2;
    
    // Ensure we don't go above the viewport top
    y = qMax(y, viewportRect.top() + LEFT_OFFSET);
    
    // Move the container
    move(x, y);
}

void SubToolbarContainer::onTabChanged(int newTabIndex, int oldTabIndex)
{
    // Save state for old tab on all subtoolbars
    if (oldTabIndex >= 0) {
        for (SubToolbar* subtoolbar : m_subtoolbars) {
            if (subtoolbar) {
                subtoolbar->saveTabState(oldTabIndex);
            }
        }
    }
    
    m_currentTabIndex = newTabIndex;
    
    // Restore state for new tab on all subtoolbars
    for (SubToolbar* subtoolbar : m_subtoolbars) {
        if (subtoolbar) {
            subtoolbar->restoreTabState(newTabIndex);
        }
    }
    
    // Update current subtoolbar display
    if (m_currentSubToolbar) {
        m_currentSubToolbar->update();
    }
}

SubToolbar* SubToolbarContainer::currentSubToolbar() const
{
    return m_currentSubToolbar;
}

ToolType SubToolbarContainer::currentTool() const
{
    return m_currentTool;
}

void SubToolbarContainer::onToolChanged(ToolType tool)
{
    showForTool(tool);
}

void SubToolbarContainer::updateSize()
{
    if (m_currentSubToolbar) {
        // Size container to fit the subtoolbar
        QSize subtoolbarSize = m_currentSubToolbar->sizeHint();
        setFixedSize(subtoolbarSize);
    } else {
        setFixedSize(0, 0);
    }
}

void SubToolbarContainer::updateVisibility()
{
    if (m_currentSubToolbar) {
        show();
        raise();  // Ensure container is on top
    } else {
        hide();
    }
}

void SubToolbarContainer::setDarkMode(bool darkMode)
{
    // Propagate dark mode to all registered subtoolbars
    for (SubToolbar* subtoolbar : m_subtoolbars) {
        if (subtoolbar) {
            subtoolbar->setDarkMode(darkMode);
        }
    }
}

