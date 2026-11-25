# MANTRA_VIRTUALCAM_DRIVER: takeover

(Public because GitHub Actions builds it free; unpromoted, so there is no README and no description:
MANTRA_MANIFEST modules/repo-privacy.md. This file is the explanation.)

## What it is

The audio half of **Mantra VirtualCam** (step VC-2 of WEB_CAM_VIEWER/docs/VIRTUALCAM_PLAN.md). A CoreAudio
AudioServerPlugIn, `Mantra VirtualCam.driver`, forked from **BlackHole 0.7.1** (Existential Audio Inc., GPL-3.0,
upstream commit 6008d29). It publishes two devices that share one ring buffer and one host-clock time line:

| Device | UID | Visible | Direction | Who uses it |
|---|---|---|---|---|
| **Mantra VirtualCam** | `MantraVirtualCam_UID` | yes | input only, 2 ch | Zoom / FaceTime / Chrome / OBS pick it as microphone |
| **Mantra VirtualCam Feed** | `MantraVirtualCamFeed_UID` | hidden (`kAudioDevicePropertyIsHidden` = 1) | output only, 2 ch | the Webcam Recorder writes the processed sound into it |

A sample the recorder writes into the Feed at output sample time S is read by the microphone at input sample time S.
Both devices give the same zero time stamps (host time), so the host time of that sample is the same on both sides.
Float32, 2 channels, 44100 / 48000 / 96000 Hz (48000 at start), zero added latency.

Marko's words (25.11.2026, WEB_CAM_VIEWER/momentaryupdates.md): *"I want that our app web cam recorder stream through
OBS virtual camera with all audio and delay processing. You need to also enable Blue Cat PatchWork to be part of the
audio pipeline by taking the source code from a BlackHole virtual audio device I have here or use BlackHole as such.
During that, you need a composite for plugin delays so my students always see me playing in sync."*

The camera stays "OBS Virtual Camera" (no paid Apple account, Marko's decision); the microphone has his name.
The app-side interface (how the recorder finds the Feed, the composite delay) is WEB_CAM_VIEWER/docs/VIRTUALCAM_BUILD.md.

## Build (GitHub only)

`.github/workflows/build.yml` on every push to main (copied from MANTRA_MANIFEST/workflows/mac-swift.yml):
1. **build**: `swift icon/make_icon.swift` (icon VIR/CAM over a microphone, Audio teal), `SIGN=- ./build.sh` (clang,
   universal arm64 + x86_64, macOS 12+, bundle filled from BlackHole.plist, **ad-hoc signature**), the unit test
   `tests/driver_test.c` (the driver compiled in, a fake HAL host, every entry point, a bit-exact loopback across the
   ring wrap, the status counters, a 96 kHz change), static checks (codesign, lipo, otool, nm, the UUID), the zip.
2. **haltest**: on the runner's own macOS 15 (never on Marko's Mac): installs BlackHole 2ch (brew cask), then this
   driver with `install.sh --yes`, restarts coreaudiod, runs `tools/vcam_probe.py` (both devices, names, hidden twin,
   channel directions, latency, the status property, BlackHole 2ch still there), a 3 s loopback Feed -> Mic, prints
   coreaudiod's log, uninstalls, then creates / checks / removes the aggregate fallback.
3. **release**: `vN` (N = `MANTRA_VERSION`, whole numbers) marked Latest, asset `MantraVirtualCam.zip`:
   `MantraVirtualCam/{Mantra VirtualCam.driver, install.sh, uninstall.sh, tools/, LICENSE, SOURCE.txt}`.

The Xcode project, `BlackHoleTests/`, `Installer/`, `Uninstaller/` are upstream's and are **not used** (they carry
Existential Audio's team id; kept so upstream changes can be merged).

## Every difference from BlackHole 0.7.1 (`git diff b092b28 -- BlackHole/`)

