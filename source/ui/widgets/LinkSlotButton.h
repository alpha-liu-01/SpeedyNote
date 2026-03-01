#ifndef LINKSLOTBUTTON_H
#define LINKSLOTBUTTON_H

#include <QWidget>
#include <QIcon>

/**
 * @brief Enum representing the state of a LinkObject slot.
 */
enum class LinkSlotState {
    Empty,      ///< Slot has no content (shows + icon)
    Position,   ///< Slot contains a position link (📍)
    Url,        ///< Slot contains a URL link (🔗)
    Markdown    ///< Slot contains a markdown link (📝)
};

// Register with Qt meta-object system for use in signals/slots
Q_DECLARE_METATYPE(LinkSlotState)

/**
 * @brief A button that shows LinkObject slot state with appropriate icon.
 * 
 * Supports long-press to delete slot content (only for non-empty slots).
 * 
 * States and icons:
 * - Empty: Plus icon (+)
 * - Position: Position icon (📍)
 * - URL: Link icon (🔗)
 * - Markdown: Markdown icon (📝)
 * 
 * Long-press behavior:
 * - Empty slot: Do nothing
 * - Filled slot: Emit deleteRequested()
 * 
 * Size: 28×28 logical pixels, round
 * 
 * Supports dark/light mode icon switching via setDarkMode().
 */
class LinkSlotButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(LinkSlotState state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)

public:
    explicit LinkSlotButton(QWidget* parent = nullptr);
    
    /**
     * @brief Get the current slot state.
     */
    LinkSlotState state() const;
    
    /**
     * @brief Set the slot state.
     * @param state The new state.
     */
    void setState(LinkSlotState state);
    
    /**
     * @brief Check if this button is currently selected.
     */
    bool isSelected() const;
    
    /**
     * @brief Set the selected state of this button.
     * @param selected True to select, false to deselect.
     */
    void setSelected(bool selected);
    
    /**
     * @brief Set custom icons for each state directly (no dark mode switching).
     * 
     * If not set, default built-in icons are used.
     */
    void setStateIcons(const QIcon& emptyIcon, 
                       const QIcon& positionIcon,
                       const QIcon& urlIcon, 
                       const QIcon& markdownIcon);
    
    /**
     * @brief Set icons for each state by base name (enables dark mode switching).
     * @param emptyBaseName Base name for empty state icon.
     * @param positionBaseName Base name for position state icon.
     * @param urlBaseName Base name for URL state icon.
     * @param markdownBaseName Base name for markdown state icon.
     */
    void setStateIconNames(const QString& emptyBaseName,
                           const QString& positionBaseName,
                           const QString& urlBaseName,
                           const QString& markdownBaseName);
    
    /**
     * @brief Set dark mode and update icons accordingly.
     * @param darkMode True for dark mode, false for light mode.
     */
    void setDarkMode(bool darkMode);
    
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
     * @brief Emitted when the button is clicked (short press).
     */
    void clicked();
    
    /**
     * @brief Emitted when the state changes.
     * @param state The new state.
     */
    void stateChanged(LinkSlotState state);
    
    /**
     * @brief Emitted when the selected state changes.
     * @param selected The new selected state.
     */
    void selectedChanged(bool selected);
    
    /**
     * @brief Emitted on long-press of a non-empty slot.
     * 
     * The receiver should show a confirmation dialog and then
     * clear the slot content if confirmed.
     */
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private:
    /**
     * @brief Get the border color based on selection state and theme.
     */
    QColor borderColor() const;
    
    /**
     * @brief Get the background color based on state.
     */
    QColor backgroundColor() const;
    
    /**
     * @brief Get the icon for the current state.
     */
    QIcon currentIcon() const;
    
    /**
     * @brief Start the long-press timer.
     */
    void startLongPressTimer();
    
    /**
     * @brief Stop and reset the long-press timer.
     */
    void stopLongPressTimer();
    
    /**
     * @brief Update tooltip based on current state.
     */
    void updateToolTip();
    
    /**
     * @brief Update icons based on current dark mode setting.
     */
    void updateIcons();

    LinkSlotState m_state = LinkSlotState::Empty;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_hovered = false;
    bool m_longPressTriggered = false;
    bool m_darkMode = false;
    int m_longPressTimerId = 0;
    
    // Custom icons (optional)
    QIcon m_icons[4];
    QString m_iconBaseNames[4];
    bool m_hasCustomIcons = false;
    
    static constexpr int BUTTON_SIZE = 24;
    static constexpr int BORDER_RADIUS = BUTTON_SIZE / 2;
    static constexpr int ICON_SIZE = 16;
    static constexpr int LONG_PRESS_MS = 500;
    static constexpr int BORDER_WIDTH_NORMAL = 1;
    static constexpr int BORDER_WIDTH_SELECTED = 2;
};

#endif // LINKSLOTBUTTON_H

