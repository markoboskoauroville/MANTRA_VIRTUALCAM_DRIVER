#!/usr/bin/env python3
"""Mantra VirtualCam probe. Read only by default.

  python3 tools/vcam_probe.py              finds the two devices, prints their properties and the 'mvst' status
  python3 tools/vcam_probe.py --json       the same as JSON
  python3 tools/vcam_probe.py --loopback 3 also runs a 3 s loopback: writes a sample-time ramp into the hidden
                                           Feed and reads the Mic; reports whether every sample arrives at the
                                           same sample time (offset 0 = zero added latency). Plays nothing
                                           audible: the Feed goes only to the Mic. Needs microphone permission
                                           for the terminal (otherwise the Mic reads silence and it says so).
Exit 0 = all good, 1 = devices missing or wrong, 2 = loopback failed.
"""
import ctypes as C
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coreaudio as ca  # noqa: E402

MIC_UID, FEED_UID = "MantraVirtualCam_UID", "MantraVirtualCamFeed_UID"


def check_devices():
    problems, report = [], {}
    visible = {ca.get_string(d, "uid "): d for d in ca.devices()}
    mic, feed = ca.device_by_uid(MIC_UID), ca.device_by_uid(FEED_UID)
    report["blackhole2chPresent"] = "BlackHole2ch_UID" in visible
    if not mic:
        problems.append("Mantra VirtualCam (mic) not found: driver not installed or not loaded")
    if not feed:
        problems.append("Mantra VirtualCam Feed not found by UID")
    if mic:
        m = ca.describe(mic); m["status"] = ca.status(mic); report["mic"] = m
        if m["name"] != "Mantra VirtualCam": problems.append("mic name %r" % m["name"])
        if m["hidden"] or MIC_UID not in visible: problems.append("mic must be visible in the device list")
        if m["inChannels"] != 2 or m["outChannels"] != 0: problems.append("mic must be 2 in / 0 out, is %d/%d" % (m["inChannels"], m["outChannels"]))
        if m["status"] is None: problems.append("mic has no 'mvst' status property")
    if feed:
        f = ca.describe(feed); f["status"] = ca.status(feed); report["feed"] = f
        if f["name"] != "Mantra VirtualCam Feed": problems.append("feed name %r" % f["name"])
        if not f["hidden"] or FEED_UID in visible: problems.append("feed must be hidden from the device list")
        if f["inChannels"] != 0 or f["outChannels"] != 2: problems.append("feed must be 0 in / 2 out, is %d/%d" % (f["inChannels"], f["outChannels"]))
    report["problems"] = problems
    return mic, feed, report


class TS(C.Structure):  # AudioTimeStamp
    _fields_ = [("sampleTime", C.c_double), ("hostTime", C.c_uint64), ("rateScalar", C.c_double),
                ("wordClock", C.c_uint64), ("smpte", C.c_ubyte * 24), ("flags", C.c_uint32), ("reserved", C.c_uint32)]


class Buf(C.Structure):
    _fields_ = [("channels", C.c_uint32), ("bytes", C.c_uint32), ("data", C.c_void_p)]


class BufList(C.Structure):
    _fields_ = [("n", C.c_uint32), ("bufs", Buf * 1)]


IOPROC = C.CFUNCTYPE(C.c_int32, C.c_uint32, C.POINTER(TS), C.POINTER(BufList), C.POINTER(TS),
                     C.POINTER(BufList), C.POINTER(TS), C.c_void_p)
ca.CA.AudioDeviceCreateIOProcID.argtypes = [C.c_uint32, IOPROC, C.c_void_p, C.POINTER(C.c_void_p)]
ca.CA.AudioDeviceStart.argtypes = [C.c_uint32, C.c_void_p]
ca.CA.AudioDeviceStop.argtypes = [C.c_uint32, C.c_void_p]
ca.CA.AudioDeviceDestroyIOProcID.argtypes = [C.c_uint32, C.c_void_p]

WRAP = 65536.0  # the ramp value v = (sampleTime mod 65536) / 65536 is exact in Float32


