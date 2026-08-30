#include "LinkObjectBar.h"

#include "../../objects/HighlightRegion.h"
#include "../widgets/LinkSlotButton.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ToggleButton.h"  // Contains SubToolbarToggle

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>

namespace {

bool paletteIsDark()
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();
    return luminance < 0.5;
}

// Dropdown entry order. Skips HighlightRegion::Style::None, so the menu index
// is not the enum value and the two have to be mapped explicitly.
constexpr HighlightRegion::Style kRegionStyles[] = {
    HighlightRegion::Style::Cover,
    HighlightRegion::Style::Underline,
    HighlightRegion::Style::DottedUnderline,
};

// Same icons the Highlighter subtoolbar's style dropdown uses, so a mark's
// style reads identically wherever it is shown.
constexpr const char* kRegionStyleIconBases[] = {
    "marker",
    "highlight_underline",
    "highlight_dotted",
};

QIcon loadRegionStyleIcon(int entry, bool dark)
{
    const QLatin1String base(kRegionStyleIconBases[entry]);
    return QIcon(dark
        ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(base)
        : QStringLiteral(":/resources/icons/%1.png").arg(base));
}

constexpr int kNumRegionStyles =
    static_cast<int>(sizeof(kRegionStyles) / sizeof(kRegionStyles[0]));

/// Dropdown entry for a HighlightRegion::Style value, or -1 when it has none
/// (Style::None, which only a hand-edited or corrupt file can produce).
int entryForRegionStyle(int style)
{
    for (int i = 0; i < kNumRegionStyles; ++i) {
        if (static_cast<int>(kRegionStyles[i]) == style) return i;
    }
    return -1;
}

}  // namespace

LinkObjectBar::LinkObjectBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("linkObjectBar"));
    setFixedHeight(36);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(PADDING_LEFT, 0, PADDING_RIGHT, 0);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    createWidgets();
    setupConnections();
    setDarkMode(paletteIsDark());
    adjustSize();
}

bool LinkObjectBar::eventFilter(QObject* watched, QEvent* event)
{
    // Handle popup close event to sync button state
    if (watched == m_descriptionPopup && event->type() == QEvent::Hide) {
        // Only emit if popup was closed by clicking outside (not by confirm/cancel buttons)
        // This prevents double signal emission
        if (!m_popupClosedByButton) {
            // Popup was closed by user clicking outside → auto-confirm
            QString newDescription = m_descriptionEdit->text().trimmed();
            emit linkObjectDescriptionChanged(newDescription);
        }
        m_popupClosedByButton = false;  // Reset flag for next time

        // Uncheck the button
        m_descriptionButton->blockSignals(true);
        m_descriptionButton->setChecked(false);
        m_descriptionButton->blockSignals(false);
    }
    return QWidget::eventFilter(watched, event);
}

