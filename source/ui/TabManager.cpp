// ============================================================================
// TabManager Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase C - Toolbar Extraction)
// ============================================================================

#include "TabManager.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

TabManager::TabManager(QTabBar* tabBar, QStackedWidget* viewportStack, QObject* parent)
    : QObject(parent)
    , m_tabBar(tabBar)
    , m_viewportStack(viewportStack)
{
    if (m_tabBar) {
        // Connect tab bar signals
        connect(m_tabBar, &QTabBar::currentChanged,
                this, &TabManager::onCurrentChanged);
        connect(m_tabBar, &QTabBar::tabCloseRequested,
                this, &TabManager::onTabCloseRequested);
    }
    
    // Sync tab bar selection with viewport stack
    if (m_tabBar && m_viewportStack) {
        connect(m_tabBar, &QTabBar::currentChanged,
                m_viewportStack, &QStackedWidget::setCurrentIndex);
    }
}

TabManager::~TabManager()
{
    // Delete all owned viewports
    // Note: QStackedWidget will have already removed them as widgets,
    // but we still own the objects
    for (DocumentViewport* viewport : m_viewports) {
        delete viewport;
    }
    m_viewports.clear();
}

// ============================================================================
// Tab Operations
// ============================================================================

int TabManager::createTab(Document* doc, const QString& title)
{
    if (!m_tabBar || !m_viewportStack) {
        return -1;
    }
    
    // Create the viewport
    DocumentViewport* viewport = new DocumentViewport(m_viewportStack);
    viewport->setDocument(doc);
    
    // BUG FIX: Block signals during tab creation to prevent currentChanged
    // from being emitted before m_viewports is updated. When addTab() is called
    // for the first tab, Qt automatically selects it and emits currentChanged,
    // but at that point m_viewports would still be empty.
    m_tabBar->blockSignals(true);
    
    // Add to tab bar
    int index = m_tabBar->addTab(title);
    
    // Add to viewport stack
    m_viewportStack->addWidget(viewport);
    
    // Track in our vectors
    m_viewports.append(viewport);
    m_baseTitles.append(title);
    m_modifiedFlags.append(false);
    
    // Make the new tab active (still blocked, so no signal yet)
    m_tabBar->setCurrentIndex(index);
    
    // Re-enable signals
    m_tabBar->blockSignals(false);
    
    // Manually sync viewport stack (since we blocked the signal that normally does this)
    m_viewportStack->setCurrentIndex(index);
    
    // Manually emit currentViewportChanged now that everything is set up
    emit currentViewportChanged(viewport);
    
    return index;
}

void TabManager::closeTab(int index)
{
    if (!m_tabBar || !m_viewportStack || index < 0 || index >= m_viewports.size()) {
        return;
    }
    
    DocumentViewport* viewport = m_viewports.at(index);
    
    // Emit signal before closing (for unsaved changes check)
    emit tabCloseRequested(index, viewport);
    
    // Block signals during removal to prevent currentChanged from firing
    // with stale indices (Qt emits currentChanged when a tab is removed)
    m_tabBar->blockSignals(true);
    m_tabBar->removeTab(index);
    m_tabBar->blockSignals(false);
    
    // Remove from viewport stack
    m_viewportStack->removeWidget(viewport);
    
    // Remove from our tracking
    m_viewports.removeAt(index);
    m_baseTitles.removeAt(index);
    m_modifiedFlags.removeAt(index);
    
    // Delete the viewport (we own it)
    delete viewport;
    
    // Sync stacked widget with tab bar (since we blocked signals)
    if (m_viewportStack && m_tabBar) {
        int newIndex = m_tabBar->currentIndex();
        if (newIndex >= 0 && newIndex < m_viewportStack->count()) {
            m_viewportStack->setCurrentIndex(newIndex);
        }
    }
    
    // Now manually emit currentViewportChanged with the correct viewport
    // (after vectors are updated and viewport is deleted)
    emit currentViewportChanged(currentViewport());
}

void TabManager::closeCurrentTab()
{
    if (m_tabBar) {
        closeTab(m_tabBar->currentIndex());
    }
}

// ============================================================================
// Access
// ============================================================================

DocumentViewport* TabManager::currentViewport() const
{
    if (!m_viewportStack || m_viewportStack->currentIndex() < 0) {
        return nullptr;
    }
    return viewportAt(m_viewportStack->currentIndex());
}

DocumentViewport* TabManager::viewportAt(int index) const
{
    if (index < 0 || index >= m_viewports.size()) {
        return nullptr;
    }
    return m_viewports.at(index);
}

Document* TabManager::documentAt(int index) const
{
    DocumentViewport* viewport = viewportAt(index);
    if (!viewport) {
        return nullptr;
    }
    return viewport->document();
}

int TabManager::currentIndex() const
{
    return m_tabBar ? m_tabBar->currentIndex() : -1;
}

int TabManager::tabCount() const
{
    return m_viewports.size();
}

// ============================================================================
// Title Management
// ============================================================================

void TabManager::setTabTitle(int index, const QString& title)
{
    if (!m_tabBar || index < 0 || index >= m_baseTitles.size()) {
        return;
    }
    
    m_baseTitles[index] = title;
    
    // Update display title (with or without * prefix)
    if (m_modifiedFlags.at(index)) {
        m_tabBar->setTabText(index, QStringLiteral("* ") + title);
    } else {
        m_tabBar->setTabText(index, title);
    }
}

void TabManager::markTabModified(int index, bool modified)
{
    if (!m_tabBar || index < 0 || index >= m_modifiedFlags.size()) {
        return;
    }
    
    // Skip if no change
    if (m_modifiedFlags.at(index) == modified) {
        return;
    }
    
    m_modifiedFlags[index] = modified;
    
    // Update display title
    const QString& baseTitle = m_baseTitles.at(index);
    if (modified) {
        m_tabBar->setTabText(index, QStringLiteral("* ") + baseTitle);
    } else {
        m_tabBar->setTabText(index, baseTitle);
    }
}

QString TabManager::tabTitle(int index) const
{
    if (index < 0 || index >= m_baseTitles.size()) {
        return QString();
    }
    return m_baseTitles.at(index);
}

// ============================================================================
// Private Slots
// ============================================================================

void TabManager::onCurrentChanged(int index)
{
    DocumentViewport* viewport = (index >= 0 && index < m_viewports.size()) 
                                  ? m_viewports.at(index) 
                                  : nullptr;
    emit currentViewportChanged(viewport);
}

void TabManager::onTabCloseRequested(int index)
{
    // The user clicked the close button on a tab
    // Emit signal so MainWindow can check for unsaved changes and prompt user.
    // MainWindow is responsible for calling closeTab() if appropriate.
    // The tab is NOT automatically closed here.
    if (index >= 0 && index < m_viewports.size()) {
        emit tabCloseAttempted(index, m_viewports.at(index));
    }
}
