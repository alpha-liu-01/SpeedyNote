#include "LassoSubToolbar.h"

#include <QSettings>

// Static member definitions
const QString LassoSubToolbar::SETTINGS_GROUP = "lasso";
const QString LassoSubToolbar::KEY_LASSO_MODE = "mode";

LassoSubToolbar::LassoSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void LassoSubToolbar::createWidgets()
{
    bool dark = isDarkMode();

    // Mode toggle (Freeform / Rectangle)
    m_modeToggle = new ModeToggleButton(this);
    m_modeToggle->setModeIconNames("rope", "borders");
    m_modeToggle->setDarkMode(dark);
    m_modeToggle->setModeToolTips(
        tr("Freeform lasso (click to switch to Rectangle)"),
        tr("Rectangle marquee (click to switch to Freeform)")
    );
    addWidget(m_modeToggle);
}

void LassoSubToolbar::setupConnections()
{
    connect(m_modeToggle, &ModeToggleButton::modeChanged, this, [this](int mode) {
        m_lassoModeIndex = mode;
        saveToSettings();
        emit lassoModeChanged(mode);
    });
}

void LassoSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    m_lassoModeIndex = qBound(0, settings.value(KEY_LASSO_MODE, 0).toInt(), 1);
    settings.endGroup();

    m_modeToggle->blockSignals(true);
    m_modeToggle->setCurrentMode(m_lassoModeIndex);
    m_modeToggle->blockSignals(false);
}

void LassoSubToolbar::saveToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(KEY_LASSO_MODE, m_lassoModeIndex);
    settings.endGroup();
}

void LassoSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void LassoSubToolbar::emitCurrentMode()
{
    emit lassoModeChanged(m_lassoModeIndex);
}

int LassoSubToolbar::currentModeIndex() const
{
    return m_lassoModeIndex;
}

void LassoSubToolbar::setModeState(int mode)
{
    m_lassoModeIndex = qBound(0, mode, 1);
    m_modeToggle->blockSignals(true);
    m_modeToggle->setCurrentMode(m_lassoModeIndex);
    m_modeToggle->blockSignals(false);
}

// The lasso mode is a global preference, not per-tab state: switching tabs
// keeps whichever mode the user last chose.
void LassoSubToolbar::restoreTabState(int tabIndex)
{
    Q_UNUSED(tabIndex);
}

void LassoSubToolbar::saveTabState(int tabIndex)
{
    Q_UNUSED(tabIndex);
}

void LassoSubToolbar::clearTabState(int tabIndex)
{
    Q_UNUSED(tabIndex);
}
