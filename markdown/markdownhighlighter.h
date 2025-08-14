/*
 * MIT License
 *
 * Copyright (c) 2014-2025 Patrizio Bekerle -- <patrizio@bekerle.com>
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

#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#ifdef QT_QUICK_LIB
#include <QQuickTextDocument>
#endif

QT_BEGIN_NAMESPACE
class QTextDocument;

QT_END_NAMESPACE

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

#ifdef QT_QUICK_LIB
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE
                   setTextDocument NOTIFY textDocumentChanged)

    QQuickTextDocument *m_quickDocument = nullptr;

   signals:
    void textDocumentChanged();

   public:
    inline QQuickTextDocument *textDocument() const { return m_quickDocument; };
    void setTextDocument(QQuickTextDocument *textDocument) {
        if (!textDocument) return;
        m_quickDocument = textDocument;
        setDocument(m_quickDocument->textDocument());
        Q_EMIT textDocumentChanged();
    };
#endif

   public:
    enum HighlightingOption {
        None = 0,
        FullyHighlightedBlockQuote = 0x01,
        Underline = 0x02
    };
    Q_DECLARE_FLAGS(HighlightingOptions, HighlightingOption)

    MarkdownHighlighter(
        QTextDocument *parent = nullptr,
        HighlightingOptions highlightingOptions = HighlightingOption::None);

    static inline QColor codeBlockBackgroundColor() {
        const QBrush brush = _formats[CodeBlock].background();

        if (!brush.isOpaque()) {
            return QColor(Qt::transparent);
        }

        return brush.color();
    }

    static constexpr inline bool isOctal(const char c) {
        return (c >= '0' && c <= '7');
    }
    static constexpr inline bool isHex(const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    }
    static constexpr inline bool isCodeBlock(const int state) {
        return state == MarkdownHighlighter::CodeBlock ||
               state == MarkdownHighlighter::CodeBlockTilde ||
               state == MarkdownHighlighter::CodeBlockComment ||
               state == MarkdownHighlighter::CodeBlockTildeComment ||
               state >= MarkdownHighlighter::CodeCpp;
    }
    static constexpr inline bool isCodeBlockEnd(const int state) {
        return state == MarkdownHighlighter::CodeBlockEnd ||
               state == MarkdownHighlighter::CodeBlockTildeEnd;
    }
    static constexpr inline bool isHeading(const int state) {
        return state >= H1 && state <= H6;
    }

    enum class RangeType { CodeSpan, Emphasis, Link };

    QPair<int, int> findPositionInRanges(MarkdownHighlighter::RangeType type,
                                         int blockNum, int pos) const;
    bool isPosInACodeSpan(int blockNumber, int position) const;
    bool isPosInALink(int blockNumber, int position) const;
    QPair<int, int> getSpanRange(RangeType rangeType, int blockNumber,
                                 int position) const;

    // we used some predefined numbers here to be compatible with
    // the peg-Markdown parser
    enum HighlighterState {
        NoState = -1,
        Link = 0,
        Image = 3,
        CodeBlock,
        CodeBlockComment,
        Italic = 7,
        Bold,
        List,
        Comment = 11,
        H1,
        H2,
        H3,
        H4,
        H5,
        H6,
        BlockQuote,
        HorizontalRuler = 21,
        Table,
        InlineCodeBlock,
        MaskedSyntax,
        CurrentLineBackgroundColor,
        BrokenLink,
        FrontmatterBlock,
        TrailingSpace,
        CheckBoxUnChecked,
        CheckBoxChecked,
        StUnderline,

        // code highlighting
        CodeKeyWord = 1000,
        CodeString = 1001,
        CodeComment = 1002,
        CodeType = 1003,
        CodeOther = 1004,
        CodeNumLiteral = 1005,
        CodeBuiltIn = 1006,

        // internal
        CodeBlockIndented = 96,
        CodeBlockTildeEnd = 97,
        CodeBlockTilde = 98,
        CodeBlockTildeComment,
        CodeBlockEnd = 100,
        HeadlineEnd,
        FrontmatterBlockEnd,

        // languages
        /*********
         * When adding a language make sure that its value is a multiple of 2
         * This is because we use the next number as comment for that language
         * In case the language doesn't support multiline comments in the
         * traditional C++ sense, leave the next value empty. Otherwise mark the
         * next value as comment for that language. e.g CodeCpp = 200
         * CodeCppComment = 201
         */
        CodeCpp = 200,
        CodeCppComment = 201,
        CodeJs = 202,
        CodeJsComment = 203,
        CodeC = 204,
        CodeCComment = 205,
        CodeBash = 206,
        CodePHP = 208,
        CodePHPComment = 209,
        CodeQML = 210,
        CodeQMLComment = 211,
        CodePython = 212,
        CodeRust = 214,
        CodeRustComment = 215,
        CodeJava = 216,
        CodeJavaComment = 217,
        CodeCSharp = 218,
        CodeCSharpComment = 219,
        CodeGo = 220,
        CodeGoComment = 221,
        CodeV = 222,
        CodeVComment = 223,
        CodeSQL = 224,
        CodeSQLComment = 225,
        CodeJSON = 226,
        CodeXML = 228,
        CodeCSS = 230,
        CodeCSSComment = 231,
        CodeTypeScript = 232,
        CodeTypeScriptComment = 233,
        CodeYAML = 234,
        CodeINI = 236,
        CodeTaggerScript = 238,
        CodeVex = 240,
        CodeVexComment = 241,
        CodeCMake = 242,
        CodeMake = 244,
        CodeNix = 246,
        CodeForth = 248,
        CodeForthComment = 249,
        CodeSystemVerilog = 250,
        CodeSystemVerilogComment = 251,
        CodeGDScript = 252,
        CodeTOML = 254,
        CodeTOMLString = 255
    };
    Q_ENUM(HighlighterState)

    static void setTextFormats(
        QHash<HighlighterState, QTextCharFormat> formats);
    static void setTextFormat(HighlighterState state, QTextCharFormat format);
    void clearDirtyBlocks();
    void setHighlightingOptions(const HighlightingOptions options);
    void initHighlightingRules();

   Q_SIGNALS:
    void highlightingFinished();

   protected Q_SLOTS:
    void timerTick();

   protected:
    struct HighlightingRule {
        explicit HighlightingRule(const HighlighterState state_)
            : state(state_) {}
        HighlightingRule() = default;

        QRegularExpression pattern;
        QString shouldContain;
        HighlighterState state = NoState;
        uint8_t capturingGroup = 0;
        uint8_t maskedGroup = 0;
    };
    struct InlineRange {
        int begin;
        int end;
        RangeType type;
        InlineRange() = default;
        InlineRange(int begin_, int end_, RangeType type_)
            : begin{begin_}, end{end_}, type{type_} {}
    };

    void highlightBlock(const QString &text) override;

    static void initTextFormats(int defaultFontSize = 12);

    static void initCodeLangs();

    void highlightMarkdown(const QString &text);

    void formatAndMaskRemaining(int formatBegin, int formatLength,
                                int beginningText, int endText,
                                const QTextCharFormat &format);

    /******************************
     *  BLOCK LEVEL FUNCTIONS
     ******************************/

    void highlightHeadline(const QString &text);

    void highlightSubHeadline(const QString &text, HighlighterState state);

    void highlightAdditionalRules(const QVector<HighlightingRule> &rules,
                                  const QString &text);

    void highlightFrontmatterBlock(const QString &text);

    void highlightCommentBlock(const QString &text);

    void highlightThematicBreak(const QString &text);

    void highlightLists(const QString &text);

    void highlightCheckbox(const QString &text, int curPos);

    /******************************
     *  INLINE FUNCTIONS
     ******************************/

    void highlightInlineRules(const QString &text);

    int highlightInlineSpans(const QString &text, int currentPos,
                             const QChar c);

    void highlightEmAndStrong(const QString &text, const int pos);

    Q_REQUIRED_RESULT int highlightInlineComment(const QString &text, int pos);

    int highlightLinkOrImage(const QString &text, int startIndex);

    void setHeadingStyles(MarkdownHighlighter::HighlighterState rule,
                          const QRegularExpressionMatch &match,
                          const int capturedGroup);

    /******************************
     *  CODE HIGHLIGHTING FUNCTIONS
     ******************************/

    void highlightIndentedCodeBlock(const QString &text);

    void highlightCodeFence(const QString &text);

    void highlightCodeBlock(const QString &text,
                            const QString &opener = QStringLiteral("```"));

    void highlightSyntax(const QString &text);

    Q_REQUIRED_RESULT int highlightNumericLiterals(const QString &text, int i);

    Q_REQUIRED_RESULT int highlightStringLiterals(QChar strType,
                                                  const QString &text, int i);

    void ymlHighlighter(const QString &text);

    void iniHighlighter(const QString &text);

    void cssHighlighter(const QString &text);

    void xmlHighlighter(const QString &text);

    void makeHighlighter(const QString &text);

    void forthHighlighter(const QString &text);
    void taggerScriptHighlighter(const QString &text);

    void gdscriptHighlighter(const QString &text);
    void sqlHighlighter(const QString &text);
    void tomlHighlighter(const QString &text);

    void addDirtyBlock(const QTextBlock &block);

    void reHighlightDirtyBlocks();

    bool _highlightingFinished;
    HighlightingOptions _highlightingOptions;
    QTimer *_timer;
    QVector<QTextBlock> _dirtyTextBlocks;
    QVector<QPair<int, int>> _linkRanges;

    QHash<int, QVector<InlineRange>> _ranges;

    static QVector<HighlightingRule> _highlightingRules;
    static QHash<HighlighterState, QTextCharFormat> _formats;
    static QHash<QString, HighlighterState> _langStringToEnum;
    static constexpr int tildeOffset = 300;
};
