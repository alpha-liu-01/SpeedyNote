// ============================================================================
// Document - Implementation
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.2.3, 1.2.4, 1.2.5)
// ============================================================================

#include "Document.h"
#include "../objects/OcrTextObject.h"
#include "../objects/LinkObject.h"
#include "../pdf/PdfMaterializer.h"
#include <QCryptographicHash>
#include <QSaveFile>
#include <QSettings>
#include <cmath>
#include <algorithm>  // Phase 5.4: for std::sort, std::greater in merge
#include <functional>
#include <limits>

#ifdef __GLIBC__
#include <malloc.h>
#endif

// ===== Constructor & Destructor =====

Document::Document()
{
    // Generate unique ID
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Set timestamps
    created = QDateTime::currentDateTime();
    lastModified = created;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document CREATED:" << this << "id=" << id.left(8);
#endif
}

Document::~Document()
{
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document DESTROYED:" << this << "id=" << id.left(8) 
             << "pages=" << m_pageOrder.size() << "tiles=" << m_tiles.size();
#endif
    // Note: m_loadedPages, m_tiles, and m_pdfProviders own unique_ptrs, auto-cleaned
    
    m_loadedPages.clear();
    m_tiles.clear();
    ++m_tileLoadVersion;
    m_pdfProviders.clear();

    // Drop any residual outline cache so subsequent document instances
    // cannot observe stale state (cache members are per-instance, but
    // being explicit keeps bundle-reload behavior predictable).
    clearLinkOutlineCache();
    
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

// ===== Factory Methods =====

std::unique_ptr<Document> Document::createNew(const QString& docName, Mode docMode)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = docMode;
    
    if (docMode == Mode::Edgeless) {
        // Don't create any tiles - they're created on-demand when user draws
        // m_tiles starts empty (default state)
        
        // Phase 5.6: Create default layer in manifest
        LayerDefinition defaultLayer;
        defaultLayer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        defaultLayer.name = "Layer 1";
        doc->m_edgelessLayers.push_back(defaultLayer);
    } else {
        // Paged mode: ensure at least one page exists
    doc->ensureMinimumPages();
    }
    
    return doc;
}

std::unique_ptr<Document> Document::createForPdf(const QString& docName, const QString& pdfPath)
{
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = Mode::Paged;
    
    // Try to load the PDF
    // Note: loadPdf() stores the path regardless of success (for relink)
    if (doc->loadPdf(pdfPath)) {
        // Create pages for all PDF pages
        doc->createPagesForPdf();
    } else {
        // PDF failed to load, path is already stored by loadPdf()
        // Create a single default page
        doc->ensureMinimumPages();
    }
    
    return doc;
}

// =========================================================================
// PDF Reference Management (Task 1.2.4)
// =========================================================================

bool Document::pdfFileExists() const
{
    const PdfSource* s = primarySource();
    if (!s || s->path.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(s->path);
}

// =========================================================================
// Multi-Source Registry
// =========================================================================

PdfSource& Document::ensurePrimarySource()
{
    // Return the existing flagged primary if there is one.
    for (PdfSource& s : m_pdfSources) {
        if (s.primary) return s;
    }
    // None yet: create a fresh primary at the front. Front placement keeps the
    // legacy "primary first" convention for single-PDF documents; correctness no
    // longer depends on position (primarySource() searches by the flag).
    PdfSource src;
    src.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    src.primary = true;
    m_pdfSources.insert(m_pdfSources.begin(), src);
    return m_pdfSources.front();
}

const PdfSource* Document::pdfSourceById(const QString& sourceId) const
{
    if (sourceId.isEmpty()) {
        return primarySource();
    }
    for (const PdfSource& s : m_pdfSources) {
        if (s.id == sourceId) return &s;
    }
    return nullptr;
}

PdfSource* Document::pdfSourceById(const QString& sourceId)
{
    if (sourceId.isEmpty()) {
        return primarySource();
    }
    for (PdfSource& s : m_pdfSources) {
        if (s.id == sourceId) return &s;
    }
    return nullptr;
}

void Document::cachePdfProviderPath(const QString& registryId, const QString& path) const
{
    const QFileInfo info(path);
    m_pdfProviderPaths[registryId] = info.absoluteFilePath();
    m_pdfProviderPathSizes[registryId] = info.size();
    m_pdfProviderPathModifiedTimes[registryId] =
        info.lastModified().toMSecsSinceEpoch();
}

bool Document::cachedPdfProviderPathIsCurrent(const QString& registryId) const
{
    auto pathIt = m_pdfProviderPaths.find(registryId);
    auto sizeIt = m_pdfProviderPathSizes.find(registryId);
    auto modifiedIt = m_pdfProviderPathModifiedTimes.find(registryId);
    if (pathIt == m_pdfProviderPaths.end()
        || sizeIt == m_pdfProviderPathSizes.end()
        || modifiedIt == m_pdfProviderPathModifiedTimes.end()) {
        return false;
    }

    const QFileInfo info(pathIt->second);
    return info.exists() && info.isFile()
        && sizeIt->second == info.size()
        && modifiedIt->second == info.lastModified().toMSecsSinceEpoch();
}

void Document::clearCachedPdfProvider(const QString& registryId) const
{
    m_pdfProviders.erase(registryId);
    m_pdfProviderPaths.erase(registryId);
    m_pdfProviderPathSizes.erase(registryId);
    m_pdfProviderPathModifiedTimes.erase(registryId);
    m_pdfProvidersUsingBundled.remove(registryId);
    m_pdfProvidersUsingRelative.remove(registryId);
    m_pdfSourceFailures.erase(registryId);
}

Document::PdfSourceOpenResult Document::openBestPdfSourceCandidate(const PdfSource& source) const
{
    PdfSourceOpenResult result;
    if (!PdfProvider::isAvailable()) {
        result.failureStatus = PdfSourceHealthStatus::Unreadable;
        return result;
    }

    struct Candidate {
        QString path;
        bool bundled = false;
        bool relative = false;
    };
    QVector<Candidate> candidates;
    QSet<QString> seen;
    auto appendCandidate = [&](const QString& path, bool bundled, bool relative) {
        if (path.isEmpty()) return;
        const QString absolute = QFileInfo(path).absoluteFilePath();
        const QString key = QDir::cleanPath(absolute);
        if (seen.contains(key)) return;
        seen.insert(key);
        candidates.append({absolute, bundled, relative});
    };

    appendCandidate(source.path, false, false);
    if (!source.relativePath.isEmpty() && !m_bundlePath.isEmpty()) {
        appendCandidate(QDir(m_bundlePath).absoluteFilePath(source.relativePath), false, true);
    }
    if (source.bundled && !source.bundledFile.isEmpty() && !m_bundlePath.isEmpty()) {
        appendCandidate(QDir(m_bundlePath).absoluteFilePath(source.bundledFile), true, false);
    }

    bool foundExisting = false;
    bool foundIdentityMismatch = false;
    bool foundUnreadable = false;
    for (const Candidate& candidate : candidates) {
        QFileInfo info(candidate.path);
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        foundExisting = true;

        // A bundled mini-PDF intentionally differs from the original full-file
        // identity. Only full external candidates are verified against hash+size.
        if (!candidate.bundled && !source.hash.isEmpty()) {
            const bool sizeMatches = source.size <= 0 || info.size() == source.size;
            const bool hashMatches = sizeMatches && computePdfHash(candidate.path) == source.hash;
            if (!hashMatches) {
                foundIdentityMismatch = true;
                continue;
            }
        }

        std::unique_ptr<PdfProvider> provider = PdfProvider::create(candidate.path);
        if (!provider || !provider->isValid()) {
            foundUnreadable = true;
            continue;
        }

        result.provider = std::move(provider);
        result.path = candidate.path;
        result.bundled = candidate.bundled;
        result.relative = candidate.relative;
        return result;
    }

    if (foundUnreadable) {
        result.failureStatus = PdfSourceHealthStatus::Unreadable;
    } else if (foundIdentityMismatch) {
        result.failureStatus = PdfSourceHealthStatus::IdentityMismatch;
    } else if (foundExisting) {
        result.failureStatus = PdfSourceHealthStatus::Unreadable;
    } else {
        result.failureStatus = PdfSourceHealthStatus::Missing;
    }
    return result;
}

QString Document::pdfPathForSource(const QString& sourceId) const
{
    const PdfSource* s = pdfSourceById(sourceId);
    if (!s) return QString();
    auto pathIt = m_pdfProviderPaths.find(s->id);
    if (pathIt != m_pdfProviderPaths.end()) {
        if (cachedPdfProviderPathIsCurrent(s->id)) {
            return pathIt->second;
        }
        clearCachedPdfProvider(s->id);
    }
    if (m_pdfSourceFailures.find(s->id) != m_pdfSourceFailures.end()) {
        return QString();
    }

    // Resolve and validate without retaining a provider. Some consumers (notably
    // MuPdfExporter) intentionally own their own MuPDF context and must not race a
    // cached Document provider for the same file.
    PdfSourceOpenResult opened = openBestPdfSourceCandidate(*s);
    if (!opened.provider) {
        m_pdfSourceFailures[s->id] = opened.failureStatus;
        return QString();
    }
    m_pdfSourceFailures.erase(s->id);
    cachePdfProviderPath(s->id, opened.path);
    if (opened.bundled) m_pdfProvidersUsingBundled.insert(s->id);
    else m_pdfProvidersUsingBundled.remove(s->id);
    if (opened.relative) m_pdfProvidersUsingRelative.insert(s->id);
    else m_pdfProvidersUsingRelative.remove(s->id);
    return opened.path;
}

PdfProvider* Document::providerForSource(const QString& sourceId) const
{
    const PdfSource* s = pdfSourceById(sourceId);
    if (!s) return nullptr;

    // Already open?
    auto it = m_pdfProviders.find(s->id);
    if (it != m_pdfProviders.end() && it->second && it->second->isValid()) {
        return it->second.get();
    }
    if (m_pdfSourceFailures.find(s->id) != m_pdfSourceFailures.end()) {
        return nullptr;
    }

    auto resolvedIt = m_pdfProviderPaths.find(s->id);
    if (resolvedIt != m_pdfProviderPaths.end()) {
        if (cachedPdfProviderPathIsCurrent(s->id)) {
            std::unique_ptr<PdfProvider> provider = PdfProvider::create(resolvedIt->second);
            if (provider && provider->isValid()) {
                PdfProvider* raw = provider.get();
                m_pdfProviders[s->id] = std::move(provider);
                m_pdfSourceFailures.erase(s->id);
                return raw;
            }
        }
        clearCachedPdfProvider(s->id);
    }

    PdfSourceOpenResult opened = openBestPdfSourceCandidate(*s);
    if (!opened.provider) {
        clearCachedPdfProvider(s->id);
        m_pdfSourceFailures[s->id] = opened.failureStatus;
        return nullptr;
    }

    PdfProvider* raw = opened.provider.get();
    m_pdfProviders[s->id] = std::move(opened.provider);
    cachePdfProviderPath(s->id, opened.path);
    m_pdfSourceFailures.erase(s->id);
    if (opened.bundled) m_pdfProvidersUsingBundled.insert(s->id);
    else m_pdfProvidersUsingBundled.remove(s->id);
    if (opened.relative) m_pdfProvidersUsingRelative.insert(s->id);
    else m_pdfProvidersUsingRelative.remove(s->id);
    return raw;
}

int Document::notebookPageCountForSource(const QString& sourceId) const
{
    int count = 0;
    for (const auto& [uuid, pdfPage] : m_pagePdfIndex) {
        Q_UNUSED(pdfPage);
        auto sourceIt = m_pagePdfSource.find(uuid);
        const QString pageSource = sourceIt != m_pagePdfSource.end()
            ? sourceIt->second : QString();
        if (pageSource == sourceId) ++count;
    }
    return count;
}

void Document::retryPdfSource(const QString& sourceId) const
{
    const PdfSource* source = pdfSourceById(sourceId);
    if (source) clearCachedPdfProvider(source->id);
}

QVector<PdfSourceHealth> Document::pdfSourceHealthSnapshot() const
{
    QHash<QString, int> referencedCounts;
    QHash<QString, QVector<int>> referencedOriginalPages;
    for (const auto& [uuid, originalPage] : m_pagePdfIndex) {
        auto sourceIt = m_pagePdfSource.find(uuid);
        const QString pageSource = sourceIt != m_pagePdfSource.end()
            ? sourceIt->second : QString();
        ++referencedCounts[pageSource];
        referencedOriginalPages[pageSource].append(originalPage);
    }

    QVector<PdfSourceHealth> result;
    result.reserve(static_cast<int>(m_pdfSources.size()));
    for (const PdfSource& source : m_pdfSources) {
        const QString sourceId = normalizedPdfSourceId(source);
        const QString activePath = pdfPathForSource(sourceId);

        PdfSourceHealth health;
        health.sourceId = sourceId;
        health.activePath = activePath;
        health.referencedPages = referencedCounts.value(sourceId);

        auto providerIt = m_pdfProviders.find(source.id);
        if (providerIt != m_pdfProviders.end()
            && providerIt->second && providerIt->second->isValid()) {
            health.title = providerIt->second->title().trimmed();
        }
        if (health.title.isEmpty()) {
            const QString displayPath = !source.path.isEmpty()
                ? source.path
                : (!source.relativePath.isEmpty() ? source.relativePath : activePath);
            health.title = QFileInfo(displayPath).completeBaseName();
        }
        if (health.title.isEmpty()) {
            const int slot = paletteSlotForSource(sourceId);
            health.title = QCoreApplication::translate("Document", "Source %1")
                .arg(slot >= 0 ? slot + 1 : 1);
        }

        if (!activePath.isEmpty()) {
            if (m_pdfProvidersUsingBundled.contains(source.id)) {
                int unavailable = 0;
                PdfProvider* bundledProvider = providerForSource(sourceId);
                const int bundledPageCount =
                    bundledProvider ? bundledProvider->pageCount() : 0;
                QSet<int> usedBundledPages;
                auto pagesIt = referencedOriginalPages.constFind(sourceId);
                if (pagesIt != referencedOriginalPages.cend()) {
                    for (int originalPage : pagesIt.value()) {
                        auto mappedIt = source.pageMap.constFind(originalPage);
                        if (mappedIt == source.pageMap.constEnd()
                            || mappedIt.value() < 0
                            || mappedIt.value() >= bundledPageCount
                            || usedBundledPages.contains(mappedIt.value())) {
                            ++unavailable;
                        } else {
                            usedBundledPages.insert(mappedIt.value());
                        }
                    }
                }
                health.unavailablePages = unavailable;
                health.status = unavailable > 0
                    ? PdfSourceHealthStatus::PartialBundled
                    : PdfSourceHealthStatus::AvailableBundled;
            } else {
                health.status = m_pdfProvidersUsingRelative.contains(source.id)
                    ? PdfSourceHealthStatus::AvailableRelative
                    : PdfSourceHealthStatus::AvailableExternal;
            }
        } else {
            auto failureIt = m_pdfSourceFailures.find(source.id);
            health.status = failureIt != m_pdfSourceFailures.end()
                ? failureIt->second
                : PdfSourceHealthStatus::Missing;
            health.unavailablePages = health.referencedPages;
        }
        result.append(health);
    }
    return result;
}

QString Document::registerSource(const QString& path, const QString& hash, qint64 size, bool bundled)
{
    // Dedup by identity (hash + size) against existing sources.
    if (!hash.isEmpty()) {
        for (PdfSource& s : m_pdfSources) {
            if (s.hash == hash && s.size == size) {
                // A newly selected copy can recover an existing deduplicated
                // source whose old absolute path has gone stale.
                if (!path.isEmpty() && QFileInfo::exists(path)
                    && (s.path.isEmpty() || !QFileInfo::exists(s.path))) {
                    locateSource(s.id, path);
                }
                return s.id;
            }
        }
    }

    PdfSource src;
    src.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    src.path = path;
    src.hash = hash;
    src.size = size;
    src.bundled = bundled;
    src.primary = false;  // Registered (imported) sources are never the primary base PDF.
    m_pdfSources.push_back(src);
    return src.id;
}

bool Document::locateSource(const QString& sourceId, const QString& newPath)
{
    PdfSource* source = pdfSourceById(sourceId);
    if (!source || newPath.isEmpty()) return false;

    QFileInfo info(newPath);
    if (!info.exists() || !info.isFile() || !PdfProvider::isAvailable()) return false;

    const QString candidateHash = computePdfHash(newPath);
    if (candidateHash.isEmpty()) return false;
    if (!source->hash.isEmpty()) {
        if (candidateHash != source->hash
            || (source->size > 0 && info.size() != source->size)) {
            return false;
        }
    }

    std::unique_ptr<PdfProvider> provider = PdfProvider::create(newPath);
    if (!provider || !provider->isValid()) return false;

    if (source->hash.isEmpty()) {
        source->hash = candidateHash;
        source->size = info.size();
    }
    source->path = info.absoluteFilePath();
    if (!m_bundlePath.isEmpty()) {
        source->relativePath = QDir(m_bundlePath).relativeFilePath(source->path);
    }
    clearCachedPdfProvider(source->id);
    cachePdfProviderPath(source->id, source->path);
    m_pdfProviders[source->id] = std::move(provider);
    markModified();
    return true;
}

QStringList Document::unreferencedSourceIds() const
{
    // Collect ids referenced by any page (empty = primary).
    QSet<QString> referenced;
    bool primaryReferenced = false;
    for (const auto& [uuid, pdfIdx] : m_pagePdfIndex) {
        Q_UNUSED(pdfIdx);
        auto it = m_pagePdfSource.find(uuid);
        if (it == m_pagePdfSource.end() || it->second.isEmpty()) {
            primaryReferenced = true;
        } else {
            referenced.insert(it->second);
        }
    }

    QStringList result;
    for (const PdfSource& s : m_pdfSources) {
        if (m_undoRetainedPdfSourceIds.contains(s.id)) {
            continue;
        }
        if (s.primary) {
            if (!primaryReferenced) result.append(s.id);
        } else if (!referenced.contains(s.id)) {
            result.append(s.id);
        }
    }
    return result;
}

void Document::retainPdfSourceForUndo(const QString& sourceId)
{
    const PdfSource* source = pdfSourceById(sourceId);
    if (source) m_undoRetainedPdfSourceIds.insert(source->id);
}

int Document::pruneUnreferencedSources()
{
    const QStringList stale = unreferencedSourceIds();
    if (stale.isEmpty()) {
        return 0;
    }

    QSet<QString> removable;

    // Close cached providers for the sources being removed, and delete any bundled
    // mini-PDF file on disk (Plan B2) so the bundle doesn't keep orphaned PDFs.
    for (const QString& id : stale) {
        clearCachedPdfProvider(id);
        const PdfSource* s = pdfSourceById(id);
        bool bundledFileRemoved = true;
        if (s && s->bundled && !s->bundledFile.isEmpty() && !m_bundlePath.isEmpty()) {
            const QString abs = QDir(m_bundlePath).absoluteFilePath(s->bundledFile);
            if (QFile::exists(abs)) {
                bundledFileRemoved = QFile::remove(abs);
                if (!bundledFileRemoved) {
                    qWarning() << "pruneUnreferencedSources: keeping source because its"
                                  " bundled PDF could not be removed:" << abs;
                }
            }
        }
        if (bundledFileRemoved) removable.insert(id);
    }

    if (removable.isEmpty()) return 0;

    // Erase the sources from the registry. toJson() will re-derive the legacy
    // pdf_path mirror from whatever primary survives (or write an empty path
    // when the registry becomes empty).
    m_pdfSources.erase(
        std::remove_if(m_pdfSources.begin(), m_pdfSources.end(),
                       [&removable](const PdfSource& s) { return removable.contains(s.id); }),
        m_pdfSources.end());

    markModified();
    return removable.size();
}

int Document::resolveSourcePageIndex(const QString& sourceId, int originalPage) const
{
    const PdfSource* s = pdfSourceById(sourceId);
    if (!s) {
        return originalPage;
    }
    pdfPathForSource(sourceId);
    if (!m_pdfProvidersUsingBundled.contains(s->id)) {
        return originalPage;
    }
    auto it = s->pageMap.constFind(originalPage);
    if (it == s->pageMap.constEnd() || it.value() < 0) return -1;
    for (auto other = s->pageMap.constBegin(); other != s->pageMap.constEnd(); ++other) {
        if (other.key() != originalPage && other.value() == it.value()) return -1;
    }
    PdfProvider* provider = providerForSource(sourceId);
    return provider && it.value() < provider->pageCount() ? it.value() : -1;
}

bool Document::needsMaterialization() const
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    QHash<QString, int> bundledPageCounts;
    // Collect referenced (sourceId -> original pages) from live pages, skipping
    // the primary source (empty id), which always stays external.
    for (int i = 0; i < pageCount(); ++i) {
        const Page* p = page(i);
        if (!p || p->pdfPageNumber < 0 || p->pdfSourceId.isEmpty()) {
            continue;
        }
        const PdfSource* s = pdfSourceById(p->pdfSourceId);
        if (!s) {
            continue;
        }
        // A referenced page not present in the (bundled) source's page map means
        // there is un-bundled imported content to materialize.
        auto mappedIt = s->pageMap.constFind(p->pdfPageNumber);
        if (!s->bundled || mappedIt == s->pageMap.constEnd()
            || mappedIt.value() < 0) {
            return true;
        }
        for (auto other = s->pageMap.constBegin(); other != s->pageMap.constEnd(); ++other) {
            if (other.key() != p->pdfPageNumber && other.value() == mappedIt.value()) {
                return true;
            }
        }

        if (!bundledPageCounts.contains(s->id)) {
            const QString bundledPath =
                QDir(m_bundlePath).absoluteFilePath(s->bundledFile);
            std::unique_ptr<PdfProvider> provider = PdfProvider::create(bundledPath);
            bundledPageCounts.insert(
                s->id, provider && provider->isValid() ? provider->pageCount() : 0);
        }
        if (mappedIt.value() >= bundledPageCounts.value(s->id)) {
            return true;
        }
    }
    return false;
}

