/*
 * MIT License
 *
 * Copyright (c) 2014-2026 Patrizio Bekerle -- <patrizio@bekerle.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "qmarkdowntextedit.h"

#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <utility>

#include "linenumberarea.h"
#include "markdownhighlighter.h"

static const QByteArray _openingCharacters = QByteArrayLiteral("([{<*\"'_~");
static const QByteArray _closingCharacters = QByteArrayLiteral(")]}>*\"'_~");

QMarkdownTextEdit::QMarkdownTextEdit(QWidget *parent, bool initHighlighter)
    : QPlainTextEdit(parent) {
    installEventFilter(this);
    viewport()->installEventFilter(this);
    _autoTextOptions = AutoTextOption::BracketClosing;

    _lineNumArea = new LineNumArea(this);
    updateLineNumberAreaWidth(0);

    // Markdown highlighting is enabled by default
    _highlightingEnabled = initHighlighter;
    if (initHighlighter) {
        _highlighter = new MarkdownHighlighter(document());
    }

    QFont font = this->font();

    // set the tab stop to the width of 4 spaces in the editor
    constexpr int tabStop = 4;
    QFontMetrics metrics(font);

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
    setTabStopWidth(tabStop * metrics.width(' '));
#else
    setTabStopDistance(tabStop * metrics.horizontalAdvance(QLatin1Char(' ')));
#endif

    // add shortcuts for duplicating text
    //    new QShortcut( QKeySequence( "Ctrl+D" ), this, SLOT( duplicateText() )
    //    ); new QShortcut( QKeySequence( "Ctrl+Alt+Down" ), this, SLOT(
    //    duplicateText() ) );

    // add a layout to the widget
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    this->setLayout(layout);

    // add the hidden search widget
    _searchWidget = new QPlainTextEditSearchWidget(this);
    this->layout()->addWidget(_searchWidget);

    connect(this, &QPlainTextEdit::textChanged, this,
            &QMarkdownTextEdit::adjustRightMargin);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &QMarkdownTextEdit::centerTheCursor);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { _lineNumArea->update(); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        _lineNumArea->update();

        const QTextBlock oldBlock = _textCursor.block();
        auto oldArea =
            blockBoundingGeometry(oldBlock).translated(contentOffset());
        _textCursor = textCursor();
        const QTextBlock newBlock = _textCursor.block();
        auto newArea =
            blockBoundingGeometry(newBlock).translated(contentOffset());
        auto areaToUpdate = oldArea | newArea;
        viewport()->update(areaToUpdate.toRect());

        // Rehighlight old and new blocks when hiding formatting syntax
        if (_highlighter && _highlighter->hideFormattingSyntax() &&
            oldBlock.blockNumber() != newBlock.blockNumber()) {
            _highlighter->setCurrentCursorBlockNumber(newBlock.blockNumber());
            _highlighter->rehighlightBlock(oldBlock);
            _highlighter->rehighlightBlock(newBlock);
        }
    });
    connect(document(), &QTextDocument::blockCountChanged, this,
            &QMarkdownTextEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this,
            &QMarkdownTextEdit::updateLineNumberArea);

    _hangingCursorRepaintTimer = new QTimer(this);
    _hangingCursorRepaintTimer->setSingleShot(false);
    const int cursorFlashTime =
        QGuiApplication::styleHints()->cursorFlashTime();
    const int repaintInterval =
        cursorFlashTime > 0 ? qMax(100, cursorFlashTime / 2) : 500;
    _hangingCursorRepaintTimer->setInterval(repaintInterval);
    connect(_hangingCursorRepaintTimer, &QTimer::timeout, this, [this]() {
        const QRect repaintRect = hangingCursorBlockRepaintRect();
        if (!repaintRect.isEmpty()) {
            viewport()->update(repaintRect);
        }
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged,
            this, [this](int flashTime) {
                const int interval =
                    flashTime > 0 ? qMax(100, flashTime / 2) : 500;
                _hangingCursorRepaintTimer->setInterval(interval);
            });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateHangingCursorRepaintState(); });

    updateSettings();

    // workaround for disabled signals up initialization
    QTimer::singleShot(300, this, &QMarkdownTextEdit::adjustRightMargin);
}

void QMarkdownTextEdit::setLineNumbersCurrentLineColor(QColor color) {
    _lineNumArea->setCurrentLineColor(std::move(color));
}

void QMarkdownTextEdit::setLineNumbersOtherLineColor(QColor color) {
    _lineNumArea->setOtherLineColor(std::move(color));
}

void QMarkdownTextEdit::setBookmarkLines(const QHash<int, int> &bookmarkLines) {
    _lineNumArea->setBookmarkLines(bookmarkLines);
}

void QMarkdownTextEdit::setSearchWidgetDebounceDelay(uint debounceDelay) {
    _debounceDelay = debounceDelay;
    searchWidget()->setDebounceDelay(_debounceDelay);
}

void QMarkdownTextEdit::setHighlightCurrentLine(bool set) {
    _highlightCurrentLine = set;
}

void QMarkdownTextEdit::setHangingIndentEnabled(bool enabled) {
    _hangingIndentEnabled = enabled;

    if (!_hangingCursorRepaintTimer) {
        return;
    }

    if (!enabled) {
        _hangingCursorRepaintTimer->stop();
        return;
    }

    updateHangingCursorRepaintState();
}

bool QMarkdownTextEdit::highlightCurrentLine() { return _highlightCurrentLine; }

bool QMarkdownTextEdit::hangingIndentEnabled() const {
    return _hangingIndentEnabled;
}

void QMarkdownTextEdit::setCurrentLineHighlightColor(const QColor &color) {
    _currentLineHighlightColor = color;
}

QColor QMarkdownTextEdit::currentLineHighlightColor() {
    return _currentLineHighlightColor;
}

/**
 * Enables or disables the Markdown highlighting
 *
 * @param enabled
 */
void QMarkdownTextEdit::setHighlightingEnabled(bool enabled) {
    if (_highlightingEnabled == enabled || _highlighter == nullptr) {
        return;
    }

    _highlightingEnabled = enabled;
    _highlighter->setDocument(enabled ? document() : Q_NULLPTR);

    if (enabled) {
        _highlighter->rehighlight();
    }
}

/**
 * @brief Returns if highlighting is enabled
 * @return Returns true if highlighting is enabled, otherwise false
 */
bool QMarkdownTextEdit::highlightingEnabled() const {
    return _highlightingEnabled && _highlighter != nullptr;
}

/**
 * Leave a little space on the right side if the document is too long, so
 * that the search buttons don't get visually blocked by the scroll bar
 */
void QMarkdownTextEdit::adjustRightMargin() {
    QMargins margins = layout()->contentsMargins();
    const int rightMargin =
        document()->size().height() > viewport()->size().height() ? 24 : 0;
    margins.setRight(rightMargin);
    layout()->setContentsMargins(margins);
}

