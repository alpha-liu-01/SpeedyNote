#pragma once

// ============================================================================
// LinkOutlineEntry - Lightweight POD describing a LinkObject that has markdown
//                    notes attached to it.
// ============================================================================
// Built once by Document::enumerateLinkOutline() and consumed by
// NotesTreePanel to populate the 3-level right-sidebar tree.
//
// The outline is intentionally small: it contains only the fields required to
// render the tree rows (description, color, slot ids) and to route navigation
// (page index / tile coordinates / doc-space position).  It never reads any
// .md file; markdown previews and bodies are loaded lazily by the panel.
// ============================================================================

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

/**
 * @brief A single markdown slot inside a LinkObject.
 */
struct LinkOutlineSlot {
    int     slotIndex = 0;   ///< 0..2 (LinkObject::SLOT_COUNT - 1)
    QString noteId;          ///< LinkSlot::markdownNoteId (filename without .md)
};

/**
 * @brief A LinkObject with at least one markdown slot, together with the
 *        positional metadata required to group it in the right-sidebar tree.
 */
struct LinkOutlineEntry {
    // Identity / display
    QString linkObjectId;            ///< Stable id (matches InsertedObject::id)
    QString description;             ///< LinkObject::description (L2 label)
    QColor  iconColor;               ///< LinkObject::iconColor (L2/L3 stripe)

    // Position (paged mode)
    int     pageIndex = -1;          ///< 0-based page index, or -1 for edgeless

    // Position (edgeless mode)
    int     tileX = 0;               ///< Tile X (edgeless only)
    int     tileY = 0;               ///< Tile Y (edgeless only)

    /// Document-space position: paged = in-page coords; edgeless = tileOrigin + object.position
    QPointF docPos;

    // Slots that point to markdown notes (size >= 1).
    QVector<LinkOutlineSlot> markdownSlots;
};
