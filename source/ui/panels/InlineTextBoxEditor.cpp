#include "InlineTextBoxEditor.h"

#include "../../../markdown/qmarkdowntextedit.h"

#include <QEvent>
#include <QKeyEvent>
#include <QPalette>
#include <QTextDocument>
#include <QTextOption>
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
    QFont font = m_editor->font();
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

bool InlineTextBoxEditor::eventFilter(QObject* watched, QEvent* event)
{
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
