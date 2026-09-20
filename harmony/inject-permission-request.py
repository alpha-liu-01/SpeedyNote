#!/usr/bin/env python3
# ============================================================================
# Request SpeedyNote's user-granted permissions from the generated ArkTS
# ============================================================================
# ohos.permission.READ_WRITE_DOCUMENTS_DIRECTORY is what allows SpeedyNote to
# read and write the user's Documents folder on 2-in-1 devices. Declaring it in
# module.json5 is necessary but not sufficient: it is a *user-granted*
# permission, so it stays ungranted (`bm dump` reports state -1) until the app
# asks for it with abilityAccessCtrl.requestPermissionsFromUser().
#
# There is no way to do that from C++. Requesting is ArkTS-only:
# Qt's own QPermission types cover camera/microphone/location and the like, and
# the HarmonyOS QPA plugin wires up only READ_PASTEBOARD and
# CUSTOM_SCREEN_CAPTURE for itself, with no general entry point. So the request
# has to live in the generated ability.
#
# Why patch generated code instead of using QT_HARMONYOS_PACKAGE_SOURCE_DIR:
# that property *replaces* Qt's template rather than overlaying it
# (harmonydeployqt copies the whole directory and validates that
# entry/src/main/module.json5 exists), so using it means vendoring all ~17 .ets
# files plus the json5 and resources, and re-vendoring them on every Qt update.
# For a single API call that trade is not worth it while Qt 6.12 is still young.
#
# Revisit if Qt gains a general permission API, or if we end up needing enough
# ArkTS of our own (NAPI bridges for pickers, sharing, Core Vision Kit OCR) that
# vendoring the template becomes worthwhile anyway. At that point this script
# should be deleted rather than extended.
#
# The patch is idempotent, and deliberately fails loudly if the anchor is
# missing, because a silent no-op would resurface as "saving is broken again"
# after a Qt upgrade.
#
# Usage:
#   ./harmony/inject-permission-request.py <deveco-project-dir>
#
# Run after harmonydeployqt and before `hvigorw assembleHap`.
# ============================================================================
import os
import sys

MARKER = "SPEEDYNOTE_PERMISSION_REQUEST"

PERMISSIONS = ["ohos.permission.READ_WRITE_DOCUMENTS_DIRECTORY"]

IMPORT_ANCHOR = "import hilog from '@ohos.hilog';"
# Permissions and PermissionRequestResult are *named* exports sitting alongside
# the default one; they are not members of the abilityAccessCtrl namespace, so
# abilityAccessCtrl.PermissionRequestResult does not compile.
IMPORT_ADDITION = ("import abilityAccessCtrl, { Permissions, PermissionRequestResult } "
                   "from '@ohos.abilityAccessCtrl';")

# Anchored on the qpa call rather than on 'onCreate(' so the request is inserted
# after Qt's app context exists but before Qt takes over.
CALL_ANCHOR = "    qpa.handleAbilityOnCreate(this, want, launchParam);"

CALL_ADDITION = """    // {marker}: added by harmony/inject-permission-request.py.
    // Fired without awaiting: the sandbox mount for the user's Documents folder
    // is established at process start, so a grant only takes effect on the next
    // launch either way, and blocking Qt's startup on a dialog would be worse.
    // HarmonyEnvironment::userDocumentsDir() reports the outcome on the C++ side.
    //
    // The annotation is required: Permissions is a closed union of string
    // literals, and ArkTS will not widen a bare string[] to Array<Permissions>.
    const speedynotePermissions: Array<Permissions> = {permissions};
    abilityAccessCtrl.createAtManager()
      .requestPermissionsFromUser(this.context, speedynotePermissions)
      .then((result: PermissionRequestResult) => {{
        hilog.info(LOG_DOMAIN, LOG_TAG,
          'SpeedyNote: permission authResults: ' + JSON.stringify(result.authResults));
      }})
      .catch((err: Error) => {{
        hilog.error(LOG_DOMAIN, LOG_TAG,
          'SpeedyNote: permission request failed: ' + JSON.stringify(err));
      }});
"""


def patch_ability(path):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    if MARKER in text:
        return False

    if IMPORT_ANCHOR not in text:
        raise RuntimeError(
            f"{path}: could not find the import anchor\n  {IMPORT_ANCHOR}\n"
            "Qt's ability template changed; update this script.")
    if CALL_ANCHOR not in text:
        raise RuntimeError(
            f"{path}: could not find the onCreate anchor\n  {CALL_ANCHOR}\n"
            "Qt's ability template changed; update this script.")

    permissions = "[" + ", ".join(f"'{p}'" for p in PERMISSIONS) + "]"
    addition = CALL_ADDITION.format(marker=MARKER, permissions=permissions)

    text = text.replace(IMPORT_ANCHOR, IMPORT_ANCHOR + "\n" + IMPORT_ADDITION, 1)
    text = text.replace(CALL_ANCHOR, CALL_ANCHOR + "\n" + addition, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {os.path.basename(sys.argv[0])} <deveco-project-dir>",
              file=sys.stderr)
        return 2

    ability = os.path.join(sys.argv[1], "entry", "src", "main", "ets",
                           "qability", "QAbility.ets")
    if not os.path.isfile(ability):
        print(f"ERROR: not found: {ability}", file=sys.stderr)
        return 1

    if patch_ability(ability):
        print("Requesting at startup: " + ", ".join(PERMISSIONS))
    else:
        print("Permission request already present, nothing to do.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
