// Ori and the Will of the Wisps VR, native mod (build 1, Phase 1).
// Loaded by the winhttp.dll proxy. Finds IL2CPP classes by name through the game's exported IL2CPP API, hooks
// MoonRenderPipeline.Render with MinHook, and renders the game's camera once per eye from the same game tick, each
// eye through its own MoonRenderPipelineView (the game's view keeps history between renders; RE_NOTES "Feasibility").
// The diorama: a flat rectangle fixed in the room (the stage) shows what the flat game shows at z 0; each eye looks
// through it from its own place (Ori VR's design). OpenXR runs on its own thread (xr\WotwXR.cpp).
// Lab commands in vrlab_command.txt (only with [Lab] Commands=1 in the ini): see PollCommands.
// MinHook (TsudaKageyu, BSD-2) is compiled in.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include "MinHook.h"
#include "../xr/WotwXR.h"

#define MOD_VERSION "1.0"
#define MOD_BUILD 5

// ---------------------------------------------------------------- log
static std::wstring g_gameDir;
static std::mutex g_logMx;
static FILE* g_log = nullptr;
static LARGE_INTEGER g_qpf, g_qp0;
static double Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart - g_qp0.QuadPart) / g_qpf.QuadPart; }
static void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

// ---------------------------------------------------------------- settings (OriWotWVR\OriWotWVR.ini)
struct Settings {
    float stageWidth = 1.85f, stageDistance = 1.0f, depth = 1.5f, resolution = 1.0f, tanSpan = 0;
    int picture = 0;            // 0 Auto, 1 Steady, 2 Panel
    int gameSettingsDone = 0;   // 1 once the VR game settings were applied (first run)
    float menuSize = 1.0f, menuHeight = 0.0f, menuDistance = 0.0f;
    int thunderShake = 0;      // 0: the camera shake of lightning strikes is off in VR
    // Ori VR's options (all of Ori VR's options)
    float popOut = 0.0f;        // 0 keeps the gameplay plane on the stage; higher brings more of the scene toward you
    float sharpen = 0.3f;       // sharpen pass strength, 0 off
    int surround = 0;           // 0 Black, 1 Green (Virtual Desktop chroma key), 2 Transparent
    int catchUp = 1;            // 1: a late frame makes up the lost time; 0: the game slows a little instead
    int sounds = 2;             // menu tones: 0 Off, 1 Low, 2 Medium, 3 High
    int screenShake = 0;        // 0: every camera shake off in VR (bosses, thunder, impacts)
    int labCommands = 0;        // [Lab] Commands=1: test commands from vrlab_command.txt and the exception logger (lab only)
};
static const Settings kDefaults;
static Settings g_set;
static std::wstring IniPath() { return g_gameDir + L"OriWotWVR\\OriWotWVR.ini"; }
static float IniF(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t b[64], d[64]; swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, b, 64, IniPath().c_str()); return (float)_wtof(b);
}
static void IniW(const wchar_t* sec, const wchar_t* key, float v) {
    wchar_t b[64]; swprintf_s(b, L"%g", v); WritePrivateProfileStringW(sec, key, b, IniPath().c_str());
}
static void LoadSettings() {
    g_set.stageWidth = IniF(L"Stage", L"Width", 1.85f);
    g_set.stageDistance = IniF(L"Stage", L"Distance", 1.0f);
    g_set.depth = IniF(L"Stage", L"Depth", 1.5f);
    g_set.resolution = IniF(L"VR", L"Resolution", 1.0f);
    g_set.picture = (int)IniF(L"VR", L"Picture", 0);
    g_set.tanSpan = IniF(L"VR", L"EyeSpanLastRun", 0);
    g_set.gameSettingsDone = (int)IniF(L"Game", L"VRSettingsApplied", 0);
    g_set.menuSize = IniF(L"Menu", L"Size", 1.0f);
    g_set.menuHeight = IniF(L"Menu", L"Height", 0.0f);
    g_set.menuDistance = IniF(L"Menu", L"Distance", 0.0f);
    g_set.thunderShake = (int)IniF(L"VR", L"ThunderShake", 0);
    g_set.popOut = IniF(L"Stage", L"PopOut", 0.0f);
    g_set.sharpen = IniF(L"VR", L"Sharpen", 0.3f);
    g_set.surround = (int)IniF(L"VR", L"Surround", 0);
    g_set.catchUp = 1;  // always on (no menu row for something that should always be on)
    g_set.sounds = (int)IniF(L"Menu", L"Sounds", 2);
    g_set.screenShake = (int)IniF(L"Stage", L"ScreenShake", 0);
    g_set.labCommands = (int)IniF(L"Lab", L"Commands", 0);
}
static void SaveSettings() {
    IniW(L"Stage", L"Width", g_set.stageWidth); IniW(L"Stage", L"Distance", g_set.stageDistance); IniW(L"Stage", L"Depth", g_set.depth);
    IniW(L"VR", L"Resolution", g_set.resolution); IniW(L"VR", L"Picture", (float)g_set.picture);
    IniW(L"Game", L"VRSettingsApplied", (float)g_set.gameSettingsDone);
    IniW(L"Menu", L"Size", g_set.menuSize); IniW(L"Menu", L"Height", g_set.menuHeight); IniW(L"Menu", L"Distance", g_set.menuDistance);
    IniW(L"Stage", L"PopOut", g_set.popOut); IniW(L"VR", L"Sharpen", g_set.sharpen); IniW(L"VR", L"Surround", (float)g_set.surround);
    IniW(L"VR", L"FrameCatchUp", (float)g_set.catchUp); IniW(L"Menu", L"Sounds", (float)g_set.sounds);
    IniW(L"Stage", L"ScreenShake", (float)g_set.screenShake);
}

// ---------------------------------------------------------------- il2cpp
struct Il2CppObject { void* klass; void* monitor; };
struct Il2CppArray { void* klass; void* monitor; void* bounds; uintptr_t max_length; void* data[1]; };
struct Il2CppString { void* klass; void* monitor; int32_t length; wchar_t chars[1]; };
struct MethodInfo { void* methodPointer; };
struct Mat4 { float m[16]; };  // Unity column-major: element (row r, col c) at m[c*4+r]
static float& M(Mat4& a, int r, int c) { return a.m[c * 4 + r]; }

#define IL2CPP_FN(ret, name, args) typedef ret(*name##_t) args; static name##_t name;
IL2CPP_FN(void*, il2cpp_domain_get, ())
IL2CPP_FN(void**, il2cpp_domain_get_assemblies, (void*, size_t*))
IL2CPP_FN(void*, il2cpp_assembly_get_image, (void*))
IL2CPP_FN(void*, il2cpp_class_from_name, (void*, const char*, const char*))
IL2CPP_FN(MethodInfo*, il2cpp_class_get_methods, (void*, void**))
IL2CPP_FN(const char*, il2cpp_method_get_name, (MethodInfo*))
IL2CPP_FN(uint32_t, il2cpp_method_get_param_count, (MethodInfo*))
IL2CPP_FN(void*, il2cpp_method_get_param, (MethodInfo*, uint32_t))
IL2CPP_FN(char*, il2cpp_type_get_name, (void*))
IL2CPP_FN(void*, il2cpp_resolve_icall, (const char*))
IL2CPP_FN(Il2CppObject*, il2cpp_object_new, (void*))
IL2CPP_FN(void*, il2cpp_class_get_field_from_name, (void*, const char*))
IL2CPP_FN(void, il2cpp_field_static_get_value, (void*, void*))
IL2CPP_FN(void, il2cpp_field_static_set_value, (void*, void*))
IL2CPP_FN(Il2CppObject*, il2cpp_string_new, (const char*))
IL2CPP_FN(uint32_t, il2cpp_gchandle_new, (Il2CppObject*, bool))
IL2CPP_FN(void*, il2cpp_class_get_parent, (void*))
IL2CPP_FN(void, il2cpp_free, (void*))
IL2CPP_FN(void, il2cpp_format_exception, (Il2CppObject*, char*, int))
IL2CPP_FN(void, il2cpp_format_stack_trace, (Il2CppObject*, char*, int))
// IL2CPP throws managed exceptions as this C++ type; same name, so MSVC's catch matches it across modules.
struct Il2CppExceptionWrapper { Il2CppObject* ex; };

