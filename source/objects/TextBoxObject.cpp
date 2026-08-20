#include "TextBoxObject.h"

#include <QAbstractTextDocumentLayout>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QPen>
#include <QTransform>
#include <QtNumeric>
#include <qmath.h>

namespace {

bool isMarkdownText(const QString& value)
{
    return value.contains(QLatin1Char('#'))
        || value.contains(QLatin1Char('*'))
        || value.contains(QLatin1Char('`'))
        || value.contains(QLatin1String("- "))
        || value.contains(QLatin1Char('>'))
        || value.contains(QLatin1Char('['));
}

Qt::Alignment horizontalAlignment(TextAlignment alignment)
{
    switch (alignment) {
    case TextAlignment::Center: return Qt::AlignHCenter;
    case TextAlignment::Right: return Qt::AlignRight;
    default: return Qt::AlignLeft;
    }
}

bool validPositive(qreal value)
{
    return qIsFinite(value) && value > 0.0;
}

qreal headingRatio(int level)
{
    static constexpr qreal ratios[] = {1.6, 1.5, 1.4, 1.3, 1.2, 1.1};
    return (level >= 1 && level <= 6) ? ratios[level - 1] : 1.0;
}

void applyAlignment(QTextDocument& document, TextAlignment alignment)
{
    QTextCursor cursor(&document);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        format.setAlignment(horizontalAlignment(alignment));
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
}

void applyHeadingSizes(QTextDocument& document, const QFont& baseFont,
                       qreal basePixelSize)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const int level =
            block.blockFormat().property(QTextFormat::HeadingLevel).toInt();
        if (level < 1 || level > 6)
            continue;

        const int pixelSize =
            qMax(1, qRound(basePixelSize * headingRatio(level)));
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;

            QTextCharFormat format = fragment.charFormat();
            format.clearProperty(QTextFormat::FontSizeAdjustment);
            format.clearProperty(QTextFormat::FontPointSize);

            QFont font = format.font();
            if (font.family().isEmpty())
                font.setFamily(baseFont.family());
            font.setPixelSize(pixelSize);
            font.setBold(true);
            format.setFont(font);

            QTextCursor cursor(&document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(),
                               QTextCursor::KeepAnchor);
            // Start from the fragment's full format so anchors/italic/code
            // styling survive while the Markdown importer's heading-size
            // adjustment is replaced by SpeedyNote's versioned ratio.
            cursor.setCharFormat(format);
        }
    }
}

void applyFontColor(QTextDocument& document, const QColor& color)
{
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setForeground(QBrush(color));
    cursor.mergeCharFormat(format);
}

QRectF documentRangeRect(const QTextDocument& document, int start, int end)
{
    if (start < 0 || end <= start)
        return QRectF();

    QRectF result;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const int blockStart = block.position();
        const int blockEnd = blockStart + block.length();
        if (end <= blockStart)
            break;
        if (start >= blockEnd || !block.layout())
            continue;

        const int localStart = qMax(0, start - blockStart);
        const int localEnd = qMin(block.length() - 1, end - blockStart);
        const QRectF blockRect =
            document.documentLayout()->blockBoundingRect(block);

        const QTextLayout* layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int lineStart = line.textStart();
            const int lineEnd = lineStart + line.textLength();
            const int selectionStart = qMax(localStart, lineStart);
            const int selectionEnd = qMin(localEnd, lineEnd);
            if (selectionEnd <= selectionStart)
                continue;

            const qreal x1 = line.cursorToX(selectionStart);
            qreal x2 = line.cursorToX(selectionEnd);
            if (x2 <= x1)
                x2 = x1 + 1.0;
            const QRectF lineRect(x1, blockRect.top() + line.y(),
                                  x2 - x1, line.height());
            result = result.isNull() ? lineRect : result.united(lineRect);
        }
    }
    return result;
}

bool sameLayoutInput(const TextBoxLayoutInput& a, const TextBoxLayoutInput& b)
{
    return a.text == b.text
        && a.fontFamily == b.fontFamily
        && qFuzzyCompare(1.0 + a.fontSize, 1.0 + b.fontSize)
        && a.fontColor == b.fontColor
        && a.alignment == b.alignment
        && a.objectSize == b.objectSize
        && a.textLayoutVersion == b.textLayoutVersion
        && a.userTextBox == b.userTextBox
        && qFuzzyCompare(1.0 + a.legacyZoom, 1.0 + b.legacyZoom);
}

} // namespace

