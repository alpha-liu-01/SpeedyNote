// ============================================================================
// TabManager Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.2)
// ============================================================================

#include "TabManager.h"
#include "../core/DocumentViewport.h"
#include "../core/Document.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

TabManager::TabManager(QTabWidget* tabWidget, QObject* parent)
    : QObject(parent)
    , m_tabWidget(tabWidget)
{
    if (m_tabWidget) {
        // Connect tab widget signals
        connect(m_tabWidget, &QTabWidget::currentChanged,
                this, &TabManager::onCurrentChanged);
        connect(m_tabWidget, &QTabWidget::tabCloseRequested,
                this, &TabManager::onTabCloseRequested);
    }
}

TabManager::~TabManager()
{
    // Delete all owned viewports
    // Note: QTabWidget will have already removed them as widgets,
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
    if (!m_tabWidget) {
        return -1;
    }
    
    // Create the viewport
    DocumentViewport* viewport = new DocumentViewport(m_tabWidget);
    viewport->setDocument(doc);
    
    // Add to tab widget
    int index = m_tabWidget->addTab(viewport, title);
    
    // Track in our vectors
    m_viewports.append(viewport);
    m_baseTitles.append(title);
    m_modifiedFlags.append(false);
    
    // Make the new tab active
    m_tabWidget->setCurrentIndex(index);
    
    return index;
}

void TabManager::closeTab(int index)
{
    if (!m_tabWidget || index < 0 || index >= m_viewports.size()) {
        return;
    }
    
    DocumentViewport* viewport = m_viewports.at(index);
    
    // Emit signal before closing (for unsaved changes check)
    emit tabCloseRequested(index, viewport);
    
    // Remove from tab widget (this removes the widget but doesn't delete it)
    m_tabWidget->removeTab(index);
    
    // Remove from our tracking
    m_viewports.removeAt(index);
    m_baseTitles.removeAt(index);
    m_modifiedFlags.removeAt(index);
    
    // Delete the viewport (we own it)
    delete viewport;
}

void TabManager::closeCurrentTab()
{
    if (m_tabWidget) {
        closeTab(m_tabWidget->currentIndex());
    }
}

// ============================================================================
// Access
// ============================================================================

DocumentViewport* TabManager::currentViewport() const
{
    if (!m_tabWidget || m_tabWidget->currentIndex() < 0) {
        return nullptr;
    }
    return viewportAt(m_tabWidget->currentIndex());
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
    return m_tabWidget ? m_tabWidget->currentIndex() : -1;
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
    if (!m_tabWidget || index < 0 || index >= m_baseTitles.size()) {
        return;
    }
    
    m_baseTitles[index] = title;
    
    // Update display title (with or without * prefix)
    if (m_modifiedFlags.at(index)) {
        m_tabWidget->setTabText(index, QStringLiteral("* ") + title);
    } else {
        m_tabWidget->setTabText(index, title);
    }
}

void TabManager::markTabModified(int index, bool modified)
{
    if (!m_tabWidget || index < 0 || index >= m_modifiedFlags.size()) {
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
        m_tabWidget->setTabText(index, QStringLiteral("* ") + baseTitle);
    } else {
        m_tabWidget->setTabText(index, baseTitle);
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