1. **Configuration block** at the top of the state section of `BlackHole/BlackHole.c` (all still `#ifndef`, so
   `GCC_PREPROCESSOR_DEFINITIONS` overrides work as upstream): kDriver_Name "Mantra VirtualCam", kPlugIn_BundleID
   `community.auroville.mantra.virtualcam.driver`, kPlugIn_Icon `MantraVirtualCam.icns`, no "%ich" name format,
   our own box/device/model UIDs, kDevice_Name "Mantra VirtualCam", kDevice2_Name "Mantra VirtualCam Feed", new
   kBox_Name "Mantra VirtualCam Box", **Device: input only, visible; Device2: output only, hidden**, kManufacturer_Name
   "Mantra (fork of BlackHole by Existential Audio)", 2 channels, kLatency_Frame_Size 0, rates 44100/48000/96000,
   **kCanBeDefaultDevice / kCanBeDefaultSystemDevice false** (nothing on the Mac starts using it by itself),
   **kEnableVolumeControl false** (the mic's volume control stays visible but never scales the samples, so Zoom's
   automatic mic volume cannot change the drum level; the loopback is bit exact).
2. **Factory**: exported `MantraVirtualCam_Create` instead of `BlackHole_Create`; plug-in factory UUID
   `35D75853-9B41-482B-8A1A-FEB55C9ECAFE` instead of `e395c745-...` (BlackHole's installer replaces that one with a
   random UUID per build; ours is fixed). The plug-in TYPE UUID is Apple's AudioServerPlugIn type, unchanged.
   Bundle id, executable name, factory UUID, device UIDs and names all differ from BlackHole2ch: no clash.
3. **The status property** (custom, read only): see below. Counters are written on the IO thread with plain aligned
   stores; no lock was added to the IO path.
4. **Two upstream bug fixes**: `kAudioPlugInPropertyTranslateUIDToDevice` / `...ToBox` refused every correct qualifier
   (`== sizeof(CFStringRef)` instead of `!=`); the Device2 control list walked Device's object list.
5. **Skipped writes read as silence (v2).** Upstream BlackHole leaves a ring slot untouched when the writer misses a
   cycle (an overload), so the mic would read sound from one ring earlier (65536 frames, 1.37 s at 48 kHz). WriteMix now
   zeroes the frames between the last write and the new one (`gapFrames` in the status). Found by the runner loopback
   of v1: one overload of the (slow, Python) test writer gave 1024 frames that did not match their sample time.
6. Box name default from kBox_Name instead of the literal "BlackHole Box". Info.plist: CFBundleVersion and
   CFBundleShortVersionString = MANTRA_VERSION, the icon, an NSHumanReadableCopyright with the GPL/source notice.

Not changed: the ring buffer (65536 frames), the zero time stamp model (period 16384 frames on the host clock), the
clock source control (Internal Fixed by default = exactly the host clock), mute, the overload check, the clearing
of the ring when the writer stops ("silence, never old sound").

## The status property `'mvst'`

On both devices, global scope, element main. Listed in `kAudioObjectPropertyCustomPropertyInfoList` as
`kAudioServerPlugInCustomPropertyDataTypeCFPropertyList`, so a client gets a **CFDictionary it must CFRelease**.
Read it from a normal thread at up to ~10 Hz (it allocates), never from an IO proc.

| Key | Meaning |
|---|---|
| version | 1 |
| sampleRate | current nominal rate |
| hostTicksPerFrame / clockTicksPerFrame / clockRatio | host ticks per frame nominal / used by the clock; ratio = 1.0 on the default Internal Fixed clock (the device IS the host clock) |
| anchorHostTime, zeroTimeStampCount, zeroTimeStampPeriod | the zero time stamp line: host = anchor + count x period x ticksPerFrame |
| ringFrames, latencyFrames, safetyOffsetFrames | 65536, 0, 0 |
| micClients, feedClients | IO clients running on the mic / the feed |
| lastWriteEndSampleTime, lastWriteHostTime, lastWriteFrames | the Feed's last WriteMix (end sample time, when, size) |
| lastReadEndSampleTime, lastReadHostTime, lastReadFrames | the mic's last ReadInput |
| fillFrames, fillMs | how far the writer is ahead of the reader (0 when either side is stopped) |
| writeCycles, readCycles, silentReads, overloads, gapFrames | counters; silentReads = reads with no fresh Feed data (underrun or no writer); gapFrames = frames zeroed because the Feed writer skipped them |
| nowHostTime | mach_absolute_time when the dictionary was made |

