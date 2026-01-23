#pragma once

// ============================================================================
// MuPdfExporterTests - Unit tests for the MuPdfExporter class
// ============================================================================
// Tests for PDF export functionality, focusing on utility functions
// that can be tested without MuPDF or file dependencies.
//
// Current tests:
// - parsePageRange() edge cases
// ============================================================================

#include "MuPdfExporter.h"
#include <QDebug>

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