TextBoxLayoutResult::TextBoxLayoutResult() = default;
TextBoxLayoutResult::~TextBoxLayoutResult() = default;
TextBoxObject::~TextBoxObject() = default;

QVector<QRectF> TextBoxLayoutResult::findTextRects(
    const QString& query, bool caseSensitive, bool wholeWord) const
{
    QVector<QRectF> rects;
    if (!document || query.isEmpty())
        return rects;

    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    if (wholeWord)
        flags |= QTextDocument::FindWholeWords;

    QTextCursor cursor(document.get());
    while (true) {
        cursor = document->find(query, cursor, flags);
        if (cursor.isNull())
            break;

        QRectF rect = documentRangeRect(*document, cursor.selectionStart(),
                                        cursor.selectionEnd());
        if (!rect.isNull()) {
            rect = QRectF(
                contentRect.left() + rect.left() * renderScale,
                contentRect.top() + rect.top() * renderScale,
                rect.width() * renderScale,
                rect.height() * renderScale);
            rects.append(rect);
        }
    }
    return rects;
}

QString TextBoxLayoutResult::anchorAtObjectPoint(
    const QPointF& objectPoint) const
{
    if (!document || renderScale <= 0.0 || !contentRect.contains(objectPoint))
        return QString();

    const QPointF documentPoint =
        (objectPoint - contentRect.topLeft()) / renderScale;
    int cursorPosition =
        document->documentLayout()->hitTest(documentPoint, Qt::FuzzyHit);
    if (cursorPosition < 0)
        return QString();

    QTextCursor cursor(document.get());
    cursor.setPosition(cursorPosition);
    QString href = cursor.charFormat().anchorHref();
    if (href.isEmpty() && cursorPosition + 1 < document->characterCount()) {
        cursor.setPosition(cursorPosition + 1);
        href = cursor.charFormat().anchorHref();
    }
    return href;
}

// ---------------------------------------------------------------------------
// Markdown detection and layout
// ---------------------------------------------------------------------------

bool TextBoxObject::isMarkdown() const
{
    return isMarkdownText(text);
}

bool TextBoxObject::usesCurrentLayout() const
{
    return type() == QLatin1String("textbox")
        && textLayoutVersion == CURRENT_TEXT_LAYOUT_VERSION;
}

bool TextBoxObject::usesLegacyLayout() const
{
    return type() == QLatin1String("textbox") && textLayoutVersion == 0;
}

TextBoxLayoutInput TextBoxObject::layoutInput(qreal legacyZoom) const
{
    TextBoxLayoutInput input;
    input.text = text;
    input.fontFamily = fontFamily;
    input.fontSize = fontSize;
    input.fontColor = fontColor;
    input.alignment = alignment;
    input.objectSize = size;
    input.textLayoutVersion = textLayoutVersion;
    input.userTextBox = type() == QLatin1String("textbox");
    input.legacyZoom = validPositive(legacyZoom) ? legacyZoom : 1.0;
    return input;
}

