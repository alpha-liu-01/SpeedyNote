#include "TextBoxFormatBar.h"

#include "../widgets/ColorPresetButton.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

TextBoxFormatBar::TextBoxFormatBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("textBoxFormatBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto* sizeLabel = new QLabel(tr("Size"), this);
    sizeLabel->setAccessibleName(tr("Base font size label"));
    layout->addWidget(sizeLabel);

    m_fontSize = new QDoubleSpinBox(this);
    m_fontSize->setRange(6.0, 144.0);
    m_fontSize->setDecimals(1);
    m_fontSize->setSingleStep(1.0);
    m_fontSize->setFixedWidth(68);
    m_fontSize->setMinimumHeight(34);
    m_fontSize->setToolTip(tr("Base font size"));
    m_fontSize->setAccessibleName(tr("Base font size"));
    m_fontSize->installEventFilter(this);
    layout->addWidget(m_fontSize);

    m_fontFamily = new QFontComboBox(this);
    // QFontComboBox reads the platform font database, so the list follows the
    // fonts installed on Windows, macOS, Linux, Android, or iOS. Bitmap-only
    // legacy aliases such as "MS Sans Serif" are unsuitable for scalable
    // document text and can make DirectWrite report invalid font metrics.
    m_fontFamily->setFontFilters(QFontComboBox::ScalableFonts);
    m_fontFamily->setMinimumWidth(130);
    m_fontFamily->setMaximumWidth(190);
    m_fontFamily->setMinimumHeight(34);
    m_fontFamily->setToolTip(tr("Font family"));
    m_fontFamily->setAccessibleName(tr("Font family"));
    m_fontFamily->installEventFilter(this);
    m_fontFamily->view()->installEventFilter(this);
    // QFontComboBox is editable, so keystrokes land on its internal line edit
    // rather than the combo. Without this the bar's Escape and Ctrl+Enter
    // contract silently stops working while the user types a font name.
    if (QLineEdit* fontEdit = m_fontFamily->lineEdit())
        fontEdit->installEventFilter(this);
    layout->addWidget(m_fontFamily);

    m_alignmentGroup = new QButtonGroup(this);
    m_alignmentGroup->setExclusive(true);
    auto makeButton = [this, layout](const QString& tooltip) {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setFixedSize(34, 34);
        button->setIconSize(QSize(19, 19));
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->installEventFilter(this);
        layout->addWidget(button);
        return button;
    };
    m_alignLeft = makeButton(tr("Align left"));
    m_alignCenter = makeButton(tr("Align center"));
    m_alignRight = makeButton(tr("Align right"));
    m_alignmentGroup->addButton(m_alignLeft, 0);
    m_alignmentGroup->addButton(m_alignCenter, 1);
    m_alignmentGroup->addButton(m_alignRight, 2);

    m_fontColor = new ColorPresetButton(this);
    m_fontColor->setButtonSize(34);
    m_fontColor->setSelected(true);
    m_fontColor->setToolTip(tr("Text color"));
    m_fontColor->setAccessibleName(tr("Text color"));
    m_fontColor->setFocusPolicy(Qt::StrongFocus);
    m_fontColor->installEventFilter(this);
    layout->addWidget(m_fontColor);

    m_backgroundColor = new ColorPresetButton(this);
    m_backgroundColor->setButtonSize(34);
    m_backgroundColor->setSelected(true);
    m_backgroundColor->setToolTip(tr("Background color"));
    m_backgroundColor->setAccessibleName(tr("Background color"));
    m_backgroundColor->setFocusPolicy(Qt::StrongFocus);
    m_backgroundColor->installEventFilter(this);
    layout->addWidget(m_backgroundColor);

    auto* opacityLabel = new QLabel(tr("Opacity"), this);
    opacityLabel->setAccessibleName(tr("Background opacity label"));
    layout->addWidget(opacityLabel);

    m_backgroundOpacity = new QSlider(Qt::Horizontal, this);
    m_backgroundOpacity->setRange(0, 255);
    m_backgroundOpacity->setFixedWidth(78);
    m_backgroundOpacity->setMinimumHeight(34);
    m_backgroundOpacity->setToolTip(tr("Background opacity"));
    m_backgroundOpacity->setAccessibleName(tr("Background opacity"));
    m_backgroundOpacity->installEventFilter(this);
    layout->addWidget(m_backgroundOpacity);

    m_border = new QToolButton(this);
    m_border->setCheckable(true);
    m_border->setFixedSize(34, 34);
    m_border->setText(QStringLiteral("□"));
    m_border->setToolTip(tr("Show border"));
    m_border->setAccessibleName(tr("Show border"));
    m_border->installEventFilter(this);
    layout->addWidget(m_border);

    QWidget::setTabOrder(m_fontSize, m_fontFamily);
    QWidget::setTabOrder(m_fontFamily, m_alignLeft);
    QWidget::setTabOrder(m_alignLeft, m_alignCenter);
    QWidget::setTabOrder(m_alignCenter, m_alignRight);
    QWidget::setTabOrder(m_alignRight, m_fontColor);
    QWidget::setTabOrder(m_fontColor, m_backgroundColor);
    QWidget::setTabOrder(m_backgroundColor, m_backgroundOpacity);
    QWidget::setTabOrder(m_backgroundOpacity, m_border);

    m_burstTimer = new QTimer(this);
    m_burstTimer->setSingleShot(true);
    m_burstTimer->setInterval(300);
    connect(m_burstTimer, &QTimer::timeout, this, [this]() {
        finishInteraction(true);
    });

    connect(m_fontSize,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        if (m_syncing)
            return;
        ensureInteractionStarted();
        emit fontSizePreviewRequested(value);
    });
    connect(m_fontSize, &QDoubleSpinBox::editingFinished,
            this, [this]() { finishInteraction(true); });

    connect(m_fontFamily, &QFontComboBox::currentFontChanged,
            this, [this](const QFont& font) {
        if (m_syncing)
            return;
        // interactionStarted() synchronously asks the viewport to snapshot the
        // object and resynchronize every control. Preserve the user's choice
        // before that re-entrant synchronization changes currentFont().
        const QString selectedFamily = font.family();
        ensureInteractionStarted();
        emit fontFamilyPreviewRequested(selectedFamily);
        if (!m_fontFamily->view()->isVisible())
            scheduleBurstFinish();
    });
    connect(m_fontFamily,
            QOverload<int>::of(&QFontComboBox::activated),
            this, [this](int) {
        m_fontChoiceActivated = true;
        finishInteraction(true);
    });

    connect(m_alignmentGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) {
        ensureInteractionStarted();
        const TextAlignment alignment = id == 1
            ? TextAlignment::Center
            : (id == 2 ? TextAlignment::Right : TextAlignment::Left);
        emit alignmentPreviewRequested(alignment);
        finishInteraction(true);
    });

    auto openFontColor = [this]() { openColorDialog(false); };
    connect(m_fontColor, &ColorPresetButton::clicked,
            this, openFontColor);
    connect(m_fontColor, &ColorPresetButton::editRequested,
            this, openFontColor);
    auto openBackgroundColor = [this]() { openColorDialog(true); };
    connect(m_backgroundColor, &ColorPresetButton::clicked,
            this, openBackgroundColor);
    connect(m_backgroundColor, &ColorPresetButton::editRequested,
            this, openBackgroundColor);

    connect(m_backgroundOpacity, &QSlider::sliderPressed,
            this, &TextBoxFormatBar::ensureInteractionStarted);
    connect(m_backgroundOpacity, &QSlider::valueChanged,
            this, [this](int value) {
        if (m_syncing)
            return;
        ensureInteractionStarted();
        m_currentBackgroundColor.setAlpha(value);
        updateSwatches();
        emit backgroundOpacityPreviewRequested(value);
        if (!m_backgroundOpacity->isSliderDown())
            scheduleBurstFinish();
    });
    connect(m_backgroundOpacity, &QSlider::sliderReleased,
            this, [this]() { finishInteraction(true); });

    connect(m_border, &QToolButton::clicked,
            this, [this](bool checked) {
        ensureInteractionStarted();
        emit borderPreviewRequested(checked);
        finishInteraction(true);
    });

    updateAlignmentIcons();
    setDarkMode(false);
}

