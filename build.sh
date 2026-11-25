#!/bin/bash
# Builds "Mantra VirtualCam.driver" (an AudioServerPlugIn) into build/. Runs on the GitHub runner only
# (MANTRA_MANIFEST modules/compile-on-github.md): never on Marko's Mac. Never installs anything.
#   SIGN=-  ad hoc (default; the driver has no team)      ARCHS="-arch arm64"  to drop x86_64
set -euo pipefail
cd "$(dirname "$0")"

NAME="Mantra VirtualCam"
BUNDLE_ID="community.auroville.mantra.virtualcam.driver"
V=$(tr -d '[:space:]' < MANTRA_VERSION)
SIGN="${SIGN:--}"
ARCHS="${ARCHS:--arch arm64 -arch x86_64}"
OUT="build/$NAME.driver"

rm -rf build
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

# The driver: BlackHole.c with the Mantra configuration block (its defaults), one universal binary.
# shellcheck disable=SC2086
clang -bundle $ARCHS -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -DDEBUG=0 \
  -framework CoreAudio -framework CoreFoundation -framework Accelerate \
  -o "$OUT/Contents/MacOS/$NAME" BlackHole/BlackHole.c

# Info.plist: the Xcode variables of BlackHole.plist filled in by hand (no xcodebuild, no team).
sed -e "s|\${EXECUTABLE_NAME}|$NAME|" \
    -e "s|\$(PRODUCT_BUNDLE_IDENTIFIER)|$BUNDLE_ID|" \
    -e "s|\${PRODUCT_NAME}|$NAME|" \
    -e "s|\$(MARKETING_VERSION)|$V|" \
    -e "s|\${MANTRA_VERSION}|$V|" \
    BlackHole/BlackHole.plist > "$OUT/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile MantraVirtualCam.icns" "$OUT/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add :NSHumanReadableCopyright string 'GPL-3.0. Fork of BlackHole, Copyright (C) 2019 Existential Audio Inc. Source: https://github.com/markoboskoauroville/MANTRA_VIRTUALCAM_DRIVER'" "$OUT/Contents/Info.plist"
plutil -lint "$OUT/Contents/Info.plist"

# The icon (made on the runner by icon/make_icon.swift; BlackHole's own icon must not be shipped).
if [ -f icon/MantraVirtualCam.icns ]; then cp icon/MantraVirtualCam.icns "$OUT/Contents/Resources/"; fi
cp LICENSE "$OUT/Contents/Resources/LICENSE.txt"

codesign --force --sign "$SIGN" --timestamp=none "$OUT"
codesign --verify --strict --verbose=2 "$OUT"
lipo -info "$OUT/Contents/MacOS/$NAME"
echo "built $OUT v$V"
