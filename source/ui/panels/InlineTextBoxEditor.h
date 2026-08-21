#pragma once

#include "../../objects/TextBoxObject.h"

#include <QTextCursor>
#include <QWidget>

class QMarkdownTextEdit;

/**
 * Viewport-owned Markdown source editor used for user-created text boxes.
 *
 * Object lookup, page constraints, undo, and lifecycle remain owned by
 * DocumentViewport. This widget only owns the Qt text-input surface.
 */
class InlineTextBoxEditor : public QWidget {
    Q_OBJECT

public:
    explicit InlineTextBoxEditor(QWidget* parent = nullptr);

    void configure(const TextBoxState& state, qreal zoom, bool darkMode);
    void setText(const QString& text, const QTextCursor* cursor = nullptr);
    QString text() const;
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor& cursor);
    QTextCursor takeCursorBeforeLastChange();
    QMarkdownTextEdit* editor() const { return m_editor; }

    /**
     * @brief Swallow the one context menu that a right-click is about to raise.
     *
     * Right-clicking the canvas creates a text box and opens this editor under
     * the cursor, so Qt hands the editor the context menu belonging to that same
     * click. The user asked for a text box, not a menu. Later right-clicks
     * inside the editor still get the normal menu.
     */
    void suppressNextContextMenu();

signals:
    void sourceChanged(const QString& source);
    void commitRequested();
    void cancelRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QMarkdownTextEdit* m_editor = nullptr;
    QTextCursor m_cursorBeforeChange;
    bool m_hasCursorBeforeChange = false;
    bool m_settingText = false;
    bool m_suppressContextMenu = false;
};
