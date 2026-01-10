#include "ObjectSelectSubToolbar.h"
#include "../widgets/ModeToggleButton.h"
#include "../widgets/LinkSlotButton.h"

#include <QSettings>
#include <QMessageBox>

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

void ObjectSelectSubToolbar::createWidgets()
{
    bool dark = isDarkMode();
    
    // Create insert mode toggle (Image ↔ Link)
    m_insertModeToggle = new ModeToggleButton(this);
    // Use icon base names for dark mode switching support
    m_insertModeToggle->setModeIconNames("background", "linkicon");  // Mode 0: Image, Mode 1: Link
    m_insertModeToggle->setDarkMode(dark);
    m_insertModeToggle->setModeToolTips(
        tr("Image insert mode (click to switch to Link)"),
        tr("Link insert mode (click to switch to Image)")
    );
    addWidget(m_insertModeToggle);
    
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
    
    // Add separator before slot buttons
    addSeparator();
    
    // Create slot buttons
    for (int i = 0; i < NUM_SLOTS; ++i) {
        m_slotButtons[i] = new LinkSlotButton(this);
        m_slotButtons[i]->setState(LinkSlotState::Empty);
        // Use icon base names for dark mode switching support
        // Note: LinkSlotButton falls back to text symbols if icons are not set
        m_slotButtons[i]->setStateIconNames("addpage", "location", "url", "markdown");
        m_slotButtons[i]->setDarkMode(dark);
        m_slotButtons[i]->setToolTip(tr("Slot %1").arg(i + 1));
        addWidget(m_slotButtons[i]);
    }
}

void ObjectSelectSubToolbar::setupConnections()
{
    // Insert mode toggle
    connect(m_insertModeToggle, &ModeToggleButton::modeChanged, 
            this, &ObjectSelectSubToolbar::onInsertModeToggled);
    
    // Action mode toggle
    connect(m_actionModeToggle, &ModeToggleButton::modeChanged, 
            this, &ObjectSelectSubToolbar::onActionModeToggled);
    
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
    
    // Load insert mode
    int insertModeInt = settings.value(KEY_INSERT_MODE, 0).toInt();
    m_insertMode = static_cast<DocumentViewport::ObjectInsertMode>(insertModeInt);
    m_insertModeToggle->setCurrentMode(insertModeInt);
    
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
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Restore modes
    m_insertMode = state.insertMode;
    m_actionMode = state.actionMode;
    
    m_insertModeToggle->setCurrentMode(static_cast<int>(m_insertMode));
    m_actionModeToggle->setCurrentMode(static_cast<int>(m_actionMode));
}

void ObjectSelectSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    
    state.insertMode = m_insertMode;
    state.actionMode = m_actionMode;
    state.initialized = true;
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

void ObjectSelectSubToolbar::onInsertModeToggled(int mode)
{
    m_insertMode = static_cast<DocumentViewport::ObjectInsertMode>(mode);
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
    // Update internal state
    m_insertMode = mode;
    
    // Block signals to avoid feedback loop (external change shouldn't emit back)
    m_insertModeToggle->blockSignals(true);
    m_insertModeToggle->setCurrentMode(static_cast<int>(mode));
    m_insertModeToggle->blockSignals(false);
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

void ObjectSelectSubToolbar::setDarkMode(bool darkMode)
{
    // Propagate dark mode to mode toggle buttons
    if (m_insertModeToggle) {
        m_insertModeToggle->setDarkMode(darkMode);
    }
    if (m_actionModeToggle) {
        m_actionModeToggle->setDarkMode(darkMode);
    }
    
    // Propagate dark mode to slot buttons
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (m_slotButtons[i]) {
            m_slotButtons[i]->setDarkMode(darkMode);
        }
    }
}

