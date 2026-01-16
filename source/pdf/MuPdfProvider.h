#pragma once

// ============================================================================
// MuPdfProvider - MuPDF implementation of PdfProvider
// ============================================================================
// Part of SpeedyNote Android port
//
// Wraps the MuPDF library to provide PDF functionality.
// This implementation is used on Android (and optionally other platforms).
//
// MuPDF advantages over Poppler:
// - Smaller binary size (~5MB vs ~15MB)
// - Fewer dependencies (everything bundled)
// - Better mobile performance
// - AGPL license (compatible with SpeedyNote)
// ============================================================================

#include "PdfProvider.h"

// Forward declarations for MuPDF types (avoid exposing mupdf headers)
struct fz_context;
struct fz_document;

/**
 * @brief PdfProvider implementation using MuPDF.
 * 
 * Wraps the MuPDF library for PDF rendering, text extraction, and navigation.
 * Used on Android where Poppler is not available.
 */
class MuPdfProvider : public PdfProvider {
public:
    /**
     * @brief Construct a provider for the given PDF file.
     * @param pdfPath Path to the PDF file.
     * 
     * Check isValid() after construction to verify the PDF loaded successfully.
     */
    explicit MuPdfProvider(const QString& pdfPath);
    
    /**
     * @brief Destructor - cleans up MuPDF resources.
     */
    ~MuPdfProvider() override;
    
    // Disable copy (MuPDF context is not copyable)
    MuPdfProvider(const MuPdfProvider&) = delete;
    MuPdfProvider& operator=(const MuPdfProvider&) = delete;
    
    // ===== Document Info =====
    bool isValid() const override;
    bool isLocked() const override;
    int pageCount() const override;
    QString title() const override;
    QString author() const override;
    QString subject() const override;
    QString filePath() const override;
    
    // ===== Outline =====
    QVector<PdfOutlineItem> outline() const override;
    bool hasOutline() const override;
    
    // ===== Page Info =====
    QSizeF pageSize(int pageIndex) const override;
    
    // ===== Rendering =====
    QImage renderPageToImage(int pageIndex, qreal dpi) const override;
    
    // ===== Text Selection =====
    QVector<PdfTextBox> textBoxes(int pageIndex) const override;
    bool supportsTextExtraction() const override { return true; }
    
    // ===== Links =====
    QVector<PdfLink> links(int pageIndex) const override;
    bool supportsLinks() const override { return true; }
    
private:
    /**
     * @brief Get metadata string from PDF.
     * @param key Metadata key (e.g., "info:Title", "info:Author")
     * @return Metadata value or empty string.
     */
    QString getMetadata(const char* key) const;
    
    /**
     * @brief Convert MuPDF outline to our format (recursive).
     * @param outline MuPDF outline structure.
     * @return List of converted outline items.
     */
    QVector<PdfOutlineItem> convertOutline(struct fz_outline* outline) const;
    
    fz_context* m_ctx = nullptr;        ///< MuPDF context (owns all allocations)
    fz_document* m_doc = nullptr;       ///< The loaded PDF document
    QString m_path;                     ///< Path to the PDF file
    int m_pageCount = 0;                ///< Cached page count
};

