#ifndef NOTEBOOKCARD_H
#define NOTEBOOKCARD_H

#include <QWidget>
#include <QPixmap>
#include <QTimer>
#include "../../core/NotebookLibrary.h"

/**
 * @brief A card widget representing a notebook in the Launcher.
 * 
 * NotebookCard displays a notebook thumbnail with name, type indicator,
 * and star status. Designed for use in grid layouts (Starred view).
 * 
 * Features:
 * - Fixed size for consistent grid layout
 * - Thumbnail with C+D hybrid display (top-crop for tall, letterbox for short)
 * - Name label (elided if too long)
 * - Type indicator icon (PDF/Edgeless/Paged)
 * - Star indicator
 * - Tap → clicked() signal
 * - Long-press (500ms) → longPressed() signal for context menu
 * - Hover effects
 * - Dark mode support
 * 
 * Phase P.3.4: Part of the new Launcher implementation.
 */
class NotebookCard : public QWidget {
    Q_OBJECT

public:
    explicit NotebookCard(QWidget* parent = nullptr);
    explicit NotebookCard(const NotebookInfo& info, QWidget* parent = nullptr);
    
    /**
     * @brief Set the notebook info to display.
     */
    void setNotebookInfo(const NotebookInfo& info);
    
    /**
     * @brief Get the current notebook info.
     */
    const NotebookInfo& notebookInfo() const { return m_info; }
    
    /**
     * @brief Get the bundle path of this notebook.
     */
    QString bundlePath() const { return m_info.bundlePath; }
    
    /**
     * @brief Set dark mode for theming.
     * @param dark True for dark theme colors.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Check if dark mode is enabled.
     */
    bool isDarkMode() const { return m_darkMode; }
    
    /**
     * @brief Set whether this card is in a selected state.
     */
    void setSelected(bool selected);
    
    /**
     * @brief Check if this card is selected.
     */
    bool isSelected() const { return m_selected; }
    
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /**
     * @brief Emitted when the card is clicked (tap).
     */
    void clicked();
    
    /**
     * @brief Emitted on long-press (for context menu).
     */
    void longPressed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void loadThumbnail();
    void drawThumbnail(QPainter* painter, const QRect& rect) const;
    QString typeIndicatorText() const;
    QColor typeIndicatorColor() const;
    QColor backgroundColor() const;
    
    NotebookInfo m_info;
    QPixmap m_thumbnail;
    QString m_thumbnailPath;
    
    bool m_darkMode = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_selected = false;
    
    // Long-press detection
    QTimer m_longPressTimer;
    QPoint m_pressPos;
    bool m_longPressTriggered = false;
    
    // Layout constants
    static constexpr int CARD_WIDTH = 120;
    static constexpr int CARD_HEIGHT = 160;
    static constexpr int THUMBNAIL_HEIGHT = 100;
    static constexpr int PADDING = 8;
    static constexpr int CORNER_RADIUS = 12;
    static constexpr int THUMBNAIL_CORNER_RADIUS = 8;
    static constexpr int LONG_PRESS_MS = 500;
    static constexpr int LONG_PRESS_MOVE_THRESHOLD = 10;
};

#endif // NOTEBOOKCARD_H

