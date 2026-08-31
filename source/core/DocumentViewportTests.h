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
#include "../objects/LinkObject.h"
#include "../objects/OcrTextObject.h"
#include "../ui/panels/InlineTextBoxEditor.h"
#include "../ui/panels/LinkObjectBar.h"
#include "../ui/panels/TextBoxFormatBar.h"
#include "../ui/widgets/ActionBarButton.h"
#include "../ui/widgets/ColorPresetButton.h"
#include "../ui/widgets/LinkSlotButton.h"
#include "../ui/widgets/ToggleButton.h"  // Contains SubToolbarToggle
#include "../../markdown/qmarkdowntextedit.h"
#include "../strokes/VectorStroke.h"
#include "../strokes/StrokePoint.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSlider>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolButton>
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
     * @brief Test that the empty space around pages acts as the Pan tool.
     */
    static bool testOffPagePanFromEmptySpace() {
        printf("  testOffPagePanFromEmptySpace... ");
        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        const bool savedSetting = DocumentViewport::panOutsidePagesEnabled();
        DocumentViewport::setPanOutsidePagesEnabled(true);

        auto doc = Document::createNew("Off-page pan");
        for (int i = 0; i < 4; ++i) {
            doc->addPage();
        }

        DocumentViewport viewport;
        viewport.resize(1200, 800);
        // beginPanGesture() refuses to run on a hidden widget, so the pan path
        // only exists once the viewport has been (offscreen-) shown.
        viewport.setAttribute(Qt::WA_DontShowOnScreen, true);
        viewport.show();
        QApplication::processEvents();
        viewport.setDocument(doc.get());
        viewport.setZoomLevel(1.0);
        viewport.setPanOffset(QPointF(0, 0));

        auto makeEvent = [&](PointerEvent::Type type, QPointF pos) {
            PointerEvent pe;
            pe.type = type;
            pe.source = PointerEvent::Stylus;
            pe.viewportPos = pos;
            pe.button = Qt::LeftButton;
            pe.pressure = 1.0;
            pe.pageHit = viewport.viewportToPage(pos);
            return pe;
        };

        const QRectF page0 = viewport.pageRect(0);
        // Zoom is 1.0, so a document-space offset is also a viewport-pixel one:
        // 60 units left of the page clears the tolerance band, 2 does not.
        const QPointF farOffPage = viewport.documentToViewport(
            QPointF(page0.left() - 60.0, page0.center().y()));
        const QPointF nearMiss = viewport.documentToViewport(
            QPointF(page0.left() - 2.0, page0.center().y()));
        const QPointF onPage = viewport.documentToViewport(page0.center());

        // ----- A press in the empty space pans instead of drawing -----
        viewport.setCurrentTool(ToolType::Pen);
        viewport.setPanOffset(QPointF(0, 0));
        viewport.handlePointerEvent(makeEvent(PointerEvent::Press, farOffPage));
        if (!viewport.m_offPagePanArmed)
            return fail("off-page press did not arm the pan");
        if (viewport.m_isDrawing)
            return fail("off-page press started a stroke");
        if (viewport.m_offPagePanDragging)
            return fail("pan started before the pointer left the tap slop");

        viewport.handlePointerEvent(
            makeEvent(PointerEvent::Move, farOffPage + QPointF(0, -150)));
        if (!viewport.m_offPagePanDragging)
            return fail("moving past the slop did not start the pan");
        viewport.handlePointerEvent(
            makeEvent(PointerEvent::Release, farOffPage + QPointF(0, -150)));
        if (viewport.m_offPagePanArmed || viewport.m_offPagePanDragging)
            return fail("release left off-page pan state behind");
        if (viewport.panOffset().y() < 100.0)
            return fail("off-page drag did not scroll the viewport");

        // ----- The setting gates the whole feature -----
        DocumentViewport::setPanOutsidePagesEnabled(false);
        viewport.handlePointerEvent(makeEvent(PointerEvent::Press, farOffPage));
        const bool armedWhileDisabled = viewport.m_offPagePanArmed;
        viewport.handlePointerEvent(makeEvent(PointerEvent::Release, farOffPage));
        DocumentViewport::setPanOutsidePagesEnabled(true);
        if (armedWhileDisabled)
            return fail("disabled setting still armed an off-page pan");

        // ----- A near-miss at the page edge still belongs to the tool -----
        viewport.handlePointerEvent(makeEvent(PointerEvent::Press, nearMiss));
        const bool armedOnNearMiss = viewport.m_offPagePanArmed;
        viewport.handlePointerEvent(makeEvent(PointerEvent::Release, nearMiss));
        if (armedOnNearMiss)
            return fail("a near-miss at the page edge should reach the tool");

        // ----- The edgeless canvas has no space outside a page -----
        auto edgelessDoc =
            Document::createNew("Off-page pan edgeless", Document::Mode::Edgeless);
        DocumentViewport edgelessViewport;
        edgelessViewport.resize(1200, 800);
        edgelessViewport.setAttribute(Qt::WA_DontShowOnScreen, true);
        edgelessViewport.show();
        QApplication::processEvents();
        edgelessViewport.setDocument(edgelessDoc.get());
        PointerEvent edgelessPress;
        edgelessPress.type = PointerEvent::Press;
        edgelessPress.source = PointerEvent::Stylus;
        edgelessPress.button = Qt::LeftButton;
        edgelessPress.viewportPos = QPointF(-400.0, 120.0);
        edgelessPress.pageHit =
            edgelessViewport.viewportToPage(edgelessPress.viewportPos);
        if (edgelessViewport.shouldArmOffPagePan(edgelessPress))
            return fail("edgeless canvas should never arm an off-page pan");
        edgelessViewport.hide();
        edgelessViewport.setDocument(nullptr);

        // ----- An in-progress lasso keeps going outside the page -----
        viewport.setCurrentTool(ToolType::Lasso);
        viewport.handlePointerEvent(makeEvent(PointerEvent::Press, onPage));
        if (!viewport.m_isDrawingLasso)
            return fail("lasso press on a page did not start a path");
        const int lassoPointsOnPage = viewport.m_lassoPath.size();
        viewport.handlePointerEvent(makeEvent(PointerEvent::Move, farOffPage));
        if (viewport.m_offPagePanArmed)
            return fail("an in-progress lasso was hijacked by the off-page pan");
        if (viewport.m_lassoPath.size() <= lassoPointsOnPage)
            return fail("lasso path did not continue outside the page");
        viewport.handlePointerEvent(makeEvent(PointerEvent::Release, farOffPage));

        // ----- A hardware eraser off-page pans and erases nothing -----
        viewport.setCurrentTool(ToolType::Pen);
        Page* firstPage = doc->page(0);
        if (!firstPage || !firstPage->activeLayer())
            return fail("first page has no active layer");
        VectorStroke stroke;
        stroke.color = Qt::black;
        stroke.baseThickness = 4.0;
        for (int i = 0; i <= 10; ++i) {
            StrokePoint pt;
            pt.pos = QPointF(60.0 + i * 20.0, 120.0);
            pt.pressure = 1.0;
            stroke.points.append(pt);
        }
        stroke.updateBoundingBox();
        firstPage->activeLayer()->addStroke(stroke);
        const int strokesBefore = firstPage->activeLayer()->strokes().size();

        viewport.setPanOffset(QPointF(0, 0));
        PointerEvent eraserPress = makeEvent(PointerEvent::Press, farOffPage);
        eraserPress.isEraser = true;
        viewport.handlePointerEvent(eraserPress);
        if (!viewport.m_offPagePanArmed)
            return fail("hardware eraser press off-page did not arm the pan");
        PointerEvent eraserMove =
            makeEvent(PointerEvent::Move, farOffPage + QPointF(0, -130));
        eraserMove.isEraser = true;
        viewport.handlePointerEvent(eraserMove);
        PointerEvent eraserRelease =
            makeEvent(PointerEvent::Release, farOffPage + QPointF(0, -130));
        eraserRelease.isEraser = true;
        viewport.handlePointerEvent(eraserRelease);
        if (viewport.m_isDrawingEraserLasso)
            return fail("hardware eraser off-page started an eraser lasso");
        if (firstPage->activeLayer()->strokes().size() != strokesBefore)
            return fail("hardware eraser off-page deleted strokes while panning");
        if (viewport.panOffset().y() < 80.0)
            return fail("eraser branch swallowed the off-page pan move");

        // ----- A tap in the empty space still drops the selection -----
        auto image = std::make_unique<ImageObject>();
        image->position = QPointF(50.0, 50.0);
        image->size = QSizeF(120.0, 90.0);
        ImageObject* imageRaw = image.get();
        firstPage->addObject(std::move(image));

        viewport.setCurrentTool(ToolType::ObjectSelect);
        viewport.selectObject(imageRaw, false);
        if (viewport.m_selectedObjects.isEmpty())
            return fail("object was not selected for the tap test");
        viewport.handlePointerEvent(makeEvent(PointerEvent::Press, farOffPage));
        if (!viewport.m_offPagePanArmed)
            return fail("off-page press under ObjectSelect did not arm the pan");
        viewport.handlePointerEvent(makeEvent(PointerEvent::Release, farOffPage));
        if (!viewport.m_selectedObjects.isEmpty())
            return fail("off-page tap did not clear the object selection");

        // ----- Handles that hang off the page still belong to the tool -----
        viewport.selectObject(imageRaw, false);
        const QRectF bounds = viewport.objectBoundsInViewport(imageRaw);
        if (bounds.isEmpty())
            return fail("selected object had no viewport bounds");
        if (viewport.objectHandleAtPoint(bounds.topLeft())
                == DocumentViewport::HandleHit::None)
            return fail("object corner was not recognised as a resize handle");
        if (!viewport.toolClaimsOffPagePress(
                makeEvent(PointerEvent::Press, bounds.topLeft())))
            return fail("resize handle press was not claimed by the tool");

        viewport.deselectAllObjects();
        viewport.hide();
        viewport.setDocument(nullptr);
        DocumentViewport::setPanOutsidePagesEnabled(savedSetting);

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

    static bool testTextBoxCreationAndWidthResize() {
        printf("  testTextBoxCreationAndWidthResize... ");

        auto doc = Document::createNew("Text geometry");
        DocumentViewport viewport;
        viewport.resize(1000, 800);
        viewport.setDocument(doc.get());

        const QPointF start(300.0, 240.0);
        const QRectF clickRect = viewport.proposedTextBoxCreationRect(
            start, QPointF(start.x() + 2.0, start.y() + 500.0), 0);
        const QRectF clickRectOtherY =
            viewport.proposedTextBoxCreationRect(
                start, QPointF(start.x() + 2.0, start.y() - 500.0), 0);
        if (!qFuzzyCompare(clickRect.width(),
                           TextBoxObject::DEFAULT_CREATION_WIDTH)
            || !qFuzzyCompare(clickRect.center().x(), start.x())
            || !qFuzzyCompare(clickRect.center().y(), start.y())
            || !qFuzzyCompare(clickRect.height(),
                              clickRectOtherY.height())) {
            printf("FAILED: click creation geometry is not centered/measured\n");
            return false;
        }

        const QRectF dragRect = viewport.proposedTextBoxCreationRect(
            start, QPointF(570.0, 740.0), 0);
        const QRectF dragRectOtherY = viewport.proposedTextBoxCreationRect(
            start, QPointF(570.0, -400.0), 0);
        if (!qFuzzyCompare(dragRect.width(), 270.0)
            || !qFuzzyCompare(dragRect.left(), start.x())
            || !qFuzzyCompare(dragRect.height(), dragRectOtherY.height())) {
            printf("FAILED: horizontal drag creation used vertical distance\n");
            return false;
        }

        viewport.createTextBoxAtRect(0, clickRect, QPointF());
        Page* page = doc->page(0);
        auto* textBox = page && !page->objects.empty()
            ? dynamic_cast<TextBoxObject*>(page->objects.back().get())
            : nullptr;
        if (!textBox
            || textBox->textLayoutVersion
                != TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION
            || !qFuzzyCompare(textBox->fontSize,
                              TextBoxObject::DEFAULT_BASE_FONT_SIZE)
            || !qFuzzyCompare(textBox->size.width(),
                              TextBoxObject::DEFAULT_CREATION_WIDTH)
            || textBox->size.height() <=
                TextBoxObject::DEFAULT_BASE_FONT_SIZE) {
            printf("FAILED: creation did not insert a measured version-1 box\n");
            return false;
        }

        viewport.m_selectedObjects = {textBox};
        textBox->rotation = 0.0;
        const QRectF bounds = viewport.objectBoundsInViewport(textBox);
        if (!viewport.hasActiveInlineTextEdit()
            || !viewport.m_inlineTextBoxEditor
            || viewport.objectHandleAtPoint(
                QPointF(bounds.left(), bounds.center().y()))
                != DocumentViewport::HandleHit::None) {
            printf("FAILED: new text box did not enter handle-free inline editing\n");
            return false;
        }
        viewport.handleInlineTextSourceChanged(QStringLiteral("Text"));
        viewport.commitInlineTextEdit();
        if (viewport.hasActiveInlineTextEdit() || !viewport.canUndo()) {
            printf("FAILED: new text box edit did not commit one insert action\n");
            return false;
        }
        const QRectF committedBounds =
            viewport.objectBoundsInViewport(textBox);
        if (viewport.objectHandleAtPoint(
                QPointF(committedBounds.left(),
                        committedBounds.center().y()))
                != DocumentViewport::HandleHit::Left
            || viewport.objectHandleAtPoint(
                QPointF(committedBounds.right(),
                        committedBounds.center().y()))
                != DocumentViewport::HandleHit::Right
            || viewport.objectHandleAtPoint(
                QPointF(committedBounds.center().x(),
                        committedBounds.bottom()))
                != DocumentViewport::HandleHit::None) {
            printf("FAILED: user text box exposed non-horizontal handles\n");
            return false;
        }

        auto ocr = std::make_unique<OcrTextObject>();
        ocr->position = QPointF(50.0, 50.0);
        ocr->size = QSizeF(180.0, 80.0);
        OcrTextObject* ocrRaw = ocr.get();
        page->addObject(std::move(ocr));
        viewport.m_selectedObjects = {ocrRaw};
        const QRectF ocrBounds = viewport.objectBoundsInViewport(ocrRaw);
        if (viewport.objectHandleAtPoint(
                QPointF(ocrBounds.center().x(), ocrBounds.bottom()))
                != DocumentViewport::HandleHit::Bottom) {
            printf("FAILED: OCR resize handles changed\n");
            return false;
        }

        auto prepareResize = [&](TextBoxObject* box,
                                 DocumentViewport::HandleHit handle) {
            viewport.m_selectedObjects = {box};
            viewport.m_isResizingObject = true;
            viewport.m_objectResizeHandle = handle;
            viewport.m_resizeOriginalSize = box->size;
            viewport.m_resizeOriginalPosition = box->position;
            viewport.m_resizeOriginalRotation = box->rotation;
            viewport.m_resizeObjectPageIndex = 0;
            viewport.m_hasResizeTextBoxState = true;
            viewport.m_textBoxResizeActivated = false;
            viewport.m_textBoxResizeChanged = false;
            viewport.m_resizeOriginalTextBoxState = box->captureState();
            viewport.m_resizeBaseTextBoxState =
                viewport.m_resizeOriginalTextBoxState;
            viewport.m_resizeLastAcceptedTextBoxState =
                viewport.m_resizeOriginalTextBoxState;
            viewport.m_resizeObjectDocCenter =
                viewport.pagePosition(0) + box->position
                + QPointF(box->size.width() / 2.0,
                          box->size.height() / 2.0);
        };
        auto pointerForLocalX = [&](const TextBoxObject* box, qreal localX) {
            const qreal radians = qDegreesToRadians(box->rotation);
            const QPointF delta(localX * qCos(radians),
                                localX * qSin(radians));
            return viewport.documentToViewport(
                viewport.m_resizeObjectDocCenter + delta);
        };

        textBox->text = QStringLiteral(
            "one two three four five six seven eight nine ten");
        textBox->position = QPointF(100.0, 120.0);
        textBox->rotation = 30.0;
        textBox->reflowToWidth(220.0);
        const TextBoxState beforeResize = textBox->captureState();
        prepareResize(textBox, DocumentViewport::HandleHit::Right);
        viewport.updateObjectResize(pointerForLocalX(textBox, 190.0));
        const TextBoxState afterResize = textBox->captureState();
        if (!viewport.m_textBoxResizeChanged
            || !qFuzzyCompare(afterResize.size.width(), 300.0)
            || !qFuzzyCompare(afterResize.fontSize, beforeResize.fontSize)) {
            printf("FAILED: width resize changed font size or wrong width\n");
            return false;
        }
        auto worldPoint = [](const TextBoxState& state,
                             const QPointF& local) {
            const QPointF center(state.size.width() / 2.0,
                                 state.size.height() / 2.0);
            const QPointF delta = local - center;
            const qreal radians = qDegreesToRadians(state.rotation);
            return state.position + center + QPointF(
                delta.x() * qCos(radians) - delta.y() * qSin(radians),
                delta.x() * qSin(radians) + delta.y() * qCos(radians));
        };
        if (QLineF(worldPoint(beforeResize, QPointF(0.0, 0.0)),
                   worldPoint(afterResize, QPointF(0.0, 0.0))).length()
            > 0.01) {
            printf("FAILED: rotated right resize moved local top-left anchor\n");
            return false;
        }

        viewport.cancelObjectPointerGesture();
        if (textBox->captureState().textLayoutVersion
                != beforeResize.textLayoutVersion
            || textBox->position != beforeResize.position
            || textBox->size != beforeResize.size) {
            printf("FAILED: resize cancel did not restore complete state\n");
            return false;
        }

        int layoutCommitSignals = 0;
        int pageModifiedSignals = 0;
        QObject::connect(&viewport,
                         &DocumentViewport::textBoxLayoutCommitted,
                         &viewport, [&layoutCommitSignals]() {
            ++layoutCommitSignals;
        });
        QObject::connect(&viewport, &DocumentViewport::pageModified,
                         &viewport, [&pageModifiedSignals](int) {
            ++pageModifiedSignals;
        });
        prepareResize(textBox, DocumentViewport::HandleHit::Right);
        viewport.updateObjectResize(pointerForLocalX(textBox, 190.0));
        const TextBoxState committedResize = textBox->captureState();
        viewport.pushObjectResizeUndo(
            textBox, beforeResize.position, beforeResize.size,
            beforeResize.rotation, true, &beforeResize);
        viewport.m_isResizingObject = false;
        viewport.m_hasResizeTextBoxState = false;
        viewport.undo();
        if (textBox->position != beforeResize.position
            || textBox->size != beforeResize.size
            || textBox->textLayoutVersion
                != beforeResize.textLayoutVersion) {
            printf("FAILED: text resize undo did not restore full state\n");
            return false;
        }
        viewport.redo();
        if (textBox->position != committedResize.position
            || textBox->size != committedResize.size
            || textBox->textLayoutVersion
                != committedResize.textLayoutVersion
            || layoutCommitSignals != 2
            || pageModifiedSignals < 2) {
            printf("FAILED: text resize redo/signals did not restore full state\n");
            return false;
        }

        TextBoxState legacy = beforeResize;
        legacy.textLayoutVersion = 0;
        legacy.fontSize = 13.0;
        legacy.size = QSizeF(220.0, 60.0);
        legacy.position = QPointF(120.0, 140.0);
        legacy.rotation = 0.0;
        textBox->applyState(legacy);
        prepareResize(textBox, DocumentViewport::HandleHit::Right);
        viewport.updateObjectResize(pointerForLocalX(textBox, 160.0));
        if (textBox->textLayoutVersion
            != TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION) {
            printf("FAILED: legacy box did not convert on width resize\n");
            return false;
        }
        viewport.cancelObjectPointerGesture();
        if (textBox->textLayoutVersion != 0
            || !qFuzzyCompare(textBox->fontSize, legacy.fontSize)
            || textBox->position != legacy.position
            || textBox->size != legacy.size) {
            printf("FAILED: legacy conversion did not roll back on cancel\n");
            return false;
        }

        // Narrowing a box flush with the page bottom grows its derived height;
        // the full proposal must be rejected instead of partially applying it.
        textBox->rotation = 0.0;
        textBox->text = QString(160, QLatin1Char('x'));
        textBox->reflowToWidth(500.0);
        textBox->position = QPointF(
            50.0, page->size.height() - textBox->size.height());
        const TextBoxState beforeRejected = textBox->captureState();
        prepareResize(textBox, DocumentViewport::HandleHit::Right);
        viewport.updateObjectResize(pointerForLocalX(
            textBox, TextBoxObject::MINIMUM_WIDTH
                - beforeRejected.size.width() / 2.0));
        if (textBox->position != beforeRejected.position
            || textBox->size != beforeRejected.size
            || viewport.m_objectGeometryFeedbackText.isEmpty()) {
            printf("FAILED: page-bottom resize was not atomically rejected\n");
            return false;
        }
        viewport.cancelObjectPointerGesture();

        TextBoxState overflowing = beforeRejected;
        overflowing.position.setY(page->size.height()
                                  - overflowing.size.height() + 20.0);
        TextBoxState lessOverflow = overflowing;
        lessOverflow.position.ry() -= 10.0;
        TextBoxState moreOverflow = overflowing;
        moreOverflow.position.ry() += 10.0;
        if (!viewport.textBoxGeometryProposalAllowed(
                overflowing, lessOverflow, 0)
            || viewport.textBoxGeometryProposalAllowed(
                overflowing, moreOverflow, 0)) {
            printf("FAILED: existing overflow policy is not monotonic\n");
            return false;
        }

        auto edgeless =
            Document::createNew("Text edgeless", Document::Mode::Edgeless);
        viewport.setDocument(edgeless.get());
        const QRectF uncapped = viewport.proposedTextBoxCreationRect(
            QPointF(10.0, 20.0), QPointF(5010.0, 9999.0), -1);
        if (!qFuzzyCompare(uncapped.width(), 5000.0)) {
            printf("FAILED: edgeless creation width was capped\n");
            return false;
        }

        Page* originTile = edgeless->getOrCreateTile(0, 0);
        auto tileText = std::make_unique<TextBoxObject>();
        tileText->textLayoutVersion =
            TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
        tileText->fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
        tileText->text = QStringLiteral("tile relocation");
        tileText->position = QPointF(
            Document::EDGELESS_TILE_SIZE - 18.0, 80.0);
        tileText->reflowToWidth(100.0);
        TextBoxObject* tileTextRaw = tileText.get();
        originTile->addObject(std::move(tileText));
        const TextBoxState tileOldState = tileTextRaw->captureState();

        viewport.m_selectedObjects = {tileTextRaw};
        viewport.m_isResizingObject = true;
        viewport.m_objectResizeHandle =
            DocumentViewport::HandleHit::Left;
        viewport.m_resizeOriginalSize = tileTextRaw->size;
        viewport.m_resizeOriginalPosition = tileTextRaw->position;
        viewport.m_resizeOriginalRotation = tileTextRaw->rotation;
        viewport.m_resizeObjectPageIndex = -1;
        viewport.m_dragObjectTileCoord = {0, 0};
        viewport.m_hasResizeTextBoxState = true;
        viewport.m_textBoxResizeActivated = false;
        viewport.m_textBoxResizeChanged = false;
        viewport.m_resizeOriginalTextBoxState = tileOldState;
        viewport.m_resizeBaseTextBoxState = tileOldState;
        viewport.m_resizeLastAcceptedTextBoxState = tileOldState;
        viewport.m_resizeObjectDocCenter =
            tileTextRaw->position
            + QPointF(tileTextRaw->size.width() / 2.0,
                      tileTextRaw->size.height() / 2.0);
        viewport.updateObjectResize(pointerForLocalX(
            tileTextRaw,
            tileOldState.size.width() / 2.0
                - TextBoxObject::MINIMUM_WIDTH));
        if (tileTextRaw->position.x()
            < Document::EDGELESS_TILE_SIZE) {
            printf("FAILED: left resize did not cross tile boundary\n");
            return false;
        }
        viewport.relocateObjectsToCorrectTiles();
        const auto newTileCoord =
            edgeless->tileCoordForPoint(QPointF(
                Document::EDGELESS_TILE_SIZE + 1.0, 80.0));
        Page* newTile =
            edgeless->getTile(newTileCoord.first, newTileCoord.second);
        if (!newTile || !newTile->objectById(tileTextRaw->id)) {
            printf("FAILED: edgeless resize did not relocate by top-left\n");
            return false;
        }
        const TextBoxState tileNewState = tileTextRaw->captureState();
        viewport.pushObjectResizeUndo(
            tileTextRaw, tileOldState.position, tileOldState.size,
            tileOldState.rotation, true, &tileOldState);
        viewport.m_isResizingObject = false;
        viewport.m_hasResizeTextBoxState = false;
        viewport.undo();
        originTile = edgeless->getTile(0, 0);
        if (!originTile || !originTile->objectById(tileTextRaw->id)
            || tileTextRaw->position != tileOldState.position) {
            printf("FAILED: cross-tile resize undo did not restore ownership\n");
            return false;
        }
        viewport.redo();
        newTile = edgeless->getTile(
            newTileCoord.first, newTileCoord.second);
        if (!newTile || !newTile->objectById(tileTextRaw->id)
            || tileTextRaw->position != tileNewState.position) {
            printf("FAILED: cross-tile resize redo did not restore ownership\n");
            return false;
        }

        viewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }

    static bool testInlineTextBoxEditing() {
        printf("  testInlineTextBoxEditing... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };
        auto doc = Document::createNew("Inline text editing");
        DocumentViewport viewport;
        viewport.resize(1000, 800);
        viewport.setDocument(doc.get());
        Page* page = doc->page(0);
        if (!page)
            return fail("missing test page");

        QSignalSpy documentSpy(
            &viewport, &DocumentViewport::documentModified);
        QSignalSpy pageSpy(
            &viewport, &DocumentViewport::pageModified);
        QSignalSpy layoutSpy(
            &viewport, &DocumentViewport::textBoxLayoutCommitted);

        viewport.createTextBoxAtRect(
            0, QRectF(120.0, 140.0,
                      TextBoxObject::DEFAULT_CREATION_WIDTH, 1.0),
            QPointF());
        auto* box = page->objects.empty()
            ? nullptr
            : dynamic_cast<TextBoxObject*>(page->objects.back().get());
        if (!box || !viewport.hasActiveInlineTextEdit()
            || !viewport.m_undoStack.isEmpty())
            return fail("new box was not a deferred inline session");

        const QString boxId = box->id;
        const qreal initialHeight = box->size.height();
        const QString initialSource =
            QStringLiteral("# Heading\nFirst line\nSecond line");
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            initialSource);
        if (box->text != initialSource
            || box->size.height() <= initialHeight)
            return fail("accepted source did not live-reflow");
        if (documentSpy.count() != 0 || pageSpy.count() != 0
            || layoutSpy.count() != 0)
            return fail("live keystrokes emitted commit invalidation");

        const QRect editorGeometry =
            viewport.m_inlineTextBoxEditor->geometry();
        box->rotation = 18.0;
        viewport.updateInlineTextEditorGeometry();
        const QRect rotatedGeometry =
            viewport.m_inlineTextBoxEditor->geometry();
        viewport.setZoomLevel(1.25);
        if (rotatedGeometry == editorGeometry
            || viewport.m_inlineTextBoxEditor->geometry()
                == rotatedGeometry)
            return fail("editor geometry did not track rotation/zoom");

        const QString beforeLocalUndo = box->text;
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral(" extra"));
        viewport.m_inlineTextBoxEditor->editor()->undo();
        if (box->text != beforeLocalUndo
            || !viewport.hasActiveInlineTextEdit())
            return fail("local editor undo escaped the edit session");

        QKeyEvent commitKey(
            QEvent::KeyPress, Qt::Key_Return,
            Qt::ControlModifier);
        QApplication::sendEvent(
            viewport.m_inlineTextBoxEditor->editor(), &commitKey);
        if (viewport.hasActiveInlineTextEdit()
            || viewport.m_undoStack.size() != 1
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectInsert
            || documentSpy.count() != 1
            || pageSpy.count() != 1
            || layoutSpy.count() != 1)
            return fail("Ctrl+Enter did not commit one insert transaction");

        viewport.undo();
        if (page->objectById(boxId))
            return fail("new-box insertion undo did not remove object");
        viewport.redo();
        box = dynamic_cast<TextBoxObject*>(page->objectById(boxId));
        if (!box || box->text != beforeLocalUndo)
            return fail("new-box insertion redo lost final state");

        const TextBoxState editStart = box->captureState();
        viewport.startInlineTextEdit(box, false);
        viewport.m_inlineTextBoxEditor->editor()->moveCursor(
            QTextCursor::End);
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral("\nThird line"));
        const TextBoxState editEnd = box->captureState();
        documentSpy.clear();
        pageSpy.clear();
        layoutSpy.clear();
        viewport.commitInlineTextEdit();
        if (viewport.m_undoStack.top().type
                != UndoAction::ObjectTextEdit
            || !viewport.m_undoStack.top().objectHasTextBoxState
            || documentSpy.count() != 1
            || pageSpy.count() != 1
            || layoutSpy.count() != 1)
            return fail("existing edit was not one full-state transaction");
        viewport.undo();
        box = dynamic_cast<TextBoxObject*>(page->objectById(boxId));
        if (!box || !DocumentViewport::textBoxStatesEqual(
                        box->captureState(), editStart))
            return fail("text edit undo did not restore full state");
        viewport.redo();
        box = dynamic_cast<TextBoxObject*>(page->objectById(boxId));
        if (!box || !DocumentViewport::textBoxStatesEqual(
                        box->captureState(), editEnd))
            return fail("text edit redo did not restore full state");

        box->rotation = 0.0;
        box->position.setY(
            page->size.height() - box->size.height());
        const TextBoxState overflowStart = box->captureState();
        viewport.startInlineTextEdit(box, false);
        auto* editor = viewport.m_inlineTextBoxEditor->editor();
        editor->moveCursor(QTextCursor::End);
        const int caretBeforeReject =
            editor->textCursor().position();
        editor->insertPlainText(
            QStringLiteral("\nRejected line\nRejected line"));
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), overflowStart)
            || editor->toPlainText() != overflowStart.text
            || editor->textCursor().position()
                != caretBeforeReject
            || viewport.m_objectGeometryFeedbackText.isEmpty())
            return fail("paged overflow was not rejected atomically");
        viewport.cancelInlineTextEdit();

        viewport.startInlineTextEdit(box, false);
        viewport.m_inlineTextBoxEditor->editor()->moveCursor(
            QTextCursor::End);
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral(" outside"));
        const QPointF outsidePoint(2.0, 2.0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QMouseEvent outsidePress(
            QEvent::MouseButtonPress, outsidePoint, outsidePoint,
            QPointF(viewport.mapToGlobal(outsidePoint.toPoint())),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
#else
        QMouseEvent outsidePress(
            QEvent::MouseButtonPress, outsidePoint,
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
#endif
        viewport.mousePressEvent(&outsidePress);
        if (viewport.hasActiveInlineTextEdit()
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectTextEdit)
            return fail("outside canvas click did not commit");

        auto legacy = std::make_unique<TextBoxObject>();
        legacy->text = QStringLiteral("Legacy");
        legacy->position = QPointF(40.0, 40.0);
        legacy->size = QSizeF(180.0, 44.0);
        TextBoxObject* legacyRaw = legacy.get();
        page->addObject(std::move(legacy));
        const TextBoxState legacyStart = legacyRaw->captureState();
        viewport.startInlineTextEdit(legacyRaw, false);
        if (!legacyRaw->usesCurrentLayout())
            return fail("legacy box did not upgrade on edit entry");
        viewport.cancelInlineTextEdit();
        if (!DocumentViewport::textBoxStatesEqual(
                legacyRaw->captureState(), legacyStart))
            return fail("legacy edit cancellation did not restore version 0");

        const int undoCountBeforeEmpty =
            viewport.m_undoStack.size();
        const int objectCountBeforeEmpty =
            static_cast<int>(page->objects.size());
        viewport.createTextBoxAtRect(
            0, QRectF(300.0, 200.0, 220.0, 1.0), QPointF());
        QKeyEvent cancelKey(
            QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(
            viewport.m_inlineTextBoxEditor->editor(), &cancelKey);
        if (static_cast<int>(page->objects.size())
                != objectCountBeforeEmpty
            || viewport.m_undoStack.size()
                != undoCountBeforeEmpty)
            return fail("empty new-box cancellation created history");

        box = dynamic_cast<TextBoxObject*>(page->objectById(boxId));
        viewport.m_selectedObjects = {box};
        viewport.startInlineTextEdit(box, false);
        viewport.m_inlineTextBoxEditor->editor()->moveCursor(
            QTextCursor::End);
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral(" draft"));
        viewport.deleteSelectedObjects();
        if (viewport.hasActiveInlineTextEdit()
            || page->objectById(boxId)
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectDelete)
            return fail("deletion did not safely terminate edit session");
        viewport.undo();
        box = dynamic_cast<TextBoxObject*>(page->objectById(boxId));
        if (!box || !box->text.endsWith(QStringLiteral(" draft")))
            return fail("delete undo lost accepted draft state");

        viewport.setCurrentTool(ToolType::ObjectSelect);
        viewport.startInlineTextEdit(box, false);
        viewport.m_inlineTextBoxEditor->editor()->moveCursor(
            QTextCursor::End);
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral(" tool"));
        viewport.setCurrentTool(ToolType::Pen);
        if (viewport.hasActiveInlineTextEdit()
            || !box->text.endsWith(QStringLiteral(" tool"))
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectTextEdit)
            return fail("tool change did not commit active edit");
        viewport.setCurrentTool(ToolType::ObjectSelect);

        auto ocr = std::make_unique<OcrTextObject>();
        ocr->text = QStringLiteral("OCR");
        OcrTextObject* ocrRaw = ocr.get();
        page->addObject(std::move(ocr));
        viewport.startInlineTextEdit(ocrRaw, false);
        if (viewport.hasActiveInlineTextEdit())
            return fail("OCR object entered user inline editor");

        viewport.m_selectedObjects.clear();
        viewport.startInlineTextEdit(box, false);
        page->removeObject(boxId);
        viewport.handleInlineTextSourceChanged(
            QStringLiteral("stale target"));
        if (viewport.hasActiveInlineTextEdit())
            return fail("missing target did not tear down safely");

        auto edgeless = Document::createNew(
            "Inline edgeless", Document::Mode::Edgeless);
        viewport.setDocument(edgeless.get());
        viewport.createTextBoxAtRect(
            -1, QRectF(20.0, 20.0, 100.0, 1.0), QPointF());
        auto* tileBox = viewport.resolveInlineTextBox();
        if (!tileBox)
            return fail("edgeless inline target was not resolvable");
        const QString manyLines =
            QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl");
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            manyLines);
        if (tileBox->text != manyLines
            || tileBox->size.height() <= initialHeight)
            return fail("edgeless growth was incorrectly capped");
        viewport.cancelInlineTextEdit();
        viewport.setDocument(nullptr);

        printf("PASSED\n");
        return true;
    }

    static bool testTextBoxFormattingBar() {
        printf("  testTextBoxFormattingBar... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };
        auto doc = Document::createNew("Text formatting");
        DocumentViewport viewport;
        viewport.resize(1100, 820);
        viewport.setDocument(doc.get());
        viewport.setCurrentTool(ToolType::ObjectSelect);
        Page* page = doc->page(0);
        if (!page)
            return fail("missing formatting test page");

        auto object = std::make_unique<TextBoxObject>();
        object->textLayoutVersion =
            TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
        object->text = QStringLiteral("One\nTwo\nThree");
        object->fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
        object->position = QPointF(180.0, 250.0);
        object->size = QSizeF(240.0, 1.0);
        object->reflowToWidth(object->size.width());
        TextBoxObject* box = object.get();
        page->addObject(std::move(object));

        viewport.selectObject(box, false);
        if (!viewport.m_textBoxFormatBar
            || viewport.m_textBoxFormatBar->isHidden()
            || viewport.m_textBoxFormatBar->parentWidget() != &viewport)
            return fail("single user text box did not show owned bar");
        auto* fontControl =
            viewport.m_textBoxFormatBar
                ->findChild<QFontComboBox*>();
        if (!fontControl || fontControl->count() < 2
            || fontControl->currentFont().family().isEmpty()
            || !box->fontFamily.isEmpty())
            return fail("platform font list/default fallback was invalid");
        int alternateFontIndex = -1;
        for (int i = 0; i < fontControl->count(); ++i) {
            if (fontControl->itemText(i).compare(
                    fontControl->currentFont().family(),
                    Qt::CaseInsensitive) != 0) {
                alternateFontIndex = i;
                break;
            }
        }
        if (alternateFontIndex < 0)
            return fail("font selector had no alternate family");
        const QString alternateFamily =
            fontControl->itemText(alternateFontIndex);
        fontControl->setCurrentIndex(alternateFontIndex);
        QMetaObject::invokeMethod(
            fontControl, "activated", Qt::DirectConnection,
            Q_ARG(int, alternateFontIndex));
        QApplication::processEvents();
        const auto alternateLayout =
            TextBoxObject::buildLayout(box->layoutInput());
        if (box->fontFamily.compare(
                alternateFamily, Qt::CaseInsensitive) != 0
            || !alternateLayout
            || alternateLayout->plainFont.family().compare(
                   alternateFamily, Qt::CaseInsensitive) != 0
            || alternateLayout->document->defaultFont().family().compare(
                   alternateFamily, Qt::CaseInsensitive) != 0
            || viewport.m_undoStack.size() != 1)
            return fail("font selector reverted instead of applying family");
        viewport.undo();
        if (!box->fontFamily.isEmpty())
            return fail("font-family undo did not restore default family");

        // The inline editor widget is reused between sessions. A box that
        // stores no family must edit in the application font instead of
        // inheriting the family of the previously edited box, which would
        // render differently once the session commits.
        box->fontFamily = alternateFamily;
        viewport.startInlineTextEdit(box, false);
        const QString editedFamily =
            viewport.m_inlineTextBoxEditor->editor()->font().family();
        viewport.cancelInlineTextEdit();
        box->fontFamily.clear();
        viewport.startInlineTextEdit(box, false);
        const QString fallbackFamily =
            viewport.m_inlineTextBoxEditor->editor()->font().family();
        viewport.cancelInlineTextEdit();
        if (editedFamily.compare(alternateFamily, Qt::CaseInsensitive) != 0
            || fallbackFamily.compare(QApplication::font().family(),
                                      Qt::CaseInsensitive) != 0)
            return fail("inline editor font did not follow the box family");

        viewport.m_undoStack.clear();
        viewport.m_redoStack.clear();
        if (!QRect(8, 8, viewport.width() - 16,
                   viewport.height() - 16)
                 .contains(viewport.m_textBoxFormatBar->geometry()))
            return fail("format bar was not clamped to viewport");

        viewport.captureObjectDragBackground();
        if (viewport.m_textBoxFormatBar->isHidden()
            || viewport.m_objectDragBackgroundSnapshot.isNull())
            return fail("drag capture did not restore live format bar");
        viewport.m_textBoxFormatBar->hide();
        viewport.m_skipSelectedObjectRendering = true;
        const QPixmap expectedDragBackground = viewport.grab();
        viewport.m_skipSelectedObjectRendering = false;
        viewport.m_textBoxFormatBar->show();
        viewport.updateTextBoxFormatBarGeometry();
        if (viewport.m_objectDragBackgroundSnapshot.toImage()
                != expectedDragBackground.toImage())
            return fail("drag snapshot retained a frozen format bar");
        viewport.m_objectDragBackgroundSnapshot = QPixmap();
        viewport.m_dragObjectRenderedCache = QPixmap();

        const QRect initialBarGeometry =
            viewport.m_textBoxFormatBar->geometry();
        viewport.setZoomLevel(1.2);
        if (viewport.m_textBoxFormatBar->geometry()
                == initialBarGeometry)
            return fail("format bar did not track zoom");
        box->rotation = 25.0;
        viewport.updateTextBoxFormatBarGeometry();
        if (!QRect(8, 8, viewport.width() - 16,
                   viewport.height() - 16)
                 .contains(viewport.m_textBoxFormatBar->geometry()))
            return fail("rotated format bar placement overflowed");

        const TextBoxState placementState = box->captureState();
        box->rotation = 0.0;
        box->position = QPointF(0.0, 0.0);
        box->size = QSizeF(100.0, page->size.height());
        viewport.updateTextBoxFormatBarGeometry();
        if (viewport.m_textBoxFormatBar->geometry().left()
                < viewport.objectBoundsInViewport(box).right())
            return fail("format bar did not choose right-side placement");
        box->position.setX(page->size.width() - box->size.width());
        viewport.updateTextBoxFormatBarGeometry();
        if (viewport.m_textBoxFormatBar->geometry().right()
                > viewport.objectBoundsInViewport(box).left())
            return fail("format bar did not choose left-side placement");
        box->position = QPointF(0.0, 0.0);
        box->size = page->size;
        viewport.updateTextBoxFormatBarGeometry();
        if (!QRect(8, 8, viewport.width() - 16,
                   viewport.height() - 16)
                 .contains(viewport.m_textBoxFormatBar->geometry()))
            return fail("least-overflow placement was not clamped");
        box->applyState(placementState);
        viewport.updateTextBoxFormatBarGeometry();
        viewport.resize(480, 820);
        viewport.updateTextBoxFormatBarGeometry();
        if (!QRect(8, 8, viewport.width() - 16,
                   viewport.height() - 16)
                 .contains(viewport.m_textBoxFormatBar->geometry()))
            return fail("narrow viewport did not clamp format bar");
        viewport.resize(1100, 820);
        viewport.updateTextBoxFormatBarGeometry();

        auto ocr = std::make_unique<OcrTextObject>();
        ocr->text = QStringLiteral("OCR");
        OcrTextObject* ocrRaw = ocr.get();
        page->addObject(std::move(ocr));
        viewport.selectObject(ocrRaw, false);
        if (!viewport.m_textBoxFormatBar->isHidden())
            return fail("OCR object incorrectly showed format bar");
        viewport.selectObject(box, false);
        viewport.selectObject(ocrRaw, true);
        if (!viewport.m_textBoxFormatBar->isHidden())
            return fail("multi-selection incorrectly showed format bar");
        viewport.selectObject(box, false);

        QSignalSpy documentSpy(
            &viewport, &DocumentViewport::documentModified);
        QSignalSpy pageSpy(
            &viewport, &DocumentViewport::pageModified);
        QSignalSpy layoutSpy(
            &viewport, &DocumentViewport::textBoxLayoutCommitted);
        viewport.m_undoStack.clear();
        viewport.m_redoStack.clear();

        const TextBoxState formatStart = box->captureState();
        auto worldTopLeft = [](const TextBoxState& state) {
            const QPointF center(
                state.size.width() / 2.0, state.size.height() / 2.0);
            const QPointF delta = -center;
            const qreal radians = qDegreesToRadians(state.rotation);
            return state.position + center + QPointF(
                delta.x() * qCos(radians)
                    - delta.y() * qSin(radians),
                delta.x() * qSin(radians)
                    + delta.y() * qCos(radians));
        };
        const QPointF anchoredTop = worldTopLeft(formatStart);
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 22.0);
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontFamily,
            QStringLiteral("Arial"));
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::Alignment,
            static_cast<int>(TextAlignment::Right));
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontColor,
            QColor(10, 20, 30));
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::BackgroundColor,
            QColor(210, 200, 190, 170));
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::BackgroundOpacity, 91);
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::Border, false);
        if (documentSpy.count() || pageSpy.count()
            || layoutSpy.count() || !viewport.m_undoStack.isEmpty())
            return fail("format previews emitted persistent invalidation");
        viewport.finishTextBoxFormatInteraction(true);

        const TextBoxState formatEnd = box->captureState();
        if (qAbs(formatEnd.fontSize - 22.0) > 0.001
            || formatEnd.fontFamily != QLatin1String("Arial")
            || formatEnd.alignment != TextAlignment::Right
            || formatEnd.fontColor != QColor(10, 20, 30)
            || formatEnd.backgroundColor.alpha() != 91
            || formatEnd.showBorder
            || QLineF(worldTopLeft(formatEnd), anchoredTop).length()
                > 0.01)
            return fail("accepted formatting lost values or top anchor");
        if (viewport.m_undoStack.size() != 1
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectTextEdit
            || !viewport.m_undoStack.top().objectHasTextBoxState
            || documentSpy.count() != 1 || pageSpy.count() != 1
            || layoutSpy.count() != 1
            || !viewport.m_pendingThumbnailPages.contains(0))
            return fail("format interaction did not coalesce/invalidate once");
        auto* sizeControl =
            viewport.m_textBoxFormatBar
                ->findChild<QDoubleSpinBox*>();
        if (!sizeControl
            || qAbs(sizeControl->value() - 22.0) > 0.001)
            return fail("bar controls did not synchronize accepted state");

        viewport.undo();
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), formatStart))
            return fail("format undo did not restore complete state");
        viewport.redo();
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), formatEnd))
            return fail("format redo did not restore complete state");

        const int undoCount = viewport.m_undoStack.size();
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 31.0);
        viewport.finishTextBoxFormatInteraction(false);
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), formatEnd)
            || viewport.m_undoStack.size() != undoCount)
            return fail("cancelled format interaction created history");
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize,
            formatEnd.fontSize);
        viewport.finishTextBoxFormatInteraction(true);
        if (viewport.m_undoStack.size() != undoCount)
            return fail("net-zero format interaction created history");

        auto* opacityControl =
            viewport.m_textBoxFormatBar->findChild<QSlider*>();
        if (!opacityControl)
            return fail("background opacity control was missing");
        const int beforeSliderUndo = viewport.m_undoStack.size();
        QMetaObject::invokeMethod(
            opacityControl, "sliderPressed", Qt::DirectConnection);
        opacityControl->setValue(80);
        opacityControl->setValue(70);
        opacityControl->setValue(60);
        QMetaObject::invokeMethod(
            opacityControl, "sliderReleased", Qt::DirectConnection);
        if (viewport.m_undoStack.size() != beforeSliderUndo + 1
            || box->backgroundColor.alpha() != 60)
            return fail("continuous slider changes did not coalesce");
        viewport.undo();
        if (box->backgroundColor.alpha()
                != formatEnd.backgroundColor.alpha())
            return fail("slider interaction undo lost opacity");
        viewport.redo();

        const auto swatches =
            viewport.m_textBoxFormatBar
                ->findChildren<ColorPresetButton*>();
        if (swatches.size() != 2)
            return fail("format color controls were missing");
        auto clickSwatch = [](ColorPresetButton* swatch) {
            const QPointF local(swatch->rect().center());
            QMouseEvent press(QEvent::MouseButtonPress, local,
                              Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local,
                                Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(swatch, &press);
            QApplication::sendEvent(swatch, &release);
            QApplication::processEvents();
        };

        const int beforeColorUndo = viewport.m_undoStack.size();
        const QColor startFontColor = box->fontColor;
        clickSwatch(swatches.at(0));
        // A press that leaks to the canvas would clear the selection and
        // leave the transaction unowned, which silently drops every preview.
        if (viewport.m_selectedObjects.size() != 1)
            return fail("swatch click leaked to the canvas selection");
        auto* fontColorDialog =
            viewport.m_textBoxFormatBar->findChild<QColorDialog*>();
        if (!fontColorDialog
            || !viewport.m_textBoxFormatTransaction.active)
            return fail("text color swatch click opened no dialog");
        const QColor pickedFontColor(24, 118, 210);
        fontColorDialog->setCurrentColor(pickedFontColor);
        QApplication::processEvents();
        if (box->fontColor != pickedFontColor)
            return fail("text color preview never reached the object");
        fontColorDialog->accept();
        QApplication::processEvents();
        if (box->fontColor != pickedFontColor
            || viewport.m_undoStack.size() != beforeColorUndo + 1
            || viewport.m_textBoxFormatTransaction.active)
            return fail("accepted text color was not committed once");
        const auto coloredLayout =
            TextBoxObject::buildLayout(box->layoutInput());
        if (!coloredLayout || !coloredLayout->document
            || coloredLayout->document->begin().begin().fragment()
                   .charFormat().foreground().color() != pickedFontColor)
            return fail("text color did not reach the rendered layout");
        viewport.undo();
        if (box->fontColor != startFontColor)
            return fail("text color undo did not restore the old color");
        viewport.redo();

        const int beforeBackgroundUndo = viewport.m_undoStack.size();
        const int keptAlpha = box->backgroundColor.alpha();
        clickSwatch(swatches.at(1));
        auto* backgroundDialog =
            viewport.m_textBoxFormatBar->findChild<QColorDialog*>();
        if (!backgroundDialog)
            return fail("background swatch click opened no dialog");
        const QColor pickedBackground(240, 200, 90);
        backgroundDialog->setCurrentColor(pickedBackground);
        QApplication::processEvents();
        if (box->backgroundColor.rgb() != pickedBackground.rgb()
            || box->backgroundColor.alpha() != keptAlpha)
            return fail("background preview lost hue or opacity");
        backgroundDialog->accept();
        QApplication::processEvents();
        if (box->backgroundColor.rgb() != pickedBackground.rgb()
            || viewport.m_undoStack.size() != beforeBackgroundUndo + 1)
            return fail("accepted background color was not committed");

        QMetaObject::invokeMethod(
            swatches.first(), "editRequested",
            Qt::DirectConnection);
        QApplication::processEvents();
        if (!viewport.m_textBoxFormatBar->hasOpenPopup()
            || !viewport.m_textBoxFormatTransaction.active)
            return fail("color popup did not own a format interaction");
        viewport.selectObject(ocrRaw, false);
        QApplication::processEvents();
        if (viewport.m_textBoxFormatBar->hasOpenPopup()
            || viewport.m_textBoxFormatTransaction.active
            || !viewport.m_textBoxFormatBar->isHidden())
            return fail("selection replacement did not tear down popup");
        viewport.selectObject(box, false);

        const int beforeOverflowUndo = viewport.m_undoStack.size();
        box->rotation = 0.0;
        box->position.setY(
            page->size.height() - box->size.height());
        const TextBoxState overflowStart = box->captureState();
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 144.0);
        viewport.finishTextBoxFormatInteraction(true);
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), overflowStart)
            || viewport.m_undoStack.size() != beforeOverflowUndo
            || viewport.m_objectGeometryFeedbackText.isEmpty())
            return fail("paged formatting overflow was not atomic");

        box->position = QPointF(160.0, 180.0);
        const TextBoxState inlineStart = box->captureState();
        viewport.m_undoStack.clear();
        documentSpy.clear();
        pageSpy.clear();
        layoutSpy.clear();
        viewport.startInlineTextEdit(box, false);
        viewport.m_inlineTextBoxEditor->editor()->moveCursor(
            QTextCursor::End);
        viewport.m_inlineTextBoxEditor->editor()->insertPlainText(
            QStringLiteral("\nInline"));
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 19.0);
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::Alignment,
            static_cast<int>(TextAlignment::Center));
        viewport.finishTextBoxFormatInteraction(true);
        if (!viewport.m_undoStack.isEmpty()
            || documentSpy.count() || layoutSpy.count())
            return fail("inline formatting committed separately");
        viewport.commitInlineTextEdit();
        const TextBoxState inlineEnd = box->captureState();
        if (viewport.m_undoStack.size() != 1
            || viewport.m_undoStack.top().type
                != UndoAction::ObjectTextEdit
            || inlineEnd.fontSize != 19.0
            || inlineEnd.alignment != TextAlignment::Center
            || !inlineEnd.text.endsWith(QStringLiteral("Inline"))
            || documentSpy.count() != 1
            || pageSpy.count() != 1
            || layoutSpy.count() != 1)
            return fail("inline text/format did not merge into one action");
        viewport.undo();
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), inlineStart))
            return fail("merged inline format undo was incomplete");
        viewport.redo();
        if (!DocumentViewport::textBoxStatesEqual(
                box->captureState(), inlineEnd))
            return fail("merged inline format redo was incomplete");

        viewport.startInlineTextEdit(box, false);
        const qreal beforeEscapeSize = box->fontSize;
        sizeControl->setValue(beforeEscapeSize + 3.0);
        QKeyEvent formatEscape(
            QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(sizeControl, &formatEscape);
        if (!viewport.hasActiveInlineTextEdit()
            || qAbs(box->fontSize - beforeEscapeSize) > 0.001)
            return fail("Escape did not cancel only active formatting");
        QKeyEvent barCommit(
            QEvent::KeyPress, Qt::Key_Return,
            Qt::ControlModifier);
        QApplication::sendEvent(sizeControl, &barCommit);
        if (viewport.hasActiveInlineTextEdit())
            return fail("Ctrl+Enter from format bar did not commit inline edit");

        auto legacy = std::make_unique<TextBoxObject>();
        legacy->text = QStringLiteral("Legacy format");
        legacy->position = QPointF(40.0, 40.0);
        legacy->size = QSizeF(180.0, 44.0);
        TextBoxObject* legacyRaw = legacy.get();
        page->addObject(std::move(legacy));
        viewport.selectObject(legacyRaw, false);
        const TextBoxState legacyStart = legacyRaw->captureState();
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::Border, false);
        viewport.finishTextBoxFormatInteraction(false);
        if (!DocumentViewport::textBoxStatesEqual(
                legacyRaw->captureState(), legacyStart))
            return fail("cancelled legacy format did not restore version 0");
        viewport.beginTextBoxFormatInteraction();
        viewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 18.0);
        viewport.finishTextBoxFormatInteraction(true);
        if (!legacyRaw->usesCurrentLayout()
            || viewport.m_undoStack.top().objectOldTextBoxState
                   .textLayoutVersion != 0)
            return fail("legacy conversion was not in format undo");
        viewport.undo();
        if (!DocumentViewport::textBoxStatesEqual(
                legacyRaw->captureState(), legacyStart))
            return fail("legacy format undo did not restore version 0");

        viewport.selectObject(box, false);
        const QJsonObject formattedJson = box->toJson();
        std::unique_ptr<InsertedObject> restored =
            InsertedObject::fromJson(formattedJson);
        auto* restoredBox =
            dynamic_cast<TextBoxObject*>(restored.get());
        if (!restoredBox
            || restoredBox->fontFamily != box->fontFamily
            || restoredBox->fontColor != box->fontColor
            || restoredBox->backgroundColor
                != box->backgroundColor
            || restoredBox->alignment != box->alignment
            || restoredBox->showBorder != box->showBorder
            || restoredBox->textLayoutVersion
                != box->textLayoutVersion)
            return fail("formatted persistence round-trip lost state");

        auto secondDoc = Document::createNew("Second format viewport");
        DocumentViewport secondViewport;
        secondViewport.resize(900, 700);
        secondViewport.setDocument(secondDoc.get());
        auto secondObject = std::make_unique<TextBoxObject>();
        secondObject->textLayoutVersion =
            TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
        secondObject->text = QStringLiteral("Second");
        secondObject->position = QPointF(100.0, 100.0);
        secondObject->size = QSizeF(200.0, 1.0);
        secondObject->reflowToWidth(200.0);
        TextBoxObject* secondBox = secondObject.get();
        secondDoc->page(0)->addObject(std::move(secondObject));
        secondViewport.selectObject(secondBox, false);
        if (!secondViewport.m_textBoxFormatBar
            || secondViewport.m_textBoxFormatBar
                == viewport.m_textBoxFormatBar
            || secondViewport.m_textBoxFormatBar->parentWidget()
                != &secondViewport)
            return fail("split viewports did not isolate format bars");

        auto edgeDoc = Document::createNew(
            "Edgeless format", Document::Mode::Edgeless);
        DocumentViewport edgeViewport;
        edgeViewport.resize(900, 700);
        edgeViewport.setDocument(edgeDoc.get());
        Page* edgeTile = edgeDoc->getOrCreateTile(0, 0);
        auto edgeObject = std::make_unique<TextBoxObject>();
        edgeObject->textLayoutVersion =
            TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
        edgeObject->text =
            QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh");
        edgeObject->position = QPointF(100.0, 4000.0);
        edgeObject->size = QSizeF(180.0, 1.0);
        edgeObject->reflowToWidth(180.0);
        TextBoxObject* edgeBox = edgeObject.get();
        edgeTile->addObject(std::move(edgeObject));
        const TextBoxState edgeStart = edgeBox->captureState();
        edgeViewport.selectObject(edgeBox, false);
        edgeViewport.beginTextBoxFormatInteraction();
        edgeViewport.applyTextBoxFormatPreview(
            DocumentViewport::TextBoxFormatChange::FontSize, 72.0);
        edgeViewport.finishTextBoxFormatInteraction(true);
        if (edgeBox->fontSize != 72.0
            || edgeBox->size.height() <= edgeStart.size.height()
            || edgeViewport.m_undoStack.size() != 1)
            return fail("edgeless formatting growth was capped");
        edgeViewport.undo();
        if (!DocumentViewport::textBoxStatesEqual(
                edgeBox->captureState(), edgeStart))
            return fail("edgeless formatting undo lost state");

        viewport.setDocument(nullptr);
        secondViewport.setDocument(nullptr);
        edgeViewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }
    
    /**
     * @brief The inline editor and format bar must not outlive their target.
     *
     * Both overlays are anchored to one selected object. Every path that
     * removes that object or the selection has to tear them down, or the user
     * is left typing into a widget floating over nothing.
     */
    /**
     * @brief New text boxes take their backdrop from the paper, not the theme.
     */
    static bool testTextBoxThemeDefaults() {
        printf("  testTextBoxThemeDefaults... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        if (TextBoxObject::defaultBackgroundColor(true).lightness() >= 128
            || TextBoxObject::defaultBackgroundColor(false).lightness()
                   < 128) {
            return fail("shared backdrop pair is not dark/light");
        }

        auto doc = Document::createNew("Theme defaults");
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());

        auto backdropForPaper = [&](const QColor& paper) {
            doc->page(0)->backgroundColor = paper;
            const QRectF rect = viewport.proposedTextBoxCreationRect(
                QPointF(200.0, 200.0), QPointF(200.0, 200.0), 0);
            viewport.createTextBoxAtRect(0, rect, QPointF());
            Page* page = doc->page(0);
            auto* box = page && !page->objects.empty()
                ? dynamic_cast<TextBoxObject*>(page->objects.back().get())
                : nullptr;
            const QColor backdrop = box ? box->backgroundColor : QColor();
            viewport.cancelInlineTextEdit();
            return backdrop;
        };

        // Each case runs under the opposite theme, because the paper a
        // notebook was created with is what the box has to read against.
        viewport.setDarkMode(false);
        if (backdropForPaper(QColor("#2b2b2b")).lightness() >= 128)
            return fail("dark paper got a bright text box backdrop");

        viewport.setDarkMode(true);
        if (backdropForPaper(Qt::white).lightness() < 128)
            return fail("white paper got a dark text box backdrop");

        // The inline editor paints straight onto that backdrop, so its
        // Markdown syntax colors have to lift or headings vanish while typing.
        auto headingSyntaxLightness = [](const QColor& backdrop) {
            InlineTextBoxEditor editor;
            TextBoxObject box;
            box.textLayoutVersion =
                TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
            box.fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
            box.backgroundColor = backdrop;
            editor.configure(box.captureState(), 1.0,
                             backdrop.lightness() < 128);
            editor.setText(QStringLiteral("# Heading"));
            QCoreApplication::processEvents();

            const QTextBlock block =
                editor.editor()->document()->firstBlock();
            if (!block.isValid() || !block.layout())
                return -1;
            // Offset 2 skips the "# " marker, which carries its own format.
            for (const auto& range : block.layout()->formats()) {
                if (range.start == 2
                    && range.format.hasProperty(
                           QTextFormat::ForegroundBrush)) {
                    return range.format.foreground().color().lightness();
                }
            }
            return -1;
        };
        const int onDark = headingSyntaxLightness(QColor(40, 40, 40, 180));
        const int onLight = headingSyntaxLightness(QColor(255, 255, 255, 180));
        if (onDark < 0 || onLight < 0)
            return fail("heading syntax color was never applied");
        if (onDark <= onLight)
            return fail("heading syntax stayed dark on a dark backdrop");

        viewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }

    static bool testTextOverlayLifecycle() {
        printf("  testTextOverlayLifecycle... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        auto doc = Document::createNew("Overlay lifecycle");
        doc->addPage();
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());
        viewport.setCurrentTool(ToolType::ObjectSelect);

        auto addBox = [&](int pageIndex, const QString& text) {
            auto object = std::make_unique<TextBoxObject>();
            object->textLayoutVersion =
                TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION;
            object->text = text;
            object->fontSize = TextBoxObject::DEFAULT_BASE_FONT_SIZE;
            object->position = QPointF(60.0, 60.0);
            object->size = QSizeF(200.0, 1.0);
            object->reflowToWidth(200.0);
            TextBoxObject* raw = object.get();
            doc->page(pageIndex)->addObject(std::move(object));
            return raw;
        };

        // Deleting the page being typed on must fold the text into the page
        // snapshot rather than leave a live editor over a destroyed page.
        TextBoxObject* onSecondPage = addBox(1, QStringLiteral("Doomed"));
        viewport.startInlineTextEdit(onSecondPage, false);
        if (!viewport.hasActiveInlineTextEdit())
            return fail("inline edit did not start");
        viewport.handleInlineTextSourceChanged(
            QStringLiteral("Doomed edit"));
        if (!viewport.deletePagesWithUndo({1}))
            return fail("page delete was rejected");
        if (viewport.hasActiveInlineTextEdit()
            || (viewport.m_inlineTextBoxEditor
                && viewport.m_inlineTextBoxEditor->isVisible()))
            return fail("page delete left the inline editor alive");
        viewport.undo();
        TextBoxObject* restored = nullptr;
        for (const auto& object : doc->page(1)->objects) {
            if (object && object->type() == QLatin1String("textbox"))
                restored = static_cast<TextBoxObject*>(object.get());
        }
        if (!restored || restored->text != QStringLiteral("Doomed edit"))
            return fail("page delete undo lost the in-progress text");

        // An owner outside the viewport (the OCR rescan) frees objects
        // directly, so forgetObject has to drop every reference first.
        TextBoxObject* target = addBox(0, QStringLiteral("Forget me"));
        const QString targetId = target->id;
        viewport.selectObject(target, false);
        viewport.m_hoveredObject = target;
        viewport.startInlineTextEdit(target, false);
        viewport.beginTextBoxFormatInteraction();
        viewport.forgetObject(targetId);
        if (viewport.hasSelectedObjects()
            || viewport.m_hoveredObject
            || viewport.hasActiveInlineTextEdit()
            || viewport.m_textBoxFormatTransaction.active)
            return fail("forgetObject left a reference behind");
        doc->page(0)->removeObject(targetId);

        // A right-click in Select mode creates a text box, leaving the new
        // editor under the cursor just in time to catch the context menu that
        // Windows raises off that very release. The editor swallows that one
        // menu and no more.
        viewport.setObjectActionMode(
            DocumentViewport::ObjectActionMode::Select);
        viewport.setObjectInsertMode(
            DocumentViewport::ObjectInsertMode::Text);
        const QPointF createAt =
            viewport.pageToViewport(0, QPointF(120.0, 400.0));
        QMouseEvent rightPress(QEvent::MouseButtonPress, createAt,
                               Qt::RightButton, Qt::RightButton,
                               Qt::NoModifier);
        QMouseEvent rightRelease(QEvent::MouseButtonRelease, createAt,
                                 Qt::RightButton, Qt::NoButton,
                                 Qt::NoModifier);
        viewport.mousePressEvent(&rightPress);
        viewport.mouseReleaseEvent(&rightRelease);
        if (!viewport.hasActiveInlineTextEdit()
            || !viewport.m_inlineTextBoxEditor)
            return fail("right-button create did not open the inline editor");
        if (!viewport.m_contextMenuObjectId.isEmpty())
            return fail("creating a box armed an object menu");

        QMarkdownTextEdit* createdEditor =
            viewport.m_inlineTextBoxEditor->editor();
        QWidget* editorSurface = createdEditor->viewport();
        // Report the menu instead of popping it up, so the test can count the
        // ones that got through without leaving a stray window behind.
        createdEditor->setContextMenuPolicy(Qt::CustomContextMenu);
        QSignalSpy menusRaised(
            createdEditor, &QWidget::customContextMenuRequested);
        auto sendContextMenu = [&]() {
            QContextMenuEvent menuEvent(
                QContextMenuEvent::Mouse, QPoint(4, 4),
                editorSurface->mapToGlobal(QPoint(4, 4)));
            QApplication::sendEvent(editorSurface, &menuEvent);
        };
        sendContextMenu();
        if (menusRaised.size() != 0)
            return fail("the creating right-click still raised a menu");
        sendContextMenu();
        if (menusRaised.size() != 1)
            return fail("suppression outlived the creating right-click");
        createdEditor->setContextMenuPolicy(Qt::DefaultContextMenu);

        auto textBoxCount = [&]() {
            int count = 0;
            for (const auto& object : doc->page(0)->objects) {
                if (object && object->type() == QLatin1String("textbox"))
                    ++count;
            }
            return count;
        };
        auto rightClickAt = [&](const QPointF& pos) {
            QMouseEvent press(QEvent::MouseButtonPress, pos, Qt::RightButton,
                              Qt::RightButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, pos,
                                Qt::RightButton, Qt::NoButton,
                                Qt::NoModifier);
            viewport.mousePressEvent(&press);
            viewport.mouseReleaseEvent(&release);
        };

        // The editor covers only the text area, so a right-click on the box's
        // padding ring lands on the canvas. It still belongs to the text being
        // edited: it must raise that editor's menu rather than stack a second
        // box on top of the first.
        TextBoxObject* createdBox = viewport.resolveInlineTextBox();
        if (!createdBox)
            return fail("the created box could not be resolved");
        viewport.handleInlineTextSourceChanged(QStringLiteral("Typed"));
        const int whileEditing = textBoxCount();
        const QRectF editedBounds =
            viewport.objectBoundsInViewport(createdBox);
        const QPointF onRing(editedBounds.left() + 2.0,
                             editedBounds.center().y());
        rightClickAt(onRing);
        if (!viewport.hasActiveInlineTextEdit())
            return fail("right-click on the edited box ended the session");
        if (textBoxCount() != whileEditing)
            return fail("right-click on the edited box created another one");
        if (!viewport.m_contextMenuTargetsInlineEditor)
            return fail("right-click on the edited box missed the editor");

        QContextMenuEvent ringMenu(
            QContextMenuEvent::Mouse, onRing.toPoint(),
            viewport.mapToGlobal(onRing.toPoint()));
        viewport.contextMenuEvent(&ringMenu);
        QMenu* raised = createdEditor->findChild<QMenu*>();
        if (!raised)
            return fail("right-click on the edited box raised no text menu");
        raised->close();
        QCoreApplication::processEvents();

        // A box that is not being edited has no text menu to offer, but a
        // right-click still must not stack a new box onto it.
        viewport.commitInlineTextEdit();
        if (viewport.hasActiveInlineTextEdit())
            return fail("commit left the inline editor open");
        const int committed = textBoxCount();
        viewport.deselectAllObjects();
        rightClickAt(viewport.objectBoundsInViewport(createdBox).center());
        if (textBoxCount() != committed)
            return fail("right-click on an existing box created another one");
        if (viewport.m_selectedObjects.size() != 1)
            return fail("right-click on an existing box did not select it");
        // The menu itself is modal, so assert the target the context menu
        // event would act on rather than popping it up.
        if (viewport.m_contextMenuObjectId != createdBox->id)
            return fail("right-click on an existing box armed no object menu");
        viewport.cancelInlineTextEdit();
        viewport.deselectAllObjects();

        // Browsing the font list previews every entry it passes over, so a
        // popup dismissed without an explicit choice has to roll the preview
        // back rather than commit whatever was highlighted last.
        TextBoxFormatBar bar;
        auto* fontCombo = bar.findChild<QFontComboBox*>();
        if (!fontCombo || !fontCombo->view())
            return fail("font combo was not found");
        QSignalSpy finished(
            &bar, &TextBoxFormatBar::interactionFinished);
        QEvent popupShow(QEvent::Show);
        QEvent popupHide(QEvent::Hide);

        QApplication::sendEvent(fontCombo->view(), &popupShow);
        fontCombo->setCurrentFont(QFont(QStringLiteral("Courier New")));
        QApplication::sendEvent(fontCombo->view(), &popupHide);
        QCoreApplication::processEvents();
        if (finished.size() != 1 || finished.at(0).at(0).toBool())
            return fail("dismissed font popup committed a browsed font");

        finished.clear();
        QApplication::sendEvent(fontCombo->view(), &popupShow);
        fontCombo->setCurrentFont(QFont(QStringLiteral("Arial")));
        emit fontCombo->activated(0);
        QApplication::sendEvent(fontCombo->view(), &popupHide);
        QCoreApplication::processEvents();
        if (finished.size() != 1 || !finished.at(0).at(0).toBool())
            return fail("activated font choice did not commit once");

        viewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }

    /**
     * @brief LinkObject controls float in the viewport, one bar per viewport.
     *
     * The bar replaces the old toolbar subtoolbar, so it has to inherit the
     * behaviour the text box format bar established: anchored placement that
     * stays inside the viewport, tracking zoom, staying out of the drag
     * snapshot, and hiding for anything that is not a single LinkObject.
     *
     * Also covers the two behaviours that make the bar reachable after a
     * highlight: the commit selects its own annotation, and because the mark
     * and its slots are one record the whole commit is a single undo entry.
     */
    static bool testLinkObjectBar() {
        printf("  testLinkObjectBar... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        auto doc = Document::createNew("Link bar");
        DocumentViewport viewport;
        viewport.resize(1100, 820);
        viewport.setDocument(doc.get());
        viewport.setCurrentTool(ToolType::ObjectSelect);
        Page* page = doc->page(0);
        if (!page)
            return fail("missing link bar test page");

        auto linkPtr = std::make_unique<LinkObject>();
        linkPtr->position = QPointF(180.0, 250.0);
        linkPtr->description = QStringLiteral("anchor");
        LinkObject* link = linkPtr.get();
        page->addObject(std::move(linkPtr));

        if (viewport.m_linkObjectBar)
            return fail("link bar was built before anything selected it");

        // An annotation with all 3 slots empty has nothing worth opening, so it
        // must not tick the scroll bar.
        if (!doc->pageLinkMarkers().isEmpty())
            return fail("slotless annotation produced a scroll-bar marker");

        viewport.selectObject(link, false);
        if (!viewport.m_linkObjectBar
            || viewport.m_linkObjectBar->isHidden()
            || viewport.m_linkObjectBar->parentWidget() != &viewport)
            return fail("single selected LinkObject did not show owned bar");

        const QRect inset(8, 8, viewport.width() - 16, viewport.height() - 16);
        if (!inset.contains(viewport.m_linkObjectBar->geometry()))
            return fail("link bar was not clamped to viewport");

        auto* colorButton =
            viewport.m_linkObjectBar->findChild<ColorPresetButton*>();
        const auto slotButtons =
            viewport.m_linkObjectBar->findChildren<LinkSlotButton*>();
        auto* descriptionEdit =
            viewport.m_linkObjectBar->findChild<QLineEdit*>();
        if (!colorButton || !descriptionEdit
            || slotButtons.size() != LinkObject::SLOT_COUNT)
            return fail("link bar was missing its controls");
        if (colorButton->color() != link->iconColor
            || descriptionEdit->text() != link->description)
            return fail("link bar did not seed from the selected object");

        link->iconColor = QColor(10, 120, 200);
        link->linkSlots[1].type = LinkSlot::Type::Url;
        link->linkSlots[1].url = QStringLiteral("https://example.invalid");
        viewport.refreshLinkObjectBar();
        if (colorButton->color() != link->iconColor
            || slotButtons[1]->state() != LinkSlotState::Url)
            return fail("link bar did not pick up slot/colour changes");
        if (doc->pageLinkMarkers().size() != 1)
            return fail("annotation with a filled slot produced no marker");

        const QRect initialGeometry = viewport.m_linkObjectBar->geometry();
        viewport.setZoomLevel(1.4);
        if (viewport.m_linkObjectBar->geometry() == initialGeometry)
            return fail("link bar did not track zoom");

        viewport.captureObjectDragBackground();
        if (viewport.m_linkObjectBar->isHidden()
            || viewport.m_objectDragBackgroundSnapshot.isNull())
            return fail("drag capture did not restore the live link bar");
        viewport.m_objectDragBackgroundSnapshot = QPixmap();
        viewport.m_dragObjectRenderedCache = QPixmap();

        viewport.setZoomLevel(1.0);
        link->position = QPointF(0.0, 0.0);
        viewport.updateLinkObjectBarGeometry();
        if (!inset.contains(viewport.m_linkObjectBar->geometry()))
            return fail("top-left annotation overflowed the viewport");
        viewport.resize(420, 700);
        viewport.updateLinkObjectBarGeometry();
        if (!QRect(8, 8, viewport.width() - 16, viewport.height() - 16)
                 .contains(viewport.m_linkObjectBar->geometry()))
            return fail("narrow viewport did not clamp the link bar");
        viewport.resize(1100, 820);
        link->position = QPointF(180.0, 250.0);
        viewport.updateLinkObjectBarGeometry();

        // Stylus events land on the deepest child and propagate back up, so the
        // canvas has to treat the bar's area as off limits or the pen cannot
        // reach the controls. A child of an unshown parent is never isVisible(),
        // so map the viewport offscreen to exercise the real gating path.
        viewport.setAttribute(Qt::WA_DontShowOnScreen, true);
        viewport.show();
        QApplication::processEvents();
        viewport.updateLinkObjectBarGeometry();
        if (!viewport.pointerOverViewportWidget(
                QRectF(viewport.m_linkObjectBar->geometry()).center()))
            return fail("link bar area was not excluded from canvas input");
        viewport.hide();

        auto ocr = std::make_unique<OcrTextObject>();
        ocr->text = QStringLiteral("OCR");
        OcrTextObject* ocrRaw = ocr.get();
        page->addObject(std::move(ocr));
        viewport.selectObject(ocrRaw, false);
        if (!viewport.m_linkObjectBar->isHidden())
            return fail("OCR object incorrectly showed the link bar");
        viewport.selectObject(link, false);
        viewport.selectObject(ocrRaw, true);
        if (!viewport.m_linkObjectBar->isHidden())
            return fail("multi-selection incorrectly showed the link bar");
        viewport.deselectAllObjects();
        if (!viewport.m_linkObjectBar->isHidden())
            return fail("deselecting left the link bar visible");

        viewport.selectObject(link, false);
        QSignalSpy appearanceSpy(
            &viewport, &DocumentViewport::linkObjectAppearanceChanged);
        viewport.setSelectedLinkDescription(QStringLiteral("edited"));
        viewport.setSelectedLinkColor(QColor(200, 30, 40));
        if (link->description != QStringLiteral("edited")
            || link->iconColor != QColor(200, 30, 40)
            || appearanceSpy.count() != 2)
            return fail("viewport link handlers did not apply or notify");

        // A multi-line highlight is a single record, so a single Ctrl+Z, and it
        // emits no ink at all.
        viewport.deselectAllObjects();
        viewport.m_undoStack.clear();
        viewport.m_redoStack.clear();
        viewport.setCurrentTool(ToolType::Highlighter);
        viewport.m_autoHighlightStyle = DocumentViewport::HighlightStyle::Cover;
        viewport.m_textSelection.clear();
        viewport.m_textSelection.source =
            DocumentViewport::TextSelection::Source::Ocr;
        viewport.m_textSelection.pageIndex = 0;
        viewport.m_textSelection.startBoxIndex = 0;
        viewport.m_textSelection.endBoxIndex = 0;
        viewport.m_textSelection.selectedText = QStringLiteral("three lines");
        viewport.m_textSelection.highlightRects = {
            QRectF(100.0, 400.0, 200.0, 14.0),
            QRectF(100.0, 420.0, 200.0, 14.0),
            QRectF(100.0, 440.0, 200.0, 14.0)
        };
        const int strokesBefore =
            page->activeLayer() ? page->activeLayer()->strokes().size() : -1;
        LinkObject* annotation = viewport.commitHighlightAnnotation();
        if (!annotation)
            return fail("cover highlight did not commit an annotation");
        if (page->activeLayer()
            && page->activeLayer()->strokes().size() != strokesBefore)
            return fail("highlight commit still emitted ink");
        if (viewport.m_undoStack.size() != 1)
            return fail("highlight commit was not a single undo entry");

        // position/size are the region bounding box, which is what makes
        // Document::maxObjectExtent cover a multi-tile highlight.
        if (annotation->region.rects.size() != 3)
            return fail("annotation did not adopt one rect per line");
        if (annotation->position != QPointF(100.0, 400.0)
            || annotation->size != QSizeF(200.0, 54.0))
            return fail("annotation bounds are not the region bounding box");
        if (annotation->region.rects.first() != QRectF(0.0, 0.0, 200.0, 14.0))
            return fail("region rects are not object-local");
        if (annotation->region.style != HighlightRegion::Style::Cover
            || annotation->region.sourceRange.source
                   != HighlightRegion::Source::Ocr)
            return fail("annotation did not record style and selection source");
        if (annotation->region.sourceRange.pageUuid != page->uuid)
            return fail("annotation did not record its page uuid");
        if (annotation->descriptionUserEdited)
            return fail("auto-derived description was marked user-edited");
        if (viewport.m_document->maxObjectExtent() < 200)
            return fail("region growth did not widen the object extent");

        if (viewport.m_selectedObjects.size() != 1
            || viewport.m_selectedObjects.first()->type()
                   != QLatin1String("link")
            || viewport.m_selectedObjects.first() == link)
            return fail("highlight commit did not select its own annotation");
        if (viewport.m_linkObjectBar->isHidden())
            return fail("highlight commit did not surface the link bar");

        viewport.undo();
        if (!viewport.m_undoStack.isEmpty())
            return fail("one undo did not unwind the highlight commit");

        viewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }

    /**
     * @brief The annotation owns its highlight, so its geometry is the mark.
     *
     * Covers the consequences of that: the page clamp must not pull a mark off
     * its text, the region rather than a 24x24 icon is the hit target (including
     * across edgeless tiles, which relies on maxObjectExtent), handles are not
     * offered for something that cannot be resized, the scroll-bar filter reads
     * `descriptionUserEdited` rather than mere non-emptiness, and a
     * cross-notebook page copy remaps or stales the source range without ever
     * dropping the rects.
     */
    static bool testHighlightAnnotationGeometry() {
        printf("  testHighlightAnnotationGeometry... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        auto commitOcrHighlight = [](DocumentViewport& vp, int pageIndex,
                                     const QVector<QRectF>& rects,
                                     const QString& text) -> LinkObject* {
            vp.m_textSelection.clear();
            vp.m_textSelection.source =
                DocumentViewport::TextSelection::Source::Ocr;
            vp.m_textSelection.pageIndex = pageIndex;
            vp.m_textSelection.startBoxIndex = 0;
            vp.m_textSelection.startCharIndex = 0;
            vp.m_textSelection.endBoxIndex = 0;
            vp.m_textSelection.endCharIndex = text.size();
            vp.m_textSelection.selectedText = text;
            vp.m_textSelection.highlightRects = rects;
            return vp.commitHighlightAnnotation();
        };

        // ===== Paged: the mark stays on its text =====
        {
            auto doc = Document::createNew("Highlight geometry");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Underline;
            Page* page = doc->page(0);
            if (!page)
                return fail("missing highlight test page");

            // Text hugging the left edge: the badge would land at x = -26, and
            // clamping the object to the page would shove the whole mark right,
            // off the words it annotates.
            LinkObject* edgeMark = commitOcrHighlight(
                viewport, 0, {QRectF(2.0, 40.0, 300.0, 14.0)},
                QStringLiteral("flush left"));
            if (!edgeMark)
                return fail("left-edge highlight did not commit");
            if (edgeMark->position != QPointF(2.0, 40.0))
                return fail("page clamping dragged the mark off its text");
            if (edgeMark->iconRect().left() >= 0.0)
                return fail("badge should be free to sit in the page margin");

            // A page-wide mark is the case clampAxis() would have centred.
            const QSizeF pageSize = doc->pageSizeAt(0);
            LinkObject* wideMark = commitOcrHighlight(
                viewport, 0,
                {QRectF(0.0, 100.0, pageSize.width() + 40.0, 14.0)},
                QStringLiteral("wider than the page"));
            if (!wideMark || wideMark->position != QPointF(0.0, 100.0))
                return fail("an over-wide mark was re-centred by the clamp");

            // Selected annotations offer no handles: resize has always been a
            // no-op for them, and rotating text rects is meaningless.
            viewport.setCurrentTool(ToolType::ObjectSelect);
            viewport.selectObject(edgeMark, false);
            const QRectF bounds = viewport.objectBoundsInViewport(edgeMark);
            if (bounds.isEmpty())
                return fail("selected annotation had no viewport bounds");
            for (const QPointF& corner : {bounds.topLeft(), bounds.topRight(),
                                          bounds.bottomLeft(),
                                          bounds.bottomRight()}) {
                if (viewport.objectHandleAtPoint(corner)
                    != DocumentViewport::HandleHit::None)
                    return fail("annotation offered a resize handle");
            }
            if (viewport.objectHandleAtPoint(
                    QPointF(bounds.center().x(),
                            bounds.top() - DocumentViewport::ROTATE_HANDLE_OFFSET))
                != DocumentViewport::HandleHit::None)
                return fail("annotation offered a rotation handle");

            // The mark is the hit target, not its bounding box. A two-line
            // selection has a gap inside the box that must not respond, which a
            // single-line mark could not distinguish.
            viewport.setCurrentTool(ToolType::Highlighter);
            LinkObject* twoLine = commitOcrHighlight(
                viewport, 0,
                {QRectF(60.0, 200.0, 200.0, 14.0),
                 QRectF(60.0, 230.0, 200.0, 14.0)},
                QStringLiteral("two lines"));
            if (!twoLine)
                return fail("two-line highlight did not commit");
            viewport.setCurrentTool(ToolType::ObjectSelect);
            viewport.deselectAllObjects();

            const QPointF pageOrigin = viewport.pagePosition(0);
            if (viewport.objectAtPoint(pageOrigin + QPointF(150.0, 206.0))
                != twoLine)
                return fail("a tap on the highlight body did not find it");
            if (viewport.objectAtPoint(pageOrigin + QPointF(150.0, 236.0))
                != twoLine)
                return fail("a tap on the second line did not find it");
            if (viewport.objectAtPoint(pageOrigin + QPointF(150.0, 222.0))
                == twoLine)
                return fail("the gap between two lines was still a hit target");
            if (viewport.objectAtPoint(pageOrigin + QPointF(150.0, 260.0))
                == twoLine)
                return fail("a tap below the mark still hit the annotation");

            // Scroll-bar markers: an auto-derived description is not content,
            // but one the user typed is.
            if (!doc->pageLinkMarkers().isEmpty())
                return fail("auto-described highlights ticked the scroll bar");
            viewport.selectObject(edgeMark, false);
            viewport.setSelectedLinkDescription(QStringLiteral("mine"));
            if (!edgeMark->descriptionUserEdited)
                return fail("typing a description did not record the edit");
            if (doc->pageLinkMarkers().size() != 1)
                return fail("a user-written description produced no marker");
            viewport.setSelectedLinkDescription(QString());
            if (edgeMark->descriptionUserEdited
                || !doc->pageLinkMarkers().isEmpty())
                return fail("clearing the description did not give up the marker");

            viewport.setDocument(nullptr);
        }

        // ===== Edgeless: a mark spanning two tiles stays tappable =====
        {
            auto doc = Document::createNew("Edgeless highlight",
                                           Document::Mode::Edgeless);
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            // Starts in tile (0,0) and runs well past its 1024pt right edge.
            const qreal tileSize = Document::EDGELESS_TILE_SIZE;
            const QRectF wide(900.0, 300.0, tileSize, 20.0);
            LinkObject* mark = commitOcrHighlight(viewport, 0, {wide},
                                                 QStringLiteral("two tiles"));
            if (!mark)
                return fail("edgeless highlight did not commit");
            if (mark->position != QPointF(900.0, 300.0))
                return fail("edgeless mark was not rebased onto its owner tile");
            if (doc->maxObjectExtent() < static_cast<int>(tileSize))
                return fail("edgeless region did not widen the object extent");

            // The far end lives in the neighbouring tile. objectAtPoint() only
            // reaches it because it widens its tile sweep by maxObjectExtent().
            const QPointF farEnd(wide.right() - 10.0, wide.center().y());
            if (doc->tileCoordForPoint(farEnd)
                == doc->tileCoordForPoint(wide.topLeft()))
                return fail("test rect did not actually span two tiles");
            if (viewport.objectAtPoint(farEnd) != mark)
                return fail("the far end of a tile-spanning mark was untappable");

            viewport.setDocument(nullptr);
        }

        // ===== Page copy: the range is remapped, or staled but never dropped =====
        {
            auto srcDoc = Document::createNew("Copy source");
            Page* srcPage = srcDoc->page(0);
            if (!srcPage)
                return fail("missing copy-source page");

            auto makeMark = [](const QString& rangeUuid) {
                auto mark = std::make_unique<LinkObject>();
                mark->setRegionFromPageRects({QRectF(50, 60, 120, 14)});
                mark->region.style = HighlightRegion::Style::Cover;
                mark->region.color = QColor(255, 255, 0, 128);
                mark->region.sourceRange.pageUuid = rangeUuid;
                mark->region.sourceRange.startBoxIndex = 1;
                mark->region.sourceRange.endBoxIndex = 1;
                return mark;
            };

            auto inSet = makeMark(srcPage->uuid);
            const QString inSetId = inSet->id;
            srcPage->addObject(std::move(inSet));

            auto outOfSet = makeMark(
                QUuid::createUuid().toString(QUuid::WithoutBraces));
            const QString outOfSetId = outOfSet->id;
            srcPage->addObject(std::move(outOfSet));

            auto destDoc = Document::createNew("Copy destination");
            const PageImportResult result =
                destDoc->importPagesFrom(srcDoc.get(), {srcPage->uuid},
                                         destDoc->pageCount());
            if (result.destStartIndex < 0)
                return fail("page import did not insert anything");

            Page* copied = destDoc->page(result.destStartIndex);
            if (!copied)
                return fail("imported page did not load");

            auto findMark = [&](const QString& oldId) -> LinkObject* {
                const QString newId = result.objectIdMap.value(oldId);
                if (newId.isEmpty()) return nullptr;
                return dynamic_cast<LinkObject*>(copied->objectById(newId));
            };

            LinkObject* copiedInSet = findMark(inSetId);
            if (!copiedInSet)
                return fail("in-set annotation did not survive the copy");
            if (copiedInSet->region.rects.size() != 1)
                return fail("copy dropped the region rects");
            if (copiedInSet->region.sourceRange.pageUuid != copied->uuid)
                return fail("in-set source range was not remapped");
            if (copiedInSet->region.sourceRange.stale)
                return fail("a remappable range was needlessly staled");

            LinkObject* copiedOutOfSet = findMark(outOfSetId);
            if (!copiedOutOfSet)
                return fail("out-of-set annotation did not survive the copy");
            if (copiedOutOfSet->region.rects.size() != 1)
                return fail("an unresolvable range dropped the highlight");
            if (!copiedOutOfSet->region.sourceRange.stale
                || copiedOutOfSet->region.sourceRange.isUsable())
                return fail("out-of-set source range was not staled");
        }

        // ===== The disk-peek marker filter reads the same flag =====
        {
            QTemporaryDir dir;
            if (!dir.isValid())
                return fail("could not create a bundle directory");

            auto doc = Document::createNew("Peek markers");
            Page* page = doc->page(0);
            if (!page)
                return fail("missing peek test page");

            auto bare = std::make_unique<LinkObject>();
            bare->setRegionFromPageRects({QRectF(40, 50, 100, 14)});
            bare->description = QStringLiteral("auto-derived");
            page->addObject(std::move(bare));

            const QString bundle = dir.filePath(QStringLiteral("peek.snb"));
            if (!doc->saveBundle(bundle))
                return fail("could not save the peek bundle");
            doc->evictPage(0);
            if (doc->isPageLoaded(0))
                return fail("page stayed loaded, so the peek path is untested");
            if (!doc->pageLinkMarkers().isEmpty())
                return fail("disk peek ticked the bar for an auto-described mark");

            // Same annotation, now with the flag set on disk.
            Page* reloaded = doc->page(0);
            if (!reloaded)
                return fail("could not reload the peek page");
            for (const auto& object : reloaded->objects) {
                if (auto* link = dynamic_cast<LinkObject*>(object.get()))
                    link->descriptionUserEdited = true;
            }
            doc->markPageDirty(0);
            if (!doc->saveBundle(bundle))
                return fail("could not re-save the peek bundle");
            doc->evictPage(0);
            doc->refreshLinkOutlineFor(0);
            if (doc->isPageLoaded(0))
                return fail("page stayed loaded on the second peek");
            if (doc->pageLinkMarkers().size() != 1)
                return fail("disk peek ignored descriptionUserEdited");
        }

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Adjust mode: re-ranging an existing highlight's covered text.
     *
     * The load-bearing decisions here are that endpoints come from the region
     * rects rather than the stored source range (whose box indices address a
     * lazily rebuilt cache), that a whole session collapses to one undo entry no
     * matter how many tweaks it contains, and that Esc leaves no entry at all.
     * Also covers the two mode-plumbing traps: entering Adjust from ObjectSelect
     * must survive the tool switch, and tapping a highlight from the Highlighter
     * must still respect the layer-affinity filter.
     */
    static bool testHighlightAdjustMode() {
        printf("  testHighlightAdjustMode... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        constexpr qreal pdfToPage = 96.0 / 72.0;

        // Seed a synthetic OCR block whose per-character rects are an even
        // split, so expected character indices are arithmetic rather than
        // dependent on a real engine.
        auto seedOcrBlock = [](DocumentViewport& vp, const QString& text,
                               const QRectF& blockRect) {
            DocumentViewport::OcrBlockRef ref;
            ref.text = text;
            ref.blockRect = blockRect;
            const qreal charWidth = blockRect.width() / text.length();
            for (int i = 0; i < text.length(); ++i) {
                ref.charRects.append(QRectF(blockRect.left() + i * charWidth,
                                            blockRect.top(), charWidth,
                                            blockRect.height()));
            }
            vp.m_ocrBlockCache.clear();
            vp.m_ocrBlockCache.append(ref);
            vp.m_lastOcrHitBlockIndex = -1;
            // Makes loadOcrBlocksForPage() a no-op so the synthetic cache stands.
            vp.m_ocrBlockCachePageIndex = 0;
            if (vp.m_document && vp.m_document->isEdgeless())
                vp.m_ocrBlockCacheTileVersion = vp.m_document->tileLoadVersion();
        };

        auto commitOcrHighlight = [](DocumentViewport& vp, int pageIndex,
                                     const QVector<QRectF>& rects,
                                     const QString& text) -> LinkObject* {
            vp.m_textSelection.clear();
            vp.m_textSelection.source =
                DocumentViewport::TextSelection::Source::Ocr;
            vp.m_textSelection.pageIndex = pageIndex;
            vp.m_textSelection.startBoxIndex = 0;
            vp.m_textSelection.startCharIndex = 0;
            vp.m_textSelection.endBoxIndex = 0;
            vp.m_textSelection.endCharIndex = text.size();
            vp.m_textSelection.selectedText = text;
            vp.m_textSelection.highlightRects = rects;
            return vp.commitHighlightAnnotation();
        };

        // ===== Endpoints come from the rects: PDF word boxes =====
        {
            auto doc = Document::createNew("Adjust PDF endpoints");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            Page* page = doc->page(0);
            if (!page)
                return fail("missing PDF endpoint page");

            // Three word boxes on one line, in PDF coordinates.
            auto makeBox = [](const QString& text, qreal left) {
                PdfTextBox box;
                box.text = text;
                const qreal charWidth = 8.0;
                box.boundingBox =
                    QRectF(left, 100.0, charWidth * text.length(), 12.0);
                for (int i = 0; i < text.length(); ++i) {
                    box.charBoundingBoxes.append(
                        QRectF(left + i * charWidth, 100.0, charWidth, 12.0));
                }
                return box;
            };
            viewport.m_textBoxCache = {makeBox(QStringLiteral("Alpha"), 100.0),
                                       makeBox(QStringLiteral("Beta"), 150.0),
                                       makeBox(QStringLiteral("Gamma"), 190.0)};
            viewport.m_textBoxCachePageIndex = 0;
            viewport.m_lastHitBoxIndex = -1;

            // A mark covering "Alpha Beta": PDF x 100..182, converted to the page
            // coordinates the region actually stores.
            auto mark = std::make_unique<LinkObject>();
            mark->setRegionFromPageRects({QRectF(100.0 * pdfToPage,
                                                 100.0 * pdfToPage,
                                                 82.0 * pdfToPage,
                                                 12.0 * pdfToPage)});
            mark->region.style = HighlightRegion::Style::Cover;
            // Deliberately bogus stored indices: derivation must ignore them.
            mark->region.sourceRange.source = HighlightRegion::Source::Pdf;
            mark->region.sourceRange.startBoxIndex = 2;
            mark->region.sourceRange.startCharIndex = 4;
            mark->region.sourceRange.endBoxIndex = 2;
            mark->region.sourceRange.endCharIndex = 4;
            LinkObject* pdfMark = mark.get();
            page->addObject(std::move(mark));

            DocumentViewport::TextSelection derived;
            if (!viewport.deriveRegionEndpoints(pdfMark, derived))
                return fail("PDF endpoint derivation failed outright");
            if (derived.source != DocumentViewport::TextSelection::Source::Pdf)
                return fail("derived selection lost its PDF source");
            if (derived.startBoxIndex != 0 || derived.startCharIndex != 0)
                return fail("PDF start endpoint did not land on the first word");
            if (derived.endBoxIndex != 1 || derived.endCharIndex != 3)
                return fail("PDF end endpoint did not land on the last word");

            // Word snapping on PDF is picking an end of the box, since MuPDF
            // emits one box per word.
            int boxIndex = 1;
            int charIndex = 1;
            viewport.snapEndpointToWord(
                DocumentViewport::TextSelection::Source::Pdf, boxIndex,
                charIndex, false);
            if (charIndex != 3)
                return fail("PDF end snap did not reach the end of the word");
            charIndex = 2;
            viewport.snapEndpointToWord(
                DocumentViewport::TextSelection::Source::Pdf, boxIndex,
                charIndex, true);
            if (charIndex != 0)
                return fail("PDF start snap did not reach the start of the word");

            viewport.setDocument(nullptr);
        }

        // ===== Endpoints from rects: OCR paragraph, plus word snapping =====
        {
            auto doc = Document::createNew("Adjust OCR endpoints");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            Page* page = doc->page(0);
            if (!page)
                return fail("missing OCR endpoint page");

            // 16 characters over 160pt, so one character is exactly 10pt wide.
            const QString text = QStringLiteral("hello world here");
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));

            auto mark = std::make_unique<LinkObject>();
            // Covers "world": characters 6..10, x 110..160.
            mark->setRegionFromPageRects({QRectF(110.0, 200.0, 50.0, 16.0)});
            mark->region.style = HighlightRegion::Style::Cover;
            mark->region.sourceRange.source = HighlightRegion::Source::Ocr;
            LinkObject* ocrMark = mark.get();
            page->addObject(std::move(mark));

            DocumentViewport::TextSelection derived;
            if (!viewport.deriveRegionEndpoints(ocrMark, derived))
                return fail("OCR endpoint derivation failed outright");
            if (derived.source != DocumentViewport::TextSelection::Source::Ocr)
                return fail("derived selection lost its OCR source");
            if (derived.startBoxIndex != 0 || derived.startCharIndex != 6)
                return fail("OCR start endpoint missed the start of the word");
            if (derived.endBoxIndex != 0 || derived.endCharIndex != 10)
                return fail("OCR end endpoint missed the end of the word");

            // An OCR block is a paragraph, so snapping has to scan its text.
            viewport.m_textSelection = derived;
            viewport.m_textSelection.startCharIndex = 7;
            viewport.m_textSelection.endCharIndex = 8;
            viewport.snapSelectionToWords();
            if (viewport.m_textSelection.startCharIndex != 6
                || viewport.m_textSelection.endCharIndex != 10)
                return fail("OCR snapping did not expand to the whole word");

            // A backwards range snaps outward the other way round.
            viewport.m_textSelection.startCharIndex = 8;
            viewport.m_textSelection.endCharIndex = 7;
            viewport.snapSelectionToWords();
            if (viewport.m_textSelection.startCharIndex != 10
                || viewport.m_textSelection.endCharIndex != 6)
                return fail("a backwards range snapped the wrong ends outward");

            viewport.setDocument(nullptr);
        }

        // ===== CJK has no word separators, so snapping leaves it alone =====
        {
            auto doc = Document::createNew("Adjust CJK snapping");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());

            seedOcrBlock(viewport, QString::fromUtf8("中文汉字测试"),
                         QRectF(40.0, 60.0, 120.0, 20.0));

            int boxIndex = 0;
            int charIndex = 2;
            viewport.snapEndpointToWord(
                DocumentViewport::TextSelection::Source::Ocr, boxIndex,
                charIndex, true);
            if (charIndex != 2)
                return fail("CJK start snap swallowed neighbouring glyphs");
            charIndex = 3;
            viewport.snapEndpointToWord(
                DocumentViewport::TextSelection::Source::Ocr, boxIndex,
                charIndex, false);
            if (charIndex != 3)
                return fail("CJK end snap swallowed neighbouring glyphs");

            viewport.setDocument(nullptr);
        }

        // ===== A whole session is one undo entry; Esc is none =====
        {
            auto doc = Document::createNew("Adjust undo coalescing");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            const QString text = QStringLiteral("hello world here");
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));

            LinkObject* mark = commitOcrHighlight(
                viewport, 0, {QRectF(110.0, 200.0, 50.0, 16.0)},
                QStringLiteral("world"));
            if (!mark)
                return fail("adjust test highlight did not commit");
            // commitHighlightAnnotation() re-seeded the cache lookup, so restore
            // the synthetic block before deriving from it.
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));

            const HighlightRegion original = mark->region;
            const QPointF originalPosition = mark->position;
            const int stackAfterCommit = viewport.m_undoStack.size();

            if (!viewport.beginHighlightAdjust())
                return fail("could not enter Adjust on a selected highlight");
            if (!viewport.isAdjustingHighlight())
                return fail("Adjust session did not report itself active");
            if (!viewport.m_adjustSession.endpointsResolved)
                return fail("Adjust could not recover the covered range");

            // Three tweaks in a row, each written straight into the mark.
            auto retarget = [&](int startChar, int endChar) {
                viewport.m_textSelection.startCharIndex = startChar;
                viewport.m_textSelection.endCharIndex = endChar;
                viewport.snapSelectionToWords();
                viewport.updateSelectedTextAndRects();
                return viewport.applyAdjustedRangeToRegion();
            };
            if (!retarget(0, 10))
                return fail("first Adjust tweak did not reach the region");
            if (!retarget(0, 15))
                return fail("second Adjust tweak did not reach the region");
            if (!retarget(6, 15))
                return fail("third Adjust tweak did not reach the region");
            if (mark->region.rects == original.rects)
                return fail("the tweaks left the region unchanged");
            if (viewport.m_undoStack.size() != stackAfterCommit)
                return fail("a mid-session tweak pushed its own undo entry");

            viewport.commitHighlightAdjust();
            if (viewport.isAdjustingHighlight())
                return fail("commit left the session active");
            if (viewport.m_undoStack.size() != stackAfterCommit + 1)
                return fail("a three-tweak session was not one undo entry");
            if (viewport.m_undoStack.top().type
                != UndoAction::ObjectRegionChange)
                return fail("the session pushed the wrong undo type");
            if (viewport.m_undoStack.top().objectNewRegion.rects
                != mark->region.rects)
                return fail("the undo entry did not capture the new region");
            if (mark->region.sourceRange.stale
                || !mark->region.sourceRange.isUsable())
                return fail("commit did not refresh the source range");

            const HighlightRegion adjusted = mark->region;
            const QPointF adjustedPosition = mark->position;

            // Undo/redo round-trip. maxObjectExtent is recalculated in both
            // directions, so a shrink is reflected rather than latched.
            viewport.undo();
            if (mark->region.rects != original.rects
                || mark->position != originalPosition)
                return fail("undo did not restore the pre-Adjust region");
            viewport.redo();
            if (mark->region.rects != adjusted.rects
                || mark->position != adjustedPosition)
                return fail("redo did not reapply the adjusted region");

            // Esc abandons a session without leaving an entry behind.
            const int stackBeforeEsc = viewport.m_undoStack.size();
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));
            viewport.selectObject(mark, false);
            if (!viewport.beginHighlightAdjust())
                return fail("could not re-enter Adjust for the Esc case");
            if (!retarget(0, 4))
                return fail("the Esc-case tweak did not reach the region");
            if (mark->region.rects == adjusted.rects)
                return fail("the Esc-case tweak changed nothing to revert");
            if (!viewport.handleEscapeKey())
                return fail("Esc did not report handling the Adjust session");
            if (viewport.isAdjustingHighlight())
                return fail("Esc left the session active");
            if (mark->region.rects != adjusted.rects
                || mark->position != adjustedPosition)
                return fail("Esc did not restore the region it started with");
            if (viewport.m_undoStack.size() != stackBeforeEsc)
                return fail("Esc still pushed an undo entry");

            viewport.setDocument(nullptr);
        }

        // ===== Entering Adjust from ObjectSelect keeps the selection =====
        {
            auto doc = Document::createNew("Adjust from ObjectSelect");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            const QString text = QStringLiteral("hello world here");
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));
            LinkObject* mark = commitOcrHighlight(
                viewport, 0, {QRectF(110.0, 200.0, 50.0, 16.0)},
                QStringLiteral("world"));
            if (!mark)
                return fail("cross-tool test highlight did not commit");

            viewport.setCurrentTool(ToolType::ObjectSelect);
            viewport.selectObject(mark, false);
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));

            if (!viewport.beginHighlightAdjust())
                return fail("Adjust could not be entered from ObjectSelect");
            if (viewport.currentTool() != ToolType::Highlighter)
                return fail("Adjust did not take over the Highlighter tool");
            if (viewport.selectedObjects().size() != 1
                || viewport.selectedObjects().first() != mark)
                return fail("the tool switch destroyed the Adjust target");

            // The bar's toggle is the visible state of the session.
            viewport.refreshLinkObjectBar();
            auto* adjustToggle = viewport.m_linkObjectBar
                ? viewport.m_linkObjectBar->findChild<SubToolbarToggle*>(
                      QStringLiteral("linkAdjustToggle"))
                : nullptr;
            if (!adjustToggle)
                return fail("the bar has no Adjust toggle");
            if (!adjustToggle->isChecked())
                return fail("the bar's Adjust toggle did not follow the session");
            viewport.commitHighlightAdjust();
            if (adjustToggle->isChecked())
                return fail("the toggle stayed checked after Done");

            viewport.setDocument(nullptr);
        }

        // ===== Tapping a highlight selects it, subject to layer affinity =====
        {
            auto doc = Document::createNew("Adjust tap to select");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;
            Page* page = doc->page(0);
            if (!page)
                return fail("missing tap-to-select page");

            const QString text = QStringLiteral("hello world here");
            seedOcrBlock(viewport, text, QRectF(50.0, 200.0, 160.0, 16.0));
            LinkObject* mark = commitOcrHighlight(
                viewport, 0, {QRectF(110.0, 200.0, 50.0, 16.0)},
                QStringLiteral("world"));
            if (!mark)
                return fail("tap-to-select highlight did not commit");
            viewport.deselectAllObjects();

            PointerEvent pe;
            pe.type = PointerEvent::Press;
            pe.source = PointerEvent::Mouse;
            pe.button = Qt::LeftButton;
            pe.viewportPos = viewport.documentToViewport(
                viewport.pagePosition(0) + QPointF(130.0, 208.0));

            // Affinity mismatch first: a highlight made on one layer must not be
            // selectable from another, exactly like every other object.
            const int trueAffinity = mark->getLayerAffinity();
            mark->setLayerAffinity(trueAffinity + 1);
            viewport.handlePointerPress_Highlighter(pe);
            if (!viewport.selectedObjects().isEmpty())
                return fail("a tap crossed the layer-affinity filter");

            mark->setLayerAffinity(trueAffinity);
            viewport.handlePointerPress_Highlighter(pe);
            if (viewport.selectedObjects().size() != 1
                || viewport.selectedObjects().first() != mark)
                return fail("a tap on a highlight did not select it");
            if (viewport.m_textSelection.isSelecting)
                return fail("tap-to-select also started a text selection");

            viewport.setDocument(nullptr);
        }

        // ===== Edgeless: tile-local rects probe a document-space OCR cache =====
        {
            auto doc = Document::createNew("Adjust edgeless",
                                           Document::Mode::Edgeless);
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            // A block living in tile (1,0), addressed in document space as the
            // edgeless cache always is.
            const qreal tileSize = Document::EDGELESS_TILE_SIZE;
            const QString text = QStringLiteral("hello world here");
            const QRectF blockRect(tileSize + 50.0, 200.0, 160.0, 16.0);

            LinkObject* mark = commitOcrHighlight(
                viewport, 0,
                {QRectF(blockRect.left() + 60.0, 200.0, 50.0, 16.0)},
                QStringLiteral("world"));
            if (!mark)
                return fail("edgeless adjust highlight did not commit");
            if (mark->position.x() >= tileSize)
                return fail("edgeless mark was not rebased tile-local");
            seedOcrBlock(viewport, text, blockRect);

            DocumentViewport::TextSelection derived;
            if (!viewport.deriveRegionEndpoints(mark, derived))
                return fail("edgeless endpoint derivation failed");
            if (derived.startCharIndex != 6 || derived.endCharIndex != 10)
                return fail("edgeless derivation ignored the tile origin");

            // Extending the mark must widen the document-wide extent, or
            // edgeless culling would clip a mark that now spans further.
            if (!viewport.beginHighlightAdjust())
                return fail("could not enter Adjust in edgeless mode");
            viewport.m_textSelection.startCharIndex = 0;
            viewport.m_textSelection.endCharIndex = 15;
            viewport.snapSelectionToWords();
            viewport.updateSelectedTextAndRects();
            if (!viewport.applyAdjustedRangeToRegion())
                return fail("the edgeless tweak did not reach the region");
            viewport.commitHighlightAdjust();
            if (doc->maxObjectExtent() < 160)
                return fail("a widened edgeless mark did not widen the extent");

            viewport.setDocument(nullptr);
        }

        printf("PASSED\n");
        return true;
    }

    static bool testHighlightAppearanceEdit() {
        printf("  testHighlightAppearanceEdit... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        // Same synthetic OCR block the Adjust tests use: an even character
        // split, so indices are arithmetic rather than engine-dependent.
        auto seedOcrBlock = [](DocumentViewport& vp, const QString& text,
                               const QRectF& blockRect) {
            DocumentViewport::OcrBlockRef ref;
            ref.text = text;
            ref.blockRect = blockRect;
            const qreal charWidth = blockRect.width() / text.length();
            for (int i = 0; i < text.length(); ++i) {
                ref.charRects.append(QRectF(blockRect.left() + i * charWidth,
                                            blockRect.top(), charWidth,
                                            blockRect.height()));
            }
            vp.m_ocrBlockCache.clear();
            vp.m_ocrBlockCache.append(ref);
            vp.m_lastOcrHitBlockIndex = -1;
            vp.m_ocrBlockCachePageIndex = 0;
            if (vp.m_document && vp.m_document->isEdgeless())
                vp.m_ocrBlockCacheTileVersion = vp.m_document->tileLoadVersion();
        };

        auto seedSelection = [](DocumentViewport& vp, int pageIndex,
                                const QVector<QRectF>& rects,
                                const QString& text) {
            vp.m_textSelection.clear();
            vp.m_textSelection.source =
                DocumentViewport::TextSelection::Source::Ocr;
            vp.m_textSelection.pageIndex = pageIndex;
            vp.m_textSelection.startBoxIndex = 0;
            vp.m_textSelection.startCharIndex = 0;
            vp.m_textSelection.endBoxIndex = 0;
            vp.m_textSelection.endCharIndex = text.size();
            vp.m_textSelection.selectedText = text;
            vp.m_textSelection.highlightRects = rects;
        };

        auto commitOcrHighlight = [&](DocumentViewport& vp, int pageIndex,
                                      const QVector<QRectF>& rects,
                                      const QString& text) -> LinkObject* {
            seedSelection(vp, pageIndex, rects, text);
            return vp.commitHighlightAnnotation();
        };

        const QString text = QStringLiteral("hello world here");
        const QRectF blockRect(50.0, 200.0, 160.0, 16.0);
        const QVector<QRectF> markRects{QRectF(110.0, 200.0, 50.0, 16.0)};

        // ===== Recolour: stored alpha, derived badge, one undoable entry =====
        {
            auto doc = Document::createNew("Highlight recolour");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            seedOcrBlock(viewport, text, blockRect);
            LinkObject* mark = commitOcrHighlight(viewport, 0, markRects,
                                                  QStringLiteral("world"));
            if (!mark)
                return fail("recolour test highlight did not commit");

            const HighlightRegion originalRegion = mark->region;
            const QColor originalTint = mark->iconColor;
            const QPointF originalPosition = mark->position;
            const QSizeF originalSize = mark->size;
            const int stackAfterCommit = viewport.m_undoStack.size();

            // The bar hands over an opaque colour; the mark is stored at 50%.
            viewport.setSelectedLinkRegionColor(QColor(0, 200, 0));

            if (mark->region.color.alpha() != HighlightRegion::DEFAULT_OPACITY)
                return fail("a recolour did not store the default opacity");
            if (mark->region.color.green() != 200 || mark->region.color.red() != 0)
                return fail("a recolour did not store the picked colour");
            if (mark->iconColor != QColor(0, 100, 0, 255))
                return fail("the badge tint was not re-derived from the mark");
            if (mark->position != originalPosition || mark->size != originalSize)
                return fail("a recolour moved the annotation");

            if (viewport.m_undoStack.size() != stackAfterCommit + 1)
                return fail("a recolour was not exactly one undo entry");
            if (viewport.m_undoStack.top().type != UndoAction::ObjectRegionChange)
                return fail("a recolour pushed the wrong undo type");

            // Re-picking the same colour is not a change.
            viewport.setSelectedLinkRegionColor(QColor(0, 200, 0));
            if (viewport.m_undoStack.size() != stackAfterCommit + 1)
                return fail("re-picking the same colour pushed an entry");

            const QColor recolouredMark = mark->region.color;
            const QColor recolouredTint = mark->iconColor;

            viewport.undo();
            if (mark->region.color != originalRegion.color)
                return fail("undo did not restore the mark's colour");
            if (mark->iconColor != originalTint)
                return fail("undo left the badge tint on the new colour");
            if (mark->region.rects != originalRegion.rects
                || mark->position != originalPosition)
                return fail("undoing a recolour disturbed the geometry");

            viewport.redo();
            if (mark->region.color != recolouredMark
                || mark->iconColor != recolouredTint)
                return fail("redo did not reapply the colour and its badge");

            // ===== Style, driven through the bar's dropdown =====
            viewport.selectObject(mark, false);
            viewport.refreshLinkObjectBar();
            auto* styleButton = viewport.m_linkObjectBar
                ? viewport.m_linkObjectBar->findChild<QToolButton*>(
                      QStringLiteral("linkRegionStyle"))
                : nullptr;
            if (!styleButton)
                return fail("the bar has no highlight style dropdown");
            if (!styleButton->isVisible() && !styleButton->isVisibleTo(
                    viewport.m_linkObjectBar))
                return fail("the style dropdown is hidden for a highlight");
            if (!styleButton->menu()
                || styleButton->menu()->actions().size() != 3)
                return fail("the style dropdown does not offer exactly 3 styles");

            const int stackBeforeStyle = viewport.m_undoStack.size();
            styleButton->menu()->actions().at(1)->trigger();  // Underline
            if (mark->region.style != HighlightRegion::Style::Underline)
                return fail("the dropdown did not restyle the mark");
            if (viewport.m_undoStack.size() != stackBeforeStyle + 1)
                return fail("a restyle was not exactly one undo entry");

            viewport.undo();
            if (mark->region.style != HighlightRegion::Style::Cover)
                return fail("undo did not restore the mark's style");

            viewport.setDocument(nullptr);
        }

        // ===== A standalone link icon still edits its own tint, no undo =====
        {
            auto doc = Document::createNew("Standalone link colour");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            Page* page = doc->page(0);
            if (!page)
                return fail("missing standalone link page");

            auto link = std::make_unique<LinkObject>();
            link->position = QPointF(100.0, 100.0);
            link->size = QSizeF(24.0, 24.0);
            link->iconColor = QColor(180, 180, 180);
            LinkObject* icon = link.get();
            page->addObject(std::move(link));

            viewport.setCurrentTool(ToolType::ObjectSelect);
            viewport.selectObject(icon, false);
            viewport.refreshLinkObjectBar();

            auto* styleButton = viewport.m_linkObjectBar
                ? viewport.m_linkObjectBar->findChild<QToolButton*>(
                      QStringLiteral("linkRegionStyle"))
                : nullptr;
            if (!styleButton)
                return fail("the bar lost its style dropdown");
            if (styleButton->isVisibleTo(viewport.m_linkObjectBar))
                return fail("the style dropdown showed for an icon-only link");

            const int stackBefore = viewport.m_undoStack.size();
            viewport.setSelectedLinkColor(QColor(10, 20, 30));
            if (icon->iconColor != QColor(10, 20, 30))
                return fail("an icon-only link did not take the new tint");
            if (viewport.m_undoStack.size() != stackBefore)
                return fail("a badge-tint edit became undoable");

            // The region path must refuse an annotation with no mark.
            viewport.setSelectedLinkRegionColor(QColor(0, 200, 0));
            if (icon->region.color.isValid())
                return fail("an icon-only link accepted a mark colour");
            if (viewport.m_undoStack.size() != stackBefore)
                return fail("a refused recolour still pushed an entry");

            viewport.setDocument(nullptr);
        }

        // ===== Recolour inside a session folds in; Esc reverts appearance =====
        {
            auto doc = Document::createNew("Recolour during Adjust");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;

            seedOcrBlock(viewport, text, blockRect);
            LinkObject* mark = commitOcrHighlight(viewport, 0, markRects,
                                                  QStringLiteral("world"));
            if (!mark)
                return fail("session recolour highlight did not commit");
            seedOcrBlock(viewport, text, blockRect);

            const QColor startColor = mark->region.color;
            const QColor startTint = mark->iconColor;
            const int stackAfterCommit = viewport.m_undoStack.size();

            if (!viewport.beginHighlightAdjust())
                return fail("could not enter Adjust for the fold-in case");

            viewport.setSelectedLinkRegionColor(QColor(0, 0, 255));
            if (mark->region.color.blue() != 255)
                return fail("a mid-session recolour did not reach the mark");
            if (viewport.m_undoStack.size() != stackAfterCommit)
                return fail("a mid-session recolour pushed its own entry");

            viewport.m_textSelection.startCharIndex = 0;
            viewport.m_textSelection.endCharIndex = 10;
            viewport.snapSelectionToWords();
            viewport.updateSelectedTextAndRects();
            if (!viewport.applyAdjustedRangeToRegion())
                return fail("the fold-in tweak did not reach the region");

            viewport.commitHighlightAdjust();
            if (viewport.m_undoStack.size() != stackAfterCommit + 1)
                return fail("a recolour plus a tweak was not one entry");
            viewport.undo();
            if (mark->region.color != startColor || mark->iconColor != startTint)
                return fail("undoing the session did not revert the colour");
            viewport.redo();
            if (mark->region.color.blue() != 255)
                return fail("redoing the session did not reapply the colour");

            // Esc reverts appearance along with geometry, and leaves no entry.
            seedOcrBlock(viewport, text, blockRect);
            viewport.selectObject(mark, false);
            const QColor beforeEsc = mark->region.color;
            const QColor beforeEscTint = mark->iconColor;
            const int stackBeforeEsc = viewport.m_undoStack.size();

            if (!viewport.beginHighlightAdjust())
                return fail("could not enter Adjust for the Esc case");
            viewport.setSelectedLinkRegionColor(QColor(255, 0, 0));
            if (mark->region.color == beforeEsc)
                return fail("the Esc-case recolour changed nothing to revert");
            if (!viewport.handleEscapeKey())
                return fail("Esc did not report handling the session");
            if (mark->region.color != beforeEsc || mark->iconColor != beforeEscTint)
                return fail("Esc did not revert the appearance change");
            if (viewport.m_undoStack.size() != stackBeforeEsc)
                return fail("the Esc-case recolour left an undo entry");

            viewport.setDocument(nullptr);
        }

        // ===== The release gate: select-only makes nothing, and keeps text =====
        {
            auto doc = Document::createNew("Highlight on release");
            DocumentViewport viewport;
            viewport.resize(1100, 820);
            viewport.setDocument(doc.get());
            viewport.setCurrentTool(ToolType::Highlighter);
            viewport.setHighlighterMode(DocumentViewport::HighlighterMode::Ocr);
            viewport.m_autoHighlightStyle =
                DocumentViewport::HighlightStyle::Cover;
            Page* page = doc->page(0);
            if (!page)
                return fail("missing release-gate page");

            auto countLinks = [](Page* p) {
                int n = 0;
                for (const auto& object : p->objects) {
                    if (object && object->type() == QLatin1String("link")) ++n;
                }
                return n;
            };

            PointerEvent pe;
            pe.type = PointerEvent::Release;
            pe.source = PointerEvent::Mouse;
            pe.button = Qt::LeftButton;
            pe.viewportPos = viewport.documentToViewport(
                viewport.pagePosition(0) + QPointF(130.0, 208.0));

            seedOcrBlock(viewport, text, blockRect);
            viewport.setHighlightOnRelease(false);
            seedSelection(viewport, 0, markRects, QStringLiteral("world"));
            viewport.m_textSelection.isSelecting = true;
            viewport.handlePointerRelease_Highlighter(pe);

            if (countLinks(page) != 0)
                return fail("select-only mode still created an annotation");
            if (!viewport.m_textSelection.isValid()
                || viewport.m_textSelection.selectedText.isEmpty())
                return fail("select-only mode discarded the text to copy");

            seedOcrBlock(viewport, text, blockRect);
            viewport.setHighlightOnRelease(true);
            seedSelection(viewport, 0, markRects, QStringLiteral("world"));
            viewport.m_textSelection.isSelecting = true;
            viewport.handlePointerRelease_Highlighter(pe);

            if (countLinks(page) != 1)
                return fail("highlight mode did not commit exactly one mark");

            viewport.setDocument(nullptr);
        }

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Two-step pairing turns two empty slots into one bidirectional link.
     *
     * The two ends normally sit on different pages, which is the whole point of
     * the gesture and also where it is easiest to get wrong: the origin's page
     * is not the current one by the time the link is finished, so anything that
     * assumes otherwise writes to the wrong container.
     */
    static bool testPositionLinkPairing() {
        printf("  testPositionLinkPairing... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        auto doc = Document::createNew("Position pairing");
        doc->addPage();
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());
        viewport.setCurrentTool(ToolType::ObjectSelect);

        Page* pageA = doc->page(0);
        Page* pageB = doc->page(1);
        if (!pageA || !pageB)
            return fail("pairing fixture is missing a page");

        auto makeLink = [](Page* page, const QPointF& pos, const QString& desc) {
            auto ptr = std::make_unique<LinkObject>();
            ptr->position = pos;
            ptr->description = desc;
            LinkObject* raw = ptr.get();
            page->addObject(std::move(ptr));
            return raw;
        };

        LinkObject* origin = makeLink(pageA, QPointF(100.0, 120.0),
                                     QStringLiteral("chapter one"));
        LinkObject* target = makeLink(pageB, QPointF(300.0, 400.0),
                                      QStringLiteral("the footnote"));

        // Nothing armed to begin with, so the menu would offer only "start".
        if (viewport.isPairingPositionLink())
            return fail("a pairing was armed before anything started one");

        viewport.beginPositionLinkPairing(origin, 0);
        if (!viewport.isPairingPositionLink())
            return fail("arming the origin did not take");
        int armedSlot = -1;
        if (!viewport.isPairingOrigin(origin, &armedSlot) || armedSlot != 0)
            return fail("armed slot was not reported back");
        if (viewport.isPairingOrigin(target))
            return fail("the target was mistaken for the origin");
        if (viewport.pairingOriginDescription() != QStringLiteral("chapter one"))
            return fail("origin description was not captured for the menu");

        // Arming writes nothing: an unfinished link is not a fact about the
        // document.
        if (!origin->linkSlots[0].isEmpty())
            return fail("arming wrote into the slot");

        // The armed slot has to look different, or a half-made link is
        // invisible. It is still Empty on disk, so the bar applies the state.
        viewport.selectObject(origin, false);
        const auto slotButtons =
            viewport.m_linkObjectBar->findChildren<LinkSlotButton*>();
        if (slotButtons.size() != LinkObject::SLOT_COUNT)
            return fail("link bar was missing its slot buttons");
        if (slotButtons[0]->state() != LinkSlotState::PendingOrigin)
            return fail("armed slot did not show the pending state");
        if (slotButtons[1]->state() != LinkSlotState::Empty)
            return fail("pending state leaked onto another slot");

        // Linking an object to itself would navigate nowhere.
        viewport.completePositionLinkPairing(origin, 1);
        if (!origin->linkSlots[1].isEmpty() || !viewport.isPairingPositionLink())
            return fail("a self-link was allowed");

        // Finding the other end means navigating there, so by the time the link
        // is finished the current page is the target's, not the origin's. That
        // is what the assertions below are really testing: anything that
        // resolves the origin's container as "the current page" gets it wrong.
        viewport.scrollToPage(1);
        viewport.selectObject(target, false);
        if (viewport.currentPageIndex() == 0)
            return fail("pairing test never left the origin's page");
        viewport.completePositionLinkPairing(target, 2);

        if (viewport.isPairingPositionLink())
            return fail("pairing stayed armed after finishing");

        // Both ends spent a slot and point at each other.
        const LinkSlot& originSlot = origin->linkSlots[0];
        const LinkSlot& targetSlot = target->linkSlots[2];
        if (originSlot.type != LinkSlot::Type::Position
            || targetSlot.type != LinkSlot::Type::Position)
            return fail("pairing did not make both slots position links");
        if (originSlot.targetObjectId != target->id
            || targetSlot.targetObjectId != origin->id)
            return fail("the two ends do not point at each other");

        // Each end records its partner's page, not its own.
        if (originSlot.targetPageUuid != pageB->uuid
            || targetSlot.targetPageUuid != pageA->uuid)
            return fail("position links recorded the wrong page");

        // The coordinate is the partner's centre, so a highlight lands on its
        // mark rather than on the corner of its bounding box.
        const QPointF targetCentre =
            target->position + QPointF(target->size.width() / 2.0,
                                       target->size.height() / 2.0);
        if (QLineF(originSlot.targetPosition, targetCentre).length() > 0.01)
            return fail("origin slot did not snapshot the target's centre");

        // Each end names the far *slot*, not just the far object. Without that
        // the pair could not be released from either end, because an object id
        // alone is ambiguous once two annotations are paired twice.
        if (originSlot.targetSlotIndex != 2 || targetSlot.targetSlotIndex != 0)
            return fail("pairing did not record the partner slot index");

        // Back where the origin lives: clearing a slot is something the user
        // does to the annotation in front of them.
        viewport.scrollToPage(0);

        // A pairing is only a pairing when both ends agree, in both
        // directions. Probing the resolver directly keeps the pair intact for
        // the teardown below.
        int probeSlot = -1;
        if (!viewport.resolvePositionLinkPartner(origin, 0, &probeSlot,
                                                nullptr, nullptr)
            || probeSlot != 2)
            return fail("a genuine pairing was not recognised as one");

        target->linkSlots[2].targetSlotIndex = 1;  // names the wrong slot now
        if (viewport.resolvePositionLinkPartner(origin, 0, nullptr, nullptr,
                                               nullptr))
            return fail("a back-reference to the wrong slot counted as a pair");
        target->linkSlots[2].targetSlotIndex = 0;

        target->linkSlots[2].targetObjectId = QStringLiteral("somebody-else");
        if (viewport.resolvePositionLinkPartner(origin, 0, nullptr, nullptr,
                                               nullptr))
            return fail("a back-reference to another object counted as a pair");
        target->linkSlots[2].targetObjectId = origin->id;

        // Releasing one end releases the other. The far half is on another
        // page, which is exactly why leaving it behind would strand a slot the
        // user cannot see and would not remember.
        viewport.selectObject(origin, false);
        viewport.clearLinkSlot(0);
        if (!origin->linkSlots[0].isEmpty())
            return fail("clearing a paired slot left the near end filled");
        if (!target->linkSlots[2].isEmpty())
            return fail("clearing a paired slot stranded the far end");

        // A one-way link has no far end to release, so clearing it must stay a
        // purely local edit.
        target->linkSlots[1].type = LinkSlot::Type::Position;
        target->linkSlots[1].targetPageUuid = pageA->uuid;
        target->linkSlots[1].targetPosition = QPointF(10.0, 20.0);
        if (viewport.resolvePositionLinkPartner(target, 1, nullptr, nullptr,
                                               nullptr))
            return fail("a coordinate-only link claimed a partner");
        target->linkSlots[1].clear();

        // Cancelling puts everything back with nothing written.
        viewport.beginPositionLinkPairing(origin, 1);
        if (!viewport.isPairingPositionLink())
            return fail("could not arm a second link");
        viewport.cancelPositionLinkPairing();
        if (viewport.isPairingPositionLink() || !origin->linkSlots[1].isEmpty())
            return fail("cancelling a pairing did not clear it");

        // Esc is the keyboard route to the same place, and it outranks
        // deselection so the more specific state goes first.
        viewport.beginPositionLinkPairing(origin, 1);
        if (!viewport.handleEscapeKey())
            return fail("Esc did not consume the armed pairing");
        if (viewport.isPairingPositionLink())
            return fail("Esc left the pairing armed");

        // Deleting the armed origin leaves no other end to write, so the
        // pairing must not outlive it.
        viewport.beginPositionLinkPairing(origin, 1);
        viewport.selectObject(origin, false);
        viewport.deleteSelectedObjects();
        if (viewport.isPairingPositionLink())
            return fail("pairing survived deletion of its origin");

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief Copy and Delete on an annotation, through the shared policy.
     *
     * The action bar buttons, Ctrl+C / Delete, and the context menu all call
     * handleCopyAction() / handleDeleteAction(), so these two functions are the
     * only place the behaviour is defined. What matters is that a selected
     * annotation copies its text rather than the object, in either tool, and
     * that a text selection still copies its text when nothing is selected.
     *
     * Also covers what object copy does with a link now that it has no clone
     * path: it skips it, and skips it without discarding the clipboard when
     * that leaves nothing to copy.
     */
    static bool testAnnotationCopyDelete() {
        printf("  testAnnotationCopyDelete... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        QClipboard* clipboard = QGuiApplication::clipboard();
        if (!clipboard)
            return fail("no clipboard available");

        auto doc = Document::createNew("Annotation copy");
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());

        Page* page = doc->page(0);
        if (!page)
            return fail("missing copy test page");

        auto markPtr = std::make_unique<LinkObject>();
        markPtr->setRegionFromPageRects({QRectF(60.0, 80.0, 180.0, 14.0)});
        markPtr->region.style = HighlightRegion::Style::Cover;
        markPtr->description = QStringLiteral("the annotated sentence");
        LinkObject* mark = markPtr.get();
        page->addObject(std::move(markPtr));

        const QString objectsBefore =
            QStringLiteral("sentinel that object copy would not overwrite");

        for (ToolType tool : {ToolType::ObjectSelect, ToolType::Highlighter}) {
            viewport.setCurrentTool(tool);
            viewport.clearObjectSelection();
            viewport.selectObject(mark, false);

            clipboard->setText(objectsBefore);
            viewport.handleCopyAction();
            if (clipboard->text() != mark->description)
                return fail("copying an annotation did not yield its text");
        }

        // A bare text selection is the other half of the merged Copy button.
        viewport.setCurrentTool(ToolType::Highlighter);
        viewport.clearObjectSelection();
        viewport.m_textSelection.clear();
        viewport.m_textSelection.source =
            DocumentViewport::TextSelection::Source::Ocr;
        viewport.m_textSelection.pageIndex = 0;
        viewport.m_textSelection.startBoxIndex = 0;
        viewport.m_textSelection.startCharIndex = 0;
        viewport.m_textSelection.endBoxIndex = 0;
        viewport.m_textSelection.endCharIndex = 11;
        viewport.m_textSelection.selectedText = QStringLiteral("loose words");
        viewport.m_textSelection.highlightRects = {QRectF(60.0, 300.0, 100.0, 14.0)};
        clipboard->setText(objectsBefore);
        viewport.handleCopyAction();
        if (clipboard->text() != QStringLiteral("loose words"))
            return fail("a bare text selection did not copy its text");

        // Object copy skips links, the way it already skips recognized text.
        // Only a multi-selection reaches this: a lone annotation is handled by
        // the description copy above and never gets to copySelectedObjects().
        auto secondMarkPtr = std::make_unique<LinkObject>();
        secondMarkPtr->setRegionFromPageRects({QRectF(60.0, 120.0, 140.0, 14.0)});
        secondMarkPtr->description = QStringLiteral("another sentence");
        LinkObject* secondMark = secondMarkPtr.get();
        page->addObject(std::move(secondMarkPtr));

        auto imagePtr = std::make_unique<ImageObject>();
        QImage swatch(16, 16, QImage::Format_ARGB32);
        swatch.fill(Qt::cyan);
        imagePtr->setSourceImage(swatch);
        imagePtr->position = QPointF(60.0, 400.0);
        ImageObject* image = imagePtr.get();
        page->addObject(std::move(imagePtr));

        viewport.setCurrentTool(ToolType::ObjectSelect);
        viewport.clearObjectSelection();
        viewport.selectObject(mark, false);
        viewport.selectObject(image, true);
        viewport.copySelectedObjects();
        if (DocumentViewport::s_objectClipboard.size() != 1)
            return fail("a link plus an image did not copy exactly one object");
        if (DocumentViewport::s_objectClipboard.first()
                .value(QStringLiteral("type")).toString()
            != QStringLiteral("image"))
            return fail("the object copied alongside a link was not the image");

        // A selection with nothing copyable must leave the clipboard alone:
        // whatever is on it is still the last thing the user actually copied.
        viewport.clearObjectSelection();
        viewport.selectObject(mark, false);
        viewport.selectObject(secondMark, true);
        viewport.copySelectedObjects();
        if (!viewport.hasObjectsInClipboard())
            return fail("a links-only copy emptied the object clipboard");
        if (DocumentViewport::s_objectClipboard.size() != 1
            || DocumentViewport::s_objectClipboard.first()
                   .value(QStringLiteral("type")).toString()
               != QStringLiteral("image"))
            return fail("a links-only copy replaced the previous clipboard");

        // Delete used to be a no-op under the Highlighter, which left the only
        // route to removing a tapped annotation in the other tool.
        viewport.setCurrentTool(ToolType::Highlighter);
        viewport.clearObjectSelection();
        viewport.selectObject(mark, false);
        // Taken before the delete: removeObject destroys the object, so mark is
        // a dangling pointer by the time the lookup below runs.
        const QString markId = mark->id;
        const int objectsBeforeDelete = static_cast<int>(page->objects.size());
        viewport.handleDeleteAction();
        if (static_cast<int>(page->objects.size()) != objectsBeforeDelete - 1)
            return fail("Delete under the Highlighter left the annotation");
        if (page->objectById(markId))
            return fail("the deleted annotation is still reachable");

        printf("PASSED\n");
        return true;
    }

    /**
     * @brief The right-click menus: which entries, and where they lead.
     *
     * exec() is modal, so the menus are asserted through the populate seam
     * rather than by popping them up. Two things are worth guarding: that a
     * link object gets its own two-entry menu instead of the object clipboard
     * one, and that the entries reach the policy functions rather than
     * duplicating the clipboard logic a third time.
     */
    static bool testAnnotationContextMenu() {
        printf("  testAnnotationContextMenu... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        QClipboard* clipboard = QGuiApplication::clipboard();
        if (!clipboard)
            return fail("no clipboard available");

        // The menu builds its labels with tr(), and translations are installed
        // before the tests run, so expectations go through the same call rather
        // than through English literals.
        auto label = [](const char* source) {
            return DocumentViewport::tr(source);
        };
        // Entry labels, skipping the separators, so a test reads like the menu.
        auto entries = [](const QMenu& menu) {
            QStringList names;
            for (QAction* action : menu.actions()) {
                if (!action->isSeparator())
                    names << action->text();
            }
            return names;
        };
        auto entryNamed = [](const QMenu& menu, const QString& name) -> QAction* {
            for (QAction* action : menu.actions()) {
                if (action->text() == name)
                    return action;
            }
            return nullptr;
        };

        auto doc = Document::createNew("Annotation menu");
        DocumentViewport viewport;
        viewport.resize(1100, 820);
        viewport.setDocument(doc.get());

        Page* page = doc->page(0);
        if (!page)
            return fail("missing menu test page");

        // A single-rect mark, so its centre is a genuine hit: containsPoint()
        // tests the rects rather than their bounding box.
        const QRectF markRect(60.0, 200.0, 200.0, 14.0);
        auto markPtr = std::make_unique<LinkObject>();
        markPtr->setRegionFromPageRects({markRect});
        markPtr->region.style = HighlightRegion::Style::Cover;
        markPtr->description = QStringLiteral("the annotated sentence");
        LinkObject* mark = markPtr.get();
        page->addObject(std::move(markPtr));

        auto imagePtr = std::make_unique<ImageObject>();
        QImage swatch(24, 24, QImage::Format_ARGB32);
        swatch.fill(Qt::magenta);
        imagePtr->setSourceImage(swatch);
        imagePtr->position = QPointF(60.0, 400.0);
        ImageObject* image = imagePtr.get();
        page->addObject(std::move(imagePtr));

        // ===== A selected link gets the link menu, not the object one =====
        viewport.setCurrentTool(ToolType::ObjectSelect);
        viewport.selectObject(mark, false);
        {
            QMenu menu;
            viewport.populateObjectContextMenu(menu);
            const QStringList names = entries(menu);
            if (names != QStringList{label("Copy Text"), label("Delete")})
                return fail("the link menu held the wrong entries");
            // These are the object-clipboard entries the link menu replaces.
            if (entryNamed(menu, label("Cut"))
                || entryNamed(menu, label("Paste"))
                || entryNamed(menu, label("Edit Text")))
                return fail("the link menu still offered object actions");

            clipboard->setText(QStringLiteral("sentinel"));
            entryNamed(menu, label("Copy Text"))->trigger();
            if (clipboard->text() != mark->description)
                return fail("the menu's Copy Text did not copy the description");
        }

        // ===== A non-link selection keeps the object menu =====
        viewport.clearObjectSelection();
        viewport.selectObject(image, false);
        {
            QMenu menu;
            viewport.populateObjectContextMenu(menu);
            for (const char* wanted : {"Cut", "Copy", "Paste", "Delete"}) {
                if (!entryNamed(menu, label(wanted)))
                    return fail("the object menu lost one of its entries");
            }
            if (entryNamed(menu, label("Copy Text")))
                return fail("an image was offered the annotation's Copy Text");
        }

        // ===== Nothing to copy reads as a greyed entry, not a dead one =====
        viewport.clearObjectSelection();
        const QString describedAgain = mark->description;
        mark->description.clear();
        viewport.selectObject(mark, false);
        {
            QMenu menu;
            viewport.populateObjectContextMenu(menu);
            QAction* copyText = entryNamed(menu, label("Copy Text"));
            if (!copyText)
                return fail("an undescribed link lost its Copy Text entry");
            if (copyText->isEnabled())
                return fail("Copy Text was live with nothing to copy");
        }
        mark->description = describedAgain;

        // ===== The Highlighter resolves its own target =====
        viewport.setCurrentTool(ToolType::Highlighter);
        viewport.deselectAllObjects();

        const QPointF pageOrigin = viewport.pagePosition(0);
        const QPoint onMark =
            viewport.documentToViewport(pageOrigin + markRect.center()).toPoint();
        const QPoint offMark =
            viewport.documentToViewport(pageOrigin + QPointF(60.0, 600.0)).toPoint();

        if (viewport.prepareAnnotationContextMenu(onMark) != mark)
            return fail("a right-click on the mark found no annotation");
        if (viewport.m_selectedObjects.size() != 1
            || viewport.m_selectedObjects.first() != mark)
            return fail("the right-click did not select the annotation");

        if (viewport.prepareAnnotationContextMenu(offMark))
            return fail("a right-click on bare page found an annotation");
        if (viewport.m_selectedObjects.size() != 1)
            return fail("a miss disturbed the existing selection");

        // ===== A text selection gets its own one-entry menu =====
        viewport.deselectAllObjects();
        viewport.m_textSelection.clear();
        viewport.m_textSelection.source =
            DocumentViewport::TextSelection::Source::Ocr;
        viewport.m_textSelection.pageIndex = 0;
        viewport.m_textSelection.startBoxIndex = 0;
        viewport.m_textSelection.startCharIndex = 0;
        viewport.m_textSelection.endBoxIndex = 0;
        viewport.m_textSelection.endCharIndex = 11;
        viewport.m_textSelection.selectedText = QStringLiteral("loose words");
        viewport.m_textSelection.highlightRects = {QRectF(60.0, 300.0, 100.0, 14.0)};
        {
            QMenu menu;
            viewport.populateTextSelectionContextMenu(menu);
            if (entries(menu) != QStringList{label("Copy")})
                return fail("the text menu was not a single Copy entry");
            clipboard->setText(QStringLiteral("sentinel"));
            menu.actions().first()->trigger();
            if (clipboard->text() != QStringLiteral("loose words"))
                return fail("the text menu's Copy did not copy the selection");
        }

        // A bare tap on text leaves a zero-length selection that isValid()
        // accepts, and Copy must not be offered for it.
        viewport.m_textSelection.selectedText.clear();
        if (viewport.m_textSelection.isValid()
            && !viewport.m_textSelection.selectedText.isEmpty())
            return fail("a zero-length selection still looked copyable");

        // Selecting an annotation drops the text selection, and the shared
        // helper is what both the tap and the right-click rely on for that.
        viewport.m_textSelection.selectedText = QStringLiteral("loose words");
        viewport.selectAnnotation(mark);
        if (viewport.m_textSelection.isValid())
            return fail("selecting an annotation kept the text selection");

        printf("PASSED\n");
        return true;
    }

    static bool testOcrTextBoxConversion() {
        printf("  testOcrTextBoxConversion... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };

        auto firstTextBox = [](Page* page) -> TextBoxObject* {
            for (const auto& object : page->objects) {
                if (object && object->type() == QLatin1String("textbox"))
                    return static_cast<TextBoxObject*>(object.get());
            }
            return nullptr;
        };

        auto makeBlock = [](const QString& text, const QRectF& rect,
                            const QVector<QString>& strokeIds) {
            OcrTextBlock block = OcrTextBlock::create();
            block.text = text;
            block.boundingRect = rect;
            block.confidence = 0.9f;
            block.engineId = QStringLiteral("test-engine");
            block.sourceStrokeIds = strokeIds;
            OcrTextBlock::WordSegment segment;
            segment.text = text;
            segment.boundingRect = rect;
            block.wordSegments = {segment};
            return block;
        };

        // The dismissal predicate is shared with the OCR result handler and
        // with load-time materialization, so check its contract directly.
        {
            const QSet<QString> suppressed{QStringLiteral("s1"),
                                           QStringLiteral("s2")};
            const QSet<QString> noKeys;
            OcrTextBlock all = makeBlock(QStringLiteral("all"),
                                         QRectF(0, 0, 10, 10),
                                         {QStringLiteral("s1"),
                                          QStringLiteral("s2")});
            OcrTextBlock partial = makeBlock(QStringLiteral("partial"),
                                             QRectF(0, 0, 10, 10),
                                             {QStringLiteral("s1"),
                                              QStringLiteral("s3")});
            OcrTextBlock none = makeBlock(QStringLiteral("none"),
                                          QRectF(0, 0, 10, 10), {});
            if (!isOcrBlockDismissed(all, suppressed, noKeys)
                || isOcrBlockDismissed(partial, suppressed, noKeys)
                || isOcrBlockDismissed(none, suppressed, noKeys)
                || isOcrBlockDismissed(all, QSet<QString>(), noKeys)) {
                return fail("dismissed-block predicate was wrong");
            }

            // A stroke-less block has no suppression to key off, so only the
            // geometry fingerprint can keep a rescan from resurrecting it. A
            // rescan mints a new block id, so the key must ignore ids.
            const QSet<QString> dismissedKeys{ocrBlockDismissalKey(none)};
            OcrTextBlock rescanned = makeBlock(QStringLiteral("none"),
                                               QRectF(0, 0, 10, 10), {});
            OcrTextBlock elsewhere = makeBlock(QStringLiteral("none"),
                                               QRectF(400, 0, 10, 10), {});
            if (!isOcrBlockDismissed(rescanned, suppressed, dismissedKeys)
                || isOcrBlockDismissed(elsewhere, suppressed, dismissedKeys)
                || isOcrBlockDismissed(all, QSet<QString>(), dismissedKeys)) {
                return fail("stroke-less dismissal key was wrong");
            }
        }

        QTemporaryDir bundle;
        if (!bundle.isValid())
            return fail("could not create temporary bundle");

        auto doc = Document::createNew("OCR conversion");
        doc->setBundlePath(bundle.path());
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());
        viewport.setCurrentTool(ToolType::ObjectSelect);
        Page* page = doc->page(0);
        if (!page)
            return fail("missing conversion test page");

        const OcrTextBlock neighbour = makeBlock(
            QStringLiteral("Neighbour"), QRectF(60.0, 320.0, 120.0, 30.0),
            {QStringLiteral("s9")});
        const OcrTextBlock block = makeBlock(
            QStringLiteral("Recognized ink"), QRectF(60.0, 90.0, 200.0, 40.0),
            {QStringLiteral("s1"), QStringLiteral("s2")});
        page->ocrTextBlocks = {neighbour, block};
        // Pretend an earlier deletion already suppressed one of the strokes.
        page->suppressedStrokeIds.insert(QStringLiteral("s1"));

        auto ocrObject = OcrTextObject::createFromBlock(
            block, QColor(20, 40, 60), false);
        ocrObject->visible = true;
        ocrObject->ocrLocked = true;
        ocrObject->showConfidence = true;
        ocrObject->ocrGridSpacing = 48;
        OcrTextObject* ocr = ocrObject.get();
        const QString ocrId = ocr->id;
        const QColor ocrColor = ocr->fontColor;
        const qreal expectedFontSize = ocr->estimateBaseFontSize();
        if (expectedFontSize <= 0.0 || expectedFontSize > 96.0)
            return fail("estimated OCR base font size was out of range");
        page->addObject(std::move(ocrObject));
        viewport.selectObject(ocr, false);

        if (!viewport.convertOcrTextToTextBox(ocr, false))
            return fail("locked OCR conversion was rejected");

        TextBoxObject* converted = firstTextBox(page);
        if (!converted)
            return fail("conversion produced no text box");
        const QString textBoxId = converted->id;
        if (textBoxId == ocrId)
            return fail("converted box reused the OCR block id");
        if (page->objectById(ocrId))
            return fail("source OCR object survived conversion");
        if (converted->text != block.text
            || converted->fontColor != ocrColor
            || !converted->visible
            || converted->fontSize != expectedFontSize
            || converted->textLayoutVersion
                != TextBoxObject::CURRENT_TEXT_LAYOUT_VERSION)
            return fail("converted box lost text or base style");
        if (qAbs(converted->size.height()
                 - converted->normalizedSizeForWidth(
                       converted->size.width()).height()) > 0.01)
            return fail("converted box height was not normalized");
        if (!page->suppressedStrokeIds.contains(QStringLiteral("s1"))
            || !page->suppressedStrokeIds.contains(QStringLiteral("s2")))
            return fail("conversion did not suppress the source strokes");
        if (page->ocrTextBlocks.size() != 1
            || page->ocrTextBlocks.first().id != neighbour.id)
            return fail("conversion removed the wrong OCR block");
        if (viewport.m_undoStack.isEmpty()
            || viewport.m_undoStack.top().type
                != UndoAction::OcrConvertToTextBox)
            return fail("conversion did not push one atomic undo action");
        if (viewport.m_selectedObjects.size() != 1
            || viewport.m_selectedObjects.first() != converted)
            return fail("conversion did not select the new text box");

        {
            QFile sidecar(bundle.path() + "/pages/" + page->uuid
                          + ".ocr.json");
            if (!sidecar.open(QIODevice::ReadOnly))
                return fail("conversion did not write the OCR sidecar");
            const QJsonObject root =
                QJsonDocument::fromJson(sidecar.readAll()).object();
            const QJsonArray savedBlocks =
                root.value(QStringLiteral("blocks")).toArray();
            QStringList savedSuppressed;
            for (const auto& value :
                 root.value(QStringLiteral("suppressedStrokeIds")).toArray()) {
                savedSuppressed.append(value.toString());
            }
            if (savedBlocks.size() != 1
                || !savedSuppressed.contains(QStringLiteral("s1"))
                || !savedSuppressed.contains(QStringLiteral("s2")))
                return fail("sidecar did not record the conversion");
        }

        viewport.undo();
        auto* restored = dynamic_cast<OcrTextObject*>(page->objectById(ocrId));
        if (!restored)
            return fail("undo did not restore the OCR object");
        if (!restored->ocrLocked || !restored->showConfidence
            || restored->ocrGridSpacing != 48
            || restored->wordSegments.size() != 1
            || restored->sourceStrokeIds.size() != 2)
            return fail("undo lost OCR object state");
        if (page->objectById(textBoxId))
            return fail("undo left the converted text box behind");
        if (!page->suppressedStrokeIds.contains(QStringLiteral("s1")))
            return fail("undo un-suppressed a previously suppressed stroke");
        if (page->suppressedStrokeIds.contains(QStringLiteral("s2")))
            return fail("undo kept the conversion's suppression");
        if (page->ocrTextBlocks.size() != 2
            || page->ocrTextBlocks[1].id != block.id
            || page->ocrTextBlocks[1].text != block.text)
            return fail("undo did not restore the block at its index");

        viewport.redo();
        TextBoxObject* redone = firstTextBox(page);
        if (!redone || redone->id != textBoxId)
            return fail("redo did not reproduce the same text box id");
        if (page->objectById(ocrId)
            || page->ocrTextBlocks.size() != 1
            || !page->suppressedStrokeIds.contains(QStringLiteral("s2")))
            return fail("redo left stale OCR state behind");
        page->removeObject(textBoxId);
        viewport.m_selectedObjects.clear();
        viewport.m_undoStack.clear();
        viewport.m_redoStack.clear();

        // A block with no source strokes can still be converted; there is
        // simply nothing to suppress.
        auto orphanObject = OcrTextObject::createFromBlock(
            makeBlock(QStringLiteral("Orphan"),
                      QRectF(60.0, 400.0, 140.0, 30.0), {}),
            QColor(10, 10, 10), false);
        orphanObject->visible = true;
        OcrTextObject* orphan = orphanObject.get();
        const QString orphanId = orphan->id;
        const int suppressedBefore = page->suppressedStrokeIds.size();
        page->addObject(std::move(orphanObject));
        if (!viewport.convertOcrTextToTextBox(orphan, false))
            return fail("conversion without stroke ids was rejected");
        if (page->objectById(orphanId)
            || page->suppressedStrokeIds.size() != suppressedBefore)
            return fail("stroke-less conversion changed suppression");
        viewport.undo();
        if (!page->objectById(orphanId))
            return fail("undo did not restore the stroke-less OCR object");
        page->removeObject(orphanId);
        viewport.m_undoStack.clear();
        viewport.m_redoStack.clear();

        // A block whose reflow cannot fit the page is rejected outright.
        QString longText = QStringLiteral("overflow");
        for (int i = 0; i < 60; ++i)
            longText += QStringLiteral(" overflow");
        const OcrTextBlock tallBlock = makeBlock(
            longText, QRectF(40.0, page->size.height() - 60.0, 60.0, 60.0),
            {QStringLiteral("s5")});
        page->ocrTextBlocks.append(tallBlock);
        auto tallObject = OcrTextObject::createFromBlock(
            tallBlock, QColor(0, 0, 0), false);
        OcrTextObject* tall = tallObject.get();
        const QString tallId = tall->id;
        const int blocksBefore = page->ocrTextBlocks.size();
        page->addObject(std::move(tallObject));
        if (viewport.convertOcrTextToTextBox(tall, false))
            return fail("overflowing conversion was accepted");
        if (!page->objectById(tallId)
            || page->ocrTextBlocks.size() != blocksBefore
            || page->suppressedStrokeIds.contains(QStringLiteral("s5"))
            || !viewport.m_undoStack.isEmpty())
            return fail("rejected conversion still mutated the page");
        page->removeObject(tallId);
        page->ocrTextBlocks.removeLast();

        // Conversion started from a double-click also opens the editor.
        auto editObject = OcrTextObject::createFromBlock(
            makeBlock(QStringLiteral("Editable"),
                      QRectF(60.0, 200.0, 160.0, 32.0),
                      {QStringLiteral("s7")}),
            QColor(0, 0, 0), false);
        editObject->visible = true;
        OcrTextObject* editable = editObject.get();
        page->addObject(std::move(editObject));
        if (!viewport.convertOcrTextToTextBox(editable, true))
            return fail("conversion with editing was rejected");
        TextBoxObject* editedBox = viewport.resolveInlineTextBox();
        if (!viewport.hasActiveInlineTextEdit() || !editedBox
            || editedBox->text != QStringLiteral("Editable"))
            return fail("conversion did not start inline editing");
        if (!viewport.m_textBoxFormatBar
            || viewport.m_textBoxFormatBar->isHidden())
            return fail("converted box did not get the format bar");
        viewport.cancelInlineTextEdit();

        // Edgeless conversion keeps the object on its owning tile.
        auto edgeless = Document::createNew(
            "OCR edgeless", Document::Mode::Edgeless);
        DocumentViewport edgeViewport;
        edgeViewport.resize(900, 700);
        edgeViewport.setDocument(edgeless.get());
        edgeViewport.setCurrentTool(ToolType::ObjectSelect);
        Page* tile = edgeless->getOrCreateTile(1, 2);
        if (!tile)
            return fail("could not create conversion tile");
        const OcrTextBlock tileBlock = makeBlock(
            QStringLiteral("Tile ink"), QRectF(30.0, 40.0, 150.0, 30.0),
            {QStringLiteral("t1")});
        tile->ocrTextBlocks = {tileBlock};
        auto tileObject = OcrTextObject::createFromBlock(
            tileBlock, QColor(0, 0, 0), false);
        tileObject->visible = true;
        OcrTextObject* tileOcr = tileObject.get();
        const QString tileOcrId = tileOcr->id;
        tile->addObject(std::move(tileObject));
        if (!edgeViewport.convertOcrTextToTextBox(tileOcr, false))
            return fail("edgeless conversion was rejected");
        TextBoxObject* tileBox = firstTextBox(tile);
        if (!tileBox || tile->objectById(tileOcrId)
            || !tile->ocrTextBlocks.isEmpty()
            || !tile->suppressedStrokeIds.contains(QStringLiteral("t1")))
            return fail("edgeless conversion left stale tile state");
        if (edgeViewport.m_undoStack.isEmpty()
            || edgeViewport.m_undoStack.top().objectTileCoord
                != Document::TileCoord(1, 2))
            return fail("edgeless conversion recorded the wrong tile");
        edgeViewport.undo();
        if (!tile->objectById(tileOcrId)
            || tile->ocrTextBlocks.size() != 1
            || tile->suppressedStrokeIds.contains(QStringLiteral("t1")))
            return fail("edgeless undo did not restore the OCR block");

        viewport.setDocument(nullptr);
        edgeViewport.setDocument(nullptr);
        printf("PASSED\n");
        return true;
    }

    /**
     * @brief The add-page button anchored under the last page.
     *
     * The load-bearing property is that the reserved band lives in
     * totalContentSize() and not in the page layout cache: the cache is also
     * pageTrackFraction()'s denominator, so putting the band there would drag
     * every scroll bar accent and marker off its page.
     */
    static bool testAddPageButton() {
        printf("  testAddPageButton... ");

        auto fail = [](const char* message) {
            printf("FAILED: %s\n", message);
            return false;
        };
        auto close = [](qreal a, qreal b) { return qAbs(a - b) < 0.5; };

        auto doc = Document::createNew("Add page button");
        doc->addPage();
        doc->addPage();

        DocumentViewport viewport;
        viewport.resize(900, 700);
        // A child of an unshown parent is never isVisible(), and the stylus
        // pass-through check below depends on visibility.
        viewport.setAttribute(Qt::WA_DontShowOnScreen, true);
        viewport.show();
        QApplication::processEvents();
        viewport.setDocument(doc.get());
        viewport.setZoomLevel(1.0);
        viewport.setLayoutMode(LayoutMode::SingleColumn);

        if (!viewport.m_addPageButton || viewport.m_addPageButton->isHidden())
            return fail("a paged document did not get an add-page button");

        // ----- The reserved band -----
        const qreal footprint =
            2 * DocumentViewport::ADD_PAGE_BUTTON_GAP + ActionBarButton::BUTTON_SIZE;
        const qreal pagesBottom = viewport.pageRect(doc->pageCount() - 1).bottom();
        if (!close(viewport.totalContentSize().height(), pagesBottom + footprint))
            return fail("the band is missing from the scroll extent at 1.0 zoom");

        // Measured in viewport pixels, so it halves in document units when the
        // zoom doubles and the button keeps fitting inside it.
        viewport.setZoomLevel(2.0);
        if (!close(viewport.totalContentSize().height(), pagesBottom + footprint / 2.0))
            return fail("the band did not track zoom");
        viewport.setZoomLevel(1.0);

        // The regression the design exists to avoid: the band must not reach
        // the layout cache, which is what page positions are measured against.
        const qreal expectedTrack = viewport.pageRect(1).top() / pagesBottom;
        if (!close(viewport.pageTrackFraction(1), expectedTrack))
            return fail("the band leaked into pageTrackFraction");

        // ----- Fully on screen at the scroll bar's maximum -----
        // Without the band this is exactly where the button would be invisible.
        const qreal viewHeight = viewport.height() / viewport.zoomLevel();
        viewport.setPanOffset(
            QPointF(0, viewport.totalContentSize().height() - viewHeight));
        if (!viewport.rect().contains(viewport.m_addPageButton->geometry()))
            return fail("the button is not fully visible at maximum scroll");

        // ----- Placement, single column -----
        auto expectedGeometry = [&](const QRectF& row) {
            const QPointF anchor =
                viewport.documentToViewport(QPointF(row.center().x(), row.bottom()));
            const int size = ActionBarButton::BUTTON_SIZE;
            return QRect(qRound(anchor.x()) - size / 2,
                         qRound(anchor.y()) + DocumentViewport::ADD_PAGE_BUTTON_GAP,
                         size, size);
        };
        const QRectF lastPage = viewport.pageRect(doc->pageCount() - 1);
        if (viewport.m_addPageButton->geometry() != expectedGeometry(lastPage))
            return fail("single column did not centre the button on the last page");

        // ----- Placement, two column with an odd count -----
        // Page index 2 is even, so it sits alone and the row is just that page.
        viewport.setLayoutMode(LayoutMode::TwoColumn);
        if (viewport.m_addPageButton->geometry()
            != expectedGeometry(viewport.pageRect(2)))
            return fail("a lone last page did not anchor the button to itself");

        // ----- Placement, two column with an even count -----
        // Page index 3 is the right half of a full row, so the row spans both.
        doc->addPage();
        viewport.notifyDocumentStructureChanged();
        const QRectF spread = viewport.pageRect(2).united(viewport.pageRect(3));
        if (viewport.m_addPageButton->geometry() != expectedGeometry(spread))
            return fail("a full last row did not centre the button on the spread");
        if (viewport.m_addPageButton->geometry()
            == expectedGeometry(viewport.pageRect(3)))
            return fail("the button anchored to the right page instead of the row");

        // ----- Stylus pass-through -----
        // A pen press over the button propagates up to the canvas, which has to
        // leave it unhandled so Qt synthesizes a mouse event for the button.
        if (!viewport.pointerOverViewportWidget(
                QRectF(viewport.m_addPageButton->geometry()).center()))
            return fail("the button area was not excluded from canvas input");

        // ----- The request reaches the signal -----
        QSignalSpy requested(&viewport, &DocumentViewport::addPageRequested);
        ActionBarButton* button = viewport.m_addPageButton;
        const QPointF centre(button->width() / 2.0, button->height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, centre, centre,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, centre, centre,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(button, &press);
        QApplication::sendEvent(button, &release);
        if (requested.count() != 1)
            return fail("clicking the button did not request a page");

        // ----- Documents with nothing to append to -----
        auto edgeless = Document::createNew("Edgeless", Document::Mode::Edgeless);
        viewport.setDocument(edgeless.get());
        if (!viewport.m_addPageButton->isHidden())
            return fail("an edgeless document showed the add-page button");
        if (viewport.addPageBandHeight() != 0.0)
            return fail("an edgeless document reserved a band");

        Document pageless;
        viewport.setDocument(&pageless);
        if (!viewport.m_addPageButton->isHidden())
            return fail("a document with no pages showed the add-page button");

        viewport.setDocument(nullptr);
        if (!viewport.m_addPageButton->isHidden())
            return fail("a viewport with no document showed the add-page button");

        viewport.hide();
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
        runTest(testAddPageButton, "testAddPageButton");
        runTest(testCoordinateTransforms, "testCoordinateTransforms");
        runTest(testPageHitDetection, "testPageHitDetection");
        runTest(testVisiblePages, "testVisiblePages");
        runTest(testScrollFractions, "testScrollFractions");
        runTest(testPdfCache, "testPdfCache");
        runTest(testPointerEvents, "testPointerEvents");
        runTest(testOffPagePanFromEmptySpace, "testOffPagePanFromEmptySpace");
        runTest(testObjectAlternateMouseMode, "testObjectAlternateMouseMode");
        runTest(testObjectGestureCancellation, "testObjectGestureCancellation");
        runTest(testObjectPageContainment, "testObjectPageContainment");
        runTest(testIntegerImageInsertScaling, "testIntegerImageInsertScaling");
        runTest(testFastImageInsertionPipeline, "testFastImageInsertionPipeline");
        runTest(testObjectGroupContainment, "testObjectGroupContainment");
        runTest(testTextBoxCreationAndWidthResize,
                "testTextBoxCreationAndWidthResize");
        runTest(testInlineTextBoxEditing,
                "testInlineTextBoxEditing");
        runTest(testTextBoxFormattingBar,
                "testTextBoxFormattingBar");
        runTest(testTextBoxThemeDefaults,
                "testTextBoxThemeDefaults");
        runTest(testTextOverlayLifecycle,
                "testTextOverlayLifecycle");
        runTest(testLinkObjectBar,
                "testLinkObjectBar");
        runTest(testHighlightAnnotationGeometry,
                "testHighlightAnnotationGeometry");
        runTest(testHighlightAdjustMode,
                "testHighlightAdjustMode");
        runTest(testHighlightAppearanceEdit,
                "testHighlightAppearanceEdit");
        runTest(testPositionLinkPairing,
                "testPositionLinkPairing");
        runTest(testAnnotationCopyDelete,
                "testAnnotationCopyDelete");
        runTest(testAnnotationContextMenu,
                "testAnnotationContextMenu");
        runTest(testOcrTextBoxConversion,
                "testOcrTextBoxConversion");
        
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
