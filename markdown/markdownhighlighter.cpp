
/*
 * MIT License
 *
 * Copyright (c) 2014-2025 Patrizio Bekerle -- <patrizio@bekerle.com>
 * Copyright (c) 2019-2021 Waqar Ahmed      -- <waqar.17a@gmail.com>
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
 *
 * QPlainTextEdit Markdown highlighter
 */

#include "markdownhighlighter.h"

#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QTextDocument>
#include <QTimer>
#include <utility>

#include "qownlanguagedata.h"

// We enable QStringView with Qt 5.15.14
// Note: QStringView::mid wasn't working correctly at least with 5.15.2
// and 5.15.3, but 5.15.14 was fine
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 14)
#define MH_SUBSTR(pos, len) text.midRef(pos, len)
#else
#define MH_SUBSTR(pos, len) QStringView(text).mid(pos, len)
#endif

QHash<QString, MarkdownHighlighter::HighlighterState>
    MarkdownHighlighter::_langStringToEnum;
QHash<MarkdownHighlighter::HighlighterState, QTextCharFormat>
    MarkdownHighlighter::_formats;
QVector<MarkdownHighlighter::HighlightingRule>
    MarkdownHighlighter::_highlightingRules;

/**
 * Markdown syntax highlighting
 * @param parent
 * @return
 */
MarkdownHighlighter::MarkdownHighlighter(
    QTextDocument *parent, HighlightingOptions highlightingOptions)
    : QSyntaxHighlighter(parent), _highlightingOptions(highlightingOptions) {
    // _highlightingOptions = highlightingOptions;
    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, &MarkdownHighlighter::timerTick);

    _timer->start(1000);

    // initialize the highlighting rules
    initHighlightingRules();

    // initialize the text formats
    initTextFormats();

    // initialize code languages
    initCodeLangs();
}

/**
 * Does jobs every second
 */
void MarkdownHighlighter::timerTick() {
    // re-highlight all dirty blocks
    reHighlightDirtyBlocks();

    // emit a signal every second if there was some highlighting done
    if (_highlightingFinished) {
        _highlightingFinished = false;
        Q_EMIT highlightingFinished();
    }
}

/**
 * Re-highlights all dirty blocks
 */
void MarkdownHighlighter::reHighlightDirtyBlocks() {
    while (_dirtyTextBlocks.count() > 0) {
        QTextBlock block = _dirtyTextBlocks.at(0);
        rehighlightBlock(block);
        _dirtyTextBlocks.removeFirst();
    }
}

/**
 * Clears the dirty blocks vector
 */
void MarkdownHighlighter::clearDirtyBlocks() {
    _ranges.clear();
    _dirtyTextBlocks.clear();
}

/**
 * Adds a dirty block to the list if it doesn't already exist
 *
 * @param block
 */
void MarkdownHighlighter::addDirtyBlock(const QTextBlock &block) {
    if (!_dirtyTextBlocks.contains(block)) {
        _dirtyTextBlocks.append(block);
    }
}

/**
 * Initializes the highlighting rules
 *
 * regexp tester:
 * https://regex101.com
 *
 * other examples:
 * /usr/share/kde4/apps/katepart/syntax/markdown.xml
 */
void MarkdownHighlighter::initHighlightingRules() {
    // highlight block quotes
    {
        HighlightingRule rule(HighlighterState::BlockQuote);
        rule.pattern = QRegularExpression(
            _highlightingOptions.testFlag(
                HighlightingOption::FullyHighlightedBlockQuote)
                ? QStringLiteral("^\\s*(>\\s*.+)")
                : QStringLiteral("^\\s*(>\\s*)+"));
        rule.shouldContain = QStringLiteral("> ");
        _highlightingRules.append(rule);
    }

    // highlight tables without starting |
    // we drop that for now, it's far too messy to deal with
    //    rule = HighlightingRule();
    //    rule.pattern = QRegularExpression("^.+? \\| .+? \\| .+$");
    //    rule.state = HighlighterState::Table;
    //    _highlightingRulesPre.append(rule);
    // highlight trailing spaces
    {
        HighlightingRule rule(HighlighterState::TrailingSpace);
        rule.pattern = QRegularExpression(QStringLiteral("( +)$"));
        rule.shouldContain = QStringLiteral("  ");
        rule.capturingGroup = 1;
        _highlightingRules.append(rule);
    }

    // highlight inline comments
    {
        // highlight comments for R Markdown for academic papers
        HighlightingRule rule(HighlighterState::Comment);
        rule.pattern =
            QRegularExpression(QStringLiteral(R"(^\[.+?\]: # \(.+?\)$)"));
        rule.shouldContain = QStringLiteral("]: # (");
        _highlightingRules.append(rule);
    }

    // highlight tables with starting |
    {
        HighlightingRule rule(HighlighterState::Table);
        rule.shouldContain = QStringLiteral("|");
        // Support up to 3 leading spaces, because md4c seems to support it
        // See https://github.com/pbek/QOwnNotes/issues/3137
        rule.pattern =
            QRegularExpression(QStringLiteral("^\\s{0,3}(\\|.+?\\|)$"));
        rule.capturingGroup = 1;
        _highlightingRules.append(rule);
    }
}

/**
 * Initializes the text formats
 *
 * @param defaultFontSize
 */
void MarkdownHighlighter::initTextFormats(int defaultFontSize) {
    QTextCharFormat format;

    // set character formats for headlines
    format = QTextCharFormat();
    format.setForeground(QColor(2, 69, 150));
    format.setFontWeight(QFont::Bold);
    format.setFontPointSize(defaultFontSize * 1.6);
    _formats[H1] = format;
    format.setFontPointSize(defaultFontSize * 1.5);
    _formats[H2] = format;
    format.setFontPointSize(defaultFontSize * 1.4);
    _formats[H3] = format;
    format.setFontPointSize(defaultFontSize * 1.3);
    _formats[H4] = format;
    format.setFontPointSize(defaultFontSize * 1.2);
    _formats[H5] = format;
    format.setFontPointSize(defaultFontSize * 1.1);
    _formats[H6] = format;
    format.setFontPointSize(defaultFontSize);

    // set character format for horizontal rulers
    format = QTextCharFormat();
    format.setForeground(Qt::darkGray);
    format.setBackground(Qt::lightGray);
    _formats[HorizontalRuler] = std::move(format);

    // set character format for lists
    format = QTextCharFormat();
    format.setForeground(QColor(163, 0, 123));
    _formats[List] = format;

    // set character format for checkbox
    format = QTextCharFormat();
    format.setForeground(QColor(123, 100, 223));
    _formats[CheckBoxUnChecked] = std::move(format);
    // set character format for checked checkbox
    format = QTextCharFormat();
    format.setForeground(QColor(223, 50, 123));
    _formats[CheckBoxChecked] = std::move(format);

    // set character format for links
    format = QTextCharFormat();
    format.setForeground(QColor(0, 128, 255));
    format.setFontUnderline(true);
    _formats[Link] = std::move(format);

    // set character format for images
    format = QTextCharFormat();
    format.setForeground(QColor(0, 191, 0));
    format.setBackground(QColor(228, 255, 228));
    _formats[Image] = std::move(format);

    // set character format for code blocks
    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // format.setBackground(QColor(220, 220, 220));
    _formats[CodeBlock] = format;
    _formats[InlineCodeBlock] = format;

    // set character format for italic
    format = QTextCharFormat();
    format.setFontWeight(QFont::StyleItalic);
    format.setFontItalic(true);
    _formats[Italic] = std::move(format);

    // set character format for underline
    format = QTextCharFormat();
    format.setFontUnderline(true);
    _formats[StUnderline] = std::move(format);

    // set character format for bold
    format = QTextCharFormat();
    format.setFontWeight(QFont::Bold);
    _formats[Bold] = std::move(format);

    // set character format for comments
    format = QTextCharFormat();
    format.setForeground(QBrush(Qt::gray));
    _formats[Comment] = std::move(format);

    // set character format for masked syntax
    format = QTextCharFormat();
    format.setForeground(QColor(204, 204, 204));
    _formats[MaskedSyntax] = std::move(format);

    // set character format for tables
    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(100, 148, 73));
    _formats[Table] = std::move(format);

    // set character format for block quotes
    format = QTextCharFormat();
    format.setForeground(Qt::darkRed);
    _formats[BlockQuote] = std::move(format);

    format = QTextCharFormat();
    _formats[HeadlineEnd] = std::move(format);
    _formats[NoState] = std::move(format);

    // set character format for trailing spaces
    format.setBackground(QColor(252, 175, 62));
    _formats[TrailingSpace] = std::move(format);

    /****************************************
     * Formats for syntax highlighting
     ***************************************/

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(249, 38, 114));
    _formats[CodeKeyWord] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(163, 155, 78));
    _formats[CodeString] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(117, 113, 94));
    _formats[CodeComment] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(84, 174, 191));
    _formats[CodeType] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(219, 135, 68));
    _formats[CodeOther] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(174, 129, 255));
    _formats[CodeNumLiteral] = std::move(format);

    format = QTextCharFormat();
    format.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    format.setForeground(QColor(1, 138, 15));
    _formats[CodeBuiltIn] = std::move(format);
}

/**
 * @brief initializes the langStringToEnum
 */
void MarkdownHighlighter::initCodeLangs() {
    MarkdownHighlighter::_langStringToEnum =
        QHash<QString, MarkdownHighlighter::HighlighterState>{
            {QLatin1String("bash"), MarkdownHighlighter::CodeBash},
            {QLatin1String("c"), MarkdownHighlighter::CodeC},
            {QLatin1String("cpp"), MarkdownHighlighter::CodeCpp},
            {QLatin1String("cxx"), MarkdownHighlighter::CodeCpp},
            {QLatin1String("c++"), MarkdownHighlighter::CodeCpp},
            {QLatin1String("c#"), MarkdownHighlighter::CodeCSharp},
            {QLatin1String("cmake"), MarkdownHighlighter::CodeCMake},
            {QLatin1String("csharp"), MarkdownHighlighter::CodeCSharp},
            {QLatin1String("css"), MarkdownHighlighter::CodeCSS},
            {QLatin1String("go"), MarkdownHighlighter::CodeGo},
            {QLatin1String("html"), MarkdownHighlighter::CodeXML},
            {QLatin1String("ini"), MarkdownHighlighter::CodeINI},
            {QLatin1String("java"), MarkdownHighlighter::CodeJava},
            {QLatin1String("javascript"), MarkdownHighlighter::CodeJava},
            {QLatin1String("js"), MarkdownHighlighter::CodeJs},
            {QLatin1String("json"), MarkdownHighlighter::CodeJSON},
            {QLatin1String("make"), MarkdownHighlighter::CodeMake},
            {QLatin1String("nix"), MarkdownHighlighter::CodeNix},
            {QLatin1String("php"), MarkdownHighlighter::CodePHP},
            {QLatin1String("py"), MarkdownHighlighter::CodePython},
            {QLatin1String("python"), MarkdownHighlighter::CodePython},
            {QLatin1String("qml"), MarkdownHighlighter::CodeQML},
            {QLatin1String("rust"), MarkdownHighlighter::CodeRust},
            {QLatin1String("sh"), MarkdownHighlighter::CodeBash},
            {QLatin1String("sql"), MarkdownHighlighter::CodeSQL},
            {QLatin1String("taggerscript"),
             MarkdownHighlighter::CodeTaggerScript},
            {QLatin1String("ts"), MarkdownHighlighter::CodeTypeScript},
            {QLatin1String("typescript"), MarkdownHighlighter::CodeTypeScript},
            {QLatin1String("v"), MarkdownHighlighter::CodeV},
            {QLatin1String("vex"), MarkdownHighlighter::CodeVex},
            {QLatin1String("xml"), MarkdownHighlighter::CodeXML},
            {QLatin1String("yml"), MarkdownHighlighter::CodeYAML},
            {QLatin1String("yaml"), MarkdownHighlighter::CodeYAML},
            {QLatin1String("forth"), MarkdownHighlighter::CodeForth},
            {QLatin1String("systemverilog"),
             MarkdownHighlighter::CodeSystemVerilog},
            {QLatin1String("gdscript"), MarkdownHighlighter::CodeGDScript},
            {QLatin1String("toml"), MarkdownHighlighter::CodeTOML},
        };
}