TextBoxFormatBar::~TextBoxFormatBar()
{
    closePopups(false);
}

void TextBoxFormatBar::setValues(const TextBoxState& state)
{
    m_syncing = true;
    const QSignalBlocker sizeBlocker(m_fontSize);
    const QSignalBlocker fontBlocker(m_fontFamily);
    const QSignalBlocker opacityBlocker(m_backgroundOpacity);
    const QSignalBlocker borderBlocker(m_border);
    const QSignalBlocker alignBlocker(m_alignmentGroup);

    m_fontSize->setValue(state.fontSize);
    QString displayedFamily = state.fontFamily.trimmed();
    if (displayedFamily.isEmpty()
        || m_fontFamily->findText(
               displayedFamily, Qt::MatchFixedString) < 0) {
        displayedFamily = QApplication::font().family();
        if (m_fontFamily->findText(
                displayedFamily, Qt::MatchFixedString) < 0
            && m_fontFamily->count() > 0) {
            displayedFamily = m_fontFamily->itemText(0);
        }
    }
    if (!displayedFamily.isEmpty())
        m_fontFamily->setCurrentFont(QFont(displayedFamily));
    const int alignmentId = state.alignment == TextAlignment::Center
        ? 1 : (state.alignment == TextAlignment::Right ? 2 : 0);
    if (auto* button = m_alignmentGroup->button(alignmentId))
        button->setChecked(true);
    m_currentFontColor = state.fontColor;
    m_currentBackgroundColor = state.backgroundColor;
    m_backgroundOpacity->setValue(state.backgroundColor.alpha());
    m_border->setChecked(state.showBorder);
    updateSwatches();
    m_syncing = false;
}

