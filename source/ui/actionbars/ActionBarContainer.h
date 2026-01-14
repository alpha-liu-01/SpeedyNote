#ifndef ACTIONBARCONTAINER_H
#define ACTIONBARCONTAINER_H

#include <QWidget>
#include <QHash>
#include <QPropertyAnimation>
#include "../../core/ToolType.h"

class ActionBar;
class PagePanelActionBar;

/**
 * @brief Manages action bar swapping, positioning, and visibility.
 * 
 * The container holds references to all action bars and shows/hides
 * them based on the current tool and selection state. It also handles
 * positioning relative to the DocumentViewport.
 * 
 * Key differences from SubToolbarContainer:
 * - Positioned on the RIGHT side of viewport (symmetrical to subtoolbars)
 * - Visibility depends on selection state, not just tool
 * - Uses string keys for flexibility ("lasso", "objectSelect", etc.)
 * - Supports slide-in animation
 * 
 * Positioning:
 * - 24px from right edge of viewport
 * - Vertically centered based on current action bar's height
 * - Recalculates on viewport resize and action bar swap
 * 
 * Visibility logic:
 * - LassoActionBar: Lasso tool + lasso selection exists
 * - ObjectSelectActionBar: ObjectSelect tool + object(s) selected
 * - TextSelectionActionBar: Highlighter tool + PDF text selected
 * - ClipboardActionBar: ObjectSelect tool + clipboard has image + no selection
 * 
 * Usage:
 * 1. Create container as child of MainWindow (or viewport parent)
 * 2. Register action bars with setActionBar()
 * 3. Connect viewport signals to context update slots
 * 4. Call updatePosition() on viewport resize
 */
class ActionBarContainer : public QWidget {
    Q_OBJECT

public:
    explicit ActionBarContainer(QWidget* parent = nullptr);
    
    /**
     * @brief Register an action bar for a specific type.
     * @param type The action bar type key (e.g., "lasso", "objectSelect").
     * @param actionBar The action bar to register.
     * 
     * The container takes ownership of the action bar.
     */
    void setActionBar(const QString& type, ActionBar* actionBar);
    
    /**
     * @brief Show a specific action bar by type.
     * @param type The action bar type key.
     * 
     * If the type is not registered, the container is hidden.
     */
    void showActionBar(const QString& type);
    
    /**
     * @brief Hide the current action bar.
     */
    void hideActionBar();
    
    /**
     * @brief Update position relative to viewport.
     * @param viewportRect The viewport's geometry in parent coordinates.
     * 
     * Call this when the viewport is resized or moved.
     */
    void updatePosition(const QRect& viewportRect);
    
    /**
     * @brief Set dark mode for all registered action bars.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
    /**
     * @brief Enable or disable animation.
     * @param enabled True to enable slide animation, false for instant show/hide.
     */
    void setAnimationEnabled(bool enabled);
    
    /**
     * @brief Get the currently visible action bar.
     * @return The current action bar, or nullptr if none is visible.
     */
    ActionBar* currentActionBar() const;
    
    /**
     * @brief Get the current tool type.
     */
    ToolType currentTool() const;
    
    // =========================================================================
    // Page Panel Action Bar (2-Column Support)
    // =========================================================================
    
    /**
     * @brief Set the Page Panel action bar.
     * @param actionBar The PagePanelActionBar instance.
     * 
     * The container takes ownership of the action bar.
     * When both PagePanelActionBar and a context action bar are visible,
     * they are arranged in 2 columns with PagePanel on the left.
     */
    void setPagePanelActionBar(PagePanelActionBar* actionBar);
    
    /**
     * @brief Get the Page Panel action bar.
     */
    PagePanelActionBar* pagePanelActionBar() const;
    
    /**
     * @brief Show or hide the Page Panel action bar.
     * @param visible True to show, false to hide.
     * 
     * Call this when the Page Panel sidebar tab is shown/hidden.
     */
    void setPagePanelVisible(bool visible);

public slots:
    /**
     * @brief Handle tool changes from Toolbar.
     * @param tool The newly selected tool.
     */
    void onToolChanged(ToolType tool);
    
    /**
     * @brief Handle lasso selection changes.
     * @param hasSelection True if lasso selection exists.
     */
    void onLassoSelectionChanged(bool hasSelection);
    
    /**
     * @brief Handle object selection changes.
     * 
     * Call this when object selection changes in ObjectSelect tool.
     * The container will query the viewport for selection state.
     */
    void onObjectSelectionChanged(bool hasSelection);
    
    /**
     * @brief Handle text selection changes.
     * @param hasSelection True if PDF text is selected.
     */
    void onTextSelectionChanged(bool hasSelection);
    
    /**
     * @brief Handle clipboard content changes.
     * 
     * Connected to QClipboard::dataChanged for efficient detection.
     */
    void onClipboardChanged();
    
    /**
     * @brief Handle stroke clipboard changes.
     * @param hasStrokes True if internal stroke clipboard has content.
     */
    void onStrokeClipboardChanged(bool hasStrokes);
    
    /**
     * @brief Handle object clipboard changes.
     * @param hasObjects True if internal object clipboard has content.
     */
    void onObjectClipboardChanged(bool hasObjects);

signals:
    /**
     * @brief Emitted when the container needs a fresh viewport rect.
     * 
     * Connect this to MainWindow::updateActionBarPosition() to ensure
     * the container gets correct positioning when becoming visible.
     */
    void positionUpdateRequested();

private:
    /**
     * @brief Determine which action bar to show based on current state.
     * 
     * Logic:
     * - Lasso tool + lasso selection → LassoActionBar
     * - ObjectSelect tool + object selected → ObjectSelectActionBar
     * - Highlighter tool + text selected → TextSelectionActionBar
     * - ObjectSelect tool + clipboard has image + no selection → ClipboardActionBar
     * - Otherwise → hide
     */
    void updateVisibility();
    
    /**
     * @brief Update the container's size to fit the current action bar.
     */
    void updateSize();
    
    /**
     * @brief Animate showing the action bar (slide in from right).
     */
    void animateShow();
    
    /**
     * @brief Animate hiding the action bar.
     */
    void animateHide();
    
    /**
     * @brief Check if system clipboard has an image.
     */
    void checkClipboardForImage();

    QHash<QString, ActionBar*> m_actionBars;
    ActionBar* m_currentActionBar = nullptr;
    QString m_currentType;
    ToolType m_currentTool = ToolType::Pen;
    QRect m_viewportRect;
    
    // Page Panel action bar (2-column support)
    PagePanelActionBar* m_pagePanelBar = nullptr;
    bool m_pagePanelVisible = false;
    
    // Context state (cached)
    bool m_hasLassoSelection = false;
    bool m_hasObjectSelection = false;
    bool m_hasTextSelection = false;
    bool m_clipboardHasImage = false;
    bool m_hasStrokesInClipboard = false;
    bool m_hasObjectsInClipboard = false;
    
    // Animation
    QPropertyAnimation* m_animation = nullptr;
    bool m_animationEnabled = true;
    bool m_isAnimating = false;
    
    /// Offset from right edge of viewport
    static constexpr int RIGHT_OFFSET = 24;
    /// Gap between columns in 2-column layout
    static constexpr int COLUMN_GAP = 24;
    /// Animation duration in milliseconds
    static constexpr int ANIMATION_DURATION = 150;
};

#endif // ACTIONBARCONTAINER_H

