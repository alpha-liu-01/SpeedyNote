// ============================================================================
// DocumentViewportTests - Unit and Visual Tests for DocumentViewport
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3.11)
// Run with: speedynote --test-viewport
// ============================================================================

#pragma once

#include "DocumentViewport.h"
#include "Document.h"
#include "ObjectConstraints.h"
#include "Page.h"
#include "../objects/ImageObject.h"
#include "../strokes/VectorStroke.h"
#include "../strokes/StrokePoint.h"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtNumeric>
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

    /**
     * @brief Test left/current and right/alternate ObjectSelect mode resolution.
     */
    static bool testObjectAlternateMouseMode() {
        printf("  testObjectAlternateMouseMode... ");

        using ActionMode = DocumentViewport::ObjectActionMode;
        const auto resolve = [](ActionMode persistent,
                                PointerEvent::Source source,
                                Qt::MouseButton button) {
            return DocumentViewport::effectiveObjectActionModeForPointer(
                persistent, source, button);
        };

        if (resolve(ActionMode::Select, PointerEvent::Mouse, Qt::LeftButton)
                != ActionMode::Select
            || resolve(ActionMode::Create, PointerEvent::Mouse, Qt::LeftButton)
                != ActionMode::Create
            || resolve(ActionMode::Select, PointerEvent::Mouse, Qt::RightButton)
                != ActionMode::Create
            || resolve(ActionMode::Create, PointerEvent::Mouse, Qt::RightButton)
                != ActionMode::Select) {
            printf("FAILED: mouse mode matrix is incorrect\n");
            return false;
        }

        // Stylus barrel buttons are deliberately outside this mouse-only UX.
        if (resolve(ActionMode::Select, PointerEvent::Stylus, Qt::RightButton)
                != ActionMode::Select
            || resolve(ActionMode::Create, PointerEvent::Stylus, Qt::RightButton)
                != ActionMode::Create) {
            printf("FAILED: stylus mode should remain persistent\n");
            return false;
        }

        DocumentViewport viewport;
        viewport.m_objectActionMode = ActionMode::Select;
        PointerEvent press;
        press.type = PointerEvent::Press;
        press.source = PointerEvent::Mouse;
        press.button = Qt::RightButton;
        press.buttons = Qt::RightButton;
        viewport.beginObjectPointerGesture(press);

        if (viewport.m_objectGestureActionMode != ActionMode::Create
            || viewport.m_objectActionMode != ActionMode::Select
            || viewport.m_objectGestureButton != Qt::RightButton) {
            printf("FAILED: gesture changed persistent mode or cached wrong state\n");
            return false;
        }

        viewport.resetObjectPointerGesture();
        if (viewport.m_objectGestureButton != Qt::NoButton
            || viewport.m_objectActionMode != ActionMode::Select) {
            printf("FAILED: gesture reset changed persistent mode\n");
            return false;
        }

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Test that cancelling or changing mode rolls back live previews.
     */
    static bool testObjectGestureCancellation() {
        printf("  testObjectGestureCancellation... ");

        DocumentViewport viewport;
        viewport.m_currentTool = ToolType::ObjectSelect;
        viewport.m_objectInsertMode = DocumentViewport::ObjectInsertMode::Text;
        viewport.m_objectActionMode = DocumentViewport::ObjectActionMode::Create;

        PointerEvent press;
        press.source = PointerEvent::Mouse;
        press.button = Qt::LeftButton;
        viewport.beginObjectPointerGesture(press);
        viewport.m_pointerActive = true;
        viewport.m_activeSource = PointerEvent::Mouse;
        viewport.m_isCreatingTextBox = true;

        viewport.setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
        if (viewport.m_isCreatingTextBox || viewport.m_pointerActive
            || viewport.m_objectGestureButton != Qt::NoButton) {
            printf("FAILED: insert-mode change did not cancel text creation\n");
            return false;
        }

        viewport.beginObjectPointerGesture(press);
        viewport.m_pointerActive = true;
        viewport.m_isCreatingTextBox = true;
        viewport.setObjectActionMode(DocumentViewport::ObjectActionMode::Select);
        if (viewport.m_isCreatingTextBox || viewport.m_pointerActive
            || viewport.m_objectGestureButton != Qt::NoButton
            || viewport.m_objectGestureActionMode
                != DocumentViewport::ObjectActionMode::Select) {
            printf("FAILED: action-mode change did not cancel and resync gesture state\n");
            return false;
        }

        viewport.m_objectActionMode = DocumentViewport::ObjectActionMode::Select;
        press.button = Qt::RightButton;
        viewport.beginObjectPointerGesture(press);
        viewport.m_pointerActive = true;
        viewport.m_activeSource = PointerEvent::Mouse;
        viewport.m_isCreatingTextBox = true;

        PointerEvent mismatchedRelease;
        mismatchedRelease.type = PointerEvent::Release;
        mismatchedRelease.source = PointerEvent::Mouse;
        mismatchedRelease.button = Qt::LeftButton;
        mismatchedRelease.buttons = Qt::RightButton;
        viewport.handlePointerRelease_ObjectSelect(mismatchedRelease);
        if (!viewport.m_isCreatingTextBox
            || viewport.m_objectGestureButton != Qt::RightButton) {
            printf("FAILED: chord release cancelled while initiating button was held\n");
            return false;
        }

        mismatchedRelease.buttons = Qt::NoButton;
        viewport.handlePointerRelease_ObjectSelect(mismatchedRelease);
        if (viewport.m_isCreatingTextBox || viewport.m_pointerActive
            || viewport.m_objectGestureButton != Qt::NoButton
            || viewport.m_objectActionMode != DocumentViewport::ObjectActionMode::Select) {
            printf("FAILED: lost initiating release left a stuck gesture\n");
            return false;
        }

        ImageObject object;
        const QPointF originalPosition(25, 40);
        object.position = QPointF(125, 140);
        viewport.m_selectedObjects.append(&object);
        viewport.m_objectOriginalPositions.insert(object.id, originalPosition);
        viewport.m_isDraggingObjects = true;
        viewport.cancelObjectPointerGesture();
        if (object.position != originalPosition || viewport.m_isDraggingObjects
            || viewport.m_selectedObjects.size() != 1) {
            printf("FAILED: drag cancellation did not restore position and selection\n");
            return false;
        }

        const QSizeF originalSize(200, 100);
        const qreal originalRotation = 15.0;
        object.position = QPointF(300, 400);
        object.size = QSizeF(500, 600);
        object.rotation = 70.0;
        viewport.m_resizeOriginalPosition = originalPosition;
        viewport.m_resizeOriginalSize = originalSize;
        viewport.m_resizeOriginalRotation = originalRotation;
        viewport.m_isResizingObject = true;
        viewport.cancelObjectPointerGesture();
        if (object.position != originalPosition || object.size != originalSize
            || !qFuzzyCompare(object.rotation, originalRotation)
            || viewport.m_isResizingObject) {
            printf("FAILED: resize cancellation did not restore transform\n");
            return false;
        }

        object.position = QPointF(900, 900);
        viewport.m_objectOriginalPositions.insert(object.id, originalPosition);
        viewport.m_isDraggingObjects = true;
        viewport.m_pointerActive = true;
        viewport.deselectAllObjects();
        if (object.position != originalPosition || !viewport.m_selectedObjects.isEmpty()
            || viewport.m_isDraggingObjects || viewport.m_pointerActive) {
            printf("FAILED: deselection did not roll back the live drag\n");
            return false;
        }

        viewport.beginObjectPointerGesture(press);
        viewport.m_pointerActive = true;
        viewport.m_activeSource = PointerEvent::Mouse;
        viewport.m_isCreatingTextBox = true;
        QFocusEvent focusOut(QEvent::FocusOut);
        viewport.focusOutEvent(&focusOut);
        if (viewport.m_isCreatingTextBox || viewport.m_pointerActive
            || viewport.m_objectGestureButton != Qt::NoButton) {
            printf("FAILED: focus loss did not cancel the object gesture\n");
            return false;
        }

        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test the page containment geometry used to keep objects on-page.
     */
    static bool testObjectPageContainment() {
        printf("  testObjectPageContainment... ");
        
        const QSizeF pageSize(800, 1000);
        
        // Already inside: untouched
        QPointF inside(100, 200);
        if (ObjectConstraints::clampPosition(inside, QSizeF(50, 50), pageSize) != inside) {
            printf("FAILED: in-bounds position should not move\n");
            return false;
        }
        
        // Past each edge: pulled flush against it
        if (ObjectConstraints::clampPosition(QPointF(-30, -40), QSizeF(50, 50), pageSize)
                != QPointF(0, 0)) {
            printf("FAILED: top-left overhang should clamp to origin\n");
            return false;
        }
        if (ObjectConstraints::clampPosition(QPointF(900, 1200), QSizeF(50, 50), pageSize)
                != QPointF(750, 950)) {
            printf("FAILED: bottom-right overhang should clamp to page edge\n");
            return false;
        }
        
        // An object dropped in the gap below the page comes back on
        if (ObjectConstraints::clampPosition(QPointF(200, 1050), QSizeF(100, 80), pageSize)
                != QPointF(200, 920)) {
            printf("FAILED: object below the page should clamp to the bottom edge\n");
            return false;
        }
        
        // Larger than the page on an axis: centered, since containment is
        // impossible and an inverted bound would be worse
        QPointF oversized = ObjectConstraints::clampPosition(QPointF(500, 10),
                                                             QSizeF(1000, 500), pageSize);
        if (!qFuzzyCompare(oversized.x(), -100.0)) {
            printf("FAILED: oversized width should center, got x=%f\n", oversized.x());
            return false;
        }
        if (!qFuzzyCompare(oversized.y(), 10.0)) {
            printf("FAILED: oversized width should not affect the y axis\n");
            return false;
        }
        
        // Shrink-to-fit preserves aspect ratio
        QSizeF fitted = ObjectConstraints::shrinkToFit(QSizeF(1600, 1000), pageSize);
        if (!qFuzzyCompare(fitted.width(), 800.0) || !qFuzzyCompare(fitted.height(), 500.0)) {
            printf("FAILED: shrinkToFit should scale uniformly, got %fx%f\n",
                   fitted.width(), fitted.height());
            return false;
        }
        if (ObjectConstraints::shrinkToFit(QSizeF(100, 100), pageSize) != QSizeF(100, 100)) {
            printf("FAILED: shrinkToFit should leave a fitting size alone\n");
            return false;
        }
        
        // Resize cap depends only on the page, so an object flush against an
        // edge can still grow (the position clamp slides it inward)
        if (!qFuzzyCompare(ObjectConstraints::maxScaleToFitPage(100.0, 800.0), 8.0)) {
            printf("FAILED: expected max scale 8.0\n");
            return false;
        }
        if (ObjectConstraints::maxScaleToFitPage(1000.0, 800.0) >= 1.0) {
            printf("FAILED: an oversized object should be capped below 1.0\n");
            return false;
        }
        
        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Test whole-number scaling for freshly inserted images.
     */
    static bool testIntegerImageInsertScaling() {
        printf("  testIntegerImageInsertScaling... ");

        const QSizeF target(900, 1200);  // Two-thirds bounds: 600 x 800

        if (ObjectConstraints::integerShrinkDivisor(QSizeF(500, 700), target) != 1
            || ObjectConstraints::shrinkByIntegerDivisor(QSizeF(500, 700), target)
                != QSizeF(500, 700)) {
            printf("FAILED: fitting images should remain unchanged\n");
            return false;
        }

        if (ObjectConstraints::integerShrinkDivisor(QSizeF(600, 800), target) != 1) {
            printf("FAILED: exact two-thirds boundary should not shrink\n");
            return false;
        }

        QSizeF wide = ObjectConstraints::shrinkByIntegerDivisor(
            QSizeF(1800, 300), target);
        if (ObjectConstraints::integerShrinkDivisor(QSizeF(1800, 300), target) != 3
            || wide != QSizeF(600, 100)) {
            printf("FAILED: width-driven image should use divisor 3\n");
            return false;
        }

        QSizeF tall = ObjectConstraints::shrinkByIntegerDivisor(
            QSizeF(300, 2400), target);
        if (ObjectConstraints::integerShrinkDivisor(QSizeF(300, 2400), target) != 3
            || tall != QSizeF(100, 800)) {
            printf("FAILED: height-driven image should use divisor 3\n");
            return false;
        }

        const QSizeF bothSource(2400, 2700);
        QSizeF both = ObjectConstraints::shrinkByIntegerDivisor(bothSource, target);
        if (ObjectConstraints::integerShrinkDivisor(bothSource, target) != 4
            || both != QSizeF(600, 675)
            || !qFuzzyCompare(both.width() / both.height(),
                              bothSource.width() / bothSource.height())) {
            printf("FAILED: both-axis scaling should use one aspect-safe divisor\n");
            return false;
        }

        const QSizeF edgeless = ObjectConstraints::shrinkByIntegerDivisor(
            QSizeF(4000, 3000),
            QSizeF(Document::EDGELESS_TILE_SIZE, Document::EDGELESS_TILE_SIZE));
        if (edgeless != QSizeF(4000.0 / 6.0, 500)) {
            printf("FAILED: edgeless tile should use divisor 6\n");
            return false;
        }

        const QSizeF source(1200, 900);
        if (ObjectConstraints::shrinkByIntegerDivisor(source, QSizeF()) != source
            || ObjectConstraints::shrinkByIntegerDivisor(QSizeF(), target) != QSizeF()) {
            printf("FAILED: invalid dimensions should be left unchanged\n");
            return false;
        }

        const qreal nan = qQNaN();
        const qreal inf = qInf();
        const QSizeF nonFiniteResult =
            ObjectConstraints::shrinkByIntegerDivisor(QSizeF(nan, 100), target);
        if (!qIsNaN(nonFiniteResult.width()) || nonFiniteResult.height() != 100
            || ObjectConstraints::integerShrinkDivisor(
                   QSizeF(100, 100), QSizeF(inf, 100)) != 1) {
            printf("FAILED: non-finite dimensions should be treated as invalid "
                   "(result=%fx%f, divisor=%d)\n",
                   nonFiniteResult.width(), nonFiniteResult.height(),
                   ObjectConstraints::integerShrinkDivisor(
                       QSizeF(100, 100), QSizeF(inf, 100)));
            return false;
        }

        const QSizeF dprTagged = ObjectConstraints::freshImageInsertSize(
            QSizeF(2000, 1000), 2.0, 2.0, QSizeF(6000, 6000));
        const QSizeF ordinaryHiDpi = ObjectConstraints::freshImageInsertSize(
            QSizeF(2000, 1000), 1.0, 2.0, QSizeF(6000, 6000));
        if (dprTagged != QSizeF(1000, 500)
            || ordinaryHiDpi != QSizeF(1000, 500)) {
            printf("FAILED: image/display DPR normalization is incorrect\n");
            return false;
        }

        const QSizeF dprAndIntegerScaled = ObjectConstraints::freshImageInsertSize(
            QSizeF(4000, 3000), 2.0, 1.0, target);
        if (dprAndIntegerScaled != QSizeF(500, 375)) {
            printf("FAILED: DPR normalization must precede integer scaling\n");
            return false;
        }

        const QSizeF hugeSource(1e20, 5e19);
        const QSizeF hugeResult =
            ObjectConstraints::shrinkByIntegerDivisor(hugeSource, target);
        if (hugeResult.width() > 600 || hugeResult.height() > 800
            || !qFuzzyCompare(hugeResult.width() / hugeResult.height(),
                              hugeSource.width() / hugeSource.height())) {
            printf("FAILED: divisor overflow fallback did not enforce bounds\n");
            return false;
        }

        DocumentViewport noDocumentViewport;
        ImageObject unplaceableImage;
        unplaceableImage.setPixmap(QPixmap(100, 100));
        if (noDocumentViewport.prepareFreshImageForInsertion(unplaceableImage)) {
            printf("FAILED: image preparation should reject missing insertion bounds\n");
            return false;
        }

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Test original-byte assets, async persistence, and image undo recovery.
     */
    static bool testFastImageInsertionPipeline() {
        printf("  testFastImageInsertionPipeline... ");

        QImage source(96, 64, QImage::Format_ARGB32);
        source.fill(QColor(25, 90, 180, 255));
        QByteArray jpegBytes;
        QBuffer jpegBuffer(&jpegBytes);
        jpegBuffer.open(QIODevice::WriteOnly);
        if (!source.save(&jpegBuffer, "JPEG", 90)) {
            printf("FAILED: could not create JPEG fixture\n");
            return false;
        }
        const QImage decoded = QImage::fromData(jpegBytes);
        if (decoded.isNull()) {
            printf("FAILED: could not decode JPEG fixture\n");
            return false;
        }

        QTemporaryDir bundle;
        if (!bundle.isValid()) {
            printf("FAILED: could not create temporary bundle\n");
            return false;
        }
        auto doc = Document::createNew("Async Image Test");
        doc->setBundlePath(bundle.path());
        auto image = std::make_unique<ImageObject>();
        image->setSourceImage(decoded, jpegBytes, "jpeg");
        if (!image->imagePath.endsWith(".jpg")
            || QFileInfo(image->imagePath).completeBaseName().size() != 64
            || image->encodedAssetData() != jpegBytes) {
            printf("FAILED: original JPEG payload/extension was not preserved\n");
            return false;
        }
        if (image->toJsonWithoutRecoveryData().contains("embeddedImageData")) {
            printf("FAILED: metadata-only undo JSON embedded image bytes\n");
            return false;
        }

        ImageObject* imagePtr = image.get();
        doc->page(0)->addObject(std::move(image));
        if (!doc->enqueueImageAssetWrite(imagePtr, decoded)
            || !doc->savePage(0)) {
            printf("FAILED: save boundary did not flush background image write\n");
            return false;
        }
        QFile asset(imagePtr->fullPath(bundle.path()));
        if (!asset.open(QIODevice::ReadOnly) || asset.readAll() != jpegBytes
            || !imagePtr->assetPersisted()) {
            printf("FAILED: persisted asset differs from original JPEG bytes\n");
            return false;
        }

        // Save As must copy existing original-format assets before scanning for
        // unsaved images; otherwise it would re-encode this JPEG as a new PNG.
        const QString originalAssetName = imagePtr->imagePath;
        QTemporaryDir saveAsBundle;
        if (!saveAsBundle.isValid() || !doc->saveBundle(saveAsBundle.path())
            || imagePtr->imagePath != originalAssetName) {
            printf("FAILED: Save As changed the original image asset identity\n");
            return false;
        }
        QFile copiedAsset(imagePtr->fullPath(saveAsBundle.path()));
        if (!copiedAsset.open(QIODevice::ReadOnly)
            || copiedAsset.readAll() != jpegBytes) {
            printf("FAILED: Save As did not preserve original image bytes\n");
            return false;
        }
        copiedAsset.close();

        // If a persisted original-format asset disappears, recovery changes
        // the path to PNG. The owning page must be saved with that new path.
        if (!QFile::remove(imagePtr->fullPath(saveAsBundle.path()))
            || !doc->saveBundle(saveAsBundle.path())
            || !imagePtr->imagePath.endsWith(".png")) {
            printf("FAILED: missing original asset was not recovered as PNG\n");
            return false;
        }
        QFile recoveredPage(saveAsBundle.path() + "/pages/"
                            + doc->page(0)->uuid + ".json");
        if (!recoveredPage.open(QIODevice::ReadOnly)) {
            printf("FAILED: recovered image page JSON was not saved\n");
            return false;
        }
        const QJsonArray recoveredObjects =
            QJsonDocument::fromJson(recoveredPage.readAll())
                .object().value("objects").toArray();
        bool recoveredPathPersisted = false;
        for (const QJsonValue& value : recoveredObjects) {
            const QJsonObject object = value.toObject();
            if (object.value("id").toString() == imagePtr->id
                && object.value("imagePath").toString() == imagePtr->imagePath) {
                recoveredPathPersisted = true;
                break;
            }
        }
        if (!recoveredPathPersisted) {
            printf("FAILED: recovered image path was not persisted in page JSON\n");
            return false;
        }

        // A representative 4K clipboard payload must enqueue without waiting
        // for its lossless PNG compression.
        QImage fourK(3840, 2160, QImage::Format_ARGB32);
        fourK.fill(QColor(120, 45, 200, 180));
        auto clipboardImage = std::make_unique<ImageObject>();
        clipboardImage->setSourceImage(fourK);
        ImageObject* clipboardPtr = clipboardImage.get();
        doc->page(0)->addObject(std::move(clipboardImage));
        QElapsedTimer enqueueTimer;
        enqueueTimer.start();
        if (!doc->enqueueImageAssetWrite(clipboardPtr, fourK)) {
            printf("FAILED: 4K clipboard image was not queued\n");
            return false;
        }
        const qint64 enqueueMs = enqueueTimer.elapsed();
        if (enqueueMs > 1000) {
            printf("FAILED: 4K enqueue blocked for %lld ms\n",
                   static_cast<long long>(enqueueMs));
            return false;
        }
        if (!doc->flushPendingImageWrites()
            || !QFile::exists(clipboardPtr->fullPath(saveAsBundle.path()))) {
            printf("FAILED: 4K clipboard background encode/write failed\n");
            return false;
        }

        // Legacy recovery JSON had no format field and always contained PNG.
        QByteArray pngBytes;
        QBuffer pngBuffer(&pngBytes);
        pngBuffer.open(QIODevice::WriteOnly);
        source.save(&pngBuffer, "PNG");
        QJsonObject legacy = imagePtr->toJsonWithoutRecoveryData();
        legacy["imagePath"] = QString();
        legacy["embeddedImageData"] = QString::fromLatin1(pngBytes.toBase64());
        legacy.remove("embeddedImageFormat");
        ImageObject legacyImage;
        legacyImage.loadFromJson(legacy);
        if (!legacyImage.isLoaded()) {
            printf("FAILED: legacy embedded PNG no longer loads\n");
            return false;
        }

        // Unsaved-document undo must retain pixels without PNG/base64 work.
        auto undoDoc = Document::createNew("Image Undo Test");
        DocumentViewport viewport;
        viewport.resize(800, 600);
        viewport.setDocument(undoDoc.get());
        viewport.insertPreparedImage(decoded, jpegBytes, "jpeg");
        if (viewport.m_undoStack.isEmpty()
            || viewport.m_undoStack.top().objectImageEncodedData != jpegBytes
            || viewport.m_undoStack.top().objectData.contains("embeddedImageData")) {
            printf("FAILED: image insertion undo did not retain compact source data\n");
            return false;
        }
        const size_t insertedCount = undoDoc->page(0)->objects.size();
        viewport.undo();
        if (undoDoc->page(0)->objects.size() + 1 != insertedCount) {
            printf("FAILED: image insertion undo did not remove object\n");
            return false;
        }
        viewport.redo();
        InsertedObject* restored = undoDoc->page(0)->objectById(
            viewport.m_undoStack.top().objectId);
        auto* restoredImage = dynamic_cast<ImageObject*>(restored);
        if (!restoredImage || !restoredImage->isLoaded()) {
            printf("FAILED: image redo did not restore recovery pixels\n");
            return false;
        }

        viewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief Test group containment and the DocumentViewport wrappers.
     */
    static bool testObjectGroupContainment() {
        printf("  testObjectGroupContainment... ");
        
        const QSizeF pageSize(800, 1000);
        
        // Two objects 140pt apart, dragged 60pt past the right edge. The
        // correction is computed once for the group so the gap survives.
        QRectF a(760, 100, 100, 50);
        QRectF b(620, 100, 100, 50);
        QRectF group = a.united(b);
        QPointF correction = ObjectConstraints::correctionToPage(group, pageSize);
        
        QRectF movedA = a.translated(correction);
        QRectF movedB = b.translated(correction);
        if (!qFuzzyCompare(movedA.right(), 800.0)) {
            printf("FAILED: group should end flush with the right edge\n");
            return false;
        }
        if (!qFuzzyCompare(movedA.left() - movedB.left(), a.left() - b.left())) {
            printf("FAILED: group clamp should preserve relative layout\n");
            return false;
        }
        
        // A group already inside needs no correction
        if (!ObjectConstraints::correctionToPage(QRectF(10, 10, 100, 100), pageSize).isNull()) {
            printf("FAILED: in-bounds group should need no correction\n");
            return false;
        }
        
        // Viewport wrapper: clamps in paged mode...
        DocumentViewport viewport;
        viewport.resize(800, 600);
        auto doc = Document::createNew("Containment Test");
        viewport.setDocument(doc.get());
        
        QSizeF docPageSize = doc->pageSizeAt(0);
        QPointF clamped = viewport.clampObjectPositionToPage(0, QPointF(-50, -50),
                                                             QSizeF(40, 40));
        if (clamped != QPointF(0, 0)) {
            printf("FAILED: paged wrapper should clamp to the page origin\n");
            return false;
        }
        clamped = viewport.clampObjectPositionToPage(
            0, QPointF(docPageSize.width() + 100, 0), QSizeF(40, 40));
        if (!qFuzzyCompare(clamped.x(), docPageSize.width() - 40.0)) {
            printf("FAILED: paged wrapper should clamp to the right edge\n");
            return false;
        }
        
        // ...and leaves edgeless documents alone, since they have no edges
        auto edgelessDoc = Document::createNew("Edgeless Test", Document::Mode::Edgeless);
        viewport.setDocument(edgelessDoc.get());
        QPointF farOut(-5000, 9000);
        if (viewport.clampObjectPositionToPage(0, farOut, QSizeF(40, 40)) != farOut) {
            printf("FAILED: edgeless mode should not clamp\n");
            return false;
        }
        
        viewport.setDocument(nullptr);
        
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
        runTest(testObjectAlternateMouseMode, "testObjectAlternateMouseMode");
        runTest(testObjectGestureCancellation, "testObjectGestureCancellation");
        runTest(testObjectPageContainment, "testObjectPageContainment");
        runTest(testIntegerImageInsertScaling, "testIntegerImageInsertScaling");
        runTest(testFastImageInsertionPipeline, "testFastImageInsertionPipeline");
        runTest(testObjectGroupContainment, "testObjectGroupContainment");
        
        printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);
        // The caller goes on to open a window and block in the event loop, so
        // without an explicit flush these results sit in the buffer and are
        // lost whenever the GUI phase is killed rather than closed normally.
        fflush(stdout);
        
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