int Document::materializeSources(QString* errorOut)
{
    if (m_bundlePath.isEmpty()) {
        return 0;
    }

    // Build sourceId -> set of referenced original page numbers (skip primary).
    std::map<QString, QList<int>> refs;
    for (int i = 0; i < pageCount(); ++i) {
        const Page* p = page(i);
        if (!p || p->pdfPageNumber < 0 || p->pdfSourceId.isEmpty()) {
            continue;
        }
        refs[p->pdfSourceId].append(p->pdfPageNumber);
    }
    if (refs.empty()) {
        return 0;
    }

    const QString pdfsDir = QDir(m_bundlePath).absoluteFilePath(QStringLiteral("pdfs"));
    int materialized = 0;

    for (auto& [sourceId, pages] : refs) {
        PdfSource* s = pdfSourceById(sourceId);
        if (!s) {
            continue;
        }
        // Skip sources that already have every referenced page bundled.
        bool anyMissing = !s->bundled;
        if (s->bundled) {
            const QString bundledPath =
                QDir(m_bundlePath).absoluteFilePath(s->bundledFile);
            std::unique_ptr<PdfProvider> bundledProvider =
                PdfProvider::create(bundledPath);
            if (!bundledProvider || !bundledProvider->isValid()) {
                bundledProvider.reset();
                clearCachedPdfProvider(s->id);
                QFile::remove(bundledPath);
                s->pageMap.clear();
                s->bundled = false;
                anyMissing = true;
            }
            const int bundledCount = bundledProvider && bundledProvider->isValid()
                ? bundledProvider->pageCount() : 0;
            QSet<int> usedBundledPages;
            for (int pg : pages) {
                auto mappedIt = s->pageMap.constFind(pg);
                if (mappedIt == s->pageMap.constEnd()
                    || mappedIt.value() < 0
                    || mappedIt.value() >= bundledCount
                    || usedBundledPages.contains(mappedIt.value())) {
                    s->pageMap.remove(pg);
                    anyMissing = true;
                } else {
                    usedBundledPages.insert(mappedIt.value());
                }
            }
        }
        if (!anyMissing) {
            continue;
        }

        if (!QDir().mkpath(pdfsDir)) {
            if (errorOut) *errorOut = QStringLiteral("Could not create bundle pdfs directory");
            continue;
        }

        const QString relFile = QStringLiteral("pdfs/src-%1.pdf").arg(s->id);
        const QString absFile = QDir(m_bundlePath).absoluteFilePath(relFile);

        // Resolve a validated full source. A bundled mini-PDF cannot be used as
        // the origin because its compact page indices differ from originalPages.
        PdfSourceOpenResult origin = openBestPdfSourceCandidate(*s);
        const QString originPath =
            origin.provider && !origin.bundled ? origin.path : QString();
        origin.provider.reset();

        // Release any cached provider before we overwrite the mini-PDF file.
        clearCachedPdfProvider(s->id);

        QString err;
        if (PdfMaterializer::materialize(originPath, absFile, pages, s->pageMap, &err)) {
            if (!s->pageMap.isEmpty()) {
                s->bundled = true;
                s->bundledFile = relFile;
                markModified();
                ++materialized;
            }
        } else if (errorOut && !err.isEmpty()) {
            *errorOut = err;
        }
        // Drop the provider again so the next open uses the (now bundled) file.
        clearCachedPdfProvider(s->id);
    }

    return materialized;
}

QString Document::computePdfHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    
    // Read first 1MB for hashing (fast even for large PDFs)
    constexpr qint64 HASH_CHUNK_SIZE = 1024 * 1024; // 1 MB
    QByteArray data = file.read(HASH_CHUNK_SIZE);
    file.close();
    
    if (data.isEmpty()) {
        return QString();
    }
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    QByteArray result = hash.result();
    
    return QStringLiteral("sha256:") + result.toHex();
}

qint64 Document::getPdfFileSize(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return -1;
    }
    return info.size();
}

bool Document::verifyPdfHash(const QString& path) const
{
    const PdfSource* s = primarySource();
    // Legacy document without hash - can't verify, assume OK
    if (!s || s->hash.isEmpty()) {
        return true;
    }
    
    // Compute hash of the candidate file
    QString candidateHash = computePdfHash(path);
    if (candidateHash.isEmpty()) {
        return false; // Can't read file
    }
    
    return (candidateHash == s->hash);
}

bool Document::loadPdf(const QString& path)
{
    // Operate on the primary source (index 0), creating it if needed.
    PdfSource& primary = ensurePrimarySource();
    
    // Unload any existing primary provider first
    clearCachedPdfProvider(primary.id);
    
    // Store the path regardless of load success (for relink)
    primary.path = path;
    
    if (path.isEmpty()) {
        return false;
    }
    
    // Check if file exists
    if (!QFileInfo::exists(path)) {
        return false;
    }
    
    // Check if PDF provider is available
    if (!PdfProvider::isAvailable()) {
        return false;
    }
    
    // Try to load the PDF
    std::unique_ptr<PdfProvider> provider = PdfProvider::create(path);
    
    if (!provider || !provider->isValid()) {
        return false;
    }
    
    // Compute and store hash if not already set (first load or legacy document)
    if (primary.hash.isEmpty()) {
        primary.hash = computePdfHash(path);
        primary.size = getPdfFileSize(path);
    }
    
    cachePdfProviderPath(primary.id, path);
    m_pdfProviders[primary.id] = std::move(provider);
    return true;
}

void Document::unloadPdf()
{
    // Release the primary provider only; source metadata is preserved for recovery.
    if (const PdfSource* s = primarySource()) {
        clearCachedPdfProvider(s->id);
    }
}

void Document::clearPdfReference()
{
    m_pdfProviders.clear();
    m_pdfProviderPaths.clear();
    m_pdfProviderPathSizes.clear();
    m_pdfProviderPathModifiedTimes.clear();
    m_pdfProvidersUsingBundled.clear();
    m_pdfProvidersUsingRelative.clear();
    m_pdfSourceFailures.clear();
    m_undoRetainedPdfSourceIds.clear();
    m_pdfSources.clear();
    m_pagePdfSource.clear();
    markModified();
}

QImage Document::renderPdfPageToImage(int pageIndex, qreal dpi) const
{
    return renderPdfPageToImage(QString(), pageIndex, dpi);
}

QImage Document::renderPdfPageToImage(const QString& sourceId, int pageIndex, qreal dpi) const
{
    PdfProvider* provider = providerForSource(sourceId);
    if (!provider || !provider->isValid()) {
        return QImage();
    }
    // pageIndex is the ORIGINAL page number; a bundled source needs its mini-PDF index.
    const int providerPage = resolveSourcePageIndex(sourceId, pageIndex);
    if (providerPage < 0) {
        return QImage();
    }
    return provider->renderPageToImage(providerPage, dpi);
}

QPixmap Document::renderPdfPageToPixmap(int pageIndex, qreal dpi) const
{
    PdfProvider* provider = providerForSource(QString());
    if (!provider || !provider->isValid()) {
        return QPixmap();
    }
    return provider->renderPageToPixmap(pageIndex, dpi);
}

QVector<QRect> Document::pdfImageRegions(int pageIndex, qreal dpi) const
{
    return pdfImageRegions(QString(), pageIndex, dpi);
}

QVector<QRect> Document::pdfImageRegions(const QString& sourceId, int pageIndex, qreal dpi) const
{
    PdfProvider* provider = providerForSource(sourceId);
    if (!provider || !provider->isValid()) {
        return {};
    }
    const int providerPage = resolveSourcePageIndex(sourceId, pageIndex);
    if (providerPage < 0) {
        return {};
    }
    return provider->imageRegions(providerPage, dpi);
}

void Document::trimPdfStore() const
{
    for (const auto& [id, provider] : m_pdfProviders) {
        Q_UNUSED(id);
        if (provider && provider->isValid()) {
            provider->trimStore();
        }
    }
}

int Document::pdfPageCount() const
{
    return pdfPageCount(QString());
}

int Document::pdfPageCount(const QString& sourceId) const
{
    PdfProvider* provider = providerForSource(sourceId);
    if (!provider || !provider->isValid()) {
        return 0;
    }
    return provider->pageCount();
}

QSizeF Document::pdfPageSize(int pageIndex) const
{
    return pdfPageSize(QString(), pageIndex);
}

QSizeF Document::pdfPageSize(const QString& sourceId, int pageIndex) const
{
    PdfProvider* provider = providerForSource(sourceId);
    if (!provider || !provider->isValid()) {
        return QSizeF();
    }
    const int providerPage = resolveSourcePageIndex(sourceId, pageIndex);
    if (providerPage < 0) {
        return QSizeF();
    }
    return provider->pageSize(providerPage);
}

int Document::notebookPageIndexForPdfPage(int pdfPageIndex) const
{
    // Use m_pagePdfIndex which maps UUID → PDF page index
    // We need to find the UUID with matching PDF page, then use pageIndexByUuid() for O(1) lookup
    // NOTE: This maps against the PRIMARY source only. Pages backed by a non-primary
    // source (m_pagePdfSource has a non-empty id) are skipped so that primary PDF
    // outline/link navigation stays correct even when extra-source pages are present.
    // Full multi-source resolution is a later plan (search/outline generalization).
    for (const auto& [uuid, pdfIdx] : m_pagePdfIndex) {
        if (pdfIdx == pdfPageIndex) {
            auto srcIt = m_pagePdfSource.find(uuid);
            if (srcIt != m_pagePdfSource.end() && !srcIt->second.isEmpty()) {
                continue;  // Not a primary-source page
            }
            // Found the UUID, use cached lookup for O(1) instead of indexOf() O(n)
            return pageIndexByUuid(uuid);
        }
    }
    return -1;  // Not found (PDF page not in notebook, or non-PDF document)
}

int Document::pdfPageIndexForNotebookPage(int notebookPageIndex) const
{
    // Bounds check
    if (notebookPageIndex < 0 || notebookPageIndex >= m_pageOrder.size()) {
        return -1;
    }
    
    // Get the UUID at this notebook page index
    QString uuid = m_pageOrder[notebookPageIndex];

    // Only primary-source pages participate in primary outline/link mapping.
    auto srcIt = m_pagePdfSource.find(uuid);
    if (srcIt != m_pagePdfSource.end() && !srcIt->second.isEmpty()) {
        return -1;  // Backed by a non-primary source
    }
    
    // Look up the PDF page index for this UUID
    auto it = m_pagePdfIndex.find(uuid);
    if (it != m_pagePdfIndex.end()) {
        return it->second;
    }
    
    return -1;  // Not a PDF page (blank or custom background)
}

bool Document::pdfBindingForNotebookPage(int notebookPageIndex, QString& outSourceId, int& outPdfPage) const
{
    // Read live Page fields so this is correct even for pages imported at runtime
    // (D1/D2), whose (sourceId, pdfPageNumber) may not yet be mirrored into the
    // manifest maps. Empty sourceId means the document's primary source.
    const Page* p = page(notebookPageIndex);
    if (!p || p->pdfPageNumber < 0) {
        return false;
    }
    outSourceId = p->pdfSourceId;
    outPdfPage = p->pdfPageNumber;
    return true;
}

int Document::notebookPageIndexForSourcePage(const QString& sourceId, int originalPage) const
{
    if (originalPage < 0) {
        return -1;
    }
    // Resolve via the manifest maps (kept in sync on insert/import/remove and keyed
    // by page uuid) plus the O(1) uuid->index cache, rather than page(i) which would
    // force-load every page (and its images) from disk just to navigate one link.
    // Matching by (source, original page number) means partial/reordered imports
    // resolve correctly; a destination not present in this document returns -1.
    for (const auto& [uuid, pdfIdx] : m_pagePdfIndex) {
        if (pdfIdx != originalPage) {
            continue;
        }
        auto srcIt = m_pagePdfSource.find(uuid);
        const QString pageSrc = (srcIt != m_pagePdfSource.end()) ? srcIt->second : QString();
        if (pageSrc == sourceId) {
            return pageIndexByUuid(uuid);
        }
    }
    return -1;
}

int Document::originalPageForProviderIndex(const QString& sourceId, int providerPage) const
{
    if (providerPage < 0) {
        return -1;
    }
    const PdfSource* s = pdfSourceById(sourceId);
    pdfPathForSource(sourceId);
    if (!s || !m_pdfProvidersUsingBundled.contains(s->id)) {
        return providerPage;
    }
    // Bundled mini-PDF: reverse the original->bundled page map.
    for (auto it = s->pageMap.constBegin(); it != s->pageMap.constEnd(); ++it) {
        if (it.value() == providerPage) {
            return it.key();
        }
    }
    return -1;
}

void Document::ensureAllPdfProvidersLoaded() const
{
    for (const PdfSource& s : m_pdfSources) {
        providerForSource(s.id);  // Open + cache; ignore unavailable sources.
    }
}

QString Document::pdfTitle() const
{
    PdfProvider* p = primaryProvider();
    if (!p || !p->isValid()) {
        return QString();
    }
    return p->title();
}

QString Document::pdfAuthor() const
{
    PdfProvider* p = primaryProvider();
    if (!p || !p->isValid()) {
        return QString();
    }
    return p->author();
}

bool Document::pdfHasOutline() const
{
    PdfProvider* p = primaryProvider();
    if (!p || !p->isValid()) {
        return false;
    }
    return p->hasOutline();
}

QVector<PdfOutlineItem> Document::pdfOutline() const
{
    PdfProvider* p = primaryProvider();
    if (!p || !p->isValid()) {
        return QVector<PdfOutlineItem>();
    }
    return p->outline();
}

// -------------------------------------------------------------------------
// OUT1: Multi-source outline aggregation + reusable source ordering/palette
// -------------------------------------------------------------------------

QStringList Document::sourceDisplayOrder() const
{
    QStringList order;
    QSet<QString> seen;

    // Primary always sorts first (represented by the empty id, matching page
    // storage where primary-backed pages carry an empty pdfSourceId).
    const bool hasPrimary = primarySource() != nullptr;
    if (hasPrimary) {
        order << QString();
        seen.insert(QString());
    }

    // Remaining sources by first appearance in page order.
    for (const QString& uuid : m_pageOrder) {
        if (m_pagePdfIndex.find(uuid) == m_pagePdfIndex.end()) {
            continue;  // not PDF-backed
        }
        auto it = m_pagePdfSource.find(uuid);
        const QString src = (it != m_pagePdfSource.end()) ? it->second : QString();
        if (src.isEmpty() && !hasPrimary) {
            continue;  // stray empty binding with no primary source
        }
        if (!seen.contains(src)) {
            seen.insert(src);
            order << src;
        }
    }
    return order;
}

int Document::paletteSlotForSource(const QString& sourceId) const
{
    return sourceDisplayOrder().indexOf(sourceId);
}

QString Document::sourceDisplayTitle(const QString& sourceId) const
{
    if (PdfProvider* p = providerForSource(sourceId)) {
        const QString t = p->title().trimmed();
        if (!t.isEmpty()) {
            return t;
        }
    }
    if (const PdfSource* s = pdfSourceById(sourceId)) {
        const QString path = !s->path.isEmpty() ? s->path : s->relativePath;
        if (!path.isEmpty()) {
            const QString base = QFileInfo(path).completeBaseName();
            if (!base.isEmpty()) {
                return base;
            }
        }
    }
    const int slot = paletteSlotForSource(sourceId);
    return QCoreApplication::translate("Document", "Source %1").arg(slot >= 0 ? slot + 1 : 1);
}

QVector<PdfOutlineItem> Document::aggregatedOutline() const
{
    const QStringList order = sourceDisplayOrder();
    if (order.isEmpty()) {
        return {};
    }

    // Collect the pruned/converted subtree for each contributing source.
    QVector<QPair<QString, QVector<PdfOutlineItem>>> contributions;
    for (const QString& src : order) {
        PdfProvider* p = providerForSource(src);
        if (!p || !p->isValid() || !p->hasOutline()) {
            continue;
        }
        const QVector<PdfOutlineItem> raw = p->outline();
        if (raw.isEmpty()) {
            continue;
        }

        // ORIGINAL pages of this source currently present in the document (sorted).
        QList<int> presentPages;
        for (const auto& [uuid, pdfIdx] : m_pagePdfIndex) {
            auto it = m_pagePdfSource.find(uuid);
            const QString pageSrc = (it != m_pagePdfSource.end()) ? it->second : QString();
            if (pageSrc == src) {
                presentPages.append(pdfIdx);
            }
        }
        if (presentPages.isEmpty()) {
            continue;  // none of this source's pages are in the document
        }
        std::sort(presentPages.begin(), presentPages.end());
        presentPages.erase(std::unique(presentPages.begin(), presentPages.end()),
                           presentPages.end());

        // All outline target pages (ORIGINAL space, sorted) so each entry's
        // covered page span is [target, nextTarget) - standard TOC semantics.
        QList<int> targets;
        std::function<void(const QVector<PdfOutlineItem>&)> gatherTargets =
            [&](const QVector<PdfOutlineItem>& items) {
                for (const PdfOutlineItem& it : items) {
                    if (it.targetPage >= 0) {
                        const int o = originalPageForProviderIndex(src, it.targetPage);
                        if (o >= 0) targets.append(o);
                    }
                    if (!it.children.isEmpty()) gatherTargets(it.children);
                }
            };
        gatherTargets(raw);
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

        auto isPresent = [&](int orig) -> bool {
            return orig >= 0 &&
                   std::binary_search(presentPages.begin(), presentPages.end(), orig);
        };
        auto coverageEnd = [&](int t) -> int {
            auto it = std::upper_bound(targets.begin(), targets.end(), t);
            return (it == targets.end()) ? std::numeric_limits<int>::max() : *it;
        };
        // Page this entry should navigate to: its exact target if imported, else the
        // first imported page within its covered span; -1 when nothing is present
        // (an inert header / greyed context entry).
        auto resolveNav = [&](int origTarget) -> int {
            if (origTarget < 0) return -1;
            if (isPresent(origTarget)) return origTarget;
            auto it = std::lower_bound(presentPages.begin(), presentPages.end(), origTarget);
            if (it != presentPages.end() && *it < coverageEnd(origTarget)) return *it;
            return -1;
        };
        auto navOf = [&](const PdfOutlineItem& it) -> int {
            return resolveNav(it.targetPage >= 0 ? originalPageForProviderIndex(src, it.targetPage) : -1);
        };
        // reaches: this entry resolves to a present page, or a descendant does.
        std::function<bool(const PdfOutlineItem&)> reaches =
            [&](const PdfOutlineItem& it) -> bool {
                if (navOf(it) >= 0) return true;
                for (const PdfOutlineItem& c : it.children) {
                    if (reaches(c)) return true;
                }
                return false;
            };
        // Unified prune + grey (Q13.6): keep a subtree that reaches a present page;
        // also keep an unresolved LEAF among present siblings (greyed context);
        // prune whole absent subtrees. Entries that reach a present page navigate
        // there (targetPage = resolved page); unresolved kept entries keep their
        // original target so computeUnavailableOutlinePages() greys them inert.
        std::function<QVector<PdfOutlineItem>(const QVector<PdfOutlineItem>&)> build =
            [&](const QVector<PdfOutlineItem>& items) -> QVector<PdfOutlineItem> {
                bool levelActive = false;
                for (const PdfOutlineItem& it : items) {
                    if (reaches(it)) { levelActive = true; break; }
                }
                QVector<PdfOutlineItem> out;
                for (const PdfOutlineItem& it : items) {
                    const bool itemReaches = reaches(it);
                    const bool keepAsContext = !itemReaches && it.children.isEmpty() && levelActive;
                    if (!itemReaches && !keepAsContext) {
                        continue;  // prune whole absent subtree
                    }
                    PdfOutlineItem copy = it;
                    copy.sourceId = src;
                    const int nav = navOf(it);
                    copy.targetPage = (nav >= 0)
                        ? nav
                        : (it.targetPage >= 0 ? originalPageForProviderIndex(src, it.targetPage) : -1);
                    copy.children = itemReaches ? build(it.children) : QVector<PdfOutlineItem>();
                    out.append(copy);
                }
                return out;
            };

        QVector<PdfOutlineItem> tree = build(raw);
        if (!tree.isEmpty()) {
            contributions.append({src, std::move(tree)});
        }
    }

    if (contributions.isEmpty()) {
        return {};
    }
    // Single contributor: no synthetic root, identical to the legacy single-PDF
    // panel. Multiple contributors: wrap each under a titled, non-navigable root.
    if (contributions.size() == 1) {
        return contributions.first().second;
    }

    QVector<PdfOutlineItem> result;
    result.reserve(contributions.size());
    for (auto& [src, tree] : contributions) {
        PdfOutlineItem root;
        root.title = sourceDisplayTitle(src);
        root.targetPage = -1;   // header, not navigable
        root.sourceId = src;
        root.isOpen = true;
        root.children = std::move(tree);
        result.append(std::move(root));
    }
    return result;
}

// =========================================================================
// Page Management (Task 1.2.5)
// =========================================================================

Page* Document::page(int index)
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return nullptr;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Check if already loaded
    auto it = m_loadedPages.find(uuid);
    if (it != m_loadedPages.end()) {
        return it->second.get();
    }
    
    // Load on demand
    if (!loadPageFromDisk(index)) {
        return nullptr;
    }
    
    // Use find() instead of [] to avoid inserting nullptr if something went wrong
    // (defensive programming - loadPageFromDisk should have inserted it)
    it = m_loadedPages.find(uuid);
    return it != m_loadedPages.end() ? it->second.get() : nullptr;
}

