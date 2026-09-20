#pragma once

// ============================================================================
// Bringing a picked PDF within reach on HarmonyOS
// ============================================================================
// QFileDialog on HarmonyOS *is* the system DocumentViewPicker, so picking a PDF
// works even from folders the sandbox otherwise hides -- and hands back an
// ordinary path we can open. What it does not hand back is a grant that outlives
// the process. Next launch, the notebook's stored path still names a real file
// that we are no longer allowed to read, which is why a tester's PDF notebook
// "can never be relinked": relinking succeeds, then expires.
//
// Android and iOS solved this years ago by copying the picked file into
// app-private storage inside the callback where the grant is still live, so the
// path a notebook stores is always one we own (PdfPickerAndroid, PdfPickerIOS,
// both landing in <AppData>/pdfs). HarmonyOS is the only sandboxed platform that
// took the desktop path instead, storing the foreign path verbatim. This puts it
// on the same footing, and deliberately reuses that same directory so the
// existing cleanup in Launcher::findImportedPdfPath() applies unchanged.
//
// The alternative -- keeping the file in place and persisting the picker's grant
// via OH_FileShare_PersistPermission -- is what HarmonyPersistentGrant probes
// for. It is 2in1-only in the SDK's device-define data, so copying is the only
// thing that works on the tablets people actually own. When the probe starts
// reporting otherwise, this is where the decision lives.
// ============================================================================

#include <QString>

namespace HarmonyPdfImport {

// A path to the picked PDF that will still be readable after a restart, copying
// it into app-private storage when that is the only way to guarantee it.
//
// Returns the input unchanged when no copy is needed: on a device with real user
// folder access, and for files already inside app-private storage. Also returns
// the input when the copy fails, which keeps this session working exactly as it
// does today rather than turning a degraded case into a broken one.
QString ensureReachable(const QString& pickedPath);

} // namespace HarmonyPdfImport
