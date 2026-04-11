#include "TextBoxObject.h"

#include <QFontMetricsF>
#include <QTextDocument>
#include <QTextOption>
#include <QPen>
#include <qmath.h>

TextBoxObject::~TextBoxObject()
{
    delete m_cachedDoc;
}

// ---------------------------------------------------------------------------
// Markdown detection
// ---------------------------------------------------------------------------

bool TextBoxObject::isMarkdown() const
{
    return text.contains(QLatin1Char('#'))
        || text.contains(QLatin1Char('*'))
        || text.contains(QLatin1Char('`'))
        || text.contains(QLatin1String("- "))
        || text.contains(QLatin1Char('>'))
        || text.contains(QLatin1Char('['));
}

// ---------------------------------------------------------------------------
// QTextDocument cache
// ---------------------------------------------------------------------------

void TextBoxObject::invalidateDocCache() const
{
    delete m_cachedDoc;
    m_cachedDoc = nullptr;
    m_cachedDocWidth = -1;
    m_cachedText.clear();
}

QTextDocument* TextBoxObject::ensureDocCache(qreal width) const
{
    if (m_cachedDoc
        && qFuzzyCompare(1.0 + m_cachedDocWidth, 1.0 + width)
        && m_cachedText == text
        && m_cachedAlignment == alignment) {
        return m_cachedDoc;
    }

    if (!m_cachedDoc)
        m_cachedDoc = new QTextDocument();

    m_cachedDoc->setMarkdown(text);
    m_cachedDoc->setTextWidth(width);

    QTextOption opt;
    switch (alignment) {
    case TextAlignment::Center: opt.setAlignment(Qt::AlignCenter); break;
    case TextAlignment::Right:  opt.setAlignment(Qt::AlignRight);  break;
    default:                    opt.setAlignment(Qt::AlignLeft);    break;
    }
    m_cachedDoc->setDefaultTextOption(opt);

    QFont docFont;
    if (!fontFamily.isEmpty())
        docFont.setFamily(fontFamily);
    if (fontSize > 0.0)
        docFont.setPixelSize(static_cast<int>(fontSize));
    m_cachedDoc->setDefaultFont(docFont);

    m_cachedDocWidth = width;
    m_cachedText = text;
    m_cachedAlignment = alignment;
    return m_cachedDoc;
}

QTextDocument* TextBoxObject::cachedDocument() const
{
    return m_cachedDoc;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static Qt::Alignment mapAlignment(TextAlignment a)
{
    switch (a) {
    case TextAlignment::Center: return Qt::AlignHCenter;
    case TextAlignment::Right:  return Qt::AlignRight;
    default:                    return Qt::AlignLeft;
    }
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

    // --- Text ---
    if (isMarkdown()) {
        // Markdown path: QTextDocument
        qreal pageWidth = textRect.width() / zoom;
        QTextDocument* doc = ensureDocCache(pageWidth);

        painter.translate(textRect.topLeft());
        painter.scale(zoom, zoom);

        if (fontSize <= 0.0) {
            // Auto-size: scale the document to fit the box height
            qreal docHeight = doc->size().height();
            if (docHeight > 0.0) {
                qreal availHeight = textRect.height() / zoom;
                if (docHeight > availHeight) {
                    qreal scale = availHeight / docHeight;
                    painter.scale(scale, scale);
                }
            }
        }

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
    if (fontSize > 0.0)
        obj["fontSize"] = fontSize;
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
}