bool QMarkdownTextEdit::eventFilter(QObject *obj, QEvent *event) {
    // qDebug() << event->type();
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Control) {
            updateLinkCursor(viewport()->mapFromGlobal(QCursor::pos()),
                             QGuiApplication::keyboardModifiers());
        }

        // disallow keys if text edit hasn't focus
        if (!this->hasFocus()) {
            return true;
        }

        if ((keyEvent->key() == Qt::Key_Escape) && _blockSelectionActive) {
            clearBlockSelection();
            return true;
        } else if ((keyEvent->key() == Qt::Key_Escape) &&
                   _searchWidget->isVisible()) {
            _searchWidget->deactivate();
            return true;
        } else if (keyEvent->key() == Qt::Key_Insert &&
                   keyEvent->modifiers().testFlag(Qt::NoModifier)) {
            setOverwriteMode(!overwriteMode());

            // This solves a UI glitch if the visual cursor was not properly
            // updated when characters have different widths
            QTextCursor cursor = this->textCursor();
            cursor.movePosition(QTextCursor::Right);
            setTextCursor(cursor);
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);

            return false;
        } else if ((keyEvent->key() == Qt::Key_Tab) ||
                   (keyEvent->key() == Qt::Key_Backtab)) {
            // handle entered tab and reverse tab keys
            return handleTabEntered(keyEvent->key() == Qt::Key_Backtab);
        } else if ((keyEvent->key() == Qt::Key_F) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            _searchWidget->activate();
            return true;
        } else if ((keyEvent->key() == Qt::Key_R) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            _searchWidget->activateReplace();
            return true;
            //        } else if (keyEvent->key() == Qt::Key_Delete) {
        } else if (keyEvent->key() == Qt::Key_Backspace) {
            return handleBackspaceEntered();
        } else if (keyEvent->key() == Qt::Key_Asterisk) {
            return handleBracketClosing(QLatin1Char('*'));
        } else if (keyEvent->key() == Qt::Key_QuoteDbl) {
            return quotationMarkCheck(QLatin1Char('"'));
            // apostrophe bracket closing is temporary disabled because
            // apostrophes are used in different contexts
            //        } else if (keyEvent->key() == Qt::Key_Apostrophe) {
            //            return handleBracketClosing("'");
            // underline bracket closing is temporary disabled because
            // underlines are used in different contexts
            //        } else if (keyEvent->key() == Qt::Key_Underscore) {
            //            return handleBracketClosing("_");
        } else if (keyEvent->key() == Qt::Key_QuoteLeft) {
            return quotationMarkCheck(QLatin1Char('`'));
        } else if (keyEvent->key() == Qt::Key_AsciiTilde) {
            return handleBracketClosing(QLatin1Char('~'));
#ifdef Q_OS_MAC
        } else if (keyEvent->modifiers().testFlag(Qt::AltModifier) &&
                   keyEvent->key() == Qt::Key_ParenLeft) {
            // bracket closing for US keyboard on macOS
            return handleBracketClosing(QLatin1Char('{'), QLatin1Char('}'));
#endif
        } else if (keyEvent->key() == Qt::Key_ParenLeft) {
            return handleBracketClosing(QLatin1Char('('), QLatin1Char(')'));
        } else if (keyEvent->key() == Qt::Key_BraceLeft) {
            return handleBracketClosing(QLatin1Char('{'), QLatin1Char('}'));
        } else if (keyEvent->key() == Qt::Key_BracketLeft) {
            return handleBracketClosing(QLatin1Char('['), QLatin1Char(']'));
        } else if (keyEvent->key() == Qt::Key_Less) {
            return handleBracketClosing(QLatin1Char('<'), QLatin1Char('>'));
#ifdef Q_OS_MAC
        } else if (keyEvent->modifiers().testFlag(Qt::AltModifier) &&
                   keyEvent->key() == Qt::Key_ParenRight) {
            // bracket closing for US keyboard on macOS
            return bracketClosingCheck(QLatin1Char('{'), QLatin1Char('}'));
#endif
        } else if (keyEvent->key() == Qt::Key_ParenRight) {
            return bracketClosingCheck(QLatin1Char('('), QLatin1Char(')'));
        } else if (keyEvent->key() == Qt::Key_BraceRight) {
            return bracketClosingCheck(QLatin1Char('{'), QLatin1Char('}'));
        } else if (keyEvent->key() == Qt::Key_BracketRight) {
            return bracketClosingCheck(QLatin1Char('['), QLatin1Char(']'));
        } else if (keyEvent->key() == Qt::Key_Greater) {
            return bracketClosingCheck(QLatin1Char('<'), QLatin1Char('>'));
        } else if ((keyEvent->key() == Qt::Key_Return ||
                    keyEvent->key() == Qt::Key_Enter) &&
                   !isReadOnly() &&
                   keyEvent->modifiers().testFlag(Qt::ShiftModifier) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            QTextCursor cursor = this->textCursor();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.insertText(QStringLiteral("\n"));
            cursor.movePosition(QTextCursor::Up);
            setTextCursor(cursor);
            return true;
        } else if ((keyEvent->key() == Qt::Key_Return ||
                    keyEvent->key() == Qt::Key_Enter) &&
                   !isReadOnly() &&
                   keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
            QTextCursor cursor = this->textCursor();
            cursor.insertText("  \n");
            return true;
        } else if ((keyEvent->key() == Qt::Key_Return ||
                    keyEvent->key() == Qt::Key_Enter) &&
                   !isReadOnly() &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            QTextCursor cursor = this->textCursor();
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertText(QStringLiteral("\n"));
            setTextCursor(cursor);
            return true;
        } else if (keyEvent == QKeySequence::Copy ||
                   keyEvent == QKeySequence::Cut) {
            // Handle block (rectangular) selection copy/cut
            if (_blockSelectionActive && _blockSelStartBlock >= 0) {
                const QString text = blockSelectionText();
                if (keyEvent == QKeySequence::Cut) {
                    removeBlockSelectionText();
                    clearBlockSelection();
                }
                qApp->clipboard()->setText(text);
                return true;
            }
            QTextCursor cursor = this->textCursor();
            if (!cursor.hasSelection()) {
                QString text;
                if (cursor.block().length() <= 1)    // no content
                    text = "\n";
                else {
                    // cursor.select(QTextCursor::BlockUnderCursor); //
                    // negative, it will include the previous paragraph
                    // separator
                    cursor.movePosition(QTextCursor::StartOfBlock);
                    cursor.movePosition(QTextCursor::EndOfBlock,
                                        QTextCursor::KeepAnchor);
                    text = cursor.selectedText();
                    if (!cursor.atEnd()) {
                        text += "\n";
                        // this is the paragraph separator
                        cursor.movePosition(QTextCursor::NextCharacter,
                                            QTextCursor::KeepAnchor, 1);
                    }
                }
                if (keyEvent == QKeySequence::Cut) {
                    if (!cursor.atEnd() && text == "\n")
                        cursor.deletePreviousChar();
                    else
                        cursor.removeSelectedText();
                    cursor.movePosition(QTextCursor::StartOfBlock);
                    setTextCursor(cursor);
                }
                qApp->clipboard()->setText(text);
                return true;
            }
        } else if ((keyEvent->key() == Qt::Key_Down) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
                   keyEvent->modifiers().testFlag(Qt::AltModifier)) {
            // duplicate text with `Ctrl + Alt + Down`
            duplicateText();
            return true;
#ifndef Q_OS_MAC
        } else if ((keyEvent->key() == Qt::Key_Down) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
                   !keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
            // scroll the page down
            auto *scrollBar = verticalScrollBar();
            scrollBar->setSliderPosition(scrollBar->sliderPosition() + 1);
            return true;
        } else if ((keyEvent->key() == Qt::Key_Up) &&
                   keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
                   !keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
            // scroll the page up
            auto *scrollBar = verticalScrollBar();
            scrollBar->setSliderPosition(scrollBar->sliderPosition() - 1);
            return true;
#endif
        } else if ((keyEvent->key() == Qt::Key_Down) &&
                   keyEvent->modifiers().testFlag(Qt::NoModifier)) {
            // if you are in the last line and press cursor down the cursor will
            // jump to the end of the line
            QTextCursor cursor = textCursor();
            if (cursor.position() >= document()->lastBlock().position()) {
                cursor.movePosition(QTextCursor::EndOfLine);

                // check if we are really in the last line, not only in
                // the last block
                if (cursor.atBlockEnd()) {
                    setTextCursor(cursor);
                }
            }
            return QPlainTextEdit::eventFilter(obj, event);
        } else if ((keyEvent->key() == Qt::Key_Up) &&
                   keyEvent->modifiers().testFlag(Qt::NoModifier)) {
            // if you are in the first line and press cursor up the cursor will
            // jump to the start of the line
            QTextCursor cursor = textCursor();
            QTextBlock block = document()->firstBlock();
            int endOfFirstLinePos = block.position() + block.length();

            if (cursor.position() <= endOfFirstLinePos) {
                cursor.movePosition(QTextCursor::StartOfLine);

                // check if we are really in the first line, not only in
                // the first block
                if (cursor.atBlockStart()) {
                    setTextCursor(cursor);
                }
            }
            return QPlainTextEdit::eventFilter(obj, event);
        } else if (keyEvent->key() == Qt::Key_Return ||
                   keyEvent->key() == Qt::Key_Enter) {
            return handleReturnEntered();
        } else if ((keyEvent->key() == Qt::Key_F3)) {
            _searchWidget->doSearch(
                !keyEvent->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        } else if ((keyEvent->key() == Qt::Key_Z) &&
                   (keyEvent->modifiers().testFlag(Qt::ControlModifier)) &&
                   !(keyEvent->modifiers().testFlag(Qt::ShiftModifier))) {
            undo();
            return true;
        } else if ((keyEvent->key() == Qt::Key_Down) &&
                   (keyEvent->modifiers().testFlag(Qt::ControlModifier)) &&
                   (keyEvent->modifiers().testFlag(Qt::ShiftModifier))) {
            moveTextUpDown(false);
            return true;
        } else if ((keyEvent->key() == Qt::Key_Up) &&
                   (keyEvent->modifiers().testFlag(Qt::ControlModifier)) &&
                   (keyEvent->modifiers().testFlag(Qt::ShiftModifier))) {
            moveTextUpDown(true);
            return true;
#ifdef Q_OS_MAC
            // https://github.com/pbek/QOwnNotes/issues/1593
            // https://github.com/pbek/QOwnNotes/issues/2643
        } else if (keyEvent->key() == Qt::Key_Home) {
            QTextCursor cursor = textCursor();
            // Meta is Control on macOS
            cursor.movePosition(
                keyEvent->modifiers().testFlag(Qt::MetaModifier)
                    ? QTextCursor::Start
                    : QTextCursor::StartOfLine,
                keyEvent->modifiers().testFlag(Qt::ShiftModifier)
                    ? QTextCursor::KeepAnchor
                    : QTextCursor::MoveAnchor);
            this->setTextCursor(cursor);
            return true;
        } else if (keyEvent->key() == Qt::Key_End) {
            QTextCursor cursor = textCursor();
            // Meta is Control on macOS
            cursor.movePosition(
                keyEvent->modifiers().testFlag(Qt::MetaModifier)
                    ? QTextCursor::End
                    : QTextCursor::EndOfLine,
                keyEvent->modifiers().testFlag(Qt::ShiftModifier)
                    ? QTextCursor::KeepAnchor
                    : QTextCursor::MoveAnchor);
            this->setTextCursor(cursor);
            return true;
#endif
        }

        return QPlainTextEdit::eventFilter(obj, event);
    } else if (event->type() == QEvent::KeyRelease) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Control) {
            updateLinkCursor(viewport()->mapFromGlobal(QCursor::pos()),
                             QGuiApplication::keyboardModifiers());
        }

        return QPlainTextEdit::eventFilter(obj, event);
    } else if (event->type() == QEvent::MouseButtonRelease) {
        _mouseButtonDown = false;
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        // track `Ctrl + Click` and `Ctrl + Shift + Click` in the text edit
        if ((obj == this->viewport()) &&
            (mouseEvent->button() == Qt::LeftButton)) {
            auto modifiers = QGuiApplication::keyboardModifiers();

            // Check for Ctrl+Shift+Click (open in new tab)
            if (modifiers.testFlag(Qt::ControlModifier) &&
                modifiers.testFlag(Qt::ShiftModifier)) {
                _openLinkInNewTab = true;
                openLinkAtCursorPosition();
                _openLinkInNewTab = false;
                return true;
            }
            // Check for Ctrl+Click (open in current tab)
            else if (modifiers == Qt::ControlModifier ||
                     modifiers == Qt::ExtraButton24) {
                _openLinkInNewTab = false;
                openLinkAtCursorPosition();
                return true;
            }
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        _mouseButtonDown = true;
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        _mouseButtonDown = true;
    } else if (event->type() == QEvent::Wheel) {
        auto *wheel = dynamic_cast<QWheelEvent *>(event);

        // emit zoom signals
        if (wheel->modifiers() == Qt::ControlModifier) {
            if (wheel->angleDelta().y() > 0) {
                Q_EMIT zoomIn();
            } else {
                Q_EMIT zoomOut();
            }

            return true;
        }
    }

    return QPlainTextEdit::eventFilter(obj, event);
}

void QMarkdownTextEdit::centerTheCursor() {
    if (_mouseButtonDown || !_centerCursor) {
        return;
    }

    // Centers the cursor every time, but not on the top and bottom,
    // bottom is done by setCenterOnScroll() in updateSettings()
    centerCursor();

    /*
        QRect cursor = cursorRect();
        QRect vp = viewport()->rect();

        qDebug() << __func__ << " - 'cursor.top': " << cursor.top();
        qDebug() << __func__ << " - 'cursor.bottom': " << cursor.bottom();
        qDebug() << __func__ << " - 'vp': " << vp.bottom();

        int bottom = 0;
        int top = 0;

        qDebug() << __func__ << " - 'viewportMargins().top()': "
                 << viewportMargins().top();

        qDebug() << __func__ << " - 'viewportMargins().bottom()': "
                 << viewportMargins().bottom();

        int vpBottom = viewportMargins().top() + viewportMargins().bottom() +
    vp.bottom(); int vpCenter = vpBottom / 2; int cBottom = cursor.bottom() +
    viewportMargins().top();

        qDebug() << __func__ << " - 'vpBottom': " << vpBottom;
        qDebug() << __func__ << " - 'vpCenter': " << vpCenter;
        qDebug() << __func__ << " - 'cBottom': " << cBottom;


        if (cBottom >= vpCenter) {
            bottom = cBottom + viewportMargins().top() / 2 +
    viewportMargins().bottom() / 2 - (vp.bottom() / 2);
    //        bottom = cBottom - (vp.bottom() / 2);
    //        bottom *= 1.5;
        }

    //    setStyleSheet(QString("QPlainTextEdit {padding-bottom:
    %1px;}").arg(QString::number(bottom)));

    //    if (cursor.top() < (vp.bottom() / 2)) {
    //        top = (vp.bottom() / 2) - cursor.top() + viewportMargins().top() /
    2 + viewportMargins().bottom() / 2;
    ////        top *= -1;
    ////        bottom *= 1.5;
    //    }
        qDebug() << __func__ << " - 'top': " << top;
        qDebug() << __func__ << " - 'bottom': " << bottom;
        setViewportMargins(0,top,0, bottom);


    //    QScrollBar* scrollbar = verticalScrollBar();
    //
    //    qDebug() << __func__ << " - 'scrollbar->value();': " <<
    scrollbar->value();;
    //    qDebug() << __func__ << " - 'scrollbar->maximum();': "
    //             << scrollbar->maximum();;


    //    scrollbar->setValue(scrollbar->value() - offset.y());
    //
    //    setViewportMargins

    //    setViewportMargins(0, 0, 0, bottom);
    */
}

