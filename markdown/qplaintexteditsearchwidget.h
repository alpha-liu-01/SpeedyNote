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

#pragma once

#include <QPlainTextEdit>
#include <QTimer>
#include <QWidget>

namespace Ui {
class QPlainTextEditSearchWidget;
}

class QPlainTextEditSearchWidget : public QWidget {
    Q_OBJECT

   public:
    enum SearchMode { PlainTextMode, WholeWordsMode, RegularExpressionMode };

    explicit QPlainTextEditSearchWidget(QPlainTextEdit *parent = nullptr);
    bool doSearch(bool searchDown = true, bool allowRestartAtTop = true,
                  bool updateUI = true);
    void setDarkMode(bool enabled);
    ~QPlainTextEditSearchWidget();

    void setSearchText(const QString &searchText);
    void setSearchMode(SearchMode searchMode);
    void setDebounceDelay(uint debounceDelay);
    void activate(bool focus);
    void clearSearchExtraSelections();
    void updateSearchExtraSelections();

   private:
    Ui::QPlainTextEditSearchWidget *ui;
    int _searchResultCount;
    int _currentSearchResult;
    QList<QTextEdit::ExtraSelection> _searchExtraSelections;
    QColor selectionColor;
    QTimer _debounceTimer;
    QString _searchTerm;
    void setSearchExtraSelections() const;
    void stopDebounce();

   protected:
    QPlainTextEdit *_textEdit;
    bool _darkMode;
    bool eventFilter(QObject *obj, QEvent *event) override;

   public Q_SLOTS:
    void activate();
    void deactivate();
    void doSearchDown();
    void doSearchUp();
    void setReplaceMode(bool enabled);
    void activateReplace();
    bool doReplace(bool forAll = false);
    void doReplaceAll();
    void reset();
    void doSearchCount();

   protected Q_SLOTS:
    void searchLineEditTextChanged(const QString &arg1);
    void performSearch();
    void updateSearchCountLabelText();
    void setSearchSelectionColor(const QColor &color);
   private Q_SLOTS:
    void on_modeComboBox_currentIndexChanged(int index);
    void on_matchCaseSensitiveButton_toggled(bool checked);
};
