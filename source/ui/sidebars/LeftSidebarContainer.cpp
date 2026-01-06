#include "LeftSidebarContainer.h"
#include "LayerPanel.h"

LeftSidebarContainer::LeftSidebarContainer(QWidget *parent)
    : QTabWidget(parent)
{
    setupUi();
}

void LeftSidebarContainer::setupUi()
{
    // Configure tab widget
    setTabPosition(QTabWidget::West);  // Tabs on left side
    setDocumentMode(true);
    
    // Create LayerPanel
    m_layerPanel = new LayerPanel(this);
    addTab(m_layerPanel, tr("Layers"));
    
    // Future: Add other panels here
    // addTab(m_outlinePanel, tr("Outline"));
    // addTab(m_bookmarksPanel, tr("Bookmarks"));
    // addTab(m_pagePanel, tr("Pages"));
}

void LeftSidebarContainer::updateTheme(bool darkMode)
{
    // Apply theme styling to container
    // LayerPanel handles its own theming
    Q_UNUSED(darkMode);
    
    // Future: Apply QSS styling if needed
}

