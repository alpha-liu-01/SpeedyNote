#ifndef UNDODELETEBUTTON_H
#define UNDODELETEBUTTON_H

#include <QWidget>
#include <QIcon>

class QTimer;

/**
 * @brief A delete button that transforms to an undo state after click.
 * 
 * This provides a "soft delete" UX where the user can undo within a timeout period.
 * 
 * Behavior:
 * 1. Normal state: Shows delete/trash icon
 * 2. Click → enters UndoPending state, shows undo icon, starts 5-sec timer
 * 3. In UndoPending:
 *    - Click → emit undoRequested(), return to Normal
 *    - Timer expires → emit deleteConfirmed(), return to Normal
 *    - confirmDelete() called → emit deleteConfirmed(), return to Normal
 * 
 * Size: 36×36 logical pixels, fully round (same as ActionBarButton)
 */
class UndoDeleteButton : public QWidget {
    Q_OBJECT

public:
    explicit UndoDeleteButton(QWidget* parent = nullptr);
    ~UndoDeleteButton();
    
    /**
     * @brief Set dark mode appearance and update icons.
     * @param dark True for dark mode, false for light mode.
     */
    void setDarkMode(bool dark);
    
    /**
     * @brief Immediately confirm the delete (skip timer).
     * 
     * Call this when an external action should confirm the delete
     * (e.g., page was successfully deleted). Only has effect in UndoPending state.
     */
    void confirmDelete();
    
    /**
     * @brief Check if the button is currently in undo-pending state.
     */
    bool isUndoPending() const;
    
    /**
     * @brief Reset button to normal state without emitting signals.
     * 
     * Useful when the operation was cancelled externally.
     */
    void reset();
    
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
     * @brief Emitted on first click (delete requested but not confirmed).
     * 
     * The caller should perform the delete operation, but keep the data
     * for potential undo.
     */
    void deleteRequested();
    
    /**
     * @brief Emitted after timeout expires or confirmDelete() is called.
     * 
     * The caller can now permanently discard the deleted data.
     */
    void deleteConfirmed();
    
    /**
     * @brief Emitted if undo is clicked within the timeout period.
     * 
     * The caller should restore the deleted data.
     */
    void undoRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private slots:
    /**
     * @brief Handle timer expiration.
     */
    void onTimerExpired();

private:
    /**
     * @brief Button states.
     */
    enum class State {
        Normal,       ///< Showing delete icon, waiting for click
        UndoPending   ///< Showing undo icon, timer running
    };
    
    /**
     * @brief Check if the application is in dark mode based on palette.
     */
    bool isDarkMode() const;
    
    /**
     * @brief Get the background color based on state and theme.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Update icons based on current dark mode setting.
     */
    void updateIcons();
    
    /**
     * @brief Start the undo timeout timer.
     */
    void startUndoTimer();
    
    /**
     * @brief Reset to normal state.
     */
    void resetToNormal();

    // State
    State m_state = State::Normal;
    QTimer* m_confirmTimer = nullptr;
    
    // Appearance
    bool m_darkMode = false;
    bool m_pressed = false;
    bool m_hovered = false;
    
    // Icons
    QIcon m_deleteIcon;
    QIcon m_undoIcon;
    
    // Constants
    static constexpr int BUTTON_SIZE = 36;
    static constexpr int ICON_SIZE = 20;
    static constexpr int UNDO_TIMEOUT_MS = 5000;  // 5 seconds
};

#endif // UNDODELETEBUTTON_H

