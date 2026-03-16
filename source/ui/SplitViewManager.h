#pragma once

// ============================================================================
// SplitViewManager - Manages dual-pane split view with independent tab bars
// ============================================================================
// Coordinates two independent panes (left/right), each with its own TabBar,
// QStackedWidget, and TabManager. Only one pane is "active" at a time; all
// MainWindow UI actions route through the active pane's viewport.
//
// When not split, the left pane occupies the full width and the right pane
// does not exist. Splitting creates the right pane on demand.
// ============================================================================

#include <QWidget>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVector>

class TabBar;
class TabManager;
class Document;
class DocumentViewport;
class QStackedWidget;

class SplitViewManager : public QWidget {
    Q_OBJECT

public:
    enum Pane { Left = 0, Right = 1 };

    explicit SplitViewManager(QWidget* parent = nullptr);
    ~SplitViewManager() override;

    // =========================================================================
    // Active pane
    // =========================================================================

    Pane activePane() const;
    void setActivePane(Pane pane);
    DocumentViewport* activeViewport() const;

    // =========================================================================
    // Split control
    // =========================================================================

    bool isSplit() const;

    /**
     * @brief Move a tab from sourcePane to the opposite pane.
     *
     * If not yet split, the right pane is created first.
     * If moving the last tab out of the right pane, the pane is auto-closed.
     */
    void splitTab(int tabIndex, Pane sourcePane);

    /**
     * @brief Move all right-pane tabs to the left pane, then destroy the right pane.
     */
    void mergePanes();

    // =========================================================================
    // Delegated TabManager API (routes to active pane by default)
    // =========================================================================

    int createTab(Document* doc, const QString& title);
    int createTabInPane(Document* doc, const QString& title, Pane pane);

    int totalTabCount() const;
    int activeTabCount() const;

    TabManager* activeTabManager() const;
    TabManager* leftTabManager() const;
    TabManager* rightTabManager() const;

    TabBar* leftTabBar() const;
    TabBar* rightTabBar() const;

    QStackedWidget* leftViewportStack() const;
    QStackedWidget* rightViewportStack() const;

    // =========================================================================
    // Iteration helpers (for session save, close-all, etc.)
    // =========================================================================

    struct TabRef {
        Pane pane;
        int index;
    };
    QVector<TabRef> allTabs() const;

    /**
     * @brief Apply a function to every TabManager (left, and right if split).
     */
    template<typename Func>
    void forEachTabManager(Func fn) {
        if (m_leftTabManager) fn(m_leftTabManager, Left);
        if (m_rightTabManager) fn(m_rightTabManager, Right);
    }

    /**
     * @brief Get the widget row that contains the tab bars (for layout insertion).
     */
    QWidget* tabBarContainer() const;

    /**
     * @brief Get the QSplitter that holds the viewport stacks (for layout insertion).
     */
    QSplitter* viewportSplitter() const;

    /**
     * @brief Apply theme to all tab bars (current and future).
     *
     * Stores the theme settings so that newly created right panes
     * automatically receive the correct theme.
     */
    void updateTheme(bool darkMode, const QColor& accentColor);

signals:
    void activeViewportChanged(DocumentViewport* viewport);
    void activePaneChanged(Pane pane);
    void tabCloseRequested(int tabId, DocumentViewport* viewport, Pane pane);
    void tabCloseAttempted(int tabId, DocumentViewport* viewport, Pane pane);
    void splitStateChanged(bool isSplit);

private slots:
    void onLeftViewportChanged(DocumentViewport* vp);
    void onRightViewportChanged(DocumentViewport* vp);
    void onLeftTabCloseAttempted(int index, DocumentViewport* vp);
    void onRightTabCloseAttempted(int index, DocumentViewport* vp);
    void onLeftTabCloseRequested(int index, DocumentViewport* vp);
    void onRightTabCloseRequested(int index, DocumentViewport* vp);

private:
    void createRightPane();
    void destroyRightPane();
    void updateTabBarContainerLayout();
    void updateActivePaneIndicator();
    void recenterAllViewports();
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Left pane (always exists)
    TabBar* m_leftTabBar = nullptr;
    QStackedWidget* m_leftViewportStack = nullptr;
    TabManager* m_leftTabManager = nullptr;

    // Right pane (created on demand)
    TabBar* m_rightTabBar = nullptr;
    QStackedWidget* m_rightViewportStack = nullptr;
    TabManager* m_rightTabManager = nullptr;

    // Layout
    QWidget* m_tabBarContainer = nullptr;
    QHBoxLayout* m_tabBarLayout = nullptr;
    QSplitter* m_splitter = nullptr;

    Pane m_activePane = Left;

    // Cached theme for applying to newly created panes
    bool m_darkMode = false;
    QColor m_accentColor;
};
