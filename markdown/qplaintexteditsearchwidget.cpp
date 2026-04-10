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

#include "qplaintexteditsearchwidget.h"

#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QScrollBar>

#include "ui_qplaintexteditsearchwidget.h"

QPlainTextEditSearchWidget::QPlainTextEditSearchWidget(QPlainTextEdit *parent)
    : QWidget(parent),
      ui(new Ui::QPlainTextEditSearchWidget),
      selectionColor(0, 180, 0, 100) {
    ui->setupUi(this);
    _textEdit = parent;
    _darkMode = false;
    hide();
    ui->searchCountLabel->setStyleSheet(QStringLiteral("* {color: grey}"));
    // hiding will leave a open space in the horizontal layout
    ui->searchCountLabel->setEnabled(false);
    _currentSearchResult = 0;
    _searchResultCount = 0;

    connect(ui->closeButton, &QPushButton::clicked, this,
            &QPlainTextEditSearchWidget::deactivate);
    connect(ui->searchLineEdit, &QLineEdit::textChanged, this,
            &QPlainTextEditSearchWidget::searchLineEditTextChanged);
    connect(ui->searchDownButton, &QPushButton::clicked, this,
            &QPlainTextEditSearchWidget::doSearchDown);
    connect(ui->searchUpButton, &QPushButton::clicked, this,
            &QPlainTextEditSearchWidget::doSearchUp);
    connect(ui->replaceToggleButton, &QPushButton::toggled, this,
            &QPlainTextEditSearchWidget::setReplaceMode);
    connect(ui->replaceButton, &QPushButton::clicked, this,
            &QPlainTextEditSearchWidget::doReplace);
    connect(ui->replaceAllButton, &QPushButton::clicked, this,
            &QPlainTextEditSearchWidget::doReplaceAll);

    connect(&_debounceTimer, &QTimer::timeout, this,
            &QPlainTextEditSearchWidget::performSearch);

    installEventFilter(this);
    ui->searchLineEdit->installEventFilter(this);
    ui->replaceLineEdit->installEventFilter(this);

#ifdef Q_OS_MAC
    // set the spacing to 8 for OS X
    layout()->setSpacing(8);
    ui->buttonFrame->layout()->setSpacing(9);

    // set the margin to 0 for the top buttons for OS X
    QString buttonStyle = QStringLiteral("QPushButton {margin: 0}");
    ui->closeButton->setStyleSheet(buttonStyle);
    ui->searchDownButton->setStyleSheet(buttonStyle);
    ui->searchUpButton->setStyleSheet(buttonStyle);
    ui->replaceToggleButton->setStyleSheet(buttonStyle);
    ui->matchCaseSensitiveButton->setStyleSheet(buttonStyle);
#endif
}

QPlainTextEditSearchWidget::~QPlainTextEditSearchWidget() { delete ui; }

void QPlainTextEditSearchWidget::activate() { activate(true); }

void QPlainTextEditSearchWidget::activateReplace() {
    // replacing is prohibited if the text edit is readonly
    if (_textEdit->isReadOnly()) {
        return;
    }

    ui->searchLineEdit->setText(_textEdit->textCursor().selectedText());
    ui->searchLineEdit->selectAll();
    activate();
    setReplaceMode(true);
}

void QPlainTextEditSearchWidget::deactivate() {
    stopDebounce();

    hide();

    // Clear the search extra selections when closing the search bar
    clearSearchExtraSelections();

    _textEdit->setFocus();
}

void QPlainTextEditSearchWidget::setReplaceMode(bool enabled) {
    ui->replaceToggleButton->setChecked(enabled);
    ui->replaceLabel->setVisible(enabled);
    ui->replaceLineEdit->setVisible(enabled);
    ui->modeLabel->setVisible(enabled);
    ui->buttonFrame->setVisible(enabled);
    ui->matchCaseSensitiveButton->setVisible(enabled);
}