void TextBoxFormatBar::setDarkMode(bool darkMode)
{
    m_darkMode = darkMode;
    updateAlignmentIcons();
    const QString background = darkMode
        ? QStringLiteral("#303030") : QStringLiteral("#f7f7f7");
    const QString foreground = darkMode
        ? QStringLiteral("#eeeeee") : QStringLiteral("#202020");
    const QString border = darkMode
        ? QStringLiteral("#5b5b5b") : QStringLiteral("#bdbdbd");
    const QString hover = darkMode
        ? QStringLiteral("#484848") : QStringLiteral("#e1e1e1");
    const QString checked = darkMode
        ? QStringLiteral("#52627a") : QStringLiteral("#c9d8ec");
    setStyleSheet(QStringLiteral(
        "#textBoxFormatBar { background: %1; border: 1px solid %3;"
        " border-radius: 7px; }"
        "#textBoxFormatBar QLabel { color: %2; }"
        "#textBoxFormatBar QToolButton { color: %2; background: transparent;"
        " border: 1px solid %3; border-radius: 4px; }"
        "#textBoxFormatBar QToolButton:hover { background: %4; }"
        "#textBoxFormatBar QToolButton:checked { background: %5; }")
        .arg(background, foreground, border, hover, checked));
}

void TextBoxFormatBar::closePopups(bool acceptPreview)
{
    m_burstTimer->stop();
    m_closingPopups = true;
    if (m_fontFamily && m_fontFamily->view())
        m_fontFamily->hidePopup();
    if (m_colorDialog) {
        m_colorDialog->disconnect(this);
        m_colorDialog->close();
        m_colorDialog->deleteLater();
        m_colorDialog.clear();
    }
    m_closingPopups = false;
    finishInteraction(acceptPreview);
}

bool TextBoxFormatBar::hasOpenPopup() const
{
    return !m_colorDialog.isNull()
        || (m_fontFamily && m_fontFamily->view()
            && m_fontFamily->view()->isVisible());
}

bool TextBoxFormatBar::controlHasFocus() const
{
    // An open popup grabs focus for its own window, so checking only the bar's
    // widget tree would report "not focused" while the user is browsing fonts
    // and hand their Ctrl+Z to the document instead.
    if (hasOpenPopup())
        return true;

    QWidget* focused = QApplication::focusWidget();
    return focused && (focused == this || isAncestorOf(focused)
        || (m_colorDialog
            && (focused == m_colorDialog
                || m_colorDialog->isAncestorOf(focused))));
}