const Page* Document::page(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return nullptr;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Check if already loaded
    auto it = m_loadedPages.find(uuid);
    if (it != m_loadedPages.end()) {
        return it->second.get();
    }
    
    // Load on demand
    if (!loadPageFromDisk(index)) {
        return nullptr;
    }
    
    // Use find() instead of at() to avoid potential std::out_of_range exception
    // (defensive programming - loadPageFromDisk should have inserted it)
    it = m_loadedPages.find(uuid);
    return it != m_loadedPages.end() ? it->second.get() : nullptr;
}

// ===== Paged Mode Lazy Loading Accessors (Phase O1.7) =====

bool Document::isPageLoaded(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    QString uuid = m_pageOrder[index];
    return m_loadedPages.find(uuid) != m_loadedPages.end();
}

QVector<int> Document::loadedPageIndices() const
{
    QVector<int> result;
    result.reserve(static_cast<int>(m_loadedPages.size()));
    
    // Iterate through loaded pages and use cached UUID→index lookup
    // This is O(loaded) after cache is built (vs O(loaded * pageCount) before)
    for (const auto& [uuid, page] : m_loadedPages) {
        int idx = pageIndexByUuid(uuid);  // O(1) cached lookup
        if (idx >= 0) {
            result.append(idx);
        }
    }
    return result;
}

QString Document::pageUuidAt(int index) const
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return QString();
    }
    return m_pageOrder[index];
}

QSizeF Document::pageSizeAt(int index) const
{
    // Bounds check
    if (index < 0 || index >= m_pageOrder.size()) {
        return QSizeF();
    }
    
    // Use cached metadata (avoids loading the full page)
    QString uuid = m_pageOrder[index];
    auto it = m_pageMetadata.find(uuid);
    if (it != m_pageMetadata.end()) {
        return it->second;
    }
    
    // Fallback: load the page and get its size
    const Page* p = page(index);
    return p ? p->size : QSizeF();
}

void Document::setPageSize(int index, const QSizeF& size)
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return;
    }
    
    // Update the layout metadata so pageSizeAt() returns the new size
    QString uuid = m_pageOrder[index];
    m_pageMetadata[uuid] = size;
    
    // Update the actual page object if it is loaded in memory
    Page* p = page(index);
    if (p) {
        p->size = size;
        m_dirtyPages.insert(uuid);
    }
    
    markModified();
}

bool Document::loadPageFromDisk(int index) const
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    
    QFile file(pagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // File doesn't exist - check if we can synthesize a pristine PDF page
        auto pdfIt = m_pagePdfIndex.find(uuid);
        if (pdfIt != m_pagePdfIndex.end()) {
            // Synthesize pristine PDF page from manifest metadata
            auto page = std::make_unique<Page>();
            page->uuid = uuid;
            page->pageIndex = index;
            page->backgroundType = Page::BackgroundType::PDF;
            page->pdfPageNumber = pdfIt->second;
            // Resolve the page's PDF source (empty = primary).
            auto srcIt = m_pagePdfSource.find(uuid);
            const QString pageSourceId = (srcIt != m_pagePdfSource.end()) ? srcIt->second : QString();
            page->pdfSourceId = pageSourceId;
            
            // Get size from metadata
            auto sizeIt = m_pageMetadata.find(uuid);
            if (sizeIt != m_pageMetadata.end()) {
                page->size = sizeIt->second;
            } else {
                // Fallback to PDF page size if available (from the page's own source).
                // Bounds-check against the provider using the resolved (mini-PDF) index,
                // since bundled sources remap the original page number.
                const int providerPage = resolveSourcePageIndex(pageSourceId, pdfIt->second);
                if (providerPage >= 0 && providerPage < pdfPageCount(pageSourceId)) {
                    QSizeF pdfSize = pdfPageSize(pageSourceId, pdfIt->second);
                    qreal scale = 96.0 / 72.0;  // PDF points to 96 dpi
                    page->size = QSizeF(pdfSize.width() * scale, pdfSize.height() * scale);
                }
            }
            
            // Apply document defaults for colors/spacing
            page->backgroundColor = defaultBackgroundColor;
            page->gridColor = defaultGridColor;
            page->gridSpacing = defaultGridSpacing;
            page->lineSpacing = defaultLineSpacing;
            
            m_loadedPages[uuid] = std::move(page);
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Synthesized pristine PDF page" << index << "(" << uuid.left(8) << ")";
#endif
            return true;
        }
        
        // Not a PDF page and file doesn't exist - actual error
        qWarning() << "Cannot load page: file not found" << pagePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Cannot load page: JSON parse error" << parseError.errorString();
        return false;
    }
    
    auto page = Page::fromJson(jsonDoc.object());
    if (!page) {
        qWarning() << "Cannot load page: Page::fromJson failed";
        return false;
    }
    
    // Phase O2 (BF.3): Load image objects from assets folder.
    // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
    // We must call loadImages() to load image files into memory for rendering.
    int imagesLoaded = page->loadImages(m_bundlePath);
    if (imagesLoaded > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "loadPageFromDisk: Loaded" << imagesLoaded << "images for page" << index;
        #endif
    }
    
    // Phase O1.5: Update max object extent from loaded objects
    for (const auto& object : page->objects) {
        int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
        if (extent > m_maxObjectExtent) {
            m_maxObjectExtent = extent;
        }
    }
    
    Page* rawPagePtr = page.get();
    m_loadedPages[uuid] = std::move(page);
    
    // Load OCR sidecar data and materialize text objects
    loadPageOcr(rawPagePtr, uuid);
    materializeOcrTextObjects(rawPagePtr);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Loaded page" << index << "(" << uuid.left(8) << ") from disk";
#endif

    // Outline cache: in-memory page is now authoritative.
    refreshLinkOutlineFor(index);

    return true;
}

bool Document::savePage(int index)
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) {
        return false;  // Not loaded, nothing to save
    }
    
    // Ensure pages directory exists
    QDir().mkpath(m_bundlePath + "/pages");
    
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    QFile file(pagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save page:" << pagePath;
        return false;
    }
    
    QJsonDocument jsonDoc(it->second->toJson());
    file.write(jsonDoc.toJson(QJsonDocument::Compact));
    file.close();
    
    // Save OCR sidecar file
    savePageOcr(uuid, it->second.get());
    
    // Clear dirty flag
    m_dirtyPages.erase(uuid);
    
    // Update metadata
    m_pageMetadata[uuid] = it->second->size;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Saved page" << index << "(" << uuid.left(8) << ") to disk";
#endif

    // Outline cache: disk + in-memory agree now; refresh from memory.
    refreshLinkOutlineFor(index);

    return true;
}

void Document::evictPage(int index)
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return;
    }
    
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) {
        return;  // Not loaded, nothing to evict
    }
    
    // Save if dirty
    if (m_dirtyPages.count(uuid) > 0) {
        if (!savePage(index)) {
            qWarning() << "Failed to save page before eviction" << index;
            // Continue with eviction anyway to free memory
        }
    }
    
    // Remove from memory
    m_loadedPages.erase(it);
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Evicted page" << index << "(" << uuid.left(8) << ") from memory";
#endif
}

void Document::markPageDirty(int index)
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return;
    }
    QString uuid = m_pageOrder[index];
    m_dirtyPages.insert(uuid);
    markModified();
}

bool Document::isPageDirty(int index) const
{
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    QString uuid = m_pageOrder[index];
    return m_dirtyPages.count(uuid) > 0;
}

// =========================================================================
// UUID→Index Cache (Phase C.0.2)
// =========================================================================

void Document::rebuildUuidCache() const
{
    m_uuidToIndexCache.clear();
    
    // Build cache from m_pageOrder (no disk I/O needed)
    for (int i = 0; i < m_pageOrder.size(); i++) {
        const QString& uuid = m_pageOrder[i];
        if (!uuid.isEmpty()) {
            m_uuidToIndexCache[uuid] = i;
        }
    }
    
    m_uuidCacheDirty = false;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Rebuilt UUID cache with" << m_uuidToIndexCache.size() << "entries";
#endif
}

int Document::pageIndexByUuid(const QString& uuid) const
{
    if (uuid.isEmpty()) {
        return -1;
    }
    
    if (m_uuidCacheDirty) {
        rebuildUuidCache();  // O(n) but only once per page change
    }
    
    return m_uuidToIndexCache.value(uuid, -1);  // O(1)
}

void Document::invalidateUuidCache()
{
    m_uuidCacheDirty = true;
}

Page* Document::addPage()
{
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    
    // Use page's own UUID (generated in Page constructor)
    QString uuid = newPage->uuid;
    
    // Add to page order and metadata
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    
    // Store in loaded pages
    m_loadedPages[uuid] = std::move(newPage);
    
    // Mark as dirty
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();

    // Outline cache: new empty page contributes no entries, but keep the
    // cache consistent by explicitly registering the container.
    if (m_linkOutlineCacheReady) {
        m_pageOutline[static_cast<int>(m_pageOrder.size()) - 1] = {};
    }

    markModified();
    return pagePtr;
}

Page* Document::insertPage(int index)
{
    // Allow inserting at the end (index == size)
    if (index < 0 || index > m_pageOrder.size()) {
        return nullptr;
    }
    
    auto newPage = createDefaultPage();
    Page* pagePtr = newPage.get();
    
    // Use page's own UUID (generated in Page constructor)
    QString uuid = newPage->uuid;
    
    // Insert into page order
    m_pageOrder.insert(index, uuid);
    m_pageMetadata[uuid] = newPage->size;
    
    // Store in loaded pages
    m_loadedPages[uuid] = std::move(newPage);
    
    // Mark as dirty
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();

    // Outline cache: shift all entries >= index up by one, then insert
    // an empty vector at `index` for the newcomer.
    if (m_linkOutlineCacheReady) {
        std::map<int, QVector<LinkOutlineEntry>> shifted;
        for (auto& kv : m_pageOutline) {
            const int newKey = (kv.first >= index) ? (kv.first + 1) : kv.first;
            QVector<LinkOutlineEntry> v = std::move(kv.second);
            if (newKey != kv.first) {
                for (auto& e : v) e.pageIndex = newKey;
            }
            shifted[newKey] = std::move(v);
        }
        shifted[index] = {};
        m_pageOutline = std::move(shifted);
    }

    markModified();
    return pagePtr;
}

Page* Document::addPageForPdf(int pdfPageIndex)
{
    auto newPage = createDefaultPage();
    
    // Configure for PDF background
    newPage->backgroundType = Page::BackgroundType::PDF;
    newPage->pdfPageNumber = pdfPageIndex;
    
    // Set page size from PDF (convert from 72 dpi to 96 dpi)
    if (isPdfLoaded() && pdfPageIndex >= 0 && pdfPageIndex < pdfPageCount()) {
        QSizeF pdfSize = pdfPageSize(pdfPageIndex);
        // PDF points are at 72 dpi, convert to 96 dpi
        qreal scale = 96.0 / 72.0;
        newPage->size = QSizeF(pdfSize.width() * scale, pdfSize.height() * scale);
    }
    
    // Use lazy loading mode from the start
    QString uuid = newPage->uuid;
    Page* pagePtr = newPage.get();
    
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    m_pagePdfIndex[uuid] = pdfPageIndex;  // Track PDF page mapping
    m_loadedPages[uuid] = std::move(newPage);
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();

    if (m_linkOutlineCacheReady) {
        m_pageOutline[static_cast<int>(m_pageOrder.size()) - 1] = {};
    }

    markModified();
    return pagePtr;
}

