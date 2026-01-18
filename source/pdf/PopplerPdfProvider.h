#pragma once

// ============================================================================
// PopplerPdfProvider - Poppler-Qt6 implementation of PdfProvider
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.2)
//
// Wraps Poppler-Qt6 library to provide PDF functionality.
// This implementation is used on desktop platforms (Windows, Linux, macOS).
// ============================================================================

#include "PdfProvider.h"
#include <poppler/qt6/poppler-qt6.h>
#include <memory>

/**
 * @brief PdfProvider implementation using Poppler-Qt6.
 * 
 * Wraps the Poppler library for PDF rendering, text extraction, and navigation.
 * Applies antialiasing and text hinting for high-quality rendering.
 */
class PopplerPdfProvider : public PdfProvider {
public:
    /**
     * @brief Construct a provider for the given PDF file.
     * @param pdfPath Path to the PDF file.
     * 
     * Check isValid() after construction to verify the PDF loaded successfully.
     */
    explicit PopplerPdfProvider(const QString& pdfPath);
    
    /**
     * @brief Destructor.
     */
    ~PopplerPdfProvider() override = default;
    
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
     * @brief Convert Poppler outline items to our format (recursive).
     * @param popplerItem The Poppler outline item.
     * @return Converted PdfOutlineItem with children.
     */
    PdfOutlineItem convertOutlineItem(const Poppler::OutlineItem& popplerItem) const;
    
    /**
     * @brief Get a Poppler page object.
     * @param pageIndex 0-based page index.
     * @return Unique pointer to page, or nullptr if invalid.
     */
    std::unique_ptr<Poppler::Page> getPage(int pageIndex) const;
    
    std::unique_ptr<Poppler::Document> m_document;  ///< The loaded PDF document
    QString m_path;                                   ///< Path to the PDF file
};