std::unique_ptr<TextBoxLayoutResult> TextBoxObject::buildLayout(
    const TextBoxLayoutInput& input)
{
    auto result = std::make_unique<TextBoxLayoutResult>();
    result->versioned =
        input.userTextBox
        && input.textLayoutVersion == CURRENT_TEXT_LAYOUT_VERSION;

    const qreal zoom = validPositive(input.legacyZoom) ? input.legacyZoom : 1.0;
    const qreal padding = result->versioned
        ? CONTENT_PADDING
        : LEGACY_SCREEN_PADDING / zoom;
    const qreal contentWidth = qMax<qreal>(
        1.0, input.objectSize.width() - 2.0 * padding);
    const qreal contentHeight = qMax<qreal>(
        1.0, input.objectSize.height() - 2.0 * padding);
    result->contentRect =
        QRectF(padding, padding, contentWidth, contentHeight);
    result->plainAlignment = horizontalAlignment(input.alignment);

    const bool useMarkdown =
        result->versioned || isMarkdownText(input.text);
    result->markdownDocument = useMarkdown;

    QFont baseFont;
    if (!input.fontFamily.isEmpty())
        baseFont.setFamily(input.fontFamily);

    qreal basePixelSize = input.fontSize;
    if (result->versioned && !validPositive(basePixelSize))
        basePixelSize = DEFAULT_BASE_FONT_SIZE;

    if (!useMarkdown && !validPositive(basePixelSize)) {
        basePixelSize = input.objectSize.height() * 0.75;
        if (validPositive(basePixelSize)) {
            QFont probe = baseFont;
            probe.setPixelSize(qMax(1, qRound(basePixelSize)));
            QFontMetricsF metrics(probe);
            const qreal textWidth = metrics.horizontalAdvance(input.text);
            if (textWidth > contentWidth && textWidth > 0.0)
                basePixelSize *= contentWidth / textWidth;
        }
        result->legacySingleLine = true;
    }

    if (validPositive(basePixelSize)) {
        const int pixelSize = result->versioned
            ? qRound(basePixelSize)
            : static_cast<int>(basePixelSize);
        baseFont.setPixelSize(qMax(1, pixelSize));
    }
    result->plainFont = baseFont;

    result->document = std::make_unique<QTextDocument>();
    result->document->setDocumentMargin(0.0);

    if (result->versioned) {
        result->document->setDefaultFont(baseFont);
        result->document->setMarkdown(input.text);
        applyHeadingSizes(*result->document, baseFont, basePixelSize);
        applyAlignment(*result->document, input.alignment);
        applyFontColor(*result->document, input.fontColor);
        result->document->setTextWidth(contentWidth);

        const qreal lineHeight = QFontMetricsF(baseFont).height();
        const qreal measuredHeight =
            qMax(lineHeight, result->document->documentLayout()
                                 ->documentSize().height());
        result->normalizedHeight =
            qCeil(measuredHeight + 2.0 * CONTENT_PADDING);
        result->contentRect.setHeight(measuredHeight);
        return result;
    }

    // Keep the legacy document setup order and scaling semantics intact.
    if (useMarkdown) {
        result->document->setMarkdown(input.text);
        applyFontColor(*result->document, input.fontColor);
        result->document->setTextWidth(contentWidth);
        applyAlignment(*result->document, input.alignment);
        result->document->setDefaultFont(baseFont);

        if (!validPositive(input.fontSize)) {
            const qreal documentHeight =
                result->document->documentLayout()->documentSize().height();
            if (documentHeight > contentHeight && documentHeight > 0.0)
                result->renderScale = contentHeight / documentHeight;
        }
    } else {
        result->document->setDefaultFont(baseFont);
        result->document->setPlainText(input.text);
        applyAlignment(*result->document, input.alignment);
        applyFontColor(*result->document, input.fontColor);
        result->document->setTextWidth(contentWidth);
        if (result->legacySingleLine) {
            const qreal lineHeight =
                result->document->documentLayout()->documentSize().height();
            result->contentRect.moveTop(
                qMax<qreal>(0.0,
                            (input.objectSize.height() - lineHeight) / 2.0));
            result->contentRect.setHeight(lineHeight);
        }
    }
    result->normalizedHeight = input.objectSize.height();
    return result;
}

const TextBoxLayoutResult* TextBoxObject::ensureLayout(qreal legacyZoom) const
{
    const TextBoxLayoutInput input = layoutInput(legacyZoom);
    if (!m_cachedLayout || !m_hasCachedLayoutInput
        || !sameLayoutInput(input, m_cachedLayoutInput)) {
        m_cachedLayout = buildLayout(input);
        m_cachedLayoutInput = input;
        m_hasCachedLayoutInput = true;
    }
    return m_cachedLayout.get();
}

void TextBoxObject::invalidateLayoutCache() const
{
    m_cachedLayout.reset();
    m_hasCachedLayoutInput = false;
}

TextBoxState TextBoxObject::captureState() const
{
    TextBoxState state;
    state.text = text;
    state.fontFamily = fontFamily;
    state.fontSize = fontSize;
    state.fontColor = fontColor;
    state.backgroundColor = backgroundColor;
    state.alignment = alignment;
    state.showBorder = showBorder;
    state.textLayoutVersion = textLayoutVersion;
    state.position = position;
    state.size = size;
    state.rotation = rotation;
    return state;
}

void TextBoxObject::applyState(const TextBoxState& state)
{
    text = state.text;
    fontFamily = state.fontFamily;
    fontSize = state.fontSize;
    fontColor = state.fontColor;
    backgroundColor = state.backgroundColor;
    alignment = state.alignment;
    showBorder = state.showBorder;
    textLayoutVersion = state.textLayoutVersion;
    position = state.position;
    size = state.size;
    rotation = state.rotation;
    invalidateLayoutCache();
}

QSizeF TextBoxObject::normalizedSizeForWidth(qreal width) const
{
    const qreal safeWidth = validPositive(width) ? width : qMax<qreal>(1.0, size.width());
    if (!usesCurrentLayout())
        return QSizeF(safeWidth, size.height());

    TextBoxLayoutInput input = layoutInput();
    input.objectSize.setWidth(safeWidth);
    std::unique_ptr<TextBoxLayoutResult> result = buildLayout(input);
    return QSizeF(safeWidth, result ? result->normalizedHeight : size.height());
}