bool Document::removePage(int index)
{
    // Cannot remove if index invalid
    if (index < 0 || index >= m_pageOrder.size()) {
        return false;
    }
    
    // Cannot remove the last page
    if (m_pageOrder.size() <= 1) {
        return false;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Remove from page order
    m_pageOrder.removeAt(index);
    
    // Evict from memory if loaded
    m_loadedPages.erase(uuid);
    
    // Remove from dirty tracking
    m_dirtyPages.erase(uuid);
    
    // Remove metadata
    m_pageMetadata.erase(uuid);
    
    // Remove PDF page index tracking
    m_pagePdfIndex.erase(uuid);
    m_pagePdfSource.erase(uuid);
    
    // Track for deletion on next save
    m_deletedPages.insert(uuid);
    
    invalidateUuidCache();

    // Outline cache: drop the page's contribution and renumber subsequent
    // entries down by 1 (dropLinkOutlineFor does both).
    dropLinkOutlineFor(index);

    markModified();
    return true;
}

bool Document::restorePageFromSnapshot(int index, const QJsonObject& pageJson)
{
    // Allow reinserting at the end (index == size).
    if (index < 0 || index > static_cast<int>(m_pageOrder.size())) {
        return false;
    }

    auto page = Page::fromJson(pageJson);
    if (!page) {
        return false;
    }

    const QString uuid = page->uuid;

    // Reinsert into page order at the requested position.
    m_pageOrder.insert(index, uuid);

    // Restore metadata + PDF-source mappings from the page's own fields.
    m_pageMetadata[uuid] = page->size;
    if (page->pdfPageNumber >= 0) {
        m_pagePdfIndex[uuid] = page->pdfPageNumber;
        if (!page->pdfSourceId.isEmpty()) {
            m_pagePdfSource[uuid] = page->pdfSourceId;
        }
    }

    // Cancel any pending on-disk deletion scheduled by removePage().
    m_deletedPages.erase(uuid);

    // Load images from the bundle. Unsaved images survive via the base64
    // fallback embedded in Page::toJson().
    if (!m_bundlePath.isEmpty()) {
        page->loadImages(m_bundlePath);
    }

    // Store the live page and mark it dirty so it re-persists on save.
    m_loadedPages[uuid] = std::move(page);
    m_dirtyPages.insert(uuid);

    invalidateUuidCache();

    // Outline cache: open a slot at `index` (shift entries >= index up by one,
    // mirroring insertPage()), then repopulate the restored page's own links.
    if (m_linkOutlineCacheReady) {
        std::map<int, QVector<LinkOutlineEntry>> shifted;
        for (auto& kv : m_pageOutline) {
            const int newKey = (kv.first >= index) ? (kv.first + 1) : kv.first;
            QVector<LinkOutlineEntry> v = std::move(kv.second);
            if (newKey != kv.first) {
                for (auto& e : v) e.pageIndex = newKey;
            }
            shifted[newKey] = std::move(v);
        }
        m_pageOutline = std::move(shifted);
        refreshLinkOutlineFor(index);
    }

    markModified();
    return true;
}

// =========================================================================
// Cross-document page import (Plan B)
// =========================================================================

QJsonObject Document::regeneratePageIds(const QJsonObject& pageJson,
                                        QHash<QString, QString>& pageMap,
                                        QHash<QString, QString>& objMap) const
{
    QJsonObject out = pageJson;

    // New page uuid. If importPagesFrom pre-assigned one (pass 1), reuse it so
    // in-set link targets can be remapped regardless of page order; otherwise
    // generate a fresh one (Plan B single-page behavior).
    const QString oldPageUuid = out.value("uuid").toString();
    QString newPageUuid;
    auto pageIt = pageMap.find(oldPageUuid);
    if (!oldPageUuid.isEmpty() && pageIt != pageMap.end()) {
        newPageUuid = pageIt.value();
    } else {
        newPageUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!oldPageUuid.isEmpty()) {
            pageMap.insert(oldPageUuid, newPageUuid);
        }
    }
    out["uuid"] = newPageUuid;

    // New layer ids. Stroke ids are intentionally preserved: they are
    // cross-document-unique UUIDs, are effectively page-scoped (OCR suppression
    // and undo are keyed per page), and keeping them preserves locked-OCR
    // sourceStrokeIds linkage without any remap.
    QJsonArray layers = out.value("layers").toArray();
    for (int i = 0; i < layers.size(); ++i) {
        QJsonObject layer = layers[i].toObject();
        layer["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        layers[i] = layer;
    }
    out["layers"] = layers;

    // New object ids.
    QJsonArray objects = out.value("objects").toArray();
    for (int i = 0; i < objects.size(); ++i) {
        QJsonObject obj = objects[i].toObject();
        const QString oldId = obj.value("id").toString();
        const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        obj["id"] = newId;
        if (!oldId.isEmpty()) {
            objMap.insert(oldId, newId);
        }
        objects[i] = obj;
    }
    out["objects"] = objects;

    return out;
}

void Document::copyImageAssets(Document* srcDoc, const QJsonObject& pageJson) const
{
    // If this document has no bundle yet (unsaved), we cannot copy files. Unsaved
    // source images travel via the embedded base64 in the page JSON and are
    // persisted on the next save of this document.
    if (!srcDoc || m_bundlePath.isEmpty()) {
        return;
    }

    const QString srcImagesDir = srcDoc->assetsImagePath();
    const QString destImagesDir = assetsImagePath();
    if (srcImagesDir.isEmpty() || destImagesDir.isEmpty()) {
        return;
    }

    const QJsonArray objects = pageJson.value("objects").toArray();
    bool ensuredDir = false;
    for (const QJsonValue& v : objects) {
        const QJsonObject obj = v.toObject();
        if (obj.value("type").toString() != QStringLiteral("image")) {
            continue;
        }
        const QString imagePath = obj.value("imagePath").toString();
        if (imagePath.isEmpty()) {
            continue;  // Unsaved image: carried by embedded base64.
        }

        const QString destFile = destImagesDir + "/" + imagePath;
        if (QFile::exists(destFile)) {
            continue;  // Already present (content-hash filenames make this deduped).
        }
        const QString srcFile = srcImagesDir + "/" + imagePath;
        if (!QFile::exists(srcFile)) {
            continue;  // Broken source reference; base64 (if present) covers it.
        }

        if (!ensuredDir) {
            QDir().mkpath(destImagesDir);
            ensuredDir = true;
        }
        QFile::copy(srcFile, destFile);
    }
}

QString Document::ensureImportedPdfSourceId(Document* srcDoc, const QString& originSourceId)
{
    if (!srcDoc) {
        return QString();
    }

    const PdfSource* os = srcDoc->pdfSourceById(originSourceId);
    if (!os) {
        return QString();  // Origin source unresolved; page stays blank.
    }

    // Reference the origin's ORIGINAL external PDF, never its bundled mini-PDF:
    // imported pages keep their original page numbers, so pointing at a compact
    // mini-PDF (a subset, re-indexed) would make them render out of range. Identity
    // (hash+size) still dedups against a matching full PDF already in this document
    // (e.g. the destination's own copy), and if no local file exists the page shows
    // a relink prompt instead of looping on an unrenderable reference.
    const QString path = os->path;
    const QString hash = os->hash;
    const qint64 size = os->size;

    // Broken/dismissed origin with neither identity nor path: nothing to
    // reference. Leave the page unresolved (blank) rather than registering a
    // junk empty source that would accumulate in pdf_sources on every import.
    if (hash.isEmpty() && path.isEmpty()) {
        return QString();
    }

    QString destId;
    if (!hash.isEmpty()) {
        // registerSource dedups by hash+size, returning an existing id on match.
        destId = registerSource(path, hash, size, /*bundled*/ false);
    } else {
        // Hashless origin (e.g. legacy doc): dedup on absolute path so repeated
        // imports of the same file don't proliferate sources.
        for (const PdfSource& s : m_pdfSources) {
            if (!s.path.isEmpty() && s.path == path) {
                destId = s.id;
                break;
            }
        }
        if (destId.isEmpty()) {
            destId = registerSource(path, hash, size, /*bundled*/ false);
        }
    }

    // A page that deduped onto the document's own base (primary) PDF must carry an
    // empty source id: unreferencedSourceIds() only counts empty-id pages as
    // referencing the primary, so an explicit primary id would cause it to be pruned
    // on save. Imported sources are non-primary, so they keep their explicit id and
    // get materialized into a mini-PDF rather than promoted to the main PDF.
    const PdfSource* dst = pdfSourceById(destId);
    if (dst && dst->primary) {
        return QString();
    }
    return destId;
}

void Document::remapImportedPdfSource(QJsonObject& pageJson, Document* srcDoc)
{
    if (pageJson.value("backgroundType").toInt(0) !=
        static_cast<int>(Page::BackgroundType::PDF)) {
        return;
    }
    if (pageJson.value("pdfPageNumber").toInt(-1) < 0) {
        return;
    }

    const QString originId = pageJson.value("pdfSourceId").toString();
    const QString destId = ensureImportedPdfSourceId(srcDoc, originId);

    if (destId.isEmpty()) {
        // Primary or unresolved: match how Page::toJson omits the key for primary
        // pages, and clear any stale origin id.
        pageJson.remove("pdfSourceId");
    } else {
        pageJson["pdfSourceId"] = destId;
    }
}

void Document::copyMarkdownNotes(Document* srcDoc, const QJsonObject& pageJson) const
{
    // No bundle to copy into (unsaved destination): notes cannot travel yet.
    // Same limitation as copyImageAssets; the real UI operates on saved docs.
    if (!srcDoc || m_bundlePath.isEmpty()) {
        return;
    }

    // Resolve (and create) the note dirs lazily: only once a markdown slot with
    // a real noteId is found. This keeps the common no-notes import from
    // creating empty assets/notes dirs in either bundle (notesPath() mkpaths).
    QString srcNotes;
    QString destNotes;

    const QJsonArray objects = pageJson.value("objects").toArray();
    for (const QJsonValue& v : objects) {
        const QJsonObject obj = v.toObject();
        if (obj.value("type").toString() != QStringLiteral("link")) {
            continue;
        }
        const QJsonArray slotArr = obj.value("slots").toArray();
        for (const QJsonValue& sv : slotArr) {
            const QJsonObject slot = sv.toObject();
            if (slot.value("type").toString() != QStringLiteral("markdown")) {
                continue;
            }
            const QString noteId = slot.value("noteId").toString();
            if (noteId.isEmpty()) {
                continue;
            }
            if (destNotes.isEmpty()) {
                srcNotes = srcDoc->notesPath();
                destNotes = notesPath();  // Creates assets/notes if missing.
                if (srcNotes.isEmpty() || destNotes.isEmpty()) {
                    return;
                }
            }
            const QString destFile = destNotes + "/" + noteId + ".md";
            if (QFile::exists(destFile)) {
                continue;  // Dedup by id: note already present.
            }
            const QString srcFile = srcNotes + "/" + noteId + ".md";
            if (QFile::exists(srcFile)) {
                QFile::copy(srcFile, destFile);
            }
        }
    }
}

void Document::remapImportedLinkTargets(QJsonObject& pageJson,
                                        const QHash<QString, QString>& pageUuidMap) const
{
    QJsonArray objects = pageJson.value("objects").toArray();
    bool objectsChanged = false;

    for (int i = 0; i < objects.size(); ++i) {
        QJsonObject obj = objects[i].toObject();
        if (obj.value("type").toString() != QStringLiteral("link")) {
            continue;
        }

        QJsonArray slotArr = obj.value("slots").toArray();
        bool slotsChanged = false;

        for (int j = 0; j < slotArr.size(); ++j) {
            QJsonObject slot = slotArr[j].toObject();
            if (slot.value("type").toString() != QStringLiteral("position")) {
                continue;  // url / markdown / empty are left untouched.
            }

            if (slot.value("edgeless").toBool()) {
                // Edgeless tile coordinates cannot be validly remapped into
                // another document: make the slot inert.
                slotArr[j] = QJsonObject{{"type", "empty"}};
                slotsChanged = true;
                continue;
            }

            const QString targetUuid = slot.value("pageUuid").toString();
            auto it = pageUuidMap.find(targetUuid);
            if (it != pageUuidMap.end()) {
                // Target page is inside the copied set: repoint to its new uuid.
                slot["pageUuid"] = it.value();
                slotArr[j] = slot;
            } else {
                // Target is outside the set / another document: make inert but
                // keep the LinkObject itself (description, icon, other slots).
                slotArr[j] = QJsonObject{{"type", "empty"}};
            }
            slotsChanged = true;
        }

        if (slotsChanged) {
            obj["slots"] = slotArr;
            objects[i] = obj;
            objectsChanged = true;
        }
    }

    if (objectsChanged) {
        pageJson["objects"] = objects;
    }
}

PageImportResult Document::importPagesFrom(Document* srcDoc, const QStringList& srcPageUuids, int destIndex)
{
    PageImportResult result;
    if (!srcDoc || srcPageUuids.isEmpty()) {
        return result;
    }

    // Clamp the insertion point into [0, pageCount()].
    const int maxIndex = static_cast<int>(m_pageOrder.size());
    if (destIndex < 0) destIndex = 0;
    if (destIndex > maxIndex) destIndex = maxIndex;

    // Validate + preserve order, dropping duplicates: a repeated uuid would be
    // assigned the same pre-seeded page uuid in pass 1 and then copied twice in
    // pass 2, inserting two pages that share one uuid and corrupting page order.
    QStringList validUuids;
    QSet<QString> seenUuids;
    for (const QString& u : srcPageUuids) {
        if (!seenUuids.contains(u) && srcDoc->pageIndexByUuid(u) >= 0) {
            seenUuids.insert(u);
            validUuids.append(u);
        }
    }
    if (validUuids.isEmpty()) {
        return result;
    }

    // Pass 1: pre-assign every new page uuid so in-set link targets can be
    // remapped regardless of page order (a link on the first page may target
    // the last page). regeneratePageIds reuses these pre-seeded mappings.
    for (const QString& u : validUuids) {
        result.pageUuidMap.insert(u, QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    // Pass 2: per-page deep copy.
    int inserted = 0;
    for (const QString& srcUuid : validUuids) {
        Page* srcPage = srcDoc->page(srcDoc->pageIndexByUuid(srcUuid));  // Loads page + images.
        if (!srcPage) {
            continue;
        }

        // Serialize the source page. Persisted images serialize as imagePath +
        // imageHash; unsaved images embed base64 (recovery fallback).
        QJsonObject json = srcPage->toJson();

        // Copy referenced on-disk image assets into this document's store BEFORE
        // insertion, so restorePageFromSnapshot's loadImages() can resolve them.
        copyImageAssets(srcDoc, json);

        // Plan B-pdf: resolve the page's PDF source into THIS document's registry
        // (dedup by identity, else register an external reference) and rewrite
        // its pdfSourceId so the copied page renders in the destination.
        remapImportedPdfSource(json, srcDoc);

        // Plan B-links: copy referenced markdown notes into this bundle (dedup by
        // id; noteId stays stable so no repoint is needed).
        copyMarkdownNotes(srcDoc, json);

        // Regenerate ids so the copy is fully independent of the source (reuses
        // the page uuid pre-assigned in pass 1).
        QJsonObject finalJson = regeneratePageIds(json, result.pageUuidMap, result.objectIdMap);

        // Plan B-links: remap position-link targets (in-set -> new page uuid;
        // out-of-set / edgeless -> inert). Runs after regen because the full
        // pageUuidMap is now known.
        remapImportedLinkTargets(finalJson, result.pageUuidMap);

        if (restorePageFromSnapshot(destIndex + inserted, finalJson)) {
            if (result.destStartIndex < 0) {
                result.destStartIndex = destIndex;
            }
            result.insertedPageJson.append(finalJson);
            ++inserted;
        }
    }

    if (!result.insertedPageJson.isEmpty()) {
        markModified();
    }
    return result;
}

bool Document::movePage(int from, int to)
{
    int count = static_cast<int>(m_pageOrder.size());
    
    // Validate indices
    if (from < 0 || from >= count || to < 0 || to >= count) {
        return false;
    }
    
    // No-op if same position
    if (from == to) {
        return true;
    }
    
    // Just reorder the UUID list - no file changes needed!
    QString uuid = m_pageOrder[from];
    m_pageOrder.removeAt(from);
    m_pageOrder.insert(to, uuid);
    
    invalidateUuidCache();

    // Outline cache: page indices are re-keyed; rebuild the map by
    // remapping old → new indices.  Cheap because values are moved, not
    // recomputed.
    if (m_linkOutlineCacheReady) {
        const int lo = qMin(from, to);
        const int hi = qMax(from, to);
        std::map<int, QVector<LinkOutlineEntry>> remapped;
        for (auto& kv : m_pageOutline) {
            int oldIdx = kv.first;
            int newIdx = oldIdx;
            if (oldIdx < lo || oldIdx > hi) {
                newIdx = oldIdx;
            } else if (oldIdx == from) {
                newIdx = to;
            } else if (from < to) {   // moved forward: pages in (from, to] shift left
                newIdx = oldIdx - 1;
            } else {                  // moved backward: pages in [to, from) shift right
                newIdx = oldIdx + 1;
            }
            QVector<LinkOutlineEntry> v = std::move(kv.second);
            if (newIdx != oldIdx) {
                for (auto& e : v) e.pageIndex = newIdx;
            }
            remapped[newIdx] = std::move(v);
        }
        m_pageOutline = std::move(remapped);
    }

    markModified();
    return true;
}

Page* Document::edgelessPage()
{
    if (mode != Mode::Edgeless) {
        return nullptr;
    }
    
    // For compatibility, return origin tile (0,0)
    // Creates it if doesn't exist
    return getOrCreateTile(0, 0);
}

const Page* Document::edgelessPage() const
{
    if (mode != Mode::Edgeless) {
        return nullptr;
    }
    // Const version uses getTile (doesn't create)
    return getTile(0, 0);
}

void Document::ensureMinimumPages()
{
    // Check if we already have pages
    if (!m_pageOrder.isEmpty()) {
        return;
    }
    
    auto newPage = createDefaultPage();
    
    // For edgeless mode, mark the page as unbounded
    if (mode == Mode::Edgeless) {
        // Edgeless pages have no fixed size (effectively infinite)
        // We use a large default but it can extend beyond
        newPage->size = QSizeF(4096, 4096);
    }
    
    // Use lazy loading mode from the start
    QString uuid = newPage->uuid;
    m_pageOrder.append(uuid);
    m_pageMetadata[uuid] = newPage->size;
    m_loadedPages[uuid] = std::move(newPage);
    m_dirtyPages.insert(uuid);
    invalidateUuidCache();
}

void Document::createPagesForPdf()
{
    // Clear existing pages (lazy loading structures)
    m_pageOrder.clear();
    m_pageMetadata.clear();
    m_pagePdfIndex.clear();
    m_pagePdfSource.clear();
    m_loadedPages.clear();
    m_dirtyPages.clear();
    invalidateUuidCache();

    // Outline cache is keyed by page index; wiping the order invalidates
    // every entry, so rebuild from scratch on next enumerate.
    clearLinkOutlineCache();
    
    if (!isPdfLoaded()) {
        // No PDF loaded, create a single default page
        ensureMinimumPages();
        return;
    }
    
    // Create one page per PDF page
    int count = pdfPageCount();
    m_pageOrder.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        addPageForPdf(i);
    }
    
    // Ensure at least one page
    if (m_pageOrder.isEmpty()) {
        ensureMinimumPages();
    }
    
    // Don't mark as modified since this is initial creation
    modified = false;
}

std::unique_ptr<Page> Document::createDefaultPage()
{
    auto page = std::make_unique<Page>();
    
    // Apply document defaults
    page->size = defaultPageSize;
    page->backgroundType = defaultBackgroundType;
    page->backgroundColor = defaultBackgroundColor;
    page->gridColor = defaultGridColor;
    page->gridSpacing = defaultGridSpacing;
    page->lineSpacing = defaultLineSpacing;
    
    return page;
}

void Document::loadAllEvictedTiles()
{
    // CR-L13: Load all tiles that exist on disk but aren't in memory.
    // This ensures destructive layer operations affect ALL tiles.
    
    if (!m_lazyLoadEnabled) {
        return;  // No evicted tiles if lazy loading isn't enabled
    }
    
    // Copy tile index since loadTileFromDisk modifies m_tiles
    std::set<TileCoord> tilesToLoad;
    for (const auto& coord : m_tileIndex) {
        if (m_tiles.find(coord) == m_tiles.end()) {
            tilesToLoad.insert(coord);
        }
    }
    
    if (!tilesToLoad.empty()) {
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "CR-L13: Loading" << tilesToLoad.size() << "evicted tiles for layer operation";
#endif
    }
    
    for (const auto& coord : tilesToLoad) {
        loadTileFromDisk(coord);
    }
}

// =========================================================================
// Edgeless Tile Management (Phase E1)
// =========================================================================

Document::TileCoord Document::tileCoordForPoint(QPointF docPt) const
{
    int tx = static_cast<int>(std::floor(docPt.x() / EDGELESS_TILE_SIZE));
    int ty = static_cast<int>(std::floor(docPt.y() / EDGELESS_TILE_SIZE));
    return {tx, ty};
}

Page* Document::getTile(int tx, int ty) const
{
    TileCoord coord(tx, ty);
    
    // 1. Check if already in memory
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end()) {
        return it->second.get();
    }
    
    // 2. If lazy loading enabled, try to load from disk
    // m_tiles is mutable, so this works on const Document
    if (m_lazyLoadEnabled && m_tileIndex.count(coord) > 0) {
        if (loadTileFromDisk(coord)) {
            // Phase 5.6.5: No sync needed - loadTileFromDisk reconstructs layers from manifest
            return m_tiles.at(coord).get();
        }
    }
    
    // 3. Tile doesn't exist
    return nullptr;
}

Page* Document::getOrCreateTile(int tx, int ty)
{
    TileCoord coord(tx, ty);
    
    // 1. Check if already in memory
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end()) {
        return it->second.get();
    }
    
    // 2. If lazy loading enabled, try to load from disk
    if (m_lazyLoadEnabled && m_tileIndex.count(coord) > 0) {
        if (loadTileFromDisk(coord)) {
            // Phase 5.6.5: No sync needed - loadTileFromDisk reconstructs layers from manifest
            return m_tiles.at(coord).get();
        }
    }
    
    // 3. Create new tile
    auto tile = std::make_unique<Page>();
    tile->size = QSizeF(EDGELESS_TILE_SIZE, EDGELESS_TILE_SIZE);
    tile->backgroundType = defaultBackgroundType;
    tile->backgroundColor = defaultBackgroundColor;
    tile->gridColor = defaultGridColor;
    tile->gridSpacing = defaultGridSpacing;
    tile->lineSpacing = defaultLineSpacing;
    
    // CR-8: Removed tile coord storage in pageIndex/pdfPageNumber - it was never read.
    // Tile coordinate is already the map key, no need to duplicate in Page.
    
    // Phase 5.6.6: Initialize tile layer structure from manifest
    if (isEdgeless() && !m_edgelessLayers.empty()) {
        tile->vectorLayers.clear();  // Clear default layer from Page constructor
        for (const auto& layerDef : m_edgelessLayers) {
            auto layer = std::make_unique<VectorLayer>(layerDef.name);
            layer->id = layerDef.id;
            layer->visible = layerDef.visible;
            layer->opacity = layerDef.opacity;
            layer->locked = layerDef.locked;
            tile->vectorLayers.push_back(std::move(layer));
        }
        tile->activeLayerIndex = m_edgelessActiveLayerIndex;
    }
    
    auto [insertIt, inserted] = m_tiles.emplace(coord, std::move(tile));
    ++m_tileLoadVersion;

    // Mark new tile as dirty (needs saving)
    m_dirtyTiles.insert(coord);
    markModified();
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document: Created tile at (" << tx << "," << ty << ") total tiles:" << m_tiles.size();
#endif
    
    return insertIt->second.get();
}

QVector<Document::TileCoord> Document::tilesInRect(QRectF docRect) const
{
    QVector<TileCoord> result;
    
    // Calculate tile range
    int minTx = static_cast<int>(std::floor(docRect.left() / EDGELESS_TILE_SIZE));
    int maxTx = static_cast<int>(std::floor(docRect.right() / EDGELESS_TILE_SIZE));
    int minTy = static_cast<int>(std::floor(docRect.top() / EDGELESS_TILE_SIZE));
    int maxTy = static_cast<int>(std::floor(docRect.bottom() / EDGELESS_TILE_SIZE));
    
    // Return all coordinates in range (even if tiles don't exist yet)
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            result.append({tx, ty});
        }
    }
    
    return result;
}

QVector<Document::TileCoord> Document::allTileCoords() const
{
    QVector<TileCoord> result;
    result.reserve(static_cast<int>(m_tiles.size()));
    
    for (const auto& pair : m_tiles) {
        result.append(pair.first);
    }
    
    return result;
}

void Document::removeTileIfEmpty(int tx, int ty)
{
    TileCoord coord(tx, ty);
    auto it = m_tiles.find(coord);
    
    if (it == m_tiles.end()) {
        return;  // Tile doesn't exist in memory
    }
    
    Page* tile = it->second.get();
    
    // Use Page::hasContent() to check if tile has any strokes or objects
    if (!tile->hasContent()) {
        // Remove from memory
        m_tiles.erase(it);
        ++m_tileLoadVersion;

        // Remove from dirty tracking (don't need to save an empty tile)
        m_dirtyTiles.erase(coord);
        
        // Track for deletion from disk on next saveBundle()
        // If tile was in m_tileIndex, it exists on disk and needs deletion
        if (m_tileIndex.count(coord) > 0) {
            m_deletedTiles.insert(coord);
            m_tileIndex.erase(coord);
        }

        // Outline cache: tile no longer exists in any form.
        dropLinkOutlineFor(coord);

        markModified();
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Document: Removed empty tile at (" << tx << "," << ty << ") remaining tiles:" << m_tiles.size();
#endif
    }
}

// =========================================================================
// Object Extent Tracking (Phase O1.5)
// =========================================================================

void Document::updateMaxObjectExtent(const InsertedObject* obj)
{
    if (!obj) return;
    
    // Get the largest dimension of this object
    int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
    
    // Update maximum if this object is larger
    if (extent > m_maxObjectExtent) {
        m_maxObjectExtent = extent;
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Document: Updated max object extent to" << m_maxObjectExtent;
#endif
    }
}

void Document::recalculateMaxObjectExtent()
{
    int newMax = 0;
    
    if (isEdgeless()) {
        // Scan all loaded tiles
        for (const auto& pair : m_tiles) {
            Page* tile = pair.second.get();
            for (const auto& obj : tile->objects) {
                int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
                newMax = qMax(newMax, extent);
            }
        }
        
        // Note: Evicted tiles are not scanned. This is acceptable because:
        // - Evicted tiles will be loaded when viewport moves to them
        // - When loaded, their objects will update maxObjectExtent via addObject
        // - Worst case: margin is temporarily too small until tiles are loaded
    } else {
        // Scan loaded pages (lazy loading mode)
        for (const auto& pair : m_loadedPages) {
            Page* page = pair.second.get();
            for (const auto& obj : page->objects) {
                int extent = static_cast<int>(qMax(obj->size.width(), obj->size.height()));
                newMax = qMax(newMax, extent);
            }
        }
    }
    
    m_maxObjectExtent = newMax;
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document: Recalculated max object extent =" << m_maxObjectExtent;
#endif
}

// =========================================================================
// Bookmarks (Task 1.2.6)
// =========================================================================

QVector<Document::Bookmark> Document::getBookmarks() const
{
    QVector<Bookmark> result;
    
    int count = pageCount();
    for (int i = 0; i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            result.append({i, p->bookmarkLabel});
        }
    }
    
    return result;
}

void Document::setBookmark(int pageIndex, const QString& label)
{
    Page* p = page(pageIndex);
    if (!p) return;
    
    p->isBookmarked = true;
    
    if (label.isEmpty()) {
        // Generate default label
        p->bookmarkLabel = QStringLiteral("Bookmark %1").arg(pageIndex + 1);
    } else {
        p->bookmarkLabel = label;
    }
    
    markModified();
}

void Document::removeBookmark(int pageIndex)
{
    Page* p = page(pageIndex);
    if (!p || !p->isBookmarked) return;
    
    p->isBookmarked = false;
    p->bookmarkLabel.clear();
    markModified();
}

bool Document::hasBookmark(int pageIndex) const
{
    const Page* p = page(pageIndex);
    return p && p->isBookmarked;
}

QString Document::bookmarkLabel(int pageIndex) const
{
    const Page* p = page(pageIndex);
    if (!p || !p->isBookmarked) return QString();
    return p->bookmarkLabel;
}

