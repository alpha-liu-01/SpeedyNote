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
#include <QSet>
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
        // Optional per-character geometry. When populated, the invariant
        // charBoundingBoxes.size() == text.length() holds (mirrors PdfTextBox).
        // Empty when unavailable; consumers fall back to boundingRect.
        QVector<QRectF> charBoundingBoxes;
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
                // Optional per-character quads; only written when present.
                if (!seg.charBoundingBoxes.isEmpty()) {
                    QJsonArray chars;
                    for (const auto& cb : seg.charBoundingBoxes) {
                        QJsonArray cr;
                        cr.append(cb.x());
                        cr.append(cb.y());
                        cr.append(cb.width());
                        cr.append(cb.height());
                        chars.append(cr);
                    }
                    w["c"] = chars;
                }
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
        // Missing score means "unknown", not "worst possible"; see
        // OcrTextObject::drawConfidenceUnderline.
        block.confidence = static_cast<float>(obj["confidence"].toDouble(1.0));
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
            for (const auto& cval : w["c"].toArray()) {
                QJsonArray cr = cval.toArray();
                if (cr.size() == 4)
                    seg.charBoundingBoxes.append(QRectF(cr[0].toDouble(), cr[1].toDouble(),
                                                        cr[2].toDouble(), cr[3].toDouble()));
            }
            block.wordSegments.append(seg);
        }
        return block;
    }
};

/**
 * @brief Stable identity for a block that carries no source stroke ids.
 *
 * Block ids are minted per scan, so they cannot match a block across a rescan.
 * Text plus rounded geometry can: rescanning the same ink yields the same words
 * in the same place, and rounding absorbs sub-pixel engine jitter.
 */
inline QString ocrBlockDismissalKey(const OcrTextBlock& block) {
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(block.text)
        .arg(qRound(block.boundingRect.x()))
        .arg(qRound(block.boundingRect.y()))
        .arg(qRound(block.boundingRect.width()))
        .arg(qRound(block.boundingRect.height()));
}

/**
 * @brief Whether the user has already dismissed @p block by deleting or
 *        converting it.
 *
 * A scan started before the user dismissed a block still carries that block in
 * its results, because the worker filtered its stroke list before the dismissal
 * was recorded. Materializing it again would resurrect what the user just got
 * rid of.
 *
 * Stroke-backed blocks are recognized through @p suppressedStrokeIds; a block
 * that only partially overlaps that set still describes live ink, so it is
 * kept. Blocks with no stroke ids cannot be expressed that way at all, so they
 * fall back to the geometry fingerprint in @p dismissedBlockKeys.
 */
inline bool isOcrBlockDismissed(const OcrTextBlock& block,
                                const QSet<QString>& suppressedStrokeIds,
                                const QSet<QString>& dismissedBlockKeys) {
    if (block.sourceStrokeIds.isEmpty()) {
        return !dismissedBlockKeys.isEmpty()
            && dismissedBlockKeys.contains(ocrBlockDismissalKey(block));
    }
    if (suppressedStrokeIds.isEmpty())
        return false;
    for (const auto& strokeId : block.sourceStrokeIds) {
        if (!suppressedStrokeIds.contains(strokeId))
            return false;
    }
    return true;
}

/**
 * @brief Flatten per-character geometry into a @p text-length rect array.
 *
 * Returns a vector of size @c text.length() where entry @c i is the canvas-space
 * rect for @c text[i], sourced from @c segments[].charBoundingBoxes. The engine
 * fills those boxes in @c text order, one per non-whitespace character, skipping
 * spaces (see RasterOcrEngine::buildResult). Whitespace characters therefore
 * carry no box; a thin gap rect spanning the space between the surrounding
 * characters is synthesized so the returned indices stay aligned with @c text
 * (this keeps drag-selection contiguous across word boundaries).
 *
 * Returns an EMPTY vector when there is no usable per-character geometry: a
 * single line-level fallback segment (empty @c charBoundingBoxes), any segment
 * whose box count disagrees with its text length, or a non-space/box count
 * mismatch. Callers must then fall back to proportional splitting of the block
 * bounding rect (mirrors the PdfTextBox::charBoundingBoxes -> boundingBox
 * fallback used by the PDF consumers).
 */
inline QVector<QRectF> flattenOcrCharRects(const QString& text,
                                           const QVector<OcrTextBlock::WordSegment>& segments) {
    const int n = text.length();
    if (n == 0 || segments.isEmpty())
        return {};

    // Collect per-character boxes in segment order (one per non-space char).
    QVector<QRectF> segBoxes;
    segBoxes.reserve(n);
    for (const auto& seg : segments) {
        if (seg.charBoundingBoxes.size() != seg.text.length())
            return {};  // missing/partial geometry -> signal fallback
        for (const auto& cb : seg.charBoundingBoxes)
            segBoxes.append(cb);
    }
    if (segBoxes.isEmpty())
        return {};

    QVector<QRectF> out(n);
    QVector<bool> filled(n, false);
    int s = 0;
    for (int i = 0; i < n; ++i) {
        if (text[i].isSpace())
            continue;  // resolved into a gap rect below
        if (s >= segBoxes.size())
            return {};  // non-space chars outnumber boxes -> mismatch
        out[i] = segBoxes[s++];
        filled[i] = true;
    }
    if (s != segBoxes.size())
        return {};  // boxes outnumber non-space chars -> mismatch

    // Resolve whitespace placeholders into thin gap rects between neighbours.
    for (int i = 0; i < n; ++i) {
        if (filled[i])
            continue;
        QRectF left, right;
        bool hasL = false, hasR = false;
        for (int j = i - 1; j >= 0; --j)
            if (filled[j]) { left = out[j]; hasL = true; break; }
        for (int j = i + 1; j < n; ++j)
            if (filled[j]) { right = out[j]; hasR = true; break; }
        if (hasL && hasR) {
            const qreal x0 = left.right();
            const qreal x1 = right.left();
            const qreal w = x1 > x0 ? (x1 - x0) : 0.0;
            const qreal top = qMin(left.top(), right.top());
            const qreal bottom = qMax(left.bottom(), right.bottom());
            out[i] = QRectF(x0, top, w, bottom - top);
        } else if (hasL) {
            out[i] = QRectF(left.right(), left.top(), 0.0, left.height());
        } else if (hasR) {
            out[i] = QRectF(right.left(), right.top(), 0.0, right.height());
        }
        // else: block is all whitespace (won't happen for valid text) -> leave
        // a default-constructed rect; harmless.
    }
    return out;
}

/// Convenience overload: flatten a whole block's per-character geometry.
/// See flattenOcrCharRects() for the size/fallback contract.
inline QVector<QRectF> flattenOcrBlockCharRects(const OcrTextBlock& block) {
    return flattenOcrCharRects(block.text, block.wordSegments);
}

Q_DECLARE_METATYPE(OcrTextBlock)
Q_DECLARE_METATYPE(QVector<OcrTextBlock>)