/**
 * Sets the text formats
 *
 * @param formats
 */
void MarkdownHighlighter::setTextFormats(
    QHash<HighlighterState, QTextCharFormat> formats) {
    _formats = std::move(formats);
}

/**
 * Sets a text format
 *
 * @param formats
 */
void MarkdownHighlighter::setTextFormat(HighlighterState state,
                                        QTextCharFormat format) {
    _formats[state] = std::move(format);
}

/**
 * Does the Markdown highlighting
 *
 * @param text
 */
void MarkdownHighlighter::highlightBlock(const QString &text) {
    if (currentBlockState() == HeadlineEnd) {
        currentBlock().previous().setUserState(NoState);
        addDirtyBlock(currentBlock().previous());
    }
    setCurrentBlockState(HighlighterState::NoState);
    currentBlock().setUserState(HighlighterState::NoState);

    highlightMarkdown(text);
    _highlightingFinished = true;
}

void MarkdownHighlighter::highlightMarkdown(const QString &text) {
    const bool isBlockCodeBlock = isCodeBlock(previousBlockState()) ||
                                  text.startsWith(QLatin1String("```")) ||
                                  text.startsWith(QLatin1String("~~~"));

    if (!text.isEmpty() && !isBlockCodeBlock) {
        highlightAdditionalRules(_highlightingRules, text);

        highlightThematicBreak(text);

        // needs to be called after the horizontal ruler highlighting
        highlightHeadline(text);

        highlightIndentedCodeBlock(text);

        highlightLists(text);

        highlightInlineRules(text);
    }

    highlightCommentBlock(text);
    if (isBlockCodeBlock) highlightCodeFence(text);
    highlightFrontmatterBlock(text);
}

/**
 * @brief gets indentation(spaces) of text
 * @param text
 * @return 1, if 1 space, 2 if 2 spaces, 3 if 3 spaces. Otherwise 0
 */
static int getIndentation(const QString &text) {
    int spaces = 0;
    // no more than 3 spaces
    while (spaces < 4 && spaces < text.length() &&
           text.at(spaces) == QLatin1Char(' '))
        ++spaces;
    return spaces;
}

static bool isParagraph(const QString &text) {
    // blank line
    if (text.isEmpty()) return false;
    int indent = getIndentation(text);
    // code block
    if (indent >= 4) return false;

    const auto textView = MH_SUBSTR(indent, -1);
    if (textView.isEmpty()) return false;

    // unordered listtextView
    if (textView.startsWith(QStringLiteral("- ")) ||
        textView.startsWith(QStringLiteral("+ ")) ||
        textView.startsWith(QStringLiteral("* "))) {
        return false;
    }
    // block quote
    if (textView.startsWith(QStringLiteral("> "))) return false;
    // atx heading
    if (textView.startsWith(QStringLiteral("#"))) {
        int firstSpace = textView.indexOf(' ');
        if (firstSpace > 0 && firstSpace <= 7) {
            return false;
        }
    }
    // hr
    auto isThematicBreak = [textView]() {
        return std::all_of(textView.begin(), textView.end(),
                           [](QChar c) {
                               auto ch = c.unicode();
                               return ch == '-' || ch == ' ' || ch == '\t';
                           }) ||
               std::all_of(textView.begin(), textView.end(),
                           [](QChar c) {
                               auto ch = c.unicode();
                               return ch == '+' || ch == ' ' || ch == '\t';
                           }) ||
               std::all_of(textView.begin(), textView.end(), [](QChar c) {
                   auto ch = c.unicode();
                   return ch == '*' || ch == ' ' || ch == '\t';
               });
    };
    if (isThematicBreak()) return false;
    // ordered list
    if (textView.at(0).isDigit()) {
        int i = 1;
        int count = 1;
        for (; i < textView.size(); ++i) {
            if (textView[i].isDigit()) {
                count++;
                continue;
            } else
                break;
        }

        // ordered list marker can't be more than 9 numbers
        if (count <= 9 && i + 1 < textView.size() &&
            (textView[i] == QLatin1Char('.') ||
             textView[i] == QLatin1Char(')')) &&
            textView[i + 1] == QLatin1Char(' ')) {
            return false;
        }
    }

    return true;
}

/**
 * Highlight headlines
 *
 * @param text
 */
void MarkdownHighlighter::highlightHeadline(const QString &text) {
    // three spaces indentation is allowed in headings
    const int spacesOffset = getIndentation(text);

    if (spacesOffset >= text.length() || spacesOffset == 4) return;

    const bool headingFound = text.at(spacesOffset) == QLatin1Char('#');

    if (headingFound) {
        int headingLevel = 0;
        int i = spacesOffset;
        if (i >= text.length()) return;
        while (i < text.length() && text.at(i) == QLatin1Char('#') &&
               i < (spacesOffset + 6))
            ++i;

        if (i < text.length() && text.at(i) == QLatin1Char(' '))
            headingLevel = i - spacesOffset;

        if (headingLevel > 0) {
            const auto state =
                HighlighterState(HighlighterState::H1 + headingLevel - 1);

            // Set styling of the "#"s to "masked syntax", but with the size of
            // the heading
            auto maskedFormat = _formats[MaskedSyntax];
            maskedFormat.setFontPointSize(_formats[state].fontPointSize());
            setFormat(0, headingLevel, maskedFormat);

            // Set the styling of the rest of the heading
            setFormat(headingLevel + 1, text.length() - 1 - headingLevel,
                      _formats[state]);

            setCurrentBlockState(state);
            return;
        }
    }

    auto hasOnlyHeadChars = [](const QString &txt, const QChar c,
                               int spaces) -> bool {
        if (txt.isEmpty()) return false;
        for (int i = spaces; i < txt.length(); ++i) {
            if (txt.at(i) != c) return false;
        }
        return true;
    };

    // take care of ==== and ---- headlines
    const QString prev = currentBlock().previous().text();
    auto prevSpaces = getIndentation(prev);
    const bool isPrevParagraph = isParagraph(prev);

    if (text.at(spacesOffset) == QLatin1Char('=') && prevSpaces < 4 &&
        isPrevParagraph) {
        const bool pattern1 =
            !prev.isEmpty() &&
            hasOnlyHeadChars(text, QLatin1Char('='), spacesOffset);
        if (pattern1) {
            highlightSubHeadline(text, H1);
            return;
        }
    } else if (text.at(spacesOffset) == QLatin1Char('-') && prevSpaces < 4 &&
               isPrevParagraph) {
        const bool pattern2 =
            !prev.isEmpty() &&
            hasOnlyHeadChars(text, QLatin1Char('-'), spacesOffset);
        if (pattern2) {
            highlightSubHeadline(text, H2);
            return;
        }
    }

    const QString nextBlockText = currentBlock().next().text();
    if (nextBlockText.isEmpty()) return;
    const int nextSpaces = getIndentation(nextBlockText);
    const bool isCurrentParagraph = isParagraph(text);

    if (nextSpaces >= nextBlockText.length()) return;

    if (nextBlockText.at(nextSpaces) == QLatin1Char('=') && nextSpaces < 4 &&
        isCurrentParagraph) {
        const bool nextHasEqualChars =
            hasOnlyHeadChars(nextBlockText, QLatin1Char('='), nextSpaces);
        if (nextHasEqualChars) {
            setFormat(0, text.length(), _formats[HighlighterState::H1]);
            setCurrentBlockState(HighlighterState::H1);
        }
    } else if (nextBlockText.at(nextSpaces) == QLatin1Char('-') &&
               nextSpaces < 4 && isCurrentParagraph) {
        const bool nextHasMinusChars =
            hasOnlyHeadChars(nextBlockText, QLatin1Char('-'), nextSpaces);
        if (nextHasMinusChars) {
            setFormat(0, text.length(), _formats[HighlighterState::H2]);
            setCurrentBlockState(HighlighterState::H2);
        }
    }
}

void MarkdownHighlighter::highlightSubHeadline(const QString &text,
                                               HighlighterState state) {
    const QTextCharFormat &maskedFormat =
        _formats[HighlighterState::MaskedSyntax];
    QTextBlock previousBlock = currentBlock().previous();

    // we check for both H1/H2 so that if the user changes his mind, and changes
    // === to ---, changes be reflected immediately
    if (previousBlockState() == H1 || previousBlockState() == H2 ||
        previousBlockState() == NoState) {
        QTextCharFormat currentMaskedFormat = maskedFormat;
        // set the font size from the current rule's font format
        currentMaskedFormat.setFontPointSize(_formats[state].fontPointSize());

        setFormat(0, text.length(), currentMaskedFormat);
        setCurrentBlockState(HeadlineEnd);

        // we want to re-highlight the previous block
        // this must not be done directly, but with a queue, otherwise it
        // will crash
        // setting the character format of the previous text, because this
        // causes text to be formatted the same way when writing after
        // the text
        if (previousBlockState() != state) {
            addDirtyBlock(previousBlock);
            previousBlock.setUserState(state);
        }
    }
}

/**
 * @brief highlight code blocks with four spaces or tabs in front of them
 * and no list character after that
 * @param text
 */
void MarkdownHighlighter::highlightIndentedCodeBlock(const QString &text) {
    if (text.isEmpty() || (!text.startsWith(QLatin1String("    ")) &&
                           !text.startsWith(QLatin1Char('\t'))))
        return;

    const QString prevTrimmed = currentBlock().previous().text().trimmed();
    // previous line must be empty according to CommonMark except if it is a
    // heading https://spec.commonmark.org/0.29/#indented-code-block
    if (!prevTrimmed.isEmpty() && previousBlockState() != CodeBlockIndented &&
        !isHeading(previousBlockState()) && previousBlockState() != HeadlineEnd)
        return;

    const QString trimmed = text.trimmed();

    // should not be in a list
    if (trimmed.startsWith(QLatin1String("- ")) ||
        trimmed.startsWith(QLatin1String("+ ")) ||
        trimmed.startsWith(QLatin1String("* ")) ||
        (trimmed.length() >= 1 && trimmed.at(0).isNumber()))
        return;

    setCurrentBlockState(CodeBlockIndented);
    setFormat(0, text.length(), _formats[CodeBlock]);
}

void MarkdownHighlighter::highlightCodeFence(const QString &text) {
    // already in tilde block
    if ((previousBlockState() == CodeBlockTilde ||
         previousBlockState() == CodeBlockTildeComment ||
         previousBlockState() >= CodeCpp + tildeOffset)) {
        highlightCodeBlock(text, QStringLiteral("~~~"));
        // start of a tilde block
    } else if ((previousBlockState() != CodeBlock &&
                previousBlockState() < CodeCpp) &&
               text.startsWith(QLatin1String("~~~"))) {
        highlightCodeBlock(text, QStringLiteral("~~~"));
    } else {
        // back tick block
        highlightCodeBlock(text);
    }
}

/**
 * Highlight multi-line code blocks
 *
 * @param text
 */
