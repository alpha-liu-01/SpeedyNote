#include "HighlighterSubToolbar.h"
#include "../widgets/ColorPresetButton.h"
#include "../widgets/ModeToggleButton.h"

#include <QAction>
#include <QColorDialog>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QToolButton>

// Static member definitions
// Default colors (same as Marker - shared via QSettings)
const QColor HighlighterSubToolbar::DEFAULT_COLORS[NUM_PRESETS] = {
    QColor(0xFF, 0xAA, 0xAA),  // Light red/pink
    QColor(0xFF, 0xFF, 0x00),  // Yellow
    QColor(0xAA, 0xAA, 0xFF)   // Light blue
};

// Shared color settings (used by both Marker and Highlighter)
const QString HighlighterSubToolbar::SETTINGS_GROUP_SHARED_COLORS = "marker";  // Colors stored under "marker" group
const QString HighlighterSubToolbar::SETTINGS_GROUP_HIGHLIGHTER = "highlighter";
const QString HighlighterSubToolbar::KEY_COLOR_PREFIX = "color";
const QString HighlighterSubToolbar::KEY_SELECTED_COLOR = "selectedColor";
const QString HighlighterSubToolbar::KEY_AUTO_HIGHLIGHT = "autoHighlight";
const QString HighlighterSubToolbar::KEY_SELECTION_SOURCE = "selectionSource";

HighlighterSubToolbar::HighlighterSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void HighlighterSubToolbar::createWidgets()
{
    // Create color preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i] = new ColorPresetButton(this);
        m_colorButtons[i]->setColor(DEFAULT_COLORS[i]);
        m_colorButtons[i]->setToolTip(tr("Color preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_colorButtons[i]);
    }
    
    // Add separator before the auto-highlight dropdown
    addSeparator();

    // Auto-highlight style dropdown (None / Cover / Underline / Dotted underline)
    m_autoHighlightButton = new QToolButton(this);
    m_autoHighlightButton->setPopupMode(QToolButton::InstantPopup);
    m_autoHighlightButton->setToolTip(tr("Auto-highlight style"));
    m_autoHighlightButton->setFixedSize(28, 28);
    m_autoHighlightButton->setIconSize(QSize(20, 20));

    m_autoHighlightMenu = new QMenu(m_autoHighlightButton);
    m_styleActions[static_cast<int>(HighlightStyle::None)]            = m_autoHighlightMenu->addAction(tr("None"));
    m_styleActions[static_cast<int>(HighlightStyle::Cover)]           = m_autoHighlightMenu->addAction(tr("Cover text"));
    m_styleActions[static_cast<int>(HighlightStyle::Underline)]       = m_autoHighlightMenu->addAction(tr("Underline"));
    m_styleActions[static_cast<int>(HighlightStyle::DottedUnderline)] = m_autoHighlightMenu->addAction(tr("Dotted underline"));
    for (int i = 0; i < kNumStyles; ++i) {
        m_styleActions[i]->setData(i);
        m_styleActions[i]->setCheckable(true);
    }
    m_autoHighlightButton->setMenu(m_autoHighlightMenu);
    // Apply dark/light stylesheet + menu-item icons once; per-click refreshes
    // only touch the trigger icon + check state.
    applyAutoHighlightStyling();
    updateAutoHighlightButtonIcon();
    addWidget(m_autoHighlightButton);

    // Separator + selection source toggle (PDF text ↔ OCR text)
    addSeparator();
    m_selectionSourceToggle = new ModeToggleButton(this);
    m_selectionSourceToggle->setModeIconNames("selection_pdf", "selection_ocr");
    m_selectionSourceToggle->setDarkMode(isDarkMode());
    m_selectionSourceToggle->setModeToolTips(
        tr("PDF text selection (click to switch to OCR text)"),
        tr("OCR text selection (click to switch to PDF text)")
    );
    addWidget(m_selectionSourceToggle);
}

void HighlighterSubToolbar::setupConnections()
{
    // Color button connections
    for (int i = 0; i < NUM_PRESETS; ++i) {
        connect(m_colorButtons[i], &ColorPresetButton::clicked, this, [this, i]() {
            onColorPresetClicked(i);
        });
        connect(m_colorButtons[i], &ColorPresetButton::editRequested, this, [this, i]() {
            onColorEditRequested(i);
        });
    }
    
    // Auto-highlight style dropdown connection
    connect(m_autoHighlightMenu, &QMenu::triggered,
            this, &HighlighterSubToolbar::onAutoHighlightStyleTriggered);

    // Selection-source toggle connection (PDF vs OCR)
    connect(m_selectionSourceToggle, &ModeToggleButton::modeChanged,
            this, &HighlighterSubToolbar::onSelectionSourceToggled);
}

void HighlighterSubToolbar::loadFromSettings()
{
    QSettings settings;
    
    // Load SHARED colors (from marker group, shared with Marker)
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    settings.endGroup();
    
    // Load highlighter-specific settings
    settings.beginGroup(SETTINGS_GROUP_HIGHLIGHTER);
    
    // Load selection
    m_selectedColorIndex = settings.value(KEY_SELECTED_COLOR, 0).toInt();

    // Load auto-highlight style. Stored as int in [0..kNumStyles-1]. Backward-compat:
    // an older `bool true` value is read via toInt() as 1 => Cover, `false` => None.
    const int styleInt = qBound(0, settings.value(KEY_AUTO_HIGHLIGHT, 0).toInt(),
                                kNumStyles - 1);
    m_autoHighlightStyle = static_cast<HighlightStyle>(styleInt);

    // Load selection source (PDF vs OCR)
    int srcInt = qBound(0, settings.value(KEY_SELECTION_SOURCE, 0).toInt(), 1);
    m_selectionSource = static_cast<SelectionSource>(srcInt);
    
    settings.endGroup();
    
    // Apply settings
    selectColorPreset(m_selectedColorIndex);
    updateAutoHighlightButtonIcon();
    if (m_selectionSourceToggle) {
        m_selectionSourceToggle->blockSignals(true);
        m_selectionSourceToggle->setCurrentMode(static_cast<int>(m_selectionSource));
        m_selectionSourceToggle->blockSignals(false);
    }
}

void HighlighterSubToolbar::saveColorsToSettings()
{
    // Save SHARED colors (affects Marker too)
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        settings.setValue(key, m_colorButtons[i]->color());
    }
    
    settings.endGroup();
}

