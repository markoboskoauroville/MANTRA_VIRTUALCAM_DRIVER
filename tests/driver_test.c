// Mantra VirtualCam driver: unit test with a fake HAL host (runs on the GitHub runner, never installs).
// It compiles the driver source in, calls the factory and the AudioServerPlugIn entry points the way
// coreaudiod would, and checks every Mantra difference from BlackHole plus a real loopback through the
// shared ring (Feed WriteMix at sample time S -> Mic ReadInput at sample time S gives the same samples).
//
//   clang -O0 -g -framework CoreAudio -framework CoreFoundation -framework Accelerate tests/driver_test.c -o build/driver_test
//   ./build/driver_test

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "../BlackHole/BlackHole.c"

static int gChecks = 0;
#define CHECK(cond, ...) do { gChecks++; if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n  ", __LINE__, #cond); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(1); } } while (0)

// ---- fake host -----------------------------------------------------------------------------------
static OSStatus Host_PropertiesChanged(AudioServerPlugInHostRef h, AudioObjectID o, UInt32 n, const AudioObjectPropertyAddress a[]) { (void)h; (void)o; (void)n; (void)a; return 0; }
static OSStatus Host_CopyFromStorage(AudioServerPlugInHostRef h, CFStringRef k, CFPropertyListRef* d) { (void)h; (void)k; *d = NULL; return 0; }
static OSStatus Host_WriteToStorage(AudioServerPlugInHostRef h, CFStringRef k, CFPropertyListRef d) { (void)h; (void)k; (void)d; return 0; }
static OSStatus Host_DeleteFromStorage(AudioServerPlugInHostRef h, CFStringRef k) { (void)h; (void)k; return 0; }
static OSStatus Host_RequestConfigChange(AudioServerPlugInHostRef h, AudioObjectID o, UInt64 a, void* i) { (void)h; (void)o; (void)a; (void)i; return 0; }
static AudioServerPlugInHostInterface gFakeHost = {
    Host_PropertiesChanged, Host_CopyFromStorage, Host_WriteToStorage, Host_DeleteFromStorage, Host_RequestConfigChange
};

// ---- helpers --------------------------------------------------------------------------------------
static AudioServerPlugInDriverRef D;

static UInt32 get_u32(AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    UInt32 v = 0xDEADBEEF, size = sizeof v;
    CHECK((*D)->HasProperty(D, obj, 0, &a), "obj %u has no '%c%c%c%c'", obj, (char)(sel>>24), (char)(sel>>16), (char)(sel>>8), (char)sel);
    OSStatus err = (*D)->GetPropertyData(D, obj, 0, &a, 0, NULL, size, &size, &v);
    CHECK(err == 0, "GetPropertyData err %d", (int)err);
    return v;
}

static CFStringRef get_str(AudioObjectID obj, AudioObjectPropertySelector sel)
{
    AudioObjectPropertyAddress a = { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef v = NULL; UInt32 size = sizeof v;
    OSStatus err = (*D)->GetPropertyData(D, obj, 0, &a, 0, NULL, size, &size, &v);
    CHECK(err == 0 && v != NULL, "string property err %d", (int)err);
    return v;
}

static void check_str(AudioObjectID obj, AudioObjectPropertySelector sel, const char* want)
{
    CFStringRef s = get_str(obj, sel);
    char buf[256] = {0};
    CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
    CHECK(strcmp(buf, want) == 0, "obj %u: got \"%s\", want \"%s\"", obj, buf, want);
    printf("  ok  obj %-2u %-28s \"%s\"\n", obj, sel == kAudioObjectPropertyName ? "name" : sel == kAudioDevicePropertyDeviceUID ? "uid" : "string", buf);
}

static UInt32 get_list(AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope, AudioObjectID* out, UInt32 max)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    CHECK((*D)->GetPropertyDataSize(D, obj, 0, &a, 0, NULL, &size) == 0, "size");
    CHECK(size <= max * sizeof(AudioObjectID), "list too long");
    CHECK((*D)->GetPropertyData(D, obj, 0, &a, 0, NULL, size, &size, out) == 0, "data");
    return size / sizeof(AudioObjectID);
}