/*
 * Handle the undo event ourselves
 * Retains the selected text as selected after undo if
 * bracket closing was used otherwise performs normal undo
 */
void QMarkdownTextEdit::undo() {
    if (isReadOnly()) {
        return;
    }

    QTextCursor cursor = textCursor();
    // if no text selected, call undo
    if (!cursor.hasSelection()) {
        QPlainTextEdit::undo();
        return;
    }

    // if text is selected and bracket closing was used
    // we retain our selection
    if (_handleBracketClosingUsed) {
        // get the selection
        int selectionEnd = cursor.selectionEnd();
        int selectionStart = cursor.selectionStart();
        // call undo
        QPlainTextEdit::undo();
        // select again
        cursor.setPosition(selectionStart - 1);
        cursor.setPosition(selectionEnd - 1, QTextCursor::KeepAnchor);
        this->setTextCursor(cursor);
        _handleBracketClosingUsed = false;
    } else {
        // if text was selected but bracket closing wasn't used
        // do normal undo
        QPlainTextEdit::undo();
        return;
    }
}

void QMarkdownTextEdit::moveTextUpDown(bool up) {
    if (isReadOnly()) {
        return;
    }

    QTextCursor cursor = textCursor();
    QTextCursor move = cursor;

    move.setVisualNavigation(false);

    move.beginEditBlock();    // open an edit block to keep undo operations sane
    bool hasSelection = cursor.hasSelection();

    if (hasSelection) {
        // if there's a selection inside the block, we select the whole block
        move.setPosition(cursor.selectionStart());
        move.movePosition(QTextCursor::StartOfBlock);
        move.setPosition(cursor.selectionEnd(), QTextCursor::KeepAnchor);
        move.movePosition(
            move.atBlockStart() ? QTextCursor::Left : QTextCursor::EndOfBlock,
            QTextCursor::KeepAnchor);
    } else {
        move.movePosition(QTextCursor::StartOfBlock);
        move.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }

    // get the text of the current block
    QString text = move.selectedText();

    move.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
    move.removeSelectedText();

    if (up) {    // up key
        move.movePosition(QTextCursor::PreviousBlock);
        move.insertBlock();
        move.movePosition(QTextCursor::Left);
    } else {    // down key
        move.movePosition(QTextCursor::EndOfBlock);
        if (move.atBlockStart()) {    // empty block
            move.movePosition(QTextCursor::NextBlock);
            move.insertBlock();
            move.movePosition(QTextCursor::Left);
        } else {
            move.insertBlock();
        }
    }

    int start = move.position();
    move.clearSelection();
    move.insertText(text);
    int end = move.position();

    // reselect
    if (hasSelection) {
        move.setPosition(end);
        move.setPosition(start, QTextCursor::KeepAnchor);
    } else {
        move.setPosition(start);
    }

    move.endEditBlock();

    setTextCursor(move);
}

void QMarkdownTextEdit::setLineNumberEnabled(bool enabled) {
    _lineNumArea->setLineNumAreaEnabled(enabled);
    updateLineNumberAreaWidth(0);
}

/**
 * Resets the cursor to Qt::IBeamCursor
 */
void QMarkdownTextEdit::resetMouseCursor() const {
    QWidget *viewPort = viewport();
    viewPort->setCursor(Qt::IBeamCursor);
}

bool QMarkdownTextEdit::hasMarkdownUrlAtViewportPosition(
    const QPoint &position) {
    QTextCursor cursor = cursorForPosition(position);
    const int clickedPosition = cursor.position();

    cursor.movePosition(QTextCursor::StartOfBlock);
    const int positionFromStart = clickedPosition - cursor.position();
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    return !getMarkdownUrlAtPosition(cursor.selectedText(), positionFromStart)
                .isEmpty();
}

void QMarkdownTextEdit::updateLinkCursor(const QPoint &position,
                                         Qt::KeyboardModifiers modifiers) {
    if (modifiers.testFlag(Qt::ControlModifier) &&
        hasMarkdownUrlAtViewportPosition(position)) {
        viewport()->setCursor(Qt::PointingHandCursor);
        return;
    }

    resetMouseCursor();
}

/**
 * Resets the cursor to Qt::IBeamCursor if the widget looses the focus
 */
void QMarkdownTextEdit::focusOutEvent(QFocusEvent *event) {
    if (_hangingCursorRepaintTimer) {
        _hangingCursorRepaintTimer->stop();
    }
    resetMouseCursor();
    QPlainTextEdit::focusOutEvent(event);
}

void QMarkdownTextEdit::focusInEvent(QFocusEvent *event) {
    updateHangingCursorRepaintState();

    QPlainTextEdit::focusInEvent(event);
}

/**
 * Enters a closing character after an opening character if needed
 *
 * @param openingCharacter
 * @param closingCharacter
 * @return
 */
bool QMarkdownTextEdit::handleBracketClosing(const QChar openingCharacter,
                                             QChar closingCharacter) {
    // check if bracket closing or read-only are enabled
    if (!(_autoTextOptions & AutoTextOption::BracketClosing) || isReadOnly()) {
        return false;
    }

    QTextCursor cursor = textCursor();

    if (closingCharacter.isNull()) {
        closingCharacter = openingCharacter;
    }

    const QString selectedText = cursor.selectedText();

    // When user currently has text selected, we prepend the openingCharacter
    // and append the closingCharacter. E.g. 'text' -> '(text)'. We keep the
    // current selectedText selected.
    if (!selectedText.isEmpty()) {
        // Insert. The selectedText is overwritten.
        const QString newText =
            openingCharacter + selectedText + closingCharacter;
        cursor.insertText(newText);

        // Re-select the selectedText.
        const int selectionEnd = cursor.position() - 1;
        const int selectionStart = selectionEnd - selectedText.length();

        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
        this->setTextCursor(cursor);
        _handleBracketClosingUsed = true;
        return true;
    }

    // get the current text from the block (inserted character not included)
    // Remove whitespace at start of string (e.g. in multilevel-lists).
    static QRegularExpression regex1("^\\s+");
    const QString text = cursor.block().text().remove(regex1);

    const int pib = cursor.positionInBlock();
    bool isPreviousAsterisk =
        pib > 0 && pib < text.length() && text.at(pib - 1) == '*';
    bool isNextAsterisk = pib < text.length() && text.at(pib) == '*';
    bool isMaybeBold = isPreviousAsterisk && isNextAsterisk;
    if (pib < text.length() && !isMaybeBold && !text.at(pib).isSpace()) {
        return false;
    }

    // Default positions to move the cursor back.
    int cursorSubtract = 1;
    // Special handling for `*` opening character, as this could be:
    // - start of a list (or sublist);
    // - start of a bold text;
    if (openingCharacter == QLatin1Char('*')) {
        // don't auto complete in code block
        bool isInCode =
            MarkdownHighlighter::isCodeBlock(cursor.block().userState());
        // we only do auto completion if there is a space before the cursor pos
        bool hasSpaceOrAsteriskBefore = !text.isEmpty() && pib > 0 &&
                                        (text.at(pib - 1).isSpace() ||
                                         text.at(pib - 1) == QLatin1Char('*'));
        // This could be the start of a list, don't autocomplete.
        bool isEmpty = text.isEmpty();

        if (isInCode || !hasSpaceOrAsteriskBefore || isEmpty) {
            return false;
        }

        // bold
        if (isPreviousAsterisk && isNextAsterisk) {
            cursorSubtract = 1;
        }

        // User wants: '**'.
        // Not the start of a list, probably bold text. We autocomplete with
        // extra closingCharacter and cursorSubtract to 'catchup'.
        if (text == QLatin1String("*")) {
            cursor.insertText(QStringLiteral("*"));
            cursorSubtract = 2;
        }
    }

    // Auto-completion for ``` pair
    if (openingCharacter == QLatin1Char('`')) {
#if QT_VERSION < QT_VERSION_CHECK(5, 12, 0)
        if (QRegExp(QStringLiteral("[^`]*``")).exactMatch(text)) {
#else
        if (QRegularExpression(
                QRegularExpression::anchoredPattern(QStringLiteral("[^`]*``")))
                .match(text)
                .hasMatch()) {
#endif
            cursor.insertText(QStringLiteral("``"));
            cursorSubtract = 3;
        }
    }

    // don't auto complete in code block
    if (openingCharacter == QLatin1Char('<') &&
        MarkdownHighlighter::isCodeBlock(cursor.block().userState())) {
        return false;
    }

    cursor.beginEditBlock();
    cursor.insertText(openingCharacter);
    cursor.insertText(closingCharacter);
    cursor.setPosition(cursor.position() - cursorSubtract);
    cursor.endEditBlock();

    setTextCursor(cursor);
    return true;
}

/**
 * Checks if the closing character should be output or not
 *
 * @param openingCharacter
 * @param closingCharacter
 * @return
 */
bool QMarkdownTextEdit::bracketClosingCheck(const QChar openingCharacter,
                                            QChar closingCharacter) {
    // check if bracket closing or read-only are enabled
    if (!(_autoTextOptions & AutoTextOption::BracketClosing) || isReadOnly()) {
        return false;
    }

    if (closingCharacter.isNull()) {
        closingCharacter = openingCharacter;
    }

    QTextCursor cursor = textCursor();
    const int positionInBlock = cursor.positionInBlock();

    // get the current text from the block
    const QString text = cursor.block().text();
    const int textLength = text.length();

    // if we are at the end of the line we just want to enter the character
    if (positionInBlock >= textLength) {
        return false;
    }

    const QChar currentChar = text.at(positionInBlock);

    //    if (closingCharacter == openingCharacter) {

    //    }

    qDebug() << __func__ << " - 'currentChar': " << currentChar;

    // if the current character is not the closing character we just want to
    // enter the character
    if (currentChar != closingCharacter) {
        return false;
    }

    const QString leftText = text.left(positionInBlock);
    const int openingCharacterCount = leftText.count(openingCharacter);
    const int closingCharacterCount = leftText.count(closingCharacter);

    // if there were enough opening characters just enter the character
    if (openingCharacterCount < (closingCharacterCount + 1)) {
        return false;
    }

    // move the cursor to the right and don't enter the character
    cursor.movePosition(QTextCursor::Right);
    setTextCursor(cursor);
    return true;
}

/**
 * Checks if the closing character should be output or not or if a closing
 * character after an opening character if needed
 *
 * @param quotationCharacter
 * @return
 */
bool QMarkdownTextEdit::quotationMarkCheck(const QChar quotationCharacter) {
    // check if bracket closing or read-only are enabled
    if (!(_autoTextOptions & AutoTextOption::BracketClosing) || isReadOnly()) {
        return false;
    }

    QTextCursor cursor = textCursor();
    const int positionInBlock = cursor.positionInBlock();

    // get the current text from the block
    const QString text = cursor.block().text();
    const int textLength = text.length();

    // if last char is not space, we are at word end, no autocompletion
    const bool isBacktick = quotationCharacter == '`';
    if (!isBacktick && positionInBlock != 0 &&
        !text.at(positionInBlock - 1).isSpace()) {
        return false;
    }

    // if we are at the end of the line we just want to enter the character
    if (positionInBlock >= textLength) {
        return handleBracketClosing(quotationCharacter);
    }

    const QChar currentChar = text.at(positionInBlock);

    // if the current character is not the quotation character we just want to
    // enter the character
    if (currentChar != quotationCharacter) {
        return handleBracketClosing(quotationCharacter);
    }

    // move the cursor to the right and don't enter the character
    cursor.movePosition(QTextCursor::Right);
    setTextCursor(cursor);
    return true;
}

/***********************************
 * helper methods for char removal
 * Rules for (') and ("):
 * if [sp]" -> opener (sp = space)
 * if "[sp] -> closer
 ***********************************/
bool isQuotOpener(int position, const QString &text) {
    if (position == 0) return true;
    const int prevCharPos = position - 1;
    return text.at(prevCharPos).isSpace();
}
bool isQuotCloser(int position, const QString &text) {
    const int nextCharPos = position + 1;
    if (nextCharPos >= text.length()) return true;
    return text.at(nextCharPos).isSpace();
}

/**
 * Handles removing of matching brackets and other Markdown characters
 * Only works with backspace to remove text
 *
 * @return
 */
bool QMarkdownTextEdit::handleBackspaceEntered() {
    if (!(_autoTextOptions & AutoTextOption::BracketRemoval) || isReadOnly()) {
        return false;
    }

    QTextCursor cursor = textCursor();

    // return if some text was selected
    if (!cursor.selectedText().isEmpty()) {
        return false;
    }

    int position = cursor.position();
    const int positionInBlock = cursor.positionInBlock();
    int block = cursor.block().blockNumber();

    if (_highlighter)
        if (_highlighter->isPosInACodeSpan(block, positionInBlock - 1))
            return false;

    // return if backspace was pressed at the beginning of a block
    if (positionInBlock == 0) {
        return false;
    }

    // get the current text from the block
    const QString text = cursor.block().text();

    char charToRemove{};

    // current char
    const char charInFront = text.at(positionInBlock - 1).toLatin1();

    if (charInFront == '*')
        return handleCharRemoval(MarkdownHighlighter::RangeType::Emphasis,
                                 block, positionInBlock - 1);
    else if (charInFront == '`')
        return handleCharRemoval(MarkdownHighlighter::RangeType::CodeSpan,
                                 block, positionInBlock - 1);

    // handle removal of ", ', and brackets

    // is it opener?
    int pos = _openingCharacters.indexOf(charInFront);
    // for " and '
    bool isOpener = false;
    bool isCloser = false;
    if (pos == 5 || pos == 6) {
        isOpener = isQuotOpener(positionInBlock - 1, text);
    } else {
        isOpener = pos != -1;
    }
    if (isOpener) {
        charToRemove = _closingCharacters.at(pos);
    } else {
        // is it closer?
        pos = _closingCharacters.indexOf(charInFront);
        if (pos == 5 || pos == 6)
            isCloser = isQuotCloser(positionInBlock - 1, text);
        else
            isCloser = pos != -1;
        if (isCloser)
            charToRemove = _openingCharacters.at(pos);
        else
            return false;
    }

    int charToRemoveIndex = -1;
    if (isOpener) {
        bool closer = true;
        charToRemoveIndex = text.indexOf(charToRemove, positionInBlock);
        if (charToRemoveIndex == -1) return false;
        if (pos == 5 || pos == 6)
            closer = isQuotCloser(charToRemoveIndex, text);
        if (!closer) return false;
        cursor.setPosition(position + (charToRemoveIndex - positionInBlock));
        cursor.deleteChar();
    } else if (isCloser) {
        charToRemoveIndex = text.lastIndexOf(charToRemove, positionInBlock - 2);
        if (charToRemoveIndex == -1) return false;
        bool opener = true;
        if (pos == 5 || pos == 6)
            opener = isQuotOpener(charToRemoveIndex, text);
        if (!opener) return false;
        const int pos = position - (positionInBlock - charToRemoveIndex);
        cursor.setPosition(pos);
        cursor.deleteChar();
        position -= 1;
    } else {
        charToRemoveIndex = text.lastIndexOf(charToRemove, positionInBlock - 2);
        if (charToRemoveIndex == -1) return false;
        const int pos = position - (positionInBlock - charToRemoveIndex);
        cursor.setPosition(pos);
        cursor.deleteChar();
        position -= 1;
    }

    // moving the cursor back to the old position so the previous character
    // can be removed
    cursor.setPosition(position);
    setTextCursor(cursor);
    return false;
}

bool QMarkdownTextEdit::handleCharRemoval(MarkdownHighlighter::RangeType type,
                                          int block, int position) {
    if (!_highlighter) return false;

    auto range = _highlighter->findPositionInRanges(type, block, position);
    if (range == QPair<int, int>{-1, -1}) return false;

    int charToRemovePos = range.first;
    if (position == range.first) charToRemovePos = range.second;

    QTextCursor cursor = textCursor();
    auto gpos = cursor.position();

    if (charToRemovePos > position) {
        cursor.setPosition(gpos + (charToRemovePos - (position + 1)));
    } else {
        cursor.setPosition(gpos - (position - charToRemovePos + 1));
        gpos--;
    }

    cursor.deleteChar();
    cursor.setPosition(gpos);
    setTextCursor(cursor);
    return false;
}

void QMarkdownTextEdit::updateLineNumAreaGeometry() {
    const auto contentsRect = this->contentsRect();
    const QRect newGeometry = {contentsRect.left(), contentsRect.top(),
                               _lineNumArea->sizeHint().width(),
                               contentsRect.height()};
    auto oldGeometry = _lineNumArea->geometry();
    if (newGeometry != oldGeometry) {
        _lineNumArea->setGeometry(newGeometry);
    }
}

void QMarkdownTextEdit::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    updateLineNumAreaGeometry();
}

