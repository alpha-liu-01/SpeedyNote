#pragma once

// ============================================================================
// MuPdfExporterTests - Unit tests for the MuPdfExporter class
// ============================================================================
// Tests for PDF export functionality, focusing on utility functions
// that can be tested without MuPDF or file dependencies.
//
// Current tests:
// - parsePageRange() edge cases
// - highlightRectsToPdfSpace() coordinate conversion
// - highlight annotations survive a real export round-trip
// ============================================================================

#include "MuPdfExporter.h"
#include <QDebug>

#ifdef SPEEDYNOTE_MUPDF_EXPORT
#include "../core/Document.h"
#include "../core/Page.h"
#include "../objects/LinkObject.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QDir>
#include <QFile>
#include <QRectF>
#include <QVector>

#include <cmath>
#include <memory>
#endif

namespace MuPdfExporterTests {

/**
 * @brief Test parsePageRange() with various inputs.
 * 
 * Tests:
 * - Empty string → all pages
 * - "all" → all pages
 * - Single page number
 * - Range "1-5"
 * - Multiple ranges "1-3, 5, 7-9"
 * - Reversed range "10-5"
 * - Out of bounds values
 * - Invalid input handling
 * - Duplicate removal
 */
inline bool testParsePageRange()
{
    qDebug() << "=== Test: parsePageRange() ===";
    bool success = true;
    
    // Test 1: Empty string means all pages
    {
        QVector<int> result = MuPdfExporter::parsePageRange("", 5);
        QVector<int> expected = {0, 1, 2, 3, 4};
        
        if (result != expected) {
            qDebug() << "FAIL: Empty string should return all pages";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Empty string → all pages: OK";
        }
    }
    
    // Test 2: "all" means all pages
    {
        QVector<int> result = MuPdfExporter::parsePageRange("all", 3);
        QVector<int> expected = {0, 1, 2};
        
        if (result != expected) {
            qDebug() << "FAIL: 'all' should return all pages";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - 'all' → all pages: OK";
        }
    }
    
    // Test 3: "ALL" (case insensitive)
    {
        QVector<int> result = MuPdfExporter::parsePageRange("ALL", 3);
        QVector<int> expected = {0, 1, 2};
        
        if (result != expected) {
            qDebug() << "FAIL: 'ALL' should be case-insensitive";
            success = false;
        } else {
            qDebug() << "  - Case insensitivity: OK";
        }
    }
    
    // Test 4: Single page number
    {
        QVector<int> result = MuPdfExporter::parsePageRange("5", 10);
        QVector<int> expected = {4};  // 1-based input → 0-based output
        
        if (result != expected) {
            qDebug() << "FAIL: Single page '5' should return [4]";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Single page '5': OK";
        }
    }
    
    // Test 5: Simple range "1-5"
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1-5", 10);
        QVector<int> expected = {0, 1, 2, 3, 4};
        
        if (result != expected) {
            qDebug() << "FAIL: Range '1-5' should return [0,1,2,3,4]";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Range '1-5': OK";
        }
    }
    
    // Test 6: Multiple ranges with single page "1-3, 5, 7-9"
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1-3, 5, 7-9", 10);
        QVector<int> expected = {0, 1, 2, 4, 6, 7, 8};
        