static HMODULE g_ga;
static bool BindIl2cpp() {
#define B(n) n = (n##_t)GetProcAddress(g_ga, #n); if (!n) { Log("IL2CPP export missing: " #n); return false; }
    B(il2cpp_domain_get) B(il2cpp_domain_get_assemblies) B(il2cpp_assembly_get_image) B(il2cpp_class_from_name)
    B(il2cpp_class_get_methods) B(il2cpp_method_get_name) B(il2cpp_method_get_param_count) B(il2cpp_method_get_param)
    B(il2cpp_type_get_name) B(il2cpp_resolve_icall) B(il2cpp_object_new) B(il2cpp_class_get_field_from_name)
    B(il2cpp_field_static_get_value) B(il2cpp_field_static_set_value) B(il2cpp_string_new) B(il2cpp_gchandle_new) B(il2cpp_class_get_parent) B(il2cpp_free) B(il2cpp_format_exception) B(il2cpp_format_stack_trace)
#undef B
    return true;
}
static void* FindClass(const char* ns, const char* name) {
    size_t n = 0;
    void** as = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) { void* k = il2cpp_class_from_name(il2cpp_assembly_get_image(as[i]), ns, name); if (k) return k; }
    return nullptr;
}
static MethodInfo* FindMethod(void* klass, const char* name, int argc, const char* types = nullptr) {
    for (void* k = klass; k; k = il2cpp_class_get_parent(k)) {
        void* it = nullptr;
        while (MethodInfo* m = il2cpp_class_get_methods(k, &it)) {
            if (strcmp(il2cpp_method_get_name(m), name) != 0) continue;
            if (argc >= 0 && (int)il2cpp_method_get_param_count(m) != argc) continue;
            if (types) {
                std::string sig;
                for (int i = 0; i < argc; i++) { char* tn = il2cpp_type_get_name(il2cpp_method_get_param(m, i)); if (i) sig += ","; sig += tn; il2cpp_free(tn); }
                if (sig != types) continue;
            }
            return m;
        }
    }
    return nullptr;
}
static int g_missing = 0;
static MethodInfo* Need(void* klass, const char* kname, const char* name, int argc, const char* types = nullptr) {
    MethodInfo* m = klass ? FindMethod(klass, name, argc, types) : nullptr;
    Log("HOOKMAP %s.%s(%d) %s", kname, name, argc, m ? "found" : "NOT FOUND");
    if (!m) g_missing++;
    return m;
}
template <class F> F P(MethodInfo* m) { return (F)m->methodPointer; }

// ---------------------------------------------------------------- game API
static MethodInfo *mCamGetW2C, *mCamSetW2C, *mCamGetProj, *mCamSetProj, *mCamResetW2C, *mCamResetProj, *mCamGetTarget,
    *mCamSetTarget, *mCamPixW, *mCamPixH, *mObjName, *mRTCtor, *mRTCreate, *mTexNative, *mTimeTime, *mTimeUnscaled,
    *mTimeScale, *mTimeFixedDelta, *mTimeMaxDelta, *mTimeFrame, *mTimeFixedTime, *mVSync, *mSetVSync, *mTargetFps, *mSetTargetFps,
    *mScreenW, *mScreenH, *mGsVsync, *mGsSetVsync, *mGsTargetFps, *mGsSetTargetFps, *mGsHdr, *mGsSetHdr, *mGsDyn, *mGsSetDyn,
    *mGsBlur, *mGsSetBlur, *mGsQuality, *mGsSetQuality, *mGsDisplayScale, *mGsSetDisplayScale, *mGsHud, *mDtmController,
    *mSetMaxDelta, *mSetTimeScale, *mRender, *mViewCtor, *mViewDispose, *mFocus, *mGsSave, *mSetHideFlags, *mObjAlive;
static void *fGsInstance, *fSyncForce, *fSyncEnable, *fDtmInstance;
typedef void (*IssuePluginEvent_t)(void* cb, int id);
static IssuePluginEvent_t g_issue;

static Mat4 CamGetMat(MethodInfo* m, Il2CppObject* cam) { return P<Mat4 (*)(Il2CppObject*, MethodInfo*)>(m)(cam, m); }
static void CamSetMat(MethodInfo* m, Il2CppObject* cam, Mat4 v) { P<void (*)(Il2CppObject*, Mat4, MethodInfo*)>(m)(cam, v, m); }
static void Call0(MethodInfo* m, Il2CppObject* o) { P<void (*)(Il2CppObject*, MethodInfo*)>(m)(o, m); }
static Il2CppObject* GetObj(MethodInfo* m, Il2CppObject* o) { return P<Il2CppObject* (*)(Il2CppObject*, MethodInfo*)>(m)(o, m); }
static float SF(MethodInfo* m) { return P<float (*)(MethodInfo*)>(m)(m); }
static int SI(MethodInfo* m) { return P<int (*)(MethodInfo*)>(m)(m); }
static void SetSI(MethodInfo* m, int v) { P<void (*)(int, MethodInfo*)>(m)(v, m); }
static float IF(MethodInfo* m, Il2CppObject* o) { return P<float (*)(Il2CppObject*, MethodInfo*)>(m)(o, m); }
static int II(MethodInfo* m, Il2CppObject* o) { return P<int (*)(Il2CppObject*, MethodInfo*)>(m)(o, m); }
static bool IB(MethodInfo* m, Il2CppObject* o) { return P<bool (*)(Il2CppObject*, MethodInfo*)>(m)(o, m); }
static void SetIB(MethodInfo* m, Il2CppObject* o, bool v) { if (m) P<void (*)(Il2CppObject*, bool, MethodInfo*)>(m)(o, v, m); }
static void SetII(MethodInfo* m, Il2CppObject* o, int v) { if (m) P<void (*)(Il2CppObject*, int, MethodInfo*)>(m)(o, v, m); }
static void SetIF(MethodInfo* m, Il2CppObject* o, float v) { if (m) P<void (*)(Il2CppObject*, float, MethodInfo*)>(m)(o, v, m); }
static void* TexPtr(Il2CppObject* t) { return P<void* (*)(Il2CppObject*, MethodInfo*)>(mTexNative)(t, mTexNative); }
static void SetTarget(Il2CppObject* cam, Il2CppObject* rt) { P<void (*)(Il2CppObject*, Il2CppObject*, MethodInfo*)>(mCamSetTarget)(cam, rt, mCamSetTarget); }

static Il2CppObject* MakeRT(int w, int h) {
    Il2CppObject* rt = il2cpp_object_new(FindClass("UnityEngine", "RenderTexture"));
    il2cpp_gchandle_new(rt, true);
    P<void (*)(Il2CppObject*, int, int, int, int, MethodInfo*)>(mRTCtor)(rt, w, h, 24, 0, mRTCtor);  // ARGB32
    P<bool (*)(Il2CppObject*, MethodInfo*)>(mRTCreate)(rt, mRTCreate);
    // HideFlags.DontUnloadUnusedAsset (32): the game's Resources.UnloadUnusedAssets (title and scene loads) destroyed
    // our eye textures otherwise, and every later call on them threw NullReferenceException (RigB, 2026-11-20).
    if (mSetHideFlags) P<void (*)(Il2CppObject*, int, MethodInfo*)>(mSetHideFlags)(rt, 32, mSetHideFlags);
    return rt;
}

// ---------------------------------------------------------------- lab snapshots (render thread readback)
struct Snap { ID3D11Texture2D* tex; std::string name; };
static std::mutex g_snapMx;
static std::vector<Snap> g_snapQueue;
static void WriteSnap(ID3D11Texture2D* src, const std::string& name) {
    ID3D11Device* dev = nullptr; src->GetDevice(&dev);
    ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st))) {
        ctx->CopyResource(st, src);
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &map))) {
            std::wstring dir = g_gameDir + L"OriWotWVR\\snaps\\";
            CreateDirectoryW(dir.c_str(), nullptr);
            FILE* f = _wfopen((dir + std::wstring(name.begin(), name.end()) + L".raw").c_str(), L"wb");
            if (f) {
                uint32_t hdr[4] = { d.Width, d.Height, (uint32_t)d.Format, 4 };
                fwrite(hdr, 4, 4, f);
                for (UINT y = 0; y < d.Height; y++) fwrite((uint8_t*)map.pData + y * map.RowPitch, 1, d.Width * 4, f);
                fclose(f);
            }
            ctx->Unmap(st, 0);
            Log("SNAP %s %ux%u format %d written", name.c_str(), d.Width, d.Height, (int)d.Format);
        }
        st->Release();
    }
    ctx->Release(); dev->Release();
}
static void __stdcall OnSnapEvent(int) {
    std::vector<Snap> q;
    { std::lock_guard<std::mutex> lk(g_snapMx); q.swap(g_snapQueue); }
    for (auto& s : q) WriteSnap(s.tex, s.name);
}

