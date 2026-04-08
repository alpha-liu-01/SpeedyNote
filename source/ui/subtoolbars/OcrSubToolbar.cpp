#include "OcrSubToolbar.h"

#include <QPushButton>
#include <QLabel>
#include <QTimer>

OcrSubToolbar::OcrSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
}

void OcrSubToolbar::createWidgets()
{
    m_scanPageButton = new QPushButton(tr("Scan Page"), this);
    m_scanPageButton->setFixedHeight(26);
    m_scanPageButton->setFocusPolicy(Qt::NoFocus);
    addWidget(m_scanPageButton);

    m_scanAllButton = new QPushButton(tr("Scan All"), this);
    m_scanAllButton->setFixedHeight(26);
    m_scanAllButton->setFocusPolicy(Qt::NoFocus);
    addWidget(m_scanAllButton);

    addSeparator();

    m_autoOcrButton = new QPushButton(tr("Auto"), this);
    m_autoOcrButton->setCheckable(true);
    m_autoOcrButton->setFixedHeight(26);
    m_autoOcrButton->setFocusPolicy(Qt::NoFocus);
    addWidget(m_autoOcrButton);

    m_showTextButton = new QPushButton(tr("Show Text"), this);
    m_showTextButton->setCheckable(true);
    m_showTextButton->setFixedHeight(26);
    m_showTextButton->setFocusPolicy(Qt::NoFocus);
    addWidget(m_showTextButton);

    addSeparator();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    addWidget(m_statusLabel);

    m_statusClearTimer = new QTimer(this);
    m_statusClearTimer->setSingleShot(true);
    connect(m_statusClearTimer, &QTimer::timeout, this, [this]() {
        m_statusLabel->clear();
    });
}

void OcrSubToolbar::setupConnections()
{
    connect(m_scanPageButton, &QPushButton::clicked, this, &OcrSubToolbar::scanPageClicked);
    connect(m_scanAllButton, &QPushButton::clicked, this, &OcrSubToolbar::scanAllClicked);

    connect(m_autoOcrButton, &QPushButton::toggled, this, &OcrSubToolbar::autoOcrToggled);
    connect(m_showTextButton, &QPushButton::toggled, this, &OcrSubToolbar::showTextToggled);
}

void OcrSubToolbar::refreshFromSettings()
{
    // OCR toggles are per-session, not persisted to QSettings (Q11.2)
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
}

void OcrSubToolbar::saveTabState(int tabId)
{
    TabState state;
    state.autoOcrEnabled = m_autoOcrButton->isChecked();
    state.showTextEnabled = m_showTextButton->isChecked();
    state.initialized = true;
    m_tabStates[tabId] = state;
}

void OcrSubToolbar::clearTabState(int tabId)
{
    m_tabStates.remove(tabId);
}

void OcrSubToolbar::setOcrAvailable(bool available)
{
    m_scanPageButton->setEnabled(available);
    m_scanAllButton->setEnabled(available);
    m_autoOcrButton->setEnabled(available);
    m_showTextButton->setEnabled(available);

    if (!available) {
        m_statusLabel->setText(tr("OCR unavailable"));
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
