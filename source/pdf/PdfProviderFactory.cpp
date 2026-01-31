// ============================================================================
// PdfProviderFactory - PDF provider creation
// ============================================================================
// This file contains the factory methods for PdfProvider.
// 
// As of v1.2.0, SpeedyNote uses MuPDF exclusively on all platforms:
//   - Eliminates symbol conflicts between MuPDF and Poppler/OpenJPEG
//   - Consistent rendering across all platforms
//   - Faster rendering performance
//   - Smaller dependency footprint
// ============================================================================

#include "PdfProvider.h"
#include "MuPdfProvider.h"

#include <memory>

// ============================================================================
// Factory Methods
// ============================================================================

std::unique_ptr<PdfProvider> PdfProvider::create(const QString& pdfPath)
{
    auto provider = std::make_unique<MuPdfProvider>(pdfPath);
    if (provider->isValid()) {
        return provider;
    }
    return nullptr;
}

bool PdfProvider::isAvailable()
{
    // MuPDF is a compile-time dependency,
    // so if this code compiles, the backend is available.
    return true;
}

