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
    
    allPass &= testCloneWithBackLink();
    qDebug() << "";
    
    allPass &= testFactoryCreation();
    qDebug() << "";
    
    allPass &= testSlotClear();
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

