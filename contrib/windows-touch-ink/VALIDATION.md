# Validation

Validated on Windows x64 with the deployed SpeedyNote 1.6.3 / Qt 6.11.2 runtime
and GCC 15.1. These are automated checks with synthetic touch/image fixtures;
they do not measure physical capacitive-pen latency or establish palm rejection.

## Input and geometry

Run `build.ps1 -RunTests` with the paths shown in README.md.

- Plugin enabled/disabled baseline, registry/data isolation, subpixel coordinates,
  continuous strokes, contact tracking, extra contacts, cancellation, focus/hide
  cleanup, real mouse, touchpad exclusion and ordinary widget input.
- Persistent gesture/nib/smoothing controls, mode changes during a stroke,
  native gesture forwarding, non-pen tools, variable simulated pressure,
  stationary-lift taper and combined nib/smoothing geometry.
- 27 independent curve checks: sampled circles/spiral, native point decimation
  and final outline rendering, uneven sample spacing, jitter, exact endpoint,
  pressure interpolation, dots, corners/hooks, finite-output bounds and capped
  work for abnormal packets. JSON metrics and comparison PNGs are written to
  `build/test-results/`.

## Actual application image integration

The opt-in script copies runtime files into `integration-build/runtime`, builds
the plugin and an image probe, and launches that **test copy** offscreen. The
probe uses its own `SpeedyNote-TouchInk-ImageTest` data directory,
`ImageIntegrationTestProfile` registry view and single-instance name. It creates
synthetic images and notebooks. It never drives a running user application.
On timeout it terminates only the test process it started. Test data is retained
for inspection. The probe is not installed into the user's edition.

Run from this directory:

```powershell
$testPaths = @{
    QtRoot = 'C:\Qt\6.11.2\mingw_64'
    Compiler = 'C:\mingw64\bin\g++.exe'
    EditionDirectory = 'C:\Apps\SpeedyNote-TouchInk'
}
.\tests\verify-images.ps1 @testPaths -Page
.\tests\verify-images.ps1 @testPaths -Page -RunOnly -Reopen
.\tests\verify-images.ps1 @testPaths -Page -RunOnly -Portable
.\tests\verify-images.ps1 @testPaths -RunOnly
.\tests\verify-images.ps1 @testPaths -RunOnly -Reopen
.\tests\verify-images.ps1 @testPaths -RunOnly -Edgeless
.\tests\verify-images.ps1 @testPaths -RunOnly -Edgeless -Reopen
```

All seven scenarios passed:

- Home plus-menu import, cancel, original source hash, matching page dimensions,
  native fixed PDF background and no image objects, pen activation, background
  surviving stroke undo, writing, zoom, ink undo/redo and save.
- Fresh-process reopen with the original input bitmap detached: background and
  saved annotations retained.
- Copy of the whole `.snb` directory opened with the original generated PDF
  temporarily hidden: the relative bundled background and ink still rendered.
  The script verifies the original asset is inside the test namespace before
  hiding it and restores it in `finally`.
- Paged and edgeless notebook image objects: file picker, screenshot paste from
  the pen tool, drag/drop, clipboard restoration, move/resize, undo/redo, picker
  cancel, save and fresh-process reopen with embedded image assets.

The main application's executable remains byte-identical in the installed
edition. This validates the add-on against 1.6.3, not against other releases.
