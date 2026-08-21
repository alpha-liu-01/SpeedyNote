#pragma once

#include "InsertedObject.h"
#include "OcrTextObject.h"
#include "TextBoxObject.h"
#include "../core/Page.h"

#include <QDebug>
#include <QFontInfo>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFormat>
#include <QtNumeric>

namespace TextBoxObjectTests {

inline bool require(bool condition, const char* message)
{
    if (!condition)
        qCritical() << "FAILED:" << message;
    return condition;
}

inline void configureCurrentBox(
    TextBoxObject& box, const QString& text = QStringLiteral("Body"))
{
    box.text = text;
    box.fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
    box.textLayoutVersion = TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
    box.size = QSizeF(220.0, 40.0);
    box.reflowToWidth(220.0);
}

inline bool testPersistenceAndOcrIsolation()
{
    bool ok = true;

    TextBoxObject legacy;
    legacy.text = QStringLiteral("Legacy");
    legacy.size = QSizeF(200.0, 40.0);
    const QJsonObject legacyJson = legacy.toJson();
    ok &= require(!legacyJson.contains(QStringLiteral("textLayoutVersion")),
                  "legacy JSON unexpectedly has a layout version");

    TextBoxObject current;
    configureCurrentBox(current);
    const QJsonObject currentJson = current.toJson();
    ok &= require(currentJson.value(QStringLiteral("textLayoutVersion")).toInt()
                      == TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION,
                  "current layout version was not serialized");
    ok &= require(qFuzzyCompare(
                      currentJson.value(QStringLiteral("fontSize")).toDouble(),
                      TextBoxObject::DEFAULT_BASE_FONT_SIZE),
                  "base font size was not serialized");

    std::unique_ptr<InsertedObject> restored =
        InsertedObject::fromJson(currentJson);
    auto* restoredText = dynamic_cast<TextBoxObject*>(restored.get());
    ok &= require(restoredText && restoredText->usesCurrentLayout(),
                  "factory round-trip lost current text layout");
    ok &= require(restoredText
                      && qFuzzyCompare(restoredText->size.height(),
                                       current.size.height()),
                  "factory round-trip lost normalized height");

    auto pageText = std::make_unique<TextBoxObject>();
    configureCurrentBox(*pageText, QStringLiteral("Page round-trip"));
    const QString pageTextId = pageText->id;
    Page page(QSizeF(800.0, 1000.0));
    page.addObject(std::move(pageText));
    std::unique_ptr<Page> restoredPage = Page::fromJson(page.toJson());
    auto* restoredPageText = restoredPage
        ? dynamic_cast<TextBoxObject*>(
              restoredPage->objectById(pageTextId))
        : nullptr;
    ok &= require(restoredPageText && restoredPageText->usesCurrentLayout(),
                  "page round-trip lost optional text layout fields");

    TextBoxObject future;
    QJsonObject futureJson = currentJson;
    futureJson[QStringLiteral("textLayoutVersion")] = 99;
    future.loadFromJson(futureJson);
    ok &= require(future.textLayoutVersion == 99,
                  "unknown layout version was not preserved");
    ok &= require(!future.upgradeToCurrentLayout(),
                  "unknown layout version was rewritten");

    OcrTextObject ocr;
    ocr.text = QStringLiteral("OCR");
    ocr.size = QSizeF(100.0, 30.0);
    ok &= require(!ocr.upgradeToCurrentLayout(),
                  "OCR object adopted user text flow layout");
    ok &= require(!ocr.usesCurrentLayout(),
                  "OCR object reports current user text layout");
    return ok;
}

inline bool testLayoutMeasurementAndCache()
{
    bool ok = true;
    TextBoxObject box;
    configureCurrentBox(
        box, QStringLiteral("# Heading\n\nA wrapped body line that "
                            "needs more than one line at small widths."));

    const QSizeF wide = box.normalizedSizeForWidth(220.0);
    const QSizeF narrow = box.normalizedSizeForWidth(90.0);
    ok &= require(wide.height() > 2.0 * TextBoxObject::CONTENT_PADDING,
                  "versioned content height was not measured");
    ok &= require(narrow.height() > wide.height(),
                  "narrow text did not grow vertically");

    TextBoxObject oneLine;
    configureCurrentBox(oneLine, QStringLiteral("Short"));
    const qreal oneLineHeight = oneLine.size.height();
    TextBoxObject explicitBreak;
    configureCurrentBox(
        explicitBreak, QStringLiteral("Short\nNext"));
    const TextBoxLayoutResult* explicitBreakLayout =
        explicitBreak.ensureLayout();
    const QTextBlock firstBreakBlock = explicitBreakLayout
        && explicitBreakLayout->document
        ? explicitBreakLayout->document->begin()
        : QTextBlock();
    const bool renderedAsMultipleLines =
        explicitBreakLayout && explicitBreakLayout->document
        && (explicitBreakLayout->document->blockCount() > 1
            || (firstBreakBlock.isValid()
                && firstBreakBlock.layout()
                && firstBreakBlock.layout()->lineCount() > 1));
    ok &= require(explicitBreak.size.height() > oneLineHeight,
                  "explicit newline did not grow the text box");
    ok &= require(renderedAsMultipleLines,
                  "Markdown collapsed an explicit newline");

    const QSizeF beforeRender = box.size;
    QImage image(500, 500, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    box.render(painter, 1.0);
    painter.end();
    ok &= require(box.size == beforeRender,
                  "render mutated text box geometry");

    const TextBoxLayoutResult* first = box.ensureLayout();
    ok &= require(first && first->document,
                  "current layout did not build a document");
    const int oldPixelSize =
        first ? first->document->defaultFont().pixelSize() : -1;
    box.fontSize = 24.0; // Deliberately do not invalidate: key detection must.
    const TextBoxLayoutResult* second = box.ensureLayout();
    const int newPixelSize =
        second ? second->document->defaultFont().pixelSize() : -1;
    ok &= require(oldPixelSize != newPixelSize && newPixelSize == 24,
                  "base font size was missing from the layout cache key");

    TextBoxObject headings;
    configureCurrentBox(
        headings,
        QStringLiteral("# H1\n\n## H2\n\n### H3\n\n#### H4\n\n"
                       "##### H5\n\n###### H6\n\nBody"));
    const TextBoxLayoutResult* headingLayout = headings.ensureLayout();
    QVector<int> headingPixels;
    if (headingLayout && headingLayout->document) {
        for (QTextBlock block = headingLayout->document->begin();
             block.isValid(); block = block.next()) {
            const int level =
                block.blockFormat()
                    .property(QTextFormat::HeadingLevel).toInt();
            if (level >= 1 && level <= 6 && !block.begin().atEnd()) {
                headingPixels.append(
                    QFontInfo(block.begin().fragment().charFormat().font())
                        .pixelSize());
            }
        }
    }
    static constexpr qreal ratios[] = {1.6, 1.5, 1.4, 1.3, 1.2, 1.1};
    ok &= require(headingPixels.size() == 6,
                  "layout did not preserve all six heading levels");
    for (int level = 0; level < headingPixels.size() && level < 6; ++level) {
        const int expected = qRound(
            TextBoxObject::DEFAULT_BASE_FONT_SIZE * ratios[level]);
        ok &= require(headingPixels[level] == expected,
                      "heading did not use its versioned ratio");
    }

    // Headings must follow the box family too. QTextCharFormat::font() reports
    // the application font for fragments that never set one, so a naive
    // "only fill in an empty family" check leaves headings on the default.
    headings.fontFamily = QStringLiteral("Courier New");
    const TextBoxLayoutResult* familyLayout = headings.ensureLayout();
    bool headingFamiliesMatch = familyLayout && familyLayout->document;
    int inspectedHeadings = 0;
    if (familyLayout && familyLayout->document) {
        for (QTextBlock block = familyLayout->document->begin();
             block.isValid(); block = block.next()) {
            const int level =
                block.blockFormat()
                    .property(QTextFormat::HeadingLevel).toInt();
            if (level < 1 || level > 6 || block.begin().atEnd())
                continue;
            ++inspectedHeadings;
            if (block.begin().fragment().charFormat().font().family()
                    .compare(headings.fontFamily, Qt::CaseInsensitive) != 0) {
                headingFamiliesMatch = false;
            }
        }
    }
    ok &= require(headingFamiliesMatch && inspectedHeadings == 6,
                  "headings ignored the text box font family");
    return ok;
}

inline bool testLegacyUpgradeAndState()
{
    bool ok = true;

    TextBoxObject fixed;
    fixed.text = QStringLiteral("Fixed");
    fixed.fontSize = 22.0;
    fixed.size = QSizeF(180.0, 50.0);
    TextBoxState before;
    TextBoxState after;
    ok &= require(fixed.upgradeToCurrentLayout(&before, &after),
                  "positive-font legacy box did not upgrade");
    ok &= require(qFuzzyCompare(fixed.fontSize, 22.0),
                  "positive legacy font size was not preserved");
    ok &= require(before.textLayoutVersion == 0
                      && after.textLayoutVersion
                             == TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION,
                  "upgrade snapshots do not bracket conversion");
    ok &= require(!fixed.upgradeToCurrentLayout(),
                  "legacy upgrade was not idempotent");

    fixed.applyState(before);
    ok &= require(fixed.usesLegacyLayout() && fixed.size == before.size,
                  "applying pre-upgrade state did not restore legacy geometry");

    TextBoxObject automatic;
    automatic.text = QStringLiteral("Auto-sized legacy text");
    automatic.size = QSizeF(190.0, 40.0);
    ok &= require(automatic.upgradeToCurrentLayout(),
                  "auto-font legacy box did not upgrade");
    ok &= require(qIsFinite(automatic.fontSize) && automatic.fontSize > 0.0,
                  "auto-font conversion did not produce a valid base size");

    TextBoxObject empty;
    empty.size = QSizeF(200.0, 40.0);
    ok &= require(empty.upgradeToCurrentLayout()
                      && qFuzzyCompare(
                          empty.fontSize,
                          TextBoxObject::DEFAULT_BASE_FONT_SIZE),
                  "empty legacy box did not use the default base size");

    TextBoxObject invalid;
    invalid.text = QStringLiteral("Invalid");
    invalid.fontSize = qQNaN();
    invalid.size = QSizeF(0.0, 0.0);
    ok &= require(invalid.upgradeToCurrentLayout()
                      && qIsFinite(invalid.fontSize)
                      && invalid.fontSize > 0.0,
                  "invalid legacy metrics did not use a safe fallback");
    return ok;
}

inline bool testSearchAndLinkGeometry()
{
    bool ok = true;
    TextBoxObject box;
    configureCurrentBox(
        box, QStringLiteral("[Open](https://example.com) and Open"));
    const std::unique_ptr<TextBoxLayoutResult> workerLayout =
        TextBoxObject::buildLayout(box.layoutInput());
    ok &= require(workerLayout && workerLayout->document,
                  "worker-owned layout was not created");
    if (!workerLayout)
        return false;

    const QVector<QRectF> allOpen =
        workerLayout->findTextRects(QStringLiteral("Open"), false, false);
    const QVector<QRectF> wholeOpen =
        workerLayout->findTextRects(QStringLiteral("open"), false, true);
    ok &= require(allOpen.size() == 2 && wholeOpen.size() == 2,
                  "shared search geometry lost Markdown/plain matches");
    for (const QRectF& rect : allOpen) {
        ok &= require(rect.left() >= 0.0
                          && rect.right() <= box.size.width() + 1.0,
                      "search rectangle lies outside the text box");
    }

    if (!allOpen.isEmpty()) {
        const QString href =
            workerLayout->anchorAtObjectPoint(allOpen.first().center());
        ok &= require(href == QStringLiteral("https://example.com"),
                      "link hit testing disagrees with rendered layout");
    }

    TextBoxObject legacy;
    legacy.text = QStringLiteral("[Legacy](https://legacy.example)");
    legacy.size = QSizeF(180.0, 24.0);
    const std::unique_ptr<TextBoxLayoutResult> legacyLayout =
        TextBoxObject::buildLayout(legacy.layoutInput());
    const QVector<QRectF> legacyRects = legacyLayout
        ? legacyLayout->findTextRects(QStringLiteral("Legacy"), true, true)
        : QVector<QRectF>();
    ok &= require(!legacyRects.isEmpty(),
                  "legacy Markdown search geometry regressed");
    return ok;
}

inline bool testChromeAndSuppressedTextRendering()
{
    bool ok = true;

    TextBoxObject empty;
    configureCurrentBox(empty, QString());
    empty.position = QPointF(4.0, 4.0);
    empty.size = QSizeF(100.0, 32.0);
    empty.backgroundColor = QColor(220, 230, 240, 255);
    empty.showBorder = true;
    QImage emptyImage(120, 48, QImage::Format_ARGB32_Premultiplied);
    emptyImage.fill(Qt::transparent);
    {
        QPainter painter(&emptyImage);
        empty.render(painter, 1.0);
    }
    ok &= require(qAlpha(emptyImage.pixel(20, 20)) > 0,
                  "empty text box did not retain chrome");

    TextBoxObject source;
    configureCurrentBox(source, QStringLiteral("Visible text"));
    source.position = QPointF(0.0, 0.0);
    source.backgroundColor = Qt::transparent;
    source.showBorder = false;
    QImage normal(260, 80, QImage::Format_ARGB32_Premultiplied);
    QImage suppressed(260, 80, QImage::Format_ARGB32_Premultiplied);
    normal.fill(Qt::transparent);
    suppressed.fill(Qt::transparent);
    {
        QPainter painter(&normal);
        source.render(painter, 1.0);
    }
    {
        QPainter painter(&suppressed);
        source.renderWithTextSuppressed(painter, 1.0);
    }
    ok &= require(normal != suppressed,
                  "text-suppressed rendering still drew source text");
    bool suppressedIsTransparent = true;
    for (int y = 0; y < suppressed.height() && suppressedIsTransparent; ++y) {
        const QRgb* row =
            reinterpret_cast<const QRgb*>(suppressed.constScanLine(y));
        for (int x = 0; x < suppressed.width(); ++x) {
            if (qAlpha(row[x]) != 0) {
                suppressedIsTransparent = false;
                break;
            }
        }
    }
    ok &= require(suppressedIsTransparent,
                  "text-suppressed rendering changed transparent chrome");
    return ok;
}

inline bool testOcrBaseFontEstimation()
{
    bool ok = true;

    auto segment = [](qreal height) {
        OcrTextBlock::WordSegment seg;
        seg.text = QStringLiteral("word");
        seg.boundingRect = QRectF(0.0, 0.0, 40.0, height);
        return seg;
    };

    // Median segment height wins over outliers, scaled the way the OCR
    // renderer scales glyphs and rounded to a half point.
    OcrTextObject multi;
    multi.size = QSizeF(200.0, 200.0);
    multi.wordSegments = {segment(20.0), segment(24.0), segment(120.0)};
    ok &= require(qAbs(multi.estimateBaseFontSize() - 18.0) < 0.001,
                  "multi-segment OCR font estimate ignored the median");

    // A single line without segment geometry falls back to the block height.
    OcrTextObject singleLine;
    singleLine.size = QSizeF(180.0, 40.0);
    ok &= require(qAbs(singleLine.estimateBaseFontSize() - 30.0) < 0.001,
                  "single-line OCR font estimate ignored block height");

    // Extremes are clamped into a usable editing range.
    OcrTextObject tiny;
    tiny.size = QSizeF(20.0, 2.0);
    OcrTextObject huge;
    huge.size = QSizeF(400.0, 400.0);
    ok &= require(qAbs(tiny.estimateBaseFontSize() - 8.0) < 0.001
                      && qAbs(huge.estimateBaseFontSize() - 96.0) < 0.001,
                  "OCR font estimate was not clamped");

    // Nothing to measure: keep the standard text box size.
    OcrTextObject empty;
    empty.size = QSizeF(0.0, 0.0);
    ok &= require(qAbs(empty.estimateBaseFontSize()
                       - TextBoxObject::DEFAULT_BASE_FONT_SIZE) < 0.001,
                  "empty OCR geometry did not fall back to the default size");
    return ok;
}

inline bool runAllTests()
{
    qDebug() << "=== TextBoxObject tests ===";
    bool ok = true;
    ok &= testOcrBaseFontEstimation();
    ok &= testPersistenceAndOcrIsolation();
    ok &= testLayoutMeasurementAndCache();
    ok &= testLegacyUpgradeAndState();
    ok &= testSearchAndLinkGeometry();
    ok &= testChromeAndSuppressedTextRendering();
    qDebug() << (ok ? "All TextBoxObject tests passed."
                    : "TextBoxObject tests FAILED.");
    return ok;
}

} // namespace TextBoxObjectTests