void MarkdownHighlighter::highlightCodeBlock(const QString &text,
                                             const QString &opener) {
    if (text.startsWith(opener)) {
        // if someone decides to put these on the same line
        // interpret it as inline code, not code block
        if (text.endsWith(QLatin1String("```")) && text.length() > 3) {
            setFormat(3, text.length() - 3,
                      _formats[HighlighterState::InlineCodeBlock]);
            setFormat(0, 3, _formats[HighlighterState::MaskedSyntax]);
            setFormat(text.length() - 3, 3,
                      _formats[HighlighterState::MaskedSyntax]);
            return;
        }
        if ((previousBlockState() != CodeBlock &&
             previousBlockState() != CodeBlockTilde) &&
            (previousBlockState() != CodeBlockComment &&
             previousBlockState() != CodeBlockTildeComment) &&
            previousBlockState() < CodeCpp) {
            const QString &lang = text.mid(3, text.length()).toLower();
            HighlighterState progLang = _langStringToEnum.value(lang);

            if (progLang >= CodeCpp) {
                const int state = text.startsWith(QLatin1String("```"))
                                      ? progLang
                                      : progLang + tildeOffset;
                setCurrentBlockState(state);
            } else {
                const int state =
                    opener == QLatin1String("```") ? CodeBlock : CodeBlockTilde;
                setCurrentBlockState(state);
            }
        } else if (isCodeBlock(previousBlockState())) {
            const int state = opener == QLatin1String("```")
                                  ? CodeBlockEnd
                                  : CodeBlockTildeEnd;
            setCurrentBlockState(state);
        }

        // set the font size from the current rule's font format
        QTextCharFormat &maskedFormat = _formats[MaskedSyntax];
        maskedFormat.setFontPointSize(_formats[CodeBlock].fontPointSize());

        setFormat(0, text.length(), maskedFormat);
    } else if (isCodeBlock(previousBlockState())) {
        setCurrentBlockState(previousBlockState());
        highlightSyntax(text);
    }
}

/**
 * @brief Does the code syntax highlighting
 * @param text
 */
void MarkdownHighlighter::highlightSyntax(const QString &text) {
    if (text.isEmpty()) return;

    const auto textLen = text.length();

    QChar comment;
    bool isCSS = false;
    bool isYAML = false;
    bool isMake = false;
    bool isForth = false;
    bool isGDScript = false;
    bool isSQL = false;
    bool isTOML = false;

    QMultiHash<char, QLatin1String> keywords{};
    QMultiHash<char, QLatin1String> others{};
    QMultiHash<char, QLatin1String> types{};
    QMultiHash<char, QLatin1String> builtin{};
    QMultiHash<char, QLatin1String> literals{};

    // apply the default code block format first
    setFormat(0, textLen, _formats[CodeBlock]);

    switch (currentBlockState()) {
        case HighlighterState::CodeCpp:
        case HighlighterState::CodeCpp + tildeOffset:
        case HighlighterState::CodeCppComment:
        case HighlighterState::CodeCppComment + tildeOffset:
            loadCppData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeJs:
        case HighlighterState::CodeJs + tildeOffset:
        case HighlighterState::CodeJsComment:
        case HighlighterState::CodeJsComment + tildeOffset:
            loadJSData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeC:
        case HighlighterState::CodeC + tildeOffset:
        case HighlighterState::CodeCComment:
        case HighlighterState::CodeCComment + tildeOffset:
            loadCppData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeBash:
        case HighlighterState::CodeBash + tildeOffset:
            loadShellData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodePHP:
        case HighlighterState::CodePHP + tildeOffset:
        case HighlighterState::CodePHPComment:
        case HighlighterState::CodePHPComment + tildeOffset:
            loadPHPData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeQML:
        case HighlighterState::CodeQML + tildeOffset:
        case HighlighterState::CodeQMLComment:
        case HighlighterState::CodeQMLComment + tildeOffset:
            loadQMLData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodePython:
        case HighlighterState::CodePython + tildeOffset:
            loadPythonData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodeRust:
        case HighlighterState::CodeRust + tildeOffset:
        case HighlighterState::CodeRustComment:
        case HighlighterState::CodeRustComment + tildeOffset:
            loadRustData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeJava:
        case HighlighterState::CodeJava + tildeOffset:
        case HighlighterState::CodeJavaComment:
        case HighlighterState::CodeJavaComment + tildeOffset:
            loadJavaData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeCSharp:
        case HighlighterState::CodeCSharp + tildeOffset:
        case HighlighterState::CodeCSharpComment:
        case HighlighterState::CodeCSharpComment + tildeOffset:
            loadCSharpData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeGo:
        case HighlighterState::CodeGo + tildeOffset:
        case HighlighterState::CodeGoComment:
        case HighlighterState::CodeGoComment + tildeOffset:
            loadGoData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeV:
        case HighlighterState::CodeV + tildeOffset:
        case HighlighterState::CodeVComment:
        case HighlighterState::CodeVComment + tildeOffset:
            loadVData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeSQL:
        case HighlighterState::CodeSQL + tildeOffset:
        case HighlighterState::CodeSQLComment:
        case HighlighterState::CodeSQLComment + tildeOffset:
            loadSQLData(types, keywords, builtin, literals, others);
            isSQL = true;
            comment =
                QLatin1Char('-');    // prevent the default comment highlighting
            break;
        case HighlighterState::CodeJSON:
        case HighlighterState::CodeJSON + tildeOffset:
            loadJSONData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeXML:
        case HighlighterState::CodeXML + tildeOffset:
            xmlHighlighter(text);
            return;
        case HighlighterState::CodeCSS:
        case HighlighterState::CodeCSS + tildeOffset:
        case HighlighterState::CodeCSSComment:
        case HighlighterState::CodeCSSComment + tildeOffset:
            isCSS = true;
            loadCSSData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeTypeScript:
        case HighlighterState::CodeTypeScript + tildeOffset:
        case HighlighterState::CodeTypeScriptComment:
        case HighlighterState::CodeTypeScriptComment + tildeOffset:
            loadTypescriptData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeYAML:
        case HighlighterState::CodeYAML + tildeOffset:
            isYAML = true;
            comment = QLatin1Char('#');
            loadYAMLData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeINI:
        case HighlighterState::CodeINI + tildeOffset:
            iniHighlighter(text);
            return;
        case HighlighterState::CodeTaggerScript:
        case HighlighterState::CodeTaggerScript + tildeOffset:
            taggerScriptHighlighter(text);
            return;
        case HighlighterState::CodeVex:
        case HighlighterState::CodeVex + tildeOffset:
        case HighlighterState::CodeVexComment:
        case HighlighterState::CodeVexComment + tildeOffset:
            loadVEXData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeCMake:
        case HighlighterState::CodeCMake + tildeOffset:
            loadCMakeData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodeMake:
        case HighlighterState::CodeMake + tildeOffset:
            isMake = true;
            loadMakeData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodeNix:
        case HighlighterState::CodeNix + tildeOffset:
            loadNixData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodeForth:
        case HighlighterState::CodeForth + tildeOffset:
        case HighlighterState::CodeForthComment:
        case HighlighterState::CodeForthComment + tildeOffset:
            isForth = true;
            loadForthData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeSystemVerilog:
        case HighlighterState::CodeSystemVerilogComment:
            loadSystemVerilogData(types, keywords, builtin, literals, others);
            break;
        case HighlighterState::CodeGDScript:
        case HighlighterState::CodeGDScript + tildeOffset:
            isGDScript = true;
            loadGDScriptData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        case HighlighterState::CodeTOML:
        case HighlighterState::CodeTOML + tildeOffset:
        case HighlighterState::CodeTOMLString:
        case HighlighterState::CodeTOMLString + tildeOffset:
            isTOML = true;
            loadTOMLData(types, keywords, builtin, literals, others);
            comment = QLatin1Char('#');
            break;
        default:
            setFormat(0, textLen, _formats[CodeBlock]);
            return;
    }

    auto applyCodeFormat =
        [this](int i, const QMultiHash<char, QLatin1String> &data,
               const QString &text, const QTextCharFormat &fmt) -> int {
        // check if we are at the beginning OR if this is the start of a word
        if (i == 0 || (!text.at(i - 1).isLetterOrNumber() &&
                       text.at(i - 1) != QLatin1Char('_'))) {
            const char c = text.at(i).toLatin1();
            auto it = data.find(c);
            for (; it != data.end() && it.key() == c; ++it) {
                // we have a word match check
                // 1. if we are at the end
                // 2. if we have a complete word
                const QLatin1String &word = it.value();
                if (word == MH_SUBSTR(i, word.size()) &&
                    (i + word.size() == text.length() ||
                     (!text.at(i + word.size()).isLetterOrNumber() &&
                      text.at(i + word.size()) != QLatin1Char('_')))) {
                    setFormat(i, word.size(), fmt);
                    i += word.size();
                }
            }
        }
        return i;
    };

    const QTextCharFormat &formatType = _formats[CodeType];
    const QTextCharFormat &formatKeyword = _formats[CodeKeyWord];
    const QTextCharFormat &formatComment = _formats[CodeComment];
    const QTextCharFormat &formatNumLit = _formats[CodeNumLiteral];
    const QTextCharFormat &formatBuiltIn = _formats[CodeBuiltIn];
    const QTextCharFormat &formatOther = _formats[CodeOther];

    for (int i = 0; i < textLen; ++i) {
        if (currentBlockState() != -1 && currentBlockState() % 2 != 0)
            goto Comment;

        while (i < textLen && !text[i].isLetter()) {
            if (text[i].isSpace()) {
                ++i;
                // make sure we don't cross the bound
                if (i == textLen) break;
                if (text[i].isLetter()) break;
                continue;
            }
            // inline comment
            if (comment.isNull() && text[i] == QLatin1Char('/')) {
                if ((i + 1) < textLen) {
                    if (text[i + 1] == QLatin1Char('/')) {
                        setFormat(i, textLen, formatComment);
                        return;
                    } else if (text[i + 1] == QLatin1Char('*')) {
                    Comment:
                        int next = text.indexOf(QLatin1String("*/"), i);
                        if (next == -1) {
                            // we didn't find a comment end.
                            // Check if we are already in a comment block
                            if (currentBlockState() % 2 == 0)
                                setCurrentBlockState(currentBlockState() + 1);
                            setFormat(i, textLen, formatComment);
                            return;
                        } else {
                            // we found a comment end
                            // mark this block as code if it was previously
                            // comment. First check if the comment ended on the
                            // same line. if modulo 2 is not equal to zero, it
                            // means we are in a comment, -1 will set this
                            // block's state as language
                            if (currentBlockState() % 2 != 0) {
                                setCurrentBlockState(currentBlockState() - 1);
                            }
                            next += 2;
                            setFormat(i, next - i, formatComment);
                            i = next;
                            if (i >= textLen) return;
                        }
                    }
                }
            } else if (text[i] == comment) {
                setFormat(i, textLen, formatComment);
                i = textLen;
                break;
                // integer literal
            } else if (text[i].isNumber()) {
                i = highlightNumericLiterals(text, i);
                // string literals
            } else if (text[i] == QLatin1Char('\"') ||
                       text[i] == QLatin1Char('\'')) {
                i = highlightStringLiterals(text.at(i), text, i);
            }
            if (i >= textLen) {
                break;
            }
            ++i;
        }

        const int pos = i;

        if (i == textLen || !text[i].isLetter()) continue;

        /* Highlight Types */
        i = applyCodeFormat(i, types, text, formatType);
        /************************************************
         next letter is usually a space, in that case
         going forward is useless, so continue;
         ************************************************/
        if (i == textLen || !text[i].isLetter()) continue;

        /* Highlight Keywords */
        i = applyCodeFormat(i, keywords, text, formatKeyword);
        if (i == textLen || !text[i].isLetter()) continue;

        /* Highlight Literals (true/false/NULL,nullptr) */
        i = applyCodeFormat(i, literals, text, formatNumLit);
        if (i == textLen || !text[i].isLetter()) continue;

        /* Highlight Builtin library stuff */
        i = applyCodeFormat(i, builtin, text, formatBuiltIn);
        if (i == textLen || !text[i].isLetter()) continue;

        /* Highlight other stuff (preprocessor etc.) */
        if (i == 0 || !text.at(i - 1).isLetter()) {
            const char c = text.at(i).toLatin1();
            auto it = others.find(c);
            for (; it != others.end() && it.key() == c; ++it) {
                const QLatin1String &word = it.value();
                if (word == MH_SUBSTR(i, word.size()) &&
                    (i + word.size() == text.length() ||
                     !text.at(i + word.size()).isLetter())) {
                    currentBlockState() == CodeCpp ||
                            currentBlockState() == CodeC
                        ? setFormat(i - 1, word.size() + 1, formatOther)
                        : setFormat(i, word.size(), formatOther);
                    i += word.size();
                }
            }
        }

        // we were unable to find any match, lets skip this word
        if (pos == i) {
            int cnt = i;
            while (cnt < textLen) {
                if (!text[cnt].isLetter()) break;
                ++cnt;
            }
            i = cnt - 1;
        }
    }

    /***********************
    **** POST PROCESSORS ***
    ***********************/

    if (isCSS) cssHighlighter(text);
    if (isYAML) ymlHighlighter(text);
    if (isMake) makeHighlighter(text);
    if (isForth) forthHighlighter(text);
    if (isGDScript) gdscriptHighlighter(text);
    if (isSQL) sqlHighlighter(text);
    if (isTOML) tomlHighlighter(text);
}

