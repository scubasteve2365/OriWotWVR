// OpenXR side of Ori and the Will of the Wisps VR. See WotwXR.h for the threading.
// Ported from Ori VR's OriVRNative.cpp (same author, MIT): runtime and add-on logging, the -32 retry without add-ons,
// the stage drawing in each eye's view (Steady, projection layer) and the per-eye quads (Panel), Picture Auto.
// New here: own XR thread and D3D11 device, ring of shared textures, nothing waits on the game or the headset.
#include "WotwXR.h"
#include "MenuDraw.h"
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace wxr {

static void NoLog(const char*, ...) {}
static void (*Log)(const char*, ...) = NoLog;
void SetLogger(void (*log)(const char*, ...)) { Log = log ? log : NoLog; }

// ------------------------------------------------------------------------------------------------ settings and state
static std::atomic<int> g_state{Off};
static std::atomic<bool> g_slotsReady{false};
static std::atomic<int> g_lastError{0};
static std::atomic<int> g_layerInUse{-1};
static int g_eyeW = 0, g_eyeH = 0, g_picture = 0;
static float g_resolution = 1, g_tanGuess = 2.2f, g_aspect = 16.f / 9.f;
static std::mutex g_stageMx;
static Stage g_stage;
static char g_runtime[XR_MAX_RUNTIME_NAME_SIZE] = "", g_headset[XR_MAX_SYSTEM_NAME_SIZE] = "";
static std::atomic<int> g_recW{0}, g_recH{0};
static std::atomic<float> g_tanSpan{0};
static std::thread g_thread;
static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_threadDone{false};
static std::atomic<bool> g_wantRecenter{true};

void Configure(Stage stage, int picture, float resolution, float tanSpanGuess, float aspect) {
    g_picture = picture; g_resolution = resolution; g_tanGuess = tanSpanGuess > 0.5f ? tanSpanGuess : 2.2f; g_aspect = aspect;
    std::lock_guard<std::mutex> lk(g_stageMx); g_stage = stage;
}
int EyeWidth() { return g_slotsReady ? g_eyeW : 0; }
int EyeHeight() { return g_slotsReady ? g_eyeH : 0; }
void SetStage(Stage stage) { std::lock_guard<std::mutex> lk(g_stageMx); g_stage = stage; g_wantRecenter = true; }
int GetState() { return g_state; }
const char* RuntimeName() { return g_runtime; }
const char* HeadsetName() { return g_headset; }
int RecommendedWidth() { return g_recW; }
int RecommendedHeight() { return g_recH; }
float EyeTanSpan() { return g_tanSpan; }
int LayerInUse() { return g_layerInUse; }
int LastError() { return g_lastError; }
void Recenter() { g_wantRecenter = true; }
static std::atomic<int> g_surround{0};        // 0 Black, 1 Green (chroma key), 2 Transparent (runtime see-through)
static std::atomic<float> g_sharpen{0.3f};
static bool g_canAlphaBlend = false, g_canAdditive = false, g_steadyPossible = false, g_isVD = false;
void SetPicture(int picture) { g_picture = picture; }
void SetSurround(int surround) { g_surround = surround; }
void SetSharpen(float amount) { g_sharpen = amount; }
bool CanSeeThrough() { return g_canAlphaBlend || g_canAdditive; }
static bool SeeThrough() { return g_surround == 2 && (g_canAlphaBlend || g_canAdditive); }
static int WantedLayer() { bool steady = g_picture == 1 || (g_picture == 0 && g_isVD); return (steady && g_steadyPossible) ? 1 : 0; }

static std::mutex g_viewsMx;
static Views g_views;
bool GetViews(Views* out) { std::lock_guard<std::mutex> lk(g_viewsMx); *out = g_views; return out->valid; }

// ------------------------------------------------------------------------------------------------ stats
static std::mutex g_statMx;
static Stats g_stats{};
static double g_waitSum = 0, g_endSum = 0; static uint32_t g_timed = 0;
void TakeStats(Stats* out) {
    std::lock_guard<std::mutex> lk(g_statMx);
    *out = g_stats;
    out->waitMsAvg = g_timed ? g_waitSum / g_timed : 0;
    out->endMsAvg = g_timed ? g_endSum / g_timed : 0;
    g_stats = Stats{}; g_waitSum = g_endSum = 0; g_timed = 0;
}
static LARGE_INTEGER g_qpf;
static double Ms() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / g_qpf.QuadPart; }

// ------------------------------------------------------------------------------------------------ OpenXR functions
static HMODULE g_loader;
static PFN_xrGetInstanceProcAddr g_gipa;
static XrInstance g_inst = XR_NULL_HANDLE;
static XrSystemId g_sys = 0;
static XrSession g_sess = XR_NULL_HANDLE;
static XrSpace g_space = XR_NULL_HANDLE;
static int64_t g_format = 0;
template <class T> static bool GetFn(const char* name, T& fn) {
    fn = nullptr;
    if (XR_FAILED(g_gipa(g_inst, name, (PFN_xrVoidFunction*)&fn)) || !fn) { Log("XR missing function %s", name); return false; }
    return true;
}
#define XRCHECK(x) do { XrResult r_ = (x); if (XR_FAILED(r_)) { Log("XR %s failed: %d", #x, (int)r_); g_lastError = (int)r_; return false; } } while (0)
static PFN_xrPollEvent p_poll; static PFN_xrBeginSession p_beginSession; static PFN_xrEndSession p_endSession;
static PFN_xrWaitFrame p_waitFrame; static PFN_xrBeginFrame p_beginFrame; static PFN_xrEndFrame p_endFrame;
static PFN_xrLocateViews p_locate; static PFN_xrLocateSpace p_locateSpace; static PFN_xrCreateReferenceSpace p_createSpace;
static PFN_xrCreateSwapchain p_createSwap; static PFN_xrEnumerateSwapchainImages p_enumImages; static PFN_xrDestroySwapchain p_destroySwap;
static PFN_xrAcquireSwapchainImage p_acquire; static PFN_xrWaitSwapchainImage p_waitImage; static PFN_xrReleaseSwapchainImage p_release;
static PFN_xrDestroySession p_destroySession; static PFN_xrDestroyInstance p_destroyInstance;

