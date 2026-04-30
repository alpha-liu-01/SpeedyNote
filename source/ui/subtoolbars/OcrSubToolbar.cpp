#include "OcrSubToolbar.h"

#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QIcon>
#include <QFrame>

OcrSubToolbar::OcrSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    m_darkMode = isDarkMode();
    createWidgets();
    setupConnections();
}

static QPushButton* makeIconButton(QWidget* parent, int size)
{
    auto* btn = new QPushButton(parent);
    btn->setFixedSize(size, size);
    btn->setIconSize(QSize(size - 10, size - 10));
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);
    return btn;
}

void OcrSubToolbar::createWidgets()
{
    m_scanPageButton = makeIconButton(this, BUTTON_SIZE);
    m_scanPageButton->setToolTip(tr("Scan Page"));
    addWidget(m_scanPageButton);

    m_scanAllButton = makeIconButton(this, BUTTON_SIZE);
    m_scanAllButton->setToolTip(tr("Scan All Pages"));
    addWidget(m_scanAllButton);

    m_scanSeparator = addSeparator();

    m_autoOcrButton = makeIconButton(this, BUTTON_SIZE);
    m_autoOcrButton->setCheckable(true);
    m_autoOcrButton->setToolTip(tr("Auto OCR"));
    addWidget(m_autoOcrButton);

    m_showTextButton = makeIconButton(this, BUTTON_SIZE);
    m_showTextButton->setCheckable(true);
    m_showTextButton->setToolTip(tr("Show Recognized Text"));
    addWidget(m_showTextButton);

    m_confidenceButton = makeIconButton(this, BUTTON_SIZE);
    m_confidenceButton->setCheckable(true);
    m_confidenceButton->setToolTip(tr("Show Confidence Colors"));
    addWidget(m_confidenceButton);

    m_snapButton = makeIconButton(this, BUTTON_SIZE);
    m_snapButton->setCheckable(true);
    m_snapButton->setToolTip(tr("Snap OCR to Grid/Lines"));
    addWidget(m_snapButton);

    addSeparator();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    addWidget(m_statusLabel);

    m_statusClearTimer = new QTimer(this);
    m_statusClearTimer->setSingleShot(true);
    connect(m_statusClearTimer, &QTimer::timeout, this, [this]() {
        m_statusLabel->clear();
    });

    updateIcons();
    applyButtonStyle();
}

void OcrSubToolbar::setupConnections()
{
    connect(m_scanPageButton, &QPushButton::clicked, this, &OcrSubToolbar::scanPageClicked);
    connect(m_scanAllButton, &QPushButton::clicked, this, &OcrSubToolbar::scanAllClicked);

    connect(m_autoOcrButton, &QPushButton::toggled, this, &OcrSubToolbar::autoOcrToggled);
    connect(m_showTextButton, &QPushButton::toggled, this, &OcrSubToolbar::showTextToggled);
    connect(m_confidenceButton, &QPushButton::toggled, this, &OcrSubToolbar::confidenceToggled);
    connect(m_snapButton, &QPushButton::toggled, this, &OcrSubToolbar::snapToGridToggled);
}

void OcrSubToolbar::updateIcons()
{
    auto load = [this](const QString& baseName) -> QIcon {
        QString path = m_darkMode
            ? QStringLiteral(":/resources/icons/%1_reversed.png").arg(baseName)
            : QStringLiteral(":/resources/icons/%1.png").arg(baseName);
        return QIcon(path);
    };

    m_scanPageButton->setIcon(load("scan"));
    m_scanAllButton->setIcon(load("scanall"));
    m_autoOcrButton->setIcon(load("auto"));
    m_showTextButton->setIcon(load("showtext"));
    m_confidenceButton->setIcon(load("warning"));
    m_snapButton->setIcon(load("aligncenter"));
}

void OcrSubToolbar::applyButtonStyle()
{
    QString style;
    if (m_darkMode) {
        style = QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 4px; }"
            "QPushButton:hover { background: rgba(255,255,255,30); }"
            "QPushButton:pressed { background: rgba(255,255,255,55); }"
            "QPushButton:checked { background: rgba(80,160,255,100); border: 1px solid rgba(80,160,255,180); }"
            "QPushButton:checked:hover { background: rgba(80,160,255,130); }");
    } else {
        style = QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 4px; }"
            "QPushButton:hover { background: rgba(0,0,0,20); }"
            "QPushButton:pressed { background: rgba(0,0,0,45); }"
            "QPushButton:checked { background: rgba(0,120,212,70); border: 1px solid rgba(0,120,212,140); }"
            "QPushButton:checked:hover { background: rgba(0,120,212,95); }");
    }

    m_scanPageButton->setStyleSheet(style);
    m_scanAllButton->setStyleSheet(style);
    m_autoOcrButton->setStyleSheet(style);
    m_showTextButton->setStyleSheet(style);
    m_confidenceButton->setStyleSheet(style);
    m_snapButton->setStyleSheet(style);
}

void OcrSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    if (m_darkMode == darkMode)
        return;
    m_darkMode = darkMode;
    updateIcons();
    applyButtonStyle();
}

void OcrSubToolbar::refreshFromSettings()
{
}

void OcrSubToolbar::restoreTabState(int tabId)
{
    if (!m_tabStates.contains(tabId))
        return;

    const TabState& state = m_tabStates[tabId];
    if (!state.initialized)
        return;

    m_autoOcrButton->blockSignals(true);
    m_autoOcrButton->setChecked(state.autoOcrEnabled);
    m_autoOcrButton->blockSignals(false);

    m_showTextButton->blockSignals(true);
    m_showTextButton->setChecked(state.showTextEnabled);
    m_showTextButton->blockSignals(false);

    m_confidenceButton->blockSignals(true);
    m_confidenceButton->setChecked(state.confidenceEnabled);
    m_confidenceButton->blockSignals(false);

    // Snap-to-grid is NOT restored here. It lives on the Document
    // (doc->ocrSnapToBackground) and is synced by
    // MainWindow::connectViewportScrollSignals() on every viewport switch.
}

void OcrSubToolbar::saveTabState(int tabId)
{
    TabState state;
    state.autoOcrEnabled = m_autoOcrButton->isChecked();
    state.showTextEnabled = m_showTextButton->isChecked();
    state.confidenceEnabled = m_confidenceButton->isChecked();
    // Snap-to-grid intentionally not saved; see TabState comment.
    state.initialized = true;
    m_tabStates[tabId] = state;
}

void OcrSubToolbar::clearTabState(int tabId)
{
    m_tabStates.remove(tabId);
}

void OcrSubToolbar::setOcrAvailable(bool available)
{
    // Engine-dependent controls: hidden when no engine is available
    m_scanPageButton->setVisible(available);
    m_scanAllButton->setVisible(available);
    m_autoOcrButton->setVisible(available);
    if (m_scanSeparator)
        m_scanSeparator->setVisible(available);

    // Display-only controls: always enabled (work on cached OCR data)
    m_showTextButton->setEnabled(true);
    m_confidenceButton->setEnabled(true);
    m_snapButton->setEnabled(true);

    if (!available) {
        m_statusLabel->setText(tr("Cached text only"));
    } else {
        m_statusLabel->clear();
    }
}

void OcrSubToolbar::setStatusText(const QString& text)
{
    m_statusClearTimer->stop();
    m_statusLabel->setText(text);
}

void OcrSubToolbar::clearStatusAfterDelay(int ms)
{
    m_statusClearTimer->start(ms);
}

bool OcrSubToolbar::isShowTextEnabled() const
{
    return m_showTextButton && m_showTextButton->isChecked();
}

bool OcrSubToolbar::isConfidenceEnabled() const
{
    return m_confidenceButton && m_confidenceButton->isChecked();
}

bool OcrSubToolbar::isSnapToGridEnabled() const
{
    return m_snapButton && m_snapButton->isChecked();
}

void OcrSubToolbar::setSnapToGridChecked(bool checked)
{
    if (m_snapButton)
        m_snapButton->setChecked(checked);
}

void OcrSubToolbar::triggerScanPage()
{
    if (m_scanPageButton && m_scanPageButton->isEnabled())
        m_scanPageButton->click();
}

void OcrSubToolbar::triggerScanAll()
{
    if (m_scanAllButton && m_scanAllButton->isEnabled())
        m_scanAllButton->click();
}

void OcrSubToolbar::toggleAutoOcr()
{
    if (m_autoOcrButton && m_autoOcrButton->isEnabled())
        m_autoOcrButton->toggle();
}

void OcrSubToolbar::toggleShowText()
{
    if (m_showTextButton && m_showTextButton->isEnabled())
        m_showTextButton->toggle();
}

void OcrSubToolbar::toggleSnapToGrid()
{
    if (m_snapButton && m_snapButton->isEnabled())
        m_snapButton->toggle();
}
