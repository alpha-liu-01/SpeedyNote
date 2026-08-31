#pragma once

#include "HighlightRegion.h"
#include "InsertedObject.h"
#include <QColor>
#include <QUrl>
#include <QPainter>
#include <QPixmap>
#include <memory>

/**
 * @brief A single link slot in a LinkObject.
 * 
 * Each LinkObject has 3 slots that can each hold a different type of link.
 */
struct LinkSlot {
    enum class Type {
        Empty,      ///< Slot is unused
        Position,   ///< Links to a page position (pageUuid + coordinates) or edgeless position
        Url,        ///< Links to an external URL
        Markdown    ///< Links to a markdown note (by ID)
    };
    
    Type type = Type::Empty;
    
    // Position link data (paged mode)
    QString targetPageUuid;
    QPointF targetPosition;      ///< Page-local coordinates for paged, document coordinates for edgeless
    
    // Position link data (edgeless mode)
    bool isEdgelessTarget = false;  ///< True if linking to an edgeless document position
    int edgelessTileX = 0;          ///< Target tile X coordinate (for edgeless)
    int edgelessTileY = 0;          ///< Target tile Y coordinate (for edgeless)
    
    /**
     * @brief The LinkObject this position link points at, when it points at one.
     *
     * Navigation runs off the coordinates above; this is the correction applied
     * on arrival, so a link keeps working after its target is dragged. Empty for
     * a link to a bare coordinate, which is every position link written before
     * pairing existed and every back-link made by @ref LinkObject::cloneWithBackLink.
     */
    QString targetObjectId;
    
    /**
     * @brief Which of the target's slots is this link's partner, or -1.
     *
     * A pairing spends a slot at each end, and clearing one end releases the
     * other -- which needs to name the far slot, not just the far object. The
     * object id alone is ambiguous once the same two annotations are paired
     * twice, since then several of the target's slots point back here.
     *
     * -1 means "no known partner": a one-way link, or one whose far end has
     * already been cleared.
     */
    int targetSlotIndex = -1;
    
    // URL link data
    QString url;
    
    // Markdown link data
    QString markdownNoteId;
    
    // Serialization
    QJsonObject toJson() const;
    static LinkSlot fromJson(const QJsonObject& obj);
    
    bool isEmpty() const { return type == Type::Empty; }
    void clear() { *this = LinkSlot(); }
};

/**
 * @brief An annotation: 3 configurable link slots plus an optional highlight.
 *
 * "Annotation" is the user-facing word for this class. A LinkObject whose
 * @ref region is empty is a standalone link icon; one whose region is non-empty
 * is a highlight. Same class, same lifetime, same controls -- "standalone" is
 * not a special case.
 *
 * LinkObject is created:
 * - Automatically when highlighting PDF or OCR text (description = extracted
 *   text, region = the selected line rects)
 * - Manually via ObjectSelect tool (description empty or user-entered, region
 *   empty)
 *
 * Each slot can independently link to:
 * - A position in the document (page + coordinates)
 * - An external URL
 * - A markdown note
 *
 * Geometry contract:
 * - Empty region: `position` is the icon's top-left and `size` is 24x24, which
 *   is how every LinkObject saved before regions existed still behaves.
 * - Non-empty region: `position`/`size` are the region's bounding box, and the
 *   icon becomes a badge derived from `position` (see @ref iconRect).
 */
class LinkObject : public InsertedObject {
public:
    static constexpr int SLOT_COUNT = 3;
    static constexpr qreal ICON_SIZE = 24.0;  ///< Icon size at 100% zoom
    static constexpr qreal ICON_GAP = 4.0;    ///< Padding between badge and region
    
    // Content
    QString description;    ///< Extracted text or user description
    QColor iconColor = QColor(100, 100, 100, 180);  ///< Icon tint color
    
    /**
     * @brief True when the user typed this description themselves.
     *
     * A highlight's description is auto-filled with the selected text, so mere
     * non-emptiness cannot distinguish "worth opening" from "auto-derived".
     * This flag drives both the icon badge (@ref shouldShowIcon) and the
     * scroll-bar marker filter.
     */
    bool descriptionUserEdited = false;
    
    /// Optional highlight geometry. Empty for a standalone link icon.
    HighlightRegion region;
    
    // The 3 link slots (named linkSlots to avoid Qt 'slots' keyword conflict)
    LinkSlot linkSlots[SLOT_COUNT];
    
    // Constructor
    LinkObject();
    
    // InsertedObject interface
    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("link"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;
    bool containsPoint(const QPointF& pt) const override;
    
    // LinkObject-specific methods
    int filledSlotCount() const;
    bool hasEmptySlot() const;
    int firstEmptySlotIndex() const;
    
    // ===== Highlight region =====
    
    /**
     * @brief Adopt a highlight region given in page (or tile) coordinates.
     *
     * The one well-defined rebase operation: `position` becomes the rects'
     * bounding-box top-left, `size` becomes its size, and the rects are stored
     * relative to it. Passing an empty list clears the region and restores the
     * icon-sized bounds.
     *
     * @param pageRects Per-line rects in the same space as `position`.
     */
    void setRegionFromPageRects(const QVector<QRectF>& pageRects);
    
    /// The region's rects translated back into page (or tile) coordinates.
    QVector<QRectF> regionRectsInPageSpace() const;
    
    /**
     * @brief Bounds of the icon badge, in page (or tile) coordinates.
     *
     * With an empty region `position` *is* the icon anchor. With a region the
     * badge sits in the margin to the left of the highlight, outside
     * `boundingRect()`, so the selection chrome and the floating control bar
     * anchor to the mark itself rather than to the badge.
     */
    QRectF iconRect() const;
    
    /**
     * @brief Whether the icon badge is drawn.
     *
     * A highlight with no attachments is just a highlight, so the badge only
     * appears when there is something worth opening. An empty-region annotation
     * always shows it: the badge is its only handle, and hiding it would strand
     * the annotation in the file with no way to reach it.
     */
    bool shouldShowIcon() const;
    
    // Copy with back-link (paged mode)
    std::unique_ptr<LinkObject> cloneWithBackLink(const QString& sourcePageUuid) const;
    
    // Copy with back-link (edgeless mode)
    std::unique_ptr<LinkObject> cloneWithBackLinkEdgeless(int tileX, int tileY, const QPointF& docPosition) const;
    
private:
    /// Paint the highlight rects for the current style.
    void renderRegion(QPainter& painter, qreal zoom) const;
    
    // Icon rendering (lazy-loaded to avoid QPixmap before QApplication)
    static const QPixmap& iconPixmap();
    void ensureIconLoaded() const;  // Kept for API compatibility, now empty
    QPixmap tintedIcon(const QColor& color, qreal size) const;
    
    // Render cache to avoid recreating tinted icon every frame
    mutable QPixmap m_cachedTintedIcon;
    mutable QColor m_cachedColor;
    mutable qreal m_cachedSize = 0.0;
};

