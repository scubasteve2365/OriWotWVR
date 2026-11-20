// Draws the mod menu snapshot into a texture on the XR thread with Dear ImGui (ocornut/imgui, MIT), styled after the
// game's own option screens: a dark panel, tabs down the left in small capitals, the chosen row lit by a soft blue-white
// streak, values on the right, button hints along the bottom. Fonts: Marcellus SC and Alegreya Sans (SIL OFL 1.1).
#include "MenuDraw.h"
#include <windows.h>
#include <mutex>
#include <string>
#include "imgui.h"
#include "imgui_impl_dx11.h"

namespace wxr {

static std::mutex g_menuMx;
static MenuSnapshot g_menu;
void SetMenu(const MenuSnapshot& m) { std::lock_guard<std::mutex> lk(g_menuMx); g_menu = m; }

static const int kW = 1400, kH = 787;  // Ori VR's menu proportions
static bool g_init = false, g_failed = false;
static ID3D11Texture2D* g_tex; static ID3D11RenderTargetView* g_rtv; static ID3D11ShaderResourceView* g_srv;
static ImFont *g_fTitle, *g_fText, *g_fSmall, *g_fTiny;

static std::string FontDir() {
    char p[MAX_PATH]; HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&FontDir, &self);
    GetModuleFileNameA(self, p, MAX_PATH); *(strrchr(p, '\\') + 1) = 0;
    return std::string(p) + "fonts\\";
}

static bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, bool srgb, void (*log)(const char*, ...)) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = kW; d.Height = kH; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_tex))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(g_tex, &rv, &g_rtv);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(g_tex, &sv, &g_srv);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)kW, (float)kH);
    std::string dir = FontDir();
    g_fTitle = io.Fonts->AddFontFromFileTTF((dir + "AlegreyaSans-Regular.ttf").c_str(), 72.0f);
    g_fText = io.Fonts->AddFontFromFileTTF((dir + "AlegreyaSans-Regular.ttf").c_str(), 42.0f);
    g_fSmall = io.Fonts->AddFontFromFileTTF((dir + "AlegreyaSans-Regular.ttf").c_str(), 30.0f);
    g_fTiny = io.Fonts->AddFontFromFileTTF((dir + "AlegreyaSans-Regular.ttf").c_str(), 25.0f);
    if (!g_fTitle || !g_fText || !g_fSmall || !g_fTiny) { log("MENU fonts missing in %s, using the built-in font", dir.c_str()); io.Fonts->Clear(); g_fTitle = g_fText = g_fSmall = g_fTiny = io.Fonts->AddFontDefault(); }
    if (!ImGui_ImplDX11_Init(dev, ctx)) return false;
    log("MENU panel ready %dx%d", kW, kH);
    return g_tex && g_rtv && g_srv;
}

static ImU32 Col(float r, float g, float b, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }

// A soft horizontal streak of light behind the chosen row or tab, like the game's menus.
static void Streak(ImDrawList* dl, float x0, float x1, float yc, float h) {
    float xm = (x0 + x1) * 0.5f;
    ImU32 clear = Col(0.55f, 0.75f, 1.0f, 0.0f), lit = Col(0.55f, 0.75f, 1.0f, 0.30f);
    dl->AddRectFilledMultiColor(ImVec2(x0, yc - h), ImVec2(xm, yc + h), clear, lit, lit, clear);
    dl->AddRectFilledMultiColor(ImVec2(xm, yc - h), ImVec2(x1, yc + h), lit, clear, clear, lit);
    ImU32 lineClear = Col(0.85f, 0.93f, 1.0f, 0.0f), line = Col(0.85f, 0.93f, 1.0f, 0.85f);
    dl->AddRectFilledMultiColor(ImVec2(x0, yc + h - 2), ImVec2(xm, yc + h), lineClear, line, line, lineClear);
    dl->AddRectFilledMultiColor(ImVec2(xm, yc + h - 2), ImVec2(x1, yc + h), line, lineClear, lineClear, line);
}

static void Text(ImDrawList* dl, ImFont* f, float x, float y, ImU32 c, const char* s, bool glow = false) {
    if (glow) for (int i = 0; i < 4; i++) dl->AddText(f, f->LegacySize, ImVec2(x + (i & 1 ? 1.5f : -1.5f), y + (i & 2 ? 1.5f : -1.5f)), Col(0.6f, 0.8f, 1.0f, 0.35f), s);
    dl->AddText(f, f->LegacySize, ImVec2(x, y), c, s);
}