int Document::nextBookmark(int fromPage) const
{
    int count = pageCount();
    if (count == 0) return -1;
    
    // Search from fromPage+1 to end
    for (int i = fromPage + 1; i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from 0 to fromPage
    for (int i = 0; i <= fromPage && i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    return -1; // No bookmarks found
}

int Document::prevBookmark(int fromPage) const
{
    int count = pageCount();
    if (count == 0) return -1;
    
    // Search from fromPage-1 down to 0
    for (int i = fromPage - 1; i >= 0; --i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    // Wrap around: search from end down to fromPage
    for (int i = count - 1; i >= fromPage && i >= 0; --i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            return i;
        }
    }
    
    return -1; // No bookmarks found
}

bool Document::toggleBookmark(int pageIndex, const QString& label)
{
    if (hasBookmark(pageIndex)) {
        removeBookmark(pageIndex);
        return false; // Removed
    } else {
        setBookmark(pageIndex, label);
        return true; // Added
    }
}

int Document::bookmarkCount() const
{
    int result = 0;
    int count = pageCount();
    for (int i = 0; i < count; ++i) {
        const Page* p = page(i);
        if (p && p->isBookmarked) {
            ++result;
        }
    }
    return result;
}

// =========================================================================
// Serialization (Task 1.2.7)
// =========================================================================

QJsonObject Document::toJson() const
{
    QJsonObject obj;
    
    // Bundle format version (integer, for forward compatibility checks)
    obj["bundle_format_version"] = BUNDLE_FORMAT_VERSION;
    
    // Identity
    obj["notebook_id"] = id;
    obj["name"] = name;
    obj["author"] = author;
    obj["created"] = created.toString(Qt::ISODate);
    obj["last_modified"] = lastModified.toString(Qt::ISODate);
    
    // Mode
    obj["mode"] = modeToString(mode);
    
    // PDF reference (path only, provider is runtime).
    // The primary source (flagged primary) is mirrored to the legacy top-level keys so
    // that older builds can still open the document. When more than one source exists, the
    // full multi-source list is additionally written to pdf_sources[]. Single-PDF
    // documents therefore serialize byte-identically to the pre-multi-source format.
    if (const PdfSource* primary = primarySource()) {
        obj["pdf_path"] = primary->path;
        if (!primary->hash.isEmpty()) {
            obj["pdf_hash"] = primary->hash;
        }
        if (primary->size > 0) {
            obj["pdf_size"] = primary->size;
        }
    } else {
        obj["pdf_path"] = QString();
    }

    // Write the full source list when more than one source exists, OR when any
    // page explicitly references a non-primary source id. The latter guards the
    // case where pruning has reduced the registry to a single *promoted* source
    // (the original primary was deleted): its id must be preserved so its pages
    // still resolve on reload instead of getting a freshly-synthesized id.
    const bool writeSourceList = m_pdfSources.size() > 1 || !m_pagePdfSource.empty();
    if (writeSourceList) {
        QJsonArray sourcesArray;
        for (const PdfSource& s : m_pdfSources) {
            QJsonObject sObj;
            sObj["id"] = s.id;
            sObj["path"] = s.path;
            if (!s.relativePath.isEmpty()) sObj["relative_path"] = s.relativePath;
            if (!s.hash.isEmpty()) sObj["hash"] = s.hash;
            if (s.size > 0) sObj["size"] = s.size;
            if (s.bundled) {
                sObj["bundled"] = true;
                if (!s.bundledFile.isEmpty()) sObj["bundled_file"] = s.bundledFile;
                if (!s.pageMap.isEmpty()) {
                    QJsonObject mapObj;
                    for (auto it = s.pageMap.constBegin(); it != s.pageMap.constEnd(); ++it) {
                        mapObj[QString::number(it.key())] = it.value();
                    }
                    sObj["page_map"] = mapObj;
                }
            }
            sourcesArray.append(sObj);
        }
        obj["pdf_sources"] = sourcesArray;
        // Authoritative primary marker: the id of the flagged primary source, or an
        // empty string when the document has no primary (import-only). Its presence
        // tells the loader that primary status is explicit (an empty value means
        // "genuinely no primary", not "old format"). Absent only in pre-fix docs,
        // where the loader falls back to treating the front source as primary.
        const PdfSource* primary = primarySource();
        obj["pdf_primary_id"] = primary ? primary->id : QString();
    }
    
    // State
    obj["last_accessed_page"] = lastAccessedPage;
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Document::toJson: lastAccessedPage =" << lastAccessedPage;
#endif
    
    // Default background settings
    obj["default_background"] = defaultBackgroundToJson();

    // OCR settings
    if (!ocrLanguage.isEmpty())
        obj["ocr_language"] = ocrLanguage;
    if (ocrSnapToBackground)
        obj["ocr_snap_to_background"] = true;

    // PDF display overrides (written only when overridden; absent = inherit global)
    if (pdfInvertDarkOverride >= 0)
        obj["pdf_invert_dark"] = (pdfInvertDarkOverride == 1);
    if (pdfInvertIncludeImagesOverride >= 0)
        obj["pdf_invert_include_images"] = (pdfInvertIncludeImagesOverride == 1);
    
    // Page count (for quick info without loading pages)
    obj["page_count"] = pageCount();
    
    return obj;
}

std::unique_ptr<Document> Document::fromJson(const QJsonObject& obj)
{
    auto doc = std::make_unique<Document>();
    
    // Clear the auto-generated ID, we'll load it from JSON
    doc->id = obj["notebook_id"].toString();
    if (doc->id.isEmpty()) {
        // Generate new ID if not present (legacy format)
        doc->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    // NOTE: format_version is no longer read - use bundle_format_version instead
    // Old files may have format_version but it's ignored for backward compatibility
    
    // Identity
    doc->name = obj["name"].toString();
    doc->author = obj["author"].toString();
    
    // Timestamps
    QString createdStr = obj["created"].toString();
    if (!createdStr.isEmpty()) {
        doc->created = QDateTime::fromString(createdStr, Qt::ISODate);
    }
    QString modifiedStr = obj["last_modified"].toString();
    if (!modifiedStr.isEmpty()) {
        doc->lastModified = QDateTime::fromString(modifiedStr, Qt::ISODate);
    }
    
    // Mode
    doc->mode = stringToMode(obj["mode"].toString("paged"));
    
    // PDF reference (don't load yet, just store paths).
    // Prefer the multi-source pdf_sources[] when present; otherwise synthesize a single
    // primary source from the legacy top-level keys (with a fresh id).
    if (obj.contains("pdf_sources") && obj["pdf_sources"].isArray()) {
        QJsonArray sourcesArray = obj["pdf_sources"].toArray();
        for (const QJsonValue& v : sourcesArray) {
            QJsonObject sObj = v.toObject();
            PdfSource s;
            s.id = sObj["id"].toString();
            if (s.id.isEmpty()) {
                s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            s.path = sObj["path"].toString();
            s.relativePath = sObj["relative_path"].toString();
            s.hash = sObj["hash"].toString();
            s.size = sObj["size"].toVariant().toLongLong();
            s.bundled = sObj["bundled"].toBool(false);
            s.bundledFile = sObj["bundled_file"].toString();
            if (sObj.contains("page_map")) {
                QJsonObject mapObj = sObj["page_map"].toObject();
                for (auto it = mapObj.begin(); it != mapObj.end(); ++it) {
                    s.pageMap.insert(it.key().toInt(), it.value().toInt());
                }
            }
            doc->m_pdfSources.push_back(s);
        }

        // Resolve which source is primary. pdf_primary_id is authoritative when
        // present (empty value => genuinely no primary, e.g. an import-only doc).
        // When the key is absent (docs written before the explicit-primary change),
        // fall back to the historical convention that the front source is primary.
        if (!doc->m_pdfSources.empty()) {
            if (obj.contains("pdf_primary_id")) {
                const QString primaryId = obj["pdf_primary_id"].toString();
                for (PdfSource& s : doc->m_pdfSources) {
                    s.primary = (!primaryId.isEmpty() && s.id == primaryId);
                }
            } else {
                doc->m_pdfSources.front().primary = true;
            }
        }
    }
    if (doc->m_pdfSources.empty()) {
        // Legacy / single-PDF: synthesize the primary from top-level keys.
        QString legacyPath = obj["pdf_path"].toString();
        QString legacyHash = obj["pdf_hash"].toString();
        qint64 legacySize = obj["pdf_size"].toVariant().toLongLong();
        QString legacyRel = obj["pdf_relative_path"].toString();  // Phase SHARE
        if (!legacyPath.isEmpty() || !legacyRel.isEmpty()) {
            PdfSource s;
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            s.path = legacyPath;
            s.relativePath = legacyRel;
            s.hash = legacyHash;
            s.size = legacySize;
            s.primary = true;  // The document's own base PDF.
            doc->m_pdfSources.push_back(s);
        }
    } else if (const PdfSource* p = doc->primarySource()) {
        // Ensure the primary picks up the legacy relative-path mirror if the array
        // entry omitted it (older multi-source writes).
        if (p->relativePath.isEmpty()) {
            const_cast<PdfSource*>(p)->relativePath = obj["pdf_relative_path"].toString();
        }
    }
    
    // State
    doc->lastAccessedPage = obj["last_accessed_page"].toInt(0);
    
    // Default background settings
    if (obj.contains("default_background")) {
        doc->loadDefaultBackgroundFromJson(obj["default_background"].toObject());
    } else {
        // Legacy format: read flat fields
        QString bgStyle = obj["background_style"].toString("None");
        doc->defaultBackgroundType = stringToBackgroundType(bgStyle);
        QString bgColor = obj["background_color"].toString("#ffffff");
        doc->defaultBackgroundColor = QColor(bgColor);
        doc->defaultGridSpacing = obj["background_density"].toInt(32);
        doc->defaultLineSpacing = obj["background_density"].toInt(32);
    }

    // OCR settings
    doc->ocrLanguage = obj["ocr_language"].toString();
    doc->ocrSnapToBackground = obj["ocr_snap_to_background"].toBool(false);

    // PDF display overrides (absent key = inherit global, stored as -1)
    doc->pdfInvertDarkOverride = obj.contains("pdf_invert_dark")
        ? (obj["pdf_invert_dark"].toBool() ? 1 : 0) : -1;
    doc->pdfInvertIncludeImagesOverride = obj.contains("pdf_invert_include_images")
        ? (obj["pdf_invert_include_images"].toBool() ? 1 : 0) : -1;
    
    // Note: Pages are NOT loaded here - call loadPagesFromJson() separately
    // or use fromFullJson() to load everything
    
    doc->modified = false;
    return doc;
}

QJsonObject Document::toFullJson() const
{
    QJsonObject obj = toJson();
    
    // Add full page content
    obj["pages"] = pagesToJson();
    
    return obj;
}

std::unique_ptr<Document> Document::fromFullJson(const QJsonObject& obj)
{
    // First, load metadata
    auto doc = fromJson(obj);
    // Note: fromJson() always returns a valid document (uses make_unique),
    // but we keep this check for defensive programming / future changes
    if (!doc) {
        return nullptr;
    }
    
    // Load page content
    if (obj.contains("pages")) {
        doc->loadPagesFromJson(obj["pages"].toArray());
    } else {
        // No pages in JSON, ensure minimum
        doc->ensureMinimumPages();
    }
    
    return doc;
}

int Document::loadPagesFromJson(const QJsonArray& pagesArray)
{
    // Clear existing pages (lazy loading structures)
    m_pageOrder.clear();
    m_pageMetadata.clear();
    m_pagePdfIndex.clear();
    m_pagePdfSource.clear();
    m_loadedPages.clear();
    m_dirtyPages.clear();
    invalidateUuidCache();

    // Outline cache is keyed by page index; the incoming pages array
    // invalidates every key, so drop the cache and lazy-rebuild on next
    // enumerate.
    clearLinkOutlineCache();

    // Phase O1.5: Reset max object extent when reloading pages
    m_maxObjectExtent = 0;
    
    m_pageOrder.reserve(pagesArray.size());
    
    int loadedCount = 0;
    
    for (const auto& val : pagesArray) {
        auto page = Page::fromJson(val.toObject());
        if (page) {
            // Phase O2 (BF.3): Load image objects from assets folder.
            // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
            // We must call loadImages() to load the image files into memory.
            if (!m_bundlePath.isEmpty()) {
                page->loadImages(m_bundlePath);
            }
            
            // Phase O1.5: Update max object extent from loaded objects
            for (const auto& object : page->objects) {
                int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
                if (extent > m_maxObjectExtent) {
                    m_maxObjectExtent = extent;
                }
            }
            
            // Use lazy loading structures
            QString uuid = page->uuid;
            m_pageOrder.append(uuid);
            m_pageMetadata[uuid] = page->size;
            if (page->backgroundType == Page::BackgroundType::PDF) {
                m_pagePdfIndex[uuid] = page->pdfPageNumber;
                if (!page->pdfSourceId.isEmpty()) {
                    m_pagePdfSource[uuid] = page->pdfSourceId;
                }
            }
            m_loadedPages[uuid] = std::move(page);
            m_dirtyPages.insert(uuid);  // Mark as dirty since loaded from JSON
            
            ++loadedCount;
        }
    }
    
    // Ensure at least one page exists
    ensureMinimumPages();
    
    return loadedCount;
}

QJsonArray Document::pagesToJson() const
{
    QJsonArray pagesArray;
    
    // Iterate pages in order
    for (const QString& uuid : m_pageOrder) {
        auto it = m_loadedPages.find(uuid);
        if (it != m_loadedPages.end()) {
            pagesArray.append(it->second->toJson());
        }
    }
    
    return pagesArray;
}

QJsonObject Document::defaultBackgroundToJson() const
{
    QJsonObject bg;
    bg["type"] = backgroundTypeToString(defaultBackgroundType);
    bg["color"] = defaultBackgroundColor.name(QColor::HexArgb);
    bg["grid_color"] = defaultGridColor.name(QColor::HexRgb);  // Use 6-char hex (#RRGGBB) for clarity
    bg["grid_spacing"] = defaultGridSpacing;
    bg["line_spacing"] = defaultLineSpacing;
    
    // Only include page size for paged documents
    // Edgeless documents use tiles (1024x1024), not pages, so these fields are unused
    if (mode == Mode::Paged) {
        bg["page_width"] = defaultPageSize.width();
        bg["page_height"] = defaultPageSize.height();
    }
    return bg;
}

void Document::loadDefaultBackgroundFromJson(const QJsonObject& obj)
{
    defaultBackgroundType = stringToBackgroundType(obj["type"].toString("None"));
    
    QString bgColor = obj["color"].toString("#ffffffff");
    defaultBackgroundColor = QColor(bgColor);
    
    QString gridColor = obj["grid_color"].toString("#c8c8c8");  // Gray (200,200,200) in 6-char hex
    defaultGridColor = QColor(gridColor);
    
    defaultGridSpacing = obj["grid_spacing"].toInt(32);
    defaultLineSpacing = obj["line_spacing"].toInt(32);
    
    if (obj.contains("page_width") && obj.contains("page_height")) {
        defaultPageSize = QSizeF(
            obj["page_width"].toDouble(816),
            obj["page_height"].toDouble(1056)
        );
    }
}

QString Document::backgroundTypeToString(Page::BackgroundType type)
{
    switch (type) {
        case Page::BackgroundType::None:   return "none";
        case Page::BackgroundType::PDF:    return "pdf";
        case Page::BackgroundType::Custom: return "custom";
        case Page::BackgroundType::Grid:   return "grid";
        case Page::BackgroundType::Lines:  return "lines";
        default: return "none";
    }
}

Page::BackgroundType Document::stringToBackgroundType(const QString& str)
{
    QString lower = str.toLower();
    if (lower == "pdf")    return Page::BackgroundType::PDF;
    if (lower == "custom") return Page::BackgroundType::Custom;
    if (lower == "grid")   return Page::BackgroundType::Grid;
    if (lower == "lines")  return Page::BackgroundType::Lines;
    return Page::BackgroundType::None;
}

QString Document::modeToString(Mode m)
{
    switch (m) {
        case Mode::Paged:    return "paged";
        case Mode::Edgeless: return "edgeless";
        default: return "paged";
    }
}

Document::Mode Document::stringToMode(const QString& str)
{
    QString lower = str.toLower();
    if (lower == "edgeless") return Mode::Edgeless;
    return Mode::Paged;
}

// =============================================================================
// Tile Persistence (Phase E5)
// =============================================================================

QVector<Document::TileCoord> Document::allLoadedTileCoords() const
{
    QVector<TileCoord> coords;
    coords.reserve(static_cast<int>(m_tiles.size()));
    for (const auto& pair : m_tiles) {
        coords.append(pair.first);
    }
    return coords;
}

QVector<Document::TileCoord> Document::allKnownTileCoords() const
{
    std::set<TileCoord> all = m_tileIndex;
    for (const auto& pair : m_tiles)
        all.insert(pair.first);
    return QVector<TileCoord>(all.begin(), all.end());
}

void Document::markTileDirty(TileCoord coord)
{
    m_dirtyTiles.insert(coord);
    markModified();
}

bool Document::saveTile(TileCoord coord)
{
    if (m_bundlePath.isEmpty()) {
        qWarning() << "Cannot save tile: bundle path not set";
        return false;
    }
    
    auto it = m_tiles.find(coord);
    if (it == m_tiles.end()) {
        qWarning() << "Cannot save tile: not loaded in memory" << coord.first << coord.second;
        return false;
    }
    
    // Ensure tiles directory exists
    QString tilesDir = m_bundlePath + "/tiles";
    QDir().mkpath(tilesDir);
    
    // Build tile file path
    QString tilePath = tilesDir + "/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save tile: failed to open file" << tilePath;
        return false;
    }
    
    // Phase 5.6.3: For edgeless mode, use compact format:
    // - layers: array of {id, strokes} (layer properties stored in manifest)
    // - objects: array of InsertedObjects (Phase O2)
    // - coord_x, coord_y: tile coordinates for debugging
    QJsonObject tileObj;
    Page* tile = it->second.get();
    
    if (isEdgeless()) {
        QJsonArray layersArray;
        for (int i = 0; i < tile->layerCount(); ++i) {
            VectorLayer* layer = tile->layer(i);
            if (layer && !layer->isEmpty()) {
                QJsonObject layerObj;
                layerObj["id"] = layer->id;
                
                QJsonArray strokesArray;
                for (const auto& stroke : layer->strokes()) {
                    strokesArray.append(stroke.toJson());
                }
                layerObj["strokes"] = strokesArray;
                
                layersArray.append(layerObj);
            }
        }
        tileObj["layers"] = layersArray;
        
        // Phase O2: Save objects to tile (BF.5)
        // Objects are stored in tile-local coordinates; skip unlocked OCR objects
        if (!tile->objects.empty()) {
            QJsonArray objectsArray;
            for (const auto& obj : tile->objects) {
                if (obj->type() == QStringLiteral("ocr_text")) {
                    auto* ocr = static_cast<OcrTextObject*>(obj.get());
                    if (!ocr->ocrLocked)
                        continue;
                }
                objectsArray.append(obj->toJson());
            }
            tileObj["objects"] = objectsArray;
        }
        
        // Store tile coordinate for debugging/verification
        tileObj["coord_x"] = coord.first;
        tileObj["coord_y"] = coord.second;
    } else {
        // Paged mode: use full Page serialization (legacy behavior)
        tileObj = tile->toJson();
    }
    
    QJsonDocument jsonDoc(tileObj);
    file.write(jsonDoc.toJson(QJsonDocument::Compact));
    file.close();
    
    // Save OCR sidecar file
    saveTileOcr(coord);
    
    // Update state
    m_dirtyTiles.erase(coord);
    m_tileIndex.insert(coord);

    // Outline cache: in-memory tile is authoritative and now saved.
    refreshLinkOutlineFor(coord);

#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Saved tile" << coord.first << "," << coord.second << "to" << tilePath;
#endif
    
    return true;
}

bool Document::loadTileFromDisk(TileCoord coord) const
{
    if (m_bundlePath.isEmpty()) {
        return false;
    }
    
    QString tilePath = m_bundlePath + "/tiles/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load tile: file not found" << tilePath;
        // CR-6: Remove from index to prevent repeated failed loads
        m_tileIndex.erase(coord);
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Cannot load tile: JSON parse error" << parseError.errorString();
        m_tileIndex.erase(coord);  // CR-6: Remove from index
        return false;
    }
    
    QJsonObject obj = jsonDoc.object();
    
    // Phase 5.6.4: For edgeless mode, reconstruct layers from manifest
    // Tile files only contain {id, strokes} per layer, not full layer properties.
    // We check for coord_x/coord_y as markers of the new compact format.
    bool isNewFormat = obj.contains("coord_x") && obj.contains("coord_y");
    
    if (mode == Mode::Edgeless && isNewFormat && !m_edgelessLayers.empty()) {
        // New compact format: reconstruct full VectorLayers from manifest
        
        // Build map of layerId → strokes from tile file
        std::map<QString, QVector<VectorStroke>> strokesByLayerId;
        QJsonArray tileLayersArray = obj["layers"].toArray();
        for (const auto& val : tileLayersArray) {
            QJsonObject layerObj = val.toObject();
            QString layerId = layerObj["id"].toString();
            QVector<VectorStroke> strokes;
            for (const auto& strokeVal : layerObj["strokes"].toArray()) {
                strokes.append(VectorStroke::fromJson(strokeVal.toObject()));
            }
            strokesByLayerId[layerId] = strokes;
        }
        
        // Create tile with default page settings
        auto tile = std::make_unique<Page>();
        tile->size = QSizeF(EDGELESS_TILE_SIZE, EDGELESS_TILE_SIZE);
        tile->backgroundType = defaultBackgroundType;
        tile->backgroundColor = defaultBackgroundColor;
        tile->gridColor = defaultGridColor;
        tile->gridSpacing = defaultGridSpacing;
        tile->lineSpacing = defaultLineSpacing;
        
        // Clear default layer and reconstruct from manifest
        tile->vectorLayers.clear();
        for (const auto& layerDef : m_edgelessLayers) {
            auto layer = std::make_unique<VectorLayer>(layerDef.name);
            layer->id = layerDef.id;
            layer->visible = layerDef.visible;
            layer->opacity = layerDef.opacity;
            layer->locked = layerDef.locked;
            
            // Add strokes if this tile has any for this layer
            auto it = strokesByLayerId.find(layerDef.id);
            if (it != strokesByLayerId.end()) {
                for (const auto& stroke : it->second) {
                    layer->addStroke(stroke);
                }
            }
            
            tile->vectorLayers.push_back(std::move(layer));
        }
        
        tile->activeLayerIndex = m_edgelessActiveLayerIndex;
        
        // Phase O1.5: Load objects from tile file
        if (obj.contains("objects")) {
            QJsonArray objectsArray = obj["objects"].toArray();
            for (const auto& val : objectsArray) {
                auto object = InsertedObject::fromJson(val.toObject());
                if (object) {
                    // Update max object extent
                    int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
                    if (extent > m_maxObjectExtent) {
                        m_maxObjectExtent = extent;
                    }
                    tile->objects.push_back(std::move(object));
                }
            }
            // Rebuild affinity map after loading objects
            tile->rebuildAffinityMap();
            
            // Phase O2 (BF.3): Load image objects from assets folder.
            // InsertedObject::fromJson() only sets imagePath; it does NOT load the pixmap.
            tile->loadImages(m_bundlePath);
        }
        
        Page* rawTilePtr = tile.get();
        m_tiles[coord] = std::move(tile);
        ++m_tileLoadVersion;
        loadTileOcr(rawTilePtr, coord);
        materializeOcrTextObjects(rawTilePtr);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded tile" << coord.first << "," << coord.second 
                 << "from disk (manifest reconstruction)";
#endif
    } else {
        // Legacy format or paged mode: use full Page deserialization
        auto tile = Page::fromJson(obj);
        if (!tile) {
            qWarning() << "Cannot load tile: Page::fromJson failed";
            m_tileIndex.erase(coord);  // CR-6: Remove from index
            return false;
        }
        
        // Phase O2 (BF.3): Load image objects from assets folder.
        // Page::fromJson() only sets imagePath; it does NOT load the actual pixmap.
        tile->loadImages(m_bundlePath);
        
        // Phase O1.5: Update max object extent from loaded objects
        for (const auto& object : tile->objects) {
            int extent = static_cast<int>(qMax(object->size.width(), object->size.height()));
            if (extent > m_maxObjectExtent) {
                m_maxObjectExtent = extent;
            }
        }
        
        Page* rawTilePtr = tile.get();
        m_tiles[coord] = std::move(tile);
        ++m_tileLoadVersion;
        loadTileOcr(rawTilePtr, coord);
        materializeOcrTextObjects(rawTilePtr);
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded tile" << coord.first << "," << coord.second << "from disk (legacy)";
#endif
    }

    // Outline cache: in-memory tile is now authoritative; reconcile with
    // any prior disk-peek entry.  Safe no-op if the cache hasn't been
    // built yet (refreshLinkOutlineFor gates on m_linkOutlineCacheReady).
    refreshLinkOutlineFor(coord);

    return true;
}

void Document::evictTile(TileCoord coord)
{
    auto it = m_tiles.find(coord);
    if (it == m_tiles.end()) {
        return;  // Not loaded, nothing to evict
    }
    
    // Save if dirty
    if (m_dirtyTiles.count(coord) > 0) {
        if (!saveTile(coord)) {
            qWarning() << "Failed to save tile before eviction" << coord.first << coord.second;
            // Continue with eviction anyway to free memory
        }
    }
    
    // Remove from memory
    m_tiles.erase(it);
    ++m_tileLoadVersion;

#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Evicted tile" << coord.first << "," << coord.second << "from memory";
#endif
}

int Document::saveUnsavedImages(const QString& bundlePath)
{
    if (bundlePath.isEmpty()) {
        return 0;
    }
    
    int savedCount = 0;
    
    // CR-O2: Use virtual saveAssets() instead of type-specific code
    // This allows future object types with assets (audio, video, etc.) to work automatically.
    // 
    // Note: saveAssets() handles already-saved assets gracefully (deduplication check).
    // We call it for all objects with loaded assets - the virtual method no-ops for
    // objects without external assets (base class returns true immediately).
    auto processPage = [&](Page* page) {
        if (!page) return;
        
        for (auto& obj : page->objects) {
            // Images: data-integrity guard. As long as we still hold the
            // pixels, guarantee the referenced asset actually exists on disk.
            // This (re)writes a missing asset whether imagePath is empty (never
            // saved) or set but the file is absent (lost / copied object), so a
            // page JSON is never written with a dangling reference.
            // saveAssets()/saveToAssets() deduplicates, so this is a no-op when
            // the file is already present.
            if (auto* img = dynamic_cast<ImageObject*>(obj.get())) {
                if (img->isLoaded()) {
                    bool needsWrite = img->imagePath.isEmpty() ||
                        !QFile::exists(img->fullPath(bundlePath));
                    if (img->saveAssets(bundlePath)) {
                        if (needsWrite) {
                            savedCount++;
                            if (!img->imagePath.isEmpty()) {
                                qWarning() << "saveUnsavedImages: wrote missing asset"
                                           << img->imagePath << "for object" << img->id;
                            }
                        }
                    } else {
                        qWarning() << "saveUnsavedImages: Failed to save asset for"
                                   << img->type() << "object" << img->id;
                    }
                }
                continue;
            }

            // Other asset-bearing object types: existing virtual path.
            // isAssetLoaded() returns false for objects without external assets
            // (base class), so this no-ops for plain objects.
            if (obj->isAssetLoaded()) {
                // saveAssets() handles deduplication internally - safe to call even
                // if asset was previously saved (just updates imagePath if needed)
                if (!obj->saveAssets(bundlePath)) {
                    qWarning() << "saveUnsavedImages: Failed to save asset for" 
                               << obj->type() << "object" << obj->id;
                    } else {
                    savedCount++;
                }
            }
        }
    };
    
    if (mode == Mode::Edgeless) {
        // Process all loaded tiles
        for (auto& pair : m_tiles) {
            processPage(pair.second.get());
        }
    } else {
        // Paged mode: process loaded pages
        for (auto& pair : m_loadedPages) {
            processPage(pair.second.get());
        }
    }
    
    if (savedCount > 0) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "saveUnsavedImages: Saved" << savedCount << "images to assets";
        #endif
    }
    
    return savedCount;
}

// =========================================================================
// Asset Cleanup (Phase C.0.4)
// =========================================================================

void Document::cleanupOrphanedAssets()
{
    if (m_bundlePath.isEmpty()) {
        return;  // Unsaved document, nothing on disk
    }
    
    QString assetsPath = m_bundlePath + "/assets/images";
    QDir assetsDir(assetsPath);
    if (!assetsDir.exists()) {
        return;  // No assets folder
    }
    
    // Step 1: Collect all referenced image filenames
    QSet<QString> referencedFiles;

    // Fail-safe guard: if we cannot fully enumerate every page/tile's image
    // references (e.g. a page JSON is locked or corrupt), we must NOT delete
    // anything - an incomplete reference set previously caused in-use assets to
    // be deleted as "orphans", silently losing inserted images.
    bool incompleteScan = false;

    // Helper to scan a page's objects for image references
    auto collectFromPage = [&](Page* p) {
        if (!p) return;
        
        for (const auto& obj : p->objects) {
            if (auto* img = dynamic_cast<ImageObject*>(obj.get())) {
                if (!img->imagePath.isEmpty()) {
                    referencedFiles.insert(img->imagePath);
                }
            }
        }
    };
    
    // Step 2: Scan all pages/tiles based on mode
    if (isEdgeless()) {
        // Edgeless mode: scan all loaded tiles
        for (const auto& coord : allLoadedTileCoords()) {
            Page* tile = getTile(coord.first, coord.second);
            collectFromPage(tile);
        }
        
        // Scan evicted tiles (on disk but not in memory) by reading their
        // JSON files directly.  Only the "objects" array is inspected for
        // imagePath references — no full Page deserialization required.
        for (const auto& coord : m_tileIndex) {
            if (m_tiles.find(coord) != m_tiles.end())
                continue;  // already scanned above

            QString tilePath = m_bundlePath + "/tiles/" +
                QString("%1,%2.json").arg(coord.first).arg(coord.second);
            QFile file(tilePath);
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "cleanupOrphanedAssets: cannot open tile" << tilePath
                           << "- skipping asset cleanup to avoid data loss";
                incompleteScan = true;
                continue;
            }

            QJsonParseError parseError;
            QJsonDocument tileDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error != QJsonParseError::NoError) {
                qWarning() << "cleanupOrphanedAssets: parse error in tile" << tilePath
                           << parseError.errorString()
                           << "- skipping asset cleanup to avoid data loss";
                incompleteScan = true;
                continue;
            }
            QJsonObject tileObj = tileDoc.object();

            QJsonArray objects = tileObj["objects"].toArray();
            for (const auto& val : objects) {
                QJsonObject obj = val.toObject();
                if (obj["type"].toString() == QLatin1String("image")) {
                    QString path = obj["imagePath"].toString();
                    if (!path.isEmpty())
                        referencedFiles.insert(path);
                }
            }
        }
    } else {
        // Paged mode: scan every page for image references.
        //
        // Loaded pages are scanned in memory (authoritative - may carry
        // imagePath changes not yet flushed to disk). Evicted pages are read
        // directly from their JSON file: this avoids mass-loading every page
        // into memory at close, and - critically - lets us detect a failed
        // read so we can abort deletion instead of dropping references and
        // deleting in-use assets (the cause of the silent image-loss bug).
        QString pagesDir = m_bundlePath + "/pages";
        for (const QString& uuid : m_pageOrder) {
            auto loadedIt = m_loadedPages.find(uuid);
            if (loadedIt != m_loadedPages.end()) {
                collectFromPage(loadedIt->second.get());
                continue;
            }

            QString pagePath = pagesDir + "/" + uuid + ".json";
            QFile file(pagePath);
            if (!file.exists()) {
                // Pristine PDF pages legitimately have no JSON file (they are
                // synthesized on load). Any other absent page carries no image
                // references, so there is nothing to collect or lose.
                continue;
            }
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "cleanupOrphanedAssets: cannot open page" << pagePath
                           << "- skipping asset cleanup to avoid data loss";
                incompleteScan = true;
                continue;
            }

            QJsonParseError parseError;
            QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error != QJsonParseError::NoError) {
                qWarning() << "cleanupOrphanedAssets: parse error in page" << pagePath
                           << parseError.errorString()
                           << "- skipping asset cleanup to avoid data loss";
                incompleteScan = true;
                continue;
            }

            QJsonArray objects = jsonDoc.object()["objects"].toArray();
            for (const auto& val : objects) {
                QJsonObject obj = val.toObject();
                if (obj["type"].toString() == QLatin1String("image")) {
                    QString path = obj["imagePath"].toString();
                    if (!path.isEmpty())
                        referencedFiles.insert(path);
                }
            }
        }
    }

    // Fail-safe: never delete from an incomplete reference set.
    if (incompleteScan) {
        qWarning() << "cleanupOrphanedAssets: reference scan incomplete -"
                   << "skipping orphan deletion to protect in-use assets";
        return;
    }
    
    // Step 3: List files on disk and delete orphans
    QStringList filesOnDisk = assetsDir.entryList(QDir::Files);
    int deletedCount = 0;
    
    for (const QString& filename : filesOnDisk) {
        if (!referencedFiles.contains(filename)) {
            QString fullPath = assetsPath + "/" + filename;
            if (QFile::remove(fullPath)) {
                deletedCount++;
#ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cleaned up orphaned asset:" << filename;
#endif
            }
        }
    }
    
