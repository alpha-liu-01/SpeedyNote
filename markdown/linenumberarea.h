#ifndef LINENUMBERAREA_H
#define LINENUMBERAREA_H

#include <QDebug>
#include <QHash>
#include <QPainter>
#include <QScrollBar>
#include <QWidget>

#include "qmarkdowntextedit.h"

class LineNumArea final : public QWidget {
    Q_OBJECT

   public:
    explicit LineNumArea(QMarkdownTextEdit *parent)
        : QWidget(parent), textEdit(parent) {
        Q_ASSERT(parent);

        _currentLineColor = QColor(QStringLiteral("#eef067"));
        _otherLinesColor = QColor(QStringLiteral("#a6a6a6"));
        setHidden(true);

        // We always use fixed font to avoid "width" issues
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }

    void setCurrentLineColor(QColor color) { _currentLineColor = color; }

    void setOtherLineColor(QColor color) {
        _otherLinesColor = std::move(color);
    }

    void setBookmarkLines(const QHash<int, int> &bookmarks) {
        _bookmarkLines = bookmarks;
        update();
    }

    int lineNumAreaWidth() const {
        int space = 0;

        if (enabled) {
            int digits = 2;
            int max = std::max(1, textEdit->blockCount());
            while (max >= 10) {
                max /= 10;
                ++digits;
            }

#if QT_VERSION >= 0x050B00
            space =
                13 + textEdit->fontMetrics().horizontalAdvance(u'9') * digits;
#else
            space =
                13 + textEdit->fontMetrics().width(QLatin1Char('9')) * digits;
#endif

            // Add extra space for bookmark markers (if any bookmarks exist)
            if (!_bookmarkLines.isEmpty()) {
#if QT_VERSION >= 0x050B00
                space += 5 + textEdit->fontMetrics().horizontalAdvance(u'9');
#else
                space += 5 + textEdit->fontMetrics().width(QLatin1Char('9'));
#endif
            }
        }

        return space + textEdit->sidebarAdditionalWidth();
    }

    bool isLineNumAreaEnabled() const { return enabled; }

    void setLineNumAreaEnabled(bool e) {
        enabled = e;
        setHidden(lineNumAreaWidth() <= 0);
    }

    QSize sizeHint() const override { return {lineNumAreaWidth(), 0}; }

   protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);

        painter.fillRect(event->rect(),
                         palette().color(QPalette::Active, QPalette::Window));

        auto block = textEdit->firstVisibleBlock();
        int blockNumber = block.blockNumber();
        qreal top = textEdit->blockBoundingGeometry(block)
                        .translated(textEdit->contentOffset())
                        .top();
        // Maybe the top is not 0?
        top += textEdit->viewportMargins().top();
        qreal bottom = top;

        const QPen currentLine = _currentLineColor;
        const QPen otherLines = _otherLinesColor;
        painter.setFont(font());

        // Calculate widths for layout
        const int additionalWidth = textEdit->sidebarAdditionalWidth();
        int lineNumberWidth = sizeHint().width() - additionalWidth;
        int bookmarkWidth = 0;
        if (enabled && !_bookmarkLines.isEmpty()) {
#if QT_VERSION >= 0x050B00
            bookmarkWidth = textEdit->fontMetrics().horizontalAdvance(u'9') + 5;
#else
            bookmarkWidth = textEdit->fontMetrics().width(QLatin1Char('9')) + 5;
#endif
        }

        while (block.isValid() && top <= event->rect().bottom()) {
            top = bottom;
            bottom = top + textEdit->blockBoundingRect(block).height();
            if (block.isVisible() && bottom >= event->rect().top()) {
                QString number = QString::number(blockNumber + 1);

                auto isCurrentLine =
                    textEdit->textCursor().blockNumber() == blockNumber;
                painter.setPen(isCurrentLine ? currentLine : otherLines);

                if (enabled) {
                    // Draw line number
                    int lineNumRightMargin =
                        bookmarkWidth > 0 ? bookmarkWidth : 0;
                    painter.drawText(additionalWidth - 5, top,
                                     lineNumberWidth - lineNumRightMargin,
                                     textEdit->fontMetrics().height(),
                                     Qt::AlignRight, number);

                    // Draw bookmark marker if this line has a bookmark
                    if (bookmarkWidth > 0) {
                        // Check if current line (blockNumber + 1) has a
                        // bookmark
                        int currentLineNumber = blockNumber + 1;
                        for (auto it = _bookmarkLines.constBegin();
                             it != _bookmarkLines.constEnd(); ++it) {
                            if (it.value() == currentLineNumber) {
                                // Draw the bookmark slot number (1-9)
                                QString bookmarkSlot =
                                    QString::number(it.key());
                                painter.setPen(QColor(QStringLiteral(
                                    "#ff6b6b")));    // Red color for bookmark
                                painter.drawText(
                                    additionalWidth + lineNumberWidth -
                                        bookmarkWidth,
                                    top, bookmarkWidth - 5,
                                    textEdit->fontMetrics().height(),
                                    Qt::AlignRight, bookmarkSlot);
                                break;
                            }
                        }
                    }
                }
            }

            block = block.next();
            ++blockNumber;
        }

        textEdit->paintSidebar(&painter, event->rect());
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (textEdit->sidebarMousePressEvent(event)) {
            return;
        }

        QWidget::mousePressEvent(event);
    }

   private:
    bool enabled = false;
    QMarkdownTextEdit *textEdit;
    QColor _currentLineColor;
    QColor _otherLinesColor;
    QHash<int, int>
        _bookmarkLines;    // Maps bookmark slot (0-9) to line number
};

#endif    // LINENUMBERAREA_H
