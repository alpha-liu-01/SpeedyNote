#include "LeftSidebarContainer.h"
#include "LayerPanel.h"
#include "OutlinePanel.h"

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
    
    // Create panels (OutlinePanel created but not added to tabs yet)
    m_outlinePanel = new OutlinePanel(this);
    m_outlinePanel->hide();  // Hide until added to tab (prevents gray block)
    m_layerPanel = new LayerPanel(this);
    
    // Only add Layers tab initially
    // Outline tab is added dynamically when PDF with outline is loaded
    addTab(m_layerPanel, tr("Layers"));
    
    // Future: Add other panels here
    // addTab(m_bookmarksPanel, tr("Bookmarks"));
    // addTab(m_pagePanel, tr("Pages"));
}

void LeftSidebarContainer::showOutlineTab(bool show)
{
    if (show && m_outlineTabIndex == -1) {
        // Insert Outline tab at position 0 (before Layers)
        m_outlinePanel->show();  // Ensure visible when added to tab
        m_outlineTabIndex = insertTab(0, m_outlinePanel, tr("Outline"));
        setCurrentIndex(0);  // Switch to Outline tab
    } else if (!show && m_outlineTabIndex != -1) {
        // Remove Outline tab
        removeTab(m_outlineTabIndex);
        m_outlinePanel->hide();  // Hide when removed from tab
        m_outlineTabIndex = -1;
    }
}

void LeftSidebarContainer::updateTheme(bool darkMode)
{
    // Update OutlinePanel theme
    if (m_outlinePanel) {
        m_outlinePanel->updateTheme(darkMode);
    }
    
    // LayerPanel handles its own theming
    // Future: Apply QSS styling to container if needed
}

