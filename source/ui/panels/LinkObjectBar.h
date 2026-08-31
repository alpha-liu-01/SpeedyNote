#ifndef LINKOBJECTBAR_H
#define LINKOBJECTBAR_H

#include "../widgets/LinkSlotButton.h"    // For LinkSlotState

#include <QColor>
#include <QWidget>

class ColorPresetButton;
class SubToolbarToggle;
class QAction;
class QHBoxLayout;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;

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
     * @param hasRegion True when the object carries a highlight at all. Drives
     *        what the single colour swatch means: the mark's colour for a
     *        highlight, the badge tint for a standalone link icon. Distinct
     *        from @p regionAdjustable, which is additionally false when the
     *        object is locked.
     * @param regionColor The highlight's colour, stored at
     *        HighlightRegion::DEFAULT_OPACITY. Shown and edited opaque.
     * @param regionStyle The highlight's HighlightRegion::Style as an int.
     */
    void setValues(const LinkSlotState states[NUM_SLOTS],
                   const QColor& iconColor,
                   const QString& description,
                   bool regionAdjustable = false,
                   bool adjusting = false,
                   bool hasRegion = false,
                   const QColor& regionColor = QColor(),
                   int regionStyle = 0);

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
     *
     * Only for standalone link icons. When the annotation carries a highlight
     * the same button edits the mark instead and emits regionColorChanged.
     */
    void linkObjectColorChanged(const QColor& color);

    /**
     * @brief Emitted when the highlight's colour is changed via the color button.
     * @param color Opaque as picked; the viewport applies the stored alpha.
     */
    void regionColorChanged(const QColor& color);

    /**
     * @brief Emitted when the highlight's style is picked from the dropdown.
     * @param style A HighlightRegion::Style value as an int.
     */
    void regionStyleChanged(int style);

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
    void onRegionStyleTriggered(QAction* action);
    void onDescriptionButtonToggled(bool checked);
    void onDescriptionConfirm();
    void onDescriptionCancel();

private:
    void createWidgets();
    void setupConnections();
    bool confirmSlotDelete(int index);

    /// Refresh the trigger icon and check state to match @ref m_regionStyle.
    void updateRegionStyleButtonIcon();
    /// Rebuild the per-action icons and dark/light stylesheets. Theme only.
    void applyRegionStyleStyling();

    QHBoxLayout* m_layout = nullptr;

    /// Number of styles the dropdown offers: Cover, Underline, DottedUnderline.
    /// HighlightRegion::Style::None is deliberately absent - it means
    /// "select text only", which is a Highlighter tool mode, not something an
    /// annotation that already exists can be set to.
    static constexpr int NUM_REGION_STYLES = 3;

    ColorPresetButton* m_colorButton = nullptr;       // Mark colour or badge tint
    QToolButton* m_regionStyleButton = nullptr;       // Highlight style dropdown
    QMenu* m_regionStyleMenu = nullptr;               // Its 3 style entries
    QAction* m_regionStyleActions[NUM_REGION_STYLES] = {nullptr, nullptr, nullptr};
    SubToolbarToggle* m_adjustButton = nullptr;       // Enter/leave Adjust mode
    SubToolbarToggle* m_descriptionButton = nullptr;  // Toggle description editor
    QWidget* m_descriptionPopup = nullptr;            // Popup container
    QLineEdit* m_descriptionEdit = nullptr;           // Description text editor
    QPushButton* m_confirmButton = nullptr;           // Confirm description
    QPushButton* m_cancelButton = nullptr;            // Cancel editing
    QString m_originalDescription;                    // For cancel functionality
    bool m_popupClosedByButton = false;               // Prevents double signal emission
    bool m_colorDialogOpen = false;                   // Modal colour dialog guard
    bool m_hasRegion = false;                         // Which colour the swatch edits
    bool m_darkMode = false;                          // For the dropdown's icons
    int m_regionStyle = 0;                            // HighlightRegion::Style as int
    LinkSlotButton* m_slotButtons[NUM_SLOTS] = {nullptr, nullptr, nullptr};

    static constexpr int PADDING_LEFT = 6;
    static constexpr int PADDING_RIGHT = 6;

    /// Size of a round button chip. Mirrors SubToolbarToggle::BUTTON_SIZE and
    /// LinkSlotButton::BUTTON_SIZE, which are private to widgets the style
    /// dropdown is not derived from, so the value is repeated here to keep the
    /// plain QToolButton the same shape as its neighbours.
    static constexpr int CHIP_SIZE = 24;
    static constexpr int CHIP_ICON_SIZE = 16;
};

#endif // LINKOBJECTBAR_H
