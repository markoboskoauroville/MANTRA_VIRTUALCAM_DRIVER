# MANTRA_VIRTUALCAM_DRIVER: lessons

1. (25.11.2026) BlackHole already has the "mirror device" idea (Device2 sharing the ring). The whole visible-mic +
   hidden-feed split is configuration (HasInput/HasOutput/IsHidden per device), not new code. Keep it that way.
2. (25.11.2026) Build without xcodebuild: one `clang -bundle` of BlackHole.c plus a hand-filled Info.plist. The Xcode
   project carries Existential Audio's DEVELOPMENT_TEAM and hardened-runtime settings that a team-less runner fights.
3. (25.11.2026) A custom property of an AudioServerPlugIn must be a CFString or CFPropertyList and be listed in
   kAudioObjectPropertyCustomPropertyInfoList, or the HAL will not carry it to the client. The client releases it.
4. (25.11.2026) Upstream 0.7.1 refuses its own TranslateUIDToDevice qualifier (`==` for `!=`). The HAL resolves
   UIDs itself, so nobody noticed; the fake-host unit test does.
5. (25.11.2026) Never install a HAL driver on Marko's Mac from a session: admin password + a coreaudiod restart
   (2 s of silence everywhere). The GitHub runner is the place to prove a driver loads; Marko installs once himself.
6. (25.11.2026) Python ctypes is enough to read CoreAudio properties and run IO procs: no compiler on the Mac.