def loopback(mic, feed, seconds):
    lock = threading.Lock()
    st = {"written": 0, "read": 0, "nonzero": 0, "match": 0, "offsets": {}, "firstWriteHost": 0, "firstReadHost": 0}

    def out_proc(dev, now, inp, intime, outp, outtime, ctx):
        b = outp.contents.bufs[0]
        ch = b.channels or 2
        frames = b.bytes // (4 * ch)
        p = C.cast(b.data, C.POINTER(C.c_float))
        t0 = outtime.contents.sampleTime
        for i in range(frames):
            v = ((t0 + i) % WRAP) / WRAP
            for c in range(ch):
                p[i * ch + c] = v if v > 0 else 1.0 / WRAP  # never exactly 0 so silence is distinguishable
        with lock:
            st["written"] += frames
            st["firstWriteHost"] = st["firstWriteHost"] or outtime.contents.hostTime
        return 0

    def in_proc(dev, now, inp, intime, outp, outtime, ctx):
        b = inp.contents.bufs[0]
        ch = b.channels or 2
        frames = b.bytes // (4 * ch)
        p = C.cast(b.data, C.POINTER(C.c_float))
        t0 = intime.contents.sampleTime
        nz = m = 0
        offs = {}
        for i in range(frames):
            v = p[i * ch]
            if v == 0.0:
                continue
            nz += 1
            exp = ((t0 + i) % WRAP) / WRAP or 1.0 / WRAP
            if v == exp:
                m += 1
            else:
                d = int(round((exp - v) * WRAP)) % int(WRAP)
                offs[d] = offs.get(d, 0) + 1
        with lock:
            st["read"] += frames; st["nonzero"] += nz; st["match"] += m
            st["firstReadHost"] = st["firstReadHost"] or intime.contents.hostTime
            for k, n in offs.items():
                st["offsets"][k] = st["offsets"].get(k, 0) + n
        return 0

    po, pi = IOPROC(out_proc), IOPROC(in_proc)
    ido, idi = C.c_void_p(), C.c_void_p()
    for dev, proc, pid in ((feed, po, ido), (mic, pi, idi)):
        err = ca.CA.AudioDeviceCreateIOProcID(dev, proc, None, C.byref(pid))
        if err:
            raise OSError(err, "AudioDeviceCreateIOProcID")
    try:
        ca.CA.AudioDeviceStart(feed, ido)
        time.sleep(0.2)
        ca.CA.AudioDeviceStart(mic, idi)
        time.sleep(max(0.5, seconds / 2))
        mid = {"mic": ca.status(mic)}
        time.sleep(max(0.5, seconds / 2))
    finally:
        ca.CA.AudioDeviceStop(mic, idi); ca.CA.AudioDeviceStop(feed, ido)
        ca.CA.AudioDeviceDestroyIOProcID(mic, idi); ca.CA.AudioDeviceDestroyIOProcID(feed, ido)
    st["statusWhileRunning"] = mid["mic"]
    st["offsets"] = {str(k): v for k, v in sorted(st["offsets"].items(), key=lambda kv: -kv[1])[:5]}
    return st


def main():
    as_json = "--json" in sys.argv
    mic, feed, rep = check_devices()
    code = 1 if rep["problems"] else 0
    if not rep["problems"] and "--loopback" in sys.argv:
        i = sys.argv.index("--loopback")
        secs = float(sys.argv[i + 1]) if len(sys.argv) > i + 1 and sys.argv[i + 1][0].isdigit() else 3.0
        lb = loopback(mic, feed, secs)
        rep["loopback"] = lb
        if lb["nonzero"] == 0:
            rep["loopbackVerdict"] = "SILENT: the Mic read only zeros (no microphone permission for this process, or the Feed did not run)"
            code = 2
        elif lb["match"] / lb["nonzero"] >= 0.99:
            rep["loopbackVerdict"] = "OK: %d of %d non-silent frames arrived at exactly their own sample time (offset 0)" % (lb["match"], lb["nonzero"])
        else:
            rep["loopbackVerdict"] = "MISMATCH: %d of %d frames matched; offsets seen (frames): %s" % (lb["match"], lb["nonzero"], lb["offsets"])
            code = 2
    if as_json:
        print(json.dumps(rep, indent=2, default=str))
    else:
        for k in ("mic", "feed"):
            if k in rep:
                d = dict(rep[k]); s = d.pop("status", None)
                print("%-5s %s" % (k, json.dumps(d)))
                if s:
                    print("      status %s" % json.dumps(s))
        print("BlackHole 2ch present: %s" % rep["blackhole2chPresent"])
        if "loopback" in rep:
            lb = dict(rep["loopback"]); lb.pop("statusWhileRunning", None)
            print("loopback %s" % json.dumps(lb))
            print("status while running %s" % json.dumps(rep["loopback"]["statusWhileRunning"]))
            print(rep["loopbackVerdict"])
        for p in rep["problems"]:
            print("PROBLEM: " + p)
        print("RESULT: %s" % ("OK" if code == 0 else "FAIL (%d)" % code))
    sys.exit(code)


if __name__ == "__main__":
    main()
