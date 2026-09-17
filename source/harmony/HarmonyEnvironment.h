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
//     .FolderObtain, and
//   - ohos.permission.READ_WRITE_DOCUMENTS_DIRECTORY has been granted. It is
//     user-granted, so it is declared in module.json5 *and* requested at
//     startup from the ArkTS side.
//
// Which devices provide the syscap is narrower than it looks, and the emulator
// will tell you otherwise: ours reports deviceType=tablet and offers the whole
// syscap set, permission dialog and all, because it is an OpenHarmony image
// rather than a retail one. Huawei documents the capability as 2-in-1 only, with
// tablets from API 26.0.0 -- and HarmonyOS 6.1, which is what this app requires
// to install at all, is API 23. So on every tablet in circulation the probe
// below returns ERR_DEVICE_NOT_SUPPORTED and the sandbox fallback is what runs;
// a retail MatePad confirmed it. Nothing here needs changing when that shifts,
// since it is a runtime probe and not a device-type test.
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

// Why that path is unavailable, or empty when it is available. Deliberately not
// translated: it exists to be read back to us out of a screenshot. Testers cannot
// run hilog, and the two causes need opposite advice -- rc=801 is a device with no
// user-folder support and nothing to be done, rc=201 is a device that has it and a
// permission that was refused, which the user can still grant in Settings.
QString accessFailureDetail();

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