/**
 * @brief Highlight string literals in code
 * @param strType str type i.e., ' or "
 * @param text the text being scanned
 * @param i pos of i in loop
 * @return pos of i after the string
 */
int MarkdownHighlighter::highlightStringLiterals(QChar strType,
                                                 const QString &text, int i) {
    const auto &strFormat = _formats[CodeString];
    setFormat(i, 1, strFormat);
    ++i;

    while (i < text.length()) {
        // look for string end
        // make sure it's not an escape seq
        if (text.at(i) == strType && text.at(i - 1) != QLatin1Char('\\')) {
            setFormat(i, 1, strFormat);
            ++i;
            break;
        }
        // look for escape sequence
        if (text.at(i) == QLatin1Char('\\') && (i + 1) < text.length()) {
            int len = 0;
            switch (text.at(i + 1).toLatin1()) {
                case 'a':
                case 'b':
                case 'e':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                case 'v':
                case '\'':
                case '"':
                case '\\':
                case '\?':
                    // 2 because we have to highlight \ as well as the following
                    // char
                    len = 2;
                    break;
                // octal esc sequence \123
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7': {
                    if (i + 4 <= text.length()) {
                        if (!isOctal(text.at(i + 2).toLatin1())) {
                            break;
                        }
                        if (!isOctal(text.at(i + 3).toLatin1())) {
                            break;
                        }
                        len = 4;
                    }
                    break;
                }
                // hex numbers \xFA
                case 'x': {
                    if (i + 3 <= text.length()) {
                        if (!isHex(text.at(i + 2).toLatin1())) {
                            break;
                        }
                        if (!isHex(text.at(i + 3).toLatin1())) {
                            break;
                        }
                        len = 4;
                    }
                    break;
                }
                // TODO: implement Unicode code point escaping
                default:
                    break;
            }

            // if len is zero, that means this wasn't an esc seq
            // increment i so that we skip this backslash
            if (len == 0) {
                setFormat(i, 1, strFormat);
                ++i;
                continue;
            }

            setFormat(i, len, _formats[CodeNumLiteral]);
            i += len;
            continue;
        }
        setFormat(i, 1, strFormat);
        ++i;
    }
    return i - 1;
}

/**
 * @brief Highlight numeric literals in code
 * @param text the text being scanned
 * @param i pos of i in loop
 * @return pos of i after the number
 *
 * @details it doesn't highlight the following yet:
 *  - 1000'0000
 */
int MarkdownHighlighter::highlightNumericLiterals(const QString &text, int i) {
    bool isPrefixAllowed = false;
    if (i == 0) {
        isPrefixAllowed = true;
    } else {
        // these values are allowed before a number
        switch (text.at(i - 1).toLatin1()) {
            // CSS number
            case ':':
                if (currentBlockState() == CodeCSS) {
                    isPrefixAllowed = true;
                }
                break;
            case '[':
            case '(':
            case '{':
            case ' ':
            case ',':
            case '=':
            case '+':
            case '-':
            case '*':
            case '/':
            case '%':
            case '<':
            case '>':
                isPrefixAllowed = true;
                break;
        }
    }

    if (!isPrefixAllowed) return i;

    const int start = i;

    if ((i + 1) >= text.length()) {
        setFormat(i, 1, _formats[CodeNumLiteral]);
        return ++i;
    }

    ++i;
    // hex numbers highlighting (only if there's a preceding zero)
    bool isCurrentHex = false;
    if (text.at(i) == QChar('x') && text.at(i - 1) == QChar('0')) {
        isCurrentHex = true;
        ++i;
    }

    while (i < text.length()) {
        if (!text.at(i).isNumber() && text.at(i) != QChar('.') &&
            text.at(i) != QChar('e') &&
            !(isCurrentHex && isHex(text.at(i).toLatin1())))
            break;
        ++i;
    }

    bool isPostfixAllowed = false;
    if (i == text.length()) {
        // cant have e at the end
        if (isCurrentHex || text.at(i - 1) != QChar('e')) {
            isPostfixAllowed = true;
        }
    } else {
        // these values are allowed after a number
        switch (text.at(i).toLatin1()) {
            case ']':
            case ')':
            case '}':
            case ' ':
            case ',':
            case '=':
            case '+':
            case '-':
            case '*':
            case '/':
            case '%':
            case '>':
            case '<':
            case ';':
                isPostfixAllowed = true;
                break;
            // for 100u, 1.0F
            case 'p':
                if (currentBlockState() == CodeCSS) {
                    if (i + 1 < text.length() && text.at(i + 1) == QChar('x')) {
                        if (i + 2 == text.length() ||
                            !text.at(i + 2).isLetterOrNumber()) {
                            isPostfixAllowed = true;
                        }
                    }
                }
                break;
            case 'e':
                if (currentBlockState() == CodeCSS) {
                    if (i + 1 < text.length() && text.at(i + 1) == QChar('m')) {
                        if (i + 2 == text.length() ||
                            !text.at(i + 2).isLetterOrNumber()) {
                            isPostfixAllowed = true;
                        }
                    }
                }
                break;
            case 'u':
            case 'l':
            case 'f':
            case 'U':
            case 'L':
            case 'F':
                if (i + 1 == text.length() ||
                    !text.at(i + 1).isLetterOrNumber()) {
                    isPostfixAllowed = true;
                    ++i;
                }
                break;
        }
    }
    if (isPostfixAllowed) {
        int end = i--;
        setFormat(start, end - start, _formats[CodeNumLiteral]);
    }
    // decrement so that the index is at the last number, not after it
    return i;
}

/**
 * @brief The Tagger Script highlighter
 * @param text
 * @details his function is responsible for taggerscript highlighting.
 * It highlights anything between a (inclusive) '&' and a (exclusive) '(' as a
 * function. An exception is the '$noop()'function, which get highlighted as a
 * comment.
 *
 * It has basic error detection when there is an unlcosed %Metadata Variable%
 */
void MarkdownHighlighter::taggerScriptHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();

    for (int i = 0; i < textLen; ++i) {
        // highlight functions, unless it's a comment function
        if (text.at(i) == QChar('$') &&
            MH_SUBSTR(i, 5) != QLatin1String("$noop")) {
            const int next = text.indexOf(QChar('('), i);
            if (next == -1) break;
            setFormat(i, next - i, _formats[CodeKeyWord]);
            i = next;
        }

        // highlight variables
        if (text.at(i) == QChar('%')) {
            const int next = text.indexOf(QChar('%'), i + 1);
            const int start = i;
            i++;
            if (next != -1) {
                setFormat(start, next - start + 1, _formats[CodeType]);
            } else {
                // error highlighting
                QTextCharFormat errorFormat = _formats[NoState];
                errorFormat.setUnderlineColor(Qt::red);
                errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
                setFormat(start, 1, errorFormat);
            }
        }

        // highlight comments
        if (MH_SUBSTR(i, 5) == QLatin1String("$noop")) {
            const int next = text.indexOf(QChar(')'), i);
            if (next == -1) break;
            setFormat(i, next - i + 1, _formats[CodeComment]);
            i = next;
        }

        // highlight escape chars
        if (text.at(i) == QChar('\\')) {
            setFormat(i, 2, _formats[CodeOther]);
            i++;
        }
    }
}

/**
 * @brief The YAML highlighter
 * @param text
 * @details This function post processes a line after the main syntax
 * highlighter has run for additional highlighting. It does these things
 *
 * If the current line is a comment, skip it
 *
 * Highlight all the words that have a colon after them as 'keyword' except:
 * If the word is a string, skip it.
 * If the colon is in between a path, skip it (C:\)
 *
 * Once the colon is found, the function will skip every character except 'h'
 *
 * If an h letter is found, check the next 4/5 letters for http/https and
 * highlight them as a link (underlined)
 */
void MarkdownHighlighter::ymlHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();
    bool colonNotFound = false;

    // if this is a comment don't do anything and just return
    if (text.trimmed().at(0) == QChar('#')) return;

    for (int i = 0; i < textLen; ++i) {
        if (!text.at(i).isLetter()) continue;

        if (colonNotFound && text.at(i) != QChar('h')) continue;

        // we found a string literal, skip it
        if (i != 0 && text.at(i - 1) == QChar('"')) {
            const int next = text.indexOf(QChar('"'), i);
            if (next == -1) break;
            i = next;
            continue;
        }

        if (i != 0 && text.at(i - 1) == QChar('\'')) {
            const int next = text.indexOf(QChar('\''), i);
            if (next == -1) break;
            i = next;
            continue;
        }

        const int colon = text.indexOf(QChar(':'), i);

        // if colon isn't found, we set this true
        if (colon == -1) colonNotFound = true;

        if (!colonNotFound) {
            // if the line ends here, format and return
            if (colon + 1 == textLen) {
                setFormat(i, colon - i, _formats[CodeKeyWord]);
                return;
            }
            // colon is found, check if it isn't some path or something else
            if (!(text.at(colon + 1) == QChar('\\') &&
                  text.at(colon + 1) == QChar('/'))) {
                setFormat(i, colon - i, _formats[CodeKeyWord]);
            }
        }

        // underlined links
        if (text.at(i) == QChar('h')) {
            if (MH_SUBSTR(i, 4) == QLatin1String("http")) {
                int space = text.indexOf(QChar(' '), i);
                if (space == -1) space = textLen;
                QTextCharFormat f = _formats[CodeString];
                f.setUnderlineStyle(QTextCharFormat::SingleUnderline);
                setFormat(i, space - i, f);
                i = space;
            }
        }
    }
}

/**
 * @brief The INI highlighter
 * @param text The text being highlighted
 * @details This function is responsible for ini highlighting.
 * It has basic error detection when
 * (1) You opened a section but didn't close with bracket e.g [Section
 * (2) You wrote an option but it didn't have an '='
 * Such errors will be marked with a dotted red underline
 *
 * It has comment highlighting support. Everything after a ';' will
 * be highlighted till the end of the line.
 *
 * An option value pair will be highlighted regardless of space. Example:
 * Option 1 = value
 * In this, 'Option 1' will be highlighted completely and not just '1'.
 * I am not sure about its correctness but for now its like this.
 *
 * The loop is unrolled frequently upon a match. Before adding anything
 * new be sure to test in debug mode and apply bound checking as required.
 */
void MarkdownHighlighter::iniHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();

    for (int i = 0; i < textLen; ++i) {
        // start of a [section]
        if (text.at(i) == QChar('[')) {
            QTextCharFormat sectionFormat = _formats[CodeType];
            int sectionEnd = text.indexOf(QChar(']'), i);
            // if an end bracket isn't found, we apply red underline to show
            // error
            if (sectionEnd == -1) {
                sectionFormat.setUnderlineStyle(QTextCharFormat::DotLine);
                sectionFormat.setUnderlineColor(Qt::red);
                sectionEnd = textLen;
            }
            sectionEnd++;
            setFormat(i, sectionEnd - i, sectionFormat);
            i = sectionEnd;
            if (i >= textLen) break;
        }

        // comment ';'
        else if (text.at(i) == QChar(';')) {
            setFormat(i, textLen - i, _formats[CodeComment]);
            i = textLen;
            break;
        }

        // key-val
        else if (text.at(i).isLetter()) {
            QTextCharFormat format = _formats[CodeKeyWord];
            int equalsPos = text.indexOf(QChar('='), i);
            if (equalsPos == -1) {
                format.setUnderlineColor(Qt::red);
                format.setUnderlineStyle(QTextCharFormat::DotLine);
                equalsPos = textLen;
            }
            setFormat(i, equalsPos - i, format);
            i = equalsPos - 1;
            if (i >= textLen) break;
        }
        // skip everything after '=' (except comment)
        else if (text.at(i) == QChar('=')) {
            const int findComment = text.indexOf(QChar(';'), i);
            if (findComment == -1) break;
            i = findComment - 1;
        }
    }
}

