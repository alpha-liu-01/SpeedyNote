#include "ActionBarContainer.h"
#include "ActionBar.h"
#include "LassoActionBar.h"
#include "ObjectSelectActionBar.h"
#include "PagePanelActionBar.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QEasingCurve>

ActionBarContainer::ActionBarContainer(QWidget* parent)
    : QWidget(parent)
{
    // Container itself is transparent - action bars provide their own styling
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    // Start hidden until an action bar is shown
    hide();
    
    // Check initial clipboard state
    checkClipboardForImage();
}

void ActionBarContainer::setActionBar(const QString& type, ActionBar* actionBar)
{
    // Remove old action bar if exists
    if (m_actionBars.contains(type)) {
        ActionBar* old = m_actionBars.take(type);
        if (old) {
            old->setParent(nullptr);
            old->deleteLater();
        }
    }
    
    // Register new action bar
    if (actionBar) {
        actionBar->setParent(this);
        actionBar->hide();  // Hidden until context matches
        m_actionBars.insert(type, actionBar);
    }
    
    // Re-evaluate visibility in case this affects current state
    updateVisibility();
}

void ActionBarContainer::showActionBar(const QString& type)
{
    // If already showing this type, just update button states (context may have changed)
    if (m_currentType == type && m_currentActionBar && m_currentActionBar->isVisible()) {
        m_currentActionBar->updateButtonStates();
        updateSize();
        updatePosition(m_viewportRect);
        return;
    }
    
    // Hide current action bar (without animation if switching)
    if (m_currentActionBar) {
        m_currentActionBar->hide();
    }
    
    m_currentType = type;
    
    // Find and show new action bar
    m_currentActionBar = m_actionBars.value(type, nullptr);
    
    if (m_currentActionBar) {
        // Update button states for current context
        m_currentActionBar->updateButtonStates();
        m_currentActionBar->show();
        
        updateSize();
        updatePosition(m_viewportRect);
        
        // Show container (might already be visible if PagePanel is shown)
        if (!isVisible()) {
            if (m_animationEnabled) {
        animateShow();
            } else {
                show();
                raise();
            }
        } else {
            // Already visible (PagePanel is showing), just raise
            raise();
        }
    } else {
        hideActionBar();
    }
}

void ActionBarContainer::hideActionBar()
{
    if (!m_currentActionBar) {
        return;
    }
    
    // Hide the current context action bar
    m_currentActionBar->hide();
    m_currentActionBar = nullptr;
    m_currentType.clear();
    
    // Update layout
    updateSize();
    updatePosition(m_viewportRect);
    
    // Hide container only if PagePanel is also not visible
    if (!m_pagePanelVisible) {
        if (m_animationEnabled && isVisible()) {
        animateHide();
    } else {
            hide();
        }
    }
}