// ------------------------------------------------------------------------------------------------ D3D11 (XR side)
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader* g_vs; static ID3D11PixelShader* g_ps; static ID3D11Buffer* g_cb;
static ID3D11SamplerState* g_linear; static ID3D11RasterizerState* g_noCull; static ID3D11BlendState* g_alphaBlend;
struct Swap { XrSwapchain handle = XR_NULL_HANDLE; std::vector<XrSwapchainImageD3D11KHR> images; std::vector<ID3D11RenderTargetView*> rtv; int w = 0, h = 0; };
static Swap g_proj[2], g_quad[2], g_menuSwap;

// Ring of shared eye pairs. Key 0: free for the game side to write. Key 1: written, owned by the XR side.
static const int kSlots = 4;
struct Slot {
    ID3D11Texture2D* xrTex[2] = {}; IDXGIKeyedMutex* xrMx[2] = {}; ID3D11ShaderResourceView* srv[2] = {};
    HANDLE share[2] = {};
    ID3D11ShaderResourceView* rawSrv[2] = {};  // the stored bytes as they are (gamma), for sharpening
    ID3D11Texture2D* gameTex[2] = {}; IDXGIKeyedMutex* gameMx[2] = {};
    std::atomic<uint64_t> seq{0};   // frame order written by the game side, 0 = never written
    Views views;                    // the views this pair was rendered with
    std::atomic<bool> xrHolds{false};
};
static Slot g_slots[kSlots];
static std::atomic<uint64_t> g_seq{0};

static bool CreateDeviceForRuntime(const LUID& want) {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return false;
    IDXGIAdapter1* chosen = nullptr; IDXGIAdapter1* a = nullptr;
    bool zero = want.HighPart == 0 && want.LowPart == 0;  // RigB's null driver reports zero
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d);
        bool match = !zero && d.AdapterLuid.HighPart == want.HighPart && d.AdapterLuid.LowPart == want.LowPart;
        if (!chosen && (match || (zero && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)))) { chosen = a; Log("XR device on %ls (%s)", d.Description, match ? "runtime's adapter" : "first adapter, runtime gave no adapter"); continue; }
        a->Release();
    }
    f->Release();
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(chosen, chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx);
    if (chosen) chosen->Release();
    if (FAILED(hr)) { Log("XR device creation failed %08lx", (unsigned long)hr); return false; }
    return true;
}

static const char* kStageShader = R"(
Texture2D src : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Stage : register(b0) { float4 corner[4]; };
void vs(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2(id & 1, id >> 1);
    pos = corner[id];
}
float4 ps(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return src.Sample(linearClamp, uv);
}
)";

static bool CreateStagePass() {
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    if (!compile) { Log("XR no shader compiler (d3dcompiler_47.dll): Steady picture off, quads used"); return false; }
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    size_t n = strlen(kStageShader);
    if (FAILED(compile(kStageShader, n, "stage", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(compile(kStageShader, n, "stage", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err))) {
        Log("XR stage shader error %s", err ? (const char*)err->GetBufferPointer() : "?"); return false;
    }
    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    vsb->Release(); psb->Release();
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 64; cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->CreateBuffer(&cb, nullptr, &g_cb);
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->CreateSamplerState(&sd, &g_linear);
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    g_dev->CreateRasterizerState(&rd, &g_noCull);
    D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = 15;
    g_dev->CreateBlendState(&bd, &g_alphaBlend);
    return g_vs && g_ps && g_cb && g_linear && g_noCull;
}

// Contrast adaptive sharpening, after the idea of AMD FidelityFX CAS (MIT licence), as in Ori VR: each pixel is pushed
// away from its neighbours, less where local contrast is already high, so edges do not ring.
static const char* kSharpenShader = R"(
Texture2D src : register(t0);
SamplerState pointClamp : register(s0);
cbuffer Params : register(b0) { float amount; float texelX; float texelY; float pad; };
void vs(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
}
float4 ps(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c = src.Sample(pointClamp, uv).rgb;
    float3 n = src.Sample(pointClamp, uv + float2(0, -texelY)).rgb;
    float3 s = src.Sample(pointClamp, uv + float2(0, texelY)).rgb;
    float3 e = src.Sample(pointClamp, uv + float2(texelX, 0)).rgb;
    float3 w = src.Sample(pointClamp, uv + float2(-texelX, 0)).rgb;
    float3 lo = min(c, min(n, min(s, min(e, w))));
    float3 hi = max(c, max(n, max(s, max(e, w))));
    float3 room = saturate(min(lo, 1 - hi) / max(hi, 0.0001));
    float3 weight = sqrt(room) * (-1.0 / lerp(8.0, 5.0, amount));
    float3 result = (c + (n + s + e + w) * weight) / (1 + 4 * weight);
    return float4(saturate(result), 1);
}
)";
static ID3D11VertexShader* g_shVs; static ID3D11PixelShader* g_shPs; static ID3D11Buffer* g_shCb; static ID3D11SamplerState* g_point;
static ID3D11Texture2D* g_passTex[2]; static ID3D11RenderTargetView* g_passRtv[2]; static ID3D11ShaderResourceView* g_passSrv[2];
static bool CreateSharpenPass() {
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    if (!compile) return false;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    size_t n = strlen(kSharpenShader);
    if (FAILED(compile(kSharpenShader, n, "sharpen", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(compile(kSharpenShader, n, "sharpen", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err))) { Log("XR sharpen off: shader error"); return false; }
    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_shVs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_shPs);
    vsb->Release(); psb->Release();
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->CreateBuffer(&cb, nullptr, &g_shCb);
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->CreateSamplerState(&sd, &g_point);
    bool srgb = g_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    for (int e = 0; e < 2; e++) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = g_eyeW; d.Height = g_eyeH; d.MipLevels = d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_passTex[e]))) return false;
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        g_dev->CreateRenderTargetView(g_passTex[e], &rv, &g_passRtv[e]);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        g_dev->CreateShaderResourceView(g_passTex[e], &sv, &g_passSrv[e]);
    }
    Log("XR sharpen pass ready");
    return g_shVs && g_shPs && g_passRtv[0] && g_passRtv[1];
}

