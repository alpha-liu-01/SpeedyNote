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

/**
 * @brief Shared helper: is @p ch a CJK/Japanese-style character?
 *
 * Used by OCR text rendering, PDF search, and text selection to decide
 * whether an inter-word / inter-block space separator should be inserted
 * when concatenating text. CJK scripts don't use inter-word spaces, so
 * we only add a space when both surrounding characters are non-CJK.
 *
 * Defined inline here so it can be shared across translation units without
 * pulling in heavier dependencies.
 */
inline bool isCjkLikeChar(QChar ch) {
    ushort u = ch.unicode();
    // Union of the ranges previously duplicated across PdfSearchEngine,
    // OcrTextObject and WindowsInkOcrEngine. Used by the text-joining
    // logic that decides whether to insert a space between adjacent tokens.
    // Note: 0x2E80-0x9FFF already encompasses CJK Radicals, Kangxi Radicals,
    // CJK Symbols and Punctuation, Hiragana, Katakana, Katakana Phonetic
    // Extensions and Unified Ideographs (plus Extension A at 0x3400-0x4DBF).
    return (u >= 0x2E80 && u <= 0x9FFF)   // CJK Radicals..Unified Ideographs (covers Hiragana/Katakana too)
        || (u >= 0xF900 && u <= 0xFAFF)    // CJK Compatibility Ideographs
        || (u >= 0xFE30 && u <= 0xFE4F)    // CJK Compatibility Forms
        || (u >= 0xFF00 && u <= 0xFFEF);   // Fullwidth forms / halfwidth Katakana
}

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
