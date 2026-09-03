#pragma once

// ============================================================================
// MuPdfLocks - process-wide lock handlers for MuPDF contexts
// ============================================================================
// MuPDF routes OpenJPEG, HarfBuzz and FreeType allocations through plain
// global variables (opj_secret, fz_hb_secret, ftmemory.user) that hold the
// fz_context of whichever thread is currently inside those libraries.  Access
// is serialised by fz_lock()/fz_unlock(), which dispatch through the
// fz_locks_context handed to fz_new_context().  Passing NULL there selects
// fz_locks_default, whose lock/unlock are empty no-ops, so concurrent renders
// stomp on the globals and a worker ends up calling fz_free(NULL, ptr).
//
// From MuPDF's own source/fitz/load-jpx.c: "It is therefore vital that any
// fz_lock/fz_unlock handlers are shared between all the fz_contexts in use at
// a time."  Every fz_new_context() call in SpeedyNote must therefore pass
// snMuPdfLocks(), which is backed by one static QMutex array.
//
// Only MuPDF's small critical sections serialise (JPEG2000 decode, FreeType
// and HarfBuzz access, allocation refcounting); page loading, rasterisation
// and pixel copying still run in parallel across threads.
// ============================================================================

// fz_locks_context is a typedef of an unnamed struct, so it cannot be forward
// declared - the MuPDF header is required here.
#include <mupdf/fitz.h>

/**
 * @brief Lock handlers shared by every MuPDF context in the process.
 *
 * Pass the result as the `locks` argument of fz_new_context(). Never pass
 * nullptr: that selects MuPDF's no-op defaults and reintroduces the race.
 */
fz_locks_context* snMuPdfLocks();
