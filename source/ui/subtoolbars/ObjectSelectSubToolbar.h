#ifndef OBJECTSELECTSUBTOOLBAR_H
#define OBJECTSELECTSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../../core/DocumentViewport.h"  // For ObjectInsertMode, ObjectActionMode
#include "../widgets/LinkSlotButton.h"    // For LinkSlotState
#include <QHash>
#include <QColor>

class ModeToggleButton;
class LinkSlotButton;
class ColorPresetButton;
class SubToolbarToggle;
class QLineEdit;
class QWidget;
class QPushButton;

/**
 * @brief Subtoolbar for the ObjectSelect tool.
 * 
 * Layout:
 * - Insert mode toggle (Image ↔ Link)
 * - Action mode toggle (Select ↔ Create)
 * - Separator
 * - 3 LinkSlotButtons for LinkObject slots
 * 
 * Slot buttons:
 * - Always visible
 * - Show Empty state if no LinkObject selected
 * - Show actual slot states when LinkObject is selected
 * 
 * Features:
 * - Mode toggles for insert type and action type
 * - Slot buttons for quick LinkObject slot access
 * - Long-press on filled slot triggers delete confirmation
 */
class ObjectSelectSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit ObjectSelectSubToolbar(QWidget* parent = nullptr);
    ~ObjectSelectSubToolbar() override;
    
    // Event filter for popup handling
    bool eventFilter(QObject* watched, QEvent* event) override;
    
    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;
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
    
    /**
     * @brief Set the insert mode toggle state from outside.
     * @param mode The new mode.
     * 
     * Used to sync the toggle when mode is changed via keyboard shortcut (Ctrl+< / Ctrl+>).
     * This updates the UI without emitting insertModeChanged to avoid loops.
     */
    void setInsertModeState(DocumentViewport::ObjectInsertMode mode);
    
    /**
     * @brief Set the action mode toggle state from outside.
     * @param mode The new mode.
     * 
     * Used to sync the toggle when mode is changed via keyboard shortcut (Ctrl+6 / Ctrl+7).
     * This updates the UI without emitting actionModeChanged to avoid loops.
     */
    void setActionModeState(DocumentViewport::ObjectActionMode mode);

signals:
    /**
     * @brief Emitted when insert mode changes.
     * @param mode The new insert mode (Image or Link).
     */
    void insertModeChanged(DocumentViewport::ObjectInsertMode mode);
    
    /**
     * @brief Emitted when action mode changes.
     * @param mode The new action mode (Select or Create).
     */
    void actionModeChanged(DocumentViewport::ObjectActionMode mode);
    
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
    void onInsertModeToggled(int mode);
    void onActionModeToggled(int mode);
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
    void loadFromSettings();
    void saveToSettings();
    bool confirmSlotDelete(int index);

    // Widgets
    ModeToggleButton* m_insertModeToggle = nullptr;
    ModeToggleButton* m_actionModeToggle = nullptr;
    ColorPresetButton* m_colorButton = nullptr;  // LinkObject color editor
    SubToolbarToggle* m_descriptionButton = nullptr;  // Toggle description editor
    QWidget* m_descriptionPopup = nullptr;       // Popup container
    QLineEdit* m_descriptionEdit = nullptr;      // Description text editor
    QPushButton* m_confirmButton = nullptr;      // Confirm description
    QPushButton* m_cancelButton = nullptr;       // Cancel editing
    QString m_originalDescription;               // For cancel functionality
    bool m_popupClosedByButton = false;          // Prevents double signal emission
    LinkSlotButton* m_slotButtons[3] = {nullptr, nullptr, nullptr};
    
    // Current state
    DocumentViewport::ObjectInsertMode m_insertMode = DocumentViewport::ObjectInsertMode::Image;
    DocumentViewport::ObjectActionMode m_actionMode = DocumentViewport::ObjectActionMode::Select;
    
    // Per-tab state storage
    struct TabState {
        DocumentViewport::ObjectInsertMode insertMode;
        DocumentViewport::ObjectActionMode actionMode;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;
    
    // QSettings keys
    static const QString SETTINGS_GROUP;
    static const QString KEY_INSERT_MODE;
    static const QString KEY_ACTION_MODE;
    
    static constexpr int NUM_SLOTS = 3;
};

#endif // OBJECTSELECTSUBTOOLBAR_H

