#ifndef OBJECTSELECTSUBTOOLBAR_H
#define OBJECTSELECTSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../widgets/LinkSlotButton.h"    // For LinkSlotState
#include <QColor>

class LinkSlotButton;
class ColorPresetButton;
class SubToolbarToggle;
class QLineEdit;
class QWidget;
class QPushButton;

/**
 * @brief Expandable controls for the Link object tool.
 * 
 * Layout:
 * - LinkObject color and description editors
 * - 3 LinkSlotButtons for LinkObject slots
 * 
 * Slot buttons:
 * - Always visible
 * - Show Empty state if no LinkObject selected
 * - Show actual slot states when LinkObject is selected
 * 
 * The controls are visible only when a single LinkObject is selected.
 */
class ObjectSelectSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit ObjectSelectSubToolbar(QWidget* parent = nullptr);
    ~ObjectSelectSubToolbar() override = default;
    
    // Event filter for popup handling
    bool eventFilter(QObject* watched, QEvent* event) override;
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void setDarkMode(bool darkMode) override;
    
    /**
     * @brief Update slot button states based on selected LinkObject.
     * @param states Array of 3 slot states (or nullptr if no LinkObject selected).
     */
    void updateSlotStates(const LinkSlotState states[3]);
    
    /**
     * @brief Clear all slot states (no LinkObject selected).
     */
    void clearSlotStates();
    
    /**
     * @brief Set the LinkObject color button state.
     * @param color The current LinkObject color.
     * @param visible Whether the color button should be visible (LinkObject selected).
     */
    void setLinkObjectColor(const QColor& color, bool visible);
    
    /**
     * @brief Set the LinkObject description for editing.
     * @param description The current description.
     * @param enabled Whether editing is enabled (LinkObject selected).
     */
    void setLinkObjectDescription(const QString& description, bool enabled);
    
signals:
    /**
     * @brief Emitted when a slot button is clicked.
     * @param index The slot index (0, 1, or 2).
     */
    void slotActivated(int index);
    
    /**
     * @brief Emitted when slot content should be cleared (after confirmation).
     * @param index The slot index (0, 1, or 2).
     */
    void slotCleared(int index);
    
    /**
     * @brief Emitted when the LinkObject color is changed via the color button.
     * @param color The new color.
     */
    void linkObjectColorChanged(const QColor& color);
    
    /**
     * @brief Emitted when the LinkObject description is changed.
     * @param description The new description.
     */
    void linkObjectDescriptionChanged(const QString& description);

private slots:
    void onSlotClicked(int index);
    void onSlotDeleteRequested(int index);
    void onColorButtonClicked();
    void onColorButtonEditRequested();
    void onDescriptionButtonToggled(bool checked);
    void onDescriptionConfirm();
    void onDescriptionCancel();

private:
    void createWidgets();
    void setupConnections();
    bool confirmSlotDelete(int index);
    
    /**
     * @brief Show/hide LinkObject-specific controls.
     * @param visible true to show controls, false to hide them.
     * 
     * Controls: color button, description button, and 3 slot buttons.
     */
    void setLinkObjectControlsVisible(bool visible);

    // Widgets
    ColorPresetButton* m_colorButton = nullptr;  // LinkObject color editor
    SubToolbarToggle* m_descriptionButton = nullptr;  // Toggle description editor
    QWidget* m_descriptionPopup = nullptr;       // Popup container
    QLineEdit* m_descriptionEdit = nullptr;      // Description text editor
    QPushButton* m_confirmButton = nullptr;      // Confirm description
    QPushButton* m_cancelButton = nullptr;       // Cancel editing
    QString m_originalDescription;               // For cancel functionality
    bool m_popupClosedByButton = false;          // Prevents double signal emission
    LinkSlotButton* m_slotButtons[3] = {nullptr, nullptr, nullptr};
    
    static constexpr int NUM_SLOTS = 3;
};

#endif // OBJECTSELECTSUBTOOLBAR_H

