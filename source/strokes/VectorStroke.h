#pragma once

// ============================================================================
// VectorStroke - A complete stroke (pen down → pen up)
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.2)
// Extracted from VectorCanvas.h for modularity
// ============================================================================

#include "StrokePoint.h"

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QColor>
#include <QRectF>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineF>
#include <QPair>
#include <QPolygonF>
#include <QUuid>

#include <cmath>

/**
 * @brief A complete vector stroke consisting of multiple points.
 * 
 * Represents a single pen stroke from pen-down to pen-up.
 * Stores all points with pressure, color, and base thickness.
 * Provides hit testing for eraser functionality and serialization.
 */
struct VectorStroke {
    QString id;                     ///< UUID for tracking (used in undo/redo)
    QVector<StrokePoint> points;    ///< All points in the stroke
    QColor color;                   ///< Stroke color
    qreal baseThickness;            ///< Base thickness before pressure scaling
    QRectF boundingBox;             ///< Cached bounding box for fast culling/hit testing
    /// Widest half-width of the ribbon, cached by updateBoundingBox() so the
    /// eraser's broad phase is O(1) per stroke instead of walking every point.
    /// Negative until computed; maxHalfWidth() then falls back to a scan.
    qreal cachedMaxHalfWidth = -1.0;
    
    /**
     * @brief Default constructor.
     * Initializes baseThickness to 5.0 pixels.
     */
    VectorStroke() : baseThickness(5.0) {}
    
    /**
     * @brief Recalculate the bounding box from current points.
     * 
     * Should be called after all points are added (when stroke is finalized).
     * Adds padding based on maximum possible stroke width.
     */
    void updateBoundingBox() {
        cachedMaxHalfWidth = baseThickness / 2.0;
        if (points.isEmpty()) {
            boundingBox = QRectF();
            return;
        }
        qreal maxWidth = baseThickness * 2;
        qreal minX = points[0].pos.x(), maxX = minX;
        qreal minY = points[0].pos.y(), maxY = minY;
        for (const auto& pt : points) {
            minX = qMin(minX, pt.pos.x());
            maxX = qMax(maxX, pt.pos.x());
            minY = qMin(minY, pt.pos.y());
            maxY = qMax(maxY, pt.pos.y());
            cachedMaxHalfWidth = qMax(cachedMaxHalfWidth, baseThickness * pt.pressure / 2.0);
        }
        boundingBox = QRectF(minX - maxWidth, minY - maxWidth,
                             maxX - minX + maxWidth * 2,
                             maxY - minY + maxWidth * 2);
    }
    