bool QPlainTextEditSearchWidget::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Escape) {
            deactivate();
            return true;
        } else if ((!_debounceTimer.isActive() &&
                    keyEvent->modifiers().testFlag(Qt::ShiftModifier) &&
                    (keyEvent->key() == Qt::Key_Return)) ||
                   (keyEvent->key() == Qt::Key_Up)) {
            doSearchUp();
            return true;
        } else if (!_debounceTimer.isActive() &&
                   ((keyEvent->key() == Qt::Key_Return) ||
                    (keyEvent->key() == Qt::Key_Down))) {
            doSearchDown();
            return true;
        } else if (!_debounceTimer.isActive() &&
                   keyEvent->key() == Qt::Key_F3) {
            doSearch(!keyEvent->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }

        //        if ((obj == ui->replaceLineEdit) && (keyEvent->key() ==
        //        Qt::Key_Tab)
        //                && ui->replaceToggleButton->isChecked()) {
        //            ui->replaceLineEdit->setFocus();
        //        }

        return false;
    }

    return QWidget::eventFilter(obj, event);
}

void QPlainTextEditSearchWidget::searchLineEditTextChanged(
    const QString &arg1) {
    _searchTerm = arg1;

    if (_debounceTimer.interval() != 0 && !_searchTerm.isEmpty()) {
        _debounceTimer.start();
        ui->searchDownButton->setEnabled(false);
        ui->searchUpButton->setEnabled(false);
    } else {
        performSearch();
    }
}

void QPlainTextEditSearchWidget::performSearch() {
    doSearchCount();
    updateSearchExtraSelections();
    doSearchDown();
}

void QPlainTextEditSearchWidget::clearSearchExtraSelections() {
    _searchExtraSelections.clear();
    setSearchExtraSelections();
}

void QPlainTextEditSearchWidget::updateSearchExtraSelections() {
    _searchExtraSelections.clear();
    const auto textCursor = _textEdit->textCursor();
    _textEdit->moveCursor(QTextCursor::Start);
    const QColor color = selectionColor;
    QTextCharFormat extraFmt;
    extraFmt.setBackground(color);
    int findCounter = 0;
    const int searchMode = ui->modeComboBox->currentIndex();

    while (doSearch(true, false, false)) {
        findCounter++;

        // prevent infinite loops from regular expression searches like "$", "^"
        // or "\b"
        if (searchMode == RegularExpressionMode && findCounter >= 10000) {
            break;
        }

        QTextEdit::ExtraSelection extra = QTextEdit::ExtraSelection();
        extra.format = extraFmt;

        extra.cursor = _textEdit->textCursor();
        _searchExtraSelections.append(extra);
    }

    _textEdit->setTextCursor(textCursor);
    this->setSearchExtraSelections();
}

void QPlainTextEditSearchWidget::setSearchExtraSelections() const {
    this->_textEdit->setExtraSelections(this->_searchExtraSelections);
}

void QPlainTextEditSearchWidget::stopDebounce() {
    _debounceTimer.stop();
    ui->searchDownButton->setEnabled(true);
    ui->searchUpButton->setEnabled(true);
}

void QPlainTextEditSearchWidget::doSearchUp() { doSearch(false); }

void QPlainTextEditSearchWidget::doSearchDown() { doSearch(true); }

bool QPlainTextEditSearchWidget::doReplace(bool forAll) {
    if (_textEdit->isReadOnly()) {
        return false;
    }

    QTextCursor cursor = _textEdit->textCursor();

    if (!forAll && cursor.selectedText().isEmpty()) {
        return false;
    }

    const int searchMode = ui->modeComboBox->currentIndex();
    if (searchMode == RegularExpressionMode) {
        QString text = cursor.selectedText();
        text.replace(QRegularExpression(ui->searchLineEdit->text()),
                     ui->replaceLineEdit->text());
        cursor.insertText(text);
    } else {
        cursor.insertText(ui->replaceLineEdit->text());
    }

    if (!forAll) {
        const int position = cursor.position();

        if (!doSearch(true)) {
            // restore the last cursor position if text wasn't found any more
            cursor.setPosition(position);
            _textEdit->setTextCursor(cursor);
        }
    }

    return true;
}

void QPlainTextEditSearchWidget::doReplaceAll() {
    if (_textEdit->isReadOnly()) {
        return;
    }

    // start at the top
    _textEdit->moveCursor(QTextCursor::Start);

    // replace until everything to the bottom is replaced
    while (doSearch(true, false) && doReplace(true)) {
    }
}

/**
 * @brief Searches for text in the text edit
 * @returns true if found
 */