/**
 * Increases (or decreases) the indention of the selected text
 * (if there is a text selected) in the noteTextEdit
 * @return
 */
bool QMarkdownTextEdit::increaseSelectedTextIndention(
    bool reverse, const QString &indentCharacters) {
    QTextCursor cursor = this->textCursor();
    QString selectedText = cursor.selectedText();

    if (!selectedText.isEmpty()) {
        // Start the selection at start of the first block of the selection
        int end = cursor.selectionEnd();
        cursor.setPosition(cursor.selectionStart());
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        this->setTextCursor(cursor);
        selectedText = cursor.selectedText();

        // we need this strange newline character we are getting in the
        // selected text for newlines
        const QString newLine =
            QString::fromUtf8(QByteArray::fromHex(QByteArrayLiteral("e280a9")));
        QString newText;

        if (reverse) {
            // un-indent text

            const int indentSize = indentCharacters == QStringLiteral("\t")
                                       ? 4
                                       : indentCharacters.length();

            // remove leading \t or spaces in following lines
            newText = selectedText.replace(
                QRegularExpression(newLine + QStringLiteral("(\\t| {1,") +
                                   QString::number(indentSize) +
                                   QStringLiteral("})")),
                QStringLiteral("\n"));

            // remove leading \t or spaces in first line
            newText.remove(QRegularExpression(QStringLiteral("^(\\t| {1,") +
                                              QString::number(indentSize) +
                                              QStringLiteral("})")));
        } else {
            // replace trailing new line to prevent an indent of the line after
            // the selection
            newText = selectedText.replace(
                QRegularExpression(QRegularExpression::escape(newLine) +
                                   QStringLiteral("$")),
                QStringLiteral("\n"));

            // indent text
            newText.replace(newLine, QStringLiteral("\n") + indentCharacters)
                .prepend(indentCharacters);

            // remove trailing \t
            static QRegularExpression regex1(QStringLiteral("\\t$"));
            newText.remove(regex1);
        }

        // insert the new text
        cursor.insertText(newText);

        // update the selection to the new text
        cursor.setPosition(cursor.position() - newText.size(),
                           QTextCursor::KeepAnchor);
        this->setTextCursor(cursor);

        return true;
    } else if (reverse) {
        const int indentSize = indentCharacters.length();

        // do the check as often as we have characters to un-indent
        for (int i = 1; i <= indentSize; i++) {
            // if nothing was selected but we want to reverse the indention
            // check if there is a \t in front or after the cursor and remove it
            // if so
            const int position = cursor.position();

            if (!cursor.atStart()) {
                // get character in front of cursor
                cursor.setPosition(position - 1, QTextCursor::KeepAnchor);
            }

            // check for \t or space in front of cursor
            static QRegularExpression regex1(QStringLiteral("[\\t ]"));
            QRegularExpressionMatch match = regex1.match(cursor.selectedText());

            if (!match.hasMatch()) {
                // (select to) check for \t or space after the cursor
                cursor.setPosition(position);

                if (!cursor.atEnd()) {
                    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
                }
            }

            match = regex1.match(cursor.selectedText());

            if (match.hasMatch()) {
                cursor.removeSelectedText();
            }

            cursor = this->textCursor();
        }

        return true;
    }

    // else just insert indentCharacters
    cursor.insertText(indentCharacters);

    return true;
}

/**
 * @brief Opens the link (if any) at the current cursor position
 */
bool QMarkdownTextEdit::openLinkAtCursorPosition() {
    QTextCursor cursor = this->textCursor();
    const int clickedPosition = cursor.position();

    // select the text in the clicked block and find out on
    // which position we clicked
    cursor.movePosition(QTextCursor::StartOfBlock);
    const int positionFromStart = clickedPosition - cursor.position();
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    const QString selectedText = cursor.selectedText();

    // find out which url in the selected text was clicked
    const QString urlString =
        getMarkdownUrlAtPosition(selectedText, positionFromStart);
    const QUrl url = QUrl(urlString);
    const bool isRelativeFileUrl =
        urlString.startsWith(QLatin1String("file://.."));
    const bool isLegacyAttachmentUrl =
        urlString.startsWith(QLatin1String("file://attachments"));

    qDebug() << __func__ << " - 'emit urlClicked( urlString )': " << urlString;

    // Check if URL matches any ignored regular expressions
    for (const QRegularExpression &regex : _ignoredClickUrlRegexps) {
        if (regex.match(urlString).hasMatch()) {
            return true;    // URL matches ignored regex, don't open it
        }
    }

    // Return false if no URL was found at the cursor position
    if (urlString.isEmpty()) {
        return false;
    }

    // Emit signal to let derived classes handle URL opening
    Q_EMIT urlClicked(urlString);

    // Check if there are signal handlers connected
    const int numReceivers = receivers(SIGNAL(urlClicked(QString)));
    qDebug() << __func__
             << " - number of urlClicked receivers:" << numReceivers;
    qDebug() << __func__ << " - _openLinkInNewTab:" << _openLinkInNewTab;

    // If signal handlers are connected, they will handle opening the URL
    // Don't call openUrl() here to avoid duplicate opening
    if (numReceivers > 0) {
        qDebug()
            << __func__
            << " - Signal handlers present, letting them handle URL opening";
        return true;
    }

    // No signal handlers connected, use base class logic (backward
    // compatibility)
    qDebug() << __func__ << " - No signal handlers, using base class openUrl";
    if ((url.isValid() && isValidUrl(urlString)) || isRelativeFileUrl ||
        urlString.startsWith(QStringLiteral("wikilink:")) ||
        isLegacyAttachmentUrl) {
        // ignore some schemata
        if (!(_ignoredClickUrlSchemata.contains(url.scheme()) ||
              isRelativeFileUrl || isLegacyAttachmentUrl)) {
            // open the url
            openUrl(urlString, _openLinkInNewTab);
        }
        return true;
    }

    return false;
}

/**
 * Checks if urlString is a valid url
 *
 * @param urlString
 * @return
 */
