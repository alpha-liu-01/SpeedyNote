/**
 * @file DialModeToolbar.h
 * @brief Toolbar for switching dial operation modes
 * 
 * This UI component can be excluded on Android builds via ENABLE_DIAL_CONTROLLER CMake option.
 * 
 * MW2.1: Extracted from MainWindow as part of modularization effort.
 */

#ifndef DIALMODETOOLBAR_H
#define DIALMODETOOLBAR_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QMap>

#include "../input/DialTypes.h"  // Shared enum definitions

// Forward declarations
class DialController;

/**
 * @brief Toolbar widget for dial mode selection
 * 
 * Provides buttons/dropdown for switching between dial operation modes:
 * - Tool selection
 * - Zoom control
 * - Pan/scroll
 * - Pen thickness
 * - Presets
 * - Page switching
 */
class DialModeToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit DialModeToolbar(DialController *controller, QWidget *parent = nullptr);
    ~DialModeToolbar();

    // Visibility
    void setToolbarVisible(bool visible);
    bool isToolbarVisible() const { return m_visible; }

    // Current mode display
    void updateModeDisplay(DialMode mode);

    // Theme support
    void updateTheme(bool darkMode);

signals:
    void modeChangeRequested(DialMode mode);
    void visibilityChanged(bool visible);

public slots:
    void onModeChanged(DialMode mode);

private:
    void setupUi();
    void createModeButtons();
    void updateButtonStyles();
    QString getModeIcon(DialMode mode) const;
    QString getModeName(DialMode mode) const;

    DialController *m_controller = nullptr;
    bool m_visible = false;
    bool m_darkMode = false;

    // UI elements
    QHBoxLayout *m_layout = nullptr;
    QLabel *m_modeLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
    
    // Mode buttons (alternative to combo)
    QMap<DialMode, QPushButton*> m_modeButtons;
    
    // Display for current dial state
    QLabel *m_dialDisplay = nullptr;
};

#endif // DIALMODETOOLBAR_H
