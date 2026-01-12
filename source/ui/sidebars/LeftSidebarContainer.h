#ifndef LEFTSIDEBARCONTAINER_H
#define LEFTSIDEBARCONTAINER_H

#include <QTabWidget>

class LayerPanel;
class OutlinePanel;
class PagePanel;

/**
 * @brief Tabbed container for left sidebar panels.
 * 
 * Uses QTabWidget to hold multiple panels:
 * - OutlinePanel (PDF table of contents - shown dynamically)
 * - LayerPanel (always present)
 * - BookmarksPanel (future)
 * - PagePanel (future)
 * 
 * NavigationBar's left sidebar toggle shows/hides this container.
 */
class LeftSidebarContainer : public QTabWidget
{
    Q_OBJECT

public:
    explicit LeftSidebarContainer(QWidget *parent = nullptr);
    
    /**
     * @brief Get the LayerPanel instance.
     * @return Pointer to LayerPanel (owned by this container).
     */
    LayerPanel* layerPanel() const { return m_layerPanel; }
    
    /**
     * @brief Get the OutlinePanel instance.
     * @return Pointer to OutlinePanel (owned by this container).
     */
    OutlinePanel* outlinePanel() const { return m_outlinePanel; }
    
    /**
     * @brief Get the PagePanel instance.
     * @return Pointer to PagePanel (owned by this container).
     */
    PagePanel* pagePanel() const { return m_pagePanel; }
    
    // =========================================================================
    // Dynamic Tab Management
    // =========================================================================
    
    /**
     * @brief Show or hide the Outline tab.
     * @param show True to show, false to hide.
     * 
     * The Outline tab is only shown when viewing a PDF with an outline.
     * When shown, it's inserted at position 0 (before other tabs).
     */
    void showOutlineTab(bool show);
    
    /**
     * @brief Check if the Outline tab is currently visible.
     * @return True if Outline tab is shown.
     */
    bool hasOutlineTab() const { return m_outlineTabIndex >= 0; }
    
    /**
     * @brief Show or hide the Pages tab.
     * @param show True to show, false to hide.
     * 
     * The Pages tab is shown for paged documents and hidden for edgeless documents.
     * When shown, it appears after Outline (if visible) and before Layers.
     */
    void showPagesTab(bool show);
    
    /**
     * @brief Check if the Pages tab is currently visible.
     * @return True if Pages tab is shown.
     */
    bool hasPagesTab() const { return m_pagesTabIndex >= 0; }
    
    /**
     * @brief Update theme colors.
     * @param darkMode True for dark theme
     */
    void updateTheme(bool darkMode);

private:
    void setupUi();
    void updateTabIndices();  // Recalculate indices after tab add/remove
    
    LayerPanel *m_layerPanel = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    PagePanel *m_pagePanel = nullptr;
    
    int m_outlineTabIndex = -1;  // -1 = tab not added
    int m_pagesTabIndex = -1;    // -1 = tab not added
    int m_layersTabIndex = 0;    // Layers is always the last tab
};

#endif // LEFTSIDEBARCONTAINER_H