void MarkdownHighlighter::cssHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();
    for (int i = 0; i < textLen; ++i) {
        if (text[i] == QLatin1Char('.') || text[i] == QLatin1Char('#')) {
            if (i + 1 >= textLen) return;
            if (text[i + 1].isSpace() || text[i + 1].isNumber()) continue;
            int space = text.indexOf(QLatin1Char(' '), i);
            if (space < 0) {
                space = text.indexOf(QLatin1Char('{'), i);
                if (space < 0) {
                    space = textLen;
                }
            }
            setFormat(i, space - i, _formats[CodeKeyWord]);
            i = space;
        } else if (text[i] == QLatin1Char('c')) {
            if (MH_SUBSTR(i, 5) == QLatin1String("color")) {
                i += 5;
                const int colon = text.indexOf(QLatin1Char(':'), i);
                if (colon < 0) continue;
                i = colon;
                ++i;
                while (i < textLen) {
                    if (!text[i].isSpace()) break;
                    ++i;
                }
                int semicolon = text.indexOf(QLatin1Char(';'), i);
                if (semicolon < 0) semicolon = textLen;
                const QString color = text.mid(i, semicolon - i);
                QColor c(color);
                if (color.startsWith(QLatin1String("rgb"))) {
                    const int t = text.indexOf(QLatin1Char('('), i);
                    const int rPos = text.indexOf(QLatin1Char(','), t);
                    const int gPos = text.indexOf(QLatin1Char(','), rPos + 1);
                    const int bPos = text.indexOf(QLatin1Char(')'), gPos);
                    if (rPos > -1 && gPos > -1 && bPos > -1) {
                        const QString r = text.mid(t + 1, rPos - (t + 1));
                        const QString g = text.mid(rPos + 1, gPos - (rPos + 1));
                        const QString b = text.mid(gPos + 1, bPos - (gPos + 1));
                        c.setRgb(r.toInt(), g.toInt(), b.toInt());
                    } else {
                        c = _formats[HighlighterState::NoState]
                                .background()
                                .color();
                    }
                }

                if (!c.isValid()) {
                    continue;
                }

                int lightness{};
                QColor foreground;
                // really dark
                if (c.lightness() <= 20) {
                    foreground = Qt::white;
                } else if (c.lightness() > 20 && c.lightness() <= 51) {
                    foreground = QColor(204, 204, 204);
                } else if (c.lightness() > 51 && c.lightness() <= 110) {
                    foreground = QColor(187, 187, 187);
                } else if (c.lightness() > 127) {
                    lightness = c.lightness() + 100;
                    foreground = c.darker(lightness);
                } else {
                    lightness = c.lightness() + 100;
                    foreground = c.lighter(lightness);
                }

                QTextCharFormat f = _formats[CodeBlock];
                f.setBackground(c);
                f.setForeground(foreground);
                // clear prev format
                setFormat(i, semicolon - i, QTextCharFormat());
                setFormat(i, semicolon - i, f);
                i = semicolon;
            }
        }
    }
}

void MarkdownHighlighter::xmlHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();

    setFormat(0, textLen, _formats[CodeBlock]);

    for (int i = 0; i < textLen; ++i) {
        if (i + 1 < textLen && text[i] == QLatin1Char('<') &&
            text[i + 1] != QLatin1Char('!')) {
            const int found = text.indexOf(QLatin1Char('>'), i);
            if (found > 0) {
                ++i;
                if (text[i] == QLatin1Char('/')) ++i;
                setFormat(i, found - i, _formats[CodeKeyWord]);
            }
        }

        if (text[i] == QLatin1Char('=')) {
            int lastSpace = text.lastIndexOf(QLatin1Char(' '), i);
            if (lastSpace == i - 1)
                lastSpace = text.lastIndexOf(QLatin1Char(' '), i - 2);
            if (lastSpace > 0) {
                setFormat(lastSpace, i - lastSpace, _formats[CodeBuiltIn]);
            }
        }

        if (text[i] == QLatin1Char('\"')) {
            const int pos = i;
            int cnt = 1;
            ++i;
            // bound check
            if ((i + 1) >= textLen) return;
            while (i < textLen) {
                if (text[i] == QLatin1Char('\"')) {
                    ++cnt;
                    ++i;
                    break;
                }
                ++i;
                ++cnt;
                // bound check
                if ((i + 1) >= textLen) {
                    ++cnt;
                    break;
                }
            }
            setFormat(pos, cnt, _formats[CodeString]);
        }
    }
}

void MarkdownHighlighter::makeHighlighter(const QString &text) {
    const int colonPos = text.indexOf(QLatin1Char(':'));
    if (colonPos == -1) return;
    setFormat(0, colonPos, _formats[CodeBuiltIn]);
}

/**
 * @brief The Forth highlighter
 * @param text
 * @details This function performs filtering of Forth code and high lights
 * the specific details.
 * 1. It highlights the "\ " comments
 * 2. It highlights the "( " comments
 */
void MarkdownHighlighter::forthHighlighter(const QString &text) {
    if (text.isEmpty()) return;

    const auto textLen = text.length();

    // Default Format
    setFormat(0, textLen, _formats[CodeBlock]);

    for (int i = 0; i < textLen; ++i) {
        // 1, It highlights the "\ " comments
        if (i + 1 <= textLen && text[i] == QLatin1Char('\\') &&
            text[i + 1] == QLatin1Char(' ')) {
            // The full line is commented
            setFormat(i + 1, textLen - 1, _formats[CodeComment]);
            break;
        }
        // 2. It highlights the "( " comments
        else if (i + 1 <= textLen && text[i] == QLatin1Char('(') &&
                 text[i + 1] == QLatin1Char(' ')) {
            // Find the End bracket
            int lastBracket = text.lastIndexOf(QLatin1Char(')'), i);
            // Can't Handle wrong Format
            if (lastBracket <= 0) return;
            // ' )' at the end of the comment
            if (lastBracket <= textLen &&
                text[lastBracket] == QLatin1Char(' ')) {
                setFormat(i, lastBracket, _formats[CodeComment]);
            }
        }
    }
}

/**
 * @brief The GDScript highlighter
 * @param text
 * @details This function is responsible for GDScript highlighting.
 * 1. Hightlight '$' NodePath constructs.
 * 2. Highlight '%' UniqueNode constructs.
 * 3. Highlight '@' annotations as `CodeOther`
 */
void MarkdownHighlighter::gdscriptHighlighter(const QString &text) {
    if (text.isEmpty()) return;

    // 1. Hightlight '$' NodePath constructs.
    // 2. Highlight '%' UniqueNode constructs.
    const QRegularExpression re = QRegularExpression(QStringLiteral(
        R"([$%][a-zA-Z_][a-zA-Z0-9_]*(/[a-zA-Z_][a-zA-Z0-9_]*)*|@)"));
    QRegularExpressionMatchIterator i = re.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        // 3. Hightlight '@' annotation symbol
        if (match.captured().startsWith(QLatin1Char('@'))) {
            setFormat(match.capturedStart(), match.capturedLength(),
                      _formats[CodeOther]);
        } else {
            setFormat(match.capturedStart(), match.capturedLength(),
                      _formats[CodeNumLiteral]);
        }
    }
}

/**
 * @brief The SQL highlighter
 * @param text
 * @details This function is responsible for SQL comment highlighting.
 * 1. Highlight "--" comments
 * 2. Highlight "/ *"-style multi-line comments
 */
void MarkdownHighlighter::sqlHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();

    for (int i = 0; i < textLen; ++i) {
        if (i + 1 > textLen) {
            break;
        }
        // Check for comments: single-line, or multi-line start or end
        if (text[i] == QLatin1Char('-') && text[i + 1] == QLatin1Char('-')) {
            setFormat(i, textLen, _formats[CodeComment]);
        } else if (text[i] == QLatin1Char('/') &&
                   text[i + 1] == QLatin1Char('*')) {
            // we're in a multi-line comment now
            if (currentBlockState() % 2 == 0) {
                setCurrentBlockState(currentBlockState() + 1);
                // Did the multi-line comment end in the same line?
                int endingComment = text.indexOf(QLatin1String("*/"), i + 2);
                int highlightEnd = textLen;
                if (endingComment > -1) {
                    highlightEnd = endingComment + 2;
                }

                setFormat(i, highlightEnd - i, _formats[CodeComment]);
            }
        } else if (text[i] == QLatin1Char('*') &&
                   text[i + 1] == QLatin1Char('/')) {
            // we're now no longer in a multi-line comment
            if (currentBlockState() % 2 != 0) {
                setCurrentBlockState(currentBlockState() - 1);
                // Did the multi-line comment start in the same line?
                int startingComment = text.indexOf(QLatin1String("/*"), 0);
                int highlightStart = 0;
                if (startingComment > -1) {
                    highlightStart = startingComment;
                }

                setFormat(highlightStart - i, i + 1, _formats[CodeComment]);
            }
        }
    }
}

/**
 * @brief The TOML highlighter
 * @param text
 * @details This function is responsible for TOML highlighting.
 */
void MarkdownHighlighter::tomlHighlighter(const QString &text) {
    if (text.isEmpty()) return;
    const auto textLen = text.length();

    bool onlyWhitespaceBeforeHeader = true;
    int possibleAssignmentPos = text.indexOf(QLatin1Char('='), 0);
    int singleQStringStart = -1;
    int doubleQStringStart = -1;
    int multiSingleQStringStart = -1;
    int multiDoubleQStringStart = -1;
    QLatin1Char singleQ = QLatin1Char('\'');
    QLatin1Char doubleQ = QLatin1Char('"');

    for (int i = 0; i < textLen; ++i) {
        if (i + 1 > textLen) {
            break;
        }

        // track the state of strings
        // multiline highlighting doesn't quite behave due to clashing handling
        // of " and ' chars, but this accomodates normal " and ' strings, as
        // well as ones wrapped by either """ or '''
        if (text[i] == doubleQ) {
            if (i + 2 <= textLen && text[i + 1] == doubleQ &&
                text[i + 2] == doubleQ) {
                if (multiDoubleQStringStart > -1) {
                    multiDoubleQStringStart = -1;
                } else {
                    multiDoubleQStringStart = i;
                    int multiDoubleQStringEnd =
                        text.indexOf(QLatin1String("\"\"\""), i + 1);
                    if (multiDoubleQStringEnd > -1) {
                        setFormat(i, multiDoubleQStringEnd - i,
                                  _formats[CodeString]);
                        i = multiDoubleQStringEnd + 2;
                        multiDoubleQStringEnd = -1;
                        multiDoubleQStringStart = -1;
                        continue;
                    }
                }
            } else {
                if (doubleQStringStart > -1) {
                    doubleQStringStart = -1;
                } else {
                    doubleQStringStart = i;
                }
            }
        } else if (text[i] == singleQ) {
            if (i + 2 <= textLen && text[i + 1] == singleQ &&
                text[i + 2] == singleQ) {
                if (multiSingleQStringStart > -1) {
                    multiSingleQStringStart = -1;
                } else {
                    multiSingleQStringStart = i;
                    int multiSingleQStringEnd =
                        text.indexOf(QLatin1String("'''"), i + 1);
                    if (multiSingleQStringEnd > -1) {
                        setFormat(i, multiSingleQStringEnd - i,
                                  _formats[CodeString]);
                        i = multiSingleQStringEnd + 2;
                        multiSingleQStringEnd = -1;
                        multiSingleQStringStart = -1;
                        continue;
                    }
                }
            } else {
                if (singleQStringStart > -1) {
                    singleQStringStart = -1;
                } else {
                    singleQStringStart = i;
                }
            }
        }

        bool inString = doubleQStringStart > -1 || singleQStringStart > -1 ||
                        multiSingleQStringStart > -1 ||
                        multiDoubleQStringStart > -1;

        // do comment highlighting
        if (text[i] == QLatin1Char('#') && !inString) {
            setFormat(i, textLen - i, _formats[CodeComment]);
            return;
        }

        // table header (all stuff preceeding must only be whitespace)
        if (text[i] == QLatin1Char('[') && onlyWhitespaceBeforeHeader) {
            int headerEnd = text.indexOf(QLatin1Char(']'), i);
            if (headerEnd > -1) {
                setFormat(i, headerEnd + 1 - i, _formats[CodeType]);
                return;
            }
        }

        // handle numbers, inf, nan and datetime the same way
        if (i > possibleAssignmentPos && !inString &&
            (text[i].isNumber() || text.indexOf(QLatin1String("inf"), i) > 0 ||
             text.indexOf(QLatin1String("nan"), i) > 0)) {
            int nextWhitespace = text.indexOf(QLatin1Char(' '), i);
            int endOfNumber = textLen;
            if (nextWhitespace > -1) {
                if (text[nextWhitespace - 1] == QLatin1Char(','))
                    nextWhitespace--;
                endOfNumber = nextWhitespace;
            }

            int highlightStart = i;
            if (i > 0) {
                if (text[i - 1] == QLatin1Char('-') ||
                    text[i - 1] == QLatin1Char('+')) {
                    highlightStart--;
                }
            }
            setFormat(highlightStart, endOfNumber - highlightStart,
                      _formats[CodeNumLiteral]);
            i = endOfNumber;
        }

        if (!text[i].isSpace()) {
            onlyWhitespaceBeforeHeader = false;
        }
    }
}

