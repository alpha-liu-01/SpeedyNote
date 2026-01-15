#ifndef TIMELINELISTVIEW_H
#define TIMELINELISTVIEW_H

#include <QListView>
#include <QTimer>
#include <QPoint>

/**
 * @brief A QListView subclass that adds long-press support for touch devices.
 * 
 * This class extends QListView to emit a longPressed signal when the user
 * presses and holds on an item for LONG_PRESS_MS milliseconds. This enables
 * context menu functionality on touch devices where right-click is not available.
 * 
 * The long-press is cancelled if the user moves their finger more than
 * LONG_PRESS_MOVE_THRESHOLD pixels from the initial press position.
 */
class TimelineListView : public QListView {
    Q_OBJECT

public:
    explicit TimelineListView(QWidget* parent = nullptr);
    
signals:
    /**
     * @brief Emitted when user long-presses on an item.
     * @param index The model index of the long-pressed item.
     * @param globalPos The global position where the long-press occurred.
     */
    void longPressed(const QModelIndex& index, const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void onLongPressTimeout();
    
    QTimer m_longPressTimer;
    QPoint m_pressPos;              // Position where press started (viewport coords)
    QModelIndex m_pressedIndex;     // Index that was pressed
    bool m_longPressTriggered = false;
    
    static constexpr int LONG_PRESS_MS = 500;           // Time to trigger long-press
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 20; // Max movement in pixels
};

#endif // TIMELINELISTVIEW_H