void ActionBarContainer::updatePosition(const QRect& viewportRect)
{
    m_viewportRect = viewportRect;
    
    // Count visible bars
    bool pagePanelShown = m_pagePanelBar && m_pagePanelVisible;
    bool contextBarShown = m_currentActionBar && m_currentActionBar->isVisible();
    
    if (!pagePanelShown && !contextBarShown) {
        return;
    }
    
    // Don't update position while animating
    if (m_isAnimating) {
        return;
    }
    
    // Calculate heights for vertical centering
    int pagePanelHeight = pagePanelShown ? m_pagePanelBar->sizeHint().height() : 0;
    int contextBarHeight = contextBarShown ? m_currentActionBar->sizeHint().height() : 0;
    int maxBarHeight = qMax(pagePanelHeight, contextBarHeight);
    
    // Calculate widths
    int pagePanelWidth = pagePanelShown ? m_pagePanelBar->sizeHint().width() : 0;
    int contextBarWidth = contextBarShown ? m_currentActionBar->sizeHint().width() : 0;
    
    // Calculate total width
    int totalWidth;
    if (pagePanelShown && contextBarShown) {
        // 2-column layout: pagePanelWidth + gap + contextBarWidth
        totalWidth = pagePanelWidth + COLUMN_GAP + contextBarWidth;
    } else {
        totalWidth = qMax(pagePanelWidth, contextBarWidth);
    }
    
    // Calculate container position (X: right-aligned, Y: vertically centered)
    int containerX = viewportRect.right() - RIGHT_OFFSET - totalWidth;
    int containerY = viewportRect.top() + (viewportRect.height() - maxBarHeight) / 2;
    containerY = qMax(containerY, viewportRect.top() + RIGHT_OFFSET);
    
    // Move container
    move(containerX, containerY);
    
    // Position child action bars within container
    if (pagePanelShown && contextBarShown) {
        // 2-column layout: PagePanel on LEFT, context bar on RIGHT
        int pagePanelY = (maxBarHeight - pagePanelHeight) / 2;
        int contextBarY = (maxBarHeight - contextBarHeight) / 2;
        
        m_pagePanelBar->move(0, pagePanelY);
        m_currentActionBar->move(pagePanelWidth + COLUMN_GAP, contextBarY);
    } else if (pagePanelShown) {
        // Only PagePanel visible
        m_pagePanelBar->move(0, 0);
    } else if (contextBarShown) {
        // Only context bar visible
        m_currentActionBar->move(0, 0);
    }
}

void ActionBarContainer::setDarkMode(bool darkMode)
{
    // Propagate dark mode to all registered action bars
    for (ActionBar* actionBar : m_actionBars) {
        if (actionBar) {
            actionBar->setDarkMode(darkMode);
        }
    }
    
    // Propagate to PagePanel action bar
    if (m_pagePanelBar) {
        m_pagePanelBar->setDarkMode(darkMode);
    }
}

void ActionBarContainer::setAnimationEnabled(bool enabled)
{
    m_animationEnabled = enabled;
}

ActionBar* ActionBarContainer::currentActionBar() const
{
    return m_currentActionBar;
}

ToolType ActionBarContainer::currentTool() const
{
    return m_currentTool;
}

// ============================================================================
// Page Panel Action Bar (2-Column Support)
// ============================================================================

void ActionBarContainer::setPagePanelActionBar(PagePanelActionBar* actionBar)
{
    // Clean up old action bar
    if (m_pagePanelBar) {
        m_pagePanelBar->setParent(nullptr);
        m_pagePanelBar->deleteLater();
    }
    
    m_pagePanelBar = actionBar;
    
    if (m_pagePanelBar) {
        m_pagePanelBar->setParent(this);
        m_pagePanelBar->setVisible(m_pagePanelVisible);
    }
    
    // Update layout
    updateSize();
    updatePosition(m_viewportRect);
}

PagePanelActionBar* ActionBarContainer::pagePanelActionBar() const
{
    return m_pagePanelBar;
}

void ActionBarContainer::setPagePanelVisible(bool visible)
{
    if (m_pagePanelVisible == visible) {
        return;
    }
    
    m_pagePanelVisible = visible;
    
    if (m_pagePanelBar) {
        m_pagePanelBar->setVisible(visible);
    }
    
    // Update layout for potential 2-column arrangement
    updateSize();
    updatePosition(m_viewportRect);
    
    // Show container if at least one bar is visible
    if (m_pagePanelVisible || (m_currentActionBar && m_currentActionBar->isVisible())) {
        show();
        raise();
    } else if (!m_pagePanelVisible && !m_currentActionBar) {
        hide();
    }
}

void ActionBarContainer::onToolChanged(ToolType tool)
{
    m_currentTool = tool;
    updateVisibility();
}