Drift: the driver runs on the host clock, so there is nothing to correct on the driver side. The drift the app
fights is between the capture microphone's crystal and the host clock; the app sees it in its own ring (capture PTS
vs Feed IO-proc host times) and uses fillFrames / silentReads only as a cross-check.

## Install on Marko's Mac (his hands, once; not done by any session)

From a Terminal, at a quiet moment (not during a lesson, a recording or an Ableton session):

    cd /tmp && gh release download --repo markoboskoauroville/MANTRA_VIRTUALCAM_DRIVER --pattern MantraVirtualCam.zip --clobber
    ditto -x -k MantraVirtualCam.zip /tmp/mvc && cd /tmp/mvc/MantraVirtualCam
    ./install.sh            # types "yes", then HIS admin password at sudo's prompt (or ./install.sh --dialog)
    python3 tools/vcam_probe.py               # read only: both devices, the status property
    python3 tools/vcam_probe.py --loopback 3  # optional: Feed -> Mic sample-exact check (needs mic permission for Terminal)

`install.sh` checks the bundle id and signature, copies to `/Library/Audio/Plug-Ins/HAL/Mantra VirtualCam.driver`,
removes the quarantine attribute, sets root:wheel, restarts coreaudiod (`launchctl kickstart -k
system/com.apple.audio.coreaudiod`, else `killall coreaudiod`): **about 2 s of silence on every device**. It then
waits for the device in system_profiler, and if it does not appear prints coreaudiod's log lines about the driver.
`uninstall.sh` removes only this driver and restarts coreaudiod. BlackHole 2ch is never touched.

## Fallback A1: the aggregate over BlackHole 2ch

`python3 tools/fallback_aggregate.py status|create|remove`. `status` (the default) changes nothing. `create` makes
a public aggregate "Mantra VirtualCam" (UID `MantraVirtualCamAggregate_UID`) over BlackHole 2ch and refuses when the
real driver is loaded. **BlackHole 2ch is Marko's Ableton route**: while the aggregate is used, the recorder's sound
reaches anything recording from BlackHole 2ch. Only when Ableton is not recording through it.

## Risks and what is not verified

- **Ad-hoc signature on Marko's Mac (macOS 15.0, SIP disabled).** Every HAL driver installed there today is
  Developer ID. The runner job proves (or not) that macOS 15 coreaudiod loads this exact ad-hoc build; Marko's Mac is
  the same major version with SIP off, which only loosens checks. If it is refused anyway: the fallback above.
- The loopback on the runner may read silence if the runner's python has no microphone permission (TCC); the
  structure checks do not depend on it, and the unit test proves the loopback in process.
- Not tested: Zoom/FaceTime picking the device (needs Marko's Mac + the install), long-run drift, 44.1 kHz in Zoom.
- One channel (mono) is not offered: 2 channels only; Zoom takes the left channel of a stereo mic, so the recorder
  writes the same sound into both.

## Files

`BlackHole/BlackHole.c` (the driver), `BlackHole/BlackHole.plist`, `build.sh`, `MANTRA_VERSION`, `icon/make_icon.swift`,
`tests/driver_test.c`, `install.sh`, `uninstall.sh`, `tools/coreaudio.py` (ctypes bridge), `tools/vcam_probe.py`,
`tools/fallback_aggregate.py`, `.github/workflows/build.yml`, `LICENSE` (GPL-3.0, upstream's), `CHANGELOG.md` and
`VERSION` (upstream's 0.7.1, kept for reference).

## License

GPL-3.0. Copyright (C) 2019 Existential Audio Inc. for BlackHole; the Mantra changes are under the same license.
Existential Audio asks forks to change kDriver_Name, the bundle id and the icon: all three are changed. The Webcam
Recorder only talks to this driver through CoreAudio (a separate program), so its own license is not affected.
