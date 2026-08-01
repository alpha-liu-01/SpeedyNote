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
#include <algorithm>

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
 * @brief Largest scale factor keeping a center-fixed resize inside the page.
 * @param center Page-local center of the object (fixed during resize).
 * @param origExtent Original extent (width or height) on the axis.
 * @param pageExtent Page extent on the same axis.
 *
 * Resizing grows the object symmetrically around its center, so the usable
 * half-extent is the distance from the center to the nearest page edge.
 * Returns a large value when the extent is degenerate so callers fall back to
 * their own maximum.
 */
inline qreal maxScaleForCenterFixedResize(qreal center, qreal origExtent, qreal pageExtent)
{
    if (origExtent <= 0.001 || pageExtent <= 0.0) {
        return 1e9;
    }
    const qreal halfLimit = std::max(0.0, std::min(center, pageExtent - center));
    return (2.0 * halfLimit) / origExtent;
}

/**
 * @brief Scale that shrinks a size to fit inside the page, preserving aspect.
 * @return 1.0 when the size already fits, otherwise the shrink factor (< 1).
 */
inline qreal shrinkToFitScale(const QSizeF& objSize, const QSizeF& pageSize)
{
    if (!pageSize.isValid() || pageSize.isEmpty() ||
        objSize.width() <= 0.0 || objSize.height() <= 0.0) {
        return 1.0;
    }
    const qreal sx = pageSize.width() / objSize.width();
    const qreal sy = pageSize.height() / objSize.height();
    return std::min({1.0, sx, sy});
}

/**
 * @brief Shrink a size to fit inside the page, preserving aspect ratio.
 */
inline QSizeF shrinkToFit(const QSizeF& objSize, const QSizeF& pageSize)
{
    const qreal scale = shrinkToFitScale(objSize, pageSize);
    if (scale >= 1.0) {
        return objSize;
    }
    return QSizeF(objSize.width() * scale, objSize.height() * scale);
}

}  // namespace ObjectConstraints