void ActionBarContainer::onLassoSelectionChanged(bool hasSelection)
{
    m_hasLassoSelection = hasSelection;
    
    // Propagate selection state to LassoActionBar so Cut/Copy/Delete visibility updates
    ActionBar* lassoBar = m_actionBars.value("lasso", nullptr);
    if (lassoBar) {
        LassoActionBar* lasso = qobject_cast<LassoActionBar*>(lassoBar);
        if (lasso) {
            lasso->setHasSelection(hasSelection);
        }
    }
    
    updateVisibility();
}

void ActionBarContainer::onObjectSelectionChanged(bool hasSelection)
{
    m_hasObjectSelection = hasSelection;
    
    // Propagate selection state to ObjectSelectActionBar so buttons visibility updates
    ActionBar* objectBar = m_actionBars.value("objectSelect", nullptr);
    if (objectBar) {
        ObjectSelectActionBar* objSelect = qobject_cast<ObjectSelectActionBar*>(objectBar);
        if (objSelect) {
            objSelect->setHasSelection(hasSelection);
        }
    }
    
    updateVisibility();
}

void ActionBarContainer::onTextSelectionChanged(bool hasSelection)
{
    m_hasTextSelection = hasSelection;
    updateVisibility();
}

void ActionBarContainer::onClipboardChanged()
{
    checkClipboardForImage();
    updateVisibility();
}

void ActionBarContainer::onStrokeClipboardChanged(bool hasStrokes)
{
    m_hasStrokesInClipboard = hasStrokes;
    
    // Propagate state to LassoActionBar so Paste button visibility updates correctly
    ActionBar* lassoBar = m_actionBars.value("lasso", nullptr);
    if (lassoBar) {
        LassoActionBar* lasso = qobject_cast<LassoActionBar*>(lassoBar);
        if (lasso) {
            lasso->setHasStrokesInClipboard(hasStrokes);
        }
    }
    
    // Re-evaluate visibility (Lasso tool may now need to show paste-only bar)
    updateVisibility();
}

void ActionBarContainer::onObjectClipboardChanged(bool hasObjects)
{
    m_hasObjectsInClipboard = hasObjects;
    
    // Propagate state to ObjectSelectActionBar so Paste button visibility updates correctly
    ActionBar* objectBar = m_actionBars.value("objectSelect", nullptr);
    if (objectBar) {
        ObjectSelectActionBar* objSelect = qobject_cast<ObjectSelectActionBar*>(objectBar);
        if (objSelect) {
            objSelect->setHasObjectInClipboard(hasObjects);
        }
    }
    
    // Re-evaluate visibility (ObjectSelect tool may now need to show paste-only bar)
    updateVisibility();
}

void ActionBarContainer::updateVisibility()
{
    QString typeToShow;
    
    // Determine which action bar to show based on current state
    switch (m_currentTool) {
        case ToolType::Lasso:
            // Show LassoActionBar if:
            // - Has lasso selection (full bar: Cut, Copy, Paste?, Delete)
            // - OR no selection but clipboard has strokes (paste-only bar)
            if (m_hasLassoSelection || m_hasStrokesInClipboard) {
                typeToShow = "lasso";
            }
            break;
            
        case ToolType::ObjectSelect:
            // Show ObjectSelectActionBar if:
            // - Has object selection (full bar)
            // - OR no selection but internal object clipboard has content (paste-only bar)
            if (m_hasObjectSelection || m_hasObjectsInClipboard) {
                typeToShow = "objectSelect";
            } else if (m_clipboardHasImage) {
                // No selection and no internal clipboard, but system clipboard has image
                typeToShow = "clipboard";
            }
            break;
            
        case ToolType::Highlighter:
            if (m_hasTextSelection) {
                typeToShow = "textSelection";
            }
            break;
            
        default:
            // No action bar for other tools
            break;
    }
    
    // Show appropriate action bar or hide
    if (!typeToShow.isEmpty() && m_actionBars.contains(typeToShow)) {
        showActionBar(typeToShow);
    } else {
        hideActionBar();
    }
}