#ifdef SPEEDYNOTE_DEBUG
    if (deletedCount > 0) {
        qDebug() << "Cleaned up" << deletedCount << "orphaned assets";
    }
#endif
}

// ===== Markdown Notes (Phase M.1) =====

QString Document::notesPath() const
{
    QString assets = assetsPath();
    if (assets.isEmpty()) {
        return QString();
    }
    
    QString notes = assets + "/notes";
    QDir().mkpath(notes);
    return notes;
}

bool Document::deleteNoteFile(const QString& noteId)
{
    QString notes = notesPath();
    if (notes.isEmpty()) {
        return false;
    }
    
    QString filePath = notes + "/" + noteId + ".md";
    if (QFile::exists(filePath)) {
        return QFile::remove(filePath);
    }
    return true;  // File didn't exist, consider it successfully "deleted"
}

// ============================================================================
// Link Outline Cache (Phase M.9)
// ============================================================================
//
// Design goals:
//   - Outline survives tile/page eviction: evicting a tile does NOT drop
//     its entries from the cache.  The sidebar view stays stable while the
//     user pans in edgeless mode.
//   - No full-document force-load on refresh: paged mode peeks page-file
//     JSON when the Page isn't already resident, instead of lazy-loading
//     it via page(i).
//   - Authoritative source: in-memory container (if loaded) is always
//     preferred over the disk copy; refreshLinkOutlineFor is called every
//     time an in-memory container becomes authoritative or is saved.
// ============================================================================

// -------- Static helper: Page* → QVector<LinkOutlineEntry> -----------------

QVector<LinkOutlineEntry>
Document::extractLinkOutlineFromPage(const Page* page,
                                      int pageIdx,
                                      int tileX,
                                      int tileY,
                                      bool edgeless,
                                      bool requireMarkdown)
{
    QVector<LinkOutlineEntry> out;
    if (!page) return out;

    const QPointF tileOrigin = edgeless
        ? QPointF(tileX * static_cast<qreal>(EDGELESS_TILE_SIZE),
                  tileY * static_cast<qreal>(EDGELESS_TILE_SIZE))
        : QPointF();

    for (const auto& objPtr : page->objects) {
        const LinkObject* link = dynamic_cast<const LinkObject*>(objPtr.get());
        if (!link) continue;

        LinkOutlineEntry entry;
        entry.markdownSlots.reserve(LinkObject::SLOT_COUNT);
        for (int i = 0; i < LinkObject::SLOT_COUNT; ++i) {
            const LinkSlot& s = link->linkSlots[i];
            if (s.type != LinkSlot::Type::Markdown) continue;
            if (s.markdownNoteId.isEmpty()) continue;
            entry.markdownSlots.push_back({ i, s.markdownNoteId });
        }
        if (requireMarkdown && entry.markdownSlots.isEmpty()) continue;

        entry.linkObjectId = link->id;
        entry.description  = link->description;
        entry.iconColor    = link->iconColor;
        entry.pageIndex    = edgeless ? -1 : pageIdx;
        entry.tileX        = tileX;
        entry.tileY        = tileY;
        entry.docPos       = edgeless ? (tileOrigin + link->position)
                                       : link->position;
        out.push_back(std::move(entry));
    }
    return out;
}

// -------- Shared JSON walker for both disk-peek helpers --------------------

QVector<LinkOutlineEntry>
Document::extractLinkOutlineFromJsonObjects(const QJsonArray& objects,
                                             int  pageIndex,
                                             int  tileX,
                                             int  tileY,
                                             const QPointF& tileOrigin,
                                             bool requireMarkdown)
{
    QVector<LinkOutlineEntry> out;
    const QColor kDefaultIcon(100, 100, 100, 180);

    for (const QJsonValue& v : objects) {
        const QJsonObject o = v.toObject();
        if (o["type"].toString() != QLatin1String("link")) continue;

        LinkOutlineEntry entry;
        entry.markdownSlots.reserve(LinkObject::SLOT_COUNT);

        const QJsonArray slotArray = o["slots"].toArray();
        const int slotMax = qMin(static_cast<int>(slotArray.size()),
                                  LinkObject::SLOT_COUNT);
        for (int i = 0; i < slotMax; ++i) {
            const QJsonObject s = slotArray[i].toObject();
            if (s["type"].toString() != QLatin1String("markdown")) continue;
            const QString noteId = s["noteId"].toString();
            if (noteId.isEmpty()) continue;
            entry.markdownSlots.push_back({ i, noteId });
        }
        if (requireMarkdown && entry.markdownSlots.isEmpty()) continue;

        entry.linkObjectId = o["id"].toString();
        entry.description  = o["description"].toString();

        // iconColor is stored by LinkObject::toJson as "#RRGGBB[AA]".
        // Fall back to LinkObject's default grey on anything unparsable.
        const QJsonValue iconVal = o.value("iconColor");
        if (iconVal.isString()) {
            const QColor c(iconVal.toString());
            entry.iconColor = c.isValid() ? c : kDefaultIcon;
        } else {
            entry.iconColor = kDefaultIcon;
        }

        entry.pageIndex = pageIndex;
        entry.tileX     = tileX;
        entry.tileY     = tileY;
        const QPointF localPos(o["x"].toDouble(), o["y"].toDouble());
        entry.docPos    = tileOrigin + localPos;  // tileOrigin == (0,0) in paged

        out.push_back(std::move(entry));
    }
    return out;
}

// -------- Disk peek: tile JSON → outline entries ---------------------------

QVector<LinkOutlineEntry>
Document::peekTileLinkOutlineFromDisk(TileCoord coord, bool requireMarkdown) const
{
    if (m_bundlePath.isEmpty()) return {};

    const QString path = m_bundlePath + "/tiles/"
        + QString("%1,%2.json").arg(coord.first).arg(coord.second);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError perr;
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !jd.isObject()) return {};

    const QPointF tileOrigin(coord.first  * static_cast<qreal>(EDGELESS_TILE_SIZE),
                              coord.second * static_cast<qreal>(EDGELESS_TILE_SIZE));
    return extractLinkOutlineFromJsonObjects(
        jd.object()["objects"].toArray(),
        /*pageIndex=*/ -1, coord.first, coord.second, tileOrigin, requireMarkdown);
}

// -------- Disk peek: page JSON → outline entries ---------------------------

QVector<LinkOutlineEntry>
Document::peekPageLinkOutlineFromDisk(int pageIndex, bool requireMarkdown) const
{
    if (m_bundlePath.isEmpty()) return {};
    if (pageIndex < 0 || pageIndex >= m_pageOrder.size()) return {};

    const QString path = m_bundlePath + "/pages/" + m_pageOrder[pageIndex] + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};  // e.g. pristine PDF page (no file yet)

    QJsonParseError perr;
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !jd.isObject()) return {};

    return extractLinkOutlineFromJsonObjects(
        jd.object()["objects"].toArray(),
        pageIndex, /*tileX=*/0, /*tileY=*/0, /*tileOrigin=*/QPointF(), requireMarkdown);
}

// -------- Cache maintenance -------------------------------------------------

void Document::buildLinkOutlineCache() const
{
    m_pageOutline.clear();
    m_tileOutline.clear();

    if (isEdgeless()) {
        // Union of in-memory tiles and disk-only tiles.  Deduped via the
        // m_tileOutline map (in-memory wins by iteration order).
        std::set<TileCoord> allCoords = m_tileIndex;
        for (const auto& kv : m_tiles) allCoords.insert(kv.first);

        for (const TileCoord& coord : allCoords) {
            auto it = m_tiles.find(coord);
            QVector<LinkOutlineEntry> entries;
            if (it != m_tiles.end() && it->second) {
                entries = extractLinkOutlineFromPage(
                    it->second.get(), -1, coord.first, coord.second, true);
            } else {
                entries = peekTileLinkOutlineFromDisk(coord);
            }
            // Store even empty vectors so we know the container has been
            // visited — not strictly required, but makes eviction a no-op
            // safe (see refreshLinkOutlineFor).
            m_tileOutline[coord] = std::move(entries);
        }
    } else {
        const int count = static_cast<int>(m_pageOrder.size());
        for (int i = 0; i < count; ++i) {
            const QString uuid = m_pageOrder[i];
            auto it = m_loadedPages.find(uuid);
            QVector<LinkOutlineEntry> entries;
            if (it != m_loadedPages.end() && it->second) {
                entries = extractLinkOutlineFromPage(
                    it->second.get(), i, 0, 0, false);
            } else {
                entries = peekPageLinkOutlineFromDisk(i);
            }
            m_pageOutline[i] = std::move(entries);
        }
    }

    m_linkOutlineCacheReady = true;
}

void Document::refreshLinkOutlineFor(TileCoord coord) const
{
    if (!m_linkOutlineCacheReady) {
        // No cache yet — refresh is deferred to first enumerate, which
        // will build the whole thing.
        return;
    }
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end() && it->second) {
        m_tileOutline[coord] = extractLinkOutlineFromPage(
            it->second.get(), -1, coord.first, coord.second, true);
    } else if (m_tileIndex.count(coord) > 0) {
        m_tileOutline[coord] = peekTileLinkOutlineFromDisk(coord);
    } else {
        // Neither loaded nor on disk: treat as gone.
        m_tileOutline.erase(coord);
    }
}

void Document::refreshLinkOutlineFor(int pageIndex) const
{
    // Nothing to do until at least one cache has been built.  Each cache keeps
    // its own ready flag so the outline (markdown-only) and the SB2 marker
    // (all-links) caches can be populated independently.
    if (!m_linkOutlineCacheReady && !m_markerCacheReady) return;

    if (pageIndex < 0 || pageIndex >= m_pageOrder.size()) {
        if (m_linkOutlineCacheReady) m_pageOutline.erase(pageIndex);
        if (m_markerCacheReady)      m_pageMarkers.erase(pageIndex);
        return;
    }

    // Re-extract from the most authoritative source once, then filter per cache
    // via requireMarkdown.
    const QString uuid = m_pageOrder[pageIndex];
    auto it = m_loadedPages.find(uuid);
    const bool loaded = (it != m_loadedPages.end() && it->second);

    auto compute = [&](bool requireMarkdown) -> QVector<LinkOutlineEntry> {
        if (loaded) {
            return extractLinkOutlineFromPage(
                it->second.get(), pageIndex, 0, 0, false, requireMarkdown);
        }
        return peekPageLinkOutlineFromDisk(pageIndex, requireMarkdown);
    };

    if (m_linkOutlineCacheReady) m_pageOutline[pageIndex] = compute(/*requireMarkdown=*/true);
    if (m_markerCacheReady)      m_pageMarkers[pageIndex] = compute(/*requireMarkdown=*/false);
}

void Document::dropLinkOutlineFor(TileCoord coord) const
{
    m_tileOutline.erase(coord);
}

void Document::dropLinkOutlineFor(int pageIndex) const
{
    // Erase the removed page and re-key higher pages down by 1 — removePage
    // compacts m_pageOrder so the caller's notion of "pageIndex i" shifts for
    // i > removed index.  Applied identically to both caches.
    auto dropAndRekey = [pageIndex](std::map<int, QVector<LinkOutlineEntry>>& map) {
        map.erase(pageIndex);
        std::map<int, QVector<LinkOutlineEntry>> shifted;
        for (auto& kv : map) {
            const int newKey = (kv.first > pageIndex) ? (kv.first - 1) : kv.first;
            QVector<LinkOutlineEntry> v = std::move(kv.second);
            if (newKey != kv.first) {
                for (auto& e : v) e.pageIndex = newKey;
            }
            shifted[newKey] = std::move(v);
        }
        map = std::move(shifted);
    };

    dropAndRekey(m_pageOutline);
    if (m_markerCacheReady) dropAndRekey(m_pageMarkers);
}

void Document::clearLinkOutlineCache() const
{
    m_pageOutline.clear();
    m_tileOutline.clear();
    m_linkOutlineCacheReady = false;
    m_pageMarkers.clear();
    m_markerCacheReady = false;
}

// -------- SB2 all-links marker cache ---------------------------------------

void Document::buildMarkerCache() const
{
    m_pageMarkers.clear();

    // Markers are a paged-mode concept (edgeless has no page track).
    if (!isEdgeless()) {
        const int count = static_cast<int>(m_pageOrder.size());
        for (int i = 0; i < count; ++i) {
            const QString uuid = m_pageOrder[i];
            auto it = m_loadedPages.find(uuid);
            QVector<LinkOutlineEntry> entries;
            if (it != m_loadedPages.end() && it->second) {
                entries = extractLinkOutlineFromPage(
                    it->second.get(), i, 0, 0, false, /*requireMarkdown=*/false);
            } else {
                entries = peekPageLinkOutlineFromDisk(i, /*requireMarkdown=*/false);
            }
            m_pageMarkers[i] = std::move(entries);
        }
    }

    m_markerCacheReady = true;
}