// ---------------------------------------------------------------- GPU time of the eye renders
struct GpuSlot { ID3D11Query *disjoint = nullptr, *t0 = nullptr, *t1 = nullptr; bool pending = false; };
static GpuSlot g_gpu[6]; static int g_gpuW = 0;
static std::mutex g_gpuMx; static double g_gpuSum = 0, g_gpuMax = 0; static uint32_t g_gpuN = 0;
static ID3D11Device* g_gameDev; static ID3D11DeviceContext* g_gameCtx;
static void __stdcall OnGpuEvent(int id) {
    if (!g_gameCtx) return;
    if (!g_gpu[0].disjoint) {
        D3D11_QUERY_DESC dj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, ts{D3D11_QUERY_TIMESTAMP, 0};
        for (auto& g : g_gpu) { g_gameDev->CreateQuery(&dj, &g.disjoint); g_gameDev->CreateQuery(&ts, &g.t0); g_gameDev->CreateQuery(&ts, &g.t1); }
    }
    GpuSlot& g = g_gpu[g_gpuW];
    if (id == 10) {
        if (g.pending) {  // read the oldest slot before reusing it; skip if not ready
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a, b;
            if (g_gameCtx->GetData(g.disjoint, &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                g_gameCtx->GetData(g.t0, &a, sizeof a, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                g_gameCtx->GetData(g.t1, &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency) {
                double ms = (double)(b - a) * 1000.0 / dj.Frequency;
                std::lock_guard<std::mutex> lk(g_gpuMx); g_gpuSum += ms; if (ms > g_gpuMax) g_gpuMax = ms; g_gpuN++;
            }
        }
        g_gameCtx->Begin(g.disjoint); g_gameCtx->End(g.t0);
    } else {
        g_gameCtx->End(g.t1); g_gameCtx->End(g.disjoint); g.pending = true;
        g_gpuW = (g_gpuW + 1) % 6;
    }
}

// ---------------------------------------------------------------- state
static bool g_vrOn = true;           // lab: "vr 0" stops eye rendering (flat game)
static bool g_flatToo = false;
// The game's interleaved rendering (MoonRenderPipeline.m_isrDRSEnabled 0x5C, m_isrCPUEnabled 0x5D) reuses depth slices
// from earlier frames when it thinks the CPU or GPU is slow. The eye views have no such history. Held off while VR runs.
static bool g_isrOff = true;
static bool g_wantFreshViews = false;
static bool g_rightUsesGameView = false;  // the right eye renders through the game's own view (the flat window is not drawn in VR)
static uint8_t g_isrSeen[2] = {0xff, 0xff};       // lab: "flat 1" keeps the game's own flat render while VR runs
static bool g_wantInfo = false, g_wantSnap = false;
static bool g_eyeCheck = false;      // lab: both eyes from the same place (zero-difference control)
static int g_lastFrame = -1, g_framesSeen = 0;
static Il2CppObject* g_eyeView[2];
static Il2CppObject* g_eyeRT[2];
static int g_eyeW = 0, g_eyeH = 0;
static uint64_t g_snapCount = 0;
static uint32_t g_renderCalls = 0, g_eyePairs = 0, g_flatSkipped = 0;
static bool g_vrStarted = false;
static uint32_t g_webBlocked = 0;
static void ApplyScreenShake();
static void Hook(MethodInfo* m, void* detour, void** orig, const char* name);
static double g_capNext = 0, g_capWaitSum = 0, g_capWaitMax = 0, g_eyeCpuSum = 0, g_eyeCpuMax = 0, g_lastFrameT = 0, g_frameMax = 0;
static uint32_t g_capped = 0, g_late = 0;
static double g_bestPeriod = 0;      // shortest steady period seen: the headset's full rate
static double g_capPeriod = 0;       // what the cap uses
static double g_lowSince = -1;

#include "Menu.inc"

static Il2CppObject* GameSettingsInstance() { Il2CppObject* gs = nullptr; if (fGsInstance) il2cpp_field_static_get_value(fGsInstance, &gs); return gs; }

static void LogGameSet(const char* when) {
    Il2CppObject* gs = GameSettingsInstance();
    if (!gs) { Log("GAMESET %s: GameSettings not ready", when); return; }
    Log("GAMESET %s: Unity vSyncCount %d, targetFrameRate %d, screen %dx%d; game Vsync %d, TargetFramerate %d, HDR %d, DynamicResolution %d, MotionBlur %d, Quality %d (0 Low 1 Balanced 2 High), ResolutionScale %.2f, Hud %d",
        when, SI(mVSync), SI(mTargetFps), SI(mScreenW), SI(mScreenH), IB(mGsVsync, gs), II(mGsTargetFps, gs), IB(mGsHdr, gs), IB(mGsDyn, gs),
        IB(mGsBlur, gs), II(mGsQuality, gs), IF(mGsDisplayScale, gs), II(mGsHud, gs));
}

// The game's settings that VR needs. VSync and the frame limit always (the mod paces the game); on first run also motion
// blur off, HDR off, dynamic resolution off, resolution scale 100%, quality Balanced (about Medium, RigB's start
// settings). Players can change the first-run ones back in the game's options.
static void ApplyGameSettings(bool firstRun) {
    Il2CppObject* gs = GameSettingsInstance();
    if (!gs) return;
    bool changed = false;
    if (IB(mGsVsync, gs)) { SetIB(mGsSetVsync, gs, false); changed = true; }
    if (II(mGsTargetFps, gs) < 200) { SetII(mGsSetTargetFps, gs, 201); changed = true; }  // 201 shows as OFF in the game's menu
    if (SI(mVSync) != 0) { SetSI(mSetVSync, 0); changed = true; }
    if (SI(mTargetFps) != -1) { SetSI(mSetTargetFps, -1); changed = true; }
    // Every VR start: HDR off (LESSONS_LEARNED HDR entry) and dynamic resolution off (the render size must hold).
    if (IB(mGsHdr, gs) || IB(mGsDyn, gs)) {
        SetIB(mGsSetHdr, gs, false); SetIB(mGsSetDyn, gs, false);
        if (!firstRun && mGsSave) Call0(mGsSave, gs);
        LogGameSet("HDR and dynamic resolution turned off for VR");
    }
    if (firstRun) {
        LogGameSet("before first-run VR settings");
        SetIB(mGsSetBlur, gs, false); SetIB(mGsSetHdr, gs, false); SetIB(mGsSetDyn, gs, false);
        SetIF(mGsSetDisplayScale, gs, 1.0f); SetII(mGsSetQuality, gs, 1);
        if (mGsSave) Call0(mGsSave, gs);  // the game's own save of settings.bin, so they hold next launch
        g_set.gameSettingsDone = 1; SaveSettings();
        LogGameSet("after first-run VR settings (motion blur, HDR, dynamic resolution off, resolution scale 100%, quality Balanced)");
    } else if (changed) {
        LogGameSet("the game turned VSync or a frame cap on during VR. Turned off again: the mod paces the game");
    }
}

// ---------------------------------------------------------------- frame cap at the headset's real rate
// Deadline based (TECHNIQUES "The mod's frame cap"): next = previous + period, re-anchored when more than a period late.
// Never below the full rate: the shortest period seen is the headset's rate; a longer report is followed only when it is
// steady for 20 s and is not 1/2, 2/3 or 1/3 of the known rate (smoothing).
static void Cap(double periodSec) {
    double now = Now();
    if (periodSec < 1.0 / 250 || periodSec > 1.0 / 30) periodSec = g_bestPeriod;  // ignore reports outside 30 to 250 Hz
    if (periodSec <= 0) return;
    if (g_bestPeriod == 0 || periodSec < g_bestPeriod * 0.98) { g_bestPeriod = periodSec; g_capPeriod = periodSec; g_lowSince = -1; Log("CAP headset rate %.1f Hz: the mod caps the game there", 1.0 / periodSec); }
    else if (periodSec > g_bestPeriod * 1.02) {
        double ratio = periodSec / g_bestPeriod;
        bool smoothing = fabs(ratio - 2) < 0.05 || fabs(ratio - 1.5) < 0.05 || fabs(ratio - 3) < 0.05;
        if (g_lowSince < 0) g_lowSince = now;
        if (!smoothing && now - g_lowSince > 20 && fabs(g_capPeriod - periodSec) > 1e-5) { g_capPeriod = periodSec; g_bestPeriod = periodSec; Log("CAP the headset now runs at %.1f Hz steadily: cap follows", 1.0 / periodSec); }
    } else g_lowSince = -1;
    if (g_capPeriod <= 0) return;
    if (g_capNext == 0 || now - g_capNext > g_capPeriod) g_capNext = now;
    double t0 = now;
    while (Now() < g_capNext) { if (g_capNext - Now() > 0.0015) Sleep(1); else YieldProcessor(); }
    double w = (Now() - t0) * 1000;
    g_capWaitSum += w; if (w > g_capWaitMax) g_capWaitMax = w; g_capped++;
    g_capNext += g_capPeriod;
}

// ---------------------------------------------------------------- periodic log block
static double g_statT0 = -1; static float g_statGame0 = 0; static int g_statFrames0 = 0;
static void Periodic() {
    double t = Now();
    float game = SF(mTimeTime); int frame = SI(mTimeFrame);
    if (g_statT0 < 0) { g_statT0 = t; g_statGame0 = game; g_statFrames0 = frame; return; }
    double dt = t - g_statT0;
    if (dt < 5.0) return;
    int frames = frame - g_statFrames0;
    double speed = (game - g_statGame0) / dt;
    bool syncForce = false, syncEnable = false;
    if (fSyncForce) il2cpp_field_static_get_value(fSyncForce, &syncForce);
    if (fSyncEnable) il2cpp_field_static_get_value(fSyncEnable, &syncEnable);
    Il2CppObject* dtm = nullptr; if (fDtmInstance) il2cpp_field_static_get_value(fDtmInstance, &dtm);
    wxr::Views v; wxr::GetViews(&v);
    double hz = v.periodSec > 0 ? 1.0 / v.periodSec : 0;
    Log("SPEED %.1f s: game speed %.3f, %.1f frames/s, headset %.1f Hz (cap %.1f Hz), timeScale %.3f, tick %.2f ms, maxDelta %.4f, SyncFramesTest force %d enable %d, DeltaTimeManager %d",
        dt, speed, frames / dt, hz, g_capPeriod > 0 ? 1.0 / g_capPeriod : 0, SF(mTimeScale), SF(mTimeFixedDelta) * 1000, SF(mTimeMaxDelta), syncForce, syncEnable,
        (dtm && mDtmController) ? II(mDtmController, dtm) : -1);
    if (g_vrStarted && fabs(speed - 1.0) > 0.05 && SF(mTimeScale) > 0.99) Log("SPEED WARNING: game time runs at %.2f of real time", speed);
    if (g_webBlocked) Log("OFFLINE %u web requests blocked so far", g_webBlocked);
    Log("INPUT pad reads: game wrapper %u, XInput DLLs %u; key reads blocked %u; menu open %d, blocking the game %d", g_xinputCalls, g_xinputNativeCalls, g_keyBlocked, menu::g_open, menu::g_block); g_xinputCalls = g_xinputNativeCalls = g_keyBlocked = 0;
    { std::lock_guard<std::mutex> lk(g_gpuMx);
      Log("GPU both eye renders avg %.2f ms max %.2f ms (%u frames measured)", g_gpuN ? g_gpuSum / g_gpuN : 0, g_gpuMax, g_gpuN); g_gpuSum = g_gpuMax = 0; g_gpuN = 0; }
    Log("PERF frame avg %.2f ms max %.2f ms, cap wait avg %.2f max %.2f ms, eye renders CPU avg %.2f max %.2f ms, %u eye pairs, %u render calls, flat render skipped %u, late frames %u",
        frames ? dt * 1000 / frames : 0, g_frameMax, g_capped ? g_capWaitSum / g_capped : 0, g_capWaitMax, g_eyePairs ? g_eyeCpuSum / g_eyePairs : 0, g_eyeCpuMax,
        g_eyePairs, g_renderCalls, g_flatSkipped, g_late);
    wxr::Stats s; wxr::TakeStats(&s);
    Log("XRSTAT state %d session %d focused %d, xr frames %u (no new image %u, layers sent %u), queued %u copied %u skipped busy %u no slot %u, copy max %.2f ms, xrWaitFrame avg %.2f max %.2f ms, xrEndFrame avg %.2f max %.2f ms, layer %d",
        wxr::GetState(), v.sessionState, v.focused, s.xrFrames, s.xrNoNewImage, s.layersSent, s.queued, s.copied, s.skippedBusy, s.skippedNoSlots, s.copyMsMax,
        s.waitMsAvg, s.waitMsMax, s.endMsAvg, s.endMsMax, wxr::LayerInUse());
    g_capWaitSum = g_capWaitMax = g_eyeCpuSum = g_eyeCpuMax = g_frameMax = 0; g_capped = g_eyePairs = g_renderCalls = g_flatSkipped = g_late = 0;
    g_statT0 = t; g_statGame0 = game; g_statFrames0 = frame;
    if (g_vrStarted) ApplyGameSettings(false);
}

static void PollCommands() {
    std::wstring path = g_gameDir + L"vrlab_command.txt";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf(); f.close();
    DeleteFileW(path.c_str());
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        Log("CMD %s", line.c_str());
        std::istringstream ls(line); std::string c; ls >> c;
        if (c == "info") g_wantInfo = true;
        else if (c == "snap") g_wantSnap = true;
        else if (c == "eyecheck") { int n; ls >> n; g_eyeCheck = n != 0; g_wantFreshViews = true; }  // fresh history for a fair control
        else if (c == "vr") { int n; ls >> n; g_vrOn = n != 0; }
        else if (c == "flat") { int n; ls >> n; g_flatToo = n != 0; }
        else if (c == "thundershake" || c == "shake") { ls >> g_set.screenShake; Log("CMD screen shake %s", g_set.screenShake ? "on" : "off"); }
        else if (c == "isroff") { int n; ls >> n; g_isrOff = n != 0; }
        else if (c == "rightgame") { int n; ls >> n; g_rightUsesGameView = n != 0; }
        else if (c == "recenter") wxr::Recenter();
        else if (c == "depth") { ls >> g_set.depth; }
        else if (c == "popout") { ls >> g_set.popOut; Log("CMD pop-out %.2f", g_set.popOut); }
        else if (c == "sharpen") { ls >> g_set.sharpen; wxr::SetSharpen(g_set.sharpen); Log("CMD sharpen %.2f", g_set.sharpen); }
        else if (c == "stage") { ls >> g_set.stageWidth >> g_set.stageDistance; wxr::SetStage({g_set.stageWidth, g_set.stageDistance}); }
        else if (c == "gamesettings") LogGameSet("asked");
        else if (c == "dyn" || c == "hdr" || c == "blur" || c == "quality") {
            int n; ls >> n; Il2CppObject* gs = GameSettingsInstance();
            if (gs) { if (c == "dyn") SetIB(mGsSetDyn, gs, n != 0); else if (c == "hdr") SetIB(mGsSetHdr, gs, n != 0); else if (c == "blur") SetIB(mGsSetBlur, gs, n != 0); else SetII(mGsSetQuality, gs, n); }
            LogGameSet("lab changed");
        }
        else if (c == "menu") { menu::SetOpen(!menu::g_open, "lab"); }
        else if (c == "fps") { int n; ls >> n; SetSI(mSetTargetFps, n); }
        else if (c == "vsync") { int n; ls >> n; SetSI(mSetVSync, n); }
        else Log("CMD unknown");
    }
}

// ---------------------------------------------------------------- eyes
static void WatchShake(Il2CppObject* cam);
typedef void (*Render_t)(Il2CppObject* self, void* ctx, Il2CppArray* cams, MethodInfo* m);
static Render_t oRender;

static void DropEyeViews(const char* why) {
    for (auto& v : g_eyeView) if (v) { try { Call0(mViewDispose, v); } catch (...) { Log("EYES disposing an eye view failed"); } v = nullptr; }
    Log("EYES eye views dropped: %s", why);
}
static double g_eyeRetryAt = 0;  // after a failure, eye rendering pauses before views are made again
static const char* g_eyeStep = "";  // what the eye render was doing, for a failure report
static void* g_viewsFor[3];
// Lab: where a game exception starts during an eye render. First-chance handler, logs GameAssembly offsets of the
// call stack (map them with tools\gadis.py / script.json). Only while an eye renders, at most 3 times.
static volatile bool g_inEye = false;
static volatile LONG g_vehLogged = 0;
static LONG CALLBACK EyeVeh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!g_vrStarted || g_vehLogged >= 6) return EXCEPTION_CONTINUE_SEARCH;
    if (code == 0x406D1388 || code == DBG_PRINTEXCEPTION_C || code == 0x4001000A) return EXCEPTION_CONTINUE_SEARCH;  // thread names, debug prints
    InterlockedIncrement(&g_vehLogged);
    void* frames[48]; USHORT n = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    uintptr_t ga = (uintptr_t)g_ga, up = (uintptr_t)GetModuleHandleW(L"UnityPlayer.dll");
    std::string line;
    char b[48];
    sprintf_s(b, "code %08lx at ", code); line += b;
    if (code == EXCEPTION_ACCESS_VIOLATION) { sprintf_s(b, "GA+%llx ", (unsigned long long)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - ga)); line += b; }
    for (USHORT i = 0; i < n; i++) {
        uintptr_t a = (uintptr_t)frames[i];
        if (a > ga && a < ga + 0x6000000) { sprintf_s(b, "GA+%llx ", (unsigned long long)(a - ga)); line += b; }
        else if (a > up && a < up + 0x3000000) { sprintf_s(b, "UP+%llx ", (unsigned long long)(a - up)); line += b; }
    }
    Log("EYES first-chance exception (in eye render %d, step %s): %s", (int)g_inEye, g_eyeStep, line.c_str());
    return EXCEPTION_CONTINUE_SEARCH;
}
static void* g_savedGameView;  // the pipeline, owner and settings the eye views were made for
static void RenderEyes(Il2CppObject* self, void* ctx, Il2CppArray* cams, Il2CppObject* cam, int frame) {
    double t0 = Now();
    void* nowFor[3] = {self, *(void**)((uint8_t*)self + 0x18), *(void**)((uint8_t*)self + 0x28)};
    if (g_wantFreshViews) { g_wantFreshViews = false; if (g_eyeView[0]) DropEyeViews("lab asked for fresh eye views"); }
    if (g_eyeView[0] && memcmp(nowFor, g_viewsFor, sizeof nowFor) != 0) {
        // The game made a new render pipeline (a graphics setting changed, or a scene asked for it): new eye views too.
        Log("EYES the game's render pipeline changed (%p -> %p): new eye views", g_viewsFor[0], self);
        DropEyeViews("pipeline changed");
    }
    if (g_eyeRT[0] && !(P<bool (*)(Il2CppObject*, MethodInfo*)>(mObjAlive)(g_eyeRT[0], mObjAlive) && P<bool (*)(Il2CppObject*, MethodInfo*)>(mObjAlive)(g_eyeRT[1], mObjAlive))) {
        Log("EYES the eye textures were destroyed by the game: made again");
        g_eyeRT[0] = g_eyeRT[1] = nullptr;
    }
    if (!g_eyeRT[0]) {
        g_eyeStep = "making eye textures";
        g_eyeW = wxr::EyeWidth(); g_eyeH = wxr::EyeHeight();
        g_eyeRT[0] = MakeRT(g_eyeW, g_eyeH); g_eyeRT[1] = MakeRT(g_eyeW, g_eyeH);
    }
    if (!g_eyeView[0]) {
        g_eyeStep = "making eye views";
        memcpy(g_viewsFor, nowFor, sizeof nowFor);
        void* owner = *(void**)((uint8_t*)self + 0x18); void* settings = *(void**)((uint8_t*)self + 0x28);
        void* kView = FindClass("Moon.Rendering", "MoonRenderPipelineView");
        for (int i = 0; i < 2; i++) {
            g_eyeView[i] = il2cpp_object_new(kView); il2cpp_gchandle_new(g_eyeView[i], true);
            P<void (*)(Il2CppObject*, int, void*, void*, MethodInfo*)>(mViewCtor)(g_eyeView[i], 0, owner, settings, mViewCtor);
        }
        Log("EYES eye textures %dx%d, two pipeline views made", g_eyeW, g_eyeH);
    }
    wxr::Views v; wxr::GetViews(&v);
    if (!g_gameDev) { ID3D11Texture2D* t = (ID3D11Texture2D*)TexPtr(g_eyeRT[0]); if (t) { t->GetDevice(&g_gameDev); g_gameDev->GetImmediateContext(&g_gameCtx); } }
    g_issue((void*)&OnGpuEvent, 10);
    Mat4 view = CamGetMat(mCamGetW2C, cam), proj = CamGetMat(mCamGetProj, cam);
    float dist = fabsf(M(view, 2, 3));
    if (dist < 0.01f) dist = 0.01f;
    float planeW = 2 * dist / M(proj, 0, 0), planeH = 2 * dist / M(proj, 1, 1);  // the z 0 rectangle the flat game shows
    float upm = planeW / g_set.stageWidth;  // game units per metre on the stage
    Il2CppArray* views = *(Il2CppArray**)((uint8_t*)self + 0x20);  // MoonRenderPipeline.views
    void* gameView = views->data[0];
    g_savedGameView = gameView;
    for (int e = 0; e < 2; e++) {
        float lx, ly, lz;
        if (g_eyeCheck) { lx = ly = lz = 0; }
        else if (v.valid && v.recenters) { lx = v.local[e][0]; ly = v.local[e][1]; lz = v.local[e][2]; }
        else { lx = e ? 0.0315f : -0.0315f; ly = lz = 0; }   // no tracking: stand at the viewing spot, fixed eye gap
        float x = lx * upm * g_set.depth, y = ly * upm * g_set.depth;
        float eyeDist = dist + lz * upm; if (eyeDist < dist * 0.25f) eyeDist = dist * 0.25f;
        Mat4 ve = view, pe = proj;
        M(ve, 0, 3) -= x; M(ve, 1, 3) -= y; M(ve, 2, 3) -= (eyeDist - dist);
        // Pop-out (Ori VR): the world depth planeZ behind z 0 sits on the stage; the flat view from the middle is unchanged
        // because the rectangle grows with the distance.
        float planeZ = g_set.popOut * dist, grow = (dist + planeZ) / dist;
        float wz = planeW * grow, hz = planeH * grow, dz = eyeDist + planeZ;
        M(pe, 0, 0) = 2 * dz / wz; M(pe, 0, 2) = -2 * x / wz;
        M(pe, 1, 1) = 2 * dz / hz; M(pe, 1, 2) = -2 * y / hz;
        CamSetMat(mCamSetW2C, cam, ve); CamSetMat(mCamSetProj, cam, pe);
        SetTarget(cam, g_eyeRT[e]);
        if (!(e == 1 && g_rightUsesGameView)) views->data[0] = g_eyeView[e];
        g_eyeStep = e ? "rendering the right eye" : "rendering the left eye";
        g_inEye = true;
        oRender(self, ctx, cams, mRender);
        g_inEye = false;
        views->data[0] = gameView;
    }
    SetTarget(cam, nullptr);
    Call0(mCamResetW2C, cam); Call0(mCamResetProj, cam);
    g_issue((void*)&OnGpuEvent, 11);
    void* l = TexPtr(g_eyeRT[0]); void* r = TexPtr(g_eyeRT[1]);
    wxr::QueueFrame(l, r, v, (uint32_t)frame);
    g_issue(wxr::RenderEventFunc(), 1);
    if (g_wantSnap) {
        g_wantSnap = false; g_snapCount++;
        Log("EYES snap %llu frame %d, eyecheck %d, depth %.2f, plane distance %.2f, head local L (%.3f %.3f %.3f) R (%.3f %.3f %.3f)",
            (unsigned long long)g_snapCount, frame, g_eyeCheck, g_set.depth, dist, v.local[0][0], v.local[0][1], v.local[0][2], v.local[1][0], v.local[1][1], v.local[1][2]);
        std::lock_guard<std::mutex> lk(g_snapMx);
        char nm[64];
        sprintf_s(nm, "s%llu_f%d_L", (unsigned long long)g_snapCount, frame); g_snapQueue.push_back({(ID3D11Texture2D*)l, nm});
        sprintf_s(nm, "s%llu_f%d_R", (unsigned long long)g_snapCount, frame); g_snapQueue.push_back({(ID3D11Texture2D*)r, nm});
        g_issue((void*)&OnSnapEvent, 2);
    }
    double ms = (Now() - t0) * 1000;
    g_eyeCpuSum += ms; if (ms > g_eyeCpuMax) g_eyeCpuMax = ms; g_eyePairs++;
}

