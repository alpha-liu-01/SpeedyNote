#pragma once

#include "InsertedObject.h"
#include <QColor>
#include <QFont>

class TextBoxObject : public InsertedObject {
public:
    QString text;
    QString fontFamily;
    qreal fontSize = 0.0;                          // 0 = auto-calculated from boundingRect height
    QColor fontColor = QColor(60, 60, 60);
    QColor backgroundColor = QColor(255, 255, 255, 160);

    TextBoxObject() = default;

    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("textbox"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;
};
