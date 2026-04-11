#include "FloatingTextEditor.h"
#include "../../../markdown/qmarkdowntextedit.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QTextCursor>

FloatingTextEditor::FloatingTextEditor(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
    resize(360, 280);
    setFocusPolicy(Qt::StrongFocus);
    buildUi();
}

void FloatingTextEditor::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- Title bar ---
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(TITLE_BAR_HEIGHT);
    titleBar->setObjectName(QStringLiteral("fte_titleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 0, 4, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new QLabel(tr("Text Editor"), titleBar);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLayout->addWidget(m_titleLabel);

    m_closeButton = new QPushButton(QStringLiteral("\u2715"), titleBar);
    m_closeButton->setObjectName(QStringLiteral("fte_close"));
    m_closeButton->setFixedSize(22, 22);
    m_closeButton->setFlat(true);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    connect(m_closeButton, &QPushButton::clicked, this, &FloatingTextEditor::closeEditor);
    titleLayout->addWidget(m_closeButton);
    root->addWidget(titleBar);

    // --- Toolbar row ---
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(TOOLBAR_HEIGHT);
    toolbar->setObjectName(QStringLiteral("fte_toolbar"));
    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(6, 2, 6, 2);
    tbLayout->setSpacing(4);

    m_alignGroup = new QButtonGroup(this);
    m_alignGroup->setExclusive(true);

    auto makeAlignBtn = [&](int id) {
        auto* btn = new QPushButton(toolbar);
        btn->setFixedSize(24, 22);
        btn->setIconSize(QSize(16, 16));
        btn->setCheckable(true);
        btn->setFocusPolicy(Qt::NoFocus);
        m_alignGroup->addButton(btn, id);
        tbLayout->addWidget(btn);
        return btn;
    };
    m_alignLeft   = makeAlignBtn(0);
    m_alignCenter = makeAlignBtn(1);
    m_alignRight  = makeAlignBtn(2);
    m_alignLeft->setToolTip(tr("Align Left"));
    m_alignCenter->setToolTip(tr("Align Center"));
    m_alignRight->setToolTip(tr("Align Right"));
    m_alignLeft->setChecked(true);
    updateAlignIcons();

    tbLayout->addSpacing(8);

    m_opacityLabel = new QLabel(tr("Opacity:"), toolbar);
    tbLayout->addWidget(m_opacityLabel);

    m_opacitySlider = new QSlider(Qt::Horizontal, toolbar);
    m_opacitySlider->setRange(0, 255);
    m_opacitySlider->setValue(180);
    m_opacitySlider->setFocusPolicy(Qt::NoFocus);
    tbLayout->addWidget(m_opacitySlider, 1);
    root->addWidget(toolbar);

    // --- Markdown editor ---
    m_editor = new QMarkdownTextEdit(this, true);
    m_editor->setLineNumberEnabled(false);
    m_editor->hideSearchWidget(true);
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);
    root->addWidget(m_editor, 1);

    // Issue 3 pattern: connect urlClicked signal for link following
    connect(m_editor, &QMarkdownTextEdit::urlClicked, this, [](const QString &url) {
        QDesktopServices::openUrl(QUrl(url));
    });

    // Connections (initially disconnected; reconnected in setTarget)
    connect(m_opacitySlider, &QSlider::valueChanged,
            this, &FloatingTextEditor::onOpacityChanged);
    connect(m_alignGroup, &QButtonGroup::idClicked,
            this, &FloatingTextEditor::onAlignmentChanged);
}