static void hkRender(Il2CppObject* self, void* ctx, Il2CppArray* cams, MethodInfo* m) {
    g_renderCalls++;
    int frame = SI(mTimeFrame);
    if (frame != g_lastFrame) {
        g_lastFrame = frame; g_framesSeen++;
        double now = Now();
        if (g_lastFrameT > 0) { double ft = (now - g_lastFrameT) * 1000; if (ft > g_frameMax) g_frameMax = ft; if (g_capPeriod > 0 && ft > g_capPeriod * 1500) g_late++; }
        g_lastFrameT = now;
        if (g_set.labCommands && (g_framesSeen % 15) == 0) PollCommands();
        if ((g_framesSeen % 72) == 0) HookXInputDlls();  // also catches an XInput DLL loaded later
        menu::Update(now);
        Periodic();
        ApplyScreenShake();
        Il2CppObject* gsNow = GameSettingsInstance();
        // Wait until the game has loaded its saved settings (GameSettings.HasLoadedGameSettingsOnce, 0x50): set earlier,
        // the game's load put HDR, dynamic resolution and motion blur back on (RigB, 2026-11-20).
        if (!g_vrStarted && g_vrOn && gsNow && *((uint8_t*)gsNow + 0x50)) {
            g_vrStarted = true;
            ApplyGameSettings(!g_set.gameSettingsDone);
            wxr::Configure({g_set.stageWidth, g_set.stageDistance}, g_set.picture, g_set.resolution, g_set.tanSpan, 16.f / 9.f);
            wxr::SetSurround(g_set.surround); wxr::SetSharpen(g_set.sharpen);
            wxr::Start();
            Log("VR starting: stage %.2f m wide at %.2f m, depth %.2f, resolution %.2f, picture %d", g_set.stageWidth, g_set.stageDistance, g_set.depth, g_set.resolution, g_set.picture);
        }
        wxr::Views v; wxr::GetViews(&v);
        if (g_vrOn && wxr::GetState() == wxr::Running) {
            Cap(v.periodSec);
            // Frame catch-up (Ori VR): On keeps the game's own step limit (33 ms: a late frame makes up up to one more
            // frame). Off limits each step to one headset frame, so the game slows a little instead.
            static float gameMax = 0; static int shown = -1;
            float cur = SF(mTimeMaxDelta);
            if (gameMax == 0) gameMax = cur;
            float want = (!g_set.catchUp && g_capPeriod > 0) ? (float)g_capPeriod : gameMax;
            if (fabsf(cur - want) > 0.0001f && mSetMaxDelta) P<void (*)(float, MethodInfo*)>(mSetMaxDelta)(want, mSetMaxDelta);
            if (shown != g_set.catchUp) { shown = g_set.catchUp; Log("CATCHUP %s: step limit %.1f ms", g_set.catchUp ? "on" : "off", want * 1000); }
        }
        static float savedSpan = 0;
        float span = wxr::EyeTanSpan();
        if (span > 0.5f && fabsf(span - savedSpan) > 0.01f) { savedSpan = span; g_set.tanSpan = span; IniW(L"VR", L"EyeSpanLastRun", span);
            if (g_eyeW) Log("RENDERSIZE check: eye image %dx%d, the stage needs %.0f px wide at the headset's density x resolution %.2f (headset span %.3f)",
                g_eyeW, g_eyeH, wxr::RecommendedWidth() * (g_set.stageWidth / g_set.stageDistance) / span * g_set.resolution, g_set.resolution, span); }
    }
    Il2CppObject* cam = (cams && cams->max_length) ? (Il2CppObject*)cams->data[0] : nullptr;
    Il2CppObject* target = cam ? GetObj(mCamGetTarget, cam) : nullptr;
    {
        uint8_t* isr = (uint8_t*)self + 0x5C;
        if (isr[0] != g_isrSeen[0] || isr[1] != g_isrSeen[1]) {
            Log("ISR the game set interleaved rendering: for slow GPU %d, for slow CPU %d%s", isr[0], isr[1], g_isrOff && g_vrOn && (isr[0] || isr[1]) ? " (held off: VR)" : "");
        }
        if (g_isrOff && g_vrOn) { isr[0] = 0; isr[1] = 0; }
        g_isrSeen[0] = isr[0]; g_isrSeen[1] = isr[1];
    }
    if (g_wantInfo && cam) {
        g_wantInfo = false;
        Mat4 v = CamGetMat(mCamGetW2C, cam), p = CamGetMat(mCamGetProj, cam);
        Log("INFO frame %d camera %dx%d, plane distance %.2f, proj m00 %.4f m11 %.4f, view translation %.2f %.2f %.2f", frame, II(mCamPixW, cam), II(mCamPixH, cam),
            fabsf(M(v, 2, 3)), M(p, 0, 0), M(p, 1, 1), M(v, 0, 3), M(v, 1, 3), M(v, 2, 3));
        LogGameSet("asked");
    }
    if (cam && !target) WatchShake(cam);
    bool vrFrame = g_vrOn && cam && !target && wxr::GetState() == wxr::Running && wxr::EyeWidth() > 0 && Now() >= g_eyeRetryAt;
    if (vrFrame) {
        static uint32_t failures = 0;
        bool failed = false;
        try { RenderEyes(self, ctx, cams, cam, frame); }
        catch (Il2CppExceptionWrapper& w) {
            failed = true; g_inEye = false;
            if (failures < 5) {
                static char msg[1024], st[4096];
                msg[0] = st[0] = 0;
                if (w.ex) { il2cpp_format_exception(w.ex, msg, sizeof msg); il2cpp_format_stack_trace(w.ex, st, sizeof st); }
                Log("EYES game exception while %s: %s | %s", g_eyeStep, msg, st);
            }
        }
        catch (...) { failed = true; g_inEye = false; if (failures < 5) Log("EYES non-game exception while %s", g_eyeStep); }
        if (failed) {
            // A game exception inside an eye render: restore the camera, rebuild the eye views, draw this frame flat.
            if (failures++ < 5 || (failures % 500) == 0) Log("EYES an eye render failed while %s (game exception %u): eye views rebuilt, this frame drawn flat", g_eyeStep, failures);
            SetTarget(cam, nullptr); Call0(mCamResetW2C, cam); Call0(mCamResetProj, cam);
            Il2CppArray* views = *(Il2CppArray**)((uint8_t*)self + 0x20);
            if (g_savedGameView && views->data[0] != g_savedGameView) { views->data[0] = g_savedGameView; Log("EYES the game's own view put back"); }
            DropEyeViews("an eye render failed");
            g_eyeRetryAt = Now() + 2.0;  // never rebuild every frame: each new view allocates all its render targets
            oRender(self, ctx, cams, m);
            return;
        }
        if (!g_flatToo) { g_flatSkipped++; return; }  // the flat window is not drawn while VR runs (saves a full render)
    }
    oRender(self, ctx, cams, m);
}

