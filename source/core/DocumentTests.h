#pragma once

// ============================================================================
// DocumentTests - Unit tests for the Document class
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.8)
// 
// Simple compile-time tests to verify Document functionality:
// - Document creation (factory methods)
// - Page management (add, remove, insert, move)
// - Bookmarks (set, remove, navigate)
// - Serialization round-trip (toFullJson/fromFullJson)
// - PDF reference (if PDF available)
// ============================================================================

#include "Document.h"
#include "Page.h"
#include <QDebug>
#include <QJsonDocument>
#include <QFileInfo>
#include <QImage>
#include <cassert>

namespace DocumentTests {

/**
 * @brief Test Document creation via factory methods.
 * 
 * Tests:
 * - createNew() creates a paged document with 1 page
 * - createNew() with Edgeless mode
 * - Default values are set correctly
 */
inline bool testDocumentCreation()
{
    qDebug() << "=== Test: Document Creation ===";
    bool success = true;
    
    // Test 1: Create new paged document
    {
        auto doc = Document::createNew("Test Notebook", Document::Mode::Paged);
        
        if (!doc) {
            qDebug() << "FAIL: createNew() returned nullptr";
            return false;
        }
        
        if (doc->name != "Test Notebook") {
            qDebug() << "FAIL: name mismatch:" << doc->name;
            success = false;
        }
        
        if (doc->mode != Document::Mode::Paged) {
            qDebug() << "FAIL: mode should be Paged";
            success = false;
        }
        
        if (!doc->isPaged()) {
            qDebug() << "FAIL: isPaged() should return true";
            success = false;
        }
        
        if (doc->pageCount() != 1) {
            qDebug() << "FAIL: should have 1 page, got:" << doc->pageCount();
            success = false;
        }
        
        if (doc->id.isEmpty()) {
            qDebug() << "FAIL: id should be generated";
            success = false;
        }
        
        if (!doc->created.isValid()) {
            qDebug() << "FAIL: created timestamp should be valid";
            success = false;
        }
        
        qDebug() << "  - Paged document creation: OK";
    }
    
    // Test 2: Create edgeless document
    {
        auto doc = Document::createNew("Edgeless Canvas", Document::Mode::Edgeless);
        
        if (doc->mode != Document::Mode::Edgeless) {
            qDebug() << "FAIL: mode should be Edgeless";
            success = false;
        }
        
        if (!doc->isEdgeless()) {
            qDebug() << "FAIL: isEdgeless() should return true";
            success = false;
        }
        
        if (doc->pageCount() != 1) {
            qDebug() << "FAIL: edgeless doc should have 1 page";
            success = false;
        }
        
        Page* ePage = doc->edgelessPage();
        if (!ePage) {
            qDebug() << "FAIL: edgelessPage() should return non-null";
            success = false;
        }
        
        // Edgeless page should have large default size
        if (ePage && ePage->size.width() < 1000) {
            qDebug() << "FAIL: edgeless page should have large size";
            success = false;
        }
        
        qDebug() << "  - Edgeless document creation: OK";
    }
    
    // Test 3: Default values
    {
        auto doc = Document::createNew("Defaults Test");
        
        if (doc->formatVersion != "2.0") {
            qDebug() << "FAIL: formatVersion should be 2.0, got:" << doc->formatVersion;
            success = false;
        }
        
        if (doc->modified != false) {
            qDebug() << "FAIL: new document should not be modified";
            success = false;
        }
        
        if (doc->lastAccessedPage != 0) {
            qDebug() << "FAIL: lastAccessedPage should be 0";
            success = false;
        }
        
        if (doc->displayName() != "Defaults Test") {
            qDebug() << "FAIL: displayName() mismatch";
            success = false;
        }
        
        qDebug() << "  - Default values: OK";
    }
    
    if (success) {
        qDebug() << "PASS: Document creation tests successful!";
    }
    
    return success;
}

/**
 * @brief Test page management operations.
 * 
 * Tests:
 * - addPage() adds pages at end
 * - insertPage() inserts at position
 * - removePage() removes pages (but not last)
 * - movePage() reorders pages
 * - page() access
 */
inline bool testPageManagement()
{
    qDebug() << "=== Test: Page Management ===";
    bool success = true;
    
    auto doc = Document::createNew("Page Test");
    
    // Test 1: Initial state
    if (doc->pageCount() != 1) {
        qDebug() << "FAIL: should start with 1 page";
        success = false;
    }
    
    // Test 2: Add pages
    Page* p2 = doc->addPage();
    Page* p3 = doc->addPage();
    
    if (doc->pageCount() != 3) {
        qDebug() << "FAIL: after addPage() x2, should have 3 pages";
        success = false;
    }
    
    if (!p2 || !p3) {
        qDebug() << "FAIL: addPage() should return non-null";
        success = false;
    }
    
    qDebug() << "  - addPage(): OK";
    
    // Test 3: Insert page at beginning
    Page* pInserted = doc->insertPage(0);
    
    if (doc->pageCount() != 4) {
        qDebug() << "FAIL: after insertPage(0), should have 4 pages";
        success = false;
    }
    
    if (doc->page(0) != pInserted) {
        qDebug() << "FAIL: inserted page should be at index 0";
        success = false;
    }
    
    qDebug() << "  - insertPage(): OK";
    
    // Test 4: page() access
    if (doc->page(-1) != nullptr) {
        qDebug() << "FAIL: page(-1) should return nullptr";
        success = false;
    }
    
    if (doc->page(100) != nullptr) {
        qDebug() << "FAIL: page(100) should return nullptr";
        success = false;
    }
    
    if (doc->page(0) == nullptr) {
        qDebug() << "FAIL: page(0) should return non-null";
        success = false;
    }
    
    qDebug() << "  - page() access: OK";
    
    // Test 5: Mark pages to track them
    doc->page(0)->pageIndex = 100;  // Marker
    doc->page(1)->pageIndex = 101;
    doc->page(2)->pageIndex = 102;
    doc->page(3)->pageIndex = 103;
    
    // Test 6: Move page
    bool moved = doc->movePage(0, 2);
    
    if (!moved) {
        qDebug() << "FAIL: movePage(0, 2) should succeed";
        success = false;
    }
    
    // After move: [101, 102, 100, 103]
    if (doc->page(0)->pageIndex != 101 || doc->page(2)->pageIndex != 100) {
        qDebug() << "FAIL: movePage() order incorrect";
        qDebug() << "  Got:" << doc->page(0)->pageIndex << doc->page(1)->pageIndex 
                 << doc->page(2)->pageIndex << doc->page(3)->pageIndex;
        success = false;
    }
    
    qDebug() << "  - movePage(): OK";
    
    // Test 7: Remove page
    bool removed = doc->removePage(1);
    
    if (!removed || doc->pageCount() != 3) {
        qDebug() << "FAIL: removePage(1) should succeed, count should be 3";
        success = false;
    }
    
    // After remove: [101, 100, 103]
    if (doc->page(1)->pageIndex != 100) {
        qDebug() << "FAIL: after removePage(), order incorrect";
        success = false;
    }
    
    qDebug() << "  - removePage(): OK";
    
    // Test 8: Cannot remove last page
    doc->removePage(0);
    doc->removePage(0);
    // Now only 1 page left
    
    bool removedLast = doc->removePage(0);
    if (removedLast) {
        qDebug() << "FAIL: should not be able to remove last page";
        success = false;
    }
    
    if (doc->pageCount() != 1) {
        qDebug() << "FAIL: should always have at least 1 page";
        success = false;
    }
    
    qDebug() << "  - Cannot remove last page: OK";
    
    // Test 9: Document should be marked modified
    if (!doc->modified) {
        qDebug() << "FAIL: document should be marked modified after page changes";
        success = false;
    }
    
    qDebug() << "  - Modified flag: OK";
    
    if (success) {
        qDebug() << "PASS: Page management tests successful!";
    }
    
    return success;
}

/**
 * @brief Test bookmark operations.
 * 
 * Tests:
 * - setBookmark() / removeBookmark()
 * - hasBookmark() / bookmarkLabel()
 * - nextBookmark() / prevBookmark() with wrap-around
 * - toggleBookmark()
 * - getBookmarks()
 */
inline bool testBookmarks()
{
    qDebug() << "=== Test: Bookmarks ===";
    bool success = true;
    
    auto doc = Document::createNew("Bookmark Test");
    
    // Add some pages
    doc->addPage();
    doc->addPage();
    doc->addPage();
    doc->addPage();
    // Now have 5 pages (0-4)
    
    doc->clearModified();
    
    // Test 1: Set bookmarks
    doc->setBookmark(1, "Chapter 1");
    doc->setBookmark(3, "Chapter 2");
    
    if (!doc->hasBookmark(1)) {
        qDebug() << "FAIL: page 1 should have bookmark";
        success = false;
    }
    
    if (doc->hasBookmark(0)) {
        qDebug() << "FAIL: page 0 should not have bookmark";
        success = false;
    }
    
    if (doc->bookmarkLabel(1) != "Chapter 1") {
        qDebug() << "FAIL: bookmark label mismatch:" << doc->bookmarkLabel(1);
        success = false;
    }
    
    qDebug() << "  - setBookmark(): OK";
    
    // Test 2: Bookmark count
    if (doc->bookmarkCount() != 2) {
        qDebug() << "FAIL: bookmarkCount should be 2, got:" << doc->bookmarkCount();
        success = false;
    }
    
    qDebug() << "  - bookmarkCount(): OK";
    
    // Test 3: Get all bookmarks
    auto bookmarks = doc->getBookmarks();
    if (bookmarks.size() != 2) {
        qDebug() << "FAIL: getBookmarks() should return 2 items";
        success = false;
    }
    
    if (bookmarks[0].pageIndex != 1 || bookmarks[1].pageIndex != 3) {
        qDebug() << "FAIL: bookmarks should be sorted by page";
        success = false;
    }
    
    qDebug() << "  - getBookmarks(): OK";
    
    // Test 4: Next bookmark
    int next = doc->nextBookmark(0);  // From page 0, next should be 1
    if (next != 1) {
        qDebug() << "FAIL: nextBookmark(0) should be 1, got:" << next;
        success = false;
    }
    
    next = doc->nextBookmark(1);  // From page 1, next should be 3
    if (next != 3) {
        qDebug() << "FAIL: nextBookmark(1) should be 3, got:" << next;
        success = false;
    }
    
    next = doc->nextBookmark(3);  // From page 3, wrap around to 1
    if (next != 1) {
        qDebug() << "FAIL: nextBookmark(3) should wrap to 1, got:" << next;
        success = false;
    }
    
    qDebug() << "  - nextBookmark(): OK";
    
    // Test 5: Previous bookmark
    int prev = doc->prevBookmark(4);  // From page 4, prev should be 3
    if (prev != 3) {
        qDebug() << "FAIL: prevBookmark(4) should be 3, got:" << prev;
        success = false;
    }
    
    prev = doc->prevBookmark(3);  // From page 3, prev should be 1
    if (prev != 1) {
        qDebug() << "FAIL: prevBookmark(3) should be 1, got:" << prev;
        success = false;
    }
    
    prev = doc->prevBookmark(1);  // From page 1, wrap around to 3
    if (prev != 3) {
        qDebug() << "FAIL: prevBookmark(1) should wrap to 3, got:" << prev;
        success = false;
    }
    
    qDebug() << "  - prevBookmark(): OK";
    
    // Test 6: Remove bookmark
    doc->removeBookmark(1);
    
    if (doc->hasBookmark(1)) {
        qDebug() << "FAIL: page 1 should no longer have bookmark";
        success = false;
    }
    
    if (doc->bookmarkCount() != 1) {
        qDebug() << "FAIL: bookmarkCount should be 1 after remove";
        success = false;
    }
    
    qDebug() << "  - removeBookmark(): OK";
    
    // Test 7: Toggle bookmark
    bool added = doc->toggleBookmark(2, "Toggled On");
    if (!added || !doc->hasBookmark(2)) {
        qDebug() << "FAIL: toggleBookmark should add bookmark";
        success = false;
    }
    
    bool removed = doc->toggleBookmark(2);
    if (removed || doc->hasBookmark(2)) {
        qDebug() << "FAIL: toggleBookmark should remove bookmark";
        success = false;
    }
    
    qDebug() << "  - toggleBookmark(): OK";
    
    // Test 8: Default bookmark label
    doc->setBookmark(0);  // No label provided
    QString label = doc->bookmarkLabel(0);
    if (!label.contains("1")) {  // Should contain page number (1-based)
        qDebug() << "FAIL: default label should contain page number:" << label;
        success = false;
    }
    
    qDebug() << "  - Default bookmark label: OK";
    
    // Test 9: Modified flag
    if (!doc->modified) {
        qDebug() << "FAIL: document should be modified after bookmark changes";
        success = false;
    }
    
    qDebug() << "  - Modified flag: OK";
    
    if (success) {
        qDebug() << "PASS: Bookmark tests successful!";
    }
    
    return success;
}

/**
 * @brief Test Document serialization round-trip.
 * 
 * Tests:
 * - toFullJson() exports all data
 * - fromFullJson() restores all data
 * - Data integrity after round-trip
 */
inline bool testSerializationRoundTrip()
{
    qDebug() << "=== Test: Document Serialization Round-Trip ===";
    bool success = true;
    
    // 1. Create a document with content
    auto doc = Document::createNew("Serialization Test", Document::Mode::Paged);
    doc->author = "Test Author";
    doc->defaultBackgroundType = Page::BackgroundType::Grid;
    doc->defaultGridSpacing = 25;
    doc->defaultBackgroundColor = QColor(240, 240, 255);
    
    // Add pages
    doc->addPage();
    doc->addPage();
    // Now 3 pages
    
    // Add bookmarks
    doc->setBookmark(0, "Introduction");
    doc->setBookmark(2, "Conclusion");
    
    // Add strokes to pages
    VectorStroke stroke1;
    stroke1.id = "stroke-001";
    stroke1.color = Qt::red;
    stroke1.baseThickness = 3.0;
    stroke1.points.append({QPointF(10, 10), 0.5});
    stroke1.points.append({QPointF(100, 50), 0.8});
    stroke1.updateBoundingBox();
    doc->page(0)->activeLayer()->addStroke(stroke1);
    
    VectorStroke stroke2;
    stroke2.id = "stroke-002";
    stroke2.color = Qt::blue;
    stroke2.baseThickness = 5.0;
    stroke2.points.append({QPointF(50, 100), 1.0});
    stroke2.points.append({QPointF(150, 150), 0.7});
    stroke2.updateBoundingBox();
    doc->page(1)->activeLayer()->addStroke(stroke2);
    
    // Store original ID for comparison
    QString originalId = doc->id;
    
    // 2. Serialize to JSON
    QJsonObject json = doc->toFullJson();
    
    // Debug: Print partial JSON
    QJsonDocument jsonDoc(json);
    qDebug() << "  Serialized JSON (first 500 chars):" 
             << QString(jsonDoc.toJson(QJsonDocument::Indented)).left(500) << "...";
    
    // 3. Deserialize
    auto restored = Document::fromFullJson(json);
    
    if (!restored) {
        qDebug() << "FAIL: fromFullJson() returned nullptr";
        return false;
    }
    
    // 4. Verify data matches
    
    // Identity
    if (restored->id != originalId) {
        qDebug() << "FAIL: id mismatch:" << restored->id << "!=" << originalId;
        success = false;
    }
    
    if (restored->name != "Serialization Test") {
        qDebug() << "FAIL: name mismatch:" << restored->name;
        success = false;
    }
    
    if (restored->author != "Test Author") {
        qDebug() << "FAIL: author mismatch:" << restored->author;
        success = false;
    }
    
    qDebug() << "  - Identity preserved: OK";
    
    // Mode
    if (restored->mode != Document::Mode::Paged) {
        qDebug() << "FAIL: mode should be Paged";
        success = false;
    }
    
    qDebug() << "  - Mode preserved: OK";
    
    // Default background
    if (restored->defaultBackgroundType != Page::BackgroundType::Grid) {
        qDebug() << "FAIL: defaultBackgroundType mismatch";
        success = false;
    }
    
    if (restored->defaultGridSpacing != 25) {
        qDebug() << "FAIL: defaultGridSpacing mismatch:" << restored->defaultGridSpacing;
        success = false;
    }
    
    qDebug() << "  - Default background preserved: OK";
    
    // Pages
    if (restored->pageCount() != 3) {
        qDebug() << "FAIL: pageCount mismatch:" << restored->pageCount() << "!= 3";
        success = false;
    }
    
    qDebug() << "  - Page count preserved: OK";
    
    // Bookmarks
    if (restored->bookmarkCount() != 2) {
        qDebug() << "FAIL: bookmarkCount mismatch:" << restored->bookmarkCount();
        success = false;
    }
    
    if (!restored->hasBookmark(0) || !restored->hasBookmark(2)) {
        qDebug() << "FAIL: bookmarks not restored correctly";
        success = false;
    }
    
    if (restored->bookmarkLabel(0) != "Introduction") {
        qDebug() << "FAIL: bookmark label mismatch:" << restored->bookmarkLabel(0);
        success = false;
    }
    
    qDebug() << "  - Bookmarks preserved: OK";
    
    // Strokes
    if (restored->page(0)->activeLayer()->strokeCount() != 1) {
        qDebug() << "FAIL: page 0 stroke count mismatch";
        success = false;
    }
    
    const auto& restoredStroke = restored->page(0)->activeLayer()->strokes()[0];
    if (restoredStroke.id != "stroke-001") {
        qDebug() << "FAIL: stroke id mismatch:" << restoredStroke.id;
        success = false;
    }
    
    if (restoredStroke.color != Qt::red) {
        qDebug() << "FAIL: stroke color mismatch";
        success = false;
    }
    
    if (restoredStroke.points.size() != 2) {
        qDebug() << "FAIL: stroke points count mismatch:" << restoredStroke.points.size();
        success = false;
    }
    
    qDebug() << "  - Strokes preserved: OK";
    
    // Modified flag should be false after loading
    if (restored->modified) {
        qDebug() << "FAIL: restored document should not be marked modified";
        success = false;
    }
    
    qDebug() << "  - Modified flag correct: OK";
    
    if (success) {
        qDebug() << "PASS: Serialization round-trip successful!";
    }
    
    return success;
}

/**
 * @brief Test PDF reference management.
 * 
 * Tests:
 * - hasPdfReference() / isPdfLoaded()
 * - Path is stored even when load fails
 * - clearPdfReference()
 * 
 * Note: Actual PDF loading is skipped if no test PDF available.
 */
inline bool testPdfReference()
{
    qDebug() << "=== Test: PDF Reference ===";
    bool success = true;
    
    auto doc = Document::createNew("PDF Test");
    
    // Test 1: Initial state - no PDF
    if (doc->hasPdfReference()) {
        qDebug() << "FAIL: new document should not have PDF reference";
        success = false;
    }
    
    if (doc->isPdfLoaded()) {
        qDebug() << "FAIL: new document should not have PDF loaded";
        success = false;
    }
    
    if (doc->pdfPageCount() != 0) {
        qDebug() << "FAIL: pdfPageCount should be 0 without PDF";
        success = false;
    }
    
    qDebug() << "  - Initial state (no PDF): OK";
    
    // Test 2: Load non-existent PDF (path should still be stored)
    bool loaded = doc->loadPdf("/nonexistent/path/to/test.pdf");
    
    if (loaded) {
        qDebug() << "FAIL: loadPdf() should fail for non-existent file";
        success = false;
    }
    
    // Path should be stored for relink
    if (doc->pdfPath() != "/nonexistent/path/to/test.pdf") {
        qDebug() << "FAIL: path should be stored even on load failure";
        success = false;
    }
    
    if (doc->hasPdfReference()) {
        // hasPdfReference checks !path.isEmpty()
        qDebug() << "  - Path stored for relink: OK";
    }
    
    if (doc->isPdfLoaded()) {
        qDebug() << "FAIL: isPdfLoaded should be false after failed load";
        success = false;
    }
    
    qDebug() << "  - Load failure handling: OK";
    
    // Test 3: Clear PDF reference
    doc->clearPdfReference();
    
    if (doc->hasPdfReference()) {
        qDebug() << "FAIL: hasPdfReference should be false after clear";
        success = false;
    }
    
    if (!doc->pdfPath().isEmpty()) {
        qDebug() << "FAIL: pdfPath should be empty after clear";
        success = false;
    }
    
    qDebug() << "  - clearPdfReference(): OK";
    
    // Test 4: Modified flag
    if (!doc->modified) {
        qDebug() << "FAIL: clearPdfReference should mark document modified";
        success = false;
    }
    
    qDebug() << "  - Modified flag: OK";
    
    // Test 5: Check if PdfProvider is available
    if (PdfProvider::isAvailable()) {
        qDebug() << "  - PdfProvider is available (Poppler found)";
    } else {
        qDebug() << "  - PdfProvider not available (skipping actual PDF load tests)";
    }
    
    if (success) {
        qDebug() << "PASS: PDF reference tests successful!";
    }
    
    return success;
}

/**
 * @brief Test metadata-only serialization.
 * 
 * Tests:
 * - toJson() produces metadata without pages
 * - fromJson() loads metadata without pages
 */
inline bool testMetadataOnlySerialization()
{
    qDebug() << "=== Test: Metadata-Only Serialization ===";
    bool success = true;
    
    // Create document with pages
    auto doc = Document::createNew("Metadata Test");
    doc->author = "Test Author";
    doc->addPage();
    doc->addPage();
    doc->setBookmark(1, "Test Bookmark");
    
    // Serialize metadata only
    QJsonObject metadataJson = doc->toJson();
    
    // Should have page_count but not pages array
    if (!metadataJson.contains("page_count")) {
        qDebug() << "FAIL: toJson() should include page_count";
        success = false;
    }
    
    if (metadataJson["page_count"].toInt() != 3) {
        qDebug() << "FAIL: page_count should be 3";
        success = false;
    }
    
    if (metadataJson.contains("pages")) {
        qDebug() << "FAIL: toJson() should NOT include pages array";
        success = false;
    }
    
    qDebug() << "  - toJson() structure: OK";
    
    // Load from metadata (no pages)
    auto restored = Document::fromJson(metadataJson);
    
    if (restored->name != "Metadata Test") {
        qDebug() << "FAIL: name not restored";
        success = false;
    }
    
    if (restored->author != "Test Author") {
        qDebug() << "FAIL: author not restored";
        success = false;
    }
    
    // Should have 0 pages (not loaded from JSON)
    if (restored->pageCount() != 0) {
        qDebug() << "FAIL: fromJson() should not create pages";
        success = false;
    }
    
    qDebug() << "  - fromJson() loads metadata only: OK";
    
    // Now load pages separately
    QJsonArray pagesArray = doc->pagesToJson();
    int loadedPages = restored->loadPagesFromJson(pagesArray);
    
    if (loadedPages != 3) {
        qDebug() << "FAIL: loadPagesFromJson should load 3 pages, got:" << loadedPages;
        success = false;
    }
    
    // Bookmark should be preserved (it's stored in Page)
    if (!restored->hasBookmark(1)) {
        qDebug() << "FAIL: bookmark should be restored with page";
        success = false;
    }
    
    qDebug() << "  - loadPagesFromJson(): OK";
    
    if (success) {
        qDebug() << "PASS: Metadata-only serialization tests successful!";
    }
    
    return success;
}

/**
 * @brief Test actual PDF loading with a real PDF file.
 * 
 * Tests (when 1.pdf exists in current directory):
 * - Document::createForPdf() factory method
 * - PDF is loaded and valid
 * - Pages created for each PDF page
 * - PDF rendering to image
 * - PDF metadata access
 * - PDF serialization round-trip
 * 
 * @return True if all tests pass, or if PDF file not found (skipped).
 */
inline bool testActualPdfLoad()
{
    qDebug() << "=== Test: Actual PDF Load ===";
    
    // Check if test PDF exists
    QString pdfPath = "1.pdf";
    if (!QFileInfo::exists(pdfPath)) {
        qDebug() << "  - SKIPPED: 1.pdf not found in current directory";
        qDebug() << "  - (Place a PDF named '1.pdf' next to the executable to run this test)";
        return true;  // Not a failure, just skipped
    }
    
    // Check if PdfProvider is available
    if (!PdfProvider::isAvailable()) {
        qDebug() << "  - SKIPPED: PdfProvider not available (Poppler not found)";
        return true;
    }
    
    qDebug() << "  - Found 1.pdf, running actual PDF tests...";
    
    bool success = true;
    
    // Test 1: Create document for PDF
    auto doc = Document::createForPdf("PDF Document Test", pdfPath);
    
    if (!doc) {
        qDebug() << "FAIL: createForPdf() returned nullptr";
        return false;
    }
    
    if (!doc->isPdfLoaded()) {
        qDebug() << "FAIL: PDF should be loaded";
        return false;
    }
    
    qDebug() << "  - createForPdf(): OK";
    
    // Test 2: PDF page count
    int pdfPageCount = doc->pdfPageCount();
    qDebug() << "  - PDF has" << pdfPageCount << "page(s)";
    
    if (pdfPageCount <= 0) {
        qDebug() << "FAIL: pdfPageCount should be > 0";
        success = false;
    }
    
    // Document should have created one page per PDF page
    if (doc->pageCount() != pdfPageCount) {
        qDebug() << "FAIL: Document page count" << doc->pageCount() 
                 << "should match PDF page count" << pdfPageCount;
        success = false;
    }
    
    qDebug() << "  - Page count matches PDF: OK";
    
    // Test 3: PDF page size
    QSizeF pageSize = doc->pdfPageSize(0);
    qDebug() << "  - PDF page 0 size:" << pageSize.width() << "x" << pageSize.height();
    
    if (pageSize.isEmpty()) {
        qDebug() << "FAIL: pdfPageSize(0) should not be empty";
        success = false;
    }
    
    qDebug() << "  - pdfPageSize(): OK";
    
    // Test 4: Pages have correct background type
    for (int i = 0; i < doc->pageCount() && i < 3; ++i) {  // Check first 3 pages
        Page* page = doc->page(i);
        if (page->backgroundType != Page::BackgroundType::PDF) {
            qDebug() << "FAIL: Page" << i << "should have PDF background type";
            success = false;
        }
        if (page->pdfPageNumber != i) {
            qDebug() << "FAIL: Page" << i << "pdfPageNumber should be" << i;
            success = false;
        }
    }
    
    qDebug() << "  - Pages have PDF background: OK";
    
    // Test 5: Page sizes are scaled from PDF (72 dpi -> 96 dpi)
    Page* firstPage = doc->page(0);
    QSizeF expectedSize = QSizeF(pageSize.width() * 96.0 / 72.0, 
                                  pageSize.height() * 96.0 / 72.0);
    
    // Allow small floating point tolerance
    if (qAbs(firstPage->size.width() - expectedSize.width()) > 1.0 ||
        qAbs(firstPage->size.height() - expectedSize.height()) > 1.0) {
        qDebug() << "FAIL: Page size" << firstPage->size 
                 << "should be approximately" << expectedSize;
        success = false;
    }
    
    qDebug() << "  - Page size scaled correctly (72->96 dpi): OK";
    
    // Test 6: PDF metadata
    QString pdfTitle = doc->pdfTitle();
    QString pdfAuthor = doc->pdfAuthor();
    qDebug() << "  - PDF Title:" << (pdfTitle.isEmpty() ? "(none)" : pdfTitle);
    qDebug() << "  - PDF Author:" << (pdfAuthor.isEmpty() ? "(none)" : pdfAuthor);
    // No failure for empty metadata - not all PDFs have it
    
    qDebug() << "  - PDF metadata access: OK";
    
    // Test 7: Render PDF page to image
    QImage rendered = doc->renderPdfPageToImage(0, 72.0);
    
    if (rendered.isNull()) {
        qDebug() << "FAIL: renderPdfPageToImage() returned null image";
        success = false;
    } else {
        qDebug() << "  - Rendered image size:" << rendered.width() << "x" << rendered.height();
    }
    
    qDebug() << "  - renderPdfPageToImage(): OK";
    
    // Test 8: Render at higher DPI
    QImage renderedHiDpi = doc->renderPdfPageToImage(0, 144.0);
    
    if (!renderedHiDpi.isNull()) {
        // Higher DPI should produce larger image
        if (renderedHiDpi.width() <= rendered.width()) {
            qDebug() << "WARN: Higher DPI should produce larger image";
        }
        qDebug() << "  - Rendered at 144 DPI:" << renderedHiDpi.width() << "x" << renderedHiDpi.height();
    }
    
    qDebug() << "  - High-DPI rendering: OK";
    
    // Test 9: Serialization with PDF reference
    QJsonObject json = doc->toFullJson();
    
    // Check PDF path is saved
    if (json["pdf_path"].toString() != pdfPath) {
        qDebug() << "FAIL: pdf_path not saved correctly in JSON";
        success = false;
    }
    
    qDebug() << "  - PDF path serialized: OK";
    
    // Test 10: Restore from JSON (PDF should reload)
    auto restored = Document::fromFullJson(json);
    
    // PDF won't be loaded yet (fromFullJson doesn't auto-load PDF)
    // User must call loadPdf() after restoring
    if (!restored->hasPdfReference()) {
        qDebug() << "FAIL: restored document should have PDF reference";
        success = false;
    }
    
    // Load the PDF
    bool reloaded = restored->loadPdf(restored->pdfPath());
    if (!reloaded) {
        qDebug() << "FAIL: could not reload PDF after restore";
        success = false;
    }
    
    if (restored->pdfPageCount() != pdfPageCount) {
        qDebug() << "FAIL: restored PDF page count mismatch";
        success = false;
    }
    
    qDebug() << "  - PDF reload after deserialize: OK";
    
    // Test 11: findPageByPdfPage
    int foundPage = doc->findPageByPdfPage(0);
    if (foundPage != 0) {
        qDebug() << "FAIL: findPageByPdfPage(0) should return 0, got:" << foundPage;
        success = false;
    }
    
    int notFoundPage = doc->findPageByPdfPage(9999);
    if (notFoundPage != -1) {
        qDebug() << "FAIL: findPageByPdfPage(9999) should return -1";
        success = false;
    }
    
    qDebug() << "  - findPageByPdfPage(): OK";
    
    // Test 12: Insert page in PDF document (pages shouldn't break)
    int originalCount = doc->pageCount();
    Page* inserted = doc->insertPage(1);
    
    if (!inserted) {
        qDebug() << "FAIL: insertPage() in PDF document should work";
        success = false;
    }
    
    if (doc->pageCount() != originalCount + 1) {
        qDebug() << "FAIL: page count should increase after insert";
        success = false;
    }
    
    // The inserted page should NOT have PDF background
    if (inserted->backgroundType == Page::BackgroundType::PDF) {
        qDebug() << "FAIL: inserted page should not have PDF background";
        success = false;
    }
    
    // PDF pages should still be in correct order (shifted)
    if (doc->page(0)->pdfPageNumber != 0) {
        qDebug() << "FAIL: page 0 should still reference PDF page 0";
        success = false;
    }
    if (doc->page(2)->pdfPageNumber != 1) {
        qDebug() << "FAIL: page 2 should reference PDF page 1 (shifted)";
        success = false;
    }
    
    qDebug() << "  - Insert page in PDF document: OK";
    
    // Test 13: Unload PDF (should preserve reference)
    doc->unloadPdf();
    
    if (doc->isPdfLoaded()) {
        qDebug() << "FAIL: isPdfLoaded should be false after unload";
        success = false;
    }
    
    if (!doc->hasPdfReference()) {
        qDebug() << "FAIL: hasPdfReference should still be true after unload";
        success = false;
    }
    
    // Rendering should return null when unloaded
    QImage nullImage = doc->renderPdfPageToImage(0, 72.0);
    if (!nullImage.isNull()) {
        qDebug() << "FAIL: rendering unloaded PDF should return null";
        success = false;
    }
    
    qDebug() << "  - unloadPdf(): OK";
    
    // Test 14: Reload after unload
    bool reloadSuccess = doc->loadPdf(doc->pdfPath());
    if (!reloadSuccess || !doc->isPdfLoaded()) {
        qDebug() << "FAIL: should be able to reload PDF after unload";
        success = false;
    }
    
    qDebug() << "  - Reload after unload: OK";
    
    if (success) {
        qDebug() << "PASS: Actual PDF load tests successful!";
    }
    
    return success;
}

/**
 * @brief Test movePage() operations in detail.
 * 
 * Tests:
 * - Move from middle positions
 * - Move to same position (no-op)
 * - Move first to last
 * - Move last to first
 * - Invalid indices (negative, out of bounds)
 * - Page identity preserved (UUIDs follow pages)
 * - Strokes follow their pages
 * - Modified flag set
 * 
 * Note: This is a more thorough test than the basic movePage() test
 * in testPageManagement(). Created for Page Panel drag-and-drop support.
 */
inline bool testMovePage()
{
    qDebug() << "=== Test: Document::movePage() Detailed ===";
    bool success = true;
    
    // Create document with 5 pages
    auto doc = Document::createNew("MovePage Test");
    for (int i = 0; i < 4; ++i) {
        doc->addPage();
    }
    // Now have 5 pages (indices 0-4)
    
    if (doc->pageCount() != 5) {
        qDebug() << "FAIL: Setup - should have 5 pages";
        return false;
    }
    
    // Store UUIDs to track page identity
    QStringList originalUuids;
    for (int i = 0; i < 5; ++i) {
        originalUuids.append(doc->page(i)->uuid);
    }
    
    // Add a stroke to page 1 to verify strokes follow pages
    VectorStroke testStroke;
    testStroke.id = "move-test-stroke";
    testStroke.color = Qt::green;
    testStroke.baseThickness = 2.0;
    testStroke.points.append({QPointF(10, 10), 0.5});
    testStroke.points.append({QPointF(50, 50), 0.8});
    testStroke.updateBoundingBox();
    doc->page(1)->activeLayer()->addStroke(testStroke);
    
    doc->clearModified();
    
    // =========================================================================
    // Test 1: Move to same position (no-op)
    // =========================================================================
    bool result = doc->movePage(2, 2);
    
    if (!result) {
        qDebug() << "FAIL: movePage(2, 2) should return true (no-op)";
        success = false;
    }
    
    // Order should be unchanged: [0, 1, 2, 3, 4]
    for (int i = 0; i < 5; ++i) {
        if (doc->page(i)->uuid != originalUuids[i]) {
            qDebug() << "FAIL: Same position move changed order";
            success = false;
            break;
        }
    }
    
    qDebug() << "  - Move to same position (no-op): OK";
    
    // =========================================================================
    // Test 2: Move page 0 to position 2
    // =========================================================================
    // Before: [0, 1, 2, 3, 4]
    // After:  [1, 2, 0, 3, 4]
    
    result = doc->movePage(0, 2);
    
    if (!result) {
        qDebug() << "FAIL: movePage(0, 2) should succeed";
        success = false;
    }
    
    // Verify new order
    if (doc->page(0)->uuid != originalUuids[1] ||
        doc->page(1)->uuid != originalUuids[2] ||
        doc->page(2)->uuid != originalUuids[0] ||
        doc->page(3)->uuid != originalUuids[3] ||
        doc->page(4)->uuid != originalUuids[4]) {
        qDebug() << "FAIL: movePage(0, 2) - order incorrect";
        qDebug() << "  Expected: [1, 2, 0, 3, 4]";
        qDebug() << "  Got UUIDs:";
        for (int i = 0; i < 5; ++i) {
            int originalIdx = originalUuids.indexOf(doc->page(i)->uuid);
            qDebug() << "    Page" << i << "= original" << originalIdx;
        }
        success = false;
    }
    
    // The stroke should still be on what was originally page 1 (now at index 0)
    if (doc->page(0)->activeLayer()->strokeCount() != 1 ||
        doc->page(0)->activeLayer()->strokes()[0].id != "move-test-stroke") {
        qDebug() << "FAIL: Stroke did not follow page during move";
        success = false;
    }
    
    qDebug() << "  - Move page 0 to position 2: OK";
    
    // =========================================================================
    // Test 3: Move last page to first position
    // =========================================================================
    // Current: [1, 2, 0, 3, 4]
    // After:   [4, 1, 2, 0, 3]
    
    result = doc->movePage(4, 0);
    
    if (!result) {
        qDebug() << "FAIL: movePage(4, 0) should succeed";
        success = false;
    }
    
    if (doc->page(0)->uuid != originalUuids[4]) {
        qDebug() << "FAIL: movePage(4, 0) - page 4 should be at index 0";
        success = false;
    }
    
    qDebug() << "  - Move last to first: OK";
    
    // =========================================================================
    // Test 4: Move first page to last position
    // =========================================================================
    // Current: [4, 1, 2, 0, 3]
    // After:   [1, 2, 0, 3, 4]
    
    result = doc->movePage(0, 4);
    
    if (!result) {
        qDebug() << "FAIL: movePage(0, 4) should succeed";
        success = false;
    }
    
    if (doc->page(4)->uuid != originalUuids[4]) {
        qDebug() << "FAIL: movePage(0, 4) - original page 4 should be at index 4";
        success = false;
    }
    
    qDebug() << "  - Move first to last: OK";
    
    // =========================================================================
    // Test 5: Invalid indices - negative
    // =========================================================================
    result = doc->movePage(-1, 2);
    if (result) {
        qDebug() << "FAIL: movePage(-1, 2) should return false";
        success = false;
    }
    
    result = doc->movePage(2, -1);
    if (result) {
        qDebug() << "FAIL: movePage(2, -1) should return false";
        success = false;
    }
    
    qDebug() << "  - Negative indices rejected: OK";
    
    // =========================================================================
    // Test 6: Invalid indices - out of bounds
    // =========================================================================
    result = doc->movePage(10, 2);
    if (result) {
        qDebug() << "FAIL: movePage(10, 2) should return false";
        success = false;
    }
    
    result = doc->movePage(2, 10);
    if (result) {
        qDebug() << "FAIL: movePage(2, 10) should return false";
        success = false;
    }
    
    result = doc->movePage(5, 2);  // Exactly at boundary (5 pages, valid indices are 0-4)
    if (result) {
        qDebug() << "FAIL: movePage(5, 2) should return false (index 5 is out of bounds)";
        success = false;
    }
    
    qDebug() << "  - Out of bounds indices rejected: OK";
    
    // =========================================================================
    // Test 7: Modified flag is set
    // =========================================================================
    doc->clearModified();
    doc->movePage(0, 1);
    
    if (!doc->modified) {
        qDebug() << "FAIL: movePage should mark document as modified";
        success = false;
    }
    
    qDebug() << "  - Modified flag set: OK";
    
    // =========================================================================
    // Test 8: UUID cache is invalidated (pageIndexByUuid should still work)
    // =========================================================================
    // After all moves, verify pageIndexByUuid returns correct values
    for (int i = 0; i < 5; ++i) {
        QString pageUuid = doc->page(i)->uuid;
        int foundIndex = doc->pageIndexByUuid(pageUuid);
        if (foundIndex != i) {
            qDebug() << "FAIL: pageIndexByUuid() returned" << foundIndex 
                     << "but page is at index" << i;
            success = false;
            break;
        }
    }
    
    qDebug() << "  - UUID cache correctly invalidated: OK";
    
    // =========================================================================
    // Test 9: Multiple consecutive moves
    // =========================================================================
    // Reset to known state
    auto doc2 = Document::createNew("MovePage Test 2");
    for (int i = 0; i < 4; ++i) {
        doc2->addPage();
    }
    
    QStringList uuids2;
    for (int i = 0; i < 5; ++i) {
        uuids2.append(doc2->page(i)->uuid);
    }
    
    // Perform multiple moves: simulate drag-and-drop reordering
    // [0,1,2,3,4] -> move 4 to 0 -> [4,0,1,2,3]
    // [4,0,1,2,3] -> move 2 to 4 -> [4,0,2,3,1]
    // [4,0,2,3,1] -> move 0 to 2 -> [0,2,4,3,1]
    
    doc2->movePage(4, 0);  // [4,0,1,2,3]
    doc2->movePage(2, 4);  // [4,0,2,3,1]
    doc2->movePage(0, 2);  // [0,2,4,3,1]
    
    // Verify final state
    // Expected order of original indices: [0, 2, 4, 3, 1]
    int expectedOrder[] = {0, 2, 4, 3, 1};
    bool orderCorrect = true;
    for (int i = 0; i < 5; ++i) {
        if (doc2->page(i)->uuid != uuids2[expectedOrder[i]]) {
            orderCorrect = false;
            break;
        }
    }
    
    if (!orderCorrect) {
        qDebug() << "FAIL: Multiple consecutive moves - order incorrect";
        qDebug() << "  Expected original indices: [0, 2, 4, 3, 1]";
        qDebug() << "  Got:";
        for (int i = 0; i < 5; ++i) {
            int originalIdx = uuids2.indexOf(doc2->page(i)->uuid);
            qDebug() << "    Position" << i << "= original" << originalIdx;
        }
        success = false;
    }
    
    qDebug() << "  - Multiple consecutive moves: OK";
    
    if (success) {
        qDebug() << "PASS: movePage() detailed tests successful!";
    }
    
    return success;
}

/**
 * @brief Run all Document tests.
 * @return True if all tests pass.
 */
inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running Document Unit Tests";
    qDebug() << "========================================\n";
    
    bool allPass = true;
    
    allPass &= testDocumentCreation();
    qDebug() << "";
    
    allPass &= testPageManagement();
    qDebug() << "";
    
    allPass &= testMovePage();  // NEW: Detailed movePage tests
    qDebug() << "";
    
    allPass &= testBookmarks();
    qDebug() << "";
    
    allPass &= testSerializationRoundTrip();
    qDebug() << "";
    
    allPass &= testPdfReference();
    qDebug() << "";
    
    allPass &= testMetadataOnlySerialization();
    qDebug() << "";
    
    allPass &= testActualPdfLoad();
    qDebug() << "";
    
    qDebug() << "\n========================================";
    if (allPass) {
        qDebug() << "ALL DOCUMENT TESTS PASSED!";
    } else {
        qDebug() << "SOME DOCUMENT TESTS FAILED!";
    }
    qDebug() << "========================================\n";
    
    return allPass;
}

} // namespace DocumentTests