void TextBoxObject::reflowToWidth(qreal width)
{
    if (!usesCurrentLayout())
        return;

    const QSizeF normalized = normalizedSizeForWidth(width);
    size = normalized;
    invalidateLayoutCache();
}

bool TextBoxObject::upgradeToCurrentLayout(TextBoxState* before,
                                           TextBoxState* after)
{
    if (!usesLegacyLayout())
        return false;

    if (before)
        *before = captureState();

    qreal resolvedBaseSize = fontSize;
    if (!validPositive(resolvedBaseSize)) {
        if (text.isEmpty()) {
            resolvedBaseSize = DEFAULT_BASE_FONT_SIZE;
        } else {
            const std::unique_ptr<TextBoxLayoutResult> legacy =
                buildLayout(layoutInput());
            if (legacy && legacy->markdownDocument && legacy->document) {
                int resolvedPixels =
                    QFontInfo(legacy->document->defaultFont()).pixelSize();
                if (resolvedPixels <= 0)
                    resolvedPixels =
                        qMax(1, qRound(QFontMetricsF(
                            legacy->document->defaultFont()).height() * 0.75));
                resolvedBaseSize = resolvedPixels * legacy->renderScale;
            } else if (legacy) {
                resolvedBaseSize = legacy->plainFont.pixelSize();
            }
        }
    }
    if (!validPositive(resolvedBaseSize))
        resolvedBaseSize = DEFAULT_BASE_FONT_SIZE;

    fontSize = qMax<qreal>(1.0, resolvedBaseSize);
    textLayoutVersion = CURRENT_TEXT_LAYOUT_VERSION;
    invalidateLayoutCache();
    reflowToWidth(size.width());

    if (after)
        *after = captureState();
    return true;
}