// The game pauses when its window loses focus. In VR the headset is the screen: keep it running.
typedef void (*Focus_t)(Il2CppObject*, bool, MethodInfo*);
static Focus_t oFocus;
static void hkFocus(Il2CppObject* self, bool focus, MethodInfo* m) {
    if (!focus && g_vrOn) { Log("FOCUS the game window lost focus: ignored while VR is on"); focus = true; }
    oFocus(self, focus, m);
}

// ---------------------------------------------------------------- screen shake, every kind
// CameraShakeLogic.UpdateOffset (TypeDefIndex 13503) adds up every CameraShake and CameraShakeAnimator (bosses, thunder,
// impacts, scripted events) and scales them by the static CameraShakeLogic.GlobalShakeMultiplier. Holding that at 0
// while VR runs stops all of them; the setting puts back the game's own value.
static void* g_fGlobalShake;
static float g_gameShake = -1;
static void ApplyScreenShake() {
    if (!g_fGlobalShake) return;
    float cur = 0; il2cpp_field_static_get_value(g_fGlobalShake, &cur);
    if (g_gameShake < 0) { g_gameShake = cur; Log("SHAKE the game's global shake multiplier is %.2f; screen shake %s in VR", cur, g_set.screenShake ? "on" : "off"); }
    else if (cur != 0 && fabsf(cur - g_gameShake) > 0.001f && !(g_set.screenShake && g_vrOn)) { g_gameShake = cur; Log("SHAKE the game set its global shake multiplier to %.2f", cur); }
    float want = (g_vrOn && !g_set.screenShake) ? 0.0f : g_gameShake;
    if (cur != want) il2cpp_field_static_set_value(g_fGlobalShake, &want);
}