void LinkObjectBar::createWidgets()
{
    static_assert(kNumRegionStyles == NUM_REGION_STYLES,
                  "kRegionStyles must have NUM_REGION_STYLES entries");

    m_darkMode = paletteIsDark();
    const bool dark = m_darkMode;

    // One swatch, two meanings. For a highlight it edits the mark's colour,
    // which is the thing the user can see; for a standalone link icon it edits
    // the badge tint, as it always has. setValues() picks which.
    m_colorButton = new ColorPresetButton(this);
    m_colorButton->setColor(QColor(180, 180, 180));
    m_colorButton->setToolTip(tr("LinkObject color (click to edit)"));
    m_layout->addWidget(m_colorButton, 0, Qt::AlignVCenter);

    // Post-hoc restyling of an existing mark. Sits next to the colour swatch so
    // the two appearance controls stay together, and is hidden for standalone
    // link icons, which have no mark to style.
    m_regionStyleButton = new QToolButton(this);
    m_regionStyleButton->setObjectName(QStringLiteral("linkRegionStyle"));
    m_regionStyleButton->setPopupMode(QToolButton::InstantPopup);
    m_regionStyleButton->setToolTip(tr("Highlight style"));
    m_regionStyleButton->setFixedSize(28, 28);
    m_regionStyleButton->setIconSize(QSize(20, 20));

    m_regionStyleMenu = new QMenu(m_regionStyleButton);
    m_regionStyleActions[0] = m_regionStyleMenu->addAction(tr("Cover text"));
    m_regionStyleActions[1] = m_regionStyleMenu->addAction(tr("Underline"));
    m_regionStyleActions[2] = m_regionStyleMenu->addAction(tr("Dotted underline"));
    for (int i = 0; i < NUM_REGION_STYLES; ++i) {
        m_regionStyleActions[i]->setData(static_cast<int>(kRegionStyles[i]));
        m_regionStyleActions[i]->setCheckable(true);
    }
    m_regionStyleButton->setMenu(m_regionStyleMenu);
    applyRegionStyleStyling();
    updateRegionStyleButtonIcon();
    m_regionStyleButton->hide();
    m_layout->addWidget(m_regionStyleButton, 0, Qt::AlignVCenter);

    // One widget serves as both the way into Adjust and the Done button that
    // leaves it, so there is never a Done control with nothing to finish.
    // Hidden until a highlight is selected; a standalone link icon has no text
    // range to re-range.
    m_adjustButton = new SubToolbarToggle(this);
    m_adjustButton->setObjectName(QStringLiteral("linkAdjustToggle"));
    m_adjustButton->setIconName("edit");
    m_adjustButton->setDarkMode(dark);
    m_adjustButton->setToolTip(tr("Adjust highlight range"));
    m_adjustButton->setChecked(false);
    m_adjustButton->hide();
    m_layout->addWidget(m_adjustButton, 0, Qt::AlignVCenter);

    // Description edit button (SubToolbarToggle handles styling)
    m_descriptionButton = new SubToolbarToggle(this);
    m_descriptionButton->setIconName("ibeam");
    m_descriptionButton->setDarkMode(dark);
    m_descriptionButton->setToolTip(tr("Edit LinkObject description"));
    m_descriptionButton->setChecked(false);
    m_layout->addWidget(m_descriptionButton, 0, Qt::AlignVCenter);

    // Slot buttons
    for (int i = 0; i < NUM_SLOTS; ++i) {
        m_slotButtons[i] = new LinkSlotButton(this);
        m_slotButtons[i]->setState(LinkSlotState::Empty);
        // Use icon base names for dark mode switching support
        // Note: LinkSlotButton falls back to text symbols if icons are not set
        m_slotButtons[i]->setStateIconNames("addtab", "link", "url", "markdown");
        m_slotButtons[i]->setDarkMode(dark);
        m_slotButtons[i]->setToolTip(tr("Slot %1").arg(i + 1));
        m_layout->addWidget(m_slotButtons[i], 0, Qt::AlignVCenter);
    }

    // Description popup with text editor and buttons
    m_descriptionPopup = new QWidget(this, Qt::Popup);
    m_descriptionPopup->installEventFilter(this);

    QHBoxLayout* popupLayout = new QHBoxLayout(m_descriptionPopup);
    popupLayout->setContentsMargins(4, 4, 4, 4);
    popupLayout->setSpacing(4);

    m_descriptionEdit = new QLineEdit(m_descriptionPopup);
    m_descriptionEdit->setPlaceholderText(tr("Enter description..."));
    m_descriptionEdit->setFixedWidth(180);
    m_descriptionEdit->setStyleSheet(
        "QLineEdit {"
        "  border-radius: 2px;"
        "  padding: 6px 10px;"
        "  font-size: 13px;"
        "}"
    );
    popupLayout->addWidget(m_descriptionEdit);

    // Confirm button (checkmark)
    m_confirmButton = new QPushButton(m_descriptionPopup);
    m_confirmButton->setIcon(QIcon(QStringLiteral(":/resources/icons/check_reversed.png")));
    m_confirmButton->setIconSize(QSize(14, 14));
    m_confirmButton->setFixedSize(28, 28);
    m_confirmButton->setToolTip(tr("Confirm"));
    m_confirmButton->setStyleSheet(
        "QPushButton { border-radius: 4px; background: #4CAF50; }"
        "QPushButton:hover { background: #45a049; }"
    );
    popupLayout->addWidget(m_confirmButton);

    // Cancel button (X)
    m_cancelButton = new QPushButton(m_descriptionPopup);
    m_cancelButton->setIcon(QIcon(QStringLiteral(":/resources/icons/cross_reversed.png")));
    m_cancelButton->setIconSize(QSize(14, 14));
    m_cancelButton->setFixedSize(28, 28);
    m_cancelButton->setToolTip(tr("Cancel"));
    m_cancelButton->setStyleSheet(
        "QPushButton { border-radius: 4px; background: #f44336; }"
        "QPushButton:hover { background: #da190b; }"
    );
    popupLayout->addWidget(m_cancelButton);
}

