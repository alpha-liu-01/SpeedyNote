// ============================================================================
// PdfProviderFactory - Platform-specific PDF provider creation
// ============================================================================
// This file contains the factory methods for PdfProvider.
// It selects the appropriate backend based on the target platform:
//   - Android: MuPDF (smaller, bundled dependencies)
//   - Alpine Linux (musl): MuPDF (avoids symbol collision with Poppler/OpenJPEG)
//   - Desktop (glibc): Poppler (feature-rich, system library)
// ============================================================================

#include "PdfProvider.h"

#include <memory>

// ============================================================================
// Platform Detection
// ============================================================================
// On Alpine Linux (musl libc), both MuPDF and Poppler use OpenJPEG for JPEG2000.
// When both are loaded as shared libraries, MuPDF's custom allocators get called
// by Poppler's OpenJPEG, causing crashes. Solution: Use MuPDF exclusively on musl.
//
// Detection: musl libc doesn't define __GLIBC__, while glibc does.
// ============================================================================

#if defined(Q_OS_ANDROID)
    // Android: Use MuPDF (smaller, bundled dependencies)
    #define SPEEDYNOTE_USE_MUPDF 1

#elif defined(__linux__) && !defined(__GLIBC__)
    // Alpine Linux / musl libc: Use MuPDF to avoid Poppler/OpenJPEG symbol collision
    #define SPEEDYNOTE_USE_MUPDF 1

#else
    // Desktop (Windows, macOS, Linux with glibc): Use Poppler
    #define SPEEDYNOTE_USE_POPPLER 1

#endif

// Include the appropriate provider
#ifdef SPEEDYNOTE_USE_MUPDF
#include "MuPdfProvider.h"
using PdfProviderImpl = MuPdfProvider;
#else
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