bool QPlainTextEditSearchWidget::doSearch(bool searchDown,
                                          bool allowRestartAtTop,
                                          bool updateUI) {
    if (_debounceTimer.isActive()) {
        stopDebounce();
    }

    const QString text = ui->searchLineEdit->text();

    if (text.isEmpty()) {
        if (updateUI) {
            ui->searchLineEdit->setStyleSheet(QLatin1String(""));
        }

        return false;
    }

    const int searchMode = ui->modeComboBox->currentIndex();
    const bool caseSensitive = ui->matchCaseSensitiveButton->isChecked();

    QFlags<QTextDocument::FindFlag> options =
        searchDown ? QTextDocument::FindFlag(0) : QTextDocument::FindBackward;
    if (searchMode == WholeWordsMode) {
        options |= QTextDocument::FindWholeWords;
    }

    if (caseSensitive) {
        options |= QTextDocument::FindCaseSensitively;
    }

    // block signal to reduce too many signals being fired and too many updates
    _textEdit->blockSignals(true);

    bool found =
        searchMode == RegularExpressionMode
            ?
#if (QT_VERSION >= QT_VERSION_CHECK(5, 13, 0))
            _textEdit->find(
                QRegularExpression(
                    text, caseSensitive
                              ? QRegularExpression::NoPatternOption
                              : QRegularExpression::CaseInsensitiveOption),
                options)
            :
#else
            _textEdit->find(QRegExp(text, caseSensitive ? Qt::CaseSensitive
                                                        : Qt::CaseInsensitive),
                            options)
            :
#endif
            _textEdit->find(text, options);

    _textEdit->blockSignals(false);

    if (found) {
        const int result =
            searchDown ? ++_currentSearchResult : --_currentSearchResult;
        _currentSearchResult = std::min(result, _searchResultCount);

        updateSearchCountLabelText();
    }

    // start at the top (or bottom) if not found
    if (!found && allowRestartAtTop) {
        _textEdit->moveCursor(searchDown ? QTextCursor::Start
                                         : QTextCursor::End);
        found =
            searchMode == RegularExpressionMode
                ?
#if (QT_VERSION >= QT_VERSION_CHECK(5, 13, 0))
                _textEdit->find(
                    QRegularExpression(
                        text, caseSensitive
                                  ? QRegularExpression::NoPatternOption
                                  : QRegularExpression::CaseInsensitiveOption),
                    options)
                :
#else
                _textEdit->find(
                    QRegExp(text, caseSensitive ? Qt::CaseSensitive
                                                : Qt::CaseInsensitive),
                    options)
                :
#endif
                _textEdit->find(text, options);

        if (found && updateUI) {
            _currentSearchResult = searchDown ? 1 : _searchResultCount;
            updateSearchCountLabelText();
        }
    }

    if (updateUI) {
        const QRect rect = _textEdit->cursorRect();
        QMargins margins = _textEdit->layout()->contentsMargins();
        const int searchWidgetHotArea = _textEdit->height() - this->height();
        const int marginBottom =
            (rect.y() > searchWidgetHotArea) ? (this->height() + 10) : 0;

        // move the search box a bit up if we would block the search result
        if (margins.bottom() != marginBottom) {
            margins.setBottom(marginBottom);
            _textEdit->layout()->setContentsMargins(margins);
        }

        // add a background color according if we found the text or not
        const QString bgColorCode = _darkMode
                                        ? (found ? QStringLiteral("#135a13")
                                                 : QStringLiteral("#8d2b36"))
                                    : found ? QStringLiteral("#D5FAE2")
                                            : QStringLiteral("#FAE9EB");
        const QString fgColorCode =
            _darkMode ? QStringLiteral("#cccccc") : QStringLiteral("#404040");

        ui->searchLineEdit->setStyleSheet(
            QStringLiteral("* { background: ") + bgColorCode +
            QStringLiteral("; color: ") + fgColorCode + QStringLiteral("; }"));

        // restore the search extra selections after the find command
        this->setSearchExtraSelections();
    }

    return found;
}

/**
 * @brief Counts the search results
 */
