// ============================================================================
// PdfMaterializer - graft chosen origin PDF pages into a bundled mini-PDF.
// ============================================================================

#include "PdfMaterializer.h"

#ifdef SPEEDYNOTE_MUPDF_EXPORT

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

bool PdfMaterializer::materialize(const QString& originPath,
                                  const QString& bundledAbsPath,
                                  const QList<int>& originalPagesToAdd,
                                  QHash<int, int>& pageMap,
                                  QString* errorOut)
{
    auto setErr = [&](const QString& m) { if (errorOut) *errorOut = m; };

    const bool miniExists = QFileInfo::exists(bundledAbsPath);

    // Only pages not already mapped need grafting (incremental append).
    QList<int> toAdd;
    for (int p : originalPagesToAdd) {
        if (p >= 0 && !pageMap.contains(p)) {
            toAdd.append(p);
        }
    }
    std::sort(toAdd.begin(), toAdd.end());
    toAdd.erase(std::unique(toAdd.begin(), toAdd.end()), toAdd.end());

    if (toAdd.isEmpty()) {
        // Everything referenced is already in the mini-PDF (or nothing to do).
        return miniExists;
    }

    // Need the origin to graft new pages. If it's gone, keep whatever exists.
    if (!QFileInfo::exists(originPath)) {
        setErr(QStringLiteral("Origin PDF unavailable: %1").arg(originPath));
        return false;
    }

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        setErr(QStringLiteral("Failed to create MuPDF context"));
        return false;
    }

    const QString tmpPath = bundledAbsPath + QStringLiteral(".tmp");
    const QByteArray originUtf8 = originPath.toUtf8();
    const QByteArray existingUtf8 = bundledAbsPath.toUtf8();
    const QByteArray tmpUtf8 = tmpPath.toUtf8();

    // Work on a copy so a mid-way failure never corrupts the caller's map.
    QHash<int, int> newMap = pageMap;

    fz_document* originDoc = nullptr;
    pdf_document* originPdf = nullptr;
    fz_document* destDocFz = nullptr;   // set only when appending to an existing mini-PDF
    pdf_document* destPdf = nullptr;
    pdf_graft_map* graft = nullptr;
    bool ok = false;

    fz_var(originDoc);
    fz_var(originPdf);
    fz_var(destDocFz);
    fz_var(destPdf);
    fz_var(graft);
    fz_var(ok);

    fz_try(ctx) {
        fz_register_document_handlers(ctx);

        // Open the origin PDF.
        originDoc = fz_open_document(ctx, originUtf8.constData());
        if (fz_needs_password(ctx, originDoc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "origin PDF is password-protected");
        }
        originPdf = pdf_document_from_fz_document(ctx, originDoc);
        if (!originPdf) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "origin file is not a PDF");
        }
        const int originCount = pdf_count_pages(ctx, originPdf);

        // Open the existing mini-PDF for append, or create a fresh one.
        int baseIndex = 0;
        if (miniExists) {
            destDocFz = fz_open_document(ctx, existingUtf8.constData());
            destPdf = pdf_document_from_fz_document(ctx, destDocFz);
            if (!destPdf) {
                fz_throw(ctx, FZ_ERROR_GENERIC, "existing mini-PDF is not a PDF");
            }
            baseIndex = pdf_count_pages(ctx, destPdf);
        } else {
            destPdf = pdf_create_document(ctx);
            baseIndex = 0;
        }

        graft = pdf_new_graft_map(ctx, destPdf);

        int nextIndex = baseIndex;
        for (int p : toAdd) {
            if (p < 0 || p >= originCount) {
                continue;  // out of range in the origin - leave unmapped
            }
            pdf_graft_mapped_page(ctx, graft, -1, originPdf, p);
            newMap.insert(p, nextIndex);
            ++nextIndex;
        }

        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        opts.do_compress_images = 1;
        opts.do_compress_fonts = 1;
        pdf_save_document(ctx, destPdf, tmpUtf8.constData(), &opts);

        ok = true;
    }
    fz_always(ctx) {
        if (graft) {
            pdf_drop_graft_map(ctx, graft);
        }
        // destPdf aliases destDocFz when opened from a file; drop once via the
        // owning handle. A freshly created document has no fz owner.
        if (destDocFz) {
            fz_drop_document(ctx, destDocFz);
        } else if (destPdf) {
            pdf_drop_document(ctx, destPdf);
        }
        if (originDoc) {
            fz_drop_document(ctx, originDoc);
        }
    }
    fz_catch(ctx) {
        setErr(QString::fromUtf8(fz_caught_message(ctx)));
        ok = false;
    }

    fz_drop_context(ctx);

    if (!ok) {
        QFile::remove(tmpPath);
        // The previous mini-PDF remains intact, but the requested new pages were
        // not materialized, so the caller must treat finalization as incomplete.
        return false;
    }

    // Commit through QSaveFile so a failed replacement cannot destroy the
    // previously valid mini-PDF. MuPDF must write its own temporary path first.
    QFile generated(tmpPath);
    QSaveFile target(bundledAbsPath);
    target.setDirectWriteFallback(false);
    if (!generated.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
        const QString detail = !generated.isOpen()
            ? generated.errorString() : target.errorString();
        QFile::remove(tmpPath);
        setErr(QStringLiteral("Could not prepare mini-PDF replacement: %1").arg(detail));
        return false;
    }
    constexpr qint64 COPY_CHUNK_SIZE = 1024 * 1024;
    bool copied = true;
    while (!generated.atEnd()) {
        const QByteArray chunk = generated.read(COPY_CHUNK_SIZE);
        if (chunk.isEmpty() && generated.error() != QFileDevice::NoError) {
            copied = false;
            break;
        }
        if (target.write(chunk) != chunk.size()) {
            copied = false;
            break;
        }
    }
    generated.close();
    if (!copied || !target.commit()) {
        const QString detail = target.errorString();
        target.cancelWriting();
        QFile::remove(tmpPath);
        setErr(QStringLiteral("Could not finalize mini-PDF: %1").arg(detail));
        return false;
    }
    QFile::remove(tmpPath);

    pageMap = newMap;
    return true;
}

#else  // SPEEDYNOTE_MUPDF_EXPORT not defined

bool PdfMaterializer::materialize(const QString&, const QString&,
                                  const QList<int>&, QHash<int, int>&,
                                  QString* errorOut)
{
    if (errorOut) {
        *errorOut = QStringLiteral("PDF materialization requires MuPDF export support");
    }
    return false;
}

#endif // SPEEDYNOTE_MUPDF_EXPORT
