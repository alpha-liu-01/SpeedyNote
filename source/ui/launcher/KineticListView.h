#ifndef KINETICLISTVIEW_H
#define KINETICLISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>

class KineticScrollHelper;

/**
 * @brief Base class for QListView with kinetic scrolling and long-press support.
 * 
 * This class provides common functionality for launcher list views:
 * - Kinetic scrolling for touch input (velocity-based momentum)
 * - Long-press detection for context menus / batch select mode
 * - Touch vs mouse input differentiation
 * 
 * Subclasses override the protected virtual methods to handle item-specific
 * actions (clicks, menu requests, etc.).
 * 
 * This eliminates code duplication between StarredListView, TimelineListView,
 * and SearchListView.
 */
class KineticListView : public QListView {
    Q_OBJECT

public:
    explicit KineticListView(QWidget* parent = nullptr);
    virtual ~KineticListView() = default;

signals:
    /**
     * @brief Emitted when a long-press is detected on an item.
     * @param index The model index that was long-pressed.
     * @param globalPos The global position for context menu placement.
     */
    void itemLongPressed(const QModelIndex& index, const QPoint& globalPos);

protected:
    // Mouse event handlers (implement kinetic scrolling)
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Handle a tap (non-scroll click) on an item.
     * @param index The tapped item's index.
     * @param pos The position of the tap in viewport coordinates.
     * 
     * Subclasses override this to handle item clicks (e.g., emit clicked signal,
     * check for menu button, etc.)
     */
    virtual void handleItemTap(const QModelIndex& index, const QPoint& pos);
    
    /**
     * @brief Handle a right-click on an item.
     * @param index The right-clicked item's index.
     * @param globalPos The global position for context menu.
     * 
     * Subclasses override this to show context menus.
     */
    virtual void handleRightClick(const QModelIndex& index, const QPoint& globalPos);
    
    /**
     * @brief Handle a long-press on an item.
     * @param index The long-pressed item's index.
     * @param globalPos The global position for context menu.
     * 
     * Default implementation emits itemLongPressed signal.
     * Subclasses can override for custom behavior.
     */
    virtual void handleLongPress(const QModelIndex& index, const QPoint& globalPos);
    
    /**
     * @brief Find index at a position, with optional fallback logic.
     * @param pos The position in viewport coordinates.
     * @return The model index at that position, or invalid if none.
     * 
     * Default implementation just calls indexAt(). Subclasses can override
     * to provide fallback logic (e.g., indexAtY for full-width items).
     */
    virtual QModelIndex indexAtPosition(const QPoint& pos) const;

private slots:
    void onLongPressTimeout();

private:
    // Long-press detection
    QTimer m_longPressTimer;
    static constexpr int LONG_PRESS_MS = 500;
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 10;
    
    // Scroll state
    QPoint m_pressPos;
    QModelIndex m_pressedIndex;
    bool m_longPressTriggered = false;
    bool m_touchScrolling = false;
    int m_scrollStartValue = 0;
    
    // Kinetic scrolling
    KineticScrollHelper* m_kineticHelper = nullptr;
};

#endif // KINETICLISTVIEW_H
