#include "ObjectSelectSubToolbar.h"
#include "../widgets/ModeToggleButton.h"
#include "../widgets/LinkSlotButton.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ToggleButton.h"  // Contains SubToolbarToggle

#include <QSettings>
#include <QMessageBox>
#include <QColorDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QIcon>
#include <QHBoxLayout>
#include <QEvent>

// Static member definitions
const QString ObjectSelectSubToolbar::SETTINGS_GROUP = "objectSelect";
const QString ObjectSelectSubToolbar::KEY_INSERT_MODE = "insertMode";
const QString ObjectSelectSubToolbar::KEY_ACTION_MODE = "actionMode";

ObjectSelectSubToolbar::ObjectSelectSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

ObjectSelectSubToolbar::~ObjectSelectSubToolbar()
{
    // Clean up popup widget (has no parent)
    delete m_descriptionPopup;
    m_descriptionPopup = nullptr;
    m_descriptionEdit = nullptr;  // Deleted as child of popup
}

bool ObjectSelectSubToolbar::eventFilter(QObject* watched, QEvent* event)
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
    return SubToolbar::eventFilter(watched, event);
}

void ObjectSelectSubToolbar::createWidgets()
{
    bool dark = isDarkMode();
    
    // Create insert mode dropdown (Image / Link / Text)
    m_insertModeButton = new QToolButton(this);
    m_insertModeButton->setPopupMode(QToolButton::InstantPopup);
    m_insertModeButton->setToolTip(tr("Insert mode"));
    m_insertModeButton->setFixedSize(28, 28);
    m_insertModeButton->setIconSize(QSize(20, 20));

    m_insertModeMenu = new QMenu(m_insertModeButton);
    m_insertImageAction = m_insertModeMenu->addAction(tr("Image"));
    m_insertLinkAction  = m_insertModeMenu->addAction(tr("Link"));
    m_insertTextAction  = m_insertModeMenu->addAction(tr("Text"));
    m_insertModeButton->setMenu(m_insertModeMenu);
    updateInsertModeIcons();
    addWidget(m_insertModeButton);
    
    // Create action mode toggle (Select ↔ Create)
    m_actionModeToggle = new ModeToggleButton(this);
    // Use icon base names for dark mode switching support
    m_actionModeToggle->setModeIconNames("select", "addtab");  // Mode 0: Select, Mode 1: Create
    m_actionModeToggle->setDarkMode(dark);
    m_actionModeToggle->setModeToolTips(
        tr("Select mode (click to switch to Create)"),
        tr("Create mode (click to switch to Select)")
    );
    addWidget(m_actionModeToggle);
    
    // Add separator before LinkObject controls (create manually to track it)
    m_linkObjectSeparator = new QFrame(this);
    m_linkObjectSeparator->setFrameShape(QFrame::VLine);
    m_linkObjectSeparator->setFrameShadow(QFrame::Plain);
    m_linkObjectSeparator->setFixedWidth(2);
    m_linkObjectSeparator->setFixedHeight(SEPARATOR_HEIGHT);
    if (isDarkMode()) {
        m_linkObjectSeparator->setStyleSheet("background-color: #4d4d4d; border: none;");
    } else {
        m_linkObjectSeparator->setStyleSheet("background-color: #D0D0D0; border: none;");
    }
    m_linkObjectSeparator->setVisible(false);
    addWidget(m_linkObjectSeparator);
    
    // Create color button for LinkObject color editing
    m_colorButton = new ColorPresetButton(this);
    m_colorButton->setColor(QColor(180, 180, 180));  // Gray when disabled
    m_colorButton->setEnabled(false);  // Disabled until LinkObject is selected
    m_colorButton->setToolTip(tr("Select a LinkObject to edit color"));
    m_colorButton->setVisible(false);  // Hidden by default
    addWidget(m_colorButton);
    
    // Create description edit button (SubToolbarToggle handles styling)
    m_descriptionButton = new SubToolbarToggle(this);
    m_descriptionButton->setIconName("ibeam");
    m_descriptionButton->setDarkMode(dark);
    m_descriptionButton->setToolTip(tr("Edit LinkObject description"));
    m_descriptionButton->setChecked(false);
    m_descriptionButton->setEnabled(false);  // Disabled until LinkObject is selected
    m_descriptionButton->setVisible(false);  // Hidden by default
    addWidget(m_descriptionButton);
    
    // Create slot buttons
    for (int i = 0; i < NUM_SLOTS; ++i) {
        m_slotButtons[i] = new LinkSlotButton(this);
        m_slotButtons[i]->setState(LinkSlotState::Empty);
        // Use icon base names for dark mode switching support
        // Note: LinkSlotButton falls back to text symbols if icons are not set
        m_slotButtons[i]->setStateIconNames("addtab", "link", "url", "markdown");
        m_slotButtons[i]->setDarkMode(dark);
        m_slotButtons[i]->setToolTip(tr("Slot %1").arg(i + 1));
        m_slotButtons[i]->setVisible(false);  // Hidden by default
        addWidget(m_slotButtons[i]);
    }
    
    // Create description popup with text editor and buttons
    m_descriptionPopup = new QWidget();
    m_descriptionPopup->setWindowFlags(Qt::Popup);
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

void ObjectSelectSubToolbar::setupConnections()
{
    // Insert mode dropdown
    connect(m_insertModeMenu, &QMenu::triggered,
            this, &ObjectSelectSubToolbar::onInsertModeActionTriggered);
    
    // Action mode toggle
    connect(m_actionModeToggle, &ModeToggleButton::modeChanged, 
            this, &ObjectSelectSubToolbar::onActionModeToggled);
    
    // Color button connections
    connect(m_colorButton, &ColorPresetButton::clicked,
            this, &ObjectSelectSubToolbar::onColorButtonClicked);
    connect(m_colorButton, &ColorPresetButton::editRequested,
            this, &ObjectSelectSubToolbar::onColorButtonEditRequested);
    
    // Description button/editor connections
    connect(m_descriptionButton, &SubToolbarToggle::toggled,
            this, &ObjectSelectSubToolbar::onDescriptionButtonToggled);
    connect(m_confirmButton, &QPushButton::clicked,
            this, &ObjectSelectSubToolbar::onDescriptionConfirm);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &ObjectSelectSubToolbar::onDescriptionCancel);
    connect(m_descriptionEdit, &QLineEdit::returnPressed,
            this, &ObjectSelectSubToolbar::onDescriptionConfirm);
    
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

void ObjectSelectSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    // Load insert mode (clamp to valid range 0-2)
    int insertModeInt = qBound(0, settings.value(KEY_INSERT_MODE, 0).toInt(), 2);
    m_insertMode = static_cast<DocumentViewport::ObjectInsertMode>(insertModeInt);
    setInsertModeState(m_insertMode);
    
    // Load action mode
    int actionModeInt = settings.value(KEY_ACTION_MODE, 0).toInt();
    m_actionMode = static_cast<DocumentViewport::ObjectActionMode>(actionModeInt);
    m_actionModeToggle->setCurrentMode(actionModeInt);
    
    settings.endGroup();
}