bool QMarkdownTextEdit::isValidUrl(const QString &urlString) {
    static QRegularExpression regex(R"(^\w+:\/\/.+)");
    const QRegularExpressionMatch match = regex.match(urlString);
    return match.hasMatch();
}

/**
 * Handles clicked urls
 *
 * examples:
 * - <https://www.qownnotes.org> opens the webpage
 * - <file:///path/to/my/file/QOwnNotes.pdf> opens the file
 *   "/path/to/my/file/QOwnNotes.pdf" if the operating system supports that
 *  handler
 */
void QMarkdownTextEdit::openUrl(const QString &urlString, bool openInNewTab) {
    Q_UNUSED(openInNewTab)
    qDebug() << "QMarkdownTextEdit " << __func__
             << " - 'urlString': " << urlString;

    QDesktopServices::openUrl(QUrl(urlString));
}

/**
 * @brief Returns the highlighter instance
 * @return
 */
MarkdownHighlighter *QMarkdownTextEdit::highlighter() { return _highlighter; }

/**
 * @brief Returns the searchWidget instance
 * @return
 */
QPlainTextEditSearchWidget *QMarkdownTextEdit::searchWidget() {
    return _searchWidget;
}

/**
 * @brief Sets url schemata that will be ignored when clicked on
 * @param ignoredUrlSchemata
 */
void QMarkdownTextEdit::setIgnoredClickUrlSchemata(
    QStringList ignoredUrlSchemata) {
    _ignoredClickUrlSchemata = std::move(ignoredUrlSchemata);
}

/**
 * @brief Sets url regular expressions that will be ignored when clicked on
 * @param ignoredClickUrlRegexps
 */
void QMarkdownTextEdit::setIgnoredClickUrlRegexps(
    QList<QRegularExpression> ignoredClickUrlRegexps) {
    _ignoredClickUrlRegexps = std::move(ignoredClickUrlRegexps);
}

/**
 * @brief Strips trailing unbalanced bracket characters from a URL string.
 *
 * Per RFC 1738, characters like { } [ ] < > are unsafe in URLs and should be
 * percent-encoded. Parentheses ( ) are technically valid but users commonly
 * wrap URLs in them. This function strips trailing closing brackets that are
 * not balanced by corresponding opening brackets within the URL, so that
 * URLs like http://example.com/wiki/Page_(test) keep their balanced parens
 * while http://example.com/) strips the trailing ')'.
 */
static QString stripTrailingBrackets(const QString &url) {
    // Bracket pairs: opener -> closer
    static const QChar pairs[][2] = {
        {'(', ')'},
        {'{', '}'},
        {'[', ']'},
        {'<', '>'},
    };

    QString result = url;

    // Repeatedly strip trailing bracket characters that are unbalanced
    bool changed = true;
    while (changed && !result.isEmpty()) {
        changed = false;
        QChar last = result.at(result.length() - 1);

        for (const auto &pair : pairs) {
            if (last == pair[1]) {
                // Count openers and closers in the URL
                int openers = 0;
                int closers = 0;
                for (const QChar &ch : result) {
                    if (ch == pair[0])
                        ++openers;
                    else if (ch == pair[1])
                        ++closers;
                }

                // If closers exceed openers, the trailing one is unbalanced
                if (closers > openers) {
                    result.chop(1);
                    changed = true;
                }
                break;
            }

            // Also strip trailing openers (e.g. http://example.com/( )
            if (last == pair[0]) {
                result.chop(1);
                changed = true;
                break;
            }
        }
    }

    return result;
}

/**
 * @brief Returns a map of parsed Markdown urls with their link texts as key
 *
 * @param text
 * @return parsed urls
 */
QMap<QString, QString> QMarkdownTextEdit::parseMarkdownUrlsFromText(
    const QString &text) {
    QMap<QString, QString> urlMap;
    QRegularExpressionMatchIterator iterator;

    // match urls like this: <http://mylink>
    //    re = QRegularExpression("(<(.+?:\\/\\/.+?)>)");
    static QRegularExpression regex1(QStringLiteral("(<(.+?)>)"));
    iterator = regex1.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString linkText = match.captured(1);
        QString url = match.captured(2);
        urlMap[linkText] = url;
    }

    // match urls like this: [this url](http://mylink)
    //    QRegularExpression re("(\\[.*?\\]\\((.+?:\\/\\/.+?)\\))");
    static QRegularExpression regex2(R"((\[.*?\]\((.+?)\)))");
    iterator = regex2.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString linkText = match.captured(1);
        QString url = match.captured(2);
        urlMap[linkText] = url;
    }

    // match urls like this: http://mylink
    static QRegularExpression regex3(R"(\b\w+?:\/\/[^\s]+)");
    iterator = regex3.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString url = stripTrailingBrackets(match.captured(0));
        urlMap[url] = url;
    }

    // match urls like this: www.github.com
    static QRegularExpression regex4(R"(\bwww\.[^\s]+\.[^\s]+\b)");
    iterator = regex4.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString url = stripTrailingBrackets(match.captured(0));
        urlMap[url] = QStringLiteral("http://") + url;
    }

    // match reference urls like this: [this url][1] with this later:
    // [1]: http://domain
    static QRegularExpression regex5(R"((\[.*?\]\[(.+?)\]))");
    iterator = regex5.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString linkText = match.captured(1);
        QString referenceId = match.captured(2);

        // search for the referenced url in the whole text edit
        //        QRegularExpression refRegExp(
        //                "\\[" + QRegularExpression::escape(referenceId) +
        //                        "\\]: (.+?:\\/\\/.+)");
        QRegularExpression refRegExp(QStringLiteral("\\[") +
                                     QRegularExpression::escape(referenceId) +
                                     QStringLiteral("\\]: (.+)"));
        QRegularExpressionMatch urlMatch = refRegExp.match(toPlainText());

        if (urlMatch.hasMatch()) {
            QString url = urlMatch.captured(1);
            urlMap[linkText] = url;
        }
    }

    static const QRegularExpression wikiLinkRegex(QStringLiteral(
        R"(\[\[([^\[\]|]+?(?:#[^\[\]|]+?)?)(?:\|[^\[\]]*?)?\]\])"));
    iterator = wikiLinkRegex.globalMatch(text);
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        const QString fullMatch = match.captured(0);
        const QString noteName = match.captured(1).trimmed();
        // Percent-encode the note name so the resulting URL is valid even when
        // the note name contains spaces or other special characters
        const QString encodedName = QString::fromUtf8(
            QUrl::toPercentEncoding(noteName, QByteArrayLiteral("/-#")));
        urlMap[fullMatch] = QStringLiteral("wikilink:") + encodedName;
    }

    return urlMap;
}

/**
 * @brief Returns the Markdown url at position
 * @param text
 * @param position
 * @return url string
 */
QString QMarkdownTextEdit::getMarkdownUrlAtPosition(const QString &text,
                                                    int position) {
    QString url;

    // get a map of parsed Markdown urls with their link texts as key
    const QMap<QString, QString> urlMap = parseMarkdownUrlsFromText(text);
    QMap<QString, QString>::const_iterator i = urlMap.constBegin();
    for (; i != urlMap.constEnd(); ++i) {
        const QString &linkText = i.key();
        const QString &urlString = i.value();

        const int foundPositionStart = text.indexOf(linkText);

        if (foundPositionStart >= 0) {
            // calculate end position of found linkText
            const int foundPositionEnd = foundPositionStart + linkText.size();

            // check if position is in found string range
            if ((position >= foundPositionStart) &&
                (position <= foundPositionEnd)) {
                url = urlString;
                break;
            }
        }
    }

    return url;
}

/**
 * @brief Duplicates the text in the text edit
 */
void QMarkdownTextEdit::duplicateText() {
    if (isReadOnly()) {
        return;
    }

    QTextCursor cursor = this->textCursor();
    QString selectedText = cursor.selectedText();

    // duplicate line if no text was selected
    if (selectedText.isEmpty()) {
        const int position = cursor.position();

        // select the whole line
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

        const int positionDiff = cursor.position() - position;
        selectedText = "\n" + cursor.selectedText();

        // insert text with new line at end of the selected line
        cursor.setPosition(cursor.selectionEnd());
        cursor.insertText(selectedText);

        // set the position to same position it was in the duplicated line
        cursor.setPosition(cursor.position() - positionDiff);
    } else {
        // duplicate selected text
        cursor.setPosition(cursor.selectionEnd());
        const int selectionStart = cursor.position();

        // insert selected text
        cursor.insertText(selectedText);
        const int selectionEnd = cursor.position();

        // select the inserted text
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }

    this->setTextCursor(cursor);
}

void QMarkdownTextEdit::setText(const QString &text) { setPlainText(text); }

void QMarkdownTextEdit::setPlainText(const QString &text) {
    // clear the dirty blocks vector to increase performance and prevent
    // a possible crash in QSyntaxHighlighter::rehighlightBlock
    if (_highlighter) _highlighter->clearDirtyBlocks();

    QPlainTextEdit::setPlainText(text);
    adjustRightMargin();
}

/**
 * Uses another widget as parent for the search widget
 */
