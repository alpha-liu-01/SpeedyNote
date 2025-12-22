#pragma once

// ============================================================================
// PageTests - Unit tests for the Page class
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.1.7)
// 
// Simple compile-time tests to verify Page functionality:
// - Serialization round-trip (toJson/fromJson)
// - Layer management
// - Object management
// - Optional PNG export for visual verification
// ============================================================================

#include "Page.h"
#include "../objects/ImageObject.h"
#include <QDebug>
#include <QJsonDocument>
#include <cassert>

namespace PageTests {

/**
 * @brief Test Page serialization round-trip.
 * 
 * Creates a page with content, serializes to JSON, deserializes,
 * and verifies the data matches.
 */
inline bool testSerializationRoundTrip()
{
    qDebug() << "=== Test: Page Serialization Round-Trip ===";
    
    // 1. Create a page with content
    auto page = Page::createDefault(QSizeF(800, 600));
    page->pageIndex = 5;
    page->backgroundType = Page::BackgroundType::Grid;
    page->gridSpacing = 25;
    page->backgroundColor = QColor(240, 240, 255);
    
    // Add a second layer
    page->addLayer("Layer 2");
    
    // Add strokes to both layers
    VectorStroke stroke1;
    stroke1.id = "stroke-001";
    stroke1.color = Qt::red;
    stroke1.baseThickness = 3.0;
    stroke1.points.append({QPointF(10, 10), 0.5});
    stroke1.points.append({QPointF(100, 50), 0.8});
    stroke1.points.append({QPointF(200, 30), 0.6});
    stroke1.updateBoundingBox();
    
    page->layer(0)->addStroke(stroke1);
    
    VectorStroke stroke2;
    stroke2.id = "stroke-002";
    stroke2.color = Qt::blue;
    stroke2.baseThickness = 5.0;
    stroke2.points.append({QPointF(50, 100), 1.0});
    stroke2.points.append({QPointF(150, 150), 0.7});
    stroke2.updateBoundingBox();
    
    page->layer(1)->addStroke(stroke2);
    
    // Add an image object
    auto img = std::make_unique<ImageObject>();
    img->id = "img-001";
    img->position = QPointF(300, 200);
    img->size = QSizeF(100, 75);
    img->imagePath = "images/test.png";
    img->zOrder = 5;
    page->addObject(std::move(img));
    
    // 2. Serialize to JSON
    QJsonObject json = page->toJson();
    
    // Debug: Print JSON
    QJsonDocument doc(json);
    qDebug() << "Serialized JSON:" << doc.toJson(QJsonDocument::Indented).left(500) << "...";
    
    // 3. Deserialize
    auto restored = Page::fromJson(json);
    
    // 4. Verify data matches
    bool success = true;
    
    // Check identity
    if (restored->pageIndex != 5) {
        qDebug() << "FAIL: pageIndex mismatch:" << restored->pageIndex << "!= 5";
        success = false;
    }
    
    if (restored->size != QSizeF(800, 600)) {
        qDebug() << "FAIL: size mismatch";
        success = false;
    }
    
    // Check background
    if (restored->backgroundType != Page::BackgroundType::Grid) {
        qDebug() << "FAIL: backgroundType mismatch";
        success = false;
    }
    
    if (restored->gridSpacing != 25) {
        qDebug() << "FAIL: gridSpacing mismatch:" << restored->gridSpacing << "!= 25";
        success = false;
    }
    
    // Check layers
    if (restored->layerCount() != 2) {
        qDebug() << "FAIL: layerCount mismatch:" << restored->layerCount() << "!= 2";
        success = false;
    }
    
    if (restored->layer(0)->strokeCount() != 1) {
        qDebug() << "FAIL: layer 0 strokeCount mismatch";
        success = false;
    }
    
    if (restored->layer(1)->strokeCount() != 1) {
        qDebug() << "FAIL: layer 1 strokeCount mismatch";
        success = false;
    }
    
    // Check stroke data preserved
    const auto& restoredStroke = restored->layer(0)->strokes()[0];
    if (restoredStroke.id != "stroke-001") {
        qDebug() << "FAIL: stroke id mismatch";
        success = false;
    }
    if (restoredStroke.color != Qt::red) {
        qDebug() << "FAIL: stroke color mismatch";
        success = false;
    }
    if (restoredStroke.points.size() != 3) {
        qDebug() << "FAIL: stroke points count mismatch";
        success = false;
    }
    
    // Check objects
    if (restored->objectCount() != 1) {
        qDebug() << "FAIL: objectCount mismatch:" << restored->objectCount() << "!= 1";
        success = false;
    }
    
    auto* restoredImg = restored->objectById("img-001");
    if (!restoredImg) {
        qDebug() << "FAIL: image object not found";
        success = false;
    } else {
        if (restoredImg->type() != "image") {
            qDebug() << "FAIL: object type mismatch";
            success = false;
        }
        if (restoredImg->position != QPointF(300, 200)) {
            qDebug() << "FAIL: image position mismatch";
            success = false;
        }
        if (restoredImg->zOrder != 5) {
            qDebug() << "FAIL: image zOrder mismatch";
            success = false;
        }
    }
    
    if (success) {
        qDebug() << "PASS: Serialization round-trip successful!";
    }
    
    return success;
}

/**
 * @brief Test layer management operations.
 */
inline bool testLayerManagement()
{
    qDebug() << "=== Test: Layer Management ===";
    
    auto page = Page::createDefault(QSizeF(800, 600));
    bool success = true;
    
    // Should start with 1 layer
    if (page->layerCount() != 1) {
        qDebug() << "FAIL: Initial layer count should be 1";
        success = false;
    }
    
    // Add layers
    page->addLayer("Layer 2");
    page->addLayer("Layer 3");
    
    if (page->layerCount() != 3) {
        qDebug() << "FAIL: After adding 2 layers, count should be 3";
        success = false;
    }
    
    // Active layer should be the last added
    if (page->activeLayerIndex != 2) {
        qDebug() << "FAIL: Active layer should be 2 after adding";
        success = false;
    }
    
    // Remove middle layer
    page->removeLayer(1);
    if (page->layerCount() != 2) {
        qDebug() << "FAIL: After removing, count should be 2";
        success = false;
    }
    
    // Should not be able to remove last layer
    page->removeLayer(0);
    page->removeLayer(0);  // Try to remove the only remaining layer
    if (page->layerCount() != 1) {
        qDebug() << "FAIL: Should not be able to remove last layer";
        success = false;
    }
    
    // Test move layer
    page->addLayer("New Layer 2");
    page->addLayer("New Layer 3");
    
    // Add content to identify layers
    VectorStroke s1; s1.id = "L1"; s1.updateBoundingBox();
    VectorStroke s2; s2.id = "L2"; s2.updateBoundingBox();
    VectorStroke s3; s3.id = "L3"; s3.updateBoundingBox();
    page->layer(0)->addStroke(s1);
    page->layer(1)->addStroke(s2);
    page->layer(2)->addStroke(s3);
    
    // Move layer 0 to position 2
    page->moveLayer(0, 2);
    
    // Verify order changed
    if (page->layer(0)->strokes()[0].id != "L2") {
        qDebug() << "FAIL: After move, layer 0 should have L2 stroke";
        success = false;
    }
    if (page->layer(2)->strokes()[0].id != "L1") {
        qDebug() << "FAIL: After move, layer 2 should have L1 stroke";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: Layer management tests successful!";
    }
    
    return success;
}

/**
 * @brief Test object management and hit testing.
 */
inline bool testObjectManagement()
{
    qDebug() << "=== Test: Object Management ===";
    
    auto page = Page::createDefault(QSizeF(800, 600));
    bool success = true;
    
    // Add objects with different z-orders
    auto img1 = std::make_unique<ImageObject>();
    img1->id = "img1";
    img1->position = QPointF(100, 100);
    img1->size = QSizeF(200, 150);
    img1->zOrder = 1;
    
    auto img2 = std::make_unique<ImageObject>();
    img2->id = "img2";
    img2->position = QPointF(150, 120);  // Overlaps with img1
    img2->size = QSizeF(200, 150);
    img2->zOrder = 2;  // On top
    
    page->addObject(std::move(img1));
    page->addObject(std::move(img2));
    
    if (page->objectCount() != 2) {
        qDebug() << "FAIL: Should have 2 objects";
        success = false;
    }
    
    // Hit test at overlapping point - should return img2 (higher z-order)
    InsertedObject* hit = page->objectAtPoint(QPointF(200, 150));
    if (!hit || hit->id != "img2") {
        qDebug() << "FAIL: Hit test should return img2 (topmost)";
        success = false;
    }
    
    // Hit test at img1-only point
    hit = page->objectAtPoint(QPointF(110, 110));
    if (!hit || hit->id != "img1") {
        qDebug() << "FAIL: Hit test should return img1";
        success = false;
    }
    
    // Hit test at empty point
    hit = page->objectAtPoint(QPointF(50, 50));
    if (hit != nullptr) {
        qDebug() << "FAIL: Hit test at empty point should return nullptr";
        success = false;
    }
    
    // Remove object
    page->removeObject("img1");
    if (page->objectCount() != 1) {
        qDebug() << "FAIL: After remove, should have 1 object";
        success = false;
    }
    
    if (page->objectById("img1") != nullptr) {
        qDebug() << "FAIL: img1 should not exist after removal";
        success = false;
    }
    
    if (success) {
        qDebug() << "PASS: Object management tests successful!";
    }
    
    return success;
}

/**
 * @brief Render a test page to PNG for visual verification.
 * @param outputPath Path to save the PNG file.
 * @return True if successful.
 */
inline bool renderTestPageToPng(const QString& outputPath)
{
    qDebug() << "=== Rendering Test Page to PNG ===" << outputPath;
    
    // Create a page with various content
    auto page = Page::createDefault(QSizeF(800, 600));
    page->backgroundType = Page::BackgroundType::Grid;
    page->gridSpacing = 20;
    page->backgroundColor = QColor(255, 255, 240);  // Light yellow
    page->gridColor = QColor(200, 200, 220);
    
    // Add strokes with varying pressure
    VectorStroke stroke;
    stroke.color = QColor(50, 50, 150);  // Dark blue
    stroke.baseThickness = 4.0;
    
    // Draw a wavy line with varying pressure
    for (int i = 0; i <= 100; i++) {
        qreal t = i / 100.0;
        qreal x = 50 + t * 700;
        qreal y = 150 + qSin(t * 6.28 * 3) * 50;
        qreal pressure = 0.3 + 0.7 * qAbs(qSin(t * 6.28 * 2));
        stroke.points.append({QPointF(x, y), pressure});
    }
    stroke.updateBoundingBox();
    page->activeLayer()->addStroke(stroke);
    
    // Add another stroke
    VectorStroke stroke2;
    stroke2.color = QColor(150, 50, 50);  // Dark red
    stroke2.baseThickness = 6.0;
    stroke2.points.append({QPointF(100, 300), 1.0});
    stroke2.points.append({QPointF(200, 350), 0.8});
    stroke2.points.append({QPointF(300, 320), 0.6});
    stroke2.points.append({QPointF(400, 380), 0.9});
    stroke2.points.append({QPointF(500, 340), 0.5});
    stroke2.updateBoundingBox();
    page->activeLayer()->addStroke(stroke2);
    
    // Add a second layer with content
    page->addLayer("Layer 2");
    VectorStroke stroke3;
    stroke3.color = QColor(50, 150, 50);  // Dark green
    stroke3.baseThickness = 3.0;
    for (int i = 0; i <= 50; i++) {
        qreal t = i / 50.0;
        qreal x = 100 + t * 300;
        qreal y = 450 + qCos(t * 6.28 * 2) * 30;
        stroke3.points.append({QPointF(x, y), 0.5 + 0.5 * t});
    }
    stroke3.updateBoundingBox();
    page->activeLayer()->addStroke(stroke3);
    
    // Render to pixmap
    QPixmap output(800, 600);
    output.fill(Qt::white);
    
    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing, true);
    page->render(painter, nullptr, 1.0);
    painter.end();
    
    // Save to file
    bool saved = output.save(outputPath, "PNG");
    
    if (saved) {
        qDebug() << "SUCCESS: Test page rendered to" << outputPath;
    } else {
        qDebug() << "FAIL: Could not save to" << outputPath;
    }
    
    return saved;
}

/**
 * @brief Run all Page tests.
 * @return True if all tests pass.
 */
inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running Page Unit Tests";
    qDebug() << "========================================\n";
    
    bool allPass = true;
    
    allPass &= testSerializationRoundTrip();
    qDebug() << "";
    
    allPass &= testLayerManagement();
    qDebug() << "";
    
    allPass &= testObjectManagement();
    qDebug() << "";
    
    // Optional: Render to PNG
    renderTestPageToPng("test_page_render.png");
    
    qDebug() << "\n========================================";
    if (allPass) {
        qDebug() << "ALL TESTS PASSED!";
    } else {
        qDebug() << "SOME TESTS FAILED!";
    }
    qDebug() << "========================================\n";
    
    return allPass;
}

} // namespace PageTests
