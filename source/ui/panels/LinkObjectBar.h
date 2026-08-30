#ifndef LINKOBJECTBAR_H
#define LINKOBJECTBAR_H

#include "../widgets/LinkSlotButton.h"    // For LinkSlotState

#include <QColor>
#include <QWidget>

class ColorPresetButton;
class SubToolbarToggle;
class QHBoxLayout;
class QLineEdit;
class QPushButton;

/**
 * Compact, viewport-owned controls for a selected LinkObject.
 *
 * Holds the annotation's color, its description editor, and its 3 link slots.
 * The bar keeps no object pointer: DocumentViewport owns the target lookup and
 * pushes state in through setValues(), mirroring TextBoxFormatBar.
 *
 * DocumentViewport anchors the bar next to the selected object and shows it
 * only while exactly one LinkObject is selected, so the controls are always
 * live whenever the bar is visible.
 */
class LinkObjectBar : public QWidget {
    Q_OBJECT

public:
    static constexpr int NUM_SLOTS = 3;

    explicit LinkObjectBar(QWidget* parent = nullptr);
    ~LinkObjectBar() override = default;

    /**
     * @brief Push the selected LinkObject's state into the controls.
     * @param states Slot states, one per slot.
     * @param iconColor The object's icon color.
     * @param description The object's description.
     * @param regionAdjustable True when the object carries a highlight, which
     *        is the only case where a text range exists to re-range. Hides the
     *        Adjust toggle for standalone link icons.
     * @param adjusting True while an Adjust session is live, so the same widget
     *        reads "Done".
     */
    void setValues(const LinkSlotState states[NUM_SLOTS],
                   const QColor& iconColor,
                   const QString& description,
                   bool regionAdjustable = false,
                   bool adjusting = false);

    void setDarkMode(bool darkMode);

    /**
     * @brief Close the description popup and any colour dialog.
     * @param acceptPreview true to commit what the user typed, false to discard.
     */
    void closePopups(bool acceptPreview = false);

    /**
     * @brief True while the description popup or a colour dialog is open.
     */
    bool hasOpenPopup() const;

    /**
     * @brief True when a control of this bar owns keyboard focus.
     *
     * MainWindow consults this so canvas shortcuts do not fire while the user
     * is typing a description.
     */
    bool controlHasFocus() const;

    bool eventFilter(QObject* watched, QEvent* event) override;

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
     */
    void linkObjectColorChanged(const QColor& color);

    /**
     * @brief Emitted when the LinkObject description is changed.
     */
    void linkObjectDescriptionChanged(const QString& description);

    /**
     * @brief Emitted when the user asks to enter or leave Adjust mode.
     * @param adjusting True to start re-ranging the highlight, false for Done.
     */
    void adjustToggled(bool adjusting);

protected:
    // Children such as the colour swatch ignore pointer events. Swallow
    // whatever reaches the bar so the canvas underneath never treats an
    // interaction with the bar as an outside click.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

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

    QHBoxLayout* m_layout = nullptr;

    ColorPresetButton* m_colorButton = nullptr;       // LinkObject color editor
    SubToolbarToggle* m_adjustButton = nullptr;       // Enter/leave Adjust mode
    SubToolbarToggle* m_descriptionButton = nullptr;  // Toggle description editor
    QWidget* m_descriptionPopup = nullptr;            // Popup container
    QLineEdit* m_descriptionEdit = nullptr;           // Description text editor
    QPushButton* m_confirmButton = nullptr;           // Confirm description
    QPushButton* m_cancelButton = nullptr;            // Cancel editing
    QString m_originalDescription;                    // For cancel functionality
    bool m_popupClosedByButton = false;               // Prevents double signal emission
    bool m_colorDialogOpen = false;                   // Modal colour dialog guard
    LinkSlotButton* m_slotButtons[NUM_SLOTS] = {nullptr, nullptr, nullptr};

    static constexpr int PADDING_LEFT = 6;
    static constexpr int PADDING_RIGHT = 6;
};

#endif // LINKOBJECTBAR_H
