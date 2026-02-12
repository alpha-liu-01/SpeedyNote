// ============================================================================
// DocumentViewportTests - Unit and Visual Tests for DocumentViewport
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.11)
// Run with: speedynote --test-viewport
// ============================================================================

#pragma once

#include "DocumentViewport.h"
#include "Document.h"
#include "Page.h"
#include "../strokes/VectorStroke.h"
#include "../strokes/StrokePoint.h"

#include <QApplication>
#include <QtMath>
#include <cstdio>
#include <memory>

/**
 * @brief Test suite for DocumentViewport.
 * 
 * Contains both unit tests (non-visual) and a visual test mode.
 */
class DocumentViewportTests {
public:
    
    // ===== Unit Tests =====
    
    /**
     * @brief Test basic viewport creation and document assignment.
     */
    static bool testViewportCreation() {
        printf("  testViewportCreation... ");
        
        DocumentViewport viewport;
        
        // Initial state
        if (viewport.document() != nullptr) {
            printf("FAILED: document should be null initially\n");
            return false;
        }
        if (viewport.zoomLevel() != 1.0) {
            printf("FAILED: zoom should be 1.0 initially\n");
            return false;
        }
        if (viewport.panOffset() != QPointF(0, 0)) {
            printf("FAILED: pan should be (0,0) initially\n");
            return false;
        }
        
        // Create and assign document
        auto doc = Document::createNew("Test");
        viewport.setDocument(doc.get());
        
        if (viewport.document() != doc.get()) {
            printf("FAILED: document not assigned correctly\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test zoom level setting and bounds.
     */
    static bool testZoomBounds() {
        printf("  testZoomBounds... ");
        
        DocumentViewport viewport;
        auto doc = Document::createNew("Test");
        viewport.setDocument(doc.get());
        
        // Normal zoom
        viewport.setZoomLevel(2.0);
        if (!qFuzzyCompare(viewport.zoomLevel(), 2.0)) {
            printf("FAILED: zoom 2.0 not set correctly\n");
            return false;
        }
        
        // Min zoom (should clamp to 0.1)
        viewport.setZoomLevel(0.01);
        if (viewport.zoomLevel() < 0.1) {
            printf("FAILED: zoom should clamp to min 0.1\n");
            return false;
        }
        
        // Max zoom (should clamp to 10.0)
        viewport.setZoomLevel(20.0);
        if (viewport.zoomLevel() > 10.0) {
            printf("FAILED: zoom should clamp to max 10.0\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test layout engine page positioning.
     */
    static bool testLayoutEngine() {
        printf("  testLayoutEngine... ");
        
        DocumentViewport viewport;
        auto doc = Document::createNew("Test");
        
        // Add multiple pages
        doc->addPage();
        doc->addPage();
        viewport.setDocument(doc.get());
        
        // Single column layout
        viewport.setLayoutMode(LayoutMode::SingleColumn);
        
        QPointF pos0 = viewport.pagePosition(0);
        QPointF pos1 = viewport.pagePosition(1);
        QPointF pos2 = viewport.pagePosition(2);
        
        // Page 0 should be at origin
        if (pos0 != QPointF(0, 0)) {
            printf("FAILED: page 0 should be at origin\n");
            return false;
        }
        
        // Pages should be stacked vertically
        if (pos1.y() <= pos0.y()) {
            printf("FAILED: page 1 should be below page 0\n");
            return false;
        }
        if (pos2.y() <= pos1.y()) {
            printf("FAILED: page 2 should be below page 1\n");
            return false;
        }
        
        // Two column layout
        viewport.setLayoutMode(LayoutMode::TwoColumn);
        
        pos0 = viewport.pagePosition(0);
        pos1 = viewport.pagePosition(1);
        pos2 = viewport.pagePosition(2);
        
        // Page 0 and 1 should be on same row
        if (pos1.y() != pos0.y()) {
            printf("FAILED: pages 0 and 1 should be on same row in TwoColumn\n");
            return false;
        }
        
        // Page 1 should be to the right of page 0
        if (pos1.x() <= pos0.x()) {
            printf("FAILED: page 1 should be right of page 0 in TwoColumn\n");
            return false;
        }
        
        // Page 2 should be on a new row
        if (pos2.y() <= pos0.y()) {
            printf("FAILED: page 2 should be on new row in TwoColumn\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test coordinate transforms.
     */
    static bool testCoordinateTransforms() {
        printf("  testCoordinateTransforms... ");
        
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Test");
        viewport.setDocument(doc.get());
        
        // At zoom 1.0, pan (0,0), viewport coords should equal document coords
        viewport.setZoomLevel(1.0);
        viewport.setPanOffset(QPointF(0, 0));
        
        QPointF viewportPt(100, 100);
        QPointF docPt = viewport.viewportToDocument(viewportPt);
        
        if (!qFuzzyCompare(docPt.x(), 100.0) || !qFuzzyCompare(docPt.y(), 100.0)) {
            printf("FAILED: viewportToDocument at zoom 1.0 pan (0,0)\n");
            return false;
        }
        
        // Test round-trip
        QPointF backToViewport = viewport.documentToViewport(docPt);
        if (!qFuzzyCompare(backToViewport.x(), viewportPt.x()) ||
            !qFuzzyCompare(backToViewport.y(), viewportPt.y())) {
            printf("FAILED: round-trip transform\n");
            return false;
        }
        
        // Test with zoom 2.0
        viewport.setZoomLevel(2.0);
        docPt = viewport.viewportToDocument(viewportPt);
        // At zoom 2.0, viewport pixel 100 = document coord 50
        if (!qFuzzyCompare(docPt.x(), 50.0) || !qFuzzyCompare(docPt.y(), 50.0)) {
            printf("FAILED: viewportToDocument at zoom 2.0\n");
            return false;
        }
        
        // Test with pan offset
        viewport.setZoomLevel(1.0);
        viewport.setPanOffset(QPointF(50, 50));
        docPt = viewport.viewportToDocument(viewportPt);
        // viewportPt / zoom + pan = 100/1 + 50 = 150
        if (!qFuzzyCompare(docPt.x(), 150.0) || !qFuzzyCompare(docPt.y(), 150.0)) {
            printf("FAILED: viewportToDocument with pan offset\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test page hit detection.
     */
    static bool testPageHitDetection() {
        printf("  testPageHitDetection... ");
        
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Test");
        doc->addPage();  // Add a second page
        viewport.setDocument(doc.get());
        viewport.setLayoutMode(LayoutMode::SingleColumn);
        viewport.setZoomLevel(1.0);
        viewport.setPanOffset(QPointF(0, 0));
        
        // Point on page 0
        QPointF pointOnPage0(100, 100);
        PageHit hit = viewport.viewportToPage(pointOnPage0);
        if (!hit.valid() || hit.pageIndex != 0) {
            printf("FAILED: point (100,100) should hit page 0\n");
            return false;
        }
        
        // Point in gap between pages should not hit any page
        Page* page0 = doc->page(0);
        qreal page0Bottom = page0->size.height();
        qreal gapY = page0Bottom + viewport.pageGap() / 2;  // Middle of gap
        
        PageHit gapHit = viewport.documentToPage(QPointF(100, gapY));
        if (gapHit.valid()) {
            printf("FAILED: point in gap should not hit any page\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test visible pages calculation.
     */
    static bool testVisiblePages() {
        printf("  testVisiblePages... ");
        
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Test");
        
        // Add 10 pages
        for (int i = 0; i < 9; ++i) {
            doc->addPage();
        }
        viewport.setDocument(doc.get());
        viewport.setLayoutMode(LayoutMode::SingleColumn);
        viewport.setZoomLevel(0.5);  // Zoom out to see more pages
        viewport.setPanOffset(QPointF(0, 0));
        
        QVector<int> visible = viewport.visiblePages();
        
        // Should have at least page 0 visible
        if (visible.isEmpty()) {
            printf("FAILED: at least page 0 should be visible\n");
            return false;
        }
        if (!visible.contains(0)) {
            printf("FAILED: page 0 should be visible at pan (0,0)\n");
            return false;
        }
        
        // Scroll to middle of document
        viewport.scrollToPage(5);
        visible = viewport.visiblePages();
        
        if (!visible.contains(5)) {
            printf("FAILED: page 5 should be visible after scrollToPage(5)\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test scroll fraction calculation.
     */
    static bool testScrollFractions() {
        printf("  testScrollFractions... ");
        
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Test");
        
        // Add pages to make content taller than viewport
        for (int i = 0; i < 5; ++i) {
            doc->addPage();
        }
        viewport.setDocument(doc.get());
        viewport.setZoomLevel(1.0);
        
        // At top, vertical fraction should be ~0
        viewport.setPanOffset(QPointF(0, 0));
        
        // Scroll to bottom
        QSizeF contentSize = viewport.totalContentSize();
        qreal viewportHeight = viewport.height() / viewport.zoomLevel();
        qreal maxPanY = contentSize.height() - viewportHeight;
        
        viewport.setPanOffset(QPointF(0, maxPanY));
        
        // Now test setVerticalScrollFraction
        viewport.setVerticalScrollFraction(0.0);
        if (viewport.panOffset().y() > 10) {  // Allow small margin
            printf("FAILED: setVerticalScrollFraction(0) should scroll to top\n");
            return false;
        }
        
        viewport.setVerticalScrollFraction(1.0);
        if (viewport.panOffset().y() < maxPanY - 10) {  // Allow small margin
            printf("FAILED: setVerticalScrollFraction(1) should scroll to bottom\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test PDF cache management.
     */
    static bool testPdfCache() {
        printf("  testPdfCache... ");
        
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Test");
        viewport.setDocument(doc.get());
        
        // Without PDF loaded, cache operations should not crash
        viewport.invalidatePdfCache();
        viewport.preloadPdfCache();
        
        // Test cache capacity changes with layout
        viewport.setLayoutMode(LayoutMode::SingleColumn);
        // Capacity should be set (can't directly test private member)
        
        viewport.setLayoutMode(LayoutMode::TwoColumn);
        // Capacity should increase
        
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test PointerEvent creation from mouse events.
     */
    static bool testPointerEvents() {
        printf("  testPointerEvents... ");
        
        // Test PointerEvent struct
        PointerEvent pe;
        pe.type = PointerEvent::Press;
        pe.source = PointerEvent::Mouse;
        pe.viewportPos = QPointF(100, 200);
        pe.pressure = 1.0;
        pe.isEraser = false;
        
        if (pe.type != PointerEvent::Press) {
            printf("FAILED: PointerEvent type not set\n");
            return false;
        }
        if (pe.source != PointerEvent::Mouse) {
            printf("FAILED: PointerEvent source not set\n");
            return false;
        }
        
        // Test GestureState
        GestureState gs;
        gs.activeGesture = GestureState::PinchZoom;
        gs.zoomFactor = 1.5;
        gs.reset();
        
        if (gs.activeGesture != GestureState::None) {
            printf("FAILED: GestureState reset failed\n");
            return false;
        }
        if (!qFuzzyCompare(gs.zoomFactor, 1.0)) {
            printf("FAILED: GestureState zoomFactor reset failed\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }
    
    // ===== Run All Unit Tests =====
    
    static bool runUnitTests() {
        printf("\n=== DocumentViewport Unit Tests ===\n\n");
        
        int passed = 0;
        int failed = 0;
        
        auto runTest = [&](bool (*test)(), const char* name) {
            if (test()) {
                passed++;
            } else {
                failed++;
                printf("  [FAILED] %s\n", name);
            }
        };
        
        runTest(testViewportCreation, "testViewportCreation");
        runTest(testZoomBounds, "testZoomBounds");
        runTest(testLayoutEngine, "testLayoutEngine");
        runTest(testCoordinateTransforms, "testCoordinateTransforms");
        runTest(testPageHitDetection, "testPageHitDetection");
        runTest(testVisiblePages, "testVisiblePages");
        runTest(testScrollFractions, "testScrollFractions");
        runTest(testPdfCache, "testPdfCache");
        runTest(testPointerEvents, "testPointerEvents");
        
        printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);
        
        return failed == 0;
    }
    
    // ===== Visual Test =====
    
    /**
     * @brief Create a test document with colorful strokes.
     */
    static std::unique_ptr<Document> createVisualTestDocument() {
        auto doc = Document::createNew("Visual Test Document");
        
        for (int i = 0; i < 5; ++i) {
            Page* page = (i == 0) ? doc->page(0) : doc->addPage();
            
            // Set different background for variety
            if (i % 2 == 1) {
                page->backgroundType = Page::BackgroundType::Grid;
                page->gridSpacing = 25;
                page->gridColor = QColor(200, 200, 220);
            } else if (i == 2) {
                page->backgroundType = Page::BackgroundType::Lines;
                page->lineSpacing = 30;
                page->gridColor = QColor(200, 200, 220);
            }
            
            // Create a colored wavy stroke
            VectorStroke stroke;
            stroke.color = QColor::fromHsv(i * 60, 200, 200);
            stroke.baseThickness = 4.0;
            
            for (int j = 0; j <= 50; ++j) {
                qreal t = j / 50.0;
                StrokePoint pt;
                pt.pos = QPointF(50 + t * 700, 150 + qSin(t * 6.28 * 3) * 80);
                pt.pressure = 0.3 + 0.7 * t;
                stroke.points.append(pt);
            }
            stroke.updateBoundingBox();
            page->activeLayer()->addStroke(stroke);
            
            // Add a second stroke (diagonal)
            VectorStroke stroke2;
            stroke2.color = QColor::fromHsv((i * 60 + 180) % 360, 150, 220);
            stroke2.baseThickness = 2.5;
            for (int j = 0; j <= 30; ++j) {
                qreal t = j / 30.0;
                StrokePoint pt;
                pt.pos = QPointF(100 + t * 600, 300 + t * 200);
                pt.pressure = 0.5 + 0.3 * qSin(t * 6.28 * 2);
                stroke2.points.append(pt);
            }
            stroke2.updateBoundingBox();
            page->activeLayer()->addStroke(stroke2);
            
            // Add page number text-like stroke (spiral)
            VectorStroke numberStroke;
            numberStroke.color = QColor(100, 100, 100);
            numberStroke.baseThickness = 2.0;
            for (int j = 0; j <= 20; ++j) {
                qreal t = j / 20.0;
                qreal angle = t * 4 * M_PI;
                qreal radius = 15 + t * 20;
                StrokePoint pt;
                pt.pos = QPointF(750 + qCos(angle) * radius, 50 + qSin(angle) * radius);
                pt.pressure = 0.8;
                numberStroke.points.append(pt);
            }
            numberStroke.updateBoundingBox();
            page->activeLayer()->addStroke(numberStroke);
        }
        
        return doc;
    }
    
    /**
     * @brief Run visual test - creates a window with test content.
     * @return Application exit code.
     */
    static int runVisualTest() {
        printf("\n=== DocumentViewport Visual Test ===\n\n");
        
        // First run unit tests
        bool unitTestsPassed = runUnitTests();
        
        if (!unitTestsPassed) {
            printf("Unit tests failed! Visual test will still run.\n\n");
        }
        
        printf("Creating visual test document with 5 pages...\n");
        auto doc = createVisualTestDocument();
        
        for (int i = 0; i < doc->pageCount(); ++i) {
            Page* page = doc->page(i);
            printf("  Page %d: %d strokes, background=%d\n", 
                   i + 1, page->activeLayer()->strokeCount(),
                   static_cast<int>(page->backgroundType));
        }
        
        printf("\nControls:\n");
        printf("  - Mouse wheel: Scroll vertically\n");
        printf("  - Ctrl + wheel: Zoom at cursor\n");
        printf("  - Shift + wheel: Scroll horizontally\n");
        printf("  - Click: Test input routing (see console output)\n");
        printf("  - Drag window edges: Test resize handling\n");
        printf("\n");
        
        // Create and show viewport
        DocumentViewport* viewport = new DocumentViewport();
        viewport->setDocument(doc.get());
        viewport->setWindowTitle("DocumentViewport Test - Phase 1.3");
        viewport->resize(900, 700);
        viewport->show();
        
        int result = qApp->exec();
        
        delete viewport;
        return result;
    }
};
