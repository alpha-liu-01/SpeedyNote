#pragma once

// ============================================================================
// ObjectConstraints - Page containment geometry for InsertedObjects
// ============================================================================
// Pure geometry helpers with no widget or document dependencies, so they can
// be unit tested directly.
//
// Invariant enforced in paged mode: an object's (unrotated) bounding rect is
// always fully contained in its owning page's rect. This keeps every object
// reachable by hit testing, which resolves the page under the cursor before
// searching that page's objects.
//
// Edgeless mode has no page edges, so callers skip these helpers there.
// ============================================================================

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtNumeric>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ObjectConstraints {

/**
 * @brief Clamp one axis so [pos, pos + extent] lies within [0, pageExtent].
 *
 * If the object is larger than the page on this axis containment is
 * impossible, so it is centered instead of producing an inverted bound.
 */
inline qreal clampAxis(qreal pos, qreal extent, qreal pageExtent)
{
    if (extent >= pageExtent) {
        return (pageExtent - extent) / 2.0;
    }
    return std::clamp(pos, 0.0, pageExtent - extent);
}

/**
 * @brief Clamp a page-local position so the object fits inside the page.
 * @param pos Page-local top-left of the object.
 * @param objSize Object bounding size.
 * @param pageSize Page size.
 */
inline QPointF clampPosition(const QPointF& pos, const QSizeF& objSize, const QSizeF& pageSize)
{
    if (!pageSize.isValid() || pageSize.isEmpty()) {
        return pos;
    }
    return QPointF(clampAxis(pos.x(), objSize.width(), pageSize.width()),
                   clampAxis(pos.y(), objSize.height(), pageSize.height()));
}

/**
 * @brief Clamp a page-local rect so it lies fully within [0, 0, pageSize].
 *
 * Only the position is adjusted; the size is preserved.
 */
inline QRectF clampToPage(const QRectF& objRect, const QSizeF& pageSize)
{
    return QRectF(clampPosition(objRect.topLeft(), objRect.size(), pageSize), objRect.size());
}

/**
 * @brief Offset needed to push a rect fully inside [0, 0, pageSize].
 *
 * Used for group moves, where the same correction must apply to every object
 * so their relative layout is preserved.
 */
inline QPointF correctionToPage(const QRectF& rect, const QSizeF& pageSize)
{
    return clampToPage(rect, pageSize).topLeft() - rect.topLeft();
}

/**
 * @brief Largest scale factor keeping a resized extent within the page.
 * @param origExtent Original extent (width or height) on the axis.
 * @param pageExtent Page extent on the same axis.
 *
 * Deliberately independent of where the object sits. Resizing grows an object
 * symmetrically about its centre, so limiting growth by the distance to the
 * nearest edge would stop an object flush against that edge from growing at
 * all. Callers cap the size here and clamp the resulting position separately,
 * which lets such an object grow away from the edge instead.
 *
 * Returns a large value for a degenerate extent so callers fall back to their
 * own maximum.
 */
inline qreal maxScaleToFitPage(qreal origExtent, qreal pageExtent)
{
    if (origExtent <= 0.001 || pageExtent <= 0.0) {
        return 1e9;
    }
    return pageExtent / origExtent;
}

/**
 * @brief Shrink a size to fit inside the page, preserving aspect ratio.
 * @return The size unchanged when it already fits.
 */
inline QSizeF shrinkToFit(const QSizeF& objSize, const QSizeF& pageSize)
{
    if (!pageSize.isValid() || pageSize.isEmpty() ||
        objSize.width() <= 0.0 || objSize.height() <= 0.0) {
        return objSize;
    }
    
    const qreal scale = std::min({1.0,
                                  pageSize.width() / objSize.width(),
                                  pageSize.height() / objSize.height()});
    if (scale >= 1.0) {
        return objSize;
    }
    return QSizeF(objSize.width() * scale, objSize.height() * scale);
}

/**
 * @brief Smallest whole-number divisor that fits a size within a fraction of a target.
 *
 * The divisor is always at least 1, so callers never upscale the source.
 * Invalid dimensions return 1 and leave sizing unchanged.
 */
inline int integerShrinkDivisor(const QSizeF& sourceSize,
                                const QSizeF& targetSize,
                                qreal maxFraction = 2.0 / 3.0)
{
    const auto finitePositive = [](qreal value) {
        return qIsFinite(value) && value > 0.0;
    };
    if (!finitePositive(sourceSize.width()) || !finitePositive(sourceSize.height())
        || !finitePositive(targetSize.width()) || !finitePositive(targetSize.height())
        || !finitePositive(maxFraction)) {
        return 1;
    }

    const qreal maxWidth = targetSize.width() * maxFraction;
    const qreal maxHeight = targetSize.height() * maxFraction;
    if (!finitePositive(maxWidth) || !finitePositive(maxHeight)) {
        return 1;
    }
    if (sourceSize.width() <= maxWidth && sourceSize.height() <= maxHeight) {
        return 1;
    }

    const qreal required = std::max(sourceSize.width() / maxWidth,
                                    sourceSize.height() / maxHeight);
    if (!qIsFinite(required)) {
        return 1;
    }
    const qreal rounded = std::ceil(required);
    if (rounded >= static_cast<qreal>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(1, static_cast<int>(rounded));
}

/**
 * @brief Uniformly shrink by the smallest integer divisor required to fit.
 */
inline QSizeF shrinkByIntegerDivisor(const QSizeF& sourceSize,
                                     const QSizeF& targetSize,
                                     qreal maxFraction = 2.0 / 3.0)
{
    const auto finitePositive = [](qreal value) {
        return qIsFinite(value) && value > 0.0;
    };
    const qreal maxWidth = targetSize.width() * maxFraction;
    const qreal maxHeight = targetSize.height() * maxFraction;
    if (!finitePositive(sourceSize.width()) || !finitePositive(sourceSize.height())
        || !finitePositive(maxWidth) || !finitePositive(maxHeight)) {
        return sourceSize;
    }
    if (sourceSize.width() <= maxWidth && sourceSize.height() <= maxHeight) {
        return sourceSize;
    }

    const int divisor = integerShrinkDivisor(sourceSize, targetSize, maxFraction);
    if (divisor > 1) {
        const QSizeF result(sourceSize.width() / divisor,
                            sourceSize.height() / divisor);
        if (result.width() <= maxWidth && result.height() <= maxHeight) {
            return result;
        }
    }

    // A finite QSizeF can require a divisor larger than int can represent.
    // Fractional fallback is preferable to violating the insertion bound.
    return shrinkToFit(sourceSize, QSizeF(maxWidth, maxHeight));
}

/**
 * @brief Convert image pixels to logical document units, then constrain them.
 *
 * Pixmap DPR describes source pixel density; display DPR describes how many
 * physical display pixels correspond to one document unit at 100% zoom.
 * Using the larger value avoids double-sizing DPR-tagged clipboard images while
 * preserving the existing 1:1 physical-pixel behavior for ordinary files.
 */
inline QSizeF freshImageInsertSize(const QSizeF& pixelSize,
                                   qreal imageDpr,
                                   qreal displayDpr,
                                   const QSizeF& targetSize)
{
    const qreal safeImageDpr = qIsFinite(imageDpr) && imageDpr > 0.0 ? imageDpr : 1.0;
    const qreal safeDisplayDpr =
        qIsFinite(displayDpr) && displayDpr > 0.0 ? displayDpr : 1.0;
    const qreal normalizationDpr = std::max(safeImageDpr, safeDisplayDpr);
    return shrinkByIntegerDivisor(
        QSizeF(pixelSize.width() / normalizationDpr,
               pixelSize.height() / normalizationDpr),
        targetSize);
}

}  // namespace ObjectConstraints
