#ifndef PAGEWHEELPICKER_H
#define PAGEWHEELPICKER_H

#include <QWidget>
#include <QElapsedTimer>

class QTimer;
class QPropertyAnimation;

/**
 * @brief iPhone-style wheel picker for page number selection.
 * 
 * A vertical scroll wheel showing 3 page numbers at a time with the current
 * page centered. Supports drag scrolling with inertia and snap-to-page.
 * 
 * Visual specs:
 * - Size: 36×72 px ("hotdog" shape with 18px border radius)
 * - Center number: 14px bold, full opacity (1-based display)
 * - Adjacent numbers: 10px light, 40% opacity
 * - Background: theme-aware (same as action bar buttons)
 * 
 * Behavior:
 * 1. Drag up/down to scroll through pages
 * 2. Release with velocity → inertia scroll with deceleration
 * 3. When velocity < threshold → snap to nearest whole page
 * 4. Emit currentPageChanged during scroll AND on final snap
 * 5. Mouse wheel also scrolls
 */
class PageWheelPicker : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageCount READ pageCount WRITE setPageCount NOTIFY pageCountChanged)

public:
    explicit PageWheelPicker(QWidget* parent = nullptr);
    ~PageWheelPicker();
    
    /**
     * @brief Get the current page index (0-based).
     */
    int currentPage() const;
    
    /**
     * @brief Set the current page index (0-based).
     * @param page The page index to display.
     */
    void setCurrentPage(int page);
    
    /**
     * @brief Get the total page count.
     */
    int pageCount() const;
    
    /**
     * @brief Set the total page count.
     * @param count The number of pages.
     */
    void setPageCount(int count);
    
    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode, false for light mode.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Get the recommended size for this widget.
     */
    QSize sizeHint() const override;
    
    /**
     * @brief Get the minimum size for this widget.
     */
    QSize minimumSizeHint() const override;

signals:
    /**
     * @brief Emitted when the current page changes (during scroll AND on snap).
     * @param page The new current page index (0-based).
     */
    void currentPageChanged(int page);
    
    /**
     * @brief Emitted when the page count changes.
     * @param count The new page count.
     */
    void pageCountChanged(int count);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    /**
     * @brief Handle inertia timer tick.
     */
    void onInertiaTimer();
    
    /**
     * @brief Handle snap animation finished.
     */
    void onSnapFinished();

private:
    /**
     * @brief Check if the application is in dark mode based on palette.
     */
    bool isDarkMode() const;
    
    /**
     * @brief Get the background color based on dark mode state.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Get text color for center (current) page.
     */
    QColor centerTextColor() const;
    
    /**
     * @brief Get text color for adjacent pages (dimmed).
     */
    QColor adjacentTextColor() const;
    
    /**
     * @brief Start inertia scrolling with current velocity.
     */
    void startInertia();
    
    /**
     * @brief Stop any ongoing inertia scrolling.
     */
    void stopInertia();
    
    /**
     * @brief Start snap animation to nearest page.
     */
    void snapToPage();
    
    /**
     * @brief Update current page from scroll offset.
     */
    void updateFromOffset();
    
    /**
     * @brief Clamp scroll offset to valid range.
     */
    void clampOffset();
    
    /**
     * @brief Get the scroll offset property (for animation).
     */
    qreal scrollOffset() const;
    
    /**
     * @brief Set the scroll offset property (for animation).
     */
    void setScrollOffset(qreal offset);

    // Page state
    int m_currentPage = 0;      // 0-based
    int m_pageCount = 1;
    
    // Scroll state
    qreal m_scrollOffset = 0.0; // Fractional page offset during drag
    qreal m_velocity = 0.0;     // For inertia (pages per second)
    QPointF m_lastPos;
    QElapsedTimer m_velocityTimer;
    bool m_dragging = false;
    
    // Animation
    QTimer* m_inertiaTimer = nullptr;
    QPropertyAnimation* m_snapAnimation = nullptr;
    
    // Appearance
    bool m_darkMode = false;
    
    // Constants
    static constexpr int WIDGET_WIDTH = 36;
    static constexpr int WIDGET_HEIGHT = 72;
    static constexpr int BORDER_RADIUS = 18;
    static constexpr int VISIBLE_PAGES = 3;
    static constexpr int ROW_HEIGHT = 24;           // Height per page number row
    static constexpr int CENTER_FONT_SIZE = 14;
    static constexpr int ADJACENT_FONT_SIZE = 10;
    static constexpr qreal ADJACENT_OPACITY = 0.4;
    static constexpr qreal DECELERATION = 0.92;     // Velocity multiplier per frame
    static constexpr qreal SNAP_THRESHOLD = 0.5;    // Pages per second threshold to start snap
    static constexpr int INERTIA_INTERVAL_MS = 16;  // ~60 FPS
    static constexpr int SNAP_DURATION_MS = 150;    // Snap animation duration
    
    // Property for QPropertyAnimation
    Q_PROPERTY(qreal scrollOffsetProperty READ scrollOffset WRITE setScrollOffset)
};

#endif // PAGEWHEELPICKER_H