/**
 * Highlight multi-line frontmatter blocks
 *
 * @param text
 */
void MarkdownHighlighter::highlightFrontmatterBlock(const QString &text) {
    if (text == QLatin1String("---")) {
        const bool foundEnd =
            previousBlockState() == HighlighterState::FrontmatterBlock;

        // return if the frontmatter block was already highlighted in previous
        // blocks, there just can be one frontmatter block
        if (!foundEnd && document()->firstBlock() != currentBlock()) {
            return;
        }

        setCurrentBlockState(foundEnd ? HighlighterState::FrontmatterBlockEnd
                                      : HighlighterState::FrontmatterBlock);

        QTextCharFormat &maskedFormat =
            _formats[HighlighterState::MaskedSyntax];
        setFormat(0, text.length(), maskedFormat);
    } else if (previousBlockState() == HighlighterState::FrontmatterBlock) {
        setCurrentBlockState(HighlighterState::FrontmatterBlock);
        setFormat(0, text.length(), _formats[HighlighterState::MaskedSyntax]);
    }
}

/**
 * Highlight multi-line comments
 *
 * @param text
 */
void MarkdownHighlighter::highlightCommentBlock(const QString &text) {
    if (text.startsWith(QLatin1String("    ")) ||
        text.startsWith(QLatin1Char('\t')))
        return;

    const QString &trimmedText = text.trimmed();
    const QString startText(QStringLiteral("<!--"));
    const QString endText(QStringLiteral("-->"));

    // we will skip this case because that is an inline comment and causes
    // troubles here
    if (trimmedText.startsWith(startText) && trimmedText.contains(endText)) {
        return;
    }

    if (!trimmedText.startsWith(startText) && trimmedText.contains(startText))
        return;

    const bool isComment =
        trimmedText.startsWith(startText) ||
        (!trimmedText.endsWith(endText) && previousBlockState() == Comment);
    const bool isCommentEnd =
        trimmedText.endsWith(endText) && previousBlockState() == Comment;
    const bool highlight = isComment || isCommentEnd;

    if (isComment) setCurrentBlockState(Comment);
    if (highlight) setFormat(0, text.length(), _formats[Comment]);
}

/**
 * @brief Highlights thematic breaks i.e., horizontal ruler <hr/>
 * @param text
 */
void MarkdownHighlighter::highlightThematicBreak(const QString &text) {
    int i = 0;
    for (; i < 4 && i < text.length(); ++i) {
        if (text.at(i) != QLatin1Char(' ')) break;
    }

    const QString sText = text.mid(i);
    if (sText.isEmpty() || i == 4 || text.startsWith(QLatin1Char('\t'))) return;

    const char c = sText.at(0).toLatin1();
    if (c != '-' && c != '_' && c != '*') return;

    int len = 0;
    bool hasSameChars = true;
    for (int i = 0; i < sText.length(); ++i) {
        if (c != sText.at(i) && sText.at(i) != QLatin1Char(' ')) {
            hasSameChars = false;
            break;
        }
        if (sText.at(i) != QLatin1Char(' ')) ++len;
    }
    if (len < 3) return;

    if (hasSameChars) setFormat(0, text.length(), _formats[HorizontalRuler]);
}

void MarkdownHighlighter::highlightCheckbox(const QString &text, int curPos) {
    if (curPos + 4 >= text.length()) return;

    const bool hasOpeningBracket = text.at(curPos + 2) == QLatin1Char('[');
    const bool hasClosingBracket = text.at(curPos + 4) == QLatin1Char(']');
    const QChar midChar = text.at(curPos + 3);
    const bool hasXorSpace = midChar == QLatin1Char(' ') ||
                             midChar == QLatin1Char('x') ||
                             midChar == QLatin1Char('X');
    const bool hasDash = midChar == QLatin1Char('-');

    if (hasOpeningBracket && hasClosingBracket && (hasXorSpace || hasDash)) {
        const int start = curPos + 2;
        constexpr int length = 3;

        const auto fmt = hasXorSpace
                             ? (midChar == QLatin1Char(' ') ? CheckBoxUnChecked
                                                            : CheckBoxChecked)
                             : MaskedSyntax;

        setFormat(start, length, _formats[fmt]);
    }
}

static bool isBeginningOfList(QChar front) {
    return front == QLatin1Char('-') || front == QLatin1Char('+') ||
           front == QLatin1Char('*') || front.isNumber();
}

/**
 * @brief Highlight lists in markdown
 * @param text - current text block
 */
void MarkdownHighlighter::highlightLists(const QString &text) {
    int spaces = 0;
    // Skip any spaces in the beginning
    while (spaces < text.length() && text.at(spaces).isSpace()) ++spaces;

    // return if we reached the end
    if (spaces >= text.length()) return;

    const QChar front = text.at(spaces);
    // check for start of list
    if (!isBeginningOfList(front)) {
        return;
    }

    const int curPos = spaces;

    // Ordered List
    if (front.isNumber()) {
        int number = curPos;
        // move forward till first non-number char
        while (number < text.length() && text.at(number).isNumber()) ++number;
        int count = number - curPos;

        // reached end?
        if (number + 1 >= text.length() || count > 9) return;

        // there should be a '.' or ')' after a number
        if ((text.at(number) == QLatin1Char('.') ||
             text.at(number) == QLatin1Char(')')) &&
            (text.at(number + 1) == QLatin1Char(' '))) {
            setCurrentBlockState(List);
            setFormat(curPos, number - curPos + 1, _formats[List]);

            // highlight checkbox if any
            highlightCheckbox(text, number);
        }

        return;
    }

    // if its just a '-' etc, no highlighting
    if (curPos + 1 >= text.length()) return;

    // check for a space after it
    if (text.at(curPos + 1) != QLatin1Char(' ')) return;

    // check if we are in checkbox list
    highlightCheckbox(text, curPos);

    /* Unordered List */
    setCurrentBlockState(List);
    setFormat(curPos, 1, _formats[List]);
}

/**
 * Format italics, bolds and links in headings(h1-h6)
 *
 * @param format The format that is being applied
 * @param match The regex match
 * @param capturedGroup The captured group
 */
void MarkdownHighlighter::setHeadingStyles(HighlighterState rule,
                                           const QRegularExpressionMatch &match,
                                           const int capturedGroup) {
    auto state = static_cast<HighlighterState>(currentBlockState());
    const QTextCharFormat &f = _formats[state];

    if (rule == HighlighterState::Link) {
        auto linkFmt = _formats[Link];
        linkFmt.setFontPointSize(f.fontPointSize());
        if (capturedGroup == 1) {
            setFormat(match.capturedStart(capturedGroup),
                      match.capturedLength(capturedGroup), linkFmt);
        }
        return;
    }
}

/**
 * Highlights the rules from the _highlightingRules list
 *
 * @param text
 */
void MarkdownHighlighter::highlightAdditionalRules(
    const QVector<HighlightingRule> &rules, const QString &text) {
    const auto &maskedFormat = _formats[HighlighterState::MaskedSyntax];

    for (const HighlightingRule &rule : rules) {
        // continue if another current block state was already set if
        // disableIfCurrentStateIsSet is set
        if (currentBlockState() != NoState) continue;

        const bool contains = text.contains(rule.shouldContain);
        if (!contains) continue;

        auto iterator = rule.pattern.globalMatch(text);
        const uint8_t capturingGroup = rule.capturingGroup;
        const uint8_t maskedGroup = rule.maskedGroup;
        const QTextCharFormat &format = _formats[rule.state];

        // find and format all occurrences
        while (iterator.hasNext()) {
            QRegularExpressionMatch match = iterator.next();

            // if there is a capturingGroup set then first highlight
            // everything as MaskedSyntax and highlight capturingGroup
            // with the real format
            if (capturingGroup > 0) {
                QTextCharFormat currentMaskedFormat = maskedFormat;
                // set the font size from the current rule's font format
                if (format.fontPointSize() > 0) {
                    currentMaskedFormat.setFontPointSize(
                        format.fontPointSize());
                }

                if (isHeading(currentBlockState())) {
                    // setHeadingStyles(format, match, maskedGroup);

                } else {
                    setFormat(match.capturedStart(maskedGroup),
                              match.capturedLength(maskedGroup),
                              currentMaskedFormat);
                }
            }
            if (isHeading(currentBlockState())) {
                setHeadingStyles(rule.state, match, capturingGroup);

            } else {
                setFormat(match.capturedStart(capturingGroup),
                          match.capturedLength(capturingGroup), format);
            }
        }
    }
}

/**
 * @brief helper function to check if we are in a link while highlighting inline
 * rules
 * @param pos
 * @param range
 */
int isInLinkRange(int pos, QVector<QPair<int, int>> &range) {
    int j = 0;
    for (const auto &i : range) {
        if (pos >= i.first && pos <= i.second) {
            // return the length of the range so that we can skip it
            const int len = i.second - i.first;
            range.remove(j);
            return len;
        }
        ++j;
    }
    return -1;
}

/**
 * @brief highlight inline rules aka Emphasis, bolds, inline code spans,
 * underlines, strikethrough, links, and images.
 */
void MarkdownHighlighter::highlightInlineRules(const QString &text) {
    // clear existing span ranges for this block
    auto it = _ranges.find(currentBlock().blockNumber());
    if (it != _ranges.end()) {
        it->clear();
    }

    for (int i = 0; i < text.length(); ++i) {
        QChar currentChar = text.at(i);

        if (currentChar == QLatin1Char('`') ||
            currentChar == QLatin1Char('~')) {
            i = highlightInlineSpans(text, i, currentChar);
        } else if (currentChar == QLatin1Char('<') &&
                   MH_SUBSTR(i, 4) == QLatin1String("<!--")) {
            i = highlightInlineComment(text, i);
        } else {
            i = highlightLinkOrImage(text, i);
        }
    }

    highlightEmAndStrong(text, 0);
}

// Helper function for MarkdownHighlighter::highlightLinkOrImage
bool isLink(const QString &text) {
    static const QLatin1String supportedSchemes[] = {
        QLatin1String("http://"),  QLatin1String("https://"),
        QLatin1String("file://"),  QLatin1String("www."),
        QLatin1String("ftp://"),   QLatin1String("mailto:"),
        QLatin1String("tel:"),     QLatin1String("sms:"),
        QLatin1String("smsto:"),   QLatin1String("data:"),
        QLatin1String("irc://"),   QLatin1String("gopher://"),
        QLatin1String("spotify:"), QLatin1String("steam:"),
        QLatin1String("bitcoin:"), QLatin1String("magnet:"),
        QLatin1String("ed2k://"),  QLatin1String("news:"),
        QLatin1String("ssh://"),   QLatin1String("note://")};

    for (const QLatin1String &scheme : supportedSchemes) {
        if (text.startsWith(scheme)) {
            return true;
        }
    }

    return false;
}

