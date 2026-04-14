#pragma once

// ============================================================================
// OcrLineGrouper - Group strokes into lines by Y-midpoint gap detection
// ============================================================================
// Shared utility for OCR engines that need to split strokes into text lines
// before recognition (e.g. MlKitOcrEngine).
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
 * @brief Group strokes into horizontal text lines using Y-midpoint gap detection.
 *
 * Algorithm:
 *  1. Collect (index, boundingBox, midY) for each stroke with a non-empty bounding box
 *  2. Sort by midpoint Y
 *  3. Compute adaptive gap threshold: median stroke height * 0.8
 *  4. Walk the sorted list; start a new group whenever the gap between consecutive
 *     midpoints exceeds the threshold
 *  5. Sort each group's strokes left-to-right by X
 *  6. Compute union bounding rect per group
 *
 * This avoids the "snowball effect" of the previous bounding-box overlap merge,
 * where expanding line ranges would absorb strokes from adjacent rows.
 *
 * @param strokes The input strokes (indices in the returned groups refer to this vector).
 * @return One StrokeLineGroup per detected line, sorted top-to-bottom.
 */
inline QVector<StrokeLineGroup> groupStrokesIntoLines(const QVector<VectorStroke>& strokes)
{
    struct Entry {
        int index;
        QRectF box;
        qreal midY;
    };

    QVector<Entry> entries;
    entries.reserve(strokes.size());
    for (int i = 0; i < strokes.size(); ++i) {
        const auto& s = strokes[i];
        if (s.points.isEmpty() || s.boundingBox.isNull())
            continue;
        entries.append({i, s.boundingBox, s.boundingBox.center().y()});
    }

    if (entries.isEmpty())
        return {};

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.midY < b.midY;
    });

    // Adaptive threshold: median stroke height * 0.8.
    // Median is robust against outliers (tall brackets, tiny dots).
    QVector<qreal> heights;
    heights.reserve(entries.size());
    for (const auto& e : entries)
        heights.append(e.box.height());
    std::sort(heights.begin(), heights.end());
    const qreal medianHeight = heights[heights.size() / 2];
    const qreal gapThreshold = medianHeight * 0.8;

    // Single-pass gap scan: split into groups at large midpoint gaps.
    QVector<StrokeLineGroup> result;
    int groupStart = 0;

    for (int i = 1; i <= entries.size(); ++i) {
        bool split = (i == entries.size())
                   || (entries[i].midY - entries[i - 1].midY > gapThreshold);
        if (!split)
            continue;

        // Build group from entries[groupStart .. i-1], sorted by X.
        QVector<int> band;
        band.reserve(i - groupStart);
        for (int j = groupStart; j < i; ++j)
            band.append(j);
        std::sort(band.begin(), band.end(), [&](int a, int b) {
            return entries[a].box.left() < entries[b].box.left();
        });

        StrokeLineGroup group;
        QRectF unionRect;
        for (int ei : band) {
            group.strokeIndices.append(entries[ei].index);
            unionRect = unionRect.isNull() ? entries[ei].box
                                           : unionRect.united(entries[ei].box);
        }
        group.boundingRect = unionRect;
        result.append(group);

        groupStart = i;
    }

    return result;
}

/**
 * @brief Split a single line group into sub-groups at large horizontal gaps.
 *
 * Strokes that are close together horizontally stay in one group (a sentence),
 * while clusters separated by a gap wider than lineHeight * gapFactor become
 * separate groups (distinct text boxes).
 *
 * @param lineGroup  The line group to split (indices refer to @p strokes).
 * @param strokes    The full stroke vector.
 * @param gapFactor  Minimum gap as a multiple of the line height to trigger a split.
 * @return One or more sub-groups.  Returns the original group unchanged when
 *         no gap exceeds the threshold (common fast path for sentences).
 */
inline QVector<StrokeLineGroup> splitLineByHorizontalGaps(
    const StrokeLineGroup& lineGroup,
    const QVector<VectorStroke>& strokes,
    qreal gapFactor = 2.0)
{
    if (lineGroup.strokeIndices.size() <= 1)
        return {lineGroup};

    struct Entry {
        int strokeIndex;
        QRectF box;
    };

    QVector<Entry> entries;
    entries.reserve(lineGroup.strokeIndices.size());
    for (int si : lineGroup.strokeIndices) {
        const auto& s = strokes[si];
        if (!s.points.isEmpty() && !s.boundingBox.isNull())
            entries.append({si, s.boundingBox});
    }

    if (entries.size() <= 1)
        return {lineGroup};

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.box.left() < b.box.left();
    });

    const qreal gapThreshold = lineGroup.boundingRect.height() * gapFactor;

    QVector<StrokeLineGroup> result;
    StrokeLineGroup current;
    QRectF currentRect = entries[0].box;
    current.strokeIndices.append(entries[0].strokeIndex);
    qreal clusterRight = entries[0].box.right();

    for (int i = 1; i < entries.size(); ++i) {
        const qreal gap = entries[i].box.left() - clusterRight;

        if (gap > gapThreshold) {
            current.boundingRect = currentRect;
            result.append(current);

            current = StrokeLineGroup();
            currentRect = entries[i].box;
        } else {
            currentRect = currentRect.united(entries[i].box);
        }

        current.strokeIndices.append(entries[i].strokeIndex);
        clusterRight = qMax(clusterRight, entries[i].box.right());
    }

    current.boundingRect = currentRect;
    result.append(current);

    if (result.size() == 1)
        return {lineGroup};

    return result;
}
