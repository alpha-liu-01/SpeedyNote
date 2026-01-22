#include "ToolbarButtons.h"
#include <QMouseEvent>
#include <QFile>
#include <QTextStream>
#include <QEvent>

// ============================================================================
// ButtonStyles
// ============================================================================

void ButtonStyles::applyToWidget(QWidget *widget, bool darkMode)
{
    if (!widget) return;
    widget->setStyleSheet(getStylesheet(darkMode));
}

QString ButtonStyles::getStylesheet(bool darkMode)
{
    QString path = darkMode 
        ? ":/resources/styles/buttons_dark.qss"
        : ":/resources/styles/buttons.qss";
    return loadFromResource(path);
}

QString ButtonStyles::loadFromResource(const QString &path)
{
    QFile file(path);
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&file);
        return stream.readAll();
    }
    return QString();
}

// ============================================================================
// ToolbarButton (Base Class)
// ============================================================================

ToolbarButton::ToolbarButton(QWidget *parent)
    : QPushButton(parent)
{
    // Fixed 36x36 logical pixel size
    setFixedSize(36, 36);
    
    // Remove default button styling for custom appearance
    setFlat(true);
    
    // Ensure focus doesn't steal from main content
    setFocusPolicy(Qt::NoFocus);
}

void ToolbarButton::setThemedIcon(const QString &baseName)
{
    m_iconBaseName = baseName;
    updateIcon();
}

void ToolbarButton::setDarkMode(bool darkMode)
{
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        updateIcon();
    }
}

void ToolbarButton::updateIcon()
{
    if (m_iconBaseName.isEmpty()) {
        return;
    }
    
    QString path = m_darkMode
        ? QString(":/resources/icons/%1_reversed.png").arg(m_iconBaseName)
        : QString(":/resources/icons/%1.png").arg(m_iconBaseName);
    
    setIcon(QIcon(path));
    setIconSize(QSize(24, 24)); // Icon slightly smaller than button for padding
}

bool ToolbarButton::event(QEvent *event)
{
    // Fix for stuck pressed/hover state when menu/popup steals focus:
    // When the window loses activation or the button is hidden,
    // force a repaint to clear any stuck visual state.
    switch (event->type()) {
        case QEvent::WindowDeactivate:
        case QEvent::Hide:
        case QEvent::Leave:
            // Force Qt to re-evaluate button state and repaint
            // This clears stuck :pressed or :hover states
            setAttribute(Qt::WA_UnderMouse, false);
            update();
            break;
        default:
            break;
    }
    
    return QPushButton::event(event);
}

// ============================================================================
// ActionButton
// ============================================================================

ActionButton::ActionButton(QWidget *parent)
    : ToolbarButton(parent)
{
    // Action buttons are not checkable - instant action only
    setCheckable(false);
    
    // Set object name for QSS styling
    setObjectName("ActionButton");
}

// ============================================================================
// ToggleButton
// ============================================================================

ToggleButton::ToggleButton(QWidget *parent)
    : ToolbarButton(parent)
{
    // Toggle buttons maintain on/off state
    setCheckable(true);
    
    // Set object name for QSS styling
    setObjectName("ToggleButton");
}

// ============================================================================
// ThreeStateButton
// ============================================================================

ThreeStateButton::ThreeStateButton(QWidget *parent)
    : ToolbarButton(parent)
{
    // Not using Qt's checkable - we manage state ourselves
    setCheckable(false);
    
    // Set object name for QSS styling
    setObjectName("ThreeStateButton");
    
    // Handle click signal to cycle states
    // (mousePressEvent is not called by QPushButton::click())
    connect(this, &QPushButton::clicked, this, [this]() {
        setState((m_state + 1) % 3);
    });
}

void ThreeStateButton::setState(int state)
{
    // Clamp to valid range
    state = qBound(0, state, 2);
    
    if (m_state != state) {
        m_state = state;
        updateIcon();
        
        // Update style (QSS can use [state="0"], [state="1"], [state="2"])
        style()->unpolish(this);
        style()->polish(this);
        
        emit stateChanged(m_state);
    }
}

void ThreeStateButton::setStateIcons(const QString &baseName0,
                                      const QString &baseName1,
                                      const QString &baseName2)
{
    m_stateIconBaseNames[0] = baseName0;
    m_stateIconBaseNames[1] = baseName1;
    m_stateIconBaseNames[2] = baseName2;
    updateIcon();
}

// Note: State cycling is handled via clicked() signal connection in constructor
// This allows both real clicks and programmatic click() to work

void ThreeStateButton::updateIcon()
{
    // If state icons are set, use them
    if (!m_stateIconBaseNames[m_state].isEmpty()) {
        QString baseName = m_stateIconBaseNames[m_state];
        QString path = m_darkMode
            ? QString(":/resources/icons/%1_reversed.png").arg(baseName)
            : QString(":/resources/icons/%1.png").arg(baseName);
        
        setIcon(QIcon(path));
        setIconSize(QSize(24, 24));
    } else {
        // Fall back to base class behavior
        ToolbarButton::updateIcon();
    }
}

// ============================================================================
// ToolButton
// ============================================================================

ToolButton::ToolButton(QWidget *parent)
    : ToggleButton(parent)
{
    // Set object name for QSS styling (override ToggleButton's)
    setObjectName("ToolButton");
}

