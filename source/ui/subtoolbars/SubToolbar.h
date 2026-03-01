#ifndef SUBTOOLBAR_H
#define SUBTOOLBAR_H

#include <QWidget>
#include <QHBoxLayout>

class QFrame;

/**
 * @brief Abstract base class for all subtoolbars.
 *
 * Subtoolbars provide tool-specific option buttons (color presets,
 * thickness presets, toggles, etc.) arranged horizontally and embedded
 * inline within an ExpandableToolButton on the main Toolbar.
 *
 * Subclasses must implement:
 * - refreshFromSettings(): Load preset values from QSettings
 * - restoreTabState(int): Restore per-tab state when switching tabs
 * - saveTabState(int): Save per-tab state before switching away
 */
class SubToolbar : public QWidget {
    Q_OBJECT

public:
    explicit SubToolbar(QWidget* parent = nullptr);
    virtual ~SubToolbar() = default;

    /**
     * @brief Refresh button values from QSettings.
     */
    virtual void refreshFromSettings() = 0;

    /**
     * @brief Restore per-tab state when switching to a tab.
     */
    virtual void restoreTabState(int tabIndex) = 0;

    /**
     * @brief Save per-tab state before switching away from a tab.
     */
    virtual void saveTabState(int tabIndex) = 0;

    /**
     * @brief Clear per-tab state when a tab is closed.
     */
    virtual void clearTabState(int tabIndex) { Q_UNUSED(tabIndex); }

    /**
     * @brief Sync shared state from QSettings.
     *
     * Called when switching to this subtoolbar to sync values shared with
     * other subtoolbars (e.g., Marker and Highlighter share colors).
     */
    virtual void syncSharedState() {}

    /**
     * @brief Set dark mode and update all button icons accordingly.
     *
     * Subclasses should override this to propagate dark mode to their buttons.
     */
    virtual void setDarkMode(bool darkMode);

signals:
    /**
     * @brief Emitted when the subtoolbar's content size changes.
     *
     * The ExpandableToolButton connects to this signal to update its
     * geometry when widgets are shown/hidden (e.g., ObjectSelect LinkObject controls).
     */
    void contentSizeChanged();

protected:
    /**
     * @brief Add a vertical separator line between button groups.
     */
    void addSeparator();

    /**
     * @brief Add a widget to the subtoolbar layout.
     */
    void addWidget(QWidget* widget);

    /**
     * @brief Add a stretch to the layout.
     */
    void addStretch();

    /**
     * @brief Check if the application is in dark mode.
     */
    bool isDarkMode() const;

    /**
     * @brief The main horizontal layout for button arrangement.
     */
    QHBoxLayout* m_layout = nullptr;

    static constexpr int PADDING_LEFT = 2;
    static constexpr int PADDING_RIGHT = 6;
    static constexpr int SEPARATOR_HEIGHT = 20;
};

#endif // SUBTOOLBAR_H