        if (result != expected) {
            qDebug() << "FAIL: '1-3, 5, 7-9' incorrect";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Multiple ranges '1-3, 5, 7-9': OK";
        }
    }
    
    // Test 7: Reversed range "5-1" should be handled (sorted)
    {
        QVector<int> result = MuPdfExporter::parsePageRange("5-1", 10);
        QVector<int> expected = {0, 1, 2, 3, 4};
        
        if (result != expected) {
            qDebug() << "FAIL: Reversed range '5-1' should work";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Reversed range '5-1': OK";
        }
    }
    
    // Test 8: Out of bounds - page 0 (invalid, should return error)
    {
        QVector<int> result = MuPdfExporter::parsePageRange("0", 5);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: '0' should return empty (invalid page)";
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Out of bounds '0' → error: OK";
        }
    }
    
    // Test 9: Out of bounds - page 100 in 5-page doc (should return error)
    {
        QVector<int> result = MuPdfExporter::parsePageRange("100", 5);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: '100' in 5-page doc should return empty (out of bounds)";
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Out of bounds '100' → error: OK";
        }
    }
    
    // Test 9b: Out of bounds range - pages 1000-1002 in 2-page doc
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1000-1002", 2);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: '1000-1002' in 2-page doc should return empty";
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Out of bounds range '1000-1002' → error: OK";
        }
    }
    
    // Test 10: Duplicates are removed
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1, 1, 2, 2, 3", 5);
        QVector<int> expected = {0, 1, 2};
        
        if (result != expected) {
            qDebug() << "FAIL: Duplicates should be removed";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Duplicate removal: OK";
        }
    }
    
    // Test 11: Overlapping ranges "1-5, 3-7" - duplicates removed
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1-5, 3-7", 10);
        QVector<int> expected = {0, 1, 2, 3, 4, 5, 6};
        
        if (result != expected) {
            qDebug() << "FAIL: Overlapping ranges should merge";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Overlapping ranges: OK";
        }
    }
    
    // Test 12: Whitespace handling "  1 - 3 , 5  "
    {
        QVector<int> result = MuPdfExporter::parsePageRange("  1 - 3 , 5  ", 10);
        QVector<int> expected = {0, 1, 2, 4};
        
        if (result != expected) {
            qDebug() << "FAIL: Whitespace should be handled";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Whitespace handling: OK";
        }
    }
    
    // Test 13: Invalid input causes error "1, abc, 3"
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1, abc, 3", 5);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: Invalid input 'abc' should return empty (error)";
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Invalid input causes error: OK";
        }
    }
    
    // Test 14: Zero total pages returns empty
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1-5", 0);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: Zero total pages should return empty";
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Zero total pages: OK";
        }
    }
    
    // Test 15: Negative total pages returns empty
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1", -5);
        
        if (!result.isEmpty()) {
            qDebug() << "FAIL: Negative total pages should return empty";
            success = false;
        } else {
            qDebug() << "  - Negative total pages: OK";
        }
    }
    
    // Test 16: Result is sorted
    {
        QVector<int> result = MuPdfExporter::parsePageRange("5, 1, 3", 10);
        QVector<int> expected = {0, 2, 4};  // Sorted order
        
        if (result != expected) {
            qDebug() << "FAIL: Result should be sorted";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Result sorted: OK";
        }
    }
    
    // Test 17: Partial overlap - range extends beyond document is clamped
    // "1-100" on a 10-page doc should export pages 1-10
    {
        QVector<int> result = MuPdfExporter::parsePageRange("1-100", 10);
        QVector<int> expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        
        if (result != expected) {
            qDebug() << "FAIL: '1-100' on 10-page doc should clamp to 1-10";
            qDebug() << "  Expected:" << expected;
            qDebug() << "  Got:" << result;
            success = false;
        } else {
            qDebug() << "  - Partial overlap (clamped): OK";
        }
    }
    
    if (success) {
        qDebug() << "=== parsePageRange(): ALL TESTS PASSED ===";
    } else {
        qDebug() << "=== parsePageRange(): SOME TESTS FAILED ===";
    }
    
    return success;
}

#ifdef SPEEDYNOTE_MUPDF_EXPORT

/**
 * @brief Test highlightRectsToPdfSpace().
 *
 * Covers the 96-to-72 DPI scale, the Y-axis flip onto a bottom-left origin,
 * and the 1:1 mapping callers rely on to walk input and output in lockstep.
 */