void LinkObjectBar::setupConnections()
{
    // Color button connections
    connect(m_colorButton, &ColorPresetButton::clicked,
            this, &LinkObjectBar::onColorButtonClicked);
    connect(m_colorButton, &ColorPresetButton::editRequested,
            this, &LinkObjectBar::onColorButtonEditRequested);

    connect(m_regionStyleMenu, &QMenu::triggered,
            this, &LinkObjectBar::onRegionStyleTriggered);

    connect(m_adjustButton, &SubToolbarToggle::toggled,
            this, [this](bool checked) { emit adjustToggled(checked); });

    // Description button/editor connections
    connect(m_descriptionButton, &SubToolbarToggle::toggled,
            this, &LinkObjectBar::onDescriptionButtonToggled);
    connect(m_confirmButton, &QPushButton::clicked,
            this, &LinkObjectBar::onDescriptionConfirm);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &LinkObjectBar::onDescriptionCancel);
    connect(m_descriptionEdit, &QLineEdit::returnPressed,
            this, &LinkObjectBar::onDescriptionConfirm);

    // Slot button connections
    for (int i = 0; i < NUM_SLOTS; ++i) {
        connect(m_slotButtons[i], &LinkSlotButton::clicked, this, [this, i]() {
            onSlotClicked(i);
        });
        connect(m_slotButtons[i], &LinkSlotButton::deleteRequested, this, [this, i]() {
            onSlotDeleteRequested(i);
        });
    }
}

void LinkObjectBar::setValues(const LinkSlotState states[NUM_SLOTS],
                              const QColor& iconColor,
                              const QString& description,
                              bool regionAdjustable,
                              bool adjusting,
                              bool hasRegion,
                              const QColor& regionColor,
                              int regionStyle)
{
    for (int i = 0; i < NUM_SLOTS; ++i) {
        m_slotButtons[i]->setState(states[i]);
    }

    m_hasRegion = hasRegion;

    if (m_colorButton) {
        if (hasRegion) {
            // Shown and edited opaque even though the mark is stored at
            // DEFAULT_OPACITY: a 50%-alpha swatch reads as muddy against the
            // bar, and the viewport re-applies the alpha on the way back in.
            QColor swatch = regionColor;
            if (swatch.isValid())
                swatch.setAlpha(255);
            m_colorButton->setColor(swatch);
            m_colorButton->setToolTip(tr("Highlight color (click to edit)"));
        } else {
            m_colorButton->setColor(iconColor);
            m_colorButton->setToolTip(tr("LinkObject color (click to edit)"));
        }
        m_colorButton->setSelected(true);  // Always selected for immediate edit
    }

    if (m_regionStyleButton) {
        m_regionStyleButton->setVisible(hasRegion);
        m_regionStyle = regionStyle;
        updateRegionStyleButtonIcon();
    }

    if (m_adjustButton) {
        m_adjustButton->setVisible(regionAdjustable);
        // Blocked: this is the viewport reporting the session state back, so
        // reacting to it would re-enter the toggle handler.
        m_adjustButton->blockSignals(true);
        m_adjustButton->setChecked(adjusting);
        m_adjustButton->blockSignals(false);
        m_adjustButton->setToolTip(adjusting ? tr("Done adjusting")
                                             : tr("Adjust highlight range"));
    }

    // setValues() also runs whenever the selected LinkObject's slots change,
    // which can happen while the popup is open. Re-seeding the editor then would
    // throw away whatever the user has typed so far.
    const bool editing = m_descriptionPopup && m_descriptionPopup->isVisible();
    if (m_descriptionEdit && !editing) {
        m_descriptionEdit->setText(description);
    }
}

void LinkObjectBar::onSlotClicked(int index)
{
    if (index < 0 || index >= NUM_SLOTS) return;

    emit slotActivated(index);
}