void ActionBarContainer::updateSize()
{
    // Count visible bars
    bool pagePanelShown = m_pagePanelBar && m_pagePanelVisible;
    bool contextBarShown = m_currentActionBar && m_currentActionBar->isVisible();
    
    if (!pagePanelShown && !contextBarShown) {
        setFixedSize(0, 0);
        return;
    }
    
    // Calculate dimensions
    int pagePanelWidth = pagePanelShown ? m_pagePanelBar->sizeHint().width() : 0;
    int pagePanelHeight = pagePanelShown ? m_pagePanelBar->sizeHint().height() : 0;
    int contextBarWidth = contextBarShown ? m_currentActionBar->sizeHint().width() : 0;
    int contextBarHeight = contextBarShown ? m_currentActionBar->sizeHint().height() : 0;
    
    int totalWidth;
    int totalHeight;
    
    if (pagePanelShown && contextBarShown) {
        // 2-column layout
        totalWidth = pagePanelWidth + COLUMN_GAP + contextBarWidth;
        totalHeight = qMax(pagePanelHeight, contextBarHeight);
    } else if (pagePanelShown) {
        totalWidth = pagePanelWidth;
        totalHeight = pagePanelHeight;
    } else {
        totalWidth = contextBarWidth;
        totalHeight = contextBarHeight;
    }
    
    setFixedSize(totalWidth, totalHeight);
}

void ActionBarContainer::animateShow()
{
    if (!m_animationEnabled || !m_currentActionBar) {
        show();
        raise();
        return;
    }
    
    // Stop any existing animation
    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }
    
    // Calculate positions
    int actionBarWidth = m_currentActionBar->sizeHint().width();
    int actionBarHeight = m_currentActionBar->sizeHint().height();
    
    // Final position (24px from right edge, vertically centered)
    int finalX = m_viewportRect.right() - RIGHT_OFFSET - actionBarWidth;
    int finalY = m_viewportRect.top() + (m_viewportRect.height() - actionBarHeight) / 2;
    finalY = qMax(finalY, m_viewportRect.top() + RIGHT_OFFSET);
    
    // Start position: 50px to the right of final position
    int startX = finalX + 50;
    int startY = finalY;
    
    m_isAnimating = true;
    
    // Create animation
    m_animation = new QPropertyAnimation(this, "pos", this);
    m_animation->setDuration(ANIMATION_DURATION);
    m_animation->setStartValue(QPoint(startX, startY));
    m_animation->setEndValue(QPoint(finalX, finalY));
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    
    // Show and start animation
    move(startX, startY);
    show();
    raise();
    
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        m_isAnimating = false;
        m_animation->deleteLater();
        m_animation = nullptr;
    });
    
    m_animation->start();
}

void ActionBarContainer::animateHide()
{
    if (!m_animationEnabled || !isVisible()) {
        if (m_currentActionBar) {
            m_currentActionBar->hide();
        }
        m_currentActionBar = nullptr;
        m_currentType.clear();
        hide();
        return;
    }
    
    // Stop any existing animation
    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }
    
    // Current position
    QPoint startPos = pos();
    
    // End position: 50px to the right
    QPoint endPos = startPos + QPoint(50, 0);
    
    m_isAnimating = true;
    
    // Create animation
    m_animation = new QPropertyAnimation(this, "pos", this);
    m_animation->setDuration(ANIMATION_DURATION);
    m_animation->setStartValue(startPos);
    m_animation->setEndValue(endPos);
    m_animation->setEasingCurve(QEasingCurve::InCubic);
    
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        m_isAnimating = false;
        if (m_currentActionBar) {
            m_currentActionBar->hide();
        }
        m_currentActionBar = nullptr;
        m_currentType.clear();
        hide();
        m_animation->deleteLater();
        m_animation = nullptr;
    });
    
    m_animation->start();
}

void ActionBarContainer::checkClipboardForImage()
{
    QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    
    m_clipboardHasImage = mimeData && mimeData->hasImage();
}