bool isValidEmail(const QString &email) {
    // Check for a single '@' character
    int atIndex = email.indexOf('@');
    if (atIndex == -1) return false;

    // Check for at least one character before and after '@'
    if (atIndex == 0 || atIndex == email.length() - 1) return false;

    // Split email into local part and domain
    QString localPart = email.left(atIndex);
    QString domain = email.mid(atIndex + 1);

    // Check local part for validity (e.g., no consecutive dots)
    if (localPart.isEmpty() || localPart.contains("..")) return false;

    // Check domain for validity (e.g., at least one dot)
    if (domain.isEmpty() || domain.indexOf('.') == -1) return false;

    return true;
}

void MarkdownHighlighter::formatAndMaskRemaining(
    int formatBegin, int formatLength, int beginningText, int endText,
    const QTextCharFormat &format) {
    int afterFormat = formatBegin + formatLength;

    auto maskedSyntax = _formats[MaskedSyntax];
    maskedSyntax.setFontPointSize(
        QSyntaxHighlighter::format(beginningText).fontPointSize());

    // highlight before the link
    setFormat(beginningText, formatBegin - beginningText, maskedSyntax);

    // highlight the link if we are not in a heading
    if (!isHeading(currentBlockState())) {
        setFormat(formatBegin, formatLength, format);
    }

    // highlight after the link
    maskedSyntax.setFontPointSize(
        QSyntaxHighlighter::format(afterFormat).fontPointSize());
    setFormat(afterFormat, endText - afterFormat, maskedSyntax);

    _ranges[currentBlock().blockNumber()].append(
        InlineRange(beginningText, formatBegin, RangeType::Link));
    _ranges[currentBlock().blockNumber()].append(
        InlineRange(afterFormat, endText, RangeType::Link));
}

/**
 * @brief This function highlights images and links in Markdown text.
 *
 * @param text The input Markdown text.
 * @param startIndex The starting index from where to begin processing.
 * @return The index where processing should continue.
 */
int MarkdownHighlighter::highlightLinkOrImage(const QString &text,
                                              int startIndex) {
    // If the first 4 characters are spaces (for 4-spaces fence code),
    // but not list markers, return
    if (text.left(4).trimmed().isEmpty()) {
        // Check for unordered list markers
        auto leftChars = text.trimmed().left(2);

        if (leftChars != QLatin1String("- ") &&
            leftChars != QLatin1String("+ ") &&
            leftChars != QLatin1String("* ")) {
            // Check for a few ordered list markers
            leftChars = text.trimmed().left(3);

            if (leftChars != QLatin1String("1) ") &&
                leftChars != QLatin1String("2) ") &&
                leftChars != QLatin1String("3) ") &&
                leftChars != QLatin1String("4) ") &&
                leftChars != QLatin1String("5) ") &&
                leftChars != QLatin1String("6) ") &&
                leftChars != QLatin1String("7) ") &&
                leftChars != QLatin1String("8) ") &&
                leftChars != QLatin1String("9) ") &&
                leftChars != QLatin1String("1. ") &&
                leftChars != QLatin1String("2. ") &&
                leftChars != QLatin1String("3. ") &&
                leftChars != QLatin1String("4. ") &&
                leftChars != QLatin1String("5. ") &&
                leftChars != QLatin1String("6. ") &&
                leftChars != QLatin1String("7. ") &&
                leftChars != QLatin1String("8. ") &&
                leftChars != QLatin1String("9. ")) {
                // Check if text starts with a "\d+. ", "\d+) "
                const static QStringList patterns = {"\\d+\\. ", "\\d+\\) "};

                // Construct the regular expression pattern
                const static QString patternString =
                    "^(" + patterns.join("|") + ")";
                const static QRegularExpression pattern(patternString);

                // Check if the text starts with any of the specified patterns
                QRegularExpressionMatch match = pattern.match(text.trimmed());

                if (!match.hasMatch()) {
                    return startIndex;
                }
            }
        }
    }

    // Get the character at the starting index
    QChar startChar = text.at(startIndex);

    // If it starts with '<', it indicates a link, or an email
    // enclosed in angle brackets
    if (startChar == QLatin1Char('<')) {
        // Find the closing '>' character to identify the end of the link
        int closingChar = text.indexOf(QLatin1Char('>'), startIndex);
        if (closingChar == -1) return startIndex;

        // Extract the content between '<' and '>'
        QString linkContent =
            text.mid(startIndex + 1, closingChar - startIndex - 1);

        // Check if it's a valid link or email
        if (!isLink(linkContent) && !isValidEmail(linkContent) &&
            !linkContent.contains(QLatin1Char('.')))
            return startIndex;

        // Apply formatting to highlight the link
        formatAndMaskRemaining(startIndex + 1, closingChar - startIndex - 1,
                               startIndex, closingChar + 1, _formats[Link]);

        return closingChar;
    }
    // Highlight http and www links
    else if (startChar != QLatin1Char('[')) {
        int space = text.indexOf(QLatin1Char(' '), startIndex);
        if (space == -1) space = text.length();

        // Allow to highlight the href in HTML tags
        if (MH_SUBSTR(startIndex, 6) == QLatin1String("href=\"")) {
            int hrefEnd = text.indexOf(QLatin1Char('"'), startIndex + 6);
            if (hrefEnd == -1) return space;

            _ranges[currentBlock().blockNumber()].append(
                InlineRange(startIndex + 6, hrefEnd, RangeType::Link));
            setFormat(startIndex + 6, hrefEnd - startIndex - 6, _formats[Link]);
            return hrefEnd;
        }

        QString link = text.mid(startIndex, space - startIndex - 1);
        if (!isLink(link)) return startIndex;

        auto linkLength = link.length();

        _ranges[currentBlock().blockNumber()].append(
            InlineRange(startIndex, startIndex + linkLength, RangeType::Link));
        setFormat(startIndex, linkLength + 1, _formats[Link]);
        return space;
    }

    // Find the index of the closing ']' character to identify the end of link
    // or image text.
    int endIndex = text.indexOf(QLatin1Char(']'), startIndex);

    // If endIndex is not found or at the end of the text, the link is invalid
    if (endIndex == -1 || endIndex == text.size() - 1) return startIndex;

    // If there is an '!' preceding the starting character, it's an image
    if (startIndex != 0 && text.at(startIndex - 1) == QLatin1Char('!')) {
        // Find the index of the closing ')' character after the image link
        int closingIndex = text.indexOf(QLatin1Char(')'), endIndex);
        if (closingIndex == -1) return startIndex;
        ++closingIndex;

        // Apply formatting to highlight the image.
        formatAndMaskRemaining(startIndex + 1, endIndex - startIndex - 1,
                               startIndex - 1, closingIndex, _formats[Image]);
        return closingIndex;
    }
    // If the character after the closing ']' is '(', it's a regular link
    else if (text.at(endIndex + 1) == QLatin1Char('(')) {
        // Find the index of the closing ')' character after the link
        int closingParenIndex = text.indexOf(QLatin1Char(')'), endIndex);
        if (closingParenIndex == -1) return startIndex;
        ++closingParenIndex;

        // If the substring starting from the current index matches "[![",
        // It's a image with link
        if (MH_SUBSTR(startIndex, 3) == QLatin1String("[![")) {
            // Apply formatting to highlight the image alt text (inside the
            // first ']')
            int altEndIndex = text.indexOf(QLatin1Char(']'), endIndex + 1);
            if (altEndIndex == -1) return startIndex;

            // Find the last `)` (href) [![Image alt](Image src)](Link href)
            int hrefIndex = text.indexOf(QLatin1Char(')'), altEndIndex);
            if (hrefIndex == -1) return startIndex;
            ++hrefIndex;

            formatAndMaskRemaining(startIndex + 3, endIndex - startIndex - 3,
                                   startIndex, hrefIndex, _formats[Link]);

            return hrefIndex;
        }

        // Apply formatting to highlight the link
        formatAndMaskRemaining(startIndex + 1, endIndex - startIndex - 1,
                               startIndex, closingParenIndex, _formats[Link]);
        return closingParenIndex;
    }
    // Reference links
    else if (text.at(endIndex + 1) == QLatin1Char('[')) {
        // Image with reference
        int origIndex = startIndex;
        if (text.at(startIndex + 1) == QLatin1Char('!')) {
            startIndex = text.indexOf(QLatin1Char('['), startIndex + 1);
            if (startIndex == -1) return origIndex;
        }

        int closingChar = text.indexOf(QLatin1Char(']'), endIndex + 1);
        if (closingChar == -1) return startIndex;
        ++closingChar;

        formatAndMaskRemaining(startIndex + 1, endIndex - startIndex - 1,
                               origIndex, closingChar, _formats[Link]);
        return closingChar;
    }
    // If the character after the closing ']' is ':', it's a reference link
    // reference
    else if (text.at(endIndex + 1) == QLatin1Char(':')) {
        formatAndMaskRemaining(0, 0, startIndex, endIndex + 1, {});
        return endIndex + 1;
    }

    return startIndex;    // If none of the conditions are met, continue
                          // processing from the same index
}

/** @brief highlight inline code spans -> `code` and highlight strikethroughs
 *
 * ---- TESTS ----
`foo`
-> <code>foo</code>
`` foo ` bar ``
-> <code>foo ` bar</code>
` `` `
-> <code>``</code>
`foo\`bar`
-><code>foo\</code>bar`
``foo`bar``
-><code>foo`bar</code>
` foo `` bar `
<code>foo `` bar</code>
*/
int MarkdownHighlighter::highlightInlineSpans(const QString &text,
                                              int currentPos, const QChar c) {
    // clear code span ranges for this block

    int i = currentPos;
    // found a backtick
    int len = 0;
    int pos = i;

    if (i != 0 && text.at(i - 1) == QChar('\\')) return currentPos;

    // keep moving forward in backtick sequence;
    while (pos < text.length() && text.at(pos) == c) {
        ++len;
        ++pos;
    }

    const QString seq = text.mid(i, len);
    const int start = i;
    i += len;
    const int next = text.indexOf(seq, i);
    if (next == -1) {
        return currentPos;
    }
    if (next + len < text.length() && text.at(next + len) == c)
        return currentPos;

    // get existing format if any
    // we want to append to the existing format, not overwrite it
    QTextCharFormat fmt = QSyntaxHighlighter::format(start + 1);
    QTextCharFormat inlineFmt;

    // select appropriate format for current text
    if (c != QLatin1Char('~')) inlineFmt = _formats[InlineCodeBlock];

    // make sure we don't change font size / existing formatting
    if (fmt.fontPointSize() > 0)
        inlineFmt.setFontPointSize(fmt.fontPointSize());

    if (c == QLatin1Char('~')) {
        inlineFmt.setFontStrikeOut(true);
        // we don't want these properties for "inline code span"
        inlineFmt.setFontItalic(fmt.fontItalic());
        inlineFmt.setFontWeight(fmt.fontWeight());
        inlineFmt.setFontUnderline(fmt.fontUnderline());
        inlineFmt.setUnderlineStyle(fmt.underlineStyle());
    }

    if (c == QLatin1Char('`')) {
        _ranges[currentBlock().blockNumber()].append(
            InlineRange(start, next, RangeType::CodeSpan));
    }

    // format the text
    setFormat(start + len, next - (start + len), inlineFmt);

    // format backticks as masked
    setFormat(start, len, _formats[MaskedSyntax]);
    setFormat(next, len, _formats[MaskedSyntax]);

    i = next + len;
    return i;
}

/**
 * @brief highlight inline comments in markdown <!-- comment -->
 * @param text
 * @param pos
 * @return position after the comment
 */
