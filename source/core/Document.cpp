// ============================================================================
// Document - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3)
// ============================================================================

#include "Document.h"

// ===== Constructor =====

Document::Document()
{
    // Generate unique ID
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set timestamps
    created = QDateTime::currentDateTime();
    lastModified = created;
}

// ===== Factory Methods =====

std::unique_ptr<Document> Document::createNew(const QString& docName, Mode docMode)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = docMode;
    
    // Note: Pages will be added in Task 1.2.5 (Page Management)
    // For now, this creates an empty document structure
    
    return doc;
}

std::unique_ptr<Document> Document::createForPdf(const QString& docName, const QString& pdfPath)
{
    // Note: Full implementation in Task 1.2.4 (PDF Reference Management)
    // For now, create a basic document and store the intent
    
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = Mode::Paged;
    
    // PDF loading and page creation will be implemented in Task 1.2.4
    // The pdfPath parameter is intentionally unused for now
    Q_UNUSED(pdfPath);
    
    return doc;
}