void HighlighterSubToolbar::saveAutoHighlightToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_HIGHLIGHTER);
    settings.setValue(KEY_SELECTED_COLOR, m_selectedColorIndex);
    settings.setValue(KEY_AUTO_HIGHLIGHT, static_cast<int>(m_autoHighlightStyle));
    settings.endGroup();
}

void HighlighterSubToolbar::saveSelectionSourceToSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_HIGHLIGHTER);
    settings.setValue(KEY_SELECTION_SOURCE, static_cast<int>(m_selectionSource));
    settings.endGroup();
}

void HighlighterSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void HighlighterSubToolbar::syncSharedState()
{
    // Sync shared colors when switching to this subtoolbar
    syncSharedColorsFromSettings();
}

void HighlighterSubToolbar::syncSharedColorsFromSettings()
{
    // Reload ONLY shared colors from QSettings
    // This preserves per-tab selection state while syncing with Marker edits
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP_SHARED_COLORS);
    
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = KEY_COLOR_PREFIX + QString::number(i + 1);
        QColor color = settings.value(key, DEFAULT_COLORS[i]).value<QColor>();
        m_colorButtons[i]->setColor(color);
    }
    
    settings.endGroup();
}

void HighlighterSubToolbar::restoreTabState(int tabIndex)
{
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Restore colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setColor(state.colors[i]);
    }
    
    // Restore selection
    selectColorPreset(state.selectedColorIndex);
    
    // NOTE: Auto-highlight style is NOT restored here.
    // DocumentViewport is the source of truth for auto-highlight style (per-viewport).
    // The dropdown is synced from viewport via setAutoHighlightStyle() in MainWindow.
}

void HighlighterSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    
    // Save colors
    for (int i = 0; i < NUM_PRESETS; ++i) {
        state.colors[i] = m_colorButtons[i]->color();
    }
    
    // Save selection
    state.selectedColorIndex = m_selectedColorIndex;
    
    // NOTE: Auto-highlight state is NOT saved here.
    // DocumentViewport stores auto-highlight state per-viewport (per-tab).
    // We don't duplicate that state in the subtoolbar.
    state.initialized = true;
}

void HighlighterSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void HighlighterSubToolbar::onColorPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Always apply the color when clicked - the preset might show as "selected"
    // but the actual current color could be different (changed via other means)
    selectColorPreset(index);
    
    // Emit color change with marker opacity applied
    QColor colorWithOpacity = m_colorButtons[index]->color();
    colorWithOpacity.setAlpha(MARKER_OPACITY);
    emit highlighterColorChanged(colorWithOpacity);
}

void HighlighterSubToolbar::onColorEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open color dialog
    QColor currentColor = m_colorButtons[index]->color();
    QColor newColor = QColorDialog::getColor(currentColor, this, tr("Select Highlighter Color"));
    
    if (newColor.isValid() && newColor != currentColor) {
        m_colorButtons[index]->setColor(newColor);
        saveColorsToSettings();  // Persist change (SHARED with Marker)
        
        // If this is the selected preset, emit change with marker opacity
        if (m_selectedColorIndex == index) {
            QColor colorWithOpacity = newColor;
            colorWithOpacity.setAlpha(MARKER_OPACITY);
            emit highlighterColorChanged(colorWithOpacity);
        }
    }
}

void HighlighterSubToolbar::onAutoHighlightStyleTriggered(QAction* action)
{
    if (!action) return;
    const int v = qBound(0, action->data().toInt(), kNumStyles - 1);
    const HighlightStyle s = static_cast<HighlightStyle>(v);
    if (s == m_autoHighlightStyle) {
        // Already active; just re-sync the check state in case it drifted.
        updateAutoHighlightButtonIcon();
        return;
    }
    m_autoHighlightStyle = s;
    saveAutoHighlightToSettings();
    updateAutoHighlightButtonIcon();
    emit autoHighlightStyleChanged(s);
}

// Resource base name of the icon for each HighlightStyle, indexed by enum value.
// Kept at file scope so it's shared by both the cheap per-click refresh and the
// one-shot restyling helper.
namespace {
constexpr const char* kHighlightStyleIconBases[] = {
    "highlight_none",       // HighlightStyle::None
    "marker",               // HighlightStyle::Cover (reuses existing marker icon)
    "highlight_underline",  // HighlightStyle::Underline
    "highlight_dotted",     // HighlightStyle::DottedUnderline
};
static_assert(sizeof(kHighlightStyleIconBases) / sizeof(kHighlightStyleIconBases[0])
                  == HighlighterSubToolbar::kNumStyles,
              "kHighlightStyleIconBases must have kNumStyles entries");

inline QIcon loadHighlightIcon(int styleIndex, bool dark) {
    const QLatin1String base(kHighlightStyleIconBases[styleIndex]);
    return QIcon(dark
        ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(base)
        : QStringLiteral(":/resources/icons/%1.png").arg(base));
}
}  // namespace

void HighlighterSubToolbar::updateAutoHighlightButtonIcon()
{
    if (!m_autoHighlightButton) return;

    // Cheap path: only the trigger-button icon and per-action check state
    // depend on the currently-selected style, so that's all we refresh here.
    // The dark/light stylesheets and per-action icons live in applyAutoHighlightStyling()
    // and are rebuilt only when the theme changes.
    const int activeIdx = static_cast<int>(m_autoHighlightStyle);
    m_autoHighlightButton->setIcon(loadHighlightIcon(activeIdx, isDarkMode()));

    for (int i = 0; i < kNumStyles; ++i) {
        if (m_styleActions[i])
            m_styleActions[i]->setChecked(i == activeIdx);
    }
}

