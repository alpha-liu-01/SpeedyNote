#include "InlineTextBoxEditor.h"

#include "../../../markdown/qmarkdowntextedit.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPalette>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

InlineTextBoxEditor::InlineTextBoxEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inlineTextBoxEditor"));
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_editor = new QMarkdownTextEdit(this, true);
    m_editor->setObjectName(QStringLiteral("inlineMarkdownEditor"));
    m_editor->setLineNumberEnabled(false);
    m_editor->hideSearchWidget(true);
    m_editor->setFrameStyle(QFrame::NoFrame);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setTabChangesFocus(false);
    m_editor->document()->setDocumentMargin(0.0);
    m_editor->setAttribute(Qt::WA_TranslucentBackground, true);
    m_editor->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    m_editor->installEventFilter(this);
    // QAbstractScrollArea routes context menus through the viewport child.
    m_editor->viewport()->installEventFilter(this);
    layout->addWidget(m_editor);

    connect(m_editor->document(), &QTextDocument::contentsChange, this,
            [this](int, int charsRemoved, int charsAdded) {
        if (charsRemoved == 0 && charsAdded == 0)
            return;
        if (m_settingText)
            return;
        if (!m_hasCursorBeforeChange) {
            m_cursorBeforeChange = m_editor->textCursor();
            m_hasCursorBeforeChange = true;
        }
        emit sourceChanged(m_editor->toPlainText());
    });
}

void InlineTextBoxEditor::configure(const TextBoxState& state, qreal zoom,
                                    bool darkMode)
{
    // The editor widget is reused across sessions, so resolve the family from
    // the object state every time. Falling back to the widget's current font
    // would leak the previous box's family into a box that stores none, which
    // renders with the application default once the session commits.
    QFont font = QApplication::font();
    if (!state.fontFamily.isEmpty())
        font.setFamily(state.fontFamily);
    font.setPixelSize(qMax(1, qRound(state.fontSize * qMax<qreal>(zoom, 0.01))));
    m_editor->setFont(font);

    QTextOption option = m_editor->document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    switch (state.alignment) {
        case TextAlignment::Center:
            option.setAlignment(Qt::AlignHCenter);
            break;
        case TextAlignment::Right:
            option.setAlignment(Qt::AlignRight);
            break;
        case TextAlignment::Left:
        default:
            option.setAlignment(Qt::AlignLeft);
            break;
    }
    m_editor->document()->setDefaultTextOption(option);

    QPalette palette = m_editor->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    palette.setColor(QPalette::Text, state.fontColor);
    palette.setColor(QPalette::Highlight,
                     darkMode ? QColor(70, 110, 170) : QColor(160, 200, 245));
    m_editor->setPalette(palette);
    m_editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#inlineMarkdownEditor {"
        " background: transparent; border: none; padding: 0px;"
        " color: %1;"
        "}").arg(state.fontColor.name(QColor::HexArgb)));

    if (MarkdownHighlighter* highlighter = m_editor->highlighter()) {
        highlighter->setBaseFontPixelSize(
            state.fontSize * qMax<qreal>(zoom, 0.01));
        // The editor is transparent, so the syntax colors have to read against
        // the box's own backdrop rather than the application theme.
        highlighter->setDarkBackdrop(
            state.backgroundColor.alpha() > 0
                ? state.backgroundColor.lightness() < 128
                : darkMode);
    }
}

void InlineTextBoxEditor::setText(const QString& text,
                                  const QTextCursor* cursor)
{
    m_settingText = true;
    m_editor->setPlainText(text);
    if (cursor)
        m_editor->setTextCursor(*cursor);
    m_settingText = false;
    m_hasCursorBeforeChange = false;
}

QString InlineTextBoxEditor::text() const
{
    return m_editor->toPlainText();
}

QTextCursor InlineTextBoxEditor::textCursor() const
{
    return m_editor->textCursor();
}

void InlineTextBoxEditor::setTextCursor(const QTextCursor& cursor)
{
    m_editor->setTextCursor(cursor);
}

QTextCursor InlineTextBoxEditor::takeCursorBeforeLastChange()
{
    const QTextCursor cursor = m_hasCursorBeforeChange
        ? m_cursorBeforeChange : m_editor->textCursor();
    m_hasCursorBeforeChange = false;
    return cursor;
}

void InlineTextBoxEditor::suppressNextContextMenu()
{
    m_suppressContextMenu = true;
    // Qt forwards a mouse-triggered context menu synchronously from the same
    // release that armed this, so anything still pending once the event loop
    // spins belongs to a later click and must not be swallowed.
    QTimer::singleShot(0, this, [this]() { m_suppressContextMenu = false; });
}

void InlineTextBoxEditor::showTextContextMenu(const QPoint& globalPos)
{
    if (!m_editor)
        return;
    QMenu* menu = m_editor->createStandardContextMenu();
    if (!menu)
        return;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(globalPos);
}

bool InlineTextBoxEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu
        && (watched == m_editor || watched == m_editor->viewport())) {
        if (m_suppressContextMenu) {
            m_suppressContextMenu = false;
            event->accept();
            return true;
        }
    }

    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            emit cancelRequested();
            return true;
        }
        if ((keyEvent->key() == Qt::Key_Return
             || keyEvent->key() == Qt::Key_Enter)
            && (keyEvent->modifiers() & Qt::ControlModifier)) {
            emit commitRequested();
            return true;
        }
        m_cursorBeforeChange = m_editor->textCursor();
        m_hasCursorBeforeChange = true;
    } else if (watched == m_editor
               && (event->type() == QEvent::InputMethod
                   || event->type() == QEvent::Drop)) {
        m_cursorBeforeChange = m_editor->textCursor();
        m_hasCursorBeforeChange = true;
    }
    return QWidget::eventFilter(watched, event);
}
