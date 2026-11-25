#!/usr/bin/env python3
"""Fallback route A1 (docs/VIRTUALCAM_PLAN.md section 5): a public aggregate device named "Mantra VirtualCam"
over the installed BlackHole 2ch, for the day the ad-hoc driver is refused by coreaudiod. No install, no password.

  python3 tools/fallback_aggregate.py status    read only: says what exists (the default; changes nothing)
  python3 tools/fallback_aggregate.py create    makes the aggregate (refuses if the real driver is loaded)
  python3 tools/fallback_aggregate.py remove    destroys the aggregate it made (found by its UID only)

WARNING: BlackHole 2ch is Marko's Ableton recording route. While the aggregate is in use, whatever the recorder
writes into it also reaches anything recording from BlackHole 2ch, and the other way round. Use it only when
Ableton is not recording through BlackHole. Nothing on the Mac is touched unless `create` or `remove` is typed.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coreaudio as ca  # noqa: E402

AGG_UID = "MantraVirtualCamAggregate_UID"
AGG_NAME = "Mantra VirtualCam"
BH_UID = "BlackHole2ch_UID"
DRIVER_UID = "MantraVirtualCam_UID"


def state():
    return {"driver": ca.device_by_uid(DRIVER_UID), "aggregate": ca.device_by_uid(AGG_UID),
            "blackhole2ch": ca.device_by_uid(BH_UID)}


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    s = state()
    print("driver Mantra VirtualCam: %s | aggregate %s: %s | BlackHole 2ch: %s" % (
        "loaded (id %d)" % s["driver"] if s["driver"] else "not loaded", AGG_UID,
        "exists (id %d)" % s["aggregate"] if s["aggregate"] else "none",
        "present (id %d)" % s["blackhole2ch"] if s["blackhole2ch"] else "missing"))
    if cmd == "status":
        if s["aggregate"]:
            print(ca.describe(s["aggregate"]))
        return 0
    if cmd == "create":
        if s["driver"]:
            print("The real driver is loaded: the fallback is not needed. Nothing changed."); return 1
        if not s["blackhole2ch"]:
            print("BlackHole 2ch is not installed. Nothing changed."); return 1
        if s["aggregate"]:
            print("Already there. Nothing changed."); return 0
        sub = ca.cfdict([("uid", ca.cfstr(BH_UID))])
        dev = ca.create_aggregate([
            ("uid", ca.cfstr(AGG_UID)), ("name", ca.cfstr(AGG_NAME)),
            ("subdevices", ca.cfarray([sub])), ("master", ca.cfstr(BH_UID)),
            ("private", ca.cfnum(0)), ("stacked", ca.cfnum(0)),
        ])
        print("Created aggregate %r id %d over BlackHole 2ch." % (AGG_NAME, dev))
        print(ca.describe(dev))
        return 0
    if cmd == "remove":
        if not s["aggregate"]:
            print("No aggregate %s. Nothing changed." % AGG_UID); return 0
        ca.destroy_aggregate(s["aggregate"])
        print("Removed aggregate id %d." % s["aggregate"]); return 0
    print(__doc__); return 1


if __name__ == "__main__":
    sys.exit(main())