QVector<Document::PageLinkMarker> Document::pageLinkMarkers() const
{
    if (isEdgeless()) return {};
    if (!m_markerCacheReady) buildMarkerCache();

    // Iterate pages in order. For LOADED pages we extract live from memory so
    // markers reflect in-session edits (new links, color/description changes,
    // moves) even if a mutation path forgot to refresh the marker cache -- the
    // page the user is editing is always the loaded/visible one. UNLOADED pages
    // fall back to the disk-peek-backed cache (no force-load on scroll).
    QVector<PageLinkMarker> out;
    const int count = static_cast<int>(m_pageOrder.size());
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        QVector<LinkOutlineEntry> live;
        const QVector<LinkOutlineEntry>* entries = nullptr;

        const QString& uuid = m_pageOrder[i];
        auto lit = m_loadedPages.find(uuid);
        if (lit != m_loadedPages.end() && lit->second) {
            live = extractLinkOutlineFromPage(lit->second.get(), i, 0, 0,
                                              /*edgeless=*/false, /*requireMarkdown=*/false);
            entries = &live;
        } else {
            auto cit = m_pageMarkers.find(i);
            if (cit != m_pageMarkers.end()) entries = &cit->second;
        }
        if (!entries || entries->isEmpty()) continue;

        // Reduce to the topmost link on the page (smallest local Y).
        const LinkOutlineEntry* top = &entries->first();
        for (const LinkOutlineEntry& e : *entries) {
            if (e.docPos.y() < top->docPos.y()) top = &e;
        }

        PageLinkMarker m;
        m.pageIndex   = i;
        m.localY      = top->docPos.y();
        m.color       = top->iconColor;   // raw; bar applies min-contrast substitution
        m.description = top->description;
        out.push_back(std::move(m));
    }
    return out;
}

// -------- Public query: flat snapshot --------------------------------------

QVector<LinkOutlineEntry> Document::enumerateLinkOutline() const
{
    if (!m_linkOutlineCacheReady) buildLinkOutlineCache();

    // Two passes on the same map: one to compute the total size for
    // reserve(), one to flatten.  Map traversal is O(n) and the entries
    // are small, so the reservation is worth it when the total count
    // runs into the hundreds.
    auto flatten = [](const auto& map) {
        int total = 0;
        for (const auto& kv : map) total += kv.second.size();
        QVector<LinkOutlineEntry> out;
        out.reserve(total);
        for (const auto& kv : map) out.append(kv.second);
        return out;
    };
    return isEdgeless() ? flatten(m_tileOutline) : flatten(m_pageOutline);
}

bool Document::saveBundle(const QString& path, bool finalize)
{
    // Save old bundle path before overwriting - needed for copying evicted tiles/pages
    QString oldBundlePath = m_bundlePath;
    m_bundlePath = path;
    
    // Phase P.1.1: Write .snb_marker file to identify this as a SpeedyNote bundle
    QString markerPath = path + "/.snb_marker";
    if (!QFile::exists(markerPath)) {
        QFile markerFile(markerPath);
        if (markerFile.open(QIODevice::WriteOnly)) {
            // Empty file - existence is enough to identify the bundle
            markerFile.close();
        }
    }
    
    // Phase O1.6: Create assets directory for object files (images, etc.)
    if (!QDir().mkpath(path + "/assets/images")) {
        qWarning() << "Cannot create assets/images directory" << path;
        return false;
    }
    
    // Phase O2 (BF.2): Save any unsaved images to assets folder BEFORE saving page JSON.
    // 
    // This is critical for images pasted into a NEW document before first save:
    // - When paste happens, bundlePath is empty, so saveToAssets() is skipped
    // - The image exists only as cachedPixmap with imagePath = ""
    // - Here we finally have a bundle path, so we can save images and set imagePath
    // - Then the serialized page JSON will have the correct imagePath reference
    saveUnsavedImages(path);

    // Plan A2 / Q7.2: drop any PDF source no page references anymore (e.g. after
    // deleting all pages backed by an imported source, or every primary-PDF page).
    // Done before the relative-path refresh and toJson() below so the serialized
    // pdf_sources[] and legacy pdf_path mirror reflect only live sources.
    pruneUnreferencedSources();

    // Save As must carry existing bundled fallbacks to the new bundle before
    // finalization decides that every referenced page is already materialized.
    if (!oldBundlePath.isEmpty()
        && QDir::cleanPath(oldBundlePath) != QDir::cleanPath(path)) {
        auto copyAtomically = [](const QString& sourcePath, const QString& destPath) {
            QFile source(sourcePath);
            if (!source.open(QIODevice::ReadOnly)) return false;
            if (!QDir().mkpath(QFileInfo(destPath).absolutePath())) return false;

            QSaveFile dest(destPath);
            dest.setDirectWriteFallback(false);
            if (!dest.open(QIODevice::WriteOnly)) return false;
            constexpr qint64 chunkSize = 1024 * 1024;
            while (!source.atEnd()) {
                const QByteArray chunk = source.read(chunkSize);
                if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
                    dest.cancelWriting();
                    return false;
                }
                if (dest.write(chunk) != chunk.size()) {
                    dest.cancelWriting();
                    return false;
                }
            }
            return dest.commit();
        };

        for (const PdfSource& source : m_pdfSources) {
            if (!source.bundled || source.bundledFile.isEmpty()) continue;
            const QString oldFile =
                QDir(oldBundlePath).absoluteFilePath(source.bundledFile);
            const QString newFile = QDir(path).absoluteFilePath(source.bundledFile);
            if (!QFileInfo::exists(oldFile) || !copyAtomically(oldFile, newFile)) {
                qWarning() << "Cannot copy bundled PDF during Save As:" << oldFile;
                m_bundlePath = oldBundlePath;
                return false;
            }
        }
    }

    // Plan B2 (Q12.1 Option D): on finalize (document close / .snbx export), graft
    // each non-primary imported source's referenced pages into a bundled mini-PDF so
    // the bundle is self-contained. Done before the relative-path refresh and toJson()
    // so the serialized pdf_sources[] carry bundled/bundled_file/page_map. Ordinary
    // saves/autosaves pass finalize=false and skip this entirely.
    if (finalize) {
        QString materializeError;
        materializeSources(&materializeError);
        if (needsMaterialization()) {
            qWarning() << "Cannot finalize bundle PDF sources:" << materializeError;
            m_bundlePath = oldBundlePath;
            return false;
        }
    }
    
    // Refresh each PDF source's relative path against the bundle location so the
    // serialized pdf_sources[]/pdf_relative_path reflect the current save target.
    {
        QDir bundleDir(path);
        for (PdfSource& s : m_pdfSources) {
            if (!s.path.isEmpty()) {
                s.relativePath = bundleDir.relativeFilePath(s.path);
            }
        }
    }
    
    // Build manifest
    QJsonObject manifest = toJson();  // Metadata only
    
    // For edgeless mode: track all tile coordinates (shared between blocks)
    std::set<TileCoord> allTileCoords;
    
    // ========== MODE-SPECIFIC SAVE ==========
    if (mode == Mode::Edgeless) {
        // Create tiles directory
        if (!QDir().mkpath(path + "/tiles")) {
            #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cannot create tiles directory" << path;
            #endif
            return false;
        }
        
        // Build tile index (union of disk tiles and memory tiles)
        allTileCoords = m_tileIndex;
        for (const auto& pair : m_tiles) {
            allTileCoords.insert(pair.first);
        }
        
        QJsonArray tileIndexArray;
        for (const auto& coord : allTileCoords) {
            tileIndexArray.append(QString("%1,%2").arg(coord.first).arg(coord.second));
        }
        manifest["tile_index"] = tileIndexArray;
        manifest["tile_size"] = EDGELESS_TILE_SIZE;
        
        // Phase 5.6: Write layer definitions to manifest
        QJsonArray layersArray;
        for (const auto& layerDef : m_edgelessLayers) {
            layersArray.append(layerDef.toJson());
        }
        manifest["layers"] = layersArray;
        manifest["active_layer_index"] = m_edgelessActiveLayerIndex;
        
        // Phase 4: Write position history to manifest
        QJsonObject lastPosObj;
        lastPosObj["x"] = m_edgelessLastPosition.x();
        lastPosObj["y"] = m_edgelessLastPosition.y();
        manifest["last_position"] = lastPosObj;
        
        QJsonArray posHistoryArray;
        for (const QPointF& pos : m_edgelessPositionHistory) {
            QJsonObject posObj;
            posObj["x"] = pos.x();
            posObj["y"] = pos.y();
            posHistoryArray.append(posObj);
        }
        manifest["position_history"] = posHistoryArray;
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "saveToBundle: Saved edgeless position" << m_edgelessLastPosition
                 << "with" << m_edgelessPositionHistory.size() << "history entries";
#endif
    } else {
        // ========== PAGED MODE SAVE (Phase O1.7.4) ==========
        if (!QDir().mkpath(path + "/pages")) {
            #ifdef SPEEDYNOTE_DEBUG
                qDebug() << "Cannot create pages directory" << path;
            #endif
            return false;
        }
        
        // Write page_order to manifest
        QJsonArray pageOrderArray;
        for (const QString& uuid : m_pageOrder) {
            pageOrderArray.append(uuid);
        }
        manifest["page_order"] = pageOrderArray;
        
        // Write page_metadata to manifest (includes pdf_page for pristine PDF page synthesis)
        QJsonObject pageMetadataObj;
        for (const auto& [uuid, size] : m_pageMetadata) {
            QJsonObject metaObj;
            metaObj["width"] = size.width();
            metaObj["height"] = size.height();
            
            // Include PDF page index if this is a PDF page
            auto pdfIt = m_pagePdfIndex.find(uuid);
            if (pdfIt != m_pagePdfIndex.end()) {
                metaObj["pdf_page"] = pdfIt->second;
            }
            // Include PDF source id only for non-primary sources (empty = primary).
            auto srcIt = m_pagePdfSource.find(uuid);
            if (srcIt != m_pagePdfSource.end() && !srcIt->second.isEmpty()) {
                metaObj["pdf_source"] = srcIt->second;
            }
            
            pageMetadataObj[uuid] = metaObj;
        }
        manifest["page_metadata"] = pageMetadataObj;
    }
    
    // Phase SHARE: Write pdf_relative_path (primary mirror) for portability.
    // Per-source relative paths were already refreshed above and serialized into
    // pdf_sources[] by toJson().
    if (const PdfSource* primary = primarySource()) {
        if (!primary->relativePath.isEmpty()) {
            manifest["pdf_relative_path"] = primary->relativePath;
        }
    }
    
    // Write manifest
    QString manifestPath = path + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write manifest" << manifestPath;
        return false;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();
    
    bool savingToNewLocation = !oldBundlePath.isEmpty() && oldBundlePath != path;
    
    // ========== COPY ASSETS WHEN SAVING TO NEW LOCATION (Phase O1.6 fix) ==========
    if (savingToNewLocation) {
        QString oldAssetsPath = oldBundlePath + "/assets/images";
        QString newAssetsPath = path + "/assets/images";
        
        QDir oldAssetsDir(oldAssetsPath);
        if (oldAssetsDir.exists()) {
            QStringList assetFiles = oldAssetsDir.entryList(QDir::Files);
            for (const QString& fileName : assetFiles) {
                QString oldFilePath = oldAssetsPath + "/" + fileName;
                QString newFilePath = newAssetsPath + "/" + fileName;
                
                // Skip if already exists (e.g., newly added images saved above)
                if (!QFile::exists(newFilePath)) {
                    if (QFile::copy(oldFilePath, newFilePath)) {
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied asset" << fileName;
#endif
                    } else {
                        qWarning() << "Failed to copy asset" << oldFilePath << "to" << newFilePath;
                    }
                }
            }
        }
    }
    
    // ========== MODE-SPECIFIC FILE HANDLING ==========
    if (mode == Mode::Edgeless) {
        // Clear manifest dirty flag after save
        m_edgelessManifestDirty = false;
        
        // ========== HANDLE TILES WHEN SAVING TO NEW LOCATION ==========
        if (savingToNewLocation) {
            // Copy evicted tiles from old bundle (tiles on disk but not in memory)
            for (const auto& coord : m_tileIndex) {
                // Skip tiles that are in memory - they'll be saved below
                if (m_tiles.find(coord) != m_tiles.end()) {
                    continue;
                }
                
                QString tileFileName = QString("%1,%2.json").arg(coord.first).arg(coord.second);
                QString oldTilePath = oldBundlePath + "/tiles/" + tileFileName;
                QString newTilePath = path + "/tiles/" + tileFileName;
                
                // Copy tile file from old location to new location
                if (QFile::exists(oldTilePath)) {
                    if (QFile::copy(oldTilePath, newTilePath)) {
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied evicted tile" << coord.first << "," << coord.second;
#endif
                    } else {
                        qWarning() << "Failed to copy tile" << oldTilePath << "to" << newTilePath;
                    }
                }
                
                // Copy OCR sidecar file if it exists
                QString ocrFileName = QString("%1,%2.ocr.json").arg(coord.first).arg(coord.second);
                QString oldOcrPath = oldBundlePath + "/tiles/" + ocrFileName;
                QString newOcrPath = path + "/tiles/" + ocrFileName;
                if (QFile::exists(oldOcrPath)) {
                    QFile::copy(oldOcrPath, newOcrPath);
                }
            }
        }
        
        // Save tiles in memory
        for (const auto& pair : m_tiles) {
            TileCoord coord = pair.first;
            // When saving to new location: save ALL in-memory tiles
            // When saving to same location: only save dirty/new tiles
            bool needsSave = savingToNewLocation || 
                             m_dirtyTiles.count(coord) > 0 || 
                             m_tileIndex.count(coord) == 0;
            if (needsSave) {
                saveTile(coord);
            }
        }
        
        // ========== DELETE EMPTY TILES FROM DISK ==========
        for (const auto& coord : m_deletedTiles) {
            QString tileFileName = QString("%1,%2.json").arg(coord.first).arg(coord.second);
            QString tilePath = path + "/tiles/" + tileFileName;
            if (QFile::exists(tilePath)) {
                if (QFile::remove(tilePath)) {
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted empty tile file:" << tileFileName;
#endif
                } else {
                    #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Failed to delete empty tile file:" << tilePath;
                    #endif
                }
            }
            // Also delete OCR sidecar
            QString ocrPath = path + "/tiles/" +
                QString("%1,%2.ocr.json").arg(coord.first).arg(coord.second);
            QFile::remove(ocrPath);
        }
        m_deletedTiles.clear();
        m_dirtyTiles.clear();
        m_tileIndex = allTileCoords;
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Saved edgeless bundle to" << path << "with" << allTileCoords.size() << "tiles";
#endif
    } else {
        // ========== PAGED MODE FILE HANDLING (Phase O1.7.4) ==========
        
        // Copy evicted pages when saving to new location
        if (savingToNewLocation) {
            for (const QString& uuid : m_pageOrder) {
                // Skip pages that are in memory - they'll be saved below
                if (m_loadedPages.find(uuid) != m_loadedPages.end()) {
                    continue;
                }
                
                QString pageFileName = uuid + ".json";
                QString oldPagePath = oldBundlePath + "/pages/" + pageFileName;
                QString newPagePath = path + "/pages/" + pageFileName;
                
                if (QFile::exists(oldPagePath)) {
                    if (QFile::copy(oldPagePath, newPagePath)) {
#ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Copied evicted page" << uuid;
#endif
                    } else {
                        #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "Failed to copy page" << oldPagePath << "to" << newPagePath;
                        #endif
                    }
                }
                
                // Copy OCR sidecar file if it exists
                QString oldOcrPath = oldBundlePath + "/pages/" + uuid + ".ocr.json";
                QString newOcrPath = path + "/pages/" + uuid + ".ocr.json";
                if (QFile::exists(oldOcrPath)) {
                    QFile::copy(oldOcrPath, newOcrPath);
                }
            }
        }
        
        // Save pages in memory
        for (const auto& [uuid, pagePtr] : m_loadedPages) {
            // Skip pristine PDF pages - they can be synthesized from manifest
            // A page is "pristine" if it has PDF background, no user content, and no bookmark
            bool isPristinePdfPage = (pagePtr->backgroundType == Page::BackgroundType::PDF) 
                                     && !pagePtr->hasContent()
                                     && !pagePtr->isBookmarked;
            if (isPristinePdfPage) {
                // Ensure PDF page index is tracked for synthesis
                if (m_pagePdfIndex.find(uuid) == m_pagePdfIndex.end()) {
                    m_pagePdfIndex[uuid] = pagePtr->pdfPageNumber;
                }
                // Track the page's source for synthesis (non-primary only).
                if (!pagePtr->pdfSourceId.isEmpty()) {
                    m_pagePdfSource[uuid] = pagePtr->pdfSourceId;
                }
                
                // Delete any stale file from when page had content
                QString pagePath = path + "/pages/" + uuid + ".json";
                if (QFile::exists(pagePath)) {
                    QFile::remove(pagePath);
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted stale file for pristine PDF page" << uuid;
#endif
                }
                
                continue;  // Don't save file - synthesize on load
            }
            
            // When saving to new location: save ALL in-memory pages (with content)
            // When saving to same location: only save dirty pages
            bool needsSave = savingToNewLocation || m_dirtyPages.count(uuid) > 0;
            if (needsSave) {
                QString pagePath = path + "/pages/" + uuid + ".json";
                QFile file(pagePath);
                if (file.open(QIODevice::WriteOnly)) {
                    QJsonDocument doc(pagePtr->toJson());
                    file.write(doc.toJson(QJsonDocument::Compact));
                    file.close();
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Saved page" << uuid;
#endif
                } else {
                    #ifdef SPEEDYNOTE_DEBUG
                        qDebug() << "Failed to save page" << pagePath;
                    #endif
                }
            }
        }
        
        // ========== DELETE REMOVED PAGES FROM DISK ==========
        for (const QString& uuid : m_deletedPages) {
            QString pagePath = path + "/pages/" + uuid + ".json";
            if (QFile::exists(pagePath)) {
                if (QFile::remove(pagePath)) {
#ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "Deleted page file:" << uuid;
#endif
                } else {
                    qWarning() << "Failed to delete page file:" << pagePath;
                }
            }
            // Also delete OCR sidecar
            QString ocrPath = path + "/pages/" + uuid + ".ocr.json";
            QFile::remove(ocrPath);
        }
        m_deletedPages.clear();
        m_dirtyPages.clear();
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Saved paged bundle to" << path << "with" << m_pageOrder.size() << "pages";
#endif
    }
    
    m_lazyLoadEnabled = true;
    clearModified();
    
    return true;
}

std::unique_ptr<Document> Document::loadBundle(const QString& path)
{
    QString manifestPath = path + "/document.json";
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open bundle manifest" << manifestPath;
        return nullptr;
    }
    
    QByteArray data = manifestFile.readAll();
    manifestFile.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Bundle manifest parse error:" << parseError.errorString();
        return nullptr;
    }
    
    QJsonObject obj = jsonDoc.object();
    
    // Phase P.1.1: Check bundle format version for forward compatibility
    int bundleVersion = obj["bundle_format_version"].toInt(1);
    if (bundleVersion > BUNDLE_FORMAT_VERSION) {
        qWarning() << "Bundle was created with a newer version of SpeedyNote (format version"
                   << bundleVersion << ", current version" << BUNDLE_FORMAT_VERSION << ")."
                   << "Some features may not work correctly. Please update SpeedyNote.";
    }
    
    // Load document metadata
    auto doc = Document::fromJson(obj);
    if (!doc) {
        qWarning() << "Failed to parse document metadata";
        return nullptr;
    }
    
    // Set bundle path and enable lazy loading
    doc->m_bundlePath = path;
    doc->m_lazyLoadEnabled = true;
    
    // ========== MODE-SPECIFIC LOADING ==========
    if (doc->mode == Mode::Edgeless) {
        // Parse tile index (just coordinates, no actual loading!)
        QJsonArray tileIndexArray = obj["tile_index"].toArray();
        for (const auto& val : tileIndexArray) {
            QStringList parts = val.toString().split(',');
            if (parts.size() == 2) {
                bool okX, okY;
                int tx = parts[0].toInt(&okX);
                int ty = parts[1].toInt(&okY);
                if (okX && okY) {
                    doc->m_tileIndex.insert({tx, ty});
                }
            }
        }
        
        // Phase 5.6: Parse layer definitions from manifest
        if (obj.contains("layers")) {
            QJsonArray layersArray = obj["layers"].toArray();
            doc->m_edgelessLayers.clear();
            for (const auto& val : layersArray) {
                doc->m_edgelessLayers.push_back(LayerDefinition::fromJson(val.toObject()));
            }
            doc->m_edgelessActiveLayerIndex = obj["active_layer_index"].toInt(0);
            
            // Clamp active layer index
            if (doc->m_edgelessActiveLayerIndex >= static_cast<int>(doc->m_edgelessLayers.size())) {
                doc->m_edgelessActiveLayerIndex = qMax(0, static_cast<int>(doc->m_edgelessLayers.size()) - 1);
            }
        }
        
        // Ensure at least one layer exists for edgeless mode
        if (doc->m_edgelessLayers.empty()) {
            LayerDefinition defaultLayer;
            defaultLayer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            defaultLayer.name = "Layer 1";
            doc->m_edgelessLayers.push_back(defaultLayer);
        }
        
        // Phase 4: Parse position history from manifest
        if (obj.contains("last_position")) {
            QJsonObject lastPosObj = obj["last_position"].toObject();
            doc->m_edgelessLastPosition = QPointF(
                lastPosObj["x"].toDouble(0.0),
                lastPosObj["y"].toDouble(0.0)
            );
        }
        
        if (obj.contains("position_history")) {
            QJsonArray posHistoryArray = obj["position_history"].toArray();
            doc->m_edgelessPositionHistory.clear();
            for (const auto& val : posHistoryArray) {
                QJsonObject posObj = val.toObject();
                doc->m_edgelessPositionHistory.append(QPointF(
                    posObj["x"].toDouble(0.0),
                    posObj["y"].toDouble(0.0)
                ));
            }
        }
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "Loaded edgeless bundle from" << path << "with" << doc->m_tileIndex.size() 
                 << "tiles indexed," << doc->m_edgelessLayers.size() << "layers"
                 << "| last position:" << doc->m_edgelessLastPosition
                 << "| history size:" << doc->m_edgelessPositionHistory.size();
#endif
    } else {
        // ========== PAGED MODE LOADING (Phase O1.7.4) ==========
        // Parse page_order (just UUIDs, no actual content loading!)
        if (obj.contains("page_order")) {
            QJsonArray pageOrderArray = obj["page_order"].toArray();
            for (const auto& val : pageOrderArray) {
                doc->m_pageOrder.append(val.toString());
            }
            
            // Parse page_metadata (sizes and PDF page indices for layout/synthesis)
            if (obj.contains("page_metadata")) {
                QJsonObject pageMetadataObj = obj["page_metadata"].toObject();
                for (auto it = pageMetadataObj.begin(); it != pageMetadataObj.end(); ++it) {
                    QString uuid = it.key();
                    QJsonObject metaObj = it.value().toObject();
                    QSizeF size(metaObj["width"].toDouble(595.0), 
                               metaObj["height"].toDouble(842.0));
                    doc->m_pageMetadata[uuid] = size;
                    
                    // Parse PDF page index for pristine page synthesis
                    if (metaObj.contains("pdf_page")) {
                        doc->m_pagePdfIndex[uuid] = metaObj["pdf_page"].toInt();
                    }
                    // Parse PDF source id (non-primary pages only; empty = primary)
                    if (metaObj.contains("pdf_source")) {
                        QString src = metaObj["pdf_source"].toString();
                        if (!src.isEmpty()) {
                            doc->m_pagePdfSource[uuid] = src;
                        }
                    }
                }
            } else {
                // Fallback: assign default size to pages missing metadata
                for (const QString& uuid : doc->m_pageOrder) {
                    if (doc->m_pageMetadata.find(uuid) == doc->m_pageMetadata.end()) {
                        doc->m_pageMetadata[uuid] = QSizeF(595.0, 842.0); // A4 default
                    }
                }
            }
            
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "Loaded paged bundle from" << path << "with" 
                     << doc->m_pageOrder.size() << "pages indexed";
#endif
        } else {
            // No page_order - this shouldn't happen for paged bundles
            qWarning() << "Paged bundle missing page_order in manifest";
        }
    }
    
    // ========== RESOLVE & LOAD PDF SOURCES ==========
    // Probe all referenced sources through the same validated candidate resolver used
    // by rendering/search/export. This makes missing or corrupt non-primary sources
    // visible to the UI immediately instead of waiting for their first repaint.
    if (!doc->m_pdfSources.empty()) {
        // Preserve the established API contract: the primary provider is eager,
        // while imported sources remain path-probed and open lazily on first use.
        if (doc->primarySource()) {
            doc->providerForSource(QString());
        }
        const QVector<PdfSourceHealth> health = doc->pdfSourceHealthSnapshot();
        for (const PdfSourceHealth& item : health) {
            if (item.requiresRepair()) {
                qWarning() << "loadBundle: PDF source needs repair:"
                           << item.title << item.activePath;
            }
        }
    }
    
    return doc;
}

