// OpenXR side of Ori and the Will of the Wisps VR (64-bit, D3D11).
// The game never waits for the headset: OpenXR runs on its own thread with its own D3D11 device. The game's render
// thread copies each finished eye pair into a ring of shared textures (keyed mutex, never waited on: a busy slot is
// skipped and counted), and the XR thread draws the newest pair onto the stage each headset frame.
#pragma once
#include <cstdint>

namespace wxr {

enum State { Off = 0, Starting = 1, Running = 2, Failed = 3, Stopped = 4 };

struct Views {
    bool valid = false;        // eye poses usable
    bool focused = false;      // session state FOCUSED
    int sessionState = 0;      // XrSessionState
    double periodSec = 0;      // the runtime's display period
    float local[2][3] = {};    // each eye relative to the recenter point, in the recenter frame (metres; x right, y up, z back)
    float pose[2][7] = {};     // each eye in the room: position xyz, orientation xyzw (as located)
    float fov[2][4] = {};      // angles left, right, up, down (radians)
    int64_t displayTime = 0;
    uint32_t recenters = 0;    // counts recenters, so the game side can tell a new one happened
};

struct Stage { float width = 1.85f, distance = 1.0f; };

void SetLogger(void (*log)(const char* fmt, ...));
// Eye image size is worked out on the XR thread once the headset's recommended size is known:
// width = recommended width * (stage width / stage distance) / tanSpan * resolution, height = width / aspect.
// tanSpanGuess is the headset's eye view width at 1 m from the last run (2.2 if unknown).
void Configure(Stage stage, int picture /*0 Auto, 1 Steady (projection), 2 Panel (quad)*/, float resolution, float tanSpanGuess, float aspect);
int EyeWidth();                     // 0 until the ring is ready
int EyeHeight();
void SetStage(Stage stage);         // live
void Start();                       // starts the XR thread once
int GetState();
const char* RuntimeName();
const char* HeadsetName();
int RecommendedWidth();
int RecommendedHeight();
float EyeTanSpan();                 // tan(right) - tan(left) of the left eye, 0 before the first located frame
int LayerInUse();                   // 1 projection, 0 quad, -1 none yet
int LastError();
bool GetViews(Views* out);
void Recenter();
void SetPicture(int picture);   // 0 Auto, 1 Steady, 2 Floating Panel; live
void SetSurround(int surround); // 0 Black, 1 Green, 2 Transparent; live
void SetSharpen(float amount);  // 0 off; live
bool CanSeeThrough();
// Game main thread: hand over this frame's eye textures (Unity native pointers) and the views they were rendered with,
// then call GL.IssuePluginEvent(RenderEventFunc(), 1). The copy happens on the game's render thread.
void QueueFrame(void* texL, void* texR, const Views& used, uint32_t frameNo);
void* RenderEventFunc();
void Shutdown();

struct Stats {
    uint32_t queued, copied, skippedBusy, skippedNoSlots, xrFrames, xrNoNewImage, layersSent;
    double waitMsAvg, waitMsMax, endMsAvg, endMsMax, copyMsMax;
};
void TakeStats(Stats* out);         // returns counters since the last call and resets them

}  // namespace wxr

// ------------------------------------------------------------------------------------------------ mod menu panel
// The menu's content is built on the game's main thread (native\Menu.cpp) and handed over as a snapshot; the XR thread
// draws it with Dear ImGui into its own texture and shows it in front of the stage.
namespace wxr {
struct MenuRow { char label[48]; char value[48]; bool button; };
struct MenuSnapshot {
    bool visible = false;
    bool problem = false;       // title reads VR DID NOT START
    int tabCount = 0, tab = 0;
    char tabs[6][24] = {};
    int rowCount = 0, row = 0;
    MenuRow rows[8] = {};
    char explain[400] = "";
    char hint[160] = "";
    char readout[200] = "";
    char version[48] = "";
    float size = 1.0f, height = 0.0f, distance = 0.8f;   // placement: scale, metres up from eye height, metres away
};
void SetMenu(const MenuSnapshot& m);
}  // namespace wxr