    /**
     * @brief Check if an eraser disc centred on `point` touches this stroke.
     *
     * The test is solid-to-solid: the eraser disc reaches the stroke's drawn
     * outline, not merely its centre line. `radius` is the eraser's own radius
     * (the circle the user sees), and the stroke contributes its rendered
     * half-width at the tested sample.
     *
     * Why the half-width is added back: a stroke is a filled ribbon of width
     * `baseThickness * pressure`, so requiring the eraser centre to come within
     * `radius` of the centre line makes the eraser stop short of the visible
     * ink by half the stroke width - the cursor circle would have to overlap
     * the stroke noticeably before anything happened. The previous version had
     * this same half-width term but grew only the hit test and not the cursor,
     * which is what made the eraser look bigger than its circle. Now both sides
     * use this rule, so the circle and the behaviour agree.
     *
     * @param point The point to test (eraser centre).
     * @param radius Eraser radius in the same units as the stroke points.
     * @return True if the disc touches any part of the stroke.
     */
    bool containsPoint(const QPointF& point, qreal radius) const {
        if (points.isEmpty()) {
            return false;
        }
        
        // Quick rejection. Pad by the widest half-width this stroke can reach,
        // otherwise a thick stroke whose outline is touched but whose centre
        // line is farther than `radius` would be culled here.
        const qreal pad = radius + maxHalfWidth();
        if (!boundingBox.adjusted(-pad, -pad, pad, pad).contains(point)) {
            return false;
        }
        
        // Single-point stroke (dot): distance to the dot's disc.
        if (points.size() == 1) {
            const qreal dx = point.x() - points[0].pos.x();
            const qreal dy = point.y() - points[0].pos.y();
            const qreal reach = radius + halfWidthAt(0);
            return dx * dx + dy * dy < reach * reach;
        }
        
        // Multi-point stroke: compare against each segment's own half-width so
        // a pressure-tapered stroke is judged by its local thickness.
        for (int i = 1; i < points.size(); ++i) {
            const qreal reach = radius
                                + (halfWidthAt(i - 1) + halfWidthAt(i)) * 0.5;
            if (distanceToSegment(point, points[i-1].pos, points[i].pos) < reach) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Rendered half-width of the stroke at point index `i`.
     *
     * Matches the polygon builder in VectorLayer, which draws a ribbon of
     * width `baseThickness * pressure`.
     */
    qreal halfWidthAt(int i) const {
        if (i < 0 || i >= points.size()) {
            return baseThickness / 2.0;
        }
        return baseThickness * points[i].pressure / 2.0;
    }
    
    /**
     * @brief Largest half-width anywhere on this stroke.
     *
     * Used to size the broad-phase rejection padding.
     */
    qreal maxHalfWidth() const {
        if (cachedMaxHalfWidth >= 0.0) {
            return cachedMaxHalfWidth;
        }
        qreal hw = baseThickness / 2.0;
        for (const StrokePoint& pt : points) {
            hw = qMax(hw, baseThickness * pt.pressure / 2.0);
        }
        return hw;
    }

    /**
     * @brief Check whether an eraser disc swept from `a` to `b` reaches this stroke.
     *
     * Pointer events arrive at a finite rate, so a fast eraser stroke skips
     * whole regions between two sampled positions. Treating the motion as a
     * capsule (a disc dragged along the segment a->b) closes those gaps; a
     * per-position disc test alone cannot.
     *
     * @param a Start of the eraser motion path.
     * @param b End of the eraser motion path.
     * @param radius Eraser radius in the same units as the stroke points.
     * @return True if the swept disc touches any part of this stroke.
     */
    bool intersectsEraserSweep(const QPointF& a, const QPointF& b, qreal radius) const {
        if (points.isEmpty()) {
            return false;
        }
        
        // Quick rejection against the swept capsule's bounding box, grown by the
        // stroke's own half-width so the broad phase covers outlines, not just
        // centre lines. QRectF::intersects() is false for a degenerate rect,
        // which an axis-aligned swipe produces, so the path is padded first.
        const qreal pad = radius + maxHalfWidth();
        const QRectF sweepRect = QRectF(a, b).normalized()
                                    .adjusted(-pad, -pad, pad, pad);
        if (!boundingBox.adjusted(-pad, -pad, pad, pad).intersects(sweepRect)) {
            return false;
        }
        
        if (points.size() == 1) {
            return distanceToSegment(points[0].pos, a, b) < radius + halfWidthAt(0);
        }
        
        // Two polylines touch when their closest approach is within the sum of
        // their half-widths; the sweep is a capsule of `radius` around a->b.
        const QRectF pathRect = QRectF(a, b).normalized();
        for (int i = 1; i < points.size(); ++i) {
            const qreal reach = radius
                                + (halfWidthAt(i - 1) + halfWidthAt(i)) * 0.5;
            const QPointF& p = points[i-1].pos;
            const QPointF& q = points[i].pos;
            // Cheap per-segment rejection before the four distance tests; a long
            // stroke has thousands of segments and the sweep only meets a few.
            if (qMax(p.x(), q.x()) < pathRect.left() - reach
                || qMin(p.x(), q.x()) > pathRect.right() + reach
                || qMax(p.y(), q.y()) < pathRect.top() - reach
                || qMin(p.y(), q.y()) > pathRect.bottom() + reach) {
                continue;
            }
            if (segmentsCloserThan(p, q, a, b, reach)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Split this stroke around an eraser sweep, keeping what it missed.
     *
     * "Partial" erasing: instead of deleting the whole stroke when the eraser
     * touches it, remove only the covered stretch and keep the remainder. The
     * eraser is modelled as a disc of `radius` dragged from `a` to `b`, so the
     * removed set is the capsule (stadium) around that path.
     *
     * Reach is measured to the stroke's centre line, with no half-width term.
     * Adding the half-width would match whole-stroke mode's trigger, but a cut
     * is a topological change: letting a graze of a thick stroke's edge slice
     * it in two looks wrong. Requiring the disc to cover the centre line keeps
     * a nick on the edge from severing the stroke.
     *
     * @param a Start of the eraser motion path.
     * @param b End of the eraser motion path.
     * @param radius Eraser radius in the same units as the stroke points.
     * @param changedBounds Optional out: region whose rendering differs
     *        between this stroke and the returned fragments (see
     *        subtractWhere). Left untouched when the sweep missed the stroke.
     * @return Surviving fragments. Returns this stroke unchanged (same id) when
     *         the sweep missed it entirely, and an empty list when nothing
     *         survived.
     */
    QVector<VectorStroke> subtractEraserSweep(const QPointF& a, const QPointF& b,
                                              qreal radius,
                                              QRectF* changedBounds = nullptr) const {
        if (points.isEmpty() || radius <= 0.0) {
            return {*this};
        }
        
        // A dot either survives whole or not at all - there is no stretch to cut.
        if (points.size() == 1) {
            if (distanceToSegment(points[0].pos, a, b) < radius) {
                if (changedBounds) {
                    *changedBounds = boundingBox;
                }
                return {};
            }
            return {*this};
        }
        
        // Broad phase: skip the per-segment work when the stroke's outline is
        // nowhere near the swept capsule. Padded by the stroke's half-width so a
        // graze is never culled here; the exact test is per segment below.
        const qreal pad = radius + maxHalfWidth();
        const QRectF sweepRect = QRectF(a, b).normalized()
                                    .adjusted(-pad, -pad, pad, pad);
        if (!boundingBox.adjusted(-pad, -pad, pad, pad).intersects(sweepRect)) {
            return {*this};
        }
        
        // Reach is to the centre line, so the capsule's own bounds suffice.
        const QRectF reach = QRectF(a, b).normalized()
                                .adjusted(-radius, -radius, radius, radius);
        return subtractWhere(reach, [&](const QPointF& p, const QPointF& q) {
            return survivingIntervals(p, q, a, b, radius);
        }, changedBounds);
    }
    
    /**
     * @brief Split this stroke around a lasso polygon, keeping what is outside.
     *
     * The lasso counterpart of subtractEraserSweep(): the removed set is the
     * polygon interior instead of a swept disc, and the cut lands exactly where
     * the stroke crosses the polygon edge.
     *
     * @param polygon Lasso path in the same coordinate space as the points.
     * @param changedBounds Optional out, as for subtractEraserSweep().
     * @return Surviving fragments, this stroke unchanged (same id) when the
     *         polygon missed it, or an empty list when nothing survived.
     */
    QVector<VectorStroke> subtractPolygon(const QPolygonF& polygon,
                                          QRectF* changedBounds = nullptr) const {
        if (points.isEmpty() || polygon.size() < 3) {
            return {*this};
        }

        const auto inside = [&polygon](const QPointF& pt) {
            // Odd-even matches the hit test the whole-stroke lasso uses
            // (strokeIntersectsLasso), so both modes agree on the region.
            return polygon.containsPoint(pt, Qt::OddEvenFill);
        };

        if (points.size() == 1) {
            if (inside(points[0].pos)) {
                if (changedBounds) {
                    *changedBounds = boundingBox;
                }
                return {};
            }
            return {*this};
        }

        const QRectF polyBounds = polygon.boundingRect();
        if (polyBounds.isEmpty() || !boundingBox.intersects(polyBounds)) {
            return {*this};
        }

        return subtractWhere(polyBounds, [&](const QPointF& p, const QPointF& q) {
            return survivingIntervalsWhere(p, q, inside);
        }, changedBounds);
    }
    
    /**
     * @brief Rebuild this stroke from the parts an erase predicate left alone.
     *
     * @param reach Bounds of everything the predicate can erase. A segment
     *        whose own bounds miss it is kept whole without being sampled,
     *        which makes cutting a long stroke cost O(points) cheap tests
     *        rather than O(length) predicate calls.
     * @param intervalsForSegment Maps one source segment to the parameter
     *        ranges [t0,t1] that survive, in increasing order.
     * @param changedBounds Optional out. Set to the region where the rendered
     *        result can differ from this stroke, so callers can repaint just
     *        that. It is NOT just the erased area: the renderer smooths points
     *        with Catmull-Rom, so cutting at a vertex also reshapes the curve
     *        for two neighbouring vertices on either side. Straight 2-point
     *        strokes are not smoothed, so only the erased stretch counts there.
     *        Padded by the stroke's widest half-width. Only written when the
     *        stroke was actually cut.
     */
    template <typename IntervalsFn>
    QVector<VectorStroke> subtractWhere(const QRectF& reach, IntervalsFn intervalsForSegment,
                                        QRectF* changedBounds = nullptr) const {
        struct Piece {
            int segment = 0;   ///< Index of the source segment (points[i]..points[i+1])
            qreal t0 = 0.0;    ///< Start parameter along that segment
            qreal t1 = 1.0;    ///< End parameter along that segment
        };
        QVector<Piece> pieces;
        QRectF changed;
        
        for (int i = 1; i < points.size(); ++i) {
            const QPointF& p = points[i - 1].pos;
            const QPointF& q = points[i].pos;
            // Inclusive overlap test: QRectF::intersects() rejects the
            // zero-height rect of a horizontal segment.
            const bool segmentMissesReach =
                qMax(p.x(), q.x()) < reach.left() || qMin(p.x(), q.x()) > reach.right()
                || qMax(p.y(), q.y()) < reach.top() || qMin(p.y(), q.y()) > reach.bottom();
            if (segmentMissesReach) {
                pieces.append(Piece{i - 1, 0.0, 1.0});
                continue;
            }
            const auto intervals = intervalsForSegment(p, q);
            for (const auto& iv : intervals) {
                pieces.append(Piece{i - 1, iv.first, iv.second});
            }
            if (changedBounds) {
                const bool untouched = intervals.size() == 1
                                       && intervals.first().first <= 0.0
                                       && intervals.first().second >= 1.0;
                if (!untouched) {
                    changed = changed.united(changedRegionForSegment(i - 1, intervals));
                }
            }
        }
        if (changedBounds && !changed.isNull()) {
            const qreal pad = maxHalfWidth() + 1.0;
            *changedBounds = changed.adjusted(-pad, -pad, pad, pad);
        }
        
        if (pieces.isEmpty()) {
            return {};
        }
        
        // Group pieces into fragments. Two consecutive pieces belong to the same
        // fragment when they are adjacent in the same segment, or when the first
        // starts a segment exactly where the previous one ended the prior one.
        QVector<QVector<Piece>> runs;
        for (const Piece& piece : pieces) {
            bool continuesPrevious = false;
            if (!runs.isEmpty()) {
                const Piece& prev = runs.last().last();
                const bool sameSegment = (piece.segment == prev.segment)
                                         && (piece.t0 <= prev.t1 + 1e-9);
                const bool adjacentSegments = (piece.segment == prev.segment + 1)
                                              && (prev.t1 >= 1.0 - 1e-9)
                                              && (piece.t0 <= 1e-9);
                continuesPrevious = sameSegment || adjacentSegments;
            }
            if (continuesPrevious) {
                runs.last().append(piece);
            } else {
                runs.append(QVector<Piece>{piece});
            }
        }
        
        // Materialise each run as a stroke. The untouched case (a single run
        // spanning the whole polyline) returns the original stroke as-is so
        // callers can detect "nothing happened" by the unchanged id.
        if (runs.size() == 1 && runs.first().size() == points.size() - 1
            && qFuzzyIsNull(runs.first().first().t0)
            && qFuzzyCompare(runs.first().last().t1, 1.0)) {
            return {*this};
        }
        
        QVector<VectorStroke> fragments;
        fragments.reserve(runs.size());
        for (const QVector<Piece>& run : runs) {
            VectorStroke fragment = *this;
            fragment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            fragment.points.clear();
            
            bool haveLast = false;
            for (const Piece& piece : run) {
                const StrokePoint& p = points[piece.segment];
                const StrokePoint& q = points[piece.segment + 1];
                // Only the first piece contributes its start; later pieces of a
                // run begin exactly where the previous one ended.
                if (!haveLast) {
                    fragment.points.append(interpolatePoint(p, q, piece.t0));
                }
                fragment.points.append(interpolatePoint(p, q, piece.t1));
                haveLast = true;
            }
            
            // A sliver renders as a round-capped blob as wide as the stroke,
            // which reads as debris left by the cut. That includes a single
            // point and a stretch far shorter than any visible mark.
            qreal fragmentLength = 0.0;
            for (int k = 1; k < fragment.points.size(); ++k) {
                fragmentLength += QLineF(fragment.points[k - 1].pos,
                                         fragment.points[k].pos).length();
            }
            if (fragment.points.size() < 2 || fragmentLength < MIN_FRAGMENT_LENGTH) {
                continue;
            }
            fragment.updateBoundingBox();
            fragments.append(fragment);
        }
        return fragments;
    }
    
    /**
     * @brief Region of one cut segment whose rendering can change.
     * @param segment Index of the segment (points[segment]..points[segment+1]).
     * @param surviving Parameter ranges of that segment that survive.
     */
    QRectF changedRegionForSegment(int segment,
                                   const QVector<QPair<qreal, qreal>>& surviving) const {
        if (points.size() == 2) {
            // Not smoothed: only the erased stretches differ.
            const StrokePoint& p = points[0];
            const StrokePoint& q = points[1];
            QRectF rect;
            bool any = false;
            const auto addGap = [&](qreal t0, qreal t1) {
                if (t1 > t0) {
                    const QRectF gap = QRectF(interpolatePoint(p, q, t0).pos,
                                              interpolatePoint(p, q, t1).pos).normalized();
                    rect = any ? rect.united(gap) : gap;
                    any = true;
                }
            };
            qreal from = 0.0;
            for (const auto& iv : surviving) {
                addGap(from, iv.first);
                from = iv.second;
            }
            addGap(from, 1.0);
            return rect;
        }
        // Catmull-Rom: segment s depends on points s-1..s+2, and the cut moves
        // the end tangents of the segments next to it, so take s-2..s+3.
        const int lo = qMax(0, segment - 2);
        const int hi = qMin(static_cast<int>(points.size()) - 1, segment + 3);
        qreal minX = points[lo].pos.x(), maxX = minX;
        qreal minY = points[lo].pos.y(), maxY = minY;
        for (int k = lo + 1; k <= hi; ++k) {
            minX = qMin(minX, points[k].pos.x());
            maxX = qMax(maxX, points[k].pos.x());
            minY = qMin(minY, points[k].pos.y());
            maxY = qMax(maxY, points[k].pos.y());
        }
        return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }

    /**
     * @brief Serialize to JSON.
     * @return JSON object containing id, color, thickness, and points array.
     */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["color"] = color.name(QColor::HexArgb);
        obj["thickness"] = baseThickness;
        QJsonArray pointsArray;
        for (const auto& pt : points) {
            pointsArray.append(pt.toJson());
        }
        obj["points"] = pointsArray;
        return obj;
    }
    
    /**
     * @brief Deserialize from JSON.
     * @param obj JSON object containing stroke data.
     * @return VectorStroke with values from JSON (bounding box auto-calculated).
     */
    static VectorStroke fromJson(const QJsonObject& obj) {
        VectorStroke stroke;
        stroke.id = obj["id"].toString();
        stroke.color = QColor(obj["color"].toString());
        stroke.baseThickness = obj["thickness"].toDouble(5.0);
        
        // Generate UUID if missing (for backwards compatibility)
        if (stroke.id.isEmpty()) {
            stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        
        QJsonArray pointsArray = obj["points"].toArray();
        for (const auto& val : pointsArray) {
            stroke.points.append(StrokePoint::fromJson(val.toObject()));
        }
        stroke.updateBoundingBox();
        return stroke;
    }
    
private:
    /// Cuts leaving less path than this (document units) are discarded.
    static constexpr qreal MIN_FRAGMENT_LENGTH = 0.5;

    /**
     * @brief Linear interpolation of position and pressure along a segment.
     */
    static StrokePoint interpolatePoint(const StrokePoint& p, const StrokePoint& q, qreal t) {
        StrokePoint out;
        out.pos = p.pos + (q.pos - p.pos) * t;
        out.pressure = p.pressure + (q.pressure - p.pressure) * t;
        // Timestamps are only used for velocity heuristics at capture time; a
        // synthesised point has no meaningful one, so leave it at 0.
        return out;
    }
    
    /**
     * @brief Parameter ranges of segment p->q that lie outside an eraser capsule.
     *
     * Returns the surviving parts of one stroke segment, as [t0, t1] pairs in
     * increasing order with 0 <= t0 < t1 <= 1.
     */
    static QVector<QPair<qreal, qreal>> survivingIntervals(const QPointF& p, const QPointF& q,
                                                           const QPointF& a, const QPointF& b,
                                                           qreal radius) {
        const auto isErased = [&](const QPointF& pt) {
            return distanceToSegment(pt, a, b) < radius;
        };

        // distance() is convex along the segment, so its maximum is at an
        // endpoint. Both endpoints erased means the whole segment is erased.
        if (isErased(p) && isErased(q)) {
            return {};
        }
        return survivingIntervalsWhere(p, q, isErased);
    }

    /**
     * @brief Surviving parameter ranges of segment p->q, keeping where !isErased.
     *
     * Samples the segment and refines each crossing by bisection, so the cut
     * lands within a fraction of a unit of the real boundary rather than at the
     * nearest sample. `isErased` must be a point predicate; the lasso path
     * passes a polygon test, the disc eraser a distance test.
     */
    template <typename ErasedFn>
    static QVector<QPair<qreal, qreal>> survivingIntervalsWhere(const QPointF& p, const QPointF& q,
                                                                ErasedFn isErased) {
        // Sampling density: fine enough to catch a shallow graze, which can be
        // a very short stretch, without scanning a long segment point by point.
        const qreal len = QLineF(p, q).length();
        const int samples = qBound(2, static_cast<int>(std::ceil(len / 1.0)) + 1, 512);

        const QPointF delta = q - p;
        QVector<QPair<qreal, qreal>> result;
        qreal runStart = -1.0;   // Start of the current surviving run, or -1
        qreal prevT = 0.0;
        bool prevErased = isErased(p);
        if (!prevErased) {
            runStart = 0.0;
        }

        for (int i = 1; i <= samples; ++i) {
            const qreal t = static_cast<qreal>(i) / samples;
            const bool erased = isErased(p + delta * t);

            if (erased != prevErased) {
                // Refine the crossing: whichever side of the midpoint still
                // matches the previous sample holds the boundary.
                qreal lo = prevT;
                qreal hi = t;
                const bool loErased = prevErased;
                for (int it = 0; it < 24; ++it) {
                    const qreal mid = 0.5 * (lo + hi);
                    if (isErased(p + delta * mid) == loErased) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                const qreal boundary = 0.5 * (lo + hi);
                if (erased) {
                    // Entering the erased region: close the surviving run here.
                    if (runStart >= 0.0 && boundary > runStart) {
                        result.append({runStart, boundary});
                    }
                    runStart = -1.0;
                } else {
                    // Leaving the erased region: begin a new surviving run.
                    runStart = boundary;
                }
                prevErased = erased;
            }
            prevT = t;
        }

        if (runStart >= 0.0 && prevT > runStart) {
            result.append({runStart, 1.0});
        }
        return result;
    }
    
    /**
     * @brief Calculate distance from a point to a line segment.
     * @param p The point to test.
     * @param a Start of segment.
     * @param b End of segment.
     * @return Distance from p to the nearest point on segment ab.
     */
    static qreal distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
        QPointF ab = b - a;
        QPointF ap = p - a;
        qreal lenSq = ab.x() * ab.x() + ab.y() * ab.y();
        if (lenSq < 0.0001) return QLineF(p, a).length();
        qreal t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / lenSq, 1.0);
        QPointF closest = a + t * ab;
        return QLineF(p, closest).length();
    }
    
    /**
     * @brief Segment-to-segment closest-approach test, capped at `limit`.
     *
     * Used by the swept-eraser test: an eraser dragged along c->d touches a
     * stroke segment a->b exactly when the two segments come within `limit`
     * of each other. Endpoint-to-segment distances alone are not sufficient
     * (two crossing segments can have all four endpoints far apart), so this
     * also tests for a proper crossing.
     */
    static bool segmentsCloserThan(const QPointF& a, const QPointF& b,
                                   const QPointF& c, const QPointF& d,
                                   qreal limit) {
        if (distanceToSegment(a, c, d) < limit) return true;
        if (distanceToSegment(b, c, d) < limit) return true;
        if (distanceToSegment(c, a, b) < limit) return true;
        if (distanceToSegment(d, a, b) < limit) return true;
        // Proper crossing: endpoints can all be farther than `limit`.
        return segmentsIntersect(a, b, c, d);
    }
    
    /**
     * @brief Orientation-based proper/improper segment intersection test.
     */
    static bool segmentsIntersect(const QPointF& a, const QPointF& b,
                                  const QPointF& c, const QPointF& d) {
        auto cross = [](const QPointF& o, const QPointF& p, const QPointF& q) {
            return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
        };
        const qreal d1 = cross(c, d, a);
        const qreal d2 = cross(c, d, b);
        const qreal d3 = cross(a, b, c);
        const qreal d4 = cross(a, b, d);
        return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
    }
};

Q_DECLARE_METATYPE(VectorStroke)
Q_DECLARE_METATYPE(QVector<VectorStroke>)