int MarkdownHighlighter::highlightInlineComment(const QString &text, int pos) {
    const int start = pos;
    pos += 4;

    if (pos >= text.length()) return pos;

    int commentEnd = text.indexOf(QLatin1String("-->"), pos);
    if (commentEnd == -1) return pos;

    commentEnd += 3;
    setFormat(start, commentEnd - start, _formats[Comment]);
    return commentEnd - 1;
}

/****************************************
 * EM and Strong Parsing + Highlighting *
 ****************************************/

struct Delimiter {
    int pos;
    int len;
    int end;
    int jump;
    bool open;
    bool close;
    char marker;
};

inline bool isMDAsciiPunct(const int ch) noexcept {
    return (ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64) ||
           (ch >= 91 && ch <= 96) || (ch >= 123 && ch <= 126);
}

/**
 * @brief scans a chain of '*' or '_'
 * @param text: current text block
 * @param start: current position in the text
 * @param canSplitWord: is Underscore
 * @return length, canOpen, canClose
 * @details Helper function for Em and strong highlighting
 */
QPair<int, QPair<bool, bool>> scanDelims(const QString &text, const int start,
                                         const bool canSplitWord) {
    int pos = start;
    const int textLen = text.length();
    const QChar marker = text.at(start);
    bool leftFlanking = true;
    bool rightFlanking = true;

    const QChar lastChar = start > 0 ? text.at(start - 1) : QChar('\0');

    while (pos < textLen && text.at(pos) == marker) ++pos;
    const int length = pos - start;

    const QChar nextChar = pos + 1 < textLen ? text.at(pos) : QChar('\0');

    const bool isLastPunct =
        isMDAsciiPunct(lastChar.toLatin1()) || lastChar.isPunct();
    const bool isNextPunct =
        isMDAsciiPunct(nextChar.toLatin1()) || nextChar.isPunct();
    // treat line end and start as whitespace
    const bool isLastWhiteSpace = lastChar.isNull() ? true : lastChar.isSpace();
    const bool isNextWhiteSpace = nextChar.isNull() ? true : nextChar.isSpace();

    if (isNextWhiteSpace) {
        leftFlanking = false;
    } else if (isNextPunct) {
        if (!(isLastWhiteSpace || isLastPunct)) leftFlanking = false;
    }

    if (isLastWhiteSpace) {
        rightFlanking = false;
    } else if (isLastPunct) {
        if (!(isNextWhiteSpace || isNextPunct)) rightFlanking = false;
    }

    //    qDebug () << isNextWhiteSpace << marker;
    //    qDebug () << text << leftFlanking << rightFlanking << lastChar <<
    //    nextChar;

    const bool canOpen = canSplitWord
                             ? leftFlanking
                             : leftFlanking && (!rightFlanking || isLastPunct);
    const bool canClose = canSplitWord
                              ? rightFlanking
                              : rightFlanking && (!leftFlanking || isNextPunct);

    return QPair<int, QPair<bool, bool>>{length, {canOpen, canClose}};
}

int collectEmDelims(const QString &text, int curPos,
                    QVector<Delimiter> &delims) {
    const char marker = text.at(curPos).toLatin1();
    const auto result = scanDelims(text, curPos, marker == '*');
    const int length = result.first;
    const bool canOpen = result.second.first;
    const bool canClose = result.second.second;
    for (int i = 0; i < length; ++i) {
        const Delimiter d = {curPos + i, length,   -1,    i,
                             canOpen,    canClose, marker};
        delims.append(d);
    }
    return curPos + length;
}

void balancePairs(QVector<Delimiter> &delims) {
    for (int i = 0; i < delims.length(); ++i) {
        const auto &lastDelim = delims.at(i);

        if (!lastDelim.close) continue;

        int j = i - lastDelim.jump - 1;

        while (j >= 0) {
            const auto &curDelim = delims.at(j);
            if (curDelim.open && curDelim.marker == lastDelim.marker &&
                curDelim.end < 0) {
                const bool oddMatch = (curDelim.close || lastDelim.open) &&
                                      curDelim.len != -1 &&
                                      lastDelim.len != -1 &&
                                      (curDelim.len + lastDelim.len) % 3 == 0;
                if (!oddMatch) {
                    delims[i].jump = i - j;
                    delims[i].open = false;
                    delims[j].end = i;
                    delims[j].jump = 0;
                    break;
                }
            }
            j -= curDelim.jump + 1;
        }
    }
}

QPair<int, int> MarkdownHighlighter::findPositionInRanges(
    MarkdownHighlighter::RangeType type, int blockNum, int pos) const {
    const QVector<InlineRange> rangeList = _ranges.value(blockNum);
    auto it = std::find_if(
        rangeList.cbegin(), rangeList.cend(),
        [pos, type](const InlineRange &range) {
            if ((pos == range.begin || pos == range.end) && range.type == type)
                return true;
            return false;
        });
    if (it == rangeList.cend()) return {-1, -1};
    return {it->begin, it->end};
}

bool MarkdownHighlighter::isPosInACodeSpan(int blockNumber,
                                           int position) const {
    const QVector<InlineRange> rangeList = _ranges.value(blockNumber);
    return std::find_if(rangeList.cbegin(), rangeList.cend(),
                        [position](const InlineRange &range) {
                            if (position > range.begin &&
                                position < range.end &&
                                range.type == RangeType::CodeSpan)
                                return true;
                            return false;
                        }) != rangeList.cend();
}

bool MarkdownHighlighter::isPosInALink(int blockNumber, int position) const {
    const QVector<InlineRange> rangeList = _ranges.value(blockNumber);
    return std::find_if(rangeList.cbegin(), rangeList.cend(),
                        [position](const InlineRange &range) {
                            return position > range.begin &&
                                   position < range.end &&
                                   range.type == RangeType::Link;
                        }) != rangeList.cend();
}

QPair<int, int> MarkdownHighlighter::getSpanRange(
    MarkdownHighlighter::RangeType rangeType, int blockNumber,
    int position) const {
    const QVector<InlineRange> rangeList = _ranges.value(blockNumber);
    const auto it =
        std::find_if(rangeList.cbegin(), rangeList.cend(),
                     [position, rangeType](const InlineRange &range) {
                         if (position > range.begin && position < range.end &&
                             range.type == rangeType)
                             return true;
                         return false;
                     });

    if (it == rangeList.cend()) {
        return QPair<int, int>(-1, -1);
    } else {
        return QPair<int, int>(it->begin, it->end);
    }
}

/**
 * @brief highlights Em/Strong in text editor
 */
void MarkdownHighlighter::highlightEmAndStrong(const QString &text,
                                               const int pos) {
    // 1. collect all em/strong delimiters
    QVector<Delimiter> delims;
    for (int i = pos; i < text.length(); ++i) {
        if (text.at(i) != QLatin1Char('_') && text.at(i) != QLatin1Char('*'))
            continue;

        bool isInCodeSpan = isPosInACodeSpan(currentBlock().blockNumber(), i);
        if (isInCodeSpan) continue;

        i = collectEmDelims(text, i, delims);
        --i;
    }

    // 2. Balance pairs
    balancePairs(delims);

    // start,length -> helper for applying masking later
    QVector<QPair<int, int>> masked;
    masked.reserve(delims.size() / 2);

    // 3. final processing & highlighting
    for (int i = delims.length() - 1; i >= 0; --i) {
        const auto &startDelim = delims.at(i);
        if (startDelim.marker != QLatin1Char('_') &&
            startDelim.marker != QLatin1Char('*'))
            continue;
        if (startDelim.end == -1) continue;

        const auto &endDelim = delims.at(startDelim.end);
        auto state = static_cast<HighlighterState>(currentBlockState());

        const bool isStrong =
            i > 0 && delims.at(i - 1).end == startDelim.end + 1 &&
            delims.at(i - 1).pos == startDelim.pos - 1 &&
            delims.at(startDelim.end + 1).pos == endDelim.pos + 1 &&
            delims.at(i - 1).marker == startDelim.marker;
        if (isStrong) {
            //            qDebug () << "St: " << startDelim.pos << endDelim.pos;
            //            qDebug () << "St Txt: "<< text.mid(startDelim.pos,
            //            endDelim.pos - startDelim.pos);
            int k = startDelim.pos;
            while (text.at(k) == startDelim.marker)
                ++k;    // look for first letter after the delim chain
            // per character highlighting
            const int boldLen = endDelim.pos - startDelim.pos;
            const bool underline = _highlightingOptions.testFlag(Underline) &&
                                   startDelim.marker == QLatin1Char('_');
            while (k != (startDelim.pos + boldLen)) {
                QTextCharFormat fmt = QSyntaxHighlighter::format(k);
#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0)
                fmt.setFontFamily(_formats[Bold].fontFamily());
#else
                const QStringList fontFamilies =
                    _formats[Bold].fontFamilies().toStringList();
                if (!fontFamilies.isEmpty()) fmt.setFontFamilies(fontFamilies);
#endif

                if (_formats[state].fontPointSize() > 0)
                    fmt.setFontPointSize(_formats[state].fontPointSize());

                // if we are in plain text, use the format's specified color
                if (fmt.foreground() == QTextCharFormat().foreground())
                    fmt.setForeground(_formats[Bold].foreground());
                if (underline) {
                    fmt.setForeground(_formats[StUnderline].foreground());
                    fmt.setFont(_formats[StUnderline].font());
                    fmt.setFontUnderline(_formats[StUnderline].fontUnderline());
                } else if (_formats[Bold].font().bold())
                    fmt.setFontWeight(QFont::Bold);
                setFormat(k, 1, fmt);
                ++k;
            }
            masked.append({startDelim.pos - 1, 2});
            masked.append({endDelim.pos, 2});

            int block = currentBlock().blockNumber();
            _ranges[block].append(InlineRange(startDelim.pos, endDelim.pos + 1,
                                              RangeType::Emphasis));
            _ranges[block].append(InlineRange(startDelim.pos - 1, endDelim.pos,
                                              RangeType::Emphasis));
            --i;
        } else {
            //            qDebug () << "Em: " << startDelim.pos << endDelim.pos;
            //            qDebug () << "Em Txt: " << text.mid(startDelim.pos,
            //            endDelim.pos - startDelim.pos);
            int k = startDelim.pos;
            while (text.at(k) == startDelim.marker) ++k;
            const bool underline = _highlightingOptions.testFlag(Underline) &&
                                   startDelim.marker == QLatin1Char('_');
            const int itLen = endDelim.pos - startDelim.pos;
            while (k != (startDelim.pos + itLen)) {
                QTextCharFormat fmt = QSyntaxHighlighter::format(k);

#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0)
                fmt.setFontFamily(_formats[Italic].fontFamily());
#else
                const QStringList fontFamilies =
                    _formats[Italic].fontFamilies().toStringList();
                if (!fontFamilies.isEmpty()) fmt.setFontFamilies(fontFamilies);
#endif

                if (_formats[state].fontPointSize() > 0)
                    fmt.setFontPointSize(_formats[state].fontPointSize());

                if (fmt.foreground() == QTextCharFormat().foreground())
                    fmt.setForeground(_formats[Italic].foreground());

                if (underline)
                    fmt.setFontUnderline(_formats[StUnderline].fontUnderline());
                else
                    fmt.setFontItalic(_formats[Italic].fontItalic());
                setFormat(k, 1, fmt);
                ++k;
            }
            masked.append({startDelim.pos, 1});
            masked.append({endDelim.pos, 1});

            int block = currentBlock().blockNumber();
            _ranges[block].append(
                InlineRange(startDelim.pos, endDelim.pos, RangeType::Emphasis));
        }
    }

    // 4. Apply masked syntax
    for (int i = 0; i < masked.length(); ++i) {
        QTextCharFormat maskedFmt = _formats[MaskedSyntax];
        auto state = static_cast<HighlighterState>(currentBlockState());
        if (_formats[state].fontPointSize() > 0)
            maskedFmt.setFontPointSize(_formats[state].fontPointSize());
        setFormat(masked.at(i).first, masked.at(i).second, maskedFmt);
    }
}

void MarkdownHighlighter::setHighlightingOptions(
    const HighlightingOptions options) {
    _highlightingOptions = options;
}

#undef MH_SUBSTR