void QMarkdownTextEdit::initSearchFrame(QWidget *searchFrame, bool darkMode) {
    _searchFrame = searchFrame;

    // remove the search widget from our layout
    layout()->removeWidget(_searchWidget);

    QLayout *layout = _searchFrame->layout();

    // create a grid layout for the frame and add the search widget to it
    if (layout == nullptr) {
        layout = new QVBoxLayout(_searchFrame);
        layout->setSpacing(0);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    _searchWidget->setDarkMode(darkMode);
    layout->addWidget(_searchWidget);
    _searchFrame->setLayout(layout);
}

/**
 * Hides the text edit and the search widget
 */
void QMarkdownTextEdit::hide() {
    _searchWidget->hide();
    QWidget::hide();
}

/**
 * Handles an entered return key
 */
bool QMarkdownTextEdit::handleReturnEntered() {
    if (isReadOnly()) {
        return true;
    }

    // This will be the main cursor to add or remove text
    QTextCursor cursor = this->textCursor();

    // We need a 2nd cursor to get the text of the current block without moving
    // the main cursor that is used to remove the selected text
    QTextCursor cursor2 = this->textCursor();
    cursor2.select(QTextCursor::BlockUnderCursor);
    const QString currentLineText = cursor2.selectedText().trimmed();

    const int position = cursor.position();
    const bool cursorAtBlockStart = cursor.atBlockStart();
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    const QString currentLinePartialText = cursor.selectedText();

    // if return is pressed and there is just an unordered list symbol then we
    // want to remove the list symbol Valid listCharacters: '+ ', '-' , '* ', '+
    // [ ] ', '+ [x] ', '- [ ] ', '- [-] ', '- [x] ', '* [ ] ', '* [x] '.
    static QRegularExpression regex1(
        R"(^(\s*)([+|\-|\*] \[(x|-| |)\]|[+\-\*])(\s+)$)");
    QRegularExpressionMatchIterator iterator =
        regex1.globalMatch(currentLinePartialText);
    if (iterator.hasNext()) {
        cursor.removeSelectedText();
        return true;
    }

    // if return is pressed and there is just an ordered list symbol then we
    // want to remove the list symbol
    static QRegularExpression regex2(R"(^(\s*)(\d+[\.|\)])(\s+)$)");
    iterator = regex2.globalMatch(currentLinePartialText);
    if (iterator.hasNext()) {
        qDebug() << cursor.selectedText();
        cursor.removeSelectedText();
        return true;
    }

    // Check if we are in an unordered list.
    // We are in a list when we have '* ', '- ' or '+ ', possibly with preceding
    // whitespace. If e.g. user has entered '**text**' and pressed enter - we
    // don't want to do more list-stuff.
    QString currentLine = currentLinePartialText.trimmed();
    QChar char0;
    QChar char1;
    if (currentLine.length() >= 1) char0 = currentLine.at(0);
    if (currentLine.length() >= 2) char1 = currentLine.at(1);
    const bool inList =
        ((char0 == QLatin1Char('*') || char0 == QLatin1Char('-') ||
          char0 == QLatin1Char('+')) &&
         char1 == QLatin1Char(' '));

    if (inList) {
        // if the current line starts with a list character (possibly after
        // whitespaces) add the whitespaces at the next line too
        // Valid listCharacters: '+ ', '-' , '* ', '+ [ ] ', '+ [x] ', '- [ ] ',
        // '- [x] ', '- [-] ', '* [ ] ', '* [x] '.
        static QRegularExpression regex3(
            R"(^(\s*)([+|\-|\*] \[(x|-| |)\]|[+\-\*])(\s+))");
        iterator = regex3.globalMatch(currentLinePartialText);
        if (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            const QString whitespaces = match.captured(1);
            QString listCharacter = match.captured(2);
            const QString whitespaceCharacter = match.captured(4);

            static QRegularExpression regex4(R"(^([+|\-|\*]) \[(x| |\-|)\])");
            // start new checkbox list item with an unchecked checkbox
            iterator = regex4.globalMatch(listCharacter);
            if (iterator.hasNext()) {
                const QRegularExpressionMatch match1 = iterator.next();
                const QString realListCharacter = match1.captured(1);
                listCharacter = realListCharacter + QStringLiteral(" [ ]");
            }

            cursor.setPosition(position);
            cursor.insertText("\n" + whitespaces + listCharacter +
                              whitespaceCharacter);

            // scroll to the cursor if we are at the bottom of the document
            ensureCursorVisible();
            return true;
        }
    }

    // check for ordered lists and increment the list number in the next line
    static QRegularExpression regex5(R"(^(\s*)(\d+)([\.|\)])(\s+))");
    iterator = regex5.globalMatch(currentLinePartialText);
    if (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString whitespaces = match.captured(1);
        const uint listNumber = match.captured(2).toUInt();
        const QString listMarker = match.captured(3);
        const QString whitespaceCharacter = match.captured(4);

        cursor.setPosition(position);
        cursor.insertText("\n" + whitespaces + QString::number(listNumber + 1) +
                          listMarker + whitespaceCharacter);

        // scroll to the cursor if we are at the bottom of the document
        ensureCursorVisible();
        return true;
    }

    // intent next line with same whitespaces as in current line
    static QRegularExpression regex6(R"(^(\s+))");
    iterator = regex6.globalMatch(currentLinePartialText);
    if (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString whitespaces = match.captured(1);

        cursor.setPosition(position);
        cursor.insertText("\n" + whitespaces);

        // scroll to the cursor if we are at the bottom of the document
        ensureCursorVisible();
        return true;
    }

    // Add new list item above current line if we are at the start the line of a
    // list item
    if (cursorAtBlockStart) {
        static QRegularExpression regex7(
            R"(^([+|\-|\*] \[(x|-| |)\]|[+\-\*])(\s+))");
        iterator = regex7.globalMatch(currentLineText);
        if (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            QString listCharacter = match.captured(1);
            const QString whitespaceCharacter = match.captured(3);

            static QRegularExpression regex8(R"(^([+|\-|\*]) \[(x| |\-|)\])");
            // start new checkbox list item with an unchecked checkbox
            iterator = regex8.globalMatch(listCharacter);
            if (iterator.hasNext()) {
                const QRegularExpressionMatch match1 = iterator.next();
                const QString realListCharacter = match1.captured(1);
                listCharacter = realListCharacter + QStringLiteral(" [ ]");
            }

            cursor.setPosition(position);
            // Enter new list item above current line
            cursor.insertText(listCharacter + whitespaceCharacter + "\n");
            // Move the cursor at the end of the new list item
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);

            // scroll to the cursor if we are at the bottom of the document
            ensureCursorVisible();
            return true;
        }
    }

    return false;
}

/**
 * Handles entered tab or reverse tab keys
 */
bool QMarkdownTextEdit::handleTabEntered(bool reverse,
                                         const QString &indentCharacters) {
    if (isReadOnly()) {
        return true;
    }

    QTextCursor cursor = this->textCursor();

    // only check for lists if we haven't a text selected
    if (cursor.selectedText().isEmpty()) {
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        const QString currentLineText = cursor.selectedText();

        // check if we want to indent or un-indent an ordered list
        // Valid listCharacters: '+ ', '-' , '* ', '+ [ ] ', '+ [x] ', '- [ ] ',
        // '- [x] ', '- [-] ', '* [ ] ', '* [x] '.
        static QRegularExpression regex1(
            R"(^(\s*)([+|\-|\*] \[(x|-| )\]|[+\-\*])(\s+)$)");
        QRegularExpressionMatchIterator i = regex1.globalMatch(currentLineText);

        if (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString whitespaces = match.captured(1);
            const QString listCharacter = match.captured(2);
            const QString whitespaceCharacter = match.captured(4);

            // add or remove one tabulator key
            if (reverse) {
                // remove one set of indentCharacters or a tabulator
                whitespaces.remove(QRegularExpression(
                    QStringLiteral("^(\\t|") +
                    QRegularExpression::escape(indentCharacters) +
                    QStringLiteral(")")));

            } else {
                whitespaces += indentCharacters;
            }

            cursor.insertText(whitespaces + listCharacter +
                              whitespaceCharacter);
            return true;
        }

        // check if we want to indent or un-indent an ordered list
        static QRegularExpression regex2(R"(^(\s*)(\d+)([\.|\)])(\s+)$)");
        i = regex2.globalMatch(currentLineText);

        if (i.hasNext()) {
            const QRegularExpressionMatch match = i.next();
            QString whitespaces = match.captured(1);
            const QString listCharacter = match.captured(2);
            const QString listMarker = match.captured(3);
            const QString whitespaceCharacter = match.captured(4);

            // add or remove one tabulator key
            if (reverse) {
                whitespaces.chop(1);
            } else {
                whitespaces += indentCharacters;
            }

            cursor.insertText(whitespaces + listCharacter + listMarker +
                              whitespaceCharacter);
            return true;
        }
    }

    // check if we want to indent the whole text
    return increaseSelectedTextIndention(reverse, indentCharacters);
}

/**
 * Sets the auto text options
 */
void QMarkdownTextEdit::setAutoTextOptions(AutoTextOptions options) {
    _autoTextOptions = options;
}

