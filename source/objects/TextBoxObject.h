#pragma once

#include "InsertedObject.h"
#include <QColor>
#include <QFont>

class QTextDocument;

enum class TextAlignment { Left, Center, Right };

class TextBoxObject : public InsertedObject {
public:
    QString text;
    QString fontFamily;
    qreal fontSize = 0.0;                          // 0 = auto-calculated from boundingRect height
    QColor fontColor = QColor(60, 60, 60);
    QColor backgroundColor = QColor(255, 255, 255, 160);
    TextAlignment alignment = TextAlignment::Left;
    bool showBorder = true;   // true for manual textbox, false for ocr_text

    TextBoxObject() = default;
    ~TextBoxObject() override;

    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("textbox"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;

    bool isMarkdown() const;
    void invalidateDocCache() const;
    QTextDocument* cachedDocument() const;
    QTextDocument* ensureDocCache(qreal width) const;

protected:

    mutable QTextDocument* m_cachedDoc = nullptr;
    mutable qreal m_cachedDocWidth = -1;
    mutable QString m_cachedText;
    mutable TextAlignment m_cachedAlignment = TextAlignment::Left;
    mutable QColor m_cachedFontColor;
};