void ObjectSelectSubToolbar::saveToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    
    settings.setValue(KEY_INSERT_MODE, static_cast<int>(m_insertMode));
    settings.setValue(KEY_ACTION_MODE, static_cast<int>(m_actionMode));
    
    settings.endGroup();
}

void ObjectSelectSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void ObjectSelectSubToolbar::restoreTabState(int tabIndex)
{
    // BUG-STB-002 FIX: Do NOT restore insert/action modes here.
    // The viewport is the source of truth for object modes (each viewport
    // stores its own mode). The subtoolbar is synced FROM the viewport
    // via setInsertModeState()/setActionModeState() in connectViewportScrollSignals().
    // Restoring modes here would conflict with the viewport's actual state.
    Q_UNUSED(tabIndex);
}

void ObjectSelectSubToolbar::saveTabState(int tabIndex)
{
    // BUG-STB-002 FIX: Do NOT save insert/action modes here.
    // The viewport stores modes per-document, not the subtoolbar.
    Q_UNUSED(tabIndex);
}

void ObjectSelectSubToolbar::clearTabState(int tabIndex)
{
    // BUG-STB-002 FIX: No per-tab state to clear (modes come from viewport)
    Q_UNUSED(tabIndex);
}

void ObjectSelectSubToolbar::updateSlotStates(const LinkSlotState states[3])
{
    if (states) {
        for (int i = 0; i < NUM_SLOTS; ++i) {
            m_slotButtons[i]->setState(states[i]);
        }
    } else {
        clearSlotStates();
    }
}