bool FloatingTextEditor::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_editor && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            closeEditor();
            return true;
        }
        if (ke->key() == Qt::Key_F && (ke->modifiers() & Qt::ControlModifier)) {
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void FloatingTextEditor::setTarget(TextBoxObject* obj)
{
    if (!obj) return;

    // Disconnect live editing while populating
    disconnect(m_editor, &QPlainTextEdit::textChanged, this, nullptr);

    m_target = obj;
    m_targetId = obj->id;
    m_originalText = obj->text;
    m_originalAlignment = obj->alignment;
    m_originalOpacity = obj->backgroundColor.alpha();

    m_editor->setPlainText(obj->text);

    int alignId = 0;
    if (obj->alignment == TextAlignment::Center) alignId = 1;
    else if (obj->alignment == TextAlignment::Right) alignId = 2;
    if (auto* btn = m_alignGroup->button(alignId))
        btn->setChecked(true);

    m_opacitySlider->setValue(obj->backgroundColor.alpha());

    // Reconnect live editing
    connect(m_editor, &QPlainTextEdit::textChanged,
            this, &FloatingTextEditor::onTextChanged);
}

void FloatingTextEditor::closeEditor()
{
    if (!m_target) {
        hide();
        return;
    }

    // Disconnect to stop further live edits
    disconnect(m_editor, &QPlainTextEdit::textChanged, this, nullptr);

    // Snapshot current values from the editor widgets (not from m_target)
    // so this remains safe even if m_target memory is suspect.
    QString currentText = m_editor->toPlainText();
    int currentAlignId = m_alignGroup->checkedId();
    TextAlignment currentAlignment = TextAlignment::Left;
    if (currentAlignId == 1) currentAlignment = TextAlignment::Center;
    else if (currentAlignId == 2) currentAlignment = TextAlignment::Right;
    int currentOpacity = m_opacitySlider->value();

    bool changed = (currentText != m_originalText)
                 || (currentAlignment != m_originalAlignment)
                 || (currentOpacity != m_originalOpacity);

    if (changed) {
        emit editorClosed(m_targetId,
                          m_originalText, currentText,
                          static_cast<int>(m_originalAlignment),
                          static_cast<int>(currentAlignment),
                          m_originalOpacity,
                          currentOpacity);
    }

    m_target = nullptr;
    m_targetId.clear();
    hide();
}

void FloatingTextEditor::updateAlignIcons()
{
    QString suffix = m_darkMode ? QStringLiteral("_reversed") : QString();
    m_alignLeft->setIcon(QIcon(QStringLiteral(":/resources/icons/alignleft%1.png").arg(suffix)));
    m_alignCenter->setIcon(QIcon(QStringLiteral(":/resources/icons/aligncenter%1.png").arg(suffix)));
    m_alignRight->setIcon(QIcon(QStringLiteral(":/resources/icons/alignright%1.png").arg(suffix)));
}

void FloatingTextEditor::setDarkMode(bool dark)
{
    m_darkMode = dark;
    updateAlignIcons();
    const QString bg   = dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#f5f5f5");
    const QString fg   = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#1a1a1a");
    const QString bar  = dark ? QStringLiteral("#353535") : QStringLiteral("#e8e8e8");
    const QString bdr  = dark ? QStringLiteral("#555555") : QStringLiteral("#c0c0c0");
    const QString btnH = dark ? QStringLiteral("#4a4a4a") : QStringLiteral("#d0d0d0");
    const QString btnC = dark ? QStringLiteral("#505060") : QStringLiteral("#b8c0d0");

    setStyleSheet(QStringLiteral(
        "FloatingTextEditor { background: %1; border: 1px solid %4; }"
        "#fte_titleBar { background: %3; color: %2; }"
        "#fte_toolbar  { background: %3; }"
        "QLabel { color: %2; }"
        "QPushButton { background: transparent; color: %2; border: 1px solid %4;"
        "             border-radius: 3px; }"
        "QPushButton:hover { background: %5; }"
        "QPushButton:checked { background: %6; }"
        "QPushButton#fte_close { border: none; }"
    ).arg(bg, fg, bar, bdr, btnH, btnC));

    if (m_editor) {
        QPalette pal = m_editor->palette();
        pal.setColor(QPalette::Base, QColor(dark ? "#1e1e1e" : "#ffffff"));
        pal.setColor(QPalette::Text, QColor(dark ? "#d4d4d4" : "#1a1a1a"));
        m_editor->setPalette(pal);
    }
}

// --- Live editing slots ---

void FloatingTextEditor::onTextChanged()
{
    if (!m_target) return;
    m_target->text = m_editor->toPlainText();
    m_target->invalidateDocCache();
    emit repaintRequested();
}

void FloatingTextEditor::onAlignmentChanged(int id)
{
    if (!m_target) return;
    TextAlignment a = TextAlignment::Left;
    if (id == 1) a = TextAlignment::Center;
    else if (id == 2) a = TextAlignment::Right;
    m_target->alignment = a;
    m_target->invalidateDocCache();
    emit repaintRequested();
}

void FloatingTextEditor::onOpacityChanged(int value)
{
    if (!m_target) return;
    QColor bg = m_target->backgroundColor;
    bg.setAlpha(value);
    m_target->backgroundColor = bg;
    emit repaintRequested();
}

// --- Drag support ---

void FloatingTextEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && e->pos().y() < TITLE_BAR_HEIGHT) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void FloatingTextEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        move(e->globalPosition().toPoint() - m_dragOffset);
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void FloatingTextEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void FloatingTextEditor::paintEvent(QPaintEvent* e)
{
    QWidget::paintEvent(e);
    QPainter p(this);
    p.setPen(QPen(QColor(m_darkMode ? "#555555" : "#c0c0c0"), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}
