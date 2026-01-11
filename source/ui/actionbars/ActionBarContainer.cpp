#include "ActionBarContainer.h"
#include "ActionBar.h"
#include "LassoActionBar.h"
#include "ObjectSelectActionBar.h"

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
    if (m_currentType == type && m_currentActionBar && isVisible()) {
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
        
        // Position action bar at (0,0) within container
        m_currentActionBar->move(0, 0);
        m_currentActionBar->show();
        
        updateSize();
        updatePosition(m_viewportRect);
        
        // Animate or show immediately
        animateShow();
    } else {
        hideActionBar();
    }
}

void ActionBarContainer::hideActionBar()
{
    if (!isVisible() && !m_isAnimating) {
        return;
    }
    
    if (m_animationEnabled) {
        animateHide();
    } else {
        if (m_currentActionBar) {
            m_currentActionBar->hide();
        }
        m_currentActionBar = nullptr;
        m_currentType.clear();
        hide();
    }
}

void ActionBarContainer::updatePosition(const QRect& viewportRect)
{
    m_viewportRect = viewportRect;
    
    if (!m_currentActionBar || (!isVisible() && !m_isAnimating)) {
        return;
    }
    
    // Calculate X position: RIGHT_OFFSET from viewport right edge
    int actionBarWidth = m_currentActionBar->sizeHint().width();
    int x = viewportRect.right() - RIGHT_OFFSET - actionBarWidth;
    
    // Calculate Y position: vertically centered in viewport
    int actionBarHeight = m_currentActionBar->sizeHint().height();
    int y = viewportRect.top() + (viewportRect.height() - actionBarHeight) / 2;
    
    // Ensure we don't go above the viewport top
    y = qMax(y, viewportRect.top() + RIGHT_OFFSET);
    
    // Move the container (unless animating - animation controls position)
    if (!m_isAnimating) {
        move(x, y);
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
    if (m_currentActionBar) {
        // Size container to fit the action bar
        QSize actionBarSize = m_currentActionBar->sizeHint();
        setFixedSize(actionBarSize);
    } else {
        setFixedSize(0, 0);
    }
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

