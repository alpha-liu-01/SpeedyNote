#!/usr/bin/env python3
# ============================================================================
# Add localized app labels to the generated DevEco project
# ============================================================================
# HarmonyOS resolves $string:app_name and $string:QAbility_label against the
# system locale by looking for a matching resources/<qualifier>/element
# directory and falling back to resources/base. Qt's CMake support exposes only
# a single QT_HARMONYOS_APP_LABEL, which lands in resources/base -- so without
# this step a Chinese-locale device shows the English name, on the platform
# whose entire market is Chinese-speaking.
#
# Same reasoning as inject-permission-request.py for patching generated output
# rather than setting QT_HARMONYOS_PACKAGE_SOURCE_DIR: that property replaces
# Qt's template wholesale, so using it would mean vendoring every .ets file and
# re-vendoring them on each Qt update. Adding a resource directory is additive
# and touches nothing Qt generates, which makes it the cheaper half of that
# trade.
#
# Idempotent, and fails loudly if the generated layout is not what we expect,
# because a silent no-op would show up as "the Chinese name disappeared"
# several Qt versions later.
#
# Usage:
#   ./harmony/add-localized-labels.py <deveco-project-dir>
#
# Run after harmonydeployqt and before `hvigorw assembleHap`.
# ============================================================================
import json
import os
import sys

# Outer keys are HarmonyOS resource qualifiers, so the language_script_region
# form the platform expects, not Qt's locale names. Inner keys are the resource
# names as the manifests reference them: app_name from AppScope/app.json5, the
# rest from entry/src/main/module.json5.
#
# zh_CN only: SpeedyNote's Chinese translation is Simplified, and Traditional
# locales (zh_TW, zh_HK) would be a mistranslation rather than a courtesy, so
# they are left to fall back to base like every other locale.
#
# The permission reason is ours (declared through the target's permission
# properties) and is shown verbatim in the system's grant dialog, which is the
# one piece of SpeedyNote's UI the platform renders rather than Qt.
LOCALIZATIONS = {
    "zh_CN": {
        "app": {
            "app_name": "极疾记",
        },
        "entry": {
            "QAbility_label": "极疾记",
            "qt_permission_reason_read_write_documents_directory":
                "用于在“文档”文件夹中打开和保存笔记本。",
        },
    },
}


def write_string_resource(resources_dir, qualifier, entries):
    """Merge entries into resources/<qualifier>/element/string.json.

    Merging rather than overwriting: harmonydeployqt owns nothing in these
    directories today, but a future Qt might emit its own localized strings
    here, and clobbering them would be a silent regression.
    """
    if not entries:
        return False

    element_dir = os.path.join(resources_dir, qualifier, "element")
    os.makedirs(element_dir, exist_ok=True)
    path = os.path.join(element_dir, "string.json")

    strings = []
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            strings = json.load(f).get("string", [])

    by_name = {item["name"]: item for item in strings}
    changed = False
    for name, value in entries.items():
        if by_name.get(name, {}).get("value") != value:
            changed = True
        by_name[name] = {"name": name, "value": value}

    if not changed:
        return False

    with open(path, "w", encoding="utf-8") as f:
        json.dump({"string": list(by_name.values())}, f,
                  ensure_ascii=False, indent=2)
        f.write("\n")
    return True


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {os.path.basename(sys.argv[0])} <deveco-project-dir>",
              file=sys.stderr)
        return 2

    project = sys.argv[1]
    # app.json5's label, shown in the launcher and app info.
    app_resources = os.path.join(project, "AppScope", "resources")
    # The ability's own label. Both exist and both are user-visible, so both
    # need the translation or the app ends up half-renamed.
    entry_resources = os.path.join(project, "entry", "src", "main", "resources")

    for resources_dir in (app_resources, entry_resources):
        if not os.path.isdir(os.path.join(resources_dir, "base", "element")):
            print(f"ERROR: expected resource layout not found under {resources_dir}\n"
                  "Qt's project template changed; update this script.",
                  file=sys.stderr)
            return 1

    wrote = []
    for qualifier, entries in LOCALIZATIONS.items():
        touched = write_string_resource(app_resources, qualifier,
                                        entries.get("app", {}))
        touched |= write_string_resource(entry_resources, qualifier,
                                         entries.get("entry", {}))
        if touched:
            wrote.append(f"{qualifier} ({entries['app']['app_name']})")

    if wrote:
        print("Localized strings added for: " + ", ".join(wrote))
    else:
        print("Localized strings already present, nothing to do.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
