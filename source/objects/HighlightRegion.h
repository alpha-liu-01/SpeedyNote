#pragma once

// ============================================================================
// HighlightRegion - the highlighted-text geometry owned by a LinkObject
// ============================================================================
// A highlight is not a stroke. It is an optional property of an annotation
// (LinkObject), which owns and renders it. That makes the half-deleted and
// half-erased states structurally impossible, and lets a highlight's colour or
// style change after the fact.
//
// Two representations are stored on purpose:
//
// - `rects` are the RENDERING truth. Display never depends on the PDF or the
//   OCR text cache being loaded, so a highlight survives a missing PDF, an OCR
//   re-run, or a cross-notebook page copy.
// - `sourceRange` is the EDIT affordance, used by Adjust mode to reconstitute a
//   text selection. It is explicitly allowed to be absent or stale; callers
//   must degrade to redefining the range by dragging when it is.
// ============================================================================

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>

/**
 * @brief Highlight geometry attached to a LinkObject.
 *
 * An empty region (no rects) means the annotation is a standalone link icon,
 * which is what the ObjectSelect tool inserts and what every LinkObject saved
 * before this type existed loads as. A non-empty region means a highlight.
 * Same class, same lifetime, same inspector.
 */
struct HighlightRegion {
    /**
     * @brief Alpha a highlight colour is stored at (50%).
     *
     * The marker tools carry their own private MARKER_OPACITY with the same
     * value, but neither is reachable from the objects or viewport layer, so
     * this is the canonical copy for anything writing `color`. Colours are
     * picked and displayed opaque and get this alpha applied on the way in,
     * matching how the marker presets behave.
     */
    static constexpr int DEFAULT_OPACITY = 128;

    /**
     * @brief Visual style of the highlight.
     *
     * Third mirror of the same 0..3 ordering as DocumentViewport::HighlightStyle
     * and HighlighterSubToolbar::HighlightStyle, bridged by static_cast at the
     * call sites. The objects layer cannot include DocumentViewport.h (the
     * dependency runs the other way), and the persisted integers must keep
     * matching across all three.
     */
    enum class Style {
        None = 0,             ///< No highlight drawn (select-text-only)
        Cover = 1,            ///< Marker fill over the whole line height
        Underline = 2,        ///< Thin solid line on the baseline
        DottedUnderline = 3,  ///< Row of dots on the baseline
    };

    /**
     * @brief Which text cache the character indices below refer to.
     *
     * Mirrors DocumentViewport::TextSelection::Source, same ordering.
     */
    enum class Source {
        Pdf = 0,  ///< PDF text layer
        Ocr = 1,  ///< OCR block cache
    };

    /**
     * @brief The text range this highlight was made from.
     *
     * Box indices address the per-page text cache rather than a global
     * character offset, because that is what the selection machinery actually
     * holds. They stay valid while the underlying text is unchanged, and go
     * stale when the PDF is replaced, OCR is re-run, or the annotation is
     * moved away from the text it marked.
     */
    struct SourceRange {
        /**
         * @brief Owning page, by UUID (never by index).
         *
         * Empty in edgeless mode, where the OCR cache is addressed by tile
         * rather than by page. Only the paged case needs a UUID, and only
         * paged pages are copyable between notebooks.
         */
        QString pageUuid;
        Source source = Source::Pdf;
        int startBoxIndex = -1;   ///< Index into the PDF text-box / OCR block cache
        int startCharIndex = -1;  ///< Character index within that box
        int endBoxIndex = -1;
        int endCharIndex = -1;
        /**
         * @brief Set when the range no longer describes the text under `rects`.
         *
         * Raised when the annotation is moved, or when a cross-notebook page
         * copy cannot resolve the page UUID. Adjust falls back to
         * drag-redefine.
         */
        bool stale = false;

        bool isValid() const {
            return startBoxIndex >= 0 && endBoxIndex >= 0;
        }

        /// True when the range can be used to reconstitute a text selection.
        bool isUsable() const { return isValid() && !stale; }