void QMarkdownTextEdit::updateLineNumberArea(const QRect rect, int dy) {
    if (dy)
        _lineNumArea->scroll(0, dy);
    else
        _lineNumArea->update(0, rect.y(), _lineNumArea->sizeHint().width(),
                             rect.height());

    if (_hangingIndentEnabled && dy == 0 && hasFocus()) {
        const bool isCaretUpdate =
            rect.width() <= (cursorWidth() + 4) &&
            rect.height() <= (fontMetrics().height() + 4);
        if (isCaretUpdate) {
            updateHangingCursorRepaintState();
        }
    }

    updateLineNumAreaGeometry();

    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

QRect QMarkdownTextEdit::hangingCursorBlockRepaintRect() const {
    if (!_hangingIndentEnabled || !hasFocus()) {
        return {};
    }

    const QTextBlock cursorBlock = textCursor().block();
    if (!cursorBlock.isValid()) {
        return {};
    }

    if (listContentIndentLength(cursorBlock.text()) <= 0) {
        return {};
    }

    QTextLayout *layout = cursorBlock.layout();
    if (!layout || layout->lineCount() <= 1) {
        return {};
    }

    // Repaint the full viewport while the caret is in a wrapped list item with
    // hanging indent. The temporary hanging-indent re-layout can create
    // additional visual lines compared to the document layout used by Qt for
    // dirty region tracking, so block-local repaints may miss the caret,
    // especially on the last visual line.
    return viewport()->rect();
}

void QMarkdownTextEdit::updateHangingCursorRepaintState() {
    if (!_hangingCursorRepaintTimer) {
        return;
    }

    const QRect repaintRect = hangingCursorBlockRepaintRect();
    if (!repaintRect.isEmpty()) {
        viewport()->update(repaintRect);
        if (!_hangingCursorRepaintTimer->isActive()) {
            _hangingCursorRepaintTimer->start();
        }
    } else if (_hangingCursorRepaintTimer->isActive()) {
        _hangingCursorRepaintTimer->stop();
    }
}

void QMarkdownTextEdit::updateLineNumberAreaWidth(int) {
    QSignalBlocker blocker(this);
    const auto oldMargins = viewportMargins();
    const int sidebarWidth = _lineNumArea->sizeHint().width();
    _lineNumArea->setHidden(sidebarWidth <= 0);
    const int width = sidebarWidth > 0
                          ? sidebarWidth + _lineNumberLeftMarginOffset
                          : oldMargins.left();
    const auto newMargins = QMargins{width, oldMargins.top(),
                                     oldMargins.right(), oldMargins.bottom()};

    if (newMargins != oldMargins) {
        setViewportMargins(newMargins);
    }

    // Grow lineNumArea font-size with the font size of the editor
    const int pointSize = this->font().pointSize();
    if (pointSize > 0) {
        QFont font = _lineNumArea->font();
        font.setPointSize(pointSize);
        _lineNumArea->setFont(font);
    }
}

/**
 * Returns the number of characters that form the list prefix in a Markdown
 * list line, including leading whitespace, list marker, and trailing space.
 * Returns 0 if the line is not a list item.
 *
 * Examples:
 *   "- text"       -> 2  (marker "- ")
 *   "  - text"     -> 4  (2 spaces + "- ")
 *   "* text"       -> 2
 *   "+ [ ] text"   -> 6  (marker "+ [ ] ")
 *   "1. text"      -> 3  (marker "1. ")
 *   "  10. text"   -> 6  (2 spaces + "10. ")
 *   "  2) text"    -> 5  (2 spaces + "2) ")
 *   "not a list"   -> 0
 */
int QMarkdownTextEdit::listContentIndentLength(const QString &text) {
    // Match unordered list items: optional whitespace + [-*+] + optional
    // checkbox + space(s)
    static QRegularExpression unorderedRegex(
        QStringLiteral(R"(^(\s*)[+\-\*](?:\s\[[ x\-]\])?\s)"));
    QRegularExpressionMatch match = unorderedRegex.match(text);
    if (match.hasMatch()) {
        return match.capturedLength(0);
    }

    // Match ordered list items: optional whitespace + digits + [.)] + space(s)
    static QRegularExpression orderedRegex(
        QStringLiteral(R"(^(\s*)\d+[.\)]\s)"));
    match = orderedRegex.match(text);
    if (match.hasMatch()) {
        return match.capturedLength(0);
    }

    return 0;
}

/**
 * @param e
 * @details This does three things:
 * 1. Overrides QPlainTextEdit::paintEvent to fix the RTL bug of QPlainTextEdit
 * 2. Paints a rectangle around code block fences [Code taken from
 * ghostwriter(which in turn is based on QPlaintextEdit::paintEvent() with
 * modifications and minor improvements for our use
 * 3. Applies hanging indentation to wrapped Markdown list lines so that
 * continuation lines align with the list content rather than the list marker
 */
void QMarkdownTextEdit::paintEvent(QPaintEvent *e) {
    QVector<BlockLayoutBackup> hangingIndentBackups;
    if (_hangingIndentEnabled) {
        hangingIndentBackups = applyHangingIndentLayout();
    }

    QTextBlock block = firstVisibleBlock();

    QPainter painter(viewport());
    const QRect viewportRect = viewport()->rect();
    // painter.fillRect(viewportRect, Qt::transparent);
    bool firstVisible = true;
    QPointF offset(contentOffset());
    QRectF blockAreaRect;    // Code or block quote rect.
    bool inBlockArea = false;

    bool clipTop = false;
    bool drawBlock = false;
    qreal dy = 0.0;
    bool done = false;

    const QColor &color = MarkdownHighlighter::codeBlockBackgroundColor();
    const int cornerRadius = 5;

    while (block.isValid() && !done) {
        const QRectF r = blockBoundingRect(block).translated(offset);
        const int state = block.userState();

        if (!inBlockArea && MarkdownHighlighter::isCodeBlock(state)) {
            // skip the backticks
            if (!block.text().startsWith(QLatin1String("```")) &&
                !block.text().startsWith(QLatin1String("~~~"))) {
                blockAreaRect = r;
                dy = 0.0;
                inBlockArea = true;
            }

            // If this is the first visible block within the viewport
            // and if the previous block is part of the text block area,
            // then the rectangle to draw for the block area will have
            // its top clipped by the viewport and will need to be
            // drawn specially.
            const int prevBlockState = block.previous().userState();
            if (firstVisible &&
                MarkdownHighlighter::isCodeBlock(prevBlockState)) {
                clipTop = true;
            }
        }
        // Else if the block ends a text block area...
        else if (inBlockArea && MarkdownHighlighter::isCodeBlockEnd(state)) {
            drawBlock = true;
            inBlockArea = false;
            blockAreaRect.setHeight(dy);
        }
        // If the block is at the end of the document and ends a text
        // block area...
        //
        if (inBlockArea && block == this->document()->lastBlock()) {
            drawBlock = true;
            inBlockArea = false;
            dy += r.height();
            blockAreaRect.setHeight(dy);
        }
        offset.ry() += r.height();
        dy += r.height();

        // If this is the last text block visible within the viewport...
        if (offset.y() > viewportRect.height()) {
            if (inBlockArea) {
                blockAreaRect.setHeight(dy);
                drawBlock = true;
            }

            // Finished drawing.
            done = true;
        }
        // If this is the last text block visible within the viewport...
        if (offset.y() > viewportRect.height()) {
            if (inBlockArea) {
                blockAreaRect.setHeight(dy);
                drawBlock = true;
            }
            // Finished drawing.
            done = true;
        }

        if (drawBlock) {
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(color));

            // If the first visible block is "clipped" such that the previous
            // block is part of the text block area, then only draw a rectangle
            // with the bottom corners rounded, and with the top corners square
            // to reflect that the first visible block is part of a larger block
            // of text.
            //
            if (clipTop) {
                QPainterPath path;
                path.setFillRule(Qt::WindingFill);
                path.addRoundedRect(blockAreaRect, cornerRadius, cornerRadius);
                qreal adjustedHeight = blockAreaRect.height() / 2;
                path.addRect(blockAreaRect.adjusted(0, 0, 0, -adjustedHeight));
                painter.drawPath(path.simplified());
                clipTop = false;
            }
            // Else draw the entire rectangle with all corners rounded.
            else {
                painter.drawRoundedRect(blockAreaRect, cornerRadius,
                                        cornerRadius);
            }

            drawBlock = false;
        }

        // this fixes the RTL bug of QPlainTextEdit
        // https://bugreports.qt.io/browse/QTBUG-7516
        if (block.text().isRightToLeft()) {
            QTextLayout *layout = block.layout();
            // opt = document()->defaultTextOption();
            QTextOption opt = QTextOption(Qt::AlignRight);
            opt.setTextDirection(Qt::RightToLeft);
            layout->setTextOption(opt);
        }

        // Current line highlight
        QTextCursor cursor = textCursor();
        if (highlightCurrentLine() && cursor.block() == block) {
            QRect lineRect = cursorRect(cursor);
            lineRect.setLeft(0);
            lineRect.setWidth(viewportRect.width());
            painter.fillRect(lineRect, currentLineHighlightColor());
        }

        block = block.next();
        firstVisible = false;
    }

    painter.end();

    QPlainTextEdit::paintEvent(e);

    // Paint block (rectangular) selection overlay on top of rendered text
    if (_blockSelectionActive && _blockSelStartBlock >= 0) {
        QPainter overlayPainter(viewport());
        paintBlockSelection(overlayPainter);
        overlayPainter.end();
    }

    if (_hangingIndentEnabled) {
        restoreHangingIndentLayout(hangingIndentBackups);
    }
}

QVector<QMarkdownTextEdit::BlockLayoutBackup>
QMarkdownTextEdit::applyHangingIndentLayout() {
    QVector<BlockLayoutBackup> backups;
    if (!_hangingIndentEnabled) {
        return backups;
    }
    QTextBlock listBlock = firstVisibleBlock();
    QPointF listOffset(contentOffset());
    while (listBlock.isValid()) {
        const QRectF r = blockBoundingRect(listBlock).translated(listOffset);
        listOffset.ry() += r.height();

        // Stop once we're past the viewport
        if (r.top() > viewport()->rect().bottom()) {
            break;
        }

        const QString text = listBlock.text();
        const int indentChars = listContentIndentLength(text);

        // Only process list items that have a prefix and wrap to multiple lines
        if (indentChars > 0) {
            QTextLayout *layout = listBlock.layout();
            const int lineCount = layout->lineCount();

            if (lineCount > 1) {
                BlockLayoutBackup backup;
                backup.block = listBlock;
                for (int i = 0; i < lineCount; ++i) {
                    QTextLine line = layout->lineAt(i);
                    LineBackup lb;
                    lb.position = line.position();
                    lb.width = line.width();
                    backup.lines.append(lb);
                }
                backups.append(backup);

                const QTextLine firstLine = layout->lineAt(0);
                const qreal baseX = firstLine.position().x();
                const qreal indentPixels = firstLine.cursorToX(indentChars);
                const qreal cursorPadding =
                    qMax<qreal>(4.0, qreal(cursorWidth()) + 2.0);

                // Use the first line's width as the text area width since
                // all original lines share the same width
                const qreal textAreaWidth = backup.lines.first().width;
                const qreal indentOffset = indentPixels - baseX;
                const qreal continuationWidth = qMax<qreal>(
                    0.0, textAreaWidth - indentOffset + cursorPadding);

                // Re-layout the block with hanging indent for continuation
                // lines. Since narrower continuation lines may cause text
                // to reflow into more lines than the original layout, we
                // keep creating lines until all text is consumed.
                layout->clearLayout();
                layout->beginLayout();
                for (int i = 0;; ++i) {
                    QTextLine line = layout->createLine();
                    if (!line.isValid()) break;

                    if (i == 0) {
                        line.setLineWidth(qMax<qreal>(0.0, textAreaWidth));
                        line.setPosition(
                            QPointF(baseX, backup.lines[0].position.y()));
                    } else {
                        line.setLineWidth(continuationWidth);
                        // Compute y-position from the previous line
                        const QTextLine prev = layout->lineAt(i - 1);
                        const qreal y = prev.position().y() + prev.height();
                        line.setPosition(QPointF(indentPixels, y));
                    }
                }
                layout->endLayout();
            }
        }

        listBlock = listBlock.next();
    }

    return backups;
}

void QMarkdownTextEdit::restoreHangingIndentLayout(
    const QVector<BlockLayoutBackup> &backups) {
    // Restore original line positions and widths so the document layout
    // engine is not confused.
    for (const auto &backup : backups) {
        QTextLayout *layout = backup.block.layout();
        layout->clearLayout();
        layout->beginLayout();
        for (int i = 0; i < backup.lines.size(); ++i) {
            QTextLine line = layout->createLine();
            if (!line.isValid()) break;

            line.setLineWidth(backup.lines[i].width);
            line.setPosition(backup.lines[i].position);
        }
        layout->endLayout();
    }
}

void QMarkdownTextEdit::mousePressEvent(QMouseEvent *event) {
    // Alt + Left click starts block (rectangular) selection
    if (event->button() == Qt::LeftButton &&
        event->modifiers().testFlag(Qt::AltModifier)) {
        _blockSelectionActive = true;
        _blockSelectionDragging = true;
        _blockSelectionAnchor = event->pos();
        _blockSelectionEnd = event->pos();
        _blockSelStartBlock = -1;
        _blockSelEndBlock = -1;
        _blockSelLeftCol = 0;
        _blockSelRightCol = 0;

        // Place cursor at click position without starting normal selection
        QTextCursor cursor = cursorForPosition(event->pos());
        cursor.clearSelection();
        setTextCursor(cursor);
        viewport()->update();
        return;
    }

    // Any non-Alt click clears block selection
    if (_blockSelectionActive) {
        clearBlockSelection();
    }

    if (_hangingIndentEnabled) {
        const QVector<BlockLayoutBackup> backups = applyHangingIndentLayout();
        QPlainTextEdit::mousePressEvent(event);
        restoreHangingIndentLayout(backups);
        updateHangingCursorRepaintState();
    } else {
        QPlainTextEdit::mousePressEvent(event);
    }
}

void QMarkdownTextEdit::keyPressEvent(QKeyEvent *event) {
    // Clear block selection when a non-modifier key is pressed
    // (copy/cut are handled separately in eventFilter)
    if (_blockSelectionActive && event->key() != Qt::Key_Alt &&
        event->key() != Qt::Key_Shift && event->key() != Qt::Key_Control &&
        event->key() != Qt::Key_Meta && event->key() != Qt::Key_Escape) {
        clearBlockSelection();
    }

    if (_hangingIndentEnabled) {
        const QVector<BlockLayoutBackup> backups = applyHangingIndentLayout();
        QPlainTextEdit::keyPressEvent(event);
        restoreHangingIndentLayout(backups);
    } else {
        QPlainTextEdit::keyPressEvent(event);
    }
}

void QMarkdownTextEdit::mouseMoveEvent(QMouseEvent *event) {
    // Update block selection rectangle during Alt+drag
    if (_blockSelectionDragging && _blockSelectionActive) {
        _blockSelectionEnd = event->pos();
        updateBlockSelection();
        viewport()->update();
        return;
    }

    if (_hangingIndentEnabled) {
        const QVector<BlockLayoutBackup> backups = applyHangingIndentLayout();
        QPlainTextEdit::mouseMoveEvent(event);
        restoreHangingIndentLayout(backups);
    } else {
        QPlainTextEdit::mouseMoveEvent(event);
    }

    updateLinkCursor(event->pos(), QGuiApplication::keyboardModifiers());
}

void QMarkdownTextEdit::mouseReleaseEvent(QMouseEvent *event) {
    // Finish block selection drag
    if (_blockSelectionDragging) {
        _blockSelectionDragging = false;
        if (_blockSelectionActive) {
            _blockSelectionEnd = event->pos();
            updateBlockSelection();
            viewport()->update();
        }
        return;
    }

    if (_hangingIndentEnabled) {
        const QVector<BlockLayoutBackup> backups = applyHangingIndentLayout();
        QPlainTextEdit::mouseReleaseEvent(event);
        restoreHangingIndentLayout(backups);
        updateHangingCursorRepaintState();
    } else {
        QPlainTextEdit::mouseReleaseEvent(event);
    }
}

