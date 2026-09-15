#pragma once

// ============================================================================
// HarmonyOS user folder access
// ============================================================================
// On HarmonyOS the app sandbox is an iOS-style hard boundary, but 2-in-1 devices
// (MateBook, MatePad Edge) relax it for the user's own document folders and
// expose them through a plain C NDK API. The returned paths are sandbox-mapped
// views of the real directories, and they behave like ordinary directory trees:
// QDir, QFileDialog, mkpath and our .snb bundle format all work over them
// unmodified. That is what lets SpeedyNote keep its desktop file-dialog flow
// here instead of needing a DocumentViewPicker bridge.
//
// Two things must both be true for this to work, and either can fail:
//   - the device provides SystemCapability.FileManagement.File.Environment
//     .FolderObtain (2-in-1 does; phones do not), and
//   - ohos.permission.READ_WRITE_DOCUMENTS_DIRECTORY has been granted. It is
//     user-granted, so it is declared in module.json5 *and* requested at
//     startup from the ArkTS side.
// Callers must therefore treat an empty return as normal and fall back to the
// app-private sandbox rather than surfacing an error.
// ============================================================================

#include <QString>

namespace HarmonyEnvironment {

// The user's Documents folder, or an empty string when the device does not
// support user folders or the permission has not been granted. Result is cached
// after the first successful call.
QString userDocumentsDir();

// True when userDocumentsDir() currently yields a usable, writable path.
bool hasUserDocumentsAccess();

// Where file dialogs should start, and the one to use as a fallback: the user's
// real Documents folder when we can reach it, otherwise a notebooks folder in
// app-private storage, which is always writable. Never returns an empty string.
//
// The distinction matters because QDir::homePath() on HarmonyOS points into
// /storage/Users/<user>, which exists and is listable but is not writable
// without the permission -- so using it unconditionally yields a save dialog
// that succeeds followed by a write that fails.
QString writableDocumentsRoot();

} // namespace HarmonyEnvironment
