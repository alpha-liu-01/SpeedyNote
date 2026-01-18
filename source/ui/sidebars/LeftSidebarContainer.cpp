#include "LeftSidebarContainer.h"
#include "LayerPanel.h"
#include "OutlinePanel.h"
#include "PagePanel.h"

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
    
    // Create panels (Outline and Pages created but not added to tabs yet)
    m_outlinePanel = new OutlinePanel(this);
    m_outlinePanel->hide();  // Hide until added to tab (prevents gray block)
    
    m_pagePanel = new PagePanel(this);
    m_pagePanel->hide();  // Hide until added to tab (prevents gray block)
    
    m_layerPanel = new LayerPanel(this);
    
    // Only add Layers tab initially
    // Outline and Pages tabs are added dynamically based on document type
    m_layersTabIndex = addTab(m_layerPanel, tr("Layers"));
}

void LeftSidebarContainer::showOutlineTab(bool show)
{
    if (show && m_outlineTabIndex == -1) {
        // Insert Outline tab at position 0 (always first)
        m_outlinePanel->show();  // Ensure visible when added to tab
        m_outlineTabIndex = insertTab(0, m_outlinePanel, tr("Outline"));
        updateTabIndices();
        setCurrentIndex(0);  // Switch to Outline tab
    } else if (!show && m_outlineTabIndex != -1) {
        // Remove Outline tab
        removeTab(m_outlineTabIndex);
        m_outlinePanel->hide();  // Hide when removed from tab
        m_outlineTabIndex = -1;
        updateTabIndices();
    }
}

void LeftSidebarContainer::showPagesTab(bool show)
{
    if (show && m_pagesTabIndex == -1) {
        // Insert Pages tab after Outline (if present) but before Layers
        int insertPos = (m_outlineTabIndex >= 0) ? 1 : 0;
        m_pagePanel->show();  // Ensure visible when added to tab
        m_pagesTabIndex = insertTab(insertPos, m_pagePanel, tr("Pages"));
        updateTabIndices();
        // Don't auto-select Pages tab - user should manually select it
        // This prevents the PagePanelActionBar from showing on startup
    } else if (!show && m_pagesTabIndex != -1) {
        // Remove Pages tab
        removeTab(m_pagesTabIndex);
        m_pagePanel->hide();  // Hide when removed from tab
        m_pagesTabIndex = -1;
        updateTabIndices();
    }
}

void LeftSidebarContainer::updateTabIndices()
{
    // Recalculate indices based on which tabs are present
    // Tab order: Outline (optional) → Pages (optional) → Layers (always last)
    
    int currentIndex = 0;
    
    if (m_outlineTabIndex >= 0) {
        m_outlineTabIndex = currentIndex++;
    }
    
    if (m_pagesTabIndex >= 0) {
        m_pagesTabIndex = currentIndex++;
    }
    
    m_layersTabIndex = currentIndex;  // Layers is always last
}

void LeftSidebarContainer::updateTheme(bool darkMode)
{
    // Update OutlinePanel theme
    if (m_outlinePanel) {
        m_outlinePanel->updateTheme(darkMode);
    }
    
    // Update PagePanel theme
    if (m_pagePanel) {
        m_pagePanel->setDarkMode(darkMode);
    }
    
    // LayerPanel handles its own theming
}

