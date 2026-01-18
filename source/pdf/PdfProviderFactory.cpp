// ============================================================================
// PdfProviderFactory - Platform-specific PDF provider creation
// ============================================================================
// This file contains the factory methods for PdfProvider.
// It selects the appropriate backend based on the target platform:
//   - Android: MuPDF (smaller, bundled dependencies)
//   - Desktop: Poppler (feature-rich, system library)
// ============================================================================

#include "PdfProvider.h"

#include <memory>

#ifdef Q_OS_ANDROID
// Android: Use MuPDF
#include "MuPdfProvider.h"
using PdfProviderImpl = MuPdfProvider;

#else
// Desktop (Windows, macOS, Linux): Use Poppler
#include "PopplerPdfProvider.h"
using PdfProviderImpl = PopplerPdfProvider;

#endif

// ============================================================================
// Factory Methods
// ============================================================================

std::unique_ptr<PdfProvider> PdfProvider::create(const QString& pdfPath)
{
    auto provider = std::make_unique<PdfProviderImpl>(pdfPath);
    if (provider->isValid()) {
        return provider;
    }
    return nullptr;
}

bool PdfProvider::isAvailable()
{
    // Both MuPDF and Poppler are compile-time dependencies,
    // so if this code compiles, the backend is available.
    return true;
}

