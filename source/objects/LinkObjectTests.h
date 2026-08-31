#pragma once

// ============================================================================
// LinkObjectTests - Unit tests for the LinkObject class
// ============================================================================
// Part of Phase C.1: LinkObject Foundation
// 
// Tests:
// - LinkObject creation and properties
// - LinkSlot serialization round-trip
// - LinkObject serialization round-trip
// - containsPoint() hit testing
// - Slot management methods
// - cloneWithBackLink() functionality
// - Factory creates LinkObject from JSON
// - HighlightRegion rebasing, badge rules, serialization and legacy loading
// ============================================================================

#include "LinkObject.h"
#include "InsertedObject.h"
#include <QDebug>
#include <QJsonDocument>
#include <cassert>

namespace LinkObjectTests {

/**
 * @brief Test LinkObject creation and default properties.
 */
inline bool testCreation()
{
    qDebug() << "=== Test: LinkObject Creation ===";
    
    bool success = true;
    
    LinkObject link;
    
    // Check default size (ICON_SIZE x ICON_SIZE)
    if (link.size != QSizeF(LinkObject::ICON_SIZE, LinkObject::ICON_SIZE)) {
        qDebug() << "FAIL: Default size should be" << LinkObject::ICON_SIZE << "x" << LinkObject::ICON_SIZE;
        qDebug() << "  Got:" << link.size;
        success = false;
    }
    
    // Check default icon color
    if (link.iconColor != QColor(100, 100, 100, 180)) {
        qDebug() << "FAIL: Default iconColor mismatch";
        success = false;
    }
    
    // Check type string
    if (link.type() != "link") {
        qDebug() << "FAIL: type() should return 'link'";
        success = false;
    }
    
    // Check all slots are empty
    for (int i = 0; i < LinkObject::SLOT_COUNT; i++) {
        if (!link.linkSlots[i].isEmpty()) {
            qDebug() << "FAIL: Slot" << i << "should be empty by default";
            success = false;
        }
    }
    
    // Check slot count methods
    if (link.filledSlotCount() != 0) {
        qDebug() << "FAIL: filledSlotCount() should be 0";
        success = false;
    }
    
    if (!link.hasEmptySlot()) {
        qDebug() << "FAIL: hasEmptySlot() should be true";
        success = false;
    }
    
    if (link.firstEmptySlotIndex() != 0) {
        qDebug() << "FAIL: firstEmptySlotIndex() should be 0";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: LinkObject creation successful!";
    }
    
    return success;
}

/**
 * @brief Test LinkSlot serialization round-trip.
 */
inline bool testLinkSlotSerialization()
{
    qDebug() << "=== Test: LinkSlot Serialization Round-Trip ===";
    
    bool success = true;
    
    // Test Empty slot
    {
        LinkSlot empty;
        QJsonObject json = empty.toJson();
        LinkSlot restored = LinkSlot::fromJson(json);
        
        if (restored.type != LinkSlot::Type::Empty) {
            qDebug() << "FAIL: Empty slot type not preserved";
            success = false;
        }
    }
    
    // Test Position slot
    {
        LinkSlot pos;
        pos.type = LinkSlot::Type::Position;
        pos.targetPageUuid = "abc123-def456";
        pos.targetPosition = QPointF(150.5, 200.25);
        
        QJsonObject json = pos.toJson();
        LinkSlot restored = LinkSlot::fromJson(json);
        
        if (restored.type != LinkSlot::Type::Position) {
            qDebug() << "FAIL: Position slot type not preserved";
            success = false;
        }
        if (restored.targetPageUuid != "abc123-def456") {
            qDebug() << "FAIL: Position slot pageUuid not preserved";
            success = false;
        }
        if (restored.targetPosition != QPointF(150.5, 200.25)) {
            qDebug() << "FAIL: Position slot targetPosition not preserved";
            qDebug() << "  Expected:" << QPointF(150.5, 200.25);
            qDebug() << "  Got:" << restored.targetPosition;
            success = false;
        }
    }
    
    // Test URL slot
    {
        LinkSlot url;
        url.type = LinkSlot::Type::Url;
        url.url = "https://example.com/page?param=value";
        
        QJsonObject json = url.toJson();
        LinkSlot restored = LinkSlot::fromJson(json);
        
        if (restored.type != LinkSlot::Type::Url) {
            qDebug() << "FAIL: URL slot type not preserved";
            success = false;
        }
        if (restored.url != "https://example.com/page?param=value") {
            qDebug() << "FAIL: URL slot url not preserved";
            success = false;
        }
    }
    
    // Test Markdown slot
    {
        LinkSlot md;
        md.type = LinkSlot::Type::Markdown;
        md.markdownNoteId = "note-789xyz";
        
        QJsonObject json = md.toJson();
        LinkSlot restored = LinkSlot::fromJson(json);
        
        if (restored.type != LinkSlot::Type::Markdown) {
            qDebug() << "FAIL: Markdown slot type not preserved";
            success = false;
        }
        if (restored.markdownNoteId != "note-789xyz") {
            qDebug() << "FAIL: Markdown slot noteId not preserved";
            success = false;
        }
    }
    
    if (success) {
        qDebug() << "PASS: LinkSlot serialization round-trip successful!";
    }
    
    return success;
}

/**
 * @brief Test LinkObject serialization round-trip.
 */
inline bool testLinkObjectSerialization()
{
    qDebug() << "=== Test: LinkObject Serialization Round-Trip ===";
    
    bool success = true;
    
    // Create a LinkObject with content
    auto link = std::make_unique<LinkObject>();
    link->id = "link-001";
    link->position = QPointF(100.5, 200.75);
    link->description = "This is a test description with special chars: äöü";
    link->iconColor = QColor(255, 128, 64, 200);
    link->zOrder = 5;
    link->layerAffinity = 2;
    
    // Fill slots
    link->linkSlots[0].type = LinkSlot::Type::Position;
    link->linkSlots[0].targetPageUuid = "page-uuid-123";
    link->linkSlots[0].targetPosition = QPointF(50, 75);
    
    link->linkSlots[1].type = LinkSlot::Type::Url;
    link->linkSlots[1].url = "https://test.com";
    
    // Slot 2 stays empty
    
    // Serialize
    QJsonObject json = link->toJson();
    
    // Debug output
    QJsonDocument doc(json);
    qDebug() << "Serialized JSON:" << doc.toJson(QJsonDocument::Compact).left(300) << "...";
    
    // Deserialize
    auto restored = std::make_unique<LinkObject>();
    restored->loadFromJson(json);
    
    // Verify
    if (restored->id != "link-001") {
        qDebug() << "FAIL: id not preserved";
        success = false;
    }
    
    if (restored->position != QPointF(100.5, 200.75)) {
        qDebug() << "FAIL: position not preserved";
        success = false;
    }
    
    if (restored->description != "This is a test description with special chars: äöü") {
        qDebug() << "FAIL: description not preserved";
        success = false;
    }
    
    if (restored->iconColor != QColor(255, 128, 64, 200)) {
        qDebug() << "FAIL: iconColor not preserved";
        qDebug() << "  Expected:" << QColor(255, 128, 64, 200);
        qDebug() << "  Got:" << restored->iconColor;
        success = false;
    }
    
    if (restored->zOrder != 5) {
        qDebug() << "FAIL: zOrder not preserved";
        success = false;
    }
    
    if (restored->layerAffinity != 2) {
        qDebug() << "FAIL: layerAffinity not preserved";
        success = false;
    }
    
    // Check slots
    if (restored->linkSlots[0].type != LinkSlot::Type::Position) {
        qDebug() << "FAIL: slot 0 type not preserved";
        success = false;
    }
    if (restored->linkSlots[0].targetPageUuid != "page-uuid-123") {
        qDebug() << "FAIL: slot 0 pageUuid not preserved";
        success = false;
    }
    
    if (restored->linkSlots[1].type != LinkSlot::Type::Url) {
        qDebug() << "FAIL: slot 1 type not preserved";
        success = false;
    }
    if (restored->linkSlots[1].url != "https://test.com") {
        qDebug() << "FAIL: slot 1 url not preserved";
        success = false;
    }
    
    if (restored->linkSlots[2].type != LinkSlot::Type::Empty) {
        qDebug() << "FAIL: slot 2 should be empty";
        success = false;
    }
    
    // Check slot count methods on restored object
    if (restored->filledSlotCount() != 2) {
        qDebug() << "FAIL: filledSlotCount() should be 2";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: LinkObject serialization round-trip successful!";
    }
    
    return success;
}

/**
 * @brief Test containsPoint() hit testing.
 */
inline bool testContainsPoint()
{
    qDebug() << "=== Test: containsPoint() Hit Testing ===";
    
    bool success = true;
    
    LinkObject link;
    link.position = QPointF(100, 200);
    // Size is ICON_SIZE (24x24) by default
    
    // Point inside icon bounds
    if (!link.containsPoint(QPointF(112, 212))) {
        qDebug() << "FAIL: Point (112, 212) should be inside icon";
        success = false;
    }
    
    // Point at top-left corner
    if (!link.containsPoint(QPointF(100, 200))) {
        qDebug() << "FAIL: Point (100, 200) should be inside icon (top-left)";
        success = false;
    }
    
    // Point at bottom-right corner (just inside)
    if (!link.containsPoint(QPointF(123, 223))) {
        qDebug() << "FAIL: Point (123, 223) should be inside icon (bottom-right)";
        success = false;
    }
    
    // Point outside (left)
    if (link.containsPoint(QPointF(99, 212))) {
        qDebug() << "FAIL: Point (99, 212) should be outside icon";
        success = false;
    }
    
    // Point outside (above)
    if (link.containsPoint(QPointF(112, 199))) {
        qDebug() << "FAIL: Point (112, 199) should be outside icon";
        success = false;
    }
    
    // Point outside (right)
    if (link.containsPoint(QPointF(125, 212))) {
        qDebug() << "FAIL: Point (125, 212) should be outside icon";
        success = false;
    }
    
    // Point outside (below)
    if (link.containsPoint(QPointF(112, 225))) {
        qDebug() << "FAIL: Point (112, 225) should be outside icon";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: containsPoint() hit testing successful!";
    }
    
    return success;
}

/**
 * @brief Test cloneWithBackLink() functionality.
 */
inline bool testCloneWithBackLink()
{
    qDebug() << "=== Test: cloneWithBackLink() ===";
    
    bool success = true;
    
    // Create original LinkObject
    LinkObject original;
    original.position = QPointF(300, 400);
    original.description = "Original description";
    original.iconColor = QColor(255, 0, 0, 150);
    
    // Clone with back-link
    QString sourcePageUuid = "source-page-uuid-abc";
    auto clone = original.cloneWithBackLink(sourcePageUuid);
    
    // Verify description and color are copied
    if (clone->description != "Original description") {
        qDebug() << "FAIL: description not copied";
        success = false;
    }
    
    if (clone->iconColor != QColor(255, 0, 0, 150)) {
        qDebug() << "FAIL: iconColor not copied";
        success = false;
    }
    
    // Verify slot 0 has back-link
    if (clone->linkSlots[0].type != LinkSlot::Type::Position) {
        qDebug() << "FAIL: slot 0 should be Position type";
        success = false;
    }
    
    if (clone->linkSlots[0].targetPageUuid != sourcePageUuid) {
        qDebug() << "FAIL: slot 0 should have sourcePageUuid";
        success = false;
    }
    
    if (clone->linkSlots[0].targetPosition != QPointF(300, 400)) {
        qDebug() << "FAIL: slot 0 should have original position";
        qDebug() << "  Expected:" << QPointF(300, 400);
        qDebug() << "  Got:" << clone->linkSlots[0].targetPosition;
        success = false;
    }
    
    // Verify other slots are empty
    if (!clone->linkSlots[1].isEmpty()) {
        qDebug() << "FAIL: slot 1 should be empty";
        success = false;
    }
    
    if (!clone->linkSlots[2].isEmpty()) {
        qDebug() << "FAIL: slot 2 should be empty";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: cloneWithBackLink() successful!";
    }
    
    return success;
}

/**
 * @brief Test factory creates LinkObject from JSON.
 */
inline bool testFactoryCreation()
{
    qDebug() << "=== Test: Factory Creates LinkObject from JSON ===";
    
    bool success = true;
    
    // Create JSON for a LinkObject
    QJsonObject json;
    json["type"] = "link";
    json["id"] = "factory-test-link";
    json["x"] = 50.0;
    json["y"] = 75.0;
    json["width"] = 24.0;
    json["height"] = 24.0;
    json["zOrder"] = 3;
    json["description"] = "Factory created";
    json["iconColor"] = "#c8ff8040";  // ARGB hex
    
    QJsonArray slotsArray;
    QJsonObject slot0;
    slot0["type"] = "url";
    slot0["url"] = "https://factory.test";
    slotsArray.append(slot0);
    slotsArray.append(QJsonObject{{"type", "empty"}});
    slotsArray.append(QJsonObject{{"type", "empty"}});
    json["slots"] = slotsArray;
    
    // Use factory
    std::unique_ptr<InsertedObject> obj = InsertedObject::fromJson(json);
    
    if (!obj) {
        qDebug() << "FAIL: Factory returned nullptr";
        return false;
    }
    
    // Verify it's a LinkObject
    if (obj->type() != "link") {
        qDebug() << "FAIL: Factory should create LinkObject";
        success = false;
    }
    
    // Cast and verify properties
    LinkObject* link = dynamic_cast<LinkObject*>(obj.get());
    if (!link) {
        qDebug() << "FAIL: dynamic_cast to LinkObject failed";
        return false;
    }
    
    if (link->id != "factory-test-link") {
        qDebug() << "FAIL: id mismatch";
        success = false;
    }
    
    if (link->position != QPointF(50, 75)) {
        qDebug() << "FAIL: position mismatch";
        success = false;
    }
    
    if (link->description != "Factory created") {
        qDebug() << "FAIL: description mismatch";
        success = false;
    }
    
    if (link->linkSlots[0].type != LinkSlot::Type::Url) {
        qDebug() << "FAIL: slot 0 type mismatch";
        success = false;
    }
    
    if (link->linkSlots[0].url != "https://factory.test") {
        qDebug() << "FAIL: slot 0 url mismatch";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: Factory creates LinkObject from JSON successful!";
    }
    
    return success;
}

/**
 * @brief Test the highlight region: rebasing, bounds, serialization, hit test.
 */
inline bool testHighlightRegion()
{
    qDebug() << "=== Test: HighlightRegion ===";
    
    bool success = true;
    
    const QVector<QRectF> pageRects = {
        QRectF(100, 400, 200, 14),
        QRectF(80,  420, 220, 14),
        QRectF(80,  440, 150, 14)
    };
    
    // ----- Rebasing: position/size become the bounding box -----
    {
        LinkObject link;
        link.setRegionFromPageRects(pageRects);
        
        // Bounding box spans x 80..300, y 400..454.
        if (link.position != QPointF(80, 400)) {
            qDebug() << "FAIL: position should be the region bbox top-left, got"
                     << link.position;
            success = false;
        }
        if (link.size != QSizeF(220, 54)) {
            qDebug() << "FAIL: size should be the region bbox size, got" << link.size;
            success = false;
        }
        if (link.region.rects.size() != 3) {
            qDebug() << "FAIL: expected 3 region rects, got" << link.region.rects.size();
            success = false;
        }
        // Object-local: the first line sits 20pt right of and level with the origin.
        if (link.region.rects[0] != QRectF(20, 0, 200, 14)) {
            qDebug() << "FAIL: region rects are not object-local, got"
                     << link.region.rects[0];
            success = false;
        }
        if (link.regionRectsInPageSpace() != pageRects) {
            qDebug() << "FAIL: regionRectsInPageSpace() did not round-trip";
            success = false;
        }
        
        // Moving the annotation is a single position change with no fixups.
        link.moveBy(QPointF(15, -25));
        const QVector<QRectF> moved = link.regionRectsInPageSpace();
        if (moved[0] != pageRects[0].translated(QPointF(15, -25))) {
            qDebug() << "FAIL: moving the object did not carry the region";
            success = false;
        }
        
        // Clearing restores icon-sized bounds.
        link.setRegionFromPageRects({});
        if (!link.region.isEmpty()
            || link.size != QSizeF(LinkObject::ICON_SIZE, LinkObject::ICON_SIZE)) {
            qDebug() << "FAIL: clearing the region did not restore icon bounds";
            success = false;
        }
    }
    
    // ----- containsPoint(): region body plus the badge -----
    {
        LinkObject link;
        link.setRegionFromPageRects(pageRects);
        link.descriptionUserEdited = true;   // badge shown
        
        if (!link.containsPoint(QPointF(150, 405))) {
            qDebug() << "FAIL: a point on the first highlighted line should hit";
            success = false;
        }
        if (!link.containsPoint(QPointF(100, 445))) {
            qDebug() << "FAIL: a point on the third highlighted line should hit";
            success = false;
        }
        // Inside the bounding box but in the gap between lines 1 and 2.
        if (link.containsPoint(QPointF(90, 416))) {
            qDebug() << "FAIL: the gap between lines should not hit";
            success = false;
        }
        // Right of the short third line, still inside the bounding box.
        if (link.containsPoint(QPointF(280, 445))) {
            qDebug() << "FAIL: bounding-box filler right of a short line should not hit";
            success = false;
        }
        // The badge sits in the margin, outside boundingRect().
        const QRectF badge = link.iconRect();
        if (badge.topLeft()
            != QPointF(80 - LinkObject::ICON_SIZE - LinkObject::ICON_GAP, 400)) {
            qDebug() << "FAIL: badge is not in the left margin, got" << badge;
            success = false;
        }
        if (!link.containsPoint(badge.center())) {
            qDebug() << "FAIL: the badge should stay hit-testable";
            success = false;
        }
        
        // Hidden badge is not a hit target, but the mark still is.
        link.descriptionUserEdited = false;
        if (link.shouldShowIcon()) {
            qDebug() << "FAIL: a highlight with nothing worth opening should hide its badge";
            success = false;
        }
        if (link.containsPoint(badge.center())) {
            qDebug() << "FAIL: a hidden badge should not be hit-testable";
            success = false;
        }
        if (!link.containsPoint(QPointF(150, 405))) {
            qDebug() << "FAIL: hiding the badge must not affect the mark";
            success = false;
        }
    }
    
    // ----- Badge visibility rules -----
    {
        // Empty region: always shown, since the badge is the only handle.
        LinkObject icon;
        if (!icon.shouldShowIcon()) {
            qDebug() << "FAIL: an empty-region annotation must always show its badge";
            success = false;
        }
        icon.description = QStringLiteral("auto");
        if (!icon.shouldShowIcon()) {
            qDebug() << "FAIL: an empty-region annotation must always show its badge";
            success = false;
        }
        if (icon.iconRect().topLeft() != icon.position) {
            qDebug() << "FAIL: with no region, position is the badge anchor";
            success = false;
        }
        
        // Region present: a filled slot is enough on its own.
        LinkObject highlight;
        highlight.setRegionFromPageRects(pageRects);
        if (highlight.shouldShowIcon()) {
            qDebug() << "FAIL: a bare highlight should hide its badge";
            success = false;
        }
        highlight.linkSlots[1].type = LinkSlot::Type::Url;
        highlight.linkSlots[1].url = QStringLiteral("https://example.com");
        if (!highlight.shouldShowIcon()) {
            qDebug() << "FAIL: a filled slot should reveal the badge";
            success = false;
        }
    }
    
    // ----- Serialization round-trip -----
    {
        LinkObject link;
        link.setRegionFromPageRects(pageRects);
        link.region.style = HighlightRegion::Style::DottedUnderline;
        link.region.color = QColor(255, 255, 0, 128);
        link.region.sourceRange.pageUuid = QStringLiteral("page-uuid-xyz");
        link.region.sourceRange.source = HighlightRegion::Source::Ocr;
        link.region.sourceRange.startBoxIndex = 2;
        link.region.sourceRange.startCharIndex = 5;
        link.region.sourceRange.endBoxIndex = 4;
        link.region.sourceRange.endCharIndex = 11;
        link.descriptionUserEdited = true;
        
        LinkObject restored;
        restored.loadFromJson(link.toJson());
        
        if (restored.region.rects != link.region.rects) {
            qDebug() << "FAIL: region rects not preserved";
            success = false;
        }
        if (restored.region.style != HighlightRegion::Style::DottedUnderline) {
            qDebug() << "FAIL: region style not preserved";
            success = false;
        }
        if (restored.region.color != QColor(255, 255, 0, 128)) {
            qDebug() << "FAIL: region color not preserved, got" << restored.region.color;
            success = false;
        }
        if (!restored.descriptionUserEdited) {
            qDebug() << "FAIL: descriptionUserEdited not preserved";
            success = false;
        }
        const HighlightRegion::SourceRange& r = restored.region.sourceRange;
        if (r.pageUuid != QStringLiteral("page-uuid-xyz")
            || r.source != HighlightRegion::Source::Ocr
            || r.startBoxIndex != 2 || r.startCharIndex != 5
            || r.endBoxIndex != 4 || r.endCharIndex != 11
            || r.stale) {
            qDebug() << "FAIL: sourceRange not preserved";
            success = false;
        }
        if (!r.isUsable()) {
            qDebug() << "FAIL: a freshly restored range should be usable";
            success = false;
        }
        
        // A clone carries the region and the flag.
        auto clone = link.cloneWithBackLink(QStringLiteral("src-page"));
        if (clone->region.rects != link.region.rects
            || !clone->descriptionUserEdited
            || clone->size != link.size) {
            qDebug() << "FAIL: clone did not carry the region";
            success = false;
        }
    }
    
    // ----- Legacy JSON: no region key at all -----
    {
        LinkObject link;
        link.setRegionFromPageRects(pageRects);
        QJsonObject json = link.toJson();
        json.remove(QStringLiteral("region"));
        json.remove(QStringLiteral("descriptionUserEdited"));
        // A pre-region document also carries icon-sized bounds.
        json["width"] = LinkObject::ICON_SIZE;
        json["height"] = LinkObject::ICON_SIZE;
        
        LinkObject restored;
        restored.loadFromJson(json);
        
        if (!restored.region.isEmpty()) {
            qDebug() << "FAIL: absent region key should load as an empty region";
            success = false;
        }
        if (restored.descriptionUserEdited) {
            qDebug() << "FAIL: absent flag should default to false";
            success = false;
        }
        if (!restored.shouldShowIcon()) {
            qDebug() << "FAIL: a legacy annotation must keep its icon";
            success = false;
        }
        // Falls all the way back to the historical 24x24 icon hit test.
        if (!restored.containsPoint(restored.position + QPointF(12, 12))
            || restored.containsPoint(restored.position + QPointF(30, 12))) {
            qDebug() << "FAIL: legacy hit testing is not the 24x24 icon rect";
            success = false;
        }
    }
    
    // ----- An empty region serializes exactly as it did before regions -----
    {
        LinkObject icon;
        icon.description = QStringLiteral("plain");
        const QJsonObject json = icon.toJson();
        if (json.contains(QStringLiteral("region"))
            || json.contains(QStringLiteral("descriptionUserEdited"))) {
            qDebug() << "FAIL: an empty region should not add keys to the JSON";
            success = false;
        }
    }
    
    if (success) {
        qDebug() << "PASS: HighlightRegion successful!";
    }
    
    return success;
}

/**
 * @brief Test slot clear() method.
 */
inline bool testSlotClear()
{
    qDebug() << "=== Test: LinkSlot clear() ===";
    
    bool success = true;
    
    LinkSlot slot;
    slot.type = LinkSlot::Type::Position;
    slot.targetPageUuid = "some-uuid";
    slot.targetPosition = QPointF(100, 200);
    
    // Verify it's not empty
    if (slot.isEmpty()) {
        qDebug() << "FAIL: Slot should not be empty before clear";
        success = false;
    }
    
    // Clear it
    slot.clear();
    
    // Verify it's now empty
    if (!slot.isEmpty()) {
        qDebug() << "FAIL: Slot should be empty after clear";
        success = false;
    }
    
    if (slot.type != LinkSlot::Type::Empty) {
        qDebug() << "FAIL: Slot type should be Empty after clear";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: LinkSlot clear() successful!";
    }
    
    return success;
}

/**
 * @brief Test a position slot's targetObjectId, including old files.
 *
 * The id is what lets a position link survive its target being dragged, and it
 * is written only when present so that a coordinate-only link round-trips
 * unchanged and an older build reading the file sees what it wrote.
 */
inline bool testPositionTargetObjectId()
{
    qDebug() << "=== Test: position slot targetObjectId ===";

    bool success = true;

    // Round-trip with an id.
    {
        LinkSlot slot;
        slot.type = LinkSlot::Type::Position;
        slot.targetPageUuid = QStringLiteral("page-uuid-9");
        slot.targetPosition = QPointF(12.5, 34.25);
        slot.targetObjectId = QStringLiteral("link-target-42");

        slot.targetSlotIndex = 2;

        const LinkSlot restored = LinkSlot::fromJson(slot.toJson());
        if (restored.targetObjectId != QStringLiteral("link-target-42")) {
            qDebug() << "FAIL: targetObjectId not preserved, got"
                     << restored.targetObjectId;
            success = false;
        }
        if (restored.targetSlotIndex != 2) {
            qDebug() << "FAIL: targetSlotIndex not preserved, got"
                     << restored.targetSlotIndex;
            success = false;
        }
        if (restored.targetPageUuid != slot.targetPageUuid
            || restored.targetPosition != slot.targetPosition) {
            qDebug() << "FAIL: coordinate lost when an id is present";
            success = false;
        }
    }

    // Slot 0 is a legitimate partner index, so it must survive a round trip
    // rather than being mistaken for "unset" and dropped.
    {
        LinkSlot slot;
        slot.type = LinkSlot::Type::Position;
        slot.targetPageUuid = QStringLiteral("page-uuid-0");
        slot.targetObjectId = QStringLiteral("partner-in-slot-zero");
        slot.targetSlotIndex = 0;

        const LinkSlot restored = LinkSlot::fromJson(slot.toJson());
        if (restored.targetSlotIndex != 0) {
            qDebug() << "FAIL: partner index 0 did not round-trip, got"
                     << restored.targetSlotIndex;
            success = false;
        }
    }

    // Round-trip in edgeless form, where the tile carries the location.
    {
        LinkSlot slot;
        slot.type = LinkSlot::Type::Position;
        slot.isEdgelessTarget = true;
        slot.edgelessTileX = 3;
        slot.edgelessTileY = -2;
        slot.targetPosition = QPointF(4000, -1000);
        slot.targetObjectId = QStringLiteral("tile-target-7");

        const LinkSlot restored = LinkSlot::fromJson(slot.toJson());
        if (!restored.isEdgelessTarget
            || restored.edgelessTileX != 3 || restored.edgelessTileY != -2
            || restored.targetObjectId != QStringLiteral("tile-target-7")) {
            qDebug() << "FAIL: edgeless position slot with an id not preserved";
            success = false;
        }
    }

    // A coordinate-only slot must not gain the key, or every existing file
    // would be rewritten on save for no reason.
    {
        LinkSlot slot;
        slot.type = LinkSlot::Type::Position;
        slot.targetPageUuid = QStringLiteral("page-uuid-1");
        slot.targetPosition = QPointF(1, 2);

        if (slot.toJson().contains(QStringLiteral("targetObjectId"))) {
            qDebug() << "FAIL: empty targetObjectId was still written";
            success = false;
        }
        if (slot.toJson().contains(QStringLiteral("targetSlotIndex"))) {
            qDebug() << "FAIL: unset targetSlotIndex was still written";
            success = false;
        }
    }

    // A file written before pairing existed: the key is simply absent.
    {
        QJsonObject legacy;
        legacy[QStringLiteral("type")] = QStringLiteral("position");
        legacy[QStringLiteral("x")] = 10.0;
        legacy[QStringLiteral("y")] = 20.0;
        legacy[QStringLiteral("pageUuid")] = QStringLiteral("legacy-page");

        const LinkSlot restored = LinkSlot::fromJson(legacy);
        if (restored.type != LinkSlot::Type::Position) {
            qDebug() << "FAIL: legacy position slot did not load";
            success = false;
        }
        if (!restored.targetObjectId.isEmpty()) {
            qDebug() << "FAIL: legacy slot invented a targetObjectId";
            success = false;
        }
        // toInt()'s default is 0, which would read as "partnered with slot 1"
        // and make every legacy position link look like half of a pair.
        if (restored.targetSlotIndex != -1) {
            qDebug() << "FAIL: legacy slot invented a partner slot index, got"
                     << restored.targetSlotIndex;
            success = false;
        }
        if (restored.targetPosition != QPointF(10, 20)
            || restored.targetPageUuid != QStringLiteral("legacy-page")) {
            qDebug() << "FAIL: legacy position slot lost its coordinate";
            success = false;
        }
    }

    if (success) {
        qDebug() << "PASS: position slot targetObjectId successful!";
    }

    return success;
}

/**
 * @brief Run all LinkObject tests.
 * @return True if all tests pass.
 */
inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running LinkObject Unit Tests (Phase C.1)";
    qDebug() << "========================================\n";
    
    bool allPass = true;
    
    allPass &= testCreation();
    qDebug() << "";
    
    allPass &= testLinkSlotSerialization();
    qDebug() << "";
    
    allPass &= testLinkObjectSerialization();
    qDebug() << "";
    
    allPass &= testContainsPoint();
    qDebug() << "";
    
    allPass &= testHighlightRegion();
    qDebug() << "";
    
    allPass &= testCloneWithBackLink();
    qDebug() << "";
    
    allPass &= testFactoryCreation();
    qDebug() << "";
    
    allPass &= testSlotClear();
    qDebug() << "";
    
    allPass &= testPositionTargetObjectId();
    qDebug() << "";
    
    qDebug() << "\n========================================";
    if (allPass) {
        qDebug() << "ALL LINKOBJECT TESTS PASSED!";
    } else {
        qDebug() << "SOME LINKOBJECT TESTS FAILED!";
    }
    qDebug() << "========================================\n";
    
    return allPass;
}

} // namespace LinkObjectTests

