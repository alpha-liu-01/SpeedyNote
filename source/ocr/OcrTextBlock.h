#pragma once

// ============================================================================
// OcrTextBlock - A recognized text region from OCR analysis
// ============================================================================
// Part of the OCR Phase 1A infrastructure.
//
// Pure data struct representing a word/phrase recognized from handwritten
// ink strokes. Stored as derived cache (not a first-class user object).
// Serialized to .ocr.json sidecar files alongside page/tile JSON.
// ============================================================================

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QRectF>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

struct OcrTextBlock {
    QString id;
    QString text;
    QRectF boundingRect;
    float confidence = 0.0f;
    QVector<QString> sourceStrokeIds;
    QString engineId;
    bool dirty = false;

    struct WordSegment {
        QString text;
        QRectF boundingRect;
    };
    QVector<WordSegment> wordSegments;

    OcrTextBlock() = default;

    static OcrTextBlock create() {
        OcrTextBlock block;
        block.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return block;
    }

    bool isValid() const {
        return !text.isEmpty() && boundingRect.isValid();
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["text"] = text;
        QJsonArray rect;
        rect.append(boundingRect.x());
        rect.append(boundingRect.y());
        rect.append(boundingRect.width());
        rect.append(boundingRect.height());
        obj["rect"] = rect;
        obj["confidence"] = static_cast<double>(confidence);
        QJsonArray strokeIds;
        for (const auto& sid : sourceStrokeIds)
            strokeIds.append(sid);
        obj["sourceStrokeIds"] = strokeIds;
        obj["engineId"] = engineId;
        obj["dirty"] = dirty;
        if (!wordSegments.isEmpty()) {
            QJsonArray words;
            for (const auto& seg : wordSegments) {
                QJsonObject w;
                w["t"] = seg.text;
                QJsonArray r;
                r.append(seg.boundingRect.x());
                r.append(seg.boundingRect.y());
                r.append(seg.boundingRect.width());
                r.append(seg.boundingRect.height());
                w["r"] = r;
                words.append(w);
            }
            obj["words"] = words;
        }
        return obj;
    }

    static OcrTextBlock fromJson(const QJsonObject& obj) {
        OcrTextBlock block;
        block.id = obj["id"].toString();
        if (block.id.isEmpty())
            block.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        block.text = obj["text"].toString();
        QJsonArray rect = obj["rect"].toArray();
        if (rect.size() == 4) {
            block.boundingRect = QRectF(
                rect[0].toDouble(), rect[1].toDouble(),
                rect[2].toDouble(), rect[3].toDouble());
        }
        block.confidence = static_cast<float>(obj["confidence"].toDouble(0.0));
        for (const auto& val : obj["sourceStrokeIds"].toArray())
            block.sourceStrokeIds.append(val.toString());
        block.engineId = obj["engineId"].toString();
        block.dirty = obj["dirty"].toBool(false);
        for (const auto& val : obj["words"].toArray()) {
            QJsonObject w = val.toObject();
            WordSegment seg;
            seg.text = w["t"].toString();
            QJsonArray r = w["r"].toArray();
            if (r.size() == 4)
                seg.boundingRect = QRectF(r[0].toDouble(), r[1].toDouble(),
                                          r[2].toDouble(), r[3].toDouble());
            block.wordSegments.append(seg);
        }
        return block;
    }
};

Q_DECLARE_METATYPE(OcrTextBlock)
Q_DECLARE_METATYPE(QVector<OcrTextBlock>)