QString TextBoxObject::anchorAtLocalPoint(const QPointF& localPoint,
                                          qreal legacyZoom) const
{
    QPointF unrotatedPoint = localPoint;
    if (!qFuzzyIsNull(rotation)) {
        const QPointF center(size.width() / 2.0, size.height() / 2.0);
        QTransform inverse;
        inverse.translate(center.x(), center.y());
        inverse.rotate(-rotation);
        inverse.translate(-center.x(), -center.y());
        unrotatedPoint = inverse.map(localPoint);
    }

    const TextBoxLayoutResult* layout = ensureLayout(legacyZoom);
    return layout ? layout->anchorAtObjectPoint(unrotatedPoint) : QString();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static Qt::Alignment mapAlignment(TextAlignment a)
{
    return horizontalAlignment(a);
}

void TextBoxObject::render(QPainter& painter, qreal zoom) const
{
    if (!visible || text.isEmpty())
        return;

    QRectF targetRect(
        position.x() * zoom,
        position.y() * zoom,
        size.width() * zoom,
        size.height() * zoom
    );

    if (targetRect.width() < 1.0 || targetRect.height() < 1.0)
        return;

    constexpr qreal pad = 2.0;
    QRectF textRect = targetRect.adjusted(pad, pad, -pad, -pad);
    if (textRect.width() < 1.0 || textRect.height() < 1.0)
        return;

    painter.save();

    // --- Rotation ---
    if (rotation != 0.0) {
        QPointF center = targetRect.center();
        painter.translate(center);
        painter.rotate(rotation);
        painter.translate(-center);
    }

    // --- Background ---
    if (backgroundColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundColor);
        painter.drawRect(targetRect);
    }

    // --- Border ---
    if (showBorder) {
        QColor borderColor = (backgroundColor.lightness() < 100)
                                 ? QColor(100, 100, 100, 120)
                                 : QColor(180, 180, 180, 120);
        painter.setPen(QPen(borderColor, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(targetRect);
    }

    if (usesCurrentLayout()) {
        const TextBoxLayoutResult* layout = ensureLayout();
        if (layout && layout->document) {
            const QPointF textOrigin(
                (position.x() + layout->contentRect.left()) * zoom,
                (position.y() + layout->contentRect.top()) * zoom);
            const qreal availableHeight =
                qMax<qreal>(0.0, size.height() - 2.0 * CONTENT_PADDING);

            painter.translate(textOrigin);
            painter.scale(zoom, zoom);
            const QRectF clip(0.0, 0.0, layout->contentRect.width(),
                              availableHeight);
            painter.setClipRect(clip);
            layout->document->drawContents(&painter, clip);
        }
        painter.restore();
        return;
    }

    // --- Text ---
    if (isMarkdown()) {
        // Markdown path: QTextDocument
        qreal pageWidth = textRect.width() / zoom;
        Q_UNUSED(pageWidth);
        const TextBoxLayoutResult* layout = ensureLayout(zoom);
        QTextDocument* doc =
            layout && layout->document ? layout->document.get() : nullptr;
        if (!doc) {
            painter.restore();
            return;
        }

        painter.translate(textRect.topLeft());
        painter.scale(zoom, zoom);

        if (layout->renderScale < 1.0)
            painter.scale(layout->renderScale, layout->renderScale);

        QRectF clip(0, 0, textRect.width() / zoom, textRect.height() / zoom);
        painter.setClipRect(clip);
        doc->drawContents(&painter, clip);
    } else if (fontSize > 0.0) {
        // Plain text with fixed font size: multi-line word wrap
        qreal effectivePixelSize = fontSize * zoom;
        if (effectivePixelSize < 1.0)
            effectivePixelSize = 1.0;

        QFont font;
        if (!fontFamily.isEmpty())
            font.setFamily(fontFamily);
        font.setPixelSize(static_cast<int>(effectivePixelSize));

        painter.setFont(font);
        painter.setPen(fontColor);
        painter.drawText(textRect, mapAlignment(alignment) | Qt::AlignTop | Qt::TextWordWrap, text);
    } else {
        // Plain text with auto font size (fontSize == 0): single-line, shrink to fit
        qreal effectivePixelSize = size.height() * zoom * 0.75;
        if (effectivePixelSize > 1.0) {
            QFont probe;
            if (!fontFamily.isEmpty())
                probe.setFamily(fontFamily);
            probe.setPixelSize(static_cast<int>(effectivePixelSize));
            QFontMetricsF fm(probe);
            qreal textWidth = fm.horizontalAdvance(text);
            if (textWidth > textRect.width() && textWidth > 0.0) {
                effectivePixelSize *= textRect.width() / textWidth;
            }
        }
        if (effectivePixelSize < 1.0)
            effectivePixelSize = 1.0;

        QFont font;
        if (!fontFamily.isEmpty())
            font.setFamily(fontFamily);
        font.setPixelSize(static_cast<int>(effectivePixelSize));

        painter.setFont(font);
        painter.setPen(fontColor);
        painter.drawText(textRect, mapAlignment(alignment) | Qt::AlignVCenter, text);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QJsonObject TextBoxObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    obj["text"] = text;
    if (!fontFamily.isEmpty())
        obj["fontFamily"] = fontFamily;
    if (textLayoutVersion > 0)
        obj["textLayoutVersion"] = textLayoutVersion;
    if (validPositive(fontSize))
        obj["fontSize"] = fontSize;
    else if (usesCurrentLayout())
        obj["fontSize"] = DEFAULT_BASE_FONT_SIZE;
    obj["fontColor"] = fontColor.name(QColor::HexArgb);
    obj["backgroundColor"] = backgroundColor.name(QColor::HexArgb);

    if (alignment != TextAlignment::Left) {
        switch (alignment) {
        case TextAlignment::Center: obj["alignment"] = QStringLiteral("center"); break;
        case TextAlignment::Right:  obj["alignment"] = QStringLiteral("right");  break;
        default: break;
        }
    }
    if (!showBorder)
        obj["showBorder"] = false;

    return obj;
}

void TextBoxObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    text = obj["text"].toString();
    fontFamily = obj["fontFamily"].toString();
    fontSize = obj["fontSize"].toDouble(0.0);
    textLayoutVersion = obj["textLayoutVersion"].toInt(0);
    if (textLayoutVersion == CURRENT_TEXT_LAYOUT_VERSION
        && !validPositive(fontSize)) {
        fontSize = DEFAULT_BASE_FONT_SIZE;
    }
    if (obj.contains("fontColor"))
        fontColor = QColor(obj["fontColor"].toString());
    if (obj.contains("backgroundColor"))
        backgroundColor = QColor(obj["backgroundColor"].toString());

    QString alignStr = obj["alignment"].toString();
    if (alignStr == QLatin1String("center"))
        alignment = TextAlignment::Center;
    else if (alignStr == QLatin1String("right"))
        alignment = TextAlignment::Right;
    else
        alignment = TextAlignment::Left;

    showBorder = obj["showBorder"].toBool(true);
    invalidateLayoutCache();
}