// ---------------------------------------------------------------- lightning camera shake
// LightningController.InitiateLightningEvent plays a lightning, a camera shake and a brightness timeline together
// (dump: TypeDefIndex 12379). The camera shake moves the game camera, which in the diorama shakes the whole world.
// Its strength comes from CameraShakeStrenghMultiplier (FloatRange at 0x50); set to 0 while VR runs, the shake
// timeline still plays but moves nothing. The flash and the lightning stay.
typedef void (*Lightning_t)(Il2CppObject*, void* ev, MethodInfo*);
static Lightning_t oLightning;
static uint32_t g_lightnings = 0;
static double g_shakeWatchUntil = 0, g_shakeMax = 0; static float g_lastCamX = 0, g_lastCamY = 0; static bool g_haveLastCam = false;
static double g_calmMax = 0;
static void hkLightning(Il2CppObject* self, void* ev, MethodInfo* m) {
    float* range = (float*)((uint8_t*)self + 0x50);
    g_lightnings++;
    // Build 4: the global shake multiplier (ApplyScreenShake) covers lightning too, so the strike's own range is left alone.
    if (g_lightnings <= 3 || (g_lightnings % 20) == 0)
        Log("THUNDER lightning %u: shake strength %.2f to %.2f, screen shake %s", g_lightnings, range[0], range[1], (!g_set.screenShake && g_vrOn) ? "off (global multiplier 0)" : "on");
    g_shakeWatchUntil = Now() + 1.5; g_shakeMax = 0;
    oLightning(self, ev, m);
}
// Lab measure: the largest frame-to-frame camera move in the 1.5 s after a lightning, against the largest in calm play.
static void WatchShake(Il2CppObject* cam) {
    Mat4 v = CamGetMat(mCamGetW2C, cam);
    float x = M(v, 0, 3), y = M(v, 1, 3);
    if (g_haveLastCam) {
        double d = sqrt((x - g_lastCamX) * (x - g_lastCamX) + (y - g_lastCamY) * (y - g_lastCamY));
        if (d < 5) {  // ignore cuts
            if (Now() < g_shakeWatchUntil) { if (d > g_shakeMax) g_shakeMax = d; }
            else if (g_shakeMax > 0) { Log("SHAKE after lightning %u: largest camera step %.3f units in 1.5 s (calm play so far: %.3f)", g_lightnings, g_shakeMax, g_calmMax); g_shakeMax = 0; }
            else if (d > g_calmMax) g_calmMax = d;
        }
    }
    g_lastCamX = x; g_lastCamY = y; g_haveLastCam = true;
}

