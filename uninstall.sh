#!/bin/bash
# Removes "Mantra VirtualCam.driver" from /Library/Audio/Plug-Ins/HAL and restarts Core Audio
# (about 2 seconds of silence on every device: never during a lesson or a recording).
#   ./uninstall.sh [--yes|--dialog]       Nothing else is touched (BlackHole 2ch stays).
set -euo pipefail
DST="/Library/Audio/Plug-Ins/HAL/Mantra VirtualCam.driver"
MODE="${1:-ask}"
[ -d "$DST" ] || { echo "Not installed: $DST"; exit 0; }
if [ "$MODE" != "--yes" ]; then
  read -r -p "Core Audio restarts (2 s of silence). Type yes to remove the Mantra VirtualCam driver: " answer
  [ "$answer" = "yes" ] || { echo "Nothing changed."; exit 1; }
fi
STEPS="rm -rf '$DST' && (launchctl kickstart -k system/com.apple.audio.coreaudiod || killall coreaudiod)"
if [ "$MODE" = "--dialog" ]; then
  osascript -e "do shell script \"$STEPS\" with administrator privileges with prompt \"Remove the Mantra VirtualCam microphone.\""
else
  sudo /bin/bash -c "$STEPS"
fi
sleep 3
if system_profiler SPAudioDataType 2>/dev/null | grep -q "Mantra VirtualCam:"; then echo "Still listed (Core Audio not restarted yet?)"; exit 2; fi
echo "Removed."