void HighlighterSubToolbar::applyAutoHighlightStyling()
{
    if (!m_autoHighlightButton) return;

    const bool dark = isDarkMode();

    // Menu-item icons (matches ObjectSelectSubToolbar's dropdown UX).
    for (int i = 0; i < kNumStyles; ++i) {
        if (m_styleActions[i])
            m_styleActions[i]->setIcon(loadHighlightIcon(i, dark));
    }

    // Style button to match the subtoolbar (black bg in dark, white bg in light).
    // setStyleSheet() triggers a full unpolish/polish, so we only do this here,
    // not on every menu click.
    const QString btnBg  = dark ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
    const QString btnHov = dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
    m_autoHighlightButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; border: none; border-radius: 4px; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton::menu-indicator { image: none; }"
    ).arg(btnBg, btnHov));

    // Style menu to match the subtoolbar background.
    if (m_autoHighlightMenu) {
        const QString menuBg      = dark ? QStringLiteral("#1a1a1a") : QStringLiteral("#ffffff");
        const QString menuFg      = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#1a1a1a");
        const QString menuHoverBg = dark ? QStringLiteral("#333333") : QStringLiteral("#e0e0e0");
        const QString menuBdr     = dark ? QStringLiteral("#444")    : QStringLiteral("#ccc");
        m_autoHighlightMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: %1; color: %2; border: 1px solid %3; }"
            "QMenu::item:selected { background: %4; }"
        ).arg(menuBg, menuFg, menuBdr, menuHoverBg));
    }
}

void HighlighterSubToolbar::onSelectionSourceToggled(int mode)
{
    SelectionSource src = (mode == 1) ? SelectionSource::Ocr : SelectionSource::Pdf;
    if (src == m_selectionSource) {
        return;
    }
    m_selectionSource = src;
    saveSelectionSourceToSettings();
    emit selectionSourceChanged(src);
}

void HighlighterSubToolbar::selectColorPreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_colorButtons[i]->setSelected(i == index);
    }
    
    m_selectedColorIndex = index;
}

void HighlighterSubToolbar::setAutoHighlightStyle(HighlightStyle style)
{
    if (m_autoHighlightStyle == style) return;

    // UI-only update: no signal emission (viewport is source of truth; this
    // setter is called when syncing FROM the viewport, so emitting back would
    // create a feedback loop).
    m_autoHighlightStyle = style;
    updateAutoHighlightButtonIcon();
}

void HighlighterSubToolbar::setSelectionSourceState(SelectionSource src)
{
    m_selectionSource = src;
    if (!m_selectionSourceToggle) {
        return;
    }
    m_selectionSourceToggle->blockSignals(true);
    m_selectionSourceToggle->setCurrentMode(static_cast<int>(src));
    m_selectionSourceToggle->blockSignals(false);
}

void HighlighterSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset values to sync with viewport
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS) {
        QColor colorWithOpacity = m_colorButtons[m_selectedColorIndex]->color();
        colorWithOpacity.setAlpha(MARKER_OPACITY);
        emit highlighterColorChanged(colorWithOpacity);
    }
    // Note: auto-highlight style is synced via setAutoHighlightStyle() separately
}

QColor HighlighterSubToolbar::currentColor() const
{
    QColor color;
    if (m_selectedColorIndex >= 0 && m_selectedColorIndex < NUM_PRESETS && m_colorButtons[m_selectedColorIndex]) {
        color = m_colorButtons[m_selectedColorIndex]->color();
    } else {
        color = DEFAULT_COLORS[0];  // Fallback to first default
    }
    color.setAlpha(MARKER_OPACITY);
    return color;
}

void HighlighterSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);

    // Dark mode affects both the button/menu stylesheets and the icon variants,
    // so rebuild everything: first the expensive one-shot styling, then refresh
    // the trigger icon for the currently-selected style.
    applyAutoHighlightStyling();
    updateAutoHighlightButtonIcon();

    if (m_selectionSourceToggle) {
        m_selectionSourceToggle->setDarkMode(darkMode);
    }
}

