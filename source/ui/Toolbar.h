#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QWidget>
#include <QButtonGroup>
#include <QColor>
#include <QPoint>
#include <QVector>
#include "ToolbarButtons.h"
#include "../core/ToolType.h"
#include "../core/DocumentViewport.h"

class QPaintEvent;
class ExpandableToolButton;
class PenSubToolbar;
class MarkerSubToolbar;
class EraserSubToolbar;
class HighlighterSubToolbar;
class OcrSubToolbar;

/**
 * Toolbar - Tab-specific tool selection and actions with inline subtoolbars.
 *
 * Layout (center-aligned):
 * [Pen(+presets)][Marker(+presets)][Eraser(+presets)][Lasso][Image][Link][ObjectText][Highlighter(+presets)][OCR(+controls)]  gap  [Undo][Redo] [Touch]
 *
 * When a tool is selected its ExpandableToolButton expands to reveal
 * inline preset buttons (colors, thicknesses, toggles).
 * The OCR button is independent and coexists with the active tool's subtoolbar.
 */
class Toolbar : public QWidget {
    Q_OBJECT

    // Allow test class to access private members
    friend class ToolbarButtonTests;

public:
    explicit Toolbar(QWidget *parent = nullptr);

    void setCurrentTool(ToolType tool);
    void setObjectInsertMode(DocumentViewport::ObjectInsertMode mode);
    void setTouchGestureMode(int mode);
    void updateTheme(bool darkMode);
    void setUndoEnabled(bool enabled);
    void setRedoEnabled(bool enabled);
    void setStraightLineMode(bool enabled);

    // Per-tab state management keyed by unique tab IDs (from TabManager)
    void onTabChanged(int newTabId, int oldTabId);
    void clearTabState(int tabId);

    // Subtoolbar accessors for MainWindow signal wiring
    PenSubToolbar* penSubToolbar() const { return m_penSubToolbar; }
    MarkerSubToolbar* markerSubToolbar() const { return m_markerSubToolbar; }
    EraserSubToolbar* eraserSubToolbar() const { return m_eraserSubToolbar; }
    HighlighterSubToolbar* highlighterSubToolbar() const { return m_highlighterSubToolbar; }
    OcrSubToolbar* ocrSubToolbar() const { return m_ocrSubToolbar; }

    void setOcrAvailable(bool available);
    // Separate from setOcrAvailable: cached text stays viewable without an
    // engine, but a confidence score depends on what produced the data.
    void setOcrConfidenceSupported(bool supported);

    /**
     * @brief The wider single page plus one pager, not the whole row.
     *
     * Reporting the whole row would keep the window too wide to ever reach
     * the width where paging takes over.
     */
    QSize minimumSizeHint() const override;

signals:
    void toolSelected(ToolType tool);
    void objectInsertModeSelected(DocumentViewport::ObjectInsertMode mode);
    void straightLineToggled(bool enabled);
    void undoClicked();
    void redoClicked();
    void touchGestureModeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onOcrExpanded(bool expanded);

private:
    void setupUi();
    void connectSignals();
    void expandToolButton(ToolType tool);
    void collapseAllToolButtons();

    ExpandableToolButton* expandableForTool(ToolType tool) const;

    QWidget* createGapWidget(int width);

    int groupWidth(const QVector<QWidget*>& widgets) const;

    /**
     * @brief Width the button row wants right now, ignoring page visibility.
     *
     * Measured from size hints, so an expanded subtoolbar counts: picking a
     * tool with a wide preset strip can be what tips the row into paging.
     */
    int naturalContentWidth() const;

    /**
     * @brief Enter or leave paged mode based on the current width.
     */
    void updatePagination();

    void applyPageVisibility();
    void setToolbarPage(int page);

    /**
     * @brief Switch to the page owning @p widget, if paging is active.
     *
     * Keeps a tool selected by shortcut from checking a button nobody can see.
     */
    void revealWidget(QWidget* widget);

    void installSwipeFilter(QWidget* root);

    // Expandable tool buttons (own subtoolbar content)
    ExpandableToolButton *m_penExpandable;
    ExpandableToolButton *m_markerExpandable;
    ExpandableToolButton *m_eraserExpandable;
    ExpandableToolButton *m_textExpandable;
    ExpandableToolButton *m_ocrExpandable;

    // Plain tool buttons (no subtoolbar)
    ToolButton *m_lassoButton;
    ToolButton *m_objectImageButton;
    /// The Link tool's controls float in the viewport (LinkObjectBar), so this
    /// button carries no subtoolbar of its own.
    ToolButton *m_objectLinkButton;
    ToolButton *m_objectTextButton;
    ToolButton *m_panButton;

    // Non-exclusive toggle
    ToggleButton *m_straightLineButton;

    // Action buttons
    ActionButton *m_undoButton;
    ActionButton *m_redoButton;

    // Tab-specific mode
    ThreeStateButton *m_touchGestureButton;

    // Overflow paging
    ActionButton *m_pagerBackButton = nullptr;
    ActionButton *m_pagerNextButton = nullptr;
    QVector<QWidget*> m_page1Widgets;
    QVector<QWidget*> m_page2Widgets;

    // Tool group for exclusive selection
    QButtonGroup *m_toolGroup;

    // Subtoolbar instances (owned by this Toolbar, embedded in ExpandableToolButtons)
    PenSubToolbar *m_penSubToolbar;
    MarkerSubToolbar *m_markerSubToolbar;
    EraserSubToolbar *m_eraserSubToolbar;
    HighlighterSubToolbar *m_highlighterSubToolbar;
    OcrSubToolbar *m_ocrSubToolbar;

    // State
    bool m_darkMode = false;
    bool m_paged = false;
    int m_currentPage = 0;
    bool m_updatingPagination = false;
    QPoint m_swipeStart;
    bool m_swipeTracking = false;
    bool m_swipeConsumed = false;
    QColor m_borderColor;
    ToolType m_currentTool = ToolType::Pen;
    DocumentViewport::ObjectInsertMode m_objectInsertMode =
        DocumentViewport::ObjectInsertMode::Image;

    static constexpr int TOOLBAR_HEIGHT = 44;
    static constexpr int PAGING_HYSTERESIS = 12;
    static constexpr int SWIPE_THRESHOLD = 48;
};

#endif // TOOLBAR_H
