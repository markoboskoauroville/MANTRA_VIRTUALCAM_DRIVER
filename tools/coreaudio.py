"""Tiny ctypes bridge to CoreAudio and CoreFoundation (no compiler, no packages). Used by vcam_probe.py and
fallback_aggregate.py. Read helpers never change anything; only the functions named create_/destroy_ do."""
import ctypes as C
import ctypes.util

CA = C.CDLL(ctypes.util.find_library("CoreAudio"))
CF = C.CDLL(ctypes.util.find_library("CoreFoundation"))

def fcc(s):
    return int.from_bytes(s.encode("ascii"), "big")

SYSTEM = 1
GLOBAL, INPUT, OUTPUT = fcc("glob"), fcc("inpt"), fcc("outp")
UTF8 = 0x08000100

class Addr(C.Structure):
    _fields_ = [("sel", C.c_uint32), ("scope", C.c_uint32), ("elem", C.c_uint32)]

CA.AudioObjectGetPropertyDataSize.argtypes = [C.c_uint32, C.POINTER(Addr), C.c_uint32, C.c_void_p, C.POINTER(C.c_uint32)]
CA.AudioObjectGetPropertyData.argtypes = [C.c_uint32, C.POINTER(Addr), C.c_uint32, C.c_void_p, C.POINTER(C.c_uint32), C.c_void_p]
CA.AudioObjectHasProperty.argtypes = [C.c_uint32, C.POINTER(Addr)]
CA.AudioObjectHasProperty.restype = C.c_ubyte
CA.AudioHardwareCreateAggregateDevice.argtypes = [C.c_void_p, C.POINTER(C.c_uint32)]
CA.AudioHardwareDestroyAggregateDevice.argtypes = [C.c_uint32]

CF.CFStringCreateWithCString.restype = C.c_void_p
CF.CFStringCreateWithCString.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32]
CF.CFStringGetCString.argtypes = [C.c_void_p, C.c_char_p, C.c_long, C.c_uint32]
CF.CFRelease.argtypes = [C.c_void_p]
CF.CFGetTypeID.argtypes = [C.c_void_p]; CF.CFGetTypeID.restype = C.c_ulong
CF.CFDictionaryGetTypeID.restype = C.c_ulong
CF.CFNumberGetTypeID.restype = C.c_ulong
CF.CFStringGetTypeID.restype = C.c_ulong
CF.CFDictionaryGetCount.argtypes = [C.c_void_p]; CF.CFDictionaryGetCount.restype = C.c_long
CF.CFDictionaryGetKeysAndValues.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
CF.CFNumberGetValue.argtypes = [C.c_void_p, C.c_long, C.c_void_p]
CF.CFNumberCreate.restype = C.c_void_p; CF.CFNumberCreate.argtypes = [C.c_void_p, C.c_long, C.c_void_p]
CF.CFArrayCreate.restype = C.c_void_p; CF.CFArrayCreate.argtypes = [C.c_void_p, C.c_void_p, C.c_long, C.c_void_p]
CF.CFDictionaryCreate.restype = C.c_void_p
CF.CFDictionaryCreate.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_long, C.c_void_p, C.c_void_p]

def _sym(name):
    return C.addressof(C.c_char.in_dll(CF, name))

def cfstr(s):
    return CF.CFStringCreateWithCString(None, s.encode("utf-8"), UTF8)

def pystr(ref):
    if not ref:
        return None
    buf = C.create_string_buffer(1024)
    CF.CFStringGetCString(ref, buf, 1024, UTF8)
    return buf.value.decode("utf-8")

def cfnum(v):
    x = C.c_int32(v)
    return CF.CFNumberCreate(None, 3, C.byref(x))  # kCFNumberSInt32Type

def cfarray(items):
    arr = (C.c_void_p * len(items))(*items)
    return CF.CFArrayCreate(None, arr, len(items), _sym("kCFTypeArrayCallBacks"))

def cfdict(pairs):
    ks = (C.c_void_p * len(pairs))(*[cfstr(k) for k, _ in pairs])
    vs = (C.c_void_p * len(pairs))(*[v for _, v in pairs])
    return CF.CFDictionaryCreate(None, ks, vs, len(pairs), _sym("kCFTypeDictionaryKeyCallBacks"),
                                 _sym("kCFTypeDictionaryValueCallBacks"))

def pyobj(ref):
    """CFDictionary / CFNumber / CFString -> Python (enough for the status property)."""
    t = CF.CFGetTypeID(ref)
    if t == CF.CFStringGetTypeID():
        return pystr(ref)
    if t == CF.CFNumberGetTypeID():
        d = C.c_double()
        CF.CFNumberGetValue(ref, 6, C.byref(d))  # kCFNumberFloat64Type
        return int(d.value) if d.value.is_integer() else d.value
    if t == CF.CFDictionaryGetTypeID():
        n = CF.CFDictionaryGetCount(ref)
        ks, vs = (C.c_void_p * n)(), (C.c_void_p * n)()
        CF.CFDictionaryGetKeysAndValues(ref, ks, vs)
        return {pyobj(k): pyobj(v) for k, v in zip(ks, vs)}
    return "<CF type %d>" % t

