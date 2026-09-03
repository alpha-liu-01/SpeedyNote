// ============================================================================
// MuPdfLocks - process-wide lock handlers for MuPDF contexts
// ============================================================================

#include "MuPdfLocks.h"

#include <QMutex>

namespace {

QMutex s_locks[FZ_LOCK_MAX];

void snMuPdfLock(void * /*user*/, int lock)
{
    s_locks[lock].lock();
}

void snMuPdfUnlock(void * /*user*/, int lock)
{
    s_locks[lock].unlock();
}

fz_locks_context s_locksContext = {
    nullptr,
    snMuPdfLock,
    snMuPdfUnlock
};

} // namespace

fz_locks_context* snMuPdfLocks()
{
    return &s_locksContext;
}
