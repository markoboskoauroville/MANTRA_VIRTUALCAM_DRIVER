#!/bin/bash
# Builds "Mantra VirtualCam.driver" (an AudioServerPlugIn) into build/. Runs on the GitHub runner only
# (MANTRA_MANIFEST modules/compile-on-github.md): never on Marko's Mac. Never installs anything.
#   SIGN=-  ad hoc (default; the driver has no team)      ARCHS="-arch arm64"  to drop x86_64
set -euo pipefail
cd "$(dirname "$0")"

# VARIANT=insert (v3, 25.11.2026): "Mantra Insert.driver", the return pair of the Webcam Recorder's INSERT (Marko: "some
# kind of insert system so it can send to BlackHole the sound and receive it from the BlackHole ... plugin compensation
# delay"): a visible OUTPUT "Mantra Insert Return" (PatchWork's output device) and its hidden INPUT twin
# "Mantra Insert Return In" (the recorder reads it), the same ring and host-clock timing as the VirtualCam pair.
VARIANT="${VARIANT:-virtualcam}"
NAME="Mantra VirtualCam"
BUNDLE_ID="community.auroville.mantra.virtualcam.driver"
FACTORY="35D75853-9B41-482B-8A1A-FEB55C9ECAFE"
DEFS=""
if [ "$VARIANT" = "insert" ]; then
  NAME="Mantra Insert"
  BUNDLE_ID="community.auroville.mantra.insert.driver"
  FACTORY="8C1E7A3D-5B2F-4E61-9D47-A2F0C3B6E915"
  DEFS="-DkDriver_Name=\"Mantra Insert\" -DkPlugIn_BundleID=\"$BUNDLE_ID\" -DkBox_UID=\"MantraInsert_Box_UID\"
        -DkDevice_UID=\"MantraInsertReturn_UID\" -DkDevice2_UID=\"MantraInsertReturnIn_UID\"
        -DkDevice_ModelUID=\"MantraInsert_ModelUID\" -DkDevice_Name=\"Mantra Insert Return\"
        -DkDevice2_Name=\"Mantra Insert Return In\" -DkBox_Name=\"Mantra Insert\"
        -DkDevice_IsHidden=false -DkDevice2_IsHidden=true
        -DkDevice_HasInput=false -DkDevice_HasOutput=true -DkDevice2_HasInput=true -DkDevice2_HasOutput=false"
fi
DEFS=$(echo $DEFS)   # one line: eval must not see the line breaks
V=$(tr -d '[:space:]' < MANTRA_VERSION)
SIGN="${SIGN:--}"
ARCHS="${ARCHS:--arch arm64 -arch x86_64}"
OUT="build/$NAME.driver"

[ "${KEEP_BUILD:-}" = 1 ] || rm -rf build
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

# The driver: BlackHole.c with the Mantra configuration block (its defaults), one universal binary.
# shellcheck disable=SC2086
eval clang $DEFS -bundle $ARCHS -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -DDEBUG=0 \
  -framework CoreAudio -framework CoreFoundation -framework Accelerate \
  -o "\"$OUT/Contents/MacOS/$NAME\"" BlackHole/BlackHole.c

# Info.plist: the Xcode variables of BlackHole.plist filled in by hand (no xcodebuild, no team).
sed -e "s|\${EXECUTABLE_NAME}|$NAME|" \
    -e "s|\$(PRODUCT_BUNDLE_IDENTIFIER)|$BUNDLE_ID|" \
    -e "s|\${PRODUCT_NAME}|$NAME|" \
    -e "s|\$(MARKETING_VERSION)|$V|" \
    -e "s|\${MANTRA_VERSION}|$V|" \
    -e "s|35D75853-9B41-482B-8A1A-FEB55C9ECAFE|$FACTORY|g" \
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