void QPlainTextEditSearchWidget::doSearchCount() {
    // Note that we are moving the anchor, so the search will start from the top
    // again! Alternative: Restore cursor position afterward, but then we will
    // not know
    //              at what _currentSearchResult we currently are
    _textEdit->moveCursor(QTextCursor::Start, QTextCursor::MoveAnchor);

    bool found;
    _searchResultCount = 0;
    _currentSearchResult = 0;
    const int searchMode = ui->modeComboBox->currentIndex();

    do {
        found = doSearch(true, false, false);
        if (found) {
            _searchResultCount++;
        }

        // prevent infinite loops from regular expression searches like "$", "^"
        // or "\b"
        if (searchMode == RegularExpressionMode &&
            _searchResultCount >= 10000) {
            break;
        }
    } while (found);

    updateSearchCountLabelText();
}

void QPlainTextEditSearchWidget::setDarkMode(bool enabled) {
    _darkMode = enabled;
}

void QPlainTextEditSearchWidget::setSearchText(const QString &searchText) {
    ui->searchLineEdit->setText(searchText);
}

void QPlainTextEditSearchWidget::setSearchMode(SearchMode searchMode) {
    ui->modeComboBox->setCurrentIndex(searchMode);
}

void QPlainTextEditSearchWidget::setDebounceDelay(uint debounceDelay) {
    _debounceTimer.setInterval(static_cast<int>(debounceDelay));
}

void QPlainTextEditSearchWidget::activate(bool focus) {
    setReplaceMode(ui->modeComboBox->currentIndex() !=
                   SearchMode::PlainTextMode);
    show();

    const int verticalScrollBarValue = _textEdit->verticalScrollBar()->value();
    const int horizontalScrollBarValue =
        _textEdit->horizontalScrollBar()->value();

    // Save the original cursor so we can restore the position after the search
    // widget initialization (for #3541)
    const QTextCursor originalCursor = _textEdit->textCursor();

    // Preset the selected text as search text if there is any, replacing any
    // existing search text. Move the cursor to the start of the selection so
    // that the first search result is the originally selected text and the view
    // does not scroll away from the user's current position (for #3541).
    const QString selectedText = originalCursor.selectedText();
    bool hasPresetSelection = false;
    if (!selectedText.isEmpty()) {
        ui->searchLineEdit->setText(selectedText);
        hasPresetSelection = true;
    }

    if (focus) {
        ui->searchLineEdit->setFocus();
    }

    ui->searchLineEdit->selectAll();
    updateSearchExtraSelections();

    // If text was preset from a selection, move the cursor one position before
    // the original selection start. QPlainTextEdit::find() searches strictly
    // after the cursor position, so placing it one character before the
    // selection ensures doSearchDown() finds the originally selected word
    // itself rather than the next occurrence in the document (for #3541).
    if (hasPresetSelection) {
        QTextCursor cursor = _textEdit->textCursor();
        const int pos = originalCursor.selectionStart();
        cursor.setPosition(pos > 0 ? pos - 1 : 0);
        _textEdit->setTextCursor(cursor);
    }

    doSearchDown();

    _textEdit->verticalScrollBar()->setValue(verticalScrollBarValue);
    _textEdit->horizontalScrollBar()->setValue(horizontalScrollBarValue);
}

void QPlainTextEditSearchWidget::reset() {
    ui->searchLineEdit->clear();
    setSearchMode(SearchMode::PlainTextMode);
    setReplaceMode(false);
    ui->searchCountLabel->setEnabled(false);
}

void QPlainTextEditSearchWidget::updateSearchCountLabelText() {
    ui->searchCountLabel->setEnabled(true);
    ui->searchCountLabel->setText(QStringLiteral("%1/%2").arg(
        _currentSearchResult == 0 ? QChar('-')
                                  : QString::number(_currentSearchResult),
        _searchResultCount == 0 ? QChar('-')
                                : QString::number(_searchResultCount)));
}

void QPlainTextEditSearchWidget::setSearchSelectionColor(const QColor &color) {
    selectionColor = color;
}

void QPlainTextEditSearchWidget::on_modeComboBox_currentIndexChanged(
    int index) {
    Q_UNUSED(index)
    doSearchCount();
    doSearchDown();
}

void QPlainTextEditSearchWidget::on_matchCaseSensitiveButton_toggled(
    bool checked) {
    Q_UNUSED(checked)
    doSearchCount();
    doSearchDown();
}