inline bool testHighlightRectsToPdfSpace()
{
    qDebug() << "=== Test: highlightRectsToPdfSpace() ===";
    bool success = true;
    
    auto nearly = [](qreal a, qreal b) { return std::fabs(a - b) < 0.001; };
    
    // Test 1: a mid-page rect, scale and flip together
    {
        // Page-space (100, 200) 300x20 on a 1000-unit-tall page. Scale is 0.75,
        // and the PDF-space lower edge comes from the page-space bottom (220).
        const QVector<QRectF> in{QRectF(100, 200, 300, 20)};
        const QVector<QRectF> out = MuPdfExporter::highlightRectsToPdfSpace(in, 1000.0);
        
        if (out.size() != 1) {
            qDebug() << "FAIL: expected 1 rect, got" << out.size();
            success = false;
        } else if (!nearly(out[0].x(), 75.0) || !nearly(out[0].y(), 585.0) ||
                   !nearly(out[0].width(), 225.0) || !nearly(out[0].height(), 15.0)) {
            qDebug() << "FAIL: expected (75, 585) 225x15, got" << out[0];
            success = false;
        } else {
            qDebug() << "  - Scale and Y flip: OK";
        }
    }
    
    // Test 2: a rect flush with the top of the page stays flush with the top
    {
        const QVector<QRectF> in{QRectF(0, 0, 96, 96)};
        const QVector<QRectF> out = MuPdfExporter::highlightRectsToPdfSpace(in, 1000.0);
        
        const qreal expectedTop = 1000.0 * 0.75;  // page top in PDF space
        if (out.size() != 1 || !nearly(out[0].y() + out[0].height(), expectedTop)) {
            qDebug() << "FAIL: top-flush rect should reach the PDF page top"
                     << expectedTop << ", got" << out;
            success = false;
        } else {
            qDebug() << "  - Top-flush rect: OK";
        }
    }
    
    // Test 3: mapping is 1:1, degenerate rects included (callers filter)
    {
        const QVector<QRectF> in{QRectF(0, 0, 10, 10), QRectF(5, 5, 0, 0),
                                 QRectF(20, 20, 10, 10)};
        const QVector<QRectF> out = MuPdfExporter::highlightRectsToPdfSpace(in, 500.0);
        
        if (out.size() != in.size()) {
            qDebug() << "FAIL: mapping should be 1:1, got" << out.size()
                     << "for" << in.size() << "inputs";
            success = false;
        } else {
            qDebug() << "  - 1:1 mapping: OK";
        }
    }
    
    // Test 4: empty input
    {
        if (!MuPdfExporter::highlightRectsToPdfSpace({}, 100.0).isEmpty()) {
            qDebug() << "FAIL: empty input should give empty output";
            success = false;
        } else {
            qDebug() << "  - Empty input: OK";
        }
    }
    
    if (success) {
        qDebug() << "=== highlightRectsToPdfSpace(): ALL TESTS PASSED ===";
    } else {
        qDebug() << "=== highlightRectsToPdfSpace(): SOME TESTS FAILED ===";
    }
    
    return success;
}

/**
 * @brief Export a page of highlights and read the annotations back out.
 *
 * A hand-written annotation dictionary that is subtly malformed produces a
 * file that still opens, so nothing short of reopening the output and walking
 * /Annots will catch it.
 */