static bool MakeSwap(Swap& s, int w, int h, uint64_t usage) {
    XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    c.usageFlags = usage; c.format = g_format; c.sampleCount = 1; c.width = w; c.height = h;
    c.faceCount = c.arraySize = c.mipCount = 1;
    XRCHECK(p_createSwap(g_sess, &c, &s.handle));
    uint32_t ic = 0;
    XRCHECK(p_enumImages(s.handle, 0, &ic, nullptr));
    s.images.assign(ic, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    XRCHECK(p_enumImages(s.handle, ic, &ic, (XrSwapchainImageBaseHeader*)s.images.data()));
    s.rtv.assign(ic, nullptr); s.w = w; s.h = h;
    return true;
}

static bool CreateSlots() {
    bool srgb = g_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    for (auto& s : g_slots) for (int e = 0; e < 2; e++) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = g_eyeW; d.Height = g_eyeH; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &s.xrTex[e]))) { Log("XR slot texture failed"); return false; }
        s.xrTex[e]->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&s.xrMx[e]);
        IDXGIResource* r = nullptr; s.xrTex[e]->QueryInterface(__uuidof(IDXGIResource), (void**)&r);
        if (r) { r->GetSharedHandle(&s.share[e]); r->Release(); }
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        g_dev->CreateShaderResourceView(s.xrTex[e], &sv, &s.srv[e]);
        D3D11_SHADER_RESOURCE_VIEW_DESC raw = sv; raw.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        g_dev->CreateShaderResourceView(s.xrTex[e], &raw, &s.rawSrv[e]);
        if (!s.xrMx[e] || !s.share[e] || !s.srv[e]) { Log("XR slot setup failed"); return false; }
    }
    g_slotsReady = true;
    Log("XR ring of %d shared eye pairs %dx%d ready", kSlots, g_eyeW, g_eyeH);
    return true;
}