// Fills the texture and returns its view, or nullptr when the menu is hidden or cannot be drawn.
ID3D11ShaderResourceView* DrawMenu(ID3D11Device* dev, ID3D11DeviceContext* ctx, bool srgb, void (*log)(const char*, ...), MenuSnapshot* outPlacement) {
    MenuSnapshot m;
    { std::lock_guard<std::mutex> lk(g_menuMx); m = g_menu; }
    *outPlacement = m;
    if (!m.visible || g_failed) return nullptr;
    if (!g_init) { g_init = true; if (!Init(dev, ctx, srgb, log)) { g_failed = true; log("MENU panel could not be made"); return nullptr; } }
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // Ori VR's layout (mockup A): VR SETTINGS, tabs across the top with LB and RB at the ends, rows
    // with the label on the left and the value on the right, the chosen row gold on a soft blue bar with arrows, the
    // explanation, a line of button hints and a small readout line, the version in the corner.
    const ImU32 gold = Col(1.0f, 0.82f, 0.45f, 1), dim = Col(0.75f, 0.78f, 0.80f, 1), title = Col(0.72f, 0.90f, 1.0f, 1),
                faint = Col(0.55f, 0.60f, 0.64f, 1), white = Col(0.93f, 0.95f, 0.97f, 1);
    auto centred = [&](ImFont* f, float y, ImU32 c, const char* t, float wrap = 0) {
        if (wrap > 0) {
            // word wrap by measuring, each line centred
            std::string text = t, line, word; float yy = y;
            auto flush = [&]() { if (line.empty()) return; ImVec2 ls = f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0, line.c_str());
                dl->AddText(f, f->LegacySize, ImVec2((kW - ls.x) * 0.5f, yy), c, line.c_str()); yy += f->LegacySize * 1.15f; line.clear(); };
            size_t i = 0;
            while (i <= text.size()) {
                size_t j = text.find(' ', i); if (j == std::string::npos) j = text.size();
                word = text.substr(i, j - i);
                std::string tryLine = line.empty() ? word : line + " " + word;
                if (!line.empty() && f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0, tryLine.c_str()).x > wrap) { flush(); line = word; }
                else line = tryLine;
                i = j + 1;
            }
            flush();
            return;
        }
        ImVec2 sz = f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0, t);
        dl->AddText(f, f->LegacySize, ImVec2((kW - sz.x) * 0.5f, y), c, t);
    };
    auto softBar = [&](float x0, float x1, float yc, float h, float a) {
        float xm = (x0 + x1) * 0.5f;
        ImU32 clear = Col(0.2f, 0.5f, 0.85f, 0), blue = Col(0.2f, 0.5f, 0.85f, a);
        dl->AddRectFilledMultiColor(ImVec2(x0, yc - h), ImVec2(xm, yc + h), clear, blue, blue, clear);
        dl->AddRectFilledMultiColor(ImVec2(xm, yc - h), ImVec2(x1, yc + h), blue, clear, clear, blue);
        dl->AddRectFilled(ImVec2(x0 + (x1 - x0) * 0.08f, yc - h), ImVec2(x1 - (x1 - x0) * 0.08f, yc + h), Col(0.2f, 0.5f, 0.85f, a * 0.6f), h);
    };
    // Panel with soft edges.
    for (int i = 0; i < 6; i++) {
        float in = i * 5.0f;
        dl->AddRectFilled(ImVec2(in, in), ImVec2(kW - in, kH - in), Col(0.015f, 0.04f, 0.075f, 0.30f + i * 0.13f), 34.0f - i * 3);
    }
    // Title and version.
    centred(g_fTitle, 26, m.problem ? Col(1, 0.55f, 0.45f, 1) : title, m.problem ? "VR DID NOT START" : "VR SETTINGS");
    ImVec2 vs = g_fSmall->CalcTextSizeA(g_fSmall->LegacySize, FLT_MAX, 0, m.version);
    dl->AddText(g_fSmall, g_fSmall->LegacySize, ImVec2(kW - 56 - vs.x, 22), faint, m.version);
    // Tabs across the top, LB and RB at the ends.
    float tabY = 112, gap = 330;
    for (int i = 0; i < m.tabCount; i++) {
        float cx = kW * 0.5f + (i - (m.tabCount - 1) * 0.5f) * gap;
        ImVec2 sz = g_fText->CalcTextSizeA(g_fText->LegacySize, FLT_MAX, 0, m.tabs[i]);
        if (i == m.tab) softBar(cx - 115, cx + 115, tabY + sz.y * 0.5f, 22, 0.55f);
        dl->AddText(g_fText, g_fText->LegacySize, ImVec2(cx - sz.x * 0.5f, tabY), i == m.tab ? gold : white, m.tabs[i]);
    }
    dl->AddText(g_fSmall, g_fSmall->LegacySize, ImVec2(46, tabY + 8), faint, "LB");
    dl->AddText(g_fSmall, g_fSmall->LegacySize, ImVec2(kW - 46 - 26, tabY + 8), faint, "RB");
    {   // thin glowing line under the tabs
        float y = tabY + 58, x0 = 210, x1 = kW - 210, xm = kW * 0.5f;
        ImU32 c0 = Col(0.45f, 0.75f, 1, 0), c1 = Col(0.45f, 0.75f, 1, 0.8f);
        dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(xm, y + 3), c0, c1, c1, c0);
        dl->AddRectFilledMultiColor(ImVec2(xm, y), ImVec2(x1, y + 3), c1, c0, c0, c1);
    }
    // Rows.
    float x0 = 180, x1 = kW - 180, ry = 200, step = 50;
    for (int i = 0; i < m.rowCount; i++) {
        bool on = i == m.row;
        ImVec2 ls = g_fText->CalcTextSizeA(g_fText->LegacySize, FLT_MAX, 0, m.rows[i].label);
        float yc = ry + ls.y * 0.5f;
        if (on) softBar(x0 - 40, x1 + 40, yc, 22, 0.7f);
        ImU32 c = on ? gold : white;
        dl->AddText(g_fText, g_fText->LegacySize, ImVec2(x0, ry), c, m.rows[i].label);
        if (m.rows[i].value[0]) {
            ImVec2 vz = g_fText->CalcTextSizeA(g_fText->LegacySize, FLT_MAX, 0, m.rows[i].value);
            float vx = x1 - vz.x;
            if (on && !m.rows[i].button) {
                vx -= 30;
                dl->AddTriangleFilled(ImVec2(vx - 28, yc), ImVec2(vx - 12, yc - 10), ImVec2(vx - 12, yc + 10), gold);
                dl->AddTriangleFilled(ImVec2(x1 + 6, yc - 10), ImVec2(x1 + 6, yc + 10), ImVec2(x1 + 22, yc), gold);
            }
            dl->AddText(g_fText, g_fText->LegacySize, ImVec2(vx, ry), c, m.rows[i].value);
        }
        ry += step;
    }
    // Explanation, then hints and readout.
    centred(g_fSmall, kH - 250, title, m.explain, kW - 260.0f);
    {
        float y = kH - 128, xm = kW * 0.5f;
        ImU32 c0 = Col(0.45f, 0.75f, 1, 0), c1 = Col(0.45f, 0.75f, 1, 0.35f);
        dl->AddRectFilledMultiColor(ImVec2(220, y), ImVec2(xm, y + 2), c0, c1, c1, c0);
        dl->AddRectFilledMultiColor(ImVec2(xm, y), ImVec2(kW - 220, y + 2), c1, c0, c0, c1);
    }
    centred(g_fSmall, kH - 110, dim, m.hint);
    centred(g_fTiny, kH - 70, faint, m.readout);
    ImGui::Render();
    float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(g_rtv, clear);
    ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, (float)kW, (float)kH, 0, 1}; ctx->RSSetViewports(1, &vp);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ID3D11RenderTargetView* none = nullptr; ctx->OMSetRenderTargets(1, &none, nullptr);
    return g_srv;
}
ID3D11Texture2D* MenuTexture() { return g_tex; }
float MenuAspect() { return (float)kH / kW; }
int MenuPixelsW() { return kW; }
int MenuPixelsH() { return kH; }

}  // namespace wxr