// ---------------------------------------------------------------- offline: the game's web requests
// The game calls PlayFab and telemetry over UnityWebRequest at start. Every http and https request is sent to a closed
// local port instead, so it fails at once and the game carries on offline (RULES "Offline, DRM and anti-cheat").
// file:// and other local requests are left alone.
typedef void* (*BeginWebRequest_t)(Il2CppObject*);
typedef Il2CppString* (*GetUrl_t)(Il2CppObject*);
typedef void (*SetUrl_t)(Il2CppObject*, Il2CppObject*);
static BeginWebRequest_t oBeginWebRequest; static GetUrl_t g_getUrl; static SetUrl_t g_setUrl;
static void* hkBeginWebRequest(Il2CppObject* req) {
    Il2CppString* u = g_getUrl ? g_getUrl(req) : nullptr;
    if (u && u->length > 4) {
        std::string url; for (int i = 0; i < u->length && i < 200; i++) url += (char)u->chars[i];
        std::string low = url; for (auto& ch : low) ch = (char)tolower(ch);
        if (low.rfind("http://", 0) == 0 || low.rfind("https://", 0) == 0) {
            if (low.find("127.0.0.1:9/") == std::string::npos) {
                g_webBlocked++;
                if (g_webBlocked <= 5 || (g_webBlocked % 50) == 0) {
                    size_t q = url.find('?'); Log("OFFLINE blocked web request %u: %s", g_webBlocked, url.substr(0, q).c_str());
                }
                g_setUrl(req, il2cpp_string_new("http://127.0.0.1:9/blocked-by-ori-vr-mod"));
            }
        }
    }
    return oBeginWebRequest(req);
}
static void HookWebRequests() {
    g_getUrl = (GetUrl_t)il2cpp_resolve_icall("UnityEngine.Networking.UnityWebRequest::GetUrl");
    g_setUrl = (SetUrl_t)il2cpp_resolve_icall("UnityEngine.Networking.UnityWebRequest::SetUrl");
    void* f = il2cpp_resolve_icall("UnityEngine.Networking.UnityWebRequest::BeginWebRequest");
    if (!f || !g_getUrl || !g_setUrl) { Log("OFFLINE web request icalls NOT FOUND: online calls not blocked"); return; }
    MH_STATUS a = MH_CreateHook(f, (void*)&hkBeginWebRequest, (void**)&oBeginWebRequest), b = MH_EnableHook(f);
    Log("HOOK icall UnityWebRequest::BeginWebRequest (offline) %s", (a == MH_OK && b == MH_OK) ? "in place" : "FAILED");
}

// ---------------------------------------------------------------- clean exit
// Both test logs ended in heap corruption (c0000374) inside UnityPlayer as the game closed (2026-11-20). On exit
// the mod now ends its OpenXR session and switches its hooks off before the process goes.
typedef void(WINAPI* ExitProcess_t)(UINT);
static ExitProcess_t oExitProcess;
static volatile LONG g_exiting = 0;
static void ShutdownForExit(const char* how) {
    if (InterlockedExchange(&g_exiting, 1)) return;
    Log("EXIT %s: shutting VR down", how);
    g_vrOn = false;
    wxr::Shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    Log("EXIT hooks off");
}
static void WINAPI hkExitProcess(UINT code) { ShutdownForExit("ExitProcess"); oExitProcess(code); }
typedef BOOL(WINAPI* TerminateProcess_t)(HANDLE, UINT);
static TerminateProcess_t oTerminateProcess;
static BOOL WINAPI hkTerminateProcess(HANDLE h, UINT code) {
    if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId()) ShutdownForExit("TerminateProcess");
    return oTerminateProcess(h, code);
}
typedef void (*Quit_t)(MethodInfo*);
static Quit_t oMoonQuit;
static void hkMoonQuit(MethodInfo* m) { oMoonQuit(m); ShutdownForExit("the game is quitting"); }
static void HookExit() {
    void* ep = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess");
    MH_STATUS a = MH_CreateHook(ep, (void*)&hkExitProcess, (void**)&oExitProcess), b = MH_EnableHook(ep);
    Log("HOOK ExitProcess (clean VR shutdown) %s", (a == MH_OK && b == MH_OK) ? "in place" : "FAILED");
    void* tp = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TerminateProcess");
    MH_STATUS c = MH_CreateHook(tp, (void*)&hkTerminateProcess, (void**)&oTerminateProcess), d = MH_EnableHook(tp);
    Log("HOOK TerminateProcess (clean VR shutdown) %s", (c == MH_OK && d == MH_OK) ? "in place" : "FAILED");
    Hook(Need(FindClass("", "MoonDebug"), "MoonDebug", "OnApplicationQuit", 0), (void*)&hkMoonQuit, (void**)&oMoonQuit, "MoonDebug.OnApplicationQuit");
}

// ---------------------------------------------------------------- setup
typedef void* (*RuntimeInvoke_t)(MethodInfo*, void*, void**, void**);
static RuntimeInvoke_t oRuntimeInvoke;
static bool g_setupDone = false;
static double g_setupT0;
static void Setup();
static void* hkRuntimeInvoke(MethodInfo* m, void* obj, void** params, void** exc) {
    if (!g_setupDone) { g_setupDone = true; if (FindClass("Moon.Rendering", "MoonRenderPipeline")) Setup(); else g_setupDone = false; }
    return oRuntimeInvoke(m, obj, params, exc);
}
static void Hook(MethodInfo* m, void* detour, void** orig, const char* name) {
    if (!m) { Log("HOOK %s NOT FOUND", name); return; }
    MH_STATUS a = MH_CreateHook(m->methodPointer, detour, orig), b = MH_EnableHook(m->methodPointer);
    Log("HOOK %s %s", name, (a == MH_OK && b == MH_OK) ? "in place" : "FAILED");
}