void LinkObjectBar::onSlotDeleteRequested(int index)
{
    if (index < 0 || index >= NUM_SLOTS) return;

    // Only process if slot is not empty
    if (m_slotButtons[index]->state() == LinkSlotState::Empty) {
        return;
    }

    if (confirmSlotDelete(index)) {
        emit slotCleared(index);
    }
}

bool LinkObjectBar::confirmSlotDelete(int index)
{
    QString slotName;
    switch (m_slotButtons[index]->state()) {
        case LinkSlotState::Position:
            slotName = tr("Position link");
            break;
        case LinkSlotState::Url:
            slotName = tr("URL link");
            break;
        case LinkSlotState::Markdown:
            slotName = tr("Markdown link");
            break;
        default:
            return false;  // Empty slot, nothing to delete
    }

    QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Clear Slot"),
        tr("Clear the %1 from slot %2?").arg(slotName).arg(index + 1),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    return result == QMessageBox::Yes;
}

void LinkObjectBar::setDarkMode(bool darkMode)
{
    m_darkMode = darkMode;
    applyRegionStyleStyling();
    updateRegionStyleButtonIcon();

    if (m_adjustButton) {
        m_adjustButton->setDarkMode(darkMode);
    }
    if (m_descriptionButton) {
        m_descriptionButton->setDarkMode(darkMode);
    }
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (m_slotButtons[i]) {
            m_slotButtons[i]->setDarkMode(darkMode);
        }
    }

    // The bar floats over the canvas, so unlike a toolbar-embedded subtoolbar it
    // has to paint its own panel background.
    const QString background = darkMode
        ? QStringLiteral("#303030") : QStringLiteral("#f7f7f7");
    const QString border = darkMode
        ? QStringLiteral("#5b5b5b") : QStringLiteral("#bdbdbd");
    setStyleSheet(QStringLiteral(
        "#linkObjectBar { background: %1; border: 1px solid %2;"
        " border-radius: 7px; }")
        .arg(background, border));
}

void LinkObjectBar::closePopups(bool acceptPreview)
{
    if (m_descriptionPopup && m_descriptionPopup->isVisible()) {
        if (acceptPreview) {
            onDescriptionConfirm();
        } else {
            onDescriptionCancel();
        }
    }
    // The style menu has no preview to accept or discard; dismissing it is the
    // whole of "close". Left open it would hang over a stale selection.
    if (m_regionStyleMenu && m_regionStyleMenu->isVisible()) {
        m_regionStyleMenu->close();
    }
}

bool LinkObjectBar::hasOpenPopup() const
{
    return m_colorDialogOpen
        || (m_descriptionPopup && m_descriptionPopup->isVisible())
        || (m_regionStyleMenu && m_regionStyleMenu->isVisible());
}

bool LinkObjectBar::controlHasFocus() const
{
    if (hasOpenPopup())
        return true;
    QWidget* focused = QApplication::focusWidget();
    return focused && (focused == this || isAncestorOf(focused));
}

void LinkObjectBar::mousePressEvent(QMouseEvent* event) { event->accept(); }

void LinkObjectBar::mouseReleaseEvent(QMouseEvent* event) { event->accept(); }

void LinkObjectBar::mouseDoubleClickEvent(QMouseEvent* event) { event->accept(); }

void LinkObjectBar::wheelEvent(QWheelEvent* event) { event->accept(); }

void LinkObjectBar::onColorButtonClicked()
{
    // Since button is always selected when enabled, clicked() is followed by editRequested()
    // Nothing to do here - editRequested will handle opening the dialog
}

void LinkObjectBar::onColorButtonEditRequested()
{
    const QColor currentColor = m_colorButton->color();
    m_colorDialogOpen = true;
    // A highlight's alpha is fixed at HighlightRegion::DEFAULT_OPACITY, so the
    // channel is hidden rather than offered: a fully opaque Cover mark would
    // hide the very text it marks.
    const QColorDialog::ColorDialogOptions options =
        m_hasRegion ? QColorDialog::ColorDialogOptions()
                    : QColorDialog::ShowAlphaChannel;
    const QColor newColor = QColorDialog::getColor(
        currentColor,
        this,
        m_hasRegion ? tr("Select Highlight Color") : tr("Select LinkObject Color"),
        options
    );
    m_colorDialogOpen = false;

    if (newColor.isValid() && newColor != currentColor) {
        m_colorButton->setColor(newColor);
        if (m_hasRegion) {
            emit regionColorChanged(newColor);
        } else {
            emit linkObjectColorChanged(newColor);
        }
    }
}

