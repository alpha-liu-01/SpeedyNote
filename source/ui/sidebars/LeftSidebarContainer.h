#ifndef LEFTSIDEBARCONTAINER_H
#define LEFTSIDEBARCONTAINER_H

#include <QTabWidget>

class LayerPanel;

/**
 * @brief Tabbed container for left sidebar panels.
 * 
 * Uses QTabWidget to hold multiple panels:
 * - LayerPanel (connected)
 * - OutlinePanel (future)
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
     * @brief Update theme colors.
     * @param darkMode True for dark theme
     */
    void updateTheme(bool darkMode);

private:
    void setupUi();
    
    LayerPanel *m_layerPanel = nullptr;
    // Future: OutlinePanel, BookmarksPanel, PagePanel
};

#endif // LEFTSIDEBARCONTAINER_H