static double dict_num(CFDictionaryRef d, const char* key)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFNumberRef n = CFDictionaryGetValue(d, k);
    CFRelease(k);
    CHECK(n != NULL, "status has no key %s", key);
    double v = 0; CFNumberGetValue(n, kCFNumberFloat64Type, &v);
    return v;
}

static CFDictionaryRef get_status(AudioObjectID dev)
{
    AudioObjectPropertyAddress a = { kMantra_StatusSelector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFPropertyListRef p = NULL; UInt32 size = 0;
    CHECK((*D)->HasProperty(D, dev, 0, &a), "no status property on %u", dev);
    Boolean settable = true;
    CHECK((*D)->IsPropertySettable(D, dev, 0, &a, &settable) == 0 && !settable, "status must be read only");
    CHECK((*D)->GetPropertyDataSize(D, dev, 0, &a, 0, NULL, &size) == 0 && size == sizeof(CFPropertyListRef), "status size %u", size);
    CHECK((*D)->GetPropertyData(D, dev, 0, &a, 0, NULL, size, &size, &p) == 0 && p != NULL, "status data");
    CHECK(CFGetTypeID(p) == CFDictionaryGetTypeID(), "status is not a dictionary");
    return (CFDictionaryRef)p;
}

static void do_io(AudioObjectID dev, AudioObjectID stream, UInt32 op, Float64 sampleTime, UInt32 frames, Float32* buf)
{
    AudioServerPlugInIOCycleInfo c; memset(&c, 0, sizeof c);
    c.mIOCycleCounter = 1;
    c.mNominalIOBufferFrameSize = frames;
    c.mCurrentTime.mSampleTime = sampleTime;           // "now" = the start of this buffer: within the deadline
    c.mCurrentTime.mFlags = kAudioTimeStampSampleTimeValid;
    c.mInputTime = c.mCurrentTime;
    c.mOutputTime = c.mCurrentTime;
    Boolean will = false, inPlace = false;
    CHECK((*D)->WillDoIOOperation(D, dev, 1, op, &will, &inPlace) == 0 && will, "WillDo");
    CHECK((*D)->BeginIOOperation(D, dev, 1, op, frames, &c) == 0, "Begin");
    OSStatus e = (*D)->DoIOOperation(D, dev, stream, 1, op, frames, &c, buf, NULL);
    CHECK(e == 0, "DoIOOperation err %d", (int)e);
    CHECK((*D)->EndIOOperation(D, dev, 1, op, frames, &c) == 0, "End");
}

int main(void)
{
    printf("Mantra VirtualCam driver unit test (fake host)\n");

    // ---- factory and COM ----
    void* drv = MantraVirtualCam_Create(NULL, kAudioServerPlugInTypeUUID);
    CHECK(drv == gAudioServerPlugInDriverRef, "factory did not return the driver for the HAL plug-in type");
    CHECK(MantraVirtualCam_Create(NULL, CFUUIDGetConstantUUIDWithBytes(NULL,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)) == NULL, "factory accepted a foreign type");
    D = (AudioServerPlugInDriverRef)drv;
    void* iface = NULL;
    CHECK((*D)->QueryInterface(D, CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID), &iface) == 0 && iface == D, "QueryInterface");
    (*D)->Release(D);
    CHECK((*D)->Initialize(D, &gFakeHost) == 0, "Initialize");
    printf("  ok  factory MantraVirtualCam_Create, QueryInterface, Initialize\n");

    // ---- plug-in: two devices, UID translation ----
    AudioObjectID ids[8]; UInt32 n;
    n = get_list(kObjectID_PlugIn, kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, ids, 8);
    CHECK(n == 2 && ids[0] == kObjectID_Device && ids[1] == kObjectID_Device2, "device list n=%u", n);
    {
        CFStringRef q = CFSTR(kDevice2_UID); AudioObjectID out = 0; UInt32 size = sizeof out;
        AudioObjectPropertyAddress a = { kAudioPlugInPropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        OSStatus e = (*D)->GetPropertyData(D, kObjectID_PlugIn, 0, &a, sizeof q, &q, size, &size, &out);
        CHECK(e == 0 && out == kObjectID_Device2, "translate Feed UID: err %d id %u", (int)e, out);
        q = CFSTR(kDevice_UID); e = (*D)->GetPropertyData(D, kObjectID_PlugIn, 0, &a, sizeof q, &q, size, &size, &out);
        CHECK(e == 0 && out == kObjectID_Device, "translate Mic UID: err %d id %u", (int)e, out);
        q = CFSTR("BlackHole2ch_UID"); e = (*D)->GetPropertyData(D, kObjectID_PlugIn, 0, &a, sizeof q, &q, size, &size, &out);
        CHECK(e == 0 && out == kAudioObjectUnknown, "BlackHole's UID must not be ours");
    }
    printf("  ok  plug-in lists devices 3 and 12; TranslateUIDToDevice finds both, not BlackHole2ch_UID\n");

    // ---- names, UIDs, manufacturer ----
    check_str(kObjectID_Device,  kAudioObjectPropertyName, "Mantra VirtualCam");
    check_str(kObjectID_Device2, kAudioObjectPropertyName, "Mantra VirtualCam Feed");
    check_str(kObjectID_Device,  kAudioDevicePropertyDeviceUID, "MantraVirtualCam_UID");
    check_str(kObjectID_Device2, kAudioDevicePropertyDeviceUID, "MantraVirtualCamFeed_UID");
    check_str(kObjectID_Device,  kAudioDevicePropertyModelUID, "MantraVirtualCam_ModelUID");
    check_str(kObjectID_Box,     kAudioObjectPropertyName, "Mantra VirtualCam Box");

    // ---- visibility and default-device rules ----
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertyIsHidden, kAudioObjectPropertyScopeGlobal) == 0, "mic must be visible");
    CHECK(get_u32(kObjectID_Device2, kAudioDevicePropertyIsHidden, kAudioObjectPropertyScopeGlobal) == 1, "feed must be hidden");
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeInput) == 0, "mic never default");
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeInput) == 0, "mic never system default");
    CHECK(get_u32(kObjectID_Device2, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeOutput) == 0, "feed never default");
    printf("  ok  mic visible, feed hidden, neither can be a default device\n");

    // ---- streams: mic input only, feed output only ----
    n = get_list(kObjectID_Device, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, ids, 8);
    CHECK(n == 1 && ids[0] == kObjectID_Stream_Input, "mic input streams n=%u", n);
    n = get_list(kObjectID_Device, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, ids, 8);
    CHECK(n == 0, "mic must have no output stream (n=%u)", n);
    n = get_list(kObjectID_Device2, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, ids, 8);
    CHECK(n == 1 && ids[0] == kObjectID_Stream_Output, "feed output streams n=%u", n);
    n = get_list(kObjectID_Device2, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, ids, 8);
    CHECK(n == 0, "feed must have no input stream (n=%u)", n);
    n = get_list(kObjectID_Device2, kAudioObjectPropertyControlList, kAudioObjectPropertyScopeGlobal, ids, 8);
    for (UInt32 i = 0; i < n; i++) CHECK(ids[i] == kObjectID_Volume_Output_Master || ids[i] == kObjectID_Mute_Output_Master, "feed control %u", ids[i]);
    printf("  ok  mic: 1 input stream, 0 output; feed: 1 output stream, 0 input; feed controls are its own (%u)\n", n);

    // ---- format: Float32, 2 ch, 44.1/48/96 kHz, 48 kHz at start ----
    {
        AudioStreamBasicDescription f; UInt32 size = sizeof f;
        AudioObjectPropertyAddress a = { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        CHECK((*D)->GetPropertyData(D, kObjectID_Stream_Input, 0, &a, 0, NULL, size, &size, &f) == 0, "format");
        CHECK(f.mFormatID == kAudioFormatLinearPCM && (f.mFormatFlags & kAudioFormatFlagIsFloat) && f.mBitsPerChannel == 32 && f.mChannelsPerFrame == 2, "format %u ch %u bits", f.mChannelsPerFrame, f.mBitsPerChannel);
        CHECK(f.mSampleRate == 48000.0, "start rate %f", f.mSampleRate);
        AudioValueRange r[8]; size = sizeof r;
        AudioObjectPropertyAddress ar = { kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        CHECK((*D)->GetPropertyData(D, kObjectID_Device, 0, &ar, 0, NULL, size, &size, r) == 0, "rates");
        CHECK(size / sizeof(AudioValueRange) == 3 && r[0].mMinimum == 44100 && r[1].mMinimum == 48000 && r[2].mMinimum == 96000, "rates n=%lu", size / sizeof(AudioValueRange));
        printf("  ok  Float32, 2 channels, rates 44100/48000/96000, 48000 at start\n");
    }

    // ---- latency and safety offset, both directions: 0 (truthful: same-cycle loopback) ----
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput) == 0, "mic latency");
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput) == 0, "mic safety");
    CHECK(get_u32(kObjectID_Device2, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) == 0, "feed latency");
    CHECK(get_u32(kObjectID_Device2, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput) == 0, "feed safety");
    CHECK(get_u32(kObjectID_Device,  kAudioDevicePropertyZeroTimeStampPeriod, kAudioObjectPropertyScopeGlobal) == kDevice_RingBufferSize, "zts period");
    printf("  ok  latency 0 and safety offset 0 on mic and feed; zero time stamp period %u\n", kDevice_RingBufferSize);

    // ---- custom property list and the status dictionary ----
    {
        AudioServerPlugInCustomPropertyInfo info; UInt32 size = sizeof info;
        AudioObjectPropertyAddress a = { kAudioObjectPropertyCustomPropertyInfoList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        CHECK((*D)->HasProperty(D, kObjectID_Device, 0, &a), "custom list");
        CHECK((*D)->GetPropertyData(D, kObjectID_Device2, 0, &a, 0, NULL, size, &size, &info) == 0, "custom list data");
        CHECK(info.mSelector == 'mvst' && info.mPropertyDataType == kAudioServerPlugInCustomPropertyDataTypeCFPropertyList, "custom info");
        CFDictionaryRef s = get_status(kObjectID_Device);
        CHECK(dict_num(s, "version") == 1 && dict_num(s, "sampleRate") == 48000 && dict_num(s, "clockRatio") == 1.0, "status basics");
        CHECK(dict_num(s, "latencyFrames") == 0 && dict_num(s, "safetyOffsetFrames") == 0 && dict_num(s, "micClients") == 0, "status zero state");
        CFRelease(s);
        printf("  ok  custom property 'mvst' (CFPropertyList, read only): version 1, 48000, clockRatio 1.0\n");
    }

    // ---- loopback: Feed WriteMix at S -> Mic ReadInput at S ----
    {
        CHECK((*D)->StartIO(D, kObjectID_Device, 1) == 0, "StartIO mic");
        CHECK((*D)->StartIO(D, kObjectID_Device2, 2) == 0, "StartIO feed");
        Float64 st = 0; UInt64 ht = 0, seed = 0;
        CHECK((*D)->GetZeroTimeStamp(D, kObjectID_Device, 1, &st, &ht, &seed) == 0 && ht != 0, "zero time stamp");
        Float64 st2 = 0; UInt64 ht2 = 0;
        CHECK((*D)->GetZeroTimeStamp(D, kObjectID_Device2, 2, &st2, &ht2, &seed) == 0 && st2 == st && ht2 == ht, "mic and feed share one time line");

        enum { N = 512 };
        static Float32 w[N * 2], r[N * 2];
        Float64 S = 100000.0;  // wraps the 65536-frame ring: tests the split copy too
        for (int blk = 0; blk < 8; blk++) {
            Float64 t = S + blk * N;
            for (int i = 0; i < N; i++) { w[2*i] = (Float32)((t + i) * 1e-6); w[2*i+1] = -w[2*i]; }
            do_io(kObjectID_Device2, kObjectID_Stream_Output, kAudioServerPlugInIOOperationWriteMix, t, N, w);
            memset(r, 0, sizeof r);
            do_io(kObjectID_Device, kObjectID_Stream_Input, kAudioServerPlugInIOOperationReadInput, t, N, r);
            for (int i = 0; i < N * 2; i++) CHECK(r[i] == w[i], "block %d sample %d: read %g wrote %g (must be bit exact, volume control off)", blk, i, r[i], w[i]);
        }
        CFDictionaryRef s = get_status(kObjectID_Device);
        CHECK(dict_num(s, "writeCycles") == 8 && dict_num(s, "readCycles") == 8 && dict_num(s, "silentReads") == 0, "counters after loopback");
        CHECK(dict_num(s, "lastWriteEndSampleTime") == S + 8 * N && dict_num(s, "lastReadEndSampleTime") == S + 8 * N, "end times");
        CHECK(dict_num(s, "fillFrames") == 0 && dict_num(s, "micClients") == 1 && dict_num(s, "feedClients") == 1, "fill after same-cycle loopback");
        CFRelease(s);
        printf("  ok  loopback bit exact over 8 x %d frames across the ring wrap; status counters match\n", N);

        // writer ahead by 2 buffers -> fill 1024 frames; reading past the writer -> silence, counted
        Float64 t = S + 8 * N;
        for (int b = 0; b < 2; b++) { for (int i = 0; i < N * 2; i++) w[i] = 0.25f; do_io(kObjectID_Device2, kObjectID_Stream_Output, kAudioServerPlugInIOOperationWriteMix, t + b * N, N, w); }
        s = get_status(kObjectID_Device);
        CHECK(dict_num(s, "fillFrames") == 2 * N, "fill %g, want %d", dict_num(s, "fillFrames"), 2 * N);
        CHECK(fabs(dict_num(s, "fillMs") - 2 * N * 1000.0 / 48000.0) < 1e-9, "fillMs");
        CFRelease(s);
        do_io(kObjectID_Device, kObjectID_Stream_Input, kAudioServerPlugInIOOperationReadInput, t + 10 * N, N, r);  // far past the writer
        for (int i = 0; i < N * 2; i++) CHECK(r[i] == 0.0f, "underrun must be silence, never old sound");
        s = get_status(kObjectID_Device);
        CHECK(dict_num(s, "silentReads") == 1, "silentReads %g", dict_num(s, "silentReads"));
        CFRelease(s);
        printf("  ok  fill = writer ahead (1024 frames = 21.333 ms); an underrun reads silence and is counted\n");

        CHECK((*D)->StopIO(D, kObjectID_Device2, 2) == 0 && (*D)->StopIO(D, kObjectID_Device, 1) == 0, "StopIO");
        CHECK(gRingBuffer == NULL, "ring freed after the last client");
    }

    // ---- sample rate change to 96 kHz goes through the host's config change ----
    {
        AudioObjectPropertyAddress a = { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        Float64 rate = 96000.0; UInt32 nChanged = 0; AudioObjectPropertyAddress changed[2];
        CHECK((*D)->SetPropertyData(D, kObjectID_Device, 0, &a, 0, NULL, sizeof rate, &rate) == 0, "set 96k");
        (void)nChanged; (void)changed;
        CHECK((*D)->PerformDeviceConfigurationChange(D, kObjectID_Device, ChangeAction_SetSampleRate, NULL) == 0, "perform change");
        CHECK(gDevice_SampleRate == 96000.0, "rate now %f", gDevice_SampleRate);
        printf("  ok  96 kHz accepted through RequestDeviceConfigurationChange / Perform\n");
    }

    printf("ALL %d CHECKS PASSED\n", gChecks);
    return 0;
}