void LinkObjectBar::onRegionStyleTriggered(QAction* action)
{
    if (!action) return;
    const int style = action->data().toInt();
    if (style == m_regionStyle) {
        // Already active; re-sync the check state in case it drifted.
        updateRegionStyleButtonIcon();
        return;
    }
    m_regionStyle = style;
    updateRegionStyleButtonIcon();
    emit regionStyleChanged(style);
}

void LinkObjectBar::updateRegionStyleButtonIcon()
{
    if (!m_regionStyleButton) return;

    // A stored Style::None has no entry: nothing shows checked, and picking any
    // style repairs the mark.
    const int active = entryForRegionStyle(m_regionStyle);
    m_regionStyleButton->setIcon(
        loadRegionStyleIcon(active >= 0 ? active : 0, m_darkMode));

    for (int i = 0; i < NUM_REGION_STYLES; ++i) {
        if (m_regionStyleActions[i])
            m_regionStyleActions[i]->setChecked(i == active);
    }
}

void LinkObjectBar::applyRegionStyleStyling()
{
    if (!m_regionStyleButton) return;

    const bool dark = m_darkMode;

    for (int i = 0; i < NUM_REGION_STYLES; ++i) {
        if (m_regionStyleActions[i])
            m_regionStyleActions[i]->setIcon(loadRegionStyleIcon(i, dark));
    }

    // setStyleSheet() forces a full unpolish/polish, so this only runs on a
    // theme change, never on a per-click style change.
    const QString btnBg  = dark ? QStringLiteral("#303030") : QStringLiteral("#f7f7f7");
    const QString btnHov = dark ? QStringLiteral("#4a4a4a") : QStringLiteral("#e0e0e0");
    m_regionStyleButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; border: none; border-radius: 4px; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; }"
    ).arg(btnBg, btnHov));

    if (m_regionStyleMenu) {
        const QString menuBg      = dark ? QStringLiteral("#1a1a1a") : QStringLiteral("#ffffff");
        const QString menuFg      = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#1a1a1a");
        const QString menuHoverBg = dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
        const QString menuBdr     = dark ? QStringLiteral("#444")    : QStringLiteral("#ccc");
        m_regionStyleMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: %1; color: %2; border: 1px solid %3; }"
            "QMenu::item:selected { background: %4; }"
        ).arg(menuBg, menuFg, menuBdr, menuHoverBg));
    }
}

void LinkObjectBar::onDescriptionButtonToggled(bool checked)
{
    if (checked) {
        // Store original description for cancel
        m_originalDescription = m_descriptionEdit->text();

        // Position popup below the button
        QPoint buttonPos = m_descriptionButton->mapToGlobal(QPoint(0, m_descriptionButton->height() + 4));
        m_descriptionPopup->move(buttonPos);
        m_descriptionPopup->show();
        m_descriptionEdit->setFocus();
        m_descriptionEdit->selectAll();
    } else {
        m_descriptionPopup->hide();
    }
}

void LinkObjectBar::onDescriptionConfirm()
{
    // Save the description and close popup
    QString newDescription = m_descriptionEdit->text().trimmed();
    emit linkObjectDescriptionChanged(newDescription);

    // Set flag to prevent eventFilter from emitting again on Hide event
    m_popupClosedByButton = true;

    // Close popup and uncheck button
    m_descriptionPopup->hide();
    m_descriptionButton->blockSignals(true);
    m_descriptionButton->setChecked(false);
    m_descriptionButton->blockSignals(false);
}

void LinkObjectBar::onDescriptionCancel()
{
    // Restore original description and close popup
    m_descriptionEdit->setText(m_originalDescription);

    // Set flag to prevent eventFilter from emitting on Hide event (cancel = no changes)
    m_popupClosedByButton = true;

    // Close popup and uncheck button (no emit - original value restored)
    m_descriptionPopup->hide();
    m_descriptionButton->blockSignals(true);
    m_descriptionButton->setChecked(false);
    m_descriptionButton->blockSignals(false);
}