void ObjectSelectSubToolbar::clearSlotStates()
{
    for (int i = 0; i < NUM_SLOTS; ++i) {
        m_slotButtons[i]->setState(LinkSlotState::Empty);
        m_slotButtons[i]->setSelected(false);
    }
}

void ObjectSelectSubToolbar::onInsertModeActionTriggered(QAction* action)
{
    DocumentViewport::ObjectInsertMode mode;
    if (action == m_insertImageAction) mode = DocumentViewport::ObjectInsertMode::Image;
    else if (action == m_insertLinkAction) mode = DocumentViewport::ObjectInsertMode::Link;
    else if (action == m_insertTextAction) mode = DocumentViewport::ObjectInsertMode::Text;
    else return;

    m_insertMode = mode;
    m_insertModeButton->setIcon(action->icon());
    saveToSettings();
    emit insertModeChanged(m_insertMode);
}

void ObjectSelectSubToolbar::onActionModeToggled(int mode)
{
    m_actionMode = static_cast<DocumentViewport::ObjectActionMode>(mode);
    saveToSettings();
    emit actionModeChanged(m_actionMode);
}

void ObjectSelectSubToolbar::onSlotClicked(int index)
{
    if (index < 0 || index >= NUM_SLOTS) return;
    
    emit slotActivated(index);
}

void ObjectSelectSubToolbar::onSlotDeleteRequested(int index)
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

bool ObjectSelectSubToolbar::confirmSlotDelete(int index)
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

void ObjectSelectSubToolbar::setInsertModeState(DocumentViewport::ObjectInsertMode mode)
{
    m_insertMode = mode;
    switch (mode) {
    case DocumentViewport::ObjectInsertMode::Image:
        m_insertModeButton->setIcon(m_insertImageAction->icon());
        break;
    case DocumentViewport::ObjectInsertMode::Link:
        m_insertModeButton->setIcon(m_insertLinkAction->icon());
        break;
    case DocumentViewport::ObjectInsertMode::Text:
        m_insertModeButton->setIcon(m_insertTextAction->icon());
        break;
    }
}

void ObjectSelectSubToolbar::setActionModeState(DocumentViewport::ObjectActionMode mode)
{
    // Update internal state
    m_actionMode = mode;
    
    // Block signals to avoid feedback loop (external change shouldn't emit back)
    m_actionModeToggle->blockSignals(true);
    m_actionModeToggle->setCurrentMode(static_cast<int>(mode));
    m_actionModeToggle->blockSignals(false);
}