QString Document::peekBundleId(const QString& path)
{
    // Lightweight manifest peek - only reads enough to get the document ID
    QString manifestPath = path + "/document.json";
    
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        // Not a valid bundle or file doesn't exist
        return QString();
    }
    
    QByteArray data = manifestFile.readAll();
    manifestFile.close();
    
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QString();
    }
    
    QJsonObject obj = jsonDoc.object();
    // Try "notebook_id" first (current format), fall back to "id" (legacy)
    QString docId = obj["notebook_id"].toString();
    if (docId.isEmpty()) {
        docId = obj["id"].toString();
    }
    return docId;
}

// =============================================================================
// Edgeless Layer Manifest API (Phase 5.6)
// =============================================================================

const LayerDefinition* Document::edgelessLayerDef(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return nullptr;
    }
    return &m_edgelessLayers[index];
}

QString Document::edgelessLayerId(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return QString();
    }
    return m_edgelessLayers[index].id;
}

int Document::addEdgelessLayer(const QString& name)
{
    LayerDefinition layerDef;
    layerDef.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layerDef.name = name.isEmpty() ? QString("Layer %1").arg(m_edgelessLayers.size() + 1) : name;
    
    m_edgelessLayers.push_back(layerDef);
    m_edgelessManifestDirty = true;
    markModified();
    
    int newIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    
    // Phase 5.6.8: Add layer to all loaded tiles
    for (auto& [coord, tile] : m_tiles) {
        auto layer = std::make_unique<VectorLayer>(layerDef.name);
        layer->id = layerDef.id;
        layer->visible = layerDef.visible;
        layer->opacity = layerDef.opacity;
        layer->locked = layerDef.locked;
        tile->vectorLayers.push_back(std::move(layer));
        m_dirtyTiles.insert(coord);
    }
    
    return newIndex;
}

bool Document::removeEdgelessLayer(int index)
{
    // Don't remove the last layer
    if (m_edgelessLayers.size() <= 1) {
        return false;
    }
    
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // CR-L13: Load all evicted tiles so we remove strokes everywhere
    loadAllEvictedTiles();
    
    m_edgelessLayers.erase(m_edgelessLayers.begin() + index);
    
    // CR-L12: Properly adjust active layer index
    if (index < m_edgelessActiveLayerIndex) {
        // Removed layer was below active, shift down
        m_edgelessActiveLayerIndex--;
    } else if (m_edgelessActiveLayerIndex >= static_cast<int>(m_edgelessLayers.size())) {
        // Active was the removed layer or above
        m_edgelessActiveLayerIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    }
    if (m_edgelessActiveLayerIndex < 0) {
        m_edgelessActiveLayerIndex = 0;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    // Phase 5.6.8: Remove layer from all loaded tiles
    // Collect tile coords first since we may need to remove empty tiles
    std::vector<TileCoord> tileCoords;
    tileCoords.reserve(m_tiles.size());
    for (const auto& [coord, tile] : m_tiles) {
        tileCoords.push_back(coord);
    }
    
    for (const auto& coord : tileCoords) {
        auto it = m_tiles.find(coord);
        if (it == m_tiles.end()) continue;
        
        Page* tile = it->second.get();
        if (index < tile->layerCount()) {
            tile->removeLayer(index);
        }
        // Also adjust tile's active layer index
        if (tile->activeLayerIndex >= tile->layerCount()) {
            tile->activeLayerIndex = tile->layerCount() - 1;
        }
        m_dirtyTiles.insert(coord);
        
        // CR-L13 fix: Remove tile if it's now empty
        removeTileIfEmpty(coord.first, coord.second);
    }
    
    return true;
}

bool Document::moveEdgelessLayer(int from, int to)
{
    int count = static_cast<int>(m_edgelessLayers.size());
    
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }
    
    // Extract the layer
    LayerDefinition layer = std::move(m_edgelessLayers[from]);
    m_edgelessLayers.erase(m_edgelessLayers.begin() + from);
    m_edgelessLayers.insert(m_edgelessLayers.begin() + to, std::move(layer));
    
    // Adjust active layer index
    if (m_edgelessActiveLayerIndex == from) {
        m_edgelessActiveLayerIndex = to;
    } else if (from < m_edgelessActiveLayerIndex && to >= m_edgelessActiveLayerIndex) {
        m_edgelessActiveLayerIndex--;
    } else if (from > m_edgelessActiveLayerIndex && to <= m_edgelessActiveLayerIndex) {
        m_edgelessActiveLayerIndex++;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    // Phase 5.6.8: Move layer on all loaded tiles
    for (auto& [coord, tile] : m_tiles) {
        tile->moveLayer(from, to);
        m_dirtyTiles.insert(coord);
    }
    
    return true;
}

bool Document::mergeEdgelessLayers(int targetIndex, const QVector<int>& sourceIndices)
{
    // Validate target index
    if (targetIndex < 0 || targetIndex >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // Validate all source indices
    for (int idx : sourceIndices) {
        if (idx < 0 || idx >= static_cast<int>(m_edgelessLayers.size())) {
            return false;
        }
        if (idx == targetIndex) {
            return false;  // Can't merge layer into itself
        }
    }
    
    // Ensure we don't remove all layers
    if (sourceIndices.size() >= static_cast<int>(m_edgelessLayers.size())) {
        return false;
    }
    
    // CR-L13: Load all evicted tiles so we merge strokes everywhere
    loadAllEvictedTiles();
    
    // For each loaded tile: move strokes from source layers to target layer
    for (auto& [coord, tile] : m_tiles) {
        VectorLayer* target = tile->layer(targetIndex);
        if (!target) continue;
        
        // Collect strokes from all source layers
        for (int srcIdx : sourceIndices) {
            VectorLayer* source = tile->layer(srcIdx);
            if (source) {
                // Move all strokes to target
                for (VectorStroke& stroke : source->strokes()) {
                    target->addStroke(std::move(stroke));
                }
                source->clear();
            }
        }
        
        m_dirtyTiles.insert(coord);
    }
    
    // Remove source layers from manifest and tiles (in reverse order to preserve indices)
    QVector<int> sortedSources = sourceIndices;
    std::sort(sortedSources.begin(), sortedSources.end(), std::greater<int>());
    
    // Collect tile coords first since we may need to remove empty tiles later
    std::vector<TileCoord> tileCoords;
    tileCoords.reserve(m_tiles.size());
    for (const auto& [coord, tile] : m_tiles) {
        tileCoords.push_back(coord);
    }
    
    for (int srcIdx : sortedSources) {
        // Remove from manifest
        m_edgelessLayers.erase(m_edgelessLayers.begin() + srcIdx);
        
        // Remove from all tiles
        for (const auto& coord : tileCoords) {
            auto it = m_tiles.find(coord);
            if (it == m_tiles.end()) continue;
            
            Page* tile = it->second.get();
            if (srcIdx < tile->layerCount()) {
                tile->removeLayer(srcIdx);
            }
        }
        
        // CR-L12: Adjust active layer index when removing layers below it
        if (srcIdx < m_edgelessActiveLayerIndex) {
            m_edgelessActiveLayerIndex--;
        }
    }
    
    // CR-L13: Check for empty tiles after all layer removals
    // (In merge, strokes are moved to target, so tiles typically won't be empty,
    // but check anyway for safety and code consistency)
    for (const auto& coord : tileCoords) {
        removeTileIfEmpty(coord.first, coord.second);
    }
    
    // Final clamp in case active was one of the removed layers
    if (m_edgelessActiveLayerIndex >= static_cast<int>(m_edgelessLayers.size())) {
        m_edgelessActiveLayerIndex = static_cast<int>(m_edgelessLayers.size()) - 1;
    }
    if (m_edgelessActiveLayerIndex < 0) {
        m_edgelessActiveLayerIndex = 0;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    return true;
}

int Document::duplicateEdgelessLayer(int index)
{
    // Validate index
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return -1;
    }
    
    // Create new layer definition as a copy of the original
    LayerDefinition newDef;
    newDef.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    newDef.name = m_edgelessLayers[index].name + " Copy";
    newDef.visible = m_edgelessLayers[index].visible;
    newDef.opacity = m_edgelessLayers[index].opacity;
    newDef.locked = false;  // Unlock the copy for immediate editing
    
    // Insert at index + 1 (above the original)
    int newIndex = index + 1;
    m_edgelessLayers.insert(m_edgelessLayers.begin() + newIndex, newDef);
    
    // For each loaded tile: duplicate the layer's strokes
    for (auto& [coord, tile] : m_tiles) {
        VectorLayer* source = tile->layer(index);
        
        // Create new layer in tile at the same position
        // First, add a layer at the end, then move it to newIndex
        VectorLayer* newLayer = tile->addLayer(newDef.name);
        if (!newLayer) continue;
        
        // Copy properties
        newLayer->visible = newDef.visible;
        newLayer->opacity = newDef.opacity;
        newLayer->locked = newDef.locked;
        
        // Deep copy strokes with new UUIDs
        if (source) {
            for (const VectorStroke& stroke : source->strokes()) {
                VectorStroke copy = stroke;  // Copy all properties
                copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);  // New UUID
                newLayer->addStroke(std::move(copy));
            }
        }
        
        // Move the new layer from the end to the correct position
        int lastIndex = tile->layerCount() - 1;
        if (lastIndex != newIndex) {
            tile->moveLayer(lastIndex, newIndex);
        }
        
        m_dirtyTiles.insert(coord);
    }
    
    // Adjust active layer index if it's at or above the insertion point
    if (m_edgelessActiveLayerIndex >= newIndex) {
        m_edgelessActiveLayerIndex++;
    }
    
    m_edgelessManifestDirty = true;
    markModified();
    
    return newIndex;
}

void Document::setEdgelessLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].visible != visible) {
        m_edgelessLayers[index].visible = visible;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync visibility to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->visible = visible;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerName(int index, const QString& name)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].name != name) {
        m_edgelessLayers[index].name = name;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync name to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->name = name;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerOpacity(int index, qreal opacity)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    opacity = qBound(0.0, opacity, 1.0);
    
    if (!qFuzzyCompare(m_edgelessLayers[index].opacity, opacity)) {
        m_edgelessLayers[index].opacity = opacity;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync opacity to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->opacity = opacity;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessLayerLocked(int index, bool locked)
{
    if (index < 0 || index >= static_cast<int>(m_edgelessLayers.size())) {
        return;
    }
    
    if (m_edgelessLayers[index].locked != locked) {
        m_edgelessLayers[index].locked = locked;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync locked state to all loaded tiles
        for (auto& [coord, tile] : m_tiles) {
            if (index < tile->layerCount()) {
                VectorLayer* layer = tile->layer(index);
                if (layer) {
                    layer->locked = locked;
                    m_dirtyTiles.insert(coord);  // CR-L5: Only mark dirty if layer was updated
                }
            }
        }
    }
}

void Document::setEdgelessActiveLayerIndex(int index)
{
    // CR-L7: Defensive - handle empty layers case
    if (m_edgelessLayers.empty()) {
        return;
    }
    
    // Clamp index to valid range
    index = qBound(0, index, static_cast<int>(m_edgelessLayers.size()) - 1);
    
    if (m_edgelessActiveLayerIndex != index) {
        m_edgelessActiveLayerIndex = index;
        m_edgelessManifestDirty = true;
        markModified();
        
        // Phase 5.6.8: Sync active layer index to all loaded tiles
        // CR-L9: Don't mark tiles dirty - activeLayerIndex is stored in manifest,
        // not per-tile. In-memory sync is for runtime use only.
        for (auto& [coord, tile] : m_tiles) {
            tile->activeLayerIndex = index;
        }
    }
}

// =========================================================================
// OCR Sidecar File I/O (Phase 1A)
// =========================================================================

bool Document::savePageOcr(const QString& uuid, const Page* page)
{
    if (m_bundlePath.isEmpty() || !page)
        return false;

    QString ocrPath = m_bundlePath + "/pages/" + uuid + ".ocr.json";

    if (page->ocrTextBlocks.isEmpty() && page->suppressedStrokeIds.isEmpty()) {
        QFile::remove(ocrPath);
        return true;
    }

    QDir().mkpath(m_bundlePath + "/pages");

    QJsonObject root;
    root["version"] = 1;
    if (!page->ocrTextBlocks.isEmpty())
        root["engineId"] = page->ocrTextBlocks.first().engineId;

    QJsonArray blocks;
    for (const auto& block : page->ocrTextBlocks)
        blocks.append(block.toJson());
    root["blocks"] = blocks;

    if (!page->suppressedStrokeIds.isEmpty()) {
        QJsonArray suppressed;
        for (const auto& id : page->suppressedStrokeIds)
            suppressed.append(id);
        root["suppressedStrokeIds"] = suppressed;
    }

    QFile file(ocrPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool Document::loadPageOcr(Page* page, const QString& uuid) const
{
    if (m_bundlePath.isEmpty() || !page)
        return false;

    QString ocrPath = m_bundlePath + "/pages/" + uuid + ".ocr.json";
    QFile file(ocrPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError)
        return false;

    QJsonObject root = jsonDoc.object();
    if (root.isEmpty())
        return false;

    page->ocrTextBlocks.clear();
    page->suppressedStrokeIds.clear();

    for (const auto& val : root["blocks"].toArray())
        page->ocrTextBlocks.append(OcrTextBlock::fromJson(val.toObject()));
    for (const auto& val : root["suppressedStrokeIds"].toArray())
        page->suppressedStrokeIds.insert(val.toString());

    page->ocrDirty = false;
    return true;
}

bool Document::saveTileOcr(TileCoord coord)
{
    if (m_bundlePath.isEmpty())
        return false;

    auto it = m_tiles.find(coord);
    if (it == m_tiles.end())
        return false;

    const Page* tile = it->second.get();
    QString ocrPath = m_bundlePath + "/tiles/" +
        QString("%1,%2.ocr.json").arg(coord.first).arg(coord.second);

    if (tile->ocrTextBlocks.isEmpty() && tile->suppressedStrokeIds.isEmpty()) {
        QFile::remove(ocrPath);
        return true;
    }

    QDir().mkpath(m_bundlePath + "/tiles");

    QJsonObject root;
    root["version"] = 1;
    if (!tile->ocrTextBlocks.isEmpty())
        root["engineId"] = tile->ocrTextBlocks.first().engineId;

    QJsonArray blocks;
    for (const auto& block : tile->ocrTextBlocks)
        blocks.append(block.toJson());
    root["blocks"] = blocks;

    if (!tile->suppressedStrokeIds.isEmpty()) {
        QJsonArray suppressed;
        for (const auto& id : tile->suppressedStrokeIds)
            suppressed.append(id);
        root["suppressedStrokeIds"] = suppressed;
    }

    QFile file(ocrPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool Document::loadTileOcr(Page* tile, TileCoord coord) const
{
    if (m_bundlePath.isEmpty() || !tile)
        return false;

    QString ocrPath = m_bundlePath + "/tiles/" +
        QString("%1,%2.ocr.json").arg(coord.first).arg(coord.second);

    QFile file(ocrPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError)
        return false;

    QJsonObject root = jsonDoc.object();
    if (root.isEmpty())
        return false;

    tile->ocrTextBlocks.clear();
    tile->suppressedStrokeIds.clear();

    for (const auto& val : root["blocks"].toArray())
        tile->ocrTextBlocks.append(OcrTextBlock::fromJson(val.toObject()));
    for (const auto& val : root["suppressedStrokeIds"].toArray())
        tile->suppressedStrokeIds.insert(val.toString());

    tile->ocrDirty = false;
    return true;
}

void Document::materializeOcrTextObjects(Page* page) const
{
    if (!page || page->ocrTextBlocks.isEmpty())
        return;

    // Collect stroke IDs claimed by locked OCR objects already on the page
    QSet<QString> lockedStrokeIds;
    for (const auto& obj : page->objects) {
        if (obj && obj->type() == QStringLiteral("ocr_text")) {
            auto* ocr = static_cast<OcrTextObject*>(obj.get());
            if (ocr->ocrLocked) {
                for (const auto& sid : ocr->sourceStrokeIds)
                    lockedStrokeIds.insert(sid);
            }
        }
    }

    // Pre-compute snap rendering state once for all blocks on this page
    bool isGrid = (page->backgroundType == Page::BackgroundType::Grid);
    bool isLines = (page->backgroundType == Page::BackgroundType::Lines);
    bool pageSnap = ocrSnapToBackground && (isGrid || isLines);
    bool pageCjk = false;
    if (pageSnap && isGrid) {
        QSettings settings("SpeedyNote", "App");
        if (settings.value("ocrCjkGridMode", false).toBool()) {
            QString lang = ocrLanguage.isEmpty()
                ? settings.value("ocrLanguage").toString()
                : ocrLanguage;
            pageCjk = lang.isEmpty()
                || lang == QLatin1String("auto")
                || lang.startsWith(QLatin1String("zh"), Qt::CaseInsensitive)
                || lang.startsWith(QLatin1String("ja"), Qt::CaseInsensitive)
                || lang.startsWith(QLatin1String("ko"), Qt::CaseInsensitive);
        }
    }
    // Grid spacing only drives the CJK grid-cell overlay; non-CJK uses line
    // spacing regardless of background (matches OcrWorker line snapping).
    int snapSpacing = pageCjk ? page->gridSpacing : page->lineSpacing;

    for (const auto& block : page->ocrTextBlocks) {
        if (block.dirty || block.text.isEmpty())
            continue;

        // Skip blocks whose strokes are claimed by locked objects
        bool suppressed = false;
        if (!lockedStrokeIds.isEmpty()) {
            for (const auto& sid : block.sourceStrokeIds) {
                if (lockedStrokeIds.contains(sid)) {
                    suppressed = true;
                    break;
                }
            }
        }
        if (suppressed) continue;

        QColor color = OcrTextObject::dominantStrokeColor(page, block.sourceStrokeIds);
        auto obj = OcrTextObject::createFromBlock(block, color, m_ocrDarkMode);
        obj->visible = m_ocrTextVisible;
        obj->showConfidence = m_ocrShowConfidence;
        obj->layerAffinity = OcrTextObject::resolveLayerAffinity(page, block.sourceStrokeIds);
        obj->ocrSnapEnabled = pageSnap;
        obj->ocrGridSpacing = snapSpacing;
        obj->ocrCjkGridMode = pageCjk;

        page->addObject(std::move(obj));
    }
}

QVector<OcrTextBlock> Document::loadOcrBlocksFromFile(const QString& ocrJsonPath)
{
    QVector<OcrTextBlock> result;

    QFile file(ocrJsonPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return result;

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError)
        return result;

    QJsonObject root = jsonDoc.object();
    for (const auto& val : root["blocks"].toArray())
        result.append(OcrTextBlock::fromJson(val.toObject()));

    return result;
}