        QJsonObject toJson() const {
            QJsonObject obj;
            if (!pageUuid.isEmpty()) {
                obj["pageUuid"] = pageUuid;
            }
            obj["source"] = static_cast<int>(source);
            obj["startBox"] = startBoxIndex;
            obj["startChar"] = startCharIndex;
            obj["endBox"] = endBoxIndex;
            obj["endChar"] = endCharIndex;
            if (stale) {
                obj["stale"] = true;
            }
            return obj;
        }

        static SourceRange fromJson(const QJsonObject& obj) {
            SourceRange range;
            range.pageUuid = obj["pageUuid"].toString();
            const int src = obj["source"].toInt(static_cast<int>(Source::Pdf));
            range.source = (src == static_cast<int>(Source::Ocr)) ? Source::Ocr
                                                                  : Source::Pdf;
            range.startBoxIndex = obj["startBox"].toInt(-1);
            range.startCharIndex = obj["startChar"].toInt(-1);
            range.endBoxIndex = obj["endBox"].toInt(-1);
            range.endCharIndex = obj["endChar"].toInt(-1);
            range.stale = obj["stale"].toBool(false);
            return range;
        }
    };

    Style style = Style::None;
    QColor color;

    /**
     * @brief Per-line highlight rectangles, in OBJECT-LOCAL coordinates.
     *
     * Relative to the owning object's `position`, like every other
     * InsertedObject's geometry. Moving the annotation is then a single
     * `position` change with no per-rect fixups, and the object's `size` is the
     * union of these rects so the edgeless tile margin
     * (Document::maxObjectExtent) covers a highlight that spans several tiles.
     */
    QVector<QRectF> rects;

    SourceRange sourceRange;

    /// True when this annotation carries no highlight (standalone link icon).
    bool isEmpty() const { return rects.isEmpty(); }

    /**
     * @brief Union of the rects, in object-local coordinates.
     * @return Null rect when the region is empty.
     */
    QRectF boundingRect() const {
        QRectF bounds;
        for (const QRectF& r : rects) {
            bounds = bounds.isNull() ? r : bounds.united(r);
        }
        return bounds;
    }

    /// True when @p localPoint (object-local) lies inside any rect.
    bool containsLocalPoint(const QPointF& localPoint) const {
        for (const QRectF& r : rects) {
            if (r.contains(localPoint)) return true;
        }
        return false;
    }

    /// Shift every rect by @p delta, keeping them relative to a moved origin.
    void translateRects(const QPointF& delta) {
        for (QRectF& r : rects) {
            r.translate(delta);
        }
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["style"] = static_cast<int>(style);
        obj["color"] = color.isValid() ? color.name(QColor::HexArgb) : QString();

        QJsonArray rectArray;
        for (const QRectF& r : rects) {
            QJsonObject rectObj;
            rectObj["x"] = r.x();
            rectObj["y"] = r.y();
            rectObj["w"] = r.width();
            rectObj["h"] = r.height();
            rectArray.append(rectObj);
        }
        obj["rects"] = rectArray;

        if (sourceRange.isValid()) {
            obj["sourceRange"] = sourceRange.toJson();
        }
        return obj;
    }

    static HighlightRegion fromJson(const QJsonObject& obj) {
        HighlightRegion region;

        const int styleValue = obj["style"].toInt(static_cast<int>(Style::None));
        region.style = (styleValue >= static_cast<int>(Style::None)
                        && styleValue <= static_cast<int>(Style::DottedUnderline))
                           ? static_cast<Style>(styleValue)
                           : Style::None;

        region.color = QColor(obj["color"].toString());

        const QJsonArray rectArray = obj["rects"].toArray();
        region.rects.reserve(rectArray.size());
        for (const QJsonValue& v : rectArray) {
            const QJsonObject rectObj = v.toObject();
            region.rects.append(QRectF(rectObj["x"].toDouble(),
                                       rectObj["y"].toDouble(),
                                       rectObj["w"].toDouble(),
                                       rectObj["h"].toDouble()));
        }

        if (obj.contains("sourceRange")) {
            region.sourceRange = SourceRange::fromJson(obj["sourceRange"].toObject());
        }
        return region;
    }
};