void ObjectSelectSubToolbar::updateInsertModeIcons()
{
    bool dark = isDarkMode();
    auto icon = [dark](const char* baseName) {
        QString path = dark
            ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(QLatin1String(baseName))
            : QStringLiteral(":/resources/icons/%1.png").arg(QLatin1String(baseName));
        return QIcon(path);
    };

    m_insertImageAction->setIcon(icon("objectinsert"));
    m_insertLinkAction->setIcon(icon("linkicon"));
    m_insertTextAction->setIcon(icon("text"));

    setInsertModeState(m_insertMode);

    // Style button to match the subtoolbar (black bg in dark, white bg in light)
    QString btnBg  = dark ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
    QString btnHov = dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
    m_insertModeButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; border: none; border-radius: 4px; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; }"
    ).arg(btnBg, btnHov));

    // Style menu to match the subtoolbar background
    QString menuBg = dark ? QStringLiteral("#1a1a1a") : QStringLiteral("#ffffff");
    QString menuFg = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#1a1a1a");
    QString menuHoverBg = dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
    QString menuBdr = dark ? QStringLiteral("#444") : QStringLiteral("#ccc");
    m_insertModeMenu->setStyleSheet(QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item:selected { background: %4; }"
    ).arg(menuBg, menuFg, menuBdr, menuHoverBg));
}

void ObjectSelectSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);

    updateInsertModeIcons();

    if (m_actionModeToggle) {
        m_actionModeToggle->setDarkMode(darkMode);
    }
    if (m_descriptionButton) {
        m_descriptionButton->setDarkMode(darkMode);
    }
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (m_slotButtons[i]) {
            m_slotButtons[i]->setDarkMode(darkMode);
        }
    }
}

void ObjectSelectSubToolbar::setLinkObjectControlsVisible(bool visible)
{
    // Show/hide all LinkObject-specific controls
    if (m_linkObjectSeparator) {
        m_linkObjectSeparator->setVisible(visible);
    }
    if (m_colorButton) {
        m_colorButton->setVisible(visible);
    }
    if (m_descriptionButton) {
        m_descriptionButton->setVisible(visible);
    }
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (m_slotButtons[i]) {
            m_slotButtons[i]->setVisible(visible);
        }
    }
    
    // Force layout update after visibility change
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    updateGeometry();
    adjustSize();
    
    // Notify container that size has changed
    emit contentSizeChanged();
}

void ObjectSelectSubToolbar::setLinkObjectColor(const QColor& color, bool visible)
{
    // Show/hide all LinkObject controls based on visibility
    setLinkObjectControlsVisible(visible);
    
    if (m_colorButton) {
        if (visible) {
            m_colorButton->setColor(color);
            m_colorButton->setEnabled(true);
            m_colorButton->setSelected(true);  // Always selected for immediate edit
            m_colorButton->setToolTip(tr("LinkObject color (click to edit)"));
        } else {
            m_colorButton->setColor(QColor(180, 180, 180));  // Gray when disabled
            m_colorButton->setEnabled(false);
            m_colorButton->setSelected(false);
            m_colorButton->setToolTip(tr("Select a LinkObject to edit color"));
        }
    }
}

void ObjectSelectSubToolbar::onColorButtonClicked()
{
    // Since button is always selected when enabled, clicked() is followed by editRequested()
    // Nothing to do here - editRequested will handle opening the dialog
}

void ObjectSelectSubToolbar::onColorButtonEditRequested()
{
    // Open color dialog immediately (button is always "selected" when enabled)
    QColor currentColor = m_colorButton->color();
    QColor newColor = QColorDialog::getColor(
        currentColor,
        this,
        tr("Select LinkObject Color"),
        QColorDialog::ShowAlphaChannel
    );
    
    if (newColor.isValid() && newColor != currentColor) {
        m_colorButton->setColor(newColor);
        emit linkObjectColorChanged(newColor);
    }
}

void ObjectSelectSubToolbar::setLinkObjectDescription(const QString& description, bool enabled)
{
    if (m_descriptionButton) {
        m_descriptionButton->setEnabled(enabled);
        if (!enabled) {
            m_descriptionButton->setChecked(false);
        }
    }
    if (m_descriptionEdit) {
        m_descriptionEdit->setText(description);
    }
    if (m_descriptionPopup && !enabled) {
        m_descriptionPopup->hide();
    }
}

void ObjectSelectSubToolbar::onDescriptionButtonToggled(bool checked)
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

void ObjectSelectSubToolbar::onDescriptionConfirm()
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

void ObjectSelectSubToolbar::onDescriptionCancel()
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


