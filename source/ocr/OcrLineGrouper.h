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
#include <QHash>
#include <QPair>
#include <algorithm>
#include <map>

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

// ============================================================================
// Grid/Line-snapped grouping for improved OCR accuracy
// ============================================================================

/**
 * @brief Group strokes into horizontal bands defined by fixed line spacing.
 *
 * Each stroke is assigned to the band containing its vertical center.
 * Bands are then split by horizontal gaps (like splitLineByHorizontalGaps).
 * Bounding rects are vertically snapped to band boundaries.
 *
 * @param strokes      Input strokes.
 * @param lineSpacing  Height of each horizontal band in page units.
 * @param gapFactor    Gap threshold multiplier for horizontal splitting.
 * @return Sub-groups with grid-snapped vertical bounds.
 */
inline QVector<StrokeLineGroup> groupStrokesByLineBands(
    const QVector<VectorStroke>& strokes,
    int lineSpacing,
    qreal gapFactor = 2.0)
{
    if (lineSpacing <= 0)
        return groupStrokesIntoLines(strokes);

    struct Entry {
        int index;
        QRectF box;
    };

    std::map<int, QVector<Entry>> bandMap;

    for (int i = 0; i < strokes.size(); ++i) {
        const auto& s = strokes[i];
        if (s.points.isEmpty() || s.boundingBox.isNull())
            continue;
        int band = static_cast<int>(s.boundingBox.center().y()) / lineSpacing;
        bandMap[band].append({i, s.boundingBox});
    }

    QVector<StrokeLineGroup> allGroups;

    for (auto& pair : bandMap) {
        int band = pair.first;
        auto& entries = pair.second;

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.box.left() < b.box.left();
        });

        qreal bandTop = band * lineSpacing;
        qreal bandBottom = (band + 1) * lineSpacing;

        StrokeLineGroup lineGroup;
        QRectF unionRect;
        for (const auto& e : entries) {
            lineGroup.strokeIndices.append(e.index);
            unionRect = unionRect.isNull() ? e.box : unionRect.united(e.box);
        }
        lineGroup.boundingRect = QRectF(unionRect.left(), bandTop,
                                        unionRect.width(), bandBottom - bandTop);

        auto subGroups = splitLineByHorizontalGaps(lineGroup, strokes, gapFactor);
        for (auto& sg : subGroups) {
            sg.boundingRect.setTop(bandTop);
            sg.boundingRect.setBottom(bandBottom);
            allGroups.append(sg);
        }
    }

    return allGroups;
}

/**
 * @brief Group strokes by grid cells and merge horizontally adjacent occupied cells.
 *
 * Each stroke is assigned to the grid cell containing its bounding box center.
 * Adjacent occupied cells on the same row are merged into a single group (one
 * "sentence" / text run). Bounding rects are snapped to exact grid cell boundaries.
 *
 * @param strokes      Input strokes.
 * @param gridSpacing  Width and height of each grid cell in page units.
 * @return Groups with grid-snapped bounding rects, sorted top-to-bottom then left-to-right.
 */
inline QVector<StrokeLineGroup> groupStrokesByGridCells(
    const QVector<VectorStroke>& strokes,
    int gridSpacing)
{
    if (gridSpacing <= 0)
        return groupStrokesIntoLines(strokes);

    // Map each stroke to its grid cell (col, row)
    using Cell = QPair<int,int>;
    QHash<Cell, QVector<int>> cellMap;

    for (int i = 0; i < strokes.size(); ++i) {
        const auto& s = strokes[i];
        if (s.points.isEmpty() || s.boundingBox.isNull())
            continue;
        int col = static_cast<int>(s.boundingBox.center().x()) / gridSpacing;
        int row = static_cast<int>(s.boundingBox.center().y()) / gridSpacing;
        cellMap[Cell(col, row)].append(i);
    }

    if (cellMap.isEmpty())
        return {};

    // Collect unique occupied cells per row, sorted by column
    std::map<int, QVector<int>> rowCols;
    for (auto it = cellMap.constBegin(); it != cellMap.constEnd(); ++it) {
        rowCols[it.key().second].append(it.key().first);
    }

    QVector<StrokeLineGroup> result;

    for (auto& rowPair : rowCols) {
        int row = rowPair.first;
        auto& cols = rowPair.second;
        std::sort(cols.begin(), cols.end());

        // Merge adjacent columns into runs
        int runStart = cols[0];
        int runEnd = cols[0];

        auto flushRun = [&]() {
            StrokeLineGroup group;
            for (int c = runStart; c <= runEnd; ++c) {
                Cell cell(c, row);
                if (cellMap.contains(cell)) {
                    for (int si : cellMap[cell])
                        group.strokeIndices.append(si);
                }
            }
            group.boundingRect = QRectF(
                runStart * gridSpacing,
                row * gridSpacing,
                (runEnd - runStart + 1) * gridSpacing,
                gridSpacing);
            result.append(group);
        };

        for (int i = 1; i < cols.size(); ++i) {
            if (cols[i] == runEnd + 1) {
                runEnd = cols[i];
            } else {
                flushRun();
                runStart = cols[i];
                runEnd = cols[i];
            }
        }
        flushRun();
    }

    return result;
}
