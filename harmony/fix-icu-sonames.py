#!/usr/bin/env python3
# ============================================================================
# Normalise ICU library names in a staged HarmonyOS project
# ============================================================================
# Qt 6.12.0's prebuilt HarmonyOS libraries were linked against a vcpkg ICU that
# uses versioned sonames, so libQt6Core.so declares:
#
#     NEEDED libicuuc.so.78  libicui18n.so.78  libicudata.so.78
#
# The ohos-additional-packages bundle ships the same ICU (78.2) with
# *unversioned* sonames, and those libraries reference each other unversioned:
#
#     libicui18n.so  ->  libicuuc.so, libicudata.so
#     libicuuc.so    ->  libicudata.so
#
# harmonydeployqt renames on copy to satisfy Qt's top-level NEEDED, staging them
# as libicuuc.so.78 and friends. That fixes the first hop and breaks the second:
# the staged libicuuc.so.78 asks for a libicudata.so that is not in the HAP, so
# the whole libqohos.so -> libQt6Gui.so -> libQt6Core.so -> libicu* chain fails
# to load. The app then dies with a misleading ArkTS error, because the failed
# native module import silently evaluates to undefined:
#
#     TypeError: Cannot read property handleAbilityStageOnCreate of undefined
#
# Fix: go the other direction and make everything unversioned. The versioned
# strings are longer, so the DT_NEEDED entries can be shortened in place without
# disturbing any ELF offsets; the trailing bytes become unreachable padding.
#
# Idempotent, and a no-op once Qt and the additional-packages bundle agree on
# sonames, so it is safe to leave in the build pipeline permanently.
#
# Usage:
#   ./harmony/fix-icu-sonames.py <staged-libs-dir>
#
# Run after harmonydeployqt and before `hvigorw assembleHap`, e.g. on
#   <build>/lib<target>-harmonyos/entry/libs/arm64-v8a
# ============================================================================
import os
import re
import sys

# Matches a versioned ICU soname followed by its NUL terminator, e.g.
# b"libicuuc.so.78\x00". Restricted to ICU because it is the only component
# where Qt's expectation and the bundle's soname disagree.
NEEDED_RE = re.compile(rb"(libicu[a-z0-9]*\.so)\.([0-9]+)\x00")


def unversion_needed_entries(libdir):
    """Rewrite versioned ICU DT_NEEDED strings to unversioned, in place."""
    patched = []
    for root, _dirs, files in os.walk(libdir):
        for name in files:
            path = os.path.join(root, name)
            with open(path, "rb") as f:
                data = f.read()
            if data[:4] != b"\x7fELF":
                continue

            def shorten(match):
                # b"libicuuc.so.78\0" -> b"libicuuc.so\0" + padding, keeping the
                # total length identical so no ELF offset shifts.
                base = match.group(1)
                padding = len(match.group(0)) - len(base) - 1
                return base + b"\x00" * (padding + 1)

            new_data = NEEDED_RE.sub(shorten, data)
            if new_data == data:
                continue
            if len(new_data) != len(data):
                raise RuntimeError(f"{path}: length changed, refusing to write")
            with open(path, "wb") as f:
                f.write(new_data)
            patched.append(os.path.relpath(path, libdir))
    return patched


def unversion_filenames(libdir):
    """Rename staged libicu*.so.NN files to their unversioned names."""
    renamed = []
    for root, _dirs, files in os.walk(libdir):
        for name in list(files):
            match = re.fullmatch(r"(libicu[a-z0-9]*\.so)\.[0-9]+", name)
            if not match:
                continue
            os.replace(os.path.join(root, name),
                       os.path.join(root, match.group(1)))
            renamed.append(f"{name} -> {match.group(1)}")
    return renamed


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {os.path.basename(sys.argv[0])} <staged-libs-dir>",
              file=sys.stderr)
        return 2
    libdir = sys.argv[1]
    if not os.path.isdir(libdir):
        print(f"ERROR: not a directory: {libdir}", file=sys.stderr)
        return 1

    patched = unversion_needed_entries(libdir)
    renamed = unversion_filenames(libdir)

    if not patched and not renamed:
        print("ICU sonames already consistent, nothing to do.")
        return 0
    print("Patched DT_NEEDED in: " + (", ".join(patched) or "(none)"))
    print("Renamed: " + (", ".join(renamed) or "(none)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
