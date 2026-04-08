#include "TextBoxObject.h"

#include <QFontMetricsF>

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

    qreal effectivePixelSize;
    if (fontSize > 0.0) {
        effectivePixelSize = fontSize * zoom;
    } else {
        // Start with height-based size, then shrink to fit width if needed
        effectivePixelSize = size.height() * zoom * 0.75;
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
    }

    if (effectivePixelSize < 1.0)
        effectivePixelSize = 1.0;

    QFont font;
    if (!fontFamily.isEmpty())
        font.setFamily(fontFamily);
    font.setPixelSize(static_cast<int>(effectivePixelSize));

    painter.save();

    if (backgroundColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundColor);
        painter.drawRect(targetRect);
    }

    painter.setFont(font);
    painter.setPen(fontColor);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    painter.restore();
}

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
}
