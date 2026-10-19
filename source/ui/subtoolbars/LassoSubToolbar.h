#ifndef LASSOSUBTOOLBAR_H
#define LASSOSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../widgets/ModeToggleButton.h"

/**
 * @brief Subtoolbar for the Lasso tool.
 *
 * Layout:
 * - One mode toggle: Freeform polygon ↔ Rectangle marquee
 *
 * The mode is a global preference persisted via DocumentViewport's
 * QSettings key (lasso/mode); this bar is only the UI surface for it.
 */
class LassoSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit LassoSubToolbar(QWidget* parent = nullptr);

    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;

    /**
     * @brief Emit lassoModeChanged for the current toggle state.
     *
     * Call when connecting to a new viewport so its mode matches the bar.
     */
    void emitCurrentMode();

    /**
     * @brief Current mode index (0 = Freeform, 1 = Rectangle).
     */
    int currentModeIndex() const;

    /**
     * @brief Set the mode from external source without emitting a signal.
     * @param mode 0 = Freeform, 1 = Rectangle.
     */
    void setModeState(int mode);

signals:
    /**
     * @brief Emitted when the lasso mode changes.
     * @param mode 0 = Freeform, 1 = Rectangle.
     */
    void lassoModeChanged(int mode);

private:
    void createWidgets();
    void setupConnections();
    void loadFromSettings();
    void saveToSettings();

    ModeToggleButton* m_modeToggle = nullptr;
    int m_lassoModeIndex = 0;  // 0 = Freeform, 1 = Rectangle

    static const QString SETTINGS_GROUP;
    static const QString KEY_LASSO_MODE;
};

#endif // LASSOSUBTOOLBAR_H