inline bool testHighlightAnnotationExport()
{
    qDebug() << "=== Test: highlight annotation export ===";
    bool success = true;
    
    auto doc = Document::createNew(QStringLiteral("Highlight Export Test"));
    Page* page = doc->page(0);
    if (!page) {
        qDebug() << "FAIL: new document has no page 0";
        return false;
    }
    
    // A Cover highlight spanning two lines, with a description that should
    // travel as /Contents.
    {
        auto link = std::make_unique<LinkObject>();
        link->setRegionFromPageRects({QRectF(100, 200, 300, 20),
                                      QRectF(100, 224, 180, 20)});
        link->region.style = HighlightRegion::Style::Cover;
        link->region.color = QColor(255, 255, 0, HighlightRegion::DEFAULT_OPACITY);
        link->description = QStringLiteral("marked passage");
        page->addObject(std::move(link));
    }
    
    // A dotted underline, which has no standard PDF type and so leans entirely
    // on the appearance stream.
    {
        auto link = std::make_unique<LinkObject>();
        link->setRegionFromPageRects({QRectF(100, 400, 250, 18)});
        link->region.style = HighlightRegion::Style::DottedUnderline;
        link->region.color = QColor(255, 0, 0, HighlightRegion::DEFAULT_OPACITY);
        page->addObject(std::move(link));
    }
    
    // Neither of these is a highlight, so neither may produce an annotation:
    // a standalone link icon, and the legacy select-only style.
    {
        auto link = std::make_unique<LinkObject>();
        link->position = QPointF(50, 50);
        page->addObject(std::move(link));
    }
    {
        auto link = std::make_unique<LinkObject>();
        link->setRegionFromPageRects({QRectF(100, 600, 200, 20)});
        link->region.style = HighlightRegion::Style::None;
        link->region.color = QColor(0, 255, 0, HighlightRegion::DEFAULT_OPACITY);
        page->addObject(std::move(link));
    }
    
    const QString outPath =
        QDir::temp().filePath(QStringLiteral("speedynote_highlight_export_test.pdf"));
    QFile::remove(outPath);
    
    MuPdfExporter exporter;
    exporter.setDocument(doc.get());
    
    PdfExportOptions options;
    options.outputPath = outPath;
    
    const PdfExportResult result = exporter.exportPdf(options);
    if (!result.success) {
        qDebug() << "FAIL: export failed:" << result.errorMessage;
        return false;
    }
    
    // Reopen and inspect
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        qDebug() << "FAIL: could not create a MuPDF context";
        return false;
    }
    
    pdf_document* pdf = nullptr;
    pdf_page* pdfPage = nullptr;
    
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        pdf = pdf_open_document(ctx, outPath.toUtf8().constData());
        pdfPage = pdf_load_page(ctx, pdf, 0);
        
        int count = 0;
        int coverQuads = 0;
        bool sawHighlight = false;
        bool sawUnderline = false;
        bool contentsMatched = false;
        bool allHaveAppearance = true;
        bool opacityMatched = true;
        
        for (pdf_annot* annot = pdf_first_annot(ctx, pdfPage); annot;
             annot = pdf_next_annot(ctx, annot)) {
            count++;
            
            const enum pdf_annot_type type = pdf_annot_type(ctx, annot);
            if (type == PDF_ANNOT_HIGHLIGHT) {
                sawHighlight = true;
                coverQuads = pdf_annot_quad_point_count(ctx, annot);
                const char* contents = pdf_annot_contents(ctx, annot);
                contentsMatched = contents &&
                                  QString::fromUtf8(contents) == QStringLiteral("marked passage");
            } else if (type == PDF_ANNOT_UNDERLINE) {
                sawUnderline = true;
            }
            
            // 128/255 is what DEFAULT_OPACITY round-trips to on /CA.
            if (std::fabs(pdf_annot_opacity(ctx, annot) - (128.0f / 255.0f)) > 0.01f) {
                opacityMatched = false;
            }
            
            if (!pdf_dict_get(ctx, pdf_annot_obj(ctx, annot), PDF_NAME(AP))) {
                allHaveAppearance = false;
            }
        }
        
        if (count != 2) {
            qDebug() << "FAIL: expected 2 annotations (empty region and None style"
                     << "must be skipped), got" << count;
            success = false;
        } else {
            qDebug() << "  - Only real highlights exported: OK";
        }
        
        if (!sawHighlight || !sawUnderline) {
            qDebug() << "FAIL: expected one Highlight and one Underline subtype;"
                     << "highlight:" << sawHighlight << "underline:" << sawUnderline;
            success = false;
        } else {
            qDebug() << "  - Cover maps to Highlight, dotted maps to Underline: OK";
        }
        
        if (coverQuads != 2) {
            qDebug() << "FAIL: two-line Cover should carry 2 quads, got" << coverQuads;
            success = false;
        } else {
            qDebug() << "  - One quad per line: OK";
        }
        
        if (!contentsMatched) {
            qDebug() << "FAIL: description should travel as /Contents";
            success = false;
        } else {
            qDebug() << "  - Description as /Contents: OK";
        }
        
        if (!opacityMatched) {
            qDebug() << "FAIL: /CA should carry the region alpha";
            success = false;
        } else {
            qDebug() << "  - Alpha on /CA: OK";
        }
        
        if (!allHaveAppearance) {
            qDebug() << "FAIL: every highlight needs its own /AP, otherwise the"
                     << "viewer draws its own idea of an underline";
            success = false;
        } else {
            qDebug() << "  - Appearance stream present: OK";
        }
        
        // Rasterize and look at the result. A structurally valid annotation can
        // still draw nothing (a malformed appearance stream) or draw in the
        // mirrored position (a flip error), and neither shows up above.
        //
        // fz_new_pixmap_from_page composites annotations, and the pixmap origin
        // is the page top-left just like SpeedyNote's page space, so a
        // page-space coordinate maps to a pixel by scale alone.
        const float renderScale = 4.0f;
        auto toPixel = [renderScale](qreal sn) {
            return static_cast<int>(sn * 0.75 * renderScale);
        };
        
        fz_pixmap* pix = fz_new_pixmap_from_page(
            ctx, reinterpret_cast<fz_page*>(pdfPage),
            fz_scale(renderScale, renderScale), fz_device_rgb(ctx), 0);
        
        auto isWhite = [&](int px, int py) {
            if (px < 0 || py < 0 || px >= pix->w || py >= pix->h) return true;
            const unsigned char* p =
                pix->samples + static_cast<qsizetype>(py) * pix->stride
                             + static_cast<qsizetype>(px) * pix->n;
            return p[0] > 250 && p[1] > 250 && p[2] > 250;
        };
        auto isYellowish = [&](int px, int py) {
            if (px < 0 || py < 0 || px >= pix->w || py >= pix->h) return false;
            const unsigned char* p =
                pix->samples + static_cast<qsizetype>(py) * pix->stride
                             + static_cast<qsizetype>(px) * pix->n;
            return p[0] > 200 && p[1] > 200 && p[2] < 200;
        };
        
        fz_try(ctx) {
            // Cover, both lines. Yellow at 50% over white is (255, 255, 127).
            const bool line1 = isYellowish(toPixel(250), toPixel(210));
            const bool line2 = isYellowish(toPixel(190), toPixel(234));
            if (!line1 || !line2) {
                qDebug() << "FAIL: Cover should paint both lines; line 1:" << line1
                         << "line 2:" << line2;
                success = false;
            } else {
                qDebug() << "  - Cover paints where the text is: OK";
            }
            
            // Just above the mark. Catches a Y flip landing it elsewhere.
            if (!isWhite(toPixel(250), toPixel(180))) {
                qDebug() << "FAIL: page above the highlight should be untouched";
                success = false;
            } else {
                qDebug() << "  - Nothing painted outside the region: OK";
            }
            
            // DottedUnderline sits in the bottom tenth of its line rect
            // (100, 400) 250x18, so the baseline band has dots and the middle
            // of the line stays clear.
            int dotPixels = 0;
            for (qreal x = 100; x < 350; x += 0.5) {
                if (!isWhite(toPixel(x), toPixel(417))) dotPixels++;
            }
            int midLinePixels = 0;
            for (qreal x = 100; x < 350; x += 0.5) {
                if (!isWhite(toPixel(x), toPixel(408))) midLinePixels++;
            }
            
            if (dotPixels == 0) {
                qDebug() << "FAIL: DottedUnderline drew nothing on its baseline";
                success = false;
            } else if (midLinePixels != 0) {
                qDebug() << "FAIL: DottedUnderline should not cover the line,"
                         << "but" << midLinePixels << "mid-line pixels are painted";
                success = false;
            } else {
                qDebug() << "  - Dotted underline on the baseline only: OK";
            }
        }
        fz_always(ctx) {
            fz_drop_pixmap(ctx, pix);
        }
        fz_catch(ctx) {
            fz_rethrow(ctx);
        }
    }
    fz_always(ctx) {
        if (pdfPage) fz_drop_page(ctx, reinterpret_cast<fz_page*>(pdfPage));
        if (pdf) pdf_drop_document(ctx, pdf);
        fz_drop_context(ctx);
    }
    fz_catch(ctx) {
        qDebug() << "FAIL: could not reopen the exported PDF:" << fz_caught_message(ctx);
        success = false;
    }
    
    QFile::remove(outPath);
    
    if (success) {
        qDebug() << "=== highlight annotation export: ALL TESTS PASSED ===";
    } else {
        qDebug() << "=== highlight annotation export: SOME TESTS FAILED ===";
    }
    
    return success;
}

#endif // SPEEDYNOTE_MUPDF_EXPORT

/**
 * @brief Run all MuPdfExporter tests.
 * @return true if all tests pass, false otherwise.
 */
inline bool runAllTests()
{
    qDebug() << "";
    qDebug() << "========================================";
    qDebug() << "   MuPdfExporter Tests";
    qDebug() << "========================================";
    
    bool allPassed = true;
    
    allPassed &= testParsePageRange();
    #ifdef SPEEDYNOTE_MUPDF_EXPORT
    allPassed &= testHighlightRectsToPdfSpace();
    allPassed &= testHighlightAnnotationExport();
    #endif
    
    qDebug() << "";
    if (allPassed) {
        qDebug() << "✅ All MuPdfExporter tests passed!";
    } else {
        qDebug() << "❌ Some MuPdfExporter tests failed!";
    }
    qDebug() << "========================================";
    qDebug() << "";
    
    return allPassed;
}

} // namespace MuPdfExporterTests

