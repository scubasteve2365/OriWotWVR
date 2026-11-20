// Mod menu (Phase 1 rows only), included by OriWotWVRNative.cpp. Runs on the game's main thread; the XR thread draws
// the snapshot (xr\MenuDraw.cpp). Opens with both sticks held 0.5 s on an Xbox pad, or Insert (free in the game's
// key bindings, KeyRebindings.txt). While it is open the game gets no pad or keyboard input, and after closing nothing
// reaches the game until every key and button is released.

namespace menu {

enum Kind { Button, Value };
struct Row { const char* label; Kind kind; };
struct Tab { const char* name; std::vector<Row> rows; };
static std::vector<Tab> g_tabs;
static bool g_open = false, g_block = false;
static int g_tab = 0, g_row = 0;
static int g_rowPerTab[3] = {0, 0, 0};
static double g_bothSticksSince = -1;
static bool g_comboUsed = false;
static bool g_lastKeyboard = false;

// Pad state as the game last read it (any slot), from the XInputGetState hooks.
static volatile uint16_t g_padButtons = 0;
static volatile int16_t g_padLY = 0, g_padLX = 0;
static volatile double g_padSeen = -1;
enum { PadUp = 0x0001, PadDown = 0x0002, PadLeft = 0x0004, PadRight = 0x0008, PadStart = 0x0010, PadBack = 0x0020,
       PadL3 = 0x0040, PadR3 = 0x0080, PadLB = 0x0100, PadRB = 0x0200, PadA = 0x1000, PadB = 0x2000 };

// ---- menu tones (Ori VR's MenuSound: soft bells made in code), played with PlaySound from memory
namespace tones {
static std::vector<uint8_t> g_wav[6];
enum { Move, Change, TabT, Open, Close, Refused };
static void AddBell(std::vector<float>& d, int start, int len, float f, float amp) {
    float decay = 6.0f / (len / 44100.0f);
    for (int i = 0; i < len && start + i < (int)d.size(); i++) {
        float tt = i / 44100.0f, attack = tt / 0.004f > 1 ? 1 : tt / 0.004f;
        d[start + i] += amp * attack * expf(-decay * tt) * (sinf(6.2831853f * f * tt) + 0.25f * sinf(12.566371f * f * tt));
    }
}
static std::vector<float> Notes(std::initializer_list<float> fs, float note, float tail) {
    int nl = (int)(note * 44100), total = nl * (int)fs.size() + (int)(tail * 44100); std::vector<float> d(total, 0); int n = 0;
    for (float f : fs) { AddBell(d, n * nl, total - n * nl, f, 0.5f); n++; }
    return d;
}
static std::vector<float> Chord(std::initializer_list<float> fs, float sec) { std::vector<float> d((int)(sec * 44100), 0); for (float f : fs) AddBell(d, 0, (int)d.size(), f, 0.4f); return d; }
static std::vector<uint8_t> Wav(const std::vector<float>& d, float vol) {
    std::vector<uint8_t> w(44 + d.size() * 2);
    auto u32 = [&](int o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto u16 = [&](int o, uint16_t v) { memcpy(&w[o], &v, 2); };
    memcpy(&w[0], "RIFF", 4); u32(4, (uint32_t)w.size() - 8); memcpy(&w[8], "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 1);
    u32(24, 44100); u32(28, 88200); u16(32, 2); u16(34, 16); memcpy(&w[36], "data", 4); u32(40, (uint32_t)d.size() * 2);
    for (size_t i = 0; i < d.size(); i++) { float v = d[i] * vol; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767); memcpy(&w[44 + i * 2], &q, 2); }
    return w;
}
static int g_builtFor = -1;
static void Play(int which) {
    static const float vols[4] = {0, 0.12f, 0.25f, 0.45f};
    int lvl = g_set.sounds < 0 ? 0 : g_set.sounds > 3 ? 3 : g_set.sounds;
    if (lvl == 0) return;
    if (g_builtFor != lvl) {
        float v = vols[lvl];
        g_wav[Move] = Wav(Notes({1318.5f}, 0.07f, 0.06f), v); g_wav[Change] = Wav(Notes({987.8f, 1318.5f}, 0.055f, 0.05f), v);
        g_wav[TabT] = Wav(Chord({784.f, 1174.7f}, 0.16f), v); g_wav[Open] = Wav(Notes({659.3f, 987.8f, 1318.5f}, 0.075f, 0.09f), v);
        g_wav[Close] = Wav(Notes({1318.5f, 987.8f, 659.3f}, 0.075f, 0.09f), v); g_wav[Refused] = Wav(Notes({220.f}, 0.12f, 0.08f), v);
        g_builtFor = lvl;
    }
    PlaySoundA((LPCSTR)g_wav[which].data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
}  // namespace tones

static void Build() {
    g_tabs = {
        {"STAGE", {{"Stage distance", Value}, {"Stage size", Value}, {"Depth", Value}, {"Pop-out", Value}, {"Screen shake", Value}, {"Recenter", Button}}},
        {"PICTURE", {{"Resolution", Value}, {"Sharpen", Value}, {"Picture", Value}, {"Surround", Value}}},
        {"MENU", {{"Sounds", Value}, {"Menu size", Value}, {"Menu height", Value}, {"Menu distance", Value}, {"Reset to defaults", Button}, {"Close", Button}}},
    };
}

static int NextEyeWidth() {
    float span = g_set.tanSpan > 0.5f ? g_set.tanSpan : 2.2f;
    int w = ((int)(wxr::RecommendedWidth() * (g_set.stageWidth / g_set.stageDistance) / span * g_set.resolution + 8) / 16) * 16;
    return w < 640 ? 640 : w > 8192 ? 8192 : w;
}
static const char* PictureName(int p) { return p == 1 ? "Steady" : p == 2 ? "Floating Panel" : "Auto"; }
static const char* SurroundName(int s) { return s == 1 ? "Green" : s == 2 ? (wxr::GetState() == wxr::Running && !wxr::CanSeeThrough() ? "Transparent (not offered, black)" : "Transparent") : "Black"; }

static void ValueText(const std::string& l, char* out, size_t n) {
    static const char* snd[4] = {"Off", "Low", "Medium", "High"};
    if (l == "Stage distance") sprintf_s(out, n, "%.2f m", g_set.stageDistance);
    else if (l == "Stage size") sprintf_s(out, n, "%.2f m", g_set.stageWidth);
    else if (l == "Depth") sprintf_s(out, n, "%d%%", (int)lroundf(g_set.depth * 100));
    else if (l == "Pop-out") sprintf_s(out, n, "%d%%", (int)lroundf(g_set.popOut * 100));
    else if (l == "Resolution") sprintf_s(out, n, "%d%%", (int)lroundf(g_set.resolution * 100));
    else if (l == "Sharpen") sprintf_s(out, n, "%d%%", (int)lroundf(g_set.sharpen * 100));
    else if (l == "Picture") {
        if (g_set.picture == 0 && wxr::LayerInUse() >= 0) sprintf_s(out, n, "Auto (%s)", wxr::LayerInUse() == 1 ? "Steady" : "Floating Panel");
        else strcpy_s(out, n, PictureName(g_set.picture));
    }
    else if (l == "Surround") strcpy_s(out, n, SurroundName(g_set.surround));
    else if (l == "Frame catch-up") strcpy_s(out, n, g_set.catchUp ? "On" : "Off");
    else if (l == "Screen shake") strcpy_s(out, n, g_set.screenShake ? "On" : "Off");
    else if (l == "Sounds") strcpy_s(out, n, snd[g_set.sounds < 0 ? 0 : g_set.sounds > 3 ? 3 : g_set.sounds]);
    else if (l == "Menu size") sprintf_s(out, n, "%d%%", (int)lroundf(g_set.menuSize * 100));
    else if (l == "Menu height") sprintf_s(out, n, "%+.2f m", g_set.menuHeight);
    else if (l == "Menu distance") sprintf_s(out, n, "%.2f m", g_set.menuDistance);
    else out[0] = 0;
}

static void Tip(const std::string& l, char* out, size_t n) {
    if (l == "Stage distance") strcpy_s(out, n, "How far away the stage floats. Closer fills more of your view.");
    else if (l == "Stage size") strcpy_s(out, n, "How wide the stage is, in metres. Bigger fills more of your view.");
    else if (l == "Depth") strcpy_s(out, n, "How strong the 3D is. Higher makes the world feel smaller and deeper. If far backgrounds strain your eyes, lower it or raise Pop-out.");
    else if (l == "Pop-out") strcpy_s(out, n, "Brings more of the scene out toward you. It also eases far backgrounds.");
    else if (l == "Recenter") strcpy_s(out, n, "Puts the stage straight in front of where you are looking now.");
    else if (l == "Screen shake") strcpy_s(out, n, "The game shakes the camera for bosses, thunder and big hits. In VR that shakes the whole stage, so it is Off by default. On brings the game's own shake back.");
    else if (l == "Resolution") {
        int nw = NextEyeWidth();
        sprintf_s(out, n, "Sharpness of the game picture. 100%% matches your headset's pixel density across the stage. Running now: %dx%d. Next launch: %dx%d (takes effect when you start the game again).",
                  g_eyeW, g_eyeH, nw, ((int)(nw * 9 / 16.f) + 1) & ~1);
    }
    else if (l == "Sharpen") strcpy_s(out, n, "Crisper edges. Too high looks harsh. 0% turns it off.");
    else if (l == "Picture") sprintf_s(out, n, "Auto picks for your headset software: Steady on Virtual Desktop, Floating Panel on everything else. Steady draws the stage into each eye's view and stays still. Floating Panel can look a little sharper, but Virtual Desktop squashes and shears it when you turn your head.%s%s%s",
                  wxr::RuntimeName()[0] ? " Detected: " : "", wxr::RuntimeName(), wxr::RuntimeName()[0] ? "." : "");
    else if (l == "Surround") strcpy_s(out, n, wxr::GetState() == wxr::Running && !wxr::CanSeeThrough()
        ? "What you see around the stage. Black is the original look. Green is for Virtual Desktop's chroma-key passthrough. Transparent is not offered by your headset software, so it shows black."
        : "What you see around the stage. Black is the original look. Green is for Virtual Desktop's chroma-key passthrough. Transparent asks your headset software to show your room.");
    else if (l == "Frame catch-up") strcpy_s(out, n, "On: when your PC misses a headset frame, the game makes up the lost time on the next one, so it never runs in slow motion. Off: the game slows down a little whenever frames are missed, as the original game does. Leave it On unless motion looks jumpy.");
    else if (l == "Sounds") strcpy_s(out, n, "A soft tone when you move, change a setting, switch tabs, open or close this menu. Off, Low, Medium or High.");
    else if (l == "Menu size") strcpy_s(out, n, "Makes this menu bigger or smaller. The stage does not change.");
    else if (l == "Menu height") strcpy_s(out, n, "Moves this menu up or down from eye height. 0 is eye height.");
    else if (l == "Menu distance") strcpy_s(out, n, "How far this menu floats in front of you. The stage does not move.");
    else if (l == "Reset to defaults") strcpy_s(out, n, "Puts every setting on all three tabs back to how it started.");
    else if (l == "Close") strcpy_s(out, n, "Closes this menu and goes back to the game. Clicking both sticks or pressing Insert also opens and closes it.");
    else out[0] = 0;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool SetF(float& f, float v, float lo, float hi) { v = Clamp(roundf(v * 100) / 100, lo, hi); if (fabsf(v - f) < 0.0001f) return false; f = v; return true; }
static void LogSettings(const char* why) {
    Log("MENU %s: stage %.2f m wide at %.2f m, depth %.2f, pop-out %.2f, resolution %.2f, sharpen %.2f, picture %s, surround %s, catch-up %d, sounds %d, menu %.2f / %+.2f m / %.2f m",
        why, g_set.stageWidth, g_set.stageDistance, g_set.depth, g_set.popOut, g_set.resolution, g_set.sharpen, PictureName(g_set.picture),
        SurroundName(g_set.surround), g_set.catchUp, g_set.sounds, g_set.menuSize, g_set.menuHeight, g_set.menuDistance);
}
static void Push() {
    wxr::SetStage({g_set.stageWidth, g_set.stageDistance}); wxr::SetPicture(g_set.picture); wxr::SetSurround(g_set.surround); wxr::SetSharpen(g_set.sharpen);
}
static bool Change(const std::string& l, int d) {
    bool c = false;
    if (l == "Stage distance") { c = SetF(g_set.stageDistance, g_set.stageDistance + d * 0.05f, 0.5f, 10.f); if (c) wxr::SetStage({g_set.stageWidth, g_set.stageDistance}); }
    else if (l == "Stage size") { c = SetF(g_set.stageWidth, g_set.stageWidth + d * 0.05f, 0.5f, 10.f); if (c) wxr::SetStage({g_set.stageWidth, g_set.stageDistance}); }
    else if (l == "Depth") c = SetF(g_set.depth, g_set.depth + d * 0.05f, 0.f, 3.f);
    else if (l == "Pop-out") c = SetF(g_set.popOut, g_set.popOut + d * 0.05f, 0.f, 1.f);
    else if (l == "Resolution") c = SetF(g_set.resolution, g_set.resolution + d * 0.1f, 0.5f, 1.5f);
    else if (l == "Sharpen") { c = SetF(g_set.sharpen, g_set.sharpen + d * 0.1f, 0.f, 1.f); wxr::SetSharpen(g_set.sharpen); }
    else if (l == "Picture") { g_set.picture = (g_set.picture + 3 + d) % 3; wxr::SetPicture(g_set.picture); c = true; }
    else if (l == "Surround") { g_set.surround = (g_set.surround + 3 + d) % 3; wxr::SetSurround(g_set.surround); c = true; }
    else if (l == "Frame catch-up") { g_set.catchUp = !g_set.catchUp; c = true; }
    else if (l == "Screen shake") { g_set.screenShake = !g_set.screenShake; c = true; }
    else if (l == "Sounds") { int v = g_set.sounds + d; if (v >= 0 && v <= 3 && v != g_set.sounds) { g_set.sounds = v; c = true; } }
    else if (l == "Menu size") c = SetF(g_set.menuSize, g_set.menuSize + d * 0.05f, 0.6f, 1.4f);
    else if (l == "Menu height") c = SetF(g_set.menuHeight, g_set.menuHeight + d * 0.05f, -0.6f, 0.6f);
    else if (l == "Menu distance") c = SetF(g_set.menuDistance, g_set.menuDistance + d * 0.05f, 0.3f, 2.f);
    if (c) { SaveSettings(); LogSettings(l.c_str()); }
    return c;
}
static void SetOpen(bool open, const char* how);
static void Press(const std::string& l) {
    if (l == "Recenter") { wxr::Recenter(); Log("MENU recenter"); }
    else if (l == "Reset to defaults") {
        Settings keep = g_set;
        g_set = kDefaults;
        g_set.tanSpan = keep.tanSpan; g_set.gameSettingsDone = keep.gameSettingsDone; g_set.thunderShake = keep.thunderShake;
        g_set.menuDistance = 0.8f;
        SaveSettings(); Push(); LogSettings("reset to defaults");
    }
    else if (l == "Close") { SetOpen(false, "Close row"); }
}

static void Snapshot() {
    wxr::MenuSnapshot s;
    s.visible = g_open;
    s.problem = wxr::GetState() == wxr::Failed;
    s.tabCount = (int)g_tabs.size(); s.tab = g_tab;
    for (int i = 0; i < s.tabCount; i++) strcpy_s(s.tabs[i], g_tabs[i].name);
    const Tab& t = g_tabs[g_tab];
    s.rowCount = (int)t.rows.size(); s.row = g_row;
    for (int i = 0; i < s.rowCount; i++) {
        strcpy_s(s.rows[i].label, t.rows[i].label);
        ValueText(t.rows[i].label, s.rows[i].value, sizeof s.rows[i].value);
        s.rows[i].button = t.rows[i].kind == Button;
    }
    if (s.problem) sprintf_s(s.explain, "OpenXR error %d. Check that your headset software (SteamVR, Virtual Desktop) is running and set as the OpenXR runtime, then start the game again. The log OriWotWVR.log in the game folder has the details.", wxr::LastError());
    else Tip(t.rows[g_row].label, s.explain, sizeof s.explain);
    strcpy_s(s.hint, g_lastKeyboard ? "Page Up Down: tabs     Arrow keys: choose and change     Enter: select     Insert or Esc: close"
                                    : "LB RB: tabs     Up Down: choose     Left Right: change     A: select     B: close");
    wxr::Views v; wxr::GetViews(&v);
    sprintf_s(s.readout, "Headset %.0f Hz  \xC2\xB7  eye %d x %d  \xC2\xB7  headset asks %d wide  \xC2\xB7  %s",
              v.periodSec > 0 ? 1.0 / v.periodSec : 0.0, g_eyeW, g_eyeH, wxr::RecommendedWidth(), wxr::RuntimeName()[0] ? wxr::RuntimeName() : "no OpenXR");
    sprintf_s(s.version, "Ori & the Will of the Wisps VR %s", MOD_VERSION);
    s.size = g_set.menuSize; s.height = g_set.menuHeight; s.distance = g_set.menuDistance;
    wxr::SetMenu(s);
}

// Repeat for held directions: first step at once, then after 0.4 s every 0.12 s.
struct Repeat { double since = -1, last = 0; bool Step(bool held, double now) {
    if (!held) { since = -1; return false; }
    if (since < 0) { since = now; last = now; return true; }
    if (now - since > 0.4 && now - last > 0.12) { last = now; return true; }
    return false; } };
static Repeat g_rUp, g_rDown, g_rLeft, g_rRight;
static bool g_prevA, g_prevB, g_prevLB, g_prevRB, g_prevIns, g_prevEnter, g_prevEsc, g_prevPgUp, g_prevPgDn;

static bool OurWindowFocused() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// The game pauses while the menu is open, as in Ori VR: time scale 0, put back on close.
static float g_savedScale = -1;
static void PauseGame(bool pause) {
    if (!mSetTimeScale) return;
    if (pause) { g_savedScale = SF(mTimeScale); P<void (*)(float, MethodInfo*)>(mSetTimeScale)(0.0f, mSetTimeScale); }
    else if (g_savedScale >= 0) { P<void (*)(float, MethodInfo*)>(mSetTimeScale)(g_savedScale, mSetTimeScale); g_savedScale = -1; }
}
static void SetOpen(bool open, const char* how) {
    if (open == g_open) return;
    g_open = open;
    PauseGame(open);
    tones::Play(open ? tones::Open : tones::Close);
    Log("MENU %s (%s)", open ? "opened" : "closed", how);
}

static void Update(double now) {
    if (g_tabs.empty()) { Build(); if (g_set.menuDistance <= 0) g_set.menuDistance = 0.8f; }
    bool kb = OurWindowFocused();
    uint16_t b = (now - g_padSeen < 0.5) ? g_padButtons : 0;
    int16_t ly = (now - g_padSeen < 0.5) ? g_padLY : 0, lx = (now - g_padSeen < 0.5) ? g_padLX : 0;
    bool ins = kb && Key(VK_INSERT);
    // Open or close: both sticks held 0.5 s, or Insert (one press, one toggle).
    bool both = (b & PadL3) && (b & PadR3);
    if (both) { if (g_bothSticksSince < 0) g_bothSticksSince = now; if (!g_comboUsed && now - g_bothSticksSince > 0.5) { g_comboUsed = true; g_lastKeyboard = false; SetOpen(!g_open, "both sticks"); } }
    else { g_bothSticksSince = -1; g_comboUsed = false; }
    if (ins && !g_prevIns) { g_lastKeyboard = true; SetOpen(!g_open, "Insert"); }
    g_prevIns = ins;
    bool keys = kb && (Key(VK_UP) || Key(VK_DOWN) || Key(VK_LEFT) || Key(VK_RIGHT) || Key(VK_RETURN) || Key(VK_ESCAPE) || Key(VK_BACK) || Key(VK_PRIOR) || Key(VK_NEXT) || ins);
    bool anyHeld = b != 0 || abs(ly) > 8000 || abs(lx) > 8000 || keys;
    // After closing, the game gets input back only once every key and button has been up for 0.15 s, so the release
    // itself (Unity's GetKeyUp, for example Esc opening the game's pause menu) never reaches the game.
    static double releasedSince = -1;
    if (g_open) { g_block = true; releasedSince = -1; }
    else if (g_block) {
        if (anyHeld) releasedSince = -1;
        else if (releasedSince < 0) releasedSince = now;
        else if (now - releasedSince > 0.15) { g_block = false; Log("MENU input back to the game"); }
    }
    if (g_open) {
        if (keys) g_lastKeyboard = true; else if (b || abs(ly) > 8000 || abs(lx) > 8000) g_lastKeyboard = false;
        const Tab& t = g_tabs[g_tab];
        bool up = (b & PadUp) || ly > 16000 || (kb && Key(VK_UP));
        bool down = (b & PadDown) || ly < -16000 || (kb && Key(VK_DOWN));
        bool left = (b & PadLeft) || lx < -16000 || (kb && Key(VK_LEFT));
        bool right = (b & PadRight) || lx > 16000 || (kb && Key(VK_RIGHT));
        bool a = (b & PadA) || (kb && Key(VK_RETURN));
        bool back = (b & PadB) || (kb && (Key(VK_ESCAPE) || Key(VK_BACK)));
        bool lb = (b & PadLB) || (kb && Key(VK_PRIOR)), rb = (b & PadRB) || (kb && Key(VK_NEXT));
        int rows = (int)t.rows.size();
        if (g_rUp.Step(up, now)) { g_row = (g_row + rows - 1) % rows; tones::Play(tones::Move); }
        if (g_rDown.Step(down, now)) { g_row = (g_row + 1) % rows; tones::Play(tones::Move); }
        const Row& r = t.rows[g_row];
        if (r.kind == Value) {
            if (g_rLeft.Step(left, now)) tones::Play(Change(r.label, -1) ? tones::Change : tones::Refused);
            if (g_rRight.Step(right, now)) tones::Play(Change(r.label, +1) ? tones::Change : tones::Refused);
        } else { g_rLeft.Step(false, now); g_rRight.Step(false, now); }
        if (a && !g_prevA && r.kind == Button) { tones::Play(tones::Change); Press(r.label); if (!g_open) tones::Play(tones::Close); }
        if (back && !g_prevB) SetOpen(false, "back");
        if ((lb && !g_prevLB) || (rb && !g_prevRB)) {
            g_rowPerTab[g_tab] = g_row;
            g_tab = (g_tab + (int)g_tabs.size() + (rb && !g_prevRB ? 1 : -1)) % (int)g_tabs.size();
            g_row = g_rowPerTab[g_tab];
            tones::Play(tones::TabT);
        }
        g_prevA = a; g_prevB = back; g_prevLB = lb; g_prevRB = rb;
    } else {
        g_prevA = (b & PadA) || (kb && Key(VK_RETURN)); g_prevB = (b & PadB) || (kb && (Key(VK_ESCAPE) || Key(VK_BACK)));
        g_prevLB = (b & PadLB) || (kb && Key(VK_PRIOR)); g_prevRB = (b & PadRB) || (kb && Key(VK_NEXT));
    }
    Snapshot();
}

}  // namespace menu

// ---------------------------------------------------------------- input hooks: the game gets nothing while the menu blocks
struct XInputStateGame { int PacketNumber; uint16_t wButtons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };
typedef int (*XInputGetState_t)(int, XInputStateGame*, MethodInfo*);
static XInputGetState_t oXInputGetState;
static uint32_t g_xinputCalls = 0;
static int hkXInputGetState(int idx, XInputStateGame* st, MethodInfo* m) {
    int r = oXInputGetState(idx, st, m);
    g_xinputCalls++;
    if (r == 0 && st) {
        menu::g_padButtons = st->wButtons; menu::g_padLX = st->lx; menu::g_padLY = st->ly; menu::g_padSeen = Now();
        if (menu::g_block) { int pn = st->PacketNumber; memset(st, 0, sizeof *st); st->PacketNumber = pn; }
    }
    return r;
}
// The game's pad reads did not go through J2i's XInputGetState on RigB (0 calls, 2026-11-20), so the exported
// XInputGetState of every loaded XInput DLL (xinput1_3, xinput1_4, xinput9_1_0 are all loaded) is hooked as well.
struct XInputStateNative { DWORD dwPacketNumber; uint16_t wButtons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };
typedef DWORD(WINAPI* XGetState_t)(DWORD, XInputStateNative*);
static XGetState_t oXGet[4];
static uint32_t g_xinputNativeCalls = 0;
static DWORD FilterNative(DWORD r, XInputStateNative* st) {
    g_xinputNativeCalls++;
    if (r == 0 && st) {
        if (st->wButtons || st->lx || st->ly || Now() - menu::g_padSeen > 0.05) {
            menu::g_padButtons = st->wButtons; menu::g_padLX = st->lx; menu::g_padLY = st->ly; menu::g_padSeen = Now();
        }
        if (menu::g_block) { DWORD pn = st->dwPacketNumber; memset(st, 0, sizeof *st); st->dwPacketNumber = pn; }
    }
    return r;
}
static DWORD WINAPI hkXGet0(DWORD i, XInputStateNative* s) { return FilterNative(oXGet[0](i, s), s); }
static DWORD WINAPI hkXGet1(DWORD i, XInputStateNative* s) { return FilterNative(oXGet[1](i, s), s); }
static DWORD WINAPI hkXGet2(DWORD i, XInputStateNative* s) { return FilterNative(oXGet[2](i, s), s); }
static DWORD WINAPI hkXGet3(DWORD i, XInputStateNative* s) { return FilterNative(oXGet[3](i, s), s); }
static bool g_xHooked[4];
static void HookXInputDlls() {
    static const wchar_t* dlls[4] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll"};
    static void* det[4] = {(void*)&hkXGet0, (void*)&hkXGet1, (void*)&hkXGet2, (void*)&hkXGet3};
    for (int i = 0; i < 4; i++) {
        if (g_xHooked[i]) continue;
        HMODULE h = GetModuleHandleW(dlls[i]);
        if (!h) continue;
        void* f = (void*)GetProcAddress(h, "XInputGetState");
        g_xHooked[i] = true;
        if (!f) continue;
        MH_STATUS a = MH_CreateHook(f, det[i], (void**)&oXGet[i]), b = MH_EnableHook(f);
        Log("HOOK %ls XInputGetState %s", dlls[i], (a == MH_OK && b == MH_OK) ? "in place" : "FAILED");
    }
}
// IL2CPP calls Unity's input icalls directly from each caller, so hooking the managed wrappers (below) misses most
// reads: the game's Esc still opened its pause menu after the mod menu closed (RigB, 2026-11-20). The icalls themselves
// (UnityPlayer functions found with il2cpp_resolve_icall) are hooked too.
typedef bool (*IcallKey_t)(int);
typedef bool (*IcallKeyStr_t)(void*);
typedef bool (*IcallAny_t)();
static IcallKey_t oIK[3]; static IcallKeyStr_t oIKS[3]; static IcallAny_t oIA[2];
static uint32_t g_keyBlocked = 0;
static bool hkIK0(int k) { if (menu::g_block) { g_keyBlocked++; return false; } return oIK[0](k); }
static bool hkIK1(int k) { if (menu::g_block) { g_keyBlocked++; return false; } return oIK[1](k); }
static bool hkIK2(int k) { if (menu::g_block) { g_keyBlocked++; return false; } return oIK[2](k); }
static bool hkIKS0(void* s) { return menu::g_block ? false : oIKS[0](s); }
static bool hkIKS1(void* s) { return menu::g_block ? false : oIKS[1](s); }
static bool hkIKS2(void* s) { return menu::g_block ? false : oIKS[2](s); }
static bool hkIA0() { return menu::g_block ? false : oIA[0](); }
static bool hkIA1() { return menu::g_block ? false : oIA[1](); }
static void HookInputIcalls() {
    struct { const char* name; void* det; void** orig; } list[] = {
        {"UnityEngine.Input::GetKeyInt", (void*)&hkIK0, (void**)&oIK[0]}, {"UnityEngine.Input::GetKeyDownInt", (void*)&hkIK1, (void**)&oIK[1]},
        {"UnityEngine.Input::GetKeyUpInt", (void*)&hkIK2, (void**)&oIK[2]}, {"UnityEngine.Input::GetKeyString", (void*)&hkIKS0, (void**)&oIKS[0]},
        {"UnityEngine.Input::GetKeyDownString", (void*)&hkIKS1, (void**)&oIKS[1]}, {"UnityEngine.Input::GetKeyUpString", (void*)&hkIKS2, (void**)&oIKS[2]},
        {"UnityEngine.Input::get_anyKey", (void*)&hkIA0, (void**)&oIA[0]}, {"UnityEngine.Input::get_anyKeyDown", (void*)&hkIA1, (void**)&oIA[1]},
    };
    for (auto& e : list) {
        void* f = il2cpp_resolve_icall(e.name);
        MH_STATUS a = f ? MH_CreateHook(f, e.det, e.orig) : MH_ERROR_NOT_EXECUTABLE, b = f ? MH_EnableHook(f) : a;
        Log("HOOK icall %s %s", e.name, (f && a == MH_OK && b == MH_OK) ? "in place" : "FAILED");
    }
}
typedef bool (*GetKey_t)(int, MethodInfo*);
static GetKey_t oGetKeyInt, oGetKeyDownInt, oGetKeyUpInt;
static bool hkGetKeyInt(int k, MethodInfo* m) { return menu::g_block ? false : oGetKeyInt(k, m); }
static bool hkGetKeyDownInt(int k, MethodInfo* m) { return menu::g_block ? false : oGetKeyDownInt(k, m); }
static bool hkGetKeyUpInt(int k, MethodInfo* m) { return menu::g_block ? false : oGetKeyUpInt(k, m); }
typedef bool (*AnyKey_t)(MethodInfo*);
static AnyKey_t oAnyKey, oAnyKeyDown;
static bool hkAnyKey(MethodInfo* m) { return menu::g_block ? false : oAnyKey(m); }
static bool hkAnyKeyDown(MethodInfo* m) { return menu::g_block ? false : oAnyKeyDown(m); }