def get(obj, sel, scope=GLOBAL, ctype=C.c_uint32, qual=None):
    a = Addr(fcc(sel) if isinstance(sel, str) else sel, scope, 0)
    out = ctype()
    size = C.c_uint32(C.sizeof(out))
    qs, qp = (C.sizeof(qual), C.byref(qual)) if qual is not None else (0, None)
    err = CA.AudioObjectGetPropertyData(obj, C.byref(a), qs, qp, C.byref(size), C.byref(out))
    if err:
        raise OSError(err, "AudioObjectGetPropertyData %s on %d" % (sel, obj))
    return out.value

def has(obj, sel, scope=GLOBAL):
    a = Addr(fcc(sel), scope, 0)
    return bool(CA.AudioObjectHasProperty(obj, C.byref(a)))

def get_list(obj, sel, scope=GLOBAL):
    a = Addr(fcc(sel), scope, 0)
    size = C.c_uint32(0)
    if CA.AudioObjectGetPropertyDataSize(obj, C.byref(a), 0, None, C.byref(size)):
        return []
    n = size.value // 4
    arr = (C.c_uint32 * max(n, 1))()
    CA.AudioObjectGetPropertyData(obj, C.byref(a), 0, None, C.byref(size), arr)
    return list(arr)[: size.value // 4]

def get_string(obj, sel, scope=GLOBAL):
    ref = get(obj, sel, scope, C.c_void_p)
    s = pystr(ref)
    if ref:
        CF.CFRelease(ref)
    return s

def devices():
    """Visible devices only (the HAL leaves hidden ones out of this list)."""
    return get_list(SYSTEM, "dev#")

def device_by_uid(uid):
    q = C.c_void_p(cfstr(uid))
    try:
        return get(SYSTEM, "uidd", GLOBAL, C.c_uint32, q)
    finally:
        CF.CFRelease(q)

def channels(dev, scope):
    """Number of channels in the stream configuration of one direction."""
    a = Addr(fcc("slay"), scope, 0)
    size = C.c_uint32(0)
    if CA.AudioObjectGetPropertyDataSize(dev, C.byref(a), 0, None, C.byref(size)) or size.value < 8:
        return 0
    buf = (C.c_ubyte * size.value)()
    CA.AudioObjectGetPropertyData(dev, C.byref(a), 0, None, C.byref(size), buf)
    nbuf = C.c_uint32.from_buffer(buf, 0).value
    total = 0
    for i in range(nbuf):
        total += C.c_uint32.from_buffer(buf, 8 + i * 16).value
    return total

def describe(dev):
    d = {"id": dev, "name": get_string(dev, "lnam"), "uid": get_string(dev, "uid ")}
    d["hidden"] = bool(get(dev, "hidn"))
    d["inChannels"], d["outChannels"] = channels(dev, INPUT), channels(dev, OUTPUT)
    d["rate"] = get(dev, "nsrt", GLOBAL, C.c_double)
    for scope, tag in ((INPUT, "in"), (OUTPUT, "out")):
        if d[tag + "Channels"]:
            d[tag + "LatencyFrames"] = get(dev, "ltnc", scope)
            d[tag + "SafetyOffsetFrames"] = get(dev, "saft", scope)
            d[tag + "BufferFrames"] = get(dev, "fsiz", scope)
            streams = get_list(dev, "stm#", scope)
            d[tag + "StreamLatencyFrames"] = [get(s, "ltnc") for s in streams]
    return d

def status(dev):
    """The Mantra VirtualCam driver's custom property 'mvst' (a CFDictionary), or None."""
    if not has(dev, "mvst"):
        return None
    ref = get(dev, "mvst", GLOBAL, C.c_void_p)
    if not ref:
        return None
    try:
        return pyobj(ref)
    finally:
        CF.CFRelease(ref)

def create_aggregate(desc_pairs):
    out = C.c_uint32(0)
    d = cfdict(desc_pairs)
    err = CA.AudioHardwareCreateAggregateDevice(d, C.byref(out))
    CF.CFRelease(d)
    if err:
        raise OSError(err, "AudioHardwareCreateAggregateDevice")
    return out.value

def destroy_aggregate(dev):
    err = CA.AudioHardwareDestroyAggregateDevice(dev)
    if err:
        raise OSError(err, "AudioHardwareDestroyAggregateDevice")