static void Setup() {
    Log("IL2CPP ready, setup on the main thread after %.2f s", Now() - g_setupT0);
    void* kCam = FindClass("UnityEngine", "Camera");
    mCamGetW2C = Need(kCam, "Camera", "get_worldToCameraMatrix", 0); mCamSetW2C = Need(kCam, "Camera", "set_worldToCameraMatrix", 1);
    mCamGetProj = Need(kCam, "Camera", "get_projectionMatrix", 0); mCamSetProj = Need(kCam, "Camera", "set_projectionMatrix", 1);
    mCamResetW2C = Need(kCam, "Camera", "ResetWorldToCameraMatrix", 0); mCamResetProj = Need(kCam, "Camera", "ResetProjectionMatrix", 0);
    mCamGetTarget = Need(kCam, "Camera", "get_targetTexture", 0); mCamSetTarget = Need(kCam, "Camera", "set_targetTexture", 1);
    mCamPixW = Need(kCam, "Camera", "get_pixelWidth", 0); mCamPixH = Need(kCam, "Camera", "get_pixelHeight", 0);
    mObjName = Need(FindClass("UnityEngine", "Object"), "Object", "get_name", 0);
    mSetHideFlags = Need(FindClass("UnityEngine", "Object"), "Object", "set_hideFlags", 1);
    mObjAlive = Need(FindClass("UnityEngine", "Object"), "Object", "op_Implicit", 1);
    void* kRT = FindClass("UnityEngine", "RenderTexture");
    mRTCtor = Need(kRT, "RenderTexture", ".ctor", 4, "System.Int32,System.Int32,System.Int32,UnityEngine.RenderTextureFormat");
    mRTCreate = Need(kRT, "RenderTexture", "Create", 0);
    mTexNative = Need(FindClass("UnityEngine", "Texture"), "Texture", "GetNativeTexturePtr", 0);
    void* kTime = FindClass("UnityEngine", "Time");
    mTimeTime = Need(kTime, "Time", "get_time", 0); mTimeUnscaled = Need(kTime, "Time", "get_unscaledTime", 0);
    mTimeScale = Need(kTime, "Time", "get_timeScale", 0);
    mSetTimeScale = Need(kTime, "Time", "set_timeScale", 1); mTimeFixedDelta = Need(kTime, "Time", "get_fixedDeltaTime", 0);
    mTimeMaxDelta = Need(kTime, "Time", "get_maximumDeltaTime", 0); mTimeFrame = Need(kTime, "Time", "get_frameCount", 0);
    mTimeFixedTime = Need(kTime, "Time", "get_fixedTime", 0);
    mSetMaxDelta = Need(kTime, "Time", "set_maximumDeltaTime", 1);
    void* kQS = FindClass("UnityEngine", "QualitySettings");
    mVSync = Need(kQS, "QualitySettings", "get_vSyncCount", 0); mSetVSync = Need(kQS, "QualitySettings", "set_vSyncCount", 1);
    void* kApp = FindClass("UnityEngine", "Application");
    mTargetFps = Need(kApp, "Application", "get_targetFrameRate", 0); mSetTargetFps = Need(kApp, "Application", "set_targetFrameRate", 1);
    void* kScreen = FindClass("UnityEngine", "Screen");
    mScreenW = Need(kScreen, "Screen", "get_width", 0); mScreenH = Need(kScreen, "Screen", "get_height", 0);
    void* kGs = FindClass("", "GameSettings");
    fGsInstance = kGs ? il2cpp_class_get_field_from_name(kGs, "Instance") : nullptr;
    mGsVsync = Need(kGs, "GameSettings", "get_Vsync", 0); mGsSetVsync = Need(kGs, "GameSettings", "set_Vsync", 1);
    mGsTargetFps = Need(kGs, "GameSettings", "get_TargetFramerate", 0); mGsSetTargetFps = Need(kGs, "GameSettings", "set_TargetFramerate", 1);
    mGsHdr = Need(kGs, "GameSettings", "get_HDRToggled", 0); mGsSetHdr = Need(kGs, "GameSettings", "set_HDRToggled", 1);
    mGsDyn = Need(kGs, "GameSettings", "get_DynamicGraphicsToggled", 0); mGsSetDyn = Need(kGs, "GameSettings", "set_DynamicGraphicsToggled", 1);
    mGsBlur = Need(kGs, "GameSettings", "get_MotionBlurEnabled", 0); mGsSetBlur = Need(kGs, "GameSettings", "set_MotionBlurEnabled", 1);
    mGsQuality = Need(kGs, "GameSettings", "get_PCQualityMode", 0); mGsSetQuality = Need(kGs, "GameSettings", "set_PCQualityMode", 1);
    mGsDisplayScale = Need(kGs, "GameSettings", "get_DisplayScale", 0); mGsSetDisplayScale = Need(kGs, "GameSettings", "set_DisplayScale", 1);
    mGsHud = Need(kGs, "GameSettings", "get_HudEnabled", 0);
    mGsSave = Need(kGs, "GameSettings", "SaveSettings", 0);
    void* kSync = FindClass("", "SyncFramesTest");
    fSyncForce = kSync ? il2cpp_class_get_field_from_name(kSync, "EnabledForceFixedUpdate") : nullptr;
    fSyncEnable = kSync ? il2cpp_class_get_field_from_name(kSync, "EnableSync") : nullptr;
    void* kDtm = FindClass("usedStandaloneScripts.DeltaTimeManagers", "DeltaTimeManager");
    fDtmInstance = kDtm ? il2cpp_class_get_field_from_name(kDtm, "Instance") : nullptr;
    mDtmController = Need(kDtm, "DeltaTimeManager", "get_Controller", 0);
    g_issue = (IssuePluginEvent_t)il2cpp_resolve_icall("UnityEngine.GL::GLIssuePluginEvent");
    Log("HOOKMAP icall GL::GLIssuePluginEvent %s", g_issue ? "found" : "NOT FOUND");
    void* kPipe = FindClass("Moon.Rendering", "MoonRenderPipeline");
    mRender = Need(kPipe, "MoonRenderPipeline", "Render", 2);
    mViewCtor = Need(FindClass("Moon.Rendering", "MoonRenderPipelineView"), "MoonRenderPipelineView", ".ctor", 3);
    mViewDispose = Need(FindClass("Moon.Rendering", "MoonRenderPipelineView"), "MoonRenderPipelineView", "Dispose", 0);
    mFocus = Need(FindClass("", "GameController"), "GameController", "OnApplicationFocus", 1);
    if (g_missing) Log("HOOKMAP %d item(s) not found: VR stays off", g_missing);
    if (g_missing || !g_issue || !mRender) { g_vrOn = false; return; }
    Hook(mRender, (void*)&hkRender, (void**)&oRender, "MoonRenderPipeline.Render");
    Hook(Need(FindClass("", "LightningController"), "LightningController", "InitiateLightningEvent", 1), (void*)&hkLightning, (void**)&oLightning, "LightningController.InitiateLightningEvent");
    { void* k = FindClass("", "CameraShakeLogic"); g_fGlobalShake = k ? il2cpp_class_get_field_from_name(k, "GlobalShakeMultiplier") : nullptr;
      Log("HOOKMAP CameraShakeLogic.GlobalShakeMultiplier %s", g_fGlobalShake ? "found" : "NOT FOUND"); }
    HookWebRequests();
    HookExit();
    if (g_set.labCommands) Log("VEH first-chance exception logger %s (lab)", AddVectoredExceptionHandler(1, EyeVeh) ? "in place" : "FAILED");
    Hook(mFocus, (void*)&hkFocus, (void**)&oFocus, "GameController.OnApplicationFocus");
    void* kX = FindClass("J2i.Net.XInputWrapper", "XInput");
    Hook(Need(kX, "XInput", "XInputGetState", 2), (void*)&hkXInputGetState, (void**)&oXInputGetState, "XInput.XInputGetState (pad for the menu)");
    HookInputIcalls();
    void* kIn = FindClass("UnityEngine", "Input");
    Hook(Need(kIn, "Input", "GetKeyInt", 1), (void*)&hkGetKeyInt, (void**)&oGetKeyInt, "Input.GetKeyInt");
    Hook(Need(kIn, "Input", "GetKeyDownInt", 1), (void*)&hkGetKeyDownInt, (void**)&oGetKeyDownInt, "Input.GetKeyDownInt");
    Hook(Need(kIn, "Input", "GetKeyUpInt", 1), (void*)&hkGetKeyUpInt, (void**)&oGetKeyUpInt, "Input.GetKeyUpInt");
    Hook(Need(kIn, "Input", "get_anyKey", 0), (void*)&hkAnyKey, (void**)&oAnyKey, "Input.anyKey");
    Hook(Need(kIn, "Input", "get_anyKeyDown", 0), (void*)&hkAnyKeyDown, (void**)&oAnyKeyDown, "Input.anyKeyDown");
}

static DWORD WINAPI InitThread(void*) {
    g_setupT0 = Now();
    while (!(g_ga = GetModuleHandleW(L"GameAssembly.dll"))) Sleep(1);
    if (!BindIl2cpp()) return 0;
    void* ri = GetProcAddress(g_ga, "il2cpp_runtime_invoke");
    MH_Initialize();
    MH_STATUS a = MH_CreateHook(ri, (void*)&hkRuntimeInvoke, (void**)&oRuntimeInvoke), b = MH_EnableHook(ri);
    Log("HOOK il2cpp_runtime_invoke (setup on the main thread) %s, GameAssembly seen after %.3f s", (a == MH_OK && b == MH_OK) ? "in place" : "FAILED", Now() - g_setupT0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_qp0);
        timeBeginPeriod(1);
        wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
        g_gameDir = p; g_gameDir = g_gameDir.substr(0, g_gameDir.find_last_of(L"\\/") + 1);
        g_log = _wfopen((g_gameDir + L"OriWotWVR.log").c_str(), L"w");
        LoadSettings();
        Log("OriWotWVR %s (build %d), built " __DATE__ " " __TIME__, MOD_VERSION, MOD_BUILD);
        Log("SETTINGS screen shake %s, lab commands %s", g_set.screenShake ? "on" : "off (default in VR)", g_set.labCommands ? "ON" : "off");
    Log("SETTINGS stage %.2f m at %.2f m, depth %.2f, resolution %.2f, picture %d, eye span last run %.3f, VR game settings applied before %d",
            g_set.stageWidth, g_set.stageDistance, g_set.depth, g_set.resolution, g_set.picture, g_set.tanSpan, g_set.gameSettingsDone);
        wxr::SetLogger(&Log);
        CloseHandle(CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