void QMarkdownTextEdit::mouseDoubleClickEvent(QMouseEvent *event) {
    if (_hangingIndentEnabled) {
        const QVector<BlockLayoutBackup> backups = applyHangingIndentLayout();
        QPlainTextEdit::mouseDoubleClickEvent(event);
        restoreHangingIndentLayout(backups);
        updateHangingCursorRepaintState();
    } else {
        QPlainTextEdit::mouseDoubleClickEvent(event);
    }
}

/**
 * Overrides QPlainTextEdit::setReadOnly to fix a problem with Chinese and
 * Japanese input methods
 *
 * @param ro
 */
void QMarkdownTextEdit::setReadOnly(bool ro) {
    QPlainTextEdit::setReadOnly(ro);

    // attempted to fix a problem with Chinese and Japanese input methods
    // @see https://github.com/pbek/QOwnNotes/issues/976
    setAttribute(Qt::WA_InputMethodEnabled, !isReadOnly());
}

void QMarkdownTextEdit::doSearch(
    QString &searchText, QPlainTextEditSearchWidget::SearchMode searchMode) {
    _searchWidget->setSearchText(searchText);
    _searchWidget->setSearchMode(searchMode);
    _searchWidget->doSearchCount();
    _searchWidget->activate(false);
}

void QMarkdownTextEdit::hideSearchWidget(bool reset) {
    _searchWidget->deactivate();

    if (reset) {
        _searchWidget->reset();
    }
}

void QMarkdownTextEdit::updateSettings() {
    // if true: centers the screen if cursor reaches bottom (but not top)
    searchWidget()->setDebounceDelay(_debounceDelay);
    setCenterOnScroll(_centerCursor);
}

void QMarkdownTextEdit::setLineNumberLeftMarginOffset(int offset) {
    _lineNumberLeftMarginOffset = offset;
}

int QMarkdownTextEdit::sidebarAdditionalWidth() const { return 0; }

void QMarkdownTextEdit::paintSidebar(QPainter *painter,
                                     const QRect &eventRect) {
    Q_UNUSED(painter)
    Q_UNUSED(eventRect)
}

bool QMarkdownTextEdit::sidebarMousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    return false;
}

/**
 * Updates the block (rectangular) selection from the stored anchor and end
 * viewport positions.  Computes the block range and pixel x-coordinates
 * that define the selection rectangle.
 */
void QMarkdownTextEdit::updateBlockSelection() {
    const QTextCursor anchorCursor = cursorForPosition(_blockSelectionAnchor);
    const QTextCursor endCursor = cursorForPosition(_blockSelectionEnd);

    const int anchorBlock = anchorCursor.blockNumber();
    const int endBlock = endCursor.blockNumber();

    _blockSelStartBlock = qMin(anchorBlock, endBlock);
    _blockSelEndBlock = qMax(anchorBlock, endBlock);

    // Use the anchor block as a reference to convert raw pixel x-coordinates
    // to character column indices.  columnForBlockAtX() extrapolates past the
    // end of short lines using font metrics, so the selection can be wider than
    // the anchor line's text content.
    QTextDocument *doc = document();
    QTextBlock refBlock = doc->findBlockByNumber(anchorBlock);
    if (!refBlock.isValid()) {
        refBlock = doc->findBlockByNumber(_blockSelStartBlock);
    }

    const int rawLeftX =
        qMin(_blockSelectionAnchor.x(), _blockSelectionEnd.x());
    const int rawRightX =
        qMax(_blockSelectionAnchor.x(), _blockSelectionEnd.x());

    _blockSelLeftCol = columnForBlockAtX(refBlock, rawLeftX);
    _blockSelRightCol = columnForBlockAtX(refBlock, rawRightX);
}

/**
 * Paints the block (rectangular) selection overlay.
 * Computes per-line x-coordinates from the stored column indices using
 * xForColumnInBlock() (which uses QTextLayout::cursorToX and extrapolates
 * past end-of-line with spaceWidth, like Kate does).  With a monospace font
 * this produces identical x-coordinates on every line, yielding a true
 * rectangle.
 */
void QMarkdownTextEdit::paintBlockSelection(QPainter &painter) {
    if (_blockSelStartBlock < 0 || _blockSelLeftCol == _blockSelRightCol) {
        return;
    }

    QTextDocument *doc = document();
    const QPointF offset = contentOffset();
    const QRect vp = viewport()->rect();
    const QPalette pal = palette();
    QColor selColor = pal.color(QPalette::Highlight);
    selColor.setAlpha(128);

    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (int i = _blockSelStartBlock; i <= _blockSelEndBlock; ++i) {
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }

        const QRectF geom = blockBoundingGeometry(block).translated(offset);

        // Skip blocks outside the viewport
        if (geom.bottom() < 0 || geom.top() > vp.bottom()) {
            continue;
        }

        const qreal leftX = xForColumnInBlock(block, _blockSelLeftCol);
        const qreal rightX = xForColumnInBlock(block, _blockSelRightCol);

        const int topY = qMax(static_cast<int>(geom.top()), 0);
        const int bottomY = qMin(static_cast<int>(geom.bottom()), vp.bottom());

        const QRect lineRect(static_cast<int>(leftX), topY,
                             static_cast<int>(rightX - leftX), bottomY - topY);
        painter.fillRect(lineRect, selColor);
    }
}

/**
 * Returns the column index in the given block that corresponds to the
 * given viewport x-coordinate.  Uses the block's QTextLayout for accurate
 * character-boundary mapping within the line, and extrapolates past the end
 * of the line using space width so that a rectangular selection can extend
 * beyond the width of a short line.
 */
int QMarkdownTextEdit::columnForBlockAtX(const QTextBlock &block, int x) const {
    if (!block.isValid()) {
        return 0;
    }

    QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0) {
        return 0;
    }

    const QPointF offset = contentOffset();
    const QRectF geom = blockBoundingGeometry(block).translated(offset);
    const int textLen = block.length() - 1;    // Exclude trailing \n

    // Convert viewport x to a local x relative to the block's left edge
    const qreal localX = x - geom.left();

    // Find the x-coordinate at the end of the line text
    const qreal endX = layout->lineAt(0).cursorToX(textLen);

    if (localX <= endX) {
        // Position is within the line text — use precise layout mapping
        return layout->lineAt(0).xToCursor(localX);
    }

    // Position is past the end of the line — extrapolate using space width
    const qreal overX = localX - endX;
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    const qreal spaceWidth =
        QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
#else
    const qreal spaceWidth = QFontMetricsF(font()).width(QLatin1Char(' '));
#endif
    if (spaceWidth <= 0) {
        return textLen;
    }
    const int extra = static_cast<int>(overX / spaceWidth + 0.5);
    return textLen + extra;
}

/**
 * Returns the viewport x-coordinate for the given column in the given block.
 * Uses QTextLayout::lineAt(0).cursorToX() for pixel-precise mapping.
 * If the column is past the end of the line's text, the position is
 * extrapolated using the average character width (like Kate does with
 * spaceWidth()), so the rectangle extends uniformly past short lines.
 */
qreal QMarkdownTextEdit::xForColumnInBlock(const QTextBlock &block,
                                           int column) const {
    if (!block.isValid()) {
        return 0;
    }

    QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0) {
        return 0;
    }

    const int textLen = block.length() - 1;    // Exclude trailing \n
    const QPointF offset = contentOffset();
    const QRectF geom = blockBoundingGeometry(block).translated(offset);

    if (column <= textLen) {
        // Column is within the line's text — use precise layout mapping
        const qreal localX = layout->lineAt(0).cursorToX(column);
        return geom.left() + localX;
    }

    // Column is past end of line — extrapolate using space width
    const qreal endX = layout->lineAt(0).cursorToX(textLen);
    const int over = column - textLen;
    // horizontalAdvance() was added in Qt 5.11; use width() on older versions
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    const qreal spaceWidth =
        QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
#else
    const qreal spaceWidth = QFontMetricsF(font()).width(QLatin1Char(' '));
#endif
    return geom.left() + endX + over * spaceWidth;
}

/**
 * Clears the block (rectangular) selection state and triggers a repaint
 */
void QMarkdownTextEdit::clearBlockSelection() {
    if (!_blockSelectionActive) {
        return;
    }

    _blockSelectionActive = false;
    _blockSelectionDragging = false;
    _blockSelStartBlock = -1;
    _blockSelEndBlock = -1;
    _blockSelLeftCol = 0;
    _blockSelRightCol = 0;

    viewport()->update();
}

/**
 * Returns the text within the block (rectangular) selection, with each
 * row on a separate line.  Uses the stored column indices directly so
 * the extracted text always matches the visual rectangle.
 */
QString QMarkdownTextEdit::blockSelectionText() const {
    if (_blockSelStartBlock < 0) {
        return {};
    }

    QTextDocument *doc = document();
    QStringList lines;
    for (int i = _blockSelStartBlock; i <= _blockSelEndBlock; ++i) {
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }
        const QString text = block.text();
        const int s = qMin(_blockSelLeftCol, text.length());
        const int e = qMin(_blockSelRightCol, text.length());
        lines.append(text.mid(s, e - s));
    }
    return lines.join(QLatin1Char('\n'));
}

/**
 * Removes the text within the block (rectangular) selection.
 * Uses the stored column indices directly.
 */
void QMarkdownTextEdit::removeBlockSelectionText() {
    if (_blockSelStartBlock < 0) {
        return;
    }

    QTextDocument *doc = document();
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();

    // Remove in reverse block order to keep earlier positions valid
    for (int i = _blockSelEndBlock; i >= _blockSelStartBlock; --i) {
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }
        const int blockLen = block.length() - 1;
        const int s = qMin(_blockSelLeftCol, blockLen);
        const int e = qMin(_blockSelRightCol, blockLen);
        if (s >= e) {
            continue;
        }
        editCursor.setPosition(block.position() + s);
        editCursor.setPosition(block.position() + e, QTextCursor::KeepAnchor);
        editCursor.removeSelectedText();
    }

    editCursor.endEditBlock();
}

/**
 * Replaces the text within the block (rectangular) selection with the
 * given text. Each line of the input replaces the corresponding row.
 * Uses the stored column indices directly.
 */
void QMarkdownTextEdit::replaceBlockSelectionText(const QString &text) {
    if (_blockSelStartBlock < 0) {
        return;
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    QTextDocument *doc = document();
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();

    // Replace in reverse block order to keep earlier positions valid
    for (int i = _blockSelEndBlock; i >= _blockSelStartBlock; --i) {
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) {
            continue;
        }
        const int blockLen = block.length() - 1;
        const int s = qMin(_blockSelLeftCol, blockLen);
        const int e = qMin(_blockSelRightCol, blockLen);
        editCursor.setPosition(block.position() + s);
        editCursor.setPosition(block.position() + e, QTextCursor::KeepAnchor);
        const int lineIdx = i - _blockSelStartBlock;
        const QString &line =
            lineIdx < lines.size() ? lines.at(lineIdx) : QString();
        editCursor.insertText(line);
    }

    editCursor.endEditBlock();
    clearBlockSelection();
}

QMargins QMarkdownTextEdit::viewportMargins() {
    return QPlainTextEdit::viewportMargins();
}
