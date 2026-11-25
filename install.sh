#!/bin/bash
# Installs "Mantra VirtualCam.driver" into /Library/Audio/Plug-Ins/HAL and restarts Core Audio.
#
#   ./install.sh            asks "yes" first, then sudo asks for the admin password in this Terminal
#   ./install.sh --dialog   the password goes into the macOS administrator dialog instead of the Terminal
#   ./install.sh --yes      no question (used by the GitHub runner test)
#
# Marko types his own password (Terminal or the macOS dialog). It is never asked for in a chat or by voice,
# never stored. Restarting coreaudiod silences EVERY audio device for about 2 seconds: never during a lesson,
# a recording or an Ableton session. BlackHole 2ch is not touched (other bundle id, other UUID, other name).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${DRIVER:-$HERE/Mantra VirtualCam.driver}"
HAL="/Library/Audio/Plug-Ins/HAL"
DST="$HAL/Mantra VirtualCam.driver"
MODE="${1:-ask}"

[ -d "$SRC" ] || { echo "Not found: $SRC (unzip the release next to this script)"; exit 1; }
# v3: the INSERT return pair ships beside it as "Mantra Insert.driver" (its own bundle id and factory); installed together
INS_SRC="$HERE/Mantra Insert.driver"
INS_DST="$HAL/Mantra Insert.driver"
INS_STEPS=""
if [ -d "$INS_SRC" ]; then
  IBID=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$INS_SRC/Contents/Info.plist")
  [ "$IBID" = "community.auroville.mantra.insert.driver" ] || { echo "Unexpected bundle id $IBID"; exit 1; }
  codesign --verify --strict "$INS_SRC" || { echo "The insert driver's signature does not verify"; exit 1; }
  INS_STEPS="rm -rf '$INS_DST' && ditto '$INS_SRC' '$INS_DST' && xattr -dr com.apple.quarantine '$INS_DST' 2>/dev/null; chown -R root:wheel '$INS_DST' && chmod -R go-w '$INS_DST'; "
  echo "and Mantra Insert driver -> $INS_DST"
fi
BID=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$SRC/Contents/Info.plist")
[ "$BID" = "community.auroville.mantra.virtualcam.driver" ] || { echo "Unexpected bundle id $BID"; exit 1; }
codesign --verify --strict "$SRC" || { echo "The driver's signature does not verify"; exit 1; }
VER=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$SRC/Contents/Info.plist")
echo "Mantra VirtualCam driver v$VER -> $DST"

if [ "$MODE" = "ask" ] || [ "$MODE" = "--dialog" ]; then
  echo "This restarts Core Audio: about 2 seconds of silence on every device."
  echo "Not during a lesson, a recording or an Ableton session."
  read -r -p "Type yes to install: " answer
  [ "$answer" = "yes" ] || { echo "Nothing changed."; exit 1; }
fi

STEPS="rm -rf '$DST' && mkdir -p '$HAL' && ditto '$SRC' '$DST' && xattr -dr com.apple.quarantine '$DST' 2>/dev/null; chown -R root:wheel '$DST' && chmod -R go-w '$DST'; $INS_STEPS(launchctl kickstart -k system/com.apple.audio.coreaudiod || killall coreaudiod)"
if [ "$MODE" = "--dialog" ]; then
  osascript -e "do shell script \"$STEPS\" with administrator privileges with prompt \"Install the Mantra VirtualCam microphone (Core Audio restarts for 2 seconds).\""
else
  sudo /bin/bash -c "$STEPS"
fi

echo "Waiting for Core Audio..."
for i in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  if system_profiler SPAudioDataType 2>/dev/null | grep -q "Mantra VirtualCam:"; then
    echo "OK: \"Mantra VirtualCam\" is a microphone now (pick it in Zoom). The Feed twin is hidden by design."
    exit 0
  fi
done
echo "NOT VISIBLE: coreaudiod did not publish the device. Its log of the last 2 minutes:"
log show --last 2m --style compact --predicate 'process == "coreaudiod" OR process CONTAINS "Core Audio Driver"' 2>/dev/null \
  | grep -i -E "mantra|virtualcam|signature|code ?sign|not valid|reject|denied" | tail -30 || true
echo "Fallback: python3 tools/fallback_aggregate.py status   (see TAKEOVER.md, route A1)"
exit 2
