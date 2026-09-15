#pragma once

// ============================================================================
// File writer for .snb bundle contents
// ============================================================================
// Everywhere except HarmonyOS this is a plain QSaveFile: the data goes to a
// temporary file in the same directory and is renamed over the target on
// commit(), so an interrupted save can never leave a half-written page, tile or
// manifest behind.
//
// HarmonyOS user folders cannot do that. The sharefs layer backing
// /storage/Users/<user> allows creating and writing files but refuses
// rename(2), so commit() fails with EACCES. Worse, QSaveFile deletes its
// temporary when the commit fails, so a bundle save used to produce the whole
// directory tree with not a single file inside it.
//
// So on HarmonyOS this degrades to writing straight to the target file. The
// cost is real and worth stating plainly: a save interrupted by a crash or a
// kill can leave a truncated file, where other platforms would still have the
// previous version. There is no way around it while the bundle lives in a user
// folder -- the atomic swap needs the rename that filesystem denies. Bundles
// kept in app-private storage are not affected, since QSaveFile is used there.
// ============================================================================

#include <QtGlobal>

#ifdef Q_OS_HARMONY

#include <QFile>

class BundleFile : public QFile
{
public:
    using QFile::QFile;

    // Accepted and ignored: this class only ever writes directly, so there is
    // no temporary file whose failure could trigger a fallback. Kept so the
    // call sites stay identical across platforms.
    void setDirectWriteFallback(bool) {}

    bool commit()
    {
        const bool flushed = flush();
        close();
        return flushed && error() == QFileDevice::NoError;
    }

    void cancelWriting()
    {
        // Nothing to discard, so the partially written target has to go. Unlike
        // QSaveFile this cannot restore a previous version; there is none left.
        close();
        remove();
    }
};

#else

#include <QSaveFile>

using BundleFile = QSaveFile;

#endif