bool TextBoxFormatBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_fontFamily->view()) {
        if (event->type() == QEvent::Show) {
            m_fontChoiceActivated = false;
        } else if (event->type() == QEvent::Hide && !m_closingPopups
                   && m_interactionActive) {
            // Keyboard browsing previews each font it passes over, so a popup
            // dismissed by clicking away must roll back to the original font
            // instead of committing whatever was last highlighted.
            // Qt hides the popup before emitting activated, so the flag is
            // read from the deferred slot rather than captured here.
            QTimer::singleShot(0, this, [this]() {
                finishInteraction(m_fontChoiceActivated);
            });
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (hasOpenPopup() || m_interactionActive) {
                const bool hadColorDialog = !m_colorDialog.isNull();
                const bool backgroundDialog =
                    m_colorDialogIsBackground;
                const bool hadFontPopup =
                    m_fontFamily->view()->isVisible();
                closePopups(false);
                if (hadColorDialog) {
                    (backgroundDialog
                         ? static_cast<QWidget*>(m_backgroundColor)
                         : static_cast<QWidget*>(m_fontColor))
                        ->setFocus(Qt::PopupFocusReason);
                } else if (hadFontPopup) {
                    m_fontFamily->setFocus(Qt::PopupFocusReason);
                }
                return true;
            }
            emit cancelInlineEditRequested();
            return true;
        }
        if ((keyEvent->modifiers() & Qt::ControlModifier)
            && (keyEvent->key() == Qt::Key_Return
                || keyEvent->key() == Qt::Key_Enter)) {
            if (hasOpenPopup())
                closePopups(true);
            else
                finishInteraction(true);
            emit commitInlineEditRequested();
            return true;
        }
        if ((watched == m_fontColor || watched == m_backgroundColor)
            && (keyEvent->key() == Qt::Key_Space
                || keyEvent->key() == Qt::Key_Return
                || keyEvent->key() == Qt::Key_Enter)) {
            openColorDialog(watched == m_backgroundColor);
            return true;
        }
    } else if (event->type() == QEvent::Wheel
               || event->type() == QEvent::KeyRelease) {
        if (watched == m_fontSize || watched == m_backgroundOpacity) {
            ensureInteractionStarted();
            scheduleBurstFinish();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TextBoxFormatBar::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void TextBoxFormatBar::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void TextBoxFormatBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void TextBoxFormatBar::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void TextBoxFormatBar::ensureInteractionStarted()
{
    if (m_syncing || m_interactionActive)
        return;
    m_interactionActive = true;
    emit interactionStarted();
}

void TextBoxFormatBar::finishInteraction(bool accept)
{
    m_burstTimer->stop();
    if (!m_interactionActive)
        return;
    m_interactionActive = false;
    emit interactionFinished(accept);
}

void TextBoxFormatBar::scheduleBurstFinish()
{
    if (m_interactionActive)
        m_burstTimer->start();
}

void TextBoxFormatBar::openColorDialog(bool background)
{
    if (m_colorDialog)
        return;
    ensureInteractionStarted();
    m_colorDialogIsBackground = background;
    const QColor initial = background
        ? m_currentBackgroundColor : m_currentFontColor;
    auto* dialog = new QColorDialog(initial, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setOption(QColorDialog::DontUseNativeDialog, true);
    dialog->setOption(QColorDialog::ShowAlphaChannel, false);
    dialog->setWindowTitle(background
        ? tr("Background color") : tr("Text color"));
    m_colorDialog = dialog;
    dialog->installEventFilter(this);
    const auto dialogChildren = dialog->findChildren<QWidget*>();
    for (QWidget* child : dialogChildren)
        child->installEventFilter(this);

    connect(dialog, &QColorDialog::currentColorChanged,
            this, [this](const QColor& selected) {
        if (!selected.isValid())
            return;
        if (m_colorDialogIsBackground) {
            QColor color = selected;
            color.setAlpha(m_currentBackgroundColor.alpha());
            m_currentBackgroundColor = color;
            updateSwatches();
            emit backgroundColorPreviewRequested(color);
        } else {
            m_currentFontColor = selected;
            updateSwatches();
            emit fontColorPreviewRequested(selected);
        }
    });
    connect(dialog, &QColorDialog::accepted,
            this, [this]() {
        m_colorDialog.clear();
        finishInteraction(true);
        (m_colorDialogIsBackground
             ? static_cast<QWidget*>(m_backgroundColor)
             : static_cast<QWidget*>(m_fontColor))
            ->setFocus(Qt::PopupFocusReason);
    });
    connect(dialog, &QColorDialog::rejected,
            this, [this]() {
        m_colorDialog.clear();
        finishInteraction(false);
        (m_colorDialogIsBackground
             ? static_cast<QWidget*>(m_backgroundColor)
             : static_cast<QWidget*>(m_fontColor))
            ->setFocus(Qt::PopupFocusReason);
    });
    connect(dialog, &QObject::destroyed,
            this, [this]() {
        m_colorDialog.clear();
        // accepted/rejected already finished the interaction in the normal
        // paths; this catches a dialog torn down by anything else, which
        // would otherwise strand the viewport transaction open forever.
        finishInteraction(false);
    });
    dialog->open();
}

void TextBoxFormatBar::updateAlignmentIcons()
{
    const QString suffix =
        m_darkMode ? QStringLiteral("_reversed") : QString();
    m_alignLeft->setIcon(QIcon(
        QStringLiteral(":/resources/icons/alignleft%1.png").arg(suffix)));
    m_alignCenter->setIcon(QIcon(
        QStringLiteral(":/resources/icons/aligncenter%1.png").arg(suffix)));
    m_alignRight->setIcon(QIcon(
        QStringLiteral(":/resources/icons/alignright%1.png").arg(suffix)));
}

void TextBoxFormatBar::updateSwatches()
{
    m_fontColor->setColor(m_currentFontColor);
    m_backgroundColor->setColor(m_currentBackgroundColor);
}