// ------------------------------------------------------------------------------------------------ add-ons and runtime
static std::string ReadText(const char* path) {
    std::string t; FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") == 0 && f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) t.append(b, n); fclose(f); }
    return t;
}
static std::string JsonString(const std::string& text, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t at = text.find(k); if (at == std::string::npos) return "";
    at = text.find(':', at + k.size()); if (at == std::string::npos) return "";
    at = text.find('"', at); if (at == std::string::npos) return "";
    std::string out;
    for (size_t i = at + 1; i < text.size(); i++) { char c = text[i]; if (c == '\\' && i + 1 < text.size()) { out += text[++i]; continue; } if (c == '"') return out; out += c; }
    return "";
}
static std::vector<std::string> g_offSwitches; static std::string g_offNames, g_stuckNames;
static void ListLayers(HKEY root, const char* rootName) {
    HKEY key;
    if (RegOpenKeyExA(root, "SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; i++) {
        char manifest[1024]; DWORD nl = sizeof manifest, type = 0, data = 0, dl = sizeof data;
        LONG r = RegEnumValueA(key, i, manifest, &nl, nullptr, &type, (BYTE*)&data, &dl);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        if (type == REG_DWORD && data != 0) { Log("XR add-on %s (%s): switched off in Windows", manifest, rootName); continue; }
        std::string text = ReadText(manifest);
        std::string name = JsonString(text, "name"), lib = JsonString(text, "library_path"), off = JsonString(text, "disable_environment");
        Log("XR add-on %s (%s): %s, off switch %s", name.c_str(), rootName, lib.c_str(), off.empty() ? "none" : off.c_str());
        if (!off.empty()) { g_offSwitches.push_back(off); g_offNames += (g_offNames.empty() ? "" : ", ") + name; }
        else g_stuckNames += (g_stuckNames.empty() ? "" : ", ") + name;
    }
    RegCloseKey(key);
}

static bool StartXr() {
    g_offSwitches.clear(); g_offNames.clear(); g_stuckNames.clear();
    ListLayers(HKEY_LOCAL_MACHINE, "HKLM"); ListLayers(HKEY_CURRENT_USER, "HKCU");
    char rt[1024]; DWORD rs = sizeof rt;
    if (GetEnvironmentVariableA("XR_RUNTIME_JSON", rt, sizeof rt) > 0) Log("XR runtime manifest (XR_RUNTIME_JSON) %s", rt);
    else if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, rt, &rs) == ERROR_SUCCESS) Log("XR runtime manifest %s", rt);
    else Log("XR no active OpenXR runtime set in Windows");

    char path[MAX_PATH]; HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&StartXr, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    *(strrchr(path, '\\') + 1) = 0; strcat_s(path, "openxr_loader.dll");
    g_loader = LoadLibraryA(path);
    if (!g_loader) { Log("XR could not load %s, error %lu", path, GetLastError()); return false; }
    g_gipa = (PFN_xrGetInstanceProcAddr)GetProcAddress(g_loader, "xrGetInstanceProcAddr");
    if (!g_gipa) return false;

    PFN_xrCreateInstance createInstance; if (!GetFn("xrCreateInstance", createInstance)) return false;
    const char* exts[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ici.applicationInfo.applicationName, "Ori & the Will of the Wisps VR");
    ici.applicationInfo.applicationVersion = 1;
    strcpy_s(ici.applicationInfo.engineName, "Unity 2018.4");
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ici.enabledExtensionCount = 1; ici.enabledExtensionNames = exts;
    XrResult created = createInstance(&ici, &g_inst);
    if (created == XR_ERROR_RUNTIME_UNAVAILABLE) {
        // -51: no OpenXR runtime set, or a broken one. Use SteamVR's for this game only, if it is installed (Ori VR 1.2).
        // Nothing in Windows changes; a runtime that starts normally is never overridden.
        char base[MAX_PATH]; DWORD sz = sizeof base; std::string steam;
        if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, base, &sz) == ERROR_SUCCESS) {
            steam = std::string(base) + "\\steamapps\\common\\SteamVR\\steamxr_win64.json";
            for (auto& c : steam) if (c == '/') c = '\\';
        }
        if (!steam.empty() && GetFileAttributesA(steam.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Log("XR runtime unavailable (-51): using SteamVR for this game only: %s", steam.c_str());
            SetEnvironmentVariableA("XR_RUNTIME_JSON", steam.c_str());
            created = createInstance(&ici, &g_inst);
        } else Log("XR runtime unavailable (-51) and no SteamVR runtime found to use instead");
    }
    if (created == XR_ERROR_FILE_ACCESS_ERROR && !g_offSwitches.empty()) {
        // -32: an add-on (API layer) refused to load. Try once more with every add-on that has an off switch switched
        // off for this game only (Ori VR 1.2.2, TECHNIQUES "OpenXR start failures" item 4).
        Log("XR could not load an add-on (-32). Trying again with these switched off for this game only: %s", g_offNames.c_str());
        for (auto& v : g_offSwitches) SetEnvironmentVariableA(v.c_str(), "1");
        created = createInstance(&ici, &g_inst);
        Log(XR_SUCCEEDED(created) ? "XR started with those add-ons switched off" : "XR still failing with those add-ons off: %d", (int)created);
    }
    XRCHECK(created);
    PFN_xrGetInstanceProperties props;
    if (GetFn("xrGetInstanceProperties", props)) {
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(props(g_inst, &ip))) { strcpy_s(g_runtime, ip.runtimeName); Log("XR runtime %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion)); }
    }
    PFN_xrGetSystem getSystem; if (!GetFn("xrGetSystem", getSystem)) return false;
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO}; sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XRCHECK(getSystem(g_inst, &sgi, &g_sys));
    PFN_xrEnumerateEnvironmentBlendModes enumBlend;
    if (GetFn("xrEnumerateEnvironmentBlendModes", enumBlend)) {
        XrEnvironmentBlendMode modes[8]; uint32_t n = 0; std::string list;
        if (XR_SUCCEEDED(enumBlend(g_inst, g_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &n, modes)))
            for (uint32_t i = 0; i < n; i++) {
                list += modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ? "opaque " : modes[i] == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE ? "additive " : modes[i] == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND ? "alpha_blend " : "other ";
                if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) g_canAlphaBlend = true;
                if (modes[i] == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE) g_canAdditive = true;
            }
        Log("XR see-through modes: %s(Transparent surround %s)", list.c_str(), g_canAlphaBlend ? "uses alpha blend" : g_canAdditive ? "uses additive" : "not offered, black used");
    }
    PFN_xrGetSystemProperties sysProps;
    if (GetFn("xrGetSystemProperties", sysProps)) {
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(sysProps(g_inst, g_sys, &sp))) { strcpy_s(g_headset, sp.systemName); Log("XR headset %s, vendor %u", sp.systemName, sp.vendorId); }
    }
    PFN_xrEnumerateViewConfigurationViews enumViews;
    if (GetFn("xrEnumerateViewConfigurationViews", enumViews)) {
        XrViewConfigurationView v[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        uint32_t vc = 0;
        if (XR_SUCCEEDED(enumViews(g_inst, g_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &vc, v))) {
            g_recW = (int)v[0].recommendedImageRectWidth; g_recH = (int)v[0].recommendedImageRectHeight;
            Log("XR headset recommends %dx%d per eye (largest %ux%u)", (int)g_recW, (int)g_recH, v[0].maxImageRectWidth, v[0].maxImageRectHeight);
        }
    }
    {
        // Render size: the stage covers (width / distance) of a flat plane 1 m away; the headset's eye image covers tanSpan
        // of it at its recommended width. Same pixel density across the stage, times the Resolution setting.
        Stage st; { std::lock_guard<std::mutex> lk(g_stageMx); st = g_stage; }
        float w = g_recW * (st.width / st.distance) / g_tanGuess * g_resolution;
        int wi = ((int)(w + 8) / 16) * 16;
        if (wi < 640) wi = 640; if (wi > 8192) wi = 8192;
        g_eyeW = wi; g_eyeH = (((int)(wi / g_aspect)) + 1) & ~1;
        Log("RENDERSIZE eye image %dx%d = recommended width %d x stage %.2f/%.2f m / eye span %.3f (from last run or default) x resolution %.2f",
            g_eyeW, g_eyeH, (int)g_recW, st.width, st.distance, g_tanGuess, g_resolution);
    }
    PFN_xrGetD3D11GraphicsRequirementsKHR req11; if (!GetFn("xrGetD3D11GraphicsRequirementsKHR", req11)) return false;
    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    XRCHECK(req11(g_inst, g_sys, &req));
    if (!CreateDeviceForRuntime(req.adapterLuid)) return false;
    PFN_xrCreateSession createSession; if (!GetFn("xrCreateSession", createSession)) return false;
    XrGraphicsBindingD3D11KHR bind{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR}; bind.device = g_dev;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.next = &bind; sci.systemId = g_sys;
    XRCHECK(createSession(g_inst, &sci, &g_sess));
    if (!GetFn("xrCreateReferenceSpace", p_createSpace)) return false;
    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; rsci.poseInReferenceSpace.orientation.w = 1;
    XRCHECK(p_createSpace(g_sess, &rsci, &g_space));
    PFN_xrEnumerateSwapchainFormats enumFormats; if (!GetFn("xrEnumerateSwapchainFormats", enumFormats)) return false;
    uint32_t n = 0; XRCHECK(enumFormats(g_sess, 0, &n, nullptr));
    std::vector<int64_t> formats(n); XRCHECK(enumFormats(g_sess, n, &n, formats.data()));
    for (auto f : formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) g_format = f;
    if (!g_format) for (auto f : formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM) g_format = f;
    if (!g_format) { Log("XR the runtime offers no RGBA8 swapchain format"); return false; }
    if (!GetFn("xrCreateSwapchain", p_createSwap) || !GetFn("xrEnumerateSwapchainImages", p_enumImages) || !GetFn("xrDestroySwapchain", p_destroySwap) ||
        !GetFn("xrPollEvent", p_poll) || !GetFn("xrBeginSession", p_beginSession) || !GetFn("xrEndSession", p_endSession) ||
        !GetFn("xrWaitFrame", p_waitFrame) || !GetFn("xrBeginFrame", p_beginFrame) || !GetFn("xrEndFrame", p_endFrame) ||
        !GetFn("xrLocateViews", p_locate) || !GetFn("xrLocateSpace", p_locateSpace) || !GetFn("xrAcquireSwapchainImage", p_acquire) ||
        !GetFn("xrWaitSwapchainImage", p_waitImage) || !GetFn("xrReleaseSwapchainImage", p_release) ||
        !GetFn("xrDestroySession", p_destroySession) || !GetFn("xrDestroyInstance", p_destroyInstance)) return false;
    bool steady = CreateStagePass() && MakeSwap(g_proj[0], g_recW, g_recH, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
                  MakeSwap(g_proj[1], g_recW, g_recH, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT);
    for (int e = 0; e < 2; e++) if (!MakeSwap(g_quad[e], g_eyeW, g_eyeH, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT)) return false;
    g_isVD = strstr(g_runtime, "VirtualDesktop") || strstr(g_runtime, "Virtual Desktop");
    g_steadyPossible = steady;
    if (!steady) Log("XR Steady picture not possible here, Floating Panel (quad) used");
    bool wantSteady = WantedLayer() == 1;
    g_layerInUse = wantSteady ? 1 : 0;
    Log("XR PICTURE setting %s, runtime \"%s\", headset \"%s\", layer %s, swapchain format %d, stage image %dx%d, eye views %dx%d",
        g_picture == 1 ? "Steady" : g_picture == 2 ? "Panel" : "Auto", g_runtime, g_headset, wantSteady ? "Steady (projection)" : "Panel (quad)",
        (int)g_format, g_eyeW, g_eyeH, (int)g_recW, (int)g_recH);
    if (!CreateSlots()) return false;
    if (!CreateSharpenPass()) Log("XR sharpen not available");
    return true;
}

// ------------------------------------------------------------------------------------------------ maths
struct V3 { float x, y, z; };
static V3 Rotate(const float q[4], V3 v) {
    V3 t{2 * (q[1] * v.z - q[2] * v.y), 2 * (q[2] * v.x - q[0] * v.z), 2 * (q[0] * v.y - q[1] * v.x)};
    return {v.x + q[3] * t.x + (q[1] * t.z - q[2] * t.y), v.y + q[3] * t.y + (q[2] * t.x - q[0] * t.z), v.z + q[3] * t.z + (q[0] * t.y - q[1] * t.x)};
}
static float g_viewPoint[3] = {0, 0, 0}, g_viewTurn[4] = {0, 0, 0, 1};  // recenter point and yaw (xyzw)
static float g_stagePose[7] = {0, 0, -1, 0, 0, 0, 1};
static float g_stageSize[2] = {1.85f, 1.04f};
static uint32_t g_recenters = 0;

static void PlaceStage(const XrView* v) {
    float p[3] = {(v[0].pose.position.x + v[1].pose.position.x) * 0.5f, (v[0].pose.position.y + v[1].pose.position.y) * 0.5f, (v[0].pose.position.z + v[1].pose.position.z) * 0.5f};
    const XrQuaternionf& q = v[0].pose.orientation;
    float qq[4] = {q.x, q.y, q.z, q.w};
    V3 fwd = Rotate(qq, {0, 0, -1});
    float yaw = atan2f(-fwd.x, -fwd.z);
    g_viewTurn[0] = 0; g_viewTurn[1] = sinf(yaw * 0.5f); g_viewTurn[2] = 0; g_viewTurn[3] = cosf(yaw * 0.5f);
    memcpy(g_viewPoint, p, sizeof p);
    Stage st; { std::lock_guard<std::mutex> lk(g_stageMx); st = g_stage; }
    V3 ahead = Rotate(g_viewTurn, {0, 0, -st.distance});
    g_stagePose[0] = p[0] + ahead.x; g_stagePose[1] = p[1] + ahead.y; g_stagePose[2] = p[2] + ahead.z;
    memcpy(g_stagePose + 3, g_viewTurn, sizeof g_viewTurn);
    g_stageSize[0] = st.width; g_stageSize[1] = st.width * g_eyeH / (float)g_eyeW;
    g_recenters++;
    Log("RECENTER view (%.3f, %.3f, %.3f) facing %.1f deg, stage %.2f x %.2f m at %.2f m", p[0], p[1], p[2], yaw * 57.2958f, g_stageSize[0], g_stageSize[1], st.distance);
}

// A room rectangle's corners in the eye's clip space: room -> eye -> lens (Ori VR's StageCorners).
static float g_menuPose[7] = {0, 0, -0.8f, 0, 0, 0, 1}, g_menuSize[2] = {0.9f, 0.5625f};
static void Corners(const float* rect, const float* size, const XrPosef& eye, const XrFovf& fov, float out[16]);
static void StageCorners(const XrPosef& eye, const XrFovf& fov, float out[16]) { Corners(g_stagePose, g_stageSize, eye, fov, out); }
static void Corners(const float* rect, const float* size, const XrPosef& eye, const XrFovf& fov, float out[16]) {
    float inv[4] = {-eye.orientation.x, -eye.orientation.y, -eye.orientation.z, eye.orientation.w};
    float hw = size[0] * 0.5f, hh = size[1] * 0.5f;
    const V3 local[4] = {{-hw, hh, 0}, {hw, hh, 0}, {-hw, -hh, 0}, {hw, -hh, 0}};
    float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), u = tanf(fov.angleUp), d = tanf(fov.angleDown);
    const float n = 0.05f, f = 100.0f;
    for (int i = 0; i < 4; i++) {
        V3 w = Rotate(rect + 3, local[i]);
        V3 v = Rotate(inv, {rect[0] + w.x - eye.position.x, rect[1] + w.y - eye.position.y, rect[2] + w.z - eye.position.z});
        out[i * 4 + 0] = (2 * v.x + (l + r) * v.z) / (r - l);
        out[i * 4 + 1] = (2 * v.y + (u + d) * v.z) / (u - d);
        out[i * 4 + 2] = (f / (f - n)) * -v.z - f * n / (f - n);
        out[i * 4 + 3] = -v.z;
    }
}

