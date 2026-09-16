# SpeedyNote for HarmonyOS — Alpha Tester Guide

Thank you for helping test the first HarmonyOS build of SpeedyNote. This guide covers installing
it on a retail HarmonyOS NEXT tablet, what to look at first, and what is already known to be
missing so you do not spend time reporting it.

This is an **alpha**. The app is feature-complete for note-taking and PDF annotation on this
platform, but two things have never been tested on real hardware — stylus input above all — and
one of them is the reason you are here.

---

## What you are given

A single file:

```
SpeedyNote-1.6.3-harmonyos-arm64.hap
```

Everything is inside it. There is nothing else to download and nothing to build.

The file is **unsigned**, and that is not an oversight. HarmonyOS will only install an app whose
signature is tied to the specific tablet it is being installed on, so there is no such thing as
a pre-signed file that works on everyone's device. Instead you sign it yourself, once, with a
free Huawei developer account and a community tool that automates the whole thing. The setup
below is a one-time cost; after that, new builds install over the top in seconds.

> **This is not the iOS situation.** There is no seven-day expiry and no re-signing treadmill.
> Once SpeedyNote is installed it keeps working indefinitely, even after your certificate
> expires. A certificate only matters at the moment you install something new.

---

## What you need

| | |
|---|---|
| Tablet | Retail HarmonyOS NEXT 6.x tablet |
| Stylus | M-Pencil — the main thing being tested |
| Account | A Huawei developer account (free registration, no Apple-style fee) |
| Computer | Windows or macOS. There is also an Android phone build of the signing tool if you would rather not use a computer |
| Tool | [`auto-installer`](https://github.com/likuai2010/auto-installer) (小白调试助手), the community signing and install tool |

---

## One-time setup

**1. Turn on Developer Mode on the tablet.**

Settings → About this tablet → tap the build/version number seven times. Then Settings → System
→ Developer options, and enable **USB debugging**. This is the same ritual as Android.

**2. Install the signing tool and sign in.**

Install `auto-installer` on your computer and log in with your own Huawei developer account. The
tool needs your account because it mints the certificate on your behalf — the certificate belongs
to you, not to us, which is what makes this scale past a handful of testers.

**3. Connect the tablet and let the tool do the work.**

With the tablet plugged in and USB debugging authorised, point the tool at
`SpeedyNote-1.6.3-harmonyos-arm64.hap`. It reads your device's ID, mints a debug certificate and
profile bound to it, signs the file, and installs it. SpeedyNote then appears in your app list
like any other app.

If the tool reports a signature error such as `9568332 sign info inconsistent`, it means the file
was installed with the wrong certificate rather than yours — let the tool re-sign it rather than
trying to install the file directly.

---

## First launch: grant the storage permission

On first launch SpeedyNote asks for permission to read and write your **Documents** folder.
**Please accept it.**

This one matters more than a typical permission prompt. HarmonyOS sandboxes apps strictly, and
without this permission SpeedyNote cannot save at all — and the failures do not look like a
permission problem. They look like the app losing your work. If you dismissed the prompt by
accident, re-grant it at Settings → Apps → SpeedyNote → Permissions.

Your notebooks live in your real Documents folder, so they are visible in the Files app and you
can move or back them up normally.

---

## What to test, in priority order

**1. The stylus. This is the point of the alpha.** SpeedyNote's headline feature is high-rate
pen input, and none of it has ever run on real hardware — every test so far was on an emulator
with no pen device at all. Please try:

- Pressure sensitivity: does line thickness respond smoothly, and over the full range?
- Tilt, if you use brush tools that respond to it.
- Latency: does ink keep up with the pen, or lag behind it?
- Palm rejection: can you rest your hand on the screen while writing?
- The M-Pencil's button and any double-tap gesture.

**2. Handwriting and PDFs.** Open a PDF, annotate it, navigate pages, pinch to zoom, two-finger
pan. Zoom and pan behaviour on a touchscreen is worth attention.

**3. Saving and reopening.** Create notebooks, close and reopen them, and confirm nothing is
lost — particularly `.snb` notebooks, which are folders rather than single files and needed
special handling on this platform.

**4. Moving around the app.** Switching between the notebook library and an open notebook,
opening dialogs and menus, the settings screen.

---

## Already known — no need to report these

| Thing | Status |
|---|---|
| Command-line features (`--export`, batch operations) | Will never work here. HarmonyOS apps cannot be command-line programs. |
| OCR / text recognition | Not available. No recognition engine has been ported to this platform yet. |
| The fullscreen button | Does nothing. HarmonyOS ignores the request; the app cannot force it. |
| Submenus in the library (Export, Move to Folder) | Cannot be opened. The system closes the whole menu when a second menu tries to open. |
| System notifications | Not implemented. |
| Squeezed or cramped dialog layouts | Known, being worked on. Worth reporting only if a dialog is unusable rather than merely ugly. |

**Please keep the tablet in normal tablet mode. Do not turn on PC mode.** The windowed desktop
mode that HarmonyOS offers on tablets is currently broken in this build — it is a known
regression, not a bug worth reporting, and everything above assumes normal tablet mode.

---

## Two risks worth knowing before you rely on it

**Saving is not crash-proof on this platform.** On every other platform SpeedyNote saves by
writing a temporary file and swapping it in, so an interrupted save cannot damage the original.
HarmonyOS forbids that operation inside your Documents folder, so saves write in place. A crash
*during* a save can leave a notebook incomplete. Please do not put irreplaceable work into this
build yet, and keep backups of anything you care about.

**Stylus behaviour is entirely unverified**, per above. If it misbehaves, that is expected
information rather than a surprise — it is exactly what we need to hear about.

---

## Reporting a problem

Please include the tablet model, the HarmonyOS version, and what you did leading up to it. A
short screen recording is usually worth more than a description, especially for anything about
ink, latency, or a dialog appearing in the wrong place.

If you are comfortable with a terminal, logs help a great deal. With the tablet connected:

```bash
# App log — start this, reproduce the problem, then stop it with Ctrl-C
hdc shell hilog -x | grep -i speedynote > speedynote-log.txt
```

If the app closed itself unexpectedly, there may also be a crash report:

```bash
hdc shell ls /data/log/faultlog/faultlogger/
hdc file recv /data/log/faultlog/faultlogger/<the newest speedynote entry> .
```

The app's internal name is `org.speedynote.speedynote`, which is what to look for in log output.

---

## Updates

New builds arrive as another `.hap` file. Sign and install it the same way — it installs over the
top, and your notebooks and settings are preserved. You do not need to uninstall first.

Your signing certificate lasts about 180 days. When it expires, the copy of SpeedyNote already on
your tablet keeps running normally; you will only need to renew before installing a *new* build.

---

## See Also

- [Building SpeedyNote for HarmonyOS](build_docs/HARMONYOS_BUILD_GUIDE.md) — for building from
  source rather than installing a provided package
