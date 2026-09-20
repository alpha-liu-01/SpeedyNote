#pragma once

// ============================================================================
// Where to offer to put a user-visible file
// ============================================================================
// QStandardPaths::DocumentsLocation is the obvious answer and the wrong one on
// HarmonyOS, where it resolves to /storage/Users/<user>/Documents: a directory that
// exists, lists, and cannot be written to unless the device grants user-folder
// access, which no tablet before API 26.0.0 does. Defaulting a save or an import
// there produces the worst kind of failure -- a dialog that accepts the path and a
// write that then fails, or a file browser showing a folder the picker cannot reach.
//
// So anything choosing a default directory for the user's own files should ask here
// instead. On HarmonyOS this defers to HarmonyEnvironment::writableDocumentsRoot(),
// which returns the real Documents folder where it is reachable and app-private
// storage where it is not; everywhere else it is DocumentsLocation unchanged.
// ============================================================================

#include <QStandardPaths>
#include <QString>

#ifdef Q_OS_HARMONY
#include "../harmony/HarmonyEnvironment.h"
#endif

namespace PlatformPaths {

// A Documents-like directory that is safe to write to. Never empty.
inline QString documentsDirectory()
{
#ifdef Q_OS_HARMONY
    return HarmonyEnvironment::writableDocumentsRoot();
#else
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
}

// Where to put notebooks by default: a SpeedyNote folder inside the above, except
// when that is already app-private storage set aside for notebooks, where nesting
// another folder in it would only lengthen the path.
inline QString notebooksDirectory()
{
#ifdef Q_OS_HARMONY
    if (!HarmonyEnvironment::hasUserDocumentsAccess()) {
        return HarmonyEnvironment::writableDocumentsRoot();
    }
#endif
    return documentsDirectory() + QStringLiteral("/SpeedyNote");
}

} // namespace PlatformPaths