// ------------------------------------------------------------------------------------------------ game side (render thread)
struct Pending { void* tex[2]; Views views; uint32_t frame; };
static std::mutex g_pendMx;
static std::deque<Pending> g_pending;
void QueueFrame(void* texL, void* texR, const Views& used, uint32_t frameNo) {
    std::lock_guard<std::mutex> lk(g_pendMx);
    if (g_pending.size() > 4) g_pending.pop_front();
    g_pending.push_back({{texL, texR}, used, frameNo});
    std::lock_guard<std::mutex> sl(g_statMx); g_stats.queued++;
}
static ID3D11Device* g_gameDev; static ID3D11DeviceContext* g_gameCtx;
static void __stdcall OnRenderEvent(int) {
    Pending p;
    { std::lock_guard<std::mutex> lk(g_pendMx); if (g_pending.empty()) return; p = g_pending.front(); g_pending.pop_front(); }
    if (!g_slotsReady || !p.tex[0] || !p.tex[1]) return;
    double t0 = Ms();
    if (!g_gameDev) {
        ((ID3D11Texture2D*)p.tex[0])->GetDevice(&g_gameDev); g_gameDev->GetImmediateContext(&g_gameCtx);
        for (auto& s : g_slots) for (int e = 0; e < 2; e++) {
            if (FAILED(g_gameDev->OpenSharedResource(s.share[e], __uuidof(ID3D11Texture2D), (void**)&s.gameTex[e]))) { Log("XR game side could not open a shared slot"); return; }
            s.gameTex[e]->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&s.gameMx[e]);
        }
        Log("XR game side opened the shared ring on the game's device");
    }
    // The oldest-written slot the XR side is not holding. Never wait: a busy slot is skipped and counted.
    Slot* best = nullptr;
    for (auto& s : g_slots) if (!s.xrHolds && (!best || s.seq < best->seq)) best = &s;  // seq 0 = free, else the oldest unshown
    if (!best) { std::lock_guard<std::mutex> sl(g_statMx); g_stats.skippedNoSlots++; return; }
    if (best->gameMx[0]->AcquireSync(0, 0) != S_OK) { std::lock_guard<std::mutex> sl(g_statMx); g_stats.skippedBusy++; return; }
    if (best->gameMx[1]->AcquireSync(0, 0) != S_OK) { best->gameMx[0]->ReleaseSync(0); std::lock_guard<std::mutex> sl(g_statMx); g_stats.skippedBusy++; return; }
    for (int e = 0; e < 2; e++) g_gameCtx->CopyResource(best->gameTex[e], (ID3D11Texture2D*)p.tex[e]);
    best->views = p.views;
    best->seq = ++g_seq;
    best->gameMx[0]->ReleaseSync(1); best->gameMx[1]->ReleaseSync(1);
    double ms = Ms() - t0;
    std::lock_guard<std::mutex> sl(g_statMx); g_stats.copied++; if (ms > g_stats.copyMsMax) g_stats.copyMsMax = ms;
}
void* RenderEventFunc() { return (void*)&OnRenderEvent; }

