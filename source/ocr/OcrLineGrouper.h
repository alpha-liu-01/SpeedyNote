#pragma once

// ============================================================================
// OcrLineGrouper - Group strokes into lines by vertical overlap
// ============================================================================
// Shared utility for OCR engines that need to split strokes into text lines
// before recognition (e.g. MlKitOcrEngine). Adapted from the line-grouping
// logic in WindowsInkOcrEngine::analyzeWithRecognizer().
// ============================================================================

#include "../strokes/VectorStroke.h"

#include <QVector>
#include <QRectF>
#include <algorithm>

struct StrokeLineGroup {
    QVector<int> strokeIndices;  ///< Indices into the input stroke vector
    QRectF boundingRect;         ///< Union of all stroke bounding boxes in this line
};

/**
 * @brief Group strokes into horizontal text lines using vertical overlap.
 *
 * Algorithm:
 *  1. Collect (index, boundingBox) for each stroke with a non-empty bounding box
 *  2. Sort by vertical midpoint
 *  3. Greedily merge: if >50% of a stroke's height overlaps an existing line, join it
 *  4. Sort each line's strokes left-to-right by X
 *  5. Compute union bounding rect per line
 *
 * @param strokes The input strokes (indices in the returned groups refer to this vector).
 * @return One StrokeLineGroup per detected line, sorted top-to-bottom.
 */
inline QVector<StrokeLineGroup> groupStrokesIntoLines(const QVector<VectorStroke>& strokes)
{
    struct Entry {
        int index;
        QRectF box;
    };

    QVector<Entry> entries;
    entries.reserve(strokes.size());
    for (int i = 0; i < strokes.size(); ++i) {
        const auto& s = strokes[i];
        if (s.points.isEmpty() || s.boundingBox.isNull())
            continue;
        entries.append({i, s.boundingBox});
    }

    if (entries.isEmpty())
        return {};

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.box.center().y() < b.box.center().y();
    });

    struct LineAccum {
        QVector<int> entryIndices; // indices into `entries`
        qreal top = 0;
        qreal bottom = 0;
    };
    QVector<LineAccum> lines;

    for (int i = 0; i < entries.size(); ++i) {
        const qreal sTop = entries[i].box.top();
        const qreal sBottom = entries[i].box.bottom();
        const qreal sHeight = entries[i].box.height();

        bool merged = false;
        for (auto& line : lines) {
            const qreal overlapTop = qMax(sTop, line.top);
            const qreal overlapBot = qMin(sBottom, line.bottom);
            const qreal overlap = qMax(0.0, overlapBot - overlapTop);
            if (overlap > sHeight * 0.5) {
                line.entryIndices.append(i);
                line.top = qMin(line.top, sTop);
                line.bottom = qMax(line.bottom, sBottom);
                merged = true;
                break;
            }
        }
        if (!merged) {
            LineAccum g;
            g.entryIndices.append(i);
            g.top = sTop;
            g.bottom = sBottom;
            lines.append(g);
        }
    }

    QVector<StrokeLineGroup> result;
    result.reserve(lines.size());

    for (const auto& line : lines) {
        QVector<int> sorted = line.entryIndices;
        std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
            return entries[a].box.left() < entries[b].box.left();
        });

        StrokeLineGroup group;
        QRectF unionRect;
        for (int ei : sorted) {
            group.strokeIndices.append(entries[ei].index);
            unionRect = unionRect.isNull() ? entries[ei].box : unionRect.united(entries[ei].box);
        }
        group.boundingRect = unionRect;
        result.append(group);
    }

    return result;
}
