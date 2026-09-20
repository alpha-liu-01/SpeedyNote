#pragma once

// ============================================================================
// HarmonyOS persistent file grants
// ============================================================================
// The other half of the storage story that HarmonyEnvironment describes. When
// OH_Environment_GetUserDocumentDir() is unavailable -- every tablet before API
// 26.0.0, so every tablet in circulation -- we cannot reach the user's folders by
// path. We can still reach individual files the user picks, because QFileDialog on
// HarmonyOS *is* the system DocumentViewPicker and picking grants a read/write
// permission on that file. The grant is temporary: it dies with the process, which
// is exactly why a notebook reopened tomorrow cannot find the PDF it was born
// from, however correct the stored path looks.
//
// ohos.permission.FILE_ACCESS_PERSIST plus the OH_FileShare_* NDK is meant to fix
// that: persist a picked URI once, re-activate it at every launch, and the path
// keeps working. It is all C, so unlike the picker itself it needs no ArkTS bridge,
// and the permission is system_grant at normal level -- declaring it in
// module.json5 is the whole of obtaining it, with no user prompt and no ACL.
//
// Whether it works on a tablet is the open question, and the sources contradict
// each other: Huawei's C API reference lists oh_file_share.h for Phone, Tablet and
// PC/2in1, while the OpenHarmony guide for the ArkTS equivalent still says the
// persistence APIs "are available only for 2-in-1 devices". The emulator cannot
// settle it -- it reports every syscap, as it did for FolderObtain -- so this
// module exists to be read off a retail tablet's About tab by a tester.
//
// Nothing here reads or writes user data yet. It is a capability probe, and the
// answer decides whether attaching a PDF outside the sandbox can keep referring to
// it in place, or must copy it into the notebook bundle.
// ============================================================================

#include <QString>

namespace HarmonyPersistentGrant {

// True when this device supports persistent URI grants, i.e. the FolderAuthorization
// syscap is present and FILE_ACCESS_PERSIST was granted. Cached after first call.
bool isSupported();

// One line describing what the probe saw, for the About tab. Deliberately not
// translated, for the same reason as HarmonyEnvironment::accessFailureDetail():
// it exists to be read back to us out of a tester's screenshot. Never empty.
QString capabilityDetail();

} // namespace HarmonyPersistentGrant