// ------------------------------------------------------------------------------------------------ XR thread
static Slot* g_shown = nullptr;
// Written slots the XR side has not taken (key 1). The newest is shown; older ones are handed straight back (key 0),
// so the game side always finds a free slot unless the XR side is mid-draw.
static uint32_t g_dropped = 0;
static Slot* TakeNewest() {
    Slot* newest = nullptr;
    for (auto& s : g_slots) if (s.seq && !s.xrHolds && (!g_shown || s.seq > g_shown->seq) && (!newest || s.seq > newest->seq)) newest = &s;
    for (auto& s : g_slots) {
        if (&s == newest || &s == g_shown || !s.seq || s.xrHolds) continue;
        if (s.xrMx[0]->AcquireSync(1, 0) == S_OK) {
            if (s.xrMx[1]->AcquireSync(1, 0) == S_OK) { s.xrMx[1]->ReleaseSync(0); s.seq = 0; g_dropped++; }
            s.xrMx[0]->ReleaseSync(0);
        }
    }
    if (!newest) return nullptr;
    if (newest->xrMx[0]->AcquireSync(1, 0) != S_OK) return nullptr;
    if (newest->xrMx[1]->AcquireSync(1, 0) != S_OK) { newest->xrMx[0]->ReleaseSync(1); return nullptr; }
    newest->xrHolds = true;
    if (g_shown) { g_shown->xrMx[0]->ReleaseSync(0); g_shown->xrMx[1]->ReleaseSync(0); g_shown->xrHolds = false; }
    g_shown = newest;
    return newest;
}

static ID3D11ShaderResourceView* g_menuSrv;  // this frame's menu, null when hidden
static bool g_sharpened = false;  // this frame's eye images went through the sharpen pass
static void SharpenShown() {
    g_sharpened = false;
    float amount = g_sharpen;
    if (amount <= 0 || !g_shown || !g_passRtv[0]) return;
    for (int e = 0; e < 2; e++) {
        float params[4] = {amount, 1.0f / g_eyeW, 1.0f / g_eyeH, 0};
        g_ctx->UpdateSubresource(g_shCb, 0, nullptr, params, 0, 0);
        D3D11_VIEWPORT vp{0, 0, (float)g_eyeW, (float)g_eyeH, 0, 1};
        g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_noCull); g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        g_ctx->OMSetRenderTargets(1, &g_passRtv[e], nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->IASetInputLayout(nullptr);
        g_ctx->VSSetShader(g_shVs, nullptr, 0); g_ctx->PSSetShader(g_shPs, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_shCb); g_ctx->PSSetSamplers(0, 1, &g_point); g_ctx->PSSetShaderResources(0, 1, &g_shown->rawSrv[e]);
        g_ctx->Draw(3, 0);
        ID3D11ShaderResourceView* no = nullptr; g_ctx->PSSetShaderResources(0, 1, &no);
        ID3D11RenderTargetView* none = nullptr; g_ctx->OMSetRenderTargets(1, &none, nullptr);
    }
    g_sharpened = true;
}
static ID3D11ShaderResourceView* EyeSrv(int e) { return g_sharpened ? g_passSrv[e] : g_shown->srv[e]; }
static ID3D11Texture2D* EyeTex(int e) { return g_sharpened ? g_passTex[e] : g_shown->xrTex[e]; }
static void SurroundColour(float c[4]) { c[0] = 0; c[1] = g_surround == 1 ? 1.0f : 0; c[2] = 0; c[3] = SeeThrough() ? 0 : 1.0f; }
static void DrawRect(ID3D11ShaderResourceView* srv, const float* corners) {
    g_ctx->UpdateSubresource(g_cb, 0, nullptr, corners, 0, 0);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->PSSetSamplers(0, 1, &g_linear); g_ctx->PSSetShaderResources(0, 1, &srv);
    g_ctx->Draw(4, 0);
    ID3D11ShaderResourceView* no = nullptr; g_ctx->PSSetShaderResources(0, 1, &no);
}
static bool DrawSteady(int e, const XrView& view, XrCompositionLayerProjectionView& out) {
    Swap& s = g_proj[e];
    uint32_t idx = 0;
    if (XR_FAILED(p_acquire(s.handle, nullptr, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = 100000000;
    if (XR_FAILED(p_waitImage(s.handle, &wi))) { p_release(s.handle, nullptr); return false; }
    if (!s.rtv[idx]) {
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = (DXGI_FORMAT)g_format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        g_dev->CreateRenderTargetView(s.images[idx].texture, &rv, &s.rtv[idx]);
    }
    float surround[4]; SurroundColour(surround);
    g_ctx->ClearRenderTargetView(s.rtv[idx], surround);
    D3D11_VIEWPORT vp{0, 0, (float)s.w, (float)s.h, 0, 1};
    g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_noCull); g_ctx->IASetInputLayout(nullptr);
    g_ctx->OMSetRenderTargets(1, &s.rtv[idx], nullptr);
    g_ctx->OMSetDepthStencilState(nullptr, 0);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    float corners[16];
    if (g_shown) { StageCorners(view.pose, view.fov, corners); DrawRect(EyeSrv(e), corners); }
    if (g_menuSrv) {
        g_ctx->OMSetBlendState(g_alphaBlend, nullptr, 0xffffffff);
        Corners(g_menuPose, g_menuSize, view.pose, view.fov, corners); DrawRect(g_menuSrv, corners);
        g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    }
    { ID3D11RenderTargetView* none = nullptr; g_ctx->OMSetRenderTargets(1, &none, nullptr); }
    p_release(s.handle, nullptr);
    out = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    out.pose = view.pose; out.fov = view.fov;
    out.subImage.swapchain = s.handle; out.subImage.imageRect.extent = {s.w, s.h};
    return true;
}

static bool CopyPanel(int e, XrCompositionLayerQuad& q) {
    Swap& s = g_quad[e];
    uint32_t idx = 0;
    if (!g_shown || XR_FAILED(p_acquire(s.handle, nullptr, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = 100000000;
    if (XR_FAILED(p_waitImage(s.handle, &wi))) { p_release(s.handle, nullptr); return false; }
    g_ctx->CopyResource(s.images[idx].texture, EyeTex(e));
    p_release(s.handle, nullptr);
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.space = g_space; q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
    q.subImage.swapchain = s.handle; q.subImage.imageRect.extent = {s.w, s.h};
    q.pose.position = {g_stagePose[0], g_stagePose[1], g_stagePose[2]};
    q.pose.orientation = {g_stagePose[3], g_stagePose[4], g_stagePose[5], g_stagePose[6]};
    q.size = {g_stageSize[0], g_stageSize[1]};
    return true;
}

static Swap g_backdrop;
static bool Backdrop(const XrView* views, XrCompositionLayerProjectionView* pv) {
    if (!g_backdrop.handle && !MakeSwap(g_backdrop, 16, 16, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) return false;
    uint32_t idx = 0;
    if (XR_FAILED(p_acquire(g_backdrop.handle, nullptr, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = 100000000;
    if (XR_FAILED(p_waitImage(g_backdrop.handle, &wi))) { p_release(g_backdrop.handle, nullptr); return false; }
    if (!g_backdrop.rtv[idx]) { D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = (DXGI_FORMAT)g_format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; g_dev->CreateRenderTargetView(g_backdrop.images[idx].texture, &rv, &g_backdrop.rtv[idx]); }
    float c[4]; SurroundColour(c); g_ctx->ClearRenderTargetView(g_backdrop.rtv[idx], c);
    p_release(g_backdrop.handle, nullptr);
    for (int e = 0; e < 2; e++) {
        pv[e] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}; pv[e].pose = views[e].pose; pv[e].fov = views[e].fov;
        pv[e].subImage.swapchain = g_backdrop.handle; pv[e].subImage.imageRect.extent = {16, 16};
    }
    return true;
}

static bool MenuQuad(XrCompositionLayerQuad& q) {
    if (!g_menuSrv) return false;
    if (!g_menuSwap.handle && !MakeSwap(g_menuSwap, MenuPixelsW(), MenuPixelsH(), XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT)) return false;
    uint32_t idx = 0;
    if (XR_FAILED(p_acquire(g_menuSwap.handle, nullptr, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = 100000000;
    if (XR_FAILED(p_waitImage(g_menuSwap.handle, &wi))) { p_release(g_menuSwap.handle, nullptr); return false; }
    g_ctx->CopyResource(g_menuSwap.images[idx].texture, MenuTexture());
    p_release(g_menuSwap.handle, nullptr);
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    q.space = g_space; q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = g_menuSwap.handle; q.subImage.imageRect.extent = {MenuPixelsW(), MenuPixelsH()};
    q.pose.position = {g_menuPose[0], g_menuPose[1], g_menuPose[2]};
    q.pose.orientation = {g_menuPose[3], g_menuPose[4], g_menuPose[5], g_menuPose[6]};
    q.size = {g_menuSize[0], g_menuSize[1]};
    return true;
}

static void PlaceMenu(const MenuSnapshot& m) {
    V3 at = Rotate(g_viewTurn, {0, m.height, -m.distance});
    g_menuPose[0] = g_viewPoint[0] + at.x; g_menuPose[1] = g_viewPoint[1] + at.y; g_menuPose[2] = g_viewPoint[2] + at.z;
    memcpy(g_menuPose + 3, g_viewTurn, sizeof g_viewTurn);
    g_menuSize[0] = 1.2f * m.size; g_menuSize[1] = g_menuSize[0] * MenuAspect();
}

static void XrThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    QueryPerformanceFrequency(&g_qpf);
    g_state = Starting;
    if (!StartXr()) { g_state = Failed; Log("XR VR DID NOT START, error %d. The game runs flat.", (int)g_lastError); return; }
    bool running = false;
    XrSessionState sstate = XR_SESSION_STATE_UNKNOWN;
    double focusedAt = -1;
    while (!g_quit) {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (p_poll(g_inst, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                sstate = ((XrEventDataSessionStateChanged&)ev).state;
                Log("XR session state %d", (int)sstate);
                if (sstate == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult r = p_beginSession(g_sess, &bi); running = XR_SUCCEEDED(r); if (running) g_state = Running;
                    Log("XR session begun: %d", (int)r);
                } else if (sstate == XR_SESSION_STATE_STOPPING) { p_endSession(g_sess); running = false; g_state = Stopped; }
                else if (sstate == XR_SESSION_STATE_EXITING || sstate == XR_SESSION_STATE_LOSS_PENDING) { running = false; g_state = Stopped; g_quit = true; }
                else if (sstate == XR_SESSION_STATE_FOCUSED && focusedAt < 0) { focusedAt = Ms(); }
            } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                Log("XR the runtime recentred (reference space change): recentring the stage"); g_wantRecenter = true;
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { g_quit = true; }
            ev = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!running) { Sleep(10); continue; }
        double t0 = Ms();
        XrFrameState fs{XR_TYPE_FRAME_STATE};
        if (XR_FAILED(p_waitFrame(g_sess, nullptr, &fs))) { Sleep(1); continue; }
        double t1 = Ms();
        p_beginFrame(g_sess, nullptr);
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        XrViewState vs{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = fs.predictedDisplayTime; li.space = g_space;
        uint32_t vc = 0;
        bool valid = XR_SUCCEEDED(p_locate(g_sess, &li, &vs, 2, &vc, views)) &&
                     (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
        // Place the stage once the session is FOCUSED: Virtual Desktop reports a stand-in head pose before that (Ori VR).
        // A session that never gets focus still gets a stage after 20 s.
        bool focused = sstate == XR_SESSION_STATE_FOCUSED;
        static double startedAt = Ms();
        if (valid && g_wantRecenter && ((focused && Ms() - focusedAt > 500) || Ms() - startedAt > 20000)) { g_wantRecenter = false; PlaceStage(views); }
        {
            std::lock_guard<std::mutex> lk(g_viewsMx);
            Views& o = g_views;
            o.valid = valid; o.focused = focused; o.sessionState = (int)sstate; o.periodSec = fs.predictedDisplayPeriod / 1e9;
            o.displayTime = fs.predictedDisplayTime; o.recenters = g_recenters;
            float inv[4] = {-g_viewTurn[0], -g_viewTurn[1], -g_viewTurn[2], g_viewTurn[3]};
            for (int e = 0; e < 2; e++) {
                const XrPosef& p = views[e].pose;
                float pp[7] = {p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w};
                memcpy(o.pose[e], pp, sizeof pp);
                V3 l = Rotate(inv, {p.position.x - g_viewPoint[0], p.position.y - g_viewPoint[1], p.position.z - g_viewPoint[2]});
                o.local[e][0] = l.x; o.local[e][1] = l.y; o.local[e][2] = l.z;
                o.fov[e][0] = views[e].fov.angleLeft; o.fov[e][1] = views[e].fov.angleRight; o.fov[e][2] = views[e].fov.angleUp; o.fov[e][3] = views[e].fov.angleDown;
            }
            if (valid) g_tanSpan = tanf(views[0].fov.angleRight) - tanf(views[0].fov.angleLeft);
        }
        bool fresh = TakeNewest() != nullptr;
        if (fresh) SharpenShown();
        {   // Picture can change live from the menu
            int want = WantedLayer();
            if (want != g_layerInUse) { g_layerInUse = want; Log("XR PICTURE now %s", want ? "Steady (projection)" : "Floating Panel (quad)"); }
        }
        MenuSnapshot place;
        g_menuSrv = g_recenters ? DrawMenu(g_dev, g_ctx, g_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, Log, &place) : nullptr;
        if (g_menuSrv) PlaceMenu(place);
        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
        fe.displayTime = fs.predictedDisplayTime; fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView pv[2];
        XrCompositionLayerQuad quads[3];
        XrCompositionLayerProjection back{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView backViews[2];
        const XrCompositionLayerBaseHeader* layers[4]; uint32_t nl = 0;
        if (SeeThrough()) fe.environmentBlendMode = g_canAlphaBlend ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
        if (fs.shouldRender && valid && g_shown && g_recenters) {
            if (g_layerInUse == 1) {
                if (DrawSteady(0, views[0], pv[0]) && DrawSteady(1, views[1], pv[1])) {
                    proj.space = g_space; proj.viewCount = 2; proj.views = pv;
                    if (SeeThrough()) proj.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                    layers[nl++] = (XrCompositionLayerBaseHeader*)&proj;
                }
            } else if (CopyPanel(0, quads[0]) && CopyPanel(1, quads[1])) {
                if (g_surround != 0 && Backdrop(views, backViews)) {
                    back.space = g_space; back.viewCount = 2; back.views = backViews;
                    if (SeeThrough()) back.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                    layers[nl++] = (XrCompositionLayerBaseHeader*)&back;
                }
                layers[nl++] = (XrCompositionLayerBaseHeader*)&quads[0]; layers[nl++] = (XrCompositionLayerBaseHeader*)&quads[1];
                if (MenuQuad(quads[2])) layers[nl++] = (XrCompositionLayerBaseHeader*)&quads[2];
            }
        }
        fe.layerCount = nl; fe.layers = layers;
        double t2 = Ms();
        p_endFrame(g_sess, &fe);
        double t3 = Ms();
        std::lock_guard<std::mutex> sl(g_statMx);
        g_stats.xrFrames++; if (!fresh) g_stats.xrNoNewImage++; if (nl) g_stats.layersSent++;
        double w = t1 - t0, en = t3 - t2;
        g_waitSum += w; g_endSum += en; g_timed++;
        if (w > g_stats.waitMsMax) g_stats.waitMsMax = w; if (en > g_stats.endMsMax) g_stats.endMsMax = en;
    }
    if (g_sess) { p_endSession(g_sess); p_destroySession(g_sess); g_sess = XR_NULL_HANDLE; }
    if (g_inst) { p_destroyInstance(g_inst); g_inst = XR_NULL_HANDLE; }
    g_threadDone = true;
    g_state = Stopped;
    Log("XR thread ended");
}

void Start() {
    int expected = Off;
    if (!g_state.compare_exchange_strong(expected, Starting)) return;
    g_thread = std::thread(XrThread);
    g_thread.detach();
}
// Called when the game closes: stop the XR thread and let it end the session, waiting at most 1 s.
void Shutdown() {
    if (g_state == Off || g_threadDone) return;
    g_quit = true;
    for (int i = 0; i < 100 && !g_threadDone; i++) Sleep(10);
    Log("XR shut down for exit (%s)", g_threadDone ? "thread ended" : "thread still busy after 1 s");
}

}  // namespace wxr
